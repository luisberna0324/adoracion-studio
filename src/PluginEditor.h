#pragma once

#include "PluginProcessor.h"
#include "platform/NativeAppleSignIn.h"
#include "data/SessionPersistence.h"
#include "data/AppleLinkFlow.h"
#include "data/CloudLibraryClient.h"
#include "data/CloudSettingsClient.h"
#include "data/ProjectStore.h"
#include "data/SongData.h"
#include "data/SongSyncService.h"
#include "data/TeamsClient.h"
#include "data/TeamsPreparationService.h"
#include "network/RemoteSession.h"
#include "network/PlaybackSession.h"
#include "ui/components/ChannelStripLookAndFeel.h"
#include "ui/UiCommon.h"
#include "ui/learning/LearningGuideModel.h"
#include "ui/learning/RemoteLearningAction.h"
#include "ui/learning/DemoLearningProgress.h"
#include <JuceHeader.h>

#include <optional>
#include <unordered_set>

class TopBar;
class WaveformBar;
class SetlistHandlePanel;
class SetlistDrawerScrim;
class SongListPanel;
class PadsComponent;
class PadsHandlePanel;
class PadsPopoverContent;
class LocalAuthCallbackServer;
class SettingsPanel;
class SettingsVersionsSheet;
class AppModal;
class PracticePanel;
class LearningGuideOverlay;
class SongLoadProgressOverlay;
class TeamsPanel;


class MTControlsAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer {
public:
  explicit MTControlsAudioProcessorEditor(MTControlsAudioProcessor &);
  ~MTControlsAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &key) override;
  bool navigateBackInSettings();
  void parentHierarchyChanged() override;

  /** Recibe un proyecto solicitado por asociación de archivo/doble clic. */
  void openProjectFromExternalApplication(const juce::File &file);
  bool isSongCurrentlyLoaded(const juce::String &songId) const noexcept;
  void flushActiveSongPersistenceNow();
  /** Resuelve el aviso de cambios pendientes antes de cerrar Standalone. */
  void requestApplicationQuit(std::function<void(bool)> onResolved);
  void refreshCloudLibrary(const juce::String &query);

  /** Resultado del hilo de reanudacion de sesion (solo MessageManager::callAsync). */
  void applySessionResumeOnMessageThread(bool success, bool validatedOnline,
                                         PersistedSessionSnapshot snapshot,
                                         const juce::String &welcomeError,
                                         bool clearSnapshotFile);

