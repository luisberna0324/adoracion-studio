#include "SongLibraryStore.h"
#include "AppSettings.h"
#include "AssetBootstrapManager.h"
#include "Localization.h"
#include "SessionAuthLogger.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace {
constexpr double kMinPartDurationSec = 0.25;
constexpr double kArrangementEpsilon = 0.0001;

#if JUCE_WINDOWS
constexpr const char *kStoredStemExtension = ".flac";
constexpr const char *kFfmpegAudioCodec = "flac";
constexpr const char *kStorageFormatLabel = "FLAC";
#else
constexpr const char *kStoredStemExtension = ".m4a";
constexpr const char *kFfmpegAudioCodec = "alac";
constexpr const char *kStorageFormatLabel = "ALAC";
#endif

bool nearlyEqual(double a, double b) {
  return std::abs(a - b) <= kArrangementEpsilon;
}

bool arrangementsEqual(const SongArrangementSnapshot &a,
                       const SongArrangementSnapshot &b) {
  if (a.parts.size() != b.parts.size())
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
    if (!nearlyEqual(lhs.startSec, rhs.startSec) ||
        !nearlyEqual(lhs.endSec, rhs.endSec))
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
  if (!sourceFile.existsAsFile())
    return false;
  clearArtworkFilesInDirectory(directory);
  const auto ext =
      normalizeArtworkExtension(sourceFile.getFileExtension());
  const auto target = directory.getChildFile("artwork" + ext);
  if (target.existsAsFile())
    target.deleteFile();
  if (!sourceFile.copyFileTo(target))
    return false;
  outArtworkPath = target.getFullPathName();
  return true;
}

bool downloadArtworkIntoDirectory(const juce::String &url,
                                  const juce::File &directory,
                                  juce::String &outArtworkPath) {
  if (url.trim().isEmpty())
    return false;

  juce::URL artworkUrl(url.trim());
  auto options = juce::URL::InputStreamOptions(
                     juce::URL::ParameterHandling::inAddress)
                     .withExtraHeaders("User-Agent: Mozilla/5.0\r\n")
                     .withConnectionTimeoutMs(8000)
                     .withHttpRequestCmd("GET");
  auto stream = artworkUrl.createInputStream(options);
  if (stream == nullptr)
    return false;

  juce::MemoryBlock bytes;
  stream->readIntoMemoryBlock(bytes);
  if (bytes.getSize() == 0)
    return false;

  clearArtworkFilesInDirectory(directory);
  const auto ext = inferArtworkExtensionFromSource(url);
  const auto target = directory.getChildFile("artwork" + ext);
  if (target.existsAsFile())
    target.deleteFile();
  if (!target.replaceWithData(bytes.getData(), bytes.getSize()))
    return false;

  outArtworkPath = target.getFullPathName();
  return true;
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

  if (downloadArtworkIntoDirectory(song.artworkSource, targetDirectory,
                                   song.artworkPath)) {
    return true;
  }

  // No bloqueamos el guardado si no se pudo descargar artwork remoto.
  song.artworkPath.clear();
  errorMessage.clear();
  return true;
}

bool repairArtworkPathForSong(ImportedSongData &song) {
  const auto songId = song.id.trim();
  song.artworkPath = song.artworkPath.trim();
  if (songId.isEmpty()) {
    if (song.artworkPath.isNotEmpty() &&
        !juce::File(song.artworkPath).existsAsFile()) {
      song.artworkPath.clear();
      return true;
    }
    return false;
  }

  auto songDir =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
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
  return loadLibrary().songs;
}

