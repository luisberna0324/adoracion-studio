#include "ui/panels/PracticePlaybackClock.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
void expectClock(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void expectClockNear(const double actual, const double expected,
                     const char *message) {
  if (std::abs(actual - expected) > 0.0000001) {
    std::cerr << "FAILED: " << message << " (actual " << actual
              << ", expected " << expected << ")\n";
    std::exit(1);
  }
}

void testClockInterpolatesAudioBlocks() {
  PracticePlaybackClock clock;
  clock.update(10.0, true, 1.0, 8000.0);
  double previous = clock.position();
  int unchangedSourceFrames = 0;
  double previousSource = 10.0;
  for (int frame = 1; frame <= 180; ++frame) {
    const double elapsed = frame / 60.0;
    // A block clock slower than the display repeats its sample on some frames.
    const double source = 10.0 + std::floor((elapsed + 0.000001) / 0.05) * 0.05;
    const double visual = clock.update(source, true, 1.0, 8000.0 + elapsed);
    const double delta = visual - previous;
    expectClock(delta >= 0.007 && delta <= 0.03,
                "block-quantised playback moves every frame without grouped jumps");
    expectClock(std::abs(visual - (10.0 + elapsed)) < 0.08,
                "the visual clock stays close to the authoritative timeline");
    if (source == previousSource) ++unchangedSourceFrames;
    previous = visual;
    previousSource = source;
  }
  expectClock(unchangedSourceFrames > 60,
              "the block-clock scenario exercises repeated source samples");
}

void testClockAbsorbsRemotePacketSetbacks() {
  PracticePlaybackClock clock;
  clock.update(20.0, true, 1.0, 50.0);
  double previous = clock.position();
  for (int frame = 1; frame <= 240; ++frame) {
    const double elapsed = frame / 60.0;
    // Between packets the adapter predicts; a newly received packet is older.
    const double observed = 20.0 + elapsed - (frame % 3 == 0 ? 0.03 : 0.0);
    const double visual = clock.update(observed, true, 1.0, 50.0 + elapsed);
    expectClock(visual >= previous && visual - previous <= 0.03,
                "small remote setbacks neither reverse playback nor cause a jump");
    expectClock(std::abs(visual - (20.0 + elapsed)) < 0.06,
                "packet correction does not accumulate timeline drift");
    previous = visual;
  }
}

void testClockTransportDiscontinuities() {
  PracticePlaybackClock clock;
  clock.update(10.0, true, 1.0, 100.0);
  clock.update(10.1, true, 1.0, 100.1);
  expectClockNear(clock.update(22.0, true, 1.0, 100.116), 22.0,
                  "an intentional forward seek is displayed immediately");
  expectClockNear(clock.update(3.0, true, 1.0, 100.132), 3.0,
                  "a backward loop wrap is displayed immediately");
  expectClockNear(clock.update(7.25, false, 1.0, 100.148), 7.25,
                  "pause preserves its exact source position");
  for (int frame = 1; frame <= 120; ++frame)
    expectClockNear(clock.update(7.25, false, 2.0, 100.148 + frame * 0.03),
                    7.25, "a paused visual clock never creeps");
  expectClockNear(clock.update(0.0, false, 1.0, 110.0), 0.0,
                  "Stop resets the displayed position immediately");
  expectClockNear(clock.update(0.0, true, 1.0, 110.02), 0.0,
                  "resuming establishes a fresh frame-time anchor");
}

void testClockFrameDeltasAndTempo() {
  PracticePlaybackClock clock;
  double timestamp = 900000.0;
  double source = 5.0;
  clock.update(source, true, 2.0, timestamp);
  const std::vector<double> frameDeltas{0.004, 0.035, 0.006, 0.019, 0.026};
  for (int frame = 0; frame < 120; ++frame) {
    const double rate = frame < 60 ? 2.0 : 0.75;
    const double delta = frameDeltas[static_cast<size_t>(frame) % frameDeltas.size()];
    source += delta * rate;
    timestamp += delta;
    expectClockNear(clock.update(source, true, rate, timestamp), source,
                    "jittered frames use elapsed time and the current playback rate");
  }
  const double before = clock.position();
  expectClockNear(clock.update(source, true, 1.0, timestamp - 10.0), before,
                  "a nonmonotonic frame timestamp cannot run the cursor backwards");
}

void testClockStopsExtrapolatingStalledSources() {
  PracticePlaybackClock clock;
  clock.update(15.0, true, 1.0, 25.0);
  double atStall = 0.0;
  for (int frame = 1; frame <= 600; ++frame) {
    const double visual = clock.update(15.0, true, 1.0, 25.0 + frame / 60.0);
    expectClock(visual >= 15.0 && visual <= 15.25,
                "unchanged source prediction stays bounded to 250 milliseconds");
    if (frame == 16) atStall = visual;
    if (frame > 16)
      expectClockNear(visual, atStall,
                      "a stalled source stops advancing instead of extrapolating forever");
  }
  expectClock(atStall > 15.0,
              "the clock interpolates briefly before declaring a genuine stall");
}

void testClockFirstFrameAndFiniteInputs() {
  PracticePlaybackClock clock;
  expectClock(!clock.isValid(), "a fresh clock lets its owner use the source position");
  expectClockNear(clock.update(12.0, true, 1.0, 700000.0), 12.0,
                  "the first frame anchors to position rather than the timestamp epoch");
  clock.clear();
  expectClock(!clock.isValid(), "clear invalidates the old visual origin");
  expectClockNear(clock.position(), 0.0, "clear removes the previous song's visual state");
  expectClockNear(clock.update(4.0, false, 1.0, 1200000.0), 4.0,
                  "a first paused frame keeps its supplied position");
  const double nan = std::numeric_limits<double>::quiet_NaN();
  clock.reset(std::numeric_limits<double>::infinity(), nan);
  expectClock(std::isfinite(clock.position()), "invalid reset inputs remain finite");
  expectClock(std::isfinite(clock.update(nan, true,
                                       std::numeric_limits<double>::infinity(), nan)),
              "invalid position, tempo and timestamp inputs cannot poison rendering");
}
} // namespace

void runPracticePlaybackClockTests() {
  testClockInterpolatesAudioBlocks();
  testClockAbsorbsRemotePacketSetbacks();
  testClockTransportDiscontinuities();
  testClockFrameDeltasAndTempo();
  testClockStopsExtrapolatingStalledSources();
  testClockFirstFrameAndFiniteInputs();
}
