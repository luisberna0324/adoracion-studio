#pragma once

#include "PluginProcessor.h"
#include "data/SongData.h"
#include "ui/components/ChannelStripLookAndFeel.h"
#include <JuceHeader.h>

class TopBar;
class SetlistHandlePanel;
class SongListPanel;
class LocalAuthCallbackServer;

class MTControlsAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer {
public:
  explicit MTControlsAudioProcessorEditor(MTControlsAudioProcessor &);
  ~MTControlsAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &key) override;

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
  juce::int64 reauthGraceDeadlineMs = 0;
  bool reauthPromptDismissed = false;
  bool reauthLoginInProgress = false;
  std::unique_ptr<juce::Component> reauthPromptModal;

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

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MTControlsAudioProcessorEditor)
};
