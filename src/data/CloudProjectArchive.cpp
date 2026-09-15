#include "CloudProjectArchive.h"

#include "ProjectStore.h"
#include "SessionAuthLogger.h"
#include "SongLibraryStore.h"

namespace {
constexpr const char* kManifestName = "manifest.json";
constexpr const char* kWaveformName = "waveform.bin";

void setError(juce::String* target, const juce::String& value) {
  if (target != nullptr) *target = value;
}

juce::String contentTypeFor(const juce::File& file) {
  const auto extension = file.getFileExtension().toLowerCase();
  if (extension == ".flac") return "audio/flac";
  if (extension == ".png") return "image/png";
  if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
  if (extension == ".webp") return "image/webp";
  return "application/octet-stream";
}

CloudUploadObject makeObject(const juce::File& file, const juce::String& contentType) {
  return {{juce::SHA256(file).toHexString().toLowerCase(), file.getSize(), contentType}, file};
}

bool copyZipEntryToFile(juce::ZipFile& zip, const juce::String& entryName,
                        const juce::File& destination) {
  const int index = zip.getIndexOfFileName(entryName);
  if (index < 0) return false;
  std::unique_ptr<juce::InputStream> input(zip.createStreamForEntry(index));
  if (!input) return false;
  destination.getParentDirectory().createDirectory();
  const auto temporary = destination.getSiblingFile(destination.getFileName() + ".part");
  temporary.deleteFile();
  auto output = temporary.createOutputStream();
  if (!output || output->writeFromInputStream(*input, -1) < 0 || !output->getStatus().wasOk()) {
    temporary.deleteFile();
    return false;
  }
  output.reset();
  return destination.existsAsFile() ? temporary.replaceFileIn(destination)
                                    : temporary.moveFileTo(destination);
}

const CloudDownloadedObject* findObject(const CloudDownloadResult& downloaded,
                                        const CloudObjectRef& reference) {
  for (const auto& object : downloaded.objects)
    if (object.reference.sha256 == reference.sha256) return &object;
  return nullptr;
}

bool safeAudioPath(const juce::String& path) {
  return path.startsWith("audio/") && !path.contains("..") &&
         juce::File(path).getFileName() != path;
}
}  // namespace

