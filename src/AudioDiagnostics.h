#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>

/**
 * AudioDiagnostics — Lock-free audio thread performance logger.
 *
 * The audio thread only reads a pre-captured wall-clock calibration and writes
 * to ring buffers using atomic indices — zero locks, allocations or wall-clock
 * syscalls.
 *
 * A background JUCE thread drains the ring buffer every 500ms and appends
 * rows to a rotating CSV file under ~/Library/Logs/MTControls/.
 *
 * CSV files:
 *   1) audio_<ts>.csv       : block-level performance
 *   2) audio_stems_<ts>.csv : per-stem timing snapshots (latency + delay)
 */
class AudioDiagnostics {
public:
  struct DiagEvent {
    double wallMs = 0.0;      // absolute wall-clock timestamp (ms since epoch)
    double blockMs = 0.0;     // time processBlock actually took
    double budgetMs = 0.0;    // time budget = (samples / sampleRate) * 1000
    double lockWaitMs = 0.0;  // time spent waiting to acquire stemListLock
    int stemCount = 0;
    int64_t streamingMisses = 0;
    bool overrun = false;     // blockMs > budgetMs
    bool isPlaying = false;
    int64_t streamingMissingFrames = 0;
    int64_t timeProcessorResetMisses = 0;
    int64_t timeProcessorServiceFailures = 0;
  };

  struct StemTimingEvent {
    static constexpr int kMaxStemMetrics = 64;

    double wallMs = 0.0;
    double tempoRatio = 1.0;
    int pitchSemiTones = 0;
    int stemCount = 0;
    int targetLatencySamples = 0;
    bool routingActive = false;
    bool timeProcessActive = false;
    bool tempoActive = false;
    std::array<int16_t, kMaxStemMetrics> processLatencySamples{};
    std::array<int16_t, kMaxStemMetrics> delaySamples{};
    // Bit flags per stem:
    //  bit0 = processed
    //  bit1 = pitchShiftable
    //  bit2 = percussive content profile
    std::array<uint8_t, kMaxStemMetrics> flags{};
  };

  AudioDiagnostics() = default;
  ~AudioDiagnostics() { stopBackgroundLogger(); }

  // ---------------------------------------------------------------------------
  // Called from the audio thread — MUST be real-time safe
  // ---------------------------------------------------------------------------
  void pushEvent(DiagEvent ev) noexcept {
    if (!active.load(std::memory_order_relaxed))
      return;

    const int writeIdx = head.load(std::memory_order_relaxed);
    const int nextIdx = (writeIdx + 1) & kMask;

    if (nextIdx == tail.load(std::memory_order_acquire))
      return; // Ring buffer full — drop event rather than block

    events[static_cast<size_t>(writeIdx)] = ev;
    head.store(nextIdx, std::memory_order_release);
  }

  void pushStemTimingEvent(const StemTimingEvent &ev) noexcept {
    if (!active.load(std::memory_order_relaxed))
      return;

    const int writeIdx = stemHead.load(std::memory_order_relaxed);
    const int nextIdx = (writeIdx + 1) & kStemMask;

    if (nextIdx == stemTail.load(std::memory_order_acquire))
      return; // Ring buffer full — drop event rather than block.

    stemEvents[static_cast<size_t>(writeIdx)] = ev;
    stemHead.store(nextIdx, std::memory_order_release);
  }

  bool isActive() const noexcept {
    return active.load(std::memory_order_acquire);
  }

  double wallTimeForHighResolutionTick(const int64_t tick) const noexcept {
    const auto ticksPerSecond =
        wallClockTicksPerSecond.load(std::memory_order_relaxed);
    if (ticksPerSecond <= 0.0)
      return wallClockBaseMs.load(std::memory_order_relaxed);
    const auto baseTick =
        wallClockBaseTick.load(std::memory_order_relaxed);
    return wallClockBaseMs.load(std::memory_order_relaxed) +
           (static_cast<double>(tick - baseTick) * 1000.0 /
            ticksPerSecond);
  }

