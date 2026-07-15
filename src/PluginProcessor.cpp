#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "data/AppSettings.h"
#include "data/AssetBootstrapManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
constexpr double kMinPlaybackTempoBpm = 20.0;
constexpr double kMaxPlaybackTempoBpm = 300.0;
constexpr double kMinPlaybackTempoRatio = 0.75;
constexpr double kMaxPlaybackTempoRatio = 2.0;
constexpr const char *kDynamicGuideLoadWarningPrefix =
    "__dynamic_guide_load_error__:";

int tempoQualityProfileForRatio(const double ratio) {
  if (ratio < 0.85)
    return 0; // Heavy slow-down profile.
  if (ratio < 0.999)
    return 1; // Moderate slow-down profile.
  return 2;   // Normal/speed-up profile.
}

int clickDivisionMaskToSubdivision(const int divisionMask) {
  const int mask = divisionMask & 0x07;
  if ((mask & 0x04) != 0)
    return 4;
  if ((mask & 0x02) != 0)
    return 2;
  return 1;
}

TimeStretchProcessor::ContentType stemContentTypeForPitchShiftable(
    const bool pitchShiftable) {
  return pitchShiftable ? TimeStretchProcessor::ContentType::Melodic
                        : TimeStretchProcessor::ContentType::Percussive;
}

juce::String buildDeviceKeyFromManager(juce::AudioDeviceManager &manager) {
  auto *currentDevice = manager.getCurrentAudioDevice();
  const juce::String typeName = manager.getCurrentAudioDeviceType();
  const juce::String deviceName =
      currentDevice != nullptr ? currentDevice->getName() : juce::String{};
  if (typeName.isEmpty() || deviceName.isEmpty())
    return {};
  return typeName + "|" + deviceName;
}

juce::File getPadsSourceFolderRomanVizcaino() {
  return juce::File::getSpecialLocation(
             juce::File::SpecialLocationType::userHomeDirectory)
      .getChildFile("Downloads")
      .getChildFile("Pads worship esential Roman  vizcaino");
}

juce::File getPadsSourceFolderEmpirikFallback() {
  return juce::File::getSpecialLocation(
             juce::File::SpecialLocationType::userHomeDirectory)
      .getChildFile("Downloads")
      .getChildFile("PADS CONTINUOS EMPIRIK");
}

juce::File getMetronomeClickSamplesFolder() {
  return AssetBootstrapManager::getClicksPackDirectory();
}

juce::String metronomeClickStyleToName(MetronomeClickStyle style) {
  switch (style) {
  case MetronomeClickStyle::Woodblock:
    return "Woodblock";
  case MetronomeClickStyle::Cowbell:
    return "Cowbell";
  case MetronomeClickStyle::Digital:
    return "Digital";
  case MetronomeClickStyle::Gentle:
    return "Gentle";
  case MetronomeClickStyle::Percussive:
    return "Percussive";
  case MetronomeClickStyle::Saw:
    return "Saw";
  case MetronomeClickStyle::Blip:
    return "Blip";
  case MetronomeClickStyle::Classic:
  default:
    return "Classic";
  }
}

PadsEngine::CrossfadePreset toPadsEngineCrossfadePreset(
    const PadsCrossfadePreset preset) {
  switch (preset) {
  case PadsCrossfadePreset::Fast:
    return PadsEngine::CrossfadePreset::Fast;
  case PadsCrossfadePreset::Long:
    return PadsEngine::CrossfadePreset::Long;
  case PadsCrossfadePreset::Medium:
  default:
    return PadsEngine::CrossfadePreset::Medium;
  }
}

juce::String arrangementSectionKey(const juce::String &partName) {
  const auto canonical = canonicalArrangementSectionName(partName);
  if (canonical.isNotEmpty()) {
    auto sectionKey = DynamicGuideEngine::normalizeSectionKey(canonical);
    const auto normalizedName =
        DynamicGuideEngine::normalizeSectionKey(partName);
    int suffixStart = normalizedName.length();
    while (suffixStart > 0 &&
           juce::CharacterFunctions::isDigit(normalizedName[suffixStart - 1])) {
      --suffixStart;
    }
    if (suffixStart > 0 && suffixStart < normalizedName.length() &&
        juce::CharacterFunctions::isWhitespace(
            normalizedName[suffixStart - 1])) {
      sectionKey << " " << normalizedName.substring(suffixStart);
    }
    return sectionKey;
  }
  return DynamicGuideEngine::normalizeSectionKey(partName);
}

juce::String arrangementCountSectionKey(const juce::String &partName) {
  const auto key = DynamicGuideEngine::normalizeSectionKey(partName);
  if (key.isEmpty())
    return {};

  bool allDigits = true;
  for (int i = 0; i < key.length(); ++i) {
    if (!juce::CharacterFunctions::isDigit(key[i])) {
      allDigits = false;
      break;
    }
  }
  if (allDigits)
    return key;

  if (key == "one" || key == "uno")
    return "1";
  if (key == "two" || key == "dos")
    return "2";
  if (key == "three" || key == "tres")
    return "3";
  if (key == "four" || key == "cuatro")
    return "4";
  if (key == "five" || key == "cinco")
    return "5";
  if (key == "six" || key == "seis")
    return "6";
  if (key == "seven" || key == "siete")
    return "7";
  return {};
}

bool isDynamicGuideUtilityCueKey(const juce::String &rawKey) {
  const auto key = DynamicGuideEngine::normalizeSectionKey(rawKey);
  static const std::array<const char *, 26> kUtilityCueKeys = {{
      "a capella", "ad lib", "all", "all in", "bass", "big", "big ending",
      "break", "breakdown", "build", "drums", "drums in", "guitar", "hits",
      "hold", "key", "key change down", "key change up", "keys", "last",
      "last time", "slowly", "slowly build", "softly", "swell",
      "worship freely"}};
  for (const auto *candidate : kUtilityCueKeys) {
    if (key == candidate)
      return true;
  }
  return false;
}

bool sectionTriggersOnEntry(const juce::String &sectionKey,
                            const juce::String &sectionName) {
  return arrangementCountSectionKey(sectionKey).isNotEmpty() ||
         arrangementCountSectionKey(sectionName).isNotEmpty() ||
         isDynamicGuideUtilityCueKey(sectionKey) ||
         isDynamicGuideUtilityCueKey(sectionName);
}

juce::String baseArrangementSectionKey(const juce::String &partName) {
  const auto canonical = canonicalArrangementSectionName(partName);
  return canonical.isNotEmpty()
             ? DynamicGuideEngine::normalizeSectionKey(canonical)
             : DynamicGuideEngine::normalizeSectionKey(partName);
}

} // namespace

#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

MTControlsAudioProcessor::MTControlsAudioProcessor()
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), false)
              .withOutput("Output", juce::AudioChannelSet::discreteChannels(maxStemCount), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
  playbackFormatManager.registerBasicFormats();
  const int readWorkerCount =
      juce::jlimit(2, 4, juce::SystemStats::getNumCpus() - 1);
  stemReadThreads.reserve(static_cast<size_t>(readWorkerCount));
  for (int i = 0; i < readWorkerCount; ++i) {
    auto worker = std::make_unique<juce::TimeSliceThread>(
        "StemReadPool-" + juce::String(i + 1));
    worker->startThread(juce::Thread::Priority::normal);
    stemReadThreads.push_back(std::move(worker));
  }
  resetStemControls(0);
  standaloneAudioDeviceBridge.setAvailableOutputChannelCount(2);
  padsEngine.setCrossfadePreset(
      toPadsEngineCrossfadePreset(AppSettings::getPadsCrossfadePreset()));
  refreshPadsRoutingFromSettings();

  const auto settings = AppSettings::getPadsPackSettings();
  if (!settings.status.isEmpty()) {
    const juce::SpinLock::ScopedLockType lock(padsStateLock);
    padsPackStatusText = settings.status;
  } else {
    const juce::SpinLock::ScopedLockType lock(padsStateLock);
    padsPackStatusText = "Not loaded";
  }
  midiBindings = AppSettings::getMidiBindings();
  {
    const juce::ScopedLock lock(midiBindingsLock);
    publishMidiBindingsLocked();
  }
  for (auto &value : midiLastSourceValues)
    value = -1;
  {
    juce::String clickError;
    if (AssetBootstrapManager::ensureClicksPackReady(&clickError)) {
      const auto clickFolder = getMetronomeClickSamplesFolder();
      loadMetronomeClickSamplesFromFolder(
          clickFolder, metronomeClickStyleToName(AppSettings::getMetronomeClickStyle()),
          &clickError);
    }
  }
  dynamicGuideLanguageCode.store(
      AppSettings::getLanguage() == AppLanguage::English ? 1 : 0,
      std::memory_order_relaxed);
  dynamicGuideEnabledState.store(dynamicGuideSettings.enabled,
                                 std::memory_order_relaxed);
}

MTControlsAudioProcessor::~MTControlsAudioProcessor() {
  stemTracks.clear();
  outgoingStemTracks.clear();
  for (auto &worker : stemReadThreads)
    worker->stopThread(3000);
  stemReadThreads.clear();
}

void MTControlsAudioProcessor::prepareToPlay(double sampleRate,
                                             int samplesPerBlock) {
  const int realtimeBlockCapacity =
      juce::jmax(samplesPerBlock, maxRealtimeBlockSize);
  juce::dsp::ProcessSpec spec;
  spec.sampleRate = sampleRate;
  spec.maximumBlockSize = static_cast<juce::uint32>(realtimeBlockCapacity);
  spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());
  gain.prepare(spec);
  gain.setRampDurationSeconds(0.02);
  dryMixBuffer.setSize(getTotalNumOutputChannels(), realtimeBlockCapacity);
  mixEngine.prepare(sampleRate, realtimeBlockCapacity,
                    getTotalNumOutputChannels());
  outgoingMixEngine.prepare(sampleRate, realtimeBlockCapacity,
                            getTotalNumOutputChannels());
  stemRoutingInputBuffer.setSize(2, (realtimeBlockCapacity * 2) + 8);
  stemRoutingTempStorage.setSize(2, realtimeBlockCapacity);
  metronomeRenderStorage.setSize(2, realtimeBlockCapacity);
  padsRenderStorage.setSize(juce::jmax(2, getTotalNumOutputChannels()),
                            realtimeBlockCapacity);
  dynamicGuideRenderStorage.setSize(2, realtimeBlockCapacity);
  outgoingStemRenderStorage.setSize(getTotalNumOutputChannels(),
                                    realtimeBlockCapacity);
  padsEngine.prepare(sampleRate, realtimeBlockCapacity);
  dynamicGuideEngine.prepare(sampleRate);
  metronomeLatencyDelay.prepare(2, kMaxRoutingPitchLatencySamples);
  metronomeLatencyDelay.setDelay(0);
  dynamicGuideLatencyDelay.prepare(2, kMaxRoutingPitchLatencySamples);
  dynamicGuideLatencyDelay.setDelay(0);
  padsEngine.setEnabled(padsEnabledState.load(std::memory_order_relaxed));
  standaloneAudioDeviceBridge.setAvailableOutputChannelCount(getTotalNumOutputChannels());
  refreshPadsRoutingFromSettings();
  stemOutputRoutingState.normalizeForDevice(getTotalNumOutputChannels());
  setMetronomeOutputRouting(
      metronomeOutputChannel.load(std::memory_order_relaxed),
      metronomeOutputWidth.load(std::memory_order_relaxed));
  setDynamicGuideOutputRouting(
      dynamicGuideOutputChannelState.load(std::memory_order_relaxed),
      dynamicGuideOutputWidthState.load(std::memory_order_relaxed));
  for (int i = 0; i < maxStemCount; ++i) {
    const int channel = stemOutputRoutingState.getStemOutputChannel(i);
    stemOutputChannels[static_cast<size_t>(i)].store(channel, std::memory_order_relaxed);
    const int width = stemOutputRoutingState.getStemOutputWidth(i);
    stemOutputWidths[static_cast<size_t>(i)].store(width, std::memory_order_relaxed);
    stemPitchProcessors[static_cast<size_t>(i)].prepare(
        sampleRate, realtimeBlockCapacity, 2);
    stemPitchProcessors[static_cast<size_t>(i)].setContentType(
        TimeStretchProcessor::ContentType::Melodic);
    stemPitchProcessors[static_cast<size_t>(i)].setPitchSemiTones(pitchActiveSemiTones);
    stemPitchProcessors[static_cast<size_t>(i)].setTempoRatio(playbackTempoRatioActive);
    stemPitchDelayLines[static_cast<size_t>(i)].prepare(2, kMaxRoutingPitchLatencySamples);
    stemPitchDelayLines[static_cast<size_t>(i)].setDelay(0);
    outgoingStemPitchProcessors[static_cast<size_t>(i)].prepare(
        sampleRate, realtimeBlockCapacity, 2);
    outgoingStemPitchProcessors[static_cast<size_t>(i)].setContentType(
        TimeStretchProcessor::ContentType::Melodic);
    outgoingStemPitchProcessors[static_cast<size_t>(i)].setPitchSemiTones(pitchActiveSemiTones);
    outgoingStemPitchProcessors[static_cast<size_t>(i)].setTempoRatio(playbackTempoRatioActive);
    outgoingStemPitchDelayLines[static_cast<size_t>(i)].prepare(2, kMaxRoutingPitchLatencySamples);
    outgoingStemPitchDelayLines[static_cast<size_t>(i)].setDelay(0);
  }
  routingPitchLatencySamples = 0;
  outgoingRoutingPitchLatencySamples = 0;

  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    playbackSampleRate = sampleRate;
    playbackBlockSize = samplesPerBlock;
    transportClock.prepare(sampleRate);
    rebuildSectionScheduler();
    metronome.prepare(sampleRate);
    for (auto &track : stemTracks)
      track->prepareToPlay(samplesPerBlock, sampleRate);
    for (auto &track : outgoingStemTracks)
      track->prepareToPlay(samplesPerBlock, sampleRate);
  }

  // Start the background diagnostics logger.
  diagnostics.startBackgroundLogger(
      juce::File::getSpecialLocation(
          juce::File::SpecialLocationType::userApplicationDataDirectory)
          .getChildFile("Logs/MTControls"));
}

void MTControlsAudioProcessor::releaseResources() {
  diagnostics.stopBackgroundLogger();
  mixEngine.reset();
  outgoingMixEngine.reset();
  padsEngine.releaseResources();
  dynamicGuideEngine.reset();
  dynamicGuideLatencyDelay.reset();
  const juce::SpinLock::ScopedLockType lock(stemListLock);
  for (auto &track : stemTracks)
    track->releaseResources();
  for (auto &track : outgoingStemTracks)
    track->releaseResources();
}

void MTControlsAudioProcessor::flushRoutingTimeProcessors() noexcept {
  for (int i = 0; i < maxStemCount; ++i) {
    stemPitchProcessors[static_cast<size_t>(i)].reset();
    stemPitchDelayLines[static_cast<size_t>(i)].reset();
    outgoingStemPitchProcessors[static_cast<size_t>(i)].reset();
    outgoingStemPitchDelayLines[static_cast<size_t>(i)].reset();
  }
  routingPitchLatencySamples = 0;
  outgoingRoutingPitchLatencySamples = 0;
}

void MTControlsAudioProcessor::flushAllTimeProcessingState() noexcept {
  mixEngine.reset();
  outgoingMixEngine.reset();
  flushRoutingTimeProcessors();
}

bool MTControlsAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  const auto input = layouts.getMainInputChannelSet();
  const auto output = layouts.getMainOutputChannelSet();
  if (input != juce::AudioChannelSet::mono() &&
      input != juce::AudioChannelSet::stereo())
    return false;
  const int outputChannels = output.size();
  return outputChannels >= 1 && outputChannels <= maxStemCount;
}

void MTControlsAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                            juce::MidiBuffer &midiMessages) {
  struct CallbackActivityGuard {
    explicit CallbackActivityGuard(std::atomic<bool> &flagIn) : flag(flagIn) {
      flag.store(true, std::memory_order_release);
    }
    ~CallbackActivityGuard() {
      flag.store(false, std::memory_order_release);
    }
    std::atomic<bool> &flag;
  } callbackActivityGuard(audioCallbackActive);

  juce::ScopedNoDenormals noDenormals;
  processMidiMapping(midiMessages);
  if (buffer.getNumSamples() > maxRealtimeBlockSize) {
    buffer.clear();
    return;
  }
  const int callbackSamples = buffer.getNumSamples();
  const int callbackChannels = buffer.getNumChannels();
  juce::AudioBuffer<float> stemRoutingTempBuffer(
      stemRoutingTempStorage.getArrayOfWritePointers(), 2, callbackSamples);
  juce::AudioBuffer<float> metronomeRenderBuffer(
      metronomeRenderStorage.getArrayOfWritePointers(), 2, callbackSamples);
  juce::AudioBuffer<float> padsRenderBuffer(
      padsRenderStorage.getArrayOfWritePointers(),
      juce::jmax(2, callbackChannels), callbackSamples);
  juce::AudioBuffer<float> dynamicGuideRenderBuffer(
      dynamicGuideRenderStorage.getArrayOfWritePointers(), 2, callbackSamples);
  juce::AudioBuffer<float> outgoingStemRenderBuffer(
      outgoingStemRenderStorage.getArrayOfWritePointers(), callbackChannels,
      callbackSamples);

  // --- Diagnostics: overall block start time ---
  const int64_t blockStartTick = juce::Time::getHighResolutionTicks();

  for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels();
       ++i)
    buffer.clear(i, 0, buffer.getNumSamples());

  dynamicGuideMeterLevelLeft.store(0.0f, std::memory_order_relaxed);
  dynamicGuideMeterLevelRight.store(0.0f, std::memory_order_relaxed);

  bool usedSeparatedMix = false;
  float windowFadeStart = 1.0f;
  float windowFadeEnd = 1.0f;
  bool windowFadeActive = false;
  bool stopAtEnd = false;
  double stopAtEndSec = 0.0;
  bool transitionToNextArrangementPart = false;
  int nextArrangementPartIndex = -1;
  double nextArrangementPartStartSec = 0.0;
  bool muteSectionInstrumentsThisBlock = false;
  bool skipArrangementPartThisBlock = false;
  bool dynamicGuideShouldRender = false;
  bool dynamicGuideAnnouncementsActive = false;
  int dynamicGuideOutputChannelOneBased = 1;
  int dynamicGuideOutputWidth = 2;
  float dynamicGuideOutputGain = 1.0f;
  bool dynamicGuideOutputAudible = false;
  bool seekAppliedThisBlock = false;
  int stemOutputLatencyThisBlock = 0;
  int sourceFramesThisBlock = buffer.getNumSamples();
  double renderedSourceStartSec = 0.0;

  if (songLoaded.load(std::memory_order_relaxed)) {
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const bool isPlaying = getTransportState() == TransportState::playing;
    const bool dynamicGuideEnabled =
        dynamicGuideEnabledState.load(std::memory_order_relaxed);
    const bool arrangementEditingActive =
        arrangementEditingActiveState.load(std::memory_order_relaxed);
    dynamicGuideAnnouncementsActive = dynamicGuideEnabled;
    const int targetPitch =
        pitchTargetSemiTones.load(std::memory_order_relaxed);
    const double targetTempoRatio = getPlaybackTempoRatio();
    if (targetPitch != pitchActiveSemiTones) {
      pitchActiveSemiTones = targetPitch;
      mixEngine.setPitchSemiTones(pitchActiveSemiTones);
      outgoingMixEngine.setPitchSemiTones(pitchActiveSemiTones);
      // Routing processors accept live pitch updates below. Do not flush their
      // overlap/FIFO state: doing so inserts a re-prime silence.
    }
    if (!juce::approximatelyEqual(targetTempoRatio, playbackTempoRatioActive)) {
      const double previousTempoRatio = playbackTempoRatioActive;
      playbackTempoRatioActive = targetTempoRatio;
      mixEngine.setTempoRatio(playbackTempoRatioActive);
      outgoingMixEngine.setTempoRatio(playbackTempoRatioActive);
      const bool crossedUnity =
          (previousTempoRatio < 1.0 && playbackTempoRatioActive >= 1.0) ||
          (previousTempoRatio > 1.0 && playbackTempoRatioActive <= 1.0);
      const bool profileChanged =
          tempoQualityProfileForRatio(previousTempoRatio) !=
          tempoQualityProfileForRatio(playbackTempoRatioActive);

      // With pitch active, tempo profile jumps can leave per-stem queues with
      // stale timing history. Hard-resync to keep percussion aligned.
      if (pitchActiveSemiTones != 0 && (crossedUnity || profileChanged)) {
        flushAllTimeProcessingState();
      } else {
        routingPitchLatencySamples = 0;
        outgoingRoutingPitchLatencySamples = 0;
      }
    }
    sourceFramesThisBlock = mixEngine.requiredInputFrames(numSamples);

    constexpr double lockWaitMs = 0.0;

    // Apply pending seek
    if (seekPending.exchange(false, std::memory_order_acquire)) {
      const double seekTarget = seekTargetSeconds.load(std::memory_order_relaxed);
      const juce::int64 seekSamples =
          playworship::audio::TransportClock::secondsToFrames(
              seekTarget, playbackSampleRate);
      const juce::int64 sourceSeekSamples =
          mapArrangementFrameToSourceFrame(seekSamples);
      for (auto& track : stemTracks)
        track->setNextReadPosition(sourceSeekSamples);

      // Seeking changes source position abruptly: flush all time-domain
      // processors so no stale pre-seek audio survives in different groups.
      flushAllTimeProcessingState();

      globalSamplePosition.store(seekSamples, std::memory_order_relaxed);
      transportClock.seek(seekSamples);
      songCurrentPositionSeconds.store(seekTarget, std::memory_order_relaxed);

      syncMetronomeToSourceSeconds(
          playworship::audio::TransportClock::framesToSeconds(
              sourceSeekSamples, playbackSampleRate));
      seekAppliedThisBlock = true;
    }

    const int stemCount = static_cast<int>(stemTracks.size());
    if (isPlaying && arrangementCustomActive.load(std::memory_order_relaxed) &&
        sectionScheduler.size() > 0) {
      const auto currentFrame = transportClock.getPositionFrames();
      const int sectionIndex = sectionScheduler.findSection(currentFrame);
      playworship::audio::SectionScheduler::Section currentSection;
      if (sectionScheduler.getSection(sectionIndex, currentSection) &&
          currentFrame >= currentSection.startFrame &&
          currentFrame < currentSection.endFrame) {
        muteSectionInstrumentsThisBlock = currentSection.muteInstruments;
        skipArrangementPartThisBlock = currentSection.skipPlayback;
      }
    }

    const int clickStemIndexLocal =
        metronomeClickStemIndex.load(std::memory_order_relaxed);
    const bool hasRealClickStem = juce::isPositiveAndBelow(clickStemIndexLocal,
                                                           stemCount);
    const bool virtualMetronomeSoloActive =
        !hasRealClickStem &&
        metronomeSoloState.load(std::memory_order_relaxed);
    const bool dynamicGuideSoloActive =
        dynamicGuideAnnouncementsActive &&
        dynamicGuideSoloState.load(std::memory_order_relaxed);

    // Evaluate global solo state (includes virtual metronome solo when no real
    // click stem exists so stems mute and only click remains audible).
    bool anySoloActive = virtualMetronomeSoloActive || dynamicGuideSoloActive;
    for (int i = 0; i < stemCount; ++i) {
      if (stemSoloStates[static_cast<size_t>(i)].load(
              std::memory_order_relaxed)) {
        anySoloActive = true;
        break;
      }
    }

    // Keep natural stem balance (no auto-normalization by stem count).
    const float mixGain = 1.0f;

    // Inject lock-free mix state into StemTracks and update UI meters
    for (int i = 0; i < stemCount; ++i) {
      auto& track = stemTracks[static_cast<size_t>(i)];
      const bool isClickGuideStem =
          stemClickGuideStates[static_cast<size_t>(i)].load(
              std::memory_order_relaxed);
      const bool mutedBySection =
          skipArrangementPartThisBlock ||
          (muteSectionInstrumentsThisBlock && !isClickGuideStem);
      track->setMuted(
          mutedBySection ||
          stemMuteStates[static_cast<size_t>(i)].load(std::memory_order_relaxed));
      track->setSoloed(stemSoloStates[static_cast<size_t>(i)].load(std::memory_order_relaxed));
      track->setVolume(juce::jlimit(0.0f, 1.0f, stemVolumes[static_cast<size_t>(i)].load(std::memory_order_relaxed)));
      track->setTrim(juce::jlimit(0.0f, 2.0f, stemGains[static_cast<size_t>(i)].load(std::memory_order_relaxed)));
      track->setGlobalMixState(mixGain, anySoloActive);
      
      // Update UI meter level (computed in the PREVIOUS getNextAudioBlock call)
      stemLevels[static_cast<size_t>(i)].store(isPlaying ? track->getMagnitude() : 0.0f,
                                               std::memory_order_relaxed);
    }

    if (dynamicGuideAnnouncementsActive) {
      dynamicGuideOutputChannelOneBased = getDynamicGuideOutputChannel();
      dynamicGuideOutputWidth =
          juce::jlimit(1, 2, getDynamicGuideOutputWidth());
      if (numChannels < 2)
        dynamicGuideOutputWidth = 1;
      if (dynamicGuideOutputWidth >= 2 &&
          dynamicGuideOutputChannelOneBased >= numChannels) {
        dynamicGuideOutputChannelOneBased = juce::jmax(1, numChannels - 1);
      }
      dynamicGuideOutputGain =
          juce::jlimit(0.0f, 1.0f,
                       dynamicGuideVolumeState.load(std::memory_order_relaxed)) *
          juce::jlimit(0.0f, 2.0f,
                       dynamicGuideGainState.load(std::memory_order_relaxed));
      const bool guideMuted =
          dynamicGuideMutedState.load(std::memory_order_relaxed);
      const bool guideSoloed =
          dynamicGuideSoloState.load(std::memory_order_relaxed);
      dynamicGuideOutputAudible = !guideMuted && (!anySoloActive || guideSoloed);
      dynamicGuideShouldRender = dynamicGuideOutputAudible &&
                                 dynamicGuideOutputGain > 0.0001f;
    }

    for (int i = stemCount; i < maxStemCount; ++i)
      stemLevels[static_cast<size_t>(i)].store(0.0f, std::memory_order_relaxed);

    bool routingActive = false;
    for (int i = 0; i < stemCount; ++i) {
      const int assignedChannel =
          stemOutputChannels[static_cast<size_t>(i)].load(std::memory_order_relaxed);
      const int assignedWidth = juce::jlimit(
          1, 2, stemOutputWidths[static_cast<size_t>(i)].load(
                    std::memory_order_relaxed));
      // Engage explicit routing not only when using channels beyond 1,
      // but also when any stem is configured as mono on a stereo-capable bus.
      if (assignedChannel > 1 || (numChannels > 1 && assignedWidth == 1)) {
        routingActive = true;
        break;
      }
    }

    const float fadeTargetLocalForMix =
        fadeTarget.load(std::memory_order_relaxed);
    if (isPlaying) {
      const bool customArrangement =
          arrangementCustomActive.load(std::memory_order_relaxed);
      const double sourceLength =
          loadedSongLengthSeconds.load(std::memory_order_relaxed);
      const double length =
          customArrangement
              ? juce::jmax(sourceLength, arrangementTotalLengthSeconds.load(
                                             std::memory_order_relaxed))
              : sourceLength;
      double windowStartSec = songStartSeconds.load(std::memory_order_relaxed);
      double windowEndSec =
          songEndSeconds.load(std::memory_order_relaxed);
      if (customArrangement)
        windowEndSec = length;
      else if (windowEndSec <= 0.0 || windowEndSec > length)
        windowEndSec = length;
      double fadeOutSec = songFadeOutSeconds.load(std::memory_order_relaxed);
      const double blockStartSec =
          playbackSampleRate > 0.0
              ? transportClock.getPositionSeconds()
              : 0.0;
      const auto blockStartFrame = transportClock.getPositionFrames();
      const auto blockSourceStartFrame =
          mapArrangementFrameToSourceFrame(blockStartFrame);
      const double blockSourceStartSec =
          playworship::audio::TransportClock::framesToSeconds(
              blockSourceStartFrame, playbackSampleRate);
      renderedSourceStartSec = blockSourceStartSec;
      const double blockEndSec =
          playbackSampleRate > 0.0
              ? blockStartSec +
                    (static_cast<double>(sourceFramesThisBlock) /
                     playbackSampleRate)
              : blockStartSec;

      if (stemCount > 0) {
        const auto currentSourceFrame =
            stemTracks.front()->getNextReadPosition();
        const auto deltaFrames =
            currentSourceFrame > blockSourceStartFrame
                ? currentSourceFrame - blockSourceStartFrame
                : blockSourceStartFrame - currentSourceFrame;
        const auto thresholdFrames =
            static_cast<juce::int64>(juce::jmax(4, sourceFramesThisBlock + 4));
        if (deltaFrames > thresholdFrames) {
          for (auto &track : stemTracks)
            track->setNextReadPosition(blockSourceStartFrame);
          flushAllTimeProcessingState();
          seekAppliedThisBlock = true;
        }
      }

      const int realtimeSectionCount = sectionScheduler.size();
      int mappedPartIndex = -1;
      if (realtimeSectionCount > 0) {
        mappedPartIndex =
            findArrangementPartIndexForArrangementTime(blockStartSec);
        if (juce::isPositiveAndBelow(mappedPartIndex, realtimeSectionCount)) {
          arrangementCurrentPartIndex.store(mappedPartIndex,
                                            std::memory_order_relaxed);
        }
      }

      const int loopPartIndex =
          arrangementLoopPartIndex.load(std::memory_order_relaxed);
      const int pendingPartIndex =
          arrangementPendingJumpPartIndex.load(std::memory_order_relaxed);
      const auto sectionDecision = sectionScheduler.evaluate(
          transportClock.getPositionFrames(), sourceFramesThisBlock,
          customArrangement, pendingPartIndex, loopPartIndex);
      const bool loopPartActive =
          juce::isPositiveAndBelow(loopPartIndex, realtimeSectionCount);
      bool loopPartEngaged = false;
      double loopStartSec = 0.0;
      double loopEndSec = 0.0;
      if (loopPartActive) {
        playworship::audio::SectionScheduler::Section loopPart;
        if (sectionScheduler.getSection(loopPartIndex, loopPart)) {
          loopStartSec =
              playworship::audio::TransportClock::framesToSeconds(
                  loopPart.startFrame, playbackSampleRate);
          loopEndSec =
              playworship::audio::TransportClock::framesToSeconds(
                  loopPart.endFrame, playbackSampleRate);
          loopPartEngaged =
              blockStartSec >= loopStartSec && blockStartSec < loopEndSec;
        }
      }

      if (!skipArrangementPartThisBlock && dynamicGuideAnnouncementsActive &&
          realtimeSectionCount > 0) {
        const bool wrappedBack =
            dynamicGuideLastBlockStartSec >= 0.0 &&
            blockStartSec + 0.02 < dynamicGuideLastBlockStartSec;
        if (seekAppliedThisBlock || wrappedBack) {
          std::fill(dynamicGuideAnnouncedByPart.begin(),
                    dynamicGuideAnnouncedByPart.end(), false);
          dynamicGuideLastPartIndex = -1;
        }

        const auto triggerPartAnnouncement =
            [this](const int guideTriggerId, const int partIndex) {
          if (!dynamicGuideEngine.triggerForId(guideTriggerId)) {
            dynamicGuideMissingPartIndex.store(partIndex,
                                               std::memory_order_release);
          }
        };

        const bool enteredPlayback = isPlaying && !dynamicGuideWasPlaying;
        if (juce::isPositiveAndBelow(mappedPartIndex,
                                     realtimeSectionCount)) {
          const auto partIdx = static_cast<size_t>(mappedPartIndex);
          playworship::audio::SectionScheduler::Section part;
          const bool hasPart =
              sectionScheduler.getSection(mappedPartIndex, part);
          const bool triggerOnEntry =
              hasPart && part.triggerGuideOnEntry;
          const auto partStartSec =
              playworship::audio::TransportClock::framesToSeconds(
                  part.startFrame, playbackSampleRate);
          const auto partEndSec =
              playworship::audio::TransportClock::framesToSeconds(
                  part.endFrame, playbackSampleRate);
          const bool playheadInsidePart =
              hasPart && blockStartSec + 0.001 >= partStartSec &&
              blockStartSec < partEndSec;

          if (playheadInsidePart &&
              (enteredPlayback || dynamicGuideLastPartIndex != mappedPartIndex ||
               seekAppliedThisBlock)) {
            const bool alreadyAnnounced = dynamicGuideAnnouncedByPart[partIdx];
            const bool shouldTriggerNow = triggerOnEntry && !alreadyAnnounced;
            if (shouldTriggerNow)
              triggerPartAnnouncement(part.guideTriggerId, mappedPartIndex);
            if (shouldTriggerNow)
              dynamicGuideAnnouncedByPart[partIdx] = true;
          }
        }

        juce::ignoreUnused(arrangementEditingActive);
        const int guidePartIndex = sectionScheduler.findGuideTrigger(
            transportClock.getPositionFrames(), sourceFramesThisBlock);
        if (juce::isPositiveAndBelow(
                guidePartIndex, realtimeSectionCount)) {
          const auto guideIndex = static_cast<size_t>(guidePartIndex);
          playworship::audio::SectionScheduler::Section part;
          const bool hasPart =
              sectionScheduler.getSection(guidePartIndex, part);
          const double partStartSec =
              playworship::audio::TransportClock::framesToSeconds(
                  part.startFrame, playbackSampleRate);
          const bool excludedByLoop =
              loopPartEngaged && partStartSec >= windowEndSec;
          if (!excludedByLoop && !dynamicGuideAnnouncedByPart[guideIndex]) {
            if (hasPart)
              triggerPartAnnouncement(part.guideTriggerId, guidePartIndex);
            dynamicGuideAnnouncedByPart[guideIndex] = true;
          }
        }

        dynamicGuideLastPartIndex = mappedPartIndex;
        dynamicGuideLastBlockStartSec = blockStartSec;
        dynamicGuideWasPlaying = true;
      } else {
        dynamicGuideWasPlaying = false;
      }

      const bool skipTransitionBlockedBySeek =
          seekAppliedThisBlock && sectionDecision.skippedSection;

      if (loopPartEngaged) {
        windowStartSec = loopStartSec;
        windowEndSec = loopEndSec;
        fadeOutSec = 0.0;
        arrangementCurrentPartIndex.store(loopPartIndex, std::memory_order_relaxed);
        arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);

        if (sectionDecision.transition && !skipTransitionBlockedBySeek) {
          transitionToNextArrangementPart = true;
          nextArrangementPartIndex = sectionDecision.targetSection;
          nextArrangementPartStartSec =
              playworship::audio::TransportClock::framesToSeconds(
                  sectionDecision.targetFrame, playbackSampleRate);
        }
      } else if (customArrangement) {
        if (sectionDecision.transition && !skipTransitionBlockedBySeek) {
          transitionToNextArrangementPart = true;
          nextArrangementPartIndex = sectionDecision.targetSection;
          nextArrangementPartStartSec =
              playworship::audio::TransportClock::framesToSeconds(
                  sectionDecision.targetFrame, playbackSampleRate);
          arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
        }
      } else if (!customArrangement && realtimeSectionCount > 0 &&
                 juce::isPositiveAndBelow(mappedPartIndex,
                                          realtimeSectionCount)) {
        if (sectionDecision.transition && !skipTransitionBlockedBySeek) {
          transitionToNextArrangementPart = true;
          nextArrangementPartIndex = sectionDecision.targetSection;
          nextArrangementPartStartSec =
              playworship::audio::TransportClock::framesToSeconds(
                  sectionDecision.targetFrame, playbackSampleRate);
          arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
        }
      }

      if (fadeOutSec > 0.0 && windowEndSec > windowStartSec &&
          playbackSampleRate > 0.0) {
        const double fadeStartSec =
            juce::jmax(windowStartSec, windowEndSec - fadeOutSec);
        const auto fadeAt = [fadeStartSec, windowEndSec, fadeOutSec](double t) {
          if (t <= fadeStartSec)
            return 1.0;
          if (t >= windowEndSec)
            return 0.0;
          return (windowEndSec - t) / fadeOutSec;
        };
        windowFadeStart = static_cast<float>(
            juce::jlimit(0.0, 1.0, fadeAt(blockStartSec)));
        windowFadeEnd = static_cast<float>(
            juce::jlimit(0.0, 1.0, fadeAt(blockEndSec)));
        windowFadeActive =
            (windowFadeStart < 0.999f) || (windowFadeEnd < 0.999f);
      }

      if (windowEndSec > 0.0 && blockEndSec >= windowEndSec) {
        stopAtEnd = true;
        stopAtEndSec = windowEndSec;
      }

      if (routingActive) {
        const bool timeProcessActive =
            (pitchActiveSemiTones != 0) ||
            !juce::approximatelyEqual(playbackTempoRatioActive, 1.0);
        const bool tempoActive =
            !juce::approximatelyEqual(playbackTempoRatioActive, 1.0);
        std::array<int, maxStemCount> stemProcessLatencies{};
        std::array<bool, maxStemCount> stemNeedsTimeProcess{};
        std::array<bool, maxStemCount> stemPitchShiftableFlags{};
        int targetLatency = 0;
        if (timeProcessActive) {
          for (int i = 0; i < stemCount; ++i) {
            const size_t stemIndex = static_cast<size_t>(i);
            const bool pitchShiftable =
                stemPitchShiftStates[stemIndex].load(
                    std::memory_order_relaxed);
            stemPitchShiftableFlags[stemIndex] = pitchShiftable;
            const bool shouldProcess = tempoActive || pitchShiftable;
            stemNeedsTimeProcess[stemIndex] = shouldProcess;
            if (!shouldProcess)
              continue;
            auto &proc = stemPitchProcessors[stemIndex];
            if (!proc.isPrepared() || proc.getNumChannels() != 2)
              continue;
            proc.setContentType(
                stemContentTypeForPitchShiftable(pitchShiftable));
            proc.setPitchSemiTones(pitchShiftable ? pitchActiveSemiTones : 0);
            proc.setTempoRatio(playbackTempoRatioActive);
            stemProcessLatencies[stemIndex] = proc.getLatencySamples();
            targetLatency =
                juce::jmax(targetLatency, stemProcessLatencies[stemIndex]);
          }
        }

        if (targetLatency != routingPitchLatencySamples) {
          routingPitchLatencySamples = targetLatency;
        }
        stemOutputLatencyThisBlock = routingPitchLatencySamples;
        for (int i = 0; i < stemCount; ++i) {
          const size_t stemIndex = static_cast<size_t>(i);
          const int desiredDelay =
              juce::jmax(0, routingPitchLatencySamples -
                                stemProcessLatencies[stemIndex]);
          auto &delay = stemPitchDelayLines[stemIndex];
          if (delay.getDelaySamples() != desiredDelay)
            delay.setDelay(desiredDelay);
        }

        if (++stemTimingSnapshotDecimator >=
            kStemTimingSnapshotEveryBlocks) {
          stemTimingSnapshotDecimator = 0;

          AudioDiagnostics::StemTimingEvent stemTimingEvent;
          stemTimingEvent.wallMs =
              static_cast<double>(juce::Time::currentTimeMillis());
          stemTimingEvent.tempoRatio = playbackTempoRatioActive;
          stemTimingEvent.pitchSemiTones = pitchActiveSemiTones;
          stemTimingEvent.stemCount = juce::jlimit(
              0, AudioDiagnostics::StemTimingEvent::kMaxStemMetrics,
              stemCount);
          stemTimingEvent.targetLatencySamples = routingPitchLatencySamples;
          stemTimingEvent.routingActive = true;
          stemTimingEvent.timeProcessActive = timeProcessActive;
          stemTimingEvent.tempoActive = tempoActive;

          for (int i = 0; i < stemTimingEvent.stemCount; ++i) {
            const size_t stemIndex = static_cast<size_t>(i);
            const int latency = stemProcessLatencies[stemIndex];
            const int delay = stemPitchDelayLines[stemIndex].getDelaySamples();
            stemTimingEvent.processLatencySamples[stemIndex] =
                static_cast<int16_t>(
                    juce::jlimit(-32768, 32767, latency));
            stemTimingEvent.delaySamples[stemIndex] =
                static_cast<int16_t>(
                    juce::jlimit(-32768, 32767, delay));

            uint8_t flags = 0;
            if (stemNeedsTimeProcess[stemIndex])
              flags |= 0x1u;
            if (stemPitchShiftableFlags[stemIndex])
              flags |= 0x2u;
            if (!stemPitchShiftableFlags[stemIndex] && tempoActive)
              flags |= 0x4u;
            stemTimingEvent.flags[stemIndex] = flags;
          }

          diagnostics.pushStemTimingEvent(stemTimingEvent);
        }

        buffer.clear();
        const int routingInputSamples =
            timeProcessActive ? sourceFramesThisBlock : numSamples;
        for (int i = 0; i < stemCount; ++i) {
          stemRoutingInputBuffer.clear();
          stemRoutingTempBuffer.clear();
          const size_t stemIndex = static_cast<size_t>(i);
          if (timeProcessActive && stemNeedsTimeProcess[stemIndex]) {
            juce::AudioSourceChannelInfo stemInfo(
                &stemRoutingInputBuffer, 0, routingInputSamples);
            stemTracks[stemIndex]->getNextAudioBlock(stemInfo);
            auto &proc = stemPitchProcessors[stemIndex];
            proc.process(stemRoutingInputBuffer, routingInputSamples,
                         stemRoutingTempBuffer, numSamples);
          } else {
            juce::AudioSourceChannelInfo stemInfo(&stemRoutingTempBuffer, 0,
                                                  numSamples);
            stemTracks[stemIndex]->getNextAudioBlock(stemInfo);
          }
          // Always run the delay line, including at zero delay, so it retains
          // enough history to engage latency compensation without a silent
          // prefill gap when pitch processing is enabled or its latency moves.
          stemPitchDelayLines[stemIndex].process(stemRoutingTempBuffer);
          int assignedWidth = juce::jlimit(
              1, 2, stemOutputWidths[stemIndex].load(
                        std::memory_order_relaxed));
          if (numChannels < 2)
            assignedWidth = 1;

          const int maxBaseChannel = juce::jmax(
              1, numChannels - (assignedWidth >= 2 ? 1 : 0));
          const int assignedChannelOneBased = juce::jlimit(
              1, maxBaseChannel,
              stemOutputChannels[stemIndex].load(std::memory_order_relaxed));
          const int outputIndex =
              juce::jlimit(0, juce::jmax(0, numChannels - 1),
                           assignedChannelOneBased - 1);
          stemOutputRouter.routeStemToOutputChannels(
              stemRoutingTempBuffer, outputIndex, assignedWidth, buffer);
        }
        usedSeparatedMix = false;
      } else {
        const bool isolateGuideDuringFade =
            fadeActive.load(std::memory_order_relaxed) ||
            !juce::approximatelyEqual(fadeGain, fadeTargetLocalForMix);
        const bool useSeparatedMix =
            isolateGuideDuringFade || windowFadeActive ||
            (pitchActiveSemiTones != 0) ||
            !juce::approximatelyEqual(playbackTempoRatioActive, 1.0);
        usedSeparatedMix = useSeparatedMix;

        if (useSeparatedMix) {
          mixEngine.mixStems(stemTracks, stemClickGuideStates.data(),
                             stemPitchShiftStates.data(), stemCount,
                             sourceFramesThisBlock, numChannels);
        } else {
          mixEngine.renderDirect(stemTracks, stemCount, buffer);
        }
      }

      const double currentSec =
          playbackSampleRate > 0.0
              ? transportClock.getPositionSeconds()
              : 0.0;
      songCurrentPositionSeconds.store(currentSec, std::memory_order_relaxed);
      transportClock.advance(sourceFramesThisBlock);
      globalSamplePosition.store(transportClock.getPositionFrames(),
                                 std::memory_order_relaxed);
    } else {
      dynamicGuideWasPlaying = false;
      const auto state = getTransportState();
      if (state == TransportState::paused) {
        const double currentSec =
            playbackSampleRate > 0.0
                ? transportClock.getPositionSeconds()
                : 0.0;
        songCurrentPositionSeconds.store(currentSec, std::memory_order_relaxed);
      } else {
        const double currentSec =
            playbackSampleRate > 0.0
                ? transportClock.getPositionSeconds()
                : 0.0;
        songCurrentPositionSeconds.store(currentSec, std::memory_order_relaxed);
      }
    }

    // --- Diagnostics: record block event ---
    {
      const double blockMs =
          juce::Time::highResolutionTicksToSeconds(
              juce::Time::getHighResolutionTicks() - blockStartTick) * 1000.0;
      const double budgetMs =
          playbackSampleRate > 0.0
              ? (static_cast<double>(numSamples) / playbackSampleRate) * 1000.0
              : 0.0;
      int64_t streamingMisses = 0;
      for (const auto &track : stemTracks)
        streamingMisses += track->getRealtimeReadMissCount();
      diagnostics.pushEvent({juce::Time::currentTimeMillis() * 1.0, blockMs,
                             budgetMs, lockWaitMs, stemCount,
                             streamingMisses, blockMs > budgetMs, isPlaying});
    }
  } else {
    // No song loaded — sin bus de entrada física: silencio (los parámetros
    // inputGain/mix/bypass siguen existiendo por compatibilidad de estado).
    songCurrentPositionSeconds.store(0.0, std::memory_order_relaxed);
    setTransportState(TransportState::stopped);

    auto *bypass = apvts.getRawParameterValue(bypassId);
    const bool isBypassed = bypass->load() > 0.5f;

    if (!isBypassed) {
      auto *inputGain = apvts.getRawParameterValue(inputGainId);
      auto *mix = apvts.getRawParameterValue(mixId);

      dryMixBuffer.makeCopyOf(buffer, true);

      gain.setGainDecibels(inputGain->load());
      juce::dsp::AudioBlock<float> block(buffer);
      gain.process(juce::dsp::ProcessContextReplacing<float>(block));

      const float wet = juce::jlimit(0.0f, 1.0f, mix->load() * 0.01f);
      const float dry = 1.0f - wet;

      for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        auto *wetData = buffer.getWritePointer(channel);
        const auto *dryData = dryMixBuffer.getReadPointer(channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
          wetData[sample] = (wetData[sample] * wet) + (dryData[sample] * dry);
      }
    }
  }

  const int numSamples = buffer.getNumSamples();
  const int numChannels = buffer.getNumChannels();
  if (numChannels != standaloneAudioDeviceBridge.getAvailableOutputChannelCount()) {
    standaloneAudioDeviceBridge.setAvailableOutputChannelCount(numChannels);
    int padsWidth = juce::jlimit(1, 2, padsOutputWidth.load(std::memory_order_relaxed));
    if (numChannels < 2)
      padsWidth = 1;
    const int padsMaxBase = juce::jmax(1, numChannels - (padsWidth >= 2 ? 1 : 0));
    const int padsChannel = juce::jlimit(
        1, padsMaxBase, padsOutputChannel.load(std::memory_order_relaxed));
    padsOutputWidth.store(padsWidth, std::memory_order_relaxed);
    padsOutputChannel.store(padsChannel, std::memory_order_relaxed);
    stemOutputRoutingState.normalizeForDevice(numChannels);
    for (int i = 0; i < maxStemCount; ++i) {
      stemOutputChannels[static_cast<size_t>(i)].store(
          stemOutputRoutingState.getStemOutputChannel(i),
          std::memory_order_relaxed);
      stemOutputWidths[static_cast<size_t>(i)].store(
          stemOutputRoutingState.getStemOutputWidth(i),
          std::memory_order_relaxed);
    }
    setMetronomeOutputRouting(
        metronomeOutputChannel.load(std::memory_order_relaxed),
        metronomeOutputWidth.load(std::memory_order_relaxed));
  }

  const float masterOutGain =
      (masterMuted.load(std::memory_order_relaxed)
           ? 0.0f
           : juce::jlimit(0.0f, 1.0f,
                          masterVolume.load(std::memory_order_relaxed))) *
      juce::jlimit(0.0f, 2.0f, masterGain.load(std::memory_order_relaxed));
  const float fadeTargetLocal = fadeTarget.load(std::memory_order_relaxed);
  float fadeStart = fadeGain;
  float fadeEnd = fadeGain;
  if (numSamples > 0 && playbackSampleRate > 0.0f &&
      !juce::approximatelyEqual(fadeGain, fadeTargetLocal)) {
    const float step =
        (fadeTargetLocal - fadeGain) /
        static_cast<float>(fadeDurationSec * playbackSampleRate);
    fadeEnd = fadeGain + (step * static_cast<float>(numSamples));
    if ((step > 0.0f && fadeEnd > fadeTargetLocal) ||
        (step < 0.0f && fadeEnd < fadeTargetLocal))
      fadeEnd = fadeTargetLocal;
    fadeGain = fadeEnd;
  }

  if (usedSeparatedMix) {
    const bool timeProcessActive =
        (pitchActiveSemiTones != 0) ||
        !juce::approximatelyEqual(playbackTempoRatioActive, 1.0);
    mixEngine.renderToOutput(buffer, timeProcessActive, sourceFramesThisBlock,
                             fadeStart, fadeEnd, windowFadeStart,
                             windowFadeEnd);
    stemOutputLatencyThisBlock = mixEngine.getOutputLatencySamples();
  } else {
    const float combinedFadeStart = fadeStart * windowFadeStart;
    const float combinedFadeEnd = fadeEnd * windowFadeEnd;
    if (!juce::approximatelyEqual(combinedFadeStart, 1.0f) ||
        !juce::approximatelyEqual(combinedFadeEnd, 1.0f)) {
      if (juce::approximatelyEqual(combinedFadeStart, combinedFadeEnd)) {
        buffer.applyGain(combinedFadeEnd);
      } else {
        buffer.applyGainRamp(0, numSamples, combinedFadeStart,
                             combinedFadeEnd);
      }
    }
  }

  if (songCrossfadeActive.load(std::memory_order_relaxed) &&
      numSamples > 0 && playbackSampleRate > 0.0 &&
      getTransportState() == TransportState::playing) {
    const float progressStart =
        songCrossfadeProgress.load(std::memory_order_relaxed);
    const float progressStep = static_cast<float>(
        (static_cast<double>(numSamples) / playbackSampleRate) /
        songCrossfadeDurationSec);
    const float progressEnd =
        juce::jlimit(0.0f, 1.0f, progressStart + progressStep);

    const float incomingStart = progressStart;
    const float incomingEnd = progressEnd;
    const float outgoingStart = 1.0f - progressStart;
    const float outgoingEnd = 1.0f - progressEnd;

    songCrossfadeProgress.store(progressEnd, std::memory_order_relaxed);
    songCrossfadeIncomingGain.store(incomingEnd, std::memory_order_relaxed);
    songCrossfadeOutgoingGain.store(outgoingEnd, std::memory_order_relaxed);

    if (juce::approximatelyEqual(incomingStart, incomingEnd))
      buffer.applyGain(incomingEnd);
    else
      buffer.applyGainRamp(0, numSamples, incomingStart, incomingEnd);

    bool renderedOutgoing = false;
    if (!outgoingStemTracks.empty()) {
      outgoingStemRenderBuffer.clear();
      outgoingMixEngine.renderDirect(
          outgoingStemTracks, static_cast<int>(outgoingStemTracks.size()),
          outgoingStemRenderBuffer);
      renderedOutgoing = true;
    }

    if (renderedOutgoing) {
      if (juce::approximatelyEqual(outgoingStart, outgoingEnd))
        outgoingStemRenderBuffer.applyGain(outgoingEnd);
      else
        outgoingStemRenderBuffer.applyGainRamp(0, numSamples, outgoingStart,
                                               outgoingEnd);
      const int addChannels = juce::jmin(numChannels, outgoingStemRenderBuffer.getNumChannels());
      for (int ch = 0; ch < addChannels; ++ch) {
        buffer.addFrom(ch, 0, outgoingStemRenderBuffer, ch, 0, numSamples);
      }
    }

    if (progressEnd >= 0.9999f) {
      songCrossfadeActive.store(false, std::memory_order_relaxed);
      songCrossfadeIncomingGain.store(1.0f, std::memory_order_relaxed);
      songCrossfadeOutgoingGain.store(0.0f, std::memory_order_relaxed);
      songCrossfadeProgress.store(1.0f, std::memory_order_relaxed);
    }
  }

  if (fadeStopsPlayback.load(std::memory_order_relaxed) &&
      fadeActive.load(std::memory_order_relaxed) &&
      juce::approximatelyEqual(fadeGain, 0.0f) &&
      getTransportState() == TransportState::playing) {
    stopSong();
  }

  const bool metronomePlaybackActive =
      isMetronomeEnabled() && getTransportState() == TransportState::playing;
  if (metronomePlaybackActive) {
    metronomeRenderBuffer.clear();

    const int activeStemCount = loadedStemCount.load(std::memory_order_relaxed);
    bool anySoloActive = false;
    for (int i = 0; i < activeStemCount; ++i) {
      if (stemSoloStates[static_cast<size_t>(i)].load(
              std::memory_order_relaxed)) {
        anySoloActive = true;
        break;
      }
    }

    float effectiveVolume =
        metronomeVolumeState.load(std::memory_order_relaxed);
    float effectiveGain =
        metronomeGainState.load(std::memory_order_relaxed);
    bool muted = metronomeMutedState.load(std::memory_order_relaxed);
    bool soloed = metronomeSoloState.load(std::memory_order_relaxed);
    int outputChannelOneBased = getMetronomeOutputChannel();
    int outputWidth = getMetronomeOutputWidth();

    effectiveVolume = juce::jlimit(0.0f, 1.0f, effectiveVolume);
    effectiveGain = juce::jlimit(0.0f, 4.0f, effectiveGain);

    outputWidth = juce::jlimit(1, 2, outputWidth);
    if (numChannels < 2)
      outputWidth = 1;
    outputChannelOneBased = juce::jlimit(1, juce::jmax(1, numChannels),
                                         outputChannelOneBased);
    if (outputWidth >= 2 && outputChannelOneBased >= numChannels)
      outputChannelOneBased = juce::jmax(1, numChannels - 1);

    const bool metronomeAudible = !muted && (!anySoloActive || soloed);
    metronome.setVolume(effectiveVolume);
    metronome.setMuted(!metronomeAudible);
    syncMetronomeToSourceSeconds(renderedSourceStartSec);
    metronome.process(metronomeRenderBuffer);
    metronomeLatencyDelay.setDelay(stemOutputLatencyThisBlock);
    metronomeLatencyDelay.process(metronomeRenderBuffer);
    if (!juce::approximatelyEqual(effectiveGain, 1.0f))
      metronomeRenderBuffer.applyGain(effectiveGain);

    const float metronomeMeterLeft =
        metronomeRenderBuffer.getMagnitude(0, 0, numSamples);
    const float metronomeMeterRight =
        outputWidth >= 2
            ? metronomeRenderBuffer.getMagnitude(
                  juce::jmin(1, metronomeRenderBuffer.getNumChannels() - 1), 0,
                  numSamples)
            : 0.0f;
    metronomeMeterLevelLeft.store(metronomeMeterLeft,
                                  std::memory_order_relaxed);
    metronomeMeterLevelRight.store(metronomeMeterRight,
                                   std::memory_order_relaxed);

    const int outputIndex = juce::jlimit(
        0, juce::jmax(0, numChannels - 1), outputChannelOneBased - 1);
    stemOutputRouter.routeStemToOutputChannels(metronomeRenderBuffer,
                                               outputIndex, outputWidth,
                                               buffer);
  } else {
    metronomeMeterLevelLeft.store(0.0f, std::memory_order_relaxed);
    metronomeMeterLevelRight.store(0.0f, std::memory_order_relaxed);
  }

  if (padsPackLoaded.load(std::memory_order_relaxed)) {
    padsRenderBuffer.clear();
    bool renderedPads = false;
    {
      const juce::SpinLock::ScopedTryLockType lock(padsStateLock);
      if (lock.isLocked()) {
        padsEngine.process(padsRenderBuffer);
        renderedPads = true;
      }
    }
    if (renderedPads) {
      const float padsOutputGain =
          (padsMuted.load(std::memory_order_relaxed)
               ? 0.0f
               : juce::jlimit(0.0f, 1.0f, padsVolume.load(std::memory_order_relaxed))) *
          juce::jlimit(0.0f, 2.0f, padsGain.load(std::memory_order_relaxed));
      if (!juce::approximatelyEqual(padsOutputGain, 1.0f))
        padsRenderBuffer.applyGain(padsOutputGain);

      // Gentle safety clipper on pads bus to tame hot transients.
      {
        constexpr float kDrive = 1.35f;
        const float norm = std::tanh(kDrive);
        for (int ch = 0; ch < padsRenderBuffer.getNumChannels(); ++ch) {
          auto *dst = padsRenderBuffer.getWritePointer(ch);
          for (int i = 0; i < numSamples; ++i) {
            const float x = dst[i];
            dst[i] = std::tanh(x * kDrive) / norm;
          }
        }
      }

      int outputWidth = juce::jlimit(1, 2, padsOutputWidth.load(std::memory_order_relaxed));
      if (numChannels < 2)
        outputWidth = 1;
      int outputChannelOneBased =
          juce::jlimit(1, juce::jmax(1, numChannels),
                       padsOutputChannel.load(std::memory_order_relaxed));
      if (outputWidth >= 2 && outputChannelOneBased >= numChannels)
        outputChannelOneBased = juce::jmax(1, numChannels - 1);

      // Some pad packs are exported as stereo with one side effectively silent.
      // For stereo pad routing, mirror the active side so output is audible on L/R.
      if (outputWidth >= 2 && padsRenderBuffer.getNumChannels() >= 2) {
        const float leftMag = padsRenderBuffer.getMagnitude(0, 0, numSamples);
        const float rightMag = padsRenderBuffer.getMagnitude(1, 0, numSamples);
        constexpr float kSilenceThreshold = 1.0e-5f;
        if (leftMag > kSilenceThreshold && rightMag <= kSilenceThreshold) {
          padsRenderBuffer.copyFrom(1, 0, padsRenderBuffer, 0, 0, numSamples);
        } else if (rightMag > kSilenceThreshold &&
                   leftMag <= kSilenceThreshold) {
          padsRenderBuffer.copyFrom(0, 0, padsRenderBuffer, 1, 0, numSamples);
        }
      }

      const int outputIndex = juce::jlimit(
          0, juce::jmax(0, numChannels - 1), outputChannelOneBased - 1);
      stemOutputRouter.routeStemToOutputChannels(padsRenderBuffer, outputIndex,
                                                 outputWidth, buffer);
    }
  }

  dynamicGuideRenderBuffer.clear();
  if (dynamicGuideShouldRender && dynamicGuideAnnouncementsActive)
    dynamicGuideEngine.render(dynamicGuideRenderBuffer);

  // Pitch/tempo processing delays the stems. Feed the guide delay line every
  // callback (including silence) so its first announcement is aligned too.
  dynamicGuideLatencyDelay.setDelay(stemOutputLatencyThisBlock);
  dynamicGuideLatencyDelay.process(dynamicGuideRenderBuffer);

  if (dynamicGuideShouldRender && dynamicGuideAnnouncementsActive) {
    if (!juce::approximatelyEqual(dynamicGuideOutputGain, 1.0f))
      dynamicGuideRenderBuffer.applyGain(dynamicGuideOutputGain);

    const float meterLeft =
        dynamicGuideRenderBuffer.getMagnitude(0, 0, numSamples);
    float meterRight =
        dynamicGuideRenderBuffer.getNumChannels() > 1
            ? dynamicGuideRenderBuffer.getMagnitude(1, 0, numSamples)
            : meterLeft;
    if (dynamicGuideOutputWidth == 1)
      meterRight = 0.0f;
    dynamicGuideMeterLevelLeft.store(meterLeft, std::memory_order_relaxed);
    dynamicGuideMeterLevelRight.store(meterRight, std::memory_order_relaxed);

    int outputWidth = juce::jlimit(1, 2, dynamicGuideOutputWidth);
    if (numChannels < 2)
      outputWidth = 1;
    int outputChannel = juce::jlimit(
        1, juce::jmax(1, numChannels), dynamicGuideOutputChannelOneBased);
    if (outputWidth >= 2 && outputChannel >= numChannels)
      outputChannel = juce::jmax(1, numChannels - 1);
    const int outputIndex = juce::jlimit(
        0, juce::jmax(0, numChannels - 1), outputChannel - 1);
    stemOutputRouter.routeStemToOutputChannels(dynamicGuideRenderBuffer,
                                               outputIndex, outputWidth,
                                               buffer);
  }

  if (!juce::approximatelyEqual(masterOutGain, 1.0f))
    buffer.applyGain(masterOutGain);

  if (transitionToNextArrangementPart) {
    arrangementCurrentPartIndex.store(nextArrangementPartIndex,
                                      std::memory_order_relaxed);
    seekTargetSeconds.store(nextArrangementPartStartSec,
                            std::memory_order_relaxed);
    seekPending.store(true, std::memory_order_release);
    songCurrentPositionSeconds.store(nextArrangementPartStartSec,
                                     std::memory_order_relaxed);
    const juce::int64 nextSamples =
        playworship::audio::TransportClock::secondsToFrames(
            nextArrangementPartStartSec, playbackSampleRate);
    if (playbackSampleRate > 0.0) {
      globalSamplePosition.store(nextSamples, std::memory_order_relaxed);
      transportClock.seek(nextSamples);
    }
    syncMetronomeToSourceSeconds(
        playworship::audio::TransportClock::framesToSeconds(
            mapArrangementFrameToSourceFrame(nextSamples),
            playbackSampleRate));
  } else if (stopAtEnd) {
    juce::ignoreUnused(stopAtEndSec);
    // Al llegar al final de la ventana, detenemos y volvemos al inicio para
    // que el seek no quede clavado al final.
    stopSong();
  }

  const float left =
      numChannels > 0 ? buffer.getMagnitude(0, 0, numSamples) : 0.0f;
  const float right =
      numChannels > 1 ? buffer.getMagnitude(1, 0, numSamples) : left;
  meterLevelLeft.store(left, std::memory_order_relaxed);
  meterLevelRight.store(right, std::memory_order_relaxed);
}

