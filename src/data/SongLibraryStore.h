#pragma once

#include "SongData.h"

class SongLibraryStore {
public:
  static std::vector<ImportedSongData> loadSongs();
  static bool appendSong(ImportedSongData song);
  static bool saveWaveformCache(const juce::String &songId,
                                const std::vector<float> &bins);
  static std::vector<float> loadWaveformCache(const juce::String &songId);

private:
  static juce::File getRootDirectory();
  static juce::File getDbFile();
  static juce::File getAudioRoot();
  static juce::File getWaveformCacheDir();
  static bool saveSongs(const std::vector<ImportedSongData> &songs);
  static void copyStemFiles(ImportedSongData &song);
  static juce::var stemToVar(const ImportedStemData &stem);
  static ImportedStemData stemFromVar(const juce::var &v);
  static juce::var songToVar(const ImportedSongData &song);
  static ImportedSongData songFromVar(const juce::var &v);
};
