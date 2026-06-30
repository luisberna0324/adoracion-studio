#pragma once

#include <JuceHeader.h>

namespace PlayWorshipUpdate {

struct UpdatePackage {
  juce::String type;
  juce::String url;
  juce::String sha256;
  juce::int64 sizeBytes = 0;
  juce::String signatureType;
  juce::String publisher;
};

struct UpdateManifest {
  bool updateAvailable = false;
  bool mandatory = false;
  juce::String currentVersion;
  juce::String latestVersion;
  juce::String channel;
  juce::String releaseTitle;
  juce::String releaseNotesUrl;
  juce::StringArray releaseItems;
  UpdatePackage package;
  UpdatePackage delta;
  bool hasDelta = false;
  juce::String rawJson;
};

struct ManifestParseResult {
  UpdateManifest manifest;
  juce::String error;
  bool ok = false;
};

ManifestParseResult parseUpdateManifest(const juce::String &json);
juce::String getPlatformId();
juce::String getArchitectureId();
int compareSemver(const juce::String &a, const juce::String &b);

} // namespace PlayWorshipUpdate
