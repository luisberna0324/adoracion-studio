#include "ui/components/ServiceTermsPrompt.h"
#include "data/ServiceTermsPolicy.h"
#include "AppIdentity.h"
#include "PluginEditor.h"
#include "DevelopmentFeatures.h"
#include "telemetry/Telemetry.h"
#include "PluginEditorSupport.h"
#include "SongLoadProbe.h"
#include "audio/AudioDeviceMonitor.h"
#include "audio/SongMixExporter.h"
#include "data/AppSettings.h"
#include "data/ApplicationDataPaths.h"
#include "analysis/GuideSectionInterpretation.h"
#include "data/AssetBootstrapManager.h"
#include "data/AuthSessionApi.h"
#include "data/CloudSyncIndexStore.h"
#include "data/CloudSettingsSyncState.h"
#include "data/LocalAuthCallbackServer.h"
#include "data/Localization.h"
#include "data/ProjectStore.h"
#include "data/PlaybackDiagnosticReport.h"
#include "data/SessionAuthLogger.h"
#include "data/SessionPersistence.h"
#include "data/SongData.h"
#include "data/PracticeWaveform.h"
#include "data/SongLibraryStore.h"
#include "ui/UiCommon.h"
#include "ui/components/PadsHandlePanel.h"
#include "ui/components/AppModal.h"
#include "ui/learning/LearningGuideOverlay.h"
#include "ui/components/ReauthPromptModal.h"
#include "ui/components/SettingsVersionsSheet.h"
#include "platform/NativeAuthSession.h"
#include "platform/NativeAppleSignIn.h"
#include "ui/components/SetlistHandlePanel.h"
#include "ui/components/WelcomeModal.h"
#include "ui/components/SongLoadProgressOverlay.h"
#include "ui/panels/ChannelGridPanel.h"
#include "ui/panels/PadsComponent.h"
#include "ui/panels/PracticeAccompaniment.h"
#include "ui/panels/PracticePanel.h"
#include "ui/panels/SectionDetectionPanel.h"
#include "ui/panels/SettingsPanel.h"
#include "ui/components/AccountDeletionModal.h"
#include "data/AccountDeletionApi.h"
#include "ui/panels/TeamsPanel.h"

#include "ui/panels/SongListPanel.h"
#include "network/RemoteStateCodec.h"
#include "network/RemoteDeviceName.h"
#include "ui/panels/TopBar.h"
#include "ui/panels/WaveformBar.h"
#if !JUCE_IOS && !JUCE_ANDROID
#include "ui/panels/LyricsPanel.h"
#endif
#include <algorithm>
#include <cmath>
#include <fstream>
#include <thread>

#if JUCE_WINDOWS
#include <windows.h>
#include <psapi.h>
#elif JUCE_MAC || JUCE_IOS
#include <mach/mach.h>
#elif JUCE_LINUX || JUCE_BSD
#include <unistd.h>
#endif

using namespace PluginEditorSupport;

class SetlistDrawerScrim final : public juce::Component {
public:
  explicit SetlistDrawerScrim(std::function<void()> dismissCallback)
      : onDismiss(std::move(dismissCallback)) {
    setInterceptsMouseClicks(true, true);
  }

  void setDrawerBounds(juce::Rectangle<int> bounds) {
    drawerBounds = bounds;
    repaint();
  }

  void setFullScreen(bool enabled) { fullScreen = enabled; }

  void paint(juce::Graphics &g) override {
    // Mirror the wizard overlay: a translucent scrim (black@44%) gives the
    // sense of an "app vertical" while the mixer stays faintly visible behind
    // any uncovered gap. The phone fullScreen branch used to be opaque
    // (UITheme::panelDark), which broke the parity with the wizard — it now
    // uses the same translucency as the non-fullScreen branch on every
    // platform. The drawer panel itself paints edge-to-edge with
    // drawStudioBackground, so it covers the scrim on its own surface.
    juce::ignoreUnused(fullScreen);
    g.fillAll(juce::Colours::black.withAlpha(0.44f));
    if (!drawerBounds.isEmpty()) {
      const auto shadow = drawerBounds.toFloat().withX(
          static_cast<float>(drawerBounds.getRight()));
      juce::ColourGradient gradient(juce::Colours::black.withAlpha(0.30f),
                                    shadow.getX(), shadow.getY(),
                                    juce::Colours::transparentBlack,
                                    shadow.getRight() + 18.0f, shadow.getY(),
                                    false);
      g.setGradientFill(gradient);
      g.fillRect(shadow.withWidth(18.0f));
    }
  }

  void mouseUp(const juce::MouseEvent &event) override {
    if (!drawerBounds.contains(event.getPosition()) && onDismiss)
      onDismiss();
  }

private:
  juce::Rectangle<int> drawerBounds;
  bool fullScreen = false;
  std::function<void()> onDismiss;
};

class PadsPopoverContent final : public juce::Component {
public:
  PadsPopoverContent(PadsComponent &padsComponent,
                     std::function<void()> onDismissCallback)
      : pads(&padsComponent), onDismiss(std::move(onDismissCallback)) {
    setWantsKeyboardFocus(true);
    closeButton.onClick = [] { dismissActiveActionPopover(); };
    closeButton.setTooltip(L10n::tr("Cerrar Pads", "Close Pads"));
    addAndMakeVisible(closeButton);
    addAndMakeVisible(padsComponent);
    setSize(520, 274);
  }

  ~PadsPopoverContent() override {
    detachPads();
    if (onDismiss)
      onDismiss();
  }

  void detachPads() {
    if (pads != nullptr && pads->getParentComponent() == this)
      removeChildComponent(pads.getComponent());
  }

  void prepareForEditorDestruction() {
    detachPads();
    onDismiss = {};
  }

  void paint(juce::Graphics &g) override {
    g.setColour(UITheme::textPrimary);
    g.setFont(makeAppFont(13.0f, AppFontWeight::SemiBold));
    g.drawText(L10n::tr("Pads", "Pads"),
               getLocalBounds().removeFromTop(44).withTrimmedRight(44).reduced(10, 0),
               juce::Justification::centredLeft);
  }

  void resized() override {
    closeButton.setBounds(getWidth() - 44, 0, 44, 44);
    if (pads != nullptr)
      pads->setBounds(getLocalBounds().withTrimmedTop(44));
  }

private:
  juce::Component::SafePointer<PadsComponent> pads;
  std::function<void()> onDismiss;
  HeaderIconButton closeButton{"closePads", IconGlyph::Close};
};

namespace {
juce::File guideSectionLearningFile() {
  return ApplicationDataPaths::applicationDataDirectory().getChildFile("MTControls")
      .getChildFile("analysis").getChildFile("guide-section-corrections-v1.json");
}

void applyReviewedGuideSectionName(playworship::analysis::SectionSuggestion &suggestion,
                                  const juce::String &name) {
  const auto recognized = playworship::analysis::normalizeGuideCue(suggestion.rawStartSeconds, name);
  suggestion.displayName = name;
  suggestion.userEdited = true;
  suggestion.type = recognized ? recognized->normalizedType : playworship::analysis::SectionType::custom;
  if (recognized && recognized->spokenOrdinal) suggestion.occurrence = *recognized->spokenOrdinal;
}

bool supportsSetlistDrawer(const juce::Component &component) {
  const auto bounds = component.getLocalBounds();
  const int shortSide = juce::jmin(bounds.getWidth(), bounds.getHeight());
  const int longSide = juce::jmax(bounds.getWidth(), bounds.getHeight());
  return useTouchOptimisedControls() && shortSide >= 700 && longSide >= 960;
}

bool isPhoneSizedLayout(const juce::Component &component) {
  return isPhoneSizedTouchLayout(component);
}

class SettingsOverlayComponent final : public juce::Component {
public:
  SettingsOverlayComponent(std::unique_ptr<SettingsPanel> content,
                           std::function<void()> dismissCb)
      : panel(std::move(content)), onDismiss(std::move(dismissCb)) {
    jassert(panel != nullptr);
    addAndMakeVisible(*panel);
    setInterceptsMouseClicks(true, true);
  }

  SettingsPanel *getPanel() const noexcept { return panel.get(); }

  void paint(juce::Graphics &g) override {
    g.fillAll(isPhoneSizedLayout(*this) ? UITheme::panelDark
                                      : juce::Colours::black.withAlpha(0.44f));
  }

  void resized() override {
    auto area = getLocalBounds();
    if (useTouchOptimisedControls()) {
      if (const auto *display = juce::Desktop::getInstance().getDisplays()
                                    .getDisplayForRect(getScreenBounds())) {
        auto insets = display->safeAreaInsets;
        insets.setBottom(juce::jmax(insets.getBottom(), display->keyboardInsets.getBottom()));
        area = insets.subtractedFrom(area);
      }
      if (isPhoneSizedLayout(*this)) {
        panel->setBounds(area);
        return;
      }
    }
    // Keep the settings sheet inside the host surface on compact devices.
    const int margin = 28;
    area.reduce(margin, margin);

    const int maxWidth = useTouchOptimisedControls() ? 920 : 1080;
    const int maxHeight = useTouchOptimisedControls() ? 820 : 780;
    const int width = juce::jmin(maxWidth, juce::jmax(1, area.getWidth()));
    const int height = juce::jmin(maxHeight, juce::jmax(1, area.getHeight()));

    auto panelBounds = juce::Rectangle<int>(width, height);
    panelBounds.setCentre(area.getCentre());
    panel->setBounds(panelBounds);
  }

  void mouseUp(const juce::MouseEvent &event) override {
    if (panel == nullptr)
      return;
    if (!panel->getBounds().contains(event.getPosition()) && onDismiss)
      onDismiss();
  }

private:
  std::unique_ptr<SettingsPanel> panel;
  std::function<void()> onDismiss;
};

void ensureStandaloneMidiInputsEnabled(juce::AudioDeviceManager &manager) {
  const auto inputs = juce::MidiInput::getAvailableDevices();
  if (inputs.isEmpty())
    return;

  bool hasEnabled = false;
  for (const auto &dev : inputs) {
    if (manager.isMidiInputDeviceEnabled(dev.identifier)) {
      hasEnabled = true;
      break;
    }
  }

  if (hasEnabled)
    return;

  for (const auto &dev : inputs)
    manager.setMidiInputDeviceEnabled(dev.identifier, true);

  SessionAuthLogger::write(
      "ensureStandaloneMidiInputsEnabled: enabled " +
      juce::String(inputs.size()) + " MIDI input device(s)");
}

bool nearlyEqualSongWindowValue(const double a, const double b) {
  return std::abs(a - b) <= 0.001;
}

double getCurrentProcessMemoryUsageMb() noexcept {
#if JUCE_WINDOWS
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (GetProcessMemoryInfo(
          GetCurrentProcess(),
          reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
          static_cast<DWORD>(sizeof(counters))) != FALSE) {
    return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
  }
  return 0.0;
#elif JUCE_MAC || JUCE_IOS
  mach_task_basic_info info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info), &count) ==
      KERN_SUCCESS) {
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
  }
  return 0.0;
#elif JUCE_LINUX || JUCE_BSD
  std::ifstream statm("/proc/self/statm");
  long totalPages = 0;
  long residentPages = 0;
  if (statm >> totalPages >> residentPages) {
    juce::ignoreUnused(totalPages);
    const auto pageBytes = static_cast<double>(::sysconf(_SC_PAGESIZE));
    if (pageBytes > 0.0)
      return (static_cast<double>(residentPages) * pageBytes) / (1024.0 * 1024.0);
  }
  return 0.0;
#else
  return 0.0;
#endif
}
} // namespace

MTControlsAudioProcessorEditor::MTControlsAudioProcessorEditor(
    MTControlsAudioProcessor &p)
    : AudioProcessorEditor(p), playbackSession(p) {
  SessionAuthLogger::write("PluginEditor ctor");
  SessionAuthLogger::write("PluginEditor ctor: log file " +
                           SessionAuthLogger::getLogFile().getFullPathName());
  currentThemeId = AppSettings::getThemePresetId();
  applyTheme(static_cast<ThemePreset>(currentThemeId));
  totalSystemMemoryMb = juce::jmax(1, juce::SystemStats::getMemorySizeInMegabytes());

  const auto userArea =
      juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
#if JUCE_IOS || JUCE_ANDROID
  const int authW = userArea.getWidth();
  const int authH = userArea.getHeight();
#elif MTCONTROLS_NATIVE_APP
  const int authW = userArea.getWidth();
  const int authH = userArea.getHeight();
#else
  const int authW = juce::jmin(kAuthWindowW, userArea.getWidth());
  const int authH = juce::jmin(kAuthWindowH, userArea.getHeight());
#endif
  setSize(authW, authH);
  setResizable(true, true);
#if JUCE_IOS || JUCE_ANDROID
  setResizeLimits(320, 280, 32000, 32000);
#else
  setResizeLimits(480, 500, 32000, 32000);
#endif

  if (const auto audioCtx = getStandaloneAudioContext(static_cast<MTControlsAudioProcessor &>(processor));
      audioCtx.manager != nullptr) {
    ensureStandaloneMidiInputsEnabled(*audioCtx.manager);
  }

  lastSuccessfulLoginMs = AppSettings::getLastSuccessfulLoginMs();
  reauthGraceDeadlineMs = AppSettings::getReauthGraceDeadlineMs();
  localDemoSession = DevelopmentFeatures::testLogin && AppSettings::getLocalDemoSessionActive();
  PersistedSessionSnapshot bootSnap;
  const bool hasBootSnapshot = SessionPersistence::load(bootSnap);
  if (!SessionPersistence::listAccountDeletionReceipts().isEmpty())
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] { if (safe) safe->showPendingAccountDeletion(); });
  const bool canFastBootFromSnapshot =
      hasBootSnapshot && canUseSnapshotForLocalBootstrap(bootSnap);
  if (hasBootSnapshot)
    lastOnlineValidationMs = bootSnap.lastOnlineValidationMs;

  initializeWelcomeModal();

  if (localDemoSession) {
    enterLocalDemoSession();
  } else if (canFastBootFromSnapshot) {
    SessionAuthLogger::write(
        "PluginEditor ctor: local session snapshot accepted, entering UI before "
        "online validation");
    applyPersistedSnapshotToActive(bootSnap);
    lastSuccessfulLoginMs =
        juce::jmax(lastSuccessfulLoginMs, bootSnap.lastOnlineValidationMs);
    AppSettings::setLastSuccessfulLoginMs(lastSuccessfulLoginMs);
#if JUCE_ANDROID
    // Let Android present the lightweight welcome surface before constructing
    // the full mixer, waveform and library component tree on the message
    // thread. JUCE components must still be created on that thread.
    showWelcomeOnlyMode();
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get())) {
      modal->setStatusText(
          L10n::tr(u8"Preparando tu espacio...", "Preparing your workspace..."),
          false);
      modal->setChecking(true);
    }
    juce::Timer::callAfterDelay(
        120,
        [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(
             this)] {
          if (safe != nullptr)
            safe->initializeMainUi();
        });
#else
    initializeMainUi();
#endif
  } else {
    showWelcomeOnlyMode();
  }

  tryResumeSessionAsync();
  if (!mainUiInitialized)
    startTimerHz(12);


}

void MTControlsAudioProcessorEditor::openProjectFromExternalApplication(
    const juce::File &file) {
  pendingExternalProjectFile = file;
  deferredStartupSongLoadPending = false;
  deferredStartupSongIndex = -1;
  deferredStartupSongLoadAtMs = 0;

  // Authentication/bootstrap may still be showing. The regular editor timer
  // will open the project as soon as the complete library UI is available.
  if (!isTimerRunning()) {
#if JUCE_ANDROID
    startTimerHz(mainUiInitialized ? 12 : 8);
#else
    startTimerHz(mainUiInitialized ? 20 : 12);
#endif
  }
}

bool MTControlsAudioProcessorEditor::isSongCurrentlyLoaded(
    const juce::String &songId) const noexcept {
  return hasCurrentLoadedSong && songId.trim().isNotEmpty() &&
         currentLoadedSong.id == songId.trim();
}

void MTControlsAudioProcessorEditor::flushActiveSongPersistenceNow() {
  if (playbackSession.isRemote()) return;
  maybePersistSongWindowForActiveSong();
  maybePersistClickDynMetronomeEnabledForActiveSong();
  if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
    grid->flushPendingPersistenceNow();
  if (hasCurrentLoadedSong && currentLoadedSong.projectPath.isNotEmpty())
    ProjectStore::flushPendingSettings(juce::File(currentLoadedSong.projectPath));
  ProjectStore::flushAllPendingSettings();
}

bool MTControlsAudioProcessorEditor::captureCloudSettings(
    const ImportedSongData &song, ProjectSongSettings &outSettings) {
  if (isSongCurrentlyLoaded(song.id))
    flushActiveSongPersistenceNow();
  const auto projectId = projectIdentityForSong(song);
  if (isProjectBackedSong(song) && projectId.isNotEmpty() &&
      ProjectStore::getCachedSettings(projectId, outSettings))
    return true;

  outSettings = {};
  if (!SongLibraryStore::getActiveSetlistMix(song.id, outSettings.mix))
    return false;
  SongLibraryStore::getActiveSetlistPitchSemitones(song.id,
                                                   outSettings.pitchSemitones);
  SongLibraryStore::getActiveSetlistTempoBpm(song.id, outSettings.tempoBpm);
  SongLibraryStore::getActiveSetlistSongWindow(
      song.id, outSettings.window, outSettings.windowCustomized);
  SongLibraryStore::getSongArrangement(
      song.id, outSettings.arrangement, outSettings.arrangementCustomized);
  SongLibraryStore::getSongFreeShowCues(song.id, outSettings.freeShowCues);
  SongLibraryStore::getActiveSetlistDynamicGuide(
      song.id, outSettings.dynamicGuide, outSettings.dynamicGuideCustomized);
  SongLibraryStore::getActiveSetlistDynamicGuideMix(
      song.id, outSettings.dynamicGuideMix);
  SongLibraryStore::getActiveSetlistClickDynMix(song.id,
                                                outSettings.clickDynMix);
  return true;
}

void MTControlsAudioProcessorEditor::rememberCloudSettingsBaseline(
    const juce::String &projectId, const int revision,
    const juce::String &snapshotHash,
    const ProjectSongSettings &settings,
    const std::vector<CloudSettingsVersion> &knownVersions) {
  CloudProjectLink link;
  if (!CloudSyncIndexStore::findByProjectId(projectId, link))
    return;
  CloudSettingsSyncState::remember(link, link.settingsJson);
  for (const auto &version : knownVersions)
    if (version.projectId == projectId && version.songId == link.remoteSongId)
      CloudSettingsSyncState::remember(
          link, juce::JSON::toString(ProjectStore::serializeSettings(version.settings)));
  link.settingsStateKnown = true;
  link.settingsRevision = juce::jmax(0, revision);
  link.settingsHash = snapshotHash.trim();
  link.settingsJson =
      juce::JSON::toString(ProjectStore::serializeSettings(settings));
  CloudSettingsSyncState::remember(link, link.settingsJson);
  CloudSyncIndexStore::upsert(std::move(link));
}

bool MTControlsAudioProcessorEditor::hasPendingCloudSettings(
    const ImportedSongData &song) {
  CloudProjectLink link;
  if (!CloudSyncIndexStore::findByProjectId(projectIdentityForSong(song), link) ||
      !link.settingsStateKnown)
    return false;
  if (link.settingsRevision <= 0)
    return true;
  if (link.settingsJson.isEmpty())
    return false;

  ProjectSongSettings localSettings;
  if (!captureCloudSettings(song, localSettings))
    return false;
  return CloudSettingsSyncState::hasPending(
      link, juce::JSON::toString(ProjectStore::serializeSettings(localSettings)));
}

void MTControlsAudioProcessorEditor::confirmCloudSettingsSync(
    const ImportedSongData &song) {
  if (playbackSession.isRemote()) return;
  if (!hasPendingCloudSettings(song) ||
      cloudSettingsInProgress.count(song.id.toStdString()) != 0)
    return;

  const auto safe =
      juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  const auto options = juce::MessageBoxOptions::makeOptionsOkCancel(
      juce::MessageBoxIconType::QuestionIcon,
      L10n::tr("Guardar versión de ajustes", "Save settings version"),
      L10n::tr(
          "¿Quieres sincronizar estos cambios en la nube y crear una nueva versión de ajustes?",
          "Sync these changes to the cloud and create a new settings version?"),
      L10n::tr("Sincronizar", "Sync"), L10n::tr("Cancelar", "Cancel"),
      this);
  juce::AlertWindow::showAsync(options, [safe, song](const int result) {
    if (result == 1 && safe != nullptr)
      safe->syncCloudSettings(song);
  });
}

void MTControlsAudioProcessorEditor::refreshCloudSettingsBaselines(
    const std::vector<CloudSongSummary> &cloudSongs) {
  if (!activeSession.allowed || activeSession.firebaseApiKey.isEmpty() ||
      activeSession.refreshToken.isEmpty())
    return;
  if (cloudSettingsClient == nullptr)
    cloudSettingsClient = std::make_unique<CloudSettingsClient>();
  const CloudTransferCredentials credentials{getApiBaseUrl(),
                                             activeSession.firebaseApiKey,
                                             activeSession.refreshToken};
  const auto safe =
      juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  for (const auto &cloudSong : cloudSongs) {
    if (cloudSong.isSharedCloud())
      continue;
    CloudProjectLink link;
    if (!CloudSyncIndexStore::findByRemoteSongId(cloudSong.songId, link))
      continue;

    if (cloudSong.settingsRevision <= 0) {
      link.settingsStateKnown = true;
      link.settingsRevision = 0;
      link.settingsHash.clear();
      link.settingsJson.clear();
      link.knownSettingsFingerprints.clear();
      CloudSyncIndexStore::upsert(std::move(link));
      continue;
    }
    if (link.settingsStateKnown && link.settingsRevision == cloudSong.settingsRevision &&
        link.settingsHash == cloudSong.settingsHash && link.settingsJson.isNotEmpty() &&
        !link.knownSettingsFingerprints.isEmpty())
      continue;
    if (!cloudSettingsStatusInProgress.insert(
            cloudSong.songId.toStdString()).second)
      continue;

    cloudSettingsClient->list(
        credentials, cloudSong.songId,
        [safe, remoteSongId = cloudSong.songId](
            std::vector<CloudSettingsVersion> versions,
            CloudTransferError error) mutable {
          if (safe == nullptr)
            return;
          safe->cloudSettingsStatusInProgress.erase(remoteSongId.toStdString());
          if (error.code.isNotEmpty() || versions.empty())
            return;
          const auto &latest = versions.front();
          safe->rememberCloudSettingsBaseline(
              latest.projectId, latest.revision, latest.snapshotHash,
              latest.settings, versions);
          if (safe->songListPanelRaw != nullptr)
            safe->songListPanelRaw->refreshFromStorageView();
        });
  }
}

bool MTControlsAudioProcessorEditor::applyCloudSettings(
    const ImportedSongData &song, const ProjectSongSettings &settings) {
  flushActiveSongPersistenceNow();
  const auto projectId = projectIdentityForSong(song);
  if (isProjectBackedSong(song)) {
    ProjectStore::scheduleSettingsSave(juce::File(song.projectPath), projectId,
                                       settings);
    ProjectStore::flushPendingSettings(juce::File(song.projectPath));
  } else {
    bool ok = SongLibraryStore::updateMixForActiveSetlist(song.id, settings.mix);
    ok = SongLibraryStore::updatePitchSemitonesForActiveSetlist(
             song.id, settings.pitchSemitones) && ok;
    ok = SongLibraryStore::updateTempoBpmForActiveSetlist(
             song.id, settings.tempoBpm) && ok;
    ok = SongLibraryStore::updateSongWindowForActiveSetlist(
             song.id, settings.window, settings.windowCustomized) && ok;
    ok = SongLibraryStore::updateSongArrangement(
             song.id, settings.arrangement, settings.arrangementCustomized) && ok;
    ok = SongLibraryStore::updateSongFreeShowCues(song.id, settings.freeShowCues) && ok;
    ok = SongLibraryStore::updateDynamicGuideForActiveSetlist(
             song.id, settings.dynamicGuide, settings.dynamicGuideCustomized) && ok;
    ok = SongLibraryStore::updateDynamicGuideMixForActiveSetlist(
             song.id, settings.dynamicGuideMix) && ok;
    ok = SongLibraryStore::updateClickDynMixForActiveSetlist(
             song.id, settings.clickDynMix) && ok;
    if (!ok)
      return false;
  }

  if (isSongCurrentlyLoaded(song.id) && loadSongAction) {
    suppressPersistenceBeforeNextSongLoad = true;
    if (!loadSongAction(song, currentSongIndex)) {
      suppressPersistenceBeforeNextSongLoad = false;
      return false;
    }
  }
  return true;
}


void MTControlsAudioProcessorEditor::syncCloudSettings(
    const ImportedSongData &song) {
  CloudProjectLink link;
  ProjectSongSettings settings;
  const auto projectId = projectIdentityForSong(song);
  if (!CloudSyncIndexStore::findByProjectId(projectId, link) ||
      !captureCloudSettings(song, settings)) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("Ajustes no disponibles", "Settings unavailable"),
        L10n::tr("No se pudo preparar la configuración de esta canción.",
                 "The song settings couldn't be prepared."));
    return;
  }
  if (link.settingsStateKnown && link.settingsRevision > 0 &&
      link.settingsJson.isNotEmpty()) {
    if (!CloudSettingsSyncState::hasPending(
            link, juce::JSON::toString(ProjectStore::serializeSettings(settings)))) {
      if (songListPanelRaw != nullptr)
        songListPanelRaw->refreshFromStorageView();
      return;
    }
  }
  if (!activeSession.allowed || activeSession.firebaseApiKey.isEmpty() ||
      activeSession.refreshToken.isEmpty()) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("Sesión requerida", "Sign-in required"),
        L10n::tr("Inicia sesión para sincronizar los ajustes.",
                 "Sign in to sync settings."));
    return;
  }
  if (!cloudSettingsInProgress.insert(song.id.toStdString()).second)
    return;
  if (cloudSettingsClient == nullptr)
    cloudSettingsClient = std::make_unique<CloudSettingsClient>();
  const CloudTransferCredentials credentials{getApiBaseUrl(),
                                             activeSession.firebaseApiKey,
                                             activeSession.refreshToken};
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  if (songListPanelRaw != nullptr)
    songListPanelRaw->setCloudUploadProgress(song.id, 0.0,
                                              "preparing-settings");
  if (isSongCurrentlyLoaded(song.id) && waveformBarPtr != nullptr)
    waveformBarPtr->setSongLoadingState(
        true, 0.0,
        L10n::tr("Preparando versión de ajustes…",
                 "Preparing settings version…"));
  const auto publishedSettings = settings;
  cloudSettingsClient->publish(
      credentials, link.remoteSongId, projectId, song, std::move(settings),
      [safe, songId = song.id](CloudTransferProgress progress) {
        if (safe == nullptr)
          return;
        if (safe->songListPanelRaw != nullptr)
          safe->songListPanelRaw->setCloudUploadProgress(songId,
                                                         progress.fraction,
                                                         progress.phase);
        if (safe->isSongCurrentlyLoaded(songId) &&
            safe->waveformBarPtr != nullptr) {
          const auto percent = juce::String(static_cast<int>(
              std::round(juce::jlimit(0.0, 1.0, progress.fraction) * 100.0)));
          safe->waveformBarPtr->setSongLoadingState(
              true, progress.fraction,
              L10n::tr("Sincronizando ajustes… ", "Syncing settings… ") +
                  percent + "%");
        }
      },
      [safe, songId = song.id, projectId,
       publishedSettings](bool created, CloudSettingsVersion version,
                          CloudTransferError error) {
        if (safe == nullptr)
          return;
        safe->cloudSettingsInProgress.erase(songId.toStdString());
        if (safe->isSongCurrentlyLoaded(songId) &&
            safe->waveformBarPtr != nullptr)
          safe->waveformBarPtr->clearSongLoadingState();
        const bool failed = error.code.isNotEmpty();
        if (safe->songListPanelRaw != nullptr) {
          if (failed)
            safe->songListPanelRaw->clearCloudUploadProgress(songId);
          else
            safe->songListPanelRaw->setCloudUploadProgress(songId, 1.0,
                                                            "complete");
        }
        if (!failed) {
          safe->rememberCloudSettingsBaseline(
              projectId, version.revision, version.snapshotHash,
              publishedSettings);
          if (safe->songListPanelRaw != nullptr)
            safe->songListPanelRaw->refreshFromStorageView();
        }
        juce::NativeMessageBox::showMessageBoxAsync(
            failed ? juce::MessageBoxIconType::WarningIcon
                   : juce::MessageBoxIconType::InfoIcon,
            failed ? L10n::tr("No se pudieron sincronizar los ajustes",
                              "Couldn't sync settings")
                   : created ? L10n::tr("Ajustes sincronizados", "Settings synced")
                             : L10n::tr("Ajustes al día", "Settings up to date"),
            failed ? (error.message.isNotEmpty() ? error.message : error.code)
                   : created ? L10n::tr("Se creó una nueva versión de ajustes.",
                                        "A new settings version was created.")
                             : L10n::tr("No había cambios para publicar.",
                                        "There were no changes to publish."));
        if (!failed) {
          juce::Timer::callAfterDelay(850, [safe, songId] {
            if (safe != nullptr && safe->songListPanelRaw != nullptr)
              safe->songListPanelRaw->clearCloudUploadProgress(songId);
          });
        }
      });
}

void MTControlsAudioProcessorEditor::showCloudSettingsVersions(
    const ImportedSongData &song, juce::Rectangle<int> screenAnchor) {
  if (playbackSession.isRemote()) return;
  if (cloudSettingsSheet != nullptr)
    cloudSettingsSheet->dismiss();
  cloudSettingsSheetOverlay.reset();

  // Keep the song action panel visible as context and place the version
  // chooser immediately beside it. Fall back to the song row anchor if the
  // action panel is no longer available.
  if (const auto actionPanelBounds = getActiveActionPopoverScreenBounds();
      !actionPanelBounds.isEmpty())
    screenAnchor = actionPanelBounds;

  auto content = std::make_unique<SettingsVersionsSheet>();
  cloudSettingsSheet = content.get();
  const auto sheet = cloudSettingsSheet;
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  content->onRetry = [safe, song, sheet] {
    if (safe != nullptr && sheet != nullptr)
      safe->loadCloudSettingsVersions(song, sheet);
  };
  content->onVersionSelected = [safe, song, sheet](
                                   const CloudSettingsVersion &version) {
    if (safe == nullptr || sheet == nullptr)
      return;
    sheet->dismiss();
    dismissActiveActionPopover();
    juce::MessageManager::callAsync([safe, song, version] {
      if (safe != nullptr)
        safe->showCloudSettingsVersionConfirmation(song, version);
    });
  };

  auto overlay = std::make_unique<SettingsVersionsSheetOverlay>(
      *this, std::move(content), getLocalArea(nullptr, screenAnchor));
  overlay->show();
  cloudSettingsSheetOverlay = std::move(overlay);
  loadCloudSettingsVersions(song, sheet);
}

void MTControlsAudioProcessorEditor::loadCloudSettingsVersions(
    const ImportedSongData &song,
    juce::Component::SafePointer<SettingsVersionsSheet> sheet) {
  if (sheet == nullptr)
    return;
  sheet->showLoading();
  CloudProjectLink link;
  if (!CloudSyncIndexStore::findByProjectId(projectIdentityForSong(song), link)) {
    sheet->showError(L10n::tr("Esta canción ya no está vinculada a la nube.",
                              "This song is no longer linked to the cloud."));
    return;
  }
  if (cloudSettingsClient == nullptr)
    cloudSettingsClient = std::make_unique<CloudSettingsClient>();
  const CloudTransferCredentials credentials{getApiBaseUrl(),
                                             activeSession.firebaseApiKey,
                                             activeSession.refreshToken};
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  cloudSettingsClient->list(
      credentials, link.remoteSongId,
      [safe, sheet, song](std::vector<CloudSettingsVersion> versions,
                         CloudTransferError error) mutable {
        // Late network responses never resurrect a cancelled/replaced sheet.
        if (safe == nullptr || sheet == nullptr || !sheet->isShowing())
          return;
        if (error.code.isNotEmpty()) {
          sheet->showError(L10n::tr(
              "No se pudieron consultar las versiones. Comprueba tu conexión e inténtalo de nuevo.",
              "Couldn't load versions. Check your connection and try again."));
          return;
        }
        if (!versions.empty()) {
          const auto &latest = versions.front();
          safe->rememberCloudSettingsBaseline(
              latest.projectId, latest.revision, latest.snapshotHash,
              latest.settings, versions);
        }
        if (safe->songListPanelRaw != nullptr)
          safe->songListPanelRaw->refreshFromStorageView();
        sheet->setVersions(std::move(versions));
      });
}

void MTControlsAudioProcessorEditor::showCloudSettingsVersionConfirmation(
    const ImportedSongData &song, const CloudSettingsVersion &version) {
  const auto safe =
      juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  const auto applyVersion = [safe, song, version] {
    if (safe == nullptr)
      return;
    if (!safe->applyCloudSettings(song, version.settings)) {
      safe->appModal = std::make_unique<AppModal>(
          *safe, AppModal::Kind::Information,
          L10n::tr("No se pudo aplicar", "Couldn't apply"),
          L10n::tr("La versión no pudo guardarse en este dispositivo.",
                   "The version couldn't be saved on this device."),
          L10n::tr("Entendido", "OK"), juce::String(), nullptr);
      safe->appModal->onClosed = [safe] {
        if (safe != nullptr) safe->appModal.reset();
      };
      safe->appModal->show();
      return;
    }
    if (safe->songListPanelRaw != nullptr)
      safe->songListPanelRaw->refreshFromStorageView();
  };

  appModal = std::make_unique<AppModal>(
      *this, AppModal::Kind::Confirm,
      L10n::tr("Confirmar versión", "Confirm version"),
      L10n::tr("¿Quieres aplicar la versión v", "Apply version v") +
          juce::String(version.revision) +
          L10n::tr(" en este dispositivo? Reemplazará los ajustes actuales.",
                   " on this device? It will replace the current settings."),
      L10n::tr("Aplicar versión", "Apply version"),
      L10n::tr("Cancelar", "Cancel"),
      [applyVersion](bool accepted, const juce::String &) {
        if (accepted) applyVersion();
      });
  appModal->onClosed = [safe] { if (safe != nullptr) safe->appModal.reset(); };
  appModal->show();
}

void MTControlsAudioProcessorEditor::saveCurrentProject() {
  if (projectSaveBusy || !hasCurrentLoadedSong ||
      currentLoadedSong.projectPath.isEmpty())
    return;

  flushActiveSongPersistenceNow();
  const juce::File file(currentLoadedSong.projectPath);
  if (!ProjectStore::hasPendingPortableChanges(file)) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::InfoIcon,
        L10n::tr("Proyecto actualizado", "Project up to date"),
        L10n::tr("No hay cambios pendientes para guardar.",
                 "There are no pending changes to save."));
    return;
  }
  savePortableProjects({file}, true);
}

