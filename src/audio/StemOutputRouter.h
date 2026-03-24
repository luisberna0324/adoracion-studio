#pragma once

#include <JuceHeader.h>

class StemOutputRouter {
public:
  void routeStemToOutputChannels(const juce::AudioBuffer<float> &stemBuffer,
                                 int outputChannelIndex, int width,
                                 juce::AudioBuffer<float> &outputBuffer) const;
};
