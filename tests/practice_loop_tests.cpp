#include "audio/PracticeLoop.h"
#include "audio/SectionScheduler.h"
#include "audio/TransportClock.h"
#include "StemTrack.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

namespace {
using Loop = playworship::audio::PracticeLoop;
using Clock = playworship::audio::TransportClock;
constexpr double sampleRate = 48000.0;

void expectLoop(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void testPracticeLoopValidation() {
  expectLoop(Loop::minimumDurationSeconds == 0.25,
             "the minimum practice loop is a quarter of a second");
  expectLoop(Loop::isValidRange(10.0, 10.25, 10.0, 20.0),
             "the exact minimum duration is valid at the song-window start");
  expectLoop(Loop::isValidRange(19.75, 20.0, 10.0, 20.0),
             "B may coincide with the song-window end");
  expectLoop(!Loop::isValidRange(10.0, 10.249, 10.0, 20.0) &&
                 !Loop::isValidRange(10.0, 10.0, 10.0, 20.0) &&
                 !Loop::isValidRange(12.0, 11.0, 10.0, 20.0),
             "too-short, empty and reversed loop selections are rejected");
  expectLoop(!Loop::isValidRange(9.75, 10.25, 10.0, 20.0) &&
                 !Loop::isValidRange(19.75, 20.25, 10.0, 20.0),
             "a practice loop cannot extend beyond persistent Inicio/Fin");
  expectLoop(!Loop::isValidRange(10.0, 10.5, 10.0, 10.0) &&
                 !Loop::isValidRange(10.0, 10.5, 20.0, 10.0),
             "empty and reversed song windows cannot accept a practice loop");
  const auto invalid = std::numeric_limits<double>::quiet_NaN();
  const auto infinity = std::numeric_limits<double>::infinity();
  for (const auto nonfinite : {invalid, infinity, -infinity}) {
    expectLoop(!Loop::isValidRange(nonfinite, 11.0, 10.0, 20.0) &&
                   !Loop::isValidRange(10.0, nonfinite, 10.0, 20.0) &&
                   !Loop::isValidRange(10.0, 11.0, nonfinite, 20.0) &&
                   !Loop::isValidRange(10.0, 11.0, 10.0, nonfinite),
               "nonfinite endpoints and windows are rejected before publication");
  }
}

void testPracticeLoopPublication() {
  Loop loop;
  const auto initial = loop.snapshot();
  expectLoop(initial.valid && !initial.active,
             "a new practice-loop source publishes an inactive coherent snapshot");
  const auto firstRevision = loop.setRange(10.0, 12.0);
  const auto first = loop.snapshot();
  expectLoop(first.valid && first.active && first.startSeconds == 10.0 &&
                 first.endSeconds == 12.0 && first.revision == firstRevision &&
                 loop.revision() == firstRevision && firstRevision > initial.revision,
             "publishing A/B exposes both endpoints and their ownership revision");
  const auto secondRevision = loop.setRange(20.0, 21.5);
  const auto second = loop.snapshot();
  expectLoop(second.valid && second.active && second.startSeconds == 20.0 &&
                 second.endSeconds == 21.5 && second.revision == secondRevision &&
                 secondRevision > firstRevision,
             "a replacement loop invalidates the previous ownership revision");
  loop.clear();
  const auto cleared = loop.snapshot();
  expectLoop(cleared.valid && !cleared.active &&
                 cleared.revision > secondRevision && loop.revision() == cleared.revision,
             "release publishes inactive state and invalidates the active owner");
  expectLoop(first.active && first.startSeconds == 10.0 && first.endSeconds == 12.0,
             "a captured audio snapshot stays unchanged after replacement and release");

  const auto preparedRevision = loop.setRange(7.25, 7.25);
  const auto prepared = loop.snapshot();
  expectLoop(prepared.valid && prepared.prepared && !prepared.active &&
                 prepared.startSeconds == 7.25 && prepared.endSeconds == 7.25 &&
                 prepared.revision == preparedRevision,
             "publishing A alone owns a prepared destination without activating a loop");
  expectLoop(!Loop::evaluate(prepared, 48000, 512, sampleRate).wrap,
             "an A-only prefetch cannot wrap or alter the current audio clock");
  loop.clear();
  const auto releasedPreparation = loop.snapshot();
  expectLoop(releasedPreparation.valid && !releasedPreparation.prepared &&
                 !releasedPreparation.active &&
                 releasedPreparation.revision > preparedRevision,
             "release invalidates an A-only preparation as well as an active loop");

  loop.setRange(10.0, 10.25);
  std::atomic<bool> start{false};
  std::atomic<bool> done{false};
  std::atomic<bool> clearDone{false};
  std::thread writer([&] {
    while (!start.load(std::memory_order_acquire))
      std::this_thread::yield();
    for (int update = 0; update < 50000; ++update) {
      if ((update % 17) == 0)
        loop.clear();
      else if ((update % 31) == 0)
        loop.setRange(42.0, 42.0);
      else if ((update % 2) == 0)
        loop.setRange(10.0, 10.25);
      else
        loop.setRange(20.0, 20.75);
    }
    done.store(true, std::memory_order_release);
  });
  std::thread revoker([&] {
    while (!start.load(std::memory_order_acquire))
      std::this_thread::yield();
    for (int revoke = 0; revoke < 50000; ++revoke)
      loop.clear();
    clearDone.store(true, std::memory_order_release);
  });
  start.store(true, std::memory_order_release);
  uint64_t lastRevision = 0;
  do {
    const auto snapshot = loop.snapshot();
    if (!snapshot.valid)
      continue; // A contested publication must be reported as unavailable.
    expectLoop(snapshot.revision >= lastRevision,
               "coherent snapshots never travel back to an older publication");
    lastRevision = snapshot.revision;
    expectLoop((snapshot.startSeconds == 10.0 && snapshot.endSeconds == 10.25) ||
                   (snapshot.startSeconds == 20.0 && snapshot.endSeconds == 20.75) ||
                   (snapshot.startSeconds == 42.0 && snapshot.endSeconds == 42.0),
               "every coherent snapshot contains one complete publication even after concurrent revocation");
    if (snapshot.active) {
      expectLoop((snapshot.startSeconds == 10.0 && snapshot.endSeconds == 10.25) ||
                     (snapshot.startSeconds == 20.0 && snapshot.endSeconds == 20.75),
                 "audio never observes A from one publication and B from another");
    } else if (snapshot.prepared) {
      expectLoop(snapshot.startSeconds == 42.0 && snapshot.endSeconds == 42.0,
                 "prepared A snapshots never inherit an endpoint from an active B publication");
    }
  } while (!done.load(std::memory_order_acquire) ||
           !clearDone.load(std::memory_order_acquire));
  writer.join();
  revoker.join();
  const auto final = loop.snapshot();
  expectLoop(final.valid && final.revision >= lastRevision &&
                 final.startSeconds == 20.0 && final.endSeconds == 20.75,
             "once publication is idle audio receives the complete final loop snapshot");
  loop.clear();
  const auto revoked = loop.snapshot();
  expectLoop(revoked.valid && !revoked.active && !revoked.prepared &&
                 revoked.revision > final.revision,
             "the final revoke cannot leave an active or prepared loop after concurrent publication");
}

void testPracticeLoopBoundaries() {
  Loop loop;
  loop.setRange(1.0, 1.25);
  const auto snapshot = loop.snapshot();
  constexpr int64_t a = 48000;
  constexpr int64_t b = 60000;
  auto boundary = Loop::evaluate(snapshot, b - 512, 511, sampleRate);
  expectLoop(!boundary.wrap,
             "a callback ending before B continues on the current source");
  boundary = Loop::evaluate(snapshot, b - 512, 512, sampleRate);
  expectLoop(boundary.wrap && boundary.offsetFrames == 512 && boundary.targetFrame == a,
             "a callback ending exactly at B wraps at its right edge");
  boundary = Loop::evaluate(snapshot, b - 137, 1024, sampleRate);
  expectLoop(boundary.wrap && boundary.offsetFrames == 137 && boundary.targetFrame == a,
             "a callback crossing B resolves the exact prefix and A destination");
  for (const int64_t lateStart : {b, b + 83}) {
    boundary = Loop::evaluate(snapshot, lateStart, 1024, sampleRate);
    expectLoop(boundary.wrap && boundary.offsetFrames == 0 && boundary.targetFrame == a,
               "a clock already at or beyond B returns to A before producing audio");
  }
  for (const int frames : {0, -1})
    expectLoop(!Loop::evaluate(snapshot, b, frames, sampleRate).wrap,
               "an empty callback cannot create a loop transition");
  for (const double rate : {0.0, -1.0,
                             std::numeric_limits<double>::quiet_NaN()})
    expectLoop(!Loop::evaluate(snapshot, b, 512, rate).wrap,
               "an invalid sample rate cannot create a loop transition");
  loop.clear();
  expectLoop(!Loop::evaluate(loop.snapshot(), b - 1, 512, sampleRate).wrap,
             "releasing A/B removes the next wrap without changing transport position");
  auto unavailable = snapshot;
  unavailable.valid = false;
  expectLoop(!Loop::evaluate(unavailable, b - 1, 512, sampleRate).wrap,
             "audio ignores an incoherent loop snapshot");
}

void testPracticeLoopSourceContinuity() {
  using Scheduler = playworship::audio::SectionScheduler;
  Scheduler::Snapshot sections;
  sections.valid = true;
  sections.count = 2;
  sections.sections[0] = {0, 48000, 0, 48000};
  sections.sections[1] = {48000, 96000, 48000, 96000};
  expectLoop(Loop::isSourceContinuous(sections, 0.5, 1.5, sampleRate),
             "a loop may cross adjacent sections that preserve the same source timeline");
  sections.sections[1] = {48000, 96000, 96000, 144000};
  expectLoop(!Loop::isSourceContinuous(sections, 0.5, 1.5, sampleRate),
             "a loop cannot combine an interior source jump with its A/B wrap");
  expectLoop(Loop::isSourceContinuous(sections, 0.5, 1.0, sampleRate) &&
                 Loop::isSourceContinuous(sections, 1.0, 1.5, sampleRate),
             "a source cut exactly at A or B is safe because only one handoff is needed");
  sections.valid = false;
  expectLoop(!Loop::isSourceContinuous(sections, 0.5, 1.0, sampleRate),
             "loop selection cannot validate against an unavailable arrange snapshot");
}

class LoopRampReader final : public juce::AudioFormatReader {
public:
  LoopRampReader() : AudioFormatReader(nullptr, "practice loop ramp") {
    sampleRate = ::sampleRate;
    bitsPerSample = 32;
    lengthInSamples = 4 * 48000;
    numChannels = 2;
    usesFloatingPointData = true;
  }

  bool readSamples(int *const *destChannels, const int numDestChannels,
                   const int offset, const juce::int64 startFrame,
                   const int frames) override {
    for (int channel = 0; channel < numDestChannels; ++channel) {
      if (destChannels[channel] == nullptr)
        continue;
      auto *destination = reinterpret_cast<float *>(destChannels[channel]) + offset;
      for (int frame = 0; frame < frames; ++frame)
        destination[frame] = sampleAt(startFrame + frame);
    }
    return true;
  }

  static float sampleAt(const juce::int64 frame) {
    return 0.1f + static_cast<float>(frame) * 1.0e-6f;
  }
};

void testPracticeLoopAudioRender() {
  Loop loop;
  loop.setRange(1.0, 1.25);
  const auto snapshot = loop.snapshot();
  constexpr juce::int64 a = 48000;
  constexpr juce::int64 b = 60000;
  constexpr juce::int64 firstFrame = b - 137;
  juce::TimeSliceThread unusedReadThread("practice-loop-synchronous-test");
  StemTrack stem(std::make_unique<LoopRampReader>(), unusedReadThread,
                 sampleRate, 32768, false);
  stem.setVolume(0.6f);
  stem.setTrim(1.2f);
  stem.prepareToPlay(1024, sampleRate);
  stem.setNextReadPosition(firstFrame);
  Clock clock;
  clock.prepare(sampleRate);
  clock.seek(firstFrame);
  clock.play();
  constexpr int renderedFrames = 2 * (b - a) + 777;
  const std::vector<int> callbackSizes{1024, 31, 257, 512};
  int rendered = 0;
  int wraps = 0;
  size_t callback = 0;
  while (rendered < renderedFrames) {
    const int frames = std::min(callbackSizes[callback++ % callbackSizes.size()],
                                renderedFrames - rendered);
    const auto boundary = Loop::evaluate(snapshot, clock.getPositionFrames(), frames,
                                         sampleRate);
    if (boundary.wrap) {
      ++wraps;
      // Disable only the discontinuity envelope to expose exact source samples.
      // Production still schedules the same prefix and target with its fade.
      stem.scheduleReadDiscontinuity(boundary.offsetFrames, boundary.targetFrame, 0);
    }
    juce::AudioBuffer<float> output(2, frames + 10);
    for (int channel = 0; channel < 2; ++channel)
      std::fill_n(output.getWritePointer(channel), output.getNumSamples(), -9.0f);
    stem.getNextAudioBlock(juce::AudioSourceChannelInfo(&output, 5, frames));
    for (int frame = 0; frame < frames; ++frame) {
      const auto sourceFrame =
          a + ((firstFrame - a + rendered + frame) % (b - a));
      const float expected = LoopRampReader::sampleAt(sourceFrame) * 0.6f * 1.2f;
      for (int channel = 0; channel < 2; ++channel)
        expectLoop(std::isfinite(output.getSample(channel, 5 + frame)) &&
                       std::abs(output.getSample(channel, 5 + frame) - expected) < 2.0e-7f,
                   "real stem audio repeats only A..B without omitted or duplicated source frames");
    }
    for (int channel = 0; channel < 2; ++channel)
      for (int guard = 0; guard < 5; ++guard)
        expectLoop(output.getSample(channel, guard) == -9.0f &&
                       output.getSample(channel, 5 + frames + guard) == -9.0f,
                   "an A/B transition leaves audio outside the callback region untouched");
    if (boundary.wrap)
      clock.seek(boundary.targetFrame);
    clock.advance(boundary.wrap ? frames - boundary.offsetFrames : frames);
    rendered += frames;
    expectLoop(clock.getPositionFrames() == stem.getNextReadPosition(),
               "the transport clock and real stem stay aligned across every A/B wrap");
  }
  expectLoop(wraps >= 3,
             "the render exercises repeated A/B transitions across varied callback sizes");
  expectLoop(stem.getVolume() == 0.6f && stem.getTrim() == 1.2f,
             "temporary A/B playback preserves the saved stem mix");
  const auto resumeFrame = clock.getPositionFrames();
  loop.clear();
  expectLoop(!Loop::evaluate(loop.snapshot(), resumeFrame, 1024, sampleRate).wrap,
             "release resumes the song from its current clock without an extra seek");
  stem.releaseResources();
}
} // namespace

void runPracticeLoopTests() {
  testPracticeLoopValidation();
  testPracticeLoopPublication();
  testPracticeLoopBoundaries();
  testPracticeLoopSourceContinuity();
  testPracticeLoopAudioRender();
}
