#pragma once

#include "CloudSyncTypes.h"

#include <functional>

struct CloudSongSummary {
  juce::String songId, title, artist, key;
  juce::String artworkPath;
  juce::String settingsHash;
  int bpm = 0, stemCount = 0, revision = 0;
  int settingsRevision = 0;
  double durationSeconds = 0.0;
  juce::int64 storageBytes = 0, updatedAtMs = 0;
  // Shared songs retain their remote identity; catalog identity includes origin.
  juce::String teamId, ownerUid, ownerEmail;
  bool isSharedCloud() const { return teamId.isNotEmpty(); }
  juce::String catalogId() const {
    return isSharedCloud() ? "shared-cloud:" + teamId + ":" + songId : songId;
  }
};

struct CloudSongPage {
  std::vector<CloudSongSummary> songs;
  juce::String nextCursor;
};

struct CloudRequestError {
  int httpCode = 0;
  juce::String code, message;
};

struct CloudCapabilities {
  juce::String accountType, trialEndsAt, updatedAt;
  bool enabled = false, isOverride = false;
  bool canManageSharedCloud = false;
  int usedDevices = 0, maxDevices = 0, syncedSongCount = 0, maxSyncedSongs = 0;
  juce::int64 usedStorageBytes = 0, maxStorageBytes = 0;
};

class CloudLibraryClient final {
public:
  using SearchCallback = std::function<void(CloudSongPage, CloudRequestError)>;
  using CapabilitiesCallback = std::function<void(CloudCapabilities, CloudRequestError)>;

  CloudLibraryClient();
  ~CloudLibraryClient();
  void search(const juce::String& apiBaseUrl, const juce::String& firebaseApiKey,
              const juce::String& refreshToken, const juce::String& query,
              const juce::String& cursor, int limit, SearchCallback callback);
  void fetchCapabilities(const juce::String& apiBaseUrl, const juce::String& firebaseApiKey,
                         const juce::String& refreshToken, CapabilitiesCallback callback);
  void cancelAll();

private:
  juce::ThreadPool pool{2};
};
