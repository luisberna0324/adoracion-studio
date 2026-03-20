#pragma once

#include "../UiCommon.h"
#include "../../data/SongData.h"
#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

class ChannelStripLookAndFeel;
class ChannelStripCard;
class MTControlsAudioProcessor;

class ChannelGridPanel final : public juce::Component {
public:
  ChannelGridPanel(ChannelStripLookAndFeel &lookAndFeelRef,
                   MTControlsAudioProcessor &processorRef);
  ~ChannelGridPanel() override;

  void setSong(const ImportedSongData &song);
  void nudgeFaders(float delta);
  void refreshThemeColours();

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  class StripSlot;

  ChannelStripLookAndFeel &lookAndFeel;
  MTControlsAudioProcessor &processor;
  juce::Viewport viewport;
  juce::Component content;
  std::vector<std::unique_ptr<StripSlot>> slots;
  std::vector<bool> adjustableStems;
  std::vector<bool> clickGuideStems;
};
