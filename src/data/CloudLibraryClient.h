#pragma once

#include "CloudSyncTypes.h"

#include <functional>

struct CloudSongSummary {
  juce::String songId, title, artist, key;
  juce::String artworkPath;
  int bpm = 0, stemCount = 0, revision = 0;
  double durationSeconds = 0.0;
  juce::int64 storageBytes = 0, updatedAtMs = 0;
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
