#pragma once

#include <JuceHeader.h>

#include <array>
#include <vector>

struct ImportedStemData {
  juce::String stemId;
  juce::String sourcePath;
  juce::String storedPath;
  juce::String fileName;
  juce::String displayName;
  juce::String category;
  bool include = true;
  double durationSec = 0.0;
  int sampleRate = 0;
  int channels = 0;
};

struct ImportedSongData {
  juce::String id;
  juce::String title;
  juce::String artist;
  int bpm = 0;
  juce::String key;
  juce::String timeSignature;
  juce::String fileType;
  int64_t createdMs = 0;
  std::vector<ImportedStemData> stems;
};

extern const std::array<const char *, 11> kTrackCategories;

juce::String makeDisplayNameFromFile(const juce::File &file);
juce::String suggestCategoryFromFilename(const juce::String &filename);
int stemPriorityForStripOrder(const ImportedStemData &stem);
std::vector<float> buildMixedWaveformBins(const ImportedSongData &song,
                                          int binCount = 1400);
