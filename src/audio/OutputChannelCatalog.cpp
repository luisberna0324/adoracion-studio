#include "OutputChannelCatalog.h"

juce::StringArray
OutputChannelCatalog::buildOutputChannelNames(const int outputChannelCount) {
  juce::StringArray names;
  const int safeCount = juce::jmax(1, outputChannelCount);
  names.ensureStorageAllocated(safeCount);

  for (int i = 0; i < safeCount; ++i)
    names.add("Canal " + juce::String(i + 1));

  return names;
}

juce::StringArray
OutputChannelCatalog::buildStereoPairNames(const int outputChannelCount) {
  juce::StringArray names;
  const int safeCount = juce::jmax(0, outputChannelCount);
  if (safeCount < 2)
    return names;

  names.ensureStorageAllocated(safeCount - 1);
  for (int i = 0; i < safeCount - 1; ++i) {
    const int left = i + 1;
    const int right = i + 2;
    names.add("Canal " + juce::String(left) + " + Canal " +
              juce::String(right));
  }

  return names;
}