void MTControlsAudioProcessorEditor::savePortableProjects(
    std::vector<juce::File> files, const bool showCompletionMessage,
    std::function<void(bool)> onComplete) {
  if (projectSaveBusy) {
    if (onComplete)
      onComplete(false);
    return;
  }

  files.erase(std::remove_if(files.begin(), files.end(), [](const juce::File &file) {
                return !ProjectStore::hasPendingPortableChanges(file);
              }),
              files.end());
  std::sort(files.begin(), files.end(), [](const juce::File &a,
                                           const juce::File &b) {
    return a.getFullPathName() < b.getFullPathName();
  });
  files.erase(std::unique(files.begin(), files.end()), files.end());
  if (files.empty()) {
    if (onComplete)
      onComplete(true);
    return;
  }

  projectSaveBusy = true;
  setEnabled(false);
  if (waveformBarPtr != nullptr)
    waveformBarPtr->setSongLoadingState(
        true, 0.0, L10n::tr("Preparando proyecto...", "Preparing project..."));

  const auto safe =
      juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  auto filesToSave = std::move(files);
  auto completionCallback = std::move(onComplete);
  std::thread([safe, filesToSave = std::move(filesToSave),
               showCompletionMessage,
               completionCallback = std::move(completionCallback)]() mutable {
    bool success = true;
    juce::String failure;
    for (size_t index = 0; index < filesToSave.size(); ++index) {
      juce::String error;
      const auto &file = filesToSave[index];
      const bool saved = ProjectStore::savePendingPortableChanges(
          file, &error,
          [safe, index, count = filesToSave.size(), name = file.getFileName()](
              const double progress, const juce::String &phase) {
            const auto totalProgress =
                (static_cast<double>(index) + progress) /
                static_cast<double>(count);
            juce::MessageManager::callAsync(
                [safe, totalProgress, phase, name] {
                  if (safe == nullptr || safe->waveformBarPtr == nullptr)
                    return;
                  const auto status =
                      phase.isNotEmpty()
                          ? phase + " " +
                                juce::String(static_cast<int>(
                                    std::round(totalProgress * 100.0))) +
                                "%"
                          : L10n::tr("Guardando ", "Saving ") + name + " " +
                                juce::String(static_cast<int>(
                                    std::round(totalProgress * 100.0))) +
                                "%";
                  safe->waveformBarPtr->setSongLoadingState(
                      true, totalProgress, status);
                });
          });
      if (!saved) {
        success = false;
        failure = file.getFileName() + ": " +
                  (error.isNotEmpty()
                       ? error
                       : L10n::tr("No se pudo guardar el proyecto.",
                                  "The project couldn't be saved."));
        break;
      }
    }

    juce::MessageManager::callAsync(
        [safe, success, failure, showCompletionMessage,
         finalCompletion = std::move(completionCallback)]() mutable {
          if (safe != nullptr) {
            safe->projectSaveBusy = false;
            safe->setEnabled(true);
            if (safe->waveformBarPtr != nullptr)
              safe->waveformBarPtr->clearSongLoadingState();
          }
          if (success && showCompletionMessage) {
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                L10n::tr("Proyecto guardado", "Project saved"),
                L10n::tr("El archivo .pwproj fue actualizado correctamente.",
                         "The .pwproj file was updated successfully."));
          } else if (!success) {
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                L10n::tr("No se pudo guardar", "Couldn't save"), failure);
          }
          if (finalCompletion)
            finalCompletion(success);
        });
  }).detach();
}

void MTControlsAudioProcessorEditor::requestApplicationQuit(
    std::function<void(bool)> onResolved) {
  flushActiveSongPersistenceNow();
  const auto pending = ProjectStore::getPendingPortableProjectFiles();
  if (pending.empty()) {
    if (onResolved)
      onResolved(true);
    return;
  }

  const auto count = static_cast<int>(pending.size());
  const auto message =
      count == 1
          ? L10n::tr(
                "Hay cambios guardados en este dispositivo que aún no están dentro del archivo .pwproj.",
                "There are changes saved on this device that are not yet inside the .pwproj file.")
          : L10n::tr("Hay cambios pendientes en ",
                     "There are pending changes in ") +
                juce::String(count) +
                L10n::tr(" proyectos.", " projects.");

  auto resolvedCallback = std::move(onResolved);
  juce::AlertWindow::showAsync(
      juce::MessageBoxOptions()
          .withIconType(juce::MessageBoxIconType::WarningIcon)
          .withTitle(L10n::tr("Guardar antes de salir", "Save before quitting"))
          .withMessage(message)
          .withButton(L10n::tr("Guardar y salir", "Save and quit"))
          .withButton(L10n::tr("Salir sin actualizar", "Quit without updating"))
          .withButton(L10n::tr("Cancelar", "Cancel")),
      [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this),
       pending, resolvedCallback = std::move(resolvedCallback)](
          const int result) mutable {
        if (result == 1 && safe != nullptr) {
          safe->savePortableProjects(std::move(pending), false,
                                     std::move(resolvedCallback));
          return;
        }
        if (resolvedCallback)
          resolvedCallback(result == 2);
      });
}

void MTControlsAudioProcessorEditor::parentHierarchyChanged() {
#if MTCONTROLS_NATIVE_APP && !JUCE_ANDROID
  auto *top = getTopLevelComponent();
  if (top == nullptr || top == this)
    return;

#if JUCE_MAC
  if (auto *window = dynamic_cast<juce::TopLevelWindow *>(top);
      window != nullptr && !window->isUsingNativeTitleBar()) {
    window->setUsingNativeTitleBar(true);
    top = getTopLevelComponent();
    if (top == nullptr || top == this)
      return;
  }
#endif

  if (initialStandaloneWindowCentered)
    return;

  auto &displays = juce::Desktop::getInstance().getDisplays();
  const auto *display = displays.getDisplayForRect(top->getBounds());
  if (display == nullptr)
    display = displays.getPrimaryDisplay();
  if (display == nullptr)
    return;

  // Standalone is intentionally work-area maximised from the first welcome
  // screen onward. Do not apply this to an editor hosted by a DAW.
  expandTopLevelWindowToPrimaryWorkArea(this);
  initialStandaloneWindowCentered = true;
#endif
}

void MTControlsAudioProcessorEditor::fitMobileEditorToHost() {
#if JUCE_IOS
  // Once attached, the host owns the editor's size. A cached display rectangle
  // can still describe the previous orientation while UIKit is rotating.
  if (auto *parent = getParentComponent()) {
    parent->resized();
    return;
  }
#endif
#if JUCE_IOS || JUCE_ANDROID
  if (const auto *display =
          juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
    setSize(display->userArea.getWidth(), display->userArea.getHeight());
  }
#endif
}

bool MTControlsAudioProcessorEditor::shouldUsePhonePortraitOrientation() const {
  // iPhone authentication uses portrait from launch through every auth step.
  // Android keeps its existing startup policy, including the deferred fast boot.
#if JUCE_IOS
  if (!mainUiInitialized ||
      (welcomeModal != nullptr && welcomeModal->isVisible()) ||
      (reauthPromptModal != nullptr && reauthPromptModal->isVisible()))
    return true;
#endif
  return isSetlistDrawerOpen || settingsOverlay != nullptr ||
         practiceOverlay != nullptr || teamsOverlay != nullptr;
}

void MTControlsAudioProcessorEditor::updatePhoneOrientation() {
#if JUCE_IOS || JUCE_ANDROID
  if (!isPhoneSizedLayout(*this) || phoneOrientationUpdatePending)
    return;
  const int desired = shouldUsePhonePortraitOrientation()
                          ? juce::Desktop::upright
                          : juce::Desktop::rotatedClockwise |
                                juce::Desktop::rotatedAntiClockwise;
  if (juce::Desktop::getInstance().getOrientationsEnabled() == desired)
    return;
#if JUCE_ANDROID
  juce::Desktop::getInstance().setOrientationsEnabled(desired);
#else
  phoneOrientationUpdatePending = true;
  // Changing the scene's orientation from inside resized() can nest native
  // layout callbacks. Coalesce requests and use the latest auth/overlay state.
  juce::MessageManager::callAsync(
      [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] {
        if (safe == nullptr)
          return;
        safe->phoneOrientationUpdatePending = false;
        juce::Desktop::getInstance().setOrientationsEnabled(
            safe->shouldUsePhonePortraitOrientation()
                ? juce::Desktop::upright
                : juce::Desktop::rotatedClockwise |
                      juce::Desktop::rotatedAntiClockwise);
      });
#endif
#endif
}

ResponsiveLayoutProfile MTControlsAudioProcessorEditor::resolveResponsiveLayoutProfile()
    const {
  switch (AppSettings::getLayoutMode()) {
  case AppLayoutMode::Compact:
    return ResponsiveLayoutProfile::Compact;
  case AppLayoutMode::Standard:
    return ResponsiveLayoutProfile::Standard;
  case AppLayoutMode::Large:
    return ResponsiveLayoutProfile::Large;
  case AppLayoutMode::Auto:
  default:
    break;
  }

  const int w = getWidth() > 0 ? getWidth() : kDefaultWindowW;
  const int h = getHeight() > 0 ? getHeight() : kDefaultWindowH;
  if (w < kCompactBreakpointW || h < kCompactBreakpointH)
    return ResponsiveLayoutProfile::Compact;
  if (w >= kLargeBreakpointW && h >= kLargeBreakpointH)
    return ResponsiveLayoutProfile::Large;
  return ResponsiveLayoutProfile::Standard;
}

void MTControlsAudioProcessorEditor::applyResponsiveLayoutProfile(
    const ResponsiveLayoutProfile profile) {
  responsiveLayoutProfile = profile;

  if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
    bar->setResponsiveProfile(profile);
  if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
    grid->setResponsiveProfile(profile);
  if (padsComponent != nullptr && phonePadsPopoverContent == nullptr)
    padsComponent->setResponsiveProfile(profile);
}

void MTControlsAudioProcessorEditor::showPhonePadsPopover(
    juce::Component &anchor) {
  if (!isPhoneSizedLayout(*this) || padsComponent == nullptr)
    return;
  if (phonePadsOpen) {
    dismissActiveActionPopover();
    return;
  }
  // A dismissed callout can still be finishing its modal teardown. Wait until
  // it has returned the shared PadsComponent before opening another one.
  if (phonePadsPopoverContent != nullptr)
    return;

  isSetlistDrawerOpen = false;
  isPadsCollapsed = true;
  if (padsHandlePanel != nullptr)
    padsHandlePanel->setCollapsed(true);
  resized();

  auto safeEditor = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  auto safePads = juce::Component::SafePointer<PadsComponent>(padsComponent.get());
  padsComponent->setResponsiveProfile(ResponsiveLayoutProfile::Compact);
  padsComponent->setPopoverPresentation(true);
  auto content = std::make_unique<PadsPopoverContent>(
      *padsComponent, [safeEditor, safePads] {
        if (safeEditor == nullptr)
          return;
        if (safePads != nullptr) {
          safeEditor->addChildComponent(*safePads);
          safePads->setVisible(false);
          safePads->setPopoverPresentation(false);
          safePads->setResponsiveProfile(safeEditor->resolveResponsiveLayoutProfile());
        }
        safeEditor->phonePadsPopoverContent = nullptr;
        safeEditor->phonePadsOpen = false;
        safeEditor->resized();
      });
  phonePadsPopoverContent = content.get();
  ActionPopoverOptions options;
  options.compactMaximumWidth = 520;
  presentActionPopover(std::move(content),
                       getLocalArea(&anchor, anchor.getLocalBounds()), this,
                       options);
  phonePadsOpen = true;
  if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
    bar->setPhoneOverlayStates(false, true);
  juce::MessageManager::callAsync([safePads] {
    if (safePads != nullptr && safePads->isShowing())
      safePads->grabKeyboardFocus();
  });
}

void MTControlsAudioProcessorEditor::updateResponsiveLayoutProfile() {
  const auto nextProfile = resolveResponsiveLayoutProfile();
  if (nextProfile != responsiveLayoutProfile)
    applyResponsiveLayoutProfile(nextProfile);
}

void MTControlsAudioProcessorEditor::refreshCloudLibrary(
    const juce::String &query) {
  if (localDemoSession || !activeSession.allowed || activeSession.firebaseApiKey.isEmpty() ||
      activeSession.refreshToken.isEmpty()) {
    ++cloudLibrarySearchGeneration;
    cloudCatalogSongs.clear();
    if (songListPanelRaw != nullptr) {
      songListPanelRaw->setCloudSongs({});
      songListPanelRaw->setCloudSearchLoading(false);
    }
    return;
  }
  if (cloudLibraryClient == nullptr)
    cloudLibraryClient = std::make_unique<CloudLibraryClient>();

  const auto generation = ++cloudLibrarySearchGeneration;
  const auto uid = activeSession.uid;
  const auto safeThis =
      juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  cloudLibraryClient->search(
      getApiBaseUrl(), activeSession.firebaseApiKey, activeSession.refreshToken,
      query, {}, 50,
      [safeThis, generation, uid](CloudSongPage page, CloudRequestError error) {
        if (safeThis == nullptr || generation != safeThis->cloudLibrarySearchGeneration ||
            uid != safeThis->activeSession.uid)
          return;
        if (safeThis->songListPanelRaw != nullptr) {
          safeThis->songListPanelRaw->setCloudSearchLoading(false);
          if (error.code.isNotEmpty()) {
            if (error.httpCode == 401 || error.httpCode == 403 || error.httpCode == 404) {
              safeThis->cloudCatalogSongs.clear();
              safeThis->songListPanelRaw->setCloudSongs({});
            }
            return;
          }
          safeThis->refreshCloudSettingsBaselines(page.songs);
          safeThis->cloudCatalogSongs = page.songs;
          safeThis->songListPanelRaw->setCloudSongs(std::move(page.songs));
        }
      });
}

void MTControlsAudioProcessorEditor::showServiceTermsIfNeeded() {
  if (serviceTermsRequestPending || localDemoSession || !activeSession.allowed ||
      activeSession.uid.isEmpty() || activeSession.refreshToken.isEmpty()) return;
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  // Consent belongs to an authenticated account. Do not open an informational
  // prompt over login or fall back to one after a failed Cloud request.
  serviceTermsRequestPending = true;
  if (!serviceTermsClient) serviceTermsClient = std::make_unique<CloudTransferClient>();
  const auto uid = activeSession.uid;
  serviceTermsClient->requestTerms({getApiBaseUrl(), activeSession.firebaseApiKey, activeSession.refreshToken},
    [safe, uid](CloudTransferError error) {
      if (!safe) return;
      safe->serviceTermsRequestPending = false;
      if (safe->activeSession.uid != uid) return;
      if (error.code.isEmpty()) AppSettings::acceptServiceTermsLocally(ServiceTermsPolicy::version, juce::Time::currentTimeMillis());
      // Closing/refusing records nothing; local playback and imports continue.
    });
}

void MTControlsAudioProcessorEditor::initializeMainUi() {
#if JUCE_ANDROID
  const auto androidUiBuildStartedMs = juce::Time::getMillisecondCounterHiRes();
  const auto logAndroidUiPhase = [androidUiBuildStartedMs](const char *phase) {
    SessionAuthLogger::write(
        "initializeMainUi(android): " + juce::String(phase) + " elapsedMs=" +
        juce::String(juce::Time::getMillisecondCounterHiRes() -
                         androidUiBuildStartedMs,
                     1));
  };
#endif
  if (!localDemoSession && lastSuccessfulLoginMs > 0) {
    const auto nowMs = juce::Time::currentTimeMillis();
    const auto sessionDueAtMs = lastSuccessfulLoginMs + kSessionReauthIntervalMs;
    if (nowMs >= sessionDueAtMs) {
      // El bloqueo en arranque solo aplica si el usuario ya eligio "Mas tarde"
      // y su periodo de gracia ya vencio.
      if (reauthGraceDeadlineMs > 0 && nowMs >= reauthGraceDeadlineMs) {
        showWelcomeOnlyMode();
        if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get())) {
          modal->setStatusText(
              "Tu periodo de gracia termino. Debes iniciar sesion para continuar.",
              true);
        }
        return;
      }
    }
  }

  if (mainUiInitialized)
    return;

  static_cast<MTControlsAudioProcessor &>(processor).startPadsPreloadAsync(!localDemoSession);

  {
    const auto audioCtx = getStandaloneAudioContext(static_cast<MTControlsAudioProcessor &>(processor));
    const auto nowMs = juce::Time::currentTimeMillis();
    nextAudioDeviceFallbackPollMs = nowMs + kAudioDevicePollIntervalMs;
    missingAudioDeviceSinceMs = 0;
    nextAudioDeviceFallbackAttemptMs = nowMs + kAudioDeviceMissingGraceMs;
    lastObservedAudioDeviceKey = getCurrentDeviceKey(audioCtx);

    if (audioCtx.manager != nullptr) {
      enforceStandaloneOutputOnlyAudio(*audioCtx.manager);
      if (auto *currentDevice = audioCtx.manager->getCurrentAudioDevice();
          currentDevice != nullptr) {
        auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
        processorRef.getAudioDeviceBridge().setAvailableOutputChannelCount(
            juce::jmax(1, currentDevice->getActiveOutputChannels().countNumberOfSetBits()));
        lastObservedActiveOutputChannels = currentDevice->getActiveOutputChannels();
      } else {
        lastObservedActiveOutputChannels.clear();
        missingAudioDeviceSinceMs = nowMs;
      }
    }
  }

  if (topBar != nullptr && waveformBar != nullptr &&
      channelGridPanel != nullptr && songListPanel != nullptr &&
      setlistHandlePanel != nullptr) {
    topBar->setVisible(true);
    waveformBar->setVisible(!shouldCollapseWaveformArea());
    channelGridPanel->setVisible(true);
    songListPanel->setVisible(true);
    setlistHandlePanel->setVisible(true);
    if (padsHandlePanel != nullptr)
      padsHandlePanel->setVisible(true);
    if (padsComponent != nullptr)
      padsComponent->setVisible(!isPadsCollapsed);
    if (welcomeModal)
      welcomeModal->setVisible(false);
    if (reauthPromptModal)
      reauthPromptModal->setVisible(false);
    setResizable(true, true);
#if JUCE_IOS || JUCE_ANDROID
    setResizeLimits(320, 280, 32000, 32000);
#else
    setResizeLimits(kMinWindowW, kMinWindowH, 32000, 32000);
#endif

#if JUCE_IOS || JUCE_ANDROID
    fitMobileEditorToHost();
#else
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] {
          if (safe != nullptr)
            expandTopLevelWindowToPrimaryWorkArea(safe.getComponent());
        });
#endif

    applyResponsiveLayoutProfile(resolveResponsiveLayoutProfile());
    if (!autoSetlistCollapseInitialized) {
      isSetlistCollapsed = false;
      autoSetlistCollapseInitialized = true;
    }
    mainUiInitialized = true;
    resized();
    updateSystemUsageMetrics();
    return;
  }

  auto &p = static_cast<MTControlsAudioProcessor &>(processor);
  auto midiLearnBindingLabelForTarget = [&p](const int targetId) -> juce::String {
    if (targetId < 0)
      return {};
    const auto bindings = p.getMidiBindings();
    const auto it =
        std::find_if(bindings.begin(), bindings.end(),
                     [targetId](const MTControlsAudioProcessor::MidiBinding &binding) {
                       return binding.targetId == targetId;
                     });
    if (it == bindings.end())
      return {};
    return it->sourceType == MidiBindingSourceType::Note
               ? ("N" + juce::String(it->ccNumber))
               : ("CC" + juce::String(it->ccNumber));
  };

  auto topBarImpl = std::make_unique<TopBar>(p, &playbackSession);
#if JUCE_ANDROID
  logAndroidUiPhase("topBar.created");
#endif
  auto *topBarRaw = topBarImpl.get();
  topBar = std::move(topBarImpl);
  topBarRaw->setResponsiveProfile(resolveResponsiveLayoutProfile());
  topBarRaw->setTransportProviders(
      [this] { return playbackSession.isSongPlaying(); },
      [this] { return playbackSession.getSongCurrentPositionSeconds(); },
      [this] {
        return playbackSession.isCustomArrangementActive() ? playbackSession.getSongLengthSeconds()
                                             : playbackSession.getSongEndSeconds();
      },
      [this] { return playbackSession.getLoadedSongKey(); },
      [this] { return currentLoadedSong.id; },
      [this]() -> const ImportedSongData * {
        if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
          return nullptr;
        return &currentLoadedSong;
      });
  topBarRaw->setOnMetronomeEnabledChanged([this](const bool enabled) {
    persistClickDynMetronomeEnabledForActiveSong(enabled);
  });
  topBarRaw->setMidiLearnBindingLabelProvider(midiLearnBindingLabelForTarget);

  auto waveformBarImpl = std::make_unique<WaveformBar>();
#if JUCE_ANDROID
  logAndroidUiPhase("waveform.created");
#endif
  auto *waveformBarRaw = waveformBarImpl.get();
  waveformBarPtr = waveformBarRaw;
  waveformBar = std::move(waveformBarImpl);
  topBarRaw->setLiveModeControls(
      [waveformBarRaw] { waveformBarRaw->setLiveMode(!waveformBarRaw->isLiveMode()); },
      [waveformBarRaw] { return waveformBarRaw->isLiveMode(); },
      [waveformBarRaw] { return waveformBarRaw->canToggleLiveMode(); });
#if !JUCE_IOS && !JUCE_ANDROID
  lyricsPanel = std::make_unique<LyricsPanel>();
  lyricsPanel->setClient(&p.getFreeShowClient());
  lyricsPanel->setTransportProviders(
      [&p] { return p.getSongCurrentPositionSeconds(); },
      [&p] { return p.isSongPlaying(); });
  lyricsPanel->setTimelineMarksProvider([waveformBarRaw](juce::Range<double> visible, float width) {
    return waveformBarRaw->getPracticeTimelineMarks(visible, width);
  });
  lyricsPanel->onTimelinePlayRequested = [this](double seconds) {
    if (!hasCurrentLoadedSong) return;
    playbackSession.seekSongTo(seconds);
    if (!playbackSession.isSongPlaying()) playbackSession.toggleSongPlayPause();
  };
  lyricsPanel->onConnectionSettingsRequested = [this] { showSettings({}, this, true); };
  lyricsPanel->onChanged = [this, &p](const playworship::lyrics::SongLyrics &lyrics) {
    if (playbackSession.isRemote() || !hasCurrentLoadedSong) return;
    juce::String error;
    if (!SongLibraryStore::updateSongFreeShowCues(currentLoadedSong.id, lyrics, &error)) {
      lyricsPanel->setSongLyrics(p.getSongLyrics());
      juce::NativeMessageBox::showMessageBoxAsync(
          juce::MessageBoxIconType::WarningIcon, L10n::tr("Letras", "Lyrics"),
          L10n::tr("No se pudieron guardar las marcas. ", "Could not save marks. ") + error);
      return;
    }
    currentLoadedSong.freeShowCues = lyrics;
    p.setSongLyrics(lyrics);
  };
  topBarRaw->setLyricsViewControls(
      [this](bool active) {
        lyricsViewActive = active;
        resized();
        repaint();
      },
      [this] {
        return lyricsViewActive && !playbackSession.isRemote() &&
               hasCurrentLoadedSong && !currentLoadedSong.blankSong;
      },
      [this, waveformBarRaw] {
        return !playbackSession.isRemote() && hasCurrentLoadedSong &&
               !currentLoadedSong.blankSong && waveformBarRaw->canToggleLiveMode();
      });
#endif
  waveformBarRaw->setMidiLearnBindingLabelProvider(
      midiLearnBindingLabelForTarget);
  waveformBarRaw->setTransportProviders(
      [this] { return playbackSession.getSongCurrentPositionSeconds(); },
      [this] { return playbackSession.getSongLengthSeconds(); },
      [this] { return playbackSession.isSongPlaying(); });
  waveformBarRaw->setSongSourceLengthProvider(
      [this] { return playbackSession.getLoadedSongLengthSeconds(); });
  waveformBarRaw->setSongEndCustomizedProvider(
      [this] { return playbackSession.isSongEndCustomized(); });
  waveformBarRaw->setSongWindowProviders(
      [this] { return playbackSession.getSongStartSeconds(); },
      [this] { return playbackSession.getSongEndSeconds(); });
  waveformBarRaw->setSeekCallback([this](double targetSeconds) {
    playbackSession.seekSongTo(targetSeconds);
  });
  playbackSession.sectionAutomationCanEdit = [this] {
    return remoteSectionAutomationOwner.empty();
  };
  playbackSession.sectionAutomationStemCanEdit = [this](int stemIndex) {
    if (remoteSectionAutomationOwner.empty()) return true;
    int includedIndex = 0;
    for (const auto &stem : currentLoadedSong.stems) {
      if (!stem.include) continue;
      if (includedIndex++ == stemIndex)
        return stemPriorityForStripOrder(stem) < 2;
    }
    return false;
  };
  const auto persistSectionAutomation = [this, &p] {
    if (playbackSession.isRemote() || !hasCurrentLoadedSong) return;
    const auto snapshot = p.getSongArrangementSnapshot();
    if (!SongLibraryStore::updateSongArrangement(
            currentLoadedSong.id, snapshot, p.isCustomArrangementActive())) {
      SessionAuthLogger::write("[Automation] Couldn't save section controls for " +
                               currentLoadedSong.id);
      appModal = std::make_unique<AppModal>(
          *this, AppModal::Kind::Information,
          L10n::tr("Automatización", "Automation"),
          L10n::tr("No se pudieron guardar los ajustes de la sección.",
                   "The section settings could not be saved."),
          L10n::tr("Entendido", "OK"), juce::String(), nullptr);
      const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
      appModal->onClosed = [safe] { if (safe) safe->appModal.reset(); };
      appModal->show();
    }
    currentLoadedSong.arrangement = snapshot;
    if (waveformBarPtr) {
      waveformBarPtr->setCommittedSectionAutomation(snapshot);
      waveformBarPtr->setSectionAutomationState({});
    }
  };
  waveformBarRaw->setSectionAutomationCallbacks(
      [this](const juce::String &partId) {
        if (!playbackSession.canEditSectionAutomation()) return false;
        if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
          grid->flushPendingPersistenceNow();
        return playbackSession.beginSectionAutomationEditing(partId);
      },
      [this, persistSectionAutomation](bool save) {
        if (!playbackSession.canEditSectionAutomation()) return;
        if (playbackSession.finishSectionAutomationEditing(save) && !playbackSession.isRemote()) {
          if (save) persistSectionAutomation();
          else if (waveformBarPtr) waveformBarPtr->setSectionAutomationState({});
        }
      },
      [this, persistSectionAutomation](const juce::String &partId) {
        if (playbackSession.canEditSectionAutomation() &&
            playbackSession.clearSectionAutomation(partId) && !playbackSession.isRemote())
          persistSectionAutomation();
      },
      [this] { return playbackSession.getSectionAutomationEditingPartId(); },
      [this] { return playbackSession.canEditSectionAutomation(); });
  // Preparation preserves playback/Loop; confirmation queues the next valid boundary.
  waveformBarRaw->setOnArrangementPartJumpRequest(
      [this, waveformBarRaw](int partIndex) {
        playbackSession.requestArrangementPartJumpById(waveformBarRaw->getArrangementPartId(partIndex));
      });
  waveformBarRaw->setOnArrangementPartLoopToggleRequest([this, waveformBarRaw](int partIndex) {
    // First-time loop arming never seeks. Replacing a currently sounding
    // section loop prepares a boundary handoff and keeps the outgoing loop
    // until arrival. The processor owns cancellation and validates the ID
    // before changing any intent, for both local and remote controls.
    playbackSession.toggleArrangementPartLoopById(waveformBarRaw->getArrangementPartId(partIndex));
  });
  waveformBarRaw->setOnArrangementPendingJumpClearRequest([this] {
    playbackSession.clearPendingArrangementPartJump();
  });
  waveformBarRaw->setPreparingArrangementPartProvider(
      [this, waveformBarRaw] {
        return waveformBarRaw->getArrangementPartIndex(playbackSession.getPreparingArrangementPartJumpId());
      });
  waveformBarRaw->setPendingArrangementPartProvider(
      [this, waveformBarRaw] {
        return waveformBarRaw->getArrangementPartIndex(playbackSession.getPendingArrangementPartJumpId());
      });
  waveformBarRaw->setActiveArrangementLoopPartProvider(
      [this, waveformBarRaw] {
        return waveformBarRaw->getArrangementPartIndex(playbackSession.getArrangementPartLoopId());
      });
  waveformBarRaw->setArrangementControlFeedbackProviders(
      [this](const juce::String &partId) {
        const auto error = playbackSession.getArrangementControlError(partId);
        if (error == "Update the server app to control sections safely")
          return L10n::tr("Actualiza la app del servidor para controlar las secciones.", "Update the server app to control sections safely");
        if (error == "The song changed; select the section again")
          return L10n::tr("La canción cambió; selecciona la sección de nuevo.", "The song changed; select the section again");
        if (error == "The arrangement changed; select the section again")
          return L10n::tr("El arreglo cambió; selecciona la sección de nuevo.", "The arrangement changed; select the section again");
        if (error == "Wait for the song to finish loading")
          return L10n::tr("Espera a que termine de cargar la canción.", "Wait for the song to finish loading");
        if (error == "The section is unavailable; select another section")
          return L10n::tr("La sección no está disponible; selecciona otra.", "The section is unavailable; select another section");
        if (error == "The section request timed out; try again")
          return L10n::tr("La solicitud no se confirmó; inténtalo de nuevo.", "The section request timed out; try again");
        return error;
      },
      [this](const juce::String &partId) { return playbackSession.isArrangementControlPending(partId); },
      [this](const juce::String &partId) {
        const auto error = playbackSession.getArrangementControlError(partId);
        if (error == "Update the server app to control sections safely")
          return L10n::tr("Actualiza servidor", "Update server");
        if (error == "Update the client app to control sections safely")
          return L10n::tr("Actualiza cliente", "Update client");
        return L10n::tr("No aplicado", "Not applied");
      });
  waveformBarRaw->setOnEditToggle([this](const bool editing) {
    auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
    if (!playbackSession.isRemote()) processorRef.setArrangementEditingActive(editing);
    resized();
    repaint();
  });
  {
    const playworship::analysis::SectionDetectionService detection;
    waveformBarRaw->setSectionDetectionAvailability(
        detection.available() || detection.guideAvailable(),
        detection.guideAvailable() ? juce::String{} : detection.unavailableReason());
    waveformBarRaw->setOnSectionDetectionRequest([this] { showSectionDetection(); });
    waveformBarRaw->setGuideAlignmentCallbacks(
        [this](double origin) {
          if (!guideReviewTranscript || guideReviewSongId != currentLoadedSong.id) return;
          auto options = guideReviewDraft.options;
          options.originSeconds = origin;
          rebuildGuideSectionReview(options);
        },
        [this](const juce::String &id, double seconds, bool snapToBars) {
          if (!guideReviewTranscript || guideReviewSongId != currentLoadedSong.id || !waveformBarPtr) return;
          juce::String error;
          if (!playworship::analysis::moveGuideSectionBoundary(guideReviewDraft, id, seconds, snapToBars, error)) {
            waveformBarPtr->setGuideAlignmentError(error); return;
          }
          waveformBarPtr->setGuideAlignmentPreview(guideReviewDraft);
        },
        [this] { finishGuideSectionReview(true); },
        [this] { finishGuideSectionReview(false); });
    waveformBarRaw->setOnGuideSecondaryCueMove([this](const juce::String &id, double seconds, bool snapToBeats) {
      if (!guideReviewTranscript || guideReviewSongId != currentLoadedSong.id || !waveformBarPtr) return;
      juce::String error;
      if (!playworship::analysis::moveGuideSecondaryCue(guideReviewDraft, id, seconds, snapToBeats, error)) {
        waveformBarPtr->setGuideAlignmentError(error); return;
      }
      for (const auto &cue : guideReviewDraft.secondaryCues) if (cue.id == id)
        guideReviewSecondaryTimes[id] = cue.timeSeconds;
      waveformBarPtr->setGuideAlignmentPreview(guideReviewDraft);
    });
    waveformBarRaw->setOnGuideCatalogSelection([this](juce::String id,
        playworship::analysis::GuideCatalogChoice choice) {
      if (!guideReviewTranscript || guideReviewSongId != currentLoadedSong.id || !waveformBarPtr) return;
      juce::String error;
      if (!playworship::analysis::chooseGuideCatalogMark(guideReviewDraft, id, choice, error)) {
        waveformBarPtr->setGuideAlignmentError(error); return;
      }
      guideReviewNames.erase(id); guideReviewSecondaryTimes.erase(id);
      for (const auto &part : guideReviewDraft.suggestions)
        if (part.id == id) guideReviewNames[id] = part.displayName;
      for (const auto &cue : guideReviewDraft.secondaryCues)
        if (cue.id == id) {
          guideReviewNames[id] = cue.name;
          if (cue.userEdited) guideReviewSecondaryTimes[id] = cue.timeSeconds;
        }
      waveformBarPtr->setGuideAlignmentPreview(guideReviewDraft);
    });
    waveformBarRaw->setOnGuideAlignmentRename([this](const juce::String &id, const juce::String &name) {
      if (!guideReviewTranscript || guideReviewSongId != currentLoadedSong.id || !waveformBarPtr) return;
      const auto cleaned = name.trim();
      if (cleaned.isEmpty() || cleaned.length() > 128) {
        waveformBarPtr->setGuideAlignmentError(L10n::tr("Escribe un nombre de hasta 128 caracteres.", "Enter a name of up to 128 characters.")); return;
      }
      const auto key = playworship::guide::normalizedWords(cleaned);
      for (const auto &entry : playworship::analysis::guideCatalogEntries())
        if (key == playworship::guide::normalizedWords(entry.key) ||
            key == playworship::guide::normalizedWords(entry.label)) {
          juce::String error;
          if (!playworship::analysis::chooseGuideCatalogMark(guideReviewDraft, id, entry, error)) {
            waveformBarPtr->setGuideAlignmentError(error); return;
          }
          guideReviewNames.erase(id); guideReviewSecondaryTimes.erase(id);
          for (const auto &part : guideReviewDraft.suggestions) if (part.id == id) guideReviewNames[id] = part.displayName;
          for (const auto &cue : guideReviewDraft.secondaryCues) if (cue.id == id) guideReviewNames[id] = cue.name;
          waveformBarPtr->setGuideAlignmentPreview(guideReviewDraft); return;
        }
      waveformBarPtr->setGuideAlignmentError(L10n::tr("Elige una marca del catálogo.", "Choose a mark from the catalogue."));
    });
    waveformBarRaw->setOnGuideAlignmentRemove([this](const juce::String &id) {
      if (!guideReviewTranscript || guideReviewSongId != currentLoadedSong.id || !waveformBarPtr) return;
      if (std::any_of(guideReviewDraft.pendingAnnouncements.begin(), guideReviewDraft.pendingAnnouncements.end(),
            [&](const auto &pending) { return pending.id == id; })) {
        juce::String error;
        if (!playworship::analysis::discardGuidePendingAnnouncement(guideReviewDraft, id, error)) {
          waveformBarPtr->setGuideAlignmentError(error); return;
        }
        guideReviewDiscarded.insert(id.toStdString());
        guideReviewNames.erase(id); guideReviewSecondaryTimes.erase(id);
        waveformBarPtr->setGuideAlignmentPreview(guideReviewDraft); return;
      }
      const auto found = std::find_if(guideReviewDraft.suggestions.begin(), guideReviewDraft.suggestions.end(),
          [&id](const auto &suggestion) { return suggestion.id == id; });
      if (found == guideReviewDraft.suggestions.end()) {
        const auto cue = std::find_if(guideReviewDraft.secondaryCues.begin(), guideReviewDraft.secondaryCues.end(),
            [&id](const auto &candidate) { return candidate.id == id; });
        if (cue == guideReviewDraft.secondaryCues.end()) return;
        juce::String error;
        if (!playworship::analysis::discardGuideSecondaryCue(guideReviewDraft, id, error)) {
          waveformBarPtr->setGuideAlignmentError(error); return;
        }
        guideReviewDiscarded.insert(id.toStdString());
        guideReviewSecondaryTimes.erase(id);
        waveformBarPtr->setGuideAlignmentPreview(guideReviewDraft);
        return;
      }
      guideReviewDiscarded.insert(id.toStdString());
      const auto index = size_t(std::distance(guideReviewDraft.suggestions.begin(), found));
      guideReviewDraft.suggestions.erase(found);
      if (index > 0) guideReviewDraft.suggestions[index - 1].endSeconds = index < guideReviewDraft.suggestions.size()
          ? guideReviewDraft.suggestions[index].startSeconds : guideReviewDraft.durationSeconds;
      waveformBarPtr->setGuideAlignmentPreview(guideReviewDraft);
    });
  }
  waveformBarRaw->setOnWaveformHeightToggle([this](bool expanded) {
    isWaveformHeightExpanded = expanded;
    resized();
    repaint();
  });
  waveformBarRaw->setOnArrangementChanged(
      [this, &p](const SongArrangementSnapshot &arrangement,
                 bool isCustomArrangement) {
        if (playbackSession.isRemote()) {
          ProjectSongSettings settings; settings.arrangement = arrangement; settings.arrangementCustomized = isCustomArrangement;
          playbackSession.send("arrange.set", ProjectStore::serializeSettings(settings)); return;
        }
        if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
          return;
        if (!SongLibraryStore::updateSongArrangement(
                currentLoadedSong.id, arrangement, isCustomArrangement)) {
          SessionAuthLogger::write(
              "[Arrangement] Couldn't persist changes for song id=" +
              currentLoadedSong.id);
        }
        p.setSongArrangement(arrangement, isCustomArrangement);
#if !JUCE_IOS && !JUCE_ANDROID
        if (lyricsPanel) {
          lyricsPanel->setArrangement(arrangement);
          lyricsPanel->setDuration(p.getSongLengthSeconds());
        }
#endif
      });
  waveformBarRaw->setOnArrangementReset([this, &p] {
    if (playbackSession.isRemote()) { playbackSession.send("arrange.reset"); return; }
    if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
      return;
    if (!SongLibraryStore::resetSongArrangement(currentLoadedSong.id)) {
      SessionAuthLogger::write(
          "[Arrangement] Couldn't reset persisted arrangement for song id=" +
          currentLoadedSong.id);
    }
    SongArrangementSnapshot arrangement;
    bool isCustomArrangement = false;
    if (SongLibraryStore::getSongArrangement(currentLoadedSong.id, arrangement,
                                             isCustomArrangement)) {
      p.setSongArrangement(arrangement, isCustomArrangement);
#if !JUCE_IOS && !JUCE_ANDROID
      if (lyricsPanel) {
        lyricsPanel->setArrangement(arrangement);
        lyricsPanel->setDuration(p.getSongLengthSeconds());
      }
#endif
    }
  });
  waveformBarRaw->setOnTempoMapChanged([this](const SongTempoMapSnapshot &tempoMap) {
    if (playbackSession.isRemote()) { playbackSession.send("tempoMap.set", playworship::remote::encodeTempoMap(tempoMap)); return; }
    if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
      return;
    currentLoadedSong.tempoMap = tempoMap;
    SongLibraryStore::updateSong(currentLoadedSong);
  });
  waveformBarRaw->setOnActiveTempoRegionChanged(
      [this, &p](const SongTempoRegionSnapshot &region) {
        if (playbackSession.isRemote()) return;
        p.setMetronomeBpm(region.bpm);
        p.setMetronomeBeatsPerBar(
            timeSignatureBeatsPerBar(region.timeSignature));
      });
  waveformBarRaw->setOnExportMixRequested(
      [this](const juce::String &format) { beginExportCurrentSongMix(format); });
  waveformBarRaw->setSongActionsStateProvider([this] {
    WaveformSongActionsState state;
    state.localAnalysisAvailable = hasCurrentLoadedSong && !playbackSession.isRemote();
    if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
      return state;
    CloudProjectLink link;
    state.cloudLinked = CloudSyncIndexStore::findByProjectId(
        projectIdentityForSong(currentLoadedSong), link);
    if (!state.cloudLinked)
      return state;
    state.settingsPending = hasPendingCloudSettings(currentLoadedSong);
    state.settingsActionBusy =
        songLoadBusy || mixExportBusy || projectSaveBusy ||
        cloudSettingsInProgress.count(currentLoadedSong.id.toStdString()) != 0;
    return state;
  });
  waveformBarRaw->setOnCloudSettingsSyncRequested([this] {
    if (hasCurrentLoadedSong)
      confirmCloudSettingsSync(currentLoadedSong);
  });
  waveformBarRaw->setOnCloudSettingsVersionsRequested(
      [this](const juce::Rectangle<int> screenAnchor) {
        if (hasCurrentLoadedSong)
          showCloudSettingsVersions(currentLoadedSong, screenAnchor);
      });

  auto channelGridImpl =
      std::make_unique<ChannelGridPanel>(stripLookAndFeel, p, &playbackSession);
