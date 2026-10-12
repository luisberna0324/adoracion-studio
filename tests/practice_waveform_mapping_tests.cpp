#include "../src/data/PracticeWaveform.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

namespace {
using Scheduler = playworship::audio::SectionScheduler;
using Frame = Scheduler::Frame;
using playworship::practice::WaveformTimeline;
using playworship::practice::projectWaveformBins;
using playworship::practice::waveformTimelineIdentity;

void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void expectBins(const std::vector<float> &actual,
                const std::vector<float> &expected, const char *message) {
  expect(actual.size() == expected.size(), message);
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (std::abs(actual[index] - expected[index]) > 0.00001f) {
      std::cerr << "FAILED: " << message << " at " << index << " (actual "
                << actual[index] << ", expected " << expected[index] << ")\n";
      std::exit(1);
    }
  }
}

Scheduler::Section original(const Frame start, const Frame end) {
  Scheduler::Section section;
  section.startFrame = start;
  section.endFrame = end;
  section.sourceStartFrame = start;
  section.sourceEndFrame = end;
  return section;
}

Scheduler::Section copy(const Frame start, const Frame end,
                         const Frame sourceStart, const Frame sourceEnd) {
  auto section = original(start, end);
  section.sourceStartFrame = sourceStart;
  section.sourceEndFrame = sourceEnd;
  section.insertedClip = true;
  return section;
}

WaveformTimeline timeline(const Frame sourceFrames, const Frame totalFrames,
                           const std::vector<Scheduler::Section> &sections = {},
                           const double sampleRate = 1.0) {
  Scheduler scheduler;
  scheduler.publish(sections);
  WaveformTimeline result;
  result.sections = scheduler.acquireSnapshot();
  result.sampleRate = sampleRate;
  result.sourceLengthSeconds = static_cast<double>(sourceFrames) / sampleRate;
  result.lengthSeconds = static_cast<double>(totalFrames) / sampleRate;
  result.customArrangement = !sections.empty();
  return result;
}

void testNoCopyIdentityAndPeakScale() {
  const std::vector<float> peaks{0.1f, 0.2f, 0.8f, 0.3f, 0.0f, 0.5f, 0.4f};
  auto source = timeline(1009, 1009, {}, 44100.0);
  expectBins(projectWaveformBins(peaks, source, 7), peaks,
             "no-copy projection preserves the source envelope at uneven frame boundaries");
  source.sections = {};
  expectBins(projectWaveformBins(peaks, source, 7), peaks,
             "invalid scheduler snapshot follows playback's source-linear fallback");
  source = timeline(1009, 1009, {original(0, 1009)}, 44100.0);
  source.sections.sections[0].sourceStartFrame = 99;
  source.sections.sections[0].sourceEndFrame = 100;
  expectBins(projectWaveformBins(peaks, source, 7), peaks,
             "original annotation source coordinates do not override the playback map");
  expectBins(projectWaveformBins(peaks, source, 1), {0.8f},
             "aggregation keeps the global source scale instead of normalizing to one");
}

void testOriginalAndTwoCopies() {
  const std::vector<float> peaks{0.1f, 0.2f, 0.3f, 0.4f,
                                 0.5f, 0.6f, 0.7f, 0.8f};
  const auto source = timeline(8, 12,
      {original(0, 2), copy(2, 4, 0, 2), copy(4, 6, 0, 2), original(6, 12)});
  expectBins(projectWaveformBins(peaks, source, 12),
      {0.1f, 0.2f, 0.1f, 0.2f, 0.1f, 0.2f,
       0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f},
      "original plus two copies repeats source peaks and shifts later originals");
}

