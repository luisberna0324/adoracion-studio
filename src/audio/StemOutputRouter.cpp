#include "StemOutputRouter.h"

void StemOutputRouter::routeStemToOutputChannels(
    const juce::AudioBuffer<float> &stemBuffer, const int outputChannelIndex,
    const int width, juce::AudioBuffer<float> &outputBuffer) const {
  if (!juce::isPositiveAndBelow(outputChannelIndex, outputBuffer.getNumChannels()))
    return;

  const int stemChannels = stemBuffer.getNumChannels();
  const int numSamples = juce::jmin(stemBuffer.getNumSamples(),
                                    outputBuffer.getNumSamples());
  if (stemChannels <= 0 || numSamples <= 0)
    return;

  const int safeWidth = juce::jlimit(1, 2, width);
  if (safeWidth >= 2) {
    if (!juce::isPositiveAndBelow(outputChannelIndex + 1,
                                  outputBuffer.getNumChannels()))
      return;
    auto *destL = outputBuffer.getWritePointer(outputChannelIndex);
    auto *destR = outputBuffer.getWritePointer(outputChannelIndex + 1);
    if (stemChannels == 1) {
      const auto *src = stemBuffer.getReadPointer(0);
      for (int sample = 0; sample < numSamples; ++sample) {
        const float v = src[sample];
        destL[sample] += v;
        destR[sample] += v;
      }
      return;
    }
    const auto *left = stemBuffer.getReadPointer(0);
    const auto *right = stemBuffer.getReadPointer(1);
    for (int sample = 0; sample < numSamples; ++sample) {
      destL[sample] += left[sample];
      destR[sample] += right[sample];
    }
    return;
  }

  auto *dest = outputBuffer.getWritePointer(outputChannelIndex);
  if (stemChannels == 1) {
    const auto *src = stemBuffer.getReadPointer(0);
    for (int sample = 0; sample < numSamples; ++sample)
      dest[sample] += src[sample];
    return;
  }

  // Stereo (or wider) folds down to mono before routing to a single
  // physical channel to avoid clipping and preserve balance.
  const auto *left = stemBuffer.getReadPointer(0);
  const auto *right = stemBuffer.getReadPointer(1);
  for (int sample = 0; sample < numSamples; ++sample)
    dest[sample] += 0.5f * (left[sample] + right[sample]);
}
