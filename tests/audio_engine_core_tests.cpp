#include "../src/audio/SectionScheduler.h"
#include "../src/audio/SongTransitionEnvelope.h"
#include "../src/audio/TransportClock.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace {

void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void testTransportClock() {
  using Clock = playworship::audio::TransportClock;
  Clock clock;
  clock.prepare(48000.0);
  clock.seek(48000);
  expect(clock.getPositionSeconds() == 1.0, "seek uses frame clock");
  clock.play();
  clock.advance(480);
  expect(clock.getPositionFrames() == 48480, "playing clock advances");
  clock.pause();
  clock.advance(480);
  expect(clock.getPositionFrames() == 48480, "paused clock does not advance");
  clock.stop(24000);
  expect(clock.getPositionFrames() == 24000, "stop returns to requested frame");
}

void testTransportClockAndArrangeBoundaries() {
  using Clock = playworship::audio::TransportClock;
  using Scheduler = playworship::audio::SectionScheduler;
  Clock clock;
  clock.prepare(48000.0);
  clock.seek(-100);
  expect(clock.getPositionFrames() == 0, "negative seek clamps to song start");
  clock.play();
  clock.advance(512);
  clock.pause();
  clock.advance(512);
  expect(clock.getPositionFrames() == 512,
         "pause freezes the sample accurate clock");
  clock.play();
  clock.advance(488);
  expect(clock.getPositionFrames() == 1000,
         "resume advances from the paused frame");
  clock.reset(-1);
  expect(clock.getState() == Clock::State::stopped &&
             clock.getPositionFrames() == 0,
         "reset clears state and clamps its return frame");
  expect(Clock::secondsToFrames(0.5, 48000.0) == 24000 &&
             Clock::secondsToFrames(NAN, 48000.0) == 0,
         "time conversion handles valid and invalid positions");

  Scheduler scheduler;
  scheduler.publish(
      {{0, 1000, 0, 1000, -1, -1, false, false, false, false},
       {1000, 1200, 400, 600, 1000, 11, true, true, false, false},
       {1200, 2200, 1000, 2000, 1200, 12, true, false, false, false}});
  const auto snapshot = scheduler.acquireSnapshot();
  expect(Scheduler::findGuideTrigger(snapshot, 900, 100) == -1 &&
             Scheduler::findGuideTrigger(snapshot, 1000, 1) == 1,
         "a guide at the block end fires in the following block");
  Scheduler::Frame source = -1;
  expect(Scheduler::mapFrameToSourceFrame(snapshot, 1100, 2200, 2000,
                                          source) &&
             source == 500,
         "seek inside an inserted clip uses its source range");
  expect(Scheduler::mapFrameToSourceFrame(snapshot, 1200, 2200, 2000,
                                          source) &&
             source == 1000,
         "seek at an inserted clip's right edge resumes the original source");
  scheduler.clear();
  expect(Scheduler::size(snapshot) == 3 &&
             Scheduler::size(scheduler.acquireSnapshot()) == 0,
         "clearing arrange publishes an empty generation without changing the old snapshot");
}

