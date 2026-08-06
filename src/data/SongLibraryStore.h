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

struct SongWindowSnapshot {
  double startSec = 0.0;
  double endSec = 0.0;
  double fadeOutSec = 0.0;
};

struct DynamicGuideMixSnapshot {
  float volume = 1.0f;
  float gain = 1.0f;
  bool mute = false;
  bool solo = false;
  // Default to mono Out 1 to mirror the click defaults. The runtime mixes
  // the (always-stereo) guide render buffer down to mono when the width
  // is 1, so the announcement stays on a single, predictable bus. Users
  // who actually want stereo can flip the width from the routing UI.
  int outputChannel = 1;
  int outputWidth = 1;
};

struct ClickDynMixSnapshot {
  int divisionMask = 1;
  bool enabled = true;
  bool metronomeEnabled = false;
  float volume = 1.0f;
  float gain = 1.0f;
  bool mute = false;
  bool solo = false;
  int outputChannel = 1;
  int outputWidth = 1;
};

enum class DynamicGuideTiming { OneBarEarly = 0 };
enum class DynamicGuideMode { ReplaceOriginalGuide = 0 };
enum class DynamicGuideLanguagePolicy { FollowAppLanguage = 0 };
enum class DynamicGuideChannelPolicy { EnsureVirtualGuideChannel = 0 };

struct DynamicGuideSettings {
  bool enabled = true;
  DynamicGuideTiming timing = DynamicGuideTiming::OneBarEarly;
  DynamicGuideMode mode = DynamicGuideMode::ReplaceOriginalGuide;
  DynamicGuideLanguagePolicy languagePolicy =
      DynamicGuideLanguagePolicy::FollowAppLanguage;
  DynamicGuideChannelPolicy guideChannelPolicy =
      DynamicGuideChannelPolicy::EnsureVirtualGuideChannel;
};

/** Comportamiento al terminar esta posición dentro del setlist. */
struct SetlistTransition {
  bool autoPlay = false;
};

