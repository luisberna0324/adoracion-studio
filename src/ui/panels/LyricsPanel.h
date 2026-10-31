#pragma once

#include "../../data/SongData.h"
#include "../../integrations/FreeShowClient.h"
#include "../UiCommon.h"
#include "TimelineInsertionAnchor.h"
#include "PracticeTimelineRuler.h"
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
  void setTimelineMarksProvider(std::function<std::vector<PracticeTimelineMark>(juce::Range<double>, float)>);
  void refreshThemeColours();
  std::function<void(const SongLyrics &)> onChanged;
  std::function<void()> onConnectionSettingsRequested;
  std::function<void(double)> onTimelinePlayRequested;

  void paint(juce::Graphics &) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &) override;

private:
  friend struct LyricsPanelTestAccess;
  class SongMap;
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
  void showDestinationMenu(double seconds, const juce::String &markId = {});
  juce::PopupMenu buildDestinationMenu(std::vector<playworship::lyrics::Target> &) const;
  juce::PopupMenu buildMarkMenu(const playworship::lyrics::Mark &) const;
  void applyDestination(const playworship::lyrics::Target &, double seconds,
                        const juce::String &markId);
  void placeInsertionAnchor(double seconds);
  void applyZoom(double factor, float mapAnchorX);
  void panTimeline(float deltaX);
  void syncNavigationFromViewport();
  void setTimelineWindow(float start, float span);
  void refreshCatalogue();
  void loadShow(const juce::String &, bool updateBinding);
  void rebuildShowSelectors();
  bool canTestMark(const playworship::lyrics::Mark &) const;
  void changed();
  void testSelected();
  void moveMark(const juce::String &, double seconds, bool notify);
  playworship::lyrics::Mark *findMark(const juce::String &);
  const playworship::lyrics::Mark *findMark(const juce::String &) const;
  juce::String reviewReason(const playworship::lyrics::Mark &) const;
  juce::String previewText(const playworship::lyrics::Mark &) const;
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
  std::function<std::vector<PracticeTimelineMark>(juce::Range<double>, float)> timelineMarksProvider;
  double zoomFactor = 1.0;
  float navigationStart = 0.0f, navigationSpan = 1.0f;
  float timelineWidth = 1.0f, mapOriginOffset = 0.0f;
  int lastNavigationViewX = -1;
  juce::String selectedId, statusText, catalogueError;
  juce::String dragId;
  TimelineInsertionAnchor insertionAnchor;
  bool draggingInsertionAnchor = false;
  double dragStartSeconds = 0.0;
  std::vector<Placement> dragPlacements, settlingFrom;
  juce::Point<float> dragOrigin;
  double settlingStarted = 0.0;
  bool dragChanged = false, updating = false;
  bool loadingCatalogue = false;
  unsigned int catalogueRequest = 0;
  juce::uint64 lastReviewRevision = 0;
  juce::uint64 lastConnectionEpoch = 0;
  bool wasConnected = false;

  juce::Viewport viewport;
  std::unique_ptr<SongMap> map;
  juce::TextButton connectionButton{"FreeShow"};
  juce::TextButton refreshButton{juce::String::fromUTF8("↻")}, addButton{"+"};
  juce::ToggleButton enabledToggle{juce::String::fromUTF8("Seguir reproducción")};
  juce::Label showLabel;
  AdaptiveComboBox showCombo;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LyricsPanel)
};
#endif
