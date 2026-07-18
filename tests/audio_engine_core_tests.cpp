#include "../src/audio/SectionScheduler.h"
#include "../src/audio/SongTransitionEnvelope.h"
#include "../src/audio/TransportClock.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
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

void testSectionScheduler() {
  using Scheduler = playworship::audio::SectionScheduler;
  Scheduler scheduler;
  scheduler.publish(
      {{0, 1000, 0, 1000, 0, 10, true, false, false, false},
       {1000, 2000, 1000, 2000, 750, 11, false, false, false, false},
       {2000, 3000, 2000, 3000, 1800, 12, false, false, false, false}});

  expect(scheduler.findSection(1500) == 1, "find current section");
  expect(scheduler.findGuideTrigger(750, 100) == 1,
         "guide trigger resolves without scanning arrangement strings");
  Scheduler::Section section;
  expect(scheduler.getSection(1, section) && section.guideTriggerId == 11,
         "section snapshot carries pre-resolved guide data");

  auto decision = scheduler.evaluate(900, 200, true, -1, -1);
  expect(!decision.transition && !decision.stop,
         "custom arrangement remains continuous at section boundary");

  decision = scheduler.evaluate(900, 200, false, 2, -1);
  expect(decision.transition && decision.targetSection == 2 &&
             decision.targetFrame == 2000,
         "pending jump executes at section boundary");

  decision = scheduler.evaluate(2900, 200, true, -1, -1);
  expect(!decision.transition && !decision.stop,
         "custom arrangement does not stop at final section marker");

  decision = scheduler.evaluate(1900, 200, false, -1, 1);
  expect(decision.transition && decision.targetSection == 1 &&
             decision.targetFrame == 1000,
         "active section loop wraps to its start");

  decision = scheduler.evaluate(900, 200, false, 1, 1);
  expect(decision.transition && decision.targetSection == 1 &&
             decision.targetFrame == 1000,
         "loop requested outside its range enters at current boundary");

  scheduler.publish(
      {{0, 1000, 0, 1000, 0, 10, true, false, false, false},
       {1000, 2000, 1000, 2000, 750, 11, false, false, false, true},
       {2000, 3000, 2000, 3000, 1800, 12, false, false, false, false}});
  decision = scheduler.evaluate(1000, 100, true, -1, -1);
  expect(decision.transition && decision.skippedSection &&
             decision.targetSection == 2 && decision.targetFrame == 2000,
         "skip section jumps immediately to next section");
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
  testTransportClock();
  testSectionScheduler();
  testSongTransitionEnvelope();
  std::cout << "audio engine core tests passed\n";
  return 0;
}