struct SetlistItem {
  // Identifica la posición del setlist, independientemente de la canción.
  // Permite evolucionar hacia canciones repetidas sin mezclar transiciones.
  juce::String itemId;
  juce::String songId;
  SetlistTransition transitionToNext;
  // Compatibilidad legacy: estos ajustes sólo se serializan para canciones
  // que todavía no viven en un proyecto .pwproj.
  SongMixSnapshot mix;
  int pitchSemitones = 0;
  double tempoBpm = 0.0;
  SongWindowSnapshot window;
  bool windowCustomized = false;
  SongArrangementSnapshot arrangement;
  bool arrangementCustomized = false;
  DynamicGuideSettings dynamicGuide;
  bool dynamicGuideCustomized = false;
  DynamicGuideMixSnapshot dynamicGuideMix;
  ClickDynMixSnapshot clickDynMix;
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

/** Progreso 0..1 y texto de fase (ES/EN) durante importacion a biblioteca. */
using SongImportProgressFn =
    std::function<void(double progress01, const juce::String &phase)>;

class SongLibraryStore {
public:
  static std::vector<ImportedSongData> loadSongs();
  static LibraryState loadLibrarySnapshot();
  static bool setActiveSetlistId(const juce::String &setlistId);
  static juce::String createSetlist(const juce::String &name);
  static bool renameSetlist(const juce::String &setlistId,
                            const juce::String &name);
  static bool deleteSetlist(const juce::String &setlistId);
  static bool addSongToSetlist(const juce::String &setlistId,
                               const juce::String &songId);
  static bool removeSongFromSetlist(const juce::String &setlistId,
                                    const juce::String &songId);
  static bool deleteSong(const juce::String &songId);
  static bool moveSongInSetlist(const juce::String &setlistId, int fromIndex,
                                int toIndex);
  static bool setAutoPlayNextForSetlist(const juce::String &setlistId,
                                        const juce::String &songId,
                                        bool enabled);
  static bool appendSong(ImportedSongData song, juce::String *errorMessage = nullptr,
                         const SongImportProgressFn &onProgress = {});
  /** Registra un .pwproj abierto sin copiar sus recursos a la biblioteca legacy. */
  static bool registerProjectSong(ImportedSongData &song);
  /** Obtiene canción y ajustes canónicos para convertir una entrada legacy. */
  static bool prepareLegacyProjectMigration(
      const juce::String &songId, ImportedSongData &outSong,
      SetlistItem &outSettingsItem, juce::String &outSourceSetlistName,
      juce::String *errorMessage = nullptr);
  /** Sustituye la entrada legacy preservando items, orden y transiciones. */
  static bool finalizeLegacyProjectMigration(
      const juce::String &legacySongId, ImportedSongData projectSong,
      juce::String *errorMessage = nullptr);
  static bool updateSong(ImportedSongData song, juce::String *errorMessage = nullptr,
                         const SongImportProgressFn &onProgress = {});
  static bool getActiveSetlistMix(const juce::String &songId,
                                  SongMixSnapshot &outMix);
  static bool updateStemVolumeForActiveSetlist(const juce::String &songId,
                                               const juce::String &stemId,
                                               float volume);
  static bool updateStemVolumesForActiveSetlist(
      const juce::String &songId,
      const std::vector<std::pair<juce::String, float>> &stemVolumes);
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
  static bool getActiveSetlistPitchSemitones(const juce::String &songId,
                                             int &outSemitones);
  static bool updatePitchSemitonesForActiveSetlist(const juce::String &songId,
                                                   int semitones);
  static bool getActiveSetlistTempoBpm(const juce::String &songId,
                                       double &outBpm);
  static bool updateTempoBpmForActiveSetlist(const juce::String &songId,
                                             double bpm);
  static bool getActiveSetlistSongWindow(const juce::String &songId,
                                         SongWindowSnapshot &outWindow,
                                         bool &outIsCustomWindow);
  static bool updateSongWindowForActiveSetlist(
      const juce::String &songId, const SongWindowSnapshot &window,
      bool isCustomWindow);
  static bool resetSongWindowForActiveSetlist(const juce::String &songId);
  static bool getSongArrangement(const juce::String &songId,
                                 SongArrangementSnapshot &outArrangement,
                                 bool &outIsCustomArrangement);
  static bool updateSongArrangement(const juce::String &songId,
                                    const SongArrangementSnapshot &arrangement,
                                    bool isCustomArrangement);
  static bool resetSongArrangement(const juce::String &songId);
  static bool getActiveSetlistArrangement(const juce::String &songId,
                                          SongArrangementSnapshot &outArrangement,
                                          bool &outIsCustomArrangement);
  static bool updateArrangementForActiveSetlist(
      const juce::String &songId, const SongArrangementSnapshot &arrangement,
      bool isCustomArrangement);
  static bool resetArrangementForActiveSetlist(const juce::String &songId);
  static bool getActiveSetlistDynamicGuide(const juce::String &songId,
                                           DynamicGuideSettings &outSettings,
                                           bool &outIsCustomDynamicGuide);
  static bool updateDynamicGuideForActiveSetlist(
      const juce::String &songId, const DynamicGuideSettings &settings,
      bool isCustomDynamicGuide);
  static bool resetDynamicGuideForActiveSetlist(const juce::String &songId);
  static bool getActiveSetlistDynamicGuideMix(const juce::String &songId,
                                              DynamicGuideMixSnapshot &outMix);
  static bool updateDynamicGuideMixForActiveSetlist(
      const juce::String &songId, const DynamicGuideMixSnapshot &mix);
  static bool getActiveSetlistClickDynMix(const juce::String &songId,
                                          ClickDynMixSnapshot &outMix);
  static bool updateClickDynMixForActiveSetlist(
      const juce::String &songId, const ClickDynMixSnapshot &mix);
  static bool saveWaveformCache(const juce::String &songId,
                                const std::vector<float> &bins);
  static std::vector<float> loadWaveformCache(const juce::String &songId);

  // Serialización reutilizable por ProjectStore. El formato interno de la
  // biblioteca y el manifiesto .pwproj comparten la representación de canción.
  static juce::var songDataToVar(const ImportedSongData &song);
  static ImportedSongData songDataFromVar(const juce::var &value,
                                          bool &changed);
  /** Conversión de stems reutilizada por los paquetes .pwproj. */
  static bool convertStemForProject(const juce::File &source,
                                    const juce::File &destination,
                                    juce::String &errorMessage);
  static juce::String projectStorageStemExtension();

