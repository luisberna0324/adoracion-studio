#include "PluginEditor.h"
#include "data/AppSettings.h"
#include "data/Localization.h"
#include "data/SongData.h"
#include "data/SongLibraryStore.h"
#include "ui/UiCommon.h"
#include "ui/components/SetlistHandlePanel.h"
#include "ui/panels/ChannelGridPanel.h"
#include "ui/panels/SettingsPanel.h"
#include "ui/panels/SongListPanel.h"
#include "ui/panels/TopBar.h"
#include "ui/panels/WaveformBar.h"
#include "audio/AudioDeviceMonitor.h"
#include <algorithm>

#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace {
constexpr int kTopBarH = 60;
constexpr int kWaveformH = 250;
constexpr int kWaveformExpandedH = 250;

constexpr int kDefaultWindowW = 1200;
constexpr int kDefaultWindowH = 760;
/** Mínimo para que la UI siga siendo usable (setlist + canales). */
constexpr int kMinWindowW = 800;
constexpr int kMinWindowH = 560;
constexpr int kScreenMargin = 40;

struct StandaloneAudioContext {
  juce::AudioDeviceManager *manager = nullptr;
  int maxInputs = 0;
  int maxOutputs = 0;
};

StandaloneAudioContext getStandaloneAudioContext() {
#if JucePlugin_Build_Standalone
  if (auto *holder = juce::StandalonePluginHolder::getInstance()) {
    StandaloneAudioContext ctx;
    ctx.manager = &holder->deviceManager;
    ctx.maxInputs = holder->getNumInputChannels();
    ctx.maxOutputs = holder->getNumOutputChannels();
    return ctx;
  }
#endif
  return {};
}

juce::String getCurrentDeviceKey(const StandaloneAudioContext &ctx) {
  if (ctx.manager == nullptr)
    return {};
  auto *device = ctx.manager->getCurrentAudioDevice();
  if (device == nullptr)
    return {};
  const juce::String typeName = ctx.manager->getCurrentAudioDeviceType();
  const juce::String deviceName = device->getName();
  if (typeName.isEmpty() || deviceName.isEmpty())
    return {};
  return typeName + "|" + deviceName;
}

#if JucePlugin_Build_Standalone
void hideStandaloneOptionsButton(juce::Component *fromComponent) {
  if (fromComponent == nullptr)
    return;

  auto *topLevel = fromComponent->getTopLevelComponent();
  if (topLevel == nullptr)
    return;

  auto hideRecursively = [](auto &&self, juce::Component *component) -> void {
    if (component == nullptr)
      return;

    if (auto *button = dynamic_cast<juce::Button *>(component)) {
      const auto text = button->getButtonText().trim();
      if (text.equalsIgnoreCase("Options") ||
          text.equalsIgnoreCase("Opciones")) {
        button->setVisible(false);
        button->setEnabled(false);
      }
    }

    for (int i = 0; i < component->getNumChildComponents(); ++i)
      self(self, component->getChildComponent(i));
  };

  hideRecursively(hideRecursively, topLevel);
}
#endif

int categoryIndexForName(const juce::String &category) {
  const auto needle = category.trim();
  if (needle.isEmpty())
    return -1;

  int otherIndex = -1;
  for (size_t i = 0; i < kTrackCategories.size(); ++i) {
    const juce::String name = kTrackCategories[i];
    if (name.equalsIgnoreCase("Other"))
      otherIndex = static_cast<int>(i);
    if (name.equalsIgnoreCase(needle))
      return static_cast<int>(i);
  }
  return otherIndex;
}

CategoryRoutingDefaults routingForCategory(
    const std::vector<CategoryRoutingDefaults> &defaults,
    const juce::String &category) {
  const int idx = categoryIndexForName(category);
  if (idx < 0 || idx >= static_cast<int>(defaults.size()))
    return CategoryRoutingDefaults{};
  return defaults[static_cast<size_t>(idx)];
}

} // namespace

