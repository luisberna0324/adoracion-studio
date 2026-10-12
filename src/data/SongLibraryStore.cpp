#include "SongLibraryStore.h"
#include "ApplicationDataPaths.h"
#include "CloudSyncIndexStore.h"
#include "TeamsPreparationService.h"
#include "SessionPersistence.h"
#include "ProjectStore.h"
#include "AppSettings.h"
#include "AssetBootstrapManager.h"
#include "PortableFlacConversion.h"
#include "Localization.h"
#include "SessionAuthLogger.h"
#include "audio/StemCache.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

namespace {
constexpr double kMinPartDurationSec = 0.25;
constexpr double kArrangementEpsilon = 0.0001;

// Internal song storage is portable and lossless on every platform.  Keeping
// this uniform prevents a project created on macOS from reintroducing ALAC in
// a load path that must also work on Windows and Linux.
constexpr const char *kStoredStemExtension = ".flac";
constexpr const char *kFfmpegAudioCodec = "flac";
constexpr const char *kStorageFormatLabel = "FLAC";

// Project packages are exchanged between platforms, so their audio format
// must not depend on the operating system that created them. JUCE ships a
// native FLAC reader on every supported desktop platform.
constexpr const char *kProjectStemExtension = ".flac";
constexpr const char *kProjectFfmpegAudioCodec = "flac";
constexpr const char *kProjectStorageFormatLabel = "FLAC";

juce::File managedImportedProjectsDirectory() {
  auto directory = SongLibraryStore::getRootDirectory()
                       .getChildFile("imports")
                       .getChildFile("projects");
  directory.createDirectory();
  return directory;
}

bool repairManagedFilePathForCurrentSandbox(juce::String &path) {
#if JUCE_IOS
  path = path.trim();
  if (path.isEmpty() || juce::File(path).existsAsFile())
    return false;

  // Installing a development build can assign a new data-container UUID while
  // preserving Library/MTControls. Paths serialized in songs.json still carry
  // the previous UUID, so rebuild them from their stable path below the
  // managed root instead of treating the song as deleted.
  const auto normalized = path.replaceCharacter('\\', '/');
  const juce::String rootMarker("/MTControls/");
  const int markerIndex = normalized.lastIndexOf(rootMarker);
  if (markerIndex < 0)
    return false;

  const auto relativePath =
      normalized.substring(markerIndex + rootMarker.length());
  if (relativePath.isEmpty())
    return false;

  const auto currentRoot = SongLibraryStore::getRootDirectory();
  const auto relocated = currentRoot.getChildFile(relativePath);
  if (!relocated.isAChildOf(currentRoot) || !relocated.existsAsFile() ||
      relocated.getSize() <= 0)
    return false;

  path = relocated.getFullPathName();
  return true;
#else
  juce::ignoreUnused(path);
  return false;
#endif
}

bool isManagedImportedProjectFile(const juce::File &file) {
  return file.existsAsFile() && file.isAChildOf(managedImportedProjectsDirectory());
}

bool repairManagedImportedProjectPath(ImportedSongData &song) {
#if JUCE_IOS
  if (!isProjectBackedSong(song) || song.projectPath.isEmpty())
    return false;

  if (repairManagedFilePathForCurrentSandbox(song.projectPath)) {
    SessionAuthLogger::write(
        "[ProjectLibrary] rebased iOS project path id=" + song.projectId);
    return true;
  }

  const juce::File previousPath(song.projectPath);
  if (previousPath.existsAsFile())
    return false;

  // iOS can assign a new data-container UUID when a development build is
  // replaced. Imported projects move with the app data, but absolute paths in
  // songs.json retain the old UUID. Recover the package by its unique import
  // filename under the current sandbox root.
  const auto relocated =
      managedImportedProjectsDirectory().getChildFile(previousPath.getFileName());
  if (!relocated.existsAsFile())
    return false;

  song.projectPath = relocated.getFullPathName();
  SessionAuthLogger::write("[ProjectLibrary] repaired iOS project path id=" +
                           song.projectId + " path=" + song.projectPath);
  return true;
#else
  juce::ignoreUnused(song);
  return false;
#endif
}

bool repairManagedStemPathsForSong(ImportedSongData &song) {
  bool changed = false;
  int repairedPathCount = 0;
  for (auto &stem : song.stems) {
    if (repairManagedFilePathForCurrentSandbox(stem.storedPath)) {
      changed = true;
      ++repairedPathCount;
    }
    if (repairManagedFilePathForCurrentSandbox(stem.sourcePath)) {
      changed = true;
      ++repairedPathCount;
    }
  }
  if (changed) {
    SessionAuthLogger::write(
        "[SongLibrary] rebased iOS managed stem paths id=" + song.id +
        " paths=" + juce::String(repairedPathCount));
  }
  return changed;
}

bool hasPlayableLocalStem(const ImportedSongData &song) {
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;
    const auto stored = juce::File(stem.storedPath);
    if (stored.existsAsFile() && stored.getSize() > 0)
      return true;
    const auto source = juce::File(stem.sourcePath);
    if (source.existsAsFile() && source.getSize() > 0)
      return true;
  }
  return false;
}

bool nearlyEqual(double a, double b) {
  return std::abs(a - b) <= kArrangementEpsilon;
}

bool arrangementsEqual(const SongArrangementSnapshot &a,
                       const SongArrangementSnapshot &b) {
  if (a.parts.size() != b.parts.size() || a.cues != b.cues)
    return false;
  for (size_t i = 0; i < a.parts.size(); ++i) {
    const auto &lhs = a.parts[i];
    const auto &rhs = b.parts[i];
    if (lhs.id != rhs.id)
      return false;
    if (lhs.name != rhs.name)
      return false;
    if (lhs.color != rhs.color)
      return false;
    if (lhs.utilityCue != rhs.utilityCue ||
        lhs.muteInstruments != rhs.muteInstruments ||
        lhs.skipPlayback != rhs.skipPlayback)
      return false;
    if (lhs.sourcePartId != rhs.sourcePartId)
      return false;
    if (lhs.automation != rhs.automation)
      return false;
    if (!nearlyEqual(lhs.startSec, rhs.startSec) ||
        !nearlyEqual(lhs.endSec, rhs.endSec) ||
        !nearlyEqual(lhs.sourceStartSec, rhs.sourceStartSec) ||
        !nearlyEqual(lhs.sourceEndSec, rhs.sourceEndSec))
      return false;
  }
  return true;
}

bool windowsEqual(const SongWindowSnapshot &a, const SongWindowSnapshot &b) {
  return nearlyEqual(a.startSec, b.startSec) && nearlyEqual(a.endSec, b.endSec) &&
         nearlyEqual(a.fadeOutSec, b.fadeOutSec);
}

DynamicGuideSettings makeDefaultDynamicGuideSettings() {
  DynamicGuideSettings settings;
  settings.enabled = true;
  return settings;
}

DynamicGuideMixSnapshot makeDefaultDynamicGuideMixSnapshot() {
  return {};
}

ClickDynMixSnapshot makeDefaultClickDynMixSnapshot() {
  return {};
}

bool dynamicGuidesEqual(const DynamicGuideSettings &a,
                        const DynamicGuideSettings &b) {
  return a.enabled == b.enabled && a.timing == b.timing && a.mode == b.mode &&
         a.languagePolicy == b.languagePolicy &&
         a.guideChannelPolicy == b.guideChannelPolicy;
}

bool dynamicGuideMixesEqual(const DynamicGuideMixSnapshot &a,
                            const DynamicGuideMixSnapshot &b) {
  return juce::approximatelyEqual(a.volume, b.volume) &&
         juce::approximatelyEqual(a.gain, b.gain) && a.mute == b.mute &&
         a.solo == b.solo && a.outputChannel == b.outputChannel &&
         a.outputWidth == b.outputWidth;
}

ClickDynMixSnapshot normalizeClickDynMixSnapshot(ClickDynMixSnapshot mix) {
  mix.divisionMask = mix.divisionMask & 0x07;
  mix.volume = juce::jlimit(0.0f, 1.0f, mix.volume);
  mix.gain = juce::jlimit(0.0f, 4.0f, mix.gain);
  mix.outputChannel = juce::jmax(1, mix.outputChannel);
  mix.outputWidth = juce::jlimit(1, 2, mix.outputWidth);
  return mix;
}

ProjectSongSettings projectSettingsFromItem(const SetlistItem &item,
                                            const ImportedSongData &song) {
  juce::ignoreUnused(song);
  ProjectSongSettings settings;
  settings.mix = item.mix;
  settings.pitchSemitones = item.pitchSemitones;
  settings.tempoBpm = item.tempoBpm;
  settings.window = item.window;
  settings.windowCustomized = item.windowCustomized;
  settings.arrangement = item.arrangement;
  settings.arrangementCustomized = item.arrangementCustomized;
  settings.dynamicGuide = item.dynamicGuide;
  settings.dynamicGuideCustomized = item.dynamicGuideCustomized;
  settings.dynamicGuideMix = item.dynamicGuideMix;
  settings.clickDynMix = item.clickDynMix;
  return settings;
}

SetlistItem projectSettingsCacheItem(const juce::String &songId,
                                     const ProjectSongSettings &settings) {
  SetlistItem item;
  item.songId = songId;
  item.mix = settings.mix;
  item.pitchSemitones = settings.pitchSemitones;
  item.tempoBpm = settings.tempoBpm;
  item.window = settings.window;
  item.windowCustomized = settings.windowCustomized;
  item.arrangement = settings.arrangement;
  item.arrangementCustomized = settings.arrangementCustomized;
  item.dynamicGuide = settings.dynamicGuide;
  item.dynamicGuideCustomized = settings.dynamicGuideCustomized;
  item.dynamicGuideMix = settings.dynamicGuideMix;
  item.clickDynMix = settings.clickDynMix;
  return item;
}

bool clickDynMixesEqual(const ClickDynMixSnapshot &a,
                        const ClickDynMixSnapshot &b) {
  return a.divisionMask == b.divisionMask && a.enabled == b.enabled &&
         a.metronomeEnabled == b.metronomeEnabled &&
         juce::approximatelyEqual(a.volume, b.volume) &&
         juce::approximatelyEqual(a.gain, b.gain) && a.mute == b.mute &&
         a.solo == b.solo && a.outputChannel == b.outputChannel &&
         a.outputWidth == b.outputWidth;
}

const ImportedStemData *findStemById(const ImportedSongData &song,
                                     const juce::String &stemId) {
  if (stemId.isEmpty())
    return nullptr;
  const auto it =
      std::find_if(song.stems.begin(), song.stems.end(),
                   [&stemId](const ImportedStemData &stem) {
                     return stem.stemId == stemId;
                   });
  if (it == song.stems.end())
    return nullptr;
  return &(*it);
}

bool fileIsInsideDirectory(const juce::File &file, const juce::File &directory) {
  if (!file.existsAsFile() || !directory.exists())
    return false;
  auto dirPath = directory.getFullPathName();
  if (!dirPath.endsWithChar(juce::File::getSeparatorChar()))
    dirPath << juce::File::getSeparatorString();
  return file.getFullPathName().startsWithIgnoreCase(dirPath);
}

juce::String normalizeArtworkExtension(const juce::String &extension) {
  auto ext = extension.trim().toLowerCase();
  if (ext.isEmpty())
    return ".jpg";
  if (!ext.startsWithChar('.'))
    ext = "." + ext;
  if (ext == ".jpeg")
    return ".jpg";
  if (ext == ".jpg" || ext == ".png" || ext == ".webp")
    return ext;
  return ".jpg";
}

juce::String inferArtworkExtensionFromSource(const juce::String &source) {
  auto clean = source.upToFirstOccurrenceOf("?", false, false)
                   .upToFirstOccurrenceOf("#", false, false);
  const juce::File asFile(clean);
  return normalizeArtworkExtension(asFile.getFileExtension());
}

void clearArtworkFilesInDirectory(const juce::File &directory) {
  if (!directory.isDirectory())
    return;
  const auto files =
      directory.findChildFiles(juce::File::findFiles, false, "artwork.*");
  for (const auto &file : files) {
    if (file.existsAsFile())
      file.deleteFile();
  }
}

bool copyArtworkIntoDirectory(const juce::File &sourceFile,
                              const juce::File &directory,
                              juce::String &outArtworkPath) {
  juce::MemoryBlock artworkBytes;
  if (!sourceFile.existsAsFile() ||
      !sourceFile.loadFileAsData(artworkBytes) || artworkBytes.getSize() == 0)
    return false;
  // The saved cover may itself be the source. Read it before replacing the
  // target; only remove covers with other extensions after the write works.
  const auto ext =
      normalizeArtworkExtension(sourceFile.getFileExtension());
  const auto target = directory.getChildFile("artwork" + ext);
  if (directory.createDirectory().failed() ||
      !target.replaceWithData(artworkBytes.getData(), artworkBytes.getSize()))
    return false;
  for (const auto &old : directory.findChildFiles(juce::File::findFiles,
                                                   false, "artwork.*"))
    if (old != target)
      old.deleteFile();
  outArtworkPath = target.getFullPathName();
  return true;
}

bool downloadArtworkIntoDirectory(const juce::String &url,
                                  const juce::File &directory,
                                  juce::String &outArtworkPath) {
  // Older projects retain their source metadata, but opening/saving them must
  // never fetch remote catalog artwork. Manual images and local copies remain.
  juce::ignoreUnused(url, directory, outArtworkPath);
  return false;
}

bool persistSongArtwork(ImportedSongData &song, const juce::File &targetDirectory,
                        const ImportedSongData *previousSong,
                        juce::String &errorMessage) {
  song.artworkSource = song.artworkSource.trim();
  if (song.artworkSource.isEmpty()) {
    song.artworkPath.clear();
    clearArtworkFilesInDirectory(targetDirectory);
    return true;
  }

  if (song.artworkSource.startsWithIgnoreCase("manual:")) {
    const auto manualPath = song.artworkSource.fromFirstOccurrenceOf(
        "manual:", false, false);
    const juce::File manualFile(manualPath);
    if (copyArtworkIntoDirectory(manualFile, targetDirectory, song.artworkPath))
      return true;

    // Si la ruta manual original ya no existe, intentamos conservar la artwork
    // previa de esta canción (misma fuente).
    if (previousSong != nullptr &&
        previousSong->artworkSource.trim() == song.artworkSource.trim()) {
      const juce::File prevArtwork(previousSong->artworkPath);
      if (copyArtworkIntoDirectory(prevArtwork, targetDirectory, song.artworkPath))
        return true;
    }

    errorMessage = "Artwork file not found: " + manualPath;
    return false;
  }

  if (previousSong != nullptr &&
      previousSong->artworkSource.trim() == song.artworkSource.trim()) {
    const juce::File prevArtwork(previousSong->artworkPath);
    if (copyArtworkIntoDirectory(prevArtwork, targetDirectory, song.artworkPath))
      return true;
  }

  // Keep an existing local cover even if its historical source is remote.
  if (copyArtworkIntoDirectory(juce::File(song.artworkPath), targetDirectory,
                               song.artworkPath))
    return true;

  if (downloadArtworkIntoDirectory(song.artworkSource, targetDirectory,
                                   song.artworkPath)) {
    return true;
  }

  // No bloqueamos el guardado si no se pudo descargar artwork remoto.
  song.artworkPath.clear();
  errorMessage.clear();
  return true;
}

void followEditedBaseTempoInDefaultMap(ImportedSongData &edited,
                                       const ImportedSongData &previous) {
  if (edited.tempoMap.regions.size() != 1 ||
      previous.tempoMap.regions.size() != 1)
    return;
  const auto &oldRegion = previous.tempoMap.regions.front();
  auto &region = edited.tempoMap.regions.front();
  const double oldBpm = previous.bpm > 0 ? previous.bpm : 120.0;
  const auto oldMeter = previous.timeSignature.trim().isNotEmpty()
                            ? previous.timeSignature.trim()
                            : juce::String("4/4");
  if (std::abs(oldRegion.startSec) > 0.01 ||
      std::abs(oldRegion.endSec - estimateSongLengthSeconds(previous)) > 0.05 ||
      std::abs(oldRegion.bpm - oldBpm) > 0.01 ||
      oldRegion.timeSignature != oldMeter ||
      oldRegion.clickDivision != ClickDivision::Quarter ||
      !oldRegion.accentEnabled)
    return;
  region.bpm = edited.bpm > 0 ? edited.bpm : 120.0;
  region.timeSignature = edited.timeSignature.trim().isNotEmpty()
                             ? edited.timeSignature.trim()
                             : juce::String("4/4");
  region.endSec = estimateSongLengthSeconds(edited);
}

bool repairArtworkPathForSong(ImportedSongData &song) {
  const auto songId = song.id.trim();
  song.artworkPath = song.artworkPath.trim();
  if (repairManagedFilePathForCurrentSandbox(song.artworkPath))
    return true;
  if (songId.isEmpty()) {
    if (song.artworkPath.isNotEmpty() &&
        !juce::File(song.artworkPath).existsAsFile()) {
      song.artworkPath.clear();
      return true;
    }
    return false;
  }

  auto songDir =
      ApplicationDataPaths::applicationDataDirectory()
          .getChildFile("MTControls")
          .getChildFile("audio")
          .getChildFile(songId);

  if (song.artworkPath.isNotEmpty()) {
    const juce::File configured(song.artworkPath);
    if (configured.existsAsFile()) {
      const auto inSongDir =
          songDir.getChildFile(configured.getFileName()).getFullPathName();
      if (!song.artworkPath.equalsIgnoreCase(inSongDir) &&
          juce::File(inSongDir).existsAsFile()) {
        song.artworkPath = inSongDir;
        return true;
      }
      return false;
    }

    const auto fallbackByName =
        songDir.getChildFile(configured.getFileName());
    if (fallbackByName.existsAsFile()) {
      song.artworkPath = fallbackByName.getFullPathName();
      return true;
    }
  }

  const auto discovered =
      songDir.findChildFiles(juce::File::findFiles, false, "artwork.*");
  if (!discovered.isEmpty()) {
    const auto detectedPath = discovered.getFirst().getFullPathName();
    if (!song.artworkPath.equalsIgnoreCase(detectedPath)) {
      song.artworkPath = detectedPath;
      return true;
    }
    return false;
  }

  if (song.artworkPath.isNotEmpty()) {
    song.artworkPath.clear();
    return true;
  }
  return false;
}
} // namespace

std::vector<ImportedSongData> SongLibraryStore::loadSongs() {
  return loadLibrarySnapshot().songs;
}

LibraryState SongLibraryStore::loadLibrarySnapshot() {
  PersistedSessionSnapshot session;
  SessionPersistence::load(session);
  return TeamsPreparationService::filterLibraryForAccount(loadLibrary(), session.uid);
}

bool SongLibraryStore::setActiveSetlistId(const juce::String &setlistId) {
  auto library = loadLibrary();
  if (!setlistExists(library, setlistId))
    return false;
  library.activeSetlistId = setlistId;
  return saveLibrary(library);
}

juce::String SongLibraryStore::createSetlist(const juce::String &name) {
  auto library = loadLibrary();
  SetlistData setlist;
  setlist.id = juce::Uuid().toString();
  setlist.name =
      name.isNotEmpty() ? name : L10n::tr("Nuevo Setlist", "New Setlist");
  const auto now = juce::Time::getCurrentTime().toMilliseconds();
  setlist.createdMs = now;
  setlist.updatedMs = now;
  library.setlists.push_back(std::move(setlist));
  if (library.activeSetlistId.isEmpty())
    library.activeSetlistId = library.setlists.back().id;
  if (!saveLibrary(library))
    return {};
  return library.setlists.back().id;
}

bool SongLibraryStore::importPreparedSetlist(SetlistData setlist) {
  if (setlist.id.trim().isEmpty() || setlist.name.trim().isEmpty())
    return false;
  auto library = loadLibrary();
  // A repeat download must preserve the musician's local rehearsal changes.
  if (findSetlistById(library, setlist.id) != nullptr)
    return true;
  for (const auto &item : setlist.items)
    if (findSongById(library.songs, item.songId) == nullptr)
      return false;
  const auto now = juce::Time::currentTimeMillis();
  setlist.createdMs = setlist.updatedMs = now;
  for (auto &item : setlist.items) {
    if (item.itemId.isEmpty()) item.itemId = juce::Uuid().toString();
    item.updatedMs = now;
  }
  if (!setlist.items.empty()) setlist.items.back().transitionToNext.autoPlay = false;
  library.setlists.push_back(std::move(setlist));
  return saveLibrary(library);
}

