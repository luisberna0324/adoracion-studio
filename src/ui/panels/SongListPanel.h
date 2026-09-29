#pragma once

#include "../../data/SongData.h"
#include "../../data/SongLibraryStore.h"
#include "../../data/ProjectStore.h"
#include "../components/SearchField.h"
#include "../components/SetlistSongsHeaderPanel.h"
#include "LibraryCatalogPanel.h"
#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <array>
#include <unordered_map>
#include <vector>

class SongImportWizardComponent;
class SongWizardOverlay;
class AppModal;

class SongListPanel final : public juce::Component, private juce::Timer {
public:
  class SwipeHintHandle final : public juce::Component {
  public:
    enum class Direction { Left, Right };

    explicit SwipeHintHandle(Direction directionToUse);

    void setHintVisible(bool shouldShowHint);
    void setOnGestureBegin(std::function<void(const juce::MouseEvent &)> cb);
    void setOnGestureDrag(std::function<void(const juce::MouseEvent &)> cb);
    void setOnGestureEnd(std::function<void(const juce::MouseEvent &)> cb);

    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &event) override;
    void mouseDrag(const juce::MouseEvent &event) override;
    void mouseUp(const juce::MouseEvent &event) override;

  private:
    Direction direction;
    bool hintVisible = false;
    std::function<void(const juce::MouseEvent &)> onGestureBegin;
    std::function<void(const juce::MouseEvent &)> onGestureDrag;
    std::function<void(const juce::MouseEvent &)> onGestureEnd;
  };

  void setRemoteLibrary(std::shared_ptr<const LibraryState> library);
  std::function<void(const juce::String &, const juce::var &, const juce::String &)> onRemoteAction;
  SongListPanel();
  ~SongListPanel() override;

  void setOnSongActivated(
      std::function<void(const ImportedSongData &, int)> cb);
  void setOnSongEditStarted(std::function<void(const juce::String &)> cb);
  void setOnSongEdited(
      std::function<void(const ImportedSongData &, int)> cb);
  void setOnSetlistNameChanged(std::function<void(const juce::String &)> cb);
  void setOnSaveProjectRequested(std::function<void()> cb);
  void setOnPadsRequested(std::function<void()> cb);
  void setOnProjectUploadRequested(
      std::function<void(const ImportedSongData &)> cb);
  void setOnCloudDownloadRequested(
      std::function<void(const juce::String &, const juce::String &)> cb);
  void setOnCloudSearchRequested(std::function<void(const juce::String &)> cb);
  void setOnCloudDeleteRequested(
      std::function<void(const juce::String &, const juce::String &)> cb);
  void setOnCloudSettingsSyncRequested(
      std::function<void(const ImportedSongData &)> cb);
  void setOnCloudSettingsVersionsRequested(
      std::function<void(const ImportedSongData &, juce::Rectangle<int>)> cb);
  void setCloudSettingsPendingProvider(
      std::function<bool(const ImportedSongData &)> provider);
  void setSaveProjectStateProvider(std::function<bool()> availableProvider,
                                   std::function<bool()> pendingProvider);
  int getSongCount() const;
  const ImportedSongData *getSongPtr(int index) const;
  int getSongIndexById(const juce::String &songId) const;
  void setSelectedSongIndex(int songIndex);
  bool isAutoPlayNextEnabledForSong(const juce::String &songId) const;
  int resolveAutoPlayNextIndex(const juce::String &currentSongId) const;
  void setNowPlayingSong(const juce::String &songId, bool isPlaying);
  void setCloudUploadProgress(const juce::String &songId, double fraction,
                              const juce::String &phase);
  void clearCloudUploadProgress(const juce::String &songId);
  void setCloudSongs(std::vector<CloudSongSummary> songs);
  void setCloudSearchLoading(bool isLoading);
  int getLastActivatedIndex() const;
  juce::String getActiveSetlistName() const;
  void refreshFromStorageView();
  void showSongPicker(juce::Component &anchor, std::function<void()> onAdd);
  void showActiveSetlist();
  void openSongImportWizardFromExternal();
  void refreshLanguageTexts();
  void refreshThemeColours();
  void setMinimalPhoneMode(bool enabled);
  void setPhoneSafeAreaInsets(juce::BorderSize<int> insets);
  void setOnOverlayCloseRequested(std::function<void()> cb);
  /** Abre un .pwproj recibido desde el sistema operativo. */
  void openProjectFile(const juce::File &file);

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  std::shared_ptr<const LibraryState> remoteLibrary;
  friend struct SongListPanelTestAccess;
  enum class ViewMode { Setlists, Songs };
  enum class SongsSubview { SetlistSongs, Library };
  struct SongsLayout {
    juce::Rectangle<int> workspace;
    juce::Rectangle<int> headerRow;
    juce::Rectangle<int> headerPanel;
    juce::Rectangle<int> listArea;
    juce::Rectangle<int> back;
    juce::Rectangle<int> title;
    juce::Rectangle<int> subtitle;
    juce::Rectangle<int> edit;
    juce::Rectangle<int> menu;
    juce::Rectangle<int> add;
    juce::Rectangle<int> close;
  };
  struct SetlistsLayout {
    juce::Rectangle<int> headerTitle;
    juce::Rectangle<int> headerSubtitle;
    juce::Rectangle<int> createRow;
    juce::Rectangle<int> close;
    juce::Rectangle<int> searchRow;
    juce::Rectangle<int> viewport;
  };

  struct SongRow {
    int order = 0;
    juce::String title;
    juce::String artist;
    juce::String artworkPath;
    juce::String key;
    int bpm = 0;
    int stemCount = 0;
    juce::String songId;
    bool autoPlayNext = false;
    bool isNowPlaying = false;
    bool isSelected = false;
    int songIndex = -1;
    double durationSeconds = 0.0;
    juce::Colour accentColour = UITheme::accent;
    double cloudUploadFraction = -1.0;
    juce::String cloudUploadPhase;
    bool requiresOptimization = false;
  };

  class ListContent : public juce::Component {
  public:
    void setSongs(std::vector<SongRow> rows);
    void setNowPlayingSong(const juce::String &songId, bool isPlaying);
    void setSelectedSongId(const juce::String &songId);
    void setCloudUploadProgress(const juce::String &songId, double fraction,
                                const juce::String &phase);
    void setOnSongActivated(std::function<void(int)> cb);
    void setOnSongsReordered(std::function<void(int, int)> cb);
    void setOnDragStateChanged(std::function<void(bool)> cb);
    void setOnAutoPlayNextToggled(std::function<void(int)> cb);
    void setOnSongRemoveRequested(std::function<void(int)> cb);
    void setOnAddSongsRequested(std::function<void()> cb);
    void setOnOptimizeRequested(std::function<void(int)> cb);
    int getPreferredHeight() const;

    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &event) override;
    void mouseDrag(const juce::MouseEvent &event) override;
    void mouseUp(const juce::MouseEvent &event) override;
    void mouseDoubleClick(const juce::MouseEvent &event) override;
    void mouseMove(const juce::MouseEvent &event) override;
    void mouseExit(const juce::MouseEvent &event) override;

  private:
    static constexpr int rowStart = 8;
    static constexpr int rowHeight = 64;
    static constexpr int rowGap = 0;
    static constexpr int rowSideInset = 0;

    int rowIndexAtY(int y) const;
    int insertionIndexAtY(int y) const;
    int rowTopAtIndex(int index) const;
    int insertionLineY(int insertionIndex) const;
    juce::Rectangle<int> optionsButtonBoundsForRow(
        const juce::Rectangle<int> &rowBounds) const;
    void showActionsMenuForRow(int rowIndex);

    std::vector<SongRow> songs;
    std::function<void(int)> onSongActivated;
    std::function<void(int, int)> onSongsReordered;
    std::function<void(bool)> onDragStateChanged;
    std::function<void(int)> onAutoPlayNextToggled;
    std::function<void(int)> onSongRemoveRequested;
    std::function<void()> onAddSongsRequested;
    std::function<void(int)> onOptimizeRequested;
    juce::Point<float> mouseDownPosition;
    bool dragCandidate = false;
    bool isDragging = false;
    int dragSourceIndex = -1;
    int dragInsertionIndex = -1;
    int pendingActionsMenuRow = -1;
    int revealedActionsRow = -1;
    int swipeCandidateRow = -1;
    float swipeOffset = 0.0f;
    float swipeStartOffset = 0.0f;
    bool swipeDragging = false;
    bool gestureDirectionDecided = false;
    int hoveredRow = -1;
    float dragRowOffsetY = 0.0f;
    float dragMouseY = 0.0f;
    std::unordered_map<std::string, juce::Image> artworkCache;
  };

  class SetlistManagerPanel;
  class SetlistsContent;
  class PageContainer;

  void openSongImportWizard(bool withinLibrary = false);
  void showLibraryCreationPage(std::unique_ptr<SongImportWizardComponent> panel);
  void showSongImportOverlay(std::unique_ptr<SongImportWizardComponent> panel);
  void openProjectFileChooser();
  void openProjectFileFromUrl(const juce::URL &url);
  void openSongEditWizard(const juce::String &songId);
  void beginLegacySongConversion(const juce::String &songId);
  void convertLegacySongAtDestination(const juce::String &songId,
                                      const juce::File &destination);
  void openSetlistManager();
  void activateSongAtIndex(int songIndex);
  void openHeaderMenu();
  void openLibraryBrowser();
  void openAddFromLibraryMenu();
  void openSetlistOptionsMenu(const juce::String &setlistId,
                              juce::Rectangle<int> targetArea);
  void showSetlistsView();
  void showSongsView(const juce::String &setlistId);
  void showLibraryView(bool animate);
  void showSongsSubview(bool animate);
  void animateTransitionIfNeeded(ViewMode previousMode);
  void animateSongsSubviewToCurrent();
  void applySongsSubviewOffset(float offsetPx);
  void updateSongsSubviewLayout(const SongsLayout &layout);
  void finishSongsSubviewAnimation();
  void beginSongsSwipeGesture(const juce::MouseEvent &event,
                              bool fromSongsToLibrary);
  void updateSongsSwipeGesture(const juce::MouseEvent &event);
  void endSongsSwipeGesture(const juce::MouseEvent &event);
  static float swipeThresholdRatio();
  SongsLayout computeSongsLayout() const;
  SetlistsLayout computeSetlistsLayout() const;
  void applySongsLayout(const SongsLayout &layout);
  void applySetlistsLayout(const SetlistsLayout &layout);
  void applyModeVisibility();
  void showPage(int index, bool animate);
  void applyInitialViewFromStorage();
  void createSetlist();
  void renameSetlistById(const juce::String &setlistId);
  void deleteSetlistById(const juce::String &setlistId);
  void renameActiveSetlist();
  void deleteActiveSetlist();
  static juce::String formatDurationHMS(double totalSeconds);
  void refreshSongsHeaderSummary();
  void promptTextAsync(const juce::String &title, const juce::String &message,
                       const juce::String &initial,
                       std::function<void(juce::String)> onComplete);
  void refreshFromStorage();
  void timerCallback() override;
  juce::Rectangle<int> getPageContentBounds(int margin) const;

  juce::Label titleLabel;
  juce::Label songsSubtitleLabel;
  std::unique_ptr<PageContainer> pageContainer;
  juce::Label setlistsHeaderLabel;
  juce::Label setlistsSubtitleLabel;
  HeaderIconButton backButton{"back", IconGlyph::ArrowLeft};
  HeaderIconButton editSetlistButton{"editSetlist", IconGlyph::Pencil};
  HeaderIconButton headerMenuButton{"headerMenu", IconGlyph::MusicNote};
  HeaderIconButton addSongsButton{"addSongs", IconGlyph::Add};
  HeaderIconButton createSetlistButton{"createSetlist", IconGlyph::Add};
  HeaderIconButton closeSetlistsButton{"closeSetlists", IconGlyph::Close};
  HeaderIconButton closeSongsButton{"closeSongs", IconGlyph::Close};
  HeaderIconButton closeLibraryButton{"closeLibrary", IconGlyph::Close};
  SearchField setlistSearchField;
  SwipeHintHandle songsToLibrarySwipeHint{SwipeHintHandle::Direction::Right};
  SwipeHintHandle libraryToSongsSwipeHint{SwipeHintHandle::Direction::Left};
  SetlistSongsHeaderPanel songsHeaderPanel;
  juce::Viewport listViewport;
  juce::Viewport setlistsViewport;
  ListContent listContent;
  LibraryCatalogPanel libraryCatalogPanel;
  std::unique_ptr<SetlistsContent> setlistsContent;
  ViewMode viewMode = ViewMode::Songs;
  SongsSubview songsSubview = SongsSubview::SetlistSongs;
  SongsLayout cachedSongsLayout;
  float songsSubviewOffsetPx = 0.0f;
  bool songsSubviewTransitionInFlight = false;
  bool songsSwipeGestureActive = false;
  float songsSwipeStartOffsetPx = 0.0f;
  juce::Point<float> songsSwipeStartPosition;
  std::vector<ImportedSongData> librarySongs;
  std::vector<int> activeSongIndices;
  juce::String activeSetlistId;
  juce::String activeSetlistName;
  juce::String nowPlayingSongId;
  juce::String selectedSongId;
  juce::String setlistSearchQuery;
  bool isPlaybackActive = false;
  bool initialViewApplied = false;
  bool transitionInFlight = false;
  bool minimalPhoneMode = false;
  juce::BorderSize<int> phoneSafeAreaInsets;
  int songsSwipeDurationMs = 250;
  int transitionDurationMs = 150;
  juce::StringArray autoPlayNextMarks;
  struct CloudUploadIndicator {
    double fraction = 0.0;
    juce::String phase;
  };
  std::unordered_map<std::string, CloudUploadIndicator> cloudUploadIndicators;
  std::function<void(const ImportedSongData &, int)> onSongActivated;
  std::function<void(const juce::String &)> onSongEditStarted;
  std::function<void(const ImportedSongData &, int)> onSongEdited;
  std::function<void(const juce::String &)> onSetlistNameChanged;
  std::function<void()> onSaveProjectRequested;
  std::function<void()> onPadsRequested;
  std::function<void()> onOverlayCloseRequested;
  std::function<void(const ImportedSongData &)> onProjectUploadRequested;
  std::function<void(const juce::String &, const juce::String &)>
      onCloudDownloadRequested;
  juce::String libraryTargetSetlistId;
  std::function<void(const juce::String &)> onCloudSearchRequested;
  std::function<void(const juce::String &, const juce::String &)>
      onCloudDeleteRequested;
  std::function<void(const ImportedSongData &)> onCloudSettingsSyncRequested;
  std::function<void(const ImportedSongData &, juce::Rectangle<int>)> onCloudSettingsVersionsRequested;
  std::function<bool(const ImportedSongData &)> cloudSettingsPendingProvider;
  std::function<bool()> saveProjectAvailableProvider;
  std::function<bool()> saveProjectPendingProvider;
  int lastActivatedIndex = -1;
  std::unique_ptr<SongWizardOverlay> songImportOverlay;
  std::unique_ptr<AppModal> appModal;
  std::unique_ptr<SongImportWizardComponent> libraryCreationWizard;
  std::unique_ptr<juce::FileChooser> projectFileChooser;
  std::unique_ptr<juce::FileChooser> projectConversionFileChooser;
};
