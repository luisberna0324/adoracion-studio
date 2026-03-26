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
  using PrimeImportChanged = std::function<void(bool)>;
  using CloseRequested = std::function<void()>;
  using RoutingDefaultsChanged =
      std::function<void(const juce::String &,
                         const std::vector<CategoryRoutingDefaults> &)>;

  SettingsPanel(ThemePreset current, bool expandWindowOnLaunch, ThemeChanged cb,
                ExpandLaunchChanged expandCb, bool primeImportEnabled,
                PrimeImportChanged primeImportCb, CloseRequested closeCb,
                std::vector<CategoryRoutingDefaults> routingDefaults,
                int availableOutputChannels,
                RoutingDefaultsChanged routingCb,
                juce::AudioDeviceManager *deviceManagerToUse,
                int maxInputChannels,
                int maxOutputChannels);

  void paint(juce::Graphics &g) override;
  void resized() override;
  void refreshThemeColours();

private:
  class SettingsTabs final : public juce::TabbedComponent {
  public:
    using juce::TabbedComponent::TabbedComponent;
    std::function<void(int, const juce::String &)> onTabChanged;

    void currentTabChanged(int newCurrentTabIndex,
                           const juce::String &newCurrentTabName) override {
      if (onTabChanged)
        onTabChanged(newCurrentTabIndex, newCurrentTabName);
    }
  };

  class RoutingRow;

  juce::Label title;
  SettingsTabs tabs{juce::TabbedButtonBar::TabsAtTop};
  juce::Component generalPage;
  juce::Component routingPage;
  juce::Component audioPage;
  juce::Label deviceLabel;
  juce::ComboBox deviceBox;
  juce::Label themeLabel;
  juce::ComboBox themeBox;
  juce::Label expandLaunchLabel;
  juce::ToggleButton expandLaunchToggle;
  juce::Label primeImportLabel;
  juce::ToggleButton primeImportToggle;
  juce::Label routingSectionLabel;
  juce::Viewport routingViewport;
  juce::Component routingContent;
  std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSelector;
  juce::Label audioUnavailableLabel;
  juce::TextButton closeButton;
  ThemeChanged onThemeChanged;
  ExpandLaunchChanged onExpandLaunchChanged;
  PrimeImportChanged onPrimeImportChanged;
  CloseRequested onClose;
  RoutingDefaultsChanged onRoutingDefaultsChanged;
  std::vector<CategoryRoutingDefaults> routingDefaults;
  int availableOutputChannels = 2;
  std::vector<std::unique_ptr<RoutingRow>> routingRows;
  std::vector<size_t> routingRowCategoryIndices;
  juce::AudioDeviceManager *deviceManager = nullptr;
  juce::String currentDeviceKey;
  struct DeviceOption {
    juce::String typeName;
    juce::String deviceName;
  };
  std::vector<DeviceOption> deviceOptions;

  void closeSelf();
  void refreshDeviceOptions();
  void applyDeviceSelection(int optionIndex);
  void refreshRoutingForDevice();
  void setRoutingDefaultsLocal(const std::vector<CategoryRoutingDefaults> &next);
  void layoutGeneralPage();
  void layoutRoutingPage();
  void layoutAudioPage();
};
