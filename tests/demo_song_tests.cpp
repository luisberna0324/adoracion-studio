#include "data/DemoSongService.h"
#include "data/AppSettings.h"
#include "data/SessionPersistence.h"
#include "IsolatedTestRoot.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool near(double actual, double expected) {
  return std::abs(actual - expected) < 0.00001;
}

juce::File demoPackage() {
  return SongLibraryStore::getRootDirectory()
      .getChildFile("demo/LuzDeEsperanza.pwproj");
}

juce::var libraryJson() {
  return juce::JSON::parse(
      SongLibraryStore::getRootDirectory().getChildFile("songs.json"));
}

juce::String setlistsJson() {
  return juce::JSON::toString(libraryJson()["setlists"]);
}

juce::String personalSongJson(const juce::String &id) {
  const auto songs = libraryJson()["songs"];
  if (const auto *array = songs.getArray())
    for (const auto &song : *array)
      if (song["id"].toString() == id)
        return juce::JSON::toString(song);
  return {};
}

void createPersonalSong(ImportedSongData &song, juce::String &setlistId) {
  const auto root = SongLibraryStore::getRootDirectory();
  const auto file = root.getChildFile("personal.wav");
  auto output = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
  juce::WavAudioFormat format;
  auto writer = format.createWriterFor(output,
      juce::AudioFormatWriterOptions{}.withSampleRate(8000)
          .withNumChannels(1).withBitsPerSample(16));
  expect(writer != nullptr, "create personal audio fixture");
  juce::AudioBuffer<float> audio(1, 8000);
  audio.clear();
  expect(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()),
         "write personal audio fixture");
  writer.reset();
  song.id = song.projectId = "personal-song";
  song.title = "Personal song";
  song.artist = "Personal artist";
  song.bpm = 123;
  song.key = "C";
  song.timeSignature = "4/4";
  ImportedStemData stem;
  stem.stemId = "personal-stem";
  stem.displayName = "Personal guitar";
  stem.category = "Guitar";
  stem.storedPath = stem.sourcePath = file.getFullPathName();
  stem.fileName = file.getFileName();
  stem.durationSec = 1;
  stem.sampleRate = 8000;
  stem.channels = 1;
  song.stems.push_back(stem);
  expect(SongLibraryStore::registerCloudCachedSong(song),
         "register personal fixture");
  setlistId = SongLibraryStore::createSetlist("Personal setlist");
  expect(setlistId.isNotEmpty() &&
             SongLibraryStore::addSongToSetlist(setlistId, song.id) &&
             SongLibraryStore::setActiveSetlistId(setlistId),
         "prepare active personal setlist");
  AppSettings::setLastSelectedSongId(song.id);
}

void expectPristine(const ImportedSongData &song) {
  expect(DemoSongService::isDemoSong(song) && song.stems.size() == 8,
         "demo has stable identity and eight stems");
  expect(song.title == "Luz de esperanza" && song.bpm == 96 && song.key == "D" &&
             song.timeSignature == "4/4", "original demo metadata");
  expect(song.arrangement.parts.size() == 6 && song.arrangement.cues.empty(),
         "six original sections without manual QA cue");
  const std::vector<juce::String> names{
      "Intro", "Verso", "Coro", "Puente", "Coro", "Final"};
  for (size_t index = 0; index < names.size(); ++index)
    expect(song.arrangement.parts[index].name == names[index],
           "original musical section sequence");

  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  for (const auto &stem : song.stems) {
    const auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(juce::File(stem.storedPath)));
    expect(reader != nullptr && reader->numChannels == 1 &&
               near(reader->sampleRate, 24000) && reader->lengthInSamples == 2160000,
           "each demo stem decodes as 90 seconds of mono 24 kHz audio");
    juce::AudioBuffer<float> samples(1, 24000);
    expect(reader->read(&samples, 0, samples.getNumSamples(), 24000, true, false) &&
               samples.getMagnitude(0, 0, samples.getNumSamples()) > 0.0001f,
           "demo stems contain audible decoded samples");
  }
  ProjectSongSettings settings;
  expect(ProjectStore::getCachedSettings(song.projectId, settings),
         "original demo settings cached");
  expect(near(settings.mix.master.volume, 0.82) &&
             near(settings.tempoBpm, 96) && settings.pitchSemitones == 0 &&
             !settings.arrangementCustomized && settings.arrangement.cues.empty(),
         "original master and section settings exclude manual QA changes");
  expect(settings.mix.stems.size() == 8, "original per-instrument mix");
  for (const auto &mix : settings.mix.stems) {
    const auto stem = std::find_if(song.stems.begin(), song.stems.end(),
        [&](const auto &candidate) { return candidate.stemId == mix.stemId; });
    expect(stem != song.stems.end() &&
               near(mix.volume, stem->category == "Click" ? 0.75 : 0.9) &&
               !mix.mute && !mix.solo,
           "original per-instrument levels, mute and solo");
  }
  const auto waveform = SongLibraryStore::loadWaveformCache(song.id);
  expect(waveform.size() == 22400 &&
             *std::max_element(waveform.begin(), waveform.end()) > 0.1f,
         "offline waveform is usable without recomputation");
}