#if JUCE_ANDROID
  logAndroidUiPhase("channelGrid.created");
#endif
  auto *channelGridRaw = channelGridImpl.get();
  channelGridPanel = std::move(channelGridImpl);
  channelGridRaw->setResponsiveProfile(resolveResponsiveLayoutProfile());
  channelGridRaw->setMidiLearnBindingLabelProvider(
      midiLearnBindingLabelForTarget);
  channelGridRaw->setOnMidiLearnTargetSelected(
      [this, &p, topBarRaw, waveformBarRaw](const int targetId) {
    if (!midiLearnActive || targetId < 0)
      return;
    midiLearnPendingTargetId = targetId;
    p.beginMidiLearnForTarget(targetId);
    if (topBarRaw != nullptr)
      topBarRaw->setMidiLearnPendingTarget(targetId);
    if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
      grid->setMidiLearnPendingTarget(targetId);
    if (waveformBarRaw != nullptr)
      waveformBarRaw->setMidiLearnPendingTarget(targetId);
    if (padsComponent != nullptr)
      padsComponent->setMidiLearnPendingTarget(targetId);
  });
  channelGridRaw->setOnGuideSourceModeChanged(
      [this, &p](const ChannelGridPanel::GuideSourceMode mode) {
        if (playbackSession.isRemote()) { playbackSession.send("guide.enabled", mode == ChannelGridPanel::GuideSourceMode::DynamicBySections); return; }
        DynamicGuideSettings settings = p.getDynamicGuideSettings();
        settings.enabled =
            mode == ChannelGridPanel::GuideSourceMode::DynamicBySections;
        if (settings.enabled)
          p.setDynamicGuideMuted(false);
        p.setDynamicGuideSettings(settings);
        if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
          return;
        if (settings.enabled) {
          SongLibraryStore::updateDynamicGuideForActiveSetlist(
              currentLoadedSong.id, settings, true);
          DynamicGuideMixSnapshot mix;
          if (!SongLibraryStore::getActiveSetlistDynamicGuideMix(
                  currentLoadedSong.id, mix)) {
            mix = DynamicGuideMixSnapshot{};
          }
          mix.volume = p.getDynamicGuideVolume();
          mix.gain = p.getDynamicGuideGain();
          mix.mute = false;
          mix.solo = p.isDynamicGuideSolo();
          mix.outputChannel = p.getDynamicGuideOutputChannel();
          mix.outputWidth = p.getDynamicGuideOutputWidth();
          SongLibraryStore::updateDynamicGuideMixForActiveSetlist(
              currentLoadedSong.id, mix);
        } else {
          SongLibraryStore::updateDynamicGuideForActiveSetlist(
              currentLoadedSong.id, settings, true);
        }
      });

  waveformBarRaw->setOnMidiLearnTargetSelected(
      [this, &p, topBarRaw, channelGridRaw, waveformBarRaw](const int targetId) {
        if (!midiLearnActive || targetId < 0)
          return;
        midiLearnPendingTargetId = targetId;
        p.beginMidiLearnForTarget(targetId);
        if (topBarRaw != nullptr)
          topBarRaw->setMidiLearnPendingTarget(targetId);
        if (channelGridRaw != nullptr)
          channelGridRaw->setMidiLearnPendingTarget(targetId);
        if (waveformBarRaw != nullptr)
          waveformBarRaw->setMidiLearnPendingTarget(targetId);
        if (padsComponent != nullptr)
          padsComponent->setMidiLearnPendingTarget(targetId);
      });

  const auto safeEditor =
      juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  p.getAudioDeviceBridge().setOnDeviceConnected(
      [safeEditor](const AudioDeviceMonitor::DeviceInfo &info) {
        juce::ignoreUnused(info);
        if (safeEditor == nullptr)
          return;

        auto *editor = safeEditor.getComponent();
        if (editor == nullptr)
          return;

        auto &processorRef =
            static_cast<MTControlsAudioProcessor &>(editor->processor);
        const auto audioCtx = PluginEditorSupport::getStandaloneAudioContext(processorRef);
        if (audioCtx.manager == nullptr)
          return;

        PluginEditorSupport::enforceStandaloneOutputOnlyAudio(*audioCtx.manager);
        if (auto *currentDevice = audioCtx.manager->getCurrentAudioDevice();
            currentDevice != nullptr) {
          processorRef.getAudioDeviceBridge().setAvailableOutputChannelCount(
              juce::jmax(1, currentDevice->getActiveOutputChannels().countNumberOfSetBits()));
          editor->lastObservedAudioDeviceKey = getCurrentDeviceKey(audioCtx);
          editor->lastObservedActiveOutputChannels = currentDevice->getActiveOutputChannels();
        }

        processorRef.refreshPadsRoutingFromSettings();
        if (editor->hasCurrentLoadedSong)
          editor->applyRoutingDefaultsToSong(editor->currentLoadedSong);
        if (auto *grid = dynamic_cast<ChannelGridPanel *>(
                editor->channelGridPanel.get())) {
          grid->refreshStemRoutingVisibility();
        }
      });

  refreshTopBarTheme = [this, topBarRaw, waveformBarRaw, channelGridRaw] {
    if (topBarRaw)
      topBarRaw->refreshThemeColours();
    if (waveformBarRaw)
      waveformBarRaw->refreshThemeColours();
#if !JUCE_IOS && !JUCE_ANDROID
    if (lyricsPanel) lyricsPanel->refreshThemeColours();
#endif
    if (channelGridRaw)
      channelGridRaw->refreshThemeColours();
    if (padsComponent)
      padsComponent->refreshThemeColours();
    if (padsHandlePanel)
      padsHandlePanel->repaint();
  };

  auto songListImpl = std::make_unique<SongListPanel>();
#if JUCE_ANDROID
  logAndroidUiPhase("songList.created");
