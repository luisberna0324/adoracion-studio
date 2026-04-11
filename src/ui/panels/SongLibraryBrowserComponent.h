#pragma once

#include "../../cloud/SongDownloadManager.h"
#include "../../data/SongData.h"
#include "../../data/SongLibraryStore.h"
#include "../UiCommon.h"

#include <functional>
#include <map>
#include <vector>

#include <JuceHeader.h>

/** Modal: filter + unified library table (local + cloud-only) + import wizard. */
class SongLibraryBrowserComponent final : public juce::Component {
public:
  using SubmitImportedSongFn =
      std::function<bool(ImportedSongData song, bool syncWithDrive,
                         juce::String &errorMessage)>;

  /** If `setlistIdForPick` is non-empty, songs already in that setlist are hidden
      and double-click adds the song to the setlist via `onLocalSongChosen`. */
  SongLibraryBrowserComponent(
      SubmitImportedSongFn submitImportedSong,
      juce::String setlistIdForPick, juce::String highlightLoadedSongId,
      std::function<void(const juce::String &songId)> onLocalSongChosen,
      bool primeImportEnabled,
      std::function<void()> onLibraryStorageChanged = {});

  void refreshLocalList();
  void paint(juce::Graphics &g) override;
  void resized() override;
  ~SongLibraryBrowserComponent() override;

private:
  class UnifiedTableModel;
  friend class UnifiedTableModel;

  struct UnifiedRow {
    juce::String songId;
    bool cloudOnly = false;
  };

  struct LibraryTable final : juce::TableListBox {
    SongLibraryBrowserComponent *browser = nullptr;
    void mouseDoubleClick(const juce::MouseEvent &e) override;
  };

  void rebuildFilteredLists();
  void handleFilterChanged();
  void launchImportWizard();
  void startRemotePreviewFetch();
  void refetchRemotePreview();

  void closeParentDialog();
  void notifyStorageChanged();

  const ImportedSongData *findLocalSong(const juce::String &songId) const;
  const ImportedSongData *findRemoteSong(const juce::String &songId) const;
  bool isInCloudColumn(const juce::String &songId, bool cloudOnlyRow) const;
  bool rowHasConflict(const juce::String &songId) const;
  juce::String busyTextFor(const juce::String &songId) const;
  void setBusy(const juce::String &songId, juce::String text);
  void clearBusy(const juce::String &songId);

  void handleLoadRow(int row);
  void showRowActionsMenu(int row, juce::Point<int> anchor);
  void confirmPullThenRun(const juce::String &songId);
  void confirmDelete(const juce::String &songId);
  void runPullAsync(const juce::String &songId);
  void runPushAsync(const juce::String &songId);
  void runDeleteAsync(const juce::String &songId);
  /** Adds the cloud-only song to the active setlist immediately and
   *  kicks off a background download, then closes the browser. */
  void addCloudSongToSetlistAndDownload(const juce::String &songId);

  juce::String titleForSongId(const juce::String &songId) const;

  SubmitImportedSongFn submitImportedSong;
  const juce::String setlistIdForPick;
  const juce::String highlightLoadedSongId;
  std::function<void(const juce::String &songId)> onLocalSongChosen;
  const bool primeImportEnabled;
  std::function<void()> onLibraryStorageChanged;

  juce::TextEditor filterEditor;
  juce::TextButton addSongButton;

  std::unique_ptr<UnifiedTableModel> tableModel;
  LibraryTable table;

  LibraryState localLibrary;
  LibraryState remoteLibrary;
  std::vector<UnifiedRow> unifiedRows;
  std::map<juce::String, juce::String> busyBySongId;

  bool drivePreviewLoading = true;
  bool driveFetchOk = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongLibraryBrowserComponent)
};
