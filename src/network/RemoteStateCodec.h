#pragma once
#include "../data/SongData.h"
#include "../data/SongLibraryStore.h"
#include <map>

namespace playworship::remote {
inline bool validateArrangementAutomation(const juce::var &arrangement) {
  const auto *parts = arrangement.getProperty("parts", {}).getArray();
  if (parts == nullptr) return false;
  if (parts->size() > 1024) return false;
  for (const auto &part : *parts) {
    if (!part.isObject()) return false;
    SectionAutomationSnapshot automation;
    if (!sectionAutomationFromVar(part.getProperty("automation", {}), automation))
      return false;
  }
  return true;
}
inline juce::var encodeTempoMap(const SongTempoMapSnapshot &map) {
  juce::Array<juce::var> values;
  for (const auto &r : map.regions) {
    auto *o = new juce::DynamicObject();
    o->setProperty("id",r.id); o->setProperty("start",r.startSec); o->setProperty("end",r.endSec);
    o->setProperty("bpm",r.bpm); o->setProperty("timeSignature",r.timeSignature);
    o->setProperty("division",static_cast<int>(r.clickDivision)); o->setProperty("accent",r.accentEnabled);
    values.add(juce::var(o));
  }
  return values;
}
inline SongTempoMapSnapshot decodeTempoMap(const juce::var &values) {
  SongTempoMapSnapshot map;
  if (const auto *array = values.getArray()) for (const auto &v : *array) {
    if (map.regions.size() >= 1024) break;
    SongTempoRegionSnapshot r; r.id=v.getProperty("id",{}).toString();
    r.startSec=static_cast<double>(v.getProperty("start",0.0)); r.endSec=static_cast<double>(v.getProperty("end",0.0));
    r.bpm=static_cast<double>(v.getProperty("bpm",120.0)); r.timeSignature=v.getProperty("timeSignature","4/4").toString();
    r.clickDivision=static_cast<ClickDivision>(static_cast<int>(v.getProperty("division",1))); r.accentEnabled=static_cast<bool>(v.getProperty("accent",true));
    map.regions.push_back(r);
  }
  return map;
}
inline juce::String encodedArtwork(const juce::File &file) {
  struct Entry { juce::int64 modified = 0, size = 0; juce::String data; };
  // This cache is used on the message thread, outside playback/network callbacks.
  static std::map<juce::String, Entry> cache;
  const auto modified = file.getLastModificationTime().toMilliseconds();
  const auto size = file.getSize();
  const auto key = file.getFullPathName();
  const auto found = cache.find(key);
  if (found != cache.end() && found->second.modified == modified && found->second.size == size)
    return found->second.data;
  Entry entry{modified, size, {}};
  auto artwork = juce::ImageFileFormat::loadFrom(file);
  if (artwork.isValid()) {
    const auto scale = juce::jmin(1.0, 160.0 / juce::jmax(artwork.getWidth(), artwork.getHeight()));
    artwork = artwork.rescaled(juce::jmax(1, static_cast<int>(artwork.getWidth()*scale)), juce::jmax(1, static_cast<int>(artwork.getHeight()*scale)));
    juce::MemoryOutputStream stream;
    juce::PNGImageFormat().writeImageToStream(artwork, stream);
    entry.data = stream.getMemoryBlock().toBase64Encoding();
  }
  if (cache.size() >= 256) cache.clear();
  cache[key] = entry;
  return entry.data;
}
inline juce::var encodeSong(const ImportedSongData &song, bool includeArtwork = true) {
  auto *o = new juce::DynamicObject();
  o->setProperty("id", song.id); o->setProperty("projectId", song.projectId);
  o->setProperty("title", song.title); o->setProperty("artist", song.artist);
  o->setProperty("bpm", song.bpm); o->setProperty("key", song.key);
  o->setProperty("timeSignature", song.timeSignature);
  o->setProperty("createdMs", static_cast<juce::int64>(song.createdMs));
  juce::Array<juce::var> stems;
  for (const auto &stem : song.stems) {
    auto *s = new juce::DynamicObject();
    s->setProperty("id", stem.stemId); s->setProperty("name", stem.displayName);
    s->setProperty("fileName", stem.fileName.replaceCharacter('\\', '/')
        .fromLastOccurrenceOf("/", false, false));
    s->setProperty("category", stem.category); s->setProperty("include", stem.include);
    s->setProperty("duration", stem.durationSec); s->setProperty("channels", stem.channels);
    stems.add(juce::var(s));
  }
  o->setProperty("stems", stems);
  o->setProperty("tempoMap", encodeTempoMap(song.tempoMap));
  o->setProperty("arrangement", SongLibraryStore::songDataToVar(song)
      .getDynamicObject()->getProperty("arrangement"));
  // Include a cheap revision in metadata comparisons; never send local paths.
  if (song.artworkPath.isNotEmpty()) {
    const juce::File file(song.artworkPath);
    o->setProperty("artworkRevision", juce::String(file.getLastModificationTime().toMilliseconds()) + ":" + juce::String(file.getSize()));
    if (includeArtwork) o->setProperty("artwork", encodedArtwork(file));
  }
  return juce::var(o);
}
inline ImportedSongData decodeSong(const juce::var &v) {
  ImportedSongData song;
  song.id = v.getProperty("id", {}).toString(); song.projectId = v.getProperty("projectId", {}).toString();
  song.title = v.getProperty("title", {}).toString(); song.artist = v.getProperty("artist", {}).toString();
  song.bpm = static_cast<int>(v.getProperty("bpm", 120)); song.key = v.getProperty("key", {}).toString();
  song.timeSignature = v.getProperty("timeSignature", {}).toString();
  song.createdMs = static_cast<juce::int64>(v.getProperty("createdMs", juce::var(0)));
  const auto stems = v.getProperty("stems", {});
  if (const auto *array = stems.getArray()) for (const auto &s : *array) {
    if (song.stems.size() >= 64) break;
    ImportedStemData stem;
    stem.stemId = s.getProperty("id", {}).toString(); stem.displayName = s.getProperty("name", {}).toString();
    stem.fileName = s.getProperty("fileName", {}).toString().substring(0, 512)
        .replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false);
    stem.category = s.getProperty("category", {}).toString(); stem.include = static_cast<bool>(s.getProperty("include", true));
    stem.durationSec = static_cast<double>(s.getProperty("duration", 0.0)); stem.channels = static_cast<int>(s.getProperty("channels", 2));
    song.stems.push_back(std::move(stem));
  }
  song.tempoMap = decodeTempoMap(v.getProperty("tempoMap", {}));
  if (v.getProperty("arrangement", {}).isObject()) {
    auto *carrier = new juce::DynamicObject();
    carrier->setProperty("arrangement", v.getProperty("arrangement", {}));
    bool changed = false;
    song.arrangement = SongLibraryStore::songDataFromVar(juce::var(carrier), changed).arrangement;
  }
  const auto encoded = v.getProperty("artwork", {}).toString();
  if (encoded.isNotEmpty() && encoded.length() < 200000) {
    juce::MemoryBlock data;
    if (data.fromBase64Encoding(encoded)) {
      const auto hash = juce::SHA256(data).toHexString();
      const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("PlayWorship-RemoteArtwork");
      const auto file = folder.getChildFile(hash + ".png");
      if (file.existsAsFile() || (folder.createDirectory().wasOk() && file.replaceWithData(data.getData(), data.getSize())))
        song.artworkPath = file.getFullPathName();
    }
  }
  return song;
}
inline LibraryState decodeLibrary(const juce::var &v) {
  LibraryState library;
  library.activeSetlistId = v.getProperty("activeSetlistId", {}).toString();
  const auto songs = v.getProperty("songs", {});
  if (const auto *array = songs.getArray()) for (const auto &s : *array) library.songs.push_back(decodeSong(s));
  const auto setlists = v.getProperty("setlists", {});
  if (const auto *array = setlists.getArray()) for (const auto &s : *array) {
    SetlistData setlist;
    setlist.id = s.getProperty("id", {}).toString(); setlist.name = s.getProperty("name", {}).toString();
    const auto items = s.getProperty("items", {});
    if (const auto *entries = items.getArray()) for (const auto &e : *entries) {
      SetlistItem item; item.itemId = e.getProperty("id", {}).toString(); item.songId = e.getProperty("songId", {}).toString();
      item.transitionToNext.autoPlay = static_cast<bool>(e.getProperty("autoPlay", false));
      setlist.items.push_back(item);
    }
    library.setlists.push_back(std::move(setlist));
  }
  return library;
}
} // namespace playworship::remote
