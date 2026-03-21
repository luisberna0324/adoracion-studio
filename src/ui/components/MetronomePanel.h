#pragma once

#include "../../PluginProcessor.h"
#include "../UiCommon.h"
#include <JuceHeader.h>

#include <deque>
#include <functional>

class MetronomePanel final : public juce::Component, private juce::Timer {
public:
  class BpmSegmentButton final : public juce::Button {
  public:
    BpmSegmentButton();

    void setBpmText(juce::String newText);

    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override;

  private:
    juce::String bpmText = "120";
  };

  class MetronomeIconButton final : public juce::Button {
  public:
    MetronomeIconButton();

    void setBeatPulse(float intensity, bool downbeat);

    void paintButton(juce::Graphics &g, bool isMouseOverButton,
                     bool isButtonDown) override;

  private:
    float pulse = 0.0f;
    bool isDownbeat = false;
  };

  class MetronomePopover final : public juce::Component {
  public:
    using TapCallback = std::function<void()>;
    using TimeSignatureCallback = std::function<void(const juce::String &)>;
    using BpmSetCallback = std::function<void(double)>;
    using BpmAdjustCallback = std::function<void(double)>;
    using BpmProvider = std::function<double()>;

    MetronomePopover(const juce::String &timeSig, TapCallback onTapCb,
                     TimeSignatureCallback onTimeSigCb,
                     BpmSetCallback onBpmSetCb,
                     BpmAdjustCallback onBpmAdjustCb,
                     BpmProvider bpmProviderCb);

    void refreshThemeColours();
    void paint(juce::Graphics &g) override;
    void resized() override;

  private:
    juce::TextButton tapButton;
    juce::ComboBox timeSigBox;
    juce::TextButton minusButton;
    juce::TextButton plusButton;
    juce::TextEditor bpmEditor;

    TapCallback onTap;
    TimeSignatureCallback onTimeSignatureChanged;
    BpmSetCallback onBpmSet;
    BpmAdjustCallback onBpmAdjust;
    BpmProvider bpmProvider;

    void syncBpmEditor();
    void submitBpmEditor();
  };

  explicit MetronomePanel(MTControlsAudioProcessor &processorRef);

  void setSongBpm(double bpm);
  void refreshThemeColours();

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  MTControlsAudioProcessor &processor;
  BpmSegmentButton bpmButton;
  MetronomeIconButton iconButton;
  bool manualOverride = false;
  double lastTickTime = 0.0;
  double beatCounter = 0.0;
  int lastBeatIndex = -1;
  float beatFlash = 0.0f;
  bool lastBeatWasDownbeat = true;
  std::deque<double> tapTimes;
  int beatsPerBar = 4;
  juce::String timeSignature = "4/4";

  void showPopover();
  void refreshFromProcessor();
  void updateBpmDisplay();
  void setBpmAbsolute(double bpm);
  void adjustBpm(double delta);
  void registerTap();
  void applyTimeSignature(const juce::String &sigText);

  void timerCallback() override;
};
