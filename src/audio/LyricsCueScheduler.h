#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace playworship::audio {

// Independent arrangement-position cues. Control publishes immutable numeric
// snapshots; the audio thread reports only spans it actually rendered. No JUCE,
// allocation, locks, wall clock or network is used by the audio-side methods.
class LyricsCueScheduler {
public:
  using Frame = std::int64_t;
  static constexpr int maxCues = 1024;
  static constexpr unsigned queueCapacity = maxCues * 2 + 8;
  struct Cue { Frame frame = 0; int index = -1; };
  struct Snapshot {
    std::uint64_t revision = 0, songEpoch = 0;
    int count = 0;
    std::array<Cue, maxCues> cues{};
    bool valid = false;
  };
  struct Event {
    std::uint64_t revision = 0, serial = 0, deliveryEpoch = 0;
    int cueIndex = -1;
    Frame outputFrameTick = 0;
  };

  // Control only. Indices refer to the original SongLyrics.marks, not sorted
  // positions; stable sort defines a deterministic winner for equal times.
  void publish(std::vector<Cue> cues, std::uint64_t revision,
               std::uint64_t songEpoch) {
    std::stable_sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) {
      return a.frame < b.frame;
    });
    const std::lock_guard<std::mutex> lock(writerMutex);
    sequence.fetch_add(1, std::memory_order_acq_rel);
    publishedRevision.store(revision, std::memory_order_relaxed);
    publishedSongEpoch.store(songEpoch, std::memory_order_relaxed);
    const int count = std::min(maxCues, static_cast<int>(cues.size()));
    publishedCount.store(count, std::memory_order_relaxed);
    for (int i = 0; i < count; ++i) {
      frames[static_cast<std::size_t>(i)].store(cues[static_cast<std::size_t>(i)].frame,
                                               std::memory_order_relaxed);
      indices[static_cast<std::size_t>(i)].store(cues[static_cast<std::size_t>(i)].index,
                                                std::memory_order_relaxed);
    }
    invalidate();
    sequence.fetch_add(1, std::memory_order_release);
  }

  Snapshot acquireSnapshot() const noexcept {
    Snapshot snapshot;
    for (int attempt = 0; attempt < 3; ++attempt) {
      const auto before = sequence.load(std::memory_order_acquire);
      if ((before & 1U) != 0) continue;
      snapshot.revision = publishedRevision.load(std::memory_order_relaxed);
      snapshot.songEpoch = publishedSongEpoch.load(std::memory_order_relaxed);
      snapshot.count = publishedCount.load(std::memory_order_relaxed);
      for (int i = 0; i < snapshot.count; ++i) {
        snapshot.cues[static_cast<std::size_t>(i)] = {
          frames[static_cast<std::size_t>(i)].load(std::memory_order_relaxed),
          indices[static_cast<std::size_t>(i)].load(std::memory_order_relaxed)};
      }
      // Complete payload reads before validating the version on ARM as well
      // as x86; atomic fields alone only make a mixed snapshot race-free.
      std::atomic_thread_fence(std::memory_order_acquire);
      if (before == sequence.load(std::memory_order_acquire)) {
        snapshot.valid = true;
        return snapshot;
      }
    }
    return {};
  }

  // Control only. A new connection epoch invalidates queued history even if
  // disconnect and reconnect both happened between two UI timer callbacks.
  void setDeliveryState(std::uint64_t epoch, bool enabled) noexcept {
    const auto next = (epoch << 1U) | (enabled ? 1U : 0U);
    if (deliveryState.exchange(next, std::memory_order_acq_rel) != next)
      invalidate();
  }

  void setMaximumDeliveryAgeFrames(Frame ageFrames) noexcept {
    maximumDeliveryAgeFrames.store(std::max<Frame>(1, ageFrames), std::memory_order_release);
  }

  // Audio only. Publishing an edit must never itself project a slide. A new
  // song arms its current cue for the first real rendered span.
  void beginBlock(const Snapshot& candidate, bool stopped, int outputFrames = 0) noexcept {
    // This bounds queued-command age even while transport is paused. It does
    // not drive cues: their sole clock remains the actual arrangement span.
    outputFrameClock.fetch_add(std::max(0, outputFrames), std::memory_order_relaxed);
    if (candidate.valid && candidate.revision != audioSnapshot.revision) {
      if (!audioSnapshot.valid || candidate.songEpoch != audioSnapshot.songEpoch) {
        initialPending = true;
        havePosition = false;
      }
      audioSnapshot = candidate;
    }
    if (stopped && !wasStopped) {
      invalidate();
      initialPending = true;
      havePosition = false;
    }
    wasStopped = stopped;
  }

  // Audio only: call after a seek/jump/loop has become effective. Omitted
  // markers never fire. Loops explicitly call this even at the same position.
  void discontinuity(Frame destination, bool projectCurrent = true,
                     bool forceNewPass = true) noexcept {
    // playSong also republishes a same-position seek for custom arrangements.
    // That source revalidation is a resume, not another lyrical pass.
    if (!forceNewPass && havePosition && !initialPending && destination == lastEnd)
      return;
    invalidate();
    lastEnd = destination;
    havePosition = true;
    skipAtStart = -1;
    initialPending = !projectCurrent;
    if (projectCurrent) {
      const int current = findCurrent(destination);
      if (current >= 0) {
        emit(current);
        if (audioSnapshot.cues[static_cast<std::size_t>(current)].frame == destination)
          skipAtStart = current;
      } else if (audioSnapshot.count > 0) {
        // Before the first active mark there is no current lyric. A jump or
        // loop back here must clear the later screen from the previous pass.
        emit(-1);
      }
    }
  }

  // Audio only. The half-open [start,end) interval must be an actually
  // rendered arrangement span, expressed in original musical/source frames.
  void renderedSpan(Frame start, Frame end) noexcept {
    if (end <= start || !audioSnapshot.valid) return;
    if (initialPending || !havePosition || start != lastEnd)
      discontinuity(start);
    for (int i = 0; i < audioSnapshot.count; ++i) {
      const auto frame = audioSnapshot.cues[static_cast<std::size_t>(i)].frame;
      if (frame >= end) break;
      if (frame >= start && !(frame == start && skipAtStart >= 0)) emit(i);
    }
    skipAtStart = -1;
    lastEnd = end;
    havePosition = true;
    initialPending = false;
  }

  // Exactly one non-audio consumer. Drain even while disconnected. Only the
  // latest still-current event may be submitted to the network worker.
  bool popEvent(Event& event) noexcept {
    const auto read = readIndex.load(std::memory_order_relaxed);
    if (read == writeIndex.load(std::memory_order_acquire)) return false;
    event = queue[read];
    readIndex.store((read + 1U) % queueCapacity, std::memory_order_release);
    return true;
  }
  bool isCurrent(const Event& event) const noexcept {
    const auto delivery = deliveryState.load(std::memory_order_acquire);
    return (delivery & 1U) != 0 && event.deliveryEpoch == (delivery >> 1U) &&
           event.revision == publishedRevision.load(std::memory_order_acquire) &&
           event.serial == latestSerial.load(std::memory_order_acquire) &&
           outputFrameClock.load(std::memory_order_acquire) - event.outputFrameTick <=
               maximumDeliveryAgeFrames.load(std::memory_order_acquire);
  }
  std::uint64_t currentSerial() const noexcept {
    return latestSerial.load(std::memory_order_acquire);
  }
  std::uint64_t droppedEvents() const noexcept {
    return overflowCount.load(std::memory_order_relaxed);
  }