bool CloudProjectArchive::buildUpload(const juce::File& projectFile,
                                      const juce::String& remoteSongId,
                                      const int baseRevision,
                                      CloudUploadRequest& outRequest,
                                      juce::String* errorMessage,
                                      BuildProgressCallback progress) {
  const auto report = [&progress](const double fraction, const juce::String& phase) {
    if (progress) progress(juce::jlimit(0.0, 0.50, fraction), phase);
  };
  setError(errorMessage, {});
  report(0.02, "opening-project");
  if (!projectFile.existsAsFile() || !projectFile.hasFileExtension(ProjectStore::kFileExtension)) {
    setError(errorMessage, "Project file not found or invalid.");
    return false;
  }
  ProjectStore::flushPendingSettings(projectFile);
  ProjectDocument document;
  juce::String openError;
  if (!ProjectStore::open(projectFile, document, &openError)) {
    setError(errorMessage, openError);
    return false;
  }
  report(0.10, "reading-project");
  juce::ZipFile zip(projectFile);
  const auto staging = SongLibraryStore::getRootDirectory().getChildFile("cloud-sync-staging")
                           .getChildFile(document.song.projectId);
  staging.createDirectory();
  const auto manifestFile = staging.getChildFile("manifest.json");
  if (!copyZipEntryToFile(zip, kManifestName, manifestFile)) {
    setError(errorMessage, "Couldn't extract project manifest.");
    return false;
  }
  report(0.18, "transforming-manifest");

  CloudUploadRequest request;
  request.baseRevision = juce::jmax(0, baseRevision);
  request.descriptor.songId = remoteSongId.trim();
  request.descriptor.projectId = document.song.projectId.trim();
  request.descriptor.title = document.song.title.trim();
  request.descriptor.artist = document.song.artist.trim();
  request.descriptor.bpm = document.song.bpm;
  request.descriptor.key = document.song.key.trim();
  request.descriptor.durationSeconds = estimateSongLengthSeconds(document.song);
  request.descriptor.createdAt = juce::Time::getCurrentTime().toISO8601(true);
  if (request.descriptor.projectId.isEmpty() || request.descriptor.title.isEmpty() ||
      request.descriptor.durationSeconds <= 0.0) {
    setError(errorMessage, "Project metadata is incomplete.");
    return false;
  }
  // ProjectStore derives projectId from song.id for older packages. Persist it
  // into the cloud manifest so the blob and descriptor have one identity.
  auto manifestRoot = juce::JSON::parse(manifestFile.loadFileAsString());
  auto* manifestObject = manifestRoot.getDynamicObject();
  auto* manifestSong = manifestObject != nullptr
                           ? manifestObject->getProperty("song").getDynamicObject()
                           : nullptr;
  if (manifestSong == nullptr) {
    setError(errorMessage, "Project manifest song is invalid.");
    return false;
  }
  manifestSong->setProperty("projectId", request.descriptor.projectId);
  if (!manifestFile.replaceWithText(juce::JSON::toString(manifestRoot, true))) {
    setError(errorMessage, "Couldn't normalize project manifest.");
    return false;
  }
  auto settings = makeObject(manifestFile, "application/json");
  request.descriptor.settings = settings.reference;
  request.objects.push_back(std::move(settings));

  const int waveformIndex = zip.getIndexOfFileName(kWaveformName);
  if (waveformIndex >= 0) {
    const auto waveformFile = staging.getChildFile(kWaveformName);
    if (!copyZipEntryToFile(zip, kWaveformName, waveformFile)) {
      setError(errorMessage, "Couldn't extract project waveform.");
      return false;
    }
    auto waveform = makeObject(waveformFile, "application/octet-stream");
    request.descriptor.waveform = waveform.reference;
    request.objects.push_back(std::move(waveform));
  }
  report(0.26, "extracting-assets");
  if (document.song.artworkPath.isNotEmpty()) {
    const juce::File artwork(document.song.artworkPath);
    if (artwork.existsAsFile()) {
      auto artworkObject = makeObject(artwork, contentTypeFor(artwork));
      request.descriptor.artwork = artworkObject.reference;
      request.objects.push_back(std::move(artworkObject));
    }
  }
  for (const auto& stem : document.song.stems) {
    const juce::File file(stem.storedPath);
    if (!file.existsAsFile()) {
      setError(errorMessage, "Project stem is unavailable: " + stem.displayName);
      return false;
    }
    auto object = makeObject(file, "audio/flac");
    CloudStemDescriptor descriptor;
    static_cast<CloudObjectRef&>(descriptor) = object.reference;
    descriptor.stemId = stem.stemId;
    descriptor.displayName = stem.displayName;
    descriptor.category = stem.category;
    descriptor.durationSec = stem.durationSec > 0.0 ? stem.durationSec : request.descriptor.durationSeconds;
    descriptor.sampleRate = stem.sampleRate;
    descriptor.channels = stem.channels;
    if (descriptor.stemId.isEmpty() || descriptor.sampleRate <= 0 || descriptor.channels <= 0) {
      setError(errorMessage, "Project stem metadata is incomplete: " + stem.displayName);
      return false;
    }
    request.descriptor.stems.push_back(std::move(descriptor));
    request.objects.push_back(std::move(object));
    const auto completed = static_cast<double>(request.descriptor.stems.size());
    report(0.26 + 0.22 * completed /
                      static_cast<double>(juce::jmax(1, static_cast<int>(document.song.stems.size()))),
           "transforming-stems");
  }
  outRequest = std::move(request);
  report(0.50, "prepared-upload");
  // Cleanup: the staging directory only existed to feed the upload
  // builder. The CloudTransferClient reads manifest.json and
  // waveform.bin via outRequest.objects which holds its own copies.
  // Removing here avoids the leak where every upload leaves a
  // cloud-sync-staging/<projectId>/ directory behind (see
  // perf/analysis_20260912/INFORME.md §2.5).
  if (staging.exists() && !staging.deleteRecursively()) {
    SessionAuthLogger::write(
        "[CloudProjectArchive] staging cleanup failed path=" +
        staging.getFullPathName());
  }
  return true;
}

