#pragma once

#include "../data/SongLibraryStore.h"
#include "DriveApiClient.h"
#include "DriveAuthSession.h"

struct CloudSyncResult {
  bool ok = false;
  juce::String message;
  int songsProcessed = 0;
  int stemsProcessed = 0;
};

class CloudLibrarySyncService {
public:
  static CloudSyncResult pushToDrive();
  /** Uploads stems for one song and merges it into the remote snapshot JSON. */
  static CloudSyncResult pushSongToDrive(const juce::String &songId);
  static CloudSyncResult pullFromDrive();
  /** Downloads one song's stems from Drive and merges metadata into the local library.
   *  @param progressCallback Called on the background thread with a value in [0,1]
   *                          after each stem is downloaded. May be nullptr. */
  static CloudSyncResult pullSongFromDrive(
      const juce::String &songId,
      std::function<void(float)> progressCallback = nullptr);
  static bool hasValidDriveSession();
  /** Downloads only `library_snapshot_v1.json` into memory (does not modify local DB). */
  static bool fetchRemoteLibraryPreview(LibraryState &outLibrary,
                                        juce::String &errorMessage);

private:
  static juce::String guessMimeTypeForFile(const juce::File &file);
  static juce::String stemExtensionFor(const ImportedStemData &stem);
  static juce::String fileSha256(const juce::File &file);
  static juce::int64 parseDriveTimeMs(const juce::String &iso8601);
};