bool SongLibraryStore::renameSetlist(const juce::String &setlistId,
                                    const juce::String &name) {
  auto library = loadLibrary();
  auto *setlist = findSetlistById(library, setlistId);
  if (setlist == nullptr)
    return false;
  setlist->name = name;
  setlist->updatedMs = juce::Time::getCurrentTime().toMilliseconds();
  return saveLibrary(library);
}

bool SongLibraryStore::deleteSetlist(const juce::String &setlistId) {
  auto library = loadLibrary();
  if (library.setlists.size() <= 1)
    return false;
  auto it = std::remove_if(
      library.setlists.begin(), library.setlists.end(),
      [&setlistId](const SetlistData &setlist) {
        return setlist.id == setlistId;
      });
  if (it == library.setlists.end())
    return false;
  library.setlists.erase(it, library.setlists.end());
  if (!setlistExists(library, library.activeSetlistId))
    library.activeSetlistId =
        library.setlists.empty() ? juce::String()
                                 : library.setlists.front().id;
  return saveLibrary(library);
}

bool SongLibraryStore::addSongToSetlist(const juce::String &setlistId,
                                       const juce::String &songId) {
  auto library = loadLibrary();
  auto *setlist = findSetlistById(library, setlistId);
  if (setlist == nullptr)
    return false;
  const auto *song = findSongById(library.songs, songId);
  if (song == nullptr)
    return false;
  const bool exists =
      std::any_of(setlist->items.begin(), setlist->items.end(),
                  [&songId](const SetlistItem &item) {
                    return item.songId == songId;
                  });
  if (exists)
    return true;
  auto item = createSetlistItem(*song);
  if (!isProjectBackedSong(*song)) {
    const SetlistItem *canonicalItem = nullptr;
    for (const auto &candidateSetlist : library.setlists) {
      for (const auto &candidateItem : candidateSetlist.items) {
        if (candidateItem.songId != songId)
          continue;
        if (canonicalItem == nullptr ||
            candidateItem.updatedMs > canonicalItem->updatedMs)
          canonicalItem = &candidateItem;
      }
    }

    if (canonicalItem != nullptr) {
      item.mix = canonicalItem->mix;
      item.pitchSemitones = canonicalItem->pitchSemitones;
      item.tempoBpm = canonicalItem->tempoBpm;
      item.window = canonicalItem->window;
      item.windowCustomized = canonicalItem->windowCustomized;
      item.arrangement = canonicalItem->arrangement;
      item.arrangementCustomized = canonicalItem->arrangementCustomized;
      item.dynamicGuide = canonicalItem->dynamicGuide;
      item.dynamicGuideCustomized = canonicalItem->dynamicGuideCustomized;
      item.dynamicGuideMix = canonicalItem->dynamicGuideMix;
      item.clickDynMix = canonicalItem->clickDynMix;
    }
  }
  setlist->items.push_back(std::move(item));
  setlist->updatedMs = juce::Time::getCurrentTime().toMilliseconds();
  return saveLibrary(library);
}

bool SongLibraryStore::removeSongFromSetlist(const juce::String &setlistId,
                                            const juce::String &songId) {
  auto library = loadLibrary();
  auto *setlist = findSetlistById(library, setlistId);
  if (setlist == nullptr)
    return false;
  const auto before = setlist->items.size();
  setlist->items.erase(
      std::remove_if(setlist->items.begin(), setlist->items.end(),
                     [&songId](const SetlistItem &item) {
                       return item.songId == songId;
                     }),
      setlist->items.end());
  if (setlist->items.size() == before)
    return false;
  if (!setlist->items.empty())
    setlist->items.back().transitionToNext.autoPlay = false;
  setlist->updatedMs = juce::Time::getCurrentTime().toMilliseconds();
  return saveLibrary(library);
}

bool SongLibraryStore::deleteSong(const juce::String &songId) {
  const auto trimmedSongId = songId.trim();
  if (trimmedSongId.isEmpty())
    return false;

  auto library = loadLibrary();
  auto songIt =
      std::find_if(library.songs.begin(), library.songs.end(),
                   [&trimmedSongId](const ImportedSongData &song) {
                     return song.id == trimmedSongId;
                   });
  if (songIt == library.songs.end())
    return false;

  // Capture local-only resources before erasing the entry. A project package
  // belongs to the user and is intentionally excluded from this cleanup.
  const auto removedSong = *songIt;
  std::vector<juce::File> removableCloudObjects;
  const auto cloudContent = getRootDirectory().getChildFile("cloud-content");
  const auto captureCloudObject = [&removableCloudObjects, &cloudContent](
                                      const juce::String &path) {
    const juce::File file(path);
    if (file.existsAsFile() && file.isAChildOf(cloudContent))
      removableCloudObjects.push_back(file);
  };
  for (const auto &stem : removedSong.stems) {
    captureCloudObject(stem.storedPath);
    captureCloudObject(stem.sourcePath);
  }
  captureCloudObject(removedSong.artworkPath);

  const auto now = juce::Time::getCurrentTime().toMilliseconds();
  library.songs.erase(songIt);
  for (auto &setlist : library.setlists) {
    const auto before = setlist.items.size();
    setlist.items.erase(
        std::remove_if(setlist.items.begin(), setlist.items.end(),
                       [&trimmedSongId](const SetlistItem &item) {
                         return item.songId == trimmedSongId;
                       }),
        setlist.items.end());
    if (setlist.items.size() != before)
      setlist.updatedMs = now;
    if (!setlist.items.empty())
      setlist.items.back().transitionToNext.autoPlay = false;
  }

  if (!saveLibrary(library))
    return false;

  getWaveformCacheDir().getChildFile(trimmedSongId + ".bin").deleteFile();
  getAudioRoot().getChildFile(trimmedSongId).deleteRecursively();
  // Flush any pending settings writes for this song's project before
  // discarding local state. Without this, a settings update queued by
  // the message thread could write song-settings.json into
  // project-cache/<id>/ AFTER we have already deleted the directory,
  // leaving an orphan file. See perf/analysis_20260912/INFORME.md §4.5.
  if (removedSong.projectPath.isNotEmpty())
    ProjectStore::flushPendingSettings(juce::File(removedSong.projectPath));
  ProjectStore::discardLocalState(projectIdentityForSong(removedSong),
                                  juce::File(removedSong.projectPath));
  CloudSyncIndexStore::removeByProjectId(projectIdentityForSong(removedSong));
  // StemCache FLAC precompute lives under decoded/<songId>/. The
  // precompute only ran when the song was once loaded as legacy
  // (non-FLAC), so this is a no-op for songs that were never
  // precomputed. See perf/analysis_20260912/INFORME.md §2.1.
  StemCache::removeSongCache(trimmedSongId);

  // Cloud objects are content-addressed and may be shared by another local
  // download. Delete only the objects that no remaining library entry uses.
  std::unordered_set<std::string> stillReferencedCloudPaths;
  for (const auto &song : library.songs) {
    const auto collectPath = [&stillReferencedCloudPaths, &cloudContent](
                                 const juce::String &path) {
      const juce::File file(path);
      if (file.isAChildOf(cloudContent))
        stillReferencedCloudPaths.insert(file.getFullPathName().toStdString());
    };
    for (const auto &stem : song.stems) {
      collectPath(stem.storedPath);
      collectPath(stem.sourcePath);
    }
    collectPath(song.artworkPath);
  }
  for (const auto &file : removableCloudObjects) {
    if (stillReferencedCloudPaths.count(file.getFullPathName().toStdString()) == 0)
      file.deleteFile();
  }
  SessionAuthLogger::write("[SongLibrary] deleted local song id=" + trimmedSongId +
                           " projectId=" + projectIdentityForSong(removedSong) +
                           " (project package and cloud remote preserved)");
  return true;
}

bool SongLibraryStore::moveSongInSetlist(const juce::String &setlistId,
                                         int fromIndex, int toIndex) {
  auto library = loadLibrary();
  auto *setlist = findSetlistById(library, setlistId);
  if (setlist == nullptr)
    return false;
  const int count = static_cast<int>(setlist->items.size());
  if (!juce::isPositiveAndBelow(fromIndex, count) ||
      !juce::isPositiveAndBelow(toIndex, count))
    return false;
  if (fromIndex == toIndex)
    return true;
  auto item = setlist->items[static_cast<size_t>(fromIndex)];
  setlist->items.erase(setlist->items.begin() + fromIndex);
  setlist->items.insert(setlist->items.begin() + toIndex, item);
  if (!setlist->items.empty())
    setlist->items.back().transitionToNext.autoPlay = false;
  setlist->updatedMs = juce::Time::getCurrentTime().toMilliseconds();
  return saveLibrary(library);
}

bool SongLibraryStore::setAutoPlayNextForSetlist(
    const juce::String &setlistId, const juce::String &songId,
    const bool enabled) {
  if (setlistId.isEmpty() || songId.isEmpty())
    return false;

  auto library = loadLibrary();
  ensureDefaultSetlist(library);
  auto *setlist = findSetlistById(library, setlistId);
  if (setlist == nullptr)
    return false;

  auto it = std::find_if(setlist->items.begin(), setlist->items.end(),
                         [&songId](const SetlistItem &item) {
                           return item.songId == songId;
                         });
  if (it == setlist->items.end())
    return false;
  if (enabled && std::next(it) == setlist->items.end())
    return false;
  if (it->transitionToNext.autoPlay == enabled)
    return true;

  const auto now = juce::Time::getCurrentTime().toMilliseconds();
  it->transitionToNext.autoPlay = enabled;
  it->updatedMs = now;
  setlist->updatedMs = now;
  return saveLibrary(library);
}

bool SongLibraryStore::appendSong(ImportedSongData song, juce::String *errorMessage,
                                   const SongImportProgressFn &onProgress) {
  SessionAuthLogger::write(
      "[SongImportStore] appendSong begin title=\"" + song.title.trim() +
      "\" id=" + song.id + " stems=" +
      juce::String(static_cast<int>(song.stems.size())));
  juce::String localError;
  const auto mapStemProgress = [&onProgress](double t, const juce::String &s) {
    if (onProgress)
      onProgress(0.02 + 0.78 * juce::jlimit(0.0, 1.0, t), s);
  };
  if (!copyStemFiles(song, localError, mapStemProgress)) {
    SessionAuthLogger::write(
        "[SongImportStore] appendSong failed in copyStemFiles error=\"" +
        localError + "\"");
    if (errorMessage != nullptr)
      *errorMessage = localError;
    return false;
  }

  if (onProgress)
    onProgress(0.84, L10n::tr(u8"Actualizando biblioteca...",
                              u8"Updating library..."));
  auto library = loadLibrary();
  ensureSongHasStemIds(song);
  sortStemsForStripOrder(song.stems);
  library.songs.push_back(song);
  ensureDefaultSetlist(library);
  if (onProgress)
    onProgress(0.92, L10n::tr(u8"Guardando cambios...", u8"Saving changes..."));
  const bool saved = saveLibrary(library);
  if (!saved && errorMessage != nullptr && localError.isEmpty())
    *errorMessage = "Couldn't persist song metadata.";
  if (!saved) {
    SessionAuthLogger::write("[SongImportStore] appendSong failed: saveLibrary=false");
  }
  if (saved && onProgress)
    onProgress(0.96, L10n::tr(u8"Canción guardada", "Song saved"));
  if (saved) {
    SessionAuthLogger::write("[SongImportStore] appendSong saved id=" + song.id);
  }
  return saved;
}

bool SongLibraryStore::registerProjectSong(ImportedSongData &song) {
  song.id = song.id.trim();
  song.projectId = song.projectId.trim();
  song.projectPath = song.projectPath.trim();
  if (song.id.isEmpty() || song.projectPath.isEmpty())
    return false;
  if (song.projectId.isEmpty())
    song.projectId = song.id;
  auto library = loadLibrary();
  ensureSongHasStemIds(song);
  sortStemsForStripOrder(song.stems);

  // `projectId` is embedded in the portable manifest and survives imports,
  // moves and iOS sandbox UUID changes. Using an absolute import path as the
  // identity used to create a second library entry and a second multi-hundred
  // MB package every time the same project was selected from Files.
  std::vector<juce::File> supersededPackages;
  std::vector<juce::String> duplicateSongIds;
  auto canonical = library.songs.end();
  for (auto it = library.songs.begin(); it != library.songs.end(); ++it) {
    if (!isProjectBackedSong(*it) ||
        it->projectId.trim() != song.projectId)
      continue;
    if (canonical == library.songs.end())
      canonical = it;
    else
      duplicateSongIds.push_back(it->id);
    if (it->projectPath != song.projectPath)
      supersededPackages.emplace_back(it->projectPath);
  }

  if (canonical != library.songs.end()) {
    song.id = canonical->id;
    *canonical = song;

    if (!duplicateSongIds.empty()) {
      for (auto &setlist : library.setlists) {
        bool setlistChanged = false;
        for (auto &item : setlist.items) {
          if (std::find(duplicateSongIds.begin(), duplicateSongIds.end(),
                        item.songId) == duplicateSongIds.end())
            continue;
          item.songId = song.id;
          setlistChanged = true;
        }
        if (setlistChanged)
          setlist.updatedMs = juce::Time::getCurrentTime().toMilliseconds();
      }
      library.songs.erase(
          std::remove_if(library.songs.begin(), library.songs.end(),
                         [&duplicateSongIds](const ImportedSongData &candidate) {
                           return std::find(duplicateSongIds.begin(),
                                            duplicateSongIds.end(),
                                            candidate.id) != duplicateSongIds.end();
                         }),
          library.songs.end());
    }
  } else {
    // A project exported from a legacy song may share its manifest ID with a
    // non-project entry. Preserve the latter and generate a library-only ID.
    const bool localIdAlreadyUsed = std::any_of(
        library.songs.begin(), library.songs.end(),
        [&song](const ImportedSongData &candidate) {
          return candidate.id == song.id;
        });
    if (localIdAlreadyUsed)
      song.id = juce::Uuid().toString();
    library.songs.push_back(song);
  }
  ensureDefaultSetlist(library);
  const bool saved = saveLibrary(library);
  if (!saved)
    return false;

  for (const auto &package : supersededPackages) {
    if (package == juce::File(song.projectPath) ||
        !isManagedImportedProjectFile(package))
      continue;
    ProjectStore::flushPendingSettings(package);
    if (package.deleteFile()) {
      SessionAuthLogger::write(
          "[ProjectLibrary] removed superseded imported package path=" +
          package.getFullPathName());
    }
  }
  return true;
}

bool SongLibraryStore::registerCloudCachedSong(ImportedSongData &song) {
  song.id = song.id.trim();
  song.projectId = song.projectId.trim();
  song.projectPath.clear();
  if (song.id.isEmpty() || song.projectId.isEmpty() || song.stems.empty())
    return false;

  auto library = loadLibrary();
  ensureSongHasStemIds(song);
  sortStemsForStripOrder(song.stems);

  auto existing = std::find_if(
      library.songs.begin(), library.songs.end(), [&song](const auto& candidate) {
        return !isProjectBackedSong(candidate) &&
               candidate.projectId.trim() == song.projectId;
      });
  if (existing != library.songs.end()) {
    song.id = existing->id;
    *existing = song;
  } else {
    const bool localIdAlreadyUsed = std::any_of(
        library.songs.begin(), library.songs.end(),
        [&song](const auto& candidate) { return candidate.id == song.id; });
    if (localIdAlreadyUsed)
      song.id = juce::Uuid().toString();
    library.songs.push_back(song);
  }

  ensureDefaultSetlist(library);
  return saveLibrary(library);
}

bool SongLibraryStore::prepareLegacyProjectMigration(
    const juce::String &songId, ImportedSongData &outSong,
    SetlistItem &outSettingsItem, juce::String &outSourceSetlistName,
    juce::String *errorMessage) {
  if (errorMessage != nullptr)
    errorMessage->clear();
  const auto trimmedSongId = songId.trim();
  auto library = loadLibrary();
  const auto *song = findSongById(library.songs, trimmedSongId);
  if (song == nullptr) {
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr("La canción legacy ya no existe.",
                               "Legacy song no longer exists.");
    return false;
  }
  if (isProjectBackedSong(*song)) {
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr("La canción ya pertenece a un proyecto.",
                               "Song is already project-backed.");
    return false;
  }

  outSong = *song;
  // Cloud-cached songs retain their portable identity when explicitly
  // converted; plain legacy songs already have an empty projectId here.
  outSong.projectPath.clear();
  outSettingsItem = createSetlistItem(*song);
  outSourceSetlistName.clear();

  auto copyItemFromSetlist = [&](const SetlistData &setlist) {
    const auto item = std::find_if(
        setlist.items.begin(), setlist.items.end(),
        [&trimmedSongId](const SetlistItem &candidate) {
          return candidate.songId == trimmedSongId;
        });
    if (item == setlist.items.end())
      return false;
    outSettingsItem = *item;
    outSourceSetlistName = setlist.name;
    return true;
  };

  const auto activeSetlist = std::find_if(
      library.setlists.begin(), library.setlists.end(),
      [&library](const SetlistData &setlist) {
        return setlist.id == library.activeSetlistId;
      });
  bool foundSettings =
      activeSetlist != library.setlists.end() &&
      copyItemFromSetlist(*activeSetlist);
  if (!foundSettings) {
    for (const auto &setlist : library.setlists) {
      if (copyItemFromSetlist(setlist)) {
        foundSettings = true;
        break;
      }
    }
  }
  if (!foundSettings)
    outSourceSetlistName = L10n::tr("Valores predeterminados",
                                    "Default settings");
  return true;
}

bool SongLibraryStore::finalizeLegacyProjectMigration(
    const juce::String &legacySongId, ImportedSongData projectSong,
    juce::String *errorMessage) {
  if (errorMessage != nullptr)
    errorMessage->clear();
  const auto trimmedSongId = legacySongId.trim();
  if (trimmedSongId.isEmpty() || !isProjectBackedSong(projectSong)) {
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr("El proyecto validado no es compatible.",
                               "Validated project is not compatible.");
    return false;
  }

  auto library = loadLibrary();
  const auto songIt = std::find_if(
      library.songs.begin(), library.songs.end(),
      [&trimmedSongId](const ImportedSongData &candidate) {
        return candidate.id == trimmedSongId;
      });
  if (songIt == library.songs.end() || isProjectBackedSong(*songIt)) {
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr("La entrada legacy cambió durante la conversión.",
                               "Legacy entry changed during conversion.");
    return false;
  }

  const auto legacySongBackup = *songIt;
  const auto legacyCreatedMs = legacySongBackup.createdMs;
  projectSong.id = trimmedSongId;
  if (projectSong.projectId.trim().isEmpty())
    projectSong.projectId = trimmedSongId;
  if (projectSong.createdMs <= 0)
    projectSong.createdMs = legacyCreatedMs;

  const auto legacyAudioDirectory =
      getRootDirectory().getChildFile("audio").getChildFile(trimmedSongId);
  const auto migrationDirectory =
      getRootDirectory().getChildFile("migration-staging");
  const auto stagedLegacyDirectory = migrationDirectory.getChildFile(
      trimmedSongId + "-" + juce::Uuid().toString());
  bool stagedLegacyAudio = false;
  if (legacyAudioDirectory.exists()) {
    migrationDirectory.createDirectory();
    if (!legacyAudioDirectory.moveFileTo(stagedLegacyDirectory)) {
      if (errorMessage != nullptr)
        *errorMessage = L10n::tr(
            "No se pudo preparar la carpeta legacy para eliminarla.",
            "Couldn't stage the legacy folder for removal.");
      return false;
    }
    stagedLegacyAudio = true;
  }

  *songIt = std::move(projectSong);
  if (!saveLibrary(library)) {
    if (stagedLegacyAudio)
      stagedLegacyDirectory.moveFileTo(legacyAudioDirectory);
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr(
          "No se pudo actualizar la biblioteca; se restauró la canción legacy.",
          "Couldn't update the library; the legacy song was restored.");
    return false;
  }

  if (stagedLegacyAudio && !stagedLegacyDirectory.deleteRecursively()) {
    *songIt = legacySongBackup;
    const bool restoredLibrary = saveLibrary(library);
    const bool restoredAudio =
        stagedLegacyDirectory.exists()
            ? stagedLegacyDirectory.moveFileTo(legacyAudioDirectory)
            : legacyAudioDirectory.exists();
    SessionAuthLogger::write(
        "[ProjectMigration] Couldn't remove staged legacy directory=" +
        stagedLegacyDirectory.getFullPathName() +
        " restoredLibrary=" + juce::String(restoredLibrary ? 1 : 0) +
        " restoredAudio=" + juce::String(restoredAudio ? 1 : 0));
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr(
          "No se pudo eliminar la estructura legacy; la conversión fue revertida.",
          "Couldn't remove the legacy structure; conversion was rolled back.");
    return false;
  }
  if (migrationDirectory.isDirectory() &&
      migrationDirectory.getNumberOfChildFiles(
          juce::File::findFilesAndDirectories) == 0)
    migrationDirectory.deleteRecursively();
  // The legacy song was precomputed into decoded/<songId>/ when it was
  // first loaded as non-FLAC. After conversion the project ships its
  // own FLAC stems, so the cache is permanently dead weight. See
  // perf/analysis_20260912/INFORME.md §2.2.
  StemCache::removeSongCache(trimmedSongId);
  return true;
}

