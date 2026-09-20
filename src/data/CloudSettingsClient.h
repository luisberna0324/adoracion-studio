#pragma once

#include "CloudTransferClient.h"
#include "ProjectStore.h"

struct CloudSettingsChangeSummary {
  int mixChanges = 0;
  int muteSoloChanges = 0;
  int routingChanges = 0;
  bool playbackChanged = false;
  bool guideClickChanged = false;
  bool arrangementChanged = false;
  juce::StringArray details;
};

struct CloudSettingsVersion {
  juce::String versionId;
  juce::String songId;
  juce::String projectId;
  int revision = 0;
  juce::String snapshotHash;
  ProjectSongSettings settings;
  CloudSettingsChangeSummary summary;
  juce::String source;
  juce::int64 createdAtMs = 0;
};

class CloudSettingsClient final {
public:
  using ProgressCallback = CloudTransferClient::ProgressCallback;
  using PublishCallback = std::function<void(bool created,
                                             CloudSettingsVersion version,
                                             CloudTransferError error)>;
  using ListCallback = std::function<void(std::vector<CloudSettingsVersion>,
                                          CloudTransferError error)>;

  CloudSettingsClient();
  ~CloudSettingsClient();

  void publish(CloudTransferCredentials credentials, juce::String remoteSongId,
               juce::String projectId, ImportedSongData song,
               ProjectSongSettings settings,
               ProgressCallback progress,
               PublishCallback callback);
  void list(CloudTransferCredentials credentials, juce::String remoteSongId,
            ListCallback callback);
  void cancelAll();

private:
  juce::ThreadPool pool{2};
};