void corruptPackagedWaveform(const juce::File &package) {
  juce::TemporaryFile replacement(package);
  {
    juce::ZipFile archive(package);
    expect(archive.getIndexOfFileName("waveform.bin") >= 0,
           "demo fixture includes a packaged waveform");
    juce::ZipFile::Builder builder;
    for (int index = 0; index < archive.getNumEntries(); ++index) {
      const auto *entry = archive.getEntry(index);
      if (entry->filename == "waveform.bin") {
        const char truncated = 0;
        builder.addEntry(new juce::MemoryInputStream(&truncated, 1, true), 0,
                         entry->filename, entry->fileTime);
      } else {
        auto *stream = archive.createStreamForEntry(index);
        expect(stream != nullptr, "read demo entry for corruption fixture");
        builder.addEntry(stream, 0, entry->filename, entry->fileTime);
      }
    }
    juce::FileOutputStream output(replacement.getFile());
    expect(output.openedOk() && builder.writeToStream(output, nullptr),
           "write demo fixture with a damaged packaged waveform");
  }
  expect(replacement.overwriteTargetFileWithTemporary(),
         "replace only the isolated demo package for the regression fixture");
}

void expectPrepared(const ImportedSongData &song,
                    const ProjectDocument &document,
                    const ProjectSongSettings &settings) {
  expect(document.projectFile == juce::File(song.projectPath) &&
             document.song.id == song.id &&
             document.song.projectId == song.projectId &&
             document.song.title == song.title && document.song.artist == song.artist &&
             document.song.stems.size() == song.stems.size(),
         "prepared document retains package, local identity and library metadata");
  expect(ProjectStore::settingsJsonEqual(document.settings, settings),
         "prepared document carries current settings for activation");
  expect(document.waveformBins == SongLibraryStore::loadWaveformCache(song.id) &&
             document.waveformBins.size() == 22400,
         "prepared document carries the validated offline waveform");
  for (size_t index = 0; index < song.stems.size(); ++index)
    expect(document.song.stems[index].stemId == song.stems[index].stemId &&
               document.song.stems[index].storedPath == song.stems[index].storedPath &&
               juce::File(document.song.stems[index].storedPath).existsAsFile(),
           "prepared document retains every installed stem identity and local resource");
}