void testTrimmedCopyOfCopy() {
  const std::vector<float> peaks{0.1f, 0.3f, 0.8f, 0.5f, 0.2f, 0.6f};
  // The processor has already resolved a trimmed copy-of-copy to [1, 3).
  // The waveform consumes that exact snapshot, rather than resolving IDs again.
  const auto source = timeline(6, 12,
      {original(0, 4), copy(4, 8, 0, 4), copy(8, 10, 1, 3), original(10, 12)});
  expectBins(projectWaveformBins(peaks, source, 12),
      {0.1f, 0.3f, 0.8f, 0.5f, 0.1f, 0.3f,
       0.8f, 0.5f, 0.3f, 0.8f, 0.2f, 0.6f},
      "a trimmed copy-of-copy follows the scheduler's explicit source range");
}

void testDiscontinuousBinsDoNotIncludeUnrelatedPeaks() {
  const auto oneCopy = timeline(8, 10,
      {original(0, 4), copy(4, 6, 0, 2), original(6, 10)});
  expectBins(projectWaveformBins({0.0f, 0.2f, 1.0f, 0.1f, 0.3f, 0.4f, 0.5f, 0.6f},
                                 oneCopy, 2),
             {1.0f, 0.6f},
             "a bin crossing a copy end cannot include the skipped source peak");

  const auto twoCopies = timeline(4, 6,
      {original(0, 1), copy(1, 2, 3, 4), copy(2, 3, 0, 1), original(3, 6)});
  expectBins(projectWaveformBins({0.1f, 0.2f, 0.9f, 0.4f}, twoCopies, 2),
             {0.4f, 0.9f},
             "a complete copy inside one output bin contributes its peak without a source hull");
}

void testShortStemAndSourceEndSilence() {
  const auto shorterStem = timeline(6, 10,
      {original(0, 6), copy(6, 10, 2, 6)});
  expectBins(projectWaveformBins({0.2f, 0.8f, 0, 0, 0, 0}, shorterStem, 10),
             {0.2f, 0.8f, 0, 0, 0, 0, 0, 0, 0, 0},
             "copying a short stem's padded source tail preserves silence");
  expectBins(projectWaveformBins({0.1f, 0.2f, 0.3f, 0.8f},
                                 timeline(4, 8, {original(0, 8)}), 8),
             {0.1f, 0.2f, 0.3f, 0.8f, 0, 0, 0, 0},
             "scheduler's sourceEnd clamp does not repeat the final peak into the tail");
  expectBins(projectWaveformBins({0.1f, 0.2f, 0.3f, 0.8f},
                                 timeline(4, 8), 8),
             {0.1f, 0.2f, 0.3f, 0.8f, 0, 0, 0, 0},
             "source-linear fallback also treats frames after sourceEnd as silence");
  expectBins(projectWaveformBins({0.1f, 0.2f, 0.3f, 0.8f},
                                 timeline(4, 8, {copy(4, 8, 3, 7)}), 8),
             {0.1f, 0.2f, 0.3f, 0.8f, 0.8f, 0, 0, 0},
             "an explicit copy partly outside the source cannot extend its last peak");
}

void testStretchedSourceUsesSchedulerRounding() {
  const auto source = timeline(4, 12, {copy(4, 12, 0, 4)});
  expectBins(projectWaveformBins({0.1f, 0.2f, 0.3f, 0.8f}, source, 12),
             {0.1f, 0.2f, 0.3f, 0.8f, 0.1f, 0.2f, 0.2f, 0.3f,
              0.3f, 0.8f, 0.8f, 0},
             "different copy durations use the scheduler's rounded source coordinates");
}

void testCompressedSourceKeepsAnEnvelope() {
  const auto source = timeline(8, 12, {copy(8, 12, 0, 8)});
  // The last output bin maps frames 8..11 to source frames 0, 2, 4, 6.
  // Preserve the peak between those frames as a conservative envelope, while
  // source frame 7 is outside that mapped span. This is intentionally not an
  // equality oracle for point-sampled compressed audio.
  expectBins(projectWaveformBins({0.1f, 0.2f, 0.3f, 0.2f, 0.4f, 0.9f, 0.3f, 1.0f},
                                 source, 3),
             {0.3f, 1.0f, 0.9f},
             "compressed copies aggregate the mapped source span conservatively");
}

