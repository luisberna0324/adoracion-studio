#pragma once

#include "CloudSyncTypes.h"

#include <functional>

struct CloudTransferCredentials {
  juce::String apiBaseUrl;
  juce::String firebaseApiKey;
  juce::String refreshToken;
};

struct CloudTransferError {
  int httpCode = 0;
  juce::String code;
  juce::String message;
};

struct CloudTransferProgress {
  juce::String phase;
  juce::String objectSha256;
  int completedObjects = 0;
  int totalObjects = 0;
  double fraction = 0.0;
};

/** A local blob and the immutable reference that will be placed in the descriptor. */
struct CloudUploadObject {
  CloudObjectRef reference;
  juce::File file;
};

struct CloudUploadRequest {
  CloudProjectDescriptor descriptor;
  int baseRevision = 0;
  std::vector<CloudUploadObject> objects;
};

struct CloudUploadResult {
  juce::String songId;
  int revision = 0;
};

struct CloudDownloadedObject {
  CloudObjectRef reference;
  juce::File file;
};

struct CloudDownloadResult {
  CloudProjectDescriptor descriptor;
  std::vector<CloudDownloadedObject> objects;
};

/**
 * Network boundary for cloud blobs. All blobs travel directly between the app
 * and signed R2 URLs; API calls only create or commit a revision manifest.
 */
class CloudTransferClient final {
public:
  using ProgressCallback = std::function<void(CloudTransferProgress)>;
  using UploadCallback = std::function<void(CloudUploadResult, CloudTransferError)>;
  using DownloadCallback = std::function<void(CloudDownloadResult, CloudTransferError)>;
  using DeleteCallback = std::function<void(CloudTransferError)>;

  CloudTransferClient();
  ~CloudTransferClient();

  void upload(CloudTransferCredentials credentials, CloudUploadRequest request,
              ProgressCallback progress, UploadCallback callback);
  void download(CloudTransferCredentials credentials, juce::String songId,
                juce::File destinationDirectory, ProgressCallback progress,
                DownloadCallback callback);
  /** Deletes one remote song owned by the authenticated account. */
  void deleteSong(CloudTransferCredentials credentials, juce::String songId,
                  DeleteCallback callback);
  void cancelAll();

private:
  juce::ThreadPool pool{2};
};
