#pragma once

#include "ChannelStripLookAndFeel.h"
#include "StereoVUMeter.h"

#include <JuceHeader.h>

#include <functional>

class ClickDivisionPanel : public juce::Component, private juce::Timer {
public:
  ClickDivisionPanel(ChannelStripLookAndFeel &lookAndFeel,
                     StereoVUMeter::LevelProvider levelProvider);
  ~ClickDivisionPanel() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void refreshThemeColours();

  void setDivision(int division);
  int getDivision() const;
  void setVolume(float volume);
  void setPowerOn(bool powerOn);
  bool isPowerOn() const noexcept { return powerOn; }
  void setMuteState(bool muted);
  void setSoloState(bool soloed);
  void setOnDivisionChanged(std::function<void(int)> cb);
  void setOnVolumeChanged(std::function<void(float)> cb);
  void setOnVolumeDragEnded(std::function<void(float)> cb);
  void setOnPowerChanged(std::function<void(bool)> cb);
  void setOnMuteChanged(std::function<void(bool)> cb);
  void setOnSoloChanged(std::function<void(bool)> cb);
  void setMidiLearnTargetIds(int quarterTargetId, int eighthTargetId,
                             int sixteenthTargetId, int muteTargetId,
                             int soloTargetId);
  void setMidiLearnSelectable(bool selectable);
  void setMidiLearnPendingTarget(int targetId);
  void setMidiLearnBindingLabelProvider(std::function<juce::String(int)> cb);
  void setOnMidiLearnTargetSelected(std::function<void(int)> cb);
  void setLayoutScale(float scale);
  void setBottomContentInset(int inset);

private:
  class LearnAwareButton : public juce::TextButton {
  public:
    using juce::TextButton::TextButton;
    std::function<void()> onPointerDown;

    void mouseDown(const juce::MouseEvent &event) override;
  };

  class DivisionButton final : public LearnAwareButton {
  public:
    DivisionButton(const juce::String &label, int divisionValue);

    void setSelected(bool selected);
    int getDivisionValue() const noexcept { return divisionValue; }

  private:
    int divisionValue = 1;
  };

  static int normalizeDivisionMask(int divisionMask) noexcept;
  bool hasMidiLearnTarget(int targetId) const noexcept;
  void selectMidiLearnTarget(int targetId);
  void updateMidiLearnBlinkTimer();
  void refreshMidiLearnTooltips();
  void drawMidiLearnFrame(juce::Graphics &g, juce::Component &component,
                          int targetId);
  void updateDivisionMaskFromButtons();
  void updateButtonStates();
  void paintOverChildren(juce::Graphics &g) override;
  void timerCallback() override;

  ChannelStripLookAndFeel &lookAndFeel;
  int currentDivisionMask = 1;
  bool powerOn = true;
  bool muted = false;
  bool soloed = false;
  float layoutScale = 1.0f;
  int bottomContentInset = 0;
  bool suppressVolumeCallback = false;
  std::function<void(int)> onDivisionChanged;
  std::function<void(float)> onVolumeChanged;
  std::function<void(float)> onVolumeDragEnded;
  std::function<void(bool)> onPowerChanged;
  std::function<void(bool)> onMuteChanged;
  std::function<void(bool)> onSoloChanged;
  std::function<void(int)> onMidiLearnTargetSelected;
  std::function<juce::String(int)> midiLearnBindingLabelProvider;
  int midiQuarterTargetId = -1;
  int midiEighthTargetId = -1;
  int midiSixteenthTargetId = -1;
  int midiMuteTargetId = -1;
  int midiSoloTargetId = -1;
  int midiLearnPendingTargetId = -1;
  bool midiLearnSelectable = false;

  std::unique_ptr<DivisionButton> btn14;
  std::unique_ptr<DivisionButton> btn18;
  std::unique_ptr<DivisionButton> btn116;
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
  std::unique_ptr<LearnAwareSlider> volumeKnob;
  std::unique_ptr<StereoVUMeter> meter;
  std::unique_ptr<LearnAwareButton> soloButton;
  std::unique_ptr<LearnAwareButton> muteButton;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClickDivisionPanel)
};