void testContentIdentity() {
  const auto source = timeline(8, 10,
      {original(0, 4), copy(4, 6, 0, 2), original(6, 10)});
  const auto identity = waveformTimelineIdentity(source);
  auto changed = source;
  ++changed.sections.generation;
  changed.sections.sections[0].startFrame = 1;
  changed.sections.sections[0].sourceStartFrame = 77;
  changed.sections.sections[0].sourceEndFrame = 99;
  changed.sections.sections[1].muteInstruments = true;
  changed.sections.sections[1].skipPlayback = true;
  changed.sections.sections[1].guideTriggerId = 22;
  changed.sections.sections[1].guideTriggerFrame = 3;
  changed.sections.sections[1].triggerGuideOnEntry = true;
  changed.sections.cueCount = 1;
  changed.sections.cues[0] = {2, 22, true};
  expect(waveformTimelineIdentity(changed) == identity,
         "cue/mute/skip/generation and original annotations do not invalidate source mapping");
  changed = source;
  changed.sections.sections[1].sourceStartFrame = 2;
  changed.sections.sections[1].sourceEndFrame = 4;
  expect(waveformTimelineIdentity(changed) != identity,
         "same-duration copy source edits invalidate the waveform");
  expect(projectWaveformBins({0.1f, 0.2f, 0.8f, 0.6f, 0, 0, 0, 0}, changed, 10) !=
             projectWaveformBins({0.1f, 0.2f, 0.8f, 0.6f, 0, 0, 0, 0}, source, 10),
         "same-duration mapping edits actually change copy peaks");
  changed = source; ++changed.sourceRevision;
  expect(waveformTimelineIdentity(changed) != identity,
         "reloading the same-duration source invalidates decoded audio");
  changed = source; changed.sampleRate = 2;
  expect(waveformTimelineIdentity(changed) != identity, "sample rate is part of identity");
  changed = source; changed.sourceLengthSeconds += 1;
  expect(waveformTimelineIdentity(changed) != identity, "source duration is part of identity");
  changed = source; changed.lengthSeconds += 1;
  expect(waveformTimelineIdentity(changed) != identity, "arrangement duration is part of identity");
  changed = source; changed.customArrangement = !changed.customArrangement;
  expect(waveformTimelineIdentity(changed) != identity, "arrangement mode is part of identity");
  changed = source; changed.sections.sections[1].startFrame += 1;
  expect(waveformTimelineIdentity(changed) != identity, "copy position is part of identity");
  changed = source; changed.sections.sections[1].insertedClip = false;
  expect(waveformTimelineIdentity(changed) != identity, "adding/removing a copy changes identity");
}

void testCancellationBoundsAndInvalidInputs() {
  const auto source = timeline(100000, 100000);
  const std::vector<float> peaks(100000, 0.6f);
  expect(projectWaveformBins(peaks, source, 100000).size() == 65536,
         "output allocation has a hard display-resolution limit");
  expect(projectWaveformBins(peaks, source, 7).size() == 7,
         "output never exceeds the requested bin count");
  expect(projectWaveformBins(peaks, source, 100, [] { return true; }).empty(),
         "pre-cancelled work produces no result");
  int checks = 0;
  expect(projectWaveformBins(peaks, source, 1, [&checks] { return ++checks >= 3; }).empty(),
         "cancellation interrupts a single long source scan and discards partial output");
  expect(checks == 3, "cancellation stops work immediately when observed");
  expect(projectWaveformBins({}, source, 10).empty(), "empty input is empty");
  expect(projectWaveformBins(peaks, source, 0).empty(), "zero output resolution is empty");
  expect(projectWaveformBins(peaks, source, -1).empty(), "negative output resolution is empty");
  auto invalid = source;
  invalid.sampleRate = 0;
  expect(projectWaveformBins(peaks, invalid, 10).empty(), "invalid sample rate is rejected");
  invalid = source; invalid.lengthSeconds = std::numeric_limits<double>::quiet_NaN();
  expect(projectWaveformBins(peaks, invalid, 10).empty(), "NaN duration is rejected");
  invalid = source; invalid.sourceLengthSeconds = std::numeric_limits<double>::max();
  expect(projectWaveformBins(peaks, invalid, 10).empty(), "overflowing frame duration is rejected");
  expectBins(projectWaveformBins({std::numeric_limits<float>::quiet_NaN(), -0.4f, 2.0f},
                                 timeline(3, 3), 3),
             {0, 0.4f, 1}, "bad input amplitudes cannot corrupt normalized drawing geometry");
}

