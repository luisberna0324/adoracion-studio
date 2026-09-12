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

class CloudLibraryClient final {
public:
  using SearchCallback = std::function<void(CloudSongPage, CloudRequestError)>;

  CloudLibraryClient();
  ~CloudLibraryClient();
  void search(const juce::String& apiBaseUrl, const juce::String& firebaseApiKey,
              const juce::String& refreshToken, const juce::String& query,
              const juce::String& cursor, int limit, SearchCallback callback);
  void cancelAll();

private:
  juce::ThreadPool pool{2};
};
