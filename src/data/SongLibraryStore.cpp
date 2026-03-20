#include "SongLibraryStore.h"

#include <utility>

std::vector<ImportedSongData> SongLibraryStore::loadSongs() {
  std::vector<ImportedSongData> songs;

  const auto dbFile = getDbFile();
  if (!dbFile.existsAsFile())
    return songs;

  const auto parsed = juce::JSON::parse(dbFile.loadFileAsString());
  auto *rootObj = parsed.getDynamicObject();
  if (rootObj == nullptr)
    return songs;

  auto *songsArray = rootObj->getProperty("songs").getArray();
  if (songsArray == nullptr)
    return songs;

  songs.reserve(static_cast<size_t>(songsArray->size()));
  for (const auto &songVar : *songsArray)
    songs.push_back(songFromVar(songVar));

  return songs;
}

bool SongLibraryStore::appendSong(ImportedSongData song) {
  copyStemFiles(song);

  auto songs = loadSongs();
  songs.push_back(std::move(song));
  return saveSongs(songs);
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
  if (size < static_cast<juce::int64>(sizeof(float)) ||
      (size % sizeof(float)) != 0)
    return {};
  std::vector<float> bins(static_cast<size_t>(size / sizeof(float)));
  juce::FileInputStream in(file);
  if (in.failedToOpen())
    return {};
  in.read(bins.data(), static_cast<int>(size));
  return bins;
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

bool SongLibraryStore::saveSongs(const std::vector<ImportedSongData> &songs) {
  juce::Array<juce::var> songVars;
  songVars.ensureStorageAllocated(static_cast<int>(songs.size()));
  for (const auto &song : songs)
    songVars.add(songToVar(song));

  juce::var rootVar(new juce::DynamicObject());
  if (auto *root = rootVar.getDynamicObject()) {
    root->setProperty("version", 1);
    root->setProperty("songs", juce::var(songVars));
  }

  const auto json = juce::JSON::toString(rootVar, true);
  return getDbFile().replaceWithText(json);
}

void SongLibraryStore::copyStemFiles(ImportedSongData &song) {
  if (song.id.isEmpty())
    song.id = juce::Uuid().toString();

  auto songDir = getAudioRoot().getChildFile(song.id);
  songDir.createDirectory();

  for (size_t i = 0; i < song.stems.size(); ++i) {
    auto &stem = song.stems[i];
    if (!stem.include || stem.sourcePath.isEmpty())
      continue;

    const juce::File src(stem.sourcePath);
    if (!src.existsAsFile())
      continue;

    const auto baseName =
        juce::File::createLegalFileName(src.getFileNameWithoutExtension());
    auto dst = songDir.getNonexistentChildFile(
        baseName.isEmpty()
            ? juce::String("stem_") + juce::String(static_cast<int>(i) + 1)
            : baseName,
        src.getFileExtension(), false);

    if (src.copyFileTo(dst))
      stem.storedPath = dst.getFullPathName();
  }
}

juce::var SongLibraryStore::stemToVar(const ImportedStemData &stem) {
  auto *obj = new juce::DynamicObject();
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