void testMaximumSnapshotAndLongArrangement() {
  constexpr Frame sourceFrames = 257;
  std::vector<float> peaks(static_cast<std::size_t>(sourceFrames), 0.3f);
  peaks[12] = 0.8f;
  std::vector<Scheduler::Section> sections;
  for (int index = 0; index < Scheduler::maxSections; ++index) {
    const Frame start = sourceFrames * (index + 1);
    sections.push_back(copy(start, start + sourceFrames, 0, sourceFrames));
  }
  const auto source = timeline(sourceFrames,
      sourceFrames * (Scheduler::maxSections + 1), sections);
  const auto result = projectWaveformBins(peaks, source, 70000);
  expect(result.size() == 65536, "maximum-size snapshots keep output memory bounded");
  expect(*std::max_element(result.begin(), result.end()) == 0.8f,
         "hundreds of copies retain source peak scale");
  expect(result.front() == 0.3f && result.back() == 0.3f,
         "the full long arrangement is projected rather than truncating at the bin cap");
}

void testFrameByFrameSchedulerOracle() {
  // Deterministic property test: arbitrary source ranges, adjacent copies,
  // bins that cross several discontinuities, and uneven source/output grids.
  std::mt19937 random(73021);
  for (int trial = 0; trial < 300; ++trial) {
    constexpr Frame sourceFrames = 53;
    std::vector<float> peaks(static_cast<std::size_t>(sourceFrames));
    for (auto &peak : peaks)
      peak = static_cast<float>(random() % 90) / 100.0f;
    std::vector<Scheduler::Section> sections;
    Frame insertedFrames = 0;
    for (Frame originalPosition = 0; originalPosition < sourceFrames; ++originalPosition) {
      if (random() % 7 != 0)
        continue;
      const Frame sourceStart = random() % (sourceFrames - 1);
      const Frame duration = 1 + random() % (sourceFrames - sourceStart);
      const Frame start = originalPosition + insertedFrames;
      sections.push_back(copy(start, start + duration, sourceStart, sourceStart + duration));
      insertedFrames += duration;
    }
    const Frame total = sourceFrames + insertedFrames;
    const auto source = timeline(sourceFrames, total, sections);
    const int count = 1 + static_cast<int>(random() % total);
    std::vector<float> expected(static_cast<std::size_t>(count), 0.0f);
    for (Frame frame = 0; frame < total; ++frame) {
      Frame mapped = frame;
      if (!Scheduler::mapFrameToSourceFrame(source.sections, frame, total, sourceFrames, mapped))
        mapped = frame;
      if (mapped >= 0 && mapped < sourceFrames) {
        const auto bin = static_cast<std::size_t>((frame * count) / total);
        expected[bin] = std::max(expected[bin], peaks[static_cast<std::size_t>(mapped)]);
      }
    }
    expectBins(projectWaveformBins(peaks, source, count), expected,
               "projection matches the actual scheduler at every source frame");
  }
}
} // namespace

int main() {
  testNoCopyIdentityAndPeakScale();
  testOriginalAndTwoCopies();
  testTrimmedCopyOfCopy();
  testDiscontinuousBinsDoNotIncludeUnrelatedPeaks();
  testShortStemAndSourceEndSilence();
  testStretchedSourceUsesSchedulerRounding();
  testCompressedSourceKeepsAnEnvelope();
  testContentIdentity();
  testCancellationBoundsAndInvalidInputs();
  testMaximumSnapshotAndLongArrangement();
  testFrameByFrameSchedulerOracle();
  std::cout << "Practice waveform mapping tests passed\n";
  return 0;
}
