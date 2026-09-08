#include "PluginEditor.h"
#include "PluginEditorSupport.h"
#include "SongLoadProbe.h"
#include "audio/AudioDeviceMonitor.h"
#include "audio/SongMixExporter.h"
#include "data/AppSettings.h"
#include "data/AssetBootstrapManager.h"
#include "data/AuthSessionApi.h"
#include "data/CloudSyncIndexStore.h"
#include "data/LocalAuthCallbackServer.h"
#include "data/Localization.h"
#include "data/ProjectStore.h"
#include "data/SessionAuthLogger.h"
#include "data/SessionPersistence.h"
#include "data/SongData.h"
#include "data/SongLibraryStore.h"
#include "ui/UiCommon.h"
#include "ui/components/PadsHandlePanel.h"
#include "ui/components/ReauthPromptModal.h"
#include "platform/NativeAuthSession.h"
#include "ui/components/SetlistHandlePanel.h"
#include "ui/components/WelcomeModal.h"
#include "ui/panels/ChannelGridPanel.h"
#include "ui/panels/PadsComponent.h"
#include "ui/panels/SettingsPanel.h"
#include "ui/panels/SongListPanel.h"
#include "ui/panels/TopBar.h"
#include "ui/panels/WaveformBar.h"
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

  void paint(juce::Graphics &g) override {
    g.fillAll(juce::Colours::black.withAlpha(0.38f));
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
  std::function<void()> onDismiss;
};

namespace {
bool supportsSetlistDrawer(const juce::Component &component) {
  const auto bounds = component.getLocalBounds();
  const int shortSide = juce::jmin(bounds.getWidth(), bounds.getHeight());
  const int longSide = juce::jmax(bounds.getWidth(), bounds.getHeight());
  return useTouchOptimisedControls() && shortSide >= 700 && longSide >= 960;
}

bool isPhoneSizedLayout(const juce::Component &component) {
  if (!useTouchOptimisedControls())
    return false;
  const auto bounds = component.getLocalBounds();
  return juce::jmin(bounds.getWidth(), bounds.getHeight()) < 600;
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
    g.fillAll(juce::Colours::black.withAlpha(0.44f));
  }

  void resized() override {
    auto area = getLocalBounds();
    const int margin = useTouchOptimisedControls() ? 18 : 28;
    area.reduce(margin, margin);

    const int maxWidth = useTouchOptimisedControls() ? 980 : 1080;
    const int maxHeight = useTouchOptimisedControls() ? 860 : 780;
    const int width = juce::jmin(maxWidth, area.getWidth());
    const int height = juce::jmin(maxHeight, area.getHeight());

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
    : AudioProcessorEditor(p) {
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

  if (const auto audioCtx = getStandaloneAudioContext();
      audioCtx.manager != nullptr) {
    ensureStandaloneMidiInputsEnabled(*audioCtx.manager);
  }

  lastSuccessfulLoginMs = AppSettings::getLastSuccessfulLoginMs();
  reauthGraceDeadlineMs = AppSettings::getReauthGraceDeadlineMs();
  PersistedSessionSnapshot bootSnap;
  const bool hasBootSnapshot = SessionPersistence::load(bootSnap);
  const bool canFastBootFromSnapshot =
      hasBootSnapshot && canUseSnapshotForLocalBootstrap(bootSnap);
  if (hasBootSnapshot)
    lastOnlineValidationMs = bootSnap.lastOnlineValidationMs;

  initializeWelcomeModal();

  if (canFastBootFromSnapshot) {
    SessionAuthLogger::write(
        "PluginEditor ctor: local session snapshot accepted, entering UI before "
        "online validation");
    applyPersistedSnapshotToActive(bootSnap);
    lastSuccessfulLoginMs =
        juce::jmax(lastSuccessfulLoginMs, bootSnap.lastOnlineValidationMs);
    AppSettings::setLastSuccessfulLoginMs(lastSuccessfulLoginMs);
    initializeMainUi();
  } else {
    showWelcomeOnlyMode();
  }

  tryResumeSessionAsync();
  if (!mainUiInitialized)
    startTimerHz(12);

#if JucePlugin_Build_Standalone
  juce::MessageManager::callAsync(
      [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(
           this)] {
        if (safe != nullptr)
          hideStandaloneOptionsButton(safe.getComponent());
      });
#endif
}

void MTControlsAudioProcessorEditor::openProjectFromExternalApplication(
    const juce::File &file) {
  pendingExternalProjectFile = file;
  deferredStartupSongLoadPending = false;
  deferredStartupSongIndex = -1;
  deferredStartupSongLoadAtMs = 0;

  // Authentication/bootstrap may still be showing. The regular editor timer
  // will open the project as soon as the complete library UI is available.
  if (!isTimerRunning())
    startTimerHz(mainUiInitialized ? 20 : 12);
}

bool MTControlsAudioProcessorEditor::isSongCurrentlyLoaded(
    const juce::String &songId) const noexcept {
  return hasCurrentLoadedSong && songId.trim().isNotEmpty() &&
         currentLoadedSong.id == songId.trim();
}

void MTControlsAudioProcessorEditor::flushActiveSongPersistenceNow() {
  maybePersistSongWindowForActiveSong();
  maybePersistClickDynMetronomeEnabledForActiveSong();
  if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
    grid->flushPendingPersistenceNow();
  if (hasCurrentLoadedSong && currentLoadedSong.projectPath.isNotEmpty())
    ProjectStore::flushPendingSettings(juce::File(currentLoadedSong.projectPath));
  ProjectStore::flushAllPendingSettings();
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
#if JucePlugin_Build_Standalone
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

  auto bounds = top->getBounds();
  const int w = juce::jmax(1, bounds.getWidth());
  const int h = juce::jmax(1, bounds.getHeight());
  bounds.setSize(w, h);
  bounds.setCentre(display->userArea.getCentre());
  top->setBounds(bounds);
  initialStandaloneWindowCentered = true;
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
  if (padsComponent != nullptr)
    padsComponent->setResponsiveProfile(profile);
}

void MTControlsAudioProcessorEditor::updateResponsiveLayoutProfile() {
  const auto nextProfile = resolveResponsiveLayoutProfile();
  if (nextProfile != responsiveLayoutProfile)
    applyResponsiveLayoutProfile(nextProfile);
}

void MTControlsAudioProcessorEditor::refreshCloudLibrary(
    const juce::String &query) {
  if (!activeSession.allowed || activeSession.firebaseApiKey.isEmpty() ||
      activeSession.refreshToken.isEmpty()) {
    if (songListPanelRaw != nullptr) {
      songListPanelRaw->setCloudSongs({});
      songListPanelRaw->setCloudSearchLoading(false);
    }
    return;
  }
  if (cloudLibraryClient == nullptr)
    cloudLibraryClient = std::make_unique<CloudLibraryClient>();

  const auto generation = ++cloudLibrarySearchGeneration;
  const auto safeThis =
      juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  cloudLibraryClient->search(
      getApiBaseUrl(), activeSession.firebaseApiKey, activeSession.refreshToken,
      query, {}, 50,
      [safeThis, generation](CloudSongPage page, CloudRequestError error) {
        if (safeThis == nullptr || generation != safeThis->cloudLibrarySearchGeneration)
          return;
        if (safeThis->songListPanelRaw != nullptr) {
          safeThis->songListPanelRaw->setCloudSearchLoading(false);
          if (error.code.isNotEmpty())
            return;
          safeThis->songListPanelRaw->setCloudSongs(std::move(page.songs));
        }
      });
}

void MTControlsAudioProcessorEditor::initializeMainUi() {
  if (lastSuccessfulLoginMs > 0) {
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

  {
    const auto audioCtx = getStandaloneAudioContext();
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
            juce::jmax(1, currentDevice->getOutputChannelNames().size()));
      } else {
        missingAudioDeviceSinceMs = nowMs;
      }
    }
  }

