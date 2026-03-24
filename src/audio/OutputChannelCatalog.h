#pragma once

#include <JuceHeader.h>

class OutputChannelCatalog {
public:
  static juce::StringArray buildOutputChannelNames(int outputChannelCount);
  static juce::StringArray buildStereoPairNames(int outputChannelCount);
};
