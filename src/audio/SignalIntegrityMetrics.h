#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <cstdint>

namespace playworship::audio {

struct SignalIntegrityMetrics {
  float inputPeak = 0.0f;
  float outputPeak = 0.0f;
  float outputRms = 0.0f;
  float maxDelta = 0.0f;
  float boundaryDelta = 0.0f;
  uint32_t nearClipSamples = 0;
  uint32_t overSamples = 0;
  uint32_t largeDeltaSamples = 0;
  uint32_t nonFiniteSamples = 0;
  int channels = 0;
  int samplesPerChannel = 0;

  bool suspicious() const noexcept {
    // Near-clipping is useful evidence but common in mastered material. Do
    // not use it to trigger a full per-stem snapshot on every block.
    return overSamples > 0 || nonFiniteSamples > 0 ||
           boundaryDelta >= 0.75f;
  }
};

struct SignalIntegrityState {
  static constexpr size_t maxChannels = 64;
  std::array<float, maxChannels> previousSample{};
  std::array<bool, maxChannels> hasPrevious{};

  void reset() noexcept {
    previousSample.fill(0.0f);
    hasPrevious.fill(false);
  }
};

// Real-time-safe: no locks, allocation, logging or wall-clock access. The
// caller can pass the gain that will be applied after this measurement; the
// returned output metrics describe that post-gain signal without modifying it.
inline SignalIntegrityMetrics measureSignalIntegrity(
    const juce::AudioBuffer<float>& buffer, const int startSample,
    const int numSamples, const float appliedGain,
    SignalIntegrityState& state) noexcept {
  SignalIntegrityMetrics result;
  const int boundedStart = juce::jlimit(0, buffer.getNumSamples(), startSample);
  const int boundedSamples = juce::jlimit(
      0, buffer.getNumSamples() - boundedStart, numSamples);
  const int channels = juce::jmin(
      buffer.getNumChannels(), static_cast<int>(SignalIntegrityState::maxChannels));
  result.channels = channels;
  result.samplesPerChannel = boundedSamples;
  if (channels <= 0 || boundedSamples <= 0)
    return result;

  double sumSquares = 0.0;
  uint64_t finiteCount = 0;
  for (int channel = 0; channel < channels; ++channel) {
    const auto* samples = buffer.getReadPointer(channel, boundedStart);
    float previous = state.previousSample[static_cast<size_t>(channel)];
    bool hasPrevious = state.hasPrevious[static_cast<size_t>(channel)];
    for (int sample = 0; sample < boundedSamples; ++sample) {
      const float input = samples[sample];
      const float output = input * appliedGain;
      if (!std::isfinite(input) || !std::isfinite(output)) {
        ++result.nonFiniteSamples;
        previous = 0.0f;
        hasPrevious = true;
        continue;
      }

      const float inputAbs = std::abs(input);
      const float outputAbs = std::abs(output);
      result.inputPeak = juce::jmax(result.inputPeak, inputAbs);
      result.outputPeak = juce::jmax(result.outputPeak, outputAbs);
      sumSquares += static_cast<double>(output) * static_cast<double>(output);
      ++finiteCount;
      if (outputAbs >= 0.999f)
        ++result.nearClipSamples;
      if (outputAbs > 1.0f)
        ++result.overSamples;

      if (hasPrevious) {
        const float delta = std::abs(output - previous);
        result.maxDelta = juce::jmax(result.maxDelta, delta);
        if (sample == 0)
          result.boundaryDelta = juce::jmax(result.boundaryDelta, delta);
        if (delta >= 0.5f)
          ++result.largeDeltaSamples;
      }
      previous = output;
      hasPrevious = true;
    }
    state.previousSample[static_cast<size_t>(channel)] = previous;
    state.hasPrevious[static_cast<size_t>(channel)] = hasPrevious;
  }
  result.outputRms = finiteCount > 0
      ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(finiteCount)))
      : 0.0f;
  return result;
}

// Keeps block-boundary history current on blocks where a full stem scan is
// decimated. This touches one sample per channel and lets the next full scan
// compare against the immediately preceding callback.
inline void updateSignalIntegrityStateFromBufferEnd(
    const juce::AudioBuffer<float>& buffer, const int startSample,
    const int numSamples, const float appliedGain,
    SignalIntegrityState& state) noexcept {
  const int boundedStart = juce::jlimit(0, buffer.getNumSamples(), startSample);
  const int boundedSamples = juce::jlimit(
      0, buffer.getNumSamples() - boundedStart, numSamples);
  if (boundedSamples <= 0)
    return;
  const int channels = juce::jmin(
      buffer.getNumChannels(),
      static_cast<int>(SignalIntegrityState::maxChannels));
  const int lastSample = boundedStart + boundedSamples - 1;
  for (int channel = 0; channel < channels; ++channel) {
    const float output = buffer.getSample(channel, lastSample) * appliedGain;
    state.previousSample[static_cast<size_t>(channel)] =
        std::isfinite(output) ? output : 0.0f;
    state.hasPrevious[static_cast<size_t>(channel)] = true;
  }
}

} // namespace playworship::audio
