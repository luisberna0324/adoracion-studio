#include "data/ProjectStore.h"
#include "IsolatedTestRoot.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using namespace playworship::lyrics;

void expect(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

bool equal(const SongLyrics &a, const SongLyrics &b) {
  SongLyrics left, right;
  return fromVar(toVar(a), left) && fromVar(toVar(b), right) &&
         juce::JSON::toString(toVar(left)) == juce::JSON::toString(toVar(right));
}

SongLyrics exampleLyrics() {
  SongLyrics result;
  result.enabled = true;
  result.showId = "own-test-show";
  result.showName = "Original test lyrics";
  result.layoutId = "layout-main";
  result.layoutName = "Main arrangement";
  Target first;
  first.showId = result.showId;
  first.layoutId = result.layoutId;
  first.groupId = "literal-coro-2";
  first.slideId = "screen-two";
  first.occurrence = 2;
  first.layoutSignature = "exact-layout-reference";
  first.title = "Coro 2 / screen 2 / occurrence 2";
  first.text = "Our own test lyric\nA second compact line";
  result.marks.push_back({"second-occurrence", 0.25, true, first});
  auto disabled = first;
  disabled.slideId = "screen-one";
  disabled.occurrence = 1;
  disabled.useActiveLayout = true;
  disabled.layoutId.clear(); disabled.layoutSignature.clear();
  result.marks.push_back({"disabled-mark", 0.5, false, disabled});
  Target hide;
  hide.hide = true;
  hide.title = "Hide lyrics";
  result.marks.push_back({"hide-mark", 0.75, true, hide});
  return result;
}

ImportedSongData blankSong(const juce::String &id) {
  ImportedSongData song;
  song.id = song.projectId = id;
  song.title = "Original lyric persistence test";
  song.bpm = 120;
  song.key = "C";
  song.timeSignature = "4/4";
  song.blankSong = true;
  return song;
}

SongArrangementSnapshot arrangement() {
  SongArrangementPartSnapshot part;
  part.id = "music-only-section";
  part.name = "Verse";
  part.color = standardArrangementPartColorForName(part.name);
  part.endSec = part.sourceEndSec = 8;
  return {{part}, {}};
}

SongLyricsDocument nativeLyricsExample() {
  SongLyricsDocument result;
  result.text = "Cantamos juntos\nSeguimos aqui";
  expect(applySongLyricsSection(result, 0, songLyricsUtf16Length(result.text), "chorus") &&
             setSongLyricsChord(result, 3, "Am7") &&
             setSongLyricsChord(result, 20, "D/F#"),
         "native coexistence fixture contains text, a section and anchored chords");
  return result;
}

juce::String canonicalNativeLyrics(const std::optional<SongLyricsDocument> &lyrics) {
  return lyrics ? juce::JSON::toString(songLyricsToVar(*lyrics), true) : juce::String();
}

bool runNativeLyricsWorker(const std::function<bool()> &action) {
  std::atomic<bool> done{false};
  bool result = false;
  std::exception_ptr failure;
  std::thread worker([&] {
    try { result = action(); } catch (...) { failure = std::current_exception(); }
    done.store(true, std::memory_order_release);
  });
  while (!done.load(std::memory_order_acquire))
    juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
  worker.join();
  if (failure) std::rethrow_exception(failure);
  return result;
}

ImportedSongData registeredSong(const juce::String &id) {
  const auto songs = SongLibraryStore::loadSongs();
  const auto found = std::find_if(songs.begin(), songs.end(),
      [&](const auto &song) { return song.id == id; });
  expect(found != songs.end(), "coexistence fixture remains in the actual song library");
  return *found;
}

void testNativeAndLegacyPayloadDiscrimination() {
  const auto cues = exampleLyrics();
  const auto native = nativeLyricsExample();
  const auto nativeJson = canonicalNativeLyrics(native);
  auto song = blankSong("both-lyrics-json");
  song.lyrics = native;
  song.freeShowCues = cues;
  auto encoded = SongLibraryStore::songDataToVar(song);
  bool changed = false;
  const auto decoded = SongLibraryStore::songDataFromVar(encoded, changed);
  expect(equal(decoded.freeShowCues, cues) && canonicalNativeLyrics(decoded.lyrics) == nativeJson &&
             encoded.hasProperty("lyrics") && encoded.hasProperty("freeShowCues"),
         "native content and FreeShow cues round-trip under distinct song keys");

  auto legacySong = SongLibraryStore::songDataToVar(blankSong("legacy-freeshow-json"));
  legacySong.getDynamicObject()->removeProperty("freeShowCues");
  legacySong.getDynamicObject()->setProperty("lyrics", toVar(cues));
  const auto restoredLegacy = SongLibraryStore::songDataFromVar(legacySong, changed);
  expect(equal(restoredLegacy.freeShowCues, cues) && !restoredLegacy.lyrics,
         "legacy FreeShow lyrics shape migrates to cues without becoming editable native lyrics");

  auto nativeOnly = SongLibraryStore::songDataToVar(song);
  nativeOnly.getDynamicObject()->removeProperty("freeShowCues");
  const auto restoredNative = SongLibraryStore::songDataFromVar(nativeOnly, changed);
  expect(restoredNative.freeShowCues.marks.empty() &&
             canonicalNativeLyrics(restoredNative.lyrics) == nativeJson,
         "a schemaVersion native document under lyrics never becomes a FreeShow payload");

  ProjectSongSettings settings;
  settings.freeShowCues = cues;
  auto legacySettings = ProjectStore::serializeSettings(settings);
  legacySettings.getDynamicObject()->removeProperty("freeShowCues");
  legacySettings.getDynamicObject()->setProperty("lyrics", toVar(cues));
  ProjectSongSettings restored;
  expect(ProjectStore::deserializeSettings(legacySettings, restored) &&
             equal(restored.freeShowCues, cues),
         "legacy FreeShow settings under lyrics remain readable");

  auto nativeSettings = ProjectStore::serializeSettings({});
  nativeSettings.getDynamicObject()->removeProperty("freeShowCues");
  nativeSettings.getDynamicObject()->setProperty("lyrics", songLyricsToVar(native));
  expect(ProjectStore::deserializeSettings(nativeSettings, restored) &&
             restored.freeShowCues.marks.empty() && !restored.freeShowCues.enabled,
         "native lyric shape in an old settings key is not interpreted as FreeShow marks");

  auto latestCues = cues;
  latestCues.marks.front().seconds = 4.25;
  auto precedence = ProjectStore::serializeSettings(settings);
  precedence.getDynamicObject()->setProperty("freeShowCues", toVar(latestCues));
  precedence.getDynamicObject()->setProperty("lyrics", toVar(cues));
  expect(ProjectStore::deserializeSettings(precedence, restored) &&
             equal(restored.freeShowCues, latestCues),
         "explicit FreeShow field wins over a legacy lyrics field");
  precedence.getDynamicObject()->setProperty("freeShowCues", "malformed");
  expect(!ProjectStore::deserializeSettings(precedence, restored),
         "malformed explicit FreeShow field cannot silently fall back to legacy lyrics");

  auto malformedLegacy = toVar(cues);
  auto *marks = malformedLegacy.getDynamicObject()->getProperty("marks").getArray();
  expect(marks != nullptr && !marks->isEmpty(), "legacy fixture has its distinctive marks shape");
  (*marks)[0].getDynamicObject()->setProperty("seconds", -1.0);
  legacySettings.getDynamicObject()->setProperty("lyrics", malformedLegacy);
  expect(!ProjectStore::deserializeSettings(legacySettings, restored),
         "recognized legacy FreeShow payload still receives strict cue validation");
}

void testCanonicalAndLegacySchemas() {
  ProjectSongSettings settings;
  settings.freeShowCues = exampleLyrics();
  settings.arrangement = arrangement();
  settings.arrangementCustomized = true;
  settings.mix.master.volume = 0.4f;
  ProjectSongSettings decoded;
  expect(ProjectStore::deserializeSettings(
             juce::JSON::parse(juce::JSON::toString(
                 ProjectStore::serializeSettings(settings))), decoded) &&
             ProjectStore::settingsJsonEqual(decoded, settings),
         "canonical JSON round-trips marks, IDs, repetition, hide and disabled state");
  auto legacy = ProjectStore::serializeSettings(settings);
  legacy.getDynamicObject()->removeProperty("freeShowCues");
  expect(ProjectStore::deserializeSettings(legacy, decoded) &&
             decoded.freeShowCues.marks.empty() && !decoded.freeShowCues.enabled &&
             decoded.arrangement.parts.front().id == "music-only-section",
         "old settings without lyrics remain readable and preserve sections");

  auto song = blankSong("legacy-song-json");
  song.freeShowCues = settings.freeShowCues;
  song.arrangement = settings.arrangement;
  bool changed = false;
  auto encodedSong = SongLibraryStore::songDataToVar(song);
  const auto restored = SongLibraryStore::songDataFromVar(encodedSong, changed);
  expect(equal(restored.freeShowCues, song.freeShowCues) &&
             restored.arrangement.parts.front().id == "music-only-section",
         "song descriptors carry independent marks without section associations");
  encodedSong.getDynamicObject()->removeProperty("freeShowCues");
  expect(SongLibraryStore::songDataFromVar(encodedSong, changed).freeShowCues.marks.empty(),
         "old song descriptors remain readable");
  const auto json = juce::JSON::toString(toVar(settings.freeShowCues));
  expect(!json.containsIgnoreCase("host") && !json.containsIgnoreCase("token") &&
             !json.containsIgnoreCase("password") && !json.containsIgnoreCase("port"),
         "portable lyric data includes no connection or authentication settings");
}

void testAtomicValidation() {
  ProjectSongSettings original;
  original.freeShowCues = exampleLyrics();
  original.tempoBpm = 103;
  for (int invalidCase = 0; invalidCase < 4; ++invalidCase) {
    auto malformed = ProjectStore::serializeSettings(original);
    auto *marks = malformed.getDynamicObject()->getProperty("freeShowCues")
                      .getDynamicObject()->getProperty("marks").getArray();
    expect(marks != nullptr && marks->size() == 3, "fixture has serialized marks");
    if (invalidCase == 0)
      (*marks)[0].getDynamicObject()->setProperty("seconds", -1.0);
    else if (invalidCase == 1)
      (*marks)[1].getDynamicObject()->setProperty("id", "second-occurrence");
    else if (invalidCase == 2)
      (*marks)[0].getDynamicObject()->getProperty("target")
          .getDynamicObject()->setProperty("occurrence", 0);
    else
      malformed.getDynamicObject()->setProperty("freeShowCues", "invalid-object");
    auto unchanged = original;
    expect(!ProjectStore::deserializeSettings(malformed, unchanged) &&
               ProjectStore::settingsJsonEqual(unchanged, original),
           "invalid lyric settings are rejected atomically");
  }
}

void writeManifestPackage(const juce::File &file, const juce::var &manifest) {
  const auto text = juce::JSON::toString(manifest);
  juce::ZipFile::Builder builder;
  builder.addEntry(new juce::MemoryInputStream(
                       text.toRawUTF8(), static_cast<size_t>(text.getNumBytesAsUTF8()), true),
                   0, "manifest.json", juce::Time::getCurrentTime());
  juce::FileOutputStream output(file);
  expect(output.openedOk() && builder.writeToStream(output, nullptr),
         "write actual old/malformed manifest package");
}

void testOldAndMalformedProject(const juce::File &root) {
  const auto id = "lyrics-old-schema-" + juce::Uuid().toString();
  auto song = SongLibraryStore::songDataToVar(blankSong(id));
  song.getDynamicObject()->removeProperty("freeShowCues");
  auto settings = ProjectStore::serializeSettings({});
  settings.getDynamicObject()->removeProperty("freeShowCues");
  juce::var manifest(new juce::DynamicObject());
  manifest.getDynamicObject()->setProperty("schemaVersion", ProjectStore::kSchemaVersion);
  manifest.getDynamicObject()->setProperty("song", song);
  manifest.getDynamicObject()->setProperty("songSettings", settings);
  const auto oldPackage = root.getChildFile(id + ".pwproj");
  writeManifestPackage(oldPackage, manifest);
  ProjectDocument oldDocument;
  juce::String error;
  expect(ProjectStore::open(oldPackage, oldDocument, &error, false) &&
             oldDocument.settings.freeShowCues.marks.empty(),
         "real old project without lyrics opens backwards compatibly");

  settings.getDynamicObject()->setProperty("freeShowCues", "malformed");
  const auto malformedPackage = root.getChildFile(id + "-malformed.pwproj");
  writeManifestPackage(malformedPackage, manifest);
  const auto before = juce::SHA256(malformedPackage).toHexString();
  ProjectDocument unchanged;
  unchanged.song.title = "unchanged sentinel";
  unchanged.settings.tempoBpm = 91;
  expect(!ProjectStore::open(malformedPackage, unchanged, &error) &&
             error.isNotEmpty() && unchanged.song.title == "unchanged sentinel" &&
             unchanged.settings.tempoBpm == 91 &&
             juce::SHA256(malformedPackage).toHexString() == before,
         "malformed actual project fails without partial output or package changes");
  juce::var cacheValue(new juce::DynamicObject());
  cacheValue.getDynamicObject()->setProperty("schemaVersion", ProjectStore::kSchemaVersion);
  cacheValue.getDynamicObject()->setProperty("songSettings", settings);
  const auto cacheFile = ProjectStore::getCacheDirectory(id).getChildFile("song-settings.json");
  const auto malformedCache = juce::JSON::toString(cacheValue);
  expect(cacheFile.replaceWithText(malformedCache) &&
             cacheFile.setLastModificationTime(juce::Time(
                 oldPackage.getLastModificationTime().toMilliseconds() + 1000)),
         "write own malformed newer sidecar fixture");
  expect(!ProjectStore::open(oldPackage, unchanged, &error) && error.isNotEmpty() &&
             unchanged.song.title == "unchanged sentinel" &&
             cacheFile.loadFileAsString() == malformedCache,
         "malformed newer cache cannot be silently overwritten by embedded settings");
  ProjectSongSettings unchangedSettings;
  unchangedSettings.tempoBpm = 98;
  expect(!ProjectStore::getCachedSettings(oldPackage, unchangedSettings) &&
             unchangedSettings.tempoBpm == 98,
         "cold cached-settings lookup fails atomically on a malformed sidecar");
  ProjectStore::discardLocalState(id, oldPackage);
}

void testProjectAndLibrary(const juce::File &root) {
  const auto id = "lyrics-project-" + juce::Uuid().toString();
  const auto archive = root.getChildFile(id + ".pwproj");
  ProjectDocument document;
  document.song = blankSong(id);
  document.settings.freeShowCues = exampleLyrics();
  document.settings.arrangement = arrangement();
  document.settings.arrangementCustomized = true;
  document.settings.tempoBpm = 111;
  document.settings.mix.master.volume = 0.61f;
  document.settings.pitchSemitones = -2;
  document.settings.blankPadEnabled = true;
  juce::String error;
  expect(ProjectStore::save(document, archive, &error), "actual portable project saves");
  ProjectDocument opened;
  expect(ProjectStore::open(archive, opened, &error) &&
             equal(opened.settings.freeShowCues, document.settings.freeShowCues) &&
             equal(opened.song.freeShowCues, document.settings.freeShowCues),
         "actual .pwproj open restores canonical and imported lyric marks");
  expect(SongLibraryStore::registerProjectSong(opened.song), "project registers in real isolated library");
  auto marks = exampleLyrics();
  marks.marks.front().seconds = 3.25;
  const auto archiveHash = juce::SHA256(archive).toHexString();
  expect(SongLibraryStore::updateSongFreeShowCues(opened.song.id, marks, &error),
         "lyric edit persists to project sidecar");
  SongLyrics loaded;
  expect(SongLibraryStore::getSongFreeShowCues(opened.song.id, loaded) && equal(loaded, marks),
         "lyric lookup reads newest sidecar snapshot");
  expect(ProjectStore::hasPendingPortableChanges(archive) &&
             juce::SHA256(archive).toHexString() == archiveHash,
         "independent mark edit is durable locally before explicit portable save");
  expect(SongLibraryStore::updateMasterVolumeForActiveSetlist(opened.song.id, 0.37f) &&
             SongLibraryStore::updateTempoBpmForActiveSetlist(opened.song.id, 135),
         "ordinary mixer and tempo updates succeed");
  auto movedSection = arrangement();
  movedSection.parts.front().startSec = 2;
  expect(SongLibraryStore::updateSongArrangement(opened.song.id, movedSection, true),
         "musical section edit succeeds independently");
  ProjectSongSettings cached;
  expect(ProjectStore::getCachedSettings(projectIdentityForSong(opened.song), cached) &&
             equal(cached.freeShowCues, marks) && cached.tempoBpm == 135 &&
             std::abs(cached.mix.master.volume - 0.37f) < 0.0001f &&
             cached.pitchSemitones == -2 && cached.blankPadEnabled == true &&
             cached.arrangement.parts.front().startSec == 2,
         "mixer, tempo, pad and sections preserve complete lyrics settings");
  const auto invalid = [&] {
    auto value = marks;
    value.marks.front().seconds = -1;
    return value;
  }();
  expect(!SongLibraryStore::updateSongFreeShowCues(opened.song.id, invalid, &error) && error.isNotEmpty() &&
             SongLibraryStore::getSongFreeShowCues(opened.song.id, loaded) && equal(loaded, marks),
         "failed mark update leaves previous sidecar intact");
  expect(ProjectStore::savePendingPortableChanges(archive, &error) &&
             ProjectStore::open(archive, opened, &error, false) &&
             equal(opened.settings.freeShowCues, marks) &&
             equal(opened.song.freeShowCues, marks) && opened.settings.tempoBpm == 135,
         "explicit portable save embeds latest independent marks and full settings");
  const auto exported = root.getChildFile(id + "-export.pwproj");
  expect(ProjectStore::save(opened, exported, &error), "real project export succeeds");
  ProjectDocument imported;
  expect(ProjectStore::open(exported, imported, &error, false) &&
             equal(imported.settings.freeShowCues, marks),
         "exported project reimports exact destinations and mark positions");
  imported.settings.freeShowCues.marks.front().seconds = std::numeric_limits<double>::infinity();
  const auto exportHash = juce::SHA256(exported).toHexString();
  expect(!ProjectStore::save(imported, exported, &error) && error.isNotEmpty() &&
             juce::SHA256(exported).toHexString() == exportHash,
         "invalid nonfinite mark cannot overwrite an existing project");
  ProjectStore::discardLocalState(id, archive);
}

void testNativeAndFreeShowCoexistence(const juce::File &root) {
  const auto id = "lyrics-coexistence-" + juce::Uuid().toString();
  const auto archive = root.getChildFile(id + ".pwproj");
  ProjectDocument document;
  document.song = blankSong(id);
  document.song.lyrics = nativeLyricsExample();
  document.settings.freeShowCues = exampleLyrics();
  document.song.freeShowCues = document.settings.freeShowCues;
  const auto originalNative = canonicalNativeLyrics(document.song.lyrics);
  juce::String error;
  expect(ProjectStore::save(document, archive, &error),
         "one actual portable project saves native lyrics/chords and FreeShow cues");
  ProjectDocument opened;
  expect(ProjectStore::open(archive, opened, &error) &&
             canonicalNativeLyrics(opened.song.lyrics) == originalNative &&
             equal(opened.settings.freeShowCues, document.settings.freeShowCues) &&
             equal(opened.song.freeShowCues, document.settings.freeShowCues),
         "the same pwproj reopens both independent documents without one replacing the other");
  expect(SongLibraryStore::registerProjectSong(opened.song),
         "register both native and FreeShow documents in the real library");

  // Leave a newer cue snapshot in the sidecar before the content-only native
  // transaction. It must not restore the older FreeShow data embedded in ZIP.
  auto latestCues = exampleLyrics();
  latestCues.marks.front().seconds = 3.25;
  expect(SongLibraryStore::updateSongFreeShowCues(id, latestCues, &error),
         "FreeShow edit establishes newer sidecar cues before a native lyric edit");
  auto editedNative = *opened.song.lyrics;
  const auto originalChordId = editedNative.chords.front().id;
  expect(replaceSongLyricsText(editedNative, 0, 0, "Hoy "),
         "edit native text while retaining and shifting its stable chord anchors");
  const auto editedNativeJson = canonicalNativeLyrics(editedNative);
  expect(runNativeLyricsWorker([&] {
    return SongLibraryStore::updateSongLyrics(id, editedNative, &error);
  }), "native content-only transaction saves the edited lyric document");
  SongLyrics loadedCues;
  const auto afterNative = registeredSong(id);
  expect(canonicalNativeLyrics(afterNative.lyrics) == editedNativeJson &&
             afterNative.lyrics->chords.front().id == originalChordId &&
             afterNative.lyrics->chords.front().offset == 7 &&
             SongLibraryStore::getSongFreeShowCues(id, loadedCues) &&
             equal(loadedCues, latestCues),
         "editing native lyrics preserves the newest FreeShow cues and stable chord identities");
  expect(ProjectStore::open(archive, opened, &error) &&
             canonicalNativeLyrics(opened.song.lyrics) == editedNativeJson &&
             equal(opened.settings.freeShowCues, latestCues),
         "reopening after native save preserves pending FreeShow settings rather than stale ZIP values");

  latestCues.marks.front().seconds = 4.5;
  latestCues.marks.front().target.title = "Changed FreeShow destination";
  expect(SongLibraryStore::updateSongFreeShowCues(id, latestCues, &error) &&
             canonicalNativeLyrics(registeredSong(id).lyrics) == editedNativeJson,
         "editing FreeShow cues preserves native text, sections and exact chord metadata");
  expect(ProjectStore::savePendingPortableChanges(archive, &error) &&
             ProjectStore::open(archive, opened, &error, false) &&
             canonicalNativeLyrics(opened.song.lyrics) == editedNativeJson &&
             equal(opened.settings.freeShowCues, latestCues) &&
             equal(opened.song.freeShowCues, latestCues),
         "consolidating and reopening the actual pwproj retains both latest independent documents");
  ProjectStore::discardLocalState(id, archive);
}

void testLegacyFreeShowProjectMigration(const juce::File &root) {
  const auto id = "lyrics-legacy-key-" + juce::Uuid().toString();
  const auto archive = root.getChildFile(id + ".pwproj");
  const auto cues = exampleLyrics();
  auto song = SongLibraryStore::songDataToVar(blankSong(id));
  song.getDynamicObject()->removeProperty("freeShowCues");
  song.getDynamicObject()->setProperty("lyrics", toVar(cues));
  auto settings = ProjectStore::serializeSettings({});
  settings.getDynamicObject()->removeProperty("freeShowCues");
  settings.getDynamicObject()->setProperty("lyrics", toVar(cues));
  juce::var manifest(new juce::DynamicObject());
  manifest.getDynamicObject()->setProperty("schemaVersion", ProjectStore::kSchemaVersion);
  manifest.getDynamicObject()->setProperty("song", song);
  manifest.getDynamicObject()->setProperty("songSettings", settings);
  writeManifestPackage(archive, manifest);
  ProjectDocument opened;
  juce::String error;
  expect(ProjectStore::open(archive, opened, &error) && !opened.song.lyrics &&
             equal(opened.song.freeShowCues, cues) && equal(opened.settings.freeShowCues, cues) &&
             SongLibraryStore::registerProjectSong(opened.song),
         "an actual legacy FreeShow pwproj opens its old lyrics keys as cues, not native text");
  const auto native = nativeLyricsExample();
  expect(runNativeLyricsWorker([&] {
    return SongLibraryStore::updateSongLyrics(id, native, &error);
  }) && ProjectStore::open(archive, opened, &error, false) &&
             canonicalNativeLyrics(opened.song.lyrics) == canonicalNativeLyrics(native) &&
             equal(opened.settings.freeShowCues, cues) && equal(opened.song.freeShowCues, cues),
         "adding native lyrics to a legacy FreeShow package migrates cues without overwriting either document");
  ProjectStore::discardLocalState(id, archive);
}

void testLegacyMigrationAndCorruption(const juce::File &root) {
  auto song = blankSong("lyrics-legacy-" + juce::Uuid().toString());
  song.projectId.clear();
  song.blankSong = false;
  const auto source = root.getChildFile(song.id + ".flac");
  {
    std::unique_ptr<juce::OutputStream> stream(source.createOutputStream());
    juce::FlacAudioFormat format;
    auto writer = format.createWriterFor(
        stream, juce::AudioFormatWriterOptions{}.withSampleRate(8000)
                    .withNumChannels(1).withBitsPerSample(16));
    expect(writer != nullptr, "create own FLAC fixture");
    juce::AudioBuffer<float> samples(1, 8000);
    for (int i = 0; i < samples.getNumSamples(); ++i)
      samples.setSample(0, i, 0.1f * std::sin(static_cast<float>(i) * 0.08f));
    expect(writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples()),
           "write own original FLAC samples");
  }
  ImportedStemData stem;
  stem.stemId = "own-test-stem";
  stem.sourcePath = source.getFullPathName();
  stem.fileName = source.getFileName();
  stem.displayName = "Original test audio";
  stem.category = "Keys";
  stem.durationSec = 1;
  stem.sampleRate = 8000;
  stem.channels = 1;
  song.stems.push_back(stem);
  song.freeShowCues = exampleLyrics();
  juce::String error;
  expect(SongLibraryStore::appendSong(song, &error), "legacy own-audio song appends");
  auto marks = exampleLyrics();
  marks.marks.front().seconds = 4.5;
  expect(SongLibraryStore::updateSongFreeShowCues(song.id, marks, &error), "legacy mark update saves");
  const auto legacySongs = SongLibraryStore::loadSongs();
  const auto found = std::find_if(legacySongs.begin(), legacySongs.end(),
                                [&](const auto &value) { return value.id == song.id; });
  expect(found != legacySongs.end() && equal(found->freeShowCues, marks),
         "legacy database round-trips lyric marks");
  const auto converted = root.getChildFile(song.id + "-converted.pwproj");
  ImportedSongData migrated;
  juce::String setlist;
  expect(ProjectStore::convertLegacySongToProject(song.id, converted, migrated, setlist, &error),
         "production legacy conversion succeeds");
  ProjectDocument reopened;
  expect(ProjectStore::open(converted, reopened, &error, false) &&
             equal(reopened.settings.freeShowCues, marks) && equal(reopened.song.freeShowCues, marks),
         "legacy conversion preserves marks in project settings and song descriptor");

  const auto database = SongLibraryStore::getRootDirectory().getChildFile("songs.json");
  const auto goodDatabase = database.loadFileAsString();
  auto bad = juce::JSON::parse(goodDatabase);
  auto *songs = bad.getDynamicObject()->getProperty("songs").getArray();
  expect(songs != nullptr && !songs->isEmpty(), "corruption fixture has library songs");
  (*songs)[0].getDynamicObject()->setProperty("freeShowCues", "malformed");
  const auto malformed = juce::JSON::toString(bad);
  expect(database.replaceWithText(malformed), "write isolated malformed fixture");
  expect(!SongLibraryStore::updateSongFreeShowCues(migrated.id, marks, &error) &&
             database.loadFileAsString() == malformed,
         "invalid legacy lyrics prevent partial library rewrite");
  expect(database.replaceWithText(goodDatabase), "restore own isolated database");
  ProjectStore::discardLocalState(projectIdentityForSong(migrated), converted);
}
} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  try {
    const auto root = isolatedTestRoot();
    expect(!SongLibraryStore::getRootDirectory().getChildFile("songs.json").exists(),
           "persistence test requires a fresh isolated database");
    testCanonicalAndLegacySchemas();
    testNativeAndLegacyPayloadDiscrimination();
    testAtomicValidation();
    testOldAndMalformedProject(root);
    testProjectAndLibrary(root);
    testNativeAndFreeShowCoexistence(root);
    testLegacyFreeShowProjectMigration(root);
    testLegacyMigrationAndCorruption(root);
    ProjectStore::flushAllPendingSettings();
    std::cout << "PASS: independent native lyrics/chords and FreeShow cues, canonical/legacy validation, sidecar/save/open/export and migration\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
