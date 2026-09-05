#pragma once

#include "CloudProjectArchive.h"
#include "CloudSyncIndexStore.h"

/** Coordinates local archive conversion, direct transfer and local cloud links. */
class SongSyncService final {
public:
  using ProgressCallback = CloudTransferClient::ProgressCallback;
  using CompletionCallback = std::function<void(ImportedSongData, CloudTransferError)>;

  SongSyncService();
  ~SongSyncService();

  void upload(CloudTransferCredentials credentials, juce::File projectFile,
              ProgressCallback progress, CompletionCallback callback);
  void download(CloudTransferCredentials credentials, juce::String remoteSongId,
                juce::File destination, ProgressCallback progress,
                CompletionCallback callback);
  void cancelAll();

private:
  juce::ThreadPool archivePool{1};
  CloudTransferClient transfer;
};