  if (topBar != nullptr && waveformBar != nullptr &&
      channelGridPanel != nullptr && songListPanel != nullptr &&
      setlistHandlePanel != nullptr) {
    topBar->setVisible(true);
    waveformBar->setVisible(true);
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

    const auto userArea =
        juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
#if JUCE_IOS || JUCE_ANDROID
    setSize(userArea.getWidth(), userArea.getHeight());
#else
    const int maxW = juce::jmax(kMinWindowW, userArea.getWidth() - kScreenMargin);
    const int maxH = juce::jmax(kMinWindowH, userArea.getHeight() - kScreenMargin);
    const int w = juce::jlimit(kMinWindowW, maxW, kDefaultWindowW);
    const int h = juce::jlimit(kMinWindowH, maxH, kDefaultWindowH);

    if (AppSettings::getExpandWindowOnLaunch()) {
      juce::MessageManager::callAsync(
          [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(
               this)] {
            if (safe != nullptr)
              expandTopLevelWindowToPrimaryWorkArea(safe.getComponent());
          });
    } else {
      setSize(w, h);
    }
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

  auto topBarImpl = std::make_unique<TopBar>(p);
  auto *topBarRaw = topBarImpl.get();
  topBar = std::move(topBarImpl);
  topBarRaw->setResponsiveProfile(resolveResponsiveLayoutProfile());
  topBarRaw->setTransportProviders(
      [&p] { return p.isSongPlaying(); },
      [&p] { return p.getSongCurrentPositionSeconds(); },
      [&p] {
        return p.isCustomArrangementActive() ? p.getSongLengthSeconds()
                                             : p.getSongEndSeconds();
      },
      [&p] { return p.getLoadedSongKey(); },
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
  auto *waveformBarRaw = waveformBarImpl.get();
  waveformBarPtr = waveformBarRaw;
  waveformBar = std::move(waveformBarImpl);
  waveformBarRaw->setMidiLearnBindingLabelProvider(
      midiLearnBindingLabelForTarget);
  waveformBarRaw->setTransportProviders(
      [&p] { return p.getSongCurrentPositionSeconds(); },
      [&p] { return p.getSongLengthSeconds(); },
      [&p] { return p.isSongPlaying(); });
  waveformBarRaw->setSongSourceLengthProvider(
      [&p] { return p.getLoadedSongLengthSeconds(); });
  waveformBarRaw->setSongEndCustomizedProvider(
      [&p] { return p.isSongEndCustomized(); });
  waveformBarRaw->setSongWindowProviders(
      [&p] { return p.getSongStartSeconds(); },
      [&p] { return p.getSongEndSeconds(); });
  waveformBarRaw->setSeekCallback([&p](double targetSeconds) {
    p.seekSongTo(targetSeconds);
  });
  // The jump button now respects the active loop: the request is queued as a
  // pending jump and the audio thread resolves whether it should escape the
  // current loop (SectionScheduler::evaluate handles that precedence). Clearing
  // the loop eagerly here would defeat the "loop on a section while
  // pre-arranging a jump to the next one" workflow, and it also hides the
  // pending-jump escape branch from the scheduler.
  waveformBarRaw->setOnArrangementPartJumpRequest(
      [&p](int partIndex) { p.requestArrangementPartJump(partIndex); });
  waveformBarRaw->setOnArrangementPartLoopToggleRequest([&p](int partIndex) {
    // Marking a loop must NOT auto-jump the playhead; the loop waits for
    // natural playback to reach the section. We still drop any previously
    // queued jump so the user can replace a pending jump with a loop target
    // without the two intents fighting each other.
    p.clearPendingArrangementPartJump();
    p.toggleArrangementPartLoop(partIndex);
  });
  waveformBarRaw->setOnArrangementPlaybackSelectionClearRequest([&p] {
    p.clearArrangementPartLoop();
    p.clearPendingArrangementPartJump();
  });
  waveformBarRaw->setPendingArrangementPartProvider(
      [&p] { return p.getPendingArrangementPartJumpIndex(); });
  waveformBarRaw->setActiveArrangementLoopPartProvider(
      [&p] { return p.getArrangementPartLoopIndex(); });
  waveformBarRaw->setOnEditToggle([this](const bool editing) {
    auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
    processorRef.setArrangementEditingActive(editing);
    resized();
    repaint();
  });
  waveformBarRaw->setOnWaveformHeightToggle([this](bool expanded) {
    isWaveformHeightExpanded = expanded;
    resized();
    repaint();
  });
  waveformBarRaw->setOnArrangementChanged(
      [this, &p](const SongArrangementSnapshot &arrangement,
                 bool isCustomArrangement) {
        if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
          return;
        if (!SongLibraryStore::updateSongArrangement(
                currentLoadedSong.id, arrangement, isCustomArrangement)) {
          SessionAuthLogger::write(
              "[Arrangement] Couldn't persist changes for song id=" +
              currentLoadedSong.id);
        }
        p.setSongArrangement(arrangement, isCustomArrangement);
      });
  waveformBarRaw->setOnArrangementReset([this, &p] {
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
    }
  });
  waveformBarRaw->setOnTempoMapChanged([this](const SongTempoMapSnapshot &tempoMap) {
    if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
      return;
    currentLoadedSong.tempoMap = tempoMap;
    SongLibraryStore::updateSong(currentLoadedSong);
  });
  waveformBarRaw->setOnActiveTempoRegionChanged(
      [&p](const SongTempoRegionSnapshot &region) {
        p.setMetronomeBpm(region.bpm);
        p.setMetronomeBeatsPerBar(
            timeSignatureBeatsPerBar(region.timeSignature));
      });
  waveformBarRaw->setOnExportMixRequested(
      [this](const juce::String &format) { beginExportCurrentSongMix(format); });

  auto channelGridImpl =
      std::make_unique<ChannelGridPanel>(stripLookAndFeel, p);
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
        const auto audioCtx = PluginEditorSupport::getStandaloneAudioContext();
        if (audioCtx.manager == nullptr)
          return;

        PluginEditorSupport::enforceStandaloneOutputOnlyAudio(*audioCtx.manager);
        if (auto *currentDevice = audioCtx.manager->getCurrentAudioDevice();
            currentDevice != nullptr) {
          processorRef.getAudioDeviceBridge().setAvailableOutputChannelCount(
              juce::jmax(1, currentDevice->getOutputChannelNames().size()));
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
    if (channelGridRaw)
      channelGridRaw->refreshThemeColours();
    if (padsComponent)
      padsComponent->refreshThemeColours();
    if (padsHandlePanel)
      padsHandlePanel->repaint();
  };

  auto songListImpl = std::make_unique<SongListPanel>();
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
  songListRaw->setOnProjectUploadRequested(
      [this](const ImportedSongData &song) {
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
      [this](const juce::String &remoteSongId,
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
    if (songLoadBusy)
      return false;
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
    maybePersistSongWindowForActiveSong();
    if (channelGridRaw != nullptr)
      channelGridRaw->flushPendingPersistenceNow();
    if (hasCurrentLoadedSong && currentLoadedSong.projectPath.isNotEmpty())
      ProjectStore::flushPendingSettings(juce::File(currentLoadedSong.projectPath));
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
      if (projectFile.existsAsFile()) {
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
    if (resolvedStems.empty()) {
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
        stemPaths, sourceSong.title, sourceSong.key, static_cast<double>(sourceSong.bpm),
        [&pushLoadUi](double stemPhase01) {
          pushLoadUi(0.55 * stemPhase01,
                     L10n::tr(u8"Leyendo audio de las pistas...",
                              u8"Reading stem audio..."));
        },
        true);
    SongLoadProbe::instance().endPhase();
    SessionAuthLogger::write("[SongLoad] processor result title=" +
                             sourceSong.title + " loaded=" +
                             juce::String(loaded ? 1 : 0) + " stems=" +
                             juce::String(stemPaths.size()));

    if (!loaded) {
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
    if (dynamicGuideSettings.enabled && !dynamicGuideCustomized)
      dynamicGuideMix.mute = false;
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

    // The gate used to skip this call for project songs, which left
    // the per-stem outputWidth at the default (1 = mono) until the
    // user opened the routing panel. Routing defaults now drive the
    // width for both legacy and project songs; the project's per-stem
    // mix (outputChannel) still owns the channel.
    applyRoutingDefaultsToSong(currentLoadedSong);
    p.refreshPadsRoutingFromSettings();
    p.setMetronomeOutputRouting(clickDynMix.outputChannel,
                                clickDynMix.outputWidth);
    p.setMetronomeEnabled(clickDynMix.metronomeEnabled);
    trackClickDynMetronomeEnabled(clickDynMix.metronomeEnabled);
    p.setSongArrangement(arrangement, isCustomArrangement);
    p.setArrangementEditingActive(false);
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

    auto cachedBins = SongLibraryStore::loadWaveformCache(loadedSong.id);
    if (cachedBins.empty()) {
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
    waveformBarRaw->setSongRhythm(static_cast<double>(loadedSong.bpm),
                                  loadedSong.timeSignature);
    waveformBarRaw->setArrangement(arrangement, isCustomArrangement);
    waveformBarRaw->setTempoMap(loadedSong.tempoMap);
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
    const auto requestedSongIdentity = playbackIdentityForSong(song);
    const auto loadedSongIdentity =
        hasCurrentLoadedSong ? playbackIdentityForSong(currentLoadedSong)
                             : juce::String{};

    const bool isSameLoadedSong =
        hasCurrentLoadedSong && requestedSongIdentity.isNotEmpty() &&
        requestedSongIdentity == loadedSongIdentity;
    if (!isSameLoadedSong && loadSongAction)
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
    if (lastSelectedSongId.isNotEmpty()) {
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
                   resolveCurrentSongIndex]() {
    if (!songListRaw)
      return;
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
    const int count = songListRaw->getSongCount();
    if (count <= 0)
      return;
    int index = resolveCurrentSongIndex();
    if (index < 0)
      index = songListRaw->getLastActivatedIndex();
    if (index < 0)
      index = 0;
    index = juce::jlimit(0, count - 1, index);

    // Prefer a song explicitly marked for auto-play-next. This keeps worship
    // flow predictable: clicking "next" while the last song plays should jump
    // to the next marked song instead of silently no-op'ing at the boundary.
    if (hasCurrentLoadedSong && currentLoadedSong.id.isNotEmpty()) {
      const int markedNext =
          songListRaw->resolveAutoPlayNextIndex(currentLoadedSong.id);
      if (juce::isPositiveAndBelow(markedNext, count) && markedNext != index) {
        requestSongLoadByIndex(markedNext);
        return;
      }
    }

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
      [&p] { p.toggleSongPlayPause(); }, [&p] { p.stopSong(); }, prevSong,
      nextSong, [&p](bool fadeOut) {
        p.setFadeStopsPlayback(false);
        p.setFadeActive(fadeOut);
      },
      [&p](double deltaSec) { p.seekSongRelative(deltaSec); });
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
    if (!supportsSetlistDrawer(*this)) {
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
  setlistDrawerScrim = std::make_unique<SetlistDrawerScrim>([this] {
    isSetlistDrawerOpen = false;
    resized();
    repaint();
  });
  if (songListRaw)
    setlistHandlePanel->setTitle(songListRaw->getActiveSetlistName());

  padsComponent = std::make_unique<PadsComponent>(p, stripLookAndFeel);
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
    isPadsCollapsed = !isPadsCollapsed;
    padsHandlePanel->setCollapsed(isPadsCollapsed);
    padsComponent->setVisible(!isPadsCollapsed);
    resized();
  };
  addAndMakeVisible(*padsHandlePanel);

  addAndMakeVisible(*topBar);
  addAndMakeVisible(*waveformBar);
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
      reauthPromptDismissed = true;
      reauthLoginInProgress = false;
      hideReauthPromptModal();
    };
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
  setSize(userArea.getWidth(), userArea.getHeight());
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

#if !JUCE_IOS && !JUCE_ANDROID
  if (AppSettings::getExpandWindowOnLaunch()) {
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(
             this)] {
          if (safe != nullptr)
            expandTopLevelWindowToPrimaryWorkArea(safe.getComponent());
        });
  }
#endif

  setWantsKeyboardFocus(true);
  grabKeyboardFocus();

  wasSongPlaying = p.isSongPlaying();
  startTimerHz(20);
  mainUiInitialized = true;
  resized();
  updateSystemUsageMetrics();
}

void MTControlsAudioProcessorEditor::initializeWelcomeModal() {
  auto modal = std::make_unique<WelcomeModal>();
  modal->onLoginRequested = [this] { launchExternalLogin(); };
  welcomeModal = std::move(modal);
  addAndMakeVisible(*welcomeModal);
  resized();
}

void MTControlsAudioProcessorEditor::launchExternalLogin() {
#if JUCE_IOS
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

  const auto started = NativeAuthSession::start(
      loginUrl, "playworship",
      [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)](
          const juce::String &callbackUrl, const juce::String &error) {
        if (safe == nullptr)
          return;
        if (callbackUrl.isNotEmpty()) {
          safe->enqueueAuthCallbackUrl(callbackUrl);
          return;
        }
        safe->reauthLoginInProgress = false;
        if (error.isNotEmpty()) {
          if (auto *modal = dynamic_cast<WelcomeModal *>(safe->welcomeModal.get()))
            modal->setStatusText("No se completo la autenticacion.", true);
          if (auto *reauth = dynamic_cast<ReauthPromptModal *>(
                  safe->reauthPromptModal.get()))
            reauth->showError("No se completo la autenticacion.");
        }
      });
  if (!started) {
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
      modal->setStatusText("No se pudo abrir el inicio de sesion.", true);
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

  juce::URL(loginUrl).launchInDefaultBrowser();
#endif
}

void MTControlsAudioProcessorEditor::completeBootstrapLogin(
    const juce::String &callbackUrl) {
  const auto params = parseCallbackParams(callbackUrl);
  const auto receivedState = params["state"].trim();
  if (authExpectedState.isEmpty() || receivedState.isEmpty() ||
      receivedState != authExpectedState) {
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get())) {
      modal->setStatusText("Autenticacion invalida. Reintenta.", true);
    }
    if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
      reauth->showError("Autenticacion invalida. Reintenta.");
    reauthLoginInProgress = false;
    return;
  }

  activeSession.uid = params["uid"].trim();
  activeSession.email = params["email"].trim();
  activeSession.plan = params["plan"].trim();
  activeSession.subscriptionStatus = params["subscriptionStatus"].trim();
  activeSession.expiresAt = params["expiresAt"].trim();
  activeSession.allowed = parseBooleanLike(params["allowed"], false);
  activeSession.reasonCode = params["reasonCode"].trim();
  activeSession.reason = params["reason"].trim();
  activeSession.refreshToken = params["refreshToken"].trim();
  activeSession.firebaseApiKey = params["firebaseApiKey"].trim();
  activeSession.licenseKey = params["licenseKey"].trim();
  const auto expMsFromParams = params["idTokenExpiresAtMs"].trim().getLargeIntValue();
  if (expMsFromParams > 0)
    activeSession.idTokenExpiresAtMs = expMsFromParams;
  const auto sessionJsonRaw = params["session"].trim();
  if (sessionJsonRaw.isNotEmpty()) {
    const auto parsed = juce::JSON::parse(sessionJsonRaw);
    activeSession.uid =
        readStringFromObject(parsed, "uid").isNotEmpty()
            ? readStringFromObject(parsed, "uid")
            : activeSession.uid;
    activeSession.email =
        readStringFromObject(parsed, "email").isNotEmpty()
            ? readStringFromObject(parsed, "email")
            : activeSession.email;
    activeSession.plan =
        readStringFromObject(parsed, "plan").isNotEmpty()
            ? readStringFromObject(parsed, "plan")
            : activeSession.plan;
    activeSession.subscriptionStatus =
        readStringFromObject(parsed, "subscriptionStatus").isNotEmpty()
            ? readStringFromObject(parsed, "subscriptionStatus")
            : activeSession.subscriptionStatus;
    activeSession.expiresAt =
        readStringFromObject(parsed, "expiresAt").isNotEmpty()
            ? readStringFromObject(parsed, "expiresAt")
            : activeSession.expiresAt;
    activeSession.allowed = parseBooleanLike(
        readStringFromObject(parsed, "allowed"), activeSession.allowed);
    activeSession.reasonCode = readStringFromObject(parsed, "reasonCode")
                                   .isNotEmpty()
                               ? readStringFromObject(parsed, "reasonCode")
                               : activeSession.reasonCode;
    activeSession.reason = readStringFromObject(parsed, "reason").isNotEmpty()
                               ? readStringFromObject(parsed, "reason")
                               : activeSession.reason;
    activeSession.refreshToken =
        readStringFromObject(parsed, "refreshToken").isNotEmpty()
            ? readStringFromObject(parsed, "refreshToken")
            : activeSession.refreshToken;
    activeSession.firebaseApiKey =
        readStringFromObject(parsed, "firebaseApiKey").isNotEmpty()
            ? readStringFromObject(parsed, "firebaseApiKey")
            : activeSession.firebaseApiKey;
    if (readStringFromObject(parsed, "licenseKey").isNotEmpty())
      activeSession.licenseKey = readStringFromObject(parsed, "licenseKey");
    const auto expMsRaw = readStringFromObject(parsed, "idTokenExpiresAtMs");
    const auto expMs = expMsRaw.getLargeIntValue();
    if (expMs > 0)
      activeSession.idTokenExpiresAtMs = expMs;

    const auto userVar = parsed.getProperty("user", juce::var{});
    if (userVar.isObject()) {
      const auto userUid = readStringFromObject(userVar, "uid");
      const auto userEmail = readStringFromObject(userVar, "email");
      const auto userPlan = readStringFromObject(userVar, "plan");
      const auto userStatus = readStringFromObject(userVar, "subscriptionStatus");
      if (userUid.isNotEmpty())
        activeSession.uid = userUid;
      if (userEmail.isNotEmpty())
        activeSession.email = userEmail;
      if (userPlan.isNotEmpty())
        activeSession.plan = userPlan;
      if (userStatus.isNotEmpty())
        activeSession.subscriptionStatus = userStatus;
    }
  }

  if (activeSession.allowed &&
      !hasValidAccessBySubscriptionState(activeSession.subscriptionStatus,
                                         activeSession.plan)) {
    activeSession.allowed = false;
    if (activeSession.reasonCode.isEmpty())
      activeSession.reasonCode = "INVALID_SUBSCRIPTION_STATE";
    if (activeSession.reason.isEmpty()) {
      activeSession.reason =
          "Subscription status is not free/trial/active with a valid plan";
    }
  }

  if (!activeSession.allowed) {
    juce::String message =
        userMessageForAccessDenial(activeSession.reasonCode, activeSession.reason);
    SessionAuthLogger::write(
        "completeBootstrapLogin: access denied allowed=" +
        juce::String(static_cast<int>(activeSession.allowed)) + " plan=" +
        activeSession.plan + " status=" + activeSession.subscriptionStatus +
        " reasonCode=" + activeSession.reasonCode + " reason=" +
        activeSession.reason);
    reauthLoginInProgress = false;
    if (mainUiInitialized) {
      handleLogout();
    }
    if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
      modal->setStatusText(message, true);
    return;
  }

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
      "completeBootstrapLogin: web login OK, snapshot saved, calling "
      "initializeMainUi");
  initializeMainUi();
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
                                                  juce::Component *parent) {
  if (!mainUiInitialized)
    return;
#if JUCE_IOS
  closeSettings();
#else
  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = L10n::tr(u8"Configuración", "Settings");
  options.dialogBackgroundColour = UITheme::appBg;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = false;
#endif
  auto &proc = static_cast<MTControlsAudioProcessor &>(processor);
  const auto routingDefaults = AppSettings::getRoutingDefaults();
  const auto audioCtx = getStandaloneAudioContext();
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
      AppSettings::getExpandWindowOnLaunch(),
      [](const bool expandOnLaunch) {
        AppSettings::setExpandWindowOnLaunch(expandOnLaunch);
      },
      AppSettings::getPrimeImportEnabled(),
      [](const bool enabled) { AppSettings::setPrimeImportEnabled(enabled); },
      AppSettings::getStemOptimizationEnabled(),
      [](const bool enabled) { AppSettings::setStemOptimizationEnabled(enabled); },
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
      SettingsPanel::SessionSummary{activeSession.uid, activeSession.email,
                                    activeSession.plan,
                                    activeSession.subscriptionStatus,
                                    activeSession.expiresAt,
                                    activeSession.allowed,
                                    activeSession.reasonCode,
                                    activeSession.reason,
                                    activeSession.licenseKey},
      [this] {
        const auto options = juce::MessageBoxOptions::makeOptionsOkCancel(
            juce::MessageBoxIconType::QuestionIcon,
            L10n::tr(u8"¿Cerrar sesión?", "Sign out?"),
            L10n::tr(
                u8"Tendrás que iniciar sesión nuevamente para acceder a tu biblioteca y a las canciones de Cloud.",
                "You'll need to sign in again to access your library and Cloud songs."),
            L10n::tr(u8"Cerrar sesión", "Sign out"),
            L10n::tr("Cancelar", "Cancel"), this);
        juce::NativeMessageBox::showAsync(
            options,
            [safe = juce::Component::SafePointer<
                 MTControlsAudioProcessorEditor>(this)](const int result) {
              if (result == 0 && safe != nullptr)
                safe->handleLogout();
            });
      },
      [this] {
        const auto options = juce::MessageBoxOptions::makeOptionsOkCancel(
            juce::MessageBoxIconType::WarningIcon,
            L10n::tr("Purgar datos locales de Cloud", "Purge local Cloud data"),
            L10n::tr(
                "Se eliminarán todas las canciones y setlists de Biblioteca, cachés, descargas y vínculos de sincronización. "
                "La Biblioteca quedará vacía. No se eliminará ningún archivo .pwproj ni contenido de la nube.",
                "All Library songs and setlists, caches, downloads, and sync links will be removed. "
                "The Library will be empty. No .pwproj file or cloud content will be deleted."),
            L10n::tr("Purgar", "Purge"), L10n::tr("Cancelar", "Cancel"),
            this);
        juce::NativeMessageBox::showAsync(
            options,
            [safeThis = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this)](int result) {
              if (result != 0 || safeThis == nullptr)
                return;
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
  panel->setSize(useTouchOptimisedControls() ? 980 : 1080,
                 useTouchOptimisedControls() ? 860 : 780);
  panel->refreshThemeColours();

#if JUCE_IOS
  auto overlay = std::make_unique<SettingsOverlayComponent>(
      std::unique_ptr<SettingsPanel>(panel), [this] { closeSettings(); });
  settingsPanelRaw = overlay->getPanel();
  settingsOverlay = std::move(overlay);
  addAndMakeVisible(*settingsOverlay);
  settingsOverlay->toFront(true);
  settingsOverlay->setBounds(getLocalBounds());
  return;
#else
  options.content.setOwned(panel);

  if (auto *window = options.launchAsync()) {
    settingsWindow = window;
    settingsPanelRaw = panel;
    window->centreAroundComponent(parent != nullptr ? parent : this, 760, 620);
  }
#endif
}

void MTControlsAudioProcessorEditor::closeSettings() {
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
}

void MTControlsAudioProcessorEditor::showWelcomeOnlyMode() {
  setResizable(true, true);
#if JUCE_IOS || JUCE_ANDROID
  setResizeLimits(320, 280, 32000, 32000);
#else
  setResizeLimits(480, 500, 32000, 32000);
#endif
#if JUCE_IOS || JUCE_ANDROID
  if (const auto *display =
          juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    setSize(display->userArea.getWidth(), display->userArea.getHeight());
#endif
  if (topBar)
    topBar->setVisible(false);
  if (waveformBar)
    waveformBar->setVisible(false);
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
  SessionAuthLogger::write("handleLogout");
  closeSettings();

  if (localAuthServer != nullptr)
    localAuthServer->stop();
  authCallbackPort = 0;

  activeSession = {};
  lastOnlineValidationMs = 0;
  SessionPersistence::clear();
  lastSuccessfulLoginMs = 0;
  AppSettings::setLastSuccessfulLoginMs(0);
  reauthGraceDeadlineMs = 0;
  AppSettings::setReauthGraceDeadlineMs(0);
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
    modal->resetStatusToDefault();
}

void MTControlsAudioProcessorEditor::showReauthPromptModal() {
  if (!mainUiInitialized || reauthPromptModal == nullptr)
    return;
  if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get())) {
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
}

PersistedSessionSnapshot
MTControlsAudioProcessorEditor::persistedSnapshotFromActiveSession(
    const juce::int64 lastOnlineMs) const {
  PersistedSessionSnapshot p;
  p.uid = activeSession.uid;
  p.email = activeSession.email;
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
  activeSession.uid = snap.uid;
  activeSession.email = snap.email;
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
}

void MTControlsAudioProcessorEditor::saveSessionSnapshotToDisk() {
  const auto nowMs = juce::Time::currentTimeMillis();
  lastOnlineValidationMs = nowMs;
  SessionPersistence::save(persistedSnapshotFromActiveSession(nowMs));
}

void MTControlsAudioProcessorEditor::tryResumeSessionAsync() {
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
  SessionAuthLogger::write("tryResumeSessionAsync: apiBase=" + apiBase +
                           " deviceIdPrefix=" + deviceId.substring(0, 12) + "...");

  std::thread([safe, snap, apiBase, deviceId] {
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

    juce::MessageManager::callAsync([safe, success, validatedOnline, out, welcomeError,
                                     clearSnapshot] {
      if (safe != nullptr)
        safe->applySessionResumeOnMessageThread(success, validatedOnline, out,
                                                welcomeError, clearSnapshot);
    });
  }).detach();
}

void MTControlsAudioProcessorEditor::applySessionResumeOnMessageThread(
    const bool success, const bool validatedOnline, PersistedSessionSnapshot snapshot,
    const juce::String &welcomeError, const bool clearSnapshotFile) {
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
}

void MTControlsAudioProcessorEditor::evaluateReauthPolicy() {
  // lastSuccessfulLoginMs: ultima confirmacion util (login web o validate-access al
  // arrancar). Se actualiza en disco via AppSettings; lastOnlineValidationMs duplica
  // el valor persistido en session_snapshot para ventana offline.
  if (!mainUiInitialized || lastSuccessfulLoginMs <= 0)
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
  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
  const int stemCount = processorRef.getLoadedStemCount();
  const auto audioCtx = getStandaloneAudioContext();
  const auto deviceKey = getCurrentDeviceKey(audioCtx);
  const auto defaults = AppSettings::getRoutingDefaultsForDevice(deviceKey);
  const auto clickRouting = routingForCategory(defaults, "Click");

  // ---- Routing probe: enter ------------------------------------------
  // Records the device key, how many stems the processor has loaded
  // and how many the song says are included, plus the click routing
  // we are about to push. Discrepancies between the two counts would
  // show up as a hidden off-by-one in the load pipeline.
  SongLoadProbe::instance().recordEvent(
      "routing", "apply.enter",
      "deviceKey=" + deviceKey + " stemCount=" + juce::String(stemCount) +
          " clickW=" + juce::String(clickRouting.width) +
          " clickC=" + juce::String(clickRouting.baseChannel));

  processorRef.setMetronomeOutputRouting(clickRouting.baseChannel,
                                         clickRouting.width);

  int stemIndex = 0;
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;
    if (!juce::isPositiveAndBelow(stemIndex, stemCount))
      break;

    const auto routing = routingForCategory(defaults, stem.category);

    // Snapshot the atomics BEFORE we mutate, so the row exposes both
    // the previous and the requested state. The bug shows up as
    // prevW=1 newW=1 on load (defaults never pushed) and prevW=1
    // newW=2 after touching the routing panel.
    const int prevW = processorRef.getStemOutputWidth(stemIndex);
    const int prevC = processorRef.getStemOutputChannel(stemIndex);

    processorRef.setStemOutputWidth(stemIndex, routing.width);
    processorRef.setStemOutputChannel(stemIndex, routing.baseChannel);

    SongLoadProbe::instance().recordEvent(
        "routing", "apply.stem",
        "idx=" + juce::String(stemIndex) + " cat=" + stem.category +
            " prevW=" + juce::String(prevW) +
            " newW=" + juce::String(routing.width) +
            " prevC=" + juce::String(prevC) +
            " newC=" + juce::String(routing.baseChannel));

    ++stemIndex;
  }

  // ---- Routing probe: exit -------------------------------------------
  SongLoadProbe::instance().recordEvent(
      "routing", "apply.exit", "stemsApplied=" + juce::String(stemIndex));

  // refreshPadsRoutingFromSettings used to be called here. It is now
  // the caller's responsibility so each call site reads the JSON
  // exactly once per user action. The dedup is visible at the two
  // call sites (loadSong and the routing panel onChanged).
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

  const auto audioCtx = getStandaloneAudioContext();
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
  if (pendingExternalProjectFile != juce::File{} && !songLoadBusy &&
      songListPanelRaw != nullptr) {
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
    const auto audioCtx = getStandaloneAudioContext();
    if (audioCtx.manager != nullptr) {
      enforceStandaloneOutputOnlyAudio(*audioCtx.manager);
      auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
      const juce::String currentDeviceKey = getCurrentDeviceKey(audioCtx);
      if (auto *currentDevice = audioCtx.manager->getCurrentAudioDevice();
          currentDevice != nullptr) {
        processorRef.getAudioDeviceBridge().setAvailableOutputChannelCount(
            juce::jmax(1, currentDevice->getOutputChannelNames().size()));
        missingAudioDeviceSinceMs = 0;
        nextAudioDeviceFallbackAttemptMs = nowMs + kAudioDeviceMissingGraceMs;

        if (currentDeviceKey.isNotEmpty() &&
            currentDeviceKey != lastObservedAudioDeviceKey) {
          // Routing probe: device change. The polling loop re-applies
          // routing whenever the device key changes; a row here
          // explains why the same apply can run outside of song load.
          SongLoadProbe::instance().recordEvent(
              "routing", "device.change",
              "deviceKey=" + currentDeviceKey +
                  " prev=" + lastObservedAudioDeviceKey +
                  " hasSong=" + juce::String(hasCurrentLoadedSong ? 1 : 0));
          lastObservedAudioDeviceKey = currentDeviceKey;
          processorRef.refreshPadsRoutingFromSettings();
          if (hasCurrentLoadedSong)
            applyRoutingDefaultsToSong(currentLoadedSong);
          if (auto *grid =
                  dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
            grid->refreshStemRoutingVisibility();
        }
      } else {
        lastObservedAudioDeviceKey.clear();
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
    if (reachedSongEnd) {
      const int nextIndex =
          songListPanelRaw->resolveAutoPlayNextIndex(currentLoadedSong.id);
      if (juce::isPositiveAndBelow(nextIndex, songListPanelRaw->getSongCount()) &&
          loadSongByIndexAction) {
        loadSongByIndexAction(nextIndex, true);
      }
    }
  }

  wasSongPlaying = isPlayingNow;
}

MTControlsAudioProcessorEditor::~MTControlsAudioProcessorEditor() {
  stopTimer();
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
  drawStudioBackground(g, getLocalBounds().toFloat());

  if (mainUiInitialized && !padsDrawerBackgroundBounds.isEmpty()) {
    const auto drawer = padsDrawerBackgroundBounds.toFloat().reduced(0.5f);
    g.setColour(UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.14f)
                    .withAlpha(0.92f));
    g.fillRoundedRectangle(drawer, 8.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.52f));
    g.drawRoundedRectangle(drawer, 8.0f, 1.0f);
  }
}

void MTControlsAudioProcessorEditor::resized() {
  if (!mainUiInitialized) {
    if (welcomeModal != nullptr)
      welcomeModal->setBounds(getLocalBounds());
    if (settingsOverlay != nullptr)
      settingsOverlay->setBounds(getLocalBounds());
    return;
  }

  auto area = getLocalBounds();
  updateResponsiveLayoutProfile();
  const bool minimalPhoneProfile =
      responsiveLayoutProfile == ResponsiveLayoutProfile::Compact &&
      isPhoneSizedLayout(*this);
  if (auto *bar = dynamic_cast<TopBar *>(topBar.get()))
    bar->setMinimalPhoneMode(minimalPhoneProfile);
  if (auto *wave = dynamic_cast<WaveformBar *>(waveformBar.get()))
    wave->setMinimalPhoneMode(minimalPhoneProfile);
#if JUCE_IOS || JUCE_ANDROID
  if (minimalPhoneProfile) {
    // Standalone iOS windows can extend under the sensor housing/notch. Keep
    // every primary control inside the display's safe area.
    if (const auto *display =
            juce::Desktop::getInstance().getDisplays().getDisplayForRect(
                getScreenBounds())) {
      const auto safeInsets = display->safeAreaInsets;
#if JUCE_IOS
      // A performance surface must work identically in both landscape
      // directions. Use the larger horizontal inset on both sides so the
      // Dynamic Island/camera housing can never cover a primary control.
      const int horizontalSafe =
          juce::jmax(safeInsets.getLeft(), safeInsets.getRight());
      area = juce::BorderSize<int>(safeInsets.getTop(), horizontalSafe,
                                   safeInsets.getBottom(), horizontalSafe)
                 .subtractedFrom(area);
#else
      area = safeInsets.subtractedFrom(area);
#endif
    }
  }
#endif
  const bool setlistDrawerSupported = supportsSetlistDrawer(*this);
  if (!setlistDrawerSupported)
    isSetlistDrawerOpen = false;

  if (topBar == nullptr || waveformBar == nullptr || channelGridPanel == nullptr ||
      songListPanel == nullptr || setlistHandlePanel == nullptr)
    return;

  if (settingsOverlay != nullptr)
    settingsOverlay->setBounds(getLocalBounds());

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
  } else if (useTouchOptimisedControls()) {
#if JUCE_ANDROID
    // Keep Android's touch targets comfortable without consuming as much
    // vertical space as the tablet profile used by iOS.
    topBarH = 60;
#else
    topBarH = 78;
#endif
  }

  topBar->setBounds(area.removeFromTop(topBarH));
  int waveH = isWaveformHeightExpanded ? waveformExpandedH : waveformH;
  // When the arrangement editor is open, the WaveformBar draws a parts toolbar
  // on top. Grow the waveform area by that amount so the toolbar doesn't
  // compress the waveform+minimap, and so the mixer / song list below get
  // pushed down instead of being covered by the expanded bar.
  if (waveformBarPtr)
    waveH += waveformBarPtr->getExtraHeightForHostLayout();
  auto waveformArea = area.removeFromTop(waveH);
  waveformBar->setBounds(waveformArea);

  auto mainArea = area;
  const int handleW = minimalPhoneProfile ? 0 : 14;
  int setlistWidth =
      juce::jlimit(setlistMinW, setlistMaxW,
                   static_cast<int>(mainArea.getWidth() * setlistRatio));
  if (minimalPhoneProfile || isSetlistCollapsed ||
      (setlistDrawerSupported && isSetlistDrawerOpen))
    setlistWidth = 0;

  auto leftSetlistArea = mainArea.removeFromLeft(setlistWidth);
  auto handleArea = mainArea.removeFromLeft(handleW);
  auto rightChannelsArea = mainArea;
  if (!minimalPhoneProfile) {
    rightChannelsArea.removeFromLeft(2);
    rightChannelsArea.removeFromRight(contentInset);
  }
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

  if (minimalPhoneProfile) {
    // Keep the phone focused on playback and the channel mixer. The setlist
    // and pads remain available in the desktop/tablet layouts.
    songListPanel->setVisible(false);
    songListPanel->setBounds({});
    setlistHandlePanel->setVisible(false);
    setlistHandlePanel->setBounds({});
    if (setlistDrawerScrim)
      setlistDrawerScrim->setVisible(false);
    if (padsHandlePanel)
      padsHandlePanel->setVisible(false);
    if (padsComponent)
      padsComponent->setVisible(false);
  } else {
    setlistHandlePanel->setVisible(true);
    if (padsHandlePanel)
      padsHandlePanel->setVisible(true);
  }

  if (setlistDrawerSupported && isSetlistDrawerOpen) {
    const int drawerTop = topBarH + contentInset;
    const int drawerBottomInset = contentInset;
    const int drawerWidth = juce::jlimit(
        useTouchOptimisedControls() ? 360 : 340,
        useTouchOptimisedControls() ? 560 : 460,
        static_cast<int>(getWidth() * (useTouchOptimisedControls() ? 0.46f : 0.38f)));
    const auto drawerBounds = juce::Rectangle<int>(
        contentInset, drawerTop,
        juce::jmin(drawerWidth, getWidth() - (2 * contentInset)),
        juce::jmax(1, getHeight() - drawerTop - drawerBottomInset));

    songListPanel->setVisible(true);
    songListPanel->setBounds(drawerBounds);
    setlistDrawerScrim->setBounds(
        juce::Rectangle<int>(0, topBarH, getWidth(),
                             juce::jmax(1, getHeight() - topBarH)));
    setlistDrawerScrim->setDrawerBounds(
        drawerBounds.translated(0, -topBarH));
    setlistDrawerScrim->setVisible(true);
    setlistDrawerScrim->toFront(false);
    songListPanel->toFront(false);

    setlistHandlePanel->setBounds(
        juce::Rectangle<int>(drawerBounds.getRight(), drawerBounds.getY() + 8,
                             handleW, juce::jmax(48, drawerBounds.getHeight() - 16)));
    setlistHandlePanel->toFront(false);
  } else if (isSetlistCollapsed) {
    songListPanel->setVisible(false);
    songListPanel->setBounds({});
    setlistDrawerScrim->setVisible(false);
  } else {
    songListPanel->setVisible(true);
    auto setlistBounds = leftSetlistArea;
    setlistBounds.removeFromLeft(contentInset);
    setlistBounds.removeFromBottom(contentInset);
    setlistBounds.removeFromRight(3);
    songListPanel->setBounds(setlistBounds);
    setlistDrawerScrim->setVisible(false);
    setlistHandlePanel->setBounds(handleArea.reduced(0, 8));
  }

  if (!(setlistDrawerSupported && isSetlistDrawerOpen))
    setlistHandlePanel->setBounds(handleArea.reduced(0, 8));
  setlistHandlePanel->setCollapsed(isSetlistCollapsed);

  if (reauthPromptModal != nullptr) {
    reauthPromptModal->setBounds(
        juce::Rectangle<int>(560, 230).withCentre(getLocalBounds().getCentre()));
    reauthPromptModal->toFront(false);
  }

}

void MTControlsAudioProcessorEditor::beginExportCurrentSongMix(
    const juce::String &formatExtension) {
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
}

void MTControlsAudioProcessorEditor::runSongMixExport(const juce::File &outputFile) {
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
  SongArrangementSnapshot exportArrangement;
  bool exportArrangementCustomized = false;
  if (SongLibraryStore::getSongArrangement(
          currentLoadedSong.id, exportArrangement,
          exportArrangementCustomized)) {
    for (const auto &part : exportArrangement.parts) {
      if (!part.skipPlayback)
        request.guideSections.push_back(
            {part.name, part.startSec, part.utilityCue});
    }
  }
  if (request.guideSections.empty()) {
    for (const auto &part : currentLoadedSong.arrangement.parts) {
      if (!part.skipPlayback)
        request.guideSections.push_back(
            {part.name, part.startSec, part.utilityCue});
    }
  }
  for (size_t i = 0; i < currentLoadedSong.stems.size(); ++i) {
    const auto &stem = currentLoadedSong.stems[i];
    juce::File file(stem.storedPath);
    if (!file.existsAsFile())
      file = juce::File(stem.sourcePath);
    if (!file.existsAsFile())
      continue;
    request.stems.push_back({file.getFullPathName(), p.getStemVolume((int) i),
                             p.getStemGain((int) i), p.isStemMuted((int) i),
                             p.isStemSoloed((int) i),
                             stemPriorityForStripOrder(stem) <= 1,
                             isStemPitchShiftable(stem)});
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
    waveformBarPtr->setSongLoadingState(true, 0.0, "Preparando exportación… 0%");

  std::thread([safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this), request] {
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
    juce::MessageManager::callAsync([safe, result] {
      if (safe == nullptr)
        return;
      safe->mixExportBusy = false;
      if (safe->waveformBarPtr != nullptr)
        safe->waveformBarPtr->setExportBusy(false);
      if (safe->waveformBarPtr != nullptr)
        safe->waveformBarPtr->clearSongLoadingState();
      juce::NativeMessageBox::showMessageBoxAsync(
          result.ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
          result.ok ? "Mix exportado" : "Error al exportar",
          result.ok ? "Archivo guardado en:\n" + result.outputFile.getFullPathName()
                    : (result.error.isNotEmpty() ? result.error : "No se pudo crear el archivo de audio."));
    });
  }).detach();
}

bool MTControlsAudioProcessorEditor::keyPressed(const juce::KeyPress &key) {
  if (!mainUiInitialized)
    return true;

  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
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
