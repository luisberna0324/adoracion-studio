#include "data/TeamsLocalStore.h"
#include "IsolatedTestRoot.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

juce::var json(const char *value) { return juce::JSON::parse(value); }

void accountScopedCaches() {
  const auto root = isolatedTestRoot();
  const auto alice = TeamsLocalStore::getAccountDirectory("alice");
  const auto bob = TeamsLocalStore::getAccountDirectory("bob");
  expect(alice.isAChildOf(root) && bob.isAChildOf(root) && alice != bob,
         "account cache roots are distinct and isolated");
  expect(TeamsLocalStore::loadOverview("alice").isVoid(), "fresh cache is empty");
  expect(TeamsLocalStore::saveOverview("alice", json(R"({"teams":[{"teamId":"band-a","name":"Equipo A"}]})")),
         "overview cache saves");
  expect(TeamsLocalStore::saveOverview("bob", json(R"({"teams":[{"teamId":"band-b","name":"Equipo B"}]})")),
         "second account cache saves");
  const auto aliceOverview = juce::JSON::toString(TeamsLocalStore::loadOverview("alice"));
  const auto bobOverview = juce::JSON::toString(TeamsLocalStore::loadOverview("bob"));
  expect(aliceOverview.contains("band-a") && !aliceOverview.contains("band-b") &&
             bobOverview.contains("band-b") && !bobOverview.contains("band-a"),
         "changing account never reads another account overview");

  const auto details = json(R"({"team":{"name":"Equipo A"},"songs":[{"songId":"song-a","revision":2,"title":"Canción"}]})");
  expect(TeamsLocalStore::saveDetails("alice", "band-a", details), "details save");
  expect(juce::JSON::toString(TeamsLocalStore::loadDetails("alice", "band-a")) ==
             juce::JSON::toString(details),
         "team metadata survives persistence");
  expect(TeamsLocalStore::loadDetails("bob", "band-a").isVoid() &&
             TeamsLocalStore::loadDetails("alice", "band-b").isVoid(),
         "team detail cache is scoped by account and team");

  const auto hostileId = TeamsLocalStore::getAccountDirectory("../../outside");
  expect(hostileId.isAChildOf(root) && hostileId.getParentDirectory() == alice.getParentDirectory(),
         "server identifiers are hashed rather than interpreted as paths");
  expect(!TeamsLocalStore::saveOverview({}, details) &&
             !TeamsLocalStore::saveDetails("alice", {}, details) &&
             !TeamsLocalStore::saveOverview("alice", json("[]")),
         "invalid cache scope and payload are rejected");
}

void credentialsStayOutOfCaches() {
  expect(TeamsLocalStore::saveOverview("safe", json(R"({"teams":[{"teamId":"band","name":"Band","artworkUrl":"secret-signed-url","nested":{"refreshToken":"secret-refresh","idToken":"secret-id","token":"secret-invite","authorization":"secret-header","downloadUrl":"secret-download"}}]})")),
         "metadata containing transient fields is sanitized");
  const auto stored = TeamsLocalStore::getAccountDirectory("safe")
                          .getChildFile("overview.json").loadFileAsString();
  expect(stored.contains("Band") && !stored.contains("secret-"),
         "credentials and signed URLs never enter persisted cache");
}

