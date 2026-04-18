#pragma once

#include "../../PluginProcessor.h"
#include "../UiCommon.h"
#include "../components/ChannelStripCard.h"
#include "../components/ChannelStripLookAndFeel.h"
#include "../components/NotePadButton.h"
#include "../components/OctaveControl.h"
#include "../components/PowerButton.h"
#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <memory>
#include <thread>

class PadsComponent : public juce::Component, private juce::Timer {
public:
    PadsComponent(MTControlsAudioProcessor& processor, ChannelStripLookAndFeel& laf);
    ~PadsComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;

    void mouseDown(const juce::MouseEvent&) override;

    void refreshThemeColours();

private:
  static constexpr int kNoteCount = 12;
  static constexpr int kPadSize = 70;

  MTControlsAudioProcessor &processor;
  std::array<std::unique_ptr<NotePadButton>, kNoteCount> notePads;
  OctaveControl octaveCtrl;
  PowerButton powerBtn;
  std::unique_ptr<ChannelStripCard> channelStrip;
  std::thread padsLoaderThread;

  int currentOctave = 0;
  int activeNoteIndex = -1;
  bool padsPoweredOn = true;
  std::array<bool, kNoteCount> qwertyWasDown{};
  std::atomic<bool> padsLoading{false};
  float loadingBeatPhase = 0.0f;
  float subtitleAlpha = 1.0f;
  bool lastPackReady = false;
  juce::Rectangle<int> presetCardBounds;
  juce::String presetTitle{"Light Ambient"};
  juce::String presetSubtitle{"Loading..."};

    void timerCallback() override;
    void startPadsLoadingAsync();
    void stopPadsLoadingThread();
    void onPadPressed(int noteIndex);
    void onPadReleased(int noteIndex);
    void applyPowerState(bool on);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadsComponent)
};
