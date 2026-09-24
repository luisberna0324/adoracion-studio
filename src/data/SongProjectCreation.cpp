#include "SongProjectCreation.h"
#include "ProjectStore.h"
#include "Localization.h"
#include "SessionAuthLogger.h"

namespace SongProjectCreation {
bool resolveDestination(const ImportedSongData &song, juce::File &destination,
                        juce::String &error) {
  error.clear();
  const auto title = juce::File::createLegalFileName(song.title.trim());
  const auto artist = juce::File::createLegalFileName(song.artist.trim());
  const auto baseName = artist.isNotEmpty() && title.isNotEmpty()
                            ? artist + " - " + title
                            : (title.isNotEmpty() ? title : "Nueva canción");
  auto directory = SongLibraryStore::getRootDirectory().getChildFile("projects");
  const auto result = directory.createDirectory();
  if (result.failed()) {
    error = result.getErrorMessage();
    return false;
  }
  destination = directory.getNonexistentChildFile(
      baseName, ProjectStore::kFileExtension, false);
  return true;
}

bool save(ImportedSongData song, juce::String &errorMessage,
          const SongImportProgressFn &progress) {
  errorMessage.clear();
  SessionAuthLogger::write(
      "[SongImport] save project begin title=\"" + song.title.trim() +
      "\" id=" + song.id + " stems=" +
      juce::String(static_cast<int>(song.stems.size())));

  const juce::File selectedDestination(song.projectPath);
  if (selectedDestination == juce::File{}) {
    errorMessage = L10n::tr("No se eligió una ubicación para el proyecto.",
                            "No project location was selected.");
    return false;
  }
  ProjectDocument document;
  document.song = std::move(song);
  document.settings.tempoBpm = document.song.bpm > 0 ? document.song.bpm : 120.0;
  document.settings.window.endSec = estimateSongLengthSeconds(document.song);
  document.settings.window.fadeOutSec = juce::jmin(1.0, document.settings.window.endSec);
  for (const auto &stem : document.song.stems) {
    if (!stem.include)
      continue;
    StemMixSnapshot mix;
    mix.stemId = stem.stemId;
    document.settings.mix.stems.push_back(mix);
  }
  if (progress) {
    progress(0.05, L10n::tr(u8"Generando forma de onda...",
                            "Generating waveform preview..."));
  }
  document.waveformBins = buildMixedWaveformBins(
      document.song, kWaveformPreviewBinCount,
      [&progress](const double waveformProgress) {
        if (progress)
          progress(0.05 + 0.15 * juce::jlimit(0.0, 1.0, waveformProgress),
                   L10n::tr(u8"Generando forma de onda...",
                            "Generating waveform preview..."));
      });
  juce::String importError;
  const bool packaged = ProjectStore::save(
      document, selectedDestination, &importError, nullptr,
      [&progress](const double projectProgress,
                  const juce::String &phase) {
        if (progress)
          progress(0.20 + 0.70 * juce::jlimit(0.0, 1.0,
                                              projectProgress),
                   phase);
      });
  ProjectDocument opened;
  const bool openedProject =
      packaged && ProjectStore::open(
                      selectedDestination, opened, &importError, true,
                      [&progress](const double projectProgress,
                                  const juce::String &phase) {
                        if (progress)
                          progress(0.90 +
                                       0.08 * juce::jlimit(
                                                  0.0, 1.0,
                                                  projectProgress),
                                   phase);
                      });
  bool success = false;
  if (openedProject) {
    if (progress)
      progress(0.985, L10n::tr(u8"Registrando canción...",
                                "Registering song..."));
    // SongLibraryStore is a read-modify-write store also used by UI
    // callbacks. Serialize the final registry transaction on that same
    // thread; only the worker waits, with no locks held. Audio work and
    // package extraction above remain off the UI thread.
    const auto registered = juce::MessageManager::callSync(
        [songToRegister = std::move(opened.song)]() mutable {
          try {
            return SongLibraryStore::registerProjectSong(songToRegister);
          } catch (...) {
            // Never leave callSync's promise unfulfilled on an exception.
            return false;
          }
        });
    success = registered.value_or(false);
    if (!success)
      importError = L10n::tr(
          u8"El proyecto se guardó, pero no pudo registrarse en la biblioteca. El archivo se conserva en:\n",
          "The project was saved but couldn't be registered in the library. Its file is preserved at:\n") +
          selectedDestination.getFullPathName();
  }

  if (!success) {
    SessionAuthLogger::write("[SongImport] appendSong failed error=\"" +
                             importError + "\"");
    errorMessage = importError.isNotEmpty() ? importError
                                            : L10n::tr(
                                                  u8"No se pudo guardar la canción.",
                                                  "Couldn't persist the song.");
    return false;
  }

  SessionAuthLogger::write(
      "[SongImport] project saved with embedded waveform");

  if (progress)
    progress(0.995, L10n::tr(u8"Actualizando lista de canciones...",
                             u8"Refreshing song list..."));
  if (progress)
    progress(1.0, L10n::tr(u8"Canción creada", "Song created"));
  return true;
}
} // namespace SongProjectCreation
