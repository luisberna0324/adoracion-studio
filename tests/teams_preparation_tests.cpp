#include "data/TeamsPreparationService.h"
#include "data/SessionPersistence.h"
#include "IsolatedTestRoot.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
bool near(double a, double b) { return std::abs(a - b) < 0.00001; }

void writeAudio(const juce::File &file) {
  auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
  juce::FlacAudioFormat format;
  auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
      .withSampleRate(8000).withNumChannels(1).withBitsPerSample(16));
  expect(writer != nullptr, "real FLAC fixture writer");
  juce::AudioBuffer<float> samples(1, 8000);
  for (int i = 0; i < samples.getNumSamples(); ++i)
    samples.setSample(0, i, 0.2f * std::sin(static_cast<float>(i) * 0.02f));
  expect(writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples()),
         "real FLAC fixture samples");
}

CloudDownloadedObject objectFor(const juce::File &file, const juce::String &type) {
  return {{juce::SHA256(file).toHexString(), file.getSize(), type}, file};
}

struct Fixture {
  juce::File directory;
  ImportedSongData source;
  CloudDownloadResult downloaded;

  Fixture() : directory(SongLibraryStore::getRootDirectory().getChildFile("cloud-content/team-fixtures")) {
    expect(directory.createDirectory().wasOk(), "fixture directory");
    const auto audio = directory.getChildFile("guitar.flac");
    writeAudio(audio);
    source.id = "publisher-local-song";
    source.projectId = "publisher-project";
    source.title = "Shared song";
    source.artist = "Band";
    source.bpm = 120;
    source.key = "C";
    ImportedStemData stem;
    stem.stemId = "guitar";
    stem.displayName = "Guitar";
    stem.category = "Guitar";
    stem.fileName = "guitar.flac";
    stem.storedPath = audio.getFullPathName();
    stem.sourcePath = stem.storedPath;
    stem.durationSec = 1;
    stem.sampleRate = 8000;
    stem.channels = 1;
    source.stems.push_back(stem);
    const auto manifest = directory.getChildFile("manifest.json");
    auto root = juce::var(new juce::DynamicObject());
    root.getDynamicObject()->setProperty("schemaVersion", ProjectStore::kSchemaVersion);
    root.getDynamicObject()->setProperty("song", SongLibraryStore::songDataToVar(source));
    expect(manifest.replaceWithText(juce::JSON::toString(root)), "portable manifest fixture");
    downloaded.descriptor.songId = "remote-song";
    downloaded.descriptor.projectId = source.projectId;
    downloaded.descriptor.revision = 1;
    downloaded.descriptor.title = source.title;
    downloaded.descriptor.bpm = source.bpm;
    downloaded.descriptor.key = source.key;
    downloaded.descriptor.durationSeconds = 1;
    downloaded.objects.push_back(objectFor(manifest, "application/json"));
    downloaded.descriptor.settings = downloaded.objects.back().reference;
    downloaded.objects.push_back(objectFor(audio, "audio/flac"));
    CloudStemDescriptor descriptorStem;
    static_cast<CloudObjectRef &>(descriptorStem) = downloaded.objects.back().reference;
    descriptorStem.stemId = stem.stemId;
    descriptorStem.displayName = stem.displayName;
    descriptorStem.category = stem.category;
    descriptorStem.durationSec = 1;
    descriptorStem.sampleRate = 8000;
    descriptorStem.channels = 1;
    downloaded.descriptor.stems.push_back(descriptorStem);

  }
};

