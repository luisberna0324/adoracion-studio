#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>
#include <utility>

class SettingsPanel final : public juce::Component {
public:
  using ThemeChanged = std::function<void(ThemePreset)>;
  using ExpandLaunchChanged = std::function<void(bool)>;
  using CloseRequested = std::function<void()>;

  SettingsPanel(ThemePreset current, bool expandWindowOnLaunch, ThemeChanged cb,
                  ExpandLaunchChanged expandCb, CloseRequested closeCb);

  void paint(juce::Graphics &g) override;
  void resized() override;
  void refreshThemeColours();

private:
  juce::Label title;
  juce::Label themeLabel;
  juce::ComboBox themeBox;
  juce::Label expandLaunchLabel;
  juce::ToggleButton expandLaunchToggle;
  juce::TextButton closeButton;
  ThemeChanged onThemeChanged;
  ExpandLaunchChanged onExpandLaunchChanged;
  CloseRequested onClose;

  void closeSelf();
};