bool SongLibraryStore::updateSong(ImportedSongData song, juce::String *errorMessage,
                                  const SongImportProgressFn &onProgress) {
  song.id = song.id.trim();
  if (song.id.isEmpty()) {
    if (errorMessage != nullptr) {
      *errorMessage = L10n::tr(u8"No se pudo identificar la canción a editar.",
                               "Couldn't identify the song to edit.");
    }
    return false;
  }

  if (onProgress) {
    onProgress(0.02,
               L10n::tr(u8"Preparando actualización...", u8"Preparing update..."));
  }

  auto library = loadLibrary();
  auto songIt =
      std::find_if(library.songs.begin(), library.songs.end(),
                   [&song](const ImportedSongData &candidate) {
                     return candidate.id == song.id;
                   });
  if (songIt == library.songs.end()) {
    if (errorMessage != nullptr) {
      *errorMessage = L10n::tr(u8"La canción ya no existe en la biblioteca.",
                               "Song no longer exists in the library.");
    }
    return false;
  }

  const auto previousSong = *songIt;
  ensureSongHasStemIds(song);

  if (isProjectBackedSong(previousSong)) {
    const juce::File projectFile(previousSong.projectPath);
    ProjectDocument original;
    juce::String projectError;
    if (!ProjectStore::open(projectFile, original, &projectError, false)) {
      if (errorMessage != nullptr)
        *errorMessage = projectError;
      return false;
    }

    song.title = song.title.trim();
    song.artist = song.artist.trim();
    song.key = song.key.trim();
    song.timeSignature = song.timeSignature.trim();
    song.projectId = projectIdentityForSong(previousSong);
    song.projectPath = previousSong.projectPath;
    song.createdMs = previousSong.createdMs;
    song.fileType = "FLAC";
    if (song.artworkSource.trim() != previousSong.artworkSource.trim())
      song.artworkPath.clear();
    song.stems.erase(std::remove_if(song.stems.begin(), song.stems.end(),
                                    [](const ImportedStemData &stem) {
                                      return !stem.include;
                                    }),
                     song.stems.end());
    if (song.stems.empty()) {
      if (errorMessage != nullptr)
        *errorMessage = L10n::tr("Debes dejar al menos un stem cargado.",
                                 "At least one stem must remain loaded.");
      return false;
    }
    sortStemsForStripOrder(song.stems);
    bool arrangementChanged = false;
    song.arrangement = reconcileArrangementForSong(
        song.arrangement.parts.empty() && song.arrangement.cues.empty()
            ? previousSong.arrangement
                                       : song.arrangement,
        song, arrangementChanged);
    followEditedBaseTempoInDefaultMap(song, previousSong);

    auto settings = original.settings;
    ProjectStore::getCachedSettings(projectFile, settings);
    bool mixChanged = false;
    settings.mix = reconcileMixForSong(settings.mix, song, mixChanged);
    if (!settings.windowCustomized)
      settings.window = createDefaultWindowForSong(song);
    settings.arrangement = reconcileArrangementForSong(settings.arrangement,
                                                       song, arrangementChanged);
    const double previousBaseBpm = previousSong.bpm > 0 ? previousSong.bpm : 120.0;
    if (std::abs(settings.tempoBpm - previousBaseBpm) < 0.01)
      settings.tempoBpm = song.bpm > 0 ? song.bpm : 120.0;

    bool stemsChanged = song.stems.size() != previousSong.stems.size();
    for (size_t i = 0; !stemsChanged && i < song.stems.size(); ++i) {
      const auto &oldStem = previousSong.stems[i];
      const auto &newStem = song.stems[i];
      stemsChanged = oldStem.stemId != newStem.stemId ||
                     oldStem.storedPath != newStem.storedPath ||
                     oldStem.sourcePath != newStem.sourcePath ||
                     oldStem.category != newStem.category;
    }

    ProjectDocument updated;
    updated.song = song;
    updated.settings = settings;
    updated.waveformBins = stemsChanged
                               ? buildMixedWaveformBins(song, kWaveformPreviewBinCount)
                               : original.waveformBins;
    const auto suffix = juce::Uuid().toString().substring(0, 8);
    const auto baseName = projectFile.getFileNameWithoutExtension();
    const auto stagedProject = projectFile.getSiblingFile(
        baseName + "_edit_stage_" + suffix + ProjectStore::kFileExtension);
    const auto backupProject = projectFile.getSiblingFile(
        baseName + "_edit_backup_" + suffix + ProjectStore::kFileExtension);
    if (!ProjectStore::save(
            updated, stagedProject, &projectError, nullptr,
            [&onProgress](double value, const juce::String &phase) {
              if (onProgress)
                onProgress(0.05 + 0.75 * value, phase);
            })) {
      stagedProject.deleteFile();
      if (errorMessage != nullptr)
        *errorMessage = projectError;
      return false;
    }
    ProjectStore::forgetRecentProject(stagedProject);
    if (!projectFile.moveFileTo(backupProject)) {
      stagedProject.deleteFile();
      if (errorMessage != nullptr)
        *errorMessage = L10n::tr("No se pudo apartar el proyecto original.",
                                 "Couldn't back up the original project.");
      return false;
    }
    if (!stagedProject.moveFileTo(projectFile)) {
      const bool restored = backupProject.moveFileTo(projectFile);
      stagedProject.deleteFile();
      if (errorMessage != nullptr)
        *errorMessage = restored
                            ? L10n::tr("No se pudo instalar el proyecto actualizado.",
                                       "Couldn't install the updated project.")
                            : L10n::tr("No se pudo instalar el proyecto. El original quedó en: ",
                                       "Couldn't install the project. The original is at: ") +
                                  backupProject.getFullPathName();
      return false;
    }
    auto restoreOriginalProject = [&] {
      const auto failedProject = projectFile.getSiblingFile(
          baseName + "_edit_failed_" + suffix + ProjectStore::kFileExtension);
      if (!projectFile.moveFileTo(failedProject))
        return false;
      if (!backupProject.moveFileTo(projectFile)) {
        failedProject.moveFileTo(projectFile);
        return false;
      }
      failedProject.deleteFile();
      clearArtworkFilesInDirectory(
          ProjectStore::getCacheDirectory(song.projectId));
      ProjectDocument restored;
      juce::String restoreError;
      ProjectStore::open(projectFile, restored, &restoreError, false);
      return true;
    };

    // The currently playing processor may still hold stems in this cache.
    // Refresh only the cover now; the next song load re-extracts audio after
    // the processor has released its file handles.
    clearArtworkFilesInDirectory(
        ProjectStore::getCacheDirectory(song.projectId));
    ProjectDocument reopened;
    if (!ProjectStore::open(projectFile, reopened, &projectError, false)) {
      const bool restored = restoreOriginalProject();
      if (errorMessage != nullptr)
        *errorMessage = restored ? projectError
                                 : projectError + " (original project: " +
                                       backupProject.getFullPathName() + ")";
      return false;
    }
    reopened.song.id = previousSong.id;
    *songIt = reopened.song;
    const auto now = juce::Time::getCurrentTime().toMilliseconds();
    for (auto &setlist : library.setlists) {
      bool changed = false;
      for (auto &item : setlist.items) {
        if (item.songId != song.id)
          continue;
        bool itemChanged = false;
        item.mix = reconcileMixForSong(item.mix, reopened.song, itemChanged);
        if (!item.windowCustomized)
          item.window = createDefaultWindowForSong(reopened.song);
        item.arrangement = reconcileArrangementForSong(item.arrangement,
                                                       reopened.song, itemChanged);
        if (std::abs(item.tempoBpm - previousBaseBpm) < 0.01) {
          item.tempoBpm = song.bpm > 0 ? song.bpm : 120.0;
          itemChanged = true;
        }
        if (itemChanged) {
          item.updatedMs = now;
          changed = true;
        }
      }
      if (changed)
        setlist.updatedMs = now;
    }
    if (!saveLibrary(library)) {
      const bool restored = restoreOriginalProject();
      if (errorMessage != nullptr)
        *errorMessage = restored
                            ? L10n::tr("No se pudo guardar la biblioteca; se restauró el proyecto anterior.",
                                       "Couldn't save the library; the previous project was restored.")
                            : L10n::tr("No se pudo guardar la biblioteca. El proyecto anterior quedó en: ",
                                       "Couldn't save the library. The previous project is at: ") +
                                  backupProject.getFullPathName();
      return false;
    }
    if (ProjectStore::writeCachedSettingsFromFile(projectFile, settings))
      ProjectStore::scheduleSettingsSave(projectFile, song.projectId, settings);
    backupProject.deleteFile();
    if (!reopened.waveformBins.empty())
      saveWaveformCache(song.id, reopened.waveformBins);
    if (onProgress)
      onProgress(1.0, L10n::tr("Canción actualizada", "Song updated"));
    return true;
  }

  song.title = song.title.trim();
  song.artist = song.artist.trim();
  song.key = song.key.trim();
  song.timeSignature = song.timeSignature.trim();
  const bool hasIncludedStems =
      std::any_of(song.stems.begin(), song.stems.end(),
                  [](const ImportedStemData &stem) { return stem.include; });
  if (hasIncludedStems) {
    song.fileType = "FLAC";
  } else if (song.fileType.trim().isEmpty()) {
    song.fileType = previousSong.fileType.trim().isNotEmpty()
                        ? previousSong.fileType.trim()
                        : "FLAC";
  }
  if (song.createdMs <= 0)
    song.createdMs = previousSong.createdMs;

  std::vector<ImportedStemData> includedStems;
  includedStems.reserve(song.stems.size());
  for (auto &stem : song.stems) {
    if (!stem.include)
      continue;
    stem.displayName = stem.displayName.trim();
    stem.category = stem.category.trim();
    if (stem.stemId.isEmpty())
      stem.stemId = juce::Uuid().toString();
    if (stem.fileName.trim().isEmpty()) {
      const auto sourceFile = juce::File(stem.sourcePath);
      if (sourceFile.existsAsFile())
        stem.fileName = sourceFile.getFileName();
      else {
        const auto storedFile = juce::File(stem.storedPath);
        if (storedFile.existsAsFile())
          stem.fileName = storedFile.getFileName();
      }
    }
    includedStems.push_back(std::move(stem));
  }
  song.stems = std::move(includedStems);
  sortStemsForStripOrder(song.stems);

  if (song.stems.empty()) {
    if (errorMessage != nullptr) {
      *errorMessage = L10n::tr("Debes dejar al menos un stem cargado.",
                               "At least one stem must remain loaded.");
    }
    return false;
  }

  if (song.arrangement.parts.empty() && song.arrangement.cues.empty() &&
      (!previousSong.arrangement.parts.empty() || !previousSong.arrangement.cues.empty()))
    song.arrangement = previousSong.arrangement;
  bool arrangementChanged = false;
  song.arrangement =
      reconcileArrangementForSong(song.arrangement, song, arrangementChanged);
  juce::ignoreUnused(arrangementChanged);
  followEditedBaseTempoInDefaultMap(song, previousSong);

  bool stemsChanged = song.stems.size() != previousSong.stems.size();

  const auto mapStemProgress = [&onProgress](const double t,
                                             const juce::String &phase) {
    if (onProgress)
      onProgress(0.04 + 0.72 * juce::jlimit(0.0, 1.0, t), phase);
  };

  auto audioRoot = getAudioRoot();
  auto songDir = audioRoot.getChildFile(song.id);
  const auto tempSuffix = juce::Uuid().toString().substring(0, 8);
  auto stageDir = audioRoot.getChildFile(song.id + "_edit_stage_" + tempSuffix);
  auto backupDir = audioRoot.getChildFile(song.id + "_edit_backup_" + tempSuffix);
  stageDir.createDirectory();
  if (!stageDir.isDirectory()) {
    if (errorMessage != nullptr) {
      *errorMessage = L10n::tr(u8"No se pudo crear la carpeta temporal de edición.",
                               "Couldn't create temporary edit folder.");
    }
    return false;
  }

  const int stemTotal = static_cast<int>(song.stems.size());
  const int denom = juce::jmax(1, stemTotal);

  auto failWithCleanup = [&](const juce::String &msg) {
    if (stageDir.exists())
      stageDir.deleteRecursively();
    if (errorMessage != nullptr) {
      *errorMessage = msg;
      // A failed rollback must never delete the only surviving original audio.
      if (backupDir.exists()) {
        *errorMessage += L10n::tr(u8" Audio anterior conservado en: ",
                                  " Previous audio preserved at: ") +
                         backupDir.getFullPathName();
      }
    }
    return false;
  };

  int stemDone = 0;
  for (auto &stem : song.stems) {
    if (onProgress) {
      const double frac =
          static_cast<double>(stemDone) / static_cast<double>(denom);
      mapStemProgress(
          frac, L10n::tr(juce::String("Procesando: ") + stem.displayName,
                         juce::String("Processing: ") + stem.displayName));
    }

    const auto legalStemId = juce::File::createLegalFileName(stem.stemId);
    if (legalStemId.isEmpty())
      return failWithCleanup("Invalid stem id while updating song.");

    juce::File sourceFile;
    const auto fromStored = juce::File(stem.storedPath);
    if (fromStored.existsAsFile())
      sourceFile = fromStored;
    if (!sourceFile.existsAsFile()) {
      const auto fromSource = juce::File(stem.sourcePath);
      if (fromSource.existsAsFile())
        sourceFile = fromSource;
    }
    if (!sourceFile.existsAsFile()) {
      if (const auto *previousStem = findStemById(previousSong, stem.stemId)) {
        const auto previousStored = juce::File(previousStem->storedPath);
        if (previousStored.existsAsFile())
          sourceFile = previousStored;
        if (!sourceFile.existsAsFile()) {
          const auto previousSource = juce::File(previousStem->sourcePath);
          if (previousSource.existsAsFile())
            sourceFile = previousSource;
        }
      }
    }
    if (!sourceFile.existsAsFile())
      return failWithCleanup("Stem source file not found: " + stem.sourcePath);

    const auto *previousStem = findStemById(previousSong, stem.stemId);
    if (previousStem == nullptr) {
      stemsChanged = true;
    } else {
      const auto prevStored = juce::File(previousStem->storedPath);
      const auto prevSource = juce::File(previousStem->sourcePath);
      const bool matchesPrevStored =
          prevStored.existsAsFile() &&
          sourceFile.getFullPathName()
              .equalsIgnoreCase(prevStored.getFullPathName());
      const bool matchesPrevSource =
          prevSource.existsAsFile() &&
          sourceFile.getFullPathName()
              .equalsIgnoreCase(prevSource.getFullPathName());
      if (!matchesPrevStored && !matchesPrevSource)
        stemsChanged = true;
    }

    const auto srcExtension = sourceFile.getFileExtension();
    const auto stagedFile =
        stageDir.getChildFile(legalStemId + kStoredStemExtension);
    juce::File stagedOutput = stagedFile;
    if (stagedFile.existsAsFile())
      stagedFile.deleteFile();

    // FLAC is already the storage format, regardless of its origin or name.
    // Preserve its bytes rather than running an unnecessary encode on edits.
    if (sourceFile.hasFileExtension(
            juce::String(kStoredStemExtension).substring(1))) {
      if (!sourceFile.copyFileTo(stagedFile))
        return failWithCleanup("Failed copying existing stem '" +
                               stem.displayName + "'.");
    } else {
      juce::String convertError;
      if (!convertStemToStorageFormat(sourceFile, stagedFile, convertError)) {
        stagedFile.deleteFile();
        const auto fallbackExtension =
            srcExtension.isNotEmpty() ? srcExtension : juce::String(".wav");
        stagedOutput = stageDir.getChildFile(legalStemId + fallbackExtension);
        if (stagedOutput.existsAsFile())
          stagedOutput.deleteFile();
        if (!sourceFile.copyFileTo(stagedOutput)) {
          return failWithCleanup("Failed converting stem '" + stem.displayName +
                                 "' and fallback copy also failed: " +
                                 convertError);
        }
        stemsChanged = true;
      } else {
        stemsChanged = true;
      }
    }

    if (!stagedOutput.existsAsFile() || stagedOutput.getSize() <= 0) {
      return failWithCleanup("Stored stem file is missing or empty: " +
                             stagedOutput.getFullPathName());
    }

    stem.fileName = stagedOutput.getFileName();
    stem.storedPath =
        songDir.getChildFile(stagedOutput.getFileName()).getFullPathName();
    if (stem.sourcePath.trim().isEmpty())
      stem.sourcePath = sourceFile.getFullPathName();

    ++stemDone;
    mapStemProgress(
        static_cast<double>(stemDone) / static_cast<double>(denom),
        L10n::tr(juce::String("Listo: ") + stem.displayName,
                 juce::String("Done: ") + stem.displayName));
  }

  {
    juce::String artworkError;
    if (!persistSongArtwork(song, stageDir, &previousSong, artworkError))
      return failWithCleanup(artworkError);
  }

  if (onProgress)
    onProgress(0.80,
               L10n::tr(u8"Aplicando cambios en audio...", u8"Applying audio changes..."));

  const bool hadSongDir = songDir.exists();
  if (hadSongDir) {
    if (!songDir.moveFileTo(backupDir))
      return failWithCleanup("Couldn't stage existing song audio directory.");
  }

  if (!stageDir.moveFileTo(songDir)) {
    if (hadSongDir && backupDir.exists()) {
      backupDir.moveFileTo(songDir);
    }
    return failWithCleanup("Couldn't move staged audio into song directory.");
  }

  if (song.artworkPath.trim().isNotEmpty()) {
    const auto stagedArtworkFile =
        juce::File(song.artworkPath.trim()).getFileName();
    if (stagedArtworkFile.isNotEmpty())
      song.artworkPath = songDir.getChildFile(stagedArtworkFile).getFullPathName();
  }
  juce::ignoreUnused(repairArtworkPathForSong(song));

  *songIt = song;

  const auto now = juce::Time::getCurrentTime().toMilliseconds();
  const double previousBaseBpm = previousSong.bpm > 0 ? previousSong.bpm : 120.0;
  for (auto &setlist : library.setlists) {
    bool setlistChanged = false;
    for (auto &item : setlist.items) {
      if (item.songId != song.id)
        continue;
      bool itemChanged = false;
      item.mix = reconcileMixForSong(item.mix, song, itemChanged);
      if (!item.windowCustomized)
        item.window = createDefaultWindowForSong(song);
      item.arrangement = reconcileArrangementForSong(item.arrangement, song,
                                                     itemChanged);
      if (std::abs(item.tempoBpm - previousBaseBpm) < 0.01) {
        item.tempoBpm = song.bpm > 0 ? song.bpm : 120.0;
        itemChanged = true;
      }
      const auto defaultArrangement = createDefaultArrangementForSong(song);
      if (!item.arrangementCustomized &&
          !arrangementsEqual(item.arrangement, defaultArrangement)) {
        item.arrangementCustomized = true;
        itemChanged = true;
      }
      if (itemChanged) {
        item.updatedMs = now;
        setlistChanged = true;
      }
    }
    if (setlistChanged)
      setlist.updatedMs = now;
  }

  if (onProgress)
    onProgress(0.86, L10n::tr(u8"Guardando cambios...", u8"Saving changes..."));

  const bool saved = saveLibrary(library);
  if (!saved) {
    if (songDir.exists())
      songDir.deleteRecursively();
    if (hadSongDir && backupDir.exists())
      backupDir.moveFileTo(songDir);
    return failWithCleanup(
        L10n::tr(u8"No se pudo guardar la canción editada.",
                  "Couldn't persist edited song."));
  }

  if (backupDir.exists())
    backupDir.deleteRecursively();

  if (stemsChanged) {
    if (onProgress) {
      onProgress(0.90, L10n::tr(u8"Generando forma de onda...",
                                u8"Generating waveform preview..."));
    }
    const auto waveformPhase = L10n::tr(u8"Generando forma de onda...",
                                        u8"Generating waveform preview...");
    auto bins = buildMixedWaveformBins(
        song, kWaveformPreviewBinCount,
        [&onProgress, &waveformPhase](const double wf01) {
          if (onProgress)
            onProgress(0.90 + 0.09 * juce::jlimit(0.0, 1.0, wf01),
                       waveformPhase);
        });
    if (bins.empty()) {
      getWaveformCacheDir().getChildFile(song.id + ".bin").deleteFile();
    } else {
      saveWaveformCache(song.id, bins);
    }
  }

  if (onProgress)
    onProgress(1.0, L10n::tr(u8"Canción actualizada", "Song updated"));

  return true;
}