juce::AudioProcessorEditor *MTControlsAudioProcessor::createEditor() {
  return new MTControlsAudioProcessorEditor(*this);
}

bool MTControlsAudioProcessor::hasEditor() const { return true; }

const juce::String MTControlsAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool MTControlsAudioProcessor::acceptsMidi() const { return true; }

bool MTControlsAudioProcessor::producesMidi() const { return false; }

bool MTControlsAudioProcessor::isMidiEffect() const { return false; }

double MTControlsAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int MTControlsAudioProcessor::midiSourceStateIndex(
    const MidiBindingSourceType sourceType, const int channel,
    const int data1) noexcept {
  const int sourceIndex =
      sourceType == MidiBindingSourceType::Note ? 1 : 0;
  const int channelIndex = juce::jlimit(1, 16, channel) - 1;
  const int dataIndex = juce::jlimit(0, 127, data1);
  return (sourceIndex * 16 * 128) + (channelIndex * 128) + dataIndex;
}

bool MTControlsAudioProcessor::isMidiTargetValid(const int targetId) noexcept {
  if (targetId >= midiTargetPadsNoteBase &&
      targetId < midiTargetPadsNoteBase + 12)
    return true;
  if (targetId >= midiTargetStemVolumeBase &&
      targetId < midiTargetStemVolumeBase + maxStemCount)
    return true;
  if (targetId >= midiTargetStemGainBase &&
      targetId < midiTargetStemGainBase + maxStemCount)
    return true;
  if (targetId >= midiTargetStemMuteBase &&
      targetId < midiTargetStemMuteBase + maxStemCount)
    return true;
  if (targetId >= midiTargetStemSoloBase &&
      targetId < midiTargetStemSoloBase + maxStemCount)
    return true;
  switch (targetId) {
  case midiTargetMasterVolume:
  case midiTargetMasterMute:
  case midiTargetTransportPlayPause:
  case midiTargetTransportStop:
  case midiTargetTransportPrevSong:
  case midiTargetTransportSeekBack:
  case midiTargetTransportSeekForward:
  case midiTargetTransportNextSong:
  case midiTargetTransportFadeToggle:
  case midiTargetMasterGain:
  case midiTargetSongStartMinus:
  case midiTargetSongStartPlus:
  case midiTargetPadsPower:
  case midiTargetPadsVolume:
  case midiTargetPadsGain:
  case midiTargetSongEndMinus:
  case midiTargetSongEndPlus:
  case midiTargetSongFadeMinus:
  case midiTargetSongFadePlus:
  case midiTargetTempoDown:
  case midiTargetTempoUp:
  case midiTargetPitchDown:
  case midiTargetPitchUp:
  case midiTargetMetronomeEnabled:
  case midiTargetMetronomeMute:
  case midiTargetMetronomeSolo:
  case midiTargetMixerNudgeUp:
  case midiTargetMixerNudgeDown:
  case midiTargetMixerApplyRouting:
  case midiTargetClickDynDivisionQuarter:
  case midiTargetClickDynDivisionEighth:
  case midiTargetClickDynDivisionSixteenth:
    return true;
  default:
    return false;
  }
}