private:
  void invalidate() noexcept { latestSerial.fetch_add(1, std::memory_order_acq_rel); }
  int findCurrent(Frame destination) const noexcept {
    int current = -1;
    for (int i = 0; i < audioSnapshot.count; ++i) {
      if (audioSnapshot.cues[static_cast<std::size_t>(i)].frame > destination) break;
      current = i;
    }
    return current;
  }
  void emit(int sortedIndex) noexcept {
    const auto serial = latestSerial.fetch_add(1, std::memory_order_acq_rel) + 1U;
    const auto delivery = deliveryState.load(std::memory_order_acquire);
    if ((delivery & 1U) == 0) return; // Advance musical state without saving history.
    const auto write = writeIndex.load(std::memory_order_relaxed);
    const auto next = (write + 1U) % queueCapacity;
    if (next == readIndex.load(std::memory_order_acquire)) {
      overflowCount.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    queue[write] = {audioSnapshot.revision, serial, delivery >> 1U,
                   sortedIndex < 0 ? -1 : audioSnapshot.cues[static_cast<std::size_t>(sortedIndex)].index,
                   outputFrameClock.load(std::memory_order_relaxed)};
    writeIndex.store(next, std::memory_order_release);
  }

  std::mutex writerMutex;
  std::atomic<std::uint64_t> sequence{0}, publishedRevision{0}, publishedSongEpoch{0};
  std::atomic<int> publishedCount{0};
  std::array<std::atomic<Frame>, maxCues> frames{};
  std::array<std::atomic<int>, maxCues> indices{};
  std::atomic<std::uint64_t> deliveryState{0}, latestSerial{1}, overflowCount{0};
  std::array<Event, queueCapacity> queue{};
  std::atomic<unsigned> readIndex{0}, writeIndex{0};
  std::atomic<Frame> outputFrameClock{0}, maximumDeliveryAgeFrames{33075};
  Snapshot audioSnapshot;
  Frame lastEnd = 0;
  int skipAtStart = -1;
  bool initialPending = true, havePosition = false, wasStopped = false;
};
} // namespace playworship::audio