bool SongLibraryStore::updateSongMetadata(ImportedSongData song,
                                          juce::String *errorMessage) {
  if (errorMessage != nullptr)
    errorMessage->clear();

  song.id = song.id.trim();
  if (song.id.isEmpty()) {
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr(u8"No se pudo identificar la canción a editar.",
                               "Couldn't identify the song to edit.");
    return false;
  }

  auto library = loadLibrary();
  auto songIt =
      std::find_if(library.songs.begin(), library.songs.end(),
                   [&song](const ImportedSongData &candidate) {
                     return candidate.id == song.id;
                   });
  if (songIt == library.songs.end()) {
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr(u8"La canción ya no existe en la biblioteca.",
                               "Song no longer exists in the library.");
    return false;
  }

  const auto previousSong = *songIt;
  if (isProjectBackedSong(previousSong)) {
    auto updated = previousSong;
    updated.title = song.title;
    updated.artist = song.artist;
    updated.bpm = song.bpm;
    updated.key = song.key;
    updated.timeSignature = song.timeSignature;
    updated.artworkSource = song.artworkSource;
    updated.artworkPath = song.artworkPath;
    return updateSong(std::move(updated), errorMessage);
  }
  auto updated = previousSong;
  updated.title = song.title.trim();
  updated.artist = song.artist.trim();
  updated.bpm = song.bpm;
  updated.key = song.key.trim();
  updated.timeSignature = song.timeSignature.trim();
  updated.fileType = song.fileType.trim().isNotEmpty()
                         ? song.fileType.trim()
                         : previousSong.fileType;
  updated.artworkSource = song.artworkSource.trim();
  if (updated.artworkSource.isEmpty())
    updated.artworkPath.clear();
  followEditedBaseTempoInDefaultMap(updated, previousSong);

  const auto artworkDir = getAudioRoot().getChildFile(updated.id);
  artworkDir.createDirectory();
  juce::String artworkError;
  if (!persistSongArtwork(updated, artworkDir, &previousSong, artworkError)) {
    if (errorMessage != nullptr)
      *errorMessage = artworkError;
    return false;
  }
  juce::ignoreUnused(repairArtworkPathForSong(updated));

  *songIt = std::move(updated);
  const double previousBaseBpm = previousSong.bpm > 0 ? previousSong.bpm : 120.0;
  const auto now = juce::Time::getCurrentTime().toMilliseconds();
  for (auto &setlist : library.setlists) {
    bool changed = false;
    for (auto &item : setlist.items) {
      if (item.songId == song.id &&
          std::abs(item.tempoBpm - previousBaseBpm) < 0.01) {
        item.tempoBpm = song.bpm > 0 ? song.bpm : 120.0;
        item.updatedMs = now;
        changed = true;
      }
    }
    if (changed)
      setlist.updatedMs = now;
  }
  if (!saveLibrary(library)) {
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr(u8"No se pudo guardar la canción editada.",
                               "Couldn't persist edited song.");
    return false;
  }

  SessionAuthLogger::write("[SongImportStore] metadata-only update saved id=" +
                           song.id);
  return true;
}

bool SongLibraryStore::getActiveSetlistMix(const juce::String &songId,
                                           SongMixSnapshot &outMix) {
  if (songId.isEmpty())
    return false;
  auto library = loadLibrary();
  ensureDefaultSetlist(library);
  auto *setlist = findSetlistById(library, library.activeSetlistId);
  if (setlist == nullptr)
    return false;
  auto it = std::find_if(setlist->items.begin(), setlist->items.end(),
                         [&songId](const SetlistItem &item) {
                           return item.songId == songId;
                         });
  const auto *song = findSongById(library.songs, songId);
  if (song == nullptr)
    return false;

  if (it == setlist->items.end())
    return false;

  bool changed = false;
  it->mix = reconcileMixForSong(it->mix, *song, changed);

  outMix = it->mix;
  if (changed) {
    const auto now = juce::Time::getCurrentTime().toMilliseconds();
    it->updatedMs = now;
    setlist->updatedMs = now;
    saveLibrary(library);
  }
  return true;
}

bool SongLibraryStore::updateStemVolumeForActiveSetlist(
    const juce::String &songId, const juce::String &stemId, float volume) {
  return updateStemMixValue(songId, stemId,
                            [volume](StemMixSnapshot &mix) {
                              mix.volume = volume;
                            });
}

bool SongLibraryStore::updateMixForActiveSetlist(const juce::String &songId,
                                                 const SongMixSnapshot &mix) {
  return updateActiveSetlistItem(
      songId, [&mix](SetlistItem &item, const ImportedSongData &song) {
        bool changed = false;
        item.mix = reconcileMixForSong(mix, song, changed);
      });
}

bool SongLibraryStore::updateStemVolumesForActiveSetlist(
    const juce::String &songId,
    const std::vector<std::pair<juce::String, float>> &stemVolumes) {
  if (stemVolumes.empty())
    return true;

  return updateActiveSetlistItem(
      songId, [&stemVolumes](SetlistItem &item, const ImportedSongData &) {
        for (const auto &entry : stemVolumes) {
          const auto &stemId = entry.first;
          if (stemId.isEmpty())
            continue;

          auto it = std::find_if(
              item.mix.stems.begin(), item.mix.stems.end(),
              [&stemId](const StemMixSnapshot &mix) { return mix.stemId == stemId; });
          if (it == item.mix.stems.end()) {
            StemMixSnapshot mix;
            mix.stemId = stemId;
            item.mix.stems.push_back(mix);
            it = std::prev(item.mix.stems.end());
          }
          it->volume = juce::jlimit(0.0f, 1.0f, entry.second);
        }
      });
}

bool SongLibraryStore::updateStemGainForActiveSetlist(
    const juce::String &songId, const juce::String &stemId, float gain) {
  return updateStemMixValue(songId, stemId,
                            [gain](StemMixSnapshot &mix) {
                              mix.gain = gain;
                            });
}

bool SongLibraryStore::updateStemMuteForActiveSetlist(
    const juce::String &songId, const juce::String &stemId, bool muted) {
  return updateStemMixValue(songId, stemId,
                            [muted](StemMixSnapshot &mix) {
                              mix.mute = muted;
                            });
}

bool SongLibraryStore::updateStemSoloForActiveSetlist(
    const juce::String &songId, const juce::String &stemId, bool soloed) {
  return updateStemMixValue(songId, stemId,
                            [soloed](StemMixSnapshot &mix) {
                              mix.solo = soloed;
                            });
}

bool SongLibraryStore::updateStemOutputChannelForActiveSetlist(
    const juce::String &songId, const juce::String &stemId,
    const int outputChannel) {
  return updateStemMixValue(songId, stemId,
                            [outputChannel](StemMixSnapshot &mix) {
                              mix.outputChannel = juce::jmax(1, outputChannel);
                            });
}

bool SongLibraryStore::updateMasterVolumeForActiveSetlist(
    const juce::String &songId, float volume) {
  return updateMasterMixValue(songId, [volume](MasterMixSnapshot &mix) {
    mix.volume = volume;
  });
}

bool SongLibraryStore::updateMasterGainForActiveSetlist(
    const juce::String &songId, float gain) {
  return updateMasterMixValue(songId, [gain](MasterMixSnapshot &mix) {
    mix.gain = gain;
  });
}

bool SongLibraryStore::updateMasterMuteForActiveSetlist(
    const juce::String &songId, bool muted) {
  return updateMasterMixValue(songId, [muted](MasterMixSnapshot &mix) {
    mix.mute = muted;
  });
}

bool SongLibraryStore::getActiveSetlistPitchSemitones(
    const juce::String &songId, int &outSemitones) {
  if (songId.isEmpty())
    return false;
  auto library = loadLibrary();
  ensureDefaultSetlist(library);
  auto *setlist = findSetlistById(library, library.activeSetlistId);
  if (setlist == nullptr)
    return false;
  auto it = std::find_if(setlist->items.begin(), setlist->items.end(),
                         [&songId](const SetlistItem &item) {
                           return item.songId == songId;
                         });
  if (it == setlist->items.end())
    return false;
  outSemitones = juce::jlimit(-12, 12, it->pitchSemitones);
  return true;
}

bool SongLibraryStore::updatePitchSemitonesForActiveSetlist(
    const juce::String &songId, int semitones) {
  return updateActiveSetlistItem(
      songId, [semitones](SetlistItem &item, const ImportedSongData &) {
        item.pitchSemitones = juce::jlimit(-12, 12, semitones);
      });
}

bool SongLibraryStore::getActiveSetlistTempoBpm(const juce::String &songId,
                                                double &outBpm) {
  if (songId.isEmpty())
    return false;
  auto library = loadLibrary();
  ensureDefaultSetlist(library);
  auto *setlist = findSetlistById(library, library.activeSetlistId);
  if (setlist == nullptr)
    return false;
  auto it = std::find_if(setlist->items.begin(), setlist->items.end(),
                         [&songId](const SetlistItem &item) {
                           return item.songId == songId;
                         });
  if (it == setlist->items.end() || it->tempoBpm <= 0.0)
    return false;
  outBpm = juce::jlimit(20.0, 300.0, it->tempoBpm);
  return true;
}

bool SongLibraryStore::updateTempoBpmForActiveSetlist(
    const juce::String &songId, double bpm) {
  return updateActiveSetlistItem(
      songId, [bpm](SetlistItem &item, const ImportedSongData &song) {
        const double fallbackBpm = song.bpm > 0 ? static_cast<double>(song.bpm)
                                                : 120.0;
        const double normalized = bpm > 0.0 ? juce::jlimit(20.0, 300.0, bpm)
                                            : fallbackBpm;
        item.tempoBpm = normalized;
      });
}

bool SongLibraryStore::getActiveSetlistSongWindow(
    const juce::String &songId, SongWindowSnapshot &outWindow,
    bool &outIsCustomWindow) {
  if (songId.isEmpty())
    return false;

  auto library = loadLibrary();
  ensureDefaultSetlist(library);
  auto *setlist = findSetlistById(library, library.activeSetlistId);
  if (setlist == nullptr)
    return false;
  const auto *song = findSongById(library.songs, songId);
  if (song == nullptr)
    return false;

  auto it = std::find_if(setlist->items.begin(), setlist->items.end(),
                         [&songId](const SetlistItem &item) {
                           return item.songId == songId;
                         });

  if (it == setlist->items.end())
    return false;

  bool changed = false;
  it->window = reconcileWindowForSong(it->window, *song, changed);
  const auto defaultWindow = createDefaultWindowForSong(*song);
  if (!it->windowCustomized) {
    if (!windowsEqual(it->window, defaultWindow)) {
      it->window = defaultWindow;
      changed = true;
    }
  } else if (windowsEqual(it->window, defaultWindow)) {
    it->windowCustomized = false;
    changed = true;
  }

  outWindow = it->window;
  outIsCustomWindow = it->windowCustomized;
  if (changed) {
    const auto now = juce::Time::getCurrentTime().toMilliseconds();
    it->updatedMs = now;
    setlist->updatedMs = now;
    saveLibrary(library);
  }
  return true;
}

bool SongLibraryStore::updateSongWindowForActiveSetlist(
    const juce::String &songId, const SongWindowSnapshot &window,
    const bool isCustomWindow) {
  return updateActiveSetlistItem(
      songId, [&window, isCustomWindow](SetlistItem &item,
                                        const ImportedSongData &song) {
        bool changed = false;
        auto next = reconcileWindowForSong(window, song, changed);
        const auto defaultWindow = createDefaultWindowForSong(song);
        if (!isCustomWindow || windowsEqual(next, defaultWindow)) {
          item.window = defaultWindow;
          item.windowCustomized = false;
          return;
        }
        item.window = std::move(next);
        item.windowCustomized = true;
      });
}

bool SongLibraryStore::resetSongWindowForActiveSetlist(
    const juce::String &songId) {
  SongWindowSnapshot defaultWindow;
  return updateSongWindowForActiveSetlist(songId, defaultWindow, false);
}

bool SongLibraryStore::getSongArrangement(
    const juce::String &songId, SongArrangementSnapshot &outArrangement,
    bool &outIsCustomArrangement) {
  if (songId.isEmpty())
    return false;

  auto library = loadLibrary();
  auto it = std::find_if(library.songs.begin(), library.songs.end(),
                         [&songId](const ImportedSongData &song) {
                           return song.id == songId;
                         });
  if (it == library.songs.end())
    return false;

  if (isProjectBackedSong(*it)) {
    ProjectSongSettings settings;
    const juce::File projectFile(it->projectPath);
    bool hasSettings = projectFile.existsAsFile() &&
                       ProjectStore::getCachedSettings(projectFile, settings);
    const auto projectId = projectIdentityForSong(*it);
    if (!hasSettings)
      hasSettings = ProjectStore::getCachedSettings(projectId, settings);
    if (!hasSettings) {
      settings.arrangement = it->arrangement;
      settings.arrangementCustomized = !it->arrangement.parts.empty() ||
                                       !it->arrangement.cues.empty();
    }

    bool changed = false;
    auto normalized =
        reconcileArrangementForSong(settings.arrangement, *it, changed);
    const bool isCustomArrangement = settings.arrangementCustomized;
    if (changed) {
      settings.arrangement = normalized;
      ProjectStore::scheduleSettingsSave(projectFile, projectId,
                                         std::move(settings));
    }
    outArrangement = std::move(normalized);
    outIsCustomArrangement = isCustomArrangement;
    return true;
  }

  bool changed = false;
  auto normalized = reconcileArrangementForSong(it->arrangement, *it, changed);
  const bool arrangementChanged =
      changed || !arrangementsEqual(it->arrangement, normalized);
  if (arrangementChanged) {
    it->arrangement = normalized;
    saveLibrary(library);
  }

  outArrangement = arrangementChanged ? it->arrangement : normalized;
  outIsCustomArrangement = !outArrangement.parts.empty() ||
                           !outArrangement.cues.empty();
  return true;
}

bool SongLibraryStore::updateSongArrangement(
    const juce::String &songId, const SongArrangementSnapshot &arrangement,
    const bool isCustomArrangement) {
  if (songId.isEmpty())
    return false;

  auto library = loadLibrary();
  auto it = std::find_if(library.songs.begin(), library.songs.end(),
                         [&songId](const ImportedSongData &song) {
                           return song.id == songId;
                         });
  if (it == library.songs.end())
    return false;

  bool changed = false;
  auto normalized = reconcileArrangementForSong(arrangement, *it, changed);
  if (!isCustomArrangement)
    normalized = createDefaultArrangementForSong(*it);

  if (isProjectBackedSong(*it)) {
    ProjectSongSettings settings;
    const juce::File projectFile(it->projectPath);
    bool hasSettings = projectFile.existsAsFile() &&
                       ProjectStore::getCachedSettings(projectFile, settings);
    const auto projectId = projectIdentityForSong(*it);
    if (!hasSettings)
      hasSettings = ProjectStore::getCachedSettings(projectId, settings);
    if (!hasSettings) {
      settings.arrangement = it->arrangement;
      settings.arrangementCustomized = !it->arrangement.parts.empty() ||
                                       !it->arrangement.cues.empty();
    }

    const bool arrangementChanged =
        changed || !arrangementsEqual(settings.arrangement, normalized) ||
        settings.arrangementCustomized != isCustomArrangement;
    if (!arrangementChanged)
      return true;

    settings.arrangement = std::move(normalized);
    settings.arrangementCustomized = isCustomArrangement;
    ProjectStore::scheduleSettingsSave(projectFile, projectId,
                                       std::move(settings));
    return true;
  }

  const bool arrangementChanged =
      changed || !arrangementsEqual(it->arrangement, normalized);
  if (!arrangementChanged)
    return true;

  it->arrangement = std::move(normalized);
  return saveLibrary(library);
}

bool SongLibraryStore::resetSongArrangement(const juce::String &songId) {
  SongArrangementSnapshot emptyArrangement;
  return updateSongArrangement(songId, emptyArrangement, false);
}

bool SongLibraryStore::getActiveSetlistArrangement(
    const juce::String &songId, SongArrangementSnapshot &outArrangement,
    bool &outIsCustomArrangement) {
  return getSongArrangement(songId, outArrangement, outIsCustomArrangement);
}

bool SongLibraryStore::updateArrangementForActiveSetlist(
    const juce::String &songId, const SongArrangementSnapshot &arrangement,
    const bool isCustomArrangement) {
  return updateSongArrangement(songId, arrangement, isCustomArrangement);
}

bool SongLibraryStore::resetArrangementForActiveSetlist(
    const juce::String &songId) {
  return resetSongArrangement(songId);
}

bool SongLibraryStore::getActiveSetlistDynamicGuide(
    const juce::String &songId, DynamicGuideSettings &outSettings,
    bool &outIsCustomDynamicGuide) {
  if (songId.isEmpty())
    return false;

  auto library = loadLibrary();
  ensureDefaultSetlist(library);
  auto *setlist = findSetlistById(library, library.activeSetlistId);
  if (setlist == nullptr)
    return false;

  const auto *song = findSongById(library.songs, songId);
  if (song == nullptr)
    return false;

  if (isProjectBackedSong(*song)) {
    ProjectSongSettings settings;
    const juce::File projectFile(song->projectPath);
    bool hasSettings = projectFile.existsAsFile() &&
                       ProjectStore::getCachedSettings(projectFile, settings);
    if (!hasSettings)
      hasSettings = ProjectStore::getCachedSettings(
          projectIdentityForSong(*song), settings);
    if (!hasSettings)
      return false;
    outSettings = settings.dynamicGuide;
    outIsCustomDynamicGuide = settings.dynamicGuideCustomized;
    return true;
  }

  auto it = std::find_if(setlist->items.begin(), setlist->items.end(),
                         [&songId](const SetlistItem &item) {
                           return item.songId == songId;
                         });
  if (it == setlist->items.end())
    return false;

  const auto defaultSettings = makeDefaultDynamicGuideSettings();
  if (!it->dynamicGuideCustomized &&
      !dynamicGuidesEqual(it->dynamicGuide, defaultSettings)) {
    it->dynamicGuideCustomized = true;
    const auto now = juce::Time::getCurrentTime().toMilliseconds();
    it->updatedMs = now;
    setlist->updatedMs = now;
    saveLibrary(library);
  }

  outSettings = it->dynamicGuide;
  outIsCustomDynamicGuide = it->dynamicGuideCustomized;
  return true;
}

bool SongLibraryStore::updateDynamicGuideForActiveSetlist(
    const juce::String &songId, const DynamicGuideSettings &settings,
    const bool isCustomDynamicGuide) {
  return updateActiveSetlistItem(
      songId, [&settings, isCustomDynamicGuide](SetlistItem &item,
                                                const ImportedSongData &) {
        const auto defaultSettings = makeDefaultDynamicGuideSettings();
        if (!isCustomDynamicGuide || dynamicGuidesEqual(settings, defaultSettings)) {
          item.dynamicGuide = defaultSettings;
          item.dynamicGuideCustomized = false;
          return;
        }
        item.dynamicGuide = settings;
        item.dynamicGuideCustomized = true;
      });
}

bool SongLibraryStore::resetDynamicGuideForActiveSetlist(
    const juce::String &songId) {
  return updateDynamicGuideForActiveSetlist(songId,
                                            makeDefaultDynamicGuideSettings(),
                                            false);
}

bool SongLibraryStore::getActiveSetlistDynamicGuideMix(
    const juce::String &songId, DynamicGuideMixSnapshot &outMix) {
  if (songId.isEmpty())
    return false;

  auto library = loadLibrary();
  ensureDefaultSetlist(library);
  auto *setlist = findSetlistById(library, library.activeSetlistId);
  if (setlist == nullptr)
    return false;

  const auto *song = findSongById(library.songs, songId);
  if (song == nullptr)
    return false;
  if (isProjectBackedSong(*song)) {
    ProjectSongSettings settings;
    const juce::File projectFile(song->projectPath);
    bool hasSettings = projectFile.existsAsFile() &&
                       ProjectStore::getCachedSettings(projectFile, settings);
    if (!hasSettings)
      hasSettings = ProjectStore::getCachedSettings(
          projectIdentityForSong(*song), settings);
    if (!hasSettings)
      return false;
    outMix = settings.dynamicGuideMix;
    return true;
  }

  auto it = std::find_if(setlist->items.begin(), setlist->items.end(),
                         [&songId](const SetlistItem &item) {
                           return item.songId == songId;
                         });
  if (it == setlist->items.end())
    return false;

  outMix = it->dynamicGuideMix;
  return true;
}

