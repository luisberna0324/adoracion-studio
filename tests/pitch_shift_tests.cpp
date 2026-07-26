#include "../src/PitchShiftProcessor.h"
#include "../src/audio/SimpleDelayLine.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

struct PitchShiftProcessorTestPeer {
  static bool hasBackend(const PitchShiftProcessor &processor) {
    return processor.hasBackendForTests();
  }

  static size_t allocatedBufferCapacity(
      const PitchShiftProcessor &processor) {
    return processor.getAllocatedBufferCapacityForTests();
  }
};

namespace {

void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void expectNear(const float actual, const float expected,
                const float tolerance, const char *message) {
  if (std::abs(actual - expected) > tolerance) {
    std::cerr << "FAILED: " << message << " (expected " << expected
              << ", got " << actual << ")\n";
    std::exit(1);
  }
}

void testReleaseResourcesLifecycle() {
  constexpr int blockSize = 512;
  PitchShiftProcessor processor;
  processor.setPitchSemiTones(-4);
  processor.setTempoRatio(0.8);
  processor.setContentType(PitchShiftProcessor::ContentType::Percussive);
  processor.prepare(48000.0, blockSize, 2);

  expect(processor.isPrepared(),
         "SoundTouch release lifecycle processor prepares");
  expect(PitchShiftProcessorTestPeer::hasBackend(processor),
         "prepare constructs the SoundTouch backend");
  expect(PitchShiftProcessorTestPeer::allocatedBufferCapacity(processor) > 0,
         "prepare reserves processing buffers");

  processor.releaseResources();
  expect(!processor.isPrepared(),
         "release marks the SoundTouch processor unprepared");
  expect(!PitchShiftProcessorTestPeer::hasBackend(processor),
         "release destroys the SoundTouch backend");
  expect(PitchShiftProcessorTestPeer::allocatedBufferCapacity(processor) == 0,
         "release returns every scratch and FIFO buffer capacity");
  expect(processor.getNumChannels() == 0,
         "release clears the prepared channel configuration");
  expect(processor.getLatencySamples() == 0,
         "release clears SoundTouch latency state");
  expect(processor.requiredInputFrames(blockSize) == 0,
         "released SoundTouch processor does not plan source frames");
  expect(!processor.tryResolvePendingReset() &&
             !processor.servicePendingRebuild(),
         "released fallback cannot report itself ready");

  juce::AudioBuffer<float> input(2, blockSize);
  juce::AudioBuffer<float> output(2, blockSize);
  input.clear();
  output.clear();
  for (int channel = 0; channel < output.getNumChannels(); ++channel) {
    for (int sample = 0; sample < blockSize; ++sample)
      output.setSample(channel, sample, 1.0f);
  }
  expect(processor.process(input, blockSize, output, blockSize) == 0,
         "released fallback rejects rendering");
  expect(output.getMagnitude(0, blockSize) == 0.0f,
         "released fallback fails closed with deterministic silence");

  input.clear();
  input.setSample(0, 0, 0.5f);
  input.setSample(1, 0, 0.5f);
  processor.process(input);
  expect(input.getSample(0, 0) == 0.5f,
         "released in-place fallback remains a safe bypass");

  processor.reset();
  processor.releaseResources();
  expect(!processor.isPrepared() &&
             PitchShiftProcessorTestPeer::allocatedBufferCapacity(processor) ==
                 0,
         "reset and repeated release are safe while unprepared");

  expect(processor.getPitchSemiTones() == -4 &&
             std::abs(processor.getTempoRatio() - 0.8) < 1.0e-12,
         "release preserves logical pitch and tempo configuration");
  processor.prepare(44100.0, blockSize, 1);
  expect(processor.isPrepared() &&
             PitchShiftProcessorTestPeer::hasBackend(processor),
         "fallback can prepare again after complete release");
  expect(processor.getPitchSemiTones() == -4 &&
             std::abs(processor.getTempoRatio() - 0.8) < 1.0e-12,
         "re-prepare retains logical processing configuration");
  expect(processor.getNumChannels() == 1 &&
             PitchShiftProcessorTestPeer::allocatedBufferCapacity(processor) >
                 0,
         "re-prepare reconstructs only the requested resources");
  expect(processor.requiredInputFrames(blockSize) == 409,
         "re-prepared fallback planner uses the retained tempo ratio");
}

} // namespace

