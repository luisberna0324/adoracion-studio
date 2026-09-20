#pragma once

#include "CloudSyncIndexStore.h"
#include <juce_cryptography/juce_cryptography.h>

// Inputs must use ProjectStore::serializeSettings + JSON::toString. These
// fingerprints describe settings only, not server metadata or stem labels.
namespace CloudSettingsSyncState {
inline juce::String fingerprint(const juce::String &canonicalSettingsJson) {
  return juce::SHA256(canonicalSettingsJson.toRawUTF8(),
                      canonicalSettingsJson.getNumBytesAsUTF8()).toHexString();
}

inline void remember(CloudProjectLink &link,
                     const juce::String &canonicalSettingsJson) {
  if (canonicalSettingsJson.isNotEmpty())
    link.knownSettingsFingerprints.addIfNotAlreadyThere(
        fingerprint(canonicalSettingsJson));
}

inline bool hasPending(const CloudProjectLink &link,
                       const juce::String &canonicalSettingsJson) {
  if (!link.settingsStateKnown || canonicalSettingsJson.isEmpty())
    return false;
  if (link.settingsRevision <= 0)
    return true; // First settings publication is still available.
  if (link.knownSettingsFingerprints.contains(fingerprint(canonicalSettingsJson)))
    return false;
  // Support indexes written before known-version fingerprints were added.
  return link.settingsJson.isNotEmpty() &&
         canonicalSettingsJson != link.settingsJson;
}
} // namespace CloudSettingsSyncState
