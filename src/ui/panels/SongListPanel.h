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

  SongListPanel();
  ~SongListPanel() override;

  void setOnSongActivated(
      std::function<void(const ImportedSongData &, int)> cb);
  int getSongCount() const;
  const ImportedSongData *getSongPtr(int index) const;
  int getLastActivatedIndex() const;

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  struct SongRow {
    int order = 0;
    juce::String title;
    juce::String meta;
    int songIndex = -1;
  };

  class ListContent : public juce::Component {
  public:
    void setSongs(std::vector<SongRow> rows);
    void setOnSongActivated(std::function<void(int)> cb);
    int getPreferredHeight() const;

    void paint(juce::Graphics &g) override;
    void mouseDoubleClick(const juce::MouseEvent &event) override;

  private:
    int rowIndexAtY(int y) const;

    std::vector<SongRow> songs;
    std::function<void(int)> onSongActivated;
  };

  void openSongImportWizard();
  void refreshFromStorage();

  juce::Viewport listViewport;
  ListContent listContent;
  AddSongButton addSongButton;
  std::vector<ImportedSongData> librarySongs;
  std::function<void(const ImportedSongData &, int)> onSongActivated;
  int lastActivatedIndex = -1;
};
