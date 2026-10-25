#pragma once
#include "../../network/PlaybackSession.h"

#include "../UiCommon.h"
#include <JuceHeader.h>
#include <deque>
#include <functional>
#include "../../data/SongLibraryStore.h"
#include "../learning/LearningGuideModel.h"

class MTControlsAudioProcessor;

class TopBar final : public juce::Component, private juce::Timer {
public:
  explicit TopBar(MTControlsAudioProcessor &processorRef, PlaybackSession *session = nullptr);
  ~TopBar() override;

  void setTransportCallbacks(std::function<void()> playPauseCallback,
                             std::function<void()> stopCallback,
                             std::function<void()> prevSongCallback,
                             std::function<void()> nextSongCallback,
                             std::function<void(bool)> fadeToggleCallback,
                             std::function<void(double)> seekRelativeCallback);

  void setTransportProviders(std::function<bool()> playingProviderFn,
                             std::function<double()> positionProviderFn,
                             std::function<double()> lengthProviderFn,
                             std::function<juce::String()> keyProviderFn,
                             std::function<juce::String()> songIdProviderFn,
                             std::function<const ImportedSongData *()>
                                 songProviderFn);

  void setOnSettings(std::function<void()> cb);
  void setLiveModeControls(std::function<void()> toggle,
                           std::function<bool()> state,
                           std::function<bool()> enabled);
  void setOnSettingsWithAnchor(
      std::function<void(juce::Rectangle<int>, juce::Component *)> cb);
  void setOnPhoneLibrary(std::function<void(juce::Component &)> cb);
  void setOnPhonePads(std::function<void(juce::Component &)> cb);
  void setOnPhonePractice(std::function<void()> cb);
  void setOnSongLyrics(std::function<void()> cb);
  void setOnLearningRequested(std::function<void()> cb);
  juce::Rectangle<int> getLearningTargetBounds(playworship::learning::Anchor) const;
  void triggerPhonePadsPopover();
  void setPhoneOverlayStates(bool libraryOpen, bool padsOpen);
  void setOnMetronomeEnabledChanged(std::function<void(bool)> cb);
  void setOnMidiLearnModeChanged(std::function<void(bool)> cb);
  void setOnMidiLearnTargetSelected(std::function<void(int)> cb);
  void setMidiLearnBindingLabelProvider(std::function<juce::String(int)> cb);
  void setMidiLearnActive(bool active);
  void setMidiLearnPendingTarget(int targetId);
  void setResponsiveProfile(ResponsiveLayoutProfile profile);
  void setMinimalPhoneMode(bool enabled);
  void setPhoneSafeAreaInsets(juce::BorderSize<int> insets);
  void setSystemUsage(double cpuUsage01, double memoryUsage01);
  void triggerWindowModeToggleFromMidi();
  void triggerSettingsFromMidi();
  void triggerSongWindowPopoverFromMidi();
  void triggerKeyPopoverFromMidi();
  void triggerMetronomePopoverFromMidi();

  void refreshThemeColours();

  void paint(juce::Graphics &g) override;
  void paintOverChildren(juce::Graphics &g) override;
  void resized() override;
  void mouseDoubleClick(const juce::MouseEvent &event) override;

private:
  friend struct WaveformEditingTestAccess;
  void refreshTimelineMenu();
  void selectTimelineView(bool sections);
  std::function<void()> onLiveModeToggle;
  std::function<bool()> liveModeState;
  std::function<bool()> liveModeEnabled;
  using IconButton = TransportIconButton;

  class RoundedLabel : public juce::Label {
  public:
    void paint(juce::Graphics &g) override;
  };

  class ClickableLabel final : public RoundedLabel {
  public:
    std::function<void()> onClick;

    void mouseDown(const juce::MouseEvent &) override;
  };

  class BpmSegmentButton final : public juce::Button {
  public:
    BpmSegmentButton();
    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override;
  };

  IconButton prevButton;
  IconButton seekBackButton;
  IconButton playButton;
  IconButton stopButton;
  IconButton seekForwardButton;
  IconButton nextButton;
  IconButton fadeButton;
  /** Maximizar / restaurar ventana (Standalone y hosts que usen ResizableWindow). */
  IconButton windowModeButton;
  IconButton settingsButton;
  IconButton midiLearnButton;
  IconButton tabletMoreButton;
  class DesktopTextButton final : public juce::Button {
  public:
    explicit DesktopTextButton(const juce::String &name) : juce::Button(name) {}
    void paintButton(juce::Graphics &, bool, bool) override;
  } waveformViewButton{"timeline-waveform"}, sectionsViewButton{"timeline-sections"},
      helpButton{"learning-help"}, lyricsButton{"song-lyrics"};
  // The parent paints the song card; this button supplies touch/keyboard access.
  class SongPickerButton final : public juce::Button {
  public:
    SongPickerButton() : juce::Button("songPicker") {}
    void paintButton(juce::Graphics &, bool, bool) override {}
  } songPickerButton;
  IconButton phonePadsButton;

