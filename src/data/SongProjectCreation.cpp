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

#if !JUCE_IOS && !JUCE_ANDROID
  // Decode M4A once, on this worker, so waveform and timing use the same
  // portable audio that will be packaged. Native M4A readers are unavailable
  // on some desktop platforms. Originals and the wizard draft stay untouched.
  juce::File m4aStage;
  const juce::ScopeGuard cleanupM4a{
      [&m4aStage] { if (m4aStage != juce::File{}) m4aStage.deleteRecursively(); }};
  const auto m4aCount = std::count_if(song.stems.begin(), song.stems.end(),
      [](const ImportedStemData &stem) {
        return juce::File(stem.storedPath.isNotEmpty() ? stem.storedPath
                                                      : stem.sourcePath)
            .hasFileExtension("m4a");
      });
  int preparedM4a = 0;
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  for (auto &stem : song.stems) {
    const juce::File source(stem.storedPath.isNotEmpty() ? stem.storedPath
                                                       : stem.sourcePath);
    if (!source.hasFileExtension("m4a"))
      continue;
    if (!source.existsAsFile() || source.getSize() <= 0) {
      errorMessage = L10n::tr("Audio no disponible: ", "Audio unavailable: ") +
                     source.getFileName();
      return false;
    }
    if (m4aStage == juce::File{}) {
      m4aStage = SongLibraryStore::getRootDirectory()
                     .getChildFile("project-staging")
                     .getChildFile(juce::Uuid().toString());
      const auto created = m4aStage.createDirectory();
      if (created.failed()) {
        errorMessage = created.getErrorMessage();
        return false;
      }
    }
    const auto converted = m4aStage.getChildFile(
        juce::String(preparedM4a) + ".flac");
    const auto phase = L10n::tr("Preparando ", "Preparing ") + source.getFileName();
    if (!SongLibraryStore::convertStemForProject(
            source, converted, errorMessage, stem.durationSec,
            [&](double value, const juce::String &) {
              if (progress)
                progress(0.05 * (preparedM4a + value) / m4aCount, phase);
            }))
      return false;
    const auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(converted));
    if (reader == nullptr || reader->sampleRate <= 0.0 ||
        reader->lengthInSamples <= 0 || reader->numChannels == 0) {
      errorMessage = L10n::tr("No se puede decodificar el stem: ",
                             "Couldn't decode stem: ") + source.getFileName();
      return false;
    }
    stem.storedPath = converted.getFullPathName();
    stem.durationSec = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    stem.sampleRate = static_cast<int>(reader->sampleRate);
    stem.channels = static_cast<int>(reader->numChannels);
    ++preparedM4a;
  }
#endif

  ProjectDocument document;
  document.song = std::move(song);
  document.settings.tempoBpm = document.song.bpm > 0 ? document.song.bpm : 120.0;
  document.settings.window.endSec = estimateSongLengthSeconds(document.song);
  if (document.song.blankSong) {
    document.settings.arrangement = document.song.arrangement;
    document.settings.arrangementCustomized = !document.song.arrangement.parts.empty();
    document.settings.blankPadEnabled = document.song.blankPadEnabled;
    document.settings.clickDynMix.metronomeEnabled = document.song.blankClickEnabled;
    document.settings.clickDynMix.divisionMask = 1;
    document.settings.clickDynMix.enabled = true;
  }
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
