#include "data/ApplicationDataPaths.h"
#include "data/CloudSettingsSyncState.h"
#include "data/CloudSyncIndexStore.h"
#include "data/SongLibraryStore.h"

#include <cstdlib>
#include <iostream>

namespace {
void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}
} // namespace

int main() {
  const auto isolatedRoot = ApplicationDataPaths::isolatedTestRoot();
  const auto libraryRoot = SongLibraryStore::getRootDirectory();
  expect(libraryRoot.isAChildOf(isolatedRoot),
         "cloud index storage stays inside the temporary root");
  expect(CloudSyncIndexStore::loadAll().empty(),
         "fresh cloud index has no links");

  const juce::String v1 = R"({"master":{"volume":1.0}})";
  const juce::String v2 = R"({"master":{"volume":0.5}})";
  CloudProjectLink link;
  link.projectId = "isolated-project";
  link.remoteSongId = "remote-song";
  link.settingsStateKnown = true;
  link.settingsRevision = 1;
  link.settingsJson = v1;
  CloudSettingsSyncState::remember(link, v1);
  expect(CloudSyncIndexStore::upsert(link), "first cloud link is persisted");

  CloudProjectLink reopened;
  expect(CloudSyncIndexStore::findByRemoteSongId("remote-song", reopened) &&
             reopened.projectId == link.projectId &&
             !CloudSettingsSyncState::hasPending(reopened, v1),
         "published settings stay clean after loading the index");
  expect(CloudSettingsSyncState::hasPending(reopened, v2),
         "a local fader edit remains unpublished after restart");

  CloudSettingsSyncState::remember(reopened, v2);
  reopened.settingsRevision = 2;
  reopened.settingsJson = v2;
  expect(CloudSyncIndexStore::upsert(reopened),
         "publishing a second settings version updates the link");
  const auto all = CloudSyncIndexStore::loadAll();
  expect(all.size() == 1 && all[0].settingsRevision == 2 &&
             all[0].knownSettingsFingerprints.size() == 2,
         "index reload retains one link and both version fingerprints");
  expect(!CloudSettingsSyncState::hasPending(all[0], v1) &&
             !CloudSettingsSyncState::hasPending(all[0], v2),
         "selecting any published version stays clean after restart");
  expect(CloudSyncIndexStore::removeByProjectId(link.projectId) &&
             CloudSyncIndexStore::loadAll().empty(),
         "removing a cloud link persists an empty index");
  std::cout << "Cloud sync index regression checks passed\n";
}