#endif
  auto *songListRaw = songListImpl.get();
  songListPanel = std::move(songListImpl);
  songListPanelRaw = songListRaw;
  songListRaw->setOnSetlistNameChanged([this, channelGridRaw](const juce::String &name) {
    if (setlistHandlePanel)
      setlistHandlePanel->setTitle(name);
    if (channelGridRaw != nullptr)
      channelGridRaw->refreshLibraryFromStorage();
  });
  songListRaw->setOnSaveProjectRequested([this] { saveCurrentProject(); });
  songListRaw->setOnPadsRequested([this] {
    if (padsHandlePanel == nullptr || padsComponent == nullptr)
      return;
    if (isPhoneSizedLayout(*this)) {
      if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
        bar->triggerPhonePadsPopover();
      return;
    }
    isPadsCollapsed = false;
    padsHandlePanel->setCollapsed(false);
    padsComponent->setVisible(true);
    resized();
  });
  songListRaw->setOnProjectUploadRequested(
      [this](const ImportedSongData &song) {
        // The library upload action is for the initial publication. Recheck
        // the link in case a menu was opened before an upload completed.
        CloudProjectLink existingLink;
        if (CloudSyncIndexStore::findByProjectId(projectIdentityForSong(song),
                                                existingLink)) {
          if (songListPanelRaw != nullptr)
            songListPanelRaw->refreshFromStorageView();
          return;
        }
        const juce::File projectFile(song.projectPath);
        if (!isProjectBackedSong(song) || !projectFile.existsAsFile()) {
          juce::NativeMessageBox::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon,
              L10n::tr("Proyecto no disponible", "Project unavailable"),
              L10n::tr("Convierte o abre un proyecto .pwproj antes de subirlo.",
                       "Convert or open a .pwproj project before uploading it."));
          return;
        }
        if (!activeSession.allowed || activeSession.firebaseApiKey.isEmpty() ||
            activeSession.refreshToken.isEmpty()) {
          juce::NativeMessageBox::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon,
              L10n::tr("Sesión requerida", "Sign-in required"),
              L10n::tr("Inicia sesión nuevamente antes de sincronizar proyectos.",
                       "Sign in again before syncing projects."));
          return;
        }
        if (!cloudUploadsInProgress.insert(song.id.toStdString()).second)
          return;

        if (songSyncService == nullptr)
          songSyncService = std::make_unique<SongSyncService>();

        const juce::String title = song.title.isNotEmpty()
                                       ? song.title
                                       : L10n::tr("Proyecto", "Project");
        const CloudTransferCredentials credentials{getApiBaseUrl(),
                                                   activeSession.firebaseApiKey,
                                                   activeSession.refreshToken};
        const auto safeThis =
            juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
        if (songListPanelRaw != nullptr)
          songListPanelRaw->setCloudUploadProgress(song.id, 0.0,
                                                    "preparing");
        songSyncService->upload(
            credentials, projectFile,
            [safeThis, songId = song.id, title](CloudTransferProgress progress) {
              SessionAuthLogger::write(
                  "Cloud upload " + title + " phase=" + progress.phase +
                  " " + juce::String(progress.completedObjects) + "/" +
                  juce::String(progress.totalObjects));
              if (safeThis != nullptr && safeThis->songListPanelRaw != nullptr)
                safeThis->songListPanelRaw->setCloudUploadProgress(
                    songId, progress.fraction, progress.phase);
            },
            [safeThis, songId = song.id](ImportedSongData,
                                         CloudTransferError failure) {
              if (safeThis == nullptr)
                return;
              safeThis->cloudUploadsInProgress.erase(songId.toStdString());
              if (failure.code.isNotEmpty()) {
                if (safeThis->songListPanelRaw != nullptr)
                  safeThis->songListPanelRaw->clearCloudUploadProgress(songId);
                if (failure.code == "NO_CHANGES") {
                  juce::NativeMessageBox::showMessageBoxAsync(
                      juce::MessageBoxIconType::InfoIcon,
                      L10n::tr("Sin nada que sincronizar", "Nothing to sync"),
                      L10n::tr("El proyecto ya está actualizado en la nube.",
                               "This project is already up to date in the cloud."));
                  return;
                }
                if (failure.code == "REVISION_CONFLICT") {
                  juce::NativeMessageBox::showMessageBoxAsync(
                      juce::MessageBoxIconType::WarningIcon,
                      L10n::tr("El proyecto cambió en la nube",
                               "Project changed in the cloud"),
                      L10n::tr(
                          "Existe una versión más reciente de este proyecto en la nube. Descárgala antes de volver a subirlo para evitar sobrescribir cambios.",
                          "A newer version of this project exists in the cloud. Download it before uploading again to avoid overwriting changes."));
                  return;
                }
                if (failure.code == "STORAGE_QUOTA_EXCEEDED") {
                  juce::NativeMessageBox::showMessageBoxAsync(
                      juce::MessageBoxIconType::WarningIcon,
                      L10n::tr("Espacio en la nube insuficiente",
                               "Not enough cloud storage"),
                      L10n::tr(
                          "Este proyecto supera el espacio disponible de tu cuenta. Libera espacio en la nube o amplía el límite y vuelve a sincronizar.",
                          "This project exceeds your account's available storage. Free some cloud space or increase the limit, then sync again."));
                  return;
                }
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    L10n::tr("No se pudo subir el proyecto",
                             "Couldn't upload project"),
                    failure.message.isNotEmpty() ? failure.message
                                                  : failure.code);
                return;
              }
              if (safeThis->songListPanelRaw != nullptr)
                safeThis->songListPanelRaw->setCloudUploadProgress(songId, 1.0,
                                                                    "complete");
              if (safeThis->songListPanelRaw != nullptr)
                safeThis->songListPanelRaw->refreshFromStorageView();
              safeThis->refreshCloudLibrary({});
              juce::Timer::callAfterDelay(850, [safeThis, songId] {
                if (safeThis == nullptr)
                  return;
                if (safeThis->songListPanelRaw != nullptr)
                  safeThis->songListPanelRaw->clearCloudUploadProgress(songId);
              });
            });
      });
  songListRaw->setOnCloudSettingsSyncRequested(
      [this](const ImportedSongData &song) { syncCloudSettings(song); });
  songListRaw->setOnCloudSettingsVersionsRequested(
      [this](const ImportedSongData &song, juce::Rectangle<int> screenAnchor) {
        showCloudSettingsVersions(song, screenAnchor);
      });
  songListRaw->setCloudSettingsPendingProvider(
      [this](const ImportedSongData &song) {
        return hasPendingCloudSettings(song);
      });
  songListRaw->setOnCloudDeleteRequested(
      [this](const juce::String &remoteSongId, const juce::String &title) {
        const auto id = remoteSongId.trim();
        if (id.isEmpty())
          return;
        const auto songTitle = title.trim().isNotEmpty()
                                   ? title.trim()
                                   : L10n::tr("esta canción", "this song");
        const auto message = L10n::tr(
            juce::String("Eliminar \"") + songTitle +
                "\" de la nube? Dejará de estar disponible en tus otros dispositivos. La copia local se conservará.",
            juce::String("Delete \"") + songTitle +
                "\" from the cloud? It will no longer be available on your other devices. The local copy will be kept.");
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle(L10n::tr("Eliminar de la nube", "Delete from cloud"))
                .withMessage(message)
                .withButton(L10n::tr("Eliminar", "Delete"))
                .withButton(L10n::tr("Cancelar", "Cancel")),
            [this, id](int result) {
              if (result != 1 || !activeSession.allowed ||
                  activeSession.firebaseApiKey.isEmpty() ||
                  activeSession.refreshToken.isEmpty() ||
                  !cloudDeletesInProgress.insert(id.toStdString()).second)
                return;
              if (songSyncService == nullptr)
                songSyncService = std::make_unique<SongSyncService>();
              const CloudTransferCredentials credentials{getApiBaseUrl(),
                                                         activeSession.firebaseApiKey,
                                                         activeSession.refreshToken};
              const auto safeThis =
                  juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
              songSyncService->deleteRemoteSong(
                  credentials, id, [safeThis, id](CloudTransferError failure) {
                    if (safeThis == nullptr)
                      return;
                    safeThis->cloudDeletesInProgress.erase(id.toStdString());
                    if (failure.code.isNotEmpty()) {
                      juce::NativeMessageBox::showMessageBoxAsync(
                          juce::MessageBoxIconType::WarningIcon,
                          L10n::tr("No se pudo eliminar de la nube",
                                   "Couldn't delete from cloud"),
                          failure.message.isNotEmpty() ? failure.message : failure.code);
                      return;
                    }
                    CloudSyncIndexStore::removeByRemoteSongId(id);
                    if (safeThis->songListPanelRaw != nullptr)
                      safeThis->songListPanelRaw->refreshFromStorageView();
                    safeThis->refreshCloudLibrary({});
                    juce::NativeMessageBox::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        L10n::tr("Canción eliminada de la nube",
                                 "Song deleted from cloud"),
                        L10n::tr("La copia local se conservó en la Biblioteca.",
                                 "The local copy was kept in the Library."));
                  });
            });
      });
  songListRaw->setOnCloudSearchRequested(
      [this](const juce::String &query) { refreshCloudLibrary(query); });
  songListRaw->setOnCloudDownloadRequested(
      [this](const juce::String &catalogSongId,
             const juce::String &targetSetlistId) {
        if (!activeSession.allowed || activeSession.firebaseApiKey.isEmpty() ||
            activeSession.refreshToken.isEmpty()) {
          juce::NativeMessageBox::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon,
              L10n::tr("Sesión requerida", "Sign-in required"),
              L10n::tr("Inicia sesión nuevamente antes de descargar canciones.",
                       "Sign in again before downloading songs."));
          return;
        }
        const auto sharedSong = std::find_if(cloudCatalogSongs.begin(), cloudCatalogSongs.end(),
            [&](const CloudSongSummary &song) {
              return song.isSharedCloud() && song.catalogId() == catalogSongId;
            });
        if (sharedSong != cloudCatalogSongs.end()) {
          downloadSharedCloudSong(*sharedSong, targetSetlistId);
          return;
        }
        if (catalogSongId.startsWith("shared-cloud:")) {
          refreshCloudLibrary({});
          return;
        }
        const auto remoteSongId = catalogSongId;
        // The menu is normally derived from this same link, but guard the
        // transfer boundary as well: a stale Cloud result must not download a
        // second copy of a song already materialized locally.
        CloudProjectLink existingLink;
        if (CloudSyncIndexStore::findByRemoteSongId(remoteSongId, existingLink)) {
          const auto localSongs = SongLibraryStore::loadSongs();
          const auto localSong = std::find_if(
              localSongs.begin(), localSongs.end(), [&existingLink](const auto &song) {
                return projectIdentityForSong(song) == existingLink.projectId;
              });
          if (localSong != localSongs.end()) {
            if (targetSetlistId.isNotEmpty())
              SongLibraryStore::addSongToSetlist(targetSetlistId, localSong->id);
            if (songListPanelRaw != nullptr)
              songListPanelRaw->refreshFromStorageView();
            refreshCloudLibrary({});
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                L10n::tr("Canción ya descargada", "Song already downloaded"),
                targetSetlistId.isNotEmpty()
                    ? L10n::tr("La canción local se agregó al setlist.",
                               "The local song was added to the setlist.")
                    : L10n::tr("La canción ya está disponible en la Biblioteca local.",
                               "The song is already available in the local Library."));
            return;
          }
        }
        if (!cloudDownloadsInProgress.insert(remoteSongId.toStdString()).second)
          return;
        if (songSyncService == nullptr)
          songSyncService = std::make_unique<SongSyncService>();

        const auto cloudCache = SongLibraryStore::getRootDirectory()
                                    .getChildFile("cloud-content");
        cloudCache.createDirectory();
        const CloudTransferCredentials credentials{getApiBaseUrl(),
                                                   activeSession.firebaseApiKey,
                                                   activeSession.refreshToken};
        const auto safeThis =
            juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
        if (songListPanelRaw != nullptr)
          songListPanelRaw->setCloudUploadProgress(remoteSongId, 0.0,
                                                    "downloading");
        songSyncService->download(
            credentials, remoteSongId, cloudCache,
            [safeThis, remoteSongId](CloudTransferProgress progress) {
              if (safeThis != nullptr && safeThis->songListPanelRaw != nullptr)
                safeThis->songListPanelRaw->setCloudUploadProgress(
                    remoteSongId, progress.fraction, progress.phase);
            },
            [safeThis, remoteSongId, targetSetlistId](ImportedSongData downloadedSong,
                                     CloudTransferError failure) {
              if (safeThis == nullptr)
                return;
              safeThis->cloudDownloadsInProgress.erase(remoteSongId.toStdString());
              if (safeThis->songListPanelRaw != nullptr)
                safeThis->songListPanelRaw->clearCloudUploadProgress(remoteSongId);
              if (failure.code.isNotEmpty()) {
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    L10n::tr("No se pudo descargar el proyecto",
                             "Couldn't download project"),
                    failure.message.isNotEmpty() ? failure.message : failure.code);
                return;
              }
              if (targetSetlistId.isNotEmpty())
                SongLibraryStore::addSongToSetlist(targetSetlistId,
                                                    downloadedSong.id);
              if (safeThis->songListPanelRaw != nullptr)
                safeThis->songListPanelRaw->refreshFromStorageView();
              safeThis->refreshCloudLibrary({});
              const bool addedToSetlist = targetSetlistId.isNotEmpty();
              juce::NativeMessageBox::showMessageBoxAsync(
                  juce::MessageBoxIconType::InfoIcon,
                  addedToSetlist
                      ? L10n::tr("Canción agregada", "Song added")
                      : L10n::tr("Canción descargada", "Song downloaded"),
                  addedToSetlist
                      ? L10n::tr("La canción se descargó y agregó al setlist.",
                                 "The song was downloaded and added to the setlist.")
                      : L10n::tr("La canción ya está disponible en la Biblioteca local.",
                                 "The song is now available in the local Library."));
            });
      });
  songListRaw->setSaveProjectStateProvider(
      [this] {
        return hasCurrentLoadedSong &&
               currentLoadedSong.projectPath.isNotEmpty() && !projectSaveBusy;
      },
      [this] {
        return hasCurrentLoadedSong &&
               currentLoadedSong.projectPath.isNotEmpty() &&
               ProjectStore::hasPendingPortableChanges(
                   juce::File(currentLoadedSong.projectPath));
      });

  struct SongLoadUiScope {
    bool &busy;
    WaveformBar *waveformBar;
    ~SongLoadUiScope() {
      busy = false;
      if (waveformBar != nullptr)
        waveformBar->clearSongLoadingState();
    }
  };

  auto loadSong = [channelGridRaw, waveformBarRaw, &p,
                   this](const ImportedSongData &song, int songIndex) -> bool {
    if (demoLoadingOverlay) return false;
    if (playbackSession.isRemote()) { playbackSession.send("song.load", {}, song.id); return true; }
    if (songLoadBusy)
      return false;
    if (songListPanelRaw != nullptr &&
        songListPanelRaw->isProjectImportInProgress() &&
        !preparedProjectForNextSongLoad)
      return false;
    // Keep the prepared result local before the progress redraw can dispatch
    // another activation. Its validated resources must not be opened twice.
    auto preparedProject = std::move(preparedProjectForNextSongLoad);
    preparedProjectForNextSongLoad.reset();
    // Bug fix: the previous version returned false here for any
    // !isProjectBackedSong(song), which short-circuited the lambda
    // before applyRoutingDefaultsToSong ran. The audio thread kept
    // playing the previous song's stems but with no per-stem width
    // applied, so a user with routing defaults (stereo instruments,
    // mono click/guide) would hear the previous song collapse to
    // mono on the new load. The settings.restore block below already
    // falls back to SongLibraryStore for non-project songs, so the
    // shared path works for both. We only need to skip the
    // .pwproj-specific open + extract block for legacy entries.
    const bool hadLoadedSongBeforeLoad = p.hasLoadedSong();
    // SongLoadProbe: RAII bracket for the whole load. The destructor pops
    // any nested unclosed phases, so early returns below don't leak rows.
    ScopedSongLoadTimer _loadSongTimer(
        "loadSong", "total",
        "title=" + song.title +
            " hadPrev=" + juce::String(hadLoadedSongBeforeLoad ? 1 : 0));
    if (!suppressPersistenceBeforeNextSongLoad) {
      maybePersistSongWindowForActiveSong();
      if (channelGridRaw != nullptr)
        channelGridRaw->flushPendingPersistenceNow();
      if (hasCurrentLoadedSong && currentLoadedSong.projectPath.isNotEmpty())
        ProjectStore::flushPendingSettings(juce::File(currentLoadedSong.projectPath));
    }
    suppressPersistenceBeforeNextSongLoad = false;
    songLoadBusy = true;
    SongLoadUiScope loadUiScope{songLoadBusy, waveformBarRaw};

    // P1.0 (perf): previously every progress tick ran pumpUi (a
    // runDispatchLoopUntil(1)) which forced a UI redraw mid-load. With
    // 38 calls per load (one before and after each stem in the
    // 19-stem load path) this dominated the warm-swap wall clock
    // (400-600ms of "other_in_lss" in the probe, all of it pumpUi).
    // We keep the state update so the progress bar value is correct, but
    // no longer force a redraw from the load path. JUCE's normal
    // message loop redraws on the next vsync anyway, so the user sees
    // the bar advance in step with the actual load phases
    // (0.86, 0.90, 0.98, 1.0) instead of getting 38 per-stem updates.
    auto pushLoadUi = [waveformBarRaw](double overall01,
                                     const juce::String &phase) {
      if (waveformBarRaw == nullptr)
        return;
      waveformBarRaw->setSongLoadingState(true, overall01, phase);
    };

    // Used only at the very end of the load to flush a final redraw so
    // the user immediately sees the progress bar reach 100% and the new
    // channel strips. Not used during the per-stem loop (see P1.0 above).
    auto pumpUiOnce = [] {
#if JUCE_ANDROID
      juce::MessageManager::callAsync([] {});
#else
      if (auto *mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(1);
#endif
    };

    // Show feedback before a cold .pwproj extraction or stem resolution.
    pushLoadUi(0.0, L10n::tr(u8"Cargando canción: ", u8"Loading song: ") +
                        song.title);
    pumpUiOnce();

    juce::StringArray stemPaths;
    std::vector<bool> clickGuideMask;
    std::vector<bool> guideMask;
    std::vector<bool> pitchShiftMask;
    int clickStemIndex = -1;
    ProjectSongSettings projectSettings;
    ImportedSongData sourceSong = song;
    const bool isProjectSong = isProjectBackedSong(song);
    const bool hasProjectSettings = isProjectSong;
    bool restoredProjectSettings = false;
    const auto localLibrarySongId = song.id;
    const auto projectId = projectIdentityForSong(song);
    juce::File projectFile(isProjectSong ? song.projectPath : juce::String{});
    if (isProjectSong) {
      if (preparedProject && preparedProject->projectFile == projectFile &&
          projectIdentityForSong(preparedProject->song) == projectId) {
        sourceSong = std::move(preparedProject->song);
        sourceSong.id = localLibrarySongId;
        projectSettings = std::move(preparedProject->settings);
        restoredProjectSettings = true;
        SongLoadProbe::instance().recordEvent(
            "loadSong", "project.prepared", "projectId=" + projectId);
      } else if (projectFile.existsAsFile()) {
        ProjectDocument document;
        juce::String projectError;
        // SongLoadProbe: time the ZIP open + stem extraction from .pwproj.
        // For non-project songs this is a no-op.
        SongLoadProbe::instance().beginPhase(
            "loadSong", "project.open",
            "size=" + juce::String(projectFile.getSize()));
        const bool projectOpened =
            ProjectStore::open(projectFile, document, &projectError);
        SongLoadProbe::instance().endPhase();
        if (projectOpened) {
          sourceSong = document.song;
          // ProjectStore devuelve el ID del manifiesto. La UI y los setlists
          // deben conservar la identidad local de esta entrada de Biblioteca.
          sourceSong.id = localLibrarySongId;
          projectSettings = document.settings;
          restoredProjectSettings = true;
        } else {
          SessionAuthLogger::write("[Project] Couldn't reopen " +
                                   projectFile.getFullPathName() + ": " +
                                   projectError);
        }
      }
      if (!restoredProjectSettings &&
          ProjectStore::getCachedSettings(projectId, projectSettings)) {
        restoredProjectSettings = true;
        SessionAuthLogger::write(
            "[Project] Restored settings from local cache for unavailable "
            "project id=" +
            projectId);
      }
      if (!restoredProjectSettings) {
        for (const auto &stem : sourceSong.stems) {
          if (!stem.include)
            continue;
          StemMixSnapshot stemMix;
          stemMix.stemId = stem.stemId;
          projectSettings.mix.stems.push_back(std::move(stemMix));
        }
        projectSettings.tempoBpm =
            sourceSong.bpm > 0 ? static_cast<double>(sourceSong.bpm) : 120.0;
        projectSettings.window.endSec = estimateSongLengthSeconds(sourceSong);
        projectSettings.window.fadeOutSec =
            juce::jmin(1.0, projectSettings.window.endSec);
        projectSettings.arrangement = sourceSong.arrangement;
        projectSettings.freeShowCues = sourceSong.freeShowCues;
        projectSettings.arrangementCustomized =
            !sourceSong.arrangement.parts.empty();
        SessionAuthLogger::write(
            "[Project] Settings unavailable; using project defaults until "
            "the package is relinked: " +
            projectFile.getFullPathName());
      }
    }
    ImportedSongData loadedSong = sourceSong;
    loadedSong.stems.clear();
    loadedSong.stems.reserve(sourceSong.stems.size());
    std::vector<ImportedStemData> resolvedStems;
    resolvedStems.reserve(sourceSong.stems.size());

    for (const auto &stem : sourceSong.stems) {
      if (!stem.include)
        continue;

      juce::File file(stem.storedPath);
      if (!file.existsAsFile())
        file = juce::File(stem.sourcePath);
      if (file.existsAsFile()) {
        ImportedStemData loadedStem = stem;
        loadedStem.storedPath = file.getFullPathName();
        loadedStem.include = true;
        resolvedStems.push_back(std::move(loadedStem));
      }
    }

    SessionAuthLogger::write(
        "[SongLoad] resolved title=" + sourceSong.title + " usable=" +
        juce::String(resolvedStems.size()) + "/" +
        juce::String(sourceSong.stems.size()) + " project=" +
        juce::String(isProjectSong ? 1 : 0));
    if (resolvedStems.empty() && !isValidBlankSong(sourceSong)) {
      juce::NativeMessageBox::showMessageBoxAsync(
          juce::MessageBoxIconType::WarningIcon,
          L10n::tr("Pistas no disponibles", "Tracks unavailable"),
          L10n::tr("La canción no tiene pistas locales reproducibles. Descárgala nuevamente desde Cloud.",
                   "The song has no playable local tracks. Download it again from Cloud."));
      return false;
    }

    sortStemsForStripOrder(resolvedStems);

    for (const auto &loadedStem : resolvedStems) {
      const int stemPriority = stemPriorityForStripOrder(loadedStem);
      const int mappedStemIndex = static_cast<int>(loadedSong.stems.size());
      if (clickStemIndex < 0 && stemPriority == 0)
        clickStemIndex = mappedStemIndex;
      stemPaths.add(loadedStem.storedPath);
      loadedSong.stems.push_back(loadedStem);
      const bool isClickGuide = stemPriority <= 1;
      const bool isGuideStem = stemPriority == 1;
      clickGuideMask.push_back(isClickGuide);
      guideMask.push_back(isGuideStem);
      pitchShiftMask.push_back(isStemPitchShiftable(loadedStem));
    }

    pushLoadUi(0.0, L10n::tr(u8"Preparando pistas...", u8"Preparing tracks..."));

    // SongLoadProbe: this is the heavy one — createReader per stem, FLAC
    // cache lookup, prepareToPlay, waitForReady, fade-wait, suspendProcessing,
    // Bungee prep. The probe records sub-stages inside loadSongStems.
    SongLoadProbe::instance().beginPhase(
        "loadSong", "processor.loadSongStems",
        "stems=" + juce::String(stemPaths.size()) +
            " prev=" + juce::String(hadLoadedSongBeforeLoad ? 1 : 0));
    const bool loaded = p.loadSongStems(
        stemPaths, sourceSong.id, sourceSong.title, sourceSong.key,
        static_cast<double>(sourceSong.bpm),
        [&pushLoadUi](double stemPhase01) {
          pushLoadUi(0.55 * stemPhase01,
                     L10n::tr(u8"Leyendo audio de las pistas...",
                              u8"Reading stem audio..."));
        },
        true, sourceSong.blankSong ? &sourceSong : nullptr);
    SongLoadProbe::instance().endPhase();
    SessionAuthLogger::write("[SongLoad] processor result title=" +
                             sourceSong.title + " loaded=" +
                             juce::String(loaded ? 1 : 0) + " stems=" +
                             juce::String(stemPaths.size()));

    if (!loaded) {
      playworship::telemetry::capture(
          playworship::telemetry::Incident::songLoadFailed);
      juce::NativeMessageBox::showMessageBoxAsync(
          juce::MessageBoxIconType::WarningIcon,
          L10n::tr("Error de carga", "Load error"),
          L10n::tr(u8"No se pudieron cargar los stems de esta canción.",
                   "Couldn't load audio stems for this song."));
      if (!hadLoadedSongBeforeLoad) {
        waveformBarRaw->setWaveformBins({});
        waveformBarRaw->setSongRhythm(0.0, {});
        p.setStemClickGuideMask({});
        p.setStemGuideMask({});
        p.setMetronomeClickStemIndex(-1);
        channelGridRaw->setSong(ImportedSongData{});
        hasCurrentLoadedSong = false;
        currentLoadedSong = ImportedSongData{};
        refreshWaveformPresentationForSong();
        resetTrackedSongWindow();
      }
      return false;
    }

    p.setStemClickGuideMask(clickGuideMask);
    p.setStemGuideMask(guideMask);
    p.setStemPitchShiftMask(pitchShiftMask);
    p.setMetronomeClickStemIndex(clickStemIndex);
    p.setDynamicGuideLanguage(AppSettings::getLanguage());
    pushLoadUi(0.58, L10n::tr(u8"Restaurando configuración...",
                              u8"Restoring settings..."));
    // SongLoadProbe: the per-stem settings restoration can be hundreds of
    // atomic stores (mix, click, guide, arrangement). Time it separately so
    // we can compare against the actual stems load.
    SongLoadProbe::instance().beginPhase("loadSong", "settings.restore");
    int savedPitchSemitones = 0;
    if (hasProjectSettings) {
      savedPitchSemitones =
          juce::jlimit(-12, 12, projectSettings.pitchSemitones);
    } else {
      SongLibraryStore::getActiveSetlistPitchSemitones(loadedSong.id,
                                                       savedPitchSemitones);
    }
    p.setPitchSemiTones(savedPitchSemitones);

    double savedTempoBpm = hasProjectSettings ? projectSettings.tempoBpm : 0.0;
    if (savedTempoBpm > 0.0 ||
        SongLibraryStore::getActiveSetlistTempoBpm(loadedSong.id,
                                                   savedTempoBpm)) {
      p.setPlaybackTempoBpm(savedTempoBpm);
    }

    DynamicGuideSettings dynamicGuideSettings;
    bool dynamicGuideCustomized = false;
    if (hasProjectSettings) {
      dynamicGuideSettings = projectSettings.dynamicGuide;
      dynamicGuideCustomized = projectSettings.dynamicGuideCustomized;
    } else if (!SongLibraryStore::getActiveSetlistDynamicGuide(
            loadedSong.id, dynamicGuideSettings, dynamicGuideCustomized)) {
      dynamicGuideSettings = DynamicGuideSettings{};
      dynamicGuideCustomized = false;
    }
    p.setDynamicGuideSettings(dynamicGuideSettings);

    DynamicGuideMixSnapshot dynamicGuideMix;
    if (hasProjectSettings) {
      dynamicGuideMix = projectSettings.dynamicGuideMix;
    } else if (!SongLibraryStore::getActiveSetlistDynamicGuideMix(loadedSong.id,
                                                            dynamicGuideMix)) {
      dynamicGuideMix = DynamicGuideMixSnapshot{};
    }
    p.setDynamicGuideVolume(dynamicGuideMix.volume);
    p.setDynamicGuideGain(dynamicGuideMix.gain);
    p.setDynamicGuideMuted(dynamicGuideMix.mute);
    p.setDynamicGuideSolo(dynamicGuideMix.solo);
    p.setDynamicGuideOutputRouting(dynamicGuideMix.outputChannel,
                                   dynamicGuideMix.outputWidth);
    pushLoadUi(0.63, L10n::tr(u8"Restaurando guía y click...",
                              u8"Restoring guide and click..."));

    ClickDynMixSnapshot clickDynMix;
    if (hasProjectSettings) {
      clickDynMix = projectSettings.clickDynMix;
    } else if (!SongLibraryStore::getActiveSetlistClickDynMix(loadedSong.id,
                                                       clickDynMix)) {
      clickDynMix = ClickDynMixSnapshot{};
    }
    p.setClickDivision(clickDynMix.divisionMask);
    p.setClickDivisionEnabled(clickDynMix.enabled);
    p.setMetronomeVolume(clickDynMix.volume);
    p.setMetronomeGain(clickDynMix.gain);
    p.setMetronomeMuted(clickDynMix.mute);
    p.setMetronomeSolo(clickDynMix.solo);
    p.setMetronomeOutputRouting(clickDynMix.outputChannel,
                                clickDynMix.outputWidth);
    pushLoadUi(0.68, L10n::tr(u8"Restaurando mezcla...",
                              u8"Restoring mix..."));

    SongMixSnapshot mixSnapshot;
    if (hasProjectSettings)
      mixSnapshot = projectSettings.mix;
    if (hasProjectSettings || SongLibraryStore::getActiveSetlistMix(loadedSong.id, mixSnapshot)) {
      p.setMasterVolume(mixSnapshot.master.volume);
      p.setMasterGain(mixSnapshot.master.gain);
      p.setMasterMute(mixSnapshot.master.mute);

      for (size_t i = 0; i < loadedSong.stems.size(); ++i) {
        const auto &stem = loadedSong.stems[i];
        if (!stem.include)
          continue;
        const auto it = std::find_if(
            mixSnapshot.stems.begin(), mixSnapshot.stems.end(),
            [&stem](const StemMixSnapshot &snapshot) {
              return snapshot.stemId == stem.stemId;
            });
        if (it == mixSnapshot.stems.end())
          continue;
        const int stemIndex = static_cast<int>(i);
        p.setStemVolume(stemIndex, it->volume);
        p.setStemGain(stemIndex, it->gain);
        p.setStemMute(stemIndex, it->mute);
        p.setStemSolo(stemIndex, it->solo);
        p.setStemOutputChannel(stemIndex, it->outputChannel);
      }
    }
    SongLoadProbe::instance().endPhase();  // settings.restore
    pushLoadUi(0.73, L10n::tr(u8"Restaurando reproducción...",
                              u8"Restoring playback..."));

    SongWindowSnapshot windowSnapshot;
    bool isCustomWindow = false;
    if (hasProjectSettings) {
      windowSnapshot = projectSettings.window;
      isCustomWindow = projectSettings.windowCustomized;
    } else {
      SongLibraryStore::getActiveSetlistSongWindow(loadedSong.id, windowSnapshot,
                                                   isCustomWindow);
    }
    if (isCustomWindow) {
      p.setSongStartSeconds(windowSnapshot.startSec);
      p.setSongEndSeconds(windowSnapshot.endSec);
      p.setSongFadeOutSeconds(windowSnapshot.fadeOutSec);
    }

    SongArrangementSnapshot arrangement;
    bool isCustomArrangement = false;
    if (hasProjectSettings) {
      arrangement = projectSettings.arrangement;
      isCustomArrangement = projectSettings.arrangementCustomized;
    } else if (!SongLibraryStore::getSongArrangement(loadedSong.id, arrangement,
                                              isCustomArrangement)) {
      arrangement.parts.clear();
      isCustomArrangement = false;
    }

    currentLoadedSong = loadedSong;
    hasCurrentLoadedSong = true;
    AppSettings::setLastSelectedSongId(currentLoadedSong.id);

    // Routing probe: load gate. Records whether the load is for a
    // project (.pwproj) or legacy song so regressions in the gate
    // show up directly in the CSV next to the apply.enter/exit rows.
    SongLoadProbe::instance().recordEvent(
        "routing", "loadSong.gate",
        "isProject=" + juce::String(hasProjectSettings ? 1 : 0) +
            " songStems=" + juce::String(loadedSong.stems.size()));

    // Device routing is applied after song mix restoration so legacy/project
    // snapshots cannot override Click/Cues mono or the selected output channel.
    applyRoutingDefaultsToSong(currentLoadedSong);
    p.refreshPadsRoutingFromSettings();
    p.setMetronomeEnabled(clickDynMix.metronomeEnabled);
    trackClickDynMetronomeEnabled(clickDynMix.metronomeEnabled);
    juce::StringArray automationStemIds;
    for (const auto &stem : loadedSong.stems)
      if (stem.include) automationStemIds.add(stem.stemId);
    p.setSectionAutomationStemIds(automationStemIds);
    p.setSongArrangement(arrangement, isCustomArrangement);
    p.setArrangementEditingActive(false);
#if !JUCE_IOS && !JUCE_ANDROID
    auto lyrics = projectSettings.freeShowCues;
    if (!hasProjectSettings)
      SongLibraryStore::getSongFreeShowCues(loadedSong.id, lyrics);
    p.setSongLyrics(lyrics);
    if (lyricsPanel) {
      lyricsPanel->setSongLyrics(lyrics);
      lyricsPanel->setArrangement(arrangement);
      lyricsPanel->setDuration(p.getSongLengthSeconds());
    }
    if (loadedSong.blankSong) lyricsViewActive = false;
#endif
    // Loading a song always starts at its beginning, independent of section
    // count, the first section's timestamp, or the previous editing state.
    p.seekSongTo(p.getSongStartSeconds());
    trackSongWindow(currentLoadedSong.id, p.getSongStartSeconds(),
                    p.getSongEndSeconds(), p.getSongFadeOutSeconds());

    // A selected pad remains global across the setlist. When the destination
    // has a different key, followSongKey triggers the engine's voice-level
    // crossfade: the outgoing pad stays audible while the new key enters.
    // The pads bus is deliberately not faded to silence during a song load.
    if (padsComponent != nullptr)
      padsComponent->followSongKey(loadedSong.key, savedPitchSemitones);

    // The processor kept the replacement at zero while the complete mix,
    // routing, click, guide and arrangement snapshot was restored.
    p.finishPendingSongTransition();

    pushLoadUi(0.77, L10n::tr(u8"Preparando interfaz...",
                              u8"Preparing interface..."));
    channelGridRaw->setSong(loadedSong);
    channelGridRaw->setGuideSourceMode(
        dynamicGuideSettings.enabled
            ? ChannelGridPanel::GuideSourceMode::DynamicBySections
            : ChannelGridPanel::GuideSourceMode::SongGuide);
    currentSongIndex = songIndex;
    pushLoadUi(0.80, L10n::tr(u8"Cargando forma de onda...",
                              u8"Loading waveform..."));

    refreshWaveformPresentationForSong();
    auto cachedBins = loadedSong.blankSong ? std::vector<float>{}
        : SongLibraryStore::loadWaveformCache(loadedSong.id);
    if (loadedSong.blankSong) {
      waveformGeneration.fetch_add(1, std::memory_order_acq_rel);
      waveformBarRaw->setWaveformBins({});
    } else if (cachedBins.empty()) {
      // P0.5 (perf): buildMixedWaveformBins decodifica TODOS los stems
      // completos en el UI thread. En Windows con stems largos y
      // antivirus activo, puede tomar 1-3s y congelar la UI durante el
      // cambio de canción. Lo movemos a un thread aparte y usamos un
      // generation token para descartar resultados tardíos si el
      // usuario cambia de canción otra vez antes de que termine.
      pushLoadUi(0.81, L10n::tr(u8"Preparando forma de onda...",
                                u8"Preparing waveform..."));
      // Reflejar visualmente el estado de carga con bins vacíos para
      // que la barra no muestre la canción anterior.
      waveformBarRaw->setWaveformBins({});
      const auto myGen =
          waveformGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
      auto safeEditor =
          juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
      const auto songIdForBg = loadedSong.id;
      const auto projectPathForBg = loadedSong.projectPath;
      const ImportedSongData songForBg = loadedSong;
      std::thread([safeEditor, myGen, songIdForBg, projectPathForBg,
                   songForBg]() {
        // El cómputo corre en background. No se reportan progresos
        // intermedios porque el spinner del SongLoadUiScope ya le da
        // feedback al usuario; el delta total es 0.5-3s.
        auto bins = buildMixedWaveformBins(
            songForBg, kWaveformPreviewBinCount, nullptr);
        juce::MessageManager::callAsync(
            [safeEditor, myGen, songIdForBg, projectPathForBg,
             bins = std::move(bins)]() mutable {
              if (safeEditor == nullptr)
                return;
              if (myGen != safeEditor->waveformGeneration.load(
                               std::memory_order_acquire)) {
                // Generación vieja: el usuario cambió de canción
                // mientras calculábamos. Descartar silenciosamente.
                return;
              }
              if (bins.empty())
                return;
              if (!SongLibraryStore::saveWaveformCache(songIdForBg, bins)) {
                SessionAuthLogger::write(
                    "loadSong: couldn't persist waveform cache for " +
                    songIdForBg);
              }
              // P1.2: the .pwproj already contains a valid waveform
              // from the original save. Regenerating it via
              // ProjectStore::updateWaveform would require re-opening
              // and re-packing the .pwproj ZIP, changing the mtime
              // and forcing a full re-extract of every stem on the
              // very next load. The sidecar above is the authoritative
              // copy of the regenerated preview, so we deliberately
              // skip the re-pack here. The .pwproj keeps a
              // slightly-stale preview waveform (within a few ULP of
              // the regenerated one) but the user never notices
              // because the WaveformBar reads from the sidecar first.
              (void)projectPathForBg;
              if (safeEditor->waveformBarPtr != nullptr)
                safeEditor->waveformBarPtr->setWaveformBins(std::move(bins));
            });
      }).detach();
    } else {
      pushLoadUi(0.99, L10n::tr(u8"Cargando forma de onda en caché...",
                                u8"Loading cached waveform..."));
    }

    pushLoadUi(1.0, L10n::tr(u8"Finalizando...", u8"Finishing..."));
    // Si no estamos esperando bins en background, aplicarlos ahora.
    // Si hay un cálculo en background, setWaveformBins ya quedó vacío
    // arriba; el callAsync posterior los va a setear.
    if (cachedBins.empty() == false) {
      waveformBarRaw->setWaveformBins(std::move(cachedBins));
    }
    closeSectionDetection();
    waveformBarRaw->setSongRhythm(static_cast<double>(loadedSong.bpm),
                                  loadedSong.timeSignature);
    waveformBarRaw->setArrangement(arrangement, isCustomArrangement);
    waveformBarRaw->setTempoMap(loadedSong.tempoMap);
    waveformBarRaw->resetPlaybackViewForSong();
    // SongLoadProbe: the ScopedSongLoadTimer at the top of the lambda closes
    // the total bracket and pops any leftover inner phases on scope exit.
    // Flush here so the user can read the CSV immediately if they switch
    // songs again before the destructor runs.
    SongLoadProbe::instance().flush();
    // Reverted pumpUiOnce here: while it improved the perceived
    // responsiveness of the final redraw, it ran with the
    // SongLoadUiScope still in its "Finalizando..." state and could
    // leave the waveform bar stuck on that label when the load was
    // fast enough that clearSongLoadingState's 180ms-min-visible-ms
    // guard deferred the clear into a future timer tick. The small
    // redraw latency (one vsync) is the lesser evil.
    return true;
  };

  loadSongAction = loadSong;
  requestSongLoadAction = [this](const ImportedSongData &song,
                                 const int songIndex) {
    if (playbackSession.isRemote()) { playbackSession.send("song.load", {}, song.id); return; }
    const auto requestedSongIdentity = playbackIdentityForSong(song);
    const auto loadedSongIdentity =
        hasCurrentLoadedSong ? playbackIdentityForSong(currentLoadedSong)
                             : juce::String{};

    const bool isSameLoadedSong =
        hasCurrentLoadedSong && requestedSongIdentity.isNotEmpty() &&
        requestedSongIdentity == loadedSongIdentity;
    // An explicit disk import has a freshly validated document even when the
    // path/identity matches the loaded song. Consume it rather than retaining
    // stale audio or settings; ordinary repeated navigation still stays a no-op.
    if ((!isSameLoadedSong || preparedProjectForNextSongLoad) && loadSongAction)
      (void)loadSongAction(song, songIndex);
  };

  songListRaw->setOnSongActivated(
      [this](const ImportedSongData &song, int songIndex) {
        deferredStartupSongLoadPending = false;
        deferredStartupSongIndex = -1;
        deferredStartupSongLoadAtMs = 0;
        if (requestSongLoadAction)
          requestSongLoadAction(song, songIndex);
      });
  songListRaw->setOnProjectActivated(
      [this](ProjectDocument document, const int songIndex) {
        deferredStartupSongLoadPending = false;
        deferredStartupSongIndex = -1;
        deferredStartupSongLoadAtMs = 0;
        const auto song = document.song;
        preparedProjectForNextSongLoad = std::move(document);
        if (requestSongLoadAction)
          requestSongLoadAction(song, songIndex);
        preparedProjectForNextSongLoad.reset();
      });
  songListRaw->setOnSongEditStarted([this](const juce::String &songId) {
    if (isSongCurrentlyLoaded(songId))
      flushActiveSongPersistenceNow();
  });
  songListRaw->setOnSongEdited(
      [this](const ImportedSongData &song, const int songIndex) {
        if (!isSongCurrentlyLoaded(song.id) || !loadSongAction)
          return;
        // Reloading the same song must read its new cover/BPM and stem map.
        // The just-saved metadata is authoritative; a pending old snapshot
        // must not be written over it during the reload.
        suppressPersistenceBeforeNextSongLoad = true;
        if (isProjectBackedSong(song) &&
            !static_cast<MTControlsAudioProcessor &>(processor).unloadSong()) {
          suppressPersistenceBeforeNextSongLoad = false;
          juce::NativeMessageBox::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon,
              L10n::tr("Proyecto actualizado", "Project updated"),
              L10n::tr("Detén la reproducción y vuelve a abrir la canción para cargar los cambios.",
                       "Stop playback and reopen the song to load the changes."));
          return;
        }
        if (!loadSongAction(song, songIndex))
          suppressPersistenceBeforeNextSongLoad = false;
      });

  auto loadSongByIndex = [songListRaw, loadSong](int index) -> bool {
    if (!songListRaw)
      return false;
    if (const auto *song = songListRaw->getSongPtr(index))
      return loadSong(*song, index);
    return false;
  };

  loadSongByIndexAction = [songListRaw, loadSongByIndex, &p](int index,
                                                              bool autoPlay) {
    if (!songListRaw)
      return;
    if (!juce::isPositiveAndBelow(index, songListRaw->getSongCount()))
      return;
    if (!loadSongByIndex(index))
      return;

    songListRaw->setSelectedSongIndex(index);
    if (autoPlay)
      p.playSong();
  };

  auto requestSongLoadByIndex = [songListRaw, this](const int index) {
    if (songListRaw == nullptr || !requestSongLoadAction)
      return;
    if (const auto *song = songListRaw->getSongPtr(index))
      requestSongLoadAction(*song, index);
  };

  channelGridRaw->setOnLibrarySongLoadRequested(
      [this, songListRaw](const ImportedSongData &song, int songIndex) {
        juce::ignoreUnused(songIndex);
        deferredStartupSongLoadPending = false;
        deferredStartupSongIndex = -1;
        deferredStartupSongLoadAtMs = 0;
        const int setlistIndex =
            songListRaw != nullptr ? songListRaw->getSongIndexById(song.id) : -1;
        const int indexForState = setlistIndex >= 0 ? setlistIndex : -1;
        if (requestSongLoadAction)
          requestSongLoadAction(song, indexForState);
      });
  channelGridRaw->setOnLibraryAddSongRequested(
      [channelGridRaw, songListRaw](const juce::StringArray &songIds) {
        if (songIds.isEmpty())
          return;
        const auto snapshot = SongLibraryStore::loadLibrarySnapshot();
        if (!snapshot.activeSetlistId.isEmpty()) {
          for (const auto &songId : songIds) {
            if (songId.isEmpty())
              continue;
            SongLibraryStore::addSongToSetlist(snapshot.activeSetlistId, songId);
          }
        }
        if (songListRaw != nullptr)
          songListRaw->refreshFromStorageView();
        if (channelGridRaw != nullptr)
          channelGridRaw->refreshLibraryFromStorage();
      });
  channelGridRaw->setOnLibraryImportRequested([channelGridRaw, songListRaw] {
    if (songListRaw != nullptr)
      songListRaw->openSongImportWizardFromExternal();
    if (songListRaw != nullptr)
      songListRaw->refreshFromStorageView();
    if (channelGridRaw != nullptr)
      channelGridRaw->refreshLibraryFromStorage();
  });

  {
    deferredStartupSongLoadPending = false;
    deferredStartupSongIndex = -1;
    deferredStartupSongLoadAtMs = 0;
    const auto lastSelectedSongId = AppSettings::getLastSelectedSongId();
    if (!localDemoSession && lastSelectedSongId.isNotEmpty()) {
      const int index = songListRaw != nullptr
                            ? songListRaw->getSongIndexById(lastSelectedSongId)
                            : -1;
      if (index >= 0) {
        deferredStartupSongLoadPending = true;
        deferredStartupSongIndex = index;
        deferredStartupSongLoadAtMs = juce::Time::currentTimeMillis() + 350;
        SessionAuthLogger::write(
            "initializeMainUi: deferred startup song load index=" +
            juce::String(index));
      }
    }
  }

  auto resolveCurrentSongIndex = [songListRaw, this]() -> int {
    if (!songListRaw)
      return -1;
    if (hasCurrentLoadedSong && currentLoadedSong.id.isNotEmpty()) {
      const int resolved = songListRaw->getSongIndexById(currentLoadedSong.id);
      if (resolved >= 0) {
        currentSongIndex = resolved;
        return resolved;
      }
    }
    return currentSongIndex;
  };

  auto prevSong = [songListRaw, requestSongLoadByIndex,
                   resolveCurrentSongIndex, this]() {
    if (!songListRaw)
      return;
    if (hasCurrentLoadedSong && currentLoadedSong.id.isNotEmpty()) {
      if (const auto previous = songListRaw->getPlaybackAdjacentSong(
              currentLoadedSong.id, -1, false); previous && requestSongLoadAction)
        requestSongLoadAction(*previous, songListRaw->getSongIndexById(previous->id));
      return;
    }
    const int count = songListRaw->getSongCount();
    if (count <= 0)
      return;
    int index = resolveCurrentSongIndex();
    if (index < 0)
      index = songListRaw->getLastActivatedIndex();
    if (index < 0)
      index = 0;
    const int target = juce::jlimit(0, count - 1, index - 1);
    if (target == index)
      return;
    requestSongLoadByIndex(target);
  };

  auto nextSong = [songListRaw, requestSongLoadByIndex,
                   resolveCurrentSongIndex,
                   this]() {
    if (!songListRaw)
      return;
    if (hasCurrentLoadedSong && currentLoadedSong.id.isNotEmpty()) {
      auto next = songListRaw->getAutoPlayNextSong(currentLoadedSong.id);
      if (!next)
        next = songListRaw->getPlaybackAdjacentSong(currentLoadedSong.id, 1, true);
      if (next && requestSongLoadAction)
        requestSongLoadAction(*next, songListRaw->getSongIndexById(next->id));
      return;
    }
    const int count = songListRaw->getSongCount();
    if (count <= 0)
      return;
    int index = resolveCurrentSongIndex();
    if (index < 0)
      index = songListRaw->getLastActivatedIndex();
    if (index < 0)
      index = 0;
    index = juce::jlimit(0, count - 1, index);

    // Fallback: walk forward by one slot, wrapping to the first song when the
    // user is already at the tail. This makes the transport button always
    // feel responsive instead of dying silently at the end of the setlist.
    int target = index + 1;
    if (target >= count)
      target = 0;
    if (target == index)
      return;
    requestSongLoadByIndex(target);
  };

  prevSongAction = prevSong;
  nextSongAction = nextSong;

  topBarRaw->setTransportCallbacks(
      [this] { playbackSession.toggleSongPlayPause(); }, [this] { playbackSession.stopSong(); }, prevSong,
      nextSong, [this](bool fadeOut) {
        playbackSession.setFadeStopsPlayback(false);
        playbackSession.setFadeActive(fadeOut);
      },
      [this](double deltaSec) { playbackSession.seekSongRelative(deltaSec); });
  topBarRaw->setOnSettingsWithAnchor(
      [this](juce::Rectangle<int> anchor, juce::Component *parent) {
        showSettings(anchor, parent);
      });
  topBarRaw->setOnMidiLearnModeChanged([this, &p, waveformBarRaw](const bool active) {
    midiLearnActive = active;
    if (!midiLearnActive) {
      midiLearnPendingTargetId = -1;
      p.beginMidiLearnForTarget(-1);
    }
    if (auto *bar = dynamic_cast<TopBar *>(topBar.get())) {
      bar->setMidiLearnActive(midiLearnActive);
      bar->setMidiLearnPendingTarget(midiLearnPendingTargetId);
    }
    if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get())) {
      grid->setMidiLearnActive(midiLearnActive);
      grid->setMidiLearnPendingTarget(midiLearnPendingTargetId);
    }
    if (waveformBarRaw != nullptr) {
      waveformBarRaw->setMidiLearnActive(midiLearnActive);
      waveformBarRaw->setMidiLearnPendingTarget(midiLearnPendingTargetId);
    }
    if (padsComponent != nullptr) {
      padsComponent->setMidiLearnActive(midiLearnActive);
      padsComponent->setMidiLearnPendingTarget(midiLearnPendingTargetId);
    }
  });
  topBarRaw->setOnMidiLearnTargetSelected(
      [this, &p, channelGridRaw, waveformBarRaw](const int targetId) {
    if (!midiLearnActive || targetId < 0)
      return;
    midiLearnPendingTargetId = targetId;
    p.beginMidiLearnForTarget(targetId);
    if (channelGridRaw != nullptr)
      channelGridRaw->setMidiLearnPendingTarget(targetId);
    if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
      bar->setMidiLearnPendingTarget(targetId);
    if (waveformBarRaw != nullptr)
      waveformBarRaw->setMidiLearnPendingTarget(targetId);
    if (padsComponent != nullptr)
      padsComponent->setMidiLearnPendingTarget(targetId);
  });

  setlistHandlePanel = std::make_unique<SetlistHandlePanel>("Setlist A");
  setlistHandlePanel->onToggle = [this] {
    autoSetlistCollapseInitialized = true;
    if (isPhoneSizedLayout(*this)) {
      // Phones keep the mixer full width. The setlist is therefore presented
      // as an overlay instead of reserving a permanent side column.
      if (phonePadsPopoverContent != nullptr)
        dismissActiveActionPopover();
      isPadsCollapsed = true;
      if (padsHandlePanel)
        padsHandlePanel->setCollapsed(true);
      isSetlistDrawerOpen = !isSetlistDrawerOpen;
      isSetlistCollapsed = false;
    } else if (!supportsSetlistDrawer(*this)) {
      isSetlistDrawerOpen = false;
      isSetlistCollapsed = !isSetlistCollapsed;
    } else if (isSetlistDrawerOpen) {
      isSetlistDrawerOpen = false;
      isSetlistCollapsed = false;
    } else if (isSetlistCollapsed) {
      // On tablets, reopening the panel keeps the mixer and pads at full
      // width by presenting the existing SongListPanel as an overlay drawer.
      isSetlistDrawerOpen = true;
    } else {
      isSetlistCollapsed = true;
    }
    resized();
    repaint();
  };
  topBarRaw->setOnPhoneLibrary(
      [safeEditor, songListRaw](juce::Component &anchor) {
        if (safeEditor == nullptr || songListRaw == nullptr)
          return;
        songListRaw->showSongPicker(anchor, [safeEditor] {
          if (safeEditor == nullptr || safeEditor->songListPanelRaw == nullptr)
            return;
          safeEditor->phonePadsOpen = false;
          safeEditor->isPadsCollapsed = true;
          safeEditor->isSetlistDrawerOpen = true;
          safeEditor->isSetlistCollapsed = false;
          safeEditor->songListPanelRaw->showActiveSetlist();
          safeEditor->resized();
          safeEditor->repaint();
        });
      });
  topBarRaw->setOnPhonePads(
      [this](juce::Component &anchor) { showPhonePadsPopover(anchor); });
  topBarRaw->setOnPhonePractice([this] { showPractice(); });
  topBarRaw->setOnLearningRequested([this] { showLearningGuide(); });
  setlistDrawerScrim = std::make_unique<SetlistDrawerScrim>([this] {
    isSetlistDrawerOpen = false;
    phonePadsOpen = false;
    isPadsCollapsed = true;
    if (padsHandlePanel)
      padsHandlePanel->setCollapsed(true);
    resized();
    repaint();
  });
  if (songListRaw != nullptr) {
    songListRaw->setOnOverlayCloseRequested([this] {
      isSetlistDrawerOpen = false;
      resized();
      repaint();
    });
  }
  if (songListRaw)
    setlistHandlePanel->setTitle(songListRaw->getActiveSetlistName());

  padsComponent = std::make_unique<PadsComponent>(p, stripLookAndFeel, &playbackSession);
#if JUCE_ANDROID
  logAndroidUiPhase("pads.created");
#endif
  padsComponent->setMidiLearnBindingLabelProvider(midiLearnBindingLabelForTarget);
  padsComponent->setOnMidiLearnTargetSelected(
      [this, &p, topBarRaw, channelGridRaw, waveformBarRaw](const int targetId) {
    if (!midiLearnActive || targetId < 0)
      return;
    midiLearnPendingTargetId = targetId;
    p.beginMidiLearnForTarget(targetId);
    if (topBarRaw != nullptr)
      topBarRaw->setMidiLearnPendingTarget(targetId);
    if (channelGridRaw != nullptr)
      channelGridRaw->setMidiLearnPendingTarget(targetId);
    if (waveformBarRaw != nullptr)
      waveformBarRaw->setMidiLearnPendingTarget(targetId);
    if (padsComponent != nullptr)
      padsComponent->setMidiLearnPendingTarget(targetId);
  });
  padsComponent->setResponsiveProfile(resolveResponsiveLayoutProfile());
  padsComponent->setVisible(false);
  addChildComponent(*padsComponent);

  padsHandlePanel = std::make_unique<PadsHandlePanel>();
  padsHandlePanel->setCollapsed(isPadsCollapsed);
  padsComponent->setVisible(!isPadsCollapsed);
  padsHandlePanel->onToggle = [this] {
    if (isPhoneSizedLayout(*this)) {
      if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
        bar->triggerPhonePadsPopover();
      return;
    }
    isPadsCollapsed = !isPadsCollapsed;
    padsHandlePanel->setCollapsed(isPadsCollapsed);
    padsComponent->setVisible(!isPadsCollapsed);
    resized();
  };
  addAndMakeVisible(*padsHandlePanel);

  addAndMakeVisible(*topBar);
  addAndMakeVisible(*waveformBar);
#if !JUCE_IOS && !JUCE_ANDROID
  addChildComponent(*lyricsPanel);
#endif
  addAndMakeVisible(*channelGridPanel);
  addAndMakeVisible(*songListPanel);
  addAndMakeVisible(*setlistHandlePanel);
  addChildComponent(*setlistDrawerScrim);

  if (reauthPromptModal == nullptr) {
    auto prompt = std::make_unique<ReauthPromptModal>();
    prompt->onLoginNow = [this] {
      reauthPromptDismissed = false;
      reauthLoginInProgress = true;
      if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
        reauth->showWaitingForCallback();
      launchExternalLogin();
    };
    prompt->onLater = [this] {
      cancelAuthenticationAttempt();
      reauthPromptDismissed = true;
      reauthLoginInProgress = false;
      hideReauthPromptModal();
    };
    prompt->onAppleLoginNow = [this] { launchAppleLogin(); };
    prompt->onPasswordLoginRequested = [this](const juce::String& email, const juce::String& password) {
      launchPasswordLogin(email, password);
    };
    prompt->onPasswordResetRequested = [this](const juce::String& email) { requestPasswordLink(email); };
    prompt->onAuthenticationCancelled = [this] { cancelAuthenticationAttempt(); };
    prompt->onInlineAuthenticationNavigation = [this] { cancelAuthenticationAttempt(true); };
    prompt->onPreferredHeightChanged = [this] { resized(); };
    prompt->setAppleSignInAvailable(appleSignInAvailable);
    reauthPromptModal = std::move(prompt);
    addAndMakeVisible(*reauthPromptModal);
    reauthPromptModal->setVisible(false);
  }
  if (welcomeModal)
    welcomeModal->setVisible(false);

  midiLearnActive = false;
  midiLearnPendingTargetId = -1;
  topBarRaw->setMidiLearnActive(false);
  topBarRaw->setMidiLearnPendingTarget(-1);
  channelGridRaw->setMidiLearnActive(false);
  channelGridRaw->setMidiLearnPendingTarget(-1);
  waveformBarRaw->setMidiLearnActive(false);
  waveformBarRaw->setMidiLearnPendingTarget(-1);
  padsComponent->setMidiLearnActive(false);
  padsComponent->setMidiLearnPendingTarget(-1);

  const auto userArea =
      juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
#if JUCE_IOS || JUCE_ANDROID
  setResizable(true, true);
  setResizeLimits(320, 280, 32000, 32000);
  fitMobileEditorToHost();
#else
  const int maxW = juce::jmax(kMinWindowW, userArea.getWidth() - kScreenMargin);
  const int maxH = juce::jmax(kMinWindowH, userArea.getHeight() - kScreenMargin);
  const int w = juce::jlimit(kMinWindowW, maxW, kDefaultWindowW);
  const int h = juce::jlimit(kMinWindowH, maxH, kDefaultWindowH);
  setResizable(true, true);
  setResizeLimits(kMinWindowW, kMinWindowH, 32000, 32000);
  setSize(w, h);
#endif
  applyResponsiveLayoutProfile(resolveResponsiveLayoutProfile());
  if (!autoSetlistCollapseInitialized) {
    isSetlistCollapsed = false;
    autoSetlistCollapseInitialized = true;
  }

#if MTCONTROLS_NATIVE_APP && !JUCE_ANDROID
  juce::MessageManager::callAsync(
      [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] {
        if (safe != nullptr)
          expandTopLevelWindowToPrimaryWorkArea(safe.getComponent());
      });
#elif !JUCE_IOS && !JUCE_ANDROID
  juce::MessageManager::callAsync(
      [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] {
        if (safe != nullptr)
          expandTopLevelWindowToPrimaryWorkArea(safe.getComponent());
      });
#endif

  setWantsKeyboardFocus(true);
  grabKeyboardFocus();

  wasSongPlaying = p.isSongPlaying();
  startTimerHz(20);
  mainUiInitialized = true;
  resized();
  updateSystemUsageMetrics();
#if JUCE_ANDROID
  logAndroidUiPhase("complete");
#endif
}

void MTControlsAudioProcessorEditor::initializeWelcomeModal() {
  if (learningGuide) pauseLearningGuide();
  auto modal = std::make_unique<WelcomeModal>();
  modal->onLoginRequested = [this] { launchExternalLogin(); };
  modal->onAppleLoginRequested = [this] { launchAppleLogin(); };
  modal->onPasswordLoginRequested = [this](const juce::String& email, const juce::String& password) {
    launchPasswordLogin(email, password);
  };
  modal->onLocalDemoLoginRequested = [this](const juce::String& username, const juce::String& password) {
    launchLocalDemoLogin(username, password);
  };
  modal->onPasswordResetRequested = [this](const juce::String& email) { requestPasswordLink(email); };
  modal->onAuthenticationCancelled = [this] { cancelAuthenticationAttempt(); };
  modal->onInlineAuthenticationNavigation = [this] { cancelAuthenticationAttempt(true); };
  modal->setAppleSignInAvailable(appleSignInAvailable);
  welcomeModal = std::move(modal);
  addAndMakeVisible(*welcomeModal);
  resized();
  refreshAppleSignInConfiguration();
}

void MTControlsAudioProcessorEditor::launchExternalLogin() {
  cancelAuthenticationAttempt(appleLinkFlow.stage == AppleLinkFlow::Stage::authenticate);
  sessionResumeCancelled = false;
  if (deletionReauthStatus) deletionReauthStatus(L10n::tr("Esperando autenticación…", "Waiting for authentication…"), false, true);
#if JUCE_IOS || JUCE_ANDROID
  NativeAuthSession::cancel();
  authExpectedState = juce::Uuid().toString();
  const juce::String redirectUri = "playworship://auth/callback";
  const juce::String loginUrl =
      getAuthBaseUrl() + "/api/auth/google/start?redirect_uri=" + urlEncode(redirectUri) +
      "&state=" + urlEncode(authExpectedState) +
      "&deviceId=" + urlEncode(buildStableDeviceId());

  if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
    modal->setStatusText("Esperando autenticacion...", false);
  if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
    reauth->showWaitingForCallback();

  const auto generation = authAttemptGeneration;
  const auto started = NativeAuthSession::start(
      loginUrl, "playworship",
      [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this), generation](
          const juce::String &callbackUrl, const juce::String &error) {
        if (safe == nullptr || generation != safe->authAttemptGeneration)
          return;
        if (callbackUrl.isNotEmpty()) {
          safe->enqueueAuthCallbackUrl(callbackUrl);
          return;
        }
        safe->cancelAuthenticationAttempt();
        if (error.isNotEmpty()) {
          if (safe->deletionReauthStatus) safe->deletionReauthStatus(L10n::tr("No se completó la autenticación.", "Authentication did not complete."), true, false);
          if (auto *modal = dynamic_cast<WelcomeModal *>(safe->welcomeModal.get()))
            modal->setStatusText("No se completo la autenticacion.", true);
          if (auto *reauth = dynamic_cast<ReauthPromptModal *>(
                  safe->reauthPromptModal.get()))
            reauth->showError("No se completo la autenticacion.");
        }
      });
  SessionAuthLogger::write(
      juce::String("launchExternalLogin: native callback browserStarted=") +
      juce::String(static_cast<int>(started)));
  if (!started) {
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
      modal->setStatusText("No se pudo abrir el inicio de sesion.", true);
    if (deletionReauthStatus) deletionReauthStatus(L10n::tr("No se pudo completar Google. Intenta de nuevo.", "Google sign-in did not complete. Try again."), true, false);
    reauthLoginInProgress = false;
  }
  return;