int main() {
  constexpr int blockSize = 512;
  PitchShiftProcessor processor;
  processor.prepare(48000.0, blockSize, 2);
  processor.setContentType(PitchShiftProcessor::ContentType::Melodic);
  processor.setPitchSemiTones(2);
  processor.setTempoRatio(0.9);
  int plannedInputFrames = 0;
  for (int blockIndex = 0; blockIndex < 1000; ++blockIndex)
    plannedInputFrames += processor.requiredInputFrames(blockSize);
  expect(plannedInputFrames == 460800,
         "tempo input planning preserves source-time accuracy");

  juce::AudioBuffer<float> block(2, blockSize);
  double phase = 0.0;
  bool producedFiniteAudio = false;
  for (int iteration = 0; iteration < 200; ++iteration) {
    for (int sample = 0; sample < blockSize; ++sample) {
      const float value = static_cast<float>(std::sin(phase));
      phase += 2.0 * 3.14159265358979323846 * 440.0 / 48000.0;
      block.setSample(0, sample, value);
      block.setSample(1, sample, value);
    }
    processor.process(block);
    for (int channel = 0; channel < 2; ++channel) {
      for (int sample = 0; sample < blockSize; ++sample) {
        const float value = block.getSample(channel, sample);
        expect(std::isfinite(value), "pitch output must remain finite");
        producedFiniteAudio = producedFiniteAudio || std::abs(value) > 1.0e-5f;
      }
    }
  }
  expect(producedFiniteAudio, "pitch ring buffer eventually produces audio");

  processor.setPitchSemiTones(-3);
  bool audibleImmediatelyAfterPitchChange = false;
  for (int iteration = 0; iteration < 3; ++iteration) {
    for (int sample = 0; sample < blockSize; ++sample) {
      const float value = static_cast<float>(std::sin(phase));
      phase += 2.0 * 3.14159265358979323846 * 440.0 / 48000.0;
      block.setSample(0, sample, value);
      block.setSample(1, sample, value);
    }
    processor.process(block);
    audibleImmediatelyAfterPitchChange =
        audibleImmediatelyAfterPitchChange ||
        block.getMagnitude(0, blockSize) > 1.0e-4f;
  }
  expect(audibleImmediatelyAfterPitchChange,
         "live pitch change preserves queued audio continuity");

  SimpleDelayLine delay;
  delay.prepare(2, 64);
  juce::AudioBuffer<float> delayBlock(2, 32);
  for (int sample = 0; sample < 32; ++sample) {
    delayBlock.setSample(0, sample, static_cast<float>(sample + 1));
    delayBlock.setSample(1, sample, static_cast<float>(sample + 1));
  }
  delay.process(delayBlock);
  delay.setDelay(10);
  for (int sample = 0; sample < 32; ++sample) {
    delayBlock.setSample(0, sample, static_cast<float>(sample + 33));
    delayBlock.setSample(1, sample, static_cast<float>(sample + 33));
  }
  delay.process(delayBlock);
  expect(delayBlock.getSample(0, 0) == 33.0f,
         "live latency compensation starts from the previous tap");

  {
    constexpr int transitionSamples =
        SimpleDelayLine::kDelayTransitionSamples;
    SimpleDelayLine transitionDelay;
    transitionDelay.prepare(1, 64);

    juce::AudioBuffer<float> preRoll(1, 64);
    for (int sample = 0; sample < preRoll.getNumSamples(); ++sample)
      preRoll.setSample(0, sample, static_cast<float>(sample + 1));
    transitionDelay.process(preRoll);

    juce::AudioBuffer<float> transitionBlock(1, transitionSamples + 1);
    for (int sample = 0; sample < transitionBlock.getNumSamples(); ++sample)
      transitionBlock.setSample(0, sample, static_cast<float>(sample + 65));

    transitionDelay.setDelay(8);
    transitionDelay.process(transitionBlock);
    expectNear(transitionBlock.getSample(0, 0), 65.0f, 1.0e-5f,
               "increasing delay keeps the first transition sample continuous");
    expectNear(transitionBlock.getSample(0, transitionSamples - 1), 312.0f,
               1.0e-4f,
               "increasing delay reaches the new tap at the transition end");
    expectNear(transitionBlock.getSample(0, transitionSamples), 313.0f,
               1.0e-4f,
               "increased delay remains on the new tap after the transition");

    float previous = preRoll.getSample(0, preRoll.getNumSamples() - 1);
    for (int sample = 0; sample < transitionBlock.getNumSamples(); ++sample) {
      const float current = transitionBlock.getSample(0, sample);
      expect(std::abs(current - previous) < 1.1f,
             "increasing delay spreads the timeline correction without a jump");
      previous = current;
    }

    juce::AudioBuffer<float> decreaseBlock(1, transitionSamples + 1);
    for (int sample = 0; sample < decreaseBlock.getNumSamples(); ++sample) {
      decreaseBlock.setSample(
          0, sample,
          static_cast<float>(sample + 65 + transitionBlock.getNumSamples()));
    }

    transitionDelay.setDelay(2);
    transitionDelay.process(decreaseBlock);
    expectNear(decreaseBlock.getSample(0, 0), 314.0f, 1.0e-4f,
               "decreasing delay starts from the previous tap");
    expectNear(decreaseBlock.getSample(0, transitionSamples - 1), 575.0f,
               1.0e-4f,
               "decreasing delay reaches the new tap at the transition end");
    expectNear(decreaseBlock.getSample(0, transitionSamples), 576.0f,
               1.0e-4f,
               "decreased delay remains on the new tap after the transition");

    previous =
        transitionBlock.getSample(0, transitionBlock.getNumSamples() - 1);
    for (int sample = 0; sample < decreaseBlock.getNumSamples(); ++sample) {
      const float current = decreaseBlock.getSample(0, sample);
      expect(std::abs(current - previous) < 1.1f,
             "decreasing delay spreads the timeline correction without a jump");
      previous = current;
    }
  }

  SimpleDelayLine startupDelay;
  startupDelay.prepare(2, 64);
  startupDelay.setDelay(10);
  juce::AudioBuffer<float> impulseBlock(2, 16);
  impulseBlock.clear();
  impulseBlock.setSample(0, 0, 1.0f);
  impulseBlock.setSample(1, 0, 1.0f);
  startupDelay.process(impulseBlock);
  for (int sample = 0; sample < 10; ++sample) {
    expect(impulseBlock.getSample(0, sample) == 0.0f,
           "clean delay ring emits zero for the complete startup delay");
  }
  expect(impulseBlock.getSample(0, 10) == 1.0f,
         "startup impulse appears at the configured delay");

  startupDelay.reset();
  startupDelay.setDelay(10);
  impulseBlock.clear();
  impulseBlock.setSample(0, 0, 0.5f);
  impulseBlock.setSample(1, 0, 0.5f);
  startupDelay.process(impulseBlock);
  for (int sample = 0; sample < 10; ++sample) {
    expect(impulseBlock.getSample(0, sample) == 0.0f,
           "reset delay ring emits zero for the complete delay");
  }
  expect(impulseBlock.getSample(0, 10) == 0.5f,
         "post-reset impulse appears at the configured delay");

  processor.reset();
  processor.setPitchSemiTones(0);
  processor.setTempoRatio(1.0);
  block.clear();
  block.setSample(0, 0, 0.5f);
  block.setSample(1, 0, 0.5f);
  processor.process(block);
  expect(block.getSample(0, 0) == 0.5f,
         "inactive processor leaves the input unchanged");

  testReleaseResourcesLifecycle();

  std::cout << "pitch shift ring buffer tests passed\n";
  return 0;
}
