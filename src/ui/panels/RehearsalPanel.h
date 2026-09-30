#pragma once
#include "../UiCommon.h"
#include "../components/ChannelStripCard.h"
#include "../components/ChannelStripLookAndFeel.h"
#include "../../network/PlaybackSession.h"

class RehearsalPanel final : public juce::Component, private juce::Timer {
public:
  class Header final : public juce::Component {
  public:
    Header();
    void paint(juce::Graphics &) override;
    void resized() override;
    HeaderIconButton close{"closeRehearsal", IconGlyph::Close};
    HeaderIconButton mixer{"rehearsalMixer", IconGlyph::More};
  } header;

  RehearsalPanel(PlaybackSession &, ChannelStripLookAndFeel &);
  ~RehearsalPanel() override;
  void refreshThemeColours();
  void refresh();
  void paint(juce::Graphics &) override;
  void resized() override;

private:
  void timerCallback() override { if (isShowing()) refresh(); }
  PlaybackSession &session;
  juce::Viewport viewport;
  juce::Component content;
  juce::Label groupLabel, members, status, levels;
  AdaptiveComboBox groupSelector;
  juce::TextButton normal;
  ChannelStripCard groupStrip, restStrip;
  juce::String groupsSignature;
  juce::StringArray groupIds;
  juce::Array<juce::var> groups;
  std::pair<float, float> meter(bool selected) const;
};