  // ---------------------------------------------------------------------------
  // Called from the UI / message thread
  // ---------------------------------------------------------------------------
  void startBackgroundLogger(const juce::File &logDirectory) {
    stopBackgroundLogger();

    logDirectory.createDirectory();
    const auto ts = juce::Time::getCurrentTime()
                        .formatted("%Y%m%d_%H%M%S");
    logFile = logDirectory.getChildFile("audio_" + ts + ".csv");
    stemLogFile = logDirectory.getChildFile("audio_stems_" + ts + ".csv");

    // Write CSV header
    if (auto stream = logFile.createOutputStream()) {
      stream->writeText(
          "wall_ms,block_ms,budget_ms,overrun,lock_wait_ms,stem_count,streaming_misses,is_playing,streaming_missing_frames,time_processor_reset_misses,time_processor_service_failures\n",
          false, false, nullptr);
    }
    if (auto stream = stemLogFile.createOutputStream()) {
      stream->writeText(
          "wall_ms,tempo_ratio,pitch_semitones,routing_active,time_process_active,tempo_active,stem_count,target_latency_samples,stem_index,processed,pitch_shiftable,content_type,proc_latency_samples,delay_samples\n",
          false, false, nullptr);
    }

    // Capture the expensive wall clock once on the message thread. Audio
    // events derive epoch timestamps from the monotonic high-resolution tick.
    wallClockBaseMs.store(
        static_cast<double>(juce::Time::currentTimeMillis()),
        std::memory_order_relaxed);
    wallClockBaseTick.store(juce::Time::getHighResolutionTicks(),
                            std::memory_order_relaxed);
    wallClockTicksPerSecond.store(
        static_cast<double>(juce::Time::getHighResolutionTicksPerSecond()),
        std::memory_order_relaxed);
    active.store(true, std::memory_order_release);
    flushThread = std::make_unique<FlushThread>(*this);
    flushThread->startThread(juce::Thread::Priority::low);
  }

  void stopBackgroundLogger() {
    active.store(false, std::memory_order_release);
    if (flushThread) {
      flushThread->stopThread(2000);
      flushThread.reset();
    }
    flushPending(); // Final drain
  }

  /** Returns human-readable stats from the last flush cycle. */
  juce::String getStats() const {
    const int over = overrunCount.load(std::memory_order_relaxed);
    const int total = totalCount.load(std::memory_order_relaxed);
    const double maxMs = maxBlockMs.load(std::memory_order_relaxed);
    const double avgMs =
        total > 0 ? totalBlockMs.load(std::memory_order_relaxed) / total : 0.0;
    const double pct = total > 0 ? (over * 100.0 / total) : 0.0;

    return juce::String::formatted(
        "Blocks: %d | Overruns: %d (%.1f%%) | Avg: %.2fms | Max: %.2fms",
        total, over, pct, avgMs, maxMs);
  }

private:
  static constexpr int kSize = 512;
  static constexpr int kMask = kSize - 1;
  static constexpr int kStemSize = 128;
  static constexpr int kStemMask = kStemSize - 1;

  std::array<DiagEvent, kSize> events{};
  std::atomic<int> head{0};
  std::atomic<int> tail{0};
  std::array<StemTimingEvent, kStemSize> stemEvents{};
  std::atomic<int> stemHead{0};
  std::atomic<int> stemTail{0};
  std::atomic<bool> active{false};
  std::atomic<double> wallClockBaseMs{0.0};
  std::atomic<int64_t> wallClockBaseTick{0};
  std::atomic<double> wallClockTicksPerSecond{0.0};

  // Cumulative stats (updated by flush thread only)
  std::atomic<int> overrunCount{0};
  std::atomic<int> totalCount{0};
  std::atomic<double> maxBlockMs{0.0};
  std::atomic<double> totalBlockMs{0.0};

  juce::File logFile;
  juce::File stemLogFile;

  void flushPending() {
    flushBlockEvents();
    flushStemTimingEvents();
  }