void installationPreservesLibrary() {
  ImportedSongData personal;
  juce::String personalSetlist;
  createPersonalSong(personal, personalSetlist);
  const auto originalSetlists = setlistsJson();
  const auto originalPersonal = personalSongJson(personal.id);
  const auto originalAudioHash = juce::SHA256(juce::File(personal.stems[0].storedPath)).toHexString();

  PersistedSessionSnapshot session;
  session.uid = "fixture-account";
  session.firebaseApiKey = "fixture-public-key";
  session.refreshToken = "fixture-refresh-token";
  session.licenseKey = "fixture-license";
  session.plan = "fixture-plan";
  session.allowed = false;
  expect(SessionPersistence::save(session), "prepare isolated denied session fixture");
  const auto sessionBytes = SessionPersistence::getSnapshotFile().loadFileAsString();

  ImportedSongData first;
  ProjectDocument firstDocument;
  double lastProgress = -1;
  const auto installed = DemoSongService::ensureInstalled(first,
      [&](double progress, const juce::String &) {
        expect(progress >= lastProgress && progress >= 0 && progress <= 1,
               "installation progress is monotonic");
        lastProgress = progress;
      }, &firstDocument);
  expect(installed.wasOk() && near(lastProgress, 1), "install bundled demo offline");
  expect(juce::File(first.projectPath) == demoPackage() &&
             demoPackage().isAChildOf(SongLibraryStore::getRootDirectory()),
         "demo package is writable private storage");
  expectPristine(first);
  ProjectSongSettings originalSettings;
  expect(ProjectStore::getCachedSettings(first.projectId, originalSettings),
         "load original settings for prepared installation output");
  expectPrepared(first, firstDocument, originalSettings);
  expect(SongLibraryStore::loadSongs().size() == 2 &&
             setlistsJson() == originalSetlists &&
             personalSongJson(personal.id) == originalPersonal &&
             SongLibraryStore::loadLibrarySnapshot().activeSetlistId == personalSetlist &&
             AppSettings::getLastSelectedSongId() == personal.id,
         "installation leaves personal song, selection and all setlists untouched");
  expect(SessionPersistence::getSnapshotFile().loadFileAsString() == sessionBytes,
         "installation does not change credentials, allowed, plan or license");

  auto renamed = first;
  renamed.title = "My demo mix";
  renamed.artist = "My notes";
  expect(SongLibraryStore::updateSongMetadata(renamed),
         "rename demo through ordinary library metadata");
  expect(DemoSongService::isDemoSong(renamed), "renamed demo retains identity");
  auto impostor = personal;
  impostor.title = first.title;
  impostor.artist = first.artist;
  expect(!DemoSongService::isDemoSong(impostor),
         "title and artist cannot label another song as demo");
  expect(SongLibraryStore::addSongToSetlist(personalSetlist, first.id),
         "user can add demo to an ordinary setlist");

  ProjectSongSettings edited;
  expect(ProjectStore::getCachedSettings(first.projectId, edited),
         "read demo settings for edit fixture");
  edited.mix.master.volume = 0.23f;
  edited.mix.stems.front().volume = 0.17f;
  edited.mix.stems.front().mute = true;
  edited.mix.stems.back().solo = true;
  edited.tempoBpm = 81;
  edited.pitchSemitones = -2;
  ProjectStore::scheduleSettingsSave(demoPackage(), first.projectId, edited);
  ProjectStore::flushPendingSettings(demoPackage());
  const auto editedSetlists = setlistsJson();
  const auto packageHash = juce::SHA256(demoPackage()).toHexString();
  const auto packageTime = demoPackage().getLastModificationTime();
  const auto sidecar = ProjectStore::getCacheDirectory(first.projectId)
      .getChildFile("song-settings.json");
  // Coarse timestamp resolution must not make the prepared document restore
  // embedded defaults over the user's latest local mix.
  expect(sidecar.setLastModificationTime(packageTime),
         "prepare equal package and edited-settings timestamps");
  const auto sidecarBytes = sidecar.loadFileAsString();
  const auto sidecarTime = sidecar.getLastModificationTime();

  ImportedSongData repeated;
  ProjectDocument repeatedDocument;
  expect(DemoSongService::ensureInstalled(repeated, {}, &repeatedDocument).wasOk() &&
             repeated.id == first.id && repeated.title == renamed.title &&
             repeated.artist == renamed.artist && SongLibraryStore::loadSongs().size() == 2,
         "repeat installation returns existing metadata without duplicate entries");
  expectPrepared(repeated, repeatedDocument, edited);
  ProjectSongSettings afterRepeat;
  expect(ProjectStore::getCachedSettings(first.projectId, afterRepeat) &&
             ProjectStore::settingsJsonEqual(edited, afterRepeat) &&
             sidecar.loadFileAsString() == sidecarBytes &&
             sidecar.getLastModificationTime() == sidecarTime &&
             juce::SHA256(demoPackage()).toHexString() == packageHash &&
             demoPackage().getLastModificationTime() == packageTime &&
             setlistsJson() == editedSetlists,
         "repeat installation preserves edits, package and setlist memberships");

  // The installed cache remains usable even if the packaged waveform later
  // becomes corrupt. Reopening it through ProjectStore would delete this cache
  // on validation failure, including the user's edited settings sidecar.
  const auto audioFile = juce::File(repeated.stems.front().storedPath);
  const auto audioHash = juce::SHA256(audioFile).toHexString();
  corruptPackagedWaveform(demoPackage());
  const auto damagedPackageHash = juce::SHA256(demoPackage()).toHexString();
  ImportedSongData usableFromCache;
  ProjectDocument cachedDocument;
  expect(DemoSongService::ensureInstalled(usableFromCache, {}, &cachedDocument).wasOk(),
         "damaged packaged waveform does not invalidate a usable installed demo");
  expectPrepared(usableFromCache, cachedDocument, edited);
  expect(juce::SHA256(audioFile).toHexString() == audioHash &&
             sidecar.loadFileAsString() == sidecarBytes &&
             sidecar.getLastModificationTime() == sidecarTime &&
             juce::SHA256(demoPackage()).toHexString() == damagedPackageHash &&
             setlistsJson() == editedSetlists,
         "preparing the installed demo preserves audio, settings and damaged package");

  expect(sidecar.deleteFile(), "simulate unavailable local demo settings");
  ProjectDocument unavailableSettings = cachedDocument;
  ImportedSongData unavailableSong = usableFromCache;
  expect(DemoSongService::ensureInstalled(unavailableSong, {}, &unavailableSettings).failed() &&
             unavailableSong.id.isEmpty() && unavailableSettings.song.id.isEmpty() &&
             juce::SHA256(audioFile).toHexString() == audioHash &&
             juce::SHA256(demoPackage()).toHexString() == damagedPackageHash &&
             !sidecar.exists() && setlistsJson() == editedSetlists,
         "missing local settings returns a recoverable error without resetting any files");
  expect(sidecar.replaceWithText(sidecarBytes) &&
             sidecar.setLastModificationTime(sidecarTime),
         "restore isolated demo settings fixture");

  expect(SongLibraryStore::deleteSong(first.id), "delete demo through ordinary library flow");
  expect(demoPackage().existsAsFile() && SongLibraryStore::loadSongs().size() == 1,
         "deletion removes library entry while preserving portable package");
  const auto afterDeletionSetlists = setlistsJson();
  ImportedSongData recovered;
  expect(DemoSongService::ensureInstalled(recovered).wasOk(),
         "explicit recovery works offline after deletion");
  expectPristine(recovered);
  expect(setlistsJson() == afterDeletionSetlists &&
             personalSongJson(personal.id) == originalPersonal &&
             juce::SHA256(juce::File(personal.stems[0].storedPath)).toHexString() == originalAudioHash &&
             SessionPersistence::getSnapshotFile().loadFileAsString() == sessionBytes,
         "recovery restores only demo and does not re-add old setlist memberships");

  // A damaged existing entry must not silently erase the user's adjustments.
  const auto missingStem = juce::File(recovered.stems.back().storedPath);
  expect(missingStem.deleteFile(), "simulate removed cached stem");
  const auto databaseBeforeFailure = libraryJson();
  ImportedSongData failed = recovered;
  ProjectDocument failedDocument = firstDocument;
  expect(DemoSongService::ensureInstalled(failed, {}, &failedDocument).failed() &&
             failed.id.isEmpty() && failedDocument.song.id.isEmpty() &&
             failedDocument.projectFile == juce::File{} &&
             failedDocument.waveformBins.empty() && failedDocument.song.stems.empty() &&
             juce::JSON::toString(libraryJson()) == juce::JSON::toString(databaseBeforeFailure),
         "damaged existing demo returns error without resetting or duplicating it");
  expect(SongLibraryStore::deleteSong(recovered.id), "remove damaged demo fixture");
}