#else
  if (localAuthServer != nullptr)
    localAuthServer->stop();
  localAuthServer = std::make_unique<LocalAuthCallbackServer>(
      [this](const juce::String &callbackUrl) { enqueueAuthCallbackUrl(callbackUrl); });
  authCallbackPort = 0;

  if (!localAuthServer->start(authCallbackPort)) {
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
      modal->setStatusText(
          "No se pudo iniciar el callback local. Verifica puertos locales.", true);
    if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
      reauth->showError("No se pudo iniciar el callback local. Verifica puertos.");
    if (deletionReauthStatus) deletionReauthStatus(L10n::tr("No se pudo completar Google. Intenta de nuevo.", "Google sign-in did not complete. Try again."), true, false);
    reauthLoginInProgress = false;
    authCallbackPort = 0;
    return;
  }

  authExpectedState = juce::Uuid().toString();
  const juce::String redirectUri =
      "http://127.0.0.1:" + juce::String(authCallbackPort) + "/auth/callback";
  const juce::String deviceId = buildStableDeviceId();
  const juce::String authBaseUrl = getAuthBaseUrl();
  const juce::String loginUrl =
      authBaseUrl + "/api/auth/google/start?redirect_uri=" + urlEncode(redirectUri) +
      "&state=" + urlEncode(authExpectedState) +
      "&deviceId=" + urlEncode(deviceId);

  if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
    modal->setStatusText("Esperando autenticacion en el navegador...", false);
  if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
    reauth->showWaitingForCallback();

  if (!juce::URL(loginUrl).launchInDefaultBrowser()) {
    cancelAuthenticationAttempt();
    if (deletionReauthStatus) deletionReauthStatus(L10n::tr("No se pudo abrir el navegador.", "Could not open the browser."), true, false);
    const auto message = L10n::tr(u8"No se pudo abrir el navegador. Puedes ingresar con correo.",
                                  "Couldn't open the browser. You can sign in with email.");
    if (auto* modal = dynamic_cast<WelcomeModal*>(welcomeModal.get()))
      modal->setStatusText(message, true);
    if (auto* modal = dynamic_cast<ReauthPromptModal*>(reauthPromptModal.get()))
      modal->showError(message);
  }
#endif
}

void MTControlsAudioProcessorEditor::completeBootstrapLogin(
    const juce::String &callbackUrl) {
  const auto params = parseCallbackParams(callbackUrl);
  const auto receivedState = params["state"].trim();
  if (authExpectedState.isEmpty() || receivedState.isEmpty() ||
      receivedState != authExpectedState) {
    SessionAuthLogger::write("completeBootstrapLogin: callback rejected (state mismatch)");
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get())) {
      modal->setStatusText("Autenticacion invalida. Reintenta.", true);
    }
    if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
      reauth->showError("Autenticacion invalida. Reintenta.");
    if (deletionReauthStatus) deletionReauthStatus(L10n::tr("No se pudo completar Google. Intenta de nuevo.", "Google sign-in did not complete. Try again."), true, false);
    reauthLoginInProgress = false;
    return;
  }

  // A callback is single-use, including a backend error. Never log its URL:
  // successful callbacks contain session credentials.
  authExpectedState.clear();
  if (params["error"].isNotEmpty()) {
    SessionAuthLogger::write("completeBootstrapLogin: backend returned OAuth error");
    const juce::String message =
        "No se pudo completar el inicio de sesion con Google. Reintenta.";
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
      modal->setStatusText(message, true);
    if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
      reauth->showError(message);
    if (deletionReauthStatus) deletionReauthStatus(L10n::tr("No se pudo completar Google. Intenta de nuevo.", "Google sign-in did not complete. Try again."), true, false);
    reauthLoginInProgress = false;
    return;
  }

  PersistedSessionSnapshot candidate;
  candidate.uid = params["uid"].trim();
  candidate.email = params["email"].trim();
  candidate.plan = params["plan"].trim();
  candidate.subscriptionStatus = params["subscriptionStatus"].trim();
  candidate.expiresAt = params["expiresAt"].trim();
  candidate.allowed = parseBooleanLike(params["allowed"], false);
  candidate.reasonCode = params["reasonCode"].trim();
  candidate.reason = params["reason"].trim();
  candidate.refreshToken = params["refreshToken"].trim();
  candidate.firebaseApiKey = params["firebaseApiKey"].trim();
  candidate.licenseKey = params["licenseKey"].trim();
  const auto expMsFromParams = params["idTokenExpiresAtMs"].trim().getLargeIntValue();
  if (expMsFromParams > 0)
    candidate.idTokenExpiresAtMs = expMsFromParams;
  const auto sessionJsonRaw = params["session"].trim();
  if (sessionJsonRaw.isNotEmpty()) {
    const auto parsed = juce::JSON::parse(sessionJsonRaw);
    candidate.uid =
        readStringFromObject(parsed, "uid").isNotEmpty()
            ? readStringFromObject(parsed, "uid")
            : candidate.uid;
    candidate.email =
        readStringFromObject(parsed, "email").isNotEmpty()
            ? readStringFromObject(parsed, "email")
            : candidate.email;
    candidate.plan =
        readStringFromObject(parsed, "plan").isNotEmpty()
            ? readStringFromObject(parsed, "plan")
            : candidate.plan;
    candidate.subscriptionStatus =
        readStringFromObject(parsed, "subscriptionStatus").isNotEmpty()
            ? readStringFromObject(parsed, "subscriptionStatus")
            : candidate.subscriptionStatus;
    candidate.expiresAt =
        readStringFromObject(parsed, "expiresAt").isNotEmpty()
            ? readStringFromObject(parsed, "expiresAt")
            : candidate.expiresAt;
    candidate.allowed = parseBooleanLike(
        readStringFromObject(parsed, "allowed"), candidate.allowed);
    candidate.reasonCode = readStringFromObject(parsed, "reasonCode")
                                   .isNotEmpty()
                               ? readStringFromObject(parsed, "reasonCode")
                               : candidate.reasonCode;
    candidate.reason = readStringFromObject(parsed, "reason").isNotEmpty()
                               ? readStringFromObject(parsed, "reason")
                               : candidate.reason;
    candidate.refreshToken =
        readStringFromObject(parsed, "refreshToken").isNotEmpty()
            ? readStringFromObject(parsed, "refreshToken")
            : candidate.refreshToken;
    candidate.firebaseApiKey =
        readStringFromObject(parsed, "firebaseApiKey").isNotEmpty()
            ? readStringFromObject(parsed, "firebaseApiKey")
            : candidate.firebaseApiKey;
    if (readStringFromObject(parsed, "licenseKey").isNotEmpty())
      candidate.licenseKey = readStringFromObject(parsed, "licenseKey");
    const auto expMsRaw = readStringFromObject(parsed, "idTokenExpiresAtMs");
    const auto expMs = expMsRaw.getLargeIntValue();
    if (expMs > 0)
      candidate.idTokenExpiresAtMs = expMs;

    const auto userVar = parsed.getProperty("user", juce::var{});
    if (userVar.isObject()) {
      const auto userUid = readStringFromObject(userVar, "uid");
      const auto userEmail = readStringFromObject(userVar, "email");
      const auto userPlan = readStringFromObject(userVar, "plan");
      const auto userStatus = readStringFromObject(userVar, "subscriptionStatus");
      if (userUid.isNotEmpty())
        candidate.uid = userUid;
      if (userEmail.isNotEmpty())
        candidate.email = userEmail;
      if (userPlan.isNotEmpty())
        candidate.plan = userPlan;
      if (userStatus.isNotEmpty())
        candidate.subscriptionStatus = userStatus;
    }
  }

  completeAuthenticatedSession(std::move(candidate));
}

void MTControlsAudioProcessorEditor::completeAuthenticatedSession(
    PersistedSessionSnapshot snapshot) {
  if (deletionReauthCandidate) {
    auto capture = deletionReauthCandidate;
    cancelAuthenticationAttempt(); // Fence duplicate callbacks before any candidate processing.
    capture(std::move(snapshot)); return;
  }
  if (SessionPersistence::isAccountDeletionPending(snapshot.uid)) {
    if (activeSession.uid == snapshot.uid) handleLogout();
    else {
      cancelAuthenticationAttempt();
      setEmailAuthenticationStatus(L10n::tr("Esta cuenta tiene una eliminación pendiente.",
        "Account deletion is pending for this account."), true, false);
    }
    return;
  }
  if (localDemoSession) return;
  if (captureInlineAppleAccount(snapshot)) return;
  ++authAttemptGeneration;
  emailAuthenticationBusy = false;
  if (auto* modal = dynamic_cast<WelcomeModal*>(welcomeModal.get())) {
    modal->setAuthenticationBusy(false);
    modal->clearCredentials();
  }
  if (auto* modal = dynamic_cast<ReauthPromptModal*>(reauthPromptModal.get())) {
    modal->setAuthenticationBusy(false);
    modal->clearCredentials();
  }
  if (snapshot.allowed && (snapshot.uid.isEmpty() || !snapshot.hasCredentials())) {
    reauthLoginInProgress = false;
    if (auto* modal = dynamic_cast<WelcomeModal*>(welcomeModal.get()))
      modal->setStatusText(L10n::tr("La sesión recibida no es válida. Reintenta.",
                                   "The returned session is invalid. Try again."), true);
    return;
  }
  if (snapshot.allowed &&
      !hasValidAccessBySubscriptionState(snapshot.subscriptionStatus,
                                         snapshot.plan)) {
    snapshot.allowed = false;
    if (snapshot.reasonCode.isEmpty())
      snapshot.reasonCode = "INVALID_SUBSCRIPTION_STATE";
    if (snapshot.reason.isEmpty()) {
      snapshot.reason =
          "Subscription status is not free/trial/active with a valid plan";
    }
  }

  if (!snapshot.allowed) {
    juce::String message =
        userMessageForAccessDenial(snapshot.reasonCode, snapshot.reason);
    SessionAuthLogger::write(
        "completeBootstrapLogin: access denied allowed=" +
        juce::String(static_cast<int>(snapshot.allowed)) + " plan=" +
        snapshot.plan + " status=" + snapshot.subscriptionStatus +
        " reasonCode=" + snapshot.reasonCode + " reason=" +
        snapshot.reason);
    reauthLoginInProgress = false;
    if (mainUiInitialized) {
      handleLogout();
    }
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
      modal->setStatusText(message, true);
    return;
  }

  applyPersistedSnapshotToActive(snapshot);
  lastSuccessfulLoginMs = juce::Time::currentTimeMillis();
  AppSettings::setLastSuccessfulLoginMs(lastSuccessfulLoginMs);
  reauthGraceDeadlineMs = 0;
  AppSettings::setReauthGraceDeadlineMs(0);
  reauthPromptDismissed = false;
  reauthLoginInProgress = false;
  hideReauthPromptModal();
  if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
    reauth->showReadyState();
  saveSessionSnapshotToDisk();
  SessionAuthLogger::write(
      "completeAuthenticatedSession: login OK, snapshot saved, calling "
      "initializeMainUi");
  initializeMainUi();
  showServiceTermsIfNeeded();
  if (songListPanelRaw) songListPanelRaw->refreshFromStorageView();
  refreshCloudLibrary({});
}

juce::String MTControlsAudioProcessorEditor::consumePendingAuthCallbackUrl() {
  const juce::ScopedLock lock(authCallbackLock);
  auto url = pendingAuthCallbackUrl;
  pendingAuthCallbackUrl.clear();
  return url;
}

void MTControlsAudioProcessorEditor::enqueueAuthCallbackUrl(
    const juce::String &callbackUrl) {
  const juce::ScopedLock lock(authCallbackLock);
  pendingAuthCallbackUrl = callbackUrl;
}

void MTControlsAudioProcessorEditor::showSettings(juce::Rectangle<int>,
                                                  juce::Component *parent, bool freeShow) {
  if (!mainUiInitialized)
    return;
#if JUCE_IOS || JUCE_ANDROID
  closeSettings();
#else
  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = L10n::tr(u8"Configuración", "Settings");
  options.dialogBackgroundColour = UITheme::appBg;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = true;
#endif
  auto &proc = static_cast<MTControlsAudioProcessor &>(processor);
  const auto routingDefaults = AppSettings::getRoutingDefaults();
  const auto audioCtx = getStandaloneAudioContext(static_cast<MTControlsAudioProcessor &>(processor));
  if (audioCtx.manager != nullptr)
    enforceStandaloneOutputOnlyAudio(*audioCtx.manager);
  auto *panel = new SettingsPanel(
      static_cast<ThemePreset>(currentThemeId),
      [this](ThemePreset preset) { applyThemePreset(static_cast<int>(preset)); },
      AppSettings::getLanguage(),
      [this](AppLanguage language) {
        AppSettings::setLanguage(language);
        auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
        processorRef.setDynamicGuideLanguage(language);
        if (songListPanelRaw != nullptr)
          songListPanelRaw->refreshLanguageTexts();
        repaint();
      },
      AppSettings::getPadsCrossfadePreset(),
      [this](const PadsCrossfadePreset preset) {
        AppSettings::setPadsCrossfadePreset(preset);
        auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
        auto enginePreset = PadsEngine::CrossfadePreset::Medium;
        if (preset == PadsCrossfadePreset::Fast)
          enginePreset = PadsEngine::CrossfadePreset::Fast;
        else if (preset == PadsCrossfadePreset::Long)
          enginePreset = PadsEngine::CrossfadePreset::Long;
        processorRef.setPadsCrossfadePreset(enginePreset);
      },
      AppSettings::getMetronomeClickStyle(),
      [this](const MetronomeClickStyle style) {
        AppSettings::setMetronomeClickStyle(style);
        auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
        const auto clickFolder = AssetBootstrapManager::getClicksPackDirectory();
        juce::String error;
        processorRef.loadMetronomeClickSamplesFromFolder(
            clickFolder,
            style == MetronomeClickStyle::Classic
                ? "Classic"
                : style == MetronomeClickStyle::Woodblock
                      ? "Woodblock"
                      : style == MetronomeClickStyle::Cowbell
                            ? "Cowbell"
                            : style == MetronomeClickStyle::Digital
                                  ? "Digital"
                                  : style == MetronomeClickStyle::Gentle
                                        ? "Gentle"
                                        : style == MetronomeClickStyle::Percussive
                                              ? "Percussive"
                                              : style == MetronomeClickStyle::Saw
                                                    ? "Saw"
            : "Blip",
            &error);
      },
      [this] {
        closeSettings();
      },
      [this] {
        auto summary = SettingsPanel::SessionSummary{activeSession.uid, activeSession.email,
                                    activeSession.plan,
                                    activeSession.subscriptionStatus,
                                    activeSession.expiresAt,
                                    activeSession.allowed,
                                    activeSession.reasonCode,
                                    activeSession.reason,
                                    activeSession.licenseKey};
        summary.localDemo = localDemoSession;
        return summary;
      }(),
      [safeThis = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] {
        const auto options = juce::MessageBoxOptions::makeOptionsOkCancel(
            juce::MessageBoxIconType::QuestionIcon,
            L10n::tr(u8"¿Cerrar sesión?", "Sign out?"),
            safeThis && safeThis->localDemoSession ? L10n::tr(
                "Saldrás de la prueba local y volverás a la bienvenida. La demo y tus ajustes permanecerán en el dispositivo.",
                "You'll leave the local trial and return to the welcome screen. The demo and your settings will remain on this device.") : L10n::tr(
                u8"Tendrás que iniciar sesión nuevamente para acceder a tu biblioteca y a las canciones de Cloud.",
                "You'll need to sign in again to access your library and Cloud songs."),
            L10n::tr(u8"Cerrar sesión", "Sign out"),
            L10n::tr("Cancelar", "Cancel"), safeThis.getComponent());
        juce::AlertWindow::showAsync(
            options,
            [safeThis](const int result) {
              if (result != 1 || safeThis == nullptr)
                return;
              juce::MessageManager::callAsync([safeThis] {
                if (safeThis != nullptr)
                  safeThis->handleLogout();
              });
            });
      },
      [safeThis = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] {
        const auto options = juce::MessageBoxOptions::makeOptionsOkCancel(
            juce::MessageBoxIconType::WarningIcon,
            L10n::tr("Purgar datos locales de Cloud", "Purge local Cloud data"),
            L10n::tr(
                "Se eliminarán todas las canciones y setlists de Biblioteca, cachés, descargas y vínculos de sincronización. "
                "La Biblioteca quedará vacía. No se eliminará ningún archivo .pwproj ni contenido de la nube.",
                "All Library songs and setlists, caches, downloads, and sync links will be removed. "
                "The Library will be empty. No .pwproj file or cloud content will be deleted."),
            L10n::tr("Purgar", "Purge"), L10n::tr("Cancelar", "Cancel"),
            safeThis.getComponent());
        juce::AlertWindow::showAsync(
            options,
            [safeThis](int result) {
              if (result != 1 || safeThis == nullptr)
                return;
              if (!safeThis->unloadActiveSongForLocalPurge()) {
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    L10n::tr("No se pudo completar la purga",
                             "Couldn't complete purge"),
                    L10n::tr(
                        "No fue posible liberar la canción activa del motor de audio. Inténtalo nuevamente.",
                        "The active song couldn't be unloaded from the audio engine. Please try again."));
                return;
              }
              const auto purge = CloudSyncIndexStore::purgeLocalState();
              if (safeThis->songListPanelRaw != nullptr)
                safeThis->songListPanelRaw->refreshFromStorageView();
              safeThis->refreshCloudLibrary({});
              juce::NativeMessageBox::showMessageBoxAsync(
                  juce::MessageBoxIconType::InfoIcon,
                  L10n::tr("Datos locales purgados", "Local data purged"),
                  L10n::tr("La Biblioteca quedó vacía. No se eliminó ningún archivo .pwproj ni contenido de la nube. Canciones eliminadas de Biblioteca: ",
                           "The Library is empty. No .pwproj file or cloud content was deleted. Songs removed from Library: ") +
                      juce::String(purge.removedLibrarySongs));
            });
      },
      routingDefaults,
      proc.getAvailableOutputChannelCount(),
      [this](const juce::String &deviceKey,
             const std::vector<CategoryRoutingDefaults> &next) {
        // Routing probe: panel onChanged. Fires once per ComboBox
        // change; combined with apply.enter/exit rows it explains why
        // touching one row in the panel re-applies the whole table.
        SongLoadProbe::instance().recordEvent(
            "routing", "panel.onChanged",
            "deviceKey=" + deviceKey +
                " categories=" + juce::String(next.size()) +
                " hasSong=" + juce::String(hasCurrentLoadedSong ? 1 : 0));
        AppSettings::setRoutingDefaultsForDevice(deviceKey, next);
        auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
        processorRef.refreshPadsRoutingFromSettings();
        if (hasCurrentLoadedSong)
          applyRoutingDefaultsToSong(currentLoadedSong);
      },
      // Standalone mode is output-only. Keep inputs out of the routing UI too.
      audioCtx.manager, 0, audioCtx.maxOutputs);
  panel->setDiagnosticsBusy(diagnosticExportBusy);
  panel->onAccountDeletionRequested = [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] {
    if (!safe || safe->localDemoSession || safe->activeSession.uid.isEmpty()) return;
    const auto uid = safe->activeSession.uid;
    auto *content = new AccountDeletionModal(uid);
    juce::Component::SafePointer<AccountDeletionModal> modal(content);
    const auto preparedReceipt = std::make_shared<juce::String>();
    const auto freshDeletionIdToken = std::make_shared<juce::String>();
    content->setSize(juce::jmin(500, juce::jmax(320, safe->getWidth() - 24)), juce::jmin(620, juce::jmax(240, safe->getHeight() - 24)));
    content->onDismissed = [safe, uid, preparedReceipt](bool preserveReceipt) {
      if (!preserveReceipt && preparedReceipt->isNotEmpty()) SessionPersistence::clearAccountDeletionReceipt(uid, *preparedReceipt);
      if (safe) { safe->deletionReauthCandidate = {}; safe->deletionReauthStatus = {}; safe->cancelAuthenticationAttempt(); }
    };
    content->onCancelled = [modal] {
      if (modal) if (auto *window = modal->findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState(0);
    };
    content->onAccepted = [safe, uid] {
      if (!safe) return false;
      if (!SessionPersistence::markAccountDeletionPending(uid)) return false;
      if (safe->activeSession.uid == uid) safe->handleLogout();
      return true;
    };
    content->onReauthenticationRequested = [safe, modal, uid, preparedReceipt, freshDeletionIdToken](int generation) {
      // This flow cannot return to review after submission; only its own unused receipt can be cleared.
      if (preparedReceipt->isNotEmpty()) { SessionPersistence::clearAccountDeletionReceipt(uid, *preparedReceipt); preparedReceipt->clear(); }
      if (!safe || !modal || safe->activeSession.uid != uid) return;
      auto *form = modal->providerForm(); if (!form) return;
      const auto original = safe->activeSession;
      juce::Component::SafePointer<ReauthPromptModal> authForm(form);
      form->setAppleSignInAvailable(safe->appleSignInAvailable);
      safe->deletionReauthStatus = [authForm](const juce::String &text, bool error, bool busy) {
        if (authForm) { authForm->setAuthenticationBusy(busy); authForm->setStatusText(text, error); }
      };
      safe->deletionReauthCandidate = [safe, modal, uid, generation, original, authForm, preparedReceipt, freshDeletionIdToken](PersistedSessionSnapshot candidate) {
        if (!safe || !modal || modal->getGeneration() != generation || modal->getState() != AccountDeletionModal::State::reauthenticating) return;
        if (candidate.uid != uid || safe->activeSession.uid != uid) {
          if (authForm) authForm->showError(L10n::tr("Usa la misma cuenta que deseas eliminar.", "Use the same account you want to delete."));
          return; // Never accept matching email as an account link or switch session.
        }
        if (authForm) authForm->setAuthenticationBusy(true);
        const auto base = getApiBaseUrl();
        std::thread([safe, modal, uid, generation, original, candidate, base, preparedReceipt, freshDeletionIdToken] {
          const auto current = AuthSessionApi::refreshFirebaseIdToken(original.firebaseApiKey, original.refreshToken);
          const auto fresh = AuthSessionApi::refreshFirebaseIdToken(candidate.firebaseApiKey, candidate.refreshToken);
          const auto proof = current.ok && fresh.ok ? AccountDeletionApi::reauthenticate(base, current.idToken, fresh.idToken) : AccountDeletionReauthentication{};
          juce::MessageManager::callAsync([safe, modal, uid, generation, proof, preparedReceipt, freshDeletionIdToken, token = fresh.idToken] {
            if (!safe || !modal || modal->getGeneration() != generation || safe->activeSession.uid != uid) return;
            safe->deletionReauthCandidate = {}; safe->deletionReauthStatus = {}; safe->cancelAuthenticationAttempt();
            if (!proof.ok || !proof.executionAvailable || !SessionPersistence::saveAccountDeletionReceipt(uid, proof.receipt)) { modal->fail(generation, uid); return; }
            *preparedReceipt = proof.receipt; *freshDeletionIdToken = token;
            modal->receiveReauthentication(generation, uid, proof.proofId, proof.expiresAtMs);
          });
        }).detach();
      };
      form->onLoginNow = [safe] { if (safe) safe->launchExternalLogin(); };
      form->onAppleLoginNow = [safe] { if (safe) safe->launchAppleLogin(); };
      form->onPasswordLoginRequested = [safe](const juce::String &email, const juce::String &password) { if (safe) safe->launchPasswordLogin(email, password); };
      form->onAuthenticationCancelled = [safe] { if (safe) safe->cancelAuthenticationAttempt(); };
      form->onInlineAuthenticationNavigation = [safe] { if (safe) safe->cancelAuthenticationAttempt(true); };
      form->onLater = [modal] { if (modal) modal->invalidate(); };
      // Password reset cannot prove identity and is intentionally offered through the normal login UI.
    };
    content->onConfirmationRequested = [safe, modal, uid, preparedReceipt, freshDeletionIdToken](int generation, const juce::String &proofId) {
      if (!safe || !modal || safe->activeSession.uid != uid) return;
      const auto receipt = SessionPersistence::loadAccountDeletionReceipt(uid), token = *freshDeletionIdToken;
      if (!SessionPersistence::markAccountDeletionReceiptSubmitted(uid, receipt)) { modal->fail(generation, uid); return; }
      preparedReceipt->clear(); freshDeletionIdToken->clear(); // Once submitted, uncertainty always preserves the receipt.
      const auto base = getApiBaseUrl();
      std::thread([modal, uid, generation, proofId, receipt, token, base] {
        const auto result = AccountDeletionApi::request(base, token, proofId, receipt);
        juce::MessageManager::callAsync([modal, uid, generation, result] {
          if (!modal) return;
          if (!result.ok || (result.state != "pending" && result.state != "complete")) { modal->fail(generation, uid); return; }
          modal->receiveStatus(generation, uid, result.state == "complete" ? AccountDeletionModal::State::complete : AccountDeletionModal::State::pending);
        });
      }).detach();
    };
    content->onStatusRequested = [modal, uid](int generation) {
      const auto receipt = SessionPersistence::loadAccountDeletionReceipt(uid), base = getApiBaseUrl();
      std::thread([modal, uid, generation, receipt, base] {
        const auto result = AccountDeletionApi::continuePending(base, receipt);
        juce::MessageManager::callAsync([modal, uid, generation, receipt, result] {
          if (!modal || modal->getGeneration() != generation) return;
          if (result.ok && result.state == "not-accepted" && SessionPersistence::acknowledgeCompletedDeletionReceipt(receipt)) { modal->invalidate(); return; }
          if (!result.ok || result.state == "prepared") { modal->fail(generation, uid); return; }
          modal->receiveStatus(generation, uid, result.state == "complete" ? AccountDeletionModal::State::complete : AccountDeletionModal::State::pending);
        });
      }).detach();
    };
    content->onPreviewRequested = [safe, modal, uid](int generation) {
      if (!safe || !modal || safe->activeSession.uid != uid) return;
      const auto base = getApiBaseUrl(); const auto key = safe->activeSession.firebaseApiKey;
      const auto refresh = safe->activeSession.refreshToken;
      std::thread([safe, modal, uid, generation, base, key, refresh] {
        const auto result = AccountDeletionApi::preview(base, key, refresh, uid);
        juce::MessageManager::callAsync([safe, modal, uid, generation, result] {
          if (!modal) return;
          if (!safe || safe->activeSession.uid != uid) { modal->invalidate(); return; }
          modal->receivePreview(generation, uid, result);
        });
      }).detach();
    };
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content); options.dialogTitle = L10n::tr("Eliminar cuenta", "Delete account");
    options.dialogBackgroundColour = AuthUi::surface; options.useNativeTitleBar = true;
    options.resizable = false; options.escapeKeyTriggersCloseButton = true;
    options.launchAsync(); content->requestPreview();
  };
  panel->onShareAudioDiagnostics = [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] {
    if (safe != nullptr) safe->shareAudioDiagnostics();
  };
#if !JUCE_IOS && !JUCE_ANDROID
  panel->setFreeShowClient(&proc.getFreeShowClient());
  if (freeShow) panel->showFreeShowSettings();
#else
  juce::ignoreUnused(freeShow);
#endif
  panel->onLearningRequested = [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] {
    juce::MessageManager::callAsync([safe] {
      if (!safe) return;
      safe->closeSettings();
      safe->showLearningGuide();
    });
  };
  panel->onTeamsRequested = [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] {
    juce::MessageManager::callAsync([safe] { if (safe) safe->showTeams(); });
  };
  // A current server capability controls visibility; the plan name never grants Teams.
  canManageSharedCloud = false;
  panel->setTeamsManagementEnabled(hasTeamsMembership);
  panel->onRemoteModeChanged = [this](playworship::remote::RemoteSession::Mode mode) { setRemoteMode(mode); };
  panel->onRemoteServerAliasChanged = [this](const juce::String &alias) {
    AppSettings::setRemoteServerAlias(alias);
    remoteSession.renameServer(playworship::remote::makeRemoteServerName(alias));
    refreshRemoteSettings();
  };
  panel->onRemoteConnect = [this](const playworship::remote::DiscoveredServer &server) {
    playbackSession.setConnected(false);
    remoteHadSnapshot = false;
    remoteConnectionStatus.clear();
    remoteSession.connectTo(server);
    refreshRemoteSettings();
  };
  panel->updateRemoteState(remoteSession.getMode(), remoteSession.getDiscoveredServers(), remoteConnectionStatus);
  panel->setSize(useTouchOptimisedControls() ? 980 : 1080,
                 useTouchOptimisedControls() ? 860 : 780);
  const auto settingsWidth = panel->getWidth();
  const auto settingsHeight = panel->getHeight();
  panel->refreshThemeColours();

  if (cloudLibraryClient == nullptr)
    cloudLibraryClient = std::make_unique<CloudLibraryClient>();

  if (activeSession.allowed && activeSession.firebaseApiKey.isNotEmpty() &&
      activeSession.refreshToken.isNotEmpty()) {
    auto safePanel = juce::Component::SafePointer<SettingsPanel>(panel);
    const auto sessionSnapshot = activeSession;
    cloudLibraryClient->fetchCapabilities(
        getApiBaseUrl(), activeSession.firebaseApiKey, activeSession.refreshToken,
        [safePanel, sessionSnapshot,
         safeEditor = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)]
        (CloudCapabilities caps, CloudRequestError error) mutable {
          if (safePanel == nullptr || safeEditor == nullptr ||
              sessionSnapshot.uid != safeEditor->activeSession.uid) return;
          if (error.code.isNotEmpty()) {
            safeEditor->canManageSharedCloud = false;
            safePanel->setTeamsManagementEnabled(safeEditor->hasTeamsMembership);
            safePanel->setQuotaUnavailable();
            return;
          }
          safeEditor->canManageSharedCloud = caps.canManageSharedCloud;
          safePanel->setTeamsManagementEnabled(caps.canManageSharedCloud || safeEditor->hasTeamsMembership);
          safePanel->updateSessionSummary(SettingsPanel::SessionSummary{
              sessionSnapshot.uid, sessionSnapshot.email, sessionSnapshot.plan,
              sessionSnapshot.subscriptionStatus,
              caps.trialEndsAt.isNotEmpty() ? caps.trialEndsAt : sessionSnapshot.expiresAt,
              sessionSnapshot.allowed, sessionSnapshot.reasonCode, sessionSnapshot.reason,
              sessionSnapshot.licenseKey, caps.accountType, caps.isOverride,
              caps.usedDevices, caps.maxDevices, caps.syncedSongCount, caps.maxSyncedSongs,
              caps.usedStorageBytes, caps.maxStorageBytes});
        });
  } else {
    panel->setQuotaUnavailable();
  }

#if JUCE_IOS || JUCE_ANDROID
  auto overlay = std::make_unique<SettingsOverlayComponent>(
      std::unique_ptr<SettingsPanel>(panel), [this] { closeSettings(); });
  settingsPanelRaw = overlay->getPanel();
  settingsOverlay = std::move(overlay);
  addAndMakeVisible(*settingsOverlay);
  settingsOverlay->toFront(true);
  settingsOverlay->setBounds(getLocalBounds());
  panel->grabKeyboardFocus();
  updatePhoneOrientation();
  return;
#else
  options.content.setOwned(panel);

  if (auto *window = options.launchAsync()) {
    settingsWindow = window;
    settingsPanelRaw = panel;
    window->centreAroundComponent(parent != nullptr ? parent : this,
                                  settingsWidth, settingsHeight);
  }
#endif
}

void MTControlsAudioProcessorEditor::closeSettings() {
  if (learningGuide && learningGuide->getParentComponent() != this) {
    addAndMakeVisible(*learningGuide);
    learningGuide->setBounds(getLocalBounds());
  }
  if (settingsWindow != nullptr) {
    settingsWindow->setVisible(false);
    settingsWindow->exitModalState(0);
    settingsWindow = nullptr;
  }

  if (settingsOverlay != nullptr) {
    settingsOverlay->setVisible(false);
    settingsOverlay.reset();
  }

  settingsPanelRaw = nullptr;
  updatePhoneOrientation();
}

