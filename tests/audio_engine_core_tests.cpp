#include "../src/audio/SectionScheduler.h"
#include "../src/audio/TransportClock.h"

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
      {{0, 1000, 0, 1000, 0, 10, true, false},
       {1000, 2000, 1000, 2000, 750, 11, false, false},
       {2000, 3000, 2000, 3000, 1800, 12, false, false}});

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
}

} // namespace

int main() {
  testTransportClock();
  testSectionScheduler();
  std::cout << "audio engine core tests passed\n";
  return 0;
}
