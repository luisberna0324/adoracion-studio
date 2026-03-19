#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <utility>

class ChannelStripLookAndFeel;
class ChannelStripCard;
class MTControlsAudioProcessor;

class MixerFaderPanel final : public juce::Component {
public:
  using AdjustCallback = std::function<void(float)>;

  MixerFaderPanel(ChannelStripLookAndFeel &lookAndFeelRef,
                  MTControlsAudioProcessor &processorRef);
  ~MixerFaderPanel() override;

  void setOnAdjust(AdjustCallback cb);
  void setSongBpm(double bpm);
  void refreshThemeColours();

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  ChannelStripLookAndFeel &lookAndFeel;
  MTControlsAudioProcessor &processor;
  std::unique_ptr<ChannelStripCard> masterStrip;
  juce::Label titleLabel;
  juce::Label masterLabel;
  juce::TextButton upButton;
  juce::TextButton downButton;
  juce::Rectangle<int> allFadersSection;
  juce::Rectangle<int> masterSection;
  AdjustCallback onAdjust;
  const float step = 0.05f;

  void configureLabel(juce::Label &label, const juce::String &text);
  void configureTitleLabel(juce::Label &label, const juce::String &text);
  void configureButton(juce::TextButton &button, const juce::String &text);
  void clearLookAndFeel();
};
