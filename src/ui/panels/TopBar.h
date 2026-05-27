#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>
#include <deque>
#include <functional>
#include "../../data/SongLibraryStore.h"

class MTControlsAudioProcessor;

class TopBar final : public juce::Component, private juce::Timer {
public:
  explicit TopBar(MTControlsAudioProcessor &processorRef);
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
                             std::function<juce::String()> songIdProviderFn);

  void setOnSettings(std::function<void()> cb);
  void setOnSettingsWithAnchor(
      std::function<void(juce::Rectangle<int>, juce::Component *)> cb);
  void setOnMidiLearnModeChanged(std::function<void(bool)> cb);
  void setOnMidiLearnTargetSelected(std::function<void(int)> cb);
  void setMidiLearnBindingLabelProvider(std::function<juce::String(int)> cb);
  void setMidiLearnActive(bool active);
  void setMidiLearnPendingTarget(int targetId);
  void setResponsiveProfile(ResponsiveLayoutProfile profile);
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
  class IconButton final : public juce::Button {
  public:
    IconButton(juce::String name, SvgIconName iconToUse,
               bool primary = false);
    std::function<void()> onPointerDown;

    void setIcon(SvgIconName iconToUse);
    void setBadgeText(juce::String text);
    void setPulseAmount(float amount, bool downbeat);

    void paintButton(juce::Graphics &gr, bool isMouseOverButton,
                     bool isButtonDown) override;

  protected:
    void mouseDown(const juce::MouseEvent &event) override;

  private:
    SvgIconName icon;
    juce::String badgeText;
    bool isPrimary = false;
    float pulseAmount = 0.0f;
    bool pulseDownbeat = false;
  };

  class ClickableLabel final : public juce::Label {
  public:
    std::function<void()> onClick;

    void mouseDown(const juce::MouseEvent &) override;
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

  ClickableLabel timeDisplay;
  ClickableLabel keyDisplay;
  juce::Label systemUsageDisplay;
  juce::TextButton bpmSegmentButton;
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
  MTControlsAudioProcessor &processor;
  juce::String lastSongTitle;
  juce::String lastKeyToken;
  juce::String activeKeyLabel;
  int baseKeySemitone = 0;
  int activeKeySemitone = 0;
  int activePitchDelta = 0;
  bool hasKey = false;
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
  void refreshFromProviders();
  void setBaseTooltip(IconButton &button, const juce::String &text);
  void refreshMidiLearnTooltips();
};
