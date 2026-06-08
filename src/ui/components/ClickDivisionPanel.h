#pragma once

#include "ChannelStripLookAndFeel.h"
#include "StereoVUMeter.h"

#include <JuceHeader.h>

#include <functional>

class ClickDivisionPanel : public juce::Component {
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
  void setOnPowerChanged(std::function<void(bool)> cb);
  void setOnMuteChanged(std::function<void(bool)> cb);
  void setOnSoloChanged(std::function<void(bool)> cb);
  void setLayoutScale(float scale);

private:
  class DivisionButton final : public juce::TextButton {
  public:
    DivisionButton(const juce::String &label, int divisionValue);

    void setSelected(bool selected);
    int getDivisionValue() const noexcept { return divisionValue; }

  private:
    int divisionValue = 1;
  };

  static int normalizeDivisionMask(int divisionMask) noexcept;
  void updateDivisionMaskFromButtons();
  void updateButtonStates();

  ChannelStripLookAndFeel &lookAndFeel;
  int currentDivisionMask = 1;
  bool powerOn = true;
  bool muted = false;
  bool soloed = false;
  float layoutScale = 1.0f;
  bool suppressVolumeCallback = false;
  std::function<void(int)> onDivisionChanged;
  std::function<void(float)> onVolumeChanged;
  std::function<void(bool)> onPowerChanged;
  std::function<void(bool)> onMuteChanged;
  std::function<void(bool)> onSoloChanged;

  std::unique_ptr<DivisionButton> btn14;
  std::unique_ptr<DivisionButton> btn18;
  std::unique_ptr<DivisionButton> btn116;
  std::unique_ptr<juce::Slider> volumeKnob;
  std::unique_ptr<StereoVUMeter> meter;
  std::unique_ptr<juce::TextButton> soloButton;
  std::unique_ptr<juce::TextButton> muteButton;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClickDivisionPanel)
};
