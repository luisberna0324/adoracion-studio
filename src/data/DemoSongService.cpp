#include "DemoSongService.h"
#include "Localization.h"
#include "SongLibraryStore.h"

#include <BinaryData.h>
#include <algorithm>

namespace {
constexpr const char *bundledResourceName = "LuzDeEsperanza_pwproj";
constexpr const char *bundledSha256 =
    "79b3408202a75dffc999a8f659a608b56c4a3eea8b0ec257f1bf36118657370f";

juce::CriticalSection &installationLock() {
  static juce::CriticalSection lock;
  return lock;
}

bool hasDemoManifest(const juce::File &file) {
  if (!file.existsAsFile() || !file.hasFileExtension("pwproj"))
    return false;
  juce::ZipFile archive(file);
  const auto index = archive.getIndexOfFileName("manifest.json");
  const auto *entry = index >= 0 ? archive.getEntry(index) : nullptr;
  if (entry == nullptr || entry->uncompressedSize <= 0 ||
      entry->uncompressedSize > 1024 * 1024)
    return false;
  const auto stream =
      std::unique_ptr<juce::InputStream>(archive.createStreamForEntry(index));
  if (stream == nullptr)
    return false;
  const auto manifest = juce::JSON::parse(stream->readEntireStreamAsString());
  return static_cast<int>(manifest["schemaVersion"]) == ProjectStore::kSchemaVersion &&
         manifest["song"]["id"].toString().trim() == DemoSongService::projectId();
}

bool hasUsableLocalAudio(const ImportedSongData &song) {
  if (song.stems.empty())
    return false;
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  for (const auto &stem : song.stems) {
    // Excluded stems must remain recoverable by the ordinary mixer/editor.
    const juce::File file(stem.storedPath);
    if (!file.existsAsFile() || file.getSize() <= 0)
      return false;
    const auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
      return false;
  }
  return true;
}

void discardFailedInstallation(const juce::File &package) {
  ProjectStore::flushPendingSettings(package);
  ProjectStore::discardLocalState(DemoSongService::projectId(), package);
  SongLibraryStore::getRootDirectory().getChildFile("waveforms")
      .getChildFile(DemoSongService::projectId() + ".bin").deleteFile();
  package.deleteFile();
}
} // namespace

bool DemoSongService::isDemoSong(const ImportedSongData &song) {
  return song.projectId.trim() == projectId();
}

juce::Result DemoSongService::ensureInstalled(
    ImportedSongData &outSong, const ProjectProgressFn &onProgress) {
  const juce::ScopedLock lock(installationLock());
  outSong = {};
  const auto root = SongLibraryStore::getRootDirectory();
  const auto database = root.getChildFile("songs.json");
  // SongLibraryStore supports legacy migrations, but a malformed inventory
  // must not be treated as an empty library by this optional installation.
  if (database.exists()) {
    const auto contents = database.existsAsFile() ? juce::JSON::parse(database)
                                                : juce::var{};
    if (contents.getDynamicObject() == nullptr || !contents["songs"].isArray())
      return juce::Result::fail(L10n::tr(
          "No se pudo leer la Biblioteca. La demo no se instaló.",
          "The Library could not be read. The demo was not installed."));
  }

  const auto songs = SongLibraryStore::loadSongs();
  const auto existing = std::find_if(songs.begin(), songs.end(), isDemoSong);
  if (existing != songs.end()) {
    if (!hasDemoManifest(juce::File(existing->projectPath)) ||
        !hasUsableLocalAudio(*existing))
      return juce::Result::fail(L10n::tr(
          "Faltan archivos de la demo instalada. Elimina la demo de la Biblioteca y vuelve a recuperarla para restaurar el original.",
          "The installed demo has missing files. Remove it from the Library and recover it again to restore the original."));
    outSong = *existing;
    if (onProgress)
      onProgress(1.0, L10n::tr("Demo disponible", "Demo ready"));
    return juce::Result::ok();
  }

  int resourceSize = 0;
  const auto *resource =
      BinaryData::getNamedResource(bundledResourceName, resourceSize);
  if (resource == nullptr || resourceSize <= 0 ||
      juce::SHA256(resource, static_cast<size_t>(resourceSize)).toHexString() !=
          bundledSha256)
    return juce::Result::fail(L10n::tr(
        "La demo incluida no está disponible. Vuelve a instalar PlayWorship.",
        "The bundled demo is unavailable. Reinstall PlayWorship."));

  const auto directory = root.getChildFile("demo");
  const auto package = directory.getChildFile("LuzDeEsperanza.pwproj");
  if (directory.isSymbolicLink() || package.isSymbolicLink() ||
      directory.createDirectory().failed() || package.isDirectory())
    return juce::Result::fail(L10n::tr(
        "No se pudo preparar el almacenamiento de la demo.",
        "The demo storage could not be prepared."));
  if (onProgress)
    onProgress(0.02, L10n::tr("Preparando demo...", "Preparing demo..."));

  juce::TemporaryFile temporary(package, juce::TemporaryFile::useHiddenFile);
  {
    juce::FileOutputStream stream(temporary.getFile());
    if (!stream.openedOk() ||
        !stream.write(resource, static_cast<size_t>(resourceSize)))
      return juce::Result::fail(L10n::tr(
          "No se pudo copiar la demo en este dispositivo.",
          "The demo could not be copied to this device."));
    stream.flush();
    if (stream.getStatus().failed() ||
        temporary.getFile().getSize() != resourceSize)
      return juce::Result::fail(L10n::tr(
          "No se pudo guardar la demo completa. Comprueba el espacio disponible.",
          "The complete demo could not be saved. Check the available storage."));
  }
  if (!temporary.overwriteTargetFileWithTemporary())
    return juce::Result::fail(L10n::tr(
        "No se pudo guardar la demo en este dispositivo.",
        "The demo could not be saved on this device."));

  // No library entry exists: a previous deletion or interrupted installation
  // must not resurrect its sidecar instead of the bundled original settings.
  ProjectStore::flushPendingSettings(package);
  ProjectStore::discardLocalState(projectId(), package);
  ProjectDocument document;
  juce::String error;
  if (!ProjectStore::open(package, document, &error, true,
          [&onProgress](double fraction, const juce::String &phase) {
            if (onProgress)
              onProgress(0.08 + 0.84 * fraction, phase);
          }) || !isDemoSong(document.song) ||
      !hasUsableLocalAudio(document.song)) {
    discardFailedInstallation(package);
    return juce::Result::fail(L10n::tr(
        "No se pudo preparar el audio de la demo. Inténtalo nuevamente.",
        "The demo audio could not be prepared. Try again."));
  }

  if (onProgress)
    onProgress(0.94, L10n::tr("Agregando demo a la Biblioteca...",
                             "Adding demo to the Library..."));
  if (!SongLibraryStore::registerProjectSong(document.song)) {
    discardFailedInstallation(package);
    return juce::Result::fail(L10n::tr(
        "No se pudo agregar la demo a la Biblioteca. Inténtalo nuevamente.",
        "The demo could not be added to the Library. Try again."));
  }
  outSong = std::move(document.song);
  if (onProgress)
    onProgress(1.0, L10n::tr("Demo disponible", "Demo ready"));
  return juce::Result::ok();
}
