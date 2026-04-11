#pragma once

#include "SongData.h"

#include <functional>

struct StemMixSnapshot {
  juce::String stemId;
  float volume = 1.0f;
  float gain = 1.0f;
  bool mute = false;
  bool solo = false;
  int outputChannel = 1;
};

struct MasterMixSnapshot {
  float volume = 1.0f;
  float gain = 1.0f;
  bool mute = false;
};

struct SongMixSnapshot {
  MasterMixSnapshot master;
  std::vector<StemMixSnapshot> stems;
};

struct SetlistItem {
  juce::String songId;
  SongMixSnapshot mix;
  SongArrangementSnapshot arrangement;
  bool arrangementCustomized = false;
  int64_t updatedMs = 0;
};

struct SetlistData {
  juce::String id;
  juce::String name;
  std::vector<SetlistItem> items;
  int64_t createdMs = 0;
  int64_t updatedMs = 0;
};

struct LibraryState {
  std::vector<ImportedSongData> songs;
  std::vector<SetlistData> setlists;
  juce::String activeSetlistId;
};

class SongLibraryStore {
public:
  static std::vector<ImportedSongData> loadSongs();
  static LibraryState loadLibrarySnapshot();
  static bool saveLibrarySnapshot(const LibraryState &library);
  /** Parses library JSON (e.g. from Drive snapshot) without reading or writing the local DB. */
  static bool parseLibraryFromJson(const juce::String &json, LibraryState &outLibrary,
                                   juce::String *errorMessage = nullptr);
  static juce::String exportLibraryJson(const LibraryState &library);
  static juce::String exportCurrentLibraryJson();
  static juce::File getLibraryDbFile();
  static juce::File getAudioStorageRoot();
  static bool setActiveSetlistId(const juce::String &setlistId);
  static juce::String createSetlist(const juce::String &name);
  static bool renameSetlist(const juce::String &setlistId,
                            const juce::String &name);
  static bool deleteSetlist(const juce::String &setlistId);
  static bool addSongToSetlist(const juce::String &setlistId,
                               const juce::String &songId);
  static bool removeSongFromSetlist(const juce::String &setlistId,
                                    const juce::String &songId);
  /** Removes the song from the global library, all setlists, local audio folder,
      and waveform cache. */
  static bool removeSongFromLibrary(const juce::String &songId,
                                    juce::String *errorMessage = nullptr);
  /** Replaces an existing song by id or appends and adds to the active setlist if new. */
  static bool upsertLibrarySong(ImportedSongData song,
                                juce::String *errorMessage = nullptr);
  static bool moveSongInSetlist(const juce::String &setlistId, int fromIndex,
                                int toIndex);
  static bool appendSong(ImportedSongData song, juce::String *errorMessage = nullptr);
  static bool getActiveSetlistMix(const juce::String &songId,
                                  SongMixSnapshot &outMix);
  static bool updateStemVolumeForActiveSetlist(const juce::String &songId,
                                               const juce::String &stemId,
                                               float volume);
  static bool updateStemGainForActiveSetlist(const juce::String &songId,
                                             const juce::String &stemId,
                                             float gain);
  static bool updateStemMuteForActiveSetlist(const juce::String &songId,
                                             const juce::String &stemId,
                                             bool muted);
  static bool updateStemSoloForActiveSetlist(const juce::String &songId,
                                             const juce::String &stemId,
                                             bool soloed);
  static bool updateStemOutputChannelForActiveSetlist(const juce::String &songId,
                                                      const juce::String &stemId,
                                                      int outputChannel);
  static bool updateMasterVolumeForActiveSetlist(const juce::String &songId,
                                                 float volume);
  static bool updateMasterGainForActiveSetlist(const juce::String &songId,
                                               float gain);
  static bool updateMasterMuteForActiveSetlist(const juce::String &songId,
                                               bool muted);
  static bool getActiveSetlistArrangement(const juce::String &songId,
                                          SongArrangementSnapshot &outArrangement,
                                          bool &outIsCustomArrangement);
  static bool updateArrangementForActiveSetlist(
      const juce::String &songId, const SongArrangementSnapshot &arrangement,
      bool isCustomArrangement);
  static bool resetArrangementForActiveSetlist(const juce::String &songId);
  static bool saveWaveformCache(const juce::String &songId,
                                const std::vector<float> &bins);
  static std::vector<float> loadWaveformCache(const juce::String &songId);

private:
  static LibraryState loadLibrary();
  static bool saveLibrary(const LibraryState &library);
  static juce::var libraryToVar(const LibraryState &library);
  static juce::File getRootDirectory();
  static juce::File getDbFile();
  static juce::File getAudioRoot();
  static juce::File getWaveformCacheDir();
  static bool copyStemFiles(ImportedSongData &song, juce::String &errorMessage);
  static juce::File resolveBundledFfmpegBinary();
  static bool convertStemToStorageFormat(const juce::File &source,
                                         const juce::File &destination,
                                         juce::String &errorMessage);
  static juce::var stemToVar(const ImportedStemData &stem);
  static ImportedStemData stemFromVar(const juce::var &v);
  static juce::var songToVar(const ImportedSongData &song);
  static ImportedSongData songFromVar(const juce::var &v);
  static bool ensureSongHasStemIds(ImportedSongData &song);
  static bool setlistExists(const LibraryState &library,
                            const juce::String &setlistId);
  static void ensureDefaultSetlist(LibraryState &library);
  static void addSongToActiveSetlist(LibraryState &library,
                                     const ImportedSongData &song);
  static SetlistData *findSetlistById(LibraryState &library,
                                      const juce::String &setlistId);
  static SetlistItem createSetlistItem(const ImportedSongData &song);
  static SongMixSnapshot createDefaultMixForSong(const ImportedSongData &song);
  static SongArrangementSnapshot
  createDefaultArrangementForSong(const ImportedSongData &song);
  static SongMixSnapshot reconcileMixForSong(const SongMixSnapshot &mix,
                                             const ImportedSongData &song,
                                             bool &changed);
  static SongArrangementSnapshot reconcileArrangementForSong(
      const SongArrangementSnapshot &arrangement, const ImportedSongData &song,
      bool &changed);
  static bool normalizeLegacyMix(SongMixSnapshot &mix);
  static juce::var arrangementPartToVar(
      const SongArrangementPartSnapshot &part);
  static SongArrangementPartSnapshot arrangementPartFromVar(const juce::var &v);
  static juce::var arrangementToVar(const SongArrangementSnapshot &arrangement);
  static SongArrangementSnapshot arrangementFromVar(const juce::var &v,
                                                    const ImportedSongData *song,
                                                    bool &changed);
  static juce::var masterMixToVar(const MasterMixSnapshot &mix);
  static MasterMixSnapshot masterMixFromVar(const juce::var &v);
  static juce::var stemMixToVar(const StemMixSnapshot &mix);
  static StemMixSnapshot stemMixFromVar(const juce::var &v,
                                        const juce::String &stemId);
  static juce::var mixToVar(const SongMixSnapshot &mix);
  static SongMixSnapshot mixFromVar(const juce::var &v,
                                    const ImportedSongData *song,
                                    bool &changed);
  static juce::var setlistItemToVar(const SetlistItem &item);
  static SetlistItem setlistItemFromVar(const juce::var &v,
                                        const ImportedSongData *song,
                                        bool &changed);
  static juce::var setlistToVar(const SetlistData &setlist);
  static SetlistData setlistFromVar(const juce::var &v,
                                    const std::vector<ImportedSongData> &songs,
                                    bool &changed);
  static const ImportedSongData *findSongById(
      const std::vector<ImportedSongData> &songs,
      const juce::String &songId);
  static bool updateActiveSetlistItem(
      const juce::String &songId,
      std::function<void(SetlistItem &, const ImportedSongData &)> updater);
  static bool updateStemMixValue(
      const juce::String &songId, const juce::String &stemId,
      std::function<void(StemMixSnapshot &)> updater);
  static bool updateMasterMixValue(
      const juce::String &songId,
      std::function<void(MasterMixSnapshot &)> updater);
};