void downloadedRevisionsRemainImmutable() {
  TeamSongLink original{"alice", "band-a", "song-a", 1, "local-a-r1"};
  TeamSongLink updated{"alice", "band-a", "song-a", 2, "local-a-r2"};
  expect(TeamsLocalStore::upsertSongLink(original) &&
             TeamsLocalStore::upsertSongLink(original),
         "download registration is idempotent");
  auto changedOriginal = original;
  changedOriginal.localSongId = "replacement-a-r1";
  expect(!TeamsLocalStore::upsertSongLink(changedOriginal),
         "an existing revision cannot silently replace the playing local song");
  expect(TeamsLocalStore::upsertSongLink(updated), "a new revision gets its own local origin");

  TeamSongLink found;
  expect(TeamsLocalStore::findSongLink("alice", "band-a", "song-a", 1, found) &&
             found.localSongId == original.localSongId,
         "publishing an update preserves the old pinned revision");
  expect(TeamsLocalStore::findSongLink("alice", "band-a", "song-a", 2, found) &&
             found.localSongId == updated.localSongId,
         "latest revision resolves independently");
  expect(!TeamsLocalStore::findSongLink("bob", "band-a", "song-a", 2, found) &&
             !TeamsLocalStore::findSongLink("alice", "band-b", "song-a", 2, found),
         "a downloaded origin never resolves for another scope");

  auto alias = updated;
  alias.teamId = "band-b";
  expect(!TeamsLocalStore::upsertSongLink(alias),
         "different origins cannot share an editable local song ID");
  alias.localSongId = "local-band-b-r2";
  expect(TeamsLocalStore::upsertSongLink(alias) &&
             TeamsLocalStore::loadSongLinks("alice", "band-a").size() == 2 &&
             TeamsLocalStore::loadSongLinks("alice", "band-b").size() == 1,
         "each team retains its own revision list");
  alias.revision = 0;
  expect(!TeamsLocalStore::upsertSongLink(alias), "unpublished revision zero is rejected");
}

void concurrentRegistrationDoesNotLoseOrigins() {
  std::atomic<bool> allSaved{true};
  std::vector<std::thread> writers;
  for (int i = 0; i < 12; ++i)
    writers.emplace_back([i, &allSaved]() {
      const TeamSongLink link{"concurrent", "band", "song-" + juce::String(i),
                              1, "local-" + juce::String(i)};
      if (!TeamsLocalStore::upsertSongLink(link))
        allSaved.store(false);
    });
  for (auto &writer : writers)
    writer.join();
  expect(allSaved.load() && TeamsLocalStore::loadSongLinks("concurrent", "band").size() == 12,
         "atomic read-modify-write preserves concurrent origin registrations");
}

void malformedCacheFailsClosed() {
  const auto aliceRoot = TeamsLocalStore::getAccountDirectory("alice");
  const auto overviewFile = aliceRoot.getChildFile("overview.json");
  expect(overviewFile.replaceWithText("{broken"), "corrupt overview fixture writes");
  expect(TeamsLocalStore::loadOverview("alice").isVoid(), "malformed metadata has no visible cache");
  expect(overviewFile.replaceWithText(R"({"schemaVersion":1,"uid":"bob","teamId":"","payload":{"teams":[{"name":"Leaked"}]}})"),
         "foreign account envelope fixture writes");
  expect(TeamsLocalStore::loadOverview("alice").isVoid(), "foreign UID envelope is rejected");

  const auto linksFile = aliceRoot.getChildFile("song-links.json");
  const juce::String malformed("{broken-origins");
  expect(linksFile.replaceWithText(malformed), "corrupt origin fixture writes");
  expect(TeamsLocalStore::loadSongLinks("alice", "band-a").empty(),
         "malformed origins fail closed");
  expect(!TeamsLocalStore::upsertSongLink({"alice", "band-a", "new-song", 1, "new-local"}) &&
             linksFile.loadFileAsString() == malformed,
         "corrupt origin index is preserved instead of silently recreating mappings");
}
} // namespace

int main() {
  try {
    const auto onboarding = TeamsLocalStore::onboardingLessonId("alice", "cloud-a", "join-1");
    expect(onboarding.isNotEmpty() && onboarding == TeamsLocalStore::onboardingLessonId("alice", "cloud-a", "join-1"), "stable onboarding identity");
    expect(onboarding != TeamsLocalStore::onboardingLessonId("bob", "cloud-a", "join-1"), "onboarding is isolated by account");
    expect(onboarding != TeamsLocalStore::onboardingLessonId("alice", "cloud-b", "join-1"), "onboarding is isolated by cloud");
    expect(onboarding != TeamsLocalStore::onboardingLessonId("alice", "cloud-a", "join-2"), "rejoining creates a new welcome");
    expect(TeamsLocalStore::onboardingLessonId("", "cloud-a", "join-1").isEmpty(), "anonymous onboarding fails closed");
    accountScopedCaches();
    credentialsStayOutOfCaches();
    downloadedRevisionsRemainImmutable();
    concurrentRegistrationDoesNotLoseOrigins();
    malformedCacheFailsClosed();
    std::cout << "Teams local cache and immutable origin checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
