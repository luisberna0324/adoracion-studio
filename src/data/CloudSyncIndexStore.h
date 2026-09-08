#pragma once

#include <JuceHeader.h>

#include <vector>

struct CloudProjectLink {
  juce::String projectId;
  juce::String remoteSongId;
  int remoteRevision = 0;
  juce::int64 downloadedAtMs = 0;
  juce::int64 lastSyncedAtMs = 0;
  juce::String descriptorFingerprint;
};

struct CloudLocalPurgeResult {
  int removedLibrarySongs = 0;
  bool clearedLocalLibrary = true;
};

/** Local-only link state. ProjectStore remains unaware of remote identities. */
class CloudSyncIndexStore final {
public:
  static std::vector<CloudProjectLink> loadAll();
  static bool findByProjectId(const juce::String& projectId, CloudProjectLink& result);
  static bool findByRemoteSongId(const juce::String& remoteSongId, CloudProjectLink& result);
  static bool upsert(CloudProjectLink link);
  static bool removeByProjectId(const juce::String& projectId);
  static bool removeByRemoteSongId(const juce::String& remoteSongId);
  /** Resets local library and caches without deleting any .pwproj package. */
  static CloudLocalPurgeResult purgeLocalState();

private:
  static juce::File getFile();
  static bool saveAll(const std::vector<CloudProjectLink>& links);
};
