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
  using PurgeLocalDataRequested = std::function<void()>;
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
    juce::String accountType;
    bool isOverride = false;
    int usedDevices = 0, maxDevices = 0, syncedSongCount = 0, maxSyncedSongs = 0;
    juce::int64 usedStorageBytes = 0, maxStorageBytes = 0;
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
                PurgeLocalDataRequested purgeLocalDataCb,
                std::vector<CategoryRoutingDefaults> routingDefaults,
                int availableOutputChannels,
                RoutingDefaultsChanged routingCb,
                juce::AudioDeviceManager *deviceManagerToUse,
                int maxInputChannels,
                int maxOutputChannels);

  void paint(juce::Graphics &g) override;
  void resized() override;
  void refreshThemeColours();
  void updateSessionSummary(SessionSummary sessionSummary);

private:
  class SettingsTabButton final : public juce::TextButton {
  public:
    enum class Position { First, Middle, Last };

    void paintButton(juce::Graphics &, bool, bool) override;
    void setPosition(Position newPosition) { position = newPosition; }
    void setActive(bool shouldBeActive) {
      active = shouldBeActive;
      repaint();
    }

  private:
    bool active = false;
    Position position = Position::Middle;
  };
  class SettingsCloseButton final : public juce::TextButton {
  public:
    void paintButton(juce::Graphics &, bool, bool) override;
  };
  class SettingsLogoutButton final : public juce::TextButton {
  public:
    void paintButton(juce::Graphics &, bool, bool) override;
  };
  class SettingsCopyButton final : public juce::TextButton,
                                   private juce::Timer {
  public:
    SettingsCopyButton();
    void setValueToCopy(const juce::String &value);
    void paintButton(juce::Graphics &, bool, bool) override;

  private:
    void timerCallback() override;
    juce::String valueToCopy;
    bool showingConfirmation = false;
  };
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
  class SettingsCard;
  class SettingsRowSeparator;

  juce::Label title;
  SettingsCloseButton closeButton;
  SettingsTabButton generalTabButton;
  SettingsTabButton accountTabButton;
  SettingsTabButton audioTabButton;
  SettingsTabButton routingTabButton;
  SettingsTabs tabs{juce::TabbedButtonBar::TabsAtLeft};
  juce::Component generalPage;
  juce::Component accountPage;
  juce::Component audioPage;
  juce::Component routingPage;
  std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSetupComp;
  juce::Label themeLabel;
  IpadNativeComboBox themeBox;
  juce::Label languageLabel;
  IpadNativeComboBox languageBox;
  juce::Label expandLaunchLabel;
  juce::ToggleButton expandLaunchToggle;
  juce::Label primeImportLabel;
  juce::ToggleButton primeImportToggle;
  juce::Label stemOptimizationLabel;
  juce::ToggleButton stemOptimizationToggle;
  juce::Label padsCrossfadeLabel;
  IpadNativeComboBox padsCrossfadeBox;
  juce::Label metronomeClickStyleLabel;
  IpadNativeComboBox metronomeClickStyleBox;
  juce::Label sessionSectionLabel;
  juce::Label sessionUidLabel;
  juce::Label sessionUidValue;
  SettingsCopyButton sessionUidCopyButton;
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
  SettingsCopyButton sessionLicenseCopyButton;
  juce::Label sessionVersionLabel;
  juce::Label sessionVersionValue;
  juce::Label sessionQuotaLabel;
  juce::Label sessionQuotaValue;
  juce::Label actionsSectionLabel;
  juce::Label actionLogoutLabel;
  juce::Label actionPurgeLabel;
  juce::Label actionInfoLabel;
  juce::HyperlinkButton infoLinkButton{
      "Saber mas", juce::URL("https://playwordship.gumroad.com/")};
  SettingsLogoutButton logoutButton;
  SettingsLogoutButton purgeButton;
  std::unique_ptr<SettingsCard> appearanceCard;
  std::unique_ptr<SettingsCard> playbackCard;
  std::unique_ptr<SettingsCard> sessionCard;
  std::unique_ptr<SettingsCard> actionsCard;
  std::unique_ptr<SettingsRowSeparator> sessionIdentitySeparator;
  std::unique_ptr<SettingsRowSeparator> actionsTitleSeparator;
  std::unique_ptr<SettingsRowSeparator> actionsRowSeparator;
  std::vector<std::unique_ptr<SettingsRowSeparator>> generalRowSeparators;
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
  CloseRequested onCloseRequested;
  LogoutRequested onLogoutRequested;
  PurgeLocalDataRequested onPurgeLocalDataRequested;
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
  void layoutAccountPage();
  void layoutAudioPage();
  void layoutRoutingPage();
  void applyLanguageTexts();
  void updateTabButtonStates(int activeIndex);
  static juce::String formatIsoDateTime(const juce::String &isoDateTime);
};