MidiBindingMode
MTControlsAudioProcessor::defaultModeForTarget(const int targetId) noexcept {
  if (targetId == midiTargetMasterVolume || targetId == midiTargetMasterGain ||
      targetId == midiTargetMetronomeVolume ||
      targetId == midiTargetMetronomeGain || targetId == midiTargetPadsVolume ||
      targetId == midiTargetPadsGain ||
      (targetId >= midiTargetStemVolumeBase &&
       targetId < midiTargetStemVolumeBase + maxStemCount) ||
      (targetId >= midiTargetStemGainBase &&
       targetId < midiTargetStemGainBase + maxStemCount)) {
    return MidiBindingMode::Absolute;
  }
  if (targetId >= midiTargetPadsNoteBase &&
      targetId < midiTargetPadsNoteBase + 12) {
    return MidiBindingMode::Toggle;
  }
  return MidiBindingMode::Toggle;
}

juce::String MTControlsAudioProcessor::midiTargetNameForId(const int targetId) {
  switch (targetId) {
  case midiTargetMasterVolume:
    return "Master Volume";
  case midiTargetMasterGain:
    return "Master Gain";
  case midiTargetMasterMute:
    return "Master Mute";
  case midiTargetTransportPlayPause:
    return "Transport Play/Pause";
  case midiTargetTransportStop:
    return "Transport Stop";
  case midiTargetMetronomeEnabled:
    return "Metronome Enabled";
  case midiTargetMetronomeVolume:
    return "Metronome Volume";
  case midiTargetMetronomeMute:
    return "Metronome Mute";
  case midiTargetTransportPrevSong:
    return "Transport Prev Song";
  case midiTargetTransportSeekBack:
    return "Transport Seek Back";
  case midiTargetTransportSeekForward:
    return "Transport Seek Forward";
  case midiTargetTransportNextSong:
    return "Transport Next Song";
  case midiTargetTransportFadeToggle:
    return "Transport Fade Toggle";
  case midiTargetUiWindowModeToggle:
    return "UI Window Maximize/Restore";
  case midiTargetUiSettingsToggle:
    return "UI Open Settings";
  case midiTargetUiSongWindowPopover:
    return "UI Open Song Window";
  case midiTargetUiKeyPopover:
    return "UI Open Key Selector";
  case midiTargetUiMetronomePopover:
    return "UI Open Metronome Panel";
  case midiTargetSongStartMinus:
    return "Song Start -1s";
  case midiTargetSongStartPlus:
    return "Song Start +1s";
  case midiTargetSongEndMinus:
    return "Song End -1s";
  case midiTargetSongEndPlus:
    return "Song End +1s";
  case midiTargetSongFadeMinus:
    return "Song Fade -1s";
  case midiTargetSongFadePlus:
    return "Song Fade +1s";
  case midiTargetTempoDown:
    return "Tempo -1 BPM";
  case midiTargetTempoUp:
    return "Tempo +1 BPM";
  case midiTargetPitchDown:
    return "Pitch -1 Semitone";
  case midiTargetPitchUp:
    return "Pitch +1 Semitone";
  case midiTargetMetronomeGain:
    return "Metronome Gain";
  case midiTargetMetronomeSolo:
    return "Metronome Solo";
  case midiTargetMixerNudgeUp:
    return "Mixer Nudge Up";
  case midiTargetMixerNudgeDown:
    return "Mixer Nudge Down";
  case midiTargetMixerApplyRouting:
    return "Mixer Apply Routing";
  case midiTargetClickDynDivisionQuarter:
    return "Click Dyn 1/4";
  case midiTargetClickDynDivisionEighth:
    return "Click Dyn 1/8";
  case midiTargetClickDynDivisionSixteenth:
    return "Click Dyn 1/16";
  case midiTargetPadsPower:
    return "Pads Power";
  case midiTargetPadsVolume:
    return "Pads Volume";
  case midiTargetPadsGain:
    return "Pads Gain";
  default:
    break;
  }
  if (targetId >= midiTargetPadsNoteBase &&
      targetId < midiTargetPadsNoteBase + 12) {
    static constexpr const char *kPadNames[] = {"C",  "C#", "D",  "D#", "E", "F",
                                                 "F#", "G",  "G#", "A",  "A#", "B"};
    const int idx = targetId - midiTargetPadsNoteBase;
    return "Pads Note " + juce::String(kPadNames[idx]);
  }
  if (targetId >= midiTargetStemVolumeBase &&
      targetId < midiTargetStemVolumeBase + maxStemCount) {
    const int stemOneBased = targetId - midiTargetStemVolumeBase + 1;
    return "Stem " + juce::String(stemOneBased) + " Volume";
  }
  if (targetId >= midiTargetStemGainBase &&
      targetId < midiTargetStemGainBase + maxStemCount) {
    const int stemOneBased = targetId - midiTargetStemGainBase + 1;
    return "Stem " + juce::String(stemOneBased) + " Gain";
  }
  if (targetId >= midiTargetStemMuteBase &&
      targetId < midiTargetStemMuteBase + maxStemCount) {
    const int stemOneBased = targetId - midiTargetStemMuteBase + 1;
    return "Stem " + juce::String(stemOneBased) + " Mute";
  }
  if (targetId >= midiTargetStemSoloBase &&
      targetId < midiTargetStemSoloBase + maxStemCount) {
    const int stemOneBased = targetId - midiTargetStemSoloBase + 1;
    return "Stem " + juce::String(stemOneBased) + " Solo";
  }
  return {};
}

std::vector<MTControlsAudioProcessor::MidiTargetDescriptor>
MTControlsAudioProcessor::getMidiTargets() const {
  std::vector<MidiTargetDescriptor> out;
  auto addTarget = [&out](const int targetId) {
    out.push_back({targetId, midiTargetNameForId(targetId),
                   defaultModeForTarget(targetId)});
  };

  addTarget(midiTargetMasterVolume);
  addTarget(midiTargetMasterGain);
  addTarget(midiTargetMasterMute);
  addTarget(midiTargetMetronomeEnabled);
  addTarget(midiTargetMetronomeMute);
  addTarget(midiTargetMetronomeSolo);
  addTarget(midiTargetClickDynDivisionQuarter);
  addTarget(midiTargetClickDynDivisionEighth);
  addTarget(midiTargetClickDynDivisionSixteenth);

  addTarget(midiTargetTransportPlayPause);
  addTarget(midiTargetTransportStop);
  addTarget(midiTargetTransportPrevSong);
  addTarget(midiTargetTransportSeekBack);
  addTarget(midiTargetTransportSeekForward);
  addTarget(midiTargetTransportNextSong);
  addTarget(midiTargetTransportFadeToggle);
  addTarget(midiTargetTempoDown);
  addTarget(midiTargetTempoUp);
  addTarget(midiTargetPitchDown);
  addTarget(midiTargetPitchUp);
  addTarget(midiTargetSongStartMinus);
  addTarget(midiTargetSongStartPlus);
  addTarget(midiTargetSongEndMinus);
  addTarget(midiTargetSongEndPlus);
  addTarget(midiTargetSongFadeMinus);
  addTarget(midiTargetSongFadePlus);

  addTarget(midiTargetMixerNudgeUp);
  addTarget(midiTargetMixerNudgeDown);
  addTarget(midiTargetMixerApplyRouting);

  addTarget(midiTargetPadsPower);
  addTarget(midiTargetPadsVolume);
  addTarget(midiTargetPadsGain);
  for (int i = 0; i < 12; ++i) {
    const int targetId = midiTargetPadsNoteBase + i;
    addTarget(targetId);
  }

  for (int i = 0; i < maxStemCount; ++i) {
    addTarget(midiTargetStemVolumeBase + i);
    addTarget(midiTargetStemGainBase + i);
    addTarget(midiTargetStemMuteBase + i);
    addTarget(midiTargetStemSoloBase + i);
  }
  return out;
}

std::vector<MTControlsAudioProcessor::MidiBinding>
MTControlsAudioProcessor::getMidiBindings() const {
  const juce::ScopedLock lock(midiBindingsLock);
  return midiBindings;
}

MTControlsAudioProcessor::MidiInputProbe
MTControlsAudioProcessor::getLastMidiInputProbe() const noexcept {
  MidiInputProbe probe;
  probe.timestampMs = midiLastMessageMs.load(std::memory_order_relaxed);
  probe.channel = midiLastMessageChannel.load(std::memory_order_relaxed);
  probe.data1 = midiLastMessageData1.load(std::memory_order_relaxed);
  probe.data2 = midiLastMessageData2.load(std::memory_order_relaxed);
  probe.type = midiLastMessageType.load(std::memory_order_relaxed);
  return probe;
}

void MTControlsAudioProcessor::setMidiBinding(const MidiBinding &binding) {
  if (!isMidiTargetValid(binding.targetId))
    return;
  const int safeChannel = juce::jlimit(1, 16, binding.midiChannel);
  const int safeData1 = juce::jlimit(0, 127, binding.ccNumber);
  const auto safeSourceType =
      binding.sourceType == MidiBindingSourceType::Note
          ? MidiBindingSourceType::Note
          : MidiBindingSourceType::CC;

  const juce::ScopedLock lock(midiBindingsLock);
  midiBindings.erase(
      std::remove_if(midiBindings.begin(), midiBindings.end(),
                     [bindingTarget = binding.targetId, safeSourceType,
                      safeChannel, safeData1](const MidiBinding &current) {
                       return current.targetId == bindingTarget ||
                              (current.sourceType == safeSourceType &&
                               current.midiChannel == safeChannel &&
                               current.ccNumber == safeData1);
                     }),
      midiBindings.end());
  MidiBinding next = binding;
  next.sourceType = safeSourceType;
  next.midiChannel = safeChannel;
  next.ccNumber = safeData1;
  midiBindings.push_back(next);
  publishMidiBindingsLocked();
}

void MTControlsAudioProcessor::clearMidiBindingForTarget(const int targetId) {
  const juce::ScopedLock lock(midiBindingsLock);
  midiBindings.erase(
      std::remove_if(midiBindings.begin(), midiBindings.end(),
                     [targetId](const MidiBinding &current) {
                       return current.targetId == targetId;
                     }),
      midiBindings.end());
  publishMidiBindingsLocked();
}

void MTControlsAudioProcessor::beginMidiLearnForTarget(const int targetId) {
  midiLearnTargetId.store(isMidiTargetValid(targetId) ? targetId : -1,
                          std::memory_order_release);
  if (targetId < 0)
    midiLearnPendingTargetId.store(-1, std::memory_order_release);
}

bool MTControlsAudioProcessor::consumePendingMidiLearnBinding(
    MidiBinding &outBinding) {
  const int targetId = midiLearnPendingTargetId.exchange(-1, std::memory_order_acq_rel);
  if (targetId < 0)
    return false;
  outBinding.targetId = targetId;
  outBinding.midiChannel =
      juce::jlimit(1, 16, midiLearnPendingChannel.load(std::memory_order_relaxed));
  outBinding.sourceType = static_cast<MidiBindingSourceType>(juce::jlimit(
      0, 1, midiLearnPendingSourceType.load(std::memory_order_relaxed)));
  outBinding.ccNumber =
      juce::jlimit(0, 127, midiLearnPendingCc.load(std::memory_order_relaxed));
  outBinding.mode = static_cast<MidiBindingMode>(
      juce::jlimit(0, 2, midiLearnPendingMode.load(std::memory_order_relaxed)));
  return true;
}

bool MTControlsAudioProcessor::consumePendingMidiUiCommand(int &outCommand) {
  const int command = midiPendingUiCommand.exchange(0, std::memory_order_acq_rel);
  if (command == 0)
    return false;
  outCommand = command;
  return true;
}

void MTControlsAudioProcessor::applyMidiBindingValue(const int targetId,
                                                     const int ccValue,
                                                     const MidiBindingMode mode) {
  if (!isMidiTargetValid(targetId))
    return;

  const float normalized =
      juce::jlimit(0.0f, 1.0f, static_cast<float>(ccValue) / 127.0f);
  const bool high = ccValue >= 64;
  const auto queueUiCommand = [this](const int commandId) {
    midiPendingUiCommand.store(commandId, std::memory_order_release);
  };
  const auto divisionBitForTarget = [](const int divisionTargetId) -> int {
    if (divisionTargetId ==
        MTControlsAudioProcessor::midiTargetClickDynDivisionQuarter)
      return 0x01;
    if (divisionTargetId ==
        MTControlsAudioProcessor::midiTargetClickDynDivisionEighth)
      return 0x02;
    if (divisionTargetId ==
        MTControlsAudioProcessor::midiTargetClickDynDivisionSixteenth)
      return 0x04;
    return 0;
  };
  const auto setClickDivisionBit = [this](const int bit, const bool enabled) {
    if (bit == 0)
      return;
    const int current = getClickDivision() & 0x07;
    const int next = enabled ? (current | bit) : (current & ~bit);
    setClickDivision(next);
  };

  if (targetId == midiTargetMasterVolume) {
    setMasterVolume(normalized);
    return;
  }
  if (targetId == midiTargetMasterGain) {
    setMasterGain(normalized * 2.0f);
    return;
  }
  if (targetId == midiTargetPadsVolume) {
    setPadsVolume(normalized);
    return;
  }
  if (targetId == midiTargetPadsGain) {
    setPadsGain(normalized * 2.0f);
    return;
  }
  if (targetId >= midiTargetStemVolumeBase &&
      targetId < midiTargetStemVolumeBase + maxStemCount) {
    const int stemIndex = targetId - midiTargetStemVolumeBase;
    setStemVolume(stemIndex, normalized);
    return;
  }
  if (targetId >= midiTargetStemGainBase &&
      targetId < midiTargetStemGainBase + maxStemCount) {
    const int stemIndex = targetId - midiTargetStemGainBase;
    setStemGain(stemIndex, normalized * 2.0f);
    return;
  }

  if (targetId >= midiTargetPadsNoteBase &&
      targetId < midiTargetPadsNoteBase + 12) {
    const int noteIndex = targetId - midiTargetPadsNoteBase;
    if (mode == MidiBindingMode::Momentary) {
      if (high)
        triggerPadNote(noteIndex, 0);
      else
        stopPadNote(noteIndex);
    } else if (high) {
      triggerPadNote(noteIndex, 0);
    }
    return;
  }

  if (mode == MidiBindingMode::Momentary) {
    if (targetId == midiTargetMasterMute)
      setMasterMute(high);
    else if (targetId == midiTargetMetronomeEnabled)
      setMetronomeEnabled(high);
    else if (const int divisionBit = divisionBitForTarget(targetId);
             divisionBit != 0)
      setClickDivisionBit(divisionBit, high);
    else if (targetId == midiTargetMetronomeMute)
      setMetronomeMuted(high);
    else if (targetId == midiTargetMetronomeSolo)
      setMetronomeSolo(high);
    else if (targetId == midiTargetPadsPower)
      setPadsEnabled(high);
    else if (targetId >= midiTargetStemMuteBase &&
             targetId < midiTargetStemMuteBase + maxStemCount)
      setStemMute(targetId - midiTargetStemMuteBase, high);
    else if (targetId >= midiTargetStemSoloBase &&
             targetId < midiTargetStemSoloBase + maxStemCount)
      setStemSolo(targetId - midiTargetStemSoloBase, high);
    else if (targetId == midiTargetTransportStop && high)
      stopSong();
    else if (targetId == midiTargetTransportSeekBack && high)
      seekSongRelative(-5.0);
    else if (targetId == midiTargetTransportSeekForward && high)
      seekSongRelative(5.0);
    else if (targetId == midiTargetTransportPrevSong && high)
      queueUiCommand(midiUiCommandPrevSong);
    else if (targetId == midiTargetTransportNextSong && high)
      queueUiCommand(midiUiCommandNextSong);
    else if (targetId == midiTargetMixerNudgeUp && high)
      queueUiCommand(midiUiCommandMixerNudgeUp);
    else if (targetId == midiTargetMixerNudgeDown && high)
      queueUiCommand(midiUiCommandMixerNudgeDown);
    else if (targetId == midiTargetMixerApplyRouting && high)
      queueUiCommand(midiUiCommandMixerApplyRouting);
    else if (targetId == midiTargetSongStartMinus && high)
      setSongStartSeconds(getSongStartSeconds() - 1.0);
    else if (targetId == midiTargetSongStartPlus && high)
      setSongStartSeconds(getSongStartSeconds() + 1.0);
    else if (targetId == midiTargetSongEndMinus && high)
      setSongEndSeconds(getSongEndSeconds() - 1.0);
    else if (targetId == midiTargetSongEndPlus && high)
      setSongEndSeconds(getSongEndSeconds() + 1.0);
    else if (targetId == midiTargetSongFadeMinus && high)
      setSongFadeOutSeconds(getSongFadeOutSeconds() - 1.0);
    else if (targetId == midiTargetSongFadePlus && high)
      setSongFadeOutSeconds(getSongFadeOutSeconds() + 1.0);
    else if (targetId == midiTargetTempoDown && high)
      setPlaybackTempoBpm(getPlaybackTempoBpm() - 1.0);
    else if (targetId == midiTargetTempoUp && high)
      setPlaybackTempoBpm(getPlaybackTempoBpm() + 1.0);
    else if (targetId == midiTargetPitchDown && high)
      setPitchSemiTones(getPitchSemiTones() - 1);
    else if (targetId == midiTargetPitchUp && high)
      setPitchSemiTones(getPitchSemiTones() + 1);
    return;
  }

  if (targetId == midiTargetMasterMute)
    setMasterMute(!isMasterMuted());
  else if (targetId == midiTargetMetronomeEnabled)
    setMetronomeEnabled(!isMetronomeEnabled());
  else if (const int divisionBit = divisionBitForTarget(targetId);
           divisionBit != 0)
    setClickDivisionBit(divisionBit, (getClickDivision() & divisionBit) == 0);
  else if (targetId == midiTargetMetronomeMute)
    setMetronomeMuted(!isMetronomeMuted());
  else if (targetId == midiTargetMetronomeSolo)
    setMetronomeSolo(!isMetronomeSolo());
  else if (targetId == midiTargetTransportPlayPause)
    toggleSongPlayPause();
  else if (targetId == midiTargetTransportStop)
    stopSong();
  else if (targetId == midiTargetTransportSeekBack)
    seekSongRelative(-5.0);
  else if (targetId == midiTargetTransportSeekForward)
    seekSongRelative(5.0);
  else if (targetId == midiTargetTransportFadeToggle) {
    setFadeStopsPlayback(false);
    setFadeActive(!isFadeActive());
  } else if (targetId == midiTargetPadsPower) {
    setPadsEnabled(!isPadsEnabled());
  } else if (targetId >= midiTargetStemMuteBase &&
             targetId < midiTargetStemMuteBase + maxStemCount) {
    const int stemIndex = targetId - midiTargetStemMuteBase;
    setStemMute(stemIndex, !isStemMuted(stemIndex));
  } else if (targetId >= midiTargetStemSoloBase &&
             targetId < midiTargetStemSoloBase + maxStemCount) {
    const int stemIndex = targetId - midiTargetStemSoloBase;
    setStemSolo(stemIndex, !isStemSoloed(stemIndex));
  } else if (targetId == midiTargetTransportPrevSong) {
    queueUiCommand(midiUiCommandPrevSong);
  } else if (targetId == midiTargetTransportNextSong) {
    queueUiCommand(midiUiCommandNextSong);
  } else if (targetId == midiTargetMixerNudgeUp) {
    queueUiCommand(midiUiCommandMixerNudgeUp);
  } else if (targetId == midiTargetMixerNudgeDown) {
    queueUiCommand(midiUiCommandMixerNudgeDown);
  } else if (targetId == midiTargetMixerApplyRouting) {
    queueUiCommand(midiUiCommandMixerApplyRouting);
  } else if (targetId == midiTargetSongStartMinus) {
    setSongStartSeconds(getSongStartSeconds() - 1.0);
  } else if (targetId == midiTargetSongStartPlus) {
    setSongStartSeconds(getSongStartSeconds() + 1.0);
  } else if (targetId == midiTargetSongEndMinus) {
    setSongEndSeconds(getSongEndSeconds() - 1.0);
  } else if (targetId == midiTargetSongEndPlus) {
    setSongEndSeconds(getSongEndSeconds() + 1.0);
  } else if (targetId == midiTargetSongFadeMinus) {
    setSongFadeOutSeconds(getSongFadeOutSeconds() - 1.0);
  } else if (targetId == midiTargetSongFadePlus) {
    setSongFadeOutSeconds(getSongFadeOutSeconds() + 1.0);
  } else if (targetId == midiTargetTempoDown) {
    setPlaybackTempoBpm(getPlaybackTempoBpm() - 1.0);
  } else if (targetId == midiTargetTempoUp) {
    setPlaybackTempoBpm(getPlaybackTempoBpm() + 1.0);
  } else if (targetId == midiTargetPitchDown) {
    setPitchSemiTones(getPitchSemiTones() - 1);
  } else if (targetId == midiTargetPitchUp) {
    setPitchSemiTones(getPitchSemiTones() + 1);
  }
}

