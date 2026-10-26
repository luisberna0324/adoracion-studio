#pragma once

#include "../../data/SongData.h"
#include "../../data/CloudLibraryClient.h"
#include "../../data/SongLibraryStore.h"
#include "../UiCommon.h"
#include "../components/SearchField.h"
#include <JuceHeader.h>

#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

class LibraryCatalogPanel final : public juce::Component {
public:
  enum class HeaderMode { SetlistTarget = 0, Library };

  using SongLoadRequested =
      std::function<void(const ImportedSongData &, int songIndex)>;
  using SongEditRequested = std::function<void(const juce::String &songId)>;
  using SongConvertRequested = std::function<void(const juce::String &songId)>;
  using ProjectUploadRequested = std::function<void(const juce::String &songId)>;
  using CloudDownloadRequested =
      std::function<void(const juce::String &songId,
                         const juce::String &targetSetlistId)>;
  using CloudSearchRequested = std::function<void(const juce::String &query)>;
  using CloudDeleteRequested = std::function<void(const juce::String &songId,
                                                   const juce::String &title)>;
  using CloudSettingsRequested = std::function<void(const juce::String &songId)>;
  using CloudSettingsVersionsRequested =
      std::function<void(const juce::String &songId, juce::Rectangle<int> screenAnchor)>;
  using CloudSettingsPendingProvider =
      std::function<bool(const juce::String &songId)>;
  using SongDeleteRequested = std::function<void(const juce::String &songId)>;
  using AddSongsRequested = std::function<void(const juce::StringArray &)>;
  using CreateSongRequested = std::function<void()>;
  using OpenProjectRequested = std::function<void()>;
  using BackRequested = std::function<void()>;

  LibraryCatalogPanel();
  ~LibraryCatalogPanel() override;

  void setOnSongLoadRequested(SongLoadRequested cb);
  void setOnSongEditRequested(SongEditRequested cb);
  void setOnSongLyricsRequested(SongEditRequested cb);
  void setOnSongConvertRequested(SongConvertRequested cb);
  void setOnProjectUploadRequested(ProjectUploadRequested cb);
  void setOnCloudDownloadRequested(CloudDownloadRequested cb);
  void setOnCloudSearchRequested(CloudSearchRequested cb);
  void setOnCloudDeleteRequested(CloudDeleteRequested cb);
  void setOnCloudSettingsSyncRequested(CloudSettingsRequested cb);
  void setOnCloudSettingsVersionsRequested(CloudSettingsVersionsRequested cb);
  void setCloudSettingsPendingProvider(CloudSettingsPendingProvider provider);
  void setOnSongDeleteRequested(SongDeleteRequested cb);
  void setOnAddSongsToSetlist(AddSongsRequested cb);
  void setOnCreateSongRequested(CreateSongRequested cb);
  void setOnOpenProjectRequested(OpenProjectRequested cb);
  void setOnBackRequested(BackRequested cb);

  void setActiveSetlistId(const juce::String &setlistId);
  void setTargetSetlistId(const juce::String &setlistId);
  void setHeaderMode(HeaderMode mode);
  void setNowPlayingSong(const juce::String &songId, bool isPlaying);
  void setCloudSongs(std::vector<CloudSongSummary> songs);
  void setCloudSearchLoading(bool isLoading);
  void setCloudUploadProgress(const juce::String &songId, double fraction,
                              const juce::String &phase);
  void clearCloudUploadProgress(const juce::String &songId);
  void openCloud();
  bool isCloudOpen() const { return isShowing() && cloudFilterButton.isActive(); }
  void refreshFromStorage();
  void refreshLanguageTexts();
  void setMinimalPhoneMode(bool enabled);
  void setHeaderTrailingInset(int inset);
  void refreshThemeColours();

  void setRemoteLibrary(std::shared_ptr<const LibraryState> library);
  std::function<void(const juce::String &, const juce::var &, const juce::String &)> onRemoteAction;
  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  friend struct TeamsWorkspaceTestAccess;
  friend struct SongListLyricsTestAccess;
  std::shared_ptr<const LibraryState> remoteLibrary;
  enum class FilterChip {
    All = 0,
    Recent,
    Favorites,
    Worship,
    Fast
  };

  enum class SortMode {
    TitleAsc = 0,
    TitleDesc,
    RecentFirst,
    BpmAsc,
    BpmDesc
  };