bool CloudProjectArchive::rebuildProject(const CloudDownloadResult& downloaded,
                                         const juce::File& destination,
                                         ImportedSongData& outSong,
                                         juce::String* errorMessage) {
  setError(errorMessage, {});
  const auto* manifest = findObject(downloaded, downloaded.descriptor.settings);
  if (!manifest || !manifest->file.existsAsFile()) {
    setError(errorMessage, "Cloud project manifest is missing.");
    return false;
  }
  const auto root = juce::JSON::parse(manifest->file.loadFileAsString());
  const auto* object = root.getDynamicObject();
  if (!object || static_cast<int>(object->getProperty("schemaVersion")) != ProjectStore::kSchemaVersion) {
    setError(errorMessage, "Cloud project manifest is unsupported.");
    return false;
  }
  bool changed = false;
  auto song = SongLibraryStore::songDataFromVar(object->getProperty("song"), changed);
  if (song.projectId.trim().isEmpty()) {
    // Legacy cloud revisions stored the original .pwproj manifest, where the
    // project identity was implicit in song.id. Restore it from the signed
    // descriptor before writing the reconstructed package.
    song.projectId = downloaded.descriptor.projectId;
    if (auto* songObject = object->getProperty("song").getDynamicObject()) {
      songObject->setProperty("projectId", song.projectId);
      if (!manifest->file.replaceWithText(juce::JSON::toString(root, true))) {
        setError(errorMessage, "Couldn't normalize legacy cloud manifest.");
        return false;
      }
    }
  }
  if (song.id.isEmpty() || song.stems.empty() ||
      song.projectId.trim() != downloaded.descriptor.projectId) {
    setError(errorMessage, "Cloud project manifest does not match its descriptor.");
    return false;
  }
  juce::ZipFile::Builder builder;
  builder.addFile(manifest->file, 0, kManifestName);
  for (const auto& descriptorStem : downloaded.descriptor.stems) {
    const auto* blob = findObject(downloaded, descriptorStem);
    const auto stem = std::find_if(song.stems.begin(), song.stems.end(), [&descriptorStem](const auto& candidate) {
      return candidate.stemId == descriptorStem.stemId;
    });
    if (!blob || stem == song.stems.end() || !safeAudioPath(stem->storedPath)) {
      setError(errorMessage, "Cloud project stem mapping is invalid.");
      return false;
    }
    builder.addFile(blob->file, 0, stem->storedPath);
  }
  if (downloaded.descriptor.waveform) {
    const auto* waveform = findObject(downloaded, *downloaded.descriptor.waveform);
    if (!waveform) { setError(errorMessage, "Cloud waveform is missing."); return false; }
    builder.addFile(waveform->file, 0, kWaveformName);
  }
  if (downloaded.descriptor.artwork) {
    const auto* artwork = findObject(downloaded, *downloaded.descriptor.artwork);
    const auto artworkPath = song.artworkPath;
    if (!artwork || artworkPath.isEmpty() || juce::File(artworkPath).getFileName() != artworkPath) {
      setError(errorMessage, "Cloud artwork mapping is invalid."); return false;
    }
    builder.addFile(artwork->file, 0, artworkPath);
  }
  const auto temporary = destination.getSiblingFile(destination.getFileName() + ".part");
  temporary.deleteFile();
  destination.getParentDirectory().createDirectory();
  auto output = temporary.createOutputStream();
  if (!output || !builder.writeToStream(*output, nullptr) || !output->getStatus().wasOk()) {
    temporary.deleteFile(); setError(errorMessage, "Couldn't write cloud project."); return false;
  }
  output.reset();
  if (!(destination.existsAsFile() ? temporary.replaceFileIn(destination)
                                   : temporary.moveFileTo(destination))) {
    temporary.deleteFile(); setError(errorMessage, "Couldn't finalize cloud project."); return false;
  }
  ProjectDocument opened;
  juce::String openError;
  if (!ProjectStore::open(destination, opened, &openError)) {
    destination.deleteFile(); setError(errorMessage, openError); return false;
  }
  outSong = opened.song;
  return true;
}

