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
  static CloudSyncResult pullFromDrive();
  static bool hasValidDriveSession();

private:
  static juce::String guessMimeTypeForFile(const juce::File &file);
  static juce::String stemExtensionFor(const ImportedStemData &stem);
  static juce::String fileSha256(const juce::File &file);
  static juce::int64 parseDriveTimeMs(const juce::String &iso8601);
};
