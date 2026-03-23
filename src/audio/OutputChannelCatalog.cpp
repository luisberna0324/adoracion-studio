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
