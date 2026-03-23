#pragma once

#include <JuceHeader.h>

class OutputChannelCatalog {
public:
  static juce::StringArray buildOutputChannelNames(int outputChannelCount);
};
