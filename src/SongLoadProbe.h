#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

/**
 * SongLoadProbe — Per-song load-path telemetry.
 *
 * Companion to AudioDiagnostics (which measures the audio thread). This probe
 * measures the UI-thread cost of swapping from one song to the next, the
 * exact path the user perceives as "tarda mucho en cargar":
 *
 *   1. ProjectStore::open (read manifest, extract stems from .pwproj ZIP)
 *   2. StemCache::isValid / getCacheFile (FLAC cache lookup hit vs miss)
 *   3. PluginProcessor::loadSongStems (createReader + prepareToPlay per stem)
 *   4. waitForNextAudioBlockReady per stem (read-ahead warm-up)
 *   5. fade-out wait (if a song was already playing)
 *   6. suspendProcessing wait
 *   7. prepareRoutingTimeProcessors (Bungee voices allocation)
 *   8. ProjectSettings restoration (mix, click, guide, arrangement, window)
 *   9. buildMixedWaveformBins (off-thread via generation token)
 *
 * Output: one CSV per load session at
 *   ~/Library/Logs/MTControls/song_load_<ts>.csv
 *
 * Each row is a (phase, subPhase, durationMs, rssKb, extra) tuple. A single
 * load can produce 20-200 rows depending on stem count. Use the `phase`
 * column to group and `subPhase` to disambiguate (e.g. "loadSong" /
 * "stem#7.createReader"). Memory is reported in KiB (RSS) at row time, so
 * per-row deltas reveal allocations vs steady-state.
 *
 * Thread safety: the probe is meant for the UI / message thread. Each method
 * takes an internal mutex — instrumentation is not on any hot path so the
 * overhead is negligible, and we never want to fight the audio thread for
 * an allocator.
 */
class SongLoadProbe {
public:
  static SongLoadProbe &instance();

  /**
   * Begin a named phase. Must be paired with endPhase() on the same thread.
   * Pairs may be nested; each `end` closes the most recent open phase.
   */
  void beginPhase(const juce::String &phase, const juce::String &subPhase = {},
                  const juce::String &extra = {});

  /**
   * Close the most recent open phase. Records a row with the elapsed
   * microseconds since beginPhase() and the current RSS.
   */
  void endPhase();

  /**
   * Pop phases off the stack until we close a phase matching (phase, sub).
   * Any intermediate phases are closed with their original start time, so
   * their rows still get written. This is used by ScopedSongLoadTimer to
   * safely unwind on early return.
   */
  void endPhaseAndPopUntil(const juce::String &phase, const juce::String &sub);

  /**
   * Record a one-shot event with no associated duration. Useful for
   * transitions that aren't bracketed (e.g. "waitForReady timed out").
   */
  void recordEvent(const juce::String &phase, const juce::String &subPhase,
                   const juce::String &extra = {});

  /**
   * Record a duration directly (microseconds). For loops where RAII is
   * awkward.
   */
  void recordDurationUs(const juce::String &phase, const juce::String &subPhase,
                        int64_t durationUs, const juce::String &extra = {});

  /**
   * Record a per-stem sub-stage. Convenience wrapper that always sets
   * subPhase to e.g. "stem#7.createReader".
   */
  void recordStemStage(int stemIndex, const juce::String &stage,
                       int64_t durationUs, const juce::String &extra = {});

  /**
   * Force a flush to disk. The probe auto-flushes every 32 rows; call this
   * at the end of a load to make sure the last rows are durable before the
   * user inspects the file.
   */
  void flush();

  /**
   * Return the full path of the active CSV. Empty if start() has not run.
   */
  juce::String getLogPath() const;

  /**
   * RSS in KiB. Uses getrusage() on POSIX (returns maxrss in KB on macOS,
   * bytes on Linux — we report KiB consistently via the right unit).
   */
  static int64_t getCurrentRssKb();

private:
  SongLoadProbe() = default;
  void ensureStarted();

  struct OpenPhase {
    juce::String phase;
    juce::String subPhase;
    juce::String extra;
    int64_t startTick = 0;
  };
  std::vector<OpenPhase> stack_;
  mutable std::mutex mutex_;
  std::ofstream stream_;
  juce::File logFile_;
  int pendingRows_ = 0;
  bool headerWritten_ = false;
  bool active_ = false;
  int sessionOrdinal_ = 0;
};

/**
 * RAII helper. Use as `ScopedSongLoadTimer t("phase", "sub");` — the
 * duration is recorded when the timer goes out of scope.
 *
 * On destruction the timer calls
 * `SongLoadProbe::endPhaseAndPopUntil(phase, sub)`, which closes the
 * matching phase plus any deeper phases that were begun but not ended
 * (e.g. due to an early `return` from a function instrumented with
 * nested phases). The outermost timer for a logical unit of work should
 * always be created first so the cleanup order is predictable.
 */
struct ScopedSongLoadTimer {
  ScopedSongLoadTimer(juce::String phase, juce::String sub = {},
                      juce::String extra = {})
      : phase(std::move(phase)), sub(std::move(sub)) {
    SongLoadProbe::instance().beginPhase(phase, sub, extra);
  }
  ~ScopedSongLoadTimer() {
    SongLoadProbe::instance().endPhaseAndPopUntil(phase, sub);
  }
  ScopedSongLoadTimer(const ScopedSongLoadTimer &) = delete;
  ScopedSongLoadTimer &operator=(const ScopedSongLoadTimer &) = delete;

private:
  juce::String phase, sub;
};
