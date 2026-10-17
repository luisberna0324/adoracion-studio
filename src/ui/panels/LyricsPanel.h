#pragma once

#include "../../data/SongData.h"
#include "../../integrations/FreeShowClient.h"
#include "../UiCommon.h"
#include <JuceHeader.h>
#include <functional>
#include <memory>

#if !JUCE_IOS && !JUCE_ANDROID

// Desktop-only editor. Selecting or editing a mark never projects it.
class LyricsPanel final : public juce::Component, private juce::Timer {
public:
  using SongLyrics = playworship::lyrics::SongLyrics;
  using FreeShowClient = playworship::lyrics::FreeShowClient;

  LyricsPanel();
  ~LyricsPanel() override;
  void setClient(FreeShowClient *);
  void setSongLyrics(const SongLyrics &);
  const SongLyrics &getSongLyrics() const noexcept { return lyrics; }
  void setArrangement(const SongArrangementSnapshot &);
  void setDuration(double seconds);
  void setTransportProviders(std::function<double()> position,
                             std::function<bool()> playing = {});
  void refreshThemeColours();
  std::function<void(const SongLyrics &)> onChanged;

  void paint(juce::Graphics &) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &) override;

private:
  friend struct LyricsPanelTestAccess;
  class SongMap;
  class DestinationPicker;
  struct Placement {
    juce::String id;
    juce::Point<float> point;
    juce::Rectangle<float> preview;
    int lane = 0;
  };
  void timerCallback() override;
  void rebuildMapLayout();
  void paintMap(juce::Graphics &);
  void selectMark(const juce::String &);
  void showMapMenu(juce::Point<float>, const juce::String &markId = {});
  void showDestinationPicker(double seconds, const juce::String &markId = {});
  void editMarkTime(const juce::String &);
  void editMarkName(const juce::String &);
  void refreshCatalogue();
  void loadShow(const juce::String &, bool updateBinding);
  void rebuildShowSelectors();
  void updateDetail();
  void changed();
  void testConnection();
  void testSelected();
  void moveMark(const juce::String &, double seconds, bool notify);
  playworship::lyrics::Mark *findMark(const juce::String &);
  const playworship::lyrics::Mark *findMark(const juce::String &) const;
  const playworship::lyrics::Layout *selectedLayout() const;
  juce::String reviewReason(const playworship::lyrics::Mark &) const;
  juce::String previewText(const playworship::lyrics::Mark &) const;
  juce::String destinationDescription(const playworship::lyrics::Mark &) const;
  juce::String hitMark(juce::Point<float>, bool pointOnly = false) const;
  juce::Point<int> markerScreenPoint(const juce::String &) const;
  float timeToX(double) const;
  double xToTime(float) const;

  SongLyrics lyrics;
  SongArrangementSnapshot arrangement;
  FreeShowClient *client = nullptr;
  playworship::lyrics::ShowCatalog catalogue;
  std::vector<playworship::lyrics::ShowSummary> shows;
  std::vector<Placement> placements;
  double duration = 1.0;
  std::function<double()> positionProvider;
  std::function<bool()> playingProvider;
  juce::String selectedId, statusText, catalogueError;
  juce::String dragId;
  double dragStartSeconds = 0.0;
  bool dragChanged = false, updating = false, settingsVisible = false;
  bool loadingCatalogue = false;
  unsigned int catalogueRequest = 0;
  juce::uint64 lastReviewRevision = 0;

  juce::Viewport viewport;
  std::unique_ptr<SongMap> map;
  juce::TextButton connectionButton{"FreeShow"}, testConnectionButton{juce::String::fromUTF8("Probar conexión")};
  juce::TextButton refreshButton{"Actualizar"}, addButton{"+ Marca"};
  juce::TextButton testButton{"Probar en FreeShow"}, detailMenuButton{juce::String::fromUTF8("•••")};
  juce::ToggleButton enabledToggle{juce::String::fromUTF8("Seguir reproducción")};
  juce::Label hostLabel, portLabel, tokenLabel, showLabel, layoutLabel;
  juce::TextEditor hostEditor, portEditor, tokenEditor;
  AdaptiveComboBox showCombo, layoutCombo;
  juce::Rectangle<int> titleBounds, statusBounds, helperBounds, detailBounds;
  std::unique_ptr<class AppModal> inputModal;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LyricsPanel)
};
#endif