MTControlsAudioProcessorEditor::MTControlsAudioProcessorEditor(
    MTControlsAudioProcessor &p)
    : AudioProcessorEditor(p) {
  currentThemeId = AppSettings::getThemePresetId();
  applyTheme(static_cast<ThemePreset>(currentThemeId));

  auto topBarImpl = std::make_unique<TopBar>(p);
  auto *topBarRaw = topBarImpl.get();
  topBar = std::move(topBarImpl);
  topBarRaw->setTransportProviders(
      [&p] { return p.isSongPlaying(); },
      [&p] { return p.getSongCurrentPositionSeconds(); },
      [&p] { return p.getSongEndSeconds(); },
      [&p] { return p.getLoadedSongKey(); });

  auto waveformBarImpl = std::make_unique<WaveformBar>();
  auto *waveformBarRaw = waveformBarImpl.get();
  waveformBar = std::move(waveformBarImpl);
  waveformBarRaw->setTransportProviders(
      [&p] { return p.getSongCurrentPositionSeconds(); },
      [&p] { return p.getSongLengthSeconds(); });
  waveformBarRaw->setSeekCallback([&p](double targetSeconds) {
    p.seekSongTo(targetSeconds);
  });
  waveformBarRaw->setOnArrangementPartJumpRequest(
      [&p](int partIndex) { p.requestArrangementPartJump(partIndex); });
  waveformBarRaw->setPendingArrangementPartProvider(
      [&p] { return p.getPendingArrangementPartJumpIndex(); });
  waveformBarRaw->setOnEditToggle([this](bool expanded) {
    isWaveformExpanded = expanded;
    resized();
    repaint();
  });
  waveformBarRaw->setOnArrangementChanged(
      [this, &p](const SongArrangementSnapshot &arrangement,
                 bool isCustomArrangement) {
        if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
          return;
        SongLibraryStore::updateArrangementForActiveSetlist(
            currentLoadedSong.id, arrangement, isCustomArrangement);
        // Las partes son marcadores visuales/editables y no deben alterar
        // el flujo de reproducción normal de la canción.
        p.setSongArrangement(arrangement, false);
      });
  waveformBarRaw->setOnArrangementReset([this, &p] {
    if (!hasCurrentLoadedSong || currentLoadedSong.id.isEmpty())
      return;
    SongLibraryStore::resetArrangementForActiveSetlist(currentLoadedSong.id);
    SongArrangementSnapshot arrangement;
    bool isCustomArrangement = false;
    if (SongLibraryStore::getActiveSetlistArrangement(currentLoadedSong.id,
                                                      arrangement,
                                                      isCustomArrangement)) {
      p.setSongArrangement(arrangement, false);
    }
  });

  auto channelGridImpl =
      std::make_unique<ChannelGridPanel>(stripLookAndFeel, p);
  auto *channelGridRaw = channelGridImpl.get();
  channelGridPanel = std::move(channelGridImpl);

  p.getAudioDeviceBridge().setOnDeviceConnected(
      [&p, this, channelGridRaw](const AudioDeviceMonitor::DeviceInfo &info) {
        if (!info.isMultichannel)
          return;
        p.getAudioDeviceBridge().setAvailableOutputChannelCount(
            juce::jmax(2, info.numOutputChannels));
        if (channelGridRaw != nullptr)
          channelGridRaw->refreshStemRoutingVisibility();
      });

  refreshTopBarTheme = [topBarRaw, waveformBarRaw, channelGridRaw] {
    if (topBarRaw)
      topBarRaw->refreshThemeColours();
    if (waveformBarRaw)
      waveformBarRaw->refreshThemeColours();
    if (channelGridRaw)
      channelGridRaw->refreshThemeColours();
  };

  auto songListImpl = std::make_unique<SongListPanel>();
  auto *songListRaw = songListImpl.get();
  songListPanel = std::move(songListImpl);
  songListPanelRaw = songListRaw;
  songListRaw->setOnSetlistNameChanged([this](const juce::String &name) {
    if (setlistHandlePanel)
      setlistHandlePanel->setTitle(name);
  });

  auto loadSong = [channelGridRaw, waveformBarRaw, &p,
                   this](const ImportedSongData &song, int songIndex) -> bool {
    juce::StringArray stemPaths;
    std::vector<bool> clickGuideMask;
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
      if (!file.existsAsFile())
        file = juce::File(stem.sourcePath);
      if (file.existsAsFile()) {
        ImportedStemData loadedStem = stem;
        loadedStem.storedPath = file.getFullPathName();
        loadedStem.include = true;
        resolvedStems.push_back(std::move(loadedStem));
      }
    }

    std::stable_sort(resolvedStems.begin(), resolvedStems.end(),
                     [](const ImportedStemData &a, const ImportedStemData &b) {
                       return stemPriorityForStripOrder(a) <
                              stemPriorityForStripOrder(b);
                     });

    for (const auto &loadedStem : resolvedStems) {
      const int stemPriority = stemPriorityForStripOrder(loadedStem);
      const int mappedStemIndex = static_cast<int>(loadedSong.stems.size());
      if (clickStemIndex < 0 && stemPriority == 0)
        clickStemIndex = mappedStemIndex;
      stemPaths.add(loadedStem.storedPath);
      loadedSong.stems.push_back(loadedStem);
      const bool isClickGuide = stemPriority <= 1;
      clickGuideMask.push_back(isClickGuide);
      pitchShiftMask.push_back(isStemPitchShiftable(loadedStem));
    }

    const bool loaded = p.loadSongStems(stemPaths, song.title, song.key,
                                        static_cast<double>(song.bpm));

    if (!loaded) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon,
          L10n::tr("Error de carga", "Load error"),
          L10n::tr("No se pudieron cargar los stems de esta canción.",
                   "Couldn't load audio stems for this song."));
      waveformBarRaw->setWaveformBins({});
      waveformBarRaw->setSongRhythm(0.0, {});
      p.setStemClickGuideMask({});
      p.setMetronomeClickStemIndex(-1);
      channelGridRaw->setSong(ImportedSongData{});
      hasCurrentLoadedSong = false;
      currentLoadedSong = ImportedSongData{};
      return false;
    }

    p.setStemClickGuideMask(clickGuideMask);
    p.setStemPitchShiftMask(pitchShiftMask);
    p.setMetronomeClickStemIndex(clickStemIndex);

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

    currentLoadedSong = loadedSong;
    hasCurrentLoadedSong = true;
    AppSettings::setLastSelectedSongId(currentLoadedSong.id);
    applyRoutingDefaultsToSong(currentLoadedSong);

    channelGridRaw->setSong(loadedSong);
    currentSongIndex = songIndex;

    auto cachedBins = SongLibraryStore::loadWaveformCache(loadedSong.id);
    if (cachedBins.empty()) {
      cachedBins = buildMixedWaveformBins(loadedSong);
      SongLibraryStore::saveWaveformCache(loadedSong.id, cachedBins);
    }

    waveformBarRaw->setWaveformBins(std::move(cachedBins));
    waveformBarRaw->setSongRhythm(static_cast<double>(loadedSong.bpm),
                                  loadedSong.timeSignature);
    SongArrangementSnapshot arrangement;
    bool isCustomArrangement = false;
    if (!SongLibraryStore::getActiveSetlistArrangement(
            loadedSong.id, arrangement, isCustomArrangement)) {
      arrangement.parts.clear();
      isCustomArrangement = false;
    }
    waveformBarRaw->setArrangement(arrangement, isCustomArrangement);
    p.setSongArrangement(arrangement, false);
    return true;
  };

  songListRaw->setOnSongActivated(
      [loadSong](const ImportedSongData &song, int songIndex) {
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
    if (loadSongByIndex(index) && autoPlay)
      p.playSong();
  };

  {
    const auto lastSelectedSongId = AppSettings::getLastSelectedSongId();
    if (lastSelectedSongId.isNotEmpty()) {
      const int index = songListRaw != nullptr
                            ? songListRaw->getSongIndexById(lastSelectedSongId)
                            : -1;
      if (index >= 0)
        loadSongByIndex(index);
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

  setlistHandlePanel = std::make_unique<SetlistHandlePanel>("Setlist A");
  setlistHandlePanel->onToggle = [this] {
    isSetlistCollapsed = !isSetlistCollapsed;
    resized();
    repaint();
  };
  if (songListRaw)
    setlistHandlePanel->setTitle(songListRaw->getActiveSetlistName());

  addAndMakeVisible(*topBar);
  addAndMakeVisible(*waveformBar);
  addAndMakeVisible(*channelGridPanel);
  addAndMakeVisible(*songListPanel);
  addAndMakeVisible(*setlistHandlePanel);

  setWantsKeyboardFocus(true);
  grabKeyboardFocus();

  const auto userArea =
      juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
  const int maxW = juce::jmax(kMinWindowW, userArea.getWidth() - kScreenMargin);
  const int maxH = juce::jmax(kMinWindowH, userArea.getHeight() - kScreenMargin);
  const int w = juce::jlimit(kMinWindowW, maxW, kDefaultWindowW);
  const int h = juce::jlimit(kMinWindowH, maxH, kDefaultWindowH);
  setSize(w, h);

  setResizable(true, true);
  setResizeLimits(kMinWindowW, kMinWindowH, 32000, 32000);

  wasSongPlaying = p.isSongPlaying();
  lastObservedSongPositionSec = p.getSongCurrentPositionSeconds();
  startTimerHz(20);

  if (AppSettings::getExpandWindowOnLaunch()) {
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(
             this)] {
          if (safe != nullptr)
            expandTopLevelWindowToPrimaryWorkArea(safe.getComponent());
        });
  }

#if JucePlugin_Build_Standalone
  juce::MessageManager::callAsync(
      [safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(
           this)] {
        if (safe != nullptr)
          hideStandaloneOptionsButton(safe.getComponent());
      });
#endif
}