void localPreparationFlow() {
  const auto root = isolatedTestRoot();
  expect(SongLibraryStore::getRootDirectory().isAChildOf(root), "library is isolated");
  expect(SessionPersistence::getSnapshotFile().isAChildOf(root),
         "session persistence is isolated before writing any credentials");
  PersistedSessionSnapshot session;
  session.uid = "alice";
  session.firebaseApiKey = "test-public-key";
  session.refreshToken = "test-refresh-token";
  session.allowed = true;
  expect(SessionPersistence::save(session), "isolated session save");
  Fixture fixture;
  auto personalSong = fixture.source;
  personalSong.id = personalSong.projectId = "personal-song";
  const auto personalAudio = fixture.directory.getChildFile("personal-guitar.flac");
  expect(juce::File(personalSong.stems.front().storedPath).copyFileTo(personalAudio),
         "personal fixture owns independent cached audio");
  personalSong.stems.front().storedPath = personalSong.stems.front().sourcePath = personalAudio.getFullPathName();
  expect(SongLibraryStore::registerCloudCachedSong(personalSong), "personal song fixture");
  const auto personalSetlist = SongLibraryStore::createSetlist("Personal");
  expect(SongLibraryStore::addSongToSetlist(personalSetlist, personalSong.id) &&
             SongLibraryStore::setActiveSetlistId(personalSetlist), "active personal fixture");

  const auto libraryBeforeDownload = SongLibraryStore::loadLibrarySnapshot();
  TeamSongLink origin{"alice", "band", "remote-song", 1, {}};
  ImportedSongData first;
  juce::String error;
  expect(TeamsPreparationService::registerPreparedDownload(origin, fixture.downloaded, first, &error),
         "verified Team preparation registers");
  expect(first.id.startsWith("team-" + TeamsPreparationService::accountTag("alice") + "-") &&
             first.projectId == first.id, "song and project identities have account namespace");
  expect(SongLibraryStore::loadLibrarySnapshot().activeSetlistId == personalSetlist,
         "preparing a song does not activate another setlist");
  expect(TeamsLocalStore::findSongLink("alice", "band", "remote-song", 1, origin) &&
             origin.localSongId == first.id, "prepared immutable origin persisted");
  {
    TeamsPreparationService offline;
    bool completed = false;
    offline.prepareSong({}, "alice", "band", "remote-song", 1, {},
        [&](ImportedSongData cached, CloudTransferError failure) {
          completed = failure.code.isEmpty() && cached.id == first.id;
        });
    expect(completed, "a prepared revision remains available without network credentials");
  }
  const auto downloadedLibrary = SongLibraryStore::loadLibrarySnapshot();
  expect(downloadedLibrary.songs.size() == libraryBeforeDownload.songs.size() + 1 &&
             downloadedLibrary.setlists.size() == libraryBeforeDownload.setlists.size(),
         "a shared Cloud download adds only a song and never generates a setlist");
  expect(SongLibraryStore::addSongToSetlist(personalSetlist, first.id),
         "downloaded song can be added to an ordinary personal setlist");
  expect(SongLibraryStore::updateStemVolumeForActiveSetlist(first.id, "guitar", 0.12f) &&
             SongLibraryStore::updateTempoBpmForActiveSetlist(first.id, 112) &&
             SongLibraryStore::updatePitchSemitonesForActiveSetlist(first.id, 5),
         "downloaded song supports normal local edits");
  ImportedSongData repeated;
  expect(TeamsPreparationService::registerPreparedDownload(origin, fixture.downloaded, repeated, &error) &&
             repeated.id == first.id && SongLibraryStore::loadSongs().size() == 2,
         "repeating a shared Cloud download is idempotent");
  SongMixSnapshot localMix;
  double tempo = 0; int pitch = 0;
  expect(SongLibraryStore::getActiveSetlistMix(first.id, localMix) &&
             near(localMix.stems.front().volume, 0.12) &&
             SongLibraryStore::getActiveSetlistTempoBpm(first.id, tempo) && near(tempo, 112) &&
             SongLibraryStore::getActiveSetlistPitchSemitones(first.id, pitch) && pitch == 5,
         "repeat download preserves personal mix, tempo and pitch edits");

  auto excluded = first;
  excluded.stems.front().include = false;
  auto editedMetadata = first;
  editedMetadata.title = "My local song title";
  expect(SongLibraryStore::updateSongMetadata(editedMetadata), "local metadata edit fixture");
  expect(juce::File(first.stems.front().storedPath).deleteFile(), "lost cached media fixture");
  expect(!TeamsPreparationService::isPreparedSongUsable(first) &&
             !TeamsPreparationService::isPreparedSongUsable(excluded),
         "missing audio is unavailable even when a stem is excluded");
  expect(juce::File(first.stems.front().storedPath).replaceWithData(nullptr, 0) &&
             !TeamsPreparationService::isPreparedSongUsable(first),
         "zero-length cached audio is unavailable");
  const auto restoredAudio = fixture.directory.getChildFile("restored-guitar.flac");
  writeAudio(restoredAudio);
  fixture.downloaded.objects.back().file = restoredAudio;
  ImportedSongData recovered;
  expect(TeamsPreparationService::registerPreparedDownload(origin, fixture.downloaded, recovered, &error) &&
             recovered.id == first.id && recovered.title == editedMetadata.title &&
             TeamsPreparationService::isPreparedSongUsable(recovered),
         "audio recovery restores the original local revision identity");
  expect(SongLibraryStore::getActiveSetlistMix(first.id, localMix) && near(localMix.stems.front().volume, 0.12),
         "audio recovery retains personal setlist mix");

  auto newer = fixture.downloaded;
  newer.descriptor.revision = 2;
  ImportedSongData second;
  expect(TeamsPreparationService::registerPreparedDownload({"alice", "band", "remote-song", 2, {}}, newer, second, &error) &&
             second.id != first.id && SongLibraryStore::loadLibrarySnapshot().activeSetlistId == personalSetlist,
         "new revision gets independent audio identity without switching playback selection");
  TeamSongLink oldLink;
  expect(TeamsLocalStore::findSongLink("alice", "band", "remote-song", 1, oldLink) &&
             oldLink.localSongId == first.id, "new revision retains old setlist pin");
  ImportedSongData personalCopy = recovered;
  personalCopy.id = "personal-owned";
  personalCopy.projectId = "personal-owned";
  expect(SongLibraryStore::registerCloudCachedSong(personalCopy), "personal song sharing a cached blob");
  const auto partial = SongLibraryStore::getRootDirectory().getChildFile("cloud-content/teams")
      .getChildFile(TeamsPreparationService::accountTag("alice"))
      .getChildFile(juce::SHA256(juce::String("band").toUTF8()).toHexString()).getChildFile("partial-download");
  partial.getParentDirectory().createDirectory();
  expect(partial.replaceWithText("partial"), "interrupted transfer fixture");
  expect(TeamsPreparationService::removeTeamDownloads("alice", "band"), "confirmed removal cleans all revisions");
  expect(!partial.exists(), "interrupted source transfer cleaned");
  expect(!fixture.directory.getChildFile("manifest.json").exists(), "unreferenced downloaded manifest deleted");
  expect(TeamsLocalStore::loadSongLinks("alice", "band").empty(), "source links cleared");
  const auto cleaned = SongLibraryStore::loadLibrarySnapshot();
  expect(std::none_of(cleaned.songs.begin(), cleaned.songs.end(), [&](const auto &s) {
    return s.id == first.id || s.id == second.id;
  }), "all team revisions removed");
  for (const auto &list : cleaned.setlists)
    for (const auto &item : list.items)
      expect(item.songId != first.id && item.songId != second.id, "setlist references removed");
  expect(TeamsPreparationService::isPreparedSongUsable(personalCopy), "personal shared audio preserved");
  expect(TeamsPreparationService::removeTeamDownloads("alice", "band"), "cleanup can retry safely");
  ImportedSongData rejected;
  expect(!TeamsPreparationService::registerPreparedDownload({"alice", "band", "remote-song", 3, {}}, newer, rejected, &error),
         "descriptor revision mismatch cannot register material");
}