void failuresDoNotRegisterPartialSong() {
  const auto root = SongLibraryStore::getRootDirectory();
  const auto before = root.getChildFile("songs.json").loadFileAsString();
  expect(root.getChildFile("demo").deleteRecursively(), "clear deleted demo storage fixture");
  expect(root.getChildFile("demo").replaceWithText("blocked"), "block installation directory");
  ImportedSongData output;
  output.id = "must-be-cleared";
  expect(DemoSongService::ensureInstalled(output).failed() && output.id.isEmpty() &&
             root.getChildFile("songs.json").loadFileAsString() == before,
         "copy failure does not add a partial entry or alter existing inventory");
  expect(root.getChildFile("demo").deleteFile(), "remove blocked directory fixture");

  // Introduce a real storage failure after extraction but before registration.
  // songs.json is restored by the test, never by the service under test.
  const auto database = root.getChildFile("songs.json");
  const auto savedDatabase = root.getChildFile("songs.saved");
  bool blocked = false;
  const auto result = DemoSongService::ensureInstalled(output,
      [&](double fraction, const juce::String &) {
        if (!blocked && fraction >= 0.94) {
          expect(database.moveFileTo(savedDatabase), "preserve library before injected save failure");
          expect(database.createDirectory().wasOk() &&
                     database.getChildFile("keep").replaceWithText("not a database"),
                 "block atomic database replacement");
          blocked = true;
        }
      });
  expect(blocked && result.failed() && output.id.isEmpty() &&
             !demoPackage().existsAsFile(),
         "registration failure returns no song and removes newly installed package");
  expect(ProjectStore::getRecentProjects().empty(),
         "registration failure removes demo from recent projects");
  expect(database.deleteRecursively() && savedDatabase.moveFileTo(database),
         "restore personal inventory after injected failure");
  expect(database.loadFileAsString() == before && SongLibraryStore::loadSongs().size() == 1,
         "personal library remains intact after registration failure");

  expect(database.moveFileTo(savedDatabase) && database.replaceWithText("{bad json"),
         "prepare unreadable library fixture");
  expect(DemoSongService::ensureInstalled(output).failed() && output.id.isEmpty() &&
             database.loadFileAsString() == "{bad json" && !demoPackage().existsAsFile(),
         "malformed library is not replaced with a fresh inventory");
  expect(database.deleteFile() && savedDatabase.moveFileTo(database),
         "restore unreadable library fixture");
}
} // namespace

int main() {
  try {
    const auto root = isolatedTestRoot();
    expect(SongLibraryStore::getRootDirectory().isAChildOf(root) &&
               SessionPersistence::getSnapshotFile().isAChildOf(root),
           "all demo storage tests require an isolated temporary root");
    installationPreservesLibrary();
    failuresDoNotRegisterPartialSong();
    ProjectStore::flushAllPendingSettings();
    std::cout << "Demo song: all checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
