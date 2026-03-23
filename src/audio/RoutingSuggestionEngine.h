#pragma once

#include <JuceHeader.h>

#include <vector>

struct StemRoutingSuggestion {
  int stemIndex = 0;
  juce::String stemName;
  /** Canal de salida 1-based (Canal 1, Canal 2, …). */
  int suggestedChannel = 1;
};

class RoutingSuggestionEngine {
public:
  /** currentChannelsOneBased[i] = canal actual del stem i (1-based), mismo orden que stemNames. */
  static std::vector<StemRoutingSuggestion> suggest(
      int numAvailableChannels, const std::vector<juce::String> &stemNames,
      const std::vector<int> &currentChannelsOneBased);

private:
  static bool stemNameIsClickOrCue(const juce::String &name);
  static bool isCurrentCompatible(int numChannels, int numStems,
                                  const std::vector<int> &current);
};
