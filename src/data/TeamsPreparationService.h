#pragma once
#include "CloudProjectArchive.h"
#include "TeamsLocalStore.h"
#include "ProjectStore.h"

/** Downloads a shared Cloud song into the local library; never changes the active transport. */
class TeamsPreparationService final {
public:
  using Completion = std::function<void(ImportedSongData, CloudTransferError)>;
  void prepareSong(CloudTransferCredentials, juce::String uid, juce::String teamId,
                   juce::String songId, int revision,
                   CloudTransferClient::ProgressCallback, Completion);
  static juce::String accountTag(const juce::String &uid);
  /** Filters a read view; callers must retain the unfiltered storage snapshot. */
  static LibraryState filterLibraryForAccount(LibraryState,
                                              const juce::String &uid);
  /** All downloaded stems must be present, including currently excluded ones. */
  static bool isPreparedSongUsable(const ImportedSongData &);
  /** Verified-download boundary, also used by preparation regression tests. */
  static bool registerPreparedDownload(TeamSongLink origin,
                                       const CloudDownloadResult &,
                                       ImportedSongData &,
                                       juce::String *errorMessage = nullptr);
  /** Call only after stopping/unloading the affected song and cancelling downloads. */
  static bool removeTeamDownloads(const juce::String &uid, const juce::String &teamId);
  ~TeamsPreparationService();
private:
  CloudTransferClient transfer;
  juce::ThreadPool pool{1};
  std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
};