namespace {
bool isContinuousMidiTargetId(const int targetId) {
  if (targetId == MTControlsAudioProcessor::midiTargetMasterVolume ||
      targetId == MTControlsAudioProcessor::midiTargetMasterGain ||
      targetId == MTControlsAudioProcessor::midiTargetMetronomeVolume ||
      targetId == MTControlsAudioProcessor::midiTargetMetronomeGain ||
      targetId == MTControlsAudioProcessor::midiTargetPadsVolume ||
      targetId == MTControlsAudioProcessor::midiTargetPadsGain)
    return true;

  if (targetId >= MTControlsAudioProcessor::midiTargetStemVolumeBase &&
      targetId < MTControlsAudioProcessor::midiTargetStemVolumeBase + 64)
    return true;
  if (targetId >= MTControlsAudioProcessor::midiTargetStemGainBase &&
      targetId < MTControlsAudioProcessor::midiTargetStemGainBase + 64)
    return true;
  return false;
}
} // namespace

void MTControlsAudioProcessor::processMidiMapping(
    const juce::MidiBuffer &midiMessages) {
  if (midiMessages.isEmpty())
    return;

  for (const auto metadata : midiMessages) {
    const auto msg = metadata.getMessage();
    const int channel = msg.getChannel();
    const bool isController = msg.isController();
    const bool isNoteOn = msg.isNoteOn(false);
    const bool isNoteOff = msg.isNoteOff(true);
    int probeType = 4;
    int probeData1 = -1;
    int probeData2 = -1;
    if (isController) {
      probeType = 1;
      probeData1 = msg.getControllerNumber();
      probeData2 = msg.getControllerValue();
    } else if (isNoteOn) {
      probeType = 2;
      probeData1 = msg.getNoteNumber();
      probeData2 = msg.getVelocity();
    } else if (isNoteOff) {
      probeType = 3;
      probeData1 = msg.getNoteNumber();
      probeData2 = msg.getVelocity();
    }
    midiLastMessageMs.store(juce::Time::currentTimeMillis(),
                            std::memory_order_relaxed);
    midiLastMessageChannel.store(channel, std::memory_order_relaxed);
    midiLastMessageData1.store(probeData1, std::memory_order_relaxed);
    midiLastMessageData2.store(probeData2, std::memory_order_relaxed);
    midiLastMessageType.store(probeType, std::memory_order_relaxed);

    MidiBindingSourceType sourceType = MidiBindingSourceType::CC;
    int sourceData1 = -1;
    int sourceValue = 0;
    if (isController) {
      sourceType = MidiBindingSourceType::CC;
      sourceData1 = msg.getControllerNumber();
      sourceValue = msg.getControllerValue();
    } else if (isNoteOn || isNoteOff) {
      sourceType = MidiBindingSourceType::Note;
      sourceData1 = msg.getNoteNumber();
      sourceValue = isNoteOn ? msg.getVelocity() : 0;
    }

    if (sourceData1 < 0)
      continue;

    const int learnTarget = midiLearnTargetId.exchange(-1, std::memory_order_acq_rel);
    if (isMidiTargetValid(learnTarget)) {
      MidiBinding learned;
      learned.targetId = learnTarget;
      learned.midiChannel = channel;
      learned.sourceType = sourceType;
      learned.ccNumber = sourceData1;
      learned.mode = defaultModeForTarget(learnTarget);
      setMidiBinding(learned);
      midiLearnPendingChannel.store(channel, std::memory_order_relaxed);
      midiLearnPendingSourceType.store(static_cast<int>(sourceType),
                                       std::memory_order_relaxed);
      midiLearnPendingCc.store(sourceData1, std::memory_order_relaxed);
      midiLearnPendingMode.store(static_cast<int>(learned.mode),
                                 std::memory_order_relaxed);
      midiLearnPendingTargetId.store(learnTarget, std::memory_order_release);
    }

    const int snapshotIndex = acquireMidiBindingSnapshot();
    const auto &bindingSnapshot =
        midiBindingSnapshots[static_cast<size_t>(snapshotIndex)];
    for (int bindingIndex = 0; bindingIndex < bindingSnapshot.count;
         ++bindingIndex) {
      const auto &binding =
          bindingSnapshot.bindings[static_cast<size_t>(bindingIndex)];
      if (binding.sourceType != sourceType || binding.midiChannel != channel ||
          binding.ccNumber != sourceData1)
        continue;

      if (binding.mode == MidiBindingMode::Absolute ||
          isContinuousMidiTargetId(binding.targetId)) {
        applyMidiBindingValue(binding.targetId, sourceValue, binding.mode);
        continue;
      }

      if (binding.sourceType == MidiBindingSourceType::Note) {
        if (binding.mode == MidiBindingMode::Toggle) {
          if (isNoteOn)
            applyMidiBindingValue(binding.targetId, 127, binding.mode);
        } else if (binding.mode == MidiBindingMode::Momentary) {
          if (isNoteOn)
            applyMidiBindingValue(binding.targetId, 127, binding.mode);
          else if (isNoteOff)
            applyMidiBindingValue(binding.targetId, 0, binding.mode);
        }
        continue;
      }

      const int stateIndex =
          midiSourceStateIndex(binding.sourceType, channel, sourceData1);
      const int previous = midiLastSourceValues[static_cast<size_t>(stateIndex)];
      midiLastSourceValues[static_cast<size_t>(stateIndex)] = sourceValue;
      const bool prevHigh = previous >= 64;
      const bool nowHigh = sourceValue >= 64;
      if (!prevHigh && nowHigh)
        applyMidiBindingValue(binding.targetId, sourceValue, binding.mode);
      else if (binding.mode == MidiBindingMode::Momentary && prevHigh && !nowHigh)
        applyMidiBindingValue(binding.targetId, sourceValue, binding.mode);
    }
    releaseMidiBindingSnapshot();
  }
}

void MTControlsAudioProcessor::publishMidiBindingsLocked() noexcept {
  const int published =
      publishedMidiBindingSnapshot.load(std::memory_order_acquire);
  const int reading =
      readingMidiBindingSnapshot.load(std::memory_order_acquire);
  int writeIndex = 0;
  while (writeIndex == published || writeIndex == reading)
    ++writeIndex;

  auto &next = midiBindingSnapshots[static_cast<size_t>(writeIndex)];
  next.count = juce::jmin(static_cast<int>(midiBindings.size()),
                          maxRealtimeMidiBindings);
  for (int i = 0; i < next.count; ++i)
    next.bindings[static_cast<size_t>(i)] =
        midiBindings[static_cast<size_t>(i)];
  publishedMidiBindingSnapshot.store(writeIndex, std::memory_order_release);
}

int MTControlsAudioProcessor::acquireMidiBindingSnapshot() const noexcept {
  for (;;) {
    const int index =
        publishedMidiBindingSnapshot.load(std::memory_order_acquire);
    readingMidiBindingSnapshot.store(index, std::memory_order_release);
    if (index ==
        publishedMidiBindingSnapshot.load(std::memory_order_acquire)) {
      return index;
    }
  }
}

void MTControlsAudioProcessor::releaseMidiBindingSnapshot() const noexcept {
  readingMidiBindingSnapshot.store(-1, std::memory_order_release);
}

int MTControlsAudioProcessor::getNumPrograms() { return 1; }

int MTControlsAudioProcessor::getCurrentProgram() { return 0; }

void MTControlsAudioProcessor::setCurrentProgram(int) {}

const juce::String MTControlsAudioProcessor::getProgramName(int) { return {}; }

void MTControlsAudioProcessor::changeProgramName(int, const juce::String &) {}

void MTControlsAudioProcessor::getStateInformation(
    juce::MemoryBlock &destData) {
  if (auto state = apvts.copyState(); state.isValid()) {
    const auto routingChild =
        state.getChildWithName(StemOutputRoutingState::stateTag);
    if (routingChild.isValid())
      state.removeChild(routingChild, nullptr);
    state.appendChild(stemOutputRoutingState.toValueTree(), nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
  }
}

void MTControlsAudioProcessor::setStateInformation(const void *data,
                                                   int sizeInBytes) {
  std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
  if (xml == nullptr)
    return;

  const juce::ValueTree tree = juce::ValueTree::fromXml(*xml);
  if (tree.isValid()) {
    apvts.replaceState(tree);
    stemOutputRoutingState.fromValueTree(
        tree.getChildWithName(StemOutputRoutingState::stateTag));
    stemOutputRoutingState.normalizeForDevice(
        standaloneAudioDeviceBridge.getAvailableOutputChannelCount());
    for (int i = 0; i < maxStemCount; ++i) {
      stemOutputChannels[static_cast<size_t>(i)].store(
          stemOutputRoutingState.getStemOutputChannel(i),
          std::memory_order_relaxed);
      stemOutputWidths[static_cast<size_t>(i)].store(
          stemOutputRoutingState.getStemOutputWidth(i),
          std::memory_order_relaxed);
    }
  }
}

juce::AudioProcessorValueTreeState &
MTControlsAudioProcessor::getValueTreeState() {
  return apvts;
}

void MTControlsAudioProcessor::getOutputMeterLevels(float &left,
                                                    float &right) const {
  left = meterLevelLeft.load(std::memory_order_relaxed);
  right = meterLevelRight.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::getDynamicGuideMeterLevels(
    float &left, float &right) const noexcept {
  left = dynamicGuideMeterLevelLeft.load(std::memory_order_relaxed);
  right = dynamicGuideMeterLevelRight.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::getMetronomeMeterLevels(
    float &left, float &right) const noexcept {
  left = metronomeMeterLevelLeft.load(std::memory_order_relaxed);
  right = metronomeMeterLevelRight.load(std::memory_order_relaxed);
}

juce::AudioProcessorValueTreeState::ParameterLayout
MTControlsAudioProcessor::createParameterLayout() {
  std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{inputGainId, 1}, "Input",
      juce::NormalisableRange<float>{-24.0f, 24.0f, 0.01f}, 0.0f,
      juce::AudioParameterFloatAttributes()
          .withLabel("dB")
          .withStringFromValueFunction([](float value, int) {
            return juce::String(value, 1) + " dB";
          })));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{mixId, 1}, "Mix",
      juce::NormalisableRange<float>{0.0f, 100.0f, 0.1f}, 100.0f,
      juce::AudioParameterFloatAttributes()
          .withLabel("%")
          .withStringFromValueFunction(
              [](float value, int) { return juce::String(value, 1) + " %"; })));

  params.push_back(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{bypassId, 1}, "Bypass", false));

  return {params.begin(), params.end()};
}
bool MTControlsAudioProcessor::loadSongStems(
    const juce::StringArray &stemPaths, const juce::String &songTitle,
    const juce::String &songKey, const double songBpm,
    const std::function<void(double)> &onLoadProgress) {
  // Snapshot playback configuration safely before we start building transports.
  double localSampleRate = 0.0;
  int localBlockSize = 0;
  {
    const juce::SpinLock::ScopedLockType cfgLock(stemListLock);
    localSampleRate = playbackSampleRate > 0.0 ? playbackSampleRate : 44100.0;
    localBlockSize = playbackBlockSize > 0 ? playbackBlockSize : 512;
  }
  const bool wasPlayingBeforeLoad = isSongPlaying();

  std::vector<std::unique_ptr<StemTrack>> newTracks;
  if (stemPaths.isEmpty())
    return false;

  const int pathCount = stemPaths.size();
  const int denom = juce::jmax(1, pathCount);

  double maxLengthSeconds = 0.0;
  for (int i = 0; i < pathCount; ++i) {
    if (newTracks.size() >= static_cast<size_t>(maxStemCount))
      break;

    if (onLoadProgress)
      onLoadProgress(static_cast<double>(i) / static_cast<double>(denom));

    const juce::File file(stemPaths[i]);
    if (!file.existsAsFile())
      continue;

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        playbackFormatManager.createReaderFor(file));
    if (reader == nullptr)
      continue;

    const double sampleRate = reader->sampleRate;
    const auto lengthInSamples = reader->lengthInSamples;
    const double lengthSeconds =
        sampleRate > 0.0 ? static_cast<double>(lengthInSamples) / sampleRate
                         : 0.0;
    maxLengthSeconds = juce::jmax(maxLengthSeconds, lengthSeconds);

    const size_t workerIndex =
        newTracks.size() % std::max<size_t>(1, stemReadThreads.size());
    auto track = std::make_unique<StemTrack>(
        std::move(reader), *stemReadThreads[workerIndex], sampleRate, 262144);
        
    track->prepareToPlay(localBlockSize, localSampleRate);
    track->setNextReadPosition(0);
    newTracks.push_back(std::move(track));
  }

  if (newTracks.empty())
    return false;

  if (onLoadProgress)
    onLoadProgress(1.0);

  // Briefly acquire the lock to swap in the new stem list.
  suspendProcessing(true);
  for (int wait = 0;
       wait < 2000 && audioCallbackActive.load(std::memory_order_acquire);
       ++wait) {
    juce::Thread::sleep(1);
  }
  if (audioCallbackActive.load(std::memory_order_acquire)) {
    suspendProcessing(false);
    return false;
  }
  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    const bool canCrossfadeDecks =
        wasPlayingBeforeLoad && songLoaded.load(std::memory_order_relaxed) &&
        !stemTracks.empty();

    if (canCrossfadeDecks) {
      outgoingStemTracks = std::move(stemTracks);
      outgoingStemRenderStorage.setSize(getTotalNumOutputChannels(),
                                        maxRealtimeBlockSize, false, false,
                                        true);
      songCrossfadeActive.store(true, std::memory_order_relaxed);
      songCrossfadeProgress.store(0.0f, std::memory_order_relaxed);
      songCrossfadeIncomingGain.store(0.0f, std::memory_order_relaxed);
      songCrossfadeOutgoingGain.store(1.0f, std::memory_order_relaxed);
    } else {
      outgoingStemTracks.clear();
      songCrossfadeActive.store(false, std::memory_order_relaxed);
      songCrossfadeProgress.store(1.0f, std::memory_order_relaxed);
      songCrossfadeIncomingGain.store(1.0f, std::memory_order_relaxed);
      songCrossfadeOutgoingGain.store(0.0f, std::memory_order_relaxed);
    }
    removeAllStemSources();
    clearLoadedSongState();

    stemTracks = std::move(newTracks);
    resetStemControls(static_cast<int>(stemTracks.size()));
    
    globalSamplePosition.store(0, std::memory_order_relaxed);
    transportClock.reset(0);
    songCurrentPositionSeconds.store(0.0, std::memory_order_relaxed);
    masterVolume.store(1.0f, std::memory_order_relaxed);
    masterGain.store(1.0f, std::memory_order_relaxed);
    masterMuted.store(false, std::memory_order_relaxed);
    requestedSeekPosition.store(-1, std::memory_order_relaxed);

    loadedSongLengthSeconds = maxLengthSeconds;
    songStartSeconds.store(0.0, std::memory_order_relaxed);
    songEndSeconds.store(maxLengthSeconds, std::memory_order_relaxed);
    songFadeOutSeconds.store(juce::jmin(1.0, maxLengthSeconds),
                             std::memory_order_relaxed);
    const double normalizedSongBpm =
        songBpm > 0.0 ? songBpm : 120.0;
    loadedSongBpm =
        juce::jlimit(kMinPlaybackTempoBpm, kMaxPlaybackTempoBpm,
                     normalizedSongBpm);
    playbackTempoBpm.store(loadedSongBpm.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
    setMetronomeBpm(playbackTempoBpm.load(std::memory_order_relaxed));
    {
      const juce::ScopedWriteLock metaLock(songMetaLock);
      loadedSongKey = songKey;
      loadedSongTitle = songTitle;
    }
    songLoaded.store(true, std::memory_order_relaxed);
    arrangementParts.clear();
    ArrangementRuntimePart initialPart;
    initialPart.id = "part-1";
    initialPart.name = "Part";
    initialPart.sectionKey = DynamicGuideEngine::normalizeSectionKey(initialPart.name);
    initialPart.guideTriggerId =
        dynamicGuideEngine.resolveTriggerId(initialPart.sectionKey);
    initialPart.startSec = 0.0;
    initialPart.endSec = maxLengthSeconds;
    initialPart.sourceStartSec = initialPart.startSec;
    initialPart.sourceEndSec = initialPart.endSec;
    arrangementParts.push_back(std::move(initialPart));
    rebuildSectionScheduler();
    arrangementCurrentPartIndex.store(0, std::memory_order_relaxed);
    arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
    arrangementCustomActive.store(false, std::memory_order_relaxed);
    arrangementTotalLengthSeconds.store(maxLengthSeconds,
                                        std::memory_order_relaxed);
    dynamicGuideAnnouncedByPart.assign(arrangementParts.size(), false);
    dynamicGuideLastPartIndex = -1;
    dynamicGuideLastBlockStartSec = -1.0;
    dynamicGuideWasPlaying = false;
    pitchTargetSemiTones.store(0, std::memory_order_relaxed);
    setTransportState(wasPlayingBeforeLoad ? TransportState::playing
                                           : TransportState::stopped);
  }
  suspendProcessing(false);

  return true;
}

