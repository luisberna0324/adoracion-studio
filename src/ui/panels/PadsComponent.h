#pragma once

#include "../../PluginProcessor.h"
#include "../../network/PlaybackSession.h"
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

class PadsComponent : public juce::Component, private juce::Timer {
public:
    PadsComponent(MTControlsAudioProcessor& processor, ChannelStripLookAndFeel& laf, PlaybackSession *session = nullptr);
    ~PadsComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;

    void mouseDown(const juce::MouseEvent&) override;

    void refreshThemeColours();
    void setResponsiveProfile(ResponsiveLayoutProfile profile);
    void setMidiLearnActive(bool active);
    void setMidiLearnPendingTarget(int targetId);
    void setMidiLearnBindingLabelProvider(std::function<juce::String(int)> cb);
    void setOnMidiLearnTargetSelected(std::function<void(int)> cb);
    /** Ajusta un pad activo a la tonalidad de la canción. */
    // Retunes an active Pad to the next song while preserving audible
    // continuity. PadsEngine crossfades the old and new voices; it never
    // requires the pads bus to go silent first.
    bool followSongKey(const juce::String& songKey, int semitoneOffset = 0);

private:
  static constexpr int kNoteCount = 12;

  MTControlsAudioProcessor &processor;
  PlaybackSession localSession;
  PlaybackSession &session;
  std::array<std::unique_ptr<NotePadButton>, kNoteCount> notePads;
  OctaveControl octaveCtrl;
  PowerButton powerBtn;
  std::unique_ptr<ChannelStripCard> channelStrip;

  int currentOctave = 0;
  int activeNoteIndex = -1;
  bool padsPoweredOn = true;
  bool midiLearnActive = false;
  int midiLearnPendingTargetId = -1;
  std::function<juce::String(int)> midiLearnBindingLabelProvider;
  std::function<void(int)> onMidiLearnTargetSelected;
  std::array<bool, kNoteCount> qwertyWasDown{};
  std::atomic<bool> padsLoading{false};
  float loadingBeatPhase = 0.0f;
  float subtitleAlpha = 1.0f;
  bool lastPackReady = false;
  juce::Rectangle<int> presetCardBounds;
  juce::String presetTitle{"Light Ambient"};
  juce::String presetSubtitle{"Loading..."};
  ResponsiveLayoutProfile responsiveProfile = ResponsiveLayoutProfile::Standard;

    void timerCallback() override;
    void startPadsLoadingAsync();
    void onPadPressed(int noteIndex);
    void onPadReleased(int noteIndex);
    void applyPowerState(bool on);
    void refreshMidiLearnTooltips();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadsComponent)
};
