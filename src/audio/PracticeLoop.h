#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace playworship::audio {

// Temporary playback intent. It is deliberately separate from the persisted
// song window and the arrangement's sections. The message thread publishes a
// range; any control thread can clear it without taking a lock.
class PracticeLoop {
public:
  static constexpr double minimumDurationSeconds = 0.25;
  struct Snapshot {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    std::uint64_t revision = 0;
    bool active = false;
    bool prepared = false;
    bool valid = true;
  };
  struct Boundary {
    bool wrap = false;
    int offsetFrames = -1;
    std::int64_t targetFrame = 0;
  };

  static bool isValidRange(double start, double end, double windowStart,
                           double windowEnd) noexcept {
    return std::isfinite(start) && std::isfinite(end) &&
           std::isfinite(windowStart) && std::isfinite(windowEnd) &&
           windowStart >= 0.0 && windowEnd >= windowStart &&
           start >= windowStart && end <= windowEnd &&
           end - start >= minimumDurationSeconds;
  }

  // Snapshot fields are SectionScheduler frames. Cuts exactly at A or B are
  // allowed: only an interior source discontinuity would require a second
  // audio handoff in the same callback as the A/B wrap.
  template <typename SectionSnapshot>
  static bool isSourceContinuous(const SectionSnapshot &sections,
                                 double start, double end,
                                 double sampleRate) noexcept {
    if (!sections.valid || !std::isfinite(start) || !std::isfinite(end) ||
        !std::isfinite(sampleRate) || sampleRate <= 0.0 || end <= start)
      return false;
    const auto first = static_cast<std::int64_t>(std::llround(start * sampleRate));
    const auto last = static_cast<std::int64_t>(std::llround(end * sampleRate));
    for (int index = 0; index + 1 < sections.count; ++index) {
      const auto &left = sections.sections[static_cast<std::size_t>(index)];
      const auto &right = sections.sections[static_cast<std::size_t>(index + 1)];
      if (left.endFrame > first && left.endFrame < last &&
          (left.endFrame != right.startFrame ||
           left.sourceEndFrame != right.sourceStartFrame))
        return false;
    }
    return true;
  }

  // Only the serialized control/message thread publishes ranges. Clear may be
  // called by MIDI on the audio thread. Its newer revision cancels an in-flight
  // publication as well as an already published range.
  std::uint64_t setRange(double start, double end) noexcept {
    const auto intent = currentRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
    publicationSequence.fetch_add(1, std::memory_order_acq_rel);
    // Pair through the atomic payload with the reader's acquire fence. If a
    // reader observes any field from this publication, its final sequence
    // check must also observe this odd sequence (or a newer one).
    std::atomic_thread_fence(std::memory_order_release);
    startSeconds.store(start, std::memory_order_relaxed);
    endSeconds.store(end, std::memory_order_relaxed);
    publishedRevision.store(intent, std::memory_order_relaxed);
    publicationSequence.fetch_add(1, std::memory_order_release);
    return intent;
  }

  void clear() noexcept {
    currentRevision.fetch_add(1, std::memory_order_acq_rel);
  }

  std::uint64_t revision() const noexcept {
    return currentRevision.load(std::memory_order_acquire);
  }

  Snapshot snapshot() const noexcept {
    for (int attempt = 0; attempt < 3; ++attempt) {
      const auto before = publicationSequence.load(std::memory_order_acquire);
      if ((before & 1U) != 0U)
        continue;
      Snapshot result;
      result.startSeconds = startSeconds.load(std::memory_order_relaxed);
      result.endSeconds = endSeconds.load(std::memory_order_relaxed);
      result.revision = publishedRevision.load(std::memory_order_relaxed);
      // An acquire load of the sequence alone does not keep earlier relaxed
      // payload loads ahead of it on weakly ordered CPUs such as ARM64.
      std::atomic_thread_fence(std::memory_order_acquire);
      const auto after = publicationSequence.load(std::memory_order_acquire);
      if (before != after || (after & 1U) != 0U)
        continue;
      const auto latest = currentRevision.load(std::memory_order_acquire);
      const bool current = result.revision != 0 && result.revision == latest;
      result.prepared = current && std::isfinite(result.startSeconds) &&
                        result.startSeconds >= 0.0 &&
                        result.startSeconds == result.endSeconds;
      result.active = current &&
                      isValidRange(result.startSeconds, result.endSeconds,
                                   0.0, result.endSeconds);
      if (!result.active && !result.prepared)
        result.revision = latest;
      return result;
    }
    Snapshot unavailable;
    unavailable.revision = revision();
    unavailable.valid = false;
    return unavailable;
  }

  static Boundary evaluate(const Snapshot &range, std::int64_t blockStart,
                            int blockFrames, double sampleRate) noexcept {
    Boundary result;
    if (!range.active || !range.valid || blockFrames <= 0 ||
        !std::isfinite(sampleRate) || sampleRate <= 0.0)
      return result;
    const auto start = static_cast<std::int64_t>(
        std::llround(range.startSeconds * sampleRate));
    const auto end = static_cast<std::int64_t>(
        std::llround(range.endSeconds * sampleRate));
    if (end <= start)
      return result;
    if (blockStart < start || blockStart >= end) {
      result.wrap = true;
      result.offsetFrames = 0;
    } else if (end - blockStart <= blockFrames) {
      result.wrap = true;
      result.offsetFrames = static_cast<int>(end - blockStart);
    }
    if (result.wrap)
      result.targetFrame = start;
    return result;
  }

private:
  std::atomic<std::uint64_t> currentRevision{0};
  std::atomic<std::uint64_t> publicationSequence{0};
  std::atomic<std::uint64_t> publishedRevision{0};
  std::atomic<double> startSeconds{0.0};
  std::atomic<double> endSeconds{0.0};
};

} // namespace playworship::audio