void MTControlsAudioProcessor::playSong() {
  // Si el procesador queda suspendido o el dispositivo de audio se cerró
  // (p. ej. tras inactividad prolongada), AudioProcessorPlayer deja de
  // llamar a processBlock: el UI puede mostrar "playing" pero el tiempo no
  // avanza hasta que se reanude el procesamiento y el dispositivo exista.
  suspendProcessing(false);
#if JucePlugin_Build_Standalone
  if (auto *holder = juce::StandalonePluginHolder::getInstance()) {
    auto &dm = holder->deviceManager;
    if (dm.getCurrentAudioDevice() == nullptr)
      dm.restartLastAudioDevice();
  }
#endif
  if (!songLoaded.load(std::memory_order_relaxed))
    return;
  const bool customArrangement =
      arrangementCustomActive.load(std::memory_order_relaxed);
  if (customArrangement) {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    if (arrangementParts.empty())
      return;
    const double current =
        songCurrentPositionSeconds.load(std::memory_order_relaxed);
    int partIndex = findArrangementPartIndexForArrangementTime(current);
    if (juce::isPositiveAndBelow(partIndex,
                                  static_cast<int>(arrangementParts.size()))) {
      const auto &part = arrangementParts[static_cast<size_t>(partIndex)];
      if (current >= part.startSec && current < part.endSec)
        arrangementCurrentPartIndex.store(partIndex, std::memory_order_relaxed);
    } else {
      arrangementCurrentPartIndex.store(partIndex, std::memory_order_relaxed);
    }
  }
  const double start = songStartSeconds.load(std::memory_order_relaxed);
  double end = songEndSeconds.load(std::memory_order_relaxed);
  if (customArrangement) {
    const double arrangementEnd =
        arrangementTotalLengthSeconds.load(std::memory_order_relaxed);
    if (arrangementEnd > start)
      end = arrangementEnd;
  }
  const double current =
      songCurrentPositionSeconds.load(std::memory_order_relaxed);
  if (current < start || (end > 0.0 && current >= end))
    seekSongTo(start);
  else if (customArrangement)
    seekSongTo(current);
  syncMetronomeToSourceSeconds(
      songCurrentPositionSeconds.load(std::memory_order_relaxed));
  setTransportState(TransportState::playing);
  if (isMetronomeEnabled())
    metronome.triggerImmediateClick();
}

void MTControlsAudioProcessor::pauseSong() {
  if (!songLoaded.load(std::memory_order_relaxed))
    return;
  setTransportState(TransportState::paused);
}

void MTControlsAudioProcessor::stopSong() {
  if (!songLoaded.load(std::memory_order_relaxed))
    return;
  setTransportState(TransportState::stopped);
  double start = songStartSeconds.load(std::memory_order_relaxed);
  if (arrangementCustomActive.load(std::memory_order_relaxed))
    start = getArrangementStartSeconds();
  arrangementCurrentPartIndex.store(0, std::memory_order_relaxed);
  arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
  seekTargetSeconds.store(start, std::memory_order_relaxed);
  seekPending.store(true, std::memory_order_release);
  globalSamplePosition.store(
      static_cast<juce::int64>(start * playbackSampleRate),
      std::memory_order_relaxed);
  transportClock.stop(playworship::audio::TransportClock::secondsToFrames(
      start, playbackSampleRate));
  songCurrentPositionSeconds.store(start, std::memory_order_relaxed);
  requestedSeekPosition.store(static_cast<juce::int64>(start * playbackSampleRate),
                              std::memory_order_relaxed);
}

void MTControlsAudioProcessor::toggleSongPlayPause() {
  if (isSongPlaying())
    pauseSong();
  else
    playSong();
}

void MTControlsAudioProcessor::seekSongRelative(const double deltaSeconds) {
  if (!songLoaded.load(std::memory_order_relaxed))
    return;

  if (std::abs(deltaSeconds) < 0.000001)
    return;

  // Use the atomically reported position (safe from any thread).
  const double current =
      songCurrentPositionSeconds.load(std::memory_order_relaxed);
  const double length =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  const bool customArrangement =
      arrangementCustomActive.load(std::memory_order_relaxed);
  const double playbackLength =
      customArrangement
          ? juce::jmax(length, arrangementTotalLengthSeconds.load(
                                   std::memory_order_relaxed))
          : length;
  double start = songStartSeconds.load(std::memory_order_relaxed);
  double end = songEndSeconds.load(std::memory_order_relaxed);
  if (customArrangement)
    end = playbackLength;
  else if (end <= 0.0 || end > playbackLength)
    end = playbackLength;
  start = juce::jlimit(0.0, end, start);
  const double target =
      juce::jlimit(start, end, current + deltaSeconds);

  seekSongTo(target);
}

void MTControlsAudioProcessor::seekSongTo(const double positionSeconds) {
  if (!songLoaded.load(std::memory_order_relaxed))
    return;

  suspendProcessing(false);

  const double length =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  const bool customArrangement =
      arrangementCustomActive.load(std::memory_order_relaxed);
  const double playbackLength =
      customArrangement
          ? juce::jmax(length, arrangementTotalLengthSeconds.load(
                                   std::memory_order_relaxed))
          : length;
  double start = songStartSeconds.load(std::memory_order_relaxed);
  double end = songEndSeconds.load(std::memory_order_relaxed);
  if (customArrangement)
    end = playbackLength;
  else if (end <= 0.0 || end > playbackLength)
    end = playbackLength;
  start = juce::jlimit(0.0, end, start);
  double target = juce::jlimit(start, end, positionSeconds);
  arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
  const int loopPartIndex =
      arrangementLoopPartIndex.load(std::memory_order_relaxed);
  if (loopPartIndex >= 0 || customArrangement) {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    if (juce::isPositiveAndBelow(loopPartIndex,
                                 static_cast<int>(arrangementParts.size()))) {
      const auto &loopPart = arrangementParts[static_cast<size_t>(loopPartIndex)];
      arrangementCurrentPartIndex.store(loopPartIndex, std::memory_order_relaxed);
      target = juce::jlimit(loopPart.startSec, loopPart.endSec, target);
    } else if (customArrangement && !arrangementParts.empty()) {
      int partIndex = findArrangementPartIndexForArrangementTime(target);
      if (juce::isPositiveAndBelow(partIndex,
                                    static_cast<int>(arrangementParts.size()))) {
        const auto &part = arrangementParts[static_cast<size_t>(partIndex)];
        arrangementCurrentPartIndex.store(
            target >= part.startSec && target < part.endSec ? partIndex : -1,
            std::memory_order_relaxed);
      } else {
        arrangementCurrentPartIndex.store(-1, std::memory_order_relaxed);
      }
    }
  }

  // Write target atomically. processBlock will pick this up and apply it
  // to ALL stems in the same audio callback → zero inter-stem desync.
  seekTargetSeconds.store(target, std::memory_order_relaxed);
  seekPending.store(true, std::memory_order_release);

  songCurrentPositionSeconds.store(target, std::memory_order_relaxed);
  if (playbackSampleRate > 0.0) {
    const juce::int64 seekSamples =
        static_cast<juce::int64>(target * playbackSampleRate);
    globalSamplePosition.store(seekSamples, std::memory_order_relaxed);
    transportClock.seek(seekSamples);
  }
}

void MTControlsAudioProcessor::setSongStartSeconds(double seconds) noexcept {
  const double length =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  double start = juce::jlimit(0.0, length, seconds);
  double end = songEndSeconds.load(std::memory_order_relaxed);
  if (end <= 0.0 || end > length)
    end = length;
  end = juce::jlimit(start, length, end);
  songStartSeconds.store(start, std::memory_order_relaxed);
  songEndSeconds.store(end, std::memory_order_relaxed);

  const double maxFade = juce::jmax(0.0, end - start);
  const double fadeOut = songFadeOutSeconds.load(std::memory_order_relaxed);
  songFadeOutSeconds.store(juce::jlimit(0.0, maxFade, fadeOut),
                           std::memory_order_relaxed);

  const double current =
      songCurrentPositionSeconds.load(std::memory_order_relaxed);
  if (current < start)
    seekSongTo(start);
}

void MTControlsAudioProcessor::setSongEndSeconds(double seconds) noexcept {
  const double length =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  double start = songStartSeconds.load(std::memory_order_relaxed);
  double end = juce::jlimit(0.0, length, seconds);
  start = juce::jlimit(0.0, end, start);
  end = juce::jlimit(start, length, end);
  songStartSeconds.store(start, std::memory_order_relaxed);
  songEndSeconds.store(end, std::memory_order_relaxed);

  const double maxFade = juce::jmax(0.0, end - start);
  const double fadeOut = songFadeOutSeconds.load(std::memory_order_relaxed);
  songFadeOutSeconds.store(juce::jlimit(0.0, maxFade, fadeOut),
                           std::memory_order_relaxed);

  const double current =
      songCurrentPositionSeconds.load(std::memory_order_relaxed);
  if (current > end) {
    seekSongTo(end);
    if (isSongPlaying())
      setTransportState(TransportState::stopped);
  }
}

void MTControlsAudioProcessor::setSongFadeOutSeconds(double seconds) noexcept {
  const double start = songStartSeconds.load(std::memory_order_relaxed);
  double end = songEndSeconds.load(std::memory_order_relaxed);
  const double length =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  if (end <= 0.0 || end > length)
    end = length;
  const double maxFade = juce::jmax(0.0, end - start);
  songFadeOutSeconds.store(juce::jlimit(0.0, maxFade, seconds),
                           std::memory_order_relaxed);
}

double MTControlsAudioProcessor::getSongStartSeconds() const noexcept {
  const double length =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  double end = songEndSeconds.load(std::memory_order_relaxed);
  if (end <= 0.0 || end > length)
    end = length;
  const double start =
      juce::jlimit(0.0, end, songStartSeconds.load(std::memory_order_relaxed));
  return start;
}

double MTControlsAudioProcessor::getSongEndSeconds() const noexcept {
  const double length =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  double end = songEndSeconds.load(std::memory_order_relaxed);
  if (end <= 0.0 || end > length)
    end = length;
  return end;
}

double MTControlsAudioProcessor::getSongFadeOutSeconds() const noexcept {
  const double start = getSongStartSeconds();
  const double end = getSongEndSeconds();
  const double maxFade = juce::jmax(0.0, end - start);
  const double fade =
      songFadeOutSeconds.load(std::memory_order_relaxed);
  return juce::jlimit(0.0, maxFade, fade);
}

void MTControlsAudioProcessor::setSongArrangement(
    const SongArrangementSnapshot &arrangement, bool isCustomArrangement) {
  constexpr double kRuntimeMinPartDurationSec = 0.25;
  const juce::SpinLock::ScopedLockType lock(stemListLock);
  const double songLength =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);

  std::vector<ArrangementRuntimePart> next;
  next.reserve(arrangement.parts.size());
  double totalLength = 0.0;
  double arrangementLength = songLength;
  double insertedLength = 0.0;
  for (const auto &part : arrangement.parts) {
    if (std::isfinite(part.endSec))
      arrangementLength = juce::jmax(arrangementLength, part.endSec);
    if (part.sourcePartId.isNotEmpty() && part.endSec > part.startSec)
      insertedLength += part.endSec - part.startSec;
  }
  const auto resolveSourceRange =
      [&arrangement, songLength](const SongArrangementPartSnapshot &part) {
        if (part.sourcePartId.isEmpty()) {
          return std::pair<double, double>{
              juce::jlimit(0.0, songLength, part.startSec),
              juce::jlimit(0.0, songLength, part.endSec)};
        }

        if (part.sourceEndSec > part.sourceStartSec) {
          double sourceStart = juce::jlimit(0.0, songLength, part.sourceStartSec);
          double sourceEnd = juce::jlimit(0.0, songLength, part.sourceEndSec);
          if (sourceEnd < sourceStart)
            std::swap(sourceStart, sourceEnd);
          return std::pair<double, double>{sourceStart, sourceEnd};
        }

        const SongArrangementPartSnapshot *source = &part;
        const int maxDepth = static_cast<int>(arrangement.parts.size());
        for (int depth = 0; depth < maxDepth; ++depth) {
          if (source->sourcePartId.isEmpty())
            break;

          const SongArrangementPartSnapshot *nextSource = nullptr;
          for (const auto &candidate : arrangement.parts) {
            if (candidate.id == source->sourcePartId) {
              nextSource = &candidate;
              break;
            }
          }

          if (nextSource == nullptr || nextSource == source)
            break;
          source = nextSource;
        }

        double sourceStart =
            source->sourcePartId.isNotEmpty() &&
                    source->sourceEndSec > source->sourceStartSec
                ? juce::jlimit(0.0, songLength, source->sourceStartSec)
                : juce::jlimit(0.0, songLength, source->startSec);
        double sourceEnd =
            source->sourcePartId.isNotEmpty() &&
                    source->sourceEndSec > source->sourceStartSec
                ? juce::jlimit(0.0, songLength, source->sourceEndSec)
                : juce::jlimit(0.0, songLength, source->endSec);
        if (sourceEnd < sourceStart)
          std::swap(sourceStart, sourceEnd);
        return std::pair<double, double>{sourceStart, sourceEnd};
      };

  for (const auto &part : arrangement.parts) {
    double start = juce::jlimit(0.0, arrangementLength, part.startSec);
    double end = juce::jlimit(0.0, arrangementLength, part.endSec);
    if (end < start)
      std::swap(start, end);
    if (songLength > 0.0 && (end - start) < kRuntimeMinPartDurationSec)
      continue;
    ArrangementRuntimePart runtimePart;
    runtimePart.id = part.id.trim();
    runtimePart.name = part.name.trim();
    if (runtimePart.id.isEmpty())
      runtimePart.id = juce::String("part-") + juce::String(static_cast<int>(next.size()) + 1);
    if (runtimePart.name.isEmpty())
      runtimePart.name = "Part";
    runtimePart.sectionKey = DynamicGuideEngine::normalizeSectionKey(runtimePart.name);
    runtimePart.guideTriggerId =
        dynamicGuideEngine.resolveTriggerId(runtimePart.sectionKey);
    runtimePart.startSec = start;
    runtimePart.endSec = end;
    const auto [sourceStart, sourceEnd] = resolveSourceRange(part);
    runtimePart.sourceStartSec = sourceStart;
    runtimePart.sourceEndSec =
        sourceEnd > sourceStart
            ? sourceEnd
            : juce::jmin(songLength, sourceStart + (end - start));
    runtimePart.insertedClip = part.sourcePartId.isNotEmpty();
    runtimePart.muteInstruments = part.muteInstruments;
    runtimePart.skipPlayback = part.skipPlayback;
    if (songLength > 0.0 &&
        (runtimePart.sourceEndSec - runtimePart.sourceStartSec) <
            kRuntimeMinPartDurationSec)
      continue;
    next.push_back(std::move(runtimePart));
    totalLength = juce::jmax(totalLength, end);
  }
  if (isCustomArrangement && songLength > 0.0 && insertedLength > 0.0)
    totalLength = juce::jmax(totalLength, songLength + insertedLength);

  if (next.size() > 1) {
    std::stable_sort(
        next.begin(), next.end(),
        [](const ArrangementRuntimePart &a, const ArrangementRuntimePart &b) {
          if (std::abs(a.startSec - b.startSec) <= 0.0001)
            return a.endSec < b.endSec;
          return a.startSec < b.startSec;
        });
  }

  // Updating a section's playback state (for example its Mute button) must
  // not reset the transport. Only a change that moves, resizes, or remaps a
  // clip requires seeking to the beginning of a custom arrangement.
  const auto sameTimeline = [this, &next] {
    if (arrangementParts.size() != next.size())
      return false;

    constexpr double kTimelineToleranceSec = 0.0001;
    for (size_t i = 0; i < next.size(); ++i) {
      const auto &current = arrangementParts[i];
      const auto &updated = next[i];
      if (current.insertedClip != updated.insertedClip ||
          std::abs(current.startSec - updated.startSec) >
              kTimelineToleranceSec ||
          std::abs(current.endSec - updated.endSec) >
              kTimelineToleranceSec ||
          std::abs(current.sourceStartSec - updated.sourceStartSec) >
              kTimelineToleranceSec ||
          std::abs(current.sourceEndSec - updated.sourceEndSec) >
              kTimelineToleranceSec)
        return false;
    }
    return true;
  };
  const bool arrangementTimelineChanged = !sameTimeline();

  for (auto &part : next) {
    const auto countKey = arrangementCountSectionKey(part.name);
    if (countKey.isNotEmpty()) {
      part.sectionKey = countKey;
      part.guideTriggerId =
          dynamicGuideEngine.resolveTriggerId(part.sectionKey);
      continue;
    }

    part.sectionKey = arrangementSectionKey(part.name);
    part.guideTriggerId =
        dynamicGuideEngine.resolveTriggerId(part.sectionKey);
    if (part.guideTriggerId < 0) {
      const auto baseKey = baseArrangementSectionKey(part.name);
      if (baseKey.isNotEmpty() && baseKey != part.sectionKey) {
        const int baseTriggerId = dynamicGuideEngine.resolveTriggerId(baseKey);
        if (baseTriggerId >= 0) {
          part.sectionKey = baseKey;
          part.guideTriggerId = baseTriggerId;
        }
      }
    }
  }

  if (next.empty()) {
    ArrangementRuntimePart runtimePart;
    runtimePart.id = "part-1";
    runtimePart.name = "Part";
    runtimePart.sectionKey = DynamicGuideEngine::normalizeSectionKey(runtimePart.name);
    runtimePart.guideTriggerId =
        dynamicGuideEngine.resolveTriggerId(runtimePart.sectionKey);
    runtimePart.startSec = 0.0;
    runtimePart.endSec = songLength;
    runtimePart.sourceStartSec = runtimePart.startSec;
    runtimePart.sourceEndSec = runtimePart.endSec;
    runtimePart.muteInstruments = false;
    runtimePart.skipPlayback = false;
    next.push_back(std::move(runtimePart));
    totalLength = songLength;
    isCustomArrangement = false;
  }

  arrangementParts = std::move(next);
  rebuildSectionScheduler();
  dynamicGuideAnnouncedByPart.assign(arrangementParts.size(), false);
  dynamicGuideLastPartIndex = -1;
  dynamicGuideLastBlockStartSec = -1.0;
  arrangementTotalLengthSeconds.store(totalLength, std::memory_order_relaxed);
  arrangementCustomActive.store(isCustomArrangement, std::memory_order_relaxed);
  arrangementCurrentPartIndex.store(0, std::memory_order_relaxed);
  arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
  arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);

  if (isCustomArrangement && arrangementTimelineChanged) {
    const double target = arrangementParts.front().startSec;
    seekTargetSeconds.store(target, std::memory_order_relaxed);
    seekPending.store(true, std::memory_order_release);
    songCurrentPositionSeconds.store(target, std::memory_order_relaxed);
    if (playbackSampleRate > 0.0) {
      globalSamplePosition.store(
          static_cast<juce::int64>(target * playbackSampleRate),
          std::memory_order_relaxed);
      transportClock.seek(
          playworship::audio::TransportClock::secondsToFrames(
              target, playbackSampleRate));
    }
    if (getTransportState() == TransportState::playing)
      setTransportState(TransportState::stopped);
  }
}

void MTControlsAudioProcessor::requestArrangementPartJump(const int partIndex) {
  if (!songLoaded.load(std::memory_order_relaxed))
    return;

  double targetStartSec = 0.0;
  bool shouldSeekNow = false;

  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    if (!juce::isPositiveAndBelow(partIndex,
                                  static_cast<int>(arrangementParts.size())))
      return;

    const bool playing = getTransportState() == TransportState::playing;
    const double currentSec =
        songCurrentPositionSeconds.load(std::memory_order_relaxed);
    const int currentPartIndex =
        findArrangementPartIndexForArrangementTime(currentSec);
    const int pendingPartIndex =
        arrangementPendingJumpPartIndex.load(std::memory_order_relaxed);

    if (playing) {
      // En reproduccion, queda en cola y se ejecuta al final de la seccion actual.
      if (pendingPartIndex == partIndex) {
        arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
        return;
      }
      if (partIndex == currentPartIndex) {
        arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
        return;
      }
      arrangementPendingJumpPartIndex.store(partIndex, std::memory_order_relaxed);
      return;
    }

    arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
    arrangementCurrentPartIndex.store(partIndex, std::memory_order_relaxed);
    targetStartSec = arrangementParts[static_cast<size_t>(partIndex)].startSec;
    shouldSeekNow = true;
  }

  if (shouldSeekNow)
    seekSongTo(targetStartSec);
}