void testSectionScheduler() {
  using Scheduler = playworship::audio::SectionScheduler;
  Scheduler scheduler;
  scheduler.publish(
      {{0, 1000, 0, 1000, 0, 10, true, false, false, false},
       {1000, 2000, 1000, 2000, 750, 11, false, false, false, false},
       {2000, 3000, 2000, 3000, 1800, 12, false, false, false, false}});

  const auto snapshot = scheduler.acquireSnapshot();
  expect(snapshot.valid && Scheduler::size(snapshot) == 3,
         "callback acquires one fixed scheduler snapshot");
  expect(Scheduler::findSection(snapshot, 1500) == 1,
         "snapshot helper finds current section");
  scheduler.publish(
      {{0, 500, 0, 500, 0, 20, true, false, false, false}});
  expect(Scheduler::size(snapshot) == 3 &&
             Scheduler::findSection(snapshot, 1500) == 1,
         "acquired snapshot remains coherent after a newer publication");

  scheduler.publish(
      {{0, 1000, 0, 1000, 0, 10, true, false, false, false},
       {1000, 2000, 1000, 2000, 750, 11, false, false, false, false},
       {2000, 3000, 2000, 3000, 1800, 12, false, false, false, false}});
  const auto transitionSnapshot = scheduler.acquireSnapshot();
  expect(transitionSnapshot.generation > snapshot.generation,
         "scheduler snapshots carry monotonically increasing generations");

  expect(scheduler.findSection(1500) == 1, "find current section");
  expect(scheduler.findGuideTrigger(750, 100) == 1,
         "guide trigger resolves without scanning arrangement strings");
  Scheduler::Section section;
  expect(scheduler.getSection(1, section) && section.guideTriggerId == 11,
         "section snapshot carries pre-resolved guide data");

  auto decision =
      Scheduler::evaluate(transitionSnapshot, 900, 200, true, -1, -1);
  expect(!decision.transition && !decision.stop,
         "custom arrangement remains continuous at section boundary");
  expect(decision.transitionOffsetFrames == -1,
         "non-transition keeps an invalid block offset");

  scheduler.publish(
      {{0, 1000, 0, 1000, 0, 10, true, false, false, false},
       {1000, 1500, 4000, 4500, 1000, 11, true, true, false, false},
       {1500, 2500, 1000, 2000, 1500, 12, true, false, false, false}});
  const auto insertedClipSnapshot = scheduler.acquireSnapshot();
  decision =
      Scheduler::evaluate(insertedClipSnapshot, 900, 200, true, -1, -1);
  expect(decision.transition && decision.targetSection == 1 &&
             decision.targetFrame == 1000 &&
             decision.transitionOffsetFrames == 100,
         "custom arrange enters inserted source clip at exact boundary");
  decision =
      Scheduler::evaluate(insertedClipSnapshot, 1400, 200, true, -1, -1);
  expect(decision.transition && decision.targetSection == 2 &&
             decision.targetFrame == 1500 &&
             decision.transitionOffsetFrames == 100,
         "custom arrange exits inserted source clip at exact boundary");

  scheduler.publish(
      {{0, 1000, 0, 1000, 0, 10, true, false, false, false},
       {1000, 2000, 1000, 2000, 750, 11, false, false, false, false},
       {2000, 3000, 2000, 3000, 1800, 12, false, false, false, false}});
  const auto restoredTransitionSnapshot = scheduler.acquireSnapshot();

  decision =
      Scheduler::evaluate(restoredTransitionSnapshot, 900, 200, false, 2, -1);
  expect(decision.transition && decision.targetSection == 2 &&
             decision.targetFrame == 2000 &&
             decision.transitionOffsetFrames == 100,
         "pending jump executes at section boundary");

  decision =
      Scheduler::evaluate(transitionSnapshot, 2900, 200, true, -1, -1);
  expect(!decision.transition && !decision.stop,
         "custom arrangement does not stop at final section marker");

  decision =
      Scheduler::evaluate(transitionSnapshot, 1900, 200, false, -1, 1);
  expect(decision.transition && decision.targetSection == 1 &&
             decision.targetFrame == 1000 &&
             decision.transitionOffsetFrames == 100,
         "active section loop wraps to its start");

  decision =
      Scheduler::evaluate(transitionSnapshot, 900, 200, false, 1, 1);
  expect(decision.transition && decision.targetSection == 1 &&
             decision.targetFrame == 1000 &&
             decision.transitionOffsetFrames == 100,
         "loop requested outside its range enters at current boundary");

  decision =
      Scheduler::evaluate(transitionSnapshot, 512, 512, false, 2, -1);
  expect(decision.transition && decision.targetSection == 2 &&
             decision.transitionOffsetFrames == 488,
         "pending jump exposes exact offset for boundary 1000 in block 512");
  const auto positionAfterSplitBlock =
      decision.targetFrame + (512 - decision.transitionOffsetFrames);
  expect(positionAfterSplitBlock == 2024,
         "split transition clock advances only the target suffix");

  decision =
      Scheduler::evaluate(transitionSnapshot, 488, 512, false, 2, -1);
  expect(decision.transition && decision.targetSection == 2 &&
             decision.transitionOffsetFrames == 512,
         "transition at the block end reports the full block offset");

  decision =
      Scheduler::evaluate(transitionSnapshot, 512, 512, false, -1, 0);
  expect(decision.transition && decision.targetSection == 0 &&
             decision.targetFrame == 0 &&
             decision.transitionOffsetFrames == 488,
         "loop exposes exact offset for boundary 1000 in block 512");

  scheduler.publish(
      {{0, 1000, 0, 1000, 0, 10, true, false, false, false},
       {1000, 2000, 1000, 2000, 750, 11, false, false, false, true},
       {2000, 3000, 2000, 3000, 1800, 12, false, false, false, false}});
  const auto skipSnapshot = scheduler.acquireSnapshot();
  decision = Scheduler::evaluate(skipSnapshot, 1000, 100, true, -1, -1);
  expect(decision.transition && decision.skippedSection &&
             decision.targetSection == 2 && decision.targetFrame == 2000 &&
             decision.transitionOffsetFrames == 0,
         "skip section jumps immediately to next section");
}

