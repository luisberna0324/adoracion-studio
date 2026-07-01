#include "../src/audio/BungeeProcessor.h"

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
  std::cout << "bungee processor tests passed\n";
  return 0;
}
