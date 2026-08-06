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

/**
 * Almacenamiento portable de canciones. Un .pwproj es un ZIP autocontenido:
 * manifest.json, audio/<stem>.<ext> y artwork.<ext>. Los recursos se extraen
 * al cache local para que el motor siga leyendo archivos normales desde disco.
 */
class ProjectStore final {
public:
  static constexpr const char *kFileExtension = ".pwproj";
  static constexpr int kSchemaVersion = 1;

  static bool save(ProjectDocument document, const juce::File &destination,
                   juce::String *errorMessage = nullptr,
                   double *progress01 = nullptr);
  static bool open(const juce::File &file, ProjectDocument &outDocument,
                   juce::String *errorMessage = nullptr,
                   bool rebuildCache = true);
  static bool updateSettings(const juce::File &file,
                             const ProjectSongSettings &settings,
                             juce::String *errorMessage = nullptr);
  static bool updateWaveform(const juce::File &file,
                             const std::vector<float> &waveformBins,
                             juce::String *errorMessage = nullptr);
  /** Guarda ajustes tras 750 ms sin cambios; la UI nunca espera el ZIP. */
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
  static void flushPendingSettings(const juce::File &file);
  static void flushAllPendingSettings();

  static std::vector<RecentProjectInfo> getRecentProjects();
  static juce::File findRecentProjectFile(const juce::String &projectId);
  static void forgetRecentProject(const juce::File &file);
  static juce::File getCacheDirectory(const juce::String &projectId);

private:
  static juce::File getRegistryFile();
  static void recordRecent(const ProjectDocument &document);
};
