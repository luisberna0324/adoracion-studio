#pragma once

#include "../UiCommon.h"
#include "../../data/AppSettings.h"
#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

class SettingsPanel final : public juce::Component,
                            public juce::ChangeListener {
public:
  ~SettingsPanel() override;

  void changeListenerCallback(juce::ChangeBroadcaster* source) override;
  using ThemeChanged = std::function<void(ThemePreset)>;
  using LanguageChanged = std::function<void(AppLanguage)>;
  using ExpandLaunchChanged = std::function<void(bool)>;
  using PrimeImportChanged = std::function<void(bool)>;
  using StemOptimizationChanged = std::function<void(bool)>;
  using PadsCrossfadeChanged = std::function<void(PadsCrossfadePreset)>;
  using MetronomeClickStyleChanged =
      std::function<void(MetronomeClickStyle)>;
  using CloseRequested = std::function<void()>;
  using LogoutRequested = std::function<void()>;
  using RoutingDefaultsChanged =
      std::function<void(const juce::String &,
                         const std::vector<CategoryRoutingDefaults> &)>;

  struct SessionSummary {
    juce::String uid;
    juce::String email;
    juce::String plan;
    juce::String subscriptionStatus;
    juce::String expiresAt;
    bool allowed = true;
    juce::String reasonCode;
    juce::String reason;
    juce::String licenseKey;
  };

  SettingsPanel(ThemePreset current, ThemeChanged cb, AppLanguage language,
                LanguageChanged languageCb, bool expandWindowOnLaunch,
                ExpandLaunchChanged expandCb, bool primeImportEnabled,
                PrimeImportChanged primeImportCb, bool stemOptimizationEnabled,
                StemOptimizationChanged stemOptimizationCb,
                PadsCrossfadePreset padsCrossfadePreset,
                PadsCrossfadeChanged padsCrossfadeCb,
                MetronomeClickStyle metronomeClickStyle,
                MetronomeClickStyleChanged metronomeClickStyleCb,
                CloseRequested closeCb,
                SessionSummary sessionSummary,
                LogoutRequested logoutCb,
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
  juce::Component audioPage;
  juce::Component routingPage;
  std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSetupComp;
  juce::Label themeLabel;
  juce::ComboBox themeBox;
  juce::Label languageLabel;
  juce::ComboBox languageBox;
  juce::Label expandLaunchLabel;
  juce::ToggleButton expandLaunchToggle;
  juce::Label primeImportLabel;
  juce::ToggleButton primeImportToggle;
  juce::Label stemOptimizationLabel;
  juce::ToggleButton stemOptimizationToggle;
  juce::Label padsCrossfadeLabel;
  juce::ComboBox padsCrossfadeBox;
  juce::Label metronomeClickStyleLabel;
  juce::ComboBox metronomeClickStyleBox;
  juce::Label sessionSectionLabel;
  juce::Label sessionUidLabel;
  juce::Label sessionUidValue;
  juce::Label sessionEmailLabel;
  juce::Label sessionEmailValue;
  juce::Label sessionPlanLabel;
  juce::Label sessionPlanValue;
  juce::Label sessionStatusLabel;
  juce::Label sessionStatusValue;
  juce::Label sessionExpiresLabel;
  juce::Label sessionExpiresValue;
  juce::Label sessionLicenseLabel;
  juce::Label sessionLicenseValue;
  juce::Label actionsSectionLabel;
  juce::Label actionLogoutLabel;
  juce::Label actionInfoLabel;
  juce::HyperlinkButton infoLinkButton{
      "Saber mas", juce::URL("https://playwordship.gumroad.com/")};
  juce::TextButton logoutButton;
  juce::Label routingSectionLabel;
  juce::Label routingHintLabel;
  juce::Viewport routingViewport;
  juce::Component routingContent;
  ThemeChanged onThemeChanged;
  LanguageChanged onLanguageChanged;
  ExpandLaunchChanged onExpandLaunchChanged;
  PrimeImportChanged onPrimeImportChanged;
  StemOptimizationChanged onStemOptimizationChanged;
  PadsCrossfadeChanged onPadsCrossfadeChanged;
  MetronomeClickStyleChanged onMetronomeClickStyleChanged;
  LogoutRequested onLogoutRequested;
  SessionSummary session;
  RoutingDefaultsChanged onRoutingDefaultsChanged;
  std::vector<CategoryRoutingDefaults> routingDefaults;
  int availableOutputChannels = 2;
  std::vector<std::unique_ptr<RoutingRow>> routingRows;
  std::vector<size_t> routingRowCategoryIndices;
  juce::AudioDeviceManager *deviceManager = nullptr;
  juce::String currentDeviceKey;

  void refreshRoutingForDevice();
  void setRoutingDefaultsLocal(const std::vector<CategoryRoutingDefaults> &next);
  void layoutGeneralPage();
  void layoutAudioPage();
  void layoutRoutingPage();
  void applyLanguageTexts();
  static juce::String formatIsoDateTime(const juce::String &isoDateTime);
};