void MTControlsAudioProcessor::toggleArrangementPartLoop(const int partIndex) {
  if (!songLoaded.load(std::memory_order_relaxed))
    return;

  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    if (!juce::isPositiveAndBelow(partIndex,
                                  static_cast<int>(arrangementParts.size())))
      return;

    const int activeLoopIndex =
        arrangementLoopPartIndex.load(std::memory_order_relaxed);
    if (activeLoopIndex == partIndex) {
      arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
      return;
    }

    const auto &loopPart = arrangementParts[static_cast<size_t>(partIndex)];
    const bool isPlayingNow =
        getTransportState() == TransportState::playing;
    const double currentSec =
        songCurrentPositionSeconds.load(std::memory_order_relaxed);
    const bool alreadyInsideLoopRange =
        currentSec >= loopPart.startSec && currentSec < loopPart.endSec;

    arrangementLoopPartIndex.store(partIndex, std::memory_order_relaxed);
    arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
    // En reproducción, si el transporte está fuera de la sección objetivo,
    // dejamos un salto en cola para garantizar que el loop entre también en
    // modo Follow sin intervención manual.
    if (isPlayingNow && !alreadyInsideLoopRange)
      arrangementPendingJumpPartIndex.store(partIndex, std::memory_order_relaxed);
  }
}

void MTControlsAudioProcessor::clearArrangementPartLoop() noexcept {
  arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
}

void MTControlsAudioProcessor::clearPendingArrangementPartJump() noexcept {
  arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
}

int MTControlsAudioProcessor::getArrangementPartLoopIndex() const noexcept {
  return arrangementLoopPartIndex.load(std::memory_order_relaxed);
}