  void flushBlockEvents() {
    const int tailIdx = tail.load(std::memory_order_relaxed);
    const int headIdx = head.load(std::memory_order_acquire);

    if (tailIdx == headIdx)
      return;

    std::ofstream out(logFile.getFullPathName().toStdString(),
                      std::ios::app | std::ios::binary);
    if (!out.is_open())
      return;

    int idx = tailIdx;
    while (idx != headIdx) {
      const auto &ev = events[static_cast<size_t>(idx)];

      out << static_cast<int64_t>(ev.wallMs) << ","
          << juce::String(ev.blockMs, 3).toStdString() << ","
          << juce::String(ev.budgetMs, 3).toStdString() << ","
          << (ev.overrun ? "1" : "0") << ","
          << juce::String(ev.lockWaitMs, 3).toStdString() << ","
          << ev.stemCount << ","
          << ev.streamingMisses << ","
          << (ev.isPlaying ? "1" : "0") << ","
          << ev.streamingMissingFrames << ","
          << ev.timeProcessorResetMisses << ","
          << ev.timeProcessorServiceFailures << "\n";

      // Update cumulative stats
      totalCount.fetch_add(1, std::memory_order_relaxed);
      if (ev.overrun)
        overrunCount.fetch_add(1, std::memory_order_relaxed);

      // Update maxBlockMs atomically (load-compare-store loop)
      double currentMax = maxBlockMs.load(std::memory_order_relaxed);
      while (ev.blockMs > currentMax &&
             !maxBlockMs.compare_exchange_weak(currentMax, ev.blockMs,
                                               std::memory_order_relaxed)) {}

      // Accumulate total for avg (approximate — relaxed ordering acceptable)
      double cur = totalBlockMs.load(std::memory_order_relaxed);
      totalBlockMs.store(cur + ev.blockMs, std::memory_order_relaxed);

      idx = (idx + 1) & kMask;
    }

    tail.store(headIdx, std::memory_order_release);
  }

  void flushStemTimingEvents() {
    const int tailIdx = stemTail.load(std::memory_order_relaxed);
    const int headIdx = stemHead.load(std::memory_order_acquire);

    if (tailIdx == headIdx)
      return;

    std::ofstream out(stemLogFile.getFullPathName().toStdString(),
                      std::ios::app | std::ios::binary);
    if (!out.is_open())
      return;

    int idx = tailIdx;
    while (idx != headIdx) {
      const auto &ev = stemEvents[static_cast<size_t>(idx)];
      const int clampedStemCount =
          juce::jlimit(0, StemTimingEvent::kMaxStemMetrics, ev.stemCount);
      for (int stemIndex = 0; stemIndex < clampedStemCount; ++stemIndex) {
        const uint8_t flags = ev.flags[static_cast<size_t>(stemIndex)];
        const int processed = (flags & 0x1u) ? 1 : 0;
        const int pitchShiftable = (flags & 0x2u) ? 1 : 0;
        const int contentType = (flags & 0x4u) ? 1 : 0; // 0=melodic, 1=percussive

        out << static_cast<int64_t>(ev.wallMs) << ","
            << juce::String(ev.tempoRatio, 6).toStdString() << ","
            << ev.pitchSemiTones << ","
            << (ev.routingActive ? "1" : "0") << ","
            << (ev.timeProcessActive ? "1" : "0") << ","
            << (ev.tempoActive ? "1" : "0") << ","
            << clampedStemCount << ","
            << ev.targetLatencySamples << ","
            << stemIndex << ","
            << processed << ","
            << pitchShiftable << ","
            << contentType << ","
            << ev.processLatencySamples[static_cast<size_t>(stemIndex)] << ","
            << ev.delaySamples[static_cast<size_t>(stemIndex)] << "\n";
      }

      idx = (idx + 1) & kStemMask;
    }

    stemTail.store(headIdx, std::memory_order_release);
  }

  class FlushThread : public juce::Thread {
  public:
    explicit FlushThread(AudioDiagnostics &owner)
        : juce::Thread("AudioDiagFlush"), diag(owner) {}

    void run() override {
      while (!threadShouldExit()) {
        wait(500); // flush every 500ms
        diag.flushPending();
      }
    }

  private:
    AudioDiagnostics &diag;
  };

  std::unique_ptr<FlushThread> flushThread;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioDiagnostics)
};