LibraryState SongLibraryStore::loadLibrarySnapshot() { return loadLibrary(); }

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
  setlist->items.push_back(createSetlistItem(*song));
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
  }

  if (!saveLibrary(library))
    return false;

  getWaveformCacheDir().getChildFile(trimmedSongId + ".bin").deleteFile();
  getAudioRoot().getChildFile(trimmedSongId).deleteRecursively();
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
  setlist->updatedMs = juce::Time::getCurrentTime().toMilliseconds();
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

  song.title = song.title.trim();
  song.artist = song.artist.trim();
  song.key = song.key.trim();
  song.timeSignature = song.timeSignature.trim();
  if (song.fileType.trim().isEmpty()) {
    song.fileType = previousSong.fileType.trim().isNotEmpty()
                        ? previousSong.fileType.trim()
                        : "Multitrack ALAC";
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

  if (song.arrangement.parts.empty() && !previousSong.arrangement.parts.empty())
    song.arrangement = previousSong.arrangement;
  bool arrangementChanged = false;
  song.arrangement =
      reconcileArrangementForSong(song.arrangement, song, arrangementChanged);
  juce::ignoreUnused(arrangementChanged);

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

  const bool stemOptimizationEnabled = AppSettings::getStemOptimizationEnabled();
  const int stemTotal = static_cast<int>(song.stems.size());
  const int denom = juce::jmax(1, stemTotal);

  auto failWithCleanup = [&](const juce::String &msg) {
    if (stageDir.exists())
      stageDir.deleteRecursively();
    if (backupDir.existsAsFile() || backupDir.exists())
      backupDir.deleteRecursively();
    if (errorMessage != nullptr)
      *errorMessage = msg;
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
    const auto targetExtension =
        stemOptimizationEnabled ? juce::String(kStoredStemExtension)
                                : (srcExtension.isNotEmpty() ? srcExtension
                                                             : juce::String(".wav"));
    const auto stagedFile = stageDir.getChildFile(legalStemId + targetExtension);
    juce::File stagedOutput = stagedFile;
    if (stagedFile.existsAsFile())
      stagedFile.deleteFile();

    const bool sourceFromCurrentSongDir =
        fileIsInsideDirectory(sourceFile, songDir);
    const auto srcPathLower = sourceFile.getFullPathName().toLowerCase();
    const bool isPrimeSource =
        srcPathLower.contains("/library/containers/com.loopcommunity.primemac/");
    const bool alreadyPrimeStemInTargetFormat =
        sourceFile.hasFileExtension(
            juce::String(kStoredStemExtension).substring(1)) &&
        sourceFile.getFileNameWithoutExtension().equalsIgnoreCase(legalStemId);

    if (!stemOptimizationEnabled) {
      if (!sourceFile.copyFileTo(stagedFile))
        return failWithCleanup("Failed copying stem '" + stem.displayName + "'.");
    } else if (sourceFromCurrentSongDir &&
               sourceFile.hasFileExtension(
                   juce::String(kStoredStemExtension).substring(1))) {
      if (!sourceFile.copyFileTo(stagedFile))
        return failWithCleanup("Failed copying existing stem '" +
                               stem.displayName + "'.");
    } else if (isPrimeSource && alreadyPrimeStemInTargetFormat) {
      if (!sourceFile.copyFileTo(stagedFile))
        return failWithCleanup("Failed copying local library stem '" +
                               stem.displayName + "'.");
    } else {
      juce::String convertError;
      if (!convertStemToStorageFormat(sourceFile, stagedFile, convertError)) {
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

    if (!stagedOutput.existsAsFile()) {
      return failWithCleanup("Converted stem file wasn't created: " +
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
  for (auto &setlist : library.setlists) {
    bool setlistChanged = false;
    for (auto &item : setlist.items) {
      if (item.songId != song.id)
        continue;
      bool itemChanged = false;
      item.mix = reconcileMixForSong(item.mix, song, itemChanged);
      item.arrangement = reconcileArrangementForSong(item.arrangement, song,
                                                     itemChanged);
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
    if (errorMessage != nullptr) {
      *errorMessage = L10n::tr(u8"No se pudo guardar la canción editada.",
                               "Couldn't persist edited song.");
    }
    return false;
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
        song, 1400, [&onProgress, &waveformPhase](const double wf01) {
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

  bool changed = false;
  auto normalized = reconcileArrangementForSong(it->arrangement, *it, changed);
  const bool arrangementChanged =
      changed || !arrangementsEqual(it->arrangement, normalized);
  if (arrangementChanged) {
    it->arrangement = normalized;
    saveLibrary(library);
  }

  outArrangement = arrangementChanged ? it->arrangement : normalized;
  outIsCustomArrangement = !outArrangement.parts.empty();
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
  juce::ignoreUnused(song);

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
  auto file = getWaveformCacheDir().getChildFile(songId + ".bin");
  juce::FileOutputStream out(file);
  if (out.failedToOpen())
    return false;
  out.write(bins.data(), bins.size() * sizeof(float));
  return true;
}

std::vector<float> SongLibraryStore::loadWaveformCache(
    const juce::String &songId) {
  if (songId.isEmpty())
    return {};
  auto file = getWaveformCacheDir().getChildFile(songId + ".bin");
  if (!file.existsAsFile())
    return {};
  const auto size = file.getSize();
  if (size < static_cast<juce::int64>(sizeof(float)))
    return {};
  const auto sizeBytes = static_cast<size_t>(size);
  if ((sizeBytes % sizeof(float)) != 0)
    return {};
  if (sizeBytes > static_cast<size_t>(std::numeric_limits<int>::max()))
    return {};
  std::vector<float> bins(sizeBytes / sizeof(float));
  juce::FileInputStream in(file);
  if (in.failedToOpen())
    return {};
  in.read(bins.data(), static_cast<int>(sizeBytes));
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
      if (ensureSongHasStemIds(song))
        needsSave = true;
      if (repairArtworkPathForSong(song))
        needsSave = true;
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
          if (item.songId != songId || item.arrangement.parts.empty())
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
    setlistVars.add(setlistToVar(setlist));

  juce::var rootVar(new juce::DynamicObject());
  if (auto *root = rootVar.getDynamicObject()) {
    root->setProperty("version", 2);
    root->setProperty("activeSetlistId", library.activeSetlistId);
    root->setProperty("songs", juce::var(songVars));
    root->setProperty("setlists", juce::var(setlistVars));
  }

  const auto json = juce::JSON::toString(rootVar, true);
  return getDbFile().replaceWithText(json);
}

juce::File SongLibraryStore::getRootDirectory() {
  auto root =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
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

bool SongLibraryStore::convertStemToStorageFormat(
    const juce::File &source, const juce::File &destination,
    juce::String &errorMessage) {
  auto ffmpegBinary = resolveBundledFfmpegBinary();
  if (!ffmpegBinary.existsAsFile()) {
    juce::String bootstrapError;
    if (AssetBootstrapManager::ensureEncoderReady(&bootstrapError))
      ffmpegBinary = resolveBundledFfmpegBinary();
    else if (bootstrapError.isNotEmpty())
      errorMessage = bootstrapError;
  }

  if (!ffmpegBinary.existsAsFile()) {
    if (errorMessage.isEmpty()) {
      errorMessage = "Couldn't find bundled or downloaded ffmpeg binary for " +
                   juce::String(kStorageFormatLabel) + " conversion.";
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
  args.add("-i");
  args.add(source.getFullPathName());
  args.add("-map_metadata");
  args.add("-1");
  args.add("-vn");
  args.add("-c:a");
  args.add(kFfmpegAudioCodec);
  args.add(destination.getFullPathName());

  juce::ChildProcess process;
  if (!process.start(args)) {
    errorMessage = "Couldn't launch ffmpeg conversion process.";
    return false;
  }

  if (!process.waitForProcessToFinish(10 * 60 * 1000)) {
    process.kill();
    errorMessage = juce::String(kStorageFormatLabel) + " conversion timed out.";
    return false;
  }

  const auto output = process.readAllProcessOutput().trim();
  if (static_cast<int>(process.getExitCode()) != 0 || !destination.existsAsFile()) {
    if (destination.existsAsFile())
      destination.deleteFile();
    errorMessage = output.isNotEmpty() ? output
                                       : "ffmpeg failed converting stem to " +
                                             juce::String(kStorageFormatLabel) + ".";
    return false;
  }

  return true;
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

  const bool stemOptimizationEnabled = AppSettings::getStemOptimizationEnabled();

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
    const auto targetExtension =
        stemOptimizationEnabled ? juce::String(kStoredStemExtension)
                                : (srcExtension.isNotEmpty() ? srcExtension
                                                             : juce::String(".wav"));
    const auto dst = songDir.getChildFile(legalStemId + targetExtension);
    juce::File outputFile = dst;
    const auto srcPathLower = src.getFullPathName().toLowerCase();
    const bool isPrimeSource =
        srcPathLower.contains("/library/containers/com.loopcommunity.primemac/");
    const bool alreadyPrimeStemInTargetFormat =
        src.hasFileExtension(juce::String(kStoredStemExtension).substring(1)) &&
        src.getFileNameWithoutExtension().equalsIgnoreCase(legalStemId);

    if (!stemOptimizationEnabled) {
      if (dst.existsAsFile())
        dst.deleteFile();
      if (!src.copyFileTo(dst))
        return failImport("Failed copying stem '" + stem.displayName + "'.");
    } else if (isPrimeSource && alreadyPrimeStemInTargetFormat) {
      if (dst.existsAsFile())
        dst.deleteFile();
      if (!src.copyFileTo(dst))
        return failImport("Failed copying local library stem '" +
                          stem.displayName + "'.");
    } else {
      juce::String convertError;
      if (!convertStemToStorageFormat(src, dst, convertError)) {
        const auto fallbackExtension =
            srcExtension.isNotEmpty() ? srcExtension : juce::String(".wav");
        outputFile = songDir.getChildFile(legalStemId + fallbackExtension);
        if (outputFile.existsAsFile())
          outputFile.deleteFile();
        if (!src.copyFileTo(outputFile)) {
          return failImport("Failed converting stem '" + stem.displayName +
                            "' and fallback copy also failed: " + convertError);
        }
      }
    }

    if (!outputFile.existsAsFile())
      return failImport("Converted stem file wasn't created: " +
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
  obj->setProperty("title", song.title);
  obj->setProperty("artist", song.artist);
  obj->setProperty("bpm", song.bpm);
  obj->setProperty("key", song.key);
  obj->setProperty("timeSignature", song.timeSignature);
  obj->setProperty("fileType", song.fileType);
  obj->setProperty("artworkPath", song.artworkPath);
  obj->setProperty("artworkSource", song.artworkSource);
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
  song.title = obj->getProperty("title").toString();
  song.artist = obj->getProperty("artist").toString();
  song.bpm = static_cast<int>(obj->getProperty("bpm"));
  song.key = obj->getProperty("key").toString();
  song.timeSignature = obj->getProperty("timeSignature").toString();
  song.fileType = obj->getProperty("fileType").toString();
  song.artworkPath = obj->getProperty("artworkPath").toString();
  song.artworkSource = obj->getProperty("artworkSource").toString();
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
  if (arrangement.parts.empty())
    return arrangement;

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
  if (obj->hasProperty("sourcePartId"))
    part.sourcePartId = obj->getProperty("sourcePartId").toString();
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
  }
  if (song != nullptr)
    arrangement = reconcileArrangementForSong(arrangement, *song, changed);
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

juce::var SongLibraryStore::setlistItemToVar(const SetlistItem &item) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("songId", item.songId);
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
  obj->setProperty("dynamicGuideMix", dynamicGuideMixToVar(item.dynamicGuideMix));
  obj->setProperty("clickDynMix", clickDynMixToVar(item.clickDynMix));
  obj->setProperty("updatedMs", static_cast<juce::int64>(item.updatedMs));
  return juce::var(obj);
}

SetlistItem SongLibraryStore::setlistItemFromVar(
    const juce::var &v, const ImportedSongData *song, bool &changed) {
  SetlistItem item;
  auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return item;
  item.songId = obj->getProperty("songId").toString();
  item.updatedMs = static_cast<juce::int64>(obj->getProperty("updatedMs"));
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

juce::var SongLibraryStore::setlistToVar(const SetlistData &setlist) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("id", setlist.id);
  obj->setProperty("name", setlist.name);
  obj->setProperty("createdMs", static_cast<juce::int64>(setlist.createdMs));
  obj->setProperty("updatedMs", static_cast<juce::int64>(setlist.updatedMs));

  juce::Array<juce::var> itemsArray;
  itemsArray.ensureStorageAllocated(static_cast<int>(setlist.items.size()));
  for (const auto &item : setlist.items)
    itemsArray.add(setlistItemToVar(item));
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

  auto it = std::find_if(setlist->items.begin(), setlist->items.end(),
                         [&songId](const SetlistItem &item) {
                           return item.songId == songId;
                         });
  if (it == setlist->items.end())
    return false;

  bool changed = false;
  it->mix = reconcileMixForSong(it->mix, *song, changed);
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
  it->arrangement =
      reconcileArrangementForSong(it->arrangement, *song, changed);
  const auto defaultArrangement = createDefaultArrangementForSong(*song);
  if (!it->arrangementCustomized &&
      !arrangementsEqual(it->arrangement, defaultArrangement)) {
    it->arrangementCustomized = true;
    changed = true;
  }
  const auto defaultDynamicGuide = makeDefaultDynamicGuideSettings();
  if (!it->dynamicGuideCustomized &&
      !dynamicGuidesEqual(it->dynamicGuide, defaultDynamicGuide)) {
    it->dynamicGuideCustomized = true;
    changed = true;
  } else if (it->dynamicGuideCustomized &&
             dynamicGuidesEqual(it->dynamicGuide, defaultDynamicGuide)) {
    it->dynamicGuideCustomized = false;
    changed = true;
  }
  {
    auto normalizedGuideMix = it->dynamicGuideMix;
    normalizedGuideMix.volume = juce::jlimit(0.0f, 1.0f, normalizedGuideMix.volume);
    normalizedGuideMix.gain = juce::jlimit(0.0f, 2.0f, normalizedGuideMix.gain);
    normalizedGuideMix.outputChannel = juce::jmax(1, normalizedGuideMix.outputChannel);
    normalizedGuideMix.outputWidth = juce::jlimit(1, 2, normalizedGuideMix.outputWidth);
    if (!dynamicGuideMixesEqual(it->dynamicGuideMix, normalizedGuideMix)) {
      it->dynamicGuideMix = normalizedGuideMix;
      changed = true;
    }
  }
  {
    const auto normalizedClickDynMix =
        normalizeClickDynMixSnapshot(it->clickDynMix);
    if (!clickDynMixesEqual(it->clickDynMix, normalizedClickDynMix)) {
      it->clickDynMix = normalizedClickDynMix;
      changed = true;
    }
  }
  updater(*it, *song);
  const auto now = juce::Time::getCurrentTime().toMilliseconds();
  it->updatedMs = now;
  setlist->updatedMs = now;
  return saveLibrary(library);
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