  /**
   * Returns the per-platform application-data root directory used by
   * the audio library (e.g. "MTControls" under
   * userApplicationDataDirectory). Exposed publicly so other
   * subsystems (StemCache, etc.) can co-locate their data with the
   * library without duplicating the path logic.
   */
  static juce::File getRootDirectory();

private:
  static LibraryState loadLibrary();
  static bool saveLibrary(const LibraryState &library);
  static juce::File getDbFile();
  static juce::File getAudioRoot();
  static juce::File getWaveformCacheDir();
  static bool copyStemFiles(ImportedSongData &song, juce::String &errorMessage,
                            const SongImportProgressFn &onProgress = {});
  static juce::File resolveBundledFfmpegBinary();
  static bool convertStemToStorageFormat(const juce::File &source,
                                         const juce::File &destination,
                                         juce::String &errorMessage);
  static juce::var stemToVar(const ImportedStemData &stem);
  static ImportedStemData stemFromVar(const juce::var &v);
  static juce::var songToVar(const ImportedSongData &song);
  static ImportedSongData songFromVar(const juce::var &v, bool &changed);
  static bool ensureSongHasStemIds(ImportedSongData &song);
  static bool setlistExists(const LibraryState &library,
                            const juce::String &setlistId);
  static void ensureDefaultSetlist(LibraryState &library);
  static SetlistData *findSetlistById(LibraryState &library,
                                      const juce::String &setlistId);
  static SetlistItem createSetlistItem(const ImportedSongData &song);
  static SongMixSnapshot createDefaultMixForSong(const ImportedSongData &song);
  static SongWindowSnapshot createDefaultWindowForSong(
      const ImportedSongData &song);
  static SongArrangementSnapshot
  createDefaultArrangementForSong(const ImportedSongData &song);
  static SongMixSnapshot reconcileMixForSong(const SongMixSnapshot &mix,
                                             const ImportedSongData &song,
                                             bool &changed);
  static SongWindowSnapshot reconcileWindowForSong(
      const SongWindowSnapshot &window, const ImportedSongData &song,
      bool &changed);
  static SongArrangementSnapshot reconcileArrangementForSong(
      const SongArrangementSnapshot &arrangement, const ImportedSongData &song,
      bool &changed);
  static bool normalizeLegacyMix(SongMixSnapshot &mix);
  static juce::var windowToVar(const SongWindowSnapshot &window);
  static SongWindowSnapshot windowFromVar(const juce::var &v,
                                          const ImportedSongData *song,
                                          bool &changed);
  static juce::var arrangementPartToVar(
      const SongArrangementPartSnapshot &part);
  static SongArrangementPartSnapshot arrangementPartFromVar(const juce::var &v);
  static juce::var arrangementToVar(const SongArrangementSnapshot &arrangement);
  static SongArrangementSnapshot arrangementFromVar(const juce::var &v,
                                                    const ImportedSongData *song,
                                                    bool &changed);
  static juce::var tempoRegionToVar(const SongTempoRegionSnapshot &region);
  static SongTempoRegionSnapshot tempoRegionFromVar(const juce::var &v);
  static juce::var tempoMapToVar(const SongTempoMapSnapshot &tempoMap);
  static SongTempoMapSnapshot tempoMapFromVar(const juce::var &v,
                                              const ImportedSongData *song,
                                              bool &changed);
  static juce::var dynamicGuideToVar(const DynamicGuideSettings &settings);
  static DynamicGuideSettings dynamicGuideFromVar(const juce::var &v,
                                                  bool &changed);
  static juce::var dynamicGuideMixToVar(const DynamicGuideMixSnapshot &mix);
  static DynamicGuideMixSnapshot dynamicGuideMixFromVar(const juce::var &v,
                                                        bool &changed);
  static juce::var clickDynMixToVar(const ClickDynMixSnapshot &mix);
  static ClickDynMixSnapshot clickDynMixFromVar(const juce::var &v,
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
  static juce::var setlistItemToVar(const SetlistItem &item,
                                    bool persistLegacySongSettings);
  static SetlistItem setlistItemFromVar(const juce::var &v,
                                        const ImportedSongData *song,
                                        bool &changed);
  static juce::var setlistToVar(
      const SetlistData &setlist,
      const std::vector<ImportedSongData> &songs);
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
