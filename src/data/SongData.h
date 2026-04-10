#pragma once

#include <JuceHeader.h>

#include <array>
#include <vector>

struct ImportedStemData {
  juce::String stemId;
  juce::String sourcePath;
  juce::String storedPath;
  juce::String fileName;
  juce::String remoteFileId;
  juce::String contentHash;
  juce::int64 updatedMs = 0;
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
  juce::String remoteFileId;
  juce::String contentHash;
  juce::int64 updatedMs = 0;
  juce::String snapshotVersion;
  std::vector<ImportedStemData> stems;
};

struct SongArrangementPartSnapshot {
  juce::String id;
  juce::String name;
  juce::String color;
  double startSec = 0.0;
  double endSec = 0.0;
};

struct SongArrangementSnapshot {
  std::vector<SongArrangementPartSnapshot> parts;
};

extern const std::array<const char *, 19> kTrackCategories;
extern const std::array<const char *, 14> kSongSectionSuggestions;

juce::String makeDisplayNameFromFile(const juce::File &file);
juce::String suggestCategoryFromFilename(const juce::String &filename);
int stemPriorityForStripOrder(const ImportedStemData &stem);
bool isStemPitchShiftable(const ImportedStemData &stem);
juce::String makeRandomArrangementPartColor();
bool isArrangementPartColorValid(const juce::String &value);
double estimateSongLengthSeconds(const ImportedSongData &song);
std::vector<float> buildMixedWaveformBins(const ImportedSongData &song,
                                          int binCount = 1400);
