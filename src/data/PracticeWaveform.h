#pragma once

#include "../audio/SectionScheduler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace playworship::practice {

// A coherent, read-only copy of the playback timeline. Source bins always span
// sourceLengthSeconds, including silence after the end of a shorter stem.
struct WaveformTimeline {
  audio::SectionScheduler::Snapshot sections;
  double sampleRate = 44100.0;
  double sourceLengthSeconds = 0.0;
  double lengthSeconds = 0.0;
  bool customArrangement = false;
  std::int64_t sourceRevision = 0;
};

namespace waveform_detail {
using Scheduler = audio::SectionScheduler;
using Frame = Scheduler::Frame;

inline Frame framesForSeconds(const double seconds,
                              const double sampleRate) noexcept {
  if (!std::isfinite(seconds) || !std::isfinite(sampleRate) || seconds <= 0.0 ||
      sampleRate <= 0.0)
    return 0;
  const double frames = seconds * sampleRate;
  // llround has undefined/domain-error behaviour outside the integer range.
  if (!std::isfinite(frames) ||
      static_cast<long double>(frames) >=
          static_cast<long double>(std::numeric_limits<Frame>::max()))
    return 0;
  return static_cast<Frame>(std::llround(frames));
}

// ceil(index * frames / count), without a potentially overflowing product.
// This is the inverse of the source builder's floor(frame * count / frames).
inline Frame binBoundary(const int index, const Frame frames,
                         const int count) noexcept {
  const Frame quotient = frames / count;
  const Frame remainder = frames % count;
  return quotient * index + (remainder * index + count - 1) / count;
}

inline std::size_t sourceBinForFrame(const Frame frame, const Frame frames,
                                     const std::size_t count) noexcept {
  const auto position = static_cast<std::uint64_t>(frame);
  const auto bins = static_cast<std::uint64_t>(count);
  const auto duration = static_cast<std::uint64_t>(frames);
  if (position <= std::numeric_limits<std::uint64_t>::max() / bins)
    return static_cast<std::size_t>((position * bins) / duration);
  // Only astronomically long timelines need this fallback. Ordinary audio
  // lengths use exact integer arithmetic, including at bucket boundaries.
  return std::min(count - 1,
                  static_cast<std::size_t>(
                      (static_cast<long double>(position) * bins) / duration));
}

inline std::uint64_t doubleBits(const double value) noexcept {
  static_assert(sizeof(double) == sizeof(std::uint64_t),
                "Waveform identities require a 64-bit double");
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}
} // namespace waveform_detail

// Content identity, deliberately independent of publication generation, guide
// cues, mute/skip flags and original-section annotation metadata. Only inserted
// clips participate in SectionScheduler's source mapping. Keeping their order
// also preserves the scheduler's precedence if sections overlap.
inline std::string waveformTimelineIdentity(const WaveformTimeline &timeline) {
  using namespace waveform_detail;
  std::string identity = "practice-waveform-v1;";
  identity.reserve(160 + static_cast<std::size_t>(Scheduler::size(timeline.sections)) * 64);
  const auto append = [&identity](const auto value) {
    identity += std::to_string(value);
    identity += ';';
  };
  append(timeline.sourceRevision);
  append(doubleBits(timeline.sampleRate));
  append(doubleBits(timeline.sourceLengthSeconds));
  append(doubleBits(timeline.lengthSeconds));
  append(timeline.customArrangement ? 1 : 0);
  for (int index = 0; index < Scheduler::size(timeline.sections); ++index) {
    const auto &section = timeline.sections.sections[static_cast<std::size_t>(index)];
    if (!section.insertedClip)
      continue;
    identity += "copy;";
    append(section.startFrame);
    append(section.endFrame);
    append(section.sourceStartFrame);
    append(section.sourceEndFrame);
  }
  return identity;
}