void testSectionSchedulerLoopEscape() {
  using Scheduler = playworship::audio::SectionScheduler;
  Scheduler scheduler;
  scheduler.publish(
      {{0, 1000, 0, 1000, 0, 10, false, false, false, false},
       {1000, 2000, 1000, 2000, 0, 11, false, false, false, false},
       {2000, 3000, 2000, 3000, 0, 12, false, false, false, false},
       {3000, 4000, 3000, 4000, 0, 13, false, false, false, false}});
  const auto snapshot = scheduler.acquireSnapshot();

  // Block fully inside the loop, no pending jump → stay in loop, no
  // transition.
  auto decision = Scheduler::evaluate(snapshot, 1500, 100, false, -1, 1);
  expect(!decision.transition && decision.currentSection == 1,
         "loop without pending jump keeps playhead inside section");

  // Block fully inside the loop WITH a pending jump to a different section:
  // the pending jump escapes the loop at the next loop boundary, landing in
  // the target section. The transition offset is clamped to blockFrames when
  // the block ends before the loop boundary, signalling that the transition
  // will fire on the next block that crosses the loop end.
  decision = Scheduler::evaluate(snapshot, 1500, 100, false, 3, 1);
  expect(decision.transition && decision.currentSection == 1 &&
             decision.targetSection == 3 && decision.targetFrame == 3000 &&
             decision.transitionOffsetFrames == 100,
         "pending jump from inside loop schedules escape at loop boundary");

  // Pending jump that targets the loop section itself is a no-op while the
  // playhead is already inside the loop. We stay in the loop without
  // emitting a transition (consuming the jump here would be redundant).
  decision = Scheduler::evaluate(snapshot, 1500, 100, false, 1, 1);
  expect(!decision.transition && decision.currentSection == 1,
         "pending jump targeting the loop section is a no-op inside the loop");

  // Block that already crosses the loop end with a pending jump to another
  // section: the loop wrap wins on the boundary, the pending jump is queued
  // for the next boundary that we just wrapped past.
  decision = Scheduler::evaluate(snapshot, 1900, 200, false, 3, 1);
  expect(decision.transition && decision.targetSection == 1 &&
             decision.targetFrame == 1000 && decision.transitionOffsetFrames == 100,
         "loop wrap still wins on the boundary block, pending jump deferred");

  // Block that crosses the loop end with a pending jump still wins as a
  // wrap-around: the loop is the dominant intent on the boundary.
  decision = Scheduler::evaluate(snapshot, 1900, 200, false, 3, 1);
  expect(decision.transition && decision.targetSection == 1 &&
             decision.targetFrame == 1000 && decision.transitionOffsetFrames == 100,
         "loop wrap still beats pending jump on the boundary block");
}

std::vector<playworship::audio::SectionScheduler::Section>
makeSchedulerStressPattern(const int marker, const int count) {
  using Scheduler = playworship::audio::SectionScheduler;
  std::vector<Scheduler::Section> sections;
  sections.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    const auto start =
        static_cast<Scheduler::Frame>(marker) + (i * 100);
    sections.push_back(
        {start,
         start + 90,
         start + 10,
         start + 80,
         start + 5,
         marker + i,
         (i % 2) == 0,
         (i % 3) == 0,
         (i % 5) == 0,
         (i % 7) == 0});
  }
  return sections;
}

bool schedulerStressSnapshotMatches(
    const playworship::audio::SectionScheduler::Snapshot &snapshot,
    const int marker, const int count) {
  using Scheduler = playworship::audio::SectionScheduler;
  if (!snapshot.valid || Scheduler::size(snapshot) != count)
    return false;
  for (int i = 0; i < count; ++i) {
    const auto start =
        static_cast<Scheduler::Frame>(marker) + (i * 100);
    const auto &section = snapshot.sections[static_cast<std::size_t>(i)];
    if (section.startFrame != start || section.endFrame != start + 90 ||
        section.sourceStartFrame != start + 10 ||
        section.sourceEndFrame != start + 80 ||
        section.guideTriggerFrame != start + 5 ||
        section.guideTriggerId != marker + i ||
        section.triggerGuideOnEntry != ((i % 2) == 0) ||
        section.insertedClip != ((i % 3) == 0) ||
        section.muteInstruments != ((i % 5) == 0) ||
        section.skipPlayback != ((i % 7) == 0)) {
      return false;
    }
  }
  return true;
}

