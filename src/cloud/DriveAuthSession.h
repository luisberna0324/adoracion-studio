#pragma once

#include <JuceHeader.h>

struct DriveAuthCredentials {
  juce::String accessToken;
  juce::String tokenType;
  juce::String grantedScopes;
  juce::String accountEmail;
  juce::int64 accessTokenExpiresAtMs = 0;
  juce::int64 updatedMs = 0;

  juce::String remoteSnapshotFileId;
  juce::int64 remoteSnapshotModifiedMs = 0;
  juce::int64 lastSyncMs = 0;
};

class DriveAuthSession {
public:
  static DriveAuthCredentials load();
  static bool save(const DriveAuthCredentials &credentials);
  static void clear();
  static bool hasValidAccessToken(
      const DriveAuthCredentials &credentials,
      juce::int64 nowMs = juce::Time::currentTimeMillis());
};
