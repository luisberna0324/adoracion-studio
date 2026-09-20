#include "data/CloudSettingsSyncState.h"
#include <cstdlib>
#include <iostream>

static void expect(bool value, const char *message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

int main() {
  // Canonical snapshots supplied by the ProjectStore serializer in production.
  const juce::String initial = R"({"master":{"volume":1.0,"mute":false}})";
  const juce::String edited = R"({"master":{"volume":0.5,"mute":false}})";
  const juce::String unpublished = R"({"master":{"volume":0.25,"mute":true}})";
  CloudProjectLink link;
  expect(!CloudSettingsSyncState::hasPending(link, initial), "unknown state stays hidden");
  link.settingsStateKnown = true;
  expect(CloudSettingsSyncState::hasPending(link, initial), "first settings sync is available");
  expect(!CloudSettingsSyncState::hasPending(link, {}), "failed capture is not a change");

  link.settingsRevision = 1;
  link.settingsJson = initial;
  expect(!CloudSettingsSyncState::hasPending(link, initial), "legacy latest baseline is clean");
  expect(CloudSettingsSyncState::hasPending(link, edited), "fader edit is pending");
  expect(!CloudSettingsSyncState::hasPending(link, initial), "undo fader edit clears pending");
  CloudSettingsSyncState::remember(link, initial);
  CloudSettingsSyncState::remember(link, edited);
  link.settingsRevision = 2;
  link.settingsJson = edited;
  expect(!CloudSettingsSyncState::hasPending(link, edited), "published edit is clean");
  expect(!CloudSettingsSyncState::hasPending(link, initial), "selecting v1 after v2 is clean");
  expect(CloudSettingsSyncState::hasPending(link, unpublished), "new edit to v1 is pending");
  expect(!CloudSettingsSyncState::hasPending(link, initial), "undo new edit to v1 clears pending");
  expect(!CloudSettingsSyncState::hasPending(link, edited), "switch back to v2 is clean");

  // A remote catalog refresh must not forget previously recognized versions.
  CloudSettingsSyncState::remember(link, unpublished);
  link.settingsRevision = 3;
  link.settingsJson = unpublished;
  expect(!CloudSettingsSyncState::hasPending(link, initial), "remote v3 does not dirty local v1");
  CloudSettingsSyncState::remember(link, initial);
  expect(link.knownSettingsFingerprints.size() == 3, "repeated histories do not duplicate hashes");
  expect(CloudSettingsSyncState::fingerprint(initial).length() == 64, "compact SHA256 fingerprints");

  // Exercise the same array encoding used by CloudSyncIndexStore for restarts.
  const auto encoded = juce::JSON::toString(juce::var(link.knownSettingsFingerprints));
  auto restored = link;
  restored.knownSettingsFingerprints.clear();
  const auto decoded = juce::JSON::parse(encoded);
  for (const auto &value : *decoded.getArray())
    restored.knownSettingsFingerprints.add(value.toString());
  expect(!CloudSettingsSyncState::hasPending(restored, initial), "v1 remains clean after persistence");
  expect(CloudSettingsSyncState::hasPending(restored, "{\"different\":true}"), "unknown settings remain pending");
  std::cout << "Cloud settings sync state: all regression checks passed\n";
}