bool SongLibraryStore::updateDynamicGuideMixForActiveSetlist(
    const juce::String &songId, const DynamicGuideMixSnapshot &mix) {
  return updateActiveSetlistItem(
      songId, [&mix](SetlistItem &item, const ImportedSongData &) {
        auto next = mix;
        next.volume = juce::jlimit(0.0f, 1.0f, next.volume);
        next.gain = juce::jlimit(0.0f, 2.0f, next.gain);
        next.outputChannel = juce::jmax(1, next.outputChannel);
        next.outputWidth = juce::jlimit(1, 2, next.outputWidth);
        item.dynamicGuideMix = next;
      });
}

bool SongLibraryStore::getActiveSetlistClickDynMix(
    const juce::String &songId, ClickDynMixSnapshot &outMix) {
  if (songId.isEmpty())
    return false;

  auto library = loadLibrary();
  ensureDefaultSetlist(library);
  auto *setlist = findSetlistById(library, library.activeSetlistId);
  if (setlist == nullptr)
    return false;

  const auto *song = findSongById(library.songs, songId);
  if (song == nullptr)
    return false;
  if (isProjectBackedSong(*song)) {
    ProjectSongSettings settings;
    const juce::File projectFile(song->projectPath);
    bool hasSettings = projectFile.existsAsFile() &&
                       ProjectStore::getCachedSettings(projectFile, settings);
    if (!hasSettings)
      hasSettings = ProjectStore::getCachedSettings(
          projectIdentityForSong(*song), settings);
    if (!hasSettings)
      return false;
    outMix = normalizeClickDynMixSnapshot(settings.clickDynMix);
    return true;
  }

  auto it = std::find_if(setlist->items.begin(), setlist->items.end(),
                         [&songId](const SetlistItem &item) {
                           return item.songId == songId;
                         });
  if (it == setlist->items.end())
    return false;

  outMix = normalizeClickDynMixSnapshot(it->clickDynMix);
  return true;
}

bool SongLibraryStore::updateClickDynMixForActiveSetlist(
    const juce::String &songId, const ClickDynMixSnapshot &mix) {
  return updateActiveSetlistItem(
      songId, [&mix](SetlistItem &item, const ImportedSongData &) {
        item.clickDynMix = normalizeClickDynMixSnapshot(mix);
      });
}

bool SongLibraryStore::saveWaveformCache(const juce::String &songId,
                                        const std::vector<float> &bins) {
  if (songId.isEmpty() || bins.empty())
    return false;
  const auto file = getWaveformCacheDir().getChildFile(songId + ".bin");
  return file.replaceWithData(bins.data(), bins.size() * sizeof(float));
}

std::vector<float> SongLibraryStore::loadWaveformCache(
    const juce::String &songId) {
  if (songId.isEmpty())
    return {};
  auto file = getWaveformCacheDir().getChildFile(songId + ".bin");
  if (!file.existsAsFile())
    return {};
  const auto size = file.getSize();
  const auto expectedSize =
      static_cast<juce::int64>(kWaveformPreviewBinCount) *
      static_cast<juce::int64>(sizeof(float));
  if (size != expectedSize)
    return {};
  const auto sizeBytes = static_cast<size_t>(size);
  if (sizeBytes > static_cast<size_t>(std::numeric_limits<int>::max()))
    return {};
  std::vector<float> bins(sizeBytes / sizeof(float));
  juce::FileInputStream in(file);
  if (in.failedToOpen())
    return {};
  if (in.read(bins.data(), static_cast<int>(sizeBytes)) !=
      static_cast<int>(sizeBytes))
    return {};
  return bins;
}

LibraryState SongLibraryStore::loadLibrary() {
  LibraryState library;
  bool needsSave = false;
  std::vector<juce::String> songsMissingArrangement;

  const auto dbFile = getDbFile();
  if (!dbFile.existsAsFile()) {
    ensureDefaultSetlist(library);
    return library;
  }

  const auto parsed = juce::JSON::parse(dbFile.loadFileAsString());
  auto *rootObj = parsed.getDynamicObject();
  if (rootObj == nullptr) {
    ensureDefaultSetlist(library);
    return library;
  }

  const int version = static_cast<int>(rootObj->getProperty("version"));
  auto *songsArray = rootObj->getProperty("songs").getArray();
  if (songsArray != nullptr) {
    library.songs.reserve(static_cast<size_t>(songsArray->size()));
    for (const auto &songVar : *songsArray) {
      bool songChanged = false;
      auto song = songFromVar(songVar, songChanged);
      if (songChanged)
        needsSave = true;
      if (repairManagedImportedProjectPath(song))
        needsSave = true;
      if (repairManagedStemPathsForSong(song))
        needsSave = true;
      if (ensureSongHasStemIds(song))
        needsSave = true;
      if (repairArtworkPathForSong(song))
        needsSave = true;
      if (isProjectBackedSong(song)) {
        const juce::File projectFile(song.projectPath);
        const juce::File artworkFile(song.artworkPath);
        if (projectFile.existsAsFile() && !artworkFile.existsAsFile()) {
          juce::String artworkError;
          if (ProjectStore::restoreArtwork(projectFile, song, &artworkError)) {
            needsSave = true;
            SessionAuthLogger::write(
                "[ProjectLibrary] restored project artwork id=" +
                song.projectId);
          } else if (artworkError.isNotEmpty()) {
            SessionAuthLogger::write(
                "[ProjectLibrary] couldn't restore project artwork id=" +
                song.projectId + " error=" + artworkError);
          }
        }
      } else if (!song.id.startsWith("team-") && !hasPlayableLocalStem(song)) {
        // A reinstall can preserve songs.json while local legacy/Cloud blobs
        // were removed with the previous sandbox. Do not leave an entry that
        // cannot be played; its setlist references are pruned on deserialization.
        SessionAuthLogger::write(
            "[SongLibrary] discarded unavailable local song id=" + song.id +
            " projectId=" + song.projectId);
        needsSave = true;
        continue;
      }
      // Team audio can be prepared again from its immutable revision. Retain
      // metadata and personal setlist adjustments while its cache is missing.
      bool hasSongArrangement = false;
      if (auto *songObj = songVar.getDynamicObject()) {
        hasSongArrangement = songObj->hasProperty("arrangement");
      }
      if (!hasSongArrangement && song.id.isNotEmpty()) {
        songsMissingArrangement.push_back(song.id);
        needsSave = true;
      }
      library.songs.push_back(std::move(song));
    }
  }

  if (version >= 2) {
    library.activeSetlistId =
        rootObj->getProperty("activeSetlistId").toString();
    auto *setlistsArray = rootObj->getProperty("setlists").getArray();
    if (setlistsArray != nullptr) {
      for (const auto &setlistVar : *setlistsArray) {
        auto setlist = setlistFromVar(setlistVar, library.songs, needsSave);
        if (setlist.id.isNotEmpty())
          library.setlists.push_back(std::move(setlist));
      }
    }
  } else {
    needsSave = true;
  }

  if (library.setlists.empty()) {
    ensureDefaultSetlist(library);
    needsSave = true;
  }

  if (library.activeSetlistId.isEmpty() ||
      !setlistExists(library, library.activeSetlistId)) {
    library.activeSetlistId =
        library.setlists.empty() ? juce::String()
                                 : library.setlists.front().id;
    needsSave = true;
  }

  // Migracion legacy: si la cancion no tenia arrangement propio,
  // promover arrangement desde setlists (prioridad: setlist activo).
  if (!songsMissingArrangement.empty()) {
    auto *activeSetlist = findSetlistById(library, library.activeSetlistId);

    for (const auto &songId : songsMissingArrangement) {
      auto songIt =
          std::find_if(library.songs.begin(), library.songs.end(),
                       [&songId](const ImportedSongData &song) {
                         return song.id == songId;
                       });
      if (songIt == library.songs.end())
        continue;

      SongArrangementSnapshot selectedLegacy;
      bool foundLegacy = false;
      auto tryPickFromSetlist = [&](const SetlistData &setlist) {
        for (const auto &item : setlist.items) {
          if (item.songId != songId ||
              (item.arrangement.parts.empty() && item.arrangement.cues.empty()))
            continue;
          selectedLegacy = item.arrangement;
          foundLegacy = true;
          return;
        }
      };

      if (activeSetlist != nullptr)
        tryPickFromSetlist(*activeSetlist);
      if (!foundLegacy) {
        for (const auto &setlist : library.setlists) {
          if (activeSetlist != nullptr && setlist.id == activeSetlist->id)
            continue;
          tryPickFromSetlist(setlist);
          if (foundLegacy)
            break;
        }
      }
      if (!foundLegacy)
        continue;

      bool changed = false;
      auto normalized =
          reconcileArrangementForSong(selectedLegacy, *songIt, changed);
      if (changed || !arrangementsEqual(songIt->arrangement, normalized)) {
        songIt->arrangement = std::move(normalized);
        needsSave = true;
      }
    }
  }

  if (needsSave)
    saveLibrary(library);

  return library;
}

bool SongLibraryStore::saveLibrary(const LibraryState &library) {
  juce::Array<juce::var> songVars;
  songVars.ensureStorageAllocated(static_cast<int>(library.songs.size()));
  for (const auto &song : library.songs)
    songVars.add(songToVar(song));

  juce::Array<juce::var> setlistVars;
  setlistVars.ensureStorageAllocated(
      static_cast<int>(library.setlists.size()));
  for (const auto &setlist : library.setlists)
    setlistVars.add(setlistToVar(setlist, library.songs));

  juce::var rootVar(new juce::DynamicObject());
  if (auto *root = rootVar.getDynamicObject()) {
    root->setProperty("version", 3);
    root->setProperty("activeSetlistId", library.activeSetlistId);
    root->setProperty("songs", juce::var(songVars));
    root->setProperty("setlists", juce::var(setlistVars));
  }

  const auto json = juce::JSON::toString(rootVar, true);
  // Check the complete write before replacing the current library. JUCE's
  // replaceWithText does not propagate appendText's result; a full disk must
  // never replace a healthy songs.json with an incomplete temporary file.
  const auto target = getDbFile();
  juce::TemporaryFile temporary(target, juce::TemporaryFile::useHiddenFile);
  const auto bytes = static_cast<size_t>(json.getNumBytesAsUTF8());
  {
    juce::FileOutputStream output(temporary.getFile());
    if (!output.openedOk() || !output.write(json.toRawUTF8(), bytes))
      return false;
    output.flush();
    if (output.getStatus().failed() ||
        temporary.getFile().getSize() != static_cast<juce::int64>(bytes))
      return false;
  }
  return temporary.overwriteTargetFileWithTemporary();
}

juce::File SongLibraryStore::getRootDirectory() {
  auto root =
      ApplicationDataPaths::applicationDataDirectory()
          .getChildFile("MTControls");
  root.createDirectory();
  root.getChildFile("audio").createDirectory();
  return root;
}

juce::File SongLibraryStore::getDbFile() {
  return getRootDirectory().getChildFile("songs.json");
}

juce::File SongLibraryStore::getAudioRoot() {
  auto audioRoot = getRootDirectory().getChildFile("audio");
  audioRoot.createDirectory();
  return audioRoot;
}

juce::File SongLibraryStore::getWaveformCacheDir() {
  auto dir = getRootDirectory().getChildFile("waveforms");
  dir.createDirectory();
  return dir;
}

juce::File SongLibraryStore::resolveBundledFfmpegBinary() {
  return AssetBootstrapManager::getAvailableEncoderBinary();
}

bool SongLibraryStore::convertStemWithFfmpeg(
    const juce::File &source, const juce::File &destination,
    const juce::String &audioCodec, const juce::String &formatLabel,
    juce::String &errorMessage, const double durationSec,
    const SongImportProgressFn &onProgress, const bool allowBootstrap) {
  errorMessage.clear();
  auto ffmpegBinary = resolveBundledFfmpegBinary();
  if (!ffmpegBinary.existsAsFile() && allowBootstrap) {
    juce::String bootstrapError;
    if (AssetBootstrapManager::ensureEncoderReady(&bootstrapError))
      ffmpegBinary = resolveBundledFfmpegBinary();
    else if (bootstrapError.isNotEmpty())
      errorMessage = bootstrapError;
  }

  if (!ffmpegBinary.existsAsFile()) {
    if (errorMessage.isEmpty()) {
      errorMessage = "Couldn't find bundled or downloaded ffmpeg binary for " +
                     formatLabel + " conversion.";
    }
    return false;
  }

  destination.getParentDirectory().createDirectory();
  if (destination.existsAsFile())
    destination.deleteFile();

  juce::StringArray args;
  args.add(ffmpegBinary.getFullPathName());
  args.add("-y");
  args.add("-v");
  args.add("error");
  args.add("-progress");
  args.add("pipe:1");
  args.add("-nostats");
  args.add("-i");
  args.add(source.getFullPathName());
  args.add("-map_metadata");
  args.add("-1");
  args.add("-vn");
  args.add("-c:a");
  args.add(audioCodec);
  args.add(destination.getFullPathName());

  juce::ChildProcess process;
  if (!process.start(args)) {
    errorMessage = "Couldn't launch ffmpeg conversion process.";
    return false;
  }

  juce::String output;
  juce::String pendingOutput;
  auto consumeOutput = [&](const char *bytes, const int byteCount) {
    const auto chunk = juce::String::fromUTF8(bytes, byteCount);
    pendingOutput += chunk;
    for (;;) {
      const auto newline = pendingOutput.indexOfChar('\n');
      if (newline < 0)
        break;
      const auto line = pendingOutput.substring(0, newline).trim();
      pendingOutput = pendingOutput.substring(newline + 1);
      if (line.startsWith("out_time_us=") ||
          line.startsWith("out_time_ms=")) {
        const auto encodedTime =
            line.fromFirstOccurrenceOf("=", false, false).getLargeIntValue();
        if (durationSec > 0.0 && onProgress) {
          const auto progress = static_cast<double>(encodedTime) /
                                (durationSec * 1000000.0);
          onProgress(juce::jlimit(0.0, 0.995, progress), {});
        }
      } else if (line == "progress=end" && onProgress) {
        onProgress(1.0, {});
      } else if (line.isNotEmpty() && !line.startsWith("bitrate=") &&
                 !line.startsWith("total_size=") &&
                 !line.startsWith("out_time=") &&
                 !line.startsWith("dup_frames=") &&
                 !line.startsWith("drop_frames=") &&
                 !line.startsWith("speed=") &&
                 !line.startsWith("progress=")) {
        output << line << "\n";
      }
    }
  };

  const auto startedAt = juce::Time::getMillisecondCounter();
  // ChildProcess::readProcessOutput waits for the requested byte count on
  // Windows and may buffer similarly through stdio on POSIX. A small buffer
  // lets each FFmpeg progress block reach the UI as soon as it is emitted.
  std::array<char, 64> processBuffer{};
  while (process.isRunning()) {
    const auto bytesRead = process.readProcessOutput(
        processBuffer.data(), static_cast<int>(processBuffer.size()));
    if (bytesRead > 0)
      consumeOutput(processBuffer.data(), bytesRead);
    else
      juce::Thread::sleep(15);

    if (juce::Time::getMillisecondCounter() - startedAt >=
        static_cast<juce::uint32>(10 * 60 * 1000)) {
      process.kill();
      errorMessage = formatLabel + " conversion timed out.";
      return false;
    }
  }
  for (;;) {
    const auto bytesRead = process.readProcessOutput(
        processBuffer.data(), static_cast<int>(processBuffer.size()));
    if (bytesRead <= 0)
      break;
    consumeOutput(processBuffer.data(), bytesRead);
  }

  if (pendingOutput.trim().isNotEmpty() &&
      !pendingOutput.trim().containsChar('='))
    output << pendingOutput.trim();
  output = output.trim();
  if (static_cast<int>(process.getExitCode()) != 0 || !destination.existsAsFile()) {
    if (destination.existsAsFile())
      destination.deleteFile();
    errorMessage = output.isNotEmpty() ? output
                                       : "ffmpeg failed converting stem to " +
                                             formatLabel + ".";
    return false;
  }

  return true;
}

bool SongLibraryStore::convertStemToStorageFormat(
    const juce::File &source, const juce::File &destination,
    juce::String &errorMessage, const double durationSec,
    const SongImportProgressFn &onProgress) {
  // Compatible legacy imports optimize locally, even when no optional desktop
  // encoder has been installed. Never download an encoder merely to optimize.
  if (portable_audio::convertToFlac(
          source, destination, errorMessage,
          [onProgress](double progress) {
            if (onProgress)
              onProgress(progress, {});
          }))
    return true;
#if JUCE_IOS || JUCE_ANDROID || MTCONTROLS_MAC_APP_STORE
  juce::ignoreUnused(durationSec);
  return false;
#else
  return convertStemWithFfmpeg(
      source, destination, kFfmpegAudioCodec, kStorageFormatLabel,
      errorMessage, durationSec, onProgress, false);
#endif
}

bool SongLibraryStore::convertStemForProject(const juce::File &source,
                                             const juce::File &destination,
                                             juce::String &errorMessage,
                                             const double durationSec,
                                             const SongImportProgressFn &onProgress) {
#if JUCE_IOS || JUCE_ANDROID || MTCONTROLS_MAC_APP_STORE
  return convertStemToStorageFormat(source, destination, errorMessage,
                                    durationSec, onProgress);
#else
  return convertStemWithFfmpeg(
      source, destination, kProjectFfmpegAudioCodec,
      kProjectStorageFormatLabel, errorMessage, durationSec, onProgress);
#endif
}

juce::String SongLibraryStore::projectStorageStemExtension() {
  return kProjectStemExtension;
}

bool SongLibraryStore::copyStemFiles(ImportedSongData &song, juce::String &errorMessage,
                                     const SongImportProgressFn &onProgress) {
  errorMessage.clear();
  if (song.id.isEmpty())
    song.id = juce::Uuid().toString();

  auto songDir = getAudioRoot().getChildFile(song.id);
  songDir.createDirectory();
  SessionAuthLogger::write("[SongImportStore] copyStemFiles begin id=" + song.id +
                           " dir=" + songDir.getFullPathName());
  auto failImport = [&songDir, &errorMessage](const juce::String &msg) {
    errorMessage = msg;
    SessionAuthLogger::write("[SongImportStore] copyStemFiles failed: " + msg);
    songDir.deleteRecursively();
    return false;
  };

  int stemsToCopy = 0;
  for (const auto &s : song.stems) {
    if (s.include && s.sourcePath.isNotEmpty())
      ++stemsToCopy;
  }
  const int denom = juce::jmax(1, stemsToCopy);
  int stemDone = 0;

  if (onProgress)
    onProgress(0.0, L10n::tr(u8"Preparando carpeta de la canción...",
                              u8"Preparing song folder..."));

  for (auto &stem : song.stems) {
    if (!stem.include || stem.sourcePath.isEmpty())
      continue;
    if (stem.stemId.isEmpty())
      stem.stemId = juce::Uuid().toString();

    if (onProgress) {
      const double frac =
          static_cast<double>(stemDone) / static_cast<double>(denom);
      onProgress(frac,
                 L10n::tr(juce::String("Procesando: ") + stem.displayName,
                          juce::String("Processing: ") + stem.displayName));
    }

    const juce::File src(stem.sourcePath);
    if (!src.existsAsFile())
      return failImport("Stem source file not found: " + stem.sourcePath);
    SessionAuthLogger::write("[SongImportStore] stem begin id=" + stem.stemId +
                             " name=\"" + stem.displayName + "\" src=" +
                             src.getFullPathName());

    const auto legalStemId = juce::File::createLegalFileName(stem.stemId);
    if (legalStemId.isEmpty())
      return failImport("Invalid stem id while importing song.");

    const auto srcExtension = src.getFileExtension();
    const auto dst = songDir.getChildFile(legalStemId + kStoredStemExtension);
    juce::File outputFile = dst;

    if (src.hasFileExtension(juce::String(kStoredStemExtension).substring(1))) {
      if (src != dst) {
        if (dst.existsAsFile())
          dst.deleteFile();
        if (!src.copyFileTo(dst))
          return failImport("Failed copying existing stem '" +
                            stem.displayName + "'.");
      }
    } else {
      juce::String convertError;
      if (!convertStemToStorageFormat(src, dst, convertError)) {
        dst.deleteFile();
        const auto fallbackExtension =
            srcExtension.isNotEmpty() ? srcExtension : juce::String(".wav");
        outputFile = songDir.getChildFile(legalStemId + fallbackExtension);
        if (src != outputFile) {
          if (outputFile.existsAsFile())
            outputFile.deleteFile();
          if (!src.copyFileTo(outputFile)) {
            return failImport("Failed converting stem '" + stem.displayName +
                              "' and fallback copy also failed: " + convertError);
          }
        }
      }
    }

    if (!outputFile.existsAsFile() || outputFile.getSize() <= 0)
      return failImport("Stored stem file is missing or empty: " +
                        outputFile.getFullPathName());

    stem.storedPath = outputFile.getFullPathName();
    stem.fileName = outputFile.getFileName();
    SessionAuthLogger::write(
        "[SongImportStore] stem done id=" + stem.stemId + " dst=" +
        stem.storedPath);
    ++stemDone;
    if (onProgress) {
      const double frac =
          static_cast<double>(stemDone) / static_cast<double>(denom);
      onProgress(frac,
                 L10n::tr(juce::String("Listo: ") + stem.displayName,
                          juce::String("Done: ") + stem.displayName));
    }
  }

  {
    juce::String artworkError;
    if (!persistSongArtwork(song, songDir, nullptr, artworkError))
      return failImport(artworkError);
  }

  SessionAuthLogger::write("[SongImportStore] copyStemFiles complete id=" + song.id +
                           " copied=" + juce::String(stemDone));
  return true;
}

