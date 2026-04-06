#include "SongLibraryStore.h"
#include "Localization.h"

#include <algorithm>
#include <cmath>
#include <limits>
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
    if (lhs.name != rhs.name)
      return false;
    if (!nearlyEqual(lhs.startSec, rhs.startSec) ||
        !nearlyEqual(lhs.endSec, rhs.endSec))
      return false;
  }
  return true;
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

bool SongLibraryStore::appendSong(ImportedSongData song, juce::String *errorMessage) {
  juce::String localError;
  if (!copyStemFiles(song, localError)) {
    if (errorMessage != nullptr)
      *errorMessage = localError;
    return false;
  }

  auto library = loadLibrary();
  ensureSongHasStemIds(song);
  library.songs.push_back(song);
  ensureDefaultSetlist(library);
  addSongToActiveSetlist(library, song);
  const bool saved = saveLibrary(library);
  if (!saved && errorMessage != nullptr && localError.isEmpty())
    *errorMessage = "Couldn't persist song metadata.";
  return saved;
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

  bool changed = false;
  if (it == setlist->items.end()) {
    setlist->items.push_back(createSetlistItem(*song));
    it = std::prev(setlist->items.end());
    changed = true;
  } else {
    it->mix = reconcileMixForSong(it->mix, *song, changed);
  }

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

bool SongLibraryStore::getActiveSetlistArrangement(
    const juce::String &songId, SongArrangementSnapshot &outArrangement,
    bool &outIsCustomArrangement) {
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
  bool changed = false;
  if (it == setlist->items.end()) {
    setlist->items.push_back(createSetlistItem(*song));
    it = std::prev(setlist->items.end());
    changed = true;
  } else {
    it->arrangement =
        reconcileArrangementForSong(it->arrangement, *song, changed);
    const auto defaultArrangement = createDefaultArrangementForSong(*song);
    if (!it->arrangementCustomized &&
        !arrangementsEqual(it->arrangement, defaultArrangement)) {
      it->arrangementCustomized = true;
      changed = true;
    }
  }

  outArrangement = it->arrangement;
  outIsCustomArrangement = it->arrangementCustomized;
  if (changed) {
    const auto now = juce::Time::getCurrentTime().toMilliseconds();
    it->updatedMs = now;
    setlist->updatedMs = now;
    saveLibrary(library);
  }
  return true;
}

bool SongLibraryStore::updateArrangementForActiveSetlist(
    const juce::String &songId, const SongArrangementSnapshot &arrangement,
    const bool isCustomArrangement) {
  return updateActiveSetlistItem(
      songId,
      [&arrangement, isCustomArrangement](SetlistItem &item,
                                          const ImportedSongData &song) {
        bool changed = false;
        item.arrangement =
            reconcileArrangementForSong(arrangement, song, changed);
        juce::ignoreUnused(changed);
        item.arrangementCustomized = isCustomArrangement;
      });
}

bool SongLibraryStore::resetArrangementForActiveSetlist(
    const juce::String &songId) {
  return updateActiveSetlistItem(
      songId, [](SetlistItem &item, const ImportedSongData &song) {
        item.arrangement = createDefaultArrangementForSong(song);
        item.arrangementCustomized = false;
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
      auto song = songFromVar(songVar);
      if (ensureSongHasStemIds(song))
        needsSave = true;
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
  const auto executable =
      juce::File::getSpecialLocation(juce::File::currentExecutableFile);
  if (!executable.existsAsFile())
    return {};

#if JUCE_MAC
  // Standalone app bundle: <App>.app/Contents/MacOS/<binary>
  const auto macOSDir = executable.getParentDirectory();
  const auto resourcesDir = macOSDir.getSiblingFile("Resources");
  const auto bundled = resourcesDir.getChildFile("ffmpeg").getChildFile("ffmpeg");
  if (bundled.existsAsFile())
    return bundled;
#elif JUCE_WINDOWS
  // Standalone app folder: <app>/ffmpeg/ffmpeg.exe
  const auto bundled = executable.getParentDirectory()
                           .getChildFile("ffmpeg")
                           .getChildFile("ffmpeg.exe");
  if (bundled.existsAsFile())
    return bundled;
#endif

  // Development fallback.
#if JUCE_MAC
  const juce::File homebrew("/opt/homebrew/bin/ffmpeg");
  if (homebrew.existsAsFile())
    return homebrew;
#endif
  return {};
}

bool SongLibraryStore::convertStemToStorageFormat(
    const juce::File &source, const juce::File &destination,
    juce::String &errorMessage) {
  const auto ffmpegBinary = resolveBundledFfmpegBinary();
  if (!ffmpegBinary.existsAsFile()) {
    errorMessage = "Couldn't find bundled ffmpeg binary for " +
                   juce::String(kStorageFormatLabel) + " conversion.";
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

bool SongLibraryStore::copyStemFiles(ImportedSongData &song, juce::String &errorMessage) {
  errorMessage.clear();
  if (song.id.isEmpty())
    song.id = juce::Uuid().toString();

  auto songDir = getAudioRoot().getChildFile(song.id);
  songDir.createDirectory();
  auto failImport = [&songDir, &errorMessage](const juce::String &msg) {
    errorMessage = msg;
    songDir.deleteRecursively();
    return false;
  };

  for (auto &stem : song.stems) {
    if (!stem.include || stem.sourcePath.isEmpty())
      continue;
    if (stem.stemId.isEmpty())
      stem.stemId = juce::Uuid().toString();

    const juce::File src(stem.sourcePath);
    if (!src.existsAsFile())
      return failImport("Stem source file not found: " + stem.sourcePath);

    const auto legalStemId = juce::File::createLegalFileName(stem.stemId);
    if (legalStemId.isEmpty())
      return failImport("Invalid stem id while importing song.");

    const auto dst = songDir.getChildFile(
        legalStemId + juce::String(kStoredStemExtension));
    const auto srcPathLower = src.getFullPathName().toLowerCase();
    const bool isPrimeSource =
        srcPathLower.contains("/library/containers/com.loopcommunity.primemac/");
    const bool alreadyPrimeStemInTargetFormat =
        src.hasFileExtension(juce::String(kStoredStemExtension).substring(1)) &&
        src.getFileNameWithoutExtension().equalsIgnoreCase(legalStemId);

    if (isPrimeSource && alreadyPrimeStemInTargetFormat) {
      if (dst.existsAsFile())
        dst.deleteFile();
      if (!src.copyFileTo(dst))
        return failImport("Failed copying local library stem '" +
                          stem.displayName + "'.");
    } else {
      if (!convertStemToStorageFormat(src, dst, errorMessage))
        return failImport("Failed converting stem '" + stem.displayName + "': " +
                          errorMessage);
    }

    if (!dst.existsAsFile())
      return failImport("Converted stem file wasn't created: " +
                        dst.getFullPathName());

    stem.storedPath = dst.getFullPathName();
    stem.fileName = dst.getFileName();
  }

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
  obj->setProperty("createdMs", static_cast<juce::int64>(song.createdMs));

  juce::Array<juce::var> stemVars;
  stemVars.ensureStorageAllocated(static_cast<int>(song.stems.size()));
  for (const auto &stem : song.stems)
    stemVars.add(stemToVar(stem));
  obj->setProperty("stems", juce::var(stemVars));
  return juce::var(obj);
}

ImportedSongData SongLibraryStore::songFromVar(const juce::var &v) {
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
  song.createdMs = static_cast<juce::int64>(obj->getProperty("createdMs"));

  auto *stemsArray = obj->getProperty("stems").getArray();
  if (stemsArray != nullptr) {
    song.stems.reserve(static_cast<size_t>(stemsArray->size()));
    for (const auto &stemVar : *stemsArray)
      song.stems.push_back(stemFromVar(stemVar));
  }

  return song;
}

bool SongLibraryStore::ensureSongHasStemIds(ImportedSongData &song) {
  bool changed = false;
  for (auto &stem : song.stems) {
    if (stem.stemId.isEmpty()) {
      stem.stemId = juce::Uuid().toString();
      changed = true;
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

void SongLibraryStore::addSongToActiveSetlist(LibraryState &library,
                                             const ImportedSongData &song) {
  if (library.setlists.empty())
    ensureDefaultSetlist(library);
  auto *setlist = findSetlistById(library, library.activeSetlistId);
  if (setlist == nullptr && !library.setlists.empty()) {
    library.activeSetlistId = library.setlists.front().id;
    setlist = &library.setlists.front();
  }
  if (setlist == nullptr)
    return;
  setlist->items.push_back(createSetlistItem(song));
  setlist->updatedMs = juce::Time::getCurrentTime().toMilliseconds();
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
  item.arrangement = createDefaultArrangementForSong(song);
  item.arrangementCustomized = false;
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

SongArrangementSnapshot SongLibraryStore::reconcileArrangementForSong(
    const SongArrangementSnapshot &arrangement, const ImportedSongData &song,
    bool &changed) {
  const double songLength = estimateSongLengthSeconds(song);
  if (arrangement.parts.empty())
    return arrangement;

  SongArrangementSnapshot out = arrangement;
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
    if (!isArrangementPartColorValid(part.color)) {
      part.color = makeRandomArrangementPartColor();
      changed = true;
    }

    const auto originalStart = part.startSec;
    const auto originalEnd = part.endSec;
    part.startSec = juce::jlimit(0.0, songLength, originalStart);
    part.endSec = juce::jlimit(0.0, songLength, originalEnd);
    if (part.endSec < part.startSec)
      std::swap(part.startSec, part.endSec);

    if (songLength > 0.0 && (part.endSec - part.startSec) < kMinPartDurationSec) {
      part.endSec = juce::jmin(songLength, part.startSec + kMinPartDurationSec);
      if (part.endSec <= part.startSec)
        part.startSec = juce::jmax(0.0, part.endSec - kMinPartDurationSec);
    }

    if (!nearlyEqual(originalStart, part.startSec) ||
        !nearlyEqual(originalEnd, part.endSec))
      changed = true;
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

juce::var SongLibraryStore::arrangementPartToVar(
    const SongArrangementPartSnapshot &part) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("id", part.id);
  obj->setProperty("name", part.name);
  obj->setProperty("color", part.color);
  obj->setProperty("startSec", part.startSec);
  obj->setProperty("endSec", part.endSec);
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
  if (item.arrangementCustomized) {
    obj->setProperty("arrangement", arrangementToVar(item.arrangement));
    obj->setProperty("arrangementCustomized", true);
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
  item.songId = obj->getProperty("songId").toString();
  item.updatedMs = static_cast<juce::int64>(obj->getProperty("updatedMs"));
  item.mix = mixFromVar(obj->getProperty("mix"), song, changed);
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
  if (song != nullptr) {
    const auto defaultArrangement = createDefaultArrangementForSong(*song);
    if (!item.arrangementCustomized &&
        !arrangementsEqual(item.arrangement, defaultArrangement)) {
      item.arrangementCustomized = true;
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
  if (it == setlist->items.end()) {
    setlist->items.push_back(createSetlistItem(*song));
    it = std::prev(setlist->items.end());
  }

  bool changed = false;
  it->mix = reconcileMixForSong(it->mix, *song, changed);
  it->arrangement =
      reconcileArrangementForSong(it->arrangement, *song, changed);
  const auto defaultArrangement = createDefaultArrangementForSong(*song);
  if (!it->arrangementCustomized &&
      !arrangementsEqual(it->arrangement, defaultArrangement)) {
    it->arrangementCustomized = true;
    changed = true;
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
