#pragma once

#include "../UiCommon.h"
#include "ChannelStripLookAndFeel.h"
#include "PowerButton.h"
#include "StereoVUMeter.h"
#include <JuceHeader.h>

#include <functional>

class ChannelStripCard : public juce::Component {
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
  void setOnGainChanged(std::function<void(float)> cb);
  void setOnMuteChanged(std::function<void(bool)> cb);
  void setOnSoloChanged(std::function<void(bool)> cb);
  void setOnPowerToggled(std::function<void(bool)> cb);

private:
  Layout baseLayout;
  Layout layout;
  float layoutScale = 1.0f;
  ChannelStripLookAndFeel &lookAndFeel;
  juce::String name;

  juce::Label volLabel;
  juce::Label gainLabel;

  juce::Slider volSlider;
  juce::Slider gainKnob;
  PowerButton powerButton;
  StereoVUMeter meter;
  juce::TextButton soloButton;
  juce::TextButton muteButton;
  juce::Colour cardBgColour;
  juce::Colour channelBandColour;
  juce::Colour channelBandTextColour;
  std::function<void(float)> onVolumeChanged;
  std::function<void(float)> onGainChanged;
  std::function<void(bool)> onMuteChanged;
  std::function<void(bool)> onSoloChanged;
  std::function<void(bool)> onPowerToggled;
  bool soloVisible = true;
  bool muteVisible = true;
  bool gainVisible = true;
  bool powerVisible = false;
  bool muteAlignedToFader = false;

  void configureLabel(juce::Label &label, const juce::String &text, float size,
                      float kerning, bool uppercase);
  void configureKnob(juce::Slider &slider, double value);
  void clearLookAndFeelFromControls();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripCard)
};