juce::var SongLibraryStore::stemToVar(const ImportedStemData &stem) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("stemId", stem.stemId);
  obj->setProperty("sourcePath", stem.sourcePath);
  obj->setProperty("storedPath", stem.storedPath);
  obj->setProperty("fileName", stem.fileName);
  obj->setProperty("displayName", stem.displayName);
  obj->setProperty("category", stem.category);
  obj->setProperty("include", stem.include);
  obj->setProperty("durationSec", stem.durationSec);
  obj->setProperty("sampleRate", stem.sampleRate);
  obj->setProperty("channels", stem.channels);
  return juce::var(obj);
}

ImportedStemData SongLibraryStore::stemFromVar(const juce::var &v) {
  ImportedStemData stem;
  auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return stem;

  stem.stemId = obj->getProperty("stemId").toString();
  stem.sourcePath = obj->getProperty("sourcePath").toString();
  stem.storedPath = obj->getProperty("storedPath").toString();
  stem.fileName = obj->getProperty("fileName").toString();
  stem.displayName = obj->getProperty("displayName").toString();
  stem.category = obj->getProperty("category").toString();
  stem.include = static_cast<bool>(obj->getProperty("include"));
  stem.durationSec = static_cast<double>(obj->getProperty("durationSec"));
  stem.sampleRate = static_cast<int>(obj->getProperty("sampleRate"));
  stem.channels = static_cast<int>(obj->getProperty("channels"));
  return stem;
}

juce::var SongLibraryStore::songToVar(const ImportedSongData &song) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("id", song.id);
  obj->setProperty("projectId", song.projectId);
  obj->setProperty("title", song.title);
  obj->setProperty("artist", song.artist);
  obj->setProperty("bpm", song.bpm);
  obj->setProperty("key", song.key);
  obj->setProperty("timeSignature", song.timeSignature);
  obj->setProperty("fileType", song.fileType);
  obj->setProperty("artworkPath", song.artworkPath);
  obj->setProperty("artworkSource", song.artworkSource);
  obj->setProperty("projectPath", song.projectPath);
  obj->setProperty("createdMs", static_cast<juce::int64>(song.createdMs));

  juce::Array<juce::var> stemVars;
  stemVars.ensureStorageAllocated(static_cast<int>(song.stems.size()));
  for (const auto &stem : song.stems)
    stemVars.add(stemToVar(stem));
  obj->setProperty("stems", juce::var(stemVars));
  obj->setProperty("arrangement", arrangementToVar(song.arrangement));
  obj->setProperty("tempoMap", tempoMapToVar(song.tempoMap));
  return juce::var(obj);
}

ImportedSongData SongLibraryStore::songFromVar(const juce::var &v,
                                               bool &changed) {
  ImportedSongData song;
  auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return song;

  song.id = obj->getProperty("id").toString();
  song.projectId = obj->getProperty("projectId").toString();
  song.title = obj->getProperty("title").toString();
  song.artist = obj->getProperty("artist").toString();
  song.bpm = static_cast<int>(obj->getProperty("bpm"));
  song.key = obj->getProperty("key").toString();
  song.timeSignature = obj->getProperty("timeSignature").toString();
  song.fileType = obj->getProperty("fileType").toString();
  song.artworkPath = obj->getProperty("artworkPath").toString();
  song.artworkSource = obj->getProperty("artworkSource").toString();
  song.projectPath = obj->getProperty("projectPath").toString();
  song.createdMs = static_cast<juce::int64>(obj->getProperty("createdMs"));

  auto *stemsArray = obj->getProperty("stems").getArray();
  if (stemsArray != nullptr) {
    song.stems.reserve(static_cast<size_t>(stemsArray->size()));
    for (const auto &stemVar : *stemsArray)
      song.stems.push_back(stemFromVar(stemVar));
  }

  if (obj->hasProperty("arrangement")) {
    song.arrangement =
        arrangementFromVar(obj->getProperty("arrangement"), &song, changed);
  }
  if (obj->hasProperty("tempoMap"))
    song.tempoMap = tempoMapFromVar(obj->getProperty("tempoMap"), &song, changed);
  else
    song.tempoMap = tempoMapFromVar({}, &song, changed);

  return song;
}

juce::var SongLibraryStore::songDataToVar(const ImportedSongData &song) {
  return songToVar(song);
}

ImportedSongData SongLibraryStore::songDataFromVar(const juce::var &value,
                                                    bool &changed) {
  return songFromVar(value, changed);
}

bool SongLibraryStore::ensureSongHasStemIds(ImportedSongData &song) {
  bool changed = false;
  std::unordered_set<std::string> seenStemIds;
  seenStemIds.reserve(song.stems.size());
  for (auto &stem : song.stems) {
    const auto normalized = stem.stemId.trim();
    const bool missingId = normalized.isEmpty();
    const bool duplicatedId =
        !missingId &&
        !seenStemIds.insert(normalized.toStdString()).second;
    if (missingId || duplicatedId) {
      stem.stemId = juce::Uuid().toString();
      changed = true;
      seenStemIds.insert(stem.stemId.toStdString());
    } else {
      stem.stemId = normalized;
    }
  }
  return changed;
}

bool SongLibraryStore::setlistExists(const LibraryState &library,
                                    const juce::String &setlistId) {
  if (setlistId.isEmpty())
    return false;
  return std::any_of(library.setlists.begin(), library.setlists.end(),
                     [&setlistId](const SetlistData &setlist) {
                       return setlist.id == setlistId;
                     });
}

void SongLibraryStore::ensureDefaultSetlist(LibraryState &library) {
  if (!library.setlists.empty())
    return;
  SetlistData setlist;
  setlist.id = "setlist_default";
  setlist.name = L10n::tr("Setlist A", "Setlist A");
  const auto now = juce::Time::getCurrentTime().toMilliseconds();
  setlist.createdMs = now;
  setlist.updatedMs = now;
  setlist.items.reserve(library.songs.size());
  for (const auto &song : library.songs)
    setlist.items.push_back(createSetlistItem(song));
  library.setlists.push_back(std::move(setlist));
  library.activeSetlistId = library.setlists.front().id;
}

SetlistData *SongLibraryStore::findSetlistById(LibraryState &library,
                                              const juce::String &setlistId) {
  if (setlistId.isEmpty())
    return nullptr;
  for (auto &setlist : library.setlists) {
    if (setlist.id == setlistId)
      return &setlist;
  }
  return nullptr;
}

SetlistItem SongLibraryStore::createSetlistItem(const ImportedSongData &song) {
  SetlistItem item;
  item.itemId = juce::Uuid().toString();
  item.songId = song.id;
  item.mix = createDefaultMixForSong(song);
  item.window = createDefaultWindowForSong(song);
  item.windowCustomized = false;
  item.arrangement = createDefaultArrangementForSong(song);
  item.arrangementCustomized = false;
  item.dynamicGuide = makeDefaultDynamicGuideSettings();
  item.dynamicGuideCustomized = false;
  item.dynamicGuideMix = makeDefaultDynamicGuideMixSnapshot();
  item.clickDynMix = makeDefaultClickDynMixSnapshot();
  item.updatedMs = juce::Time::getCurrentTime().toMilliseconds();
  return item;
}

SongMixSnapshot SongLibraryStore::createDefaultMixForSong(
    const ImportedSongData &song) {
  SongMixSnapshot mix;
  mix.master = MasterMixSnapshot{};
  mix.stems.reserve(song.stems.size());
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;
    StemMixSnapshot stemMix;
    stemMix.stemId = stem.stemId;
    mix.stems.push_back(std::move(stemMix));
  }
  return mix;
}

SongWindowSnapshot SongLibraryStore::createDefaultWindowForSong(
    const ImportedSongData &song) {
  SongWindowSnapshot window;
  const double songLength = juce::jmax(0.0, estimateSongLengthSeconds(song));
  window.startSec = 0.0;
  window.endSec = songLength;
  window.fadeOutSec = juce::jmin(1.0, songLength);
  return window;
}

SongArrangementSnapshot SongLibraryStore::createDefaultArrangementForSong(
    const ImportedSongData &song) {
  juce::ignoreUnused(song);
  SongArrangementSnapshot arrangement;
  return arrangement;
}

SongMixSnapshot SongLibraryStore::reconcileMixForSong(
    const SongMixSnapshot &mix, const ImportedSongData &song, bool &changed) {
  SongMixSnapshot out = mix;
  std::vector<StemMixSnapshot> nextStems;
  nextStems.reserve(song.stems.size());
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;
    auto it = std::find_if(out.stems.begin(), out.stems.end(),
                           [&stem](const StemMixSnapshot &snapshot) {
                             return snapshot.stemId == stem.stemId;
                           });
    if (it != out.stems.end()) {
      nextStems.push_back(*it);
    } else {
      StemMixSnapshot snapshot;
      snapshot.stemId = stem.stemId;
      nextStems.push_back(snapshot);
      changed = true;
    }
  }
  if (nextStems.size() != out.stems.size())
    changed = true;
  out.stems = std::move(nextStems);
  return out;
}

SongWindowSnapshot SongLibraryStore::reconcileWindowForSong(
    const SongWindowSnapshot &window, const ImportedSongData &song,
    bool &changed) {
  SongWindowSnapshot out = window;
  const double songLength = juce::jmax(0.0, estimateSongLengthSeconds(song));

  const auto originalStart = out.startSec;
  const auto originalEnd = out.endSec;
  const auto originalFade = out.fadeOutSec;

  if (!std::isfinite(out.startSec))
    out.startSec = 0.0;
  if (!std::isfinite(out.endSec))
    out.endSec = songLength;
  if (!std::isfinite(out.fadeOutSec))
    out.fadeOutSec = 0.0;

  out.endSec = juce::jlimit(0.0, songLength, out.endSec);
  out.startSec = juce::jlimit(0.0, out.endSec, out.startSec);
  out.fadeOutSec =
      juce::jlimit(0.0, juce::jmax(0.0, out.endSec - out.startSec), out.fadeOutSec);

  if (!nearlyEqual(originalStart, out.startSec) ||
      !nearlyEqual(originalEnd, out.endSec) ||
      !nearlyEqual(originalFade, out.fadeOutSec)) {
    changed = true;
  }

  return out;
}

SongArrangementSnapshot SongLibraryStore::reconcileArrangementForSong(
    const SongArrangementSnapshot &arrangement, const ImportedSongData &song,
    bool &changed) {
  const double songLength = estimateSongLengthSeconds(song);
  SongArrangementSnapshot out = arrangement;
  double arrangementLength = songLength;
  for (const auto &part : out.parts) {
    if (std::isfinite(part.endSec))
      arrangementLength = juce::jmax(arrangementLength, part.endSec);
  }

  for (auto &part : out.parts) {
    if (part.id.isEmpty()) {
      part.id = juce::Uuid().toString();
      changed = true;
    }
    if (part.name.trim().isEmpty()) {
      part.name = "Part";
      changed = true;
    } else if (part.name != part.name.trim()) {
      part.name = part.name.trim();
      changed = true;
    }
    auto targetColour = standardArrangementPartColorForName(part.name);
    if (targetColour.isEmpty())
      targetColour = fallbackArrangementPartColorForName(part.name);
    if (part.color != targetColour) {
      part.color = targetColour;
      changed = true;
    }

    const auto originalStart = part.startSec;
    const auto originalEnd = part.endSec;
    part.startSec = juce::jlimit(0.0, arrangementLength, originalStart);
    part.endSec = juce::jlimit(0.0, arrangementLength, originalEnd);
    if (part.endSec < part.startSec)
      std::swap(part.startSec, part.endSec);

    if (arrangementLength > 0.0 &&
        (part.endSec - part.startSec) < kMinPartDurationSec) {
      part.endSec =
          juce::jmin(arrangementLength, part.startSec + kMinPartDurationSec);
      if (part.endSec <= part.startSec)
        part.startSec = juce::jmax(0.0, part.endSec - kMinPartDurationSec);
    }

    const auto originalSourceStart = part.sourceStartSec;
    const auto originalSourceEnd = part.sourceEndSec;
    if (songLength > 0.0) {
      bool hasSourceRange = true;
      if (part.sourcePartId.isEmpty()) {
        part.sourceStartSec = juce::jlimit(0.0, songLength, part.startSec);
        part.sourceEndSec = juce::jlimit(0.0, songLength, part.endSec);
      } else if (part.sourceEndSec <= part.sourceStartSec) {
        part.sourceStartSec = 0.0;
        part.sourceEndSec = 0.0;
        hasSourceRange = false;
      } else {
        part.sourceStartSec = juce::jlimit(0.0, songLength, part.sourceStartSec);
        part.sourceEndSec = juce::jlimit(0.0, songLength, part.sourceEndSec);
      }
      if (hasSourceRange && part.sourceEndSec < part.sourceStartSec)
        std::swap(part.sourceStartSec, part.sourceEndSec);
      if (hasSourceRange &&
          (part.sourceEndSec - part.sourceStartSec) < kMinPartDurationSec) {
        part.sourceEndSec =
            juce::jmin(songLength, part.sourceStartSec + kMinPartDurationSec);
        if (part.sourceEndSec <= part.sourceStartSec)
          part.sourceStartSec =
              juce::jmax(0.0, part.sourceEndSec - kMinPartDurationSec);
      }
    }

    if (!nearlyEqual(originalStart, part.startSec) ||
        !nearlyEqual(originalEnd, part.endSec) ||
        !nearlyEqual(originalSourceStart, part.sourceStartSec) ||
        !nearlyEqual(originalSourceEnd, part.sourceEndSec))
      changed = true;
  }

  for (auto &part : out.parts) {
    if (part.sourcePartId.isEmpty())
      continue;
    if (part.sourceEndSec > part.sourceStartSec)
      continue;

    const SongArrangementPartSnapshot *source = nullptr;
    for (const auto &candidate : out.parts) {
      if (candidate.id == part.sourcePartId) {
        source = &candidate;
        break;
      }
    }
    if (source == nullptr)
      continue;

    const double nextSourceStart =
        source->sourcePartId.isNotEmpty() &&
                source->sourceEndSec > source->sourceStartSec
            ? source->sourceStartSec
            : juce::jlimit(0.0, songLength, source->startSec);
    const double nextSourceEnd =
        source->sourcePartId.isNotEmpty() &&
                source->sourceEndSec > source->sourceStartSec
            ? source->sourceEndSec
            : juce::jlimit(0.0, songLength, source->endSec);
    if (!nearlyEqual(part.sourceStartSec, nextSourceStart) ||
        !nearlyEqual(part.sourceEndSec, nextSourceEnd)) {
      part.sourceStartSec = nextSourceStart;
      part.sourceEndSec = nextSourceEnd;
      changed = true;
    }
  }

  if (out.parts.size() > 1) {
    auto sorted = out.parts;
    std::stable_sort(
        sorted.begin(), sorted.end(),
        [](const SongArrangementPartSnapshot &a,
           const SongArrangementPartSnapshot &b) {
          if (nearlyEqual(a.startSec, b.startSec))
            return a.endSec < b.endSec;
          return a.startSec < b.startSec;
        });

    bool orderChanged = sorted.size() != out.parts.size();
    if (!orderChanged) {
      for (size_t i = 0; i < sorted.size(); ++i) {
        const auto &prev = out.parts[i];
        const auto &next = sorted[i];
        if (prev.id != next.id || !nearlyEqual(prev.startSec, next.startSec) ||
            !nearlyEqual(prev.endSec, next.endSec)) {
          orderChanged = true;
          break;
        }
      }
    }
    if (orderChanged)
      changed = true;
    out.parts = std::move(sorted);
  }

  if (migrateLegacyArrangementCues(out))
    changed = true;
  return out;
}

bool SongLibraryStore::normalizeLegacyMix(SongMixSnapshot &mix) {
  const bool masterDefault = juce::approximatelyEqual(mix.master.volume, 1.0f) &&
                             juce::approximatelyEqual(mix.master.gain, 0.0f) &&
                             !mix.master.mute;
  if (!masterDefault)
    return false;

  for (const auto &stem : mix.stems) {
    const bool stemDefault = juce::approximatelyEqual(stem.volume, 1.0f) &&
                             juce::approximatelyEqual(stem.gain, 0.0f) &&
                             !stem.mute && !stem.solo &&
                             stem.outputChannel == 1;
    if (!stemDefault)
      return false;
  }

  mix.master.gain = 1.0f;
  for (auto &stem : mix.stems)
    stem.gain = 1.0f;
  return true;
}

juce::var SongLibraryStore::windowToVar(const SongWindowSnapshot &window) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("startSec", window.startSec);
  obj->setProperty("endSec", window.endSec);
  obj->setProperty("fadeOutSec", window.fadeOutSec);
  return juce::var(obj);
}

SongWindowSnapshot SongLibraryStore::windowFromVar(
    const juce::var &v, const ImportedSongData *song, bool &changed) {
  SongWindowSnapshot window;
  auto *obj = v.getDynamicObject();
  if (obj != nullptr) {
    window.startSec = static_cast<double>(obj->getProperty("startSec"));
    window.endSec = static_cast<double>(obj->getProperty("endSec"));
    if (obj->hasProperty("fadeOutSec"))
      window.fadeOutSec = static_cast<double>(obj->getProperty("fadeOutSec"));
  }
  if (song != nullptr)
    window = reconcileWindowForSong(window, *song, changed);
  return window;
}

juce::var SongLibraryStore::arrangementPartToVar(
    const SongArrangementPartSnapshot &part) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("id", part.id);
  obj->setProperty("name", part.name);
  obj->setProperty("color", part.color);
  obj->setProperty("startSec", part.startSec);
  obj->setProperty("endSec", part.endSec);
  obj->setProperty("sourceStartSec", part.sourceStartSec);
  obj->setProperty("sourceEndSec", part.sourceEndSec);
  obj->setProperty("utilityCue", part.utilityCue);
  obj->setProperty("muteInstruments", part.muteInstruments);
  obj->setProperty("skipPlayback", part.skipPlayback);
  if (part.automation.hasOverrides())
    obj->setProperty("automation", sectionAutomationToVar(part.automation));
  if (part.sourcePartId.isNotEmpty())
    obj->setProperty("sourcePartId", part.sourcePartId);
  return juce::var(obj);
}

SongArrangementPartSnapshot
SongLibraryStore::arrangementPartFromVar(const juce::var &v) {
  SongArrangementPartSnapshot part;
  auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return part;
  part.id = obj->getProperty("id").toString();
  part.name = obj->getProperty("name").toString();
  if (obj->hasProperty("color"))
    part.color = obj->getProperty("color").toString();
  part.startSec = static_cast<double>(obj->getProperty("startSec"));
  part.endSec = static_cast<double>(obj->getProperty("endSec"));
  if (obj->hasProperty("sourceStartSec"))
    part.sourceStartSec = static_cast<double>(obj->getProperty("sourceStartSec"));
  if (obj->hasProperty("sourceEndSec"))
    part.sourceEndSec = static_cast<double>(obj->getProperty("sourceEndSec"));
  if (obj->hasProperty("utilityCue"))
    part.utilityCue = static_cast<bool>(obj->getProperty("utilityCue"));
  if (obj->hasProperty("sourcePartId"))
    part.sourcePartId = obj->getProperty("sourcePartId").toString();
  if (obj->hasProperty("muteInstruments"))
    part.muteInstruments = static_cast<bool>(obj->getProperty("muteInstruments"));
  if (obj->hasProperty("skipPlayback"))
    part.skipPlayback = static_cast<bool>(obj->getProperty("skipPlayback"));
  // Invalid automation is ignored as one block. Legacy sections keep an empty
  // snapshot; a malformed override cannot remove or partially alter a section.
  sectionAutomationFromVar(obj->getProperty("automation"), part.automation);
  return part;
}

