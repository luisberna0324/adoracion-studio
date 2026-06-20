#include "../src/PitchShiftProcessor.h"
#include "../src/audio/SimpleDelayLine.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
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
  expect(delayBlock.getSample(0, 0) == 23.0f,
         "engaging latency compensation uses retained history");

  processor.reset();
  processor.setPitchSemiTones(0);
  processor.setTempoRatio(1.0);
  block.clear();
  block.setSample(0, 0, 0.5f);
  block.setSample(1, 0, 0.5f);
  processor.process(block);
  expect(block.getSample(0, 0) == 0.5f,
         "inactive processor leaves the input unchanged");

  std::cout << "pitch shift ring buffer tests passed\n";
  return 0;
}
