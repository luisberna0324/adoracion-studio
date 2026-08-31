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
#include <unordered_map>
#include <vector>

class SongListPanel final : public juce::Component, private juce::Timer {
public:
  class HeaderIconButton final : public juce::Button {
  public:
    HeaderIconButton(const juce::String &name, IconGlyph glyphToUse);
    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override;
    void setAccent(bool shouldAccent);

  private:
    IconGlyph glyph;
    bool accent = false;
  };

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

  SongListPanel();
  ~SongListPanel() override;

  void setOnSongActivated(
      std::function<void(const ImportedSongData &, int)> cb);
  void setOnSetlistNameChanged(std::function<void(const juce::String &)> cb);
  void setOnSaveProjectRequested(std::function<void()> cb);
  void setSaveProjectStateProvider(std::function<bool()> availableProvider,
                                   std::function<bool()> pendingProvider);
  int getSongCount() const;
  const ImportedSongData *getSongPtr(int index) const;
  int getSongIndexById(const juce::String &songId) const;
  void setSelectedSongIndex(int songIndex);
  bool isAutoPlayNextEnabledForSong(const juce::String &songId) const;
  int resolveAutoPlayNextIndex(const juce::String &currentSongId) const;
  void setNowPlayingSong(const juce::String &songId, bool isPlaying);
  int getLastActivatedIndex() const;
  juce::String getActiveSetlistName() const;
  void refreshFromStorageView();
  void openSongImportWizardFromExternal();
  void refreshLanguageTexts();
  void refreshThemeColours();
  /** Abre un .pwproj recibido desde el sistema operativo. */
  void openProjectFile(const juce::File &file);

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  enum class ViewMode { Setlists, Songs };
  enum class SongsSubview { SetlistSongs, Library };
  struct SongsLayout {
    juce::Rectangle<int> workspace;
    juce::Rectangle<int> headerRow;
    juce::Rectangle<int> headerPanel;
    juce::Rectangle<int> listArea;
    juce::Rectangle<int> back;
    juce::Rectangle<int> title;
    juce::Rectangle<int> edit;
    juce::Rectangle<int> menu;
  };
  struct SetlistsLayout {
    juce::Rectangle<int> headerTitle;
    juce::Rectangle<int> headerSubtitle;
    juce::Rectangle<int> createRow;
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
  };

  class ListContent : public juce::Component {
  public:
    void setSongs(std::vector<SongRow> rows);
    void setNowPlayingSong(const juce::String &songId, bool isPlaying);
    void setSelectedSongId(const juce::String &songId);
    void setOnSongActivated(std::function<void(int)> cb);
    void setOnSongsReordered(std::function<void(int, int)> cb);
    void setOnDragStateChanged(std::function<void(bool)> cb);
    void setOnAutoPlayNextToggled(std::function<void(int)> cb);
    void setOnSongRemoveRequested(std::function<void(int)> cb);
    void setOnAddSongsRequested(std::function<void()> cb);
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
    static constexpr int rowHeight = 72;
    static constexpr int rowGap = 8;
    static constexpr int rowSideInset = 12;

    int rowIndexAtY(int y) const;
    int insertionIndexAtY(int y) const;
    int rowTopAtIndex(int index) const;
    int insertionLineY(int insertionIndex) const;
    juce::Rectangle<int> addSongsRowBounds() const;
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
    juce::Point<float> mouseDownPosition;
    bool dragCandidate = false;
    bool isDragging = false;
    int dragSourceIndex = -1;
    int dragInsertionIndex = -1;
    int pendingActionsMenuRow = -1;
    int hoveredRow = -1;
    float dragRowOffsetY = 0.0f;
    float dragMouseY = 0.0f;
    std::unordered_map<std::string, juce::Image> artworkCache;
  };

  class SetlistManagerPanel;
  class SetlistsContent;

  void openSongImportWizard();
  void openSongImportWizard(const juce::File &destination);
  void openProjectFileChooser();
  void openSongEditWizard(const juce::String &songId);
  void beginLegacySongConversion(const juce::String &songId);
  void convertLegacySongAtDestination(const juce::String &songId,
                                      const juce::File &destination);
  void openSetlistManager();
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

  juce::Label titleLabel;
  juce::Label setlistsHeaderLabel;
  juce::Label setlistsSubtitleLabel;
  HeaderIconButton backButton{"back", IconGlyph::ArrowLeft};
  HeaderIconButton editSetlistButton{"editSetlist", IconGlyph::Pencil};
  HeaderIconButton headerMenuButton{"headerMenu", IconGlyph::More};
  juce::TextButton createSetlistButton{"+ Crear setlist"};
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
  int songsSwipeDurationMs = 250;
  int transitionDurationMs = 150;
  juce::StringArray autoPlayNextMarks;
  std::function<void(const ImportedSongData &, int)> onSongActivated;
  std::function<void(const juce::String &)> onSetlistNameChanged;
  std::function<void()> onSaveProjectRequested;
  std::function<bool()> saveProjectAvailableProvider;
  std::function<bool()> saveProjectPendingProvider;
  int lastActivatedIndex = -1;
  std::unique_ptr<juce::FileChooser> projectFileChooser;
  std::unique_ptr<juce::FileChooser> projectConversionFileChooser;
};