private:
  void updateTeamsOnboarding();
  void processTeamsRevocations();
  juce::var teamsPendingRevocations;
  bool hasTeamsMembership = false;
  void appendTeamsLearningLessons();
  void openTeamsLearningCloud();
  struct TeamsLearningSource { juce::String teamId, owner, lessonId; };
  std::vector<TeamsLearningSource> teamsLearningSources;
  std::unique_ptr<TeamsClient> teamsOnboardingClient;
  juce::StringArray teamsWelcomesShown;
  juce::int64 teamsNextPollMs = 0, teamsNextGuideCheckMs = 0;
  uint64_t teamsOnboardingGeneration = 0;
  bool teamsOnboardingPending = false, teamsMembershipsVerified = false;
  bool teamsWelcomeVisible = false;
  bool teamsLearningDownloaded = false, teamsLearningAdded = false;
  void showTeams();
  void closeTeams();
  void handleTeamsAction(const juce::String &, juce::var);
  void displayTeamDetails(juce::var);
  void downloadSharedCloudSong(const CloudSongSummary &, const juce::String &targetSetlistId);
  void invalidateCloudAccountView();
  bool canManageSharedCloud = false;
  std::unique_ptr<TeamsPanel> teamsOverlay;
  std::unique_ptr<TeamsClient> teamsClient;
  std::unique_ptr<TeamsPreparationService> teamsPreparation;
  juce::var teamsDetails;
  uint64_t teamsDetailsGeneration = 0;
  void showLearningGuide();
  void showPracticeLearningGuide();
  void offerDemoWelcome();
  void prepareDemoLesson();
  playworship::learning::DemoLearningProgress::Snapshot demoLearningSnapshot() const;
  bool isLearningPracticeStep() const;
  void showLearningCatalog();
  void startLearningLesson(const juce::String &id);
  void advanceLearningStep(bool forward);
  void restartLearningLesson();
  void pauseLearningGuide();
  void refreshLearningStep();
  void updateLearningGuideTarget();
  juce::Rectangle<int> getLearningTargetBounds(playworship::learning::Anchor) const;
  const playworship::learning::Lesson *currentLearningLesson() const;
  playworship::learning::StepFacts learningStepFacts() const;
  bool canAdvanceLearningStep() const;
  void updatePhoneOrientation();
  bool shouldUsePhonePortraitOrientation() const;
  void fitMobileEditorToHost();
  void showPhonePadsPopover(juce::Component &anchor);
  void showPractice();
  void closePractice();
  bool phoneOrientationUpdatePending = false;
  void applyRemoteSnapshot(const juce::var &snapshot);
  void applyRemoteWaveform(const std::vector<uint8_t> &bytes);
  void refreshRemoteSettings();
  void refreshRemoteIndicator();
  void publishRemoteMeters();
  void setRemoteMode(playworship::remote::RemoteSession::Mode mode);
  void processRemoteEvents();
  void publishRemoteState();
  bool applyRemoteCommand(const juce::var &command, juce::String &error);
  bool captureCloudSettings(const ImportedSongData &song,
                            ProjectSongSettings &outSettings);
  bool applyCloudSettings(const ImportedSongData &song,
                          const ProjectSongSettings &settings);
  bool hasPendingCloudSettings(const ImportedSongData &song);
  void refreshCloudSettingsBaselines(
      const std::vector<CloudSongSummary> &cloudSongs);
  void rememberCloudSettingsBaseline(const juce::String &projectId,
                                     int revision,
                                     const juce::String &snapshotHash,
                                     const ProjectSongSettings &settings,
                                     const std::vector<CloudSettingsVersion> &knownVersions = {});
  void confirmCloudSettingsSync(const ImportedSongData &song);
  void syncCloudSettings(const ImportedSongData &song);
  void showCloudSettingsVersions(const ImportedSongData &song, juce::Rectangle<int> screenAnchor);
  void showCloudSettingsVersionConfirmation(
      const ImportedSongData &song, const CloudSettingsVersion &version);
  void loadCloudSettingsVersions(const ImportedSongData &song,
                                juce::Component::SafePointer<SettingsVersionsSheet> sheet);
  juce::Component::SafePointer<SettingsVersionsSheet> cloudSettingsSheet;
  std::unique_ptr<juce::Component> cloudSettingsSheetOverlay;
  std::unique_ptr<AppModal> appModal;
  std::unique_ptr<LearningGuideOverlay> learningGuide;
  std::unique_ptr<playworship::learning::ProgressStore> learningProgress;
  std::vector<playworship::learning::Lesson> learningLessons;
  juce::String learningLessonId;
  juce::String learningSaveError;
  int learningStepIndex = 0;
  bool learningPhoneContent = false;
  bool learningEnglishContent = false;
  bool learningRevealTargetPending = false;
  playworship::learning::RemotePrerequisite learningLastPrerequisite =
      playworship::learning::RemotePrerequisite::None;
  juce::String learningObservedStepKey;
  playworship::learning::RemoteLearningAction learningRemoteAction;
  playworship::learning::DemoLearningProgress demoLearningProgress;
  std::unique_ptr<SongLoadProgressOverlay> demoLoadingOverlay;
  bool demoWelcomeChecked = false;
  bool localDemoSession = false;
  friend struct LocalDemoAccessTestAccess;
  PlaybackSession playbackSession;
  ChannelStripLookAndFeel stripLookAndFeel;

  std::unique_ptr<TopBar> topBar;
  std::unique_ptr<juce::Component> waveformBar;
  std::unique_ptr<juce::Component> channelGridPanel;
  std::unique_ptr<juce::Component> songListPanel;
  std::unique_ptr<SetlistHandlePanel> setlistHandlePanel;
  std::unique_ptr<SetlistDrawerScrim> setlistDrawerScrim;
  int currentThemeId = 1;
  std::function<void()> refreshTopBarTheme;
  bool isSetlistCollapsed = false;
  bool isSetlistDrawerOpen = false;
  int currentSongIndex = -1;
  std::function<void()> prevSongAction;
  std::function<void()> nextSongAction;
  bool isWaveformHeightExpanded = false;
  ImportedSongData currentLoadedSong;
  bool hasCurrentLoadedSong = false;
  SongListPanel *songListPanelRaw = nullptr;
  WaveformBar *waveformBarPtr = nullptr;
  std::function<bool(const ImportedSongData &, int)> loadSongAction;
  std::function<void(const ImportedSongData &, int)> requestSongLoadAction;
  std::optional<ProjectDocument> preparedProjectForNextSongLoad;
  std::function<void(int, bool)> loadSongByIndexAction;
  bool wasSongPlaying = false;
  juce::File pendingExternalProjectFile;
  struct SessionSummary {
    juce::String uid;
    juce::String email;
    juce::String authProvider;
    juce::String authProviderUser;
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
  void showServiceTermsIfNeeded();
  bool serviceTermsRequestPending = false;
  std::unique_ptr<CloudTransferClient> serviceTermsClient;
  std::unique_ptr<SongSyncService> songSyncService;
  std::unique_ptr<CloudLibraryClient> cloudLibraryClient;
  std::unique_ptr<CloudSettingsClient> cloudSettingsClient;
  std::unordered_set<std::string> cloudUploadsInProgress;
  std::unordered_set<std::string> cloudDownloadsInProgress;
  std::unordered_set<std::string> cloudDeletesInProgress;
  std::unordered_set<std::string> cloudSettingsInProgress;
  std::unordered_set<std::string> cloudSettingsStatusInProgress;
  uint64_t cloudLibrarySearchGeneration = 0;
  std::vector<CloudSongSummary> cloudCatalogSongs;
  bool mainUiInitialized = false;
  ResponsiveLayoutProfile responsiveLayoutProfile =
      ResponsiveLayoutProfile::Standard;
  bool autoSetlistCollapseInitialized = false;
  std::unique_ptr<juce::Component> welcomeModal;
  juce::BorderSize<int> lastAuthKeyboardInsets;
  std::unique_ptr<LocalAuthCallbackServer> localAuthServer;
  juce::String authExpectedState;
  juce::String pendingAuthCallbackUrl;
  juce::CriticalSection authCallbackLock;
  int authCallbackPort = 0;
  juce::Component::SafePointer<juce::DialogWindow> settingsWindow;
  std::unique_ptr<juce::Component> settingsOverlay;
  std::unique_ptr<PracticePanel> practiceOverlay;
  playworship::remote::RemoteSession remoteSession;
  std::string remotePracticeOwner;
  std::string remoteSectionAutomationOwner;
  juce::String remoteSectionAutomationCommandId;
  juce::String remoteSectionAutomationSongId;
  juce::String remotePracticeCommandId;
  uint64_t remotePracticeRevision = 0;
  std::string remotePracticeLoopOwner;
  juce::String remotePracticeLoopCommandId;
  uint64_t remotePracticeLoopRevision = 0;
  std::map<std::string, PracticeScrubLease> remotePracticeScrubLeases;
  ImportedSongData remoteSavedLocalSong;
  bool remoteSavedHasSong = false;
  bool remoteHadSnapshot = false;
  bool remoteVisualStatePending = false;
  juce::var remoteSavedLocalSettings;
  std::vector<float> remoteSavedLocalWaveform;
  juce::String remoteLibrarySignature, remoteSettingsSignature, remoteChannelSignature, remoteStateSignature;
  juce::String remoteExpectedWaveformHash, remoteConnectionStatus;
  juce::int64 nextRemoteSettingsRefreshMs = 0;
  std::shared_ptr<const LibraryState> remoteDisplayedLibrary;
  juce::int64 nextRemotePublishMs = 0;
  juce::int64 nextRemoteLibraryMs = 0;
  juce::var remoteLibraryCache;
  juce::var remoteReceivedSong;
  juce::String remoteLibraryMetadataSignature;
  juce::int64 remoteLibraryRevision = 0;
  bool remoteLibraryDirty = true;
  juce::var remoteSettingsCache;
  juce::String remoteSettingsSongId;
  juce::int64 nextRemoteSettingsMs = 0;
  juce::int64 remoteSnapshotSequence = 0;
  juce::String remoteWaveformSongId;
  juce::String remoteWaveformHash;
  std::vector<float> remoteWaveformBins;
  bool remoteClientAudioMuted = false;
  bool remotePreviousPadsEnabled = false;
  bool remotePreviousMetronomeEnabled = false;
  juce::Component::SafePointer<SettingsPanel> settingsPanelRaw;
  juce::int64 lastSuccessfulLoginMs = 0;
  /** Espejo del snapshot; ultima validacion online exitosa (login web o validate-access). */
  juce::int64 lastOnlineValidationMs = 0;
  juce::int64 reauthGraceDeadlineMs = 0;
  bool reauthPromptDismissed = false;
  bool reauthLoginInProgress = false;
  bool appleSignInAvailable = false;
  bool appleLoginInProgress = false;
  bool appleLinkInProgress = false;
  AppleLinkFlow appleLinkFlow;
  PersistedSessionSnapshot appleLinkCandidate;
  juce::ScopedMessageBox appleLinkDialog;
  // Existing provider login results are intercepted during deletion reauth;
  // they never replace or persist the original account session.
  std::function<void(PersistedSessionSnapshot)> deletionReauthCandidate;
  std::function<void(const juce::String &, bool, bool)> deletionReauthStatus;
  uint64_t authAttemptGeneration = 0;
  bool emailAuthenticationBusy = false;
  uint64_t appleConfigurationGeneration = 0;
  uint64_t appleCredentialGeneration = 0;
  std::unique_ptr<NativeAppleSignIn::CredentialMonitor> appleCredentialMonitor;
  std::unique_ptr<juce::Component> reauthPromptModal;
  std::unique_ptr<PadsComponent> padsComponent;
  std::unique_ptr<PadsHandlePanel> padsHandlePanel;
  juce::Component::SafePointer<PadsPopoverContent> phonePadsPopoverContent;
  bool isPadsCollapsed = false;
  bool phonePadsOpen = false;
  bool midiLearnActive = false;
  int midiLearnPendingTargetId = -1;
  bool songLoadBusy = false;
  bool suppressPersistenceBeforeNextSongLoad = false;
  bool mixExportBusy = false;
  bool projectSaveBusy = false;
  // P0.5 (perf): generation token para descartar resultados tardíos de
  // la generación de waveform en background. Si el usuario cambia de
  // canción dos veces seguidas, solo el último set de bins debe ganar.
  std::atomic<uint64_t> waveformGeneration{0};
  std::unique_ptr<juce::FileChooser> mixExportFileChooser;
  juce::ScopedMessageBox mixExportShareSheet;
  juce::ScopedMessageBox diagnosticShareSheet;
  std::unique_ptr<juce::FileChooser> diagnosticFileChooser;
  bool diagnosticExportBusy = false;
  void shareAudioDiagnostics();
  juce::File pendingSharedMixExport;
  bool deferredStartupSongLoadPending = false;
  int deferredStartupSongIndex = -1;
  juce::int64 deferredStartupSongLoadAtMs = 0;
  bool initialStandaloneWindowCentered = false;
  juce::int64 nextAudioDeviceFallbackPollMs = 0;
  juce::int64 missingAudioDeviceSinceMs = 0;
  juce::int64 nextAudioDeviceFallbackAttemptMs = 0;
  juce::String lastObservedAudioDeviceKey;
  juce::BigInteger lastObservedActiveOutputChannels;
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
  bool sessionResumeCancelled = false;

  void timerCallback() override;
  void showSettings(juce::Rectangle<int> anchor,
                    juce::Component *parent);
  void closeSettings();
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
  bool unloadActiveSongForLocalPurge();
  void updateSystemUsageMetrics();
  void launchExternalLogin();
  void launchPasswordLogin(const juce::String& email, const juce::String& password);
  void launchLocalDemoLogin(const juce::String& username, const juce::String& password);
  void enterLocalDemoSession();
  void prepareDemoSong(bool startLesson);
  void requestPasswordLink(const juce::String& email);
  void cancelAuthenticationAttempt(bool preserveAppleLinkFlow = false);
  void resetAppleLinkFlow();
  bool acceptAppleLinkCallback(const juce::String& expectedUid);
  bool captureInlineAppleAccount(const PersistedSessionSnapshot&);
  void updateInlineAppleLinkUi(bool active);
  void setEmailAuthenticationStatus(const juce::String& text, bool error, bool busy);
  void refreshAppleSignInConfiguration();
  void refreshAppleCredentialMonitoring();
  void applyAppleCredentialState(uint64_t generation, const juce::String& uid,
                                const juce::String& appleUser, NativeAppleSignIn::CredentialState);
  void launchAppleLogin(bool linkExisting = false);
  void setAppleAuthenticationStatus(const juce::String &text, bool error, bool busy);
  void completeAuthenticatedSession(PersistedSessionSnapshot snapshot);
  void completeBootstrapLogin(const juce::String &callbackUrl);
  void handleLogout();
  void showWelcomeOnlyMode();
  void evaluateReauthPolicy();
  void showReauthPromptModal();
  void hideReauthPromptModal();
  juce::String consumePendingAuthCallbackUrl();
  void enqueueAuthCallbackUrl(const juce::String &callbackUrl);
  void tryResumeSessionAsync();
  void showPendingAccountDeletion();
  PersistedSessionSnapshot persistedSnapshotFromActiveSession(
      juce::int64 lastOnlineMs) const;
  void applyPersistedSnapshotToActive(const PersistedSessionSnapshot &snap);
  void saveSessionSnapshotToDisk();
  void beginExportCurrentSongMix(const juce::String &formatExtension);
  void runSongMixExport(const juce::File &outputFile,
                        bool shareWhenComplete = false);
  void presentExportShareSheet(const juce::File &outputFile);
  void saveCurrentProject();
  void savePortableProjects(std::vector<juce::File> files,
                            bool showCompletionMessage,
                            std::function<void(bool)> onComplete = {});

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MTControlsAudioProcessorEditor)
};
