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
  /** Deletes the exact temporary directory created for one completed upload. */
  static void cleanupUploadStaging(const juce::File& stagingDirectory);
  /** Removes upload staging abandoned by an earlier app process. */
  static void cleanupStaleUploadStaging();
  static bool rebuildProject(const CloudDownloadResult& downloaded,
                             const juce::File& destination, ImportedSongData& outSong,
                             juce::String* errorMessage = nullptr);
  /** Uses verified cloud blobs directly, without creating a duplicate .pwproj. */
  static bool materializeCachedSong(const CloudDownloadResult& downloaded,
                                    ImportedSongData& outSong,
                                    std::vector<float>& outWaveformBins,
                                    juce::String* errorMessage = nullptr);
};
