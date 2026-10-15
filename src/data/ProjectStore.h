#pragma once

#include "SongLibraryStore.h"

/** Ajustes persistidos dentro de un proyecto .pwproj. */
struct ProjectSongSettings {
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
  std::optional<bool> blankPadEnabled;
};

struct ProjectDocument {
  ImportedSongData song;
  ProjectSongSettings settings;
  std::vector<float> waveformBins;
  juce::File projectFile;
};

struct RecentProjectInfo {
  juce::String id;
  juce::String title;
  juce::File file;
  juce::int64 lastOpenedMs = 0;
  bool available = false;
};

using ProjectProgressFn =
    std::function<void(double progress01, const juce::String &phase)>;
using ProjectMigrationProgressFn = ProjectProgressFn;

/**
 * Almacenamiento portable de canciones. Un .pwproj es un ZIP autocontenido:
 * manifest.json, audio/<stem>.flac y artwork.<ext>. Los recursos se extraen
 * al cache local para que el motor siga leyendo archivos normales desde disco.
 * Todos los stems del paquete son FLAC, independientemente de la plataforma.
 */
class ProjectStore final {
public:
  static constexpr const char *kFileExtension = ".pwproj";
  static constexpr int kSchemaVersion = 1;

  static bool save(ProjectDocument document, const juce::File &destination,
                   juce::String *errorMessage = nullptr,
                   double *progress01 = nullptr,
                   const ProjectProgressFn &onProgress = {});
  static bool open(const juce::File &file, ProjectDocument &outDocument,
                   juce::String *errorMessage = nullptr,
                   bool rebuildCache = true,
                   const ProjectProgressFn &onProgress = {});
  /** Restores only the embedded cover art without extracting project stems. */
  static bool restoreArtwork(const juce::File &file, ImportedSongData &song,
                             juce::String *errorMessage = nullptr);
  /** Convierte una canción legacy, valida el paquete y retira su audio interno. */
  static bool convertLegacySongToProject(
      const juce::String &legacySongId, const juce::File &destination,
      ImportedSongData &outProjectSong, juce::String &outSourceSetlistName,
      juce::String *errorMessage = nullptr,
      const ProjectMigrationProgressFn &onProgress = {});
  static bool updateSettings(const juce::File &file,
                             const ProjectSongSettings &settings,
                             juce::String *errorMessage = nullptr);
  static bool updateWaveform(const juce::File &file,
                             const std::vector<float> &waveformBins,
                             juce::String *errorMessage = nullptr);
  /**
   * True when the two settings serialize to the same JSON. Used to
   * skip the re-pack of the .pwproj ZIP when settings have not actually
   * changed since the last save (which would otherwise change the ZIP
   * mtime and force a re-extract on the next load).
   */
  static bool settingsJsonEqual(const ProjectSongSettings &a,
                                const ProjectSongSettings &b);
  /** Canonical representation shared by local persistence and cloud versions. */
  static juce::var serializeSettings(const ProjectSongSettings &settings);
  static bool deserializeSettings(const juce::var &value,
                                  ProjectSongSettings &outSettings);
  // Internal helper: persist settings to the per-project sidecar
  // without touching the .pwproj. Used by updateSettings to avoid the
  // mtime-bump that invalidated the cache for every subsequent load.
  static bool writeCachedSettingsFromFile(const juce::File &file,
                                         const ProjectSongSettings &settings);
  /** Guarda el snapshot local y marca el paquete para consolidación explícita. */
  static void scheduleSettingsSave(const juce::File &file,
                                   ProjectSongSettings settings);
  /** Actualiza primero la caché local, incluso si el paquete fue movido. */
  static void scheduleSettingsSave(const juce::File &file,
                                   const juce::String &projectId,
                                   ProjectSongSettings settings);
  /** Devuelve el último snapshot en memoria, incluyendo cambios aún no empaquetados. */
  static bool getCachedSettings(const juce::File &file,
                                ProjectSongSettings &outSettings);
  /** Recupera el snapshot local por identidad, sin depender de la ruta del ZIP. */
  static bool getCachedSettings(const juce::String &projectId,
                                ProjectSongSettings &outSettings);
  /** Indica si el sidecar contiene cambios aún no consolidados en el ZIP. */
  static bool hasPendingPortableChanges(const juce::File &file);
  /** Proyectos abiertos/modificados que todavía requieren consolidación. */
  static std::vector<juce::File> getPendingPortableProjectFiles();
  /**
   * Consolida el último sidecar dentro del .pwproj mediante reemplazo atómico.
   * Si aparecen cambios durante el guardado, el proyecto permanece pendiente.
   */
  static bool savePendingPortableChanges(
      const juce::File &file, juce::String *errorMessage = nullptr,
      const ProjectProgressFn &onProgress = {});
  /** Termina cualquier escritura local pendiente; no reconstruye el ZIP. */
  static void flushPendingSettings(const juce::File &file);
  static void flushAllPendingSettings();

  static std::vector<RecentProjectInfo> getRecentProjects();
  static juce::File findRecentProjectFile(const juce::String &projectId);
  static void forgetRecentProject(const juce::File &file);
  /** Removes only local runtime state; never deletes the .pwproj package. */
  static void discardLocalState(const juce::String &projectId,
                                const juce::File &projectFile);
  static juce::File getCacheDirectory(const juce::String &projectId);

private:
  static juce::File getRegistryFile();
  static void recordRecent(const ProjectDocument &document);
};
