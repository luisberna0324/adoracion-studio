#pragma once

#include "PluginProcessor.h"
#include <JuceHeader.h>

class ChannelStripLookAndFeel : public juce::LookAndFeel_V4 {
public:
  void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, const float rotaryStartAngle,
                        const float rotaryEndAngle, juce::Slider &) override;

  void drawLinearSlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, float minSliderPos, float maxSliderPos,
                        const juce::Slider::SliderStyle style,
                        juce::Slider &slider) override;

  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &backgroundColour,
                            bool isMouseOverButton, bool isButtonDown) override;

  juce::Font getTextButtonFont(juce::TextButton &, int buttonHeight) override;
};

class StereoVUMeter : public juce::Component, private juce::Timer {
public:
  struct Layout {
    int scaleWidth = 14;
    int gap = 3;
    int columnWidth = 20;
    int peakLedSize = 7;
    int trackPadding = 4;
    int segmentWidth = 12;
    int segmentHeight = 4;
    int segmentGap = 1;
    int segments = 30;
    int getTrackHeight() const {
      return (segments * segmentHeight) + ((segments - 1) * segmentGap) +
             (trackPadding * 2);
    }
    int getBlockWidth() const { return scaleWidth + gap + columnWidth; }
    int getBlockHeight() const { return peakLedSize + gap + getTrackHeight(); }

    static Layout createCompact() {
      Layout l;
      l.segmentWidth = 10;
      l.segmentHeight = 3;
      // Make column width a bit smaller to fit the 10px segments:
      l.columnWidth = 16;
      return l;
    }
  };

  using LevelProvider = std::function<std::pair<float, float>()>;
  using MuteProvider = std::function<bool()>;

  StereoVUMeter(LevelProvider levelProvider, MuteProvider muteProvider,
                bool isCompact = false);
  void paint(juce::Graphics &g) override;

  const Layout &getLayout() const { return layout; }

private:
  Layout layout;
  LevelProvider levels;
  MuteProvider isMuted;

  float levelL = 0.0f;
  double holdUntilL = 0.0;

  void timerCallback() override;
};

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
  void setVolume(float volume);
  void setGain(float gain);
  void setMuteState(bool muted);
  void setSoloState(bool soloed);
  void setSoloEnabled(bool enabled);
  void setSoloVisible(bool visible);
  void setGainVisible(bool visible);
  void setMuteAlignedToFader(bool aligned);
  void setChannelBandColours(juce::Colour bandColour, juce::Colour textColour);
  void setCardBgColour(juce::Colour colour);
  void refreshThemeColours();
  void setOnVolumeChanged(std::function<void(float)> cb);
  void setOnGainChanged(std::function<void(float)> cb);
  void setOnMuteChanged(std::function<void(bool)> cb);
  void setOnSoloChanged(std::function<void(bool)> cb);

private:
  Layout layout;
  ChannelStripLookAndFeel &lookAndFeel;
  juce::String name;

  juce::Label volLabel;
  juce::Label gainLabel;

  juce::Slider volSlider;
  juce::Slider gainKnob;
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
  bool soloVisible = true;
  bool gainVisible = true;
  bool muteAlignedToFader = false;

  void configureLabel(juce::Label &label, const juce::String &text, float size,
                      float kerning, bool uppercase);
  void configureKnob(juce::Slider &slider, double value);
  void clearLookAndFeelFromControls();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripCard)
};

class TopBar;

class MTControlsAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
  explicit MTControlsAudioProcessorEditor(MTControlsAudioProcessor &);
  ~MTControlsAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &key) override;

private:
  class SetlistHandlePanel;

  ChannelStripLookAndFeel stripLookAndFeel;

  std::unique_ptr<TopBar> topBar;
  std::unique_ptr<juce::Component> waveformBar;
  std::unique_ptr<juce::Component> mixerFaderPanel;
  std::unique_ptr<juce::Component> channelGridPanel;
  std::unique_ptr<juce::Component> songListPanel;
  std::unique_ptr<SetlistHandlePanel> setlistHandlePanel;
  int currentThemeId = 1;
  std::function<void()> refreshTopBarTheme;
  bool isSetlistCollapsed = false;
  int currentSongIndex = -1;
  std::function<void()> prevSongAction;
  std::function<void()> nextSongAction;
  bool isWaveformExpanded = false;
  void showSettings(juce::Rectangle<int> anchor,
                    juce::Component *parent);
  void applyThemePreset(int presetId);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MTControlsAudioProcessorEditor)
};