void MTControlsAudioProcessorEditor::showPractice() {
  if (practiceOverlay != nullptr || !isPhoneSizedLayout(*this))
    return;

  PracticePanel::Sources sources;
  sources.songId = [this] {
    if (playbackSession.isRemote())
      return PlaybackSession::field(
                 PlaybackSession::field(playbackSession.snapshot(), "song"), "id")
          .toString();
    return currentLoadedSong.id;
  };
  sources.title = [this] { return playbackSession.getLoadedSongTitle(); };
  sources.positionSeconds =
      [this] { return playbackSession.getSongCurrentPositionSeconds(); };
  sources.lengthSeconds =
      [this] { return playbackSession.getSongLengthSeconds(); };
  const auto originalBpm = [this] {
    return playbackSession.isRemote()
        ? PlaybackSession::number(PlaybackSession::field(playbackSession.snapshot(), "song"), "bpm", 120.0)
        : static_cast<MTControlsAudioProcessor &>(processor).getOriginalSongBpm();
  };
  sources.songBpm = originalBpm;
  sources.timeSignature = [this] { return currentLoadedSong.timeSignature; };
  sources.playbackRate = [this, originalBpm] {
    return playbackSession.getPlaybackTempoBpm() /
           juce::jmax(1.0, originalBpm());
  };
  sources.tempoBpm = [this] { return playbackSession.getPlaybackTempoBpm(); };
  sources.setTempoBpm = [this](double bpm) {
    playbackSession.setPlaybackTempoBpm(bpm);
    if (!playbackSession.isRemote() && currentLoadedSong.id.isNotEmpty())
      SongLibraryStore::updateTempoBpmForActiveSetlist(
          currentLoadedSong.id, playbackSession.getPlaybackTempoBpm());
  };
  sources.pitchSemitones = [this] { return playbackSession.getPitchSemiTones(); };
  sources.songKey = [this] { return playbackSession.getLoadedSongKey(); };
  sources.setPitchSemitones = [this](int semitones) {
    playbackSession.setPitchSemiTones(semitones);
    if (!playbackSession.isRemote() && currentLoadedSong.id.isNotEmpty())
      SongLibraryStore::updatePitchSemitonesForActiveSetlist(currentLoadedSong.id, semitones);
  };
  sources.isLoopActive = [this] { return playbackSession.isPracticeLoopActive(); };
  sources.loopRange = [this] { return playbackSession.getPracticeLoopRange(); };
  sources.loopIdentity = [this] { return playbackSession.getPracticeLoopIdentity(); };
  sources.snapLoopPosition = [this, originalBpm](double position, bool isEnd) {
    const double start = playbackSession.getSongStartSeconds();
    double end = playbackSession.getSongEndSeconds();
    const auto snap = [this, originalBpm](double seconds, juce::Range<double> allowed) {
      return waveformBarPtr != nullptr
          ? waveformBarPtr->getPracticeSnappedPosition(seconds, allowed)
          : PracticeTimelineRuler::snapToPulse(seconds, originalBpm(),
                currentLoadedSong.timeSignature, {}, allowed);
    };
    // Leave a real pulse for B, even when Fin is between grid lines.
    if (!isEnd)
      end = snap(end, {start, end}) - playbackSession.getMinimumPracticeLoopDuration();
    if (!std::isfinite(end) || end < start)
      return std::numeric_limits<double>::quiet_NaN();
    return snap(position, {start, end});
  };
  sources.prepareLoop = [this](double start) { playbackSession.preparePracticeLoop(start); };
  sources.setLoopRange = [this](double start, double end) {
    return playbackSession.setPracticeLoopRange(start, end);
  };
  sources.releaseLoop = [this] { playbackSession.releasePracticeLoop(); };
  sources.ownsLoop = [this] { return playbackSession.ownsPracticeLoop(); };
  sources.loopRequestRejected = [this] { return playbackSession.wasPracticeLoopRequestRejected(); };
  sources.loopError = [this] { return playbackSession.getPracticeLoopError(); };
  sources.minimumLoopDuration = [this] { return playbackSession.getMinimumPracticeLoopDuration(); };
  sources.timelineMarks = [this, originalBpm](juce::Range<double> visible, float width) {
    return waveformBarPtr != nullptr ? waveformBarPtr->getPracticeTimelineMarks(visible, width)
        : PracticeTimelineRuler::build(visible, width, originalBpm(),
                                       currentLoadedSong.timeSignature);
  };
  sources.seekRange = [this] {
    auto range = juce::Range<double>(playbackSession.getSongStartSeconds(),
                                      playbackSession.getSongEndSeconds());
    if (playbackSession.isPracticeLoopActive())
      return range.getIntersectionWith(playbackSession.getPracticeLoopRange());
    const int loop = waveformBarPtr != nullptr
        ? waveformBarPtr->getArrangementPartIndex(playbackSession.getArrangementPartLoopId()) : -1;
    if (loop >= 0 && waveformBarPtr != nullptr) {
      const auto loopRange = waveformBarPtr->getArrangementPartTimeRange(loop);
      if (!loopRange.isEmpty())
        range = range.getIntersectionWith(loopRange);
    }
    return range;
  };
  sources.seekToSeconds =
      [this](double seconds) { playbackSession.seekSongTo(seconds); };
  sources.beginScrub = [this] { playbackSession.beginPracticeScrub(); };
  sources.finishScrub =
      [this](double seconds) { return playbackSession.finishPracticeScrub(seconds); };
  sources.scrubFinishRejected = [this] {
    return playbackSession.wasPracticeScrubFinishRejected();
  };
  sources.cancelScrub = [this](bool restorePlayback) {
    playbackSession.cancelPracticeScrub(restorePlayback);
  };
  sources.isPlaying = [this] { return playbackSession.isSongPlaying(); };
  sources.canControl = [this] {
    return playbackSession.hasLoadedSong() &&
           (!playbackSession.isRemote() || playbackSession.isConnected());
  };
  const auto canControlAccompaniment = [this] {
    return hasCurrentLoadedSong && currentLoadedSong.id.isNotEmpty() &&
           !songLoadBusy && playbackSession.hasLoadedSong() &&
           (!playbackSession.isRemote() || playbackSession.isConnected());
  };
  const auto accompanimentIdentity = [this] {
    auto identity = playbackSession.isRemote() ? juce::String("remote")
                                              : juce::String("local");
    const auto append = [&identity](const juce::String &value) {
      identity << '|' << value.length() << ':' << value;
    };
    append(currentLoadedSong.id);
    if (playbackSession.isRemote())
      append(remoteSession.getSelectedServerName());
    return identity;
  };
  const auto guideSelection = [this, canControlAccompaniment] {
    playworship::practice::GuideSelection selection;
    if (!canControlAccompaniment() || !playbackSession.hasGuideSourceState())
      return selection;
    selection.dynamic = playbackSession.isDynamicGuideEnabled();
    if (selection.dynamic) {
      selection.available = playbackSession.hasDynamicGuideAssets() &&
                            playbackSession.hasConfirmedDynamicGuideMixState();
    } else {
      selection.stemIndices = playworship::practice::originalGuideStemIndices(
          currentLoadedSong.stems, playbackSession.getLoadedStemCount(),
          [](const ImportedStemData &stem) {
            return stemPriorityForStripOrder(stem) == 1;
          });
      selection.stemIndices.erase(
          std::remove_if(selection.stemIndices.begin(), selection.stemIndices.end(),
                         [this](const int index) {
                           return !playbackSession.hasConfirmedStemMixState(index);
                         }),
          selection.stemIndices.end());
      selection.available = !selection.stemIndices.empty();
    }
    return selection;
  };
  sources.guideState = [this, canControlAccompaniment, accompanimentIdentity,
                        guideSelection] {
    PracticePanel::AccompanimentState state;
    const auto selection = guideSelection();
    state.identity = accompanimentIdentity() +
        (playbackSession.hasGuideSourceState()
             ? (playbackSession.isDynamicGuideEnabled() ? "|guide:dynamic"
                                                        : "|guide:original")
             : "|guide:unknown");
    // Include the exact loaded targets so replacing/reordering guide stems
    // cancels a pending request even if the song ID did not change.
    int includedIndex = 0;
    for (const auto &stem : currentLoadedSong.stems) {
      if (!stem.include)
        continue;
      const int index = includedIndex++;
      if (std::find(selection.stemIndices.begin(), selection.stemIndices.end(),
                    index) == selection.stemIndices.end())
        continue;
      state.identity << '|' << index << ':' << stem.stemId.length() << ':'
                     << stem.stemId;
    }
    state.available = selection.available;
    state.enabled = playworship::practice::guideEnabled(playbackSession, selection);
    if (!canControlAccompaniment())
      state.unavailableReason = L10n::tr("Carga una canción y conecta el control.",
                                         "Load a song and connect the controller.");
    else if (!playbackSession.hasGuideSourceState())
      state.unavailableReason = L10n::tr(
          "Actualiza PlayWorship en el servidor para controlar la guía.",
          "Update PlayWorship on the server to control the guide.");
    else if (selection.dynamic && !playbackSession.hasConfirmedDynamicGuideMixState())
      state.unavailableReason = L10n::tr("El estado de mezcla de la guía no está disponible.",
                                         "The guide mixer state is unavailable.");
    else if (!selection.available)
      state.unavailableReason = selection.dynamic
          ? L10n::tr("El paquete de guía dinámica no está cargado.",
                     "The dynamic guide pack is not loaded.")
          : L10n::tr("Esta canción no tiene una pista de guía original cargada.",
                     "This song has no loaded original guide track.");
    return state;
  };
  sources.setGuideEnabled = [this, guideSelection](const bool enabled) {
    playworship::practice::setGuideEnabled(playbackSession, guideSelection(), enabled);
  };
  sources.metronomeState = [this, canControlAccompaniment, accompanimentIdentity] {
    PracticePanel::AccompanimentState state;
    state.identity = accompanimentIdentity() + "|metronome:dynamic";
    state.available = canControlAccompaniment() &&
                      playbackSession.hasConfirmedMetronomeState();
    state.enabled = state.available &&
                    playworship::practice::metronomeEnabled(playbackSession);
    if (!canControlAccompaniment())
      state.unavailableReason = L10n::tr("Carga una canción y conecta el control.",
                                         "Load a song and connect the controller.");
    else if (!state.available)
      state.unavailableReason = L10n::tr("El estado del metrónomo no está disponible.",
                                         "The metronome state is unavailable.");
    return state;
  };
  sources.setMetronomeEnabled = [this, canControlAccompaniment](const bool enabled) {
    if (!canControlAccompaniment() || !playbackSession.hasConfirmedMetronomeState())
      return;
    playworship::practice::setMetronomeEnabled(playbackSession, enabled);
    if (!playbackSession.isRemote())
      persistClickDynMetronomeEnabledForActiveSong(playbackSession.isMetronomeEnabled());
  };
  sources.instruments = [this] {
    std::vector<PracticePanel::Instrument> result;
    if (!hasCurrentLoadedSong)
      return result;
    for (size_t i = 0; i < currentLoadedSong.stems.size(); ++i) {
      const auto &stem = currentLoadedSong.stems[i];
      const auto label = stem.displayName + " " + stem.category + " " + stem.fileName;
      if (!stem.include || stemPriorityForStripOrder(stem) <= 1 ||
          label.containsIgnoreCase(juce::String::fromUTF8("guía")))
        continue;
      auto name = stem.displayName.trim();
      if (name.isEmpty())
        name = stem.category.trim();
      if (name.isEmpty())
        name = stem.fileName.trim();
      result.push_back({juce::String(static_cast<int>(i)), name});
    }
    return result;
  };
  sources.isRemote = [this] { return playbackSession.isRemote(); };
  sources.waveformRevision = [this] {
    if (playbackSession.isRemote() || !hasCurrentLoadedSong)
      return juce::String{};
    const auto timeline = static_cast<MTControlsAudioProcessor &>(processor)
                              .getPracticeWaveformTimeline();
    auto identity = juce::String::fromUTF8(
        playworship::practice::waveformTimelineIdentity(timeline).c_str());
    // Source metadata is in memory. Do not stat files from the panel's timer.
    // The processor's source revision also invalidates same-path reloads.
    const auto append = [&identity](const juce::String &value) {
      identity << '|' << value.length() << ':' << value;
    };
    for (const auto &stem : currentLoadedSong.stems) {
      append(stem.stemId);
      append(stem.storedPath);
      append(stem.sourcePath);
      append(juce::String(stem.include ? 1 : 0));
      append(juce::String(stem.durationSec, 9));
      append(juce::String(stem.sampleRate));
      append(juce::String(stem.channels));
    }
    return identity;
  };
  sources.listeningMode =
      [this] { return playbackSession.getPracticeListeningMode(); };
  sources.listeningInstrumentId = [this] {
    const int target = playbackSession.getPracticeListeningStem();
    int loadedIndex = 0;
    for (size_t rawIndex = 0; rawIndex < currentLoadedSong.stems.size();
         ++rawIndex) {
      if (!currentLoadedSong.stems[rawIndex].include)
        continue;
      if (loadedIndex++ == target)
        return juce::String(static_cast<int>(rawIndex));
    }
    return juce::String{};
  };
  sources.displayListeningMode =
      [this] { return playbackSession.getConfirmedPracticeListeningMode(); };
  sources.displayListeningInstrumentId = [this] {
    const int target = playbackSession.getConfirmedPracticeListeningStem();
    int loadedIndex = 0;
    for (size_t rawIndex = 0; rawIndex < currentLoadedSong.stems.size(); ++rawIndex) {
      if (!currentLoadedSong.stems[rawIndex].include)
        continue;
      if (loadedIndex++ == target)
        return juce::String(static_cast<int>(rawIndex));
    }
    return juce::String{};
  };
  struct ListeningOwnership {
    uint64_t revision = 0;
    bool remote = false;
  };
  const auto listeningOwnership = std::make_shared<ListeningOwnership>();
  sources.setListeningMode = [this, listeningOwnership](
                                const juce::String &instrumentId, int mode) {
    int target = -1;
    if (mode != 0) {
      if (instrumentId.isEmpty())
        return;
      const int rawTarget = instrumentId.getIntValue();
      int loadedIndex = 0;
      for (size_t rawIndex = 0; rawIndex < currentLoadedSong.stems.size();
           ++rawIndex) {
        if (!currentLoadedSong.stems[rawIndex].include)
          continue;
        if (static_cast<int>(rawIndex) == rawTarget) {
          target = loadedIndex;
          break;
        }
        ++loadedIndex;
      }
      if (target < 0)
        return;
    }
    playbackSession.setPracticeListeningMode(target, mode);
    listeningOwnership->remote = playbackSession.isRemote();
    listeningOwnership->revision =
        static_cast<MTControlsAudioProcessor &>(processor)
            .getPracticeListeningRevision();
  };
  sources.canRetargetListening = [this, listeningOwnership] {
    if (listeningOwnership->remote != playbackSession.isRemote())
      return false;
    if (playbackSession.isRemote())
      return playbackSession.ownsPracticeListening();
    return static_cast<MTControlsAudioProcessor &>(processor)
                   .getPracticeListeningRevision() == listeningOwnership->revision;
  };
  sources.finishListening = [this, listeningOwnership] {
    if (listeningOwnership->remote != playbackSession.isRemote())
      return;
    // The server checks remote ownership; local panels check the revision so
    // closing an older panel cannot undo a newer client gesture.
    if (playbackSession.isRemote())
      playbackSession.releasePracticeListening();
    else if (static_cast<MTControlsAudioProcessor &>(processor)
                 .getPracticeListeningRevision() == listeningOwnership->revision)
      playbackSession.setPracticeListeningMode(-1, 0);
  };
  sources.requestInstrumentWaveform = [this](
                                          const juce::String &instrumentId,
                                          std::shared_ptr<std::atomic<bool>> cancel,
                                          std::function<void(std::vector<float>)> deliver) {
    const int index = instrumentId.getIntValue();
    if (playbackSession.isRemote() || !hasCurrentLoadedSong ||
        !juce::isPositiveAndBelow(index,
                                 static_cast<int>(currentLoadedSong.stems.size()))) {
      deliver({});
      return;
    }
    const auto stem = currentLoadedSong.stems[static_cast<size_t>(index)];
    const auto timeline = static_cast<MTControlsAudioProcessor &>(processor)
                              .getPracticeWaveformTimeline();
    std::thread([stem, timeline, cancel,
                 deliver = std::move(deliver)]() mutable {
      const auto cancelled = [cancel] {
        return cancel->load(std::memory_order_relaxed);
      };
      const auto sourceBins = buildStemWaveformBins(
          stem, timeline.sourceLengthSeconds, kWaveformPreviewBinCount, cancelled);
      // Copies are views of the same source peaks, not new audio files or
      // independently normalised waves. Use the playback scheduler's map.
      auto bins = playworship::practice::projectWaveformBins(
          sourceBins, timeline, kWaveformPreviewBinCount, cancelled);
      if (cancel->load(std::memory_order_relaxed))
        return;
      juce::MessageManager::callAsync(
          [cancel, deliver = std::move(deliver),
           bins = std::move(bins)]() mutable {
            if (!cancel->load(std::memory_order_relaxed))
              deliver(std::move(bins));
          });
    }).detach();
  };
  sources.playPause = [this] { playbackSession.toggleSongPlayPause(); };
  sources.stop = [this] { playbackSession.stopSong(); };
  sources.close = [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] {
    juce::MessageManager::callAsync([safe] {
      if (safe != nullptr)
        safe->closePractice();
    });
  };

  sources.showHelp = [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)] {
    if (safe != nullptr)
      safe->showPracticeLearningGuide();
  };

  practiceOverlay = std::make_unique<PracticePanel>(std::move(sources));
  addAndMakeVisible(*practiceOverlay);
  practiceOverlay->setBounds(getLocalBounds());
  practiceOverlay->toFront(true);
  if (learningGuide) {
    learningGuide->toFront(false);
    updateLearningGuideTarget();
  }
  updatePhoneOrientation();
}

void MTControlsAudioProcessorEditor::closePractice() {
  if (practiceOverlay == nullptr)
    return;
  practiceOverlay->setVisible(false);
  practiceOverlay.reset();
  updatePhoneOrientation();
  resized();
}

void MTControlsAudioProcessorEditor::showSectionDetection() {
  if (sectionDetectionPanel || guideReviewTranscript || !hasCurrentLoadedSong || playbackSession.isRemote() || !waveformBarPtr)
    return;
  const auto arrangement = waveformBarPtr->getArrangementSnapshot();
  // Providers analyze original synchronized files. A reordered source map
  // needs a separate projection; never apply source times to that arrangement.
  for (const auto &part : arrangement.parts)
    if (part.sourcePartId.isNotEmpty() ||
        (part.sourceEndSec > part.sourceStartSec &&
         (std::abs(part.startSec-part.sourceStartSec)>0.000001 || std::abs(part.endSec-part.sourceEndSec)>0.000001)))
      return;
  SectionDetectionPanel::Actions actions;
  const auto id = currentLoadedSong.id;
  auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  actions.preview = [safe,id](std::vector<SongArrangementPartSnapshot> preview) {
    if(safe && safe->hasCurrentLoadedSong && safe->currentLoadedSong.id==id && safe->waveformBarPtr)
      safe->waveformBarPtr->setSectionDetectionPreview(std::move(preview));
  };
  actions.listen = [safe,id](double time) {
    if(safe && safe->hasCurrentLoadedSong && safe->currentLoadedSong.id==id) {
      auto &p=static_cast<MTControlsAudioProcessor&>(safe->processor);
      p.seekSongTo(time); p.playSong();
    }
  };
  actions.accept = [safe,id](const auto &suggestions, double duration, juce::String &error) {
    if(!safe || !safe->hasCurrentLoadedSong || safe->currentLoadedSong.id!=id || !safe->waveformBarPtr || safe->playbackSession.isRemote()) {
      error=juce::String::fromUTF8("La canción cambió; vuelve a analizar la canción actual."); return false;
    }
    auto plan=playworship::analysis::appendConfirmedSuggestions(safe->waveformBarPtr->getArrangementSnapshot(), suggestions, duration);
    if(plan.error.isNotEmpty()) { error=plan.error; return false; }
    if(!safe->waveformBarPtr->applyDetectedSections(plan.arrangement)) { error=juce::String::fromUTF8("El arreglo cambió o hay un conflicto con secciones existentes."); return false; }
    return true;
  };
  actions.reviewGuide = [safe,id](const playworship::analysis::GuideTranscript &transcript) {
    if (safe && safe->hasCurrentLoadedSong && safe->currentLoadedSong.id == id && !safe->playbackSession.isRemote())
      safe->beginGuideSectionReview(transcript);
  };
  actions.close = [safe] { juce::MessageManager::callAsync([safe] { if(safe) safe->closeSectionDetection(true); }); };
  sectionDetectionPanel = std::make_unique<SectionDetectionPanel>(currentLoadedSong,std::move(actions));
  sectionDetectionPanel->showModal(*this);
  sectionDetectionPanel->beginSuggestionFlow();
}

void MTControlsAudioProcessorEditor::closeSectionDetection(bool keepGuideReview) {
  if(waveformBarPtr) waveformBarPtr->clearSectionDetectionPreview();
  sectionDetectionPanel.reset();
  if (!keepGuideReview) finishGuideSectionReview(false);
}

void MTControlsAudioProcessorEditor::beginGuideSectionReview(
    const playworship::analysis::GuideTranscript &transcript) {
  if (!hasCurrentLoadedSong || currentLoadedSong.blankSong || songLoadBusy ||
      playbackSession.isRemote() || !waveformBarPtr) return;
#if !JUCE_IOS && !JUCE_ANDROID
  // Always install the result on the waveform, including programmatic changes
  // of view while analysis was running.
  lyricsViewActive = false;
#endif
  guideReviewTranscript = transcript;
  // The guide may stop before the musical tail. The draft ends at the full
  // original source, never at the last spoken announcement or playback tempo.
  const auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
  guideReviewTranscript->durationSeconds = std::max(transcript.durationSeconds,
                                                    processorRef.getLoadedSongLengthSeconds());
  guideReviewSongId = currentLoadedSong.id;
  guideReviewNames.clear(); guideReviewDiscarded.clear(); guideReviewSecondaryTimes.clear();
  playworship::analysis::GuideAlignmentOptions options;
  options.bpm = currentLoadedSong.bpm;
  options.timeSignature = currentLoadedSong.timeSignature;
  options.tempoMap = currentLoadedSong.tempoMap;
  options = playworship::analysis::makeInitialGuideAlignmentOptions(*guideReviewTranscript, options);
  juce::String learningError;
  if (!playworship::analysis::loadGuideSectionLearnedMappings(
          guideSectionLearningFile(), options.learnedMappings, learningError))
    juce::Logger::writeToLog("Guide section corrections could not be loaded; using the built-in catalog.");
  guideReviewDraft = playworship::analysis::alignGuideSections(*guideReviewTranscript, options);
  waveformBarPtr->setGuideAlignmentPreview(guideReviewDraft);
  if (guideReviewDraft.error.isNotEmpty()) waveformBarPtr->setGuideAlignmentError(guideReviewDraft.error);
  resized();
  repaint();
}

void MTControlsAudioProcessorEditor::rebuildGuideSectionReview(
    const playworship::analysis::GuideAlignmentOptions &options) {
  if (!guideReviewTranscript || guideReviewSongId != currentLoadedSong.id || !waveformBarPtr || playbackSession.isRemote()) return;
  auto rebuilt = playworship::analysis::alignGuideSections(*guideReviewTranscript, options);
  if (!playworship::analysis::canReviewGuideAlignment(rebuilt)) {
    waveformBarPtr->setGuideAlignmentError(rebuilt.error.isNotEmpty() ? rebuilt.error :
        L10n::tr("La propuesta tiene datos inválidos.", "The suggestion contains invalid data.")); return;
  }
  rebuilt.suggestions.erase(std::remove_if(rebuilt.suggestions.begin(), rebuilt.suggestions.end(),
      [this](const auto &suggestion) { return guideReviewDiscarded.count(suggestion.id.toStdString()) != 0; }), rebuilt.suggestions.end());
  std::vector<juce::String> discardedPending;
  for (const auto &pending : rebuilt.pendingAnnouncements)
    if (guideReviewDiscarded.count(pending.id.toStdString())) discardedPending.push_back(pending.id);
  for (const auto &id : discardedPending) {
    juce::String error;
    if (!playworship::analysis::discardGuidePendingAnnouncement(rebuilt, id, error)) {
      waveformBarPtr->setGuideAlignmentError(error); return;
    }
  }
  for (size_t i = 0; i < rebuilt.suggestions.size(); ++i) {
    auto &suggestion = rebuilt.suggestions[i];
    suggestion.endSeconds = i + 1 < rebuilt.suggestions.size()
        ? rebuilt.suggestions[i + 1].startSeconds : rebuilt.durationSeconds;
    const auto corrected = guideReviewNames.find(suggestion.id);
    if (corrected != guideReviewNames.end()) {
      applyReviewedGuideSectionName(suggestion, corrected->second);
    }
  }
  // Apply explicit per-source review decisions before deciding whether a
  // rebuilt timing issue is still pending. Structural errors were rejected above.
  std::vector<juce::String> discardedSecondary;
  for (const auto &cue : rebuilt.secondaryCues)
    if (guideReviewDiscarded.count(cue.id.toStdString())) discardedSecondary.push_back(cue.id);
  for (const auto &id : discardedSecondary) {
    juce::String error;
    if (!playworship::analysis::discardGuideSecondaryCue(rebuilt, id, error)) {
      waveformBarPtr->setGuideAlignmentError(error); return;
    }
  }
  for (auto &cue : rebuilt.secondaryCues) {
    const auto correctedName = guideReviewNames.find(cue.id);
    if (correctedName != guideReviewNames.end()) { cue.name = correctedName->second; cue.userEdited = true; }
    const auto correctedTime = guideReviewSecondaryTimes.find(cue.id);
    if (correctedTime != guideReviewSecondaryTimes.end()) {
      juce::String error;
      if (!playworship::analysis::moveGuideSecondaryCue(rebuilt, cue.id, correctedTime->second, false, error)) {
        waveformBarPtr->setGuideAlignmentError(error); return;
      }
    }
  }
  guideReviewDraft = std::move(rebuilt);
  waveformBarPtr->setGuideAlignmentPreview(guideReviewDraft);
}

void MTControlsAudioProcessorEditor::finishGuideSectionReview(bool confirm) {
  if (!confirm) {
    if (waveformBarPtr) waveformBarPtr->clearGuideAlignmentPreview();
    guideReviewTranscript.reset(); guideReviewDraft = {};
    guideReviewSongId.clear(); guideReviewNames.clear(); guideReviewDiscarded.clear(); guideReviewSecondaryTimes.clear();
    return;
  }
  if (!guideReviewTranscript || !hasCurrentLoadedSong || guideReviewSongId != currentLoadedSong.id ||
      playbackSession.isRemote() || !waveformBarPtr) return;
  if (guideReviewDraft.error.isNotEmpty()) {
    waveformBarPtr->setGuideAlignmentError(guideReviewDraft.error); return;
  }
  if (!guideReviewDraft.pendingAnnouncements.empty()) {
    waveformBarPtr->setGuideAlignmentError(L10n::tr("Elige una marca del catálogo para cada anuncio pendiente.",
        "Choose a catalogue mark for each pending announcement.")); return;
  }
  if (guideReviewDraft.suggestions.empty() && guideReviewDraft.secondaryCues.empty()) {
    waveformBarPtr->setGuideAlignmentError(L10n::tr("No hay sugerencias para confirmar.", "There are no suggestions to confirm.")); return;
  }
  const auto plan = playworship::analysis::appendConfirmedGuideSuggestions(
      waveformBarPtr->getArrangementSnapshot(), guideReviewDraft);
  if (plan.error.isNotEmpty()) { waveformBarPtr->setGuideAlignmentError(plan.error); return; }
  std::vector<playworship::analysis::GuideConfirmedSectionRename> reviewedNames;
  for (const auto &[id, name] : guideReviewNames) reviewedNames.push_back({id, name});
  std::vector<playworship::analysis::SectionSuggestion> acceptedSuggestions;
  for (const auto &suggestion : guideReviewDraft.suggestions)
    if (plan.addedSuggestionIds.contains(suggestion.id)) acceptedSuggestions.push_back(suggestion);
  const auto learned = playworship::analysis::guideSectionCorrectionsForConfirmedReview(
      *guideReviewTranscript, acceptedSuggestions, reviewedNames);
  if (!waveformBarPtr->applyDetectedSections(plan.arrangement)) {
    waveformBarPtr->setGuideAlignmentError(L10n::tr(
        "Hay un conflicto con las secciones existentes. Ajusta o descarta las marcas que coincidan.",
        "There is a conflict with existing sections. Adjust or discard overlapping suggestions."));
    return;
  }
  juce::String learningError;
  const bool remembered = learned.empty() || playworship::analysis::mergeAndSaveGuideSectionLearnedMappings(
      guideSectionLearningFile(), learned, learningError);
  finishGuideSectionReview(false);
  if (!remembered)
    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
        L10n::tr("Sugerencias confirmadas", "Suggestions confirmed"),
        L10n::tr("Las secciones se guardaron, pero no pudimos recordar las correcciones de nombres para próximos análisis.",
                 "The sections were saved, but the name corrections could not be remembered for future analyses."), this);
}

bool MTControlsAudioProcessorEditor::navigateBackInSettings() {
  if (settingsOverlay == nullptr || settingsPanelRaw == nullptr)
    return false;
  return settingsPanelRaw->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
}

void MTControlsAudioProcessorEditor::showWelcomeOnlyMode() {
  canManageSharedCloud = false;
  cloudCatalogSongs.clear();
  ++cloudLibrarySearchGeneration;
  cloudDownloadsInProgress.clear();
  closeTeams();
  if (teamsClient) teamsClient->cancelAll();
  teamsPreparation.reset();
  closePractice();
  setResizable(true, true);
#if JUCE_IOS || JUCE_ANDROID
  setResizeLimits(320, 280, 32000, 32000);
#else
  setResizeLimits(480, 500, 32000, 32000);
#endif
#if JUCE_IOS || JUCE_ANDROID
  fitMobileEditorToHost();
#endif
  if (topBar)
    topBar->setVisible(false);
  if (waveformBar)
    waveformBar->setVisible(false);
#if !JUCE_IOS && !JUCE_ANDROID
  if (lyricsPanel) lyricsPanel->setVisible(false);
#endif
  if (channelGridPanel)
    channelGridPanel->setVisible(false);
  if (songListPanel)
    songListPanel->setVisible(false);
  if (setlistHandlePanel)
    setlistHandlePanel->setVisible(false);
  if (padsComponent)
    padsComponent->setVisible(false);
  if (padsHandlePanel)
    padsHandlePanel->setVisible(false);
  if (welcomeModal) {
    welcomeModal->setVisible(true);
    welcomeModal->toFront(false);
  }
  if (reauthPromptModal)
    reauthPromptModal->setVisible(false);
  mainUiInitialized = false;
  autoSetlistCollapseInitialized = false;
  resized();
}

void MTControlsAudioProcessorEditor::handleLogout() {
  const bool wasLocalDemo = localDemoSession;
  deletionReauthCandidate = {}; deletionReauthStatus = {};
  cancelAuthenticationAttempt();
  appleLoginInProgress = false;
  appleLinkInProgress = false;
  SessionAuthLogger::write("handleLogout");
  if (remoteSession.getMode() !=
      playworship::remote::RemoteSession::Mode::Local)
    setRemoteMode(playworship::remote::RemoteSession::Mode::Local);
  sessionResumeCancelled = true;
  closeSettings();
  closePractice();
  if (demoLoadingOverlay) demoLoadingOverlay->exitModalState(0);
  demoLoadingOverlay.reset();

  if (localAuthServer != nullptr)
    localAuthServer->stop();
  authCallbackPort = 0;

  ++appleCredentialGeneration;
  appleCredentialMonitor.reset();
  if (serviceTermsClient) serviceTermsClient->cancelAll();
  serviceTermsRequestPending = false;
  activeSession = {};
  activeSession.allowed = false;
  localDemoSession = false;
  AppSettings::setLocalDemoSessionActive(false);
  lastOnlineValidationMs = 0;
  if (!wasLocalDemo) SessionPersistence::clear();
  lastSuccessfulLoginMs = 0;
  if (!wasLocalDemo) AppSettings::setLastSuccessfulLoginMs(0);
  reauthGraceDeadlineMs = 0;
  if (!wasLocalDemo) AppSettings::setReauthGraceDeadlineMs(0);
  reauthPromptDismissed = false;
  reauthLoginInProgress = false;
  hideReauthPromptModal();
  authExpectedState.clear();
  deferredStartupSongLoadPending = false;
  deferredStartupSongIndex = -1;
  deferredStartupSongLoadAtMs = 0;
  {
    const juce::ScopedLock lock(authCallbackLock);
    pendingAuthCallbackUrl.clear();
  }
  if (auto &proc = static_cast<MTControlsAudioProcessor &>(processor);
      proc.isSongPlaying()) {
    proc.stopSong();
  }
  showWelcomeOnlyMode();
  if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
    { modal->setChecking(false); modal->clearCredentials(); modal->resetStatusToDefault(); }
}

void MTControlsAudioProcessorEditor::showReauthPromptModal() {
  if (!mainUiInitialized || reauthPromptModal == nullptr)
    return;
  if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get());
      reauth != nullptr && !reauthPromptModal->isVisible()) {
    if (reauthLoginInProgress)
      reauth->showWaitingForCallback();
    else
      reauth->showReadyState();
  }
  reauthPromptModal->setVisible(true);
  reauthPromptModal->toFront(false);
  resized();
}

void MTControlsAudioProcessorEditor::hideReauthPromptModal() {
  if (reauthPromptModal == nullptr)
    return;
  reauthPromptModal->setVisible(false);
  updatePhoneOrientation();
}

PersistedSessionSnapshot
MTControlsAudioProcessorEditor::persistedSnapshotFromActiveSession(
    const juce::int64 lastOnlineMs) const {
  PersistedSessionSnapshot p;
  p.uid = activeSession.uid;
  p.email = activeSession.email;
  p.authProvider = activeSession.authProvider;
  p.authProviderUser = activeSession.authProviderUser;
  p.plan = activeSession.plan;
  p.subscriptionStatus = activeSession.subscriptionStatus;
  p.expiresAt = activeSession.expiresAt;
  p.licenseKey = activeSession.licenseKey;
  p.allowed = activeSession.allowed;
  p.reasonCode = activeSession.reasonCode;
  p.reason = activeSession.reason;
  p.refreshToken = activeSession.refreshToken;
  p.firebaseApiKey = activeSession.firebaseApiKey;
  p.idTokenExpiresAtMs = activeSession.idTokenExpiresAtMs;
  p.lastOnlineValidationMs = lastOnlineMs;
  return p;
}

void MTControlsAudioProcessorEditor::applyPersistedSnapshotToActive(
    const PersistedSessionSnapshot &snap) {
  if (localDemoSession) return;
  if (activeSession.uid.isNotEmpty() && activeSession.uid != snap.uid) {
    if (serviceTermsClient) serviceTermsClient->cancelAll();
    serviceTermsRequestPending = false;
    invalidateCloudAccountView();
  }
  activeSession.uid = snap.uid;
  activeSession.email = snap.email;
  activeSession.authProvider = snap.authProvider;
  activeSession.authProviderUser = snap.authProviderUser;
  activeSession.plan = snap.plan;
  activeSession.subscriptionStatus = snap.subscriptionStatus;
  activeSession.expiresAt = snap.expiresAt;
  activeSession.licenseKey = snap.licenseKey;
  activeSession.allowed = snap.allowed;
  activeSession.reasonCode = snap.reasonCode;
  activeSession.reason = snap.reason;
  activeSession.refreshToken = snap.refreshToken;
  activeSession.firebaseApiKey = snap.firebaseApiKey;
  activeSession.idTokenExpiresAtMs = snap.idTokenExpiresAtMs;
  lastOnlineValidationMs = snap.lastOnlineValidationMs;
  refreshAppleCredentialMonitoring();
}

void MTControlsAudioProcessorEditor::saveSessionSnapshotToDisk() {
  if (localDemoSession) return;
  const auto nowMs = juce::Time::currentTimeMillis();
  lastOnlineValidationMs = nowMs;
  SessionPersistence::save(persistedSnapshotFromActiveSession(nowMs));
}