juce::var SongLibraryStore::arrangementToVar(
    const SongArrangementSnapshot &arrangement) {
  auto *obj = new juce::DynamicObject();
  juce::Array<juce::var> partsArray;
  partsArray.ensureStorageAllocated(static_cast<int>(arrangement.parts.size()));
  for (const auto &part : arrangement.parts)
    partsArray.add(arrangementPartToVar(part));
  obj->setProperty("parts", juce::var(partsArray));
  juce::Array<juce::var> cuesArray;
  cuesArray.ensureStorageAllocated(static_cast<int>(arrangement.cues.size()));
  for (const auto &cue : arrangement.cues)
    cuesArray.add(arrangementCueToVar(cue));
  // Preserve the canonical representation of songs without secondary marks:
  // upgrading the app alone must not create a new Cloud settings version.
  if (!cuesArray.isEmpty())
    obj->setProperty("cues", juce::var(cuesArray));
  return juce::var(obj);
}

SongArrangementSnapshot SongLibraryStore::arrangementFromVar(
    const juce::var &v, const ImportedSongData *song, bool &changed) {
  SongArrangementSnapshot arrangement;
  auto *obj = v.getDynamicObject();
  if (obj != nullptr) {
    if (auto *partsArray = obj->getProperty("parts").getArray()) {
      arrangement.parts.reserve(static_cast<size_t>(partsArray->size()));
      for (const auto &partVar : *partsArray) {
        auto part = arrangementPartFromVar(partVar);
        if (part.name.isEmpty() && part.id.isEmpty() &&
            nearlyEqual(part.startSec, 0.0) && nearlyEqual(part.endSec, 0.0))
          continue;
        arrangement.parts.push_back(std::move(part));
      }
    }
    if (auto *cuesArray = obj->getProperty("cues").getArray()) {
      arrangement.cues.reserve(static_cast<size_t>(cuesArray->size()));
      for (const auto &cueVar : *cuesArray) {
        SongArrangementCueSnapshot cue;
        if (arrangementCueFromVar(cueVar, cue))
          arrangement.cues.push_back(std::move(cue));
        else
          changed = true;
      }
    }
  }
  if (song != nullptr)
    arrangement = reconcileArrangementForSong(arrangement, *song, changed);
  else if (migrateLegacyArrangementCues(arrangement))
    changed = true;
  return arrangement;
}

juce::var SongLibraryStore::tempoRegionToVar(
    const SongTempoRegionSnapshot &region) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("id", region.id);
  obj->setProperty("startSec", region.startSec);
  obj->setProperty("endSec", region.endSec);
  obj->setProperty("bpm", region.bpm);
  obj->setProperty("timeSignature", region.timeSignature);
  obj->setProperty("clickDivision", static_cast<int>(region.clickDivision));
  obj->setProperty("accentEnabled", region.accentEnabled);
  return juce::var(obj);
}

SongTempoRegionSnapshot SongLibraryStore::tempoRegionFromVar(
    const juce::var &v) {
  SongTempoRegionSnapshot region;
  auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return region;
  region.id = obj->getProperty("id").toString();
  region.startSec = static_cast<double>(obj->getProperty("startSec"));
  region.endSec = static_cast<double>(obj->getProperty("endSec"));
  region.bpm = static_cast<double>(obj->getProperty("bpm"));
  region.timeSignature = obj->getProperty("timeSignature").toString();
  const int division = static_cast<int>(obj->getProperty("clickDivision"));
  region.clickDivision =
      division >= 4 ? ClickDivision::Sixteenth
                    : (division >= 2 ? ClickDivision::Eighth
                                     : ClickDivision::Quarter);
  if (obj->hasProperty("accentEnabled"))
    region.accentEnabled = static_cast<bool>(obj->getProperty("accentEnabled"));
  return region;
}

juce::var SongLibraryStore::tempoMapToVar(
    const SongTempoMapSnapshot &tempoMap) {
  auto *obj = new juce::DynamicObject();
  juce::Array<juce::var> regionsArray;
  regionsArray.ensureStorageAllocated(static_cast<int>(tempoMap.regions.size()));
  for (const auto &region : tempoMap.regions)
    regionsArray.add(tempoRegionToVar(region));
  obj->setProperty("regions", juce::var(regionsArray));
  return juce::var(obj);
}

SongTempoMapSnapshot SongLibraryStore::tempoMapFromVar(
    const juce::var &v, const ImportedSongData *song, bool &changed) {
  SongTempoMapSnapshot tempoMap;
  if (auto *obj = v.getDynamicObject()) {
    if (auto *regionsArray = obj->getProperty("regions").getArray()) {
      tempoMap.regions.reserve(static_cast<size_t>(regionsArray->size()));
      for (const auto &regionVar : *regionsArray) {
        auto region = tempoRegionFromVar(regionVar);
        if (region.id.isEmpty())
          region.id = juce::Uuid().toString();
        if (region.bpm <= 0.0)
          region.bpm = song != nullptr && song->bpm > 0 ? song->bpm : 120.0;
        if (region.timeSignature.trim().isEmpty())
          region.timeSignature =
              song != nullptr && song->timeSignature.trim().isNotEmpty()
                  ? song->timeSignature.trim()
                  : "4/4";
        if (region.endSec > region.startSec)
          tempoMap.regions.push_back(std::move(region));
      }
    }
  }

  const double songLength = song != nullptr ? estimateSongLengthSeconds(*song) : 0.0;
  if (tempoMap.regions.empty() && songLength > 0.0) {
    SongTempoRegionSnapshot region;
    region.id = juce::Uuid().toString();
    region.startSec = 0.0;
    region.endSec = songLength;
    region.bpm = song != nullptr && song->bpm > 0 ? song->bpm : 120.0;
    region.timeSignature =
        song != nullptr && song->timeSignature.trim().isNotEmpty()
            ? song->timeSignature.trim()
            : "4/4";
    region.clickDivision = ClickDivision::Quarter;
    region.accentEnabled = true;
    tempoMap.regions.push_back(std::move(region));
    changed = true;
  }
  return tempoMap;
}

juce::var SongLibraryStore::dynamicGuideToVar(
    const DynamicGuideSettings &settings) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("enabled", settings.enabled);
  obj->setProperty("timing", "one_bar_early");
  obj->setProperty("mode", "replace_original_guide");
  obj->setProperty("languagePolicy", "follow_app_language");
  obj->setProperty("guideChannelPolicy", "ensure_virtual_guide_channel");
  return juce::var(obj);
}

DynamicGuideSettings SongLibraryStore::dynamicGuideFromVar(const juce::var &v,
                                                          bool &changed) {
  auto out = makeDefaultDynamicGuideSettings();
  auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return out;

  out.enabled = static_cast<bool>(obj->getProperty("enabled"));

  const auto timing = obj->getProperty("timing").toString().trim().toLowerCase();
  if (timing.isNotEmpty() && timing != "one_bar_early")
    changed = true;

  const auto mode = obj->getProperty("mode").toString().trim().toLowerCase();
  if (mode.isNotEmpty() && mode != "replace_original_guide")
    changed = true;

  const auto languagePolicy =
      obj->getProperty("languagePolicy").toString().trim().toLowerCase();
  if (languagePolicy.isNotEmpty() && languagePolicy != "follow_app_language")
    changed = true;

  const auto guideChannelPolicy =
      obj->getProperty("guideChannelPolicy").toString().trim().toLowerCase();
  if (guideChannelPolicy.isNotEmpty() &&
      guideChannelPolicy != "ensure_virtual_guide_channel") {
    changed = true;
  }

  return out;
}

juce::var SongLibraryStore::dynamicGuideMixToVar(
    const DynamicGuideMixSnapshot &mix) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("volume", mix.volume);
  obj->setProperty("gain", mix.gain);
  obj->setProperty("mute", mix.mute);
  obj->setProperty("solo", mix.solo);
  obj->setProperty("outputChannel", mix.outputChannel);
  obj->setProperty("outputWidth", mix.outputWidth);
  return juce::var(obj);
}

DynamicGuideMixSnapshot SongLibraryStore::dynamicGuideMixFromVar(
    const juce::var &v, bool &changed) {
  auto out = makeDefaultDynamicGuideMixSnapshot();
  auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return out;

  out.volume = juce::jlimit(0.0f, 1.0f, static_cast<float>(obj->getProperty("volume")));
  out.gain = juce::jlimit(0.0f, 2.0f, static_cast<float>(obj->getProperty("gain")));
  out.mute = static_cast<bool>(obj->getProperty("mute"));
  out.solo = static_cast<bool>(obj->getProperty("solo"));
  out.outputChannel = juce::jmax(1, static_cast<int>(obj->getProperty("outputChannel")));
  out.outputWidth = juce::jlimit(1, 2, static_cast<int>(obj->getProperty("outputWidth")));

  const auto defaults = makeDefaultDynamicGuideMixSnapshot();
  if (!dynamicGuideMixesEqual(out, defaults))
    changed = true;
  return out;
}

juce::var SongLibraryStore::clickDynMixToVar(const ClickDynMixSnapshot &mix) {
  const auto normalized = normalizeClickDynMixSnapshot(mix);
  auto *obj = new juce::DynamicObject();
  obj->setProperty("divisionMask", normalized.divisionMask);
  obj->setProperty("enabled", normalized.enabled);
  obj->setProperty("metronomeEnabled", normalized.metronomeEnabled);
  obj->setProperty("volume", normalized.volume);
  obj->setProperty("gain", normalized.gain);
  obj->setProperty("mute", normalized.mute);
  obj->setProperty("solo", normalized.solo);
  obj->setProperty("outputChannel", normalized.outputChannel);
  obj->setProperty("outputWidth", normalized.outputWidth);
  return juce::var(obj);
}

ClickDynMixSnapshot SongLibraryStore::clickDynMixFromVar(const juce::var &v,
                                                         bool &changed) {
  auto out = makeDefaultClickDynMixSnapshot();
  auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return out;

  if (obj->hasProperty("divisionMask"))
    out.divisionMask = static_cast<int>(obj->getProperty("divisionMask"));
  if (obj->hasProperty("enabled"))
    out.enabled = static_cast<bool>(obj->getProperty("enabled"));
  if (obj->hasProperty("metronomeEnabled"))
    out.metronomeEnabled =
        static_cast<bool>(obj->getProperty("metronomeEnabled"));
  if (obj->hasProperty("volume"))
    out.volume = static_cast<float>(obj->getProperty("volume"));
  if (obj->hasProperty("gain"))
    out.gain = static_cast<float>(obj->getProperty("gain"));
  if (obj->hasProperty("mute"))
    out.mute = static_cast<bool>(obj->getProperty("mute"));
  if (obj->hasProperty("solo"))
    out.solo = static_cast<bool>(obj->getProperty("solo"));
  if (obj->hasProperty("outputChannel"))
    out.outputChannel = static_cast<int>(obj->getProperty("outputChannel"));
  if (obj->hasProperty("outputWidth"))
    out.outputWidth = static_cast<int>(obj->getProperty("outputWidth"));

  const auto normalized = normalizeClickDynMixSnapshot(out);
  if (!clickDynMixesEqual(out, normalized))
    changed = true;
  out = normalized;

  const auto defaults = makeDefaultClickDynMixSnapshot();
  if (!clickDynMixesEqual(out, defaults))
    changed = true;
  return out;
}

juce::var SongLibraryStore::masterMixToVar(const MasterMixSnapshot &mix) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("volume", mix.volume);
  obj->setProperty("gain", mix.gain);
  obj->setProperty("mute", mix.mute);
  return juce::var(obj);
}

MasterMixSnapshot SongLibraryStore::masterMixFromVar(const juce::var &v) {
  MasterMixSnapshot mix;
  auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return mix;
  mix.volume = static_cast<float>(obj->getProperty("volume"));
  if (obj->hasProperty("gain"))
    mix.gain = static_cast<float>(obj->getProperty("gain"));
  mix.mute = static_cast<bool>(obj->getProperty("mute"));
  return mix;
}

juce::var SongLibraryStore::stemMixToVar(const StemMixSnapshot &mix) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("volume", mix.volume);
  obj->setProperty("gain", mix.gain);
  obj->setProperty("mute", mix.mute);
  obj->setProperty("solo", mix.solo);
  obj->setProperty("outputChannel", mix.outputChannel);
  return juce::var(obj);
}

StemMixSnapshot SongLibraryStore::stemMixFromVar(const juce::var &v,
                                                const juce::String &stemId) {
  StemMixSnapshot mix;
  mix.stemId = stemId;
  auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return mix;
  mix.volume = static_cast<float>(obj->getProperty("volume"));
  if (obj->hasProperty("gain"))
    mix.gain = static_cast<float>(obj->getProperty("gain"));
  mix.mute = static_cast<bool>(obj->getProperty("mute"));
  mix.solo = static_cast<bool>(obj->getProperty("solo"));
  if (obj->hasProperty("outputChannel"))
    mix.outputChannel =
        juce::jmax(1, static_cast<int>(obj->getProperty("outputChannel")));
  else
    mix.outputChannel = 1;
  return mix;
}

juce::var SongLibraryStore::mixToVar(const SongMixSnapshot &mix) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("master", masterMixToVar(mix.master));

  auto *stemsObj = new juce::DynamicObject();
  for (const auto &stem : mix.stems) {
    if (stem.stemId.isNotEmpty())
      stemsObj->setProperty(stem.stemId, stemMixToVar(stem));
  }
  obj->setProperty("stems", juce::var(stemsObj));
  return juce::var(obj);
}

SongMixSnapshot SongLibraryStore::mixFromVar(const juce::var &v,
                                            const ImportedSongData *song,
                                            bool &changed) {
  SongMixSnapshot mix;
  auto *obj = v.getDynamicObject();
  if (obj != nullptr) {
    mix.master = masterMixFromVar(obj->getProperty("master"));
    auto stemsVar = obj->getProperty("stems");
    if (auto *stemsObj = stemsVar.getDynamicObject()) {
      const auto &props = stemsObj->getProperties();
      for (const auto &prop : props) {
        auto stemId = prop.name.toString();
        mix.stems.push_back(stemMixFromVar(prop.value, stemId));
      }
    } else if (auto *stemsArray = stemsVar.getArray()) {
      for (const auto &stemVar : *stemsArray) {
        auto *stemObj = stemVar.getDynamicObject();
        if (stemObj == nullptr)
          continue;
        const auto stemId = stemObj->getProperty("stemId").toString();
        if (stemId.isEmpty())
          continue;
        mix.stems.push_back(stemMixFromVar(stemVar, stemId));
      }
    }
  }

  if (song != nullptr)
    mix = reconcileMixForSong(mix, *song, changed);
  if (normalizeLegacyMix(mix))
    changed = true;
  return mix;
}

juce::var SongLibraryStore::setlistItemToVar(
    const SetlistItem &item, const bool persistLegacySongSettings) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("itemId", item.itemId);
  obj->setProperty("songId", item.songId);
  auto *transition = new juce::DynamicObject();
  transition->setProperty("autoPlay", item.transitionToNext.autoPlay);
  obj->setProperty("transitionToNext", transition);
  if (persistLegacySongSettings) {
    obj->setProperty("mix", mixToVar(item.mix));
    obj->setProperty("pitchSemitones", item.pitchSemitones);
    if (item.tempoBpm > 0.0)
      obj->setProperty("tempoBpm", item.tempoBpm);
    if (item.windowCustomized) {
      obj->setProperty("songWindow", windowToVar(item.window));
      obj->setProperty("windowCustomized", true);
    }
    if (item.arrangementCustomized) {
      obj->setProperty("arrangement", arrangementToVar(item.arrangement));
      obj->setProperty("arrangementCustomized", true);
    }
    if (item.dynamicGuideCustomized) {
      obj->setProperty("dynamicGuide", dynamicGuideToVar(item.dynamicGuide));
      obj->setProperty("dynamicGuideCustomized", true);
    }
    obj->setProperty("dynamicGuideMix",
                     dynamicGuideMixToVar(item.dynamicGuideMix));
    obj->setProperty("clickDynMix", clickDynMixToVar(item.clickDynMix));
  }
  obj->setProperty("updatedMs", static_cast<juce::int64>(item.updatedMs));
  return juce::var(obj);
}

SetlistItem SongLibraryStore::setlistItemFromVar(
    const juce::var &v, const ImportedSongData *song, bool &changed) {
  SetlistItem item;
  auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return item;
  item.itemId = obj->getProperty("itemId").toString().trim();
  if (item.itemId.isEmpty()) {
    item.itemId = juce::Uuid().toString();
    changed = true;
  }
  item.songId = obj->getProperty("songId").toString();
  if (const auto *transition =
          obj->getProperty("transitionToNext").getDynamicObject()) {
    item.transitionToNext.autoPlay =
        static_cast<bool>(transition->getProperty("autoPlay"));
  } else if (obj->hasProperty("autoPlayNext")) {
    // v2: la marca ya significaba "al terminar, reproducir siguiente".
    item.transitionToNext.autoPlay =
        static_cast<bool>(obj->getProperty("autoPlayNext"));
    changed = true;
  }
  item.updatedMs = static_cast<juce::int64>(obj->getProperty("updatedMs"));
  if (song != nullptr && isProjectBackedSong(*song)) {
    constexpr const char *legacySongSettingKeys[] = {
        "mix", "pitchSemitones", "tempoBpm", "songWindow",
        "windowCustomized", "arrangement", "arrangementCustomized",
        "dynamicGuide", "dynamicGuideCustomized", "dynamicGuideMix",
        "clickDynMix"};
    for (const auto *key : legacySongSettingKeys) {
      if (obj->hasProperty(key)) {
        changed = true;
        break;
      }
    }
    item.mix = createDefaultMixForSong(*song);
    item.window = createDefaultWindowForSong(*song);
    item.arrangement = createDefaultArrangementForSong(*song);
    item.dynamicGuide = makeDefaultDynamicGuideSettings();
    item.dynamicGuideMix = makeDefaultDynamicGuideMixSnapshot();
    item.clickDynMix = makeDefaultClickDynMixSnapshot();
    return item;
  }
  item.mix = mixFromVar(obj->getProperty("mix"), song, changed);
  if (obj->hasProperty("pitchSemitones"))
    item.pitchSemitones = juce::jlimit(
        -12, 12, static_cast<int>(obj->getProperty("pitchSemitones")));
  if (obj->hasProperty("tempoBpm"))
    item.tempoBpm =
        juce::jlimit(20.0, 300.0,
                     static_cast<double>(obj->getProperty("tempoBpm")));
  if (song != nullptr)
    item.window = createDefaultWindowForSong(*song);
  if (obj->hasProperty("songWindow")) {
    item.window = windowFromVar(obj->getProperty("songWindow"), song, changed);
    item.windowCustomized = true;
  }
  if (obj->hasProperty("windowCustomized"))
    item.windowCustomized =
        static_cast<bool>(obj->getProperty("windowCustomized"));
  if (song != nullptr)
    item.arrangement = createDefaultArrangementForSong(*song);
  if (obj->hasProperty("arrangement")) {
    item.arrangement = arrangementFromVar(obj->getProperty("arrangement"), song,
                                          changed);
    item.arrangementCustomized = true;
  }
  if (obj->hasProperty("arrangementCustomized"))
    item.arrangementCustomized =
        static_cast<bool>(obj->getProperty("arrangementCustomized"));
  item.dynamicGuide = makeDefaultDynamicGuideSettings();
  if (obj->hasProperty("dynamicGuide")) {
    item.dynamicGuide =
        dynamicGuideFromVar(obj->getProperty("dynamicGuide"), changed);
    item.dynamicGuideCustomized = true;
  }
  if (obj->hasProperty("dynamicGuideCustomized"))
    item.dynamicGuideCustomized =
        static_cast<bool>(obj->getProperty("dynamicGuideCustomized"));
  item.dynamicGuideMix = makeDefaultDynamicGuideMixSnapshot();
  if (obj->hasProperty("dynamicGuideMix")) {
    item.dynamicGuideMix =
        dynamicGuideMixFromVar(obj->getProperty("dynamicGuideMix"), changed);
  }
  item.clickDynMix = makeDefaultClickDynMixSnapshot();
  if (obj->hasProperty("clickDynMix")) {
    item.clickDynMix = clickDynMixFromVar(obj->getProperty("clickDynMix"),
                                          changed);
  }
  if (song != nullptr) {
    const auto defaultWindow = createDefaultWindowForSong(*song);
    if (!item.windowCustomized) {
      if (!windowsEqual(item.window, defaultWindow))
        changed = true;
      item.window = defaultWindow;
    } else if (windowsEqual(item.window, defaultWindow)) {
      item.windowCustomized = false;
      changed = true;
    }

    const auto defaultArrangement = createDefaultArrangementForSong(*song);
    if (!item.arrangementCustomized &&
        !arrangementsEqual(item.arrangement, defaultArrangement)) {
      item.arrangementCustomized = true;
      changed = true;
    }
    const auto defaultDynamicGuide = makeDefaultDynamicGuideSettings();
    if (!item.dynamicGuideCustomized &&
        !dynamicGuidesEqual(item.dynamicGuide, defaultDynamicGuide)) {
      item.dynamicGuideCustomized = true;
      changed = true;
    } else if (item.dynamicGuideCustomized &&
               dynamicGuidesEqual(item.dynamicGuide, defaultDynamicGuide)) {
      item.dynamicGuideCustomized = false;
      changed = true;
    }
    auto normalizedGuideMix = item.dynamicGuideMix;
    normalizedGuideMix.volume =
        juce::jlimit(0.0f, 1.0f, normalizedGuideMix.volume);
    normalizedGuideMix.gain = juce::jlimit(0.0f, 2.0f, normalizedGuideMix.gain);
    normalizedGuideMix.outputChannel = juce::jmax(1, normalizedGuideMix.outputChannel);
    normalizedGuideMix.outputWidth = juce::jlimit(1, 2, normalizedGuideMix.outputWidth);
    if (!dynamicGuideMixesEqual(item.dynamicGuideMix, normalizedGuideMix)) {
      item.dynamicGuideMix = normalizedGuideMix;
      changed = true;
    }
    const auto normalizedClickDynMix =
        normalizeClickDynMixSnapshot(item.clickDynMix);
    if (!clickDynMixesEqual(item.clickDynMix, normalizedClickDynMix)) {
      item.clickDynMix = normalizedClickDynMix;
      changed = true;
    }
  }
  return item;
}

