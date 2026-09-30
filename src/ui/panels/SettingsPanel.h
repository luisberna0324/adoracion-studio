#pragma once

#include "../UiCommon.h"
#include "../../data/AppSettings.h"
#include "../../network/RemoteSession.h"
#include <JuceHeader.h>

#include <functional>
#include <array>
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
  bool keyPressed(const juce::KeyPress &) override;
  void refreshThemeColours();
  void updateSessionSummary(SessionSummary sessionSummary);
  void setQuotaUnavailable();
  std::function<void()> onShareAudioDiagnostics;
  std::function<void(playworship::remote::RemoteSession::Mode)> onRemoteModeChanged;
  std::function<void(const playworship::remote::DiscoveredServer &)> onRemoteConnect;
  std::function<void(const juce::String &)> onRemoteServerAliasChanged;
  void updateRemoteState(playworship::remote::RemoteSession::Mode mode,
                         const std::vector<playworship::remote::DiscoveredServer> &servers,
                         const juce::String &status,
                         const juce::String &selectedServerName = {});
  void setDiagnosticsBusy(bool busy);

#if JUCE_IOS
  // UIKit reads and mutates the same settings model as the JUCE desktop view.
  struct IOSSettingsState {
    int theme = 0, language = 0, padsCrossfade = 0, clickStyle = 0;
    bool stemOptimization = false, diagnosticsBusy = false, quotaUnavailable = false;
    SessionSummary session;
    juce::String version, remoteAlias, remoteStatus, deviceName;
    playworship::remote::RemoteSession::Mode remoteMode =
        playworship::remote::RemoteSession::Mode::Local;
    std::vector<playworship::remote::DiscoveredServer> servers;
    int selectedServer = -1, selectedDevice = -1, outputChannels = 2;
    juce::StringArray devices, categories;
    std::vector<CategoryRoutingDefaults> routing;
  };
  IOSSettingsState iosState() const;
  void iosSelectTheme(int id);
  void iosSelectLanguage(int id);
  void iosSetStemOptimization(bool enabled);
  void iosSelectPadsCrossfade(int id);
  void iosSelectClickStyle(int id);
  void iosSelectRoutingDevice(int index);
  void iosSetRouting(size_t categoryIndex, int width, int baseChannel);
  void iosSetRemoteMode(playworship::remote::RemoteSession::Mode mode);
  void iosSelectRemoteServer(int index);
  void iosSetRemoteAlias(const juce::String &alias);
  void iosRequestLogout();
  void iosRequestPurge();
  void iosShareDiagnostics();
#endif

