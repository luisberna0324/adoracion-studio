#pragma once

#include <JuceHeader.h>

#include <vector>

struct PrimeTrackData {
  juce::String stemId;
  juce::String displayName;
  juce::String category;
  juce::String sourcePath;
};

struct PrimeSongData {
  juce::String songId;
  juce::String title;
  juce::String artist;
  int bpm = 0;
  juce::String key;
  juce::String timeSignature;
  std::vector<PrimeTrackData> tracks;
};

class PrimeLibrarySource {
public:
  static bool isSupportedOnCurrentPlatform() noexcept;
  static bool isAvailable();
  static std::vector<PrimeSongData> loadSongs(juce::String &errorMessage);

private:
  static juce::File getPrimeDocumentsDirectory();
  static juce::File locateDatabaseFile();
  static juce::File locateTracksDirectory(const juce::File &primeDocumentsDir);
};

