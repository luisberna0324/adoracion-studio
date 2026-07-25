#include "../src/audio/BungeeProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

struct BungeeProcessorTestPeer {
  static void failNextVoiceBuilds(BungeeProcessor &processor,
                                  const int failureCount) {
    processor.setForcedVoiceBuildFailuresForTests(failureCount);
  }
};

namespace {
void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

int measureOutputLatencySamples(const double tempoRatio) {
  constexpr int outputFrames = 512;
  constexpr double sampleRate = 48000.0;
  BungeeProcessor processor;
  expect(processor.prepare(sampleRate, outputFrames, 2),
         "latency test processor prepares");
  processor.setPitchSemiTones(3);
  processor.setTempoRatio(tempoRatio);

  juce::AudioBuffer<float> input(2, (outputFrames * 2) + 8);
  juce::AudioBuffer<float> output(2, outputFrames);
  double phase = 0.0;
  for (int block = 0; block < 120; ++block) {
    const int inputFrames = processor.requiredInputFrames(outputFrames);
    for (int sample = 0; sample < inputFrames; ++sample) {
      const float value = static_cast<float>(std::sin(phase));
      phase += 2.0 * 3.14159265358979323846 * 440.0 / sampleRate;
      input.setSample(0, sample, value);
      input.setSample(1, sample, value);
    }
    output.clear();
    processor.process(input, inputFrames, output, outputFrames);
  }

  const int latency = processor.getOutputLatencySamples();
  expect(latency > 0, "Bungee reports a settled positive output latency");
  expect(latency == processor.getLatencySamples(),
         "legacy latency getter preserves the output-sample contract");
  return latency;
}

float renderSignal(BungeeProcessor &processor, const int blockCount,
                   double &phase) {
  constexpr int outputFrames = 512;
  constexpr double sampleRate = 48000.0;
  juce::AudioBuffer<float> input(2, (outputFrames * 2) + 8);
  juce::AudioBuffer<float> output(2, outputFrames);
  float peak = 0.0f;
  for (int block = 0; block < blockCount; ++block) {
    const int inputFrames = processor.requiredInputFrames(outputFrames);
    for (int sample = 0; sample < inputFrames; ++sample) {
      const float value = static_cast<float>(std::sin(phase));
      phase += 2.0 * 3.14159265358979323846 * 440.0 / sampleRate;
      input.setSample(0, sample, value);
      input.setSample(1, sample, value);
    }
    output.clear();
    processor.process(input, inputFrames, output, outputFrames);
    peak = std::max(peak, output.getMagnitude(0, outputFrames));
  }
  return peak;
}

float renderSilence(BungeeProcessor &processor, const int blockCount,
                    const int firstInputFrames = 0) {
  constexpr int outputFrames = 512;
  juce::AudioBuffer<float> input(2, (outputFrames * 2) + 8);
  juce::AudioBuffer<float> output(2, outputFrames);
  input.clear();
  float peak = 0.0f;
  for (int block = 0; block < blockCount; ++block) {
    const int inputFrames =
        block == 0 && firstInputFrames > 0
            ? firstInputFrames
            : processor.requiredInputFrames(outputFrames);
    output.clear();
    processor.process(input, inputFrames, output, outputFrames);
    peak = std::max(peak, output.getMagnitude(0, outputFrames));
  }
  return peak;
}

void testRealtimeResetVoiceLifecycle() {
  constexpr int outputFrames = 512;
  BungeeProcessor processor;
  expect(processor.prepare(48000.0, outputFrames, 2),
         "double-slot reset processor prepares");
  expect(processor.isPrepared(), "prepare publishes an active voice");
  expect(processor.hasReadyVoice(), "prepare publishes a clean spare voice");
  expect(processor.getOutputLatencySamples() > 0,
         "prepare pre-warms the active voice to a stable latency");
  expect(!processor.hasPendingReset(),
         "prepare starts without a pending reset");
  expect(processor.getResetSuccessCount() == 0,
         "prepare clears successful reset diagnostics");
  expect(processor.getResetMissCount() == 0,
         "prepare clears reset miss diagnostics");
  expect(processor.getServiceFailureCount() == 0,
         "prepare clears service failure diagnostics");

  const int initialSlot = processor.getActiveVoiceSlotForDiagnostics();
  const uint64_t initialGeneration = processor.getActiveVoiceGeneration();
  expect(initialSlot >= 0, "active voice slot is available for diagnostics");
  expect(initialGeneration > 0, "active voice has a nonzero generation");

  processor.setPitchSemiTones(3);
  processor.setTempoRatio(1.0);
  double phase = 0.0;
  expect(renderSignal(processor, 100, phase) > 1.0e-4f,
         "active voice accumulates audible history");
  expect(processor.getOutputLatencySamples() > 0,
         "used active voice publishes latency");
  expect(renderSilence(processor, 1) > 1.0e-4f,
         "used voice has an audible tail before reset");
  renderSignal(processor, 12, phase);

  // Leave a fractional planner remainder that would make the next block use
  // 385 frames unless reset() clears it.
  processor.setTempoRatio(0.751);
  expect(processor.requiredInputFrames(outputFrames) == 384,
         "fractional reset test primes a nonzero input remainder");
  const int latencyBeforeFirstReset = processor.getOutputLatencySamples();

  processor.reset();
  expect(processor.getResetSuccessCount() == 1,
         "first reset swaps to the prepared voice");
  expect(processor.getResetMissCount() == 0,
         "first reset does not report a miss");
  expect(!processor.hasReadyVoice(),
         "old active voice is retired until serviced");
  expect(!processor.hasPendingReset(),
         "a ready spare completes the first reset immediately");
  expect(processor.getActiveVoiceSlotForDiagnostics() != initialSlot,
         "reset changes the active stable slot");
  expect(std::abs(processor.getOutputLatencySamples() -
                  latencyBeforeFirstReset) <=
             std::max(4, latencyBeforeFirstReset / 5),
         "fresh voice publishes its pre-warmed stable output latency");
  const int firstFramesAfterReset =
      processor.requiredInputFrames(outputFrames);
  expect(firstFramesAfterReset == 384,
         "reset clears fractional input-frame remainder");

  const float firstFreshSilence =
      renderSilence(processor, 24, firstFramesAfterReset);
  expect(firstFreshSilence <= 1.0e-7f,
         "first pre-warmed voice contains no retired audio history");

  expect(processor.servicePendingRebuild(),
         "non-RT service rebuilds the retired voice");
  expect(processor.hasReadyVoice(),
         "serviced retired voice is published as ready");

  expect(renderSignal(processor, 100, phase) > 1.0e-4f,
         "second active voice accumulates audible history");
  expect(renderSilence(processor, 1) > 1.0e-4f,
         "second used voice has an audible tail before reset");
  renderSignal(processor, 12, phase);

  processor.reset();
  expect(processor.getResetSuccessCount() == 2,
         "second serviced reset swaps successfully");
  expect(processor.getResetMissCount() == 0,
         "successful second reset has no reset misses");
  expect(processor.getActiveVoiceSlotForDiagnostics() == initialSlot,
         "second reset reuses the rebuilt stable slot");
  expect(processor.getActiveVoiceGeneration() > initialGeneration,
         "rebuilt slot advances generation to avoid ABA");
  expect(std::abs(processor.getOutputLatencySamples() -
                  latencyBeforeFirstReset) <=
             std::max(4, latencyBeforeFirstReset / 5),
         "second fresh voice retains pre-warmed stable latency");
  expect(renderSilence(processor, 24) <= 1.0e-7f,
         "second fresh voice contains no retired audio history");

  const int activeSlotBeforePendingReset =
      processor.getActiveVoiceSlotForDiagnostics();
  const uint64_t generationBeforePendingReset =
      processor.getActiveVoiceGeneration();
  processor.reset();
  expect(processor.getResetSuccessCount() == 2,
         "reset without a ready spare does not swap immediately");
  expect(processor.getResetMissCount() == 1,
         "reset without a ready spare increments the miss counter");
  expect(processor.hasPendingReset(),
         "reset without a spare remains pending");
  expect(processor.getActiveVoiceSlotForDiagnostics() ==
                 activeSlotBeforePendingReset &&
             processor.getActiveVoiceGeneration() ==
                 generationBeforePendingReset,
         "pending reset preserves the active token until replacement");
  expect(processor.requiredInputFrames(outputFrames) == 384,
         "a missed reset still clears fractional input planning");
  expect(renderSignal(processor, 8, phase) <= 1.0e-7f,
         "pending reset emits silence instead of stale active history");

  expect(processor.servicePendingRebuild(),
         "service prepares a replacement for a pending reset");
  expect(processor.hasReadyVoice(),
         "pending reset replacement is published as ready");
  expect(processor.tryResolvePendingReset(),
         "audio preflight activates the rebuilt replacement");
  expect(!processor.hasPendingReset(),
         "preflight completes the pending reset before grouped rendering");
  expect(renderSilence(processor, 1) <= 1.0e-7f,
         "the resolved voice renders without residual audio");
  expect(processor.getResetSuccessCount() == 3,
         "deferred swap is counted as a successful reset");
  expect(processor.getActiveVoiceSlotForDiagnostics() !=
             activeSlotBeforePendingReset,
         "deferred reset activates the rebuilt spare");
  expect(processor.getOutputLatencySamples() > 0,
         "deferred reset publishes pre-warmed latency immediately");
}

void testFailedPrepareRecovery() {
  constexpr int outputFrames = 512;
  BungeeProcessor processor;
  BungeeProcessorTestPeer::failNextVoiceBuilds(processor, 1);
  expect(!processor.prepare(48000.0, outputFrames, 2),
         "injected voice construction failure makes prepare fail");
  expect(!processor.isPrepared(),
         "failed prepare does not publish a partial active voice");
  expect(!processor.hasReadyVoice(),
         "failed prepare leaves recoverable empty slots");

  expect(processor.servicePendingRebuild(),
         "service reconstructs the first empty slot after prepare failure");
  expect(processor.hasReadyVoice(),
         "recovered empty slot is published as ready");

  juce::AudioBuffer<float> input(2, (outputFrames * 2) + 8);
  juce::AudioBuffer<float> output(2, outputFrames);
  input.clear();
  output.clear();
  const int inputFrames = processor.requiredInputFrames(outputFrames);
  processor.process(input, inputFrames, output, outputFrames);
  expect(processor.isPrepared(),
         "audio thread activates the first recovered voice");

  expect(processor.servicePendingRebuild(),
         "service reconstructs the second empty slot");
  expect(processor.hasReadyVoice(),
         "prepare recovery restores a complete active/ready pair");
  expect(processor.getServiceFailureCount() == 0,
         "prepare failure is distinct from background service failures");
}

void testServiceFailureBackoff() {
  constexpr int outputFrames = 512;
  BungeeProcessor processor;
  expect(processor.prepare(48000.0, outputFrames, 2),
         "service backoff processor prepares");
  processor.reset();
  expect(!processor.hasReadyVoice(),
         "reset leaves one retired slot for the service");

  BungeeProcessorTestPeer::failNextVoiceBuilds(processor, 2);
  expect(!processor.servicePendingRebuild(),
         "first injected service rebuild fails");
  expect(processor.getServiceFailureCount() == 1,
         "first service failure is counted");
  expect(!processor.servicePendingRebuild(),
         "immediate retry is suppressed by backoff");
  expect(processor.getServiceFailureCount() == 1,
         "suppressed retry does not consume another build attempt");

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  expect(!processor.servicePendingRebuild(),
         "second injected rebuild fails after the first backoff");
  expect(processor.getServiceFailureCount() == 2,
         "persistent service failures accumulate");
  expect(!processor.servicePendingRebuild(),
         "second backoff also suppresses an immediate retry");
  expect(processor.getServiceFailureCount() == 2,
         "second suppressed retry leaves the failure count unchanged");

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  expect(processor.servicePendingRebuild(),
         "service recovers after the exponential backoff");
  expect(processor.hasReadyVoice(),
         "successful retry publishes the rebuilt spare");
}
} // namespace

