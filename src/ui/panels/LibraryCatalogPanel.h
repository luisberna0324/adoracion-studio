#pragma once

#include "../../data/SongData.h"
#include "../../data/SongLibraryStore.h"
#include "../UiCommon.h"
#include "../components/SearchField.h"
#include <JuceHeader.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

class LibraryCatalogPanel final : public juce::Component {
public:
  using SongLoadRequested =
      std::function<void(const ImportedSongData &, int songIndex)>;
  using AddSongsRequested = std::function<void(const juce::StringArray &)>;
  using ImportRequested = std::function<void()>;
  using BackRequested = std::function<void()>;

  LibraryCatalogPanel();
  ~LibraryCatalogPanel() override;

  void setOnSongLoadRequested(SongLoadRequested cb);
  void setOnAddSongsToSetlist(AddSongsRequested cb);
  void setOnImportRequested(ImportRequested cb);
  void setOnBackRequested(BackRequested cb);

  void setActiveSetlistId(const juce::String &setlistId);
  void setNowPlayingSong(const juce::String &songId, bool isPlaying);
  void refreshFromStorage();
  void refreshLanguageTexts();
  void refreshThemeColours();

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
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
    double durationSeconds = 0.0;
    int64_t createdMs = 0;
    int bpm = 0;
    int stemCount = 0;
    int setlistUsageCount = 0;
    bool inSetlist = false;
    bool isNowPlaying = false;
  };

  class IconButton final : public juce::Button {
  public:
    IconButton(const juce::String &name, IconGlyph glyphToUse);

    void setStyle(bool filled, juce::Colour iconColour);
    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override;

  private:
    IconGlyph glyph;
    bool filledStyle = false;
    juce::Colour glyphColour = juce::Colours::white;
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
                 bool hideOptionsButtons);
    void setNowPlayingSong(const juce::String &songId, bool isPlaying);
    void setOnLoadRequested(LoadCallback cb);
    void setOnToggleRequested(ToggleCallback cb);
    void setOnOptionsRequested(OptionsCallback cb);
    int getPreferredHeight(int width) const;

    void paint(juce::Graphics &g) override;
    void mouseMove(const juce::MouseEvent &event) override;
    void mouseExit(const juce::MouseEvent &event) override;
    void mouseUp(const juce::MouseEvent &event) override;

  private:
    std::vector<CatalogItem> rows;
    juce::StringArray selectedSongIds;
    juce::String nowPlayingSongId;
    int hoveredRowIndex = -1;
    bool playbackActive = false;
    bool hideOptionsButtons = false;
    LoadCallback onLoadRequested;
    ToggleCallback onToggleRequested;
    OptionsCallback onOptionsRequested;

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
  void togglePendingSelection(int rowIndex);
  void commitPendingSelection();
  bool matchesFilter(const ImportedSongData &song, int usageCount) const;
  bool matchesSearch(const ImportedSongData &song) const;
  int usageCountForSongId(const juce::String &songId) const;

  juce::String titleTextForActiveSetlist() const;
  juce::String subtitleTextForActiveSetlist() const;

  IconButton backButton{"libraryBack", IconGlyph::ArrowLeft};
  IconButton menuButton{"libraryMenu", IconGlyph::More};
  SearchField searchField;
  juce::Viewport chipsViewport;
  std::array<std::unique_ptr<FilterChipButton>, 5> chipButtons;
  IconButton advancedFilterButton{"libraryFilter", IconGlyph::Settings};
  std::unique_ptr<ChipsRow> chipsRow;
  juce::Viewport contentViewport;
  CatalogContent content;
  FooterActionButton confirmButton;
  juce::Rectangle<int> headerTitleBounds;
  juce::Rectangle<int> headerSubtitleBounds;

  std::vector<ImportedSongData> librarySongs;
  std::vector<SetlistData> setlists;
  std::vector<CatalogItem> visibleRows;
  juce::StringArray activeSetlistSongIds;
  juce::StringArray pendingSelectedSongIds;
  juce::String activeSetlistId;
  juce::String activeSetlistName;
  juce::String selectedSongId;
  juce::String nowPlayingSongId;
  bool playbackActive = false;
  FilterChip activeFilter = FilterChip::All;
  SortMode sortMode = SortMode::TitleAsc;

  SongLoadRequested onSongLoadRequested;
  AddSongsRequested onAddSongsToSetlist;
  ImportRequested onImportRequested;
  BackRequested onBackRequested;
};