  struct CatalogItem {
    int songIndex = -1;
    juce::String songId;
    juce::String title;
    juce::String artist;
    juce::String key;
    juce::String dominantCategory;
    juce::String artworkPath;
    bool blankSong = false;
    double durationSeconds = 0.0;
    int64_t createdMs = 0;
    int bpm = 0;
    int stemCount = 0;
    int setlistUsageCount = 0;
    juce::Colour accentColour = UITheme::accent;
    bool inSetlist = false;
    bool isNowPlaying = false;
    bool isProjectBacked = false;
    bool isProjectAvailable = false;
    bool isCloudOnly = false;
    bool isSharedCloud = false;
    bool isCloudSynced = false;
    bool isCloudDownloaded = false;
    juce::String localSongId;
    juce::String remoteSongId;
    double cloudUploadFraction = -1.0;
    juce::String cloudUploadPhase;
  };

  class IconButton final : public juce::Button {
  public:
    IconButton(const juce::String &name, IconGlyph glyphToUse);

    void setStyle(bool filled, juce::Colour iconColour);
    void setAccent(bool shouldAccent);
    void setSvgIcon(SvgIconName iconToUse);
    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override;

  private:
    IconGlyph glyph;
    SvgIconName svgIcon = SvgIconName::ArrowLeft;
    bool useSvgIcon = false;
    bool filledStyle = false;
    bool accentStyle = false;
    juce::Colour glyphColour = juce::Colours::white;
  };

  class SourceFilterButton final : public juce::Button {
  public:
    SourceFilterButton(const juce::String &name, juce::String labelToUse);

    void setActive(bool shouldBeActive);
    bool isActive() const;
    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override;

  private:
    juce::String label;
    bool active = false;
  };

  class FilterChipButton final : public juce::Button {
  public:
    FilterChipButton(const juce::String &name, FilterChip chipToUse,
                     juce::String textToUse);

    void setLabelText(const juce::String &text);
    void setActive(bool shouldBeActive);
    FilterChip getChip() const;
    int getPreferredWidth() const;
    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override;

  private:
    FilterChip chip;
    juce::String labelText;
    bool active = false;
  };

  class FooterActionButton final : public juce::Button {
  public:
    FooterActionButton();

    void setCount(int nextCount);
    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override;

  private:
    int count = 0;
  };

  class ChipsRow final : public juce::Component {
  public:
    ChipsRow(std::array<std::unique_ptr<FilterChipButton>, 5> &chipButtons,
             IconButton &advancedFilterButton);

    int getPreferredWidth() const;
    void resized() override;

  private:
    std::array<std::unique_ptr<FilterChipButton>, 5> &chips;
    IconButton &filterButton;
  };

  class CatalogContent final : public juce::Component {
  public:
    using LoadCallback = std::function<void(int rowIndex)>;
    using ToggleCallback = std::function<void(int rowIndex)>;
    using OptionsCallback = std::function<void(int rowIndex, juce::Rectangle<int>)>;

    void setRows(std::vector<CatalogItem> rows,
                 const juce::StringArray &selectedSongIds,
                 bool hideOptionsButtons, bool allowSelection);
    void setNowPlayingSong(const juce::String &songId, bool isPlaying);
    void setCloudUploadProgress(const juce::String &songId, double fraction,
                                const juce::String &phase);
    void setOnLoadRequested(LoadCallback cb);
    void setOnToggleRequested(ToggleCallback cb);
    void setOnOptionsRequested(OptionsCallback cb);
    int getPreferredHeight(int width) const;

    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &event) override;
    void mouseDrag(const juce::MouseEvent &event) override;
    void mouseMove(const juce::MouseEvent &event) override;
    void mouseExit(const juce::MouseEvent &event) override;
    void mouseUp(const juce::MouseEvent &event) override;
    void mouseDoubleClick(const juce::MouseEvent &event) override;

  private:
    std::vector<CatalogItem> rows;
    juce::StringArray selectedSongIds;
    juce::String nowPlayingSongId;
    std::unordered_map<std::string, juce::Image> artworkCache;
    int hoveredRowIndex = -1;
    bool playbackActive = false;
    bool hideOptionsButtons = false;
    bool selectionEnabled = true;
    LoadCallback onLoadRequested;
    ToggleCallback onToggleRequested;
    OptionsCallback onOptionsRequested;
    juce::Point<float> swipeStartPosition;
    int swipeCandidateRow = -1;
    int revealedActionsRow = -1;
    float swipeOffset = 0.0f;
    float swipeStartOffset = 0.0f;
    bool swipeDragging = false;

