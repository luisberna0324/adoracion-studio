#pragma once

#include "../../data/SongData.h"
#include "../../data/SongLibraryStore.h"
#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>

class SongListPanel final : public juce::Component {
public:
  class AddSongButton final : public juce::Button {
  public:
    AddSongButton();
    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override;
  };

  class ManageSetlistsButton final : public juce::Button {
  public:
    ManageSetlistsButton();
    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override;
  };

  SongListPanel();
  ~SongListPanel() override;

  void setOnSongActivated(
      std::function<void(const ImportedSongData &, int)> cb);
  void setOnSetlistNameChanged(std::function<void(const juce::String &)> cb);
  int getSongCount() const;
  const ImportedSongData *getSongPtr(int index) const;
  int getSongIndexById(const juce::String &songId) const;
  bool isAutoPlayNextEnabledForSong(const juce::String &songId) const;
  void setNowPlayingSong(const juce::String &songId, bool isPlaying);
  int getLastActivatedIndex() const;
  juce::String getActiveSetlistName() const;
  void refreshLanguageTexts();

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  struct SongRow {
    int order = 0;
    juce::String title;
    juce::String meta;
    juce::String songId;
    bool autoPlayNext = false;
    bool isNowPlaying = false;
    bool isSelected = false;
    int songIndex = -1;
  };

  class ListContent : public juce::Component {
  public:
    void setSongs(std::vector<SongRow> rows);
    void setNowPlayingSong(const juce::String &songId, bool isPlaying);
    void setSelectedSongId(const juce::String &songId);
    void setOnSongActivated(std::function<void(int)> cb);
    void setOnSongsReordered(std::function<void(int, int)> cb);
    void setOnAutoPlayNextToggled(std::function<void(int)> cb);
    void setOnSongRemoveRequested(std::function<void(int)> cb);
    int getPreferredHeight() const;

    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &event) override;
    void mouseDrag(const juce::MouseEvent &event) override;
    void mouseUp(const juce::MouseEvent &event) override;
    void mouseDoubleClick(const juce::MouseEvent &event) override;

  private:
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
    std::function<void(int)> onAutoPlayNextToggled;
    std::function<void(int)> onSongRemoveRequested;
    juce::Point<float> mouseDownPosition;
    bool dragCandidate = false;
    bool isDragging = false;
    bool didDragSinceMouseDown = false;
    int dragSourceIndex = -1;
    int dragInsertionIndex = -1;
    int pendingActionsMenuRow = -1;
    float dragRowOffsetY = 0.0f;
    float dragMouseY = 0.0f;
  };

  class SetlistManagerPanel;

  void openSongImportWizard();
  void openSetlistManager();
  void refreshFromStorage();

  juce::Viewport listViewport;
  ListContent listContent;
  AddSongButton addSongButton;
  ManageSetlistsButton manageSetlistsButton;
  std::vector<ImportedSongData> librarySongs;
  std::vector<int> activeSongIndices;
  juce::String activeSetlistId;
  juce::String activeSetlistName;
  juce::String nowPlayingSongId;
  juce::String selectedSongId;
  bool isPlaybackActive = false;
  juce::StringArray autoPlayNextMarks;
  std::function<void(const ImportedSongData &, int)> onSongActivated;
  std::function<void(const juce::String &)> onSetlistNameChanged;
  int lastActivatedIndex = -1;
};
