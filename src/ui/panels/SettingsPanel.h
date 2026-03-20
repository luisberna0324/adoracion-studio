#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>
#include <utility>

class SettingsPanel final : public juce::Component {
public:
  using ThemeChanged = std::function<void(ThemePreset)>;
  using CloseRequested = std::function<void()>;

  SettingsPanel(ThemePreset current, ThemeChanged cb, CloseRequested closeCb);

  void paint(juce::Graphics &g) override;
  void resized() override;
  void refreshThemeColours();

private:
  juce::Label title;
  juce::Label themeLabel;
  juce::ComboBox themeBox;
  juce::TextButton closeButton;
  ThemeChanged onThemeChanged;
  CloseRequested onClose;

  void closeSelf();
};