    juce::Rectangle<int> itemBoundsForIndex(int index) const;
    juce::Rectangle<int> toggleBoundsForIndex(int index) const;
    juce::Rectangle<int> menuBoundsForIndex(int index) const;
    int rowIndexAt(juce::Point<int> point) const;
  };

  void rebuildRows();
  void updateContentSize();
  void refreshFilterButtonStates();
  void refreshFooterState();
  void refreshTexts();
  void openHeaderMenu();
  void openAdvancedFilterMenu();
  void openRowMenu(int rowIndex, juce::Rectangle<int> targetBounds);
  std::unique_ptr<juce::Component> createRowMenu(int rowIndex,
                                               juce::Rectangle<int> targetBounds);
  void togglePendingSelection(int rowIndex);
  void commitPendingSelection();
  bool matchesFilter(const ImportedSongData &song, int usageCount) const;
  bool matchesSearch(const ImportedSongData &song) const;
  int usageCountForSongId(const juce::String &songId) const;

  juce::String titleTextForActiveSetlist() const;
  juce::String subtitleTextForActiveSetlist() const;

  HeaderIconButton backButton{"libraryBack", IconGlyph::ArrowLeft};
  IconButton menuButton{"libraryMenu", IconGlyph::More};
  SearchField searchField;
  SourceFilterButton cloudFilterButton{"cloudFilter", "Cloud"};
  SourceFilterButton localFilterButton{"localFilter", "Local"};
  juce::Viewport chipsViewport;
  std::array<std::unique_ptr<FilterChipButton>, 5> chipButtons;
  IconButton advancedFilterButton{"libraryFilter", IconGlyph::Settings};
  std::unique_ptr<ChipsRow> chipsRow;
  juce::Viewport contentViewport;
  CatalogContent content;
  juce::TextButton loadProjectButton{"Cargar"};
  HeaderIconButton loadSongButton{"loadSong", IconGlyph::ArrowLeft};
  HeaderIconButton createSongButton{"createSong", IconGlyph::Add};
  int headerTrailingInset = 0;
  bool minimalPhoneMode = false;
  FooterActionButton confirmButton;
  juce::Rectangle<int> headerTitleBounds;
  juce::Rectangle<int> headerSubtitleBounds;

  std::vector<ImportedSongData> librarySongs;
  std::vector<CloudSongSummary> cloudSongs;
  std::vector<SetlistData> setlists;
  std::vector<CatalogItem> visibleRows;
  struct CloudUploadIndicator {
    double fraction = 0.0;
    juce::String phase;
  };
  std::unordered_map<std::string, CloudUploadIndicator> cloudUploadIndicators;
  juce::StringArray activeSetlistSongIds;
  juce::StringArray pendingSelectedSongIds;
  juce::String activeSetlistId;
  juce::String targetSetlistId;
  juce::String activeSetlistName;
  HeaderMode headerMode = HeaderMode::SetlistTarget;
  juce::String selectedSongId;
  juce::String nowPlayingSongId;
  bool playbackActive = false;
  FilterChip activeFilter = FilterChip::All;
  SortMode sortMode = SortMode::TitleAsc;

  SongLoadRequested onSongLoadRequested;
  SongEditRequested onSongEditRequested;
  SongEditRequested onSongLyricsRequested;
  SongConvertRequested onSongConvertRequested;
  ProjectUploadRequested onProjectUploadRequested;
  CloudDownloadRequested onCloudDownloadRequested;
  CloudSearchRequested onCloudSearchRequested;
  CloudDeleteRequested onCloudDeleteRequested;
  CloudSettingsRequested onCloudSettingsSyncRequested;
  CloudSettingsVersionsRequested onCloudSettingsVersionsRequested;
  CloudSettingsPendingProvider cloudSettingsPendingProvider;
  SongDeleteRequested onSongDeleteRequested;
  AddSongsRequested onAddSongsToSetlist;
  CreateSongRequested onCreateSongRequested;
  OpenProjectRequested onOpenProjectRequested;
  BackRequested onBackRequested;
};
