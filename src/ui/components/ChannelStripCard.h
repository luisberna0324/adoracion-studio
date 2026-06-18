#pragma once

#include "../UiCommon.h"
#include "ChannelStripLookAndFeel.h"
#include "PowerButton.h"
#include "StereoVUMeter.h"
#include <JuceHeader.h>

#include <functional>

class ChannelStripCard : public juce::Component, private juce::Timer {
public:
  struct Layout {
    int width = 90;
    int height = 244;
    int paddingX = 12;
    int paddingBottom = 12;
    int faderWidth = 26;
    int faderHeight = 150;
    int faderMeterGap = 8;
    int soloWidth = 65;
    int soloHeight = 22;
    int buttonGap = 3;
    int buttonToBandGap = 6;
    int channelBandHeight = 16;
    int knobSize = 34;
    int knobTop = 8;
    int meterToKnobGap = 10;

    static Layout createCompact() {
      Layout l;
      l.width = 72;
      l.height = 276;
      l.faderWidth = 22;
      l.faderHeight = 150;
      l.faderMeterGap = 5;
      l.soloWidth = 54;
      l.soloHeight = 18;
      l.buttonGap = 2;
      l.buttonToBandGap = 4;
      l.channelBandHeight = 14;
      l.knobSize = 30;
      l.knobTop = 6;
      return l;
    }
  };

  ChannelStripCard(const juce::String &channelName,
                   ChannelStripLookAndFeel &lookAndFeelRef,
                   StereoVUMeter::LevelProvider levelProvider,
                   bool isCompact = false);
  ~ChannelStripCard() override;

  void paint(juce::Graphics &g) override;
  void paintOverChildren(juce::Graphics &g) override;
  void resized() override;

  const juce::String &getChannelName() const;
  const Layout &getLayout() const { return layout; }
  juce::Rectangle<int> getGainKnobBounds() const { return gainKnob.getBounds(); }
  void setLayoutScale(float scale);
  void setVolume(float volume);
  void setGain(float gain);
  void setMuteState(bool muted);
  void setSoloState(bool soloed);
  void setSoloEnabled(bool enabled);
  void setSoloVisible(bool visible);
  void setMuteVisible(bool visible);
  void setGainVisible(bool visible);
  void setPowerVisible(bool visible);
  void setPowerState(bool poweredOn);
  bool isPowerOn() const { return powerButton.isPoweredOn(); }
  void setMuteAlignedToFader(bool aligned);
  void setChannelBandColours(juce::Colour bandColour, juce::Colour textColour);
  void setCardBgColour(juce::Colour colour);
  void refreshThemeColours();
  void setOnVolumeChanged(std::function<void(float)> cb);
  void setOnVolumeDragEnded(std::function<void(float)> cb);
  void setOnGainChanged(std::function<void(float)> cb);
  void setOnGainDragEnded(std::function<void(float)> cb);
  void setOnMuteChanged(std::function<void(bool)> cb);
  void setOnSoloChanged(std::function<void(bool)> cb);
  void setOnPowerToggled(std::function<void(bool)> cb);
  void setMidiLearnTargetIds(int volumeTargetId, int gainTargetId,
                             int muteTargetId, int soloTargetId,
                             int powerTargetId);
  void setMidiLearnSelectable(bool selectable);
  void setMidiLearnPendingTarget(int targetId);
  void setMidiLearnBindingLabelProvider(std::function<juce::String(int)> cb);
  void setOnMidiLearnTargetSelected(std::function<void(int)> cb);
  bool hasMidiLearnTarget(int targetId) const;
  bool hasAnyMidiLearnTarget() const;

private:
  class LearnAwareSlider final : public juce::Slider {
  public:
    std::function<void()> onPointerDown;
    std::function<void()> onDragEnd;

    void mouseDown(const juce::MouseEvent &event) override {
      juce::Slider::mouseDown(event);
      if (onPointerDown)
        onPointerDown();
    }

    void mouseUp(const juce::MouseEvent &event) override {
      juce::Slider::mouseUp(event);
      if (onDragEnd)
        onDragEnd();
    }
  };

  Layout baseLayout;
  Layout layout;
  float layoutScale = 1.0f;
  ChannelStripLookAndFeel &lookAndFeel;
  juce::String name;

  juce::Label volLabel;
  juce::Label gainLabel;

  LearnAwareSlider volSlider;
  LearnAwareSlider gainKnob;
  PowerButton powerButton;
  StereoVUMeter meter;
  juce::TextButton soloButton;
  juce::TextButton muteButton;
  juce::Colour cardBgColour;
  juce::Colour channelBandColour;
  juce::Colour channelBandTextColour;
  std::function<void(float)> onVolumeChanged;
  std::function<void(float)> onVolumeDragEnded;
  std::function<void(float)> onGainChanged;
  std::function<void(float)> onGainDragEnded;
  std::function<void(bool)> onMuteChanged;
  std::function<void(bool)> onSoloChanged;
  std::function<void(bool)> onPowerToggled;
  bool soloVisible = true;
  bool muteVisible = true;
  bool gainVisible = true;
  bool powerVisible = false;
  bool muteAlignedToFader = false;
  bool midiLearnSelectable = false;
  int midiLearnPendingTargetId = -1;
  int midiVolumeTargetId = -1;
  int midiGainTargetId = -1;
  int midiMuteTargetId = -1;
  int midiSoloTargetId = -1;
  int midiPowerTargetId = -1;
  bool suppressVolumeCallback = false;
  std::function<juce::String(int)> midiLearnBindingLabelProvider;
  std::function<void(int)> onMidiLearnTargetSelected;

  void timerCallback() override;
  void updateMidiLearnBlinkTimer();
  void refreshMidiLearnTooltips();

  void configureLabel(juce::Label &label, const juce::String &text, float size,
                      float kerning, bool uppercase);
  void configureKnob(juce::Slider &slider, double value);
  void clearLookAndFeelFromControls();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripCard)
};
