#pragma once

#include "../UiCommon.h"
#include "../../data/AppSettings.h"
#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

class SettingsPanel final : public juce::Component {
public:
  using ThemeChanged = std::function<void(ThemePreset)>;
  using ExpandLaunchChanged = std::function<void(bool)>;
  using CloseRequested = std::function<void()>;
  using RoutingDefaultsChanged =
      std::function<void(const std::vector<CategoryRoutingDefaults> &)>;

  SettingsPanel(ThemePreset current, bool expandWindowOnLaunch, ThemeChanged cb,
                ExpandLaunchChanged expandCb, CloseRequested closeCb,
                std::vector<CategoryRoutingDefaults> routingDefaults,
                int availableOutputChannels,
                RoutingDefaultsChanged routingCb);

  void paint(juce::Graphics &g) override;
  void resized() override;
  void refreshThemeColours();

private:
  class RoutingRow;

  juce::Label title;
  juce::TextButton generalTab;
  juce::TextButton routingTab;
  juce::Label themeLabel;
  juce::ComboBox themeBox;
  juce::Label expandLaunchLabel;
  juce::ToggleButton expandLaunchToggle;
  juce::Label routingSectionLabel;
  juce::Viewport routingViewport;
  juce::Component routingContent;
  juce::TextButton closeButton;
  ThemeChanged onThemeChanged;
  ExpandLaunchChanged onExpandLaunchChanged;
  CloseRequested onClose;
  RoutingDefaultsChanged onRoutingDefaultsChanged;
  std::vector<CategoryRoutingDefaults> routingDefaults;
  int availableOutputChannels = 2;
  std::vector<std::unique_ptr<RoutingRow>> routingRows;
  int activeSection = 0;

  void closeSelf();
  void setActiveSection(int section);
};