void testSectionSchedulerConcurrentReadersSeeCoherentSnapshots() {
  using Scheduler = playworship::audio::SectionScheduler;
  constexpr int markerA = 10000;
  constexpr int markerB = 50000;
  constexpr int countA = 64;
  constexpr int countB = 128;

  Scheduler scheduler;
  const auto patternA = makeSchedulerStressPattern(markerA, countA);
  const auto patternB = makeSchedulerStressPattern(markerB, countB);
  scheduler.publish(patternA);

  std::atomic<bool> start{false};
  std::atomic<bool> stop{false};
  std::atomic<bool> failed{false};
  std::atomic<int> validReads{0};
  std::atomic<int> readersReady{0};
  const auto reader = [&] {
    while (!start.load(std::memory_order_acquire))
      std::this_thread::yield();
    readersReady.fetch_add(1, std::memory_order_release);
    while (!stop.load(std::memory_order_acquire)) {
      const auto snapshot = scheduler.acquireSnapshot();
      if (!snapshot.valid)
        continue;
      validReads.fetch_add(1, std::memory_order_relaxed);
      const bool matchesA =
          schedulerStressSnapshotMatches(snapshot, markerA, countA);
      const bool matchesB =
          schedulerStressSnapshotMatches(snapshot, markerB, countB);
      if (!matchesA && !matchesB) {
        failed.store(true, std::memory_order_release);
        return;
      }
    }
  };

  std::thread firstReader(reader);
  std::thread secondReader(reader);
  start.store(true, std::memory_order_release);
  while (readersReady.load(std::memory_order_acquire) != 2)
    std::this_thread::yield();
  for (int publication = 0; publication < 2000; ++publication)
    scheduler.publish((publication & 1) == 0 ? patternB : patternA);
  stop.store(true, std::memory_order_release);
  firstReader.join();
  secondReader.join();

  expect(!failed.load(std::memory_order_acquire),
         "concurrent readers never observe a mixed scheduler generation");
  expect(validReads.load(std::memory_order_relaxed) > 0,
         "concurrent readers acquire valid scheduler snapshots");
}

void testSongTransitionEnvelope() {
  constexpr double sampleRate = 48000.0;
  constexpr double fadeOutSeconds = 0.10;
  constexpr double fadeInSeconds = 0.15;

  float gain = 1.0f;
  float previousEnd = gain;
  int renderedSamples = 0;
  while (gain > 0.0f) {
    const int blockSize = renderedSamples == 0 ? 127 : 511;
    const auto ramp = playworship::audio::advanceSongTransitionGain(
        gain, 0.0f, blockSize, sampleRate, fadeOutSeconds);
    expect(std::abs(ramp.startGain - previousEnd) < 1.0e-6f,
           "fade-out remains continuous across blocks");
    expect(ramp.endGain <= ramp.startGain,
           "fade-out gain is monotonic");
    gain = ramp.endGain;
    previousEnd = ramp.endGain;
    renderedSamples += blockSize;
  }
  expect(gain == 0.0f, "fade-out reaches exact silence");
  expect(renderedSamples >= static_cast<int>(sampleRate * fadeOutSeconds),
         "fade-out duration is sample based");

  previousEnd = gain;
  while (gain < 1.0f) {
    const auto ramp = playworship::audio::advanceSongTransitionGain(
        gain, 1.0f, 257, sampleRate, fadeInSeconds);
    expect(std::abs(ramp.startGain - previousEnd) < 1.0e-6f,
           "fade-in remains continuous across blocks");
    expect(ramp.endGain >= ramp.startGain, "fade-in gain is monotonic");
    gain = ramp.endGain;
    previousEnd = ramp.endGain;
  }
  expect(gain == 1.0f, "fade-in reaches exact unity");

  const auto immediate = playworship::audio::advanceSongTransitionGain(
      0.5f, 0.0f, 64, 0.0, fadeOutSeconds);
  expect(immediate.reachedTarget && immediate.endGain == 0.0f,
         "invalid sample rate resolves safely to target");
}

} // namespace

int main() {
  {
    using Scheduler = playworship::audio::SectionScheduler;
    Scheduler scheduler;
    constexpr auto sectionStart = 232 * 48000;
    constexpr auto songEnd = 255 * 48000;
    scheduler.publish({{sectionStart, songEnd, sectionStart, songEnd}});
    const auto snapshot = scheduler.acquireSnapshot();
    Scheduler::Frame source = -1;
    expect(Scheduler::mapFrameToSourceFrame(snapshot, 0, songEnd, songEnd, source)
               && source == 0,
           "a single late section must preserve the beginning of the source");
    const auto decision = Scheduler::evaluate(snapshot, 0, 1920, true, -1, -1);
    expect(!decision.transition && !decision.stop,
           "playback before the only section must continue from zero");
  }
  testTransportClock();
  testTransportClockAndArrangeBoundaries();
  testSectionScheduler();
  testSectionSchedulerLoopEscape();
  testSectionSchedulerConcurrentReadersSeeCoherentSnapshots();
  testSongTransitionEnvelope();
  std::cout << "audio engine core tests passed\n";
  return 0;
}