int MTControlsAudioProcessor::getPendingArrangementPartJumpIndex() const noexcept {
  return arrangementPendingJumpPartIndex.load(std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isCustomArrangementActive() const noexcept {
  return arrangementCustomActive.load(std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::hasLoadedSong() const noexcept {
  return songLoaded.load(std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isSongPlaying() const noexcept {
  return getTransportState() == TransportState::playing;
}

double MTControlsAudioProcessor::getSongCurrentPositionSeconds() const {
  return transportClock.getPositionSeconds();
}

double MTControlsAudioProcessor::getSongLengthSeconds() const {
  double length = loadedSongLengthSeconds.load(std::memory_order_relaxed);
  if (arrangementCustomActive.load(std::memory_order_relaxed))
    length = juce::jmax(length, arrangementTotalLengthSeconds.load(
                                    std::memory_order_relaxed));
  return length;
}

double MTControlsAudioProcessor::getLoadedSongBpm() const {
  return loadedSongBpm.load(std::memory_order_relaxed);
}

juce::String MTControlsAudioProcessor::getLoadedSongKey() const {
  const juce::ScopedReadLock lock(songMetaLock);
  return loadedSongKey;
}

juce::String MTControlsAudioProcessor::getLoadedSongTitle() const {
  const juce::ScopedReadLock lock(songMetaLock);
  return loadedSongTitle;
}

int MTControlsAudioProcessor::getLoadedStemCount() const noexcept {
  return loadedStemCount.load(std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getStemLevel(
    const int stemIndex) const noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return 0.0f;

  return stemLevels[static_cast<size_t>(stemIndex)].load(
      std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setStemVolume(const int stemIndex,
                                             const float volume) noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return;

  stemVolumes[static_cast<size_t>(stemIndex)].store(
      juce::jlimit(0.0f, 1.0f, volume), std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getStemVolume(
    const int stemIndex) const noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return 1.0f;

  return stemVolumes[static_cast<size_t>(stemIndex)].load(
      std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setStemGain(const int stemIndex,
                                           const float gainValue) noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return;

  stemGains[static_cast<size_t>(stemIndex)].store(
      juce::jlimit(0.0f, 2.0f, gainValue), std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getStemGain(
    const int stemIndex) const noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return 1.0f;

  return stemGains[static_cast<size_t>(stemIndex)].load(
      std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setStemMute(const int stemIndex,
                                           const bool muted) noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return;

  stemMuteStates[static_cast<size_t>(stemIndex)].store(
      muted, std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isStemMuted(const int stemIndex) const noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return false;

  return stemMuteStates[static_cast<size_t>(stemIndex)].load(
      std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setStemSolo(const int stemIndex,
                                           const bool soloed) noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return;

  stemSoloStates[static_cast<size_t>(stemIndex)].store(
      soloed, std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isStemSoloed(
    const int stemIndex) const noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return false;

  return stemSoloStates[static_cast<size_t>(stemIndex)].load(
      std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setStemOutputChannel(
    const int stemIndex, const int outputChannel) noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return;

  const int width =
      stemOutputRoutingState.getStemOutputWidth(stemIndex);
  const int maxChannels =
      juce::jmax(1, standaloneAudioDeviceBridge.getAvailableOutputChannelCount());
  const int maxBase = juce::jmax(1, maxChannels - (width >= 2 ? 1 : 0));
  const int normalizedChannel = juce::jlimit(
      1, maxBase,
      outputChannel);
  stemOutputChannels[static_cast<size_t>(stemIndex)].store(
      normalizedChannel, std::memory_order_relaxed);
  stemOutputRoutingState.setStemOutputChannel(stemIndex, normalizedChannel);
}

int MTControlsAudioProcessor::getStemOutputChannel(const int stemIndex) const noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return 1;

  return stemOutputChannels[static_cast<size_t>(stemIndex)].load(
      std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setStemOutputWidth(const int stemIndex,
                                                  const int width) noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return;

  const int normalizedWidth = juce::jlimit(1, 2, width);
  stemOutputWidths[static_cast<size_t>(stemIndex)].store(
      normalizedWidth, std::memory_order_relaxed);
  stemOutputRoutingState.setStemOutputWidth(stemIndex, normalizedWidth);

  const int currentChannel =
      stemOutputChannels[static_cast<size_t>(stemIndex)].load(
          std::memory_order_relaxed);
  setStemOutputChannel(stemIndex, currentChannel);
}

int MTControlsAudioProcessor::getStemOutputWidth(
    const int stemIndex) const noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return 1;

  return stemOutputWidths[static_cast<size_t>(stemIndex)].load(
      std::memory_order_relaxed);
}

juce::StringArray MTControlsAudioProcessor::getAvailableOutputChannelNames() const {
  return standaloneAudioDeviceBridge.getOutputChannelNames();
}

int MTControlsAudioProcessor::getAvailableOutputChannelCount() const noexcept {
  return standaloneAudioDeviceBridge.getAvailableOutputChannelCount();
}

AppAudioMode MTControlsAudioProcessor::getCurrentAudioMode() const noexcept {
  return standaloneAudioDeviceBridge.getCurrentAudioMode();
}

StandaloneAudioDeviceBridge &
MTControlsAudioProcessor::getAudioDeviceBridge() noexcept {
  return standaloneAudioDeviceBridge;
}

const StandaloneAudioDeviceBridge &
MTControlsAudioProcessor::getAudioDeviceBridge() const noexcept {
  return standaloneAudioDeviceBridge;
}

void MTControlsAudioProcessor::setMasterVolume(const float volume) noexcept {
  masterVolume.store(juce::jlimit(0.0f, 1.0f, volume),
                     std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getMasterVolume() const noexcept {
  return masterVolume.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMasterGain(const float gainValue) noexcept {
  masterGain.store(juce::jlimit(0.0f, 2.0f, gainValue),
                   std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getMasterGain() const noexcept {
  return masterGain.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMasterMute(const bool muted) noexcept {
  masterMuted.store(muted, std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isMasterMuted() const noexcept {
  return masterMuted.load(std::memory_order_relaxed);
}

juce::String MTControlsAudioProcessor::buildPadsPackHash(
    const juce::Array<juce::File> &files) {
  juce::int64 accumulator = 1469598103934665603LL;
  for (const auto &f : files) {
    accumulator ^= f.getSize();
    accumulator *= 1099511628211LL;
    accumulator ^= f.getLastModificationTime().toMilliseconds();
    accumulator *= 1099511628211LL;
    accumulator ^= f.getFileName().hashCode64();
    accumulator *= 1099511628211LL;
  }
  return juce::String::toHexString(static_cast<long long>(accumulator));
}

bool MTControlsAudioProcessor::loadPadsPackInternal(
    const juce::File &folderToLoad, const juce::String &sourcePathForSettings,
    const bool persistSettings) {
  const juce::SpinLock::ScopedLockType lock(padsStateLock);
  auto load = padsEngine.loadFromFolder(folderToLoad, playbackFormatManager);
  padsPackLoaded.store(load.ok, std::memory_order_relaxed);
  padsPackStatusText = load.statusText;

  if (persistSettings) {
    PadsPackSettings settings;
    settings.folderPath = folderToLoad.getFullPathName();
    settings.sourcePath = sourcePathForSettings;
    const auto files = folderToLoad.findChildFiles(
        juce::File::findFiles, false, "*.mp3;*.wav;*.aif;*.aiff");
    settings.hash = buildPadsPackHash(files);
    settings.importedAtMs = juce::Time::currentTimeMillis();
    settings.valid = load.ok;
    settings.status = load.statusText;
    AppSettings::setPadsPackSettings(settings);
  }
  return load.ok;
}

bool MTControlsAudioProcessor::loadPadsPackFromFolder(
    const juce::File &sourceFolder) {
  if (!sourceFolder.isDirectory())
    return false;

  auto targetRoot =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("MTControls")
          .getChildFile("PadsPacks")
          .getChildFile("RomanVizcaino");
  if (targetRoot.exists())
    targetRoot.deleteRecursively();
  targetRoot.createDirectory();

  const auto files = sourceFolder.findChildFiles(
      juce::File::findFiles, false, "*.mp3;*.wav;*.aif;*.aiff");
  for (const auto &file : files) {
    const auto dest = targetRoot.getChildFile(file.getFileName());
    if (!file.copyFileTo(dest)) {
      const juce::SpinLock::ScopedLockType lock(padsStateLock);
      padsPackLoaded.store(false, std::memory_order_relaxed);
      padsPackStatusText = "Import failed";
      return false;
    }
  }

  return loadPadsPackInternal(targetRoot, sourceFolder.getFullPathName(), true);
}

bool MTControlsAudioProcessor::reloadLastPadsPack() {
  const auto settings = AppSettings::getPadsPackSettings();
  if (settings.folderPath.isEmpty())
    return false;
  const juce::File folder(settings.folderPath);
  if (!folder.isDirectory())
    return false;
  return loadPadsPackInternal(folder, settings.sourcePath, false);
}

bool MTControlsAudioProcessor::ensurePadsPackReady() {
  refreshPadsRoutingFromSettings();
  if (padsPackLoaded.load(std::memory_order_relaxed))
    return true;
  {
    const juce::SpinLock::ScopedLockType lock(padsStateLock);
    padsPackStatusText = "Loading...";
  }

  {
    juce::String assetsError;
    const auto managedPadsFolder = AssetBootstrapManager::getPadsPackDirectory();
    {
      const juce::SpinLock::ScopedLockType lock(padsStateLock);
      padsPackStatusText = "Descargando recursos... / Downloading resources...";
    }
    if (AssetBootstrapManager::ensurePadsPackReady(&assetsError) &&
        managedPadsFolder.isDirectory() &&
        loadPadsPackInternal(managedPadsFolder, managedPadsFolder.getFullPathName(), true)) {
      return true;
    }
    if (assetsError.isNotEmpty()) {
      const juce::SpinLock::ScopedLockType lock(padsStateLock);
      padsPackStatusText = assetsError;
    }
  }

  const auto settings = AppSettings::getPadsPackSettings();
  const auto romanSource = getPadsSourceFolderRomanVizcaino();
  const juce::String romanPath = romanSource.getFullPathName();
  const bool shouldPreferRomanImport =
      romanSource.isDirectory() &&
      !settings.sourcePath.equalsIgnoreCase(romanPath);

  if (shouldPreferRomanImport && loadPadsPackFromFolder(romanSource))
    return true;
  if (reloadLastPadsPack())
    return true;
  if (romanSource.isDirectory() && loadPadsPackFromFolder(romanSource))
    return true;

  const auto empirikFallback = getPadsSourceFolderEmpirikFallback();
  if (empirikFallback.isDirectory() && loadPadsPackFromFolder(empirikFallback))
    return true;

  const juce::SpinLock::ScopedLockType lock(padsStateLock);
  padsPackStatusText = "Missing files";
  return false;
}

void MTControlsAudioProcessor::refreshPadsRoutingFromSettings() noexcept {
  juce::String deviceKey;
#if JucePlugin_Build_Standalone
  if (auto *holder = juce::StandalonePluginHolder::getInstance())
    deviceKey = buildDeviceKeyFromManager(holder->deviceManager);
#endif

  {
    if (deviceKey.isEmpty()) {
      auto &bridgeManager = standaloneAudioDeviceBridge.getAudioDeviceManager();
      deviceKey = buildDeviceKeyFromManager(bridgeManager);
    }
  }

  auto defaultsByDevice = AppSettings::getRoutingDefaultsForDevice(deviceKey);
  CategoryRoutingDefaults padsDefaults = AppSettings::getRoutingDefaultsForCategory("Pads/Ambience");
  for (size_t i = 0; i < kTrackCategories.size(); ++i) {
    if (!juce::String(kTrackCategories[i]).equalsIgnoreCase("Pads/Ambience"))
      continue;
    if (i < defaultsByDevice.size())
      padsDefaults = defaultsByDevice[i];
    break;
  }

  int width = juce::jlimit(1, 2, padsDefaults.width);
  const int availableChannels = juce::jmax(1, getAvailableOutputChannelCount());
  if (availableChannels < 2)
    width = 1;
  const int maxBaseChannel =
      juce::jmax(1, availableChannels - (width >= 2 ? 1 : 0));
  const int channel = juce::jlimit(1, maxBaseChannel, padsDefaults.baseChannel);
  padsOutputChannel.store(channel, std::memory_order_relaxed);
  padsOutputWidth.store(width, std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setPadsEnabled(const bool enabled) noexcept {
  padsEnabledState.store(enabled, std::memory_order_relaxed);
  const juce::SpinLock::ScopedLockType lock(padsStateLock);
  padsEngine.setEnabled(enabled);
}

bool MTControlsAudioProcessor::isPadsEnabled() const noexcept {
  return padsEnabledState.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setPadsVolume(const float volume) noexcept {
  padsVolume.store(juce::jlimit(0.0f, 1.0f, volume),
                   std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getPadsVolume() const noexcept {
  return padsVolume.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setPadsGain(const float gainValue) noexcept {
  padsGain.store(juce::jlimit(0.0f, 1.2f, gainValue),
                 std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getPadsGain() const noexcept {
  return padsGain.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setPadsCrossfadePreset(
    const PadsEngine::CrossfadePreset preset) noexcept {
  const juce::SpinLock::ScopedLockType lock(padsStateLock);
  padsEngine.setCrossfadePreset(preset);
}

PadsEngine::CrossfadePreset
MTControlsAudioProcessor::getPadsCrossfadePreset() const noexcept {
  const juce::SpinLock::ScopedTryLockType lock(padsStateLock);
  if (lock.isLocked())
    return padsEngine.getCrossfadePreset();
  return PadsEngine::CrossfadePreset::Medium;
}

void MTControlsAudioProcessor::triggerPadNote(const int noteIndex,
                                              const int octave) {
  if (!padsPackLoaded.load(std::memory_order_relaxed))
    ensurePadsPackReady();
  if (!padsPackLoaded.load(std::memory_order_relaxed))
    return;
  const juce::SpinLock::ScopedLockType lock(padsStateLock);
  padsEngine.triggerNote(noteIndex, octave);
}

void MTControlsAudioProcessor::stopPadNote(const int noteIndex) {
  const juce::SpinLock::ScopedLockType lock(padsStateLock);
  padsEngine.stopNote(noteIndex);
}

float MTControlsAudioProcessor::getPadsOutputLevel() const noexcept {
  return padsEngine.getOutputLevel();
}

bool MTControlsAudioProcessor::isPadsPackLoaded() const noexcept {
  return padsPackLoaded.load(std::memory_order_relaxed);
}

juce::String MTControlsAudioProcessor::getPadsPackStatusText() const {
  const juce::SpinLock::ScopedTryLockType lock(padsStateLock);
  if (lock.isLocked())
    return padsPackStatusText;

  if (padsPackLoaded.load(std::memory_order_relaxed))
    return "Loaded";
  return "Loading...";
}

void MTControlsAudioProcessor::setFadeActive(bool shouldFadeOut) noexcept {
  fadeActive.store(shouldFadeOut, std::memory_order_relaxed);
  fadeTarget.store(shouldFadeOut ? 0.0f : 1.0f, std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isFadeActive() const noexcept {
  return fadeActive.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setFadeStopsPlayback(bool shouldStop) noexcept {
  fadeStopsPlayback.store(shouldStop, std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isFadeStopsPlayback() const noexcept {
  return fadeStopsPlayback.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMetronomeEnabled(bool enabled) noexcept {
  metronome.setEnabled(enabled);
  metronome.reset();
  if (enabled) {
    const double currentSec =
        songCurrentPositionSeconds.load(std::memory_order_relaxed);
    syncMetronomeToSourceSeconds(currentSec);
    if (getTransportState() == TransportState::playing)
      metronome.triggerImmediateClick();
  }
}

bool MTControlsAudioProcessor::isMetronomeEnabled() const noexcept {
  return metronome.isEnabled();
}

void MTControlsAudioProcessor::setMetronomeBpm(double bpm) noexcept {
  metronome.setBpm(bpm);
}

double MTControlsAudioProcessor::getMetronomeBpm() const noexcept {
  return metronome.getBpm();
}

void MTControlsAudioProcessor::setPlaybackTempoBpm(double bpm) noexcept {
  const double rawOriginalBpm = loadedSongBpm.load(std::memory_order_relaxed);
  double minBpm = kMinPlaybackTempoBpm;
  double maxBpm = kMaxPlaybackTempoBpm;

  if (rawOriginalBpm > 0.0) {
    const double originalBpm = juce::jmax(kMinPlaybackTempoBpm, rawOriginalBpm);
    minBpm = juce::jmax(kMinPlaybackTempoBpm,
                        originalBpm * kMinPlaybackTempoRatio);
    maxBpm = juce::jmin(kMaxPlaybackTempoBpm,
                        originalBpm * kMaxPlaybackTempoRatio);
    if (maxBpm < minBpm)
      maxBpm = minBpm;
  }

  const double normalized = juce::jlimit(minBpm, maxBpm, bpm);
  playbackTempoBpm.store(normalized, std::memory_order_relaxed);
  setMetronomeBpm(normalized);
}

double MTControlsAudioProcessor::getPlaybackTempoBpm() const noexcept {
  return playbackTempoBpm.load(std::memory_order_relaxed);
}

double MTControlsAudioProcessor::getOriginalSongBpm() const noexcept {
  return loadedSongBpm.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMetronomeBeatsPerBar(int beats) noexcept {
  metronome.setBeatsPerBar(beats);
}

void MTControlsAudioProcessor::setMetronomeVolume(const float volume) noexcept {
  metronomeVolumeState.store(juce::jlimit(0.0f, 1.0f, volume),
                             std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getMetronomeVolume() const noexcept {
  return metronomeVolumeState.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMetronomeMuted(const bool muted) noexcept {
  metronomeMutedState.store(muted, std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isMetronomeMuted() const noexcept {
  return metronomeMutedState.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMetronomeGain(
    const float gainValue) noexcept {
  metronomeGainState.store(juce::jlimit(0.0f, 4.0f, gainValue),
                           std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getMetronomeGain() const noexcept {
  return metronomeGainState.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMetronomeSolo(const bool soloed) noexcept {
  metronomeSoloState.store(soloed, std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isMetronomeSolo() const noexcept {
  return metronomeSoloState.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMetronomeOutputRouting(
    const int outputChannel, const int width) noexcept {
  const int availableChannels =
      juce::jmax(1, getAvailableOutputChannelCount());
  int normalizedWidth = juce::jlimit(1, 2, width);
  int normalizedChannel = juce::jlimit(1, availableChannels, outputChannel);
  if (normalizedWidth >= 2 && normalizedChannel >= availableChannels)
    normalizedChannel = juce::jmax(1, availableChannels - 1);

  metronomeOutputChannel.store(normalizedChannel, std::memory_order_relaxed);
  metronomeOutputWidth.store(normalizedWidth, std::memory_order_relaxed);
}

int MTControlsAudioProcessor::getMetronomeOutputChannel() const noexcept {
  const int clickStemIndex =
      metronomeClickStemIndex.load(std::memory_order_relaxed);
  const int stemCount = loadedStemCount.load(std::memory_order_relaxed);
  if (juce::isPositiveAndBelow(clickStemIndex, stemCount)) {
    return stemOutputChannels[static_cast<size_t>(clickStemIndex)].load(
        std::memory_order_relaxed);
  }
  return metronomeOutputChannel.load(std::memory_order_relaxed);
}

int MTControlsAudioProcessor::getMetronomeOutputWidth() const noexcept {
  const int clickStemIndex =
      metronomeClickStemIndex.load(std::memory_order_relaxed);
  const int stemCount = loadedStemCount.load(std::memory_order_relaxed);
  if (juce::isPositiveAndBelow(clickStemIndex, stemCount)) {
    return stemOutputWidths[static_cast<size_t>(clickStemIndex)].load(
        std::memory_order_relaxed);
  }
  return metronomeOutputWidth.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setDynamicGuideVolume(
    const float volume) noexcept {
  dynamicGuideVolumeState.store(juce::jlimit(0.0f, 1.0f, volume),
                                std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getDynamicGuideVolume() const noexcept {
  return dynamicGuideVolumeState.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setDynamicGuideGain(
    const float gainValue) noexcept {
  dynamicGuideGainState.store(juce::jlimit(0.0f, 2.0f, gainValue),
                              std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getDynamicGuideGain() const noexcept {
  return dynamicGuideGainState.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setDynamicGuideMuted(const bool muted) noexcept {
  dynamicGuideMutedState.store(muted, std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isDynamicGuideMuted() const noexcept {
  return dynamicGuideMutedState.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setDynamicGuideSolo(const bool soloed) noexcept {
  dynamicGuideSoloState.store(soloed, std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isDynamicGuideSolo() const noexcept {
  return dynamicGuideSoloState.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setDynamicGuideOutputRouting(
    const int outputChannel, const int width) noexcept {
  const int availableChannels = juce::jmax(1, getAvailableOutputChannelCount());
  int normalizedWidth = juce::jlimit(1, 2, width);
  int normalizedChannel = juce::jlimit(1, availableChannels, outputChannel);
  if (normalizedWidth >= 2 && normalizedChannel >= availableChannels)
    normalizedChannel = juce::jmax(1, availableChannels - 1);

  dynamicGuideOutputChannelState.store(normalizedChannel,
                                       std::memory_order_relaxed);
  dynamicGuideOutputWidthState.store(normalizedWidth, std::memory_order_relaxed);
}

int MTControlsAudioProcessor::getDynamicGuideOutputChannel() const noexcept {
  const int guideStemIndex = findFirstGuideStemIndex(
      loadedStemCount.load(std::memory_order_relaxed));
  if (guideStemIndex >= 0) {
    return stemOutputChannels[static_cast<size_t>(guideStemIndex)].load(
        std::memory_order_relaxed);
  }
  return dynamicGuideOutputChannelState.load(std::memory_order_relaxed);
}

int MTControlsAudioProcessor::getDynamicGuideOutputWidth() const noexcept {
  const int guideStemIndex = findFirstGuideStemIndex(
      loadedStemCount.load(std::memory_order_relaxed));
  if (guideStemIndex >= 0) {
    return stemOutputWidths[static_cast<size_t>(guideStemIndex)].load(
        std::memory_order_relaxed);
  }
  return dynamicGuideOutputWidthState.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMetronomeClickStemIndex(
    const int stemIndexOrMinusOne) noexcept {
  if (stemIndexOrMinusOne < 0 || stemIndexOrMinusOne >= maxStemCount) {
    metronomeClickStemIndex.store(-1, std::memory_order_relaxed);
    return;
  }
  metronomeClickStemIndex.store(stemIndexOrMinusOne,
                                std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::loadMetronomeClickSamplesFromFolder(
    const juce::File &folder, const juce::String &style,
    juce::String *errorMessage) {
  return metronome.loadClickSamplesFromFolder(
      folder, style, playbackFormatManager, errorMessage);
}

void MTControlsAudioProcessor::clearMetronomeClickSamples() noexcept {
  metronome.clearClickSamples();
}

bool MTControlsAudioProcessor::hasMetronomeClickSamples() const noexcept {
  return metronome.hasClickSamples();
}

void MTControlsAudioProcessor::setClickDivision(const int division) noexcept {
  const int normalized = division & 0x07;
  clickDivisionState.store(normalized, std::memory_order_relaxed);
  const bool enabled =
      clickDivisionEnabledState.load(std::memory_order_relaxed);
  metronome.setDivisionMask(enabled ? normalized : 0);
  metronome.setSubdivision(clickDivisionMaskToSubdivision(normalized));
}

int MTControlsAudioProcessor::getClickDivision() const noexcept {
  return clickDivisionState.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setClickDivisionEnabled(
    const bool enabled) noexcept {
  clickDivisionEnabledState.store(enabled, std::memory_order_relaxed);
  const int division = clickDivisionState.load(std::memory_order_relaxed);
  metronome.setDivisionMask(enabled ? division : 0);
  metronome.setSubdivision(clickDivisionMaskToSubdivision(division));
}

bool MTControlsAudioProcessor::isClickDivisionEnabled() const noexcept {
  return clickDivisionEnabledState.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::clearLoadedSongState() {
  loadedSongLengthSeconds = 0.0;
  loadedSongBpm = 120.0;
  playbackTempoBpm.store(120.0, std::memory_order_relaxed);
  playbackTempoRatioActive = 1.0;
  loadedSongKey.clear();
  loadedSongTitle.clear();
  songStartSeconds.store(0.0, std::memory_order_relaxed);
  songEndSeconds.store(0.0, std::memory_order_relaxed);
  songFadeOutSeconds.store(0.0, std::memory_order_relaxed);
  resetStemControls(0);
  masterVolume.store(1.0f, std::memory_order_relaxed);
  masterGain.store(1.0f, std::memory_order_relaxed);
  masterMuted.store(false, std::memory_order_relaxed);
  dynamicGuideVolumeState.store(1.0f, std::memory_order_relaxed);
  dynamicGuideGainState.store(1.0f, std::memory_order_relaxed);
  dynamicGuideMutedState.store(false, std::memory_order_relaxed);
  dynamicGuideSoloState.store(false, std::memory_order_relaxed);
  dynamicGuideOutputChannelState.store(1, std::memory_order_relaxed);
  dynamicGuideOutputWidthState.store(2, std::memory_order_relaxed);
  songCurrentPositionSeconds.store(0.0, std::memory_order_relaxed);
  songLoaded.store(false, std::memory_order_relaxed);
  arrangementParts.clear();
  sectionScheduler.clear();
  transportClock.reset(0);
  arrangementCustomActive.store(false, std::memory_order_relaxed);
  arrangementCurrentPartIndex.store(0, std::memory_order_relaxed);
  arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
  arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
  arrangementTotalLengthSeconds.store(0.0, std::memory_order_relaxed);
  dynamicGuideAnnouncedByPart.clear();
  dynamicGuideLastBlockStartSec = -1.0;
  dynamicGuideLastPartIndex = -1;
  dynamicGuideWasPlaying = false;
  dynamicGuideEngine.reset();
  pitchTargetSemiTones.store(0, std::memory_order_relaxed);
  mixEngine.reset();
  metronome.reset();
  metronomeClickStemIndex.store(-1, std::memory_order_relaxed);
  routingPitchLatencySamples = 0;
  outgoingRoutingPitchLatencySamples = 0;
  for (auto &delay : stemPitchDelayLines)
    delay.reset();
  for (auto &processor : stemPitchProcessors)
    processor.reset();
  setTransportState(TransportState::stopped);
}

void MTControlsAudioProcessor::removeAllStemSources() {
  stemTracks.clear();
}

void MTControlsAudioProcessor::resetStemControls(
    const int activeStemCount) noexcept {
  const int clampedCount = juce::jlimit(0, maxStemCount, activeStemCount);
  loadedStemCount.store(clampedCount, std::memory_order_relaxed);
  stemOutputRoutingState.resetToDefaults(clampedCount);
  stemOutputRoutingState.normalizeForDevice(
      standaloneAudioDeviceBridge.getAvailableOutputChannelCount());

    for (int i = 0; i < maxStemCount; ++i) {
      stemLevels[static_cast<size_t>(i)].store(0.0f, std::memory_order_relaxed);
      stemVolumes[static_cast<size_t>(i)].store(1.0f, std::memory_order_relaxed);
      stemGains[static_cast<size_t>(i)].store(1.0f, std::memory_order_relaxed);
      stemMuteStates[static_cast<size_t>(i)].store(false,
                                                   std::memory_order_relaxed);
      stemSoloStates[static_cast<size_t>(i)].store(false,
                                                   std::memory_order_relaxed);
      stemClickGuideStates[static_cast<size_t>(i)].store(
          false, std::memory_order_relaxed);
      stemGuideStates[static_cast<size_t>(i)].store(
          false, std::memory_order_relaxed);
      stemPitchShiftStates[static_cast<size_t>(i)].store(
          true, std::memory_order_relaxed);
      stemOutputChannels[static_cast<size_t>(i)].store(
          stemOutputRoutingState.getStemOutputChannel(i), std::memory_order_relaxed);
      stemOutputWidths[static_cast<size_t>(i)].store(
          stemOutputRoutingState.getStemOutputWidth(i), std::memory_order_relaxed);
      stemPitchDelayLines[static_cast<size_t>(i)].setDelay(0);
      stemPitchProcessors[static_cast<size_t>(i)].setContentType(
          TimeStretchProcessor::ContentType::Melodic);
      stemPitchProcessors[static_cast<size_t>(i)].reset();
    }
  }

void MTControlsAudioProcessor::setStemClickGuideMask(
    const std::vector<bool> &mask) noexcept {
  const int limit = juce::jmin(static_cast<int>(mask.size()), maxStemCount);
  for (int i = 0; i < maxStemCount; ++i) {
    const bool isClickGuide = i < limit ? mask[static_cast<size_t>(i)] : false;
    stemClickGuideStates[static_cast<size_t>(i)].store(
        isClickGuide, std::memory_order_relaxed);
  }
}

void MTControlsAudioProcessor::setStemGuideMask(
    const std::vector<bool> &mask) noexcept {
  const int limit = juce::jmin(static_cast<int>(mask.size()), maxStemCount);
  for (int i = 0; i < maxStemCount; ++i) {
    const bool isGuideStem = i < limit ? mask[static_cast<size_t>(i)] : false;
    stemGuideStates[static_cast<size_t>(i)].store(
        isGuideStem, std::memory_order_relaxed);
  }
}

void MTControlsAudioProcessor::setDynamicGuideSettings(
    const DynamicGuideSettings &settings) {
  {
    const juce::SpinLock::ScopedLockType lock(dynamicGuideStateLock);
    dynamicGuideSettings = settings;
  }
  dynamicGuideEnabledState.store(settings.enabled, std::memory_order_relaxed);
  if (!settings.enabled) {
    dynamicGuideEngine.reset();
    return;
  }
  juce::String loadError;
  if (!reloadDynamicGuideAssets(&loadError)) {
    const juce::SpinLock::ScopedLockType warningLock(dynamicGuideWarningLock);
    dynamicGuideLastMissingSection =
        juce::String(kDynamicGuideLoadWarningPrefix) + loadError;
  }
}

DynamicGuideSettings MTControlsAudioProcessor::getDynamicGuideSettings() const {
  const juce::SpinLock::ScopedLockType lock(dynamicGuideStateLock);
  return dynamicGuideSettings;
}

void MTControlsAudioProcessor::setDynamicGuideLanguage(const AppLanguage language) {
  const int code = language == AppLanguage::English ? 1 : 0;
  const int previous = dynamicGuideLanguageCode.exchange(code, std::memory_order_relaxed);
  if (previous == code)
    return;
  juce::String loadError;
  if (!reloadDynamicGuideAssets(&loadError)) {
    const juce::SpinLock::ScopedLockType warningLock(dynamicGuideWarningLock);
    dynamicGuideLastMissingSection =
        juce::String(kDynamicGuideLoadWarningPrefix) + loadError;
  }
}

void MTControlsAudioProcessor::setArrangementEditingActive(
    const bool editingActive) noexcept {
  const bool previous =
      arrangementEditingActiveState.exchange(editingActive, std::memory_order_relaxed);
  if (previous == editingActive)
    return;

  if (editingActive) {
    dynamicGuideEngine.reset();
    dynamicGuideWasPlaying = false;
    dynamicGuideLastPartIndex = -1;
    dynamicGuideLastBlockStartSec = -1.0;
    std::fill(dynamicGuideAnnouncedByPart.begin(), dynamicGuideAnnouncedByPart.end(),
              false);
  }
}

bool MTControlsAudioProcessor::consumeDynamicGuideMissingSectionWarning(
    juce::String &outSectionName) {
  const int missingPart =
      dynamicGuideMissingPartIndex.exchange(-1, std::memory_order_acq_rel);
  if (missingPart >= 0) {
    const juce::SpinLock::ScopedLockType stemLock(stemListLock);
    if (juce::isPositiveAndBelow(
            missingPart, static_cast<int>(arrangementParts.size()))) {
      const auto &part = arrangementParts[static_cast<size_t>(missingPart)];
      outSectionName =
          part.sectionKey.isNotEmpty() ? part.sectionKey : part.name;
      return true;
    }
  }
  const juce::SpinLock::ScopedLockType lock(dynamicGuideWarningLock);
  if (dynamicGuideLastMissingSection.isEmpty())
    return false;
  outSectionName = dynamicGuideLastMissingSection;
  dynamicGuideLastMissingSection.clear();
  return true;
}

void MTControlsAudioProcessor::setStemPitchShiftMask(
    const std::vector<bool> &mask) noexcept {
  const int limit = juce::jmin(static_cast<int>(mask.size()), maxStemCount);
  for (int i = 0; i < maxStemCount; ++i) {
    const bool canPitch = i < limit ? mask[static_cast<size_t>(i)] : true;
    stemPitchShiftStates[static_cast<size_t>(i)].store(
        canPitch, std::memory_order_relaxed);
    const auto contentType = stemContentTypeForPitchShiftable(canPitch);
    stemPitchProcessors[static_cast<size_t>(i)].setContentType(contentType);
    outgoingStemPitchProcessors[static_cast<size_t>(i)].setContentType(
        contentType);
  }
}

void MTControlsAudioProcessor::setPitchSemiTones(
    const int semitoneDelta) noexcept {
  pitchTargetSemiTones.store(semitoneDelta, std::memory_order_relaxed);
}

int MTControlsAudioProcessor::getPitchSemiTones() const noexcept {
  return pitchTargetSemiTones.load(std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::reloadDynamicGuideAssets(juce::String *errorMessage) {
  const bool wasSuspended = isSuspended();
  suspendProcessing(true);
  for (int wait = 0;
       wait < 2000 && audioCallbackActive.load(std::memory_order_acquire);
       ++wait) {
    juce::Thread::sleep(1);
  }
  if (audioCallbackActive.load(std::memory_order_acquire)) {
    if (!wasSuspended)
      suspendProcessing(false);
    if (errorMessage != nullptr)
      *errorMessage = "Audio callback did not stop for guide reload.";
    return false;
  }

  juce::String localError;
  if (!AssetBootstrapManager::ensureGuidesPackReady(&localError)) {
    if (!wasSuspended)
      suspendProcessing(false);
    if (errorMessage != nullptr)
      *errorMessage = localError;
    return false;
  }

  const auto sectionsDir = AssetBootstrapManager::getGuidesLanguageSectionsDirectory(
      currentDynamicGuideLanguageCode());
  const auto cuesDir = AssetBootstrapManager::getGuidesLanguageCuesDirectory(
      currentDynamicGuideLanguageCode());
  if (!dynamicGuideEngine.loadGuideDirectories({sectionsDir, cuesDir},
                                               playbackFormatManager,
                                               &localError)) {
    if (!wasSuspended)
      suspendProcessing(false);
    if (errorMessage != nullptr)
      *errorMessage = localError;
    return false;
  }

  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    for (auto &part : arrangementParts)
      part.guideTriggerId =
          dynamicGuideEngine.resolveTriggerId(part.sectionKey);
  }

  {
    const juce::SpinLock::ScopedLockType warningLock(dynamicGuideWarningLock);
    if (dynamicGuideLastMissingSection.startsWith(kDynamicGuideLoadWarningPrefix))
      dynamicGuideLastMissingSection.clear();
  }

  if (errorMessage != nullptr)
    errorMessage->clear();
  if (!wasSuspended)
    suspendProcessing(false);
  return true;
}

juce::String MTControlsAudioProcessor::currentDynamicGuideLanguageCode() const noexcept {
  return dynamicGuideLanguageCode.load(std::memory_order_relaxed) == 1 ? "en" : "es";
}

int MTControlsAudioProcessor::findFirstGuideStemIndex(const int stemCount) const noexcept {
  const int limit = juce::jlimit(0, maxStemCount, stemCount);
  for (int i = 0; i < limit; ++i) {
    if (stemGuideStates[static_cast<size_t>(i)].load(std::memory_order_relaxed))
      return i;
  }
  return -1;
}

double MTControlsAudioProcessor::getPlaybackTempoRatio() const noexcept {
  const double originalBpm =
      juce::jmax(kMinPlaybackTempoBpm,
                 loadedSongBpm.load(std::memory_order_relaxed));
  const double playbackBpm =
      juce::jmax(kMinPlaybackTempoBpm,
                 playbackTempoBpm.load(std::memory_order_relaxed));
  return juce::jlimit(kMinPlaybackTempoRatio, kMaxPlaybackTempoRatio,
                      playbackBpm / originalBpm);
}

void MTControlsAudioProcessor::syncMetronomeToSourceSeconds(
    const double sourceSeconds) noexcept {
  // The transport clock is expressed in source-song time, while the
  // metronome advances in rendered/output time at playback BPM.
  const double ratio =
      juce::jmax(kMinPlaybackTempoRatio, getPlaybackTempoRatio());
  metronome.syncPhaseForTimeSeconds(juce::jmax(0.0, sourceSeconds) / ratio);
}

int MTControlsAudioProcessor::findArrangementPartIndexForArrangementTime(
    const double arrangementSeconds) const noexcept {
  const double sampleRate =
      playbackSampleRate > 0.0 ? playbackSampleRate : 44100.0;
  return sectionScheduler.findSection(
      playworship::audio::TransportClock::secondsToFrames(arrangementSeconds,
                                                           sampleRate));
}

juce::int64 MTControlsAudioProcessor::mapArrangementFrameToSourceFrame(
    const juce::int64 arrangementFrame) const noexcept {
  const double sampleRate =
      playbackSampleRate > 0.0 ? playbackSampleRate : 44100.0;
  const auto arrangementEndFrame =
      playworship::audio::TransportClock::secondsToFrames(
          arrangementTotalLengthSeconds.load(std::memory_order_relaxed),
          sampleRate);
  const auto sourceEndFrame =
      playworship::audio::TransportClock::secondsToFrames(
          loadedSongLengthSeconds.load(std::memory_order_relaxed), sampleRate);
  juce::int64 sourceFrame = arrangementFrame;
  if (sectionScheduler.mapFrameToSourceFrame(arrangementFrame,
                                             arrangementEndFrame,
                                             sourceEndFrame, sourceFrame))
    return sourceFrame;
  return arrangementFrame;
}

double MTControlsAudioProcessor::getArrangementStartSeconds() const noexcept {
  const juce::SpinLock::ScopedLockType lock(stemListLock);
  if (arrangementParts.empty())
    return songStartSeconds.load(std::memory_order_relaxed);
  return arrangementParts.front().startSec;
}

void MTControlsAudioProcessor::rebuildSectionScheduler() noexcept {
  std::vector<playworship::audio::SectionScheduler::Section> sections;
  sections.reserve(arrangementParts.size());
  const double sampleRate =
      playbackSampleRate > 0.0 ? playbackSampleRate : 44100.0;
  const double bpm = juce::jmax(1.0, getPlaybackTempoBpm());
  const auto oneBarFrames =
      playworship::audio::TransportClock::secondsToFrames((60.0 / bpm) * 4.0,
                                                           sampleRate);
  for (const auto &part : arrangementParts) {
    const auto startFrame =
        playworship::audio::TransportClock::secondsToFrames(part.startSec,
                                                             sampleRate);
    const bool triggerOnEntry =
        sectionTriggersOnEntry(part.sectionKey, part.name);
    sections.push_back({
        startFrame,
        playworship::audio::TransportClock::secondsToFrames(part.endSec,
                                                             sampleRate),
        playworship::audio::TransportClock::secondsToFrames(part.sourceStartSec,
                                                             sampleRate),
        playworship::audio::TransportClock::secondsToFrames(part.sourceEndSec,
                                                             sampleRate),
        triggerOnEntry
            ? startFrame
            : std::max<juce::int64>(0, startFrame - oneBarFrames),
        part.guideTriggerId,
        triggerOnEntry,
        part.insertedClip,
        part.muteInstruments,
        part.skipPlayback});
  }
  sectionScheduler.publish(sections);
}

MTControlsAudioProcessor::TransportState
MTControlsAudioProcessor::getTransportState() const noexcept {
  return static_cast<TransportState>(
      transportState.load(std::memory_order_relaxed));
}

void MTControlsAudioProcessor::setTransportState(
    const TransportState state) noexcept {
  transportState.store(static_cast<int>(state), std::memory_order_relaxed);
  switch (state) {
  case TransportState::playing:
    transportClock.play();
    break;
  case TransportState::paused:
    transportClock.pause();
    break;
  case TransportState::stopped:
    transportClock.pause();
    break;
  }
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new MTControlsAudioProcessor();
}