void MTControlsAudioProcessorEditor::tryResumeSessionAsync() {
  if (localDemoSession) return;
  SessionAuthLogger::write("tryResumeSessionAsync: begin");
  PersistedSessionSnapshot snap;
  if (!SessionPersistence::load(snap)) {
    SessionAuthLogger::write(
        "tryResumeSessionAsync: no valid snapshot, user must use web login");
    return;
  }
  if (!snap.allowed ||
      !hasValidAccessBySubscriptionState(snap.subscriptionStatus, snap.plan)) {
    SessionAuthLogger::write(
        "tryResumeSessionAsync: snapshot invalid allowed=" +
        juce::String(static_cast<int>(snap.allowed)) + " plan=" + snap.plan +
        " status=" + snap.subscriptionStatus + " -> clearing snapshot");
    SessionPersistence::clear();
    return;
  }

  if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get())) {
    modal->setStatusText(L10n::tr(u8"Comprobando sesión...", "Checking session..."),
                         false);
    modal->setChecking(true);
  }

  juce::Component::SafePointer<MTControlsAudioProcessorEditor> safe(this);
  const juce::String apiBase = getApiBaseUrl();
  const juce::String deviceId = buildStableDeviceId();
  const auto generation = authAttemptGeneration;
  SessionAuthLogger::write("tryResumeSessionAsync: apiBase=" + apiBase +
                           " deviceIdPrefix=" + deviceId.substring(0, 12) + "...");

  std::thread([safe, snap, apiBase, deviceId, generation] {
    bool success = false;
    bool validatedOnline = false;
    bool clearSnapshot = false;
    juce::String welcomeError;
    PersistedSessionSnapshot out = snap;

    const auto tryOfflineWithinTtl = [&]() {
      const juce::int64 nowMs = juce::Time::currentTimeMillis();
      const juce::int64 ageMs = nowMs - snap.lastOnlineValidationMs;
      if (snap.lastOnlineValidationMs > 0 &&
          ageMs <= kMaxOfflineMs && snap.allowed &&
          hasValidAccessBySubscriptionState(snap.subscriptionStatus, snap.plan)) {
        SessionAuthLogger::write(
            "tryResumeSessionAsync(bg): offline fallback OK ageMs=" +
            juce::String(ageMs) + " maxOfflineMs=" + juce::String(kMaxOfflineMs));
        success = true;
        validatedOnline = false;
        out = snap;
        return true;
      }
      SessionAuthLogger::write(
          "tryResumeSessionAsync(bg): offline fallback FAIL ageMs=" +
          juce::String(ageMs) + " lastOnlineMs=" +
          juce::String(snap.lastOnlineValidationMs) + " nowMs=" +
          juce::String(nowMs));
      welcomeError =
          L10n::tr(u8"Se requiere conexión para validar la suscripción.",
                   "An internet connection is required to validate your subscription.");
      clearSnapshot = false;
      return false;
    };

    const auto fr =
        AuthSessionApi::refreshFirebaseIdToken(snap.firebaseApiKey, snap.refreshToken);
    if (fr.ok) {
      out.refreshToken =
          fr.refreshToken.isNotEmpty() ? fr.refreshToken : snap.refreshToken;

      const auto va = AuthSessionApi::fetchValidateAccess(
          apiBase, fr.idToken, deviceId, snap.licenseKey);
      if (va.rawError == "network" || va.rawError == "parse") {
        SessionAuthLogger::write(
            va.rawError == "network"
                ? "tryResumeSessionAsync(bg): validate-access network error, trying "
                  "offline TTL"
                : "tryResumeSessionAsync(bg): validate-access non-JSON/infra "
                  "(parse), trying offline TTL");
        if (!tryOfflineWithinTtl())
          success = false;
      } else if (va.allowed && hasValidAccessBySubscriptionState(va.subscriptionStatus,
                                                                   va.plan)) {
        SessionAuthLogger::write(
            "tryResumeSessionAsync(bg): validate-access OK (online)");
        success = true;
        validatedOnline = true;
        out.allowed = true;
        out.plan = va.plan;
        out.subscriptionStatus = va.subscriptionStatus;
        out.expiresAt = va.expiresAt;
        if (va.licenseKey.isNotEmpty())
          out.licenseKey = va.licenseKey;
        out.reasonCode.clear();
        out.reason.clear();
        out.lastOnlineValidationMs = juce::Time::currentTimeMillis();
      } else {
        SessionAuthLogger::write(
            "tryResumeSessionAsync(bg): validate-access denied allowed=" +
            juce::String(static_cast<int>(va.allowed)) + " reasonCode=" +
            va.reasonCode);
        clearSnapshot = true;
        welcomeError =
            userMessageForAccessDenial(va.reasonCode, va.reason);
      }
    } else {
      if (fr.errorMessage == "network" || fr.errorMessage == "parse") {
        SessionAuthLogger::write(
            fr.errorMessage == "network"
                ? "tryResumeSessionAsync(bg): Firebase refresh network, trying "
                  "offline TTL"
                : "tryResumeSessionAsync(bg): Firebase refresh parse/infra, trying "
                  "offline TTL");
        if (!tryOfflineWithinTtl())
          success = false;
      } else {
        SessionAuthLogger::write(
            "tryResumeSessionAsync(bg): Firebase refresh failed error=" +
            fr.errorMessage);
        clearSnapshot = true;
        welcomeError =
            L10n::tr(u8"Sesión expirada. Vuelve a iniciar sesión.",
                     "Session expired. Please sign in again.");
      }
    }

    SessionAuthLogger::write(
        juce::String("tryResumeSessionAsync(bg): done success=") +
        juce::String(static_cast<int>(success)) + " validatedOnline=" +
        juce::String(static_cast<int>(validatedOnline)) + " clearSnapshot=" +
        juce::String(static_cast<int>(clearSnapshot)));

    juce::MessageManager::callAsync([safe, generation, success, validatedOnline, out, welcomeError,
                                     clearSnapshot] {
      if (safe != nullptr && generation == safe->authAttemptGeneration)
        safe->applySessionResumeOnMessageThread(success, validatedOnline, out,
                                                welcomeError, clearSnapshot);
    });
  }).detach();
}

void MTControlsAudioProcessorEditor::applySessionResumeOnMessageThread(
    const bool success, const bool validatedOnline, PersistedSessionSnapshot snapshot,
    const juce::String &welcomeError, const bool clearSnapshotFile) {
  if (localDemoSession || sessionResumeCancelled) {
    SessionAuthLogger::write(
        "applySessionResumeOnMessageThread: skipped (session was signed out)");
    return;
  }

  if (authExpectedState.isNotEmpty()) {
    SessionAuthLogger::write(
        "applySessionResumeOnMessageThread: skipped (user started web login, "
        "authExpectedState set)");
    return;
  }

  if (mainUiInitialized) {
    if (clearSnapshotFile) {
      SessionAuthLogger::write(
          "applySessionResumeOnMessageThread: online validation denied active UI, "
          "logging out");
      handleLogout();
      if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get())) {
        if (welcomeError.isNotEmpty())
          modal->setStatusText(welcomeError, true);
        else
          modal->resetStatusToDefault();
      }
      return;
    }

    if (!success) {
      SessionAuthLogger::write(
          "applySessionResumeOnMessageThread: active UI kept after background "
          "validation failure welcomeError=" +
          welcomeError);
      return;
    }

    SessionAuthLogger::write(
        juce::String("applySessionResumeOnMessageThread: active UI session "
                     "refreshed validatedOnline=") +
        juce::String(static_cast<int>(validatedOnline)));
    applyPersistedSnapshotToActive(snapshot);
    if (validatedOnline) {
      SessionPersistence::save(snapshot);
      lastSuccessfulLoginMs = juce::Time::currentTimeMillis();
      lastOnlineValidationMs = snapshot.lastOnlineValidationMs;
      AppSettings::setLastSuccessfulLoginMs(lastSuccessfulLoginMs);
    }
    reauthGraceDeadlineMs = 0;
    AppSettings::setReauthGraceDeadlineMs(0);
    reauthPromptDismissed = false;
    reauthLoginInProgress = false;
    hideReauthPromptModal();
    if (songListPanelRaw) songListPanelRaw->refreshFromStorageView();
    refreshCloudLibrary({});
    return;
  }

  if (clearSnapshotFile)
    SessionPersistence::clear();

  if (!success) {
    SessionAuthLogger::write(
        "applySessionResumeOnMessageThread: failure welcomeError=" + welcomeError);
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get())) {
      modal->setChecking(false);
      if (welcomeError.isNotEmpty())
        modal->setStatusText(welcomeError, true);
      else
        modal->resetStatusToDefault();
    }
    return;
  }

  SessionAuthLogger::write(
      juce::String("applySessionResumeOnMessageThread: entering main UI ") +
      "validatedOnline=" + juce::String(static_cast<int>(validatedOnline)));

  applyPersistedSnapshotToActive(snapshot);
  if (validatedOnline) {
    SessionPersistence::save(snapshot);
    lastSuccessfulLoginMs = juce::Time::currentTimeMillis();
    lastOnlineValidationMs = snapshot.lastOnlineValidationMs;
    AppSettings::setLastSuccessfulLoginMs(lastSuccessfulLoginMs);
  } else {
    lastSuccessfulLoginMs =
        juce::jmax(lastSuccessfulLoginMs, snapshot.lastOnlineValidationMs);
    AppSettings::setLastSuccessfulLoginMs(lastSuccessfulLoginMs);
  }
  reauthGraceDeadlineMs = 0;
  AppSettings::setReauthGraceDeadlineMs(0);
  reauthPromptDismissed = false;
  reauthLoginInProgress = false;

  initializeMainUi();
  refreshCloudLibrary({});
  if (validatedOnline) showServiceTermsIfNeeded();
}

void MTControlsAudioProcessorEditor::evaluateReauthPolicy() {
  // lastSuccessfulLoginMs: ultima confirmacion util (login web o validate-access al
  // arrancar). Se actualiza en disco via AppSettings; lastOnlineValidationMs duplica
  // el valor persistido en session_snapshot para ventana offline.
  if (localDemoSession || !mainUiInitialized || lastSuccessfulLoginMs <= 0)
    return;

  if (!hasValidAccessBySubscriptionState(activeSession.subscriptionStatus,
                                         activeSession.plan)) {
    activeSession.allowed = false;
    activeSession.reasonCode =
        activeSession.subscriptionStatus.trim().toLowerCase() == "cancelled"
            ? "CANCELLED_SUBSCRIPTION"
            : "INVALID_SUBSCRIPTION_STATE";
    activeSession.reason =
        "Subscription status is not free/trial/active with a valid plan";
    SessionAuthLogger::write(
        "evaluateReauthPolicy: invalid subscription state -> handleLogout code=" +
        activeSession.reasonCode);
    handleLogout();
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
      modal->setStatusText(userMessageForAccessDenial(activeSession.reasonCode,
                                                      activeSession.reason),
                           true);
    return;
  }

  const auto nowMs = juce::Time::currentTimeMillis();
  const auto sessionDueAtMs = lastSuccessfulLoginMs + kSessionReauthIntervalMs;
  if (nowMs < sessionDueAtMs) {
    if (reauthGraceDeadlineMs != 0) {
      reauthGraceDeadlineMs = 0;
      AppSettings::setReauthGraceDeadlineMs(0);
    }
    hideReauthPromptModal();
    return;
  }

  // La gracia inicia en el momento en que solicitamos re-login por primera vez.
  if (reauthGraceDeadlineMs <= 0) {
    reauthGraceDeadlineMs = nowMs + kReauthGraceWindowMs;
    AppSettings::setReauthGraceDeadlineMs(reauthGraceDeadlineMs);
    reauthPromptDismissed = false;
  }

  if (reauthGraceDeadlineMs > 0 && nowMs >= reauthGraceDeadlineMs) {
    SessionAuthLogger::write(
        "evaluateReauthPolicy: reauth grace expired -> handleLogout");
    handleLogout();
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
      modal->setStatusText(
          "Tu periodo de gracia termino. Debes iniciar sesion para continuar.",
          true);
    return;
  }

  // Si ya hay gracia activa (el usuario pulso "Mas tarde"), no molestamos
  // hasta que venza esa gracia.
  if (reauthGraceDeadlineMs > 0 && reauthPromptDismissed) {
    hideReauthPromptModal();
    return;
  }

  showReauthPromptModal();
}

void MTControlsAudioProcessorEditor::applyThemePreset(int presetId) {
  currentThemeId = juce::jlimit(2, 3, presetId);
  AppSettings::setThemePresetId(currentThemeId);
  applyTheme(static_cast<ThemePreset>(currentThemeId));
  if (refreshTopBarTheme)
    refreshTopBarTheme();
  if (songListPanelRaw != nullptr)
    songListPanelRaw->refreshThemeColours();
  repaint();
}

void MTControlsAudioProcessorEditor::applyRoutingDefaultsToSong(
    const ImportedSongData &song) {
  if (playbackSession.isRemote()) return;
  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
  const auto audioCtx = getStandaloneAudioContext(processorRef);
  const auto deviceKey = getCurrentDeviceKey(audioCtx);
  const auto defaults = AppSettings::getRoutingDefaultsForDevice(deviceKey);
  const auto clickRouting = routingForCategory(defaults, "Click");
  const auto guideRouting = routingForCategory(defaults, "Cues");
  SongLoadProbe::instance().recordEvent(
      "routing", "apply.enter",
      "deviceKey=" + deviceKey +
          " stemCount=" + juce::String(processorRef.getLoadedStemCount()) +
          " clickW=" + juce::String(clickRouting.width) +
          " clickC=" + juce::String(clickRouting.baseChannel) +
          " guideW=" + juce::String(guideRouting.width) +
          " guideC=" + juce::String(guideRouting.baseChannel));

  processorRef.applySongOutputRoutingDefaults(song, defaults);

  SongLoadProbe::instance().recordEvent(
      "routing", "apply.exit",
      "stemsApplied=" + juce::String(juce::jmin(
          processorRef.getLoadedStemCount(),
          static_cast<int>(std::count_if(song.stems.begin(), song.stems.end(),
              [](const auto &stem) { return stem.include; })))));
  // Pads remain the caller's responsibility, avoiding an extra settings read
  // on each song load or routing-table change.
}

bool MTControlsAudioProcessorEditor::unloadActiveSongForLocalPurge() {
  if (hasCurrentLoadedSong)
    flushActiveSongPersistenceNow();

  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
  if (!processorRef.unloadSong())
    return false;

  // A waveform preview may still be rendering on a detached worker. Advance
  // the generation before clearing the UI so its late callback cannot restore
  // the purged song's bins. Do this only after a successful unload so a failed
  // purge leaves the current song completely untouched.
  waveformGeneration.fetch_add(1, std::memory_order_acq_rel);

  deferredStartupSongLoadPending = false;
  deferredStartupSongIndex = -1;
  deferredStartupSongLoadAtMs = 0;
  pendingExternalProjectFile = juce::File{};
  wasSongPlaying = false;
  currentSongIndex = -1;
  hasCurrentLoadedSong = false;
  currentLoadedSong = {};
#if !JUCE_IOS && !JUCE_ANDROID
  lyricsViewActive = false;
  if (lyricsPanel) {
    lyricsPanel->setSongLyrics({});
    lyricsPanel->setArrangement({});
    lyricsPanel->setVisible(false);
  }
#endif
  AppSettings::setLastSelectedSongId({});
  resetTrackedSongWindow();
  hasTrackedClickDynMetronomeEnabled = false;
  trackedClickDynMetronomeEnabled = false;

  if (waveformBarPtr != nullptr) {
    waveformBarPtr->clearSongLoadingState();
    refreshWaveformPresentationForSong();
    waveformBarPtr->setWaveformBins({});
    waveformBarPtr->setSongRhythm(0.0, {});
    waveformBarPtr->setArrangement({}, false);
    waveformBarPtr->setTempoMap({});
    waveformBarPtr->resetPlaybackViewForSong();
  }
  if (auto *grid =
          dynamic_cast<ChannelGridPanel *>(channelGridPanel.get())) {
    grid->setSong({});
  }

  repaint();
  return true;
}

void MTControlsAudioProcessorEditor::resetTrackedSongWindow() {
  trackedSongWindowSongId.clear();
  trackedSongWindowStartSec = 0.0;
  trackedSongWindowEndSec = 0.0;
  trackedSongWindowFadeSec = 0.0;
  hasTrackedSongWindow = false;
}

void MTControlsAudioProcessorEditor::trackSongWindow(
    const juce::String &songId, const double startSec, const double endSec,
    const double fadeSec) {
  trackedSongWindowSongId = songId;
  trackedSongWindowStartSec = startSec;
  trackedSongWindowEndSec = endSec;
  trackedSongWindowFadeSec = fadeSec;
  hasTrackedSongWindow = songId.isNotEmpty();
}

void MTControlsAudioProcessorEditor::maybePersistSongWindowForActiveSong() {
  if (playbackSession.isRemote()) return;
  if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty()) {
    resetTrackedSongWindow();
    return;
  }

  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
  const auto songId = currentLoadedSong.id;
  const double startSec = processorRef.getSongStartSeconds();
  const double endSec = processorRef.getSongEndSeconds();
  const double fadeSec = processorRef.getSongFadeOutSeconds();

  if (!hasTrackedSongWindow || trackedSongWindowSongId != songId) {
    trackSongWindow(songId, startSec, endSec, fadeSec);
    return;
  }

  if (nearlyEqualSongWindowValue(startSec, trackedSongWindowStartSec) &&
      nearlyEqualSongWindowValue(endSec, trackedSongWindowEndSec) &&
      nearlyEqualSongWindowValue(fadeSec, trackedSongWindowFadeSec)) {
    return;
  }

  SongWindowSnapshot snapshot;
  snapshot.startSec = startSec;
  snapshot.endSec = endSec;
  snapshot.fadeOutSec = fadeSec;

  const double songLength = juce::jmax(0.0, processorRef.getSongLengthSeconds());
  const bool isDefaultWindow =
      nearlyEqualSongWindowValue(startSec, 0.0) &&
      nearlyEqualSongWindowValue(endSec, songLength) &&
      nearlyEqualSongWindowValue(fadeSec, juce::jmin(1.0, songLength));

  if (!SongLibraryStore::updateSongWindowForActiveSetlist(songId, snapshot,
                                                           !isDefaultWindow)) {
    SessionAuthLogger::write(
        "[SongWindow] Failed to persist setlist song window for songId=" + songId);
  }

  trackSongWindow(songId, startSec, endSec, fadeSec);
}

void MTControlsAudioProcessorEditor::persistClickDynMetronomeEnabledForActiveSong(
    const bool enabled) {
  if (playbackSession.isRemote()) return;
  if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
    return;

  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
  ClickDynMixSnapshot mix;
  mix.divisionMask = processorRef.getClickDivision();
  mix.enabled = processorRef.isClickDivisionEnabled();
  mix.metronomeEnabled = enabled;
  mix.volume = processorRef.getMetronomeVolume();
  mix.gain = processorRef.getMetronomeGain();
  mix.mute = processorRef.isMetronomeMuted();
  mix.solo = processorRef.isMetronomeSolo();
  mix.outputChannel = processorRef.getMetronomeOutputChannel();
  mix.outputWidth = processorRef.getMetronomeOutputWidth();
  SongLibraryStore::updateClickDynMixForActiveSetlist(currentLoadedSong.id, mix);
  trackClickDynMetronomeEnabled(enabled);
}

void MTControlsAudioProcessorEditor::trackClickDynMetronomeEnabled(
    const bool enabled) {
  trackedClickDynMetronomeEnabled = enabled;
  hasTrackedClickDynMetronomeEnabled = true;
}

void MTControlsAudioProcessorEditor::
    maybePersistClickDynMetronomeEnabledForActiveSong() {
  if (songLoadBusy || !hasCurrentLoadedSong || currentLoadedSong.id.isEmpty()) {
    hasTrackedClickDynMetronomeEnabled = false;
    return;
  }

  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
  const bool enabled = processorRef.isMetronomeEnabled();
  if (hasTrackedClickDynMetronomeEnabled &&
      trackedClickDynMetronomeEnabled == enabled) {
    return;
  }

  persistClickDynMetronomeEnabledForActiveSong(enabled);
}

void MTControlsAudioProcessorEditor::updateSystemUsageMetrics() {
  if (!mainUiInitialized)
    return;

  auto *topBarRaw = dynamic_cast<TopBar *>(topBar.get());
  if (topBarRaw == nullptr)
    return;

  const auto audioCtx = getStandaloneAudioContext(static_cast<MTControlsAudioProcessor &>(processor));
  double rawAudioCpu01 = 0.0;
  if (audioCtx.manager != nullptr)
    rawAudioCpu01 = juce::jlimit(0.0, 1.0, audioCtx.manager->getCpuUsage());

  const double rawProcessMemoryMb = getCurrentProcessMemoryUsageMb();
  if (smoothedProcessMemoryMb <= 0.0) {
    smoothedProcessMemoryMb = juce::jmax(0.0, rawProcessMemoryMb);
  } else if (rawProcessMemoryMb > 0.0) {
    smoothedProcessMemoryMb +=
        (rawProcessMemoryMb - smoothedProcessMemoryMb) * 0.14;
  }

  smoothedAudioCpuUsage01 += (rawAudioCpu01 - smoothedAudioCpuUsage01) * 0.22;

  const int totalMemoryMb = juce::jmax(
      1, totalSystemMemoryMb > 0 ? totalSystemMemoryMb
                                 : juce::SystemStats::getMemorySizeInMegabytes());
  const double memoryUsage01 = juce::jlimit(
      0.0, 1.0, smoothedProcessMemoryMb / static_cast<double>(totalMemoryMb));

  topBarRaw->setSystemUsage(smoothedAudioCpuUsage01, memoryUsage01);
}

void MTControlsAudioProcessorEditor::timerCallback() {
  if (guideReviewTranscript && (!hasCurrentLoadedSong || songLoadBusy ||
      playbackSession.isRemote() || guideReviewSongId != currentLoadedSong.id))
    finishGuideSectionReview(false);
  if (appleLinkFlow.stage != AppleLinkFlow::Stage::idle &&
      !appleLinkFlow.valid(juce::Time::currentTimeMillis())) {
    cancelAuthenticationAttempt();
    setAppleAuthenticationStatus(L10n::tr("La vinculación venció. Vuelve a iniciar el acceso con Apple.",
                                         "Linking expired. Start Apple sign-in again."), true, false);
  }
#if JUCE_IOS
  // Keyboard notifications update JUCE's display insets without necessarily
  // resizing the fullscreen window. Relayout visible auth only when they change.
  if ((welcomeModal != nullptr && welcomeModal->isVisible()) ||
      (reauthPromptModal != nullptr && reauthPromptModal->isVisible())) {
    if (const auto *display = juce::Desktop::getInstance().getDisplays()
                                  .getDisplayForRect(getScreenBounds())) {
      if (lastAuthKeyboardInsets != display->keyboardInsets) {
        lastAuthKeyboardInsets = display->keyboardInsets;
        resized();
      }
    }
  }
#endif
  const auto callbackUrl = consumePendingAuthCallbackUrl();
  if (callbackUrl.isNotEmpty()) {
    completeBootstrapLogin(callbackUrl);
    if (!mainUiInitialized)
      return;
  }

  if (!mainUiInitialized) {
    return;
  }

  const auto nowMs = juce::Time::currentTimeMillis();
  if (demoLoadingOverlay) {
    auto &p = static_cast<MTControlsAudioProcessor &>(processor);
    if (p.isSongPlaying()) p.stopSong();
    return;
  }
  offerDemoWelcome();
  updateTeamsOnboarding();
  updateLearningGuideTarget();
  if (pendingExternalProjectFile != juce::File{} && !songLoadBusy &&
      songListPanelRaw != nullptr &&
      !songListPanelRaw->isProjectImportInProgress()) {
    const auto projectFile = pendingExternalProjectFile;
    pendingExternalProjectFile = juce::File{};

    maybePersistSongWindowForActiveSong();
    maybePersistClickDynMetronomeEnabledForActiveSong();
    if (auto *grid =
            dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
      grid->flushPendingPersistenceNow();
    if (hasCurrentLoadedSong && currentLoadedSong.projectPath.isNotEmpty())
      ProjectStore::flushPendingSettings(
          juce::File(currentLoadedSong.projectPath));
    ProjectStore::flushAllPendingSettings();

    // Opening a document from the OS is a load operation, never an autoplay
    // request. Stop before replacing the song so loadSongStems() cannot inherit
    // a playing transport state from the previous project.
    auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
    processorRef.stopSong();

    songListPanelRaw->openProjectFile(projectFile);
  }

  if (deferredStartupSongLoadPending && !songLoadBusy &&
      (songListPanelRaw == nullptr ||
       !songListPanelRaw->isProjectImportInProgress()) &&
      nowMs >= deferredStartupSongLoadAtMs) {
    const int index = deferredStartupSongIndex;
    deferredStartupSongLoadPending = false;
    deferredStartupSongIndex = -1;
    deferredStartupSongLoadAtMs = 0;

    if (songListPanelRaw != nullptr && loadSongByIndexAction &&
        juce::isPositiveAndBelow(index, songListPanelRaw->getSongCount())) {
      SessionAuthLogger::write(
          "timerCallback: running deferred startup song load index=" +
          juce::String(index));
      loadSongByIndexAction(index, false);
    }
  }

  if (nowMs >= nextAudioDeviceFallbackPollMs) {
    nextAudioDeviceFallbackPollMs = nowMs + kAudioDevicePollIntervalMs;
    const auto audioCtx = getStandaloneAudioContext(static_cast<MTControlsAudioProcessor &>(processor));
    if (audioCtx.manager != nullptr) {
      enforceStandaloneOutputOnlyAudio(*audioCtx.manager);
      auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
      const juce::String currentDeviceKey = getCurrentDeviceKey(audioCtx);
      if (auto *currentDevice = audioCtx.manager->getCurrentAudioDevice();
          currentDevice != nullptr) {
        processorRef.getAudioDeviceBridge().setAvailableOutputChannelCount(
            juce::jmax(1, currentDevice->getActiveOutputChannels().countNumberOfSetBits()));
        missingAudioDeviceSinceMs = 0;
        nextAudioDeviceFallbackAttemptMs = nowMs + kAudioDeviceMissingGraceMs;

        const auto activeOutputs = currentDevice->getActiveOutputChannels();
        const bool deviceChanged = currentDeviceKey != lastObservedAudioDeviceKey;
        const bool activeOutputsChanged =
            activeOutputs != lastObservedActiveOutputChannels;
        if (currentDeviceKey.isNotEmpty() &&
            (deviceChanged || activeOutputsChanged)) {
          playworship::telemetry::capture(
              playworship::telemetry::Incident::audioDeviceChanged);
          // A changed active-channel mask can alter routing while keeping the
          // same driver/device name, including a mono/stereo selection change.
          SongLoadProbe::instance().recordEvent(
              "routing", "device.change",
              "deviceKey=" + currentDeviceKey +
                  " prev=" + lastObservedAudioDeviceKey +
                  " activeOutputs=" + activeOutputs.toString(16) +
                  " previousActiveOutputs=" + lastObservedActiveOutputChannels.toString(16) +
                  " hasSong=" + juce::String(hasCurrentLoadedSong ? 1 : 0));
          lastObservedAudioDeviceKey = currentDeviceKey;
          lastObservedActiveOutputChannels = activeOutputs;
          processorRef.refreshPadsRoutingFromSettings();
          if (hasCurrentLoadedSong)
            applyRoutingDefaultsToSong(currentLoadedSong);
          if (auto *grid =
                  dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
            grid->refreshStemRoutingVisibility();
        }
      } else {
        lastObservedAudioDeviceKey.clear();
        lastObservedActiveOutputChannels.clear();
        if (missingAudioDeviceSinceMs <= 0)
          missingAudioDeviceSinceMs = nowMs;

        const bool missingLongEnough =
            (nowMs - missingAudioDeviceSinceMs) >= kAudioDeviceMissingGraceMs;
        if (missingLongEnough && nowMs >= nextAudioDeviceFallbackAttemptMs) {
          fallbackToBuiltInOrDefaultOutput(*audioCtx.manager);
          nextAudioDeviceFallbackAttemptMs = nowMs + kAudioDeviceFallbackRetryMs;
        }
      }
    }
  }

  evaluateReauthPolicy();
  if (!mainUiInitialized)
    return;

  processRemoteEvents();
  if (remoteSession.getMode() ==
          playworship::remote::RemoteSession::Mode::Server &&
      nowMs >= nextRemotePublishMs) {
    nextRemotePublishMs = nowMs + 200;
    publishRemoteState();
  }

  if (remoteSession.getMode() == playworship::remote::RemoteSession::Mode::Server)
    publishRemoteMeters();
  if (nowMs >= nextRemoteSettingsRefreshMs) {
    nextRemoteSettingsRefreshMs = nowMs + 500;
    refreshRemoteSettings();
  }
  if (playbackSession.isRemote()) {
    if (remoteVisualStatePending && waveformBarPtr && !waveformBarPtr->isMouseButtonDown(true)) applyRemoteSnapshot(playbackSession.snapshot());
    if (songListPanelRaw) songListPanelRaw->setNowPlayingSong(currentLoadedSong.id, playbackSession.isSongPlaying());
    updateSystemUsageMetrics();
    return;
  }
  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
  MTControlsAudioProcessor::MidiBinding learnedBinding;
  if (processorRef.consumePendingMidiLearnBinding(learnedBinding)) {
    // The audio callback only publishes the fixed-size learn result. Mutating
    // the binding vector and persisting settings belongs to the message thread.
    processorRef.setMidiBinding(learnedBinding);
    AppSettings::setMidiBindings(processorRef.getMidiBindings());
    midiLearnPendingTargetId = -1;
    if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
      bar->setMidiLearnPendingTarget(midiLearnPendingTargetId);
    if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
      grid->setMidiLearnPendingTarget(midiLearnPendingTargetId);
    if (auto *wave = dynamic_cast<WaveformBar *>(waveformBar.get()))
      wave->setMidiLearnPendingTarget(midiLearnPendingTargetId);
    if (padsComponent != nullptr)
      padsComponent->setMidiLearnPendingTarget(midiLearnPendingTargetId);
  }
  int midiUiCommand = 0;
  if (processorRef.consumePendingMidiUiCommand(midiUiCommand)) {
    switch (midiUiCommand) {
    case MTControlsAudioProcessor::midiUiCommandPrevSong:
      if (prevSongAction)
        prevSongAction();
      break;
    case MTControlsAudioProcessor::midiUiCommandNextSong:
      if (nextSongAction)
        nextSongAction();
      break;
    case MTControlsAudioProcessor::midiUiCommandToggleWindowMode:
      if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
        bar->triggerWindowModeToggleFromMidi();
      break;
    case MTControlsAudioProcessor::midiUiCommandToggleSettings:
      if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
        bar->triggerSettingsFromMidi();
      break;
    case MTControlsAudioProcessor::midiUiCommandShowSongWindowPopover:
      if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
        bar->triggerSongWindowPopoverFromMidi();
      break;
    case MTControlsAudioProcessor::midiUiCommandShowKeyPopover:
      if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
        bar->triggerKeyPopoverFromMidi();
      break;
    case MTControlsAudioProcessor::midiUiCommandShowMetronomePopover:
      if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
        bar->triggerMetronomePopoverFromMidi();
      break;
    case MTControlsAudioProcessor::midiUiCommandMixerNudgeUp:
      if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
        grid->triggerMidiNudgeUp();
      break;
    case MTControlsAudioProcessor::midiUiCommandMixerNudgeDown:
      if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
        grid->triggerMidiNudgeDown();
      break;
    case MTControlsAudioProcessor::midiUiCommandMixerApplyRouting:
      if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
        grid->triggerMidiApplyRouting();
      break;
    case MTControlsAudioProcessor::midiUiCommandWaveformEditToggle:
      if (auto *wave = dynamic_cast<WaveformBar *>(waveformBar.get()))
        wave->triggerEditToggleFromMidi();
      break;
    default:
      break;
    }
  }

  juce::String ignoredDynamicGuideWarning;
  processorRef.consumeDynamicGuideMissingSectionWarning(
      ignoredDynamicGuideWarning);

  maybePersistSongWindowForActiveSong();
  maybePersistClickDynMetronomeEnabledForActiveSong();
  updateSystemUsageMetrics();

  const bool isPlayingNow = processorRef.isSongPlaying();
  const double currentPosSec = processorRef.getSongCurrentPositionSeconds();

  if (songListPanelRaw != nullptr) {
    const bool hasPlayingSong = isPlayingNow && hasCurrentLoadedSong &&
                                currentLoadedSong.id.isNotEmpty();
    songListPanelRaw->setNowPlayingSong(
        hasPlayingSong ? currentLoadedSong.id : juce::String(), hasPlayingSong);
    if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get())) {
      grid->setLibraryNowPlayingSong(
          hasPlayingSong ? currentLoadedSong.id : juce::String(), hasPlayingSong);
    }
  }

  if (wasSongPlaying && !isPlayingNow &&
      songListPanelRaw != nullptr &&
      hasCurrentLoadedSong && currentLoadedSong.id.isNotEmpty()) {
    const double startSec = processorRef.getSongStartSeconds();
    const double endSec = processorRef.getSongEndSeconds();
    const bool reachedSongEnd =
        endSec > startSec && currentPosSec >= (endSec - 0.12);

    // Natural completion leaves the processor parked exactly at Fin. A manual
    // stop seeks back to Inicio, so checking the current cursor distinguishes
    // both cases without requiring an impossible wrap-to-start transition.
    if (reachedSongEnd && !(learningGuide && learningLessonId == "demo-song")) {
      // The browsing workspace is a filtered view. Continue the explicitly
      // active setlist even while the musician browses another team's library.
      const auto next = songListPanelRaw->getAutoPlayNextSong(currentLoadedSong.id);
      if (next && loadSongAction) {
        const int visibleIndex = songListPanelRaw->getSongIndexById(next->id);
        if (loadSongAction(*next, visibleIndex)) {
          if (visibleIndex >= 0) songListPanelRaw->setSelectedSongIndex(visibleIndex);
          processorRef.playSong();
        }
      }
    }
  }

  wasSongPlaying = isPlayingNow;
}

MTControlsAudioProcessorEditor::~MTControlsAudioProcessorEditor() {
  appleLinkFlow.cancel();
  appleLinkCandidate = {};
  appleLinkDialog.close();
  ++authAttemptGeneration;
  ++appleConfigurationGeneration;
  ++appleCredentialGeneration;
  appleCredentialMonitor.reset();
  NativeAppleSignIn::cancel();
  NativeAuthSession::cancel();
  stopTimer();
  if (!playbackSession.isRemote())
    static_cast<MTControlsAudioProcessor &>(processor).finishSectionAutomationEditing(false);
  closePractice();
  static_cast<MTControlsAudioProcessor &>(processor)
      .setPracticeListeningMode(-1, 0);
  if (phonePadsPopoverContent != nullptr)
    phonePadsPopoverContent->prepareForEditorDestruction();
  remoteSession.stop();
  if (remoteClientAudioMuted) {
    auto &audioProcessor = static_cast<MTControlsAudioProcessor &>(processor);
    audioProcessor.setRemoteClientMode(false);
    audioProcessor.setPadsEnabled(remotePreviousPadsEnabled);
    audioProcessor.setMetronomeEnabled(remotePreviousMetronomeEnabled);
  }
  diagnosticShareSheet.close();
  mixExportShareSheet.close();
  if (pendingSharedMixExport.existsAsFile())
    pendingSharedMixExport.deleteFile();
  closeSettings();
  maybePersistSongWindowForActiveSong();
  maybePersistClickDynMetronomeEnabledForActiveSong();
  if (auto *channelGridRaw =
          dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
    channelGridRaw->flushPendingPersistenceNow();
  if (hasCurrentLoadedSong && currentLoadedSong.projectPath.isNotEmpty())
    ProjectStore::flushPendingSettings(juce::File(currentLoadedSong.projectPath));
  ProjectStore::flushAllPendingSettings();
  if (localAuthServer)
    localAuthServer->stop();
  localAuthServer.reset();
  settingsPanelRaw = nullptr;
  loadSongByIndexAction = {};
  songListPanelRaw = nullptr;
  auto &proc = static_cast<MTControlsAudioProcessor &>(processor);
  proc.getAudioDeviceBridge().setOnDeviceConnected({});
  proc.getAudioDeviceBridge().setOnDeviceDisconnected({});
}

void MTControlsAudioProcessorEditor::paint(juce::Graphics &g) {
#if JUCE_ANDROID
  // The Android peer software-paints the editor on every playback frame.
  // The Android peer software-paints the editor on every playback frame.
  // Skipping the tiled-noise studio background there is enough to remove
  // the dominant cost without changing the visual output (the opaque
  // WaveformBar and ChannelGridPanel cover their own area on top of it).
  const auto androidBounds = getLocalBounds().toFloat();
  g.fillAll(UITheme::appBgBottom);
#else
  drawStudioBackground(g, getLocalBounds().toFloat());
#endif

  if (mainUiInitialized && !padsDrawerBackgroundBounds.isEmpty()) {
    const auto drawer = padsDrawerBackgroundBounds.toFloat().reduced(0.5f);
    g.setColour(UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.14f)
                    .withAlpha(0.92f));
    g.fillRoundedRectangle(drawer, 8.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.52f));
    g.drawRoundedRectangle(drawer, 8.0f, 1.0f);
  }
}

bool MTControlsAudioProcessorEditor::shouldCollapseWaveformArea() const {
  return hasCurrentLoadedSong && currentLoadedSong.blankSong &&
         currentLoadedSong.stems.empty();
}

void MTControlsAudioProcessorEditor::refreshWaveformPresentationForSong() {
  if (waveformBarPtr == nullptr)
    return;
  const bool collapsed = shouldCollapseWaveformArea();
  const bool changed = waveformBarPtr->isBlankSong() != collapsed;
  waveformBarPtr->setBlankSong(collapsed);
  if (changed) {
    if (topBar != nullptr)
      topBar->resized();
    resized();
  }
}

