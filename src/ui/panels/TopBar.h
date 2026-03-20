#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>
#include <deque>
#include <functional>

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
                             std::function<void(double)> seekRelativeCallback,
                             std::function<void(bool)> monoClickCallback);

  void setTransportProviders(std::function<bool()> playingProviderFn,
                             std::function<double()> positionProviderFn,
                             std::function<double()> lengthProviderFn,
                             std::function<juce::String()> keyProviderFn);

  void setMonoClickProvider(std::function<bool()> monoClickProviderFn);
  void setOnSettings(std::function<void()> cb);
  void setOnSettingsWithAnchor(
      std::function<void(juce::Rectangle<int>, juce::Component *)> cb);

  void refreshThemeColours();

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  class IconButton final : public juce::Button {
  public:
    IconButton(juce::String name, IconGlyph g, bool primary = false);

    void setGlyph(IconGlyph g);
    void setPulseAmount(float amount, bool downbeat);

    void paintButton(juce::Graphics &gr, bool isMouseOverButton,
                     bool isButtonDown) override;

  private:
    IconGlyph glyph;
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
  IconButton settingsButton;

  ClickableLabel timeDisplay;
  juce::Label keyDisplay;
  juce::TextButton monoClickButton;
  juce::TextButton bpmSegmentButton;
  IconButton metroToggleButton{"metroToggle", IconGlyph::Metronome};

  std::function<void()> onPlayPause;
  std::function<void()> onStop;
  std::function<void()> onPrevSong;
  std::function<void()> onNextSong;
  std::function<void(bool)> onFadeToggle;
  std::function<void(double)> onSeekRelative;
  std::function<void(bool)> onMonoClickToggle;
  std::function<void()> onSettings;
  std::function<void(juce::Rectangle<int>, juce::Component *)>
      onSettingsWithAnchor;

  std::function<bool()> playingProvider;
  std::function<double()> positionProvider;
  std::function<double()> lengthProvider;
  std::function<juce::String()> keyProvider;
  std::function<bool()> monoClickProvider;
  MTControlsAudioProcessor &processor;
  std::deque<double> tapTimes;
  double lastTickTime = 0.0;
  double beatCounter = 0.0;
  int lastBeatIndex = -1;
  float beatFlash = 0.0f;
  bool lastBeatWasDownbeat = true;
  int beatsPerBar = 4;
  juce::String timeSignature = "4/4";

  void clearFadeIfActive();
  void configureDisplay(juce::Label &label, const juce::String &text);
  void configureMetronomeControls();
  void timerCallback() override;
  void updateBpmDisplay();
  void applyTimeSignature(const juce::String &sigText);
  void setBpmAbsolute(double bpm);
  void adjustBpm(double delta);
  void registerTap();
  void showMetronomePopover();
  void showSongWindowPopover();
  void refreshFromProviders();
};
