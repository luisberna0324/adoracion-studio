#pragma once

#include "CloudTransferClient.h"
#include "SongData.h"

/** Converts portable .pwproj archives to cloud blobs and back without network knowledge. */
class CloudProjectArchive final {
public:
  using BuildProgressCallback = std::function<void(double fraction, const juce::String& phase)>;

  static bool buildUpload(const juce::File& projectFile, const juce::String& remoteSongId,
                          int baseRevision, CloudUploadRequest& outRequest,
                          juce::String* errorMessage = nullptr,
                          BuildProgressCallback progress = {});
  static bool rebuildProject(const CloudDownloadResult& downloaded,
                             const juce::File& destination, ImportedSongData& outSong,
                             juce::String* errorMessage = nullptr);
};