void accountReadViewDoesNotDeleteStorage() {
  LibraryState raw;
  ImportedSongData personal; personal.id = "personal";
  ImportedSongData alice; alice.id = "team-" + TeamsPreparationService::accountTag("alice") + "-song";
  ImportedSongData bob; bob.id = "team-" + TeamsPreparationService::accountTag("bob") + "-song";
  ImportedSongData unknown; unknown.id = "team-legacy-unknown";
  raw.songs = {personal, alice, bob, unknown};
  SetlistData personalList; personalList.id = "personal-list";
  personalList.items = {SetlistItem{}, SetlistItem{}, SetlistItem{}};
  personalList.items[0].songId = personal.id;
  personalList.items[1].songId = alice.id;
  personalList.items[2].songId = bob.id;
  SetlistData aliceList; aliceList.id = "team-list-" + TeamsPreparationService::accountTag("alice") + "-list";
  aliceList.items = {personalList.items[1]};
  SetlistData bobList; bobList.id = "team-list-" + TeamsPreparationService::accountTag("bob") + "-list";
  bobList.items = {personalList.items[2]};
  raw.setlists = {personalList, aliceList, bobList}; raw.activeSetlistId = bobList.id;
  const auto visible = TeamsPreparationService::filterLibraryForAccount(raw, "alice");
  expect(visible.songs.size() == 2 && visible.setlists.size() == 2 &&
             visible.setlists.front().items.size() == 2 && visible.activeSetlistId == personalList.id,
         "read view hides foreign and unknown Teams material");
  expect(raw.songs.size() == 4 && raw.setlists.size() == 3 && raw.setlists.front().items.size() == 3,
         "read filtering never mutates the raw persistent snapshot");
  const auto signedOut = TeamsPreparationService::filterLibraryForAccount(raw, {});
  expect(signedOut.songs.size() == 1 && signedOut.setlists.size() == 1 &&
             signedOut.setlists.front().items.size() == 1,
         "signed-out view contains personal content only");
}

} // namespace

int main() {
  try {
    localPreparationFlow();
    accountReadViewDoesNotDeleteStorage();
    std::cout << "Teams shared Cloud downloads, local edits and account-view checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n'; return 1;
  }
}