// Projects an already globally-normalized source envelope onto the arrangement.
// It never copies PCM, reads a file, or normalizes individual copies/viewports.
// Each output bin is split at *all* inserted-clip boundaries before consulting
// the playback mapper. Mapping only a bin's two endpoints would include unrelated
// source peaks across a jump, or miss a complete copy inside the bin. Within
// one continuous range this is a conservative peak envelope: a compressed copy
// can include peaks between mapped frames, rather than point-sampling them away.
inline std::vector<float> projectWaveformBins(
    const std::vector<float> &sourceBins, const WaveformTimeline &timeline,
    const int binCount, const std::function<bool()> &shouldCancel = {}) {
  using namespace waveform_detail;
  if (sourceBins.empty() || binCount <= 0 || (shouldCancel && shouldCancel()))
    return {};
  const Frame sourceFrames =
      framesForSeconds(timeline.sourceLengthSeconds, timeline.sampleRate);
  const Frame arrangementFrames =
      framesForSeconds(timeline.lengthSeconds, timeline.sampleRate);
  if (sourceFrames <= 0 || arrangementFrames <= 0)
    return {};

  constexpr int maximumOutputBins = 65536;
  const int outputCount = static_cast<int>(std::min<Frame>(
      std::min(binCount, maximumOutputBins), arrangementFrames));
  std::vector<float> result(static_cast<std::size_t>(outputCount), 0.0f);

  // At most 256 copy ranges / 512 frame boundaries, fixed stack storage. There
  // are no per-bin allocations or arrangement-length/sample-count buffers.
  std::array<Frame, Scheduler::maxSections * 2> boundaries{};
  std::size_t boundaryCount = 0;
  for (int index = 0; index < Scheduler::size(timeline.sections); ++index) {
    const auto &section = timeline.sections.sections[static_cast<std::size_t>(index)];
    if (!section.insertedClip)
      continue;
    for (const Frame boundary : {section.startFrame, section.endFrame})
      if (boundary > 0 && boundary < arrangementFrames)
        boundaries[boundaryCount++] = boundary;
  }
  std::sort(boundaries.begin(), boundaries.begin() + boundaryCount);
  boundaryCount = static_cast<std::size_t>(
      std::unique(boundaries.begin(), boundaries.begin() + boundaryCount) -
      boundaries.begin());

  const auto map = [&timeline, arrangementFrames, sourceFrames](const Frame frame) {
    Frame sourceFrame = frame;
    // The fallback is identical to the processor's playback mapping. In
    // particular, an empty/temporarily invalid snapshot remains source-linear.
    if (!Scheduler::mapFrameToSourceFrame(timeline.sections, frame,
                                          arrangementFrames, sourceFrames,
                                          sourceFrame))
      sourceFrame = frame;
    return sourceFrame;
  };
  std::size_t boundaryIndex = 0;
  std::size_t peaksSinceCancellationCheck = 0;
  for (int bin = 0; bin < outputCount; ++bin) {
    if (shouldCancel && shouldCancel())
      return {};
    const Frame end = binBoundary(bin + 1, arrangementFrames, outputCount);
    Frame cursor = binBoundary(bin, arrangementFrames, outputCount);
    float peak = 0.0f;
    while (cursor < end) {
      while (boundaryIndex < boundaryCount && boundaries[boundaryIndex] <= cursor)
        ++boundaryIndex;
      const Frame rangeEnd = boundaryIndex < boundaryCount
          ? std::min(end, boundaries[boundaryIndex]) : end;
      const Frame sourceBegin = map(cursor);
      // Use the last frame *inside* the range. map(rangeEnd) can already be on
      // the other side of a copy's discontinuity. SourceEnd itself is silence,
      // never a request to repeat the last source bin into the tail.
      const Frame sourceLast = map(rangeEnd - 1);
      if (sourceBegin >= 0 && sourceBegin < sourceFrames &&
          sourceLast >= sourceBegin) {
        const auto first = sourceBinForFrame(sourceBegin, sourceFrames, sourceBins.size());
        const auto last = sourceBinForFrame(std::min(sourceLast, sourceFrames - 1),
                                            sourceFrames, sourceBins.size());
        for (auto index = first; index <= last; ++index) {
          const float value = sourceBins[index];
          if (std::isfinite(value))
            peak = std::max(peak, std::min(1.0f, std::abs(value)));
          // A single output interval may scan the entire source. Cancellation
          // must remain responsive even when outputCount is one.
          if (++peaksSinceCancellationCheck == 1024) {
            peaksSinceCancellationCheck = 0;
            if (shouldCancel && shouldCancel())
              return {};
          }
        }
      }
      cursor = rangeEnd;
    }
    result[static_cast<std::size_t>(bin)] = peak;
  }
  if (shouldCancel && shouldCancel())
    return {};
  return result;
}

} // namespace playworship::practice