int main() {
  constexpr int outputFrames = 512;
  BungeeProcessor processor;
  expect(processor.prepare(48000.0, outputFrames, 2),
         "Bungee processor prepares");
  processor.setPitchSemiTones(3);
  processor.setTempoRatio(0.75);
  const int inputFrames = processor.requiredInputFrames(outputFrames);
  expect(inputFrames == 384, "tempo ratio controls input consumption");

  BungeeProcessor fractionalTempoProcessor;
  expect(fractionalTempoProcessor.prepare(48000.0, outputFrames, 2),
         "fractional tempo processor prepares");
  fractionalTempoProcessor.setTempoRatio(0.751);
  int accumulatedInputFrames = 0;
  constexpr int planningBlocks = 1000;
  for (int block = 0; block < planningBlocks; ++block)
    accumulatedInputFrames +=
        fractionalTempoProcessor.requiredInputFrames(outputFrames);
  const int expectedInputFrames =
      static_cast<int>(std::floor(outputFrames * 0.751 * planningBlocks));
  expect(accumulatedInputFrames == expectedInputFrames,
         "fractional input consumption does not drift");

  juce::AudioBuffer<float> input(2, 1024);
  juce::AudioBuffer<float> output(2, outputFrames);
  double phase = 0.0;
  bool audible = false;
  for (int block = 0; block < 100; ++block) {
    for (int sample = 0; sample < inputFrames; ++sample) {
      const float value = static_cast<float>(std::sin(phase));
      phase += 2.0 * 3.14159265358979323846 * 440.0 / 48000.0;
      input.setSample(0, sample, value);
      input.setSample(1, sample, value);
    }
    output.clear();
    processor.process(input, inputFrames, output, outputFrames);
    for (int sample = 0; sample < outputFrames; ++sample) {
      const float value = output.getSample(0, sample);
      expect(std::isfinite(value), "Bungee output remains finite");
      audible = audible || std::abs(value) > 1.0e-5f;
    }
  }
  expect(audible, "Bungee eventually produces audible output");
  expect(processor.getLatencySamples() >= 0, "latency is available");

  const int slowLatency = measureOutputLatencySamples(0.75);
  const int unityLatency = measureOutputLatencySamples(1.0);
  const int fastLatency = measureOutputLatencySamples(2.0);
  expect(slowLatency > unityLatency,
         "0.75 ratio converts input latency to a longer output timeline");
  expect(fastLatency < unityLatency,
         "2.0 ratio converts input latency to a shorter output timeline");

  // Converting the public output latencies back to Bungee's input domain
  // should recover approximately the same structural latency at every ratio.
  const double inputDomainLatencies[] = {
      static_cast<double>(slowLatency) * 0.75,
      static_cast<double>(unityLatency),
      static_cast<double>(fastLatency) * 2.0};
  double minInputLatency = std::numeric_limits<double>::max();
  double maxInputLatency = 0.0;
  for (const double latency : inputDomainLatencies) {
    minInputLatency = std::min(minInputLatency, latency);
    maxInputLatency = std::max(maxInputLatency, latency);
  }
  expect(maxInputLatency <= minInputLatency * 1.2,
         "latency conversion is consistent for ratios 0.75, 1.0 and 2.0");

  testRealtimeResetVoiceLifecycle();
  testFailedPrepareRecovery();
  testServiceFailureBackoff();

  std::cout << "bungee processor tests passed\n";
  return 0;
}
