#pragma once

#include <JuceHeader.h>

class StemOutputRouter {
public:
  void routeStemToOutputChannel(const juce::AudioBuffer<float> &stemBuffer,
                                int outputChannelIndex,
                                juce::AudioBuffer<float> &outputBuffer) const;
};