private:
  friend struct SettingsPanelTestAccess;
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
  enum class Destination { Home, Appearance, Playback, Library, Audio, Routing, Remote, Account, Diagnostics };
  class SettingsHomePage;

  bool usesGroupedNavigation() const;
  void showSettingsPage(Destination destination);
  void layoutMobileGeneralPage();
  int mobileGeneralCardHeight() const;
  int mobilePageContentHeight(int width) const;
  juce::String pageTitle() const;
  Destination destination = Destination::Home;
  int homeScrollY = 0;
  std::unique_ptr<SettingsHomePage> homePage;
  HeaderIconButton backButton{"settingsBack", IconGlyph::ArrowLeft};

  juce::Label title;
  HeaderIconButton closeButton{"settingsClose", IconGlyph::Close};
  SettingsTabButton generalTabButton;
  SettingsTabButton accountTabButton;
  SettingsTabButton audioTabButton;
  SettingsTabButton routingTabButton;
  SettingsTabButton diagnosticsTabButton;
  SettingsTabs tabs{juce::TabbedButtonBar::TabsAtLeft};
  juce::Viewport settingsViewport;
  juce::Component generalPage;
  juce::Component accountPage;
  juce::Component audioPage;
  juce::Component routingPage;
  juce::Component diagnosticsPage;
  juce::Label diagnosticsSectionLabel;
  juce::Label diagnosticsHint;
  juce::Label diagnosticsPrivacyHint;
  juce::TextButton diagnosticsButton;
  bool diagnosticsBusy = false;
  std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSetupComp;
  juce::Label audioHelp;
  juce::Label themeLabel;
  juce::Label themeHelp;
  AdaptiveComboBox themeBox;
  juce::Label languageLabel;
  juce::Label languageHelp;
  AdaptiveComboBox languageBox;
  juce::Label expandLaunchLabel;
  juce::Label expandLaunchHelp;
  juce::ToggleButton expandLaunchToggle;
  juce::Label primeImportLabel;
  juce::Label primeImportHelp;
  juce::ToggleButton primeImportToggle;
  juce::Label stemOptimizationLabel;
  juce::Label stemOptimizationHelp;
  juce::ToggleButton stemOptimizationToggle;
  juce::Label padsCrossfadeLabel;
  juce::Label padsCrossfadeHelp;
  AdaptiveComboBox padsCrossfadeBox;
  juce::Label metronomeClickStyleLabel;
  juce::Label metronomeClickStyleHelp;
  AdaptiveComboBox metronomeClickStyleBox;
  juce::Label remoteControlLabel;
  juce::Label remoteControlHelp;
  juce::Label remoteTargetHelp;
  juce::Component remoteModeControl;
  juce::TextButton remoteLocalButton, remoteClientButton, remoteServerButton;
  AdaptiveComboBox remoteServersBox;
  juce::TextEditor remoteServerNameEditor;
  juce::String remoteServerAlias;
  juce::Label remoteStatus;
  std::vector<playworship::remote::DiscoveredServer> remoteServers;
  playworship::remote::RemoteSession::Mode remoteMode = playworship::remote::RemoteSession::Mode::Local;
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
  juce::Label sessionDevicesQuotaLabel;
  juce::Label sessionDevicesQuotaValue;
  juce::Label sessionSongsQuotaLabel;
  juce::Label sessionSongsQuotaValue;
  juce::Label sessionStorageQuotaLabel;
  juce::Label sessionStorageQuotaValue;
  bool quotaFetchFailed = false;
  juce::Label actionsSectionLabel;
  juce::Label actionLogoutLabel;
  juce::Label actionLogoutHelp;
  juce::Label actionPurgeLabel;
  juce::Label actionPurgeHelp;
  juce::Label actionInfoLabel;
  juce::Label actionInfoHelp;
  juce::HyperlinkButton infoLinkButton{
      "Soporte", juce::URL("mailto:hola@playworship.app")};
  SettingsLogoutButton logoutButton;
  SettingsLogoutButton purgeButton;
  std::unique_ptr<SettingsCard> appearanceCard;
  std::unique_ptr<SettingsCard> playbackCard;
  std::unique_ptr<SettingsCard> sessionCard;
  std::unique_ptr<SettingsCard> actionsCard;
  std::unique_ptr<SettingsCard> diagnosticsCard;
  std::unique_ptr<SettingsRowSeparator> sessionIdentitySeparator;
  std::unique_ptr<SettingsRowSeparator> actionsTitleSeparator;
  std::unique_ptr<SettingsRowSeparator> actionsRowSeparator;
  std::vector<std::unique_ptr<SettingsRowSeparator>> generalRowSeparators;
  juce::Label routingSectionLabel;
  juce::Label routingDeviceLabel;
  juce::Label routingDeviceHelp;
  AdaptiveComboBox routingDeviceBox;
  juce::Label routingControlsHelp;
  juce::Label routingHintLabel;
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
  struct OutputDeviceChoice {
    juce::String typeName;
    juce::String deviceName;
  };
  std::vector<OutputDeviceChoice> outputDeviceChoices;

  void refreshRoutingForDevice();
  void refreshRoutingDeviceOptions();
  void selectRoutingDevice();
  void setRoutingDefaultsLocal(const std::vector<CategoryRoutingDefaults> &next);
  void layoutGeneralPage();
  void layoutAccountPage();
  void layoutAudioPage();
  void layoutRoutingPage();
  int routingPageContentHeight(int width) const;
  void layoutDiagnosticsPage();
  void applyLanguageTexts();
  void updateTabButtonStates(int activeIndex);
  static juce::String formatIsoDateTime(const juce::String &isoDateTime);
};
