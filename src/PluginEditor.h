#pragma once

#include "PluginProcessor.h"
#include "data/SongData.h"
#include "ui/components/ChannelStripLookAndFeel.h"
#include <JuceHeader.h>

class TopBar;
class SetlistHandlePanel;

class MTControlsAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
  explicit MTControlsAudioProcessorEditor(MTControlsAudioProcessor &);
  ~MTControlsAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &key) override;

private:
  ChannelStripLookAndFeel stripLookAndFeel;

  std::unique_ptr<TopBar> topBar;
  std::unique_ptr<juce::Component> waveformBar;
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
  ImportedSongData currentLoadedSong;
  bool hasCurrentLoadedSong = false;
  void showSettings(juce::Rectangle<int> anchor,
                    juce::Component *parent);
  void applyThemePreset(int presetId);
  void applyRoutingDefaultsToSong(const ImportedSongData &song);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MTControlsAudioProcessorEditor)
};
