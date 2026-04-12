#pragma once

#include "PluginProcessor.h"
#include "data/SessionPersistence.h"
#include "data/SongData.h"
#include "ui/components/ChannelStripLookAndFeel.h"
#include <JuceHeader.h>

class TopBar;
class SetlistHandlePanel;
class SongListPanel;
class SongLoadProgressOverlay;
class LocalAuthCallbackServer;

class MTControlsAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer {
public:
  explicit MTControlsAudioProcessorEditor(MTControlsAudioProcessor &);
  ~MTControlsAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &key) override;

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
  juce::int64 lastSuccessfulLoginMs = 0;
  /** Espejo del snapshot; ultima validacion online exitosa (login web o validate-access). */
  juce::int64 lastOnlineValidationMs = 0;
  juce::int64 reauthGraceDeadlineMs = 0;
  bool reauthPromptDismissed = false;
  bool reauthLoginInProgress = false;
  std::unique_ptr<juce::Component> reauthPromptModal;
  std::unique_ptr<SongLoadProgressOverlay> songLoadOverlay;
  bool songLoadBusy = false;

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

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MTControlsAudioProcessorEditor)
};