void MTControlsAudioProcessorEditor::resized() {
  if (demoLoadingOverlay) demoLoadingOverlay->setBounds(getLocalBounds());
  // Authentication also owns the phone orientation before the main UI exists.
  updatePhoneOrientation();
  if (!mainUiInitialized) {
    if (welcomeModal != nullptr) {
      auto welcomeBounds = getLocalBounds();
      if (useTouchOptimisedControls()) {
        if (const auto* display = juce::Desktop::getInstance().getDisplays()
                                      .getDisplayForRect(getScreenBounds())) {
          auto insets = display->safeAreaInsets;
          insets.setBottom(juce::jmax(insets.getBottom(), display->keyboardInsets.getBottom()));
          welcomeBounds = insets.subtractedFrom(welcomeBounds);
        }
      }
      welcomeModal->setBounds(welcomeBounds);
    }
    if (settingsOverlay != nullptr)
      settingsOverlay->setBounds(getLocalBounds());
    if (practiceOverlay != nullptr)
      practiceOverlay->setBounds(getLocalBounds());
    return;
  }

  updateResponsiveLayoutProfile();
  // Phone behaviour is determined by the available short side, not by the
  // user-selectable desktop density profile. A wide landscape phone can use
  // the Standard profile while still needing the phone-safe surface.
  layoutMainUi(isPhoneSizedLayout(*this));
}

void MTControlsAudioProcessorEditor::layoutMainUi(const bool minimalPhoneProfile) {
  auto area = getLocalBounds();
  if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
    bar->setMinimalPhoneMode(minimalPhoneProfile);
  if (auto *wave = dynamic_cast<WaveformBar *>(waveformBar.get()))
    wave->setMinimalPhoneMode(minimalPhoneProfile);
  if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
    grid->setMinimalPhoneMode(minimalPhoneProfile);
  int phoneBottomBleed = 0;
  juce::BorderSize<int> phoneSafeInsets;
  juce::BorderSize<int> phoneHorizontalInsets;
#if JUCE_IOS || JUCE_ANDROID
  if (minimalPhoneProfile) {
    if (const auto *display =
            juce::Desktop::getInstance().getDisplays().getDisplayForRect(
                getScreenBounds())) {
      const auto safeInsets = display->safeAreaInsets;
      phoneSafeInsets = safeInsets;
      phoneBottomBleed = safeInsets.getBottom();
      // Visual surfaces and the scrolling mixer reach the screen edges.
      // Each panel protects its controls using the actual inset on each side.
      phoneHorizontalInsets = {0, safeInsets.getLeft(), 0, safeInsets.getRight()};
      area = juce::BorderSize<int>(safeInsets.getTop(), 0,
                                   safeInsets.getBottom(), 0).subtractedFrom(area);
    }
  }
#if JUCE_IOS
  else if (const auto *display =
               juce::Desktop::getInstance().getDisplays().getDisplayForRect(
                   getScreenBounds())) {
    // Tablet controls stay inside the status bar and home-indicator safe area.
    area = display->safeAreaInsets.subtractedFrom(area);
  }
#endif
#endif
#if JUCE_IOS
  const auto tabletSafeContentBounds = area;
#endif
  // Phone mixer bounds already end at the platform safe area. Applying that
  // inset again inside every strip lifts the title and S/M row away from the
  // visible bottom edge.
  auto phoneTopBarInsets = phoneHorizontalInsets;
#if JUCE_ANDROID
  // Paint behind a visible status bar while keeping fixed controls below it.
  phoneTopBarInsets.setTop(phoneSafeInsets.getTop());
#endif
  if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
    bar->setPhoneSafeAreaInsets(phoneTopBarInsets);
  if (auto *wave = dynamic_cast<WaveformBar *>(waveformBar.get()))
    wave->setPhoneSafeAreaInsets(phoneHorizontalInsets);
  if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get())) {
    grid->setPhoneBottomContentInset(0);
    grid->setPhoneHorizontalContentInsets(phoneHorizontalInsets.getLeft(),
                                          phoneHorizontalInsets.getRight());
  }
  const bool setlistDrawerSupported = supportsSetlistDrawer(*this);
  const bool setlistOverlaySupported =
      setlistDrawerSupported || minimalPhoneProfile;
  if (!setlistOverlaySupported)
    isSetlistDrawerOpen = false;

  if (topBar == nullptr || waveformBar == nullptr || channelGridPanel == nullptr ||
      songListPanel == nullptr || setlistHandlePanel == nullptr)
    return;

  if (teamsOverlay != nullptr)
    teamsOverlay->setBounds(getLocalBounds());
  if (settingsOverlay != nullptr)
    settingsOverlay->setBounds(getLocalBounds());
  if (practiceOverlay != nullptr)
    practiceOverlay->setBounds(getLocalBounds());

  int topBarH = kDefaultTopBarH;
  int waveformH = kDefaultWaveformH;
  int waveformExpandedH = kDefaultWaveformExpandedH;
  float setlistRatio = 0.30f;
  int setlistMinW = 388;
  int setlistMaxW = 430;
  int contentInset = 8;

  if (responsiveLayoutProfile == ResponsiveLayoutProfile::Compact) {
    topBarH = 54;
    waveformH = kCompactWaveformH;
    waveformExpandedH = kCompactWaveformExpandedH;
    setlistRatio = 0.31f;
    setlistMinW = 300;
    setlistMaxW = 360;
    contentInset = 6;
  } else if (responsiveLayoutProfile == ResponsiveLayoutProfile::Large) {
    topBarH = 62;
    waveformH = kLargeWaveformH;
    waveformExpandedH = kDefaultWaveformExpandedH;
    setlistRatio = 0.28f;
    setlistMinW = 420;
    setlistMaxW = 460;
    contentInset = 9;
  }

  if (minimalPhoneProfile) {
    const int phoneHeight = area.getHeight();
    if (phoneHeight <= 375) {
      topBarH = 58;
      waveformH = 108;
      waveformExpandedH = waveformH;
      contentInset = 4;
    } else if (phoneHeight <= 410) {
      topBarH = 64;
      waveformH = 124;
      waveformExpandedH = waveformH;
      contentInset = 5;
    } else {
      topBarH = 70;
      waveformH = 140;
      waveformExpandedH = waveformH;
      contentInset = 6;
    }
#if JUCE_ANDROID
    // Give phone faders more travel while keeping the waveform usable.
    waveformH = juce::jmin(waveformH, juce::roundToInt(phoneHeight * 0.24f));
    waveformExpandedH = waveformH;
#endif
  } else if (useTouchOptimisedControls()) {
#if JUCE_ANDROID
    // Keep Android's touch targets comfortable without consuming as much
    // vertical space as the tablet profile used by iOS.
    topBarH = 60;
#else
    topBarH = 78;
#endif
  }

  auto topBarArea = area.removeFromTop(topBarH);
#if JUCE_ANDROID
  if (minimalPhoneProfile)
    topBarArea.setTop(0);
#endif
  topBar->setBounds(topBarArea);
  int waveH = isWaveformHeightExpanded ? waveformExpandedH : waveformH;
  // When the arrangement editor is open, the WaveformBar draws a parts toolbar
  // on top. Grow the waveform area by that amount so the toolbar doesn't
  // compress the waveform+minimap, and so the mixer / song list below get
  // pushed down instead of being covered by the expanded bar.
  if (waveformBarPtr)
    waveH += waveformBarPtr->getExtraHeightForHostLayout();
#if !JUCE_IOS && !JUCE_ANDROID
  const bool showLyrics = lyricsViewActive && !playbackSession.isRemote() &&
                          hasCurrentLoadedSong && !currentLoadedSong.blankSong;
  if (showLyrics) waveH = juce::jmax(waveH, juce::jmin(400, area.getHeight() - 180));
#endif
  const bool collapsed = shouldCollapseWaveformArea();
  if (collapsed)
    waveH = 0;
  auto waveformArea = area.removeFromTop(waveH);
  waveformBar->setBounds(waveformArea);
  waveformBar->setVisible(!collapsed && topBar->isVisible());
#if !JUCE_IOS && !JUCE_ANDROID
  if (lyricsPanel) {
    lyricsPanel->setBounds(waveformArea);
    lyricsPanel->setVisible(showLyrics && !collapsed && topBar->isVisible());
    if (showLyrics) waveformBar->setVisible(false);
  }
#endif

  auto mainArea = area;
  const int handleW = minimalPhoneProfile ? 0 : 14;
  int setlistWidth =
      juce::jlimit(setlistMinW, setlistMaxW,
                   static_cast<int>(mainArea.getWidth() * setlistRatio));
  if (minimalPhoneProfile || isSetlistCollapsed ||
      (setlistOverlaySupported && isSetlistDrawerOpen))
    setlistWidth = 0;

  auto leftSetlistArea = mainArea.removeFromLeft(setlistWidth);
  auto handleArea = mainArea.removeFromLeft(handleW);
  auto rightChannelsArea = mainArea;
#if JUCE_IOS
  // Let the channel backgrounds and their title bands cover the lower safe
  // area. The strips themselves use no internal footer inset, so the footer
  // remains attached to the visual bottom instead of leaving an empty band.
  if (minimalPhoneProfile && phoneBottomBleed > 0)
    rightChannelsArea.setBottom(
        juce::jmin(getHeight(), rightChannelsArea.getBottom() + phoneBottomBleed));
#endif
  if (!minimalPhoneProfile) {
    rightChannelsArea.removeFromLeft(2);
    rightChannelsArea.removeFromRight(contentInset);
  }
  // En teléfonos el mixer debe aprovechar hasta el borde inferior seguro.
  // En tablet/escritorio conservamos el margen visual del layout tradicional.
  if (!minimalPhoneProfile)
    rightChannelsArea.removeFromBottom(contentInset);
  padsDrawerBackgroundBounds = {};

  if (padsHandlePanel && !minimalPhoneProfile) {
    int handleH = 16;
    int padsW = 584; // base width for standard profile
    if (responsiveLayoutProfile == ResponsiveLayoutProfile::Compact) {
      handleH = 14;
      padsW = 518;
    } else if (responsiveLayoutProfile == ResponsiveLayoutProfile::Large) {
      handleH = 18;
      padsW = 640;
    }

    const bool compact = responsiveLayoutProfile == ResponsiveLayoutProfile::Compact;
    const bool large = responsiveLayoutProfile == ResponsiveLayoutProfile::Large;
    const int boundedPadsW = juce::jmin(padsW, rightChannelsArea.getWidth());

    // Match panel height to real Pads content to avoid excess vertical space.
    constexpr int kCols = 6;
    constexpr int kRows = 2;
    const int basePadSize = compact ? 62 : (large ? 78 : 70);
    const int gridInset = compact ? 12 : (large ? 18 : 16);
    const int stripW = compact ? 78 : (large ? 98 : 90);
    const int stripRightInset = compact ? 6 : 8;
    const int stripLeftGap = compact ? 8 : (large ? 12 : 10);
    const int minGapX = compact ? 6 : (large ? 10 : 8);
    const int targetGapY = compact ? 6 : (large ? 10 : 8);
    const int bottomRowHeight = compact ? 56 : (large ? 68 : 64);
    const int bottomRowGap = compact ? 2 : (large ? 4 : 3);

    const int padsUsableWidth = juce::jmax(
        120, boundedPadsW - (2 * gridInset) - stripRightInset - stripW - stripLeftGap);
    const int maxPadSizeForWidth = juce::jmax(
        36, (padsUsableWidth - (minGapX * (kCols - 1))) / kCols);
    const int padSize = juce::jmin(basePadSize, maxPadSizeForWidth);
    const int padsGridHeight = (kRows * padSize) + ((kRows - 1) * targetGapY);
    const int extraContainerAir = compact ? 10 : (large ? 16 : 14);
    const int padsH = juce::jmax(
        190, (2 * gridInset) + padsGridHeight + bottomRowGap + bottomRowHeight +
                 extraContainerAir);

    const int totalPadsH = isPadsCollapsed ? handleH : handleH + padsH;
    auto padsBar = rightChannelsArea.removeFromBottom(totalPadsH);

    padsHandlePanel->setBounds(padsBar.removeFromTop(handleH));

    if (!isPadsCollapsed && padsComponent) {
      padsDrawerBackgroundBounds = padsBar;
      padsComponent->setBounds(padsBar.withWidth(boundedPadsW));
    }
  }

  channelGridPanel->setBounds(rightChannelsArea);

  if (songListPanelRaw != nullptr) {
    songListPanelRaw->setMinimalPhoneMode(minimalPhoneProfile);
    songListPanelRaw->setPhoneSafeAreaInsets(phoneSafeInsets);
  }

  if (minimalPhoneProfile) {
    // Keep the mixer aligned to the waveform. The setlist is exposed through
    // stable TopBar actions instead of a persistent side column.
    // Keyboard/safe-area updates relayout the editor while a library field
    // owns focus. Hiding its parent here, even until the drawer branch below,
    // makes JUCE drop that focus and dismiss the Android input connection.
    // Keep an open drawer attached and visible throughout the entire reflow.
    if (!isSetlistDrawerOpen) {
      songListPanel->setVisible(false);
      songListPanel->setBounds({});
      if (setlistDrawerScrim)
        setlistDrawerScrim->setVisible(false);
    }
    setlistHandlePanel->setVisible(false);
    if (auto *phoneTopBar = dynamic_cast<TopBar *>(topBar.get()))
      phoneTopBar->setPhoneOverlayStates(isSetlistDrawerOpen, phonePadsOpen);
    if (padsHandlePanel)
      padsHandlePanel->setVisible(false);
    if (padsComponent && padsComponent->getParentComponent() == this)
      padsComponent->setVisible(false);
  } else {
    if (auto *desktopTopBar = dynamic_cast<TopBar *>(topBar.get()))
      desktopTopBar->setPhoneOverlayStates(false, false);
    setlistHandlePanel->setPhoneOverlayMode(false);
    setlistHandlePanel->setVisible(true);
    if (padsHandlePanel)
      padsHandlePanel->setVisible(true);
    if (padsComponent && padsComponent->getParentComponent() == this)
      padsComponent->setVisible(!isPadsCollapsed);
  }

  if (setlistOverlaySupported && isSetlistDrawerOpen) {
    juce::Rectangle<int> drawerBounds;
    if (minimalPhoneProfile) {
      // The library/setlist page paints edge to edge like a native full-screen
      // surface. SongListPanel applies the safe insets to its controls.
      drawerBounds = getLocalBounds();
    } else {
#if JUCE_IOS
      const auto drawerHostBounds = tabletSafeContentBounds;
      const int drawerTop = topBarArea.getBottom() + contentInset;
#else
      const auto drawerHostBounds = getLocalBounds();
      const int drawerTop = topBarH + contentInset;
#endif
      const int drawerWidth = juce::jlimit(
          useTouchOptimisedControls() ? 360 : 340,
          useTouchOptimisedControls() ? 560 : 460,
          static_cast<int>(drawerHostBounds.getWidth() *
                           (useTouchOptimisedControls() ? 0.46f : 0.38f)));
      drawerBounds = juce::Rectangle<int>(
          drawerHostBounds.getX() + contentInset, drawerTop,
          juce::jmin(drawerWidth, drawerHostBounds.getWidth() - (2 * contentInset)),
          juce::jmax(1, drawerHostBounds.getBottom() - drawerTop - contentInset));
    }

    songListPanel->setVisible(true);
    songListPanel->setBounds(drawerBounds);
    auto scrimBounds = minimalPhoneProfile
        ? getLocalBounds()
        : juce::Rectangle<int>(0, topBarH, getWidth(),
                               juce::jmax(1, getHeight() - topBarH));
#if JUCE_IOS
    if (!minimalPhoneProfile)
      scrimBounds = juce::Rectangle<int>(
          tabletSafeContentBounds.getX(), topBarArea.getBottom(),
          tabletSafeContentBounds.getWidth(),
          juce::jmax(1, tabletSafeContentBounds.getBottom() - topBarArea.getBottom()));
#endif
    setlistDrawerScrim->setBounds(scrimBounds);
    setlistDrawerScrim->setFullScreen(minimalPhoneProfile);
    setlistDrawerScrim->setDrawerBounds(
        drawerBounds.translated(-scrimBounds.getX(), -scrimBounds.getY()));
    setlistDrawerScrim->setVisible(true);
    setlistDrawerScrim->toFront(false);
    songListPanel->toFront(false);

    if (!minimalPhoneProfile) {
      setlistHandlePanel->setBounds(
          juce::Rectangle<int>(drawerBounds.getRight(), drawerBounds.getY() + 8,
                               handleW, juce::jmax(48, drawerBounds.getHeight() - 16)));
      setlistHandlePanel->toFront(false);
    }
  } else if (isSetlistCollapsed) {
    songListPanel->setVisible(false);
    songListPanel->setBounds({});
    setlistDrawerScrim->setVisible(false);
  } else if (!minimalPhoneProfile) {
    songListPanel->setVisible(true);
    auto setlistBounds = leftSetlistArea;
    setlistBounds.removeFromLeft(contentInset);
    setlistBounds.removeFromBottom(contentInset);
    setlistBounds.removeFromRight(3);
    songListPanel->setBounds(setlistBounds);
    setlistDrawerScrim->setVisible(false);
    setlistHandlePanel->setBounds(handleArea.reduced(0, 8));
  }

  if (!(setlistOverlaySupported && isSetlistDrawerOpen) && !minimalPhoneProfile)
    setlistHandlePanel->setBounds(handleArea.reduced(0, 8));
  setlistHandlePanel->setCollapsed(isSetlistCollapsed);

  // Keyboard/safe-area changes bring the open library drawer to the front.
  // Its song editor is a host sibling and must stay above that drawer without
  // taking focus from the field that caused the relayout.
  if (songListPanelRaw != nullptr)
    songListPanelRaw->bringSongEditorOverlayToFront();

  if (reauthPromptModal != nullptr) {
    const auto *prompt = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get());
    auto authBounds = getLocalBounds();
    if (useTouchOptimisedControls()) {
      if (const auto* display = juce::Desktop::getInstance().getDisplays()
                                    .getDisplayForRect(getScreenBounds())) {
        auto insets = display->safeAreaInsets;
        insets.setBottom(juce::jmax(insets.getBottom(), display->keyboardInsets.getBottom()));
        authBounds = insets.subtractedFrom(authBounds);
      }
    }
    authBounds.reduce(16, 12);
    reauthPromptModal->setBounds(
        juce::Rectangle<int>(juce::jmin(560, juce::jmax(1, authBounds.getWidth())),
                             juce::jmin(juce::jmax(1, authBounds.getHeight()),
                                        prompt ? prompt->preferredHeight() : 230))
            .withCentre(authBounds.getCentre()));
    reauthPromptModal->toFront(false);
  }

  if (practiceOverlay != nullptr)
    practiceOverlay->toFront(false);

  if (learningGuide != nullptr) {
    updateLearningGuideTarget();
  }


}

void MTControlsAudioProcessorEditor::beginExportCurrentSongMix(
    const juce::String &formatExtension) {
#if MTCONTROLS_MAC_APP_STORE
  if (formatExtension == "mp3" || formatExtension == "m4a") {
    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
        L10n::tr("Exportar mezcla", "Export mix"),
        L10n::tr("Esta versión exporta la mezcla en WAV.", "This edition exports mixes in WAV."));
    return;
  }
#endif
  if (playbackSession.isRemote()) return;
  if (mixExportBusy || songLoadBusy)
    return;

  auto &p = static_cast<MTControlsAudioProcessor &>(processor);
  if (!hasCurrentLoadedSong || !p.hasLoadedSong()) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon, "Exportar mix",
        "Carga una canción antes de exportar el mix.");
    return;
  }

  auto title = currentLoadedSong.title.trim();
  if (title.isEmpty())
    title = "PlayWorship Mix";
  title = title.replaceCharacters("\\/:*?\"<>|", "---------");
  auto folder = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
  if (!folder.exists())
    folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
  const auto extension = formatExtension == "mp3" || formatExtension == "m4a"
                             ? formatExtension
                             : juce::String("wav");
#if JUCE_IOS || JUCE_ANDROID
  // Mobile document pickers return provider URLs rather than ordinary writable
  // file paths. Render inside the app first, then let the native share sheet
  // copy the completed file to Files, Drive, AirDrop, or another destination.
  // MP3/M4A currently use the desktop FFmpeg helper, which cannot run as a
  // child process on iOS/Android, so the mobile UI only offers WAV.
  auto exportFolder =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("PlayWorship")
          .getChildFile("Exports");
  if (exportFolder.createDirectory().failed()) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon, "Error al exportar",
        "No se pudo preparar la carpeta temporal de exportación.");
    return;
  }
  const auto timestamp =
      juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
  runSongMixExport(
      exportFolder.getChildFile(title + " Bounce-" + timestamp + ".wav"),
      true);
  juce::ignoreUnused(extension);
#else
  mixExportFileChooser = std::make_unique<juce::FileChooser>(
      "Guardar bounce como...",
      folder.getChildFile(title + " Bounce." + extension), "*." + extension);
  mixExportFileChooser->launchAsync(
      juce::FileBrowserComponent::saveMode |
          juce::FileBrowserComponent::warnAboutOverwriting,
      [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this),
       extension](
          const juce::FileChooser &chooser) {
        if (safe == nullptr)
          return;
        auto file = chooser.getResult();
        if (file == juce::File())
          return;
        safe->runSongMixExport(file.hasFileExtension(extension)
                                    ? file
                                    : file.withFileExtension(extension));
      });
#endif
}

void MTControlsAudioProcessorEditor::runSongMixExport(
    const juce::File &outputFile, const bool shareWhenComplete) {
  if (mixExportBusy || !hasCurrentLoadedSong)
    return;
  auto &p = static_cast<MTControlsAudioProcessor &>(processor);
  if (!p.hasLoadedSong())
    return;

  if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
    grid->flushPendingPersistenceNow();

  SongMixExportRequest request;
  request.outputFile = outputFile;
  if (outputFile.hasFileExtension("mp3"))
    request.format = SongMixExportRequest::Format::Mp3;
  else if (outputFile.hasFileExtension("m4a"))
    request.format = SongMixExportRequest::Format::M4a;
  request.sampleRate = p.getSampleRate() > 0.0 ? p.getSampleRate() : 44100.0;
  request.startSeconds = p.getSongStartSeconds();
  request.endSeconds = p.getSongEndSeconds();
  request.fadeOutSeconds = p.getSongFadeOutSeconds();
  request.pitchSemiTones = p.getPitchSemiTones();
  request.tempoRatio = p.getOriginalSongBpm() > 0.0
                           ? p.getPlaybackTempoBpm() / p.getOriginalSongBpm()
                           : 1.0;
  request.playbackBpm = p.getMetronomeBpm();
  request.masterVolume = p.getMasterVolume();
  request.masterGain = p.getMasterGain();
  request.masterMuted = p.isMasterMuted();
  request.arrangement = p.getSongArrangementSnapshot();
  request.dynamicClick.enabled =
      p.isMetronomeEnabled() && p.isClickDivisionEnabled();
  request.dynamicClick.volume = p.getMetronomeVolume();
  request.dynamicClick.gain = p.getMetronomeGain();
  request.dynamicClick.muted = p.isMetronomeMuted();
  request.dynamicClick.soloed = p.isMetronomeSolo();
  request.clickDivisionMask = p.getClickDivision();
  request.beatsPerBar =
      timeSignatureBeatsPerBar(currentLoadedSong.timeSignature);
  switch (AppSettings::getMetronomeClickStyle()) {
  case MetronomeClickStyle::Woodblock: request.clickStyle = "Woodblock"; break;
  case MetronomeClickStyle::Cowbell: request.clickStyle = "Cowbell"; break;
  case MetronomeClickStyle::Digital: request.clickStyle = "Digital"; break;
  case MetronomeClickStyle::Gentle: request.clickStyle = "Gentle"; break;
  case MetronomeClickStyle::Percussive: request.clickStyle = "Percussive"; break;
  case MetronomeClickStyle::Saw: request.clickStyle = "Saw"; break;
  case MetronomeClickStyle::Blip: request.clickStyle = "Blip"; break;
  case MetronomeClickStyle::Classic: request.clickStyle = "Classic"; break;
  }

  const auto guideSettings = p.getDynamicGuideSettings();
  request.dynamicGuide.enabled = guideSettings.enabled;
  request.dynamicGuide.volume = p.getDynamicGuideVolume();
  request.dynamicGuide.gain = p.getDynamicGuideGain();
  request.dynamicGuide.muted = p.isDynamicGuideMuted();
  request.dynamicGuide.soloed = p.isDynamicGuideSolo();
  request.guideLanguage =
      AppSettings::getLanguage() == AppLanguage::English ? "en" : "es";
  for (const auto &part : request.arrangement.parts) {
    if (!part.skipPlayback)
      request.guideSections.push_back(
          {part.name, part.startSec, part.utilityCue});
  }
  for (size_t i = 0; i < currentLoadedSong.stems.size(); ++i) {
    const auto &stem = currentLoadedSong.stems[i];
    if (!stem.include) continue;
    juce::File file(stem.storedPath);
    if (!file.existsAsFile())
      file = juce::File(stem.sourcePath);
    if (!file.existsAsFile())
      continue;
    request.stems.push_back({file.getFullPathName(), p.getStemBaseVolume((int) i),
                             p.getStemBaseGain((int) i), p.isStemBaseMuted((int) i),
                             p.isStemSoloed((int) i),
                             stemPriorityForStripOrder(stem) <= 1,
                             isStemPitchShiftable(stem), stem.stemId});
  }
  if (request.stems.empty()) {
    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                           "Exportar mix",
                                           "No se encontraron pistas de audio para exportar.");
    return;
  }

  mixExportBusy = true;
  if (waveformBarPtr != nullptr)
    waveformBarPtr->setExportBusy(true);
  if (waveformBarPtr != nullptr)
    waveformBarPtr->setSongLoadingState(true, 0.0, "Preparando exportación…");

  std::thread([safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this),
               request, shareWhenComplete] {
    const auto formatLabel = request.format == SongMixExportRequest::Format::Mp3
                                 ? juce::String("MP3")
                                 : (request.format == SongMixExportRequest::Format::M4a
                                        ? juce::String("M4A")
                                        : juce::String("WAV"));
    const auto result = SongMixExporter::exportToWav(
        request, [safe, formatLabel](const double progress) {
          juce::MessageManager::callAsync([safe, progress, formatLabel] {
            if (safe != nullptr && safe->waveformBarPtr != nullptr)
              safe->waveformBarPtr->setSongLoadingState(
                  true, progress,
                  (progress >= 1.0 && formatLabel != "WAV"
                       ? "Codificando " + formatLabel + "…"
                       : "Exportando " + formatLabel + "… " +
                             juce::String((int) std::round(progress * 100.0)) + "%"));
          });
        });
    juce::MessageManager::callAsync([safe, result, shareWhenComplete] {
      if (safe == nullptr)
        return;
      safe->mixExportBusy = false;
      if (safe->waveformBarPtr != nullptr)
        safe->waveformBarPtr->setExportBusy(false);
      if (safe->waveformBarPtr != nullptr)
        safe->waveformBarPtr->clearSongLoadingState();
      if (result.ok && shareWhenComplete) {
        safe->presentExportShareSheet(result.outputFile);
        return;
      }
      juce::NativeMessageBox::showMessageBoxAsync(
          result.ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
          result.ok ? "Mix exportado" : "Error al exportar",
          result.ok ? "Archivo guardado en:\n" + result.outputFile.getFullPathName()
                    : (result.error.isNotEmpty() ? result.error : "No se pudo crear el archivo de audio."));
    });
  }).detach();
}

void MTControlsAudioProcessorEditor::shareAudioDiagnostics() {
  if (diagnosticExportBusy) return;
  diagnosticExportBusy = true;
  if (settingsPanelRaw != nullptr) settingsPanelRaw->setDiagnosticsBusy(true);
  auto& proc = static_cast<MTControlsAudioProcessor&>(processor);
  const auto files = proc.getAudioDiagnosticReportFiles();
  auto* details = new juce::DynamicObject();
  details->setProperty("accountUid", activeSession.uid);
  details->setProperty("appVersion", playworship::appVersion);
  details->setProperty("compiledAt", juce::String(__DATE__) + " " + __TIME__);
  details->setProperty("os", juce::SystemStats::getOperatingSystemName());
  details->setProperty("deviceModel", juce::SystemStats::getDeviceDescription());
  details->setProperty("cpuCores", juce::SystemStats::getNumCpus());
  details->setProperty("memoryMB", juce::SystemStats::getMemorySizeInMegabytes());
  details->setProperty("is64Bit", juce::SystemStats::isOperatingSystem64Bit());
  const auto context = getStandaloneAudioContext(static_cast<MTControlsAudioProcessor &>(processor));
  if (context.manager != nullptr)
    details->setProperty("audioBackend", context.manager->getCurrentAudioDeviceType());
  const juce::var metadata(details);
  const auto endMs = static_cast<double>(juce::Time::currentTimeMillis());
  const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("PlayWorship-diagnostics");
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  std::thread([safe, files, metadata, endMs, directory] {
    const auto result = PlaybackDiagnosticReport::create(files, metadata, endMs, directory);
    juce::MessageManager::callAsync([safe, result] {
      if (safe == nullptr) return;
      const auto finish = [safe] {
        if (safe == nullptr) return;
        safe->diagnosticExportBusy = false;
        if (safe->settingsPanelRaw != nullptr) safe->settingsPanelRaw->setDiagnosticsBusy(false);
      };
      if (result.error.isNotEmpty()) {
        finish();
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            L10n::tr("Diagnóstico", "Diagnostics"),
            L10n::tr("No se pudo generar el informe. Reproduce una canción e inténtalo de nuevo.\n", "Could not generate the report. Play a song and try again.\n") + result.error);
        return;
      }
#if JUCE_IOS || JUCE_ANDROID
      safe->diagnosticShareSheet = juce::ContentSharer::shareFilesScoped(
          {juce::URL(result.file)}, [safe, finish](bool, const juce::String& error) {
            finish();
            if (safe != nullptr && error.isNotEmpty())
              juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                  L10n::tr("No se pudo compartir", "Could not share"), error);
          }, safe.getComponent());
#else
      safe->diagnosticFileChooser = std::make_unique<juce::FileChooser>(
          L10n::tr("Guardar diagnóstico para compartir", "Save diagnostics to share"),
          juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(result.file.getFileName()), "*.zip");
      safe->diagnosticFileChooser->launchAsync(
          juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
              juce::FileBrowserComponent::warnAboutOverwriting,
          [safe, finish, file = result.file](const juce::FileChooser& chooser) {
            const auto destination = chooser.getResult();
            if (destination == juce::File{}) { finish(); return; }
            std::thread([safe, finish, file, destination] {
              const bool ok = file.copyFileTo(destination);
              juce::MessageManager::callAsync([safe, finish, ok, destination] {
                finish();
                if (safe == nullptr) return;
                juce::NativeMessageBox::showMessageBoxAsync(ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                    L10n::tr("Diagnóstico", "Diagnostics"),
                    ok ? L10n::tr("Listo para adjuntar al chat:\n", "Ready to attach to your chat:\n") + destination.getFullPathName()
                       : L10n::tr("No se pudo guardar el archivo.", "Could not save the file."));
              });
            }).detach();
          });
#endif
    });
  }).detach();
}

void MTControlsAudioProcessorEditor::presentExportShareSheet(
    const juce::File &outputFile) {
  if (!outputFile.existsAsFile()) {
    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon, "Error al exportar",
        "El archivo exportado no está disponible para compartir.");
    return;
  }

  pendingSharedMixExport = outputFile;
  const auto safe =
      juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  mixExportShareSheet = juce::ContentSharer::shareFilesScoped(
      {juce::URL(outputFile)},
      [safe, outputFile](const bool, const juce::String &error) {
        outputFile.deleteFile();
        if (safe == nullptr)
          return;
        safe->pendingSharedMixExport = juce::File{};
        if (error.isNotEmpty())
          juce::NativeMessageBox::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon, "Error al compartir",
              error);
      },
      this);
}

bool MTControlsAudioProcessorEditor::keyPressed(const juce::KeyPress &key) {
  if (!mainUiInitialized)
    return true;
  if (learningGuide && learningGuide->isVisible() && key == juce::KeyPress::escapeKey) {
    pauseLearningGuide();
    return true;
  }
  if (practiceOverlay != nullptr) {
    practiceOverlay->keyPressed(key);
    return true;
  }
  if (settingsOverlay != nullptr) {
    if (key == juce::KeyPress::escapeKey)
      navigateBackInSettings();
    return true;
  }
  auto &processorRef = playbackSession;
  const auto keyCode = key.getKeyCode();
  const auto modifiers = key.getModifiers();
  const bool altDown = modifiers.isAltDown();

  if (waveformBar != nullptr && waveformBar->keyPressed(key))
    return true;

  if (keyCode == juce::KeyPress::spaceKey) {
    if (altDown) {
      processorRef.stopSong();
      return true;
    }
    processorRef.toggleSongPlayPause();
    return true;
  }

  if (keyCode == juce::KeyPress::leftKey) {
    if (altDown) {
      if (prevSongAction)
        prevSongAction();
    } else {
      processorRef.seekSongRelative(-5.0);
    }
    return true;
  }

  if (keyCode == juce::KeyPress::rightKey) {
    if (altDown) {
      if (nextSongAction)
        nextSongAction();
    } else {
      processorRef.seekSongRelative(5.0);
    }
    return true;
  }

  if (key == juce::KeyPress::escapeKey) {
    processorRef.stopSong();
    return true;
  }

  return false;
}

void MTControlsAudioProcessorEditor::showPendingAccountDeletion() {
  const auto receipts = SessionPersistence::listAccountDeletionReceipts(); if (receipts.isEmpty()) return;
  const auto receipt = receipts[0]; const juce::String scope("deletion-status-recovery");
  auto *content = new AccountDeletionModal(scope); juce::Component::SafePointer<AccountDeletionModal> modal(content);
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this); const auto completed = std::make_shared<bool>(false);
  content->setSize(juce::jmin(500, juce::jmax(320, getWidth() - 24)), juce::jmin(620, juce::jmax(240, getHeight() - 24)));
  content->onAccepted = [receipt] { return SessionPersistence::blockAccountForDeletionReceipt(receipt); };
  content->onDismissed = [safe, receipt, completed](bool) {
    if (*completed) SessionPersistence::acknowledgeCompletedDeletionReceipt(receipt);
    if (safe && *completed) juce::MessageManager::callAsync([safe] { if (safe) safe->showPendingAccountDeletion(); });
  };
  content->onCancelled = [modal] { if (modal) if (auto *window = modal->findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState(0); };
  content->onStatusRequested = [safe, modal, receipt, scope, completed](int generation) {
    const auto base = getApiBaseUrl();
    std::thread([safe, modal, receipt, scope, completed, generation, base] {
      const auto result = AccountDeletionApi::continuePending(base, receipt);
      juce::MessageManager::callAsync([safe, modal, receipt, scope, completed, generation, result] {
        if (!modal || !safe || modal->getGeneration() != generation || modal->getState() != AccountDeletionModal::State::pending) return;
        if (result.ok && ((result.state == "prepared" && SessionPersistence::clearUnsubmittedDeletionReceipt(receipt)) ||
                          (result.state == "not-accepted" && SessionPersistence::acknowledgeCompletedDeletionReceipt(receipt)))) {
          modal->invalidate(); safe->tryResumeSessionAsync(); return;
        }
        if (!result.ok || result.state == "prepared") { modal->fail(generation, scope); return; }
        modal->receiveStatus(generation, scope, result.state == "complete" ? AccountDeletionModal::State::complete : AccountDeletionModal::State::pending);
        *completed = modal->getState() == AccountDeletionModal::State::complete;
      });
    }).detach();
  };
  juce::DialogWindow::LaunchOptions options; options.content.setOwned(content); options.dialogTitle = L10n::tr("Estado de eliminación", "Deletion status");
  options.dialogBackgroundColour = AuthUi::surface; options.useNativeTitleBar = true; options.resizable = false; options.escapeKeyTriggersCloseButton = true;
  options.launchAsync(); content->resumePendingStatus();
}
