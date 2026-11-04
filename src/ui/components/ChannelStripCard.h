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
  void setBottomContentInset(int inset);
  void setVolume(float volume);
  // Group updates may clamp the actively dragged fader as well as its peers.
  void setLinkedVolume(float volume);
  void setFaderLinkButtonVisible(bool visible);
  void setFaderLinkButtonState(bool linked);
  void setFaderLinkButtonEnabled(bool enabled);
  void setFaderLinkButtonTooltip(const juce::String &tooltip);
  void setOnFaderLinkToggled(std::function<void(bool)> cb);
  void setFaderLinked(bool linked);
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
  void setLeadingBoundaryVisible(bool visible) { leadingBoundaryVisible = visible; repaint(); }
  void setSectionAutomationState(bool automated, bool editing);
  void refreshThemeColours();
  void setOnVolumeChanged(std::function<void(float)> cb);
  void setOnVolumeDragStarted(std::function<void()> cb);
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
  class FaderLinkButton final : public juce::TextButton {
  public:
    void paintButton(juce::Graphics &, bool mouseOver, bool buttonDown) override;
  };

  class LearnAwareSlider final : public juce::Slider {
  public:
    std::function<void()> onPointerDown;
    std::function<void()> onPointerDrag;
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

    void mouseDrag(const juce::MouseEvent &event) override {
      juce::Slider::mouseDrag(event);
      if (onPointerDrag)
        onPointerDrag();
    }
  };

  Layout baseLayout;
  Layout layout;
  float layoutScale = 1.0f;
  int bottomContentInset = 0;
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
  FaderLinkButton faderLinkButton;
  bool leadingBoundaryVisible = false;
  juce::Colour cardBgColour;
  juce::Colour channelBandColour;
  juce::Colour channelBandTextColour;
  std::function<void(float)> onVolumeChanged;
  std::function<void()> onVolumeDragStarted;
  std::function<void(bool)> onFaderLinkToggled;
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
  bool faderLinkButtonVisible = false;
  bool faderLinked = false;
  bool midiLearnSelectable = false;
  bool gainValueTooltipVisible = false;
  bool sectionAutomated = false;
  bool sectionAutomationEditing = false;
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
  void updateHighlightTimer();
  void refreshMidiLearnTooltips();

  static juce::String formatGainDecibels(float gain);
  void drawGainValueTooltip(juce::Graphics &g) const;
  void configureLabel(juce::Label &label, const juce::String &text, float size,
                      float kerning, bool uppercase);
  void configureKnob(juce::Slider &slider, double value);
  void clearLookAndFeelFromControls();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripCard)
};