juce::var SongLibraryStore::setlistToVar(
    const SetlistData &setlist,
    const std::vector<ImportedSongData> &songs) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("id", setlist.id);
  obj->setProperty("name", setlist.name);
  obj->setProperty("createdMs", static_cast<juce::int64>(setlist.createdMs));
  obj->setProperty("updatedMs", static_cast<juce::int64>(setlist.updatedMs));

  juce::Array<juce::var> itemsArray;
  itemsArray.ensureStorageAllocated(static_cast<int>(setlist.items.size()));
  for (const auto &item : setlist.items) {
    const auto *song = findSongById(songs, item.songId);
    const bool persistLegacySongSettings =
        song == nullptr || !isProjectBackedSong(*song);
    itemsArray.add(
        setlistItemToVar(item, persistLegacySongSettings));
  }
  obj->setProperty("items", juce::var(itemsArray));
  return juce::var(obj);
}

SetlistData SongLibraryStore::setlistFromVar(
    const juce::var &v, const std::vector<ImportedSongData> &songs,
    bool &changed) {
  SetlistData setlist;
  auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return setlist;
  setlist.id = obj->getProperty("id").toString();
  setlist.name = obj->getProperty("name").toString();
  setlist.createdMs =
      static_cast<juce::int64>(obj->getProperty("createdMs"));
  setlist.updatedMs =
      static_cast<juce::int64>(obj->getProperty("updatedMs"));

  auto *itemsArray = obj->getProperty("items").getArray();
  if (itemsArray != nullptr) {
    for (const auto &itemVar : *itemsArray) {
      auto *itemObj = itemVar.getDynamicObject();
      if (itemObj == nullptr)
        continue;
      const auto songId = itemObj->getProperty("songId").toString();
      const auto *song = findSongById(songs, songId);
      if (song == nullptr) {
        changed = true;
        continue;
      }
      setlist.items.push_back(setlistItemFromVar(itemVar, song, changed));
    }
  }
  if (!setlist.items.empty() &&
      setlist.items.back().transitionToNext.autoPlay) {
    setlist.items.back().transitionToNext.autoPlay = false;
    changed = true;
  }

  return setlist;
}

const ImportedSongData *SongLibraryStore::findSongById(
    const std::vector<ImportedSongData> &songs, const juce::String &songId) {
  if (songId.isEmpty())
    return nullptr;
  for (const auto &song : songs) {
    if (song.id == songId)
      return &song;
  }
  return nullptr;
}

bool SongLibraryStore::updateActiveSetlistItem(
    const juce::String &songId,
    std::function<void(SetlistItem &, const ImportedSongData &)> updater) {
  if (songId.isEmpty())
    return false;
  auto library = loadLibrary();
  ensureDefaultSetlist(library);
  auto *setlist = findSetlistById(library, library.activeSetlistId);
  if (setlist == nullptr)
    return false;
  const auto *song = findSongById(library.songs, songId);
  if (song == nullptr)
    return false;
  const bool isProjectSong = isProjectBackedSong(*song);
  juce::File projectFile(isProjectSong ? song->projectPath : juce::String{});
  ProjectSongSettings cachedSettings;
  if (isProjectSong) {
    bool hasCachedProjectSettings =
        projectFile.existsAsFile() &&
        ProjectStore::getCachedSettings(projectFile, cachedSettings);
    const auto projectId = projectIdentityForSong(*song);
    if (!hasCachedProjectSettings)
      hasCachedProjectSettings =
          ProjectStore::getCachedSettings(projectId, cachedSettings);
    if (!hasCachedProjectSettings) {
      cachedSettings.mix = createDefaultMixForSong(*song);
      cachedSettings.tempoBpm =
          song->bpm > 0 ? static_cast<double>(song->bpm) : 120.0;
      cachedSettings.window = createDefaultWindowForSong(*song);
      cachedSettings.arrangement = song->arrangement;
      cachedSettings.arrangementCustomized =
          !song->arrangement.parts.empty() || !song->arrangement.cues.empty();
      cachedSettings.dynamicGuide = makeDefaultDynamicGuideSettings();
      cachedSettings.dynamicGuideMix = makeDefaultDynamicGuideMixSnapshot();
      cachedSettings.clickDynMix = makeDefaultClickDynMixSnapshot();
    }
    auto cachedItem = projectSettingsCacheItem(songId, cachedSettings);
    updater(cachedItem, *song);
    cachedItem.updatedMs = juce::Time::getCurrentTime().toMilliseconds();
    ProjectStore::scheduleSettingsSave(
        projectFile, projectId, projectSettingsFromItem(cachedItem, *song));
    return true;
  }

  auto it = std::find_if(setlist->items.begin(), setlist->items.end(),
                         [&songId](const SetlistItem &item) {
                           return item.songId == songId;
                         });
  if (it == setlist->items.end())
    return false;

  const auto defaultArrangement = createDefaultArrangementForSong(*song);
  const auto defaultWindow = createDefaultWindowForSong(*song);
  const auto defaultDynamicGuide = makeDefaultDynamicGuideSettings();
  const auto now = juce::Time::getCurrentTime().toMilliseconds();

  for (auto &targetSetlist : library.setlists) {
    bool setlistChanged = false;
    for (auto &targetItem : targetSetlist.items) {
      if (targetItem.songId != songId)
        continue;

      bool changed = false;
      targetItem.mix = reconcileMixForSong(targetItem.mix, *song, changed);
      targetItem.window = reconcileWindowForSong(targetItem.window, *song, changed);
      if (!targetItem.windowCustomized) {
        if (!windowsEqual(targetItem.window, defaultWindow)) {
          targetItem.window = defaultWindow;
          changed = true;
        }
      } else if (windowsEqual(targetItem.window, defaultWindow)) {
        targetItem.windowCustomized = false;
        changed = true;
      }
      targetItem.arrangement =
          reconcileArrangementForSong(targetItem.arrangement, *song, changed);
      if (!targetItem.arrangementCustomized &&
          !arrangementsEqual(targetItem.arrangement, defaultArrangement)) {
        targetItem.arrangementCustomized = true;
        changed = true;
      }
      if (!targetItem.dynamicGuideCustomized &&
          !dynamicGuidesEqual(targetItem.dynamicGuide, defaultDynamicGuide)) {
        targetItem.dynamicGuideCustomized = true;
        changed = true;
      } else if (targetItem.dynamicGuideCustomized &&
                 dynamicGuidesEqual(targetItem.dynamicGuide, defaultDynamicGuide)) {
        targetItem.dynamicGuideCustomized = false;
        changed = true;
      }
      {
        auto normalizedGuideMix = targetItem.dynamicGuideMix;
        normalizedGuideMix.volume =
            juce::jlimit(0.0f, 1.0f, normalizedGuideMix.volume);
        normalizedGuideMix.gain =
            juce::jlimit(0.0f, 2.0f, normalizedGuideMix.gain);
        normalizedGuideMix.outputChannel =
            juce::jmax(1, normalizedGuideMix.outputChannel);
        normalizedGuideMix.outputWidth =
            juce::jlimit(1, 2, normalizedGuideMix.outputWidth);
        if (!dynamicGuideMixesEqual(targetItem.dynamicGuideMix,
                                    normalizedGuideMix)) {
          targetItem.dynamicGuideMix = normalizedGuideMix;
          changed = true;
        }
      }
      {
        const auto normalizedClickDynMix =
            normalizeClickDynMixSnapshot(targetItem.clickDynMix);
        if (!clickDynMixesEqual(targetItem.clickDynMix, normalizedClickDynMix)) {
          targetItem.clickDynMix = normalizedClickDynMix;
          changed = true;
        }
      }

      updater(targetItem, *song);
      targetItem.updatedMs = now;
      setlistChanged = true;
      juce::ignoreUnused(changed);
    }
    if (setlistChanged)
      targetSetlist.updatedMs = now;
  }
  const bool saved = saveLibrary(library);
  return saved;
}

bool SongLibraryStore::updateStemMixValue(
    const juce::String &songId, const juce::String &stemId,
    std::function<void(StemMixSnapshot &)> updater) {
  if (stemId.isEmpty())
    return false;
  return updateActiveSetlistItem(
      songId, [&stemId, &updater](SetlistItem &item,
                                 const ImportedSongData &song) {
        juce::ignoreUnused(song);
        auto it = std::find_if(item.mix.stems.begin(), item.mix.stems.end(),
                               [&stemId](const StemMixSnapshot &mix) {
                                 return mix.stemId == stemId;
                               });
        if (it == item.mix.stems.end()) {
          StemMixSnapshot mix;
          mix.stemId = stemId;
          item.mix.stems.push_back(mix);
          it = std::prev(item.mix.stems.end());
        }
        updater(*it);
      });
}

bool SongLibraryStore::updateMasterMixValue(
    const juce::String &songId,
    std::function<void(MasterMixSnapshot &)> updater) {
  return updateActiveSetlistItem(
      songId, [&updater](SetlistItem &item, const ImportedSongData &) {
        updater(item.mix.master);
      });
}

namespace {
// Compute total bytes of a single file (0 if it cannot be stat'd).
juce::int64 fileSizeSafe(const juce::File &file) {
  return file.existsAsFile() ? file.getSize() : 0;
}
// Compute total bytes of a directory (recursive). Missing = 0.
juce::int64 dirSizeSafe(const juce::File &dir) {
  if (!dir.isDirectory())
    return 0;
  juce::int64 total = 0;
  for (juce::DirectoryIterator it(dir, true); it.next();)
    total += fileSizeSafe(it.getFile());
  return total;
}
}  // namespace

SongLibraryStore::OrphanReport
SongLibraryStore::reconcileOrphanedAssets(bool dryRun) {
  OrphanReport report;

  const auto library = loadLibrary();

  // Collect every basename referenced by songs.json across the four
  // directories we reconcile. The reconciliation is name-based because
  // the file layout is content-addressed by hash (cloud-content,
  // cloud-artwork) or by sanitized title (decoded/).
  std::unordered_set<std::string> referencedCloudObjectNames;
  std::unordered_set<std::string> referencedCloudArtworkNames;
  std::unordered_set<std::string> referencedSongIds;
  for (const auto &song : library.songs) {
    if (song.id.isNotEmpty())
      referencedSongIds.insert(song.id.trim().toStdString());
    if (song.projectId.isNotEmpty())
      referencedSongIds.insert(song.projectId.trim().toStdString());
    for (const auto &stem : song.stems) {
      for (const auto &path : {stem.storedPath, stem.sourcePath}) {
        if (path.isEmpty())
          continue;
        const auto file = juce::File(path);
        const auto name = file.getFileName().toStdString();
        if (name.empty())
          continue;
        if (path.contains("cloud-content/objects/"))
          referencedCloudObjectNames.insert(name);
      }
    }
    if (song.artworkPath.isNotEmpty() &&
        song.artworkPath.contains("cloud-content/objects/")) {
      referencedCloudObjectNames.insert(
          juce::File(song.artworkPath).getFileName().toStdString());
    }
  }

  const auto root = getRootDirectory();

  // ---- 1. cloud-content/objects/ -----------------------------------
  // Only orphan-by-name. The objects are content-addressed and the
  // song references the same hash whether it is a stem or an artwork.
  const auto cloudObjects =
      root.getChildFile("cloud-content").getChildFile("objects");
  if (cloudObjects.isDirectory()) {
    juce::Array<juce::File> entries;
    cloudObjects.findChildFiles(entries, juce::File::findFiles, false);
    for (const auto &file : entries) {
      const auto name = file.getFileName().toStdString();
      if (name.empty() || referencedCloudObjectNames.count(name) != 0)
        continue;
      const auto bytes = fileSizeSafe(file);
      report.cloudObjectBytes += bytes;
      report.cloudObjectFiles += 1;
      if (!dryRun)
        file.deleteFile();
    }
  }

  // ---- 2. cloud-artwork/ -------------------------------------------
  // cloud-artwork entries are referenced indirectly via the
  // `artworkSource` URL — songs.json stores the URL, not the local
  // cache path. A cloud-artwork/<sha>.jpg file is "orphan" when no
  // remaining song has a cloud-content artworkPath that the URL would
  // resolve to (i.e. we conservatively treat cloud-artwork as a cache
  // and remove only what songs.json can confirm is unused).
  // For safety we only remove cloud-artwork files when the song that
  // produced them is gone — we cross-check via the URL hash.
  const auto cloudArtwork = root.getChildFile("cloud-artwork");
  if (cloudArtwork.isDirectory()) {
    // Build set of artworkSource URLs that still apply.
    std::unordered_set<std::string> liveArtworkUrls;
    for (const auto &song : library.songs)
      if (song.artworkSource.isNotEmpty())
        liveArtworkUrls.insert(song.artworkSource.trim().toStdString());
    // For each cached artwork, look up the song that would have
    // produced it via artworkSource == its URL. The cache file name
    // is sha256(content) so we cannot reconstruct the URL from the
    // file alone; conservatively only delete files that DO NOT match
    // any song's artworkPath or are clearly old. To stay safe we
    // require the file name to appear in some artworkPath across the
    // library OR keep it. This is the same conservative policy the
    // original deleteSong used: skip cloud-artwork entirely on a
    // single-song delete.
    juce::ignoreUnused(liveArtworkUrls);
    // We do not auto-delete cloud-artwork here because the cache key
    // (sha256) cannot be reverse-mapped to a song URL without an
    // index. Operators that want to reclaim this space can use the
    // explicit "Limpiar caché de artwork" button (future). Reporting
    // only.
    juce::Array<juce::File> entries;
    cloudArtwork.findChildFiles(entries, juce::File::findFiles, false);
    report.cloudArtworkFiles = entries.size();
    for (const auto &f : entries)
      report.cloudArtworkBytes += fileSizeSafe(f);
  }

  // ---- 3. cloud-sync-staging/<id>/ ---------------------------------
  // Staging directories for prepareUploadRequest. The directory name
  // equals projectId. Orphan when projectId is not referenced by any
  // song or by cloud-sync-index.json.
  std::unordered_set<std::string> liveCloudProjectIds;
  for (const auto &song : library.songs) {
    if (song.projectId.isNotEmpty())
      liveCloudProjectIds.insert(song.projectId.trim().toStdString());
  }
  // Also include cloud-sync-index.json entries — a project that is in
  // the cloud index but temporarily removed from the library should
  // not have its staging purged.
  const auto cloudSyncIndex =
      root.getChildFile("cloud-sync-index.json");
  if (cloudSyncIndex.existsAsFile()) {
    const auto parsed = juce::JSON::parse(cloudSyncIndex);
    if (const auto *rootObj = parsed.getDynamicObject()) {
      if (const auto *links = rootObj->getProperty("links").getArray()) {
        for (const auto &link : *links) {
          if (const auto *o = link.getDynamicObject()) {
            const auto id = o->getProperty("projectId").toString().trim();
            if (id.isNotEmpty())
              liveCloudProjectIds.insert(id.toStdString());
          }
        }
      }
    }
  }
  const auto syncStaging = root.getChildFile("cloud-sync-staging");
  if (syncStaging.isDirectory()) {
    juce::Array<juce::File> dirs;
    syncStaging.findChildFiles(dirs, juce::File::findDirectories, false);
    for (const auto &dir : dirs) {
      const auto dirName = dir.getFileName().toStdString();
      if (!dirName.empty() && liveCloudProjectIds.count(dirName) != 0)
        continue;
      const auto bytes = dirSizeSafe(dir);
      report.syncStagingBytes += bytes;
      report.syncStagingDirs += 1;
      if (!dryRun)
        dir.deleteRecursively();
    }
  }

  // ---- 4. decoded/<songId>/ (StemCache) ------------------------------
  // The cache directory is named after the song's immutable id (UUID
  // hex). An entry is orphan when no song has that exact id. This
  // matches the lookup path used by loadSongStems (PluginProcessor.cpp).
  //
  // Before reconciling, run a one-shot migration that renames any
  // legacy decoded/<sanitize(title)>/ directory to decoded/<songId>/
  // so the historical cache survives the layout change. This is a
  // no-op on subsequent runs (idempotent). See
  // perf/analysis_20260912/INFORME.md §4.1.
  {
    std::vector<juce::String> validSongIds;
    std::vector<std::pair<juce::String, juce::String>> titleToId;
    validSongIds.reserve(library.songs.size());
    for (const auto &song : library.songs) {
      const auto id = song.id.trim();
      if (id.isNotEmpty())
        validSongIds.push_back(id);
      if (song.title.isNotEmpty() && song.id.isNotEmpty())
        titleToId.emplace_back(song.title, song.id);
    }
    const int renamed =
        StemCache::migrateTitleKeyedDirectoriesToIds(validSongIds, titleToId);
    juce::ignoreUnused(renamed);
  }
  std::unordered_set<std::string> referencedCacheDirs;
  for (const auto &song : library.songs)
    referencedCacheDirs.insert(song.id.trim().toStdString());
  const auto decodedRoot = root.getChildFile("decoded");
  if (decodedRoot.isDirectory()) {
    juce::Array<juce::File> dirs;
    decodedRoot.findChildFiles(dirs, juce::File::findDirectories, false);
    for (const auto &dir : dirs) {
      const auto dirName = dir.getFileName().toStdString();
      if (!dirName.empty() && referencedCacheDirs.count(dirName) != 0)
        continue;
      const auto bytes = dirSizeSafe(dir);
      report.stemCacheBytes += bytes;
      report.stemCacheDirs += 1;
      if (!dryRun)
        dir.deleteRecursively();
    }
  }

  // ---- Report -------------------------------------------------------
  const auto log = [&](const juce::String &line) {
    SessionAuthLogger::write("[OrphanReconcile" +
                              juce::String(dryRun ? "/dry-run" : "") + "] " +
                              line);
  };
  log("cloud-content/objects/: " +
      juce::String(report.cloudObjectFiles) + " files, " +
      juce::String(report.cloudObjectBytes / 1024 / 1024) + " MB");
  log("cloud-artwork/: " + juce::String(report.cloudArtworkFiles) +
      " files (not auto-deleted; see INFORME.md §2.4), " +
      juce::String(report.cloudArtworkBytes / 1024) + " KB reported");
  log("cloud-sync-staging/: " + juce::String(report.syncStagingDirs) +
      " dirs, " +
      juce::String(report.syncStagingBytes / 1024) + " KB");
  log("decoded/: " + juce::String(report.stemCacheDirs) + " dirs, " +
      juce::String(report.stemCacheBytes / 1024 / 1024) + " MB");
  log("TOTAL: " + juce::String(report.totalBytes() / 1024 / 1024) + " MB " +
      juce::String(dryRun ? "(dry run; pass dryRun=false to apply)"
                          : "(APPLIED)"));
  return report;
}
