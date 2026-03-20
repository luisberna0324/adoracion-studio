#pragma once

#include <JuceHeader.h>

class ChannelStripCard;
class ChannelStripLookAndFeel;

class WaveformRowComponent : public juce::Component {
public:
  WaveformRowComponent(ChannelStripLookAndFeel &lookAndFeel,
                       juce::String trackName = "Lluvia",
                       juce::String trackTime = "0:00 / 4:23");
  ~WaveformRowComponent() override;

  void paint(juce::Graphics &g) override;
  void resized() override;

  void setTrackInfo(juce::String trackName, juce::String trackTime);

  int getTargetHeight() const;

private:
  ChannelStripLookAndFeel &lnf;
  static constexpr int kRowHeight = 72;

  class Artwork;
  class PlayButton;
  class WaveformDisplay;

  std::unique_ptr<Artwork> artwork;

  juce::Label nameLabel;
  juce::Label timeLabel;

  std::unique_ptr<PlayButton> playButton;
  std::unique_ptr<WaveformDisplay> waveform;

  juce::Slider volSlider;
  juce::Label volLabel;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformRowComponent)
};
