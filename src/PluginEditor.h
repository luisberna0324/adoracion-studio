#pragma once

#include "PluginProcessor.h"
#include "data/SessionPersistence.h"
#include "data/SongData.h"
#include "ui/components/ChannelStripLookAndFeel.h"
#include "update/UpdateService.h"
#include <JuceHeader.h>

class TopBar;
class SetlistHandlePanel;
class SongListPanel;
class PadsComponent;
class PadsHandlePanel;
class LocalAuthCallbackServer;
class SettingsPanel;

class MTControlsAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer {
public:
  explicit MTControlsAudioProcessorEditor(MTControlsAudioProcessor &);
  ~MTControlsAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &key) override;
  void parentHierarchyChanged() override;

  /** Resultado del hilo de reanudacion de sesion (solo MessageManager::callAsync). */
  void applySessionResumeOnMessageThread(bool success, bool validatedOnline,
                                         PersistedSessionSnapshot snapshot,
                                         const juce::String &welcomeError,
                                         bool clearSnapshotFile);

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
  SongListPanel *songListPanelRaw = nullptr;
  std::function<void(int, bool)> loadSongByIndexAction;
  bool wasSongPlaying = false;
  double lastObservedSongPositionSec = 0.0;
  struct SessionSummary {
    juce::String uid;
    juce::String email;
    juce::String plan;
    juce::String subscriptionStatus;
    juce::String expiresAt;
    bool allowed = true;
    juce::String reasonCode;
    juce::String reason;
    juce::String refreshToken;
    juce::String firebaseApiKey;
    juce::int64 idTokenExpiresAtMs = 0;
    juce::String licenseKey;
  };
  SessionSummary activeSession;
  bool mainUiInitialized = false;
  ResponsiveLayoutProfile responsiveLayoutProfile =
      ResponsiveLayoutProfile::Standard;
  bool autoSetlistCollapseInitialized = false;
  std::unique_ptr<juce::Component> welcomeModal;
  std::unique_ptr<LocalAuthCallbackServer> localAuthServer;
  juce::String authExpectedState;
  juce::String pendingAuthCallbackUrl;
  juce::CriticalSection authCallbackLock;
  int authCallbackPort = 0;
  juce::Component::SafePointer<juce::DialogWindow> settingsWindow;
  juce::Component::SafePointer<SettingsPanel> settingsPanelRaw;
  juce::int64 lastSuccessfulLoginMs = 0;
  /** Espejo del snapshot; ultima validacion online exitosa (login web o validate-access). */
  juce::int64 lastOnlineValidationMs = 0;
  juce::int64 reauthGraceDeadlineMs = 0;
  bool reauthPromptDismissed = false;
  bool reauthLoginInProgress = false;
  std::unique_ptr<juce::Component> reauthPromptModal;
  std::unique_ptr<PadsComponent> padsComponent;
  std::unique_ptr<PadsHandlePanel> padsHandlePanel;
  bool isPadsCollapsed = false;
  bool midiLearnActive = false;
  int midiLearnPendingTargetId = -1;
  bool songLoadBusy = false;
  bool deferredStartupSongLoadPending = false;
  int deferredStartupSongIndex = -1;
  juce::int64 deferredStartupSongLoadAtMs = 0;
  bool initialStandaloneWindowCentered = false;
  juce::int64 nextAudioDeviceFallbackPollMs = 0;
  juce::int64 missingAudioDeviceSinceMs = 0;
  juce::int64 nextAudioDeviceFallbackAttemptMs = 0;
  juce::String lastObservedAudioDeviceKey;
  juce::Rectangle<int> padsDrawerBackgroundBounds;
  juce::String trackedSongWindowSongId;
  double trackedSongWindowStartSec = 0.0;
  double trackedSongWindowEndSec = 0.0;
  double trackedSongWindowFadeSec = 0.0;
  bool hasTrackedSongWindow = false;
  bool hasTrackedClickDynMetronomeEnabled = false;
  bool trackedClickDynMetronomeEnabled = false;
  double smoothedAudioCpuUsage01 = 0.0;
  double smoothedProcessMemoryMb = 0.0;
  int totalSystemMemoryMb = 0;
  std::unique_ptr<PlayWorshipUpdate::UpdateService> updateService;
  std::unique_ptr<juce::AlertWindow> updateProgressWindow;
  double updateProgressValue = 0.0;
  juce::int64 nextUpdateCheckMs = 0;

  void timerCallback() override;
  void showSettings(juce::Rectangle<int> anchor,
                    juce::Component *parent);
  void applyThemePreset(int presetId);
  void applyRoutingDefaultsToSong(const ImportedSongData &song);
  ResponsiveLayoutProfile resolveResponsiveLayoutProfile() const;
  void applyResponsiveLayoutProfile(ResponsiveLayoutProfile profile);
  void updateResponsiveLayoutProfile();
  void initializeMainUi();
  void initializeWelcomeModal();
  void resetTrackedSongWindow();
  void trackSongWindow(const juce::String &songId, double startSec,
                       double endSec, double fadeSec);
  void maybePersistSongWindowForActiveSong();
  void persistClickDynMetronomeEnabledForActiveSong(bool enabled);
  void trackClickDynMetronomeEnabled(bool enabled);
  void maybePersistClickDynMetronomeEnabledForActiveSong();
  void updateSystemUsageMetrics();
  void launchExternalLogin();
  void completeBootstrapLogin(const juce::String &callbackUrl);
  void handleLogout();
  void showWelcomeOnlyMode();
  void evaluateReauthPolicy();
  void showReauthPromptModal();
  void hideReauthPromptModal();
  juce::String consumePendingAuthCallbackUrl();
  void enqueueAuthCallbackUrl(const juce::String &callbackUrl);
  void tryResumeSessionAsync();
  PersistedSessionSnapshot persistedSnapshotFromActiveSession(
      juce::int64 lastOnlineMs) const;
  void applyPersistedSnapshotToActive(const PersistedSessionSnapshot &snap);
  void saveSessionSnapshotToDisk();
  void initializeUpdateService();
  void maybeRunPeriodicUpdateCheck();
  void showUpdateAvailablePrompt(PlayWorshipUpdate::UpdateManifest manifest);
  void showUpdateProgress(const PlayWorshipUpdate::UpdateProgress &progress);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MTControlsAudioProcessorEditor)
};