bool CloudProjectArchive::materializeCachedSong(
    const CloudDownloadResult& downloaded, ImportedSongData& outSong,
    std::vector<float>& outWaveformBins, juce::String* errorMessage) {
  setError(errorMessage, {});
  outSong = {};
  outWaveformBins.clear();

  const auto* manifest = findObject(downloaded, downloaded.descriptor.settings);
  if (manifest == nullptr || !manifest->file.existsAsFile()) {
    setError(errorMessage, "Cloud project manifest is missing.");
    return false;
  }

  const auto root = juce::JSON::parse(manifest->file.loadFileAsString());
  const auto* object = root.getDynamicObject();
  if (object == nullptr ||
      static_cast<int>(object->getProperty("schemaVersion")) !=
          ProjectStore::kSchemaVersion) {
    setError(errorMessage, "Cloud project manifest is unsupported.");
    return false;
  }

  bool changed = false;
  auto song = SongLibraryStore::songDataFromVar(object->getProperty("song"), changed);
  if (song.projectId.trim().isEmpty())
    song.projectId = downloaded.descriptor.projectId;
  if (song.id.isEmpty() || song.stems.empty() ||
      song.projectId.trim() != downloaded.descriptor.projectId) {
    setError(errorMessage, "Cloud project manifest does not match its descriptor.");
    return false;
  }

  for (const auto& descriptorStem : downloaded.descriptor.stems) {
    const auto* blob = findObject(downloaded, descriptorStem);
    const auto stem = std::find_if(
        song.stems.begin(), song.stems.end(),
        [&descriptorStem](const auto& candidate) {
          return candidate.stemId == descriptorStem.stemId;
        });
    if (blob == nullptr || !blob->file.existsAsFile() ||
        stem == song.stems.end()) {
      setError(errorMessage, "Cloud project stem mapping is invalid.");
      return false;
    }
    stem->storedPath = blob->file.getFullPathName();
    stem->sourcePath = stem->storedPath;
    stem->fileName = blob->file.getFileName();
  }

  if (downloaded.descriptor.artwork.has_value()) {
    const auto* artwork = findObject(downloaded, *downloaded.descriptor.artwork);
    if (artwork == nullptr || !artwork->file.existsAsFile()) {
      setError(errorMessage, "Cloud artwork is missing.");
      return false;
    }
    song.artworkPath = artwork->file.getFullPathName();
    song.artworkSource.clear();
  } else {
    song.artworkPath.clear();
  }

  if (downloaded.descriptor.waveform.has_value()) {
    const auto* waveform = findObject(downloaded, *downloaded.descriptor.waveform);
    const auto byteCount = static_cast<juce::int64>(kWaveformPreviewBinCount) *
                           static_cast<juce::int64>(sizeof(float));
    if (waveform == nullptr || !waveform->file.existsAsFile() ||
        waveform->file.getSize() != byteCount) {
      setError(errorMessage, "Cloud waveform is invalid.");
      return false;
    }
    juce::FileInputStream input(waveform->file);
    outWaveformBins.resize(kWaveformPreviewBinCount);
    if (!input.openedOk() ||
        input.read(outWaveformBins.data(), static_cast<int>(byteCount)) !=
            byteCount) {
      outWaveformBins.clear();
      setError(errorMessage, "Couldn't read cloud waveform.");
      return false;
    }
  }

  song.projectPath.clear();
  song.fileType = "FLAC";
  outSong = std::move(song);
  return true;
}
