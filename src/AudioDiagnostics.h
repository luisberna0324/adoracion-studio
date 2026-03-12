#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <fstream>

/**
 * AudioDiagnostics — Lock-free audio thread performance logger.
 *
 * The audio thread only calls pushEvent(), which writes to a ring buffer
 * using atomic indices — zero locks, zero allocations, zero syscalls.
 *
 * A background JUCE thread drains the ring buffer every 500ms and appends
 * rows to a rotating CSV file under ~/Library/Logs/MTControls/.
 *
 * CSV columns:
 *   wall_ms, block_ms, budget_ms, overrun, lock_wait_ms, stem_count, is_playing
 */
class AudioDiagnostics {
public:
  struct DiagEvent {
    double wallMs = 0.0;      // absolute wall-clock timestamp (ms since epoch)
    double blockMs = 0.0;     // time processBlock actually took
    double budgetMs = 0.0;    // time budget = (samples / sampleRate) * 1000
    double lockWaitMs = 0.0;  // time spent waiting to acquire stemListLock
    int stemCount = 0;
    bool overrun = false;     // blockMs > budgetMs
    bool isPlaying = false;
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

  // ---------------------------------------------------------------------------
  // Called from the UI / message thread
  // ---------------------------------------------------------------------------
  void startBackgroundLogger(const juce::File &logDirectory) {
    stopBackgroundLogger();

    logDirectory.createDirectory();
    const auto ts = juce::Time::getCurrentTime()
                        .formatted("%Y%m%d_%H%M%S");
    logFile = logDirectory.getChildFile("audio_" + ts + ".csv");

    // Write CSV header
    if (auto stream = logFile.createOutputStream()) {
      stream->writeText(
          "wall_ms,block_ms,budget_ms,overrun,lock_wait_ms,stem_count,is_playing\n",
          false, false, nullptr);
    }

    active.store(true, std::memory_order_relaxed);
    flushThread = std::make_unique<FlushThread>(*this);
    flushThread->startThread(juce::Thread::Priority::low);
  }

  void stopBackgroundLogger() {
    active.store(false, std::memory_order_relaxed);
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

  std::array<DiagEvent, kSize> events{};
  std::atomic<int> head{0};
  std::atomic<int> tail{0};
  std::atomic<bool> active{false};

  // Cumulative stats (updated by flush thread only)
  std::atomic<int> overrunCount{0};
  std::atomic<int> totalCount{0};
  std::atomic<double> maxBlockMs{0.0};
  std::atomic<double> totalBlockMs{0.0};

  juce::File logFile;

  void flushPending() {
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
          << (ev.isPlaying ? "1" : "0") << "\n";

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