void MTControlsAudioProcessorEditor::showSettings(juce::Rectangle<int>,
                                                  juce::Component *parent) {
  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = L10n::tr("Configuración", "Settings");
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
      [] {}, routingDefaults, proc.getAvailableOutputChannelCount(),
      [this](const juce::String &deviceKey,
             const std::vector<CategoryRoutingDefaults> &next) {
        AppSettings::setRoutingDefaultsForDevice(deviceKey, next);
        if (hasCurrentLoadedSong)
          applyRoutingDefaultsToSong(currentLoadedSong);
      },
      audioCtx.manager, audioCtx.maxInputs, audioCtx.maxOutputs);
  panel->setSize(760, 620);
  panel->refreshThemeColours();
  options.content.setOwned(panel);

  if (auto *window = options.launchAsync()) {
    window->centreAroundComponent(parent != nullptr ? parent : this, 760, 620);
  }
}

void MTControlsAudioProcessorEditor::applyThemePreset(int presetId) {
  currentThemeId = juce::jlimit(1, 3, presetId);
  AppSettings::setThemePresetId(currentThemeId);
  applyTheme(static_cast<ThemePreset>(currentThemeId));
  if (refreshTopBarTheme)
    refreshTopBarTheme();
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
}

void MTControlsAudioProcessorEditor::timerCallback() {
  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);
  const bool isPlayingNow = processorRef.isSongPlaying();
  const double currentPosSec = processorRef.getSongCurrentPositionSeconds();

  if (songListPanelRaw != nullptr) {
    const bool hasPlayingSong = isPlayingNow && hasCurrentLoadedSong &&
                                currentLoadedSong.id.isNotEmpty();
    songListPanelRaw->setNowPlayingSong(
        hasPlayingSong ? currentLoadedSong.id : juce::String(), hasPlayingSong);
  }

  if (wasSongPlaying && !isPlayingNow && songListPanelRaw != nullptr &&
      hasCurrentLoadedSong && currentLoadedSong.id.isNotEmpty()) {
    const double startSec = processorRef.getSongStartSeconds();
    const double endSec = processorRef.getSongEndSeconds();
    const bool reachedSongEnd =
        endSec > startSec && lastObservedSongPositionSec >= (endSec - 0.12);
    const bool wrappedToStart = currentPosSec <= (startSec + 0.12);

    if (reachedSongEnd && wrappedToStart &&
        songListPanelRaw->isAutoPlayNextEnabledForSong(currentLoadedSong.id)) {
      const int currentIndex =
          songListPanelRaw->getSongIndexById(currentLoadedSong.id);
      const int nextIndex = currentIndex + 1;
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
  loadSongByIndexAction = {};
  songListPanelRaw = nullptr;
  auto &proc = static_cast<MTControlsAudioProcessor &>(processor);
  proc.getAudioDeviceBridge().setOnDeviceConnected({});
  proc.getAudioDeviceBridge().setOnDeviceDisconnected({});
}

void MTControlsAudioProcessorEditor::paint(juce::Graphics &g) {
  drawStudioBackground(g, getLocalBounds().toFloat());
}

void MTControlsAudioProcessorEditor::resized() {
  auto area = getLocalBounds();

  topBar->setBounds(area.removeFromTop(kTopBarH));
  const int waveH =
      isWaveformExpanded ? kWaveformExpandedH : kWaveformH;
  auto waveformArea = area.removeFromTop(waveH);
  waveformBar->setBounds(waveformArea);

  auto mainArea = area;
  constexpr int handleW = 28;
  int setlistWidth =
      juce::jlimit(280, 460, static_cast<int>(mainArea.getWidth() * 0.35f));
  if (isSetlistCollapsed)
    setlistWidth = 0;

  auto leftSetlistArea = mainArea.removeFromLeft(setlistWidth);
  auto handleArea = mainArea.removeFromLeft(handleW);
  auto rightChannelsArea = mainArea.reduced(8);
  channelGridPanel->setBounds(rightChannelsArea);

  if (isSetlistCollapsed) {
    songListPanel->setVisible(false);
    songListPanel->setBounds({});
  } else {
    songListPanel->setVisible(true);
    songListPanel->setBounds(leftSetlistArea.reduced(8));
  }

  setlistHandlePanel->setBounds(handleArea.reduced(2, 8));
  setlistHandlePanel->setCollapsed(isSetlistCollapsed);
}

bool MTControlsAudioProcessorEditor::keyPressed(const juce::KeyPress &key) {
  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);

  if (key == juce::KeyPress::spaceKey) {
    processorRef.toggleSongPlayPause();
    return true;
  }

  if (key == juce::KeyPress::leftKey) {
    if (prevSongAction)
      prevSongAction();
    return true;
  }

  if (key == juce::KeyPress::rightKey) {
    if (nextSongAction)
      nextSongAction();
    return true;
  }

  if (key == juce::KeyPress::escapeKey) {
    processorRef.stopSong();
    return true;
  }

  return false;
}
