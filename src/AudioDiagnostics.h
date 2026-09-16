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
 *   1) audio_<ts>.csv          : block-level performance (incl. per-stage
 *                                microsecond timings and gap events)
 *   2) audio_stems_<ts>.csv    : per-stem timing snapshots (latency + delay)
 *   3) audio_gaps_<ts>.csv     : underrun gap events (callback missed its slot)
 */
class AudioDiagnostics {
public:
  /**
   * Per-block event captured at the end of processBlock.
   *
   * Stage timings (stage*_us) record the wall-clock duration of each
   * sub-stage inside processBlock in microseconds. They are stored as int16_t
   * because the audio thread is real-time and we want zero allocation; a
   * single block budget (~11.6ms @ 44.1kHz / 512) is far below the int16
   * ceiling (~32.7ms unsigned, ~65.5ms with sign), but we still saturate on
   * extreme outliers so the writer never wraps.
   *
   * Convention for zero: a stage that didn't run in this block keeps the
   * default 0; do not interpret that as "ran in 0us".
   */
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

    // ---- per-stage timings (microseconds, saturated to int16 range) ------
    //
    // Convention: a stage that didn't run in this block keeps the default
    // 0; do not interpret that as "ran in 0us".
    //
    // stagePreReadUs:    from processBlock entry to the first stem read.
    //                    Includes the atomic state reads, section snapshot,
    //                    song transition gain math, tempo ratio updates,
    //                    seek coordination and per-stem state injection.
    //                    A growing preRead means the audio thread is being
    //                    slowed down BEFORE the heavy lifting — usually
    //                    cache-line contention with the UI thread.
    // stageReadNextUs:   the per-stem getNextAudioBlock loop (all stems).
    // stageDecodeRenderUs: the time spent inside pitch / time-stretch
    //                    processors (proc.process) plus the per-stem delay
    //                    lines. A subset of stageReadNextUs.
    // stageMixStemsUs:   mixEngine.mixStems or mixEngine.renderDirect
    //                    (separated vs direct render path).
    // stageMixOutputUs:  mixEngine.renderToOutput — the master bus where
    //                    stems are summed, fade gain is applied and
    //                    (optionally) metronome / pads / dynamic guide
    //                    are mixed in.
    // stageRampTransitionUs: the final applySongTransitionRamp (fade
    //                    envelope applied to the output buffer).
    int16_t stagePreReadUs = 0;        // entry → first stem read
    int16_t stageReadNextUs = 0;       // getNextAudioBlock across all stems
    int16_t stageDecodeRenderUs = 0;   // pitch/time-stretch processing
    int16_t stageMixStemsUs = 0;       // mixEngine.mixStems / renderDirect
    int16_t stageMixOutputUs = 0;      // mixEngine.renderToOutput (master bus)
    int16_t stageRampTransitionUs = 0; // applySongTransitionRamp (fade envelope)
  };

  /**
   * Underrun / gap event — fired when the audio callback was not called on
   * its expected schedule (the gap is the silent time before processBlock
   * actually started). The user perceives these as "mini cortes" in the
   * audio output.
   *
   * `songPositionSec` is the playback position at the moment the gap was
   * detected (i.e. when the callback finally ran), so the analysis can map
   * gaps to specific moments in the song.
   */
  struct GapEvent {
    double wallMs = 0.0;            // when the callback finally ran
    double deltaMs = 0.0;           // time since the previous block ended
    double songPositionSec = 0.0;   // song position when the gap was noticed
    int stemCount = 0;
    bool isPlaying = false;
  };

  // Saturating cast helper for the audio thread (no branches, no exceptions).
  static inline int16_t saturateUsToInt16(double micros) noexcept {
    constexpr double kMax = 32767.0;
    constexpr double kMin = -32768.0;
    if (micros > kMax)
      return 32767;
    if (micros < kMin)
      return -32768;
    return static_cast<int16_t>(micros);
  }

  struct StemTimingEvent {
    static constexpr int kMaxStemMetrics = 64;

    double wallMs = 0.0;
    double tempoRatio = 1.0;
    int pitchSemiTones = 0;
    int stemCount = 0;
    int targetLatencySamples = 0;
    bool routingActive = false;
    // True when every active stem resolves to the same (channel, width)
    // destination AND the device has at least two physical output
    // channels. Under these conditions the audio thread uses the
    // StemMixEngine grouped path (a single Bungee call per group instead
    // of one per stem) and routes the result via StemOutputRouter once.
    // False means the older per-stem Bungee loop is still running.
    bool routingGroupedEligible = false;
    bool timeProcessActive = false;
    bool tempoActive = false;
    std::array<int16_t, kMaxStemMetrics> processLatencySamples{};
    std::array<int16_t, kMaxStemMetrics> delaySamples{};
    // Per-stem routing snapshot taken just before the path decision.
    // Used to diagnose `routing_grouped_eligible=0` cases: when the
    // grouped path refuses to engage because some stems have a
    // different (channel, width) destination, these two arrays make it
    // obvious which stems are responsible.
    std::array<int16_t, kMaxStemMetrics> routingChannelOneBased{};
    std::array<int16_t, kMaxStemMetrics> routingWidth{};
    // Bit flags per stem:
    //  bit0 = processed
    //  bit1 = pitchShiftable
    //  bit2 = percussive content profile
    std::array<uint8_t, kMaxStemMetrics> flags{};

    // ---- Benchmark: worker drain instrumentation ----------------------
    // For each stem the audio thread samples the worker's most recent
    // visit. The flush thread writes one row per (event, stem) pair.
    //
    // workerDrainUs: how long the most recent useTimeSlice() took for
    //   this stem. 0 if the worker has not visited it yet.
    // workerGapUs: wall-clock microseconds since the most recent drain
    //   ENDED. Computed by the audio thread as
    //     (now - lastDrainStartTick - lastDrainDurationTicks)
    //   A large gap means the worker is slow to come back to this stem;
    //   that is the smoking gun for round-robin scheduling latency.
    // workerVisitCount: monotonically increasing per stem; the analysis
    //   can divide by elapsed time to get visits/sec per stem.
    std::array<int32_t, kMaxStemMetrics> workerDrainUs{};
    std::array<int32_t, kMaxStemMetrics> workerGapUs{};
    std::array<uint32_t, kMaxStemMetrics> workerVisitCount{};

    // ---- Benchmark: REAL drain instrumentation -----------------------
    // Separated from the "any drain" stats above because most
    // useTimeSlice calls are no-ops (buffer full). The audio thread
    // samples these to learn:
    //   realDrainUs:  duration of the most recent REAL drain (disk/decoder)
    //   realDrainGapUs:  inter-real-drain gap (computed by the audio
    //     thread as lastRealDrainEndTick - previousRealDrainEndTick, so
    //     it is NOT biased by the decimation cadence).
    //   realDrainCount:  cumulative count of real drains for this stem.
    std::array<int32_t, kMaxStemMetrics> realDrainUs{};
    std::array<int32_t, kMaxStemMetrics> realDrainGapUs{};
    std::array<uint32_t, kMaxStemMetrics> realDrainCount{};

    // ---- Benchmark: AAC decode + resample instrumentation -----------
    // The background worker calls ResamplingPositionableSource::
    // getNextAudioBlock() to pull PCM out of the AudioFormatReader
    // (which decodes AAC/WAV/FLAC) and feed it through the JUCE
    // resampler. These stats measure that path directly. The
    // decode time is the one the audio thread cannot see from
    // processBlock — this is what fills in the missing piece.
    //   decodeUs:  duration of the most recent decode+resample call.
    //   decodeGapUs:  wall-clock microseconds between the start of
    //     the most recent decode call and the start of the
    //     second-to-most-recent one. Captured directly by the
    //     worker (no decimation bias).
    //   decodeCount:  total decode+resample calls for this stem.
    std::array<int32_t, kMaxStemMetrics> decodeUs{};
    std::array<int32_t, kMaxStemMetrics> decodeGapUs{};
    std::array<uint32_t, kMaxStemMetrics> decodeCount{};

    // ---- Benchmark: Bungee (pitch/time-stretch) processing ----------
    // When the user transposes pitch or changes tempo, the audio
    // thread calls BungeeProcessor::process() on each stem that
    // needs the transform. That work is on the audio thread, so
    // its cost directly eats into the block budget. Captured per
    // stem so we can see which stems are expensive to pitch-shift
    // (typically percussive content is harder for the algorithm).
    //   bungeeUs:  duration of the most recent Bungee process()
    //     call for this stem. 0 when pitch/tempo is off.
    //   bungeeGapUs:  inter-call gap (decimation bias applies,
    //     same caveat as workerGapUs).
    //   bungeeCount:  cumulative number of Bungee process()
    //     invocations for this stem.
    std::array<int32_t, kMaxStemMetrics> bungeeUs{};
    std::array<int32_t, kMaxStemMetrics> bungeeGapUs{};
    std::array<uint32_t, kMaxStemMetrics> bungeeCount{};
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

  /**
   * Push a detected underrun / gap event. Called from the audio thread.
   * Like the other pushers, this is lock-free and drops on full.
   */
  void pushGapEvent(const GapEvent &ev) noexcept {
    if (!active.load(std::memory_order_relaxed))
      return;

    const int writeIdx = gapHead.load(std::memory_order_relaxed);
    const int nextIdx = (writeIdx + 1) & kGapMask;

    if (nextIdx == gapTail.load(std::memory_order_acquire))
      return; // Ring buffer full — drop event rather than block.

    gapEvents[static_cast<size_t>(writeIdx)] = ev;
    gapHead.store(nextIdx, std::memory_order_release);
  }

  /**
   * Returns the high-resolution tick at which the previous processBlock
   * ended, or 0 if no previous block has been recorded. Used by the audio
   * thread to detect underrun gaps without ever calling the wall clock.
   *
   * Stored as a relaxed atomic so the audio thread can read it without
   * ordering constraints — the value is only used to compute a duration
   * via the high-resolution timer, not for synchronization.
   */
  int64_t getPreviousBlockEndTick() const noexcept {
    return previousBlockEndTick.load(std::memory_order_relaxed);
  }

  void setPreviousBlockEndTick(int64_t tick) noexcept {
    previousBlockEndTick.store(tick, std::memory_order_relaxed);
  }

  /**
   * Underrun threshold in milliseconds. A gap greater than this between
   * consecutive processBlock calls is logged as a GapEvent. The default
   * (15ms) is well above the typical jitter of a healthy audio callback
   * (~5-6ms on macOS at 44.1kHz/512) and well below the threshold at which
   * the user starts to perceive a "cut" in the audio (~30ms+).
   */
  static constexpr double kDefaultGapThresholdMs = 15.0;
  static constexpr int64_t kTicksPerMillisecond = 1000; // only used for order-of-magnitude math

  double gapThresholdMs = kDefaultGapThresholdMs;
  double getGapThresholdMs() const noexcept {
    return gapThresholdMs;
  }
  void setGapThresholdMs(double ms) noexcept {
    gapThresholdMs = ms > 0.0 ? ms : kDefaultGapThresholdMs;
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
    gapLogFile = logDirectory.getChildFile("audio_gaps_" + ts + ".csv");

    // Write CSV header
    if (auto stream = logFile.createOutputStream()) {
      stream->writeText(
          "wall_ms,block_ms,budget_ms,overrun,lock_wait_ms,stem_count,streaming_misses,is_playing,streaming_missing_frames,time_processor_reset_misses,time_processor_service_failures,stage_pre_read_us,stage_read_next_us,stage_decode_render_us,stage_mix_stems_us,stage_mix_output_us,stage_ramp_transition_us\n",
          false, false, nullptr);
    }
    if (auto stream = stemLogFile.createOutputStream()) {
      stream->writeText(
          "wall_ms,tempo_ratio,pitch_semitones,routing_active,routing_grouped_eligible,time_process_active,tempo_active,stem_count,target_latency_samples,stem_index,processed,pitch_shiftable,content_type,proc_latency_samples,delay_samples,routing_channel_one_based,routing_width,worker_drain_us,worker_gap_us,worker_visit_count,real_drain_us,real_drain_gap_us,real_drain_count,decode_us,decode_gap_us,decode_count,bungee_us,bungee_gap_us,bungee_count\n",
          false, false, nullptr);
    }
    if (auto stream = gapLogFile.createOutputStream()) {
      stream->writeText(
          "wall_ms,delta_ms,song_position_sec,stem_count,is_playing\n",
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
    previousBlockEndTick.store(0, std::memory_order_relaxed);
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
    const int gaps = gapCount.load(std::memory_order_relaxed);
    const double maxGap = maxGapMs.load(std::memory_order_relaxed);

    return juce::String::formatted(
        "Blocks: %d | Overruns: %d (%.1f%%) | Avg: %.2fms | Max: %.2fms | Gaps: %d (max %.0fms)",
        total, over, pct, avgMs, maxMs, gaps, maxGap);
  }

private:
  static constexpr int kSize = 512;
  static constexpr int kMask = kSize - 1;
  static constexpr int kStemSize = 128;
  static constexpr int kStemMask = kStemSize - 1;
  // Gaps are rare; a small ring buffer is enough and keeps the
  // memory footprint trivial.
  static constexpr int kGapSize = 64;
  static constexpr int kGapMask = kGapSize - 1;

  std::array<DiagEvent, kSize> events{};
  std::atomic<int> head{0};
  std::atomic<int> tail{0};
  std::array<StemTimingEvent, kStemSize> stemEvents{};
  std::atomic<int> stemHead{0};
  std::atomic<int> stemTail{0};
  std::array<GapEvent, kGapSize> gapEvents{};
  std::atomic<int> gapHead{0};
  std::atomic<int> gapTail{0};
  std::atomic<bool> active{false};
  std::atomic<double> wallClockBaseMs{0.0};
  std::atomic<int64_t> wallClockBaseTick{0};
  std::atomic<double> wallClockTicksPerSecond{0.0};
  std::atomic<int64_t> previousBlockEndTick{0};

  // Cumulative stats (updated by flush thread only)
  std::atomic<int> overrunCount{0};
  std::atomic<int> totalCount{0};
  std::atomic<double> maxBlockMs{0.0};
  std::atomic<double> totalBlockMs{0.0};
  // Cumulative gap stats so the UI can show "n gaps detected" without
  // touching the CSV.
  std::atomic<int> gapCount{0};
  std::atomic<double> maxGapMs{0.0};

  juce::File logFile;
  juce::File stemLogFile;
  juce::File gapLogFile;

  void flushPending() {
    flushBlockEvents();
    flushStemTimingEvents();
    flushGapEvents();
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
          << ev.timeProcessorServiceFailures << ","
          << ev.stagePreReadUs << ","
          << ev.stageReadNextUs << ","
          << ev.stageDecodeRenderUs << ","
          << ev.stageMixStemsUs << ","
          << ev.stageMixOutputUs << ","
          << ev.stageRampTransitionUs << "\n";

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

  void flushGapEvents() {
    const int tailIdx = gapTail.load(std::memory_order_relaxed);
    const int headIdx = gapHead.load(std::memory_order_acquire);

    if (tailIdx == headIdx)
      return;

    std::ofstream out(gapLogFile.getFullPathName().toStdString(),
                      std::ios::app | std::ios::binary);
    if (!out.is_open())
      return;

    int idx = tailIdx;
    while (idx != headIdx) {
      const auto &ev = gapEvents[static_cast<size_t>(idx)];

      out << static_cast<int64_t>(ev.wallMs) << ","
          << juce::String(ev.deltaMs, 3).toStdString() << ","
          << juce::String(ev.songPositionSec, 3).toStdString() << ","
          << ev.stemCount << ","
          << (ev.isPlaying ? "1" : "0") << "\n";

      gapCount.fetch_add(1, std::memory_order_relaxed);

      double currentMax = maxGapMs.load(std::memory_order_relaxed);
      while (ev.deltaMs > currentMax &&
             !maxGapMs.compare_exchange_weak(currentMax, ev.deltaMs,
                                             std::memory_order_relaxed)) {}

      idx = (idx + 1) & kGapMask;
    }

    gapTail.store(headIdx, std::memory_order_release);
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
            << (ev.routingGroupedEligible ? "1" : "0") << ","
            << (ev.timeProcessActive ? "1" : "0") << ","
            << (ev.tempoActive ? "1" : "0") << ","
            << clampedStemCount << ","
            << ev.targetLatencySamples << ","
            << stemIndex << ","
            << processed << ","
            << pitchShiftable << ","
            << contentType << ","
            << ev.processLatencySamples[static_cast<size_t>(stemIndex)] << ","
            << ev.delaySamples[static_cast<size_t>(stemIndex)] << ","
            << ev.routingChannelOneBased[static_cast<size_t>(stemIndex)] << ","
            << ev.routingWidth[static_cast<size_t>(stemIndex)] << ","
            << ev.workerDrainUs[static_cast<size_t>(stemIndex)] << ","
            << ev.workerGapUs[static_cast<size_t>(stemIndex)] << ","
            << ev.workerVisitCount[static_cast<size_t>(stemIndex)] << ","
            << ev.realDrainUs[static_cast<size_t>(stemIndex)] << ","
            << ev.realDrainGapUs[static_cast<size_t>(stemIndex)] << ","
            << ev.realDrainCount[static_cast<size_t>(stemIndex)] << ","
            << ev.decodeUs[static_cast<size_t>(stemIndex)] << ","
            << ev.decodeGapUs[static_cast<size_t>(stemIndex)] << ","
            << ev.decodeCount[static_cast<size_t>(stemIndex)] << ","
            << ev.bungeeUs[static_cast<size_t>(stemIndex)] << ","
            << ev.bungeeGapUs[static_cast<size_t>(stemIndex)] << ","
            << ev.bungeeCount[static_cast<size_t>(stemIndex)] << "\n";
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