  ClickableLabel timeDisplay;
  ClickableLabel keyDisplay;
  RoundedLabel systemUsageDisplay;
  BpmSegmentButton bpmSegmentButton;
  IconButton metroToggleButton{"metroToggle", SvgIconName::Metronome};

  std::function<void()> onPlayPause;
  std::function<void()> onStop;
  std::function<void()> onPrevSong;
  std::function<void()> onNextSong;
  std::function<void(bool)> onFadeToggle;
  std::function<void(double)> onSeekRelative;
  std::function<void()> onSettings;
  std::function<void(juce::Rectangle<int>, juce::Component *)>
      onSettingsWithAnchor;
  std::function<void(juce::Component &)> onPhoneLibrary;
  std::function<void(juce::Component &)> onPhonePads;
  std::function<void()> onPhonePractice;
  std::function<void()> onSongLyrics;
  std::function<void()> onLearningRequested;
  std::function<void(bool)> onMetronomeEnabledChanged;
  std::function<void(bool)> onMidiLearnModeChanged;
  std::function<void(int)> onMidiLearnTargetSelected;
  std::function<juce::String(int)> midiLearnBindingLabelProvider;
  bool midiLearnActive = false;
  int midiLearnPendingTargetId = -1;

  std::function<bool()> playingProvider;
  std::function<double()> positionProvider;
  std::function<double()> lengthProvider;
  std::function<juce::String()> keyProvider;
  std::function<juce::String()> songIdProvider;
  std::function<const ImportedSongData *()> songProvider;
  bool timelineControlsAvailable() const {
    const auto *song = songProvider ? songProvider() : nullptr;
    return song == nullptr || !song->blankSong || !song->stems.empty();
  }
  bool songLyricsAvailable() const;
  bool lastSongLyricsAvailable = false;
  bool lastTimelineControlsAvailable = true;
  MTControlsAudioProcessor &processor;
  PlaybackSession localSession;
  PlaybackSession &session;
  juce::Rectangle<int> songInfoBounds;
  juce::Image songArtwork;
  juce::String songTitleText;
  juce::String songArtistText;
  double songTextScrollStartMs = 0.0;
  bool songTextOverflows = false;
  juce::String lastSongIdentity;
  juce::String lastArtworkPath;
  juce::String lastArtworkSource;
  juce::Time lastArtworkModifiedTime;
  juce::int64 lastArtworkSize = -1;
  juce::uint32 lastArtworkStatAtMs = 0;
  juce::Colour songAccentColour = UITheme::accent;
  juce::String lastKeyToken;
  juce::String activeKeyLabel;
  int baseKeySemitone = 0;
  int activeKeySemitone = 0;
  int activePitchDelta = 0;
  bool hasKey = false;
  float songTitleFontSize = 13.0f;
  float songArtistFontSize = 9.5f;
  int songArtworkSize = 36;
  int songCardHeight = 48;
  std::deque<double> tapTimes;
  double lastTickTime = 0.0;
  double beatCounter = 0.0;
  int lastBeatIndex = -1;
  float beatFlash = 0.0f;
  bool lastBeatWasDownbeat = true;
  bool lastMetronomeEnabled = false;
  bool lastTopLevelMaximised = false;
  double lastCpuUsage01 = -1.0;
  double lastMemoryUsage01 = -1.0;
  ResponsiveLayoutProfile responsiveProfile = ResponsiveLayoutProfile::Standard;
  bool minimalPhoneMode = false;
  juce::BorderSize<int> phoneSafeAreaInsets;
  int beatsPerBar = 4;
  juce::String timeSignature = "4/4";

  void clearFadeIfActive();
  void applyResponsiveTypography();
  void configureDisplay(juce::Label &label, const juce::String &text);
  void configureMetronomeControls();
  void timerCallback() override;
  void updateBpmDisplay();
  void applyTimeSignature(const juce::String &sigText);
  void setBpmAbsolute(double bpm);
  void adjustBpm(double delta);
  void registerTap();
  void showMetronomePopover();
  void showKeyPopover();
  void showSongWindowPopover();
  void toggleWindowMode();
  void openSettingsPanel();
  void showTabletMorePopover();
  std::pair<bool, bool> morePrimaryActions() const;
  std::unique_ptr<juce::Component> createMorePopoverContent();
  std::function<void()> refreshMorePopover;
  void refreshFromProviders();
  void paintSongInfoCard(juce::Graphics &g);
  juce::Rectangle<int> getSongInfoAnchorBounds() const;
  void setBaseTooltip(juce::Button &component, const juce::String &text);
  void setBaseTooltip(juce::Label &component, const juce::String &text);
  void refreshMidiLearnTooltips();
};
