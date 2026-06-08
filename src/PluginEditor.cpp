#include "PluginEditor.h"
#include "PluginEditorSupport.h"
#include "audio/AudioDeviceMonitor.h"
#include "data/AppSettings.h"
#include "data/AssetBootstrapManager.h"
#include "data/AuthSessionApi.h"
#include "data/LocalAuthCallbackServer.h"
#include "data/Localization.h"
#include "data/SessionAuthLogger.h"
#include "data/SessionPersistence.h"
#include "data/SongData.h"
#include "data/SongLibraryStore.h"
#include "ui/UiCommon.h"
#include "ui/components/PadsHandlePanel.h"
#include "ui/components/ReauthPromptModal.h"
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

namespace {
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
  const int authW = juce::jmin(kAuthWindowW, userArea.getWidth());
  const int authH = juce::jmin(kAuthWindowH, userArea.getHeight());
  setSize(authW, authH);
  setResizable(false, false);
  setResizeLimits(authW, authH, authW, authH);

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
    if (welcomeModal)
      welcomeModal->setVisible(false);
    if (reauthPromptModal)
      reauthPromptModal->setVisible(false);
    setResizable(true, true);
    setResizeLimits(kMinWindowW, kMinWindowH, 32000, 32000);

    const auto userArea =
        juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
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
      [&p] { return p.getSongEndSeconds(); },
      [&p] { return p.getLoadedSongKey(); },
      [this] { return currentLoadedSong.id; });
  topBarRaw->setOnMetronomeEnabledChanged([this](const bool enabled) {
    persistClickDynMetronomeEnabledForActiveSong(enabled);
  });
  topBarRaw->setMidiLearnBindingLabelProvider(midiLearnBindingLabelForTarget);

  auto waveformBarImpl = std::make_unique<WaveformBar>();
  auto *waveformBarRaw = waveformBarImpl.get();
  waveformBar = std::move(waveformBarImpl);
  waveformBarRaw->setMidiLearnBindingLabelProvider(
      midiLearnBindingLabelForTarget);
  waveformBarRaw->setTransportProviders(
      [&p] { return p.getSongCurrentPositionSeconds(); },
      [&p] { return p.getSongLengthSeconds(); },
      [&p] { return p.isSongPlaying(); });
  waveformBarRaw->setSongWindowProviders(
      [&p] { return p.getSongStartSeconds(); },
      [&p] { return p.getSongEndSeconds(); });
  waveformBarRaw->setSeekCallback([&p](double targetSeconds) {
    p.seekSongTo(targetSeconds);
  });
  waveformBarRaw->setOnArrangementPartJumpRequest([&p](int partIndex) {
    const int activeLoopPartIndex = p.getArrangementPartLoopIndex();
    if (activeLoopPartIndex >= 0 && activeLoopPartIndex != partIndex)
      p.clearArrangementPartLoop();
    p.requestArrangementPartJump(partIndex);
  });
  waveformBarRaw->setOnArrangementPartLoopToggleRequest(
      [&p](int partIndex) { p.toggleArrangementPartLoop(partIndex); });
  waveformBarRaw->setPendingArrangementPartProvider(
      [&p] { return p.getPendingArrangementPartJumpIndex(); });
  waveformBarRaw->setActiveArrangementLoopPartProvider(
      [&p] { return p.getArrangementPartLoopIndex(); });
  waveformBarRaw->setOnEditToggle([this](bool expanded) {
    isWaveformExpanded = expanded;
    auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
    processorRef.setArrangementEditingActive(expanded);
    resized();
    repaint();
  });
  waveformBarRaw->setOnArrangementChanged(
      [this, &p](const SongArrangementSnapshot &arrangement,
                 bool isCustomArrangement) {
        if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
          return;
        SongLibraryStore::updateSongArrangement(
            currentLoadedSong.id, arrangement, isCustomArrangement);
        // Las partes son marcadores visuales/editables y no deben alterar
        // el flujo de reproduccion normal de la cancion.
        p.setSongArrangement(arrangement, false);
      });
  waveformBarRaw->setOnArrangementReset([this, &p] {
    if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
      return;
    SongLibraryStore::resetSongArrangement(currentLoadedSong.id);
    SongArrangementSnapshot arrangement;
    bool isCustomArrangement = false;
    if (SongLibraryStore::getSongArrangement(currentLoadedSong.id, arrangement,
                                             isCustomArrangement)) {
      p.setSongArrangement(arrangement, false);
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
    maybePersistSongWindowForActiveSong();
    if (channelGridRaw != nullptr)
      channelGridRaw->flushPendingPersistenceNow();
    p.setMetronomeEnabled(false);
    songLoadBusy = true;
    SongLoadUiScope loadUiScope{songLoadBusy, waveformBarRaw};

    auto pumpUi = [] {
      if (auto *mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(14);
    };

    auto pushLoadUi = [waveformBarRaw, pumpUi](double overall01,
                                               const juce::String &phase) {
      if (waveformBarRaw == nullptr)
        return;
      waveformBarRaw->setSongLoadingState(true, overall01, phase);
      pumpUi();
    };

    juce::StringArray stemPaths;
    std::vector<bool> clickGuideMask;
    std::vector<bool> guideMask;
    std::vector<bool> pitchShiftMask;
    int clickStemIndex = -1;
    ImportedSongData loadedSong = song;
    loadedSong.stems.clear();
    loadedSong.stems.reserve(song.stems.size());
    std::vector<ImportedStemData> resolvedStems;
    resolvedStems.reserve(song.stems.size());

    for (const auto &stem : song.stems) {
      if (!stem.include)
        continue;

      juce::File file(stem.storedPath);
#if JUCE_WINDOWS
      // Legacy fallback: older imports stored ALAC .m4a, which can fail to
      // decode on some Windows setups. Prefer original source when available.
      if (file.existsAsFile() && file.hasFileExtension(".m4a")) {
        const juce::File source(stem.sourcePath);
        if (source.existsAsFile())
          file = source;
      }
#endif
      if (!file.existsAsFile())
        file = juce::File(stem.sourcePath);
      if (file.existsAsFile()) {
        ImportedStemData loadedStem = stem;
        loadedStem.storedPath = file.getFullPathName();
        loadedStem.include = true;
        resolvedStems.push_back(std::move(loadedStem));
      }
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

    const bool loaded = p.loadSongStems(
        stemPaths, song.title, song.key, static_cast<double>(song.bpm),
        [&pushLoadUi](double stemPhase01) {
          pushLoadUi(0.55 * stemPhase01,
                     L10n::tr(u8"Leyendo audio de las pistas...",
                              u8"Reading stem audio..."));
        });

    if (!loaded) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon,
          L10n::tr("Error de carga", "Load error"),
          L10n::tr(u8"No se pudieron cargar los stems de esta canción.",
                   "Couldn't load audio stems for this song."));
      waveformBarRaw->setWaveformBins({});
      waveformBarRaw->setSongRhythm(0.0, {});
      p.setStemClickGuideMask({});
      p.setStemGuideMask({});
      p.setMetronomeClickStemIndex(-1);
      channelGridRaw->setSong(ImportedSongData{});
      hasCurrentLoadedSong = false;
      currentLoadedSong = ImportedSongData{};
      resetTrackedSongWindow();
      return false;
    }

    p.setStemClickGuideMask(clickGuideMask);
    p.setStemGuideMask(guideMask);
    p.setStemPitchShiftMask(pitchShiftMask);
    p.setMetronomeClickStemIndex(clickStemIndex);
    p.setDynamicGuideLanguage(AppSettings::getLanguage());

    DynamicGuideSettings dynamicGuideSettings;
    bool dynamicGuideCustomized = false;
    if (!SongLibraryStore::getActiveSetlistDynamicGuide(
            loadedSong.id, dynamicGuideSettings, dynamicGuideCustomized)) {
      dynamicGuideSettings = DynamicGuideSettings{};
      dynamicGuideCustomized = false;
    }
    p.setDynamicGuideSettings(dynamicGuideSettings);

    DynamicGuideMixSnapshot dynamicGuideMix;
    if (!SongLibraryStore::getActiveSetlistDynamicGuideMix(loadedSong.id,
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

    ClickDynMixSnapshot clickDynMix;
    if (!SongLibraryStore::getActiveSetlistClickDynMix(loadedSong.id,
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

    SongMixSnapshot mixSnapshot;
    if (SongLibraryStore::getActiveSetlistMix(loadedSong.id, mixSnapshot)) {
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
      }
    }

    SongWindowSnapshot windowSnapshot;
    bool isCustomWindow = false;
    if (SongLibraryStore::getActiveSetlistSongWindow(loadedSong.id, windowSnapshot,
                                                      isCustomWindow) &&
        isCustomWindow) {
      p.setSongStartSeconds(windowSnapshot.startSec);
      p.setSongEndSeconds(windowSnapshot.endSec);
      p.setSongFadeOutSeconds(windowSnapshot.fadeOutSec);
    }

    currentLoadedSong = loadedSong;
    hasCurrentLoadedSong = true;
    AppSettings::setLastSelectedSongId(currentLoadedSong.id);
    applyRoutingDefaultsToSong(currentLoadedSong);
    trackSongWindow(currentLoadedSong.id, p.getSongStartSeconds(),
                    p.getSongEndSeconds(), p.getSongFadeOutSeconds());

    channelGridRaw->setSong(loadedSong);
    channelGridRaw->setGuideSourceMode(
        dynamicGuideSettings.enabled
            ? ChannelGridPanel::GuideSourceMode::DynamicBySections
            : ChannelGridPanel::GuideSourceMode::SongGuide);
    channelGridRaw->setGuideWarningMessage({});
    currentSongIndex = songIndex;

    auto cachedBins = SongLibraryStore::loadWaveformCache(loadedSong.id);
    if (cachedBins.empty()) {
      pushLoadUi(0.58, L10n::tr(u8"Generando vista de forma de onda...",
                                u8"Generating waveform preview..."));
      cachedBins = buildMixedWaveformBins(
          loadedSong, 1400, [&pushLoadUi](const double wf01) {
            pushLoadUi(0.58 + 0.40 * wf01,
                       L10n::tr(u8"Generando vista de forma de onda...",
                                u8"Generating waveform preview..."));
          });
      SongLibraryStore::saveWaveformCache(loadedSong.id, cachedBins);
    } else {
      pushLoadUi(0.98, L10n::tr(u8"Cargando forma de onda en caché...",
                                u8"Loading cached waveform..."));
    }

    pushLoadUi(1.0, L10n::tr(u8"Finalizando...", u8"Finishing..."));
    waveformBarRaw->setWaveformBins(std::move(cachedBins));
    waveformBarRaw->setSongRhythm(static_cast<double>(loadedSong.bpm),
                                  loadedSong.timeSignature);
    SongArrangementSnapshot arrangement;
    bool isCustomArrangement = false;
    if (!SongLibraryStore::getSongArrangement(loadedSong.id, arrangement,
                                              isCustomArrangement)) {
      arrangement.parts.clear();
      isCustomArrangement = false;
    }
    waveformBarRaw->setArrangement(arrangement, isCustomArrangement);
    waveformBarRaw->setTempoMap(loadedSong.tempoMap);
    p.setMetronomeOutputRouting(clickDynMix.outputChannel,
                                clickDynMix.outputWidth);
    p.setMetronomeEnabled(clickDynMix.metronomeEnabled);
    trackClickDynMetronomeEnabled(clickDynMix.metronomeEnabled);
    p.setSongArrangement(arrangement, false);
    p.setArrangementEditingActive(false);
    return true;
  };

  songListRaw->setOnSongActivated(
      [this, loadSong, &p](const ImportedSongData &song, int songIndex) {
        deferredStartupSongLoadPending = false;
        deferredStartupSongIndex = -1;
        deferredStartupSongLoadAtMs = 0;
        if (p.isSongPlaying())
          p.stopSong();
        (void)loadSong(song, songIndex);
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

  channelGridRaw->setOnLibrarySongLoadRequested(
      [this, songListRaw, loadSong, &p](const ImportedSongData &song,
                                        int songIndex) {
        juce::ignoreUnused(songIndex);
        deferredStartupSongLoadPending = false;
        deferredStartupSongIndex = -1;
        deferredStartupSongLoadAtMs = 0;
        if (p.isSongPlaying())
          p.stopSong();
        const int setlistIndex =
            songListRaw != nullptr ? songListRaw->getSongIndexById(song.id) : -1;
        const int indexForState = setlistIndex >= 0 ? setlistIndex : -1;
        (void)loadSong(song, indexForState);
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

  auto prevSong = [songListRaw, loadSongByIndex, resolveCurrentSongIndex]() {
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
    loadSongByIndex(target);
  };

  auto nextSong = [songListRaw, loadSongByIndex, resolveCurrentSongIndex]() {
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
    const int target = juce::jlimit(0, count - 1, index + 1);
    if (target == index)
      return;
    loadSongByIndex(target);
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
    isSetlistCollapsed = !isSetlistCollapsed;
    resized();
    repaint();
  };
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
  const int maxW = juce::jmax(kMinWindowW, userArea.getWidth() - kScreenMargin);
  const int maxH = juce::jmax(kMinWindowH, userArea.getHeight() - kScreenMargin);
  const int w = juce::jlimit(kMinWindowW, maxW, kDefaultWindowW);
  const int h = juce::jlimit(kMinWindowH, maxH, kDefaultWindowH);
  setResizable(true, true);
  setResizeLimits(kMinWindowW, kMinWindowH, 32000, 32000);
  setSize(w, h);
  applyResponsiveLayoutProfile(resolveResponsiveLayoutProfile());
  if (!autoSetlistCollapseInitialized) {
    isSetlistCollapsed = false;
    autoSetlistCollapseInitialized = true;
  }

  if (AppSettings::getExpandWindowOnLaunch()) {
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(
             this)] {
          if (safe != nullptr)
            expandTopLevelWindowToPrimaryWorkArea(safe.getComponent());
        });
  }

  setWantsKeyboardFocus(true);
  grabKeyboardFocus();

  wasSongPlaying = p.isSongPlaying();
  lastObservedSongPositionSec = p.getSongCurrentPositionSeconds();
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
  const juce::String apiBaseUrl = getApiBaseUrl();
  const juce::String loginUrl =
      apiBaseUrl + "/login?redirect_uri=" + urlEncode(redirectUri) +
      "&state=" + urlEncode(authExpectedState) +
      "&deviceId=" + urlEncode(deviceId);

  if (auto *modal = dynamic_cast<WelcomeModal *>(welcomeModal.get()))
    modal->setStatusText("Esperando autenticacion en el navegador...", false);
  if (auto *reauth = dynamic_cast<ReauthPromptModal *>(reauthPromptModal.get()))
    reauth->showWaitingForCallback();

  juce::URL(loginUrl).launchInDefaultBrowser();
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
          "Subscription status is not trial/active with a valid plan";
    }
  }

  if (!activeSession.allowed) {
    juce::String message =
        userMessageForAccessDenial(activeSession.reasonCode, activeSession.reason);
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
  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = L10n::tr(u8"Configuración", "Settings");
  options.dialogBackgroundColour = UITheme::appBg;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = false;
  auto &proc = static_cast<MTControlsAudioProcessor &>(processor);
  const auto routingDefaults = AppSettings::getRoutingDefaults();
  const auto audioCtx = getStandaloneAudioContext();
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
      [] {},
      SettingsPanel::SessionSummary{activeSession.uid, activeSession.email,
                                    activeSession.plan,
                                    activeSession.subscriptionStatus,
                                    activeSession.expiresAt,
                                    activeSession.allowed,
                                    activeSession.reasonCode,
                                    activeSession.reason,
                                    activeSession.licenseKey},
      [this] { handleLogout(); }, routingDefaults,
      proc.getAvailableOutputChannelCount(),
      [this](const juce::String &deviceKey,
             const std::vector<CategoryRoutingDefaults> &next) {
        AppSettings::setRoutingDefaultsForDevice(deviceKey, next);
        auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
        processorRef.refreshPadsRoutingFromSettings();
        if (hasCurrentLoadedSong)
          applyRoutingDefaultsToSong(currentLoadedSong);
      },
      audioCtx.manager, audioCtx.maxInputs, audioCtx.maxOutputs);
  panel->setSize(760, 620);
  panel->refreshThemeColours();
  options.content.setOwned(panel);

  if (auto *window = options.launchAsync()) {
    settingsWindow = window;
    settingsPanelRaw = panel;
    window->centreAroundComponent(parent != nullptr ? parent : this, 760, 620);
  }
}

void MTControlsAudioProcessorEditor::showWelcomeOnlyMode() {
  setResizable(false, false);
  setResizeLimits(kAuthWindowW, kAuthWindowH, kAuthWindowW, kAuthWindowH);
  setSize(kAuthWindowW, kAuthWindowH);
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
  if (welcomeModal)
    welcomeModal->setVisible(true);
  if (reauthPromptModal)
    reauthPromptModal->setVisible(false);
  mainUiInitialized = false;
  autoSetlistCollapseInitialized = false;
  resized();
}

void MTControlsAudioProcessorEditor::handleLogout() {
  SessionAuthLogger::write("handleLogout");
  if (settingsWindow != nullptr) {
    settingsWindow->setVisible(false);
    settingsWindow->exitModalState(0);
    settingsWindow = nullptr;
    settingsPanelRaw = nullptr;
  }

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
        "Subscription status is not trial/active with a valid plan";
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
  processorRef.setMetronomeOutputRouting(clickRouting.baseChannel,
                                         clickRouting.width);

  int stemIndex = 0;
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;
    if (!juce::isPositiveAndBelow(stemIndex, stemCount))
      break;

    const auto routing = routingForCategory(defaults, stem.category);
    processorRef.setStemOutputWidth(stemIndex, routing.width);
    processorRef.setStemOutputChannel(stemIndex, routing.baseChannel);
    ++stemIndex;
  }
  processorRef.refreshPadsRoutingFromSettings();
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
    AppSettings::setMidiBindings(processorRef.getMidiBindings());
    juce::ignoreUnused(learnedBinding);
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

  juce::String missingDynamicGuideSection;
  if (processorRef.consumeDynamicGuideMissingSectionWarning(
          missingDynamicGuideSection)) {
    if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get())) {
      constexpr const char *kDynamicGuideLoadWarningPrefix =
          "__dynamic_guide_load_error__:";
      juce::String message;
      if (missingDynamicGuideSection.startsWith(kDynamicGuideLoadWarningPrefix)) {
        const auto detail =
            missingDynamicGuideSection.fromFirstOccurrenceOf(
                kDynamicGuideLoadWarningPrefix, false, false);
        message = L10n::tr(u8"No se pudieron cargar los audios de guía dinámica: ",
                           "Couldn't load dynamic guide audio: ") +
                  detail;
      } else {
        message = L10n::tr(u8"Sin audio de guía para sección: ",
                           "Missing guide audio for section: ") +
                  missingDynamicGuideSection;
      }
      grid->setGuideWarningMessage(message);
    }
  }

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

  if (wasSongPlaying && !isPlayingNow && songListPanelRaw != nullptr &&
      hasCurrentLoadedSong && currentLoadedSong.id.isNotEmpty()) {
    const double startSec = processorRef.getSongStartSeconds();
    const double endSec = processorRef.getSongEndSeconds();
    const bool reachedSongEnd =
        endSec > startSec && lastObservedSongPositionSec >= (endSec - 0.12);
    const bool wrappedToStart = currentPosSec <= (startSec + 0.12);

    if (reachedSongEnd && wrappedToStart) {
      const int nextIndex =
          songListPanelRaw->resolveAutoPlayNextIndex(currentLoadedSong.id);
      if (juce::isPositiveAndBelow(nextIndex, songListPanelRaw->getSongCount()) &&
          loadSongByIndexAction) {
        loadSongByIndexAction(nextIndex, true);
      }
    }
  }

  if (isPlayingNow)
    lastObservedSongPositionSec = currentPosSec;
  else if (!wasSongPlaying)
    lastObservedSongPositionSec = currentPosSec;

  wasSongPlaying = isPlayingNow;
}

MTControlsAudioProcessorEditor::~MTControlsAudioProcessorEditor() {
  stopTimer();
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
    return;
  }

  auto area = getLocalBounds();
  updateResponsiveLayoutProfile();

  if (topBar == nullptr || waveformBar == nullptr || channelGridPanel == nullptr ||
      songListPanel == nullptr || setlistHandlePanel == nullptr)
    return;

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

  topBar->setBounds(area.removeFromTop(topBarH));
  const int waveH = isWaveformExpanded ? waveformExpandedH : waveformH;
  auto waveformArea = area.removeFromTop(waveH);
  waveformBar->setBounds(waveformArea);

  auto mainArea = area;
  constexpr int handleW = 14;
  int setlistWidth =
      juce::jlimit(setlistMinW, setlistMaxW,
                   static_cast<int>(mainArea.getWidth() * setlistRatio));
  if (isSetlistCollapsed)
    setlistWidth = 0;

  auto leftSetlistArea = mainArea.removeFromLeft(setlistWidth);
  auto handleArea = mainArea.removeFromLeft(handleW);
  auto rightChannelsArea = mainArea;
  rightChannelsArea.removeFromLeft(2);
  rightChannelsArea.removeFromTop(contentInset);
  rightChannelsArea.removeFromRight(contentInset);
  rightChannelsArea.removeFromBottom(contentInset);
  padsDrawerBackgroundBounds = {};

  if (padsHandlePanel) {
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

  if (isSetlistCollapsed) {
    songListPanel->setVisible(false);
    songListPanel->setBounds({});
  } else {
    songListPanel->setVisible(true);
    auto setlistBounds = leftSetlistArea;
    setlistBounds.removeFromLeft(contentInset);
    setlistBounds.removeFromTop(contentInset);
    setlistBounds.removeFromBottom(contentInset);
    setlistBounds.removeFromRight(3);
    songListPanel->setBounds(setlistBounds);
  }

  setlistHandlePanel->setBounds(handleArea.reduced(0, 8));
  setlistHandlePanel->setCollapsed(isSetlistCollapsed);

  if (reauthPromptModal != nullptr) {
    reauthPromptModal->setBounds(
        juce::Rectangle<int>(560, 230).withCentre(getLocalBounds().getCentre()));
    reauthPromptModal->toFront(false);
  }

}

bool MTControlsAudioProcessorEditor::keyPressed(const juce::KeyPress &key) {
  if (!mainUiInitialized)
    return true;

  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
  const auto keyCode = key.getKeyCode();
  const auto modifiers = key.getModifiers();
  const bool altDown = modifiers.isAltDown();

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
