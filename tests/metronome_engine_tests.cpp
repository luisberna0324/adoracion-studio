#include "../src/MetronomeEngine.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

struct MetronomeEngineTestPeer {
  static int activeSubdivision(const MetronomeEngine &engine) noexcept {
    return engine.activeSubdivision;
  }

  static double phase(const MetronomeEngine &engine) noexcept {
    return engine.phase;
  }

  static double subPhase(const MetronomeEngine &engine) noexcept {
    return engine.subPhase;
  }

  static double activeBpm(const MetronomeEngine &engine) noexcept {
    return engine.activeBpm;
  }

  static int beatIndex(const MetronomeEngine &engine) noexcept {
    return engine.beatIndex;
  }

  static int subIndex(const MetronomeEngine &engine) noexcept {
    return engine.subIndex;
  }

  static uint64_t requestedResetGeneration(
      const MetronomeEngine &engine) noexcept {
    return engine.requestedResetGeneration.load(std::memory_order_acquire);
  }

  static uint64_t appliedResetGeneration(
      const MetronomeEngine &engine) noexcept {
    return engine.appliedResetGeneration;
  }
};

namespace {

void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void expectNear(const double actual, const double expected,
                const char *message) {
  expect(std::abs(actual - expected) < 1.0e-9, message);
}

void testSubdivisionPublicationPersistsUntilBeatBoundary() {
  MetronomeEngine engine;
  engine.setSubdivision(1);
  engine.prepare(1000.0);
  engine.setEnabled(true);
  engine.syncPhaseForBeatPosition(0.0, 60.0);

  expect(MetronomeEngineTestPeer::activeSubdivision(engine) == 1,
         "subdivision starts from the prepared target");

  engine.setSubdivision(4);
  juce::AudioBuffer<float> beforeBoundary(2, 999);
  beforeBoundary.clear();
  engine.process(beforeBoundary);
  expect(MetronomeEngineTestPeer::activeSubdivision(engine) == 1,
         "subdivision remains stable inside the current beat");
  expect(engine.getSubdivision() == 4,
         "requested subdivision remains published before the boundary");

  juce::AudioBuffer<float> boundary(2, 1);
  boundary.clear();
  engine.process(boundary);
  expect(MetronomeEngineTestPeer::activeSubdivision(engine) == 4,
         "requested subdivision becomes active on the beat boundary");
  expect(engine.getSubdivision() == 4,
         "audio processing never clears the persistent request");
}

void testLatestSubdivisionRequestWinsWithoutAConsumableSentinel() {
  MetronomeEngine engine;
  engine.setSubdivision(1);
  engine.prepare(1000.0);
  engine.setEnabled(true);
  engine.syncPhaseForBeatPosition(0.0, 60.0);

  engine.setSubdivision(4);
  engine.setSubdivision(1);

  juce::AudioBuffer<float> oneBeat(2, 1000);
  oneBeat.clear();
  engine.process(oneBeat);

  expect(engine.getSubdivision() == 1,
         "the latest control value remains published after processing");
  expect(MetronomeEngineTestPeer::activeSubdivision(engine) == 1,
         "a superseded subdivision cannot reappear at the boundary");
}

void testResetGenerationIsConsumedByCoherentBeatSync() {
  MetronomeEngine engine;
  engine.setSubdivision(2);
  engine.prepare(1000.0);
  engine.requestReset();
  const auto requested =
      MetronomeEngineTestPeer::requestedResetGeneration(engine);

  engine.syncPhaseForBeatPosition(3.25, 90.0);

  expect(MetronomeEngineTestPeer::appliedResetGeneration(engine) == requested,
         "audio sync consumes the complete reset generation");
  expect(MetronomeEngineTestPeer::activeSubdivision(engine) == 2,
         "reset applies the latest persistent subdivision");
  expect(MetronomeEngineTestPeer::beatIndex(engine) == 3,
         "beat sync publishes one coherent bar position");
  expect(MetronomeEngineTestPeer::subIndex(engine) == 0,
         "subdivision index derives from the same beat position");
  expectNear(MetronomeEngineTestPeer::phase(engine), 0.25,
             "beat phase derives from the requested musical position");
  expectNear(MetronomeEngineTestPeer::subPhase(engine), 0.5,
             "subdivision phase derives from the same musical position");
  expectNear(MetronomeEngineTestPeer::activeBpm(engine), 90.0,
             "effective BPM is applied with the phase");
}

void testProcessDoesNotConsumeControlResetBetweenSyncAndRender() {
  MetronomeEngine engine;
  engine.prepare(1000.0);
  engine.setEnabled(true);
  engine.syncPhaseForBeatPosition(2.5, 60.0);
  const auto appliedBefore =
      MetronomeEngineTestPeer::appliedResetGeneration(engine);

  engine.requestReset();
  juce::AudioBuffer<float> output(2, 10);
  output.clear();
  engine.process(output);

  expect(MetronomeEngineTestPeer::appliedResetGeneration(engine) ==
             appliedBefore,
         "render does not consume a reset published after beat sync");
  expect(MetronomeEngineTestPeer::beatIndex(engine) == 2,
         "render preserves the synced bar position until the next sync");

  engine.syncPhaseForBeatPosition(1.25, 90.0);
  expect(MetronomeEngineTestPeer::appliedResetGeneration(engine) !=
             appliedBefore,
         "the following beat sync consumes the pending reset");
  expect(MetronomeEngineTestPeer::beatIndex(engine) == 1,
         "reset consumption is coherent with the new beat sync");
}

void testRangeProcessingDoesNotTouchAdjacentSamples() {
  MetronomeEngine engine;
  engine.prepare(48000.0);
  engine.setEnabled(true);
  engine.setVolume(1.0f);
  engine.setDivisionMask(1);
  engine.syncPhaseForBeatPosition(0.0, 120.0);
  engine.triggerImmediateClick();

  juce::AudioBuffer<float> output(2, 64);
  output.clear();
  engine.process(output, 16, 16);

  for (int channel = 0; channel < output.getNumChannels(); ++channel) {
    expect(output.getMagnitude(channel, 0, 16) == 0.0f,
           "range render leaves the prefix untouched");
    expect(output.getMagnitude(channel, 32, 32) == 0.0f,
           "range render leaves the suffix untouched");
    expect(output.getMagnitude(channel, 16, 16) > 0.0f,
           "range render advances and emits the click inside its segment");
  }
}

void testSyncedBeatBoundaryRendersAtBlockStart() {
  MetronomeEngine engine;
  engine.prepare(1000.0);
  engine.setEnabled(true);
  engine.setVolume(1.0f);
  engine.setDivisionMask(1);
  engine.setSubdivision(4);

  // This is the callback pattern that used to drop a beat: transport sync
  // publishes an exact integer beat before rendering the next block.
  engine.syncPhaseForBeatPosition(2.0, 60.0);
  juce::AudioBuffer<float> boundaryBlock(2, 32);
  boundaryBlock.clear();
  engine.process(boundaryBlock);

  expect(MetronomeEngineTestPeer::activeSubdivision(engine) == 4,
         "a callback-aligned beat publishes the pending subdivision");

  for (int channel = 0; channel < boundaryBlock.getNumChannels(); ++channel)
    expect(boundaryBlock.getMagnitude(channel, 0,
                                      boundaryBlock.getNumSamples()) > 0.0f,
           "an exact callback-aligned beat emits its click in that block");

  engine.syncPhaseForBeatPosition(2.25, 60.0);
  juce::AudioBuffer<float> betweenBeats(2, 32);
  betweenBeats.clear();
  engine.process(betweenBeats);
  for (int channel = 0; channel < betweenBeats.getNumChannels(); ++channel)
    expect(betweenBeats.getMagnitude(channel, 0,
                                     betweenBeats.getNumSamples()) == 0.0f,
           "sync inside a beat does not create a false boundary click");
}

} // namespace

int main() {
  testSubdivisionPublicationPersistsUntilBeatBoundary();
  testLatestSubdivisionRequestWinsWithoutAConsumableSentinel();
  testResetGenerationIsConsumedByCoherentBeatSync();
  testProcessDoesNotConsumeControlResetBetweenSyncAndRender();
  testRangeProcessingDoesNotTouchAdjacentSamples();
  testSyncedBeatBoundaryRendersAtBlockStart();
  std::cout << "Metronome engine tests passed\n";
  return 0;
}
