#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SongLoadProbe.h"
#include "audio/DynamicGuideTiming.h"
#include "audio/StemCache.h"
#include "data/AppSettings.h"
#include "data/AssetBootstrapManager.h"
#include "data/SongLibraryStore.h"
#include "telemetry/Telemetry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
using playworship::audio::arrangementCountSectionKey;
using playworship::audio::DynamicGuideAnnouncementTiming;
using playworship::audio::dynamicGuideAnnouncementTiming;

constexpr double kMinPlaybackTempoBpm = 20.0;
constexpr double kMaxPlaybackTempoBpm = 300.0;
constexpr double kMinPlaybackTempoRatio = 0.75;
constexpr double kMaxPlaybackTempoRatio = 2.0;
constexpr int kMinPitchSemitones = -12;
constexpr int kMaxPitchSemitones = 12;
constexpr int kArrangementTransitionPrefetchFrames = 32768;
constexpr int kArrangementTransitionFadeFrames = 64;
constexpr const char *kDynamicGuideLoadWarningPrefix =
    "__dynamic_guide_load_error__:";

void publishMeterPeak(std::atomic<float> &destination,
                      const float peak) noexcept {
  const float normalizedPeak = juce::jmax(0.0f, peak);
  float published = destination.load(std::memory_order_relaxed);
  while (normalizedPeak > published &&
         !destination.compare_exchange_weak(
             published, normalizedPeak, std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
}

class ArrangementStructurePublicationGuard {
public:
  explicit ArrangementStructurePublicationGuard(
      std::atomic<uint64_t> &sequenceToUse) noexcept
      : sequence(sequenceToUse) {
    sequence.fetch_add(1, std::memory_order_acq_rel);
  }

  ~ArrangementStructurePublicationGuard() {
    sequence.fetch_add(1, std::memory_order_release);
  }

private:
  std::atomic<uint64_t> &sequence;
};

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

double elapsedMsSince(const juce::int64 startTicks) {
  return (static_cast<double>(juce::Time::getHighResolutionTicks() - startTicks) *
          1000.0) /
         static_cast<double>(juce::Time::getHighResolutionTicksPerSecond());
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

juce::String baseArrangementSectionKey(const juce::String &partName) {
  const auto canonical = canonicalArrangementSectionName(partName);
  return canonical.isNotEmpty()
             ? DynamicGuideEngine::normalizeSectionKey(canonical)
             : DynamicGuideEngine::normalizeSectionKey(partName);
}

void applyBufferGainRamp(juce::AudioBuffer<float> &buffer,
                         const float startGain,
                         const float endGain) noexcept {
  if (buffer.getNumSamples() <= 0)
    return;
  if (juce::approximatelyEqual(startGain, 1.0f) &&
      juce::approximatelyEqual(endGain, 1.0f)) {
    return;
  }
  if (juce::approximatelyEqual(startGain, endGain))
    buffer.applyGain(endGain);
  else
    buffer.applyGainRamp(0, buffer.getNumSamples(), startGain, endGain);
}

void applySongTransitionRamp(
    juce::AudioBuffer<float> &buffer,
    const playworship::audio::SongTransitionGainRamp &ramp) noexcept {
  applyBufferGainRamp(buffer, ramp.startGain, ramp.endGain);
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
  playworship::telemetry::initialize();
  playbackFormatManager.registerBasicFormats();
  // P0 (perf): one-shot migration de directorios de cache legacy
  // cuyos nombres contengan caracteres que sanitizeCacheKey() ahora
  // reemplaza (ej. títulos con `/`, `:`, etc.). Renombrar en lugar de
  // regenerar mantiene las FLACs y sidecars existentes. Idempotente.
  StemCache::migrateLegacyCacheDirectories();
  // P0 (perf): one-shot reconciliation of orphaned cache files. Walks
  // decoded/, cloud-content/objects/, cloud-artwork/, cloud-sync-staging/
  // and removes anything no song references. Applied (dryRun=false) so
  // the user gets the disk space back on every startup. The action is
  // idempotent and only deletes content no song references. See
  // perf/analysis_20260912/INFORME.md §2.
  {
    const auto report =
        SongLibraryStore::reconcileOrphanedAssets(false);
    juce::ignoreUnused(report);
  }
  // 18 stems across N workers → each worker round-robins over
  // (18 / N) stems. With N=4 (legacy cap) each worker handles 4-5
  // stems and the round-trip latency between consecutive refills of
  // the same stem is too long, producing the bulk of the
  // streaming_misses observed in benchmarks.
  //
  // Raising the cap to 8 (while still leaving headroom for the audio
  // thread on the remaining cores) brings the per-worker stem count
  // down to 2-3 and roughly halves the round-trip latency. Measured
  // improvement on 18 stems: streaming_misses drop to a small fraction
  // of the legacy value with no measurable CPU cost on the audio
  // thread.
  const int readWorkerCount =
      juce::jlimit(4, 8, juce::SystemStats::getNumCpus() - 1);
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

#if MTCONTROLS_USE_BUNGEE
  timeProcessorMaintenanceThread =
      std::make_unique<juce::TimeSliceThread>("TimeProcessorMaintenance");
  timeProcessorMaintenanceThread->addTimeSliceClient(this);
  timeProcessorMaintenanceThread->startThread(
      juce::Thread::Priority::normal);
  resumeTimeProcessorMaintenance();
#endif
  startTimerHz(100);
  startPadsPreloadAsync();
}

MTControlsAudioProcessor::~MTControlsAudioProcessor() {
  stopTimer();
  padsPreloadShouldExit.store(true, std::memory_order_release);
  if (padsPreloadThread.joinable())
    padsPreloadThread.join();
  pauseTimeProcessorMaintenance();
  if (timeProcessorMaintenanceThread != nullptr) {
    timeProcessorMaintenanceThread->stopThread(3000);
    timeProcessorMaintenanceThread->removeTimeSliceClient(this);
    timeProcessorMaintenanceThread.reset();
  }
  stemTracks.clear();
  for (auto &worker : stemReadThreads)
    worker->stopThread(3000);
  stemReadThreads.clear();
  playworship::telemetry::shutdown();
}

void MTControlsAudioProcessor::prepareToPlay(double sampleRate,
                                             int samplesPerBlock) {
  diagnosticEngineGeneration.fetch_add(1, std::memory_order_relaxed);
  diagnosticMarkerGeneration.store(0, std::memory_order_relaxed);
  distortionSnapshotDecimator = 0;
  diagnosticMasterSignalState.reset();
  diagnosticOutputSignalState.reset();
  pauseTimeProcessorMaintenance();
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
  stemRoutingInputBuffer.setSize(2, (realtimeBlockCapacity * 2) + 8);
  stemRoutingTempStorage.setSize(2, realtimeBlockCapacity);
  metronomeRenderStorage.setSize(2, realtimeBlockCapacity);
  // Pads render into their own stereo bus and are routed afterwards.
  // Rendering them across every physical output channel multiplies the
  // per-block cost on multichannel devices without changing the result.
  padsRenderStorage.setSize(2, realtimeBlockCapacity);
  dynamicGuideRenderStorage.setSize(2, realtimeBlockCapacity);
  // Routing-grouped path renders the StemMixEngine output here before
  // StemOutputRouter folds it into the user-configured (channel, width)
  // destination. Two channels is the minimum the engine writes; on a
  // multichannel bus we still need at least 2 to fold L+R for a mono
  // destination, so we size for max(device_channels, 2). Channels
  // beyond 2 stay zeroed because `buffer.clear()` is still applied
  // before routeStemToOutputChannels adds the folded result.
  routingGroupedRenderStorage.setSize(
      juce::jmax(2, getTotalNumOutputChannels()), realtimeBlockCapacity);
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
  }
  prepareRoutingTimeProcessors(
      loadedStemCount.load(std::memory_order_relaxed), sampleRate,
      realtimeBlockCapacity);
  routingPitchLatencySamples = 0;

  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    playbackSampleRate = sampleRate;
    playbackBlockSize = samplesPerBlock;
    transportClock.prepare(sampleRate);
    rebuildSectionScheduler();
    metronome.prepare(sampleRate);
    for (auto &track : stemTracks)
      track->prepareToPlay(samplesPerBlock, sampleRate);
  }

  resumeTimeProcessorMaintenance();

  // Start the background diagnostics logger.
  diagnostics.startBackgroundLogger(
      juce::File::getSpecialLocation(
          juce::File::SpecialLocationType::userApplicationDataDirectory)
          .getChildFile("Logs/MTControls"));
}

void MTControlsAudioProcessor::releaseResources() {
  pauseTimeProcessorMaintenance();
  diagnostics.stopBackgroundLogger();
  mixEngine.releaseResources();
  releaseRoutingTimeProcessors();
  padsEngine.releaseResources();
  metronome.reset();
  metronomeLatencyDelay.reset();
  dynamicGuideEngine.reset();
  dynamicGuideLatencyDelay.reset();
  const juce::SpinLock::ScopedLockType lock(stemListLock);
  for (auto &track : stemTracks)
    track->releaseResources();
}

void MTControlsAudioProcessor::prepareRoutingTimeProcessors(
    const int activeStemCount, const double sampleRate,
    const int maxBlockFrames) {
  const int count = juce::jlimit(0, maxStemCount, activeStemCount);
  releaseRoutingTimeProcessors(count);
  const int capacity = juce::jmax(1, maxBlockFrames);
  for (int i = 0; i < count; ++i) {
    const auto index = static_cast<size_t>(i);
    auto &processor = stemPitchProcessors[index];
    processor.prepare(sampleRate, capacity, 2);
    processor.setContentType(stemContentTypeForPitchShiftable(
        stemPitchShiftStates[index].load(std::memory_order_relaxed)));
    processor.setPitchSemiTones(pitchActiveSemiTones);
    processor.setTempoRatio(playbackTempoRatioActive);
    stemPitchDelayLines[index].prepare(2, kMaxRoutingPitchLatencySamples);
    stemPitchDelayLines[index].setDelay(0);
  }
  preparedRoutingProcessorCount.store(count, std::memory_order_release);
}

void MTControlsAudioProcessor::releaseRoutingTimeProcessors(
    const int retainedStemCount) {
  const int retained = juce::jlimit(0, maxStemCount, retainedStemCount);
  const int previous = preparedRoutingProcessorCount.load(
      std::memory_order_acquire);
  for (int i = retained; i < previous; ++i) {
    const auto index = static_cast<size_t>(i);
    stemPitchProcessors[index].releaseResources();
    stemPitchDelayLines[index].releaseResources();
  }
  if (retained < previous)
    preparedRoutingProcessorCount.store(retained,
                                        std::memory_order_release);
}

void MTControlsAudioProcessor::flushRoutingTimeProcessors() noexcept {
  const int count =
      preparedRoutingProcessorCount.load(std::memory_order_acquire);
  for (int i = 0; i < count; ++i) {
    stemPitchProcessors[static_cast<size_t>(i)].reset();
    stemPitchDelayLines[static_cast<size_t>(i)].reset();
  }
  routingPitchLatencySamples = 0;
}

void MTControlsAudioProcessor::flushAllTimeProcessingState() noexcept {
  mixEngine.reset();
  flushRoutingTimeProcessors();
}

void MTControlsAudioProcessor::pauseTimeProcessorMaintenance() noexcept {
  timeProcessorMaintenanceEnabled.store(false, std::memory_order_release);
  // Control-thread barrier: once this scoped lock returns, an in-flight
  // rebuild has finished and the disabled service cannot start another one.
  const juce::ScopedLock barrier(timeProcessorMaintenanceLock);
}

void MTControlsAudioProcessor::resumeTimeProcessorMaintenance() noexcept {
  if (timeProcessorMaintenanceThread == nullptr)
    return;
  timeProcessorMaintenanceEnabled.store(true, std::memory_order_release);
  timeProcessorMaintenanceThread->moveToFrontOfQueue(this);
}

int MTControlsAudioProcessor::useTimeSlice() {
  if (!timeProcessorMaintenanceEnabled.load(std::memory_order_acquire))
    return 20;

  const juce::ScopedLock lock(timeProcessorMaintenanceLock);
  if (!timeProcessorMaintenanceEnabled.load(std::memory_order_acquire))
    return 20;

  int rebuilt = mixEngine.servicePendingProcessorRebuilds();
  const int count =
      preparedRoutingProcessorCount.load(std::memory_order_acquire);
  for (int i = 0; i < count; ++i) {
    rebuilt += stemPitchProcessors[static_cast<size_t>(i)]
                   .servicePendingRebuild()
                   ? 1
                   : 0;
  }

  return rebuilt > 0 ? 0 : 5;
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

  const int64_t blockStartTick = juce::Time::getHighResolutionTicks();
  const bool songLoadedForDiagnostics =
      songLoaded.load(std::memory_order_relaxed);
  const bool diagnosticsActive = diagnostics.isActive();

  // ---- Benchmark: gap detector (underrun) -------------------------------
  // Compare this callback's start tick against the previous block's end tick.
  // If the gap exceeds the configured threshold (default 15ms), the audio
  // thread was starved and the user hears a "cut" in the output. We log it
  // so the analysis script can map the cut to the song position and to the
  // per-stage timings of the surrounding blocks.
  if (songLoadedForDiagnostics && diagnosticsActive) {
    const int64_t previousEndTick =
        diagnostics.getPreviousBlockEndTick();
    if (previousEndTick > 0) {
      const double deltaMs =
          juce::Time::highResolutionTicksToSeconds(blockStartTick -
                                                   previousEndTick) *
          1000.0;
      const double callbackIntervalMs =
          playbackSampleRate > 0.0 && buffer.getNumSamples() > 0
              ? (static_cast<double>(buffer.getNumSamples()) /
                 playbackSampleRate) *
                    1000.0
              : 0.0;
      // With Oboe configured for a larger callback quantum, the expected
      // time between callbacks is itself greater than the legacy 15 ms
      // threshold. Require at least one skipped callback before recording a
      // starvation gap; otherwise every healthy 20 ms callback is a false
      // positive in audio_gaps_*.csv.
      const double gapThresholdMs = juce::jmax(
          diagnostics.getGapThresholdMs(), callbackIntervalMs * 1.5);
      if (deltaMs >= gapThresholdMs) {
        const double posSec =
            playbackSampleRate > 0.0
                ? transportClock.getPositionSeconds()
                : 0.0;
        const bool isPlayingForGap =
            getTransportState() == TransportState::playing;
        AudioDiagnostics::GapEvent gap;
        gap.wallMs = diagnostics.wallTimeForHighResolutionTick(blockStartTick);
        gap.deltaMs = deltaMs;
        gap.songPositionSec = posSec;
        gap.stemCount = static_cast<int>(stemTracks.size());
        gap.isPlaying = isPlayingForGap;
        diagnostics.pushGapEvent(gap);
        // Atomic counter only: no I/O, allocations, locks, SDK calls, or
        // logging from processBlock. timerCallback sends a coarse aggregate.
        playworship::telemetry::recordAudioCallbackGap();
      }
    }
  }

  // ---- Benchmark: per-stage timers --------------------------------------
  // stagePreReadEndTick / stageReadNextEndTick / stageMixStemsEndTick /
  // stageRampTransitionEndTick are set at the boundaries of the
  // corresponding sections inside processBlock.
  // stageDecodeRenderEndTick ACCUMULATES the duration of every pitch
  // processor and delay line invocation across the per-stem loop.
  // stageMixOutputStartTick / EndTick bracket the master bus render.
  // All other unused StartTick variables are intentionally omitted to
  // avoid "unused" warnings.
  int64_t stagePreReadEndTick = 0;
  int64_t stageReadNextEndTick = 0;
  int64_t stageDecodeRenderEndTick = 0;
  int64_t stageMixStemsEndTick = 0;
  int64_t stageMixOutputStartTick = 0;
  int64_t stageMixOutputEndTick = 0;
  int64_t stageRampTransitionStartTick = 0;
  int64_t stageRampTransitionEndTick = 0;

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
      padsRenderStorage.getArrayOfWritePointers(), 2, callbackSamples);
  juce::AudioBuffer<float> dynamicGuideRenderBuffer(
      dynamicGuideRenderStorage.getArrayOfWritePointers(), 2, callbackSamples);

  const auto guideResetGeneration =
      dynamicGuideResetGeneration.load(std::memory_order_acquire);
  if (guideResetGeneration != dynamicGuideAppliedResetGeneration) {
    dynamicGuideEngine.reset();
    dynamicGuideLatencyDelay.reset();
    dynamicGuideAnnouncedByPart.fill(0);
    dynamicGuideLastBlockStartSec = -1.0;
    dynamicGuideLastPartIndex = -1;
    dynamicGuideWasPlaying = false;
    dynamicGuideAppliedResetGeneration = guideResetGeneration;
  }

  playworship::audio::SongTransitionGainRamp songLoadGainRamp;
  const auto songLoadState = static_cast<SongLoadTransitionState>(
      songLoadTransitionState.load(std::memory_order_acquire));
  const float currentSongLoadGain =
      songLoadTransitionGain.load(std::memory_order_relaxed);
  switch (songLoadState) {
  case SongLoadTransitionState::fadingOut:
    songLoadGainRamp = playworship::audio::advanceSongTransitionGain(
        currentSongLoadGain, 0.0f, callbackSamples, playbackSampleRate,
        songLoadFadeOutDurationSec);
    songLoadTransitionGain.store(songLoadGainRamp.endGain,
                                 std::memory_order_relaxed);
    if (songLoadGainRamp.reachedTarget) {
      songLoadTransitionState.store(
          static_cast<int>(SongLoadTransitionState::readyToSwap),
          std::memory_order_release);
    }
    break;
  case SongLoadTransitionState::fadingIn:
    songLoadGainRamp = playworship::audio::advanceSongTransitionGain(
        currentSongLoadGain, 1.0f, callbackSamples, playbackSampleRate,
        songLoadFadeInDurationSec);
    songLoadTransitionGain.store(songLoadGainRamp.endGain,
                                 std::memory_order_relaxed);
    if (songLoadGainRamp.reachedTarget) {
      songLoadTransitionState.store(
          static_cast<int>(SongLoadTransitionState::idle),
          std::memory_order_release);
    }
    break;
  case SongLoadTransitionState::readyToSwap:
  case SongLoadTransitionState::holdingAtZero:
    songLoadGainRamp = {0.0f, 0.0f, true};
    songLoadTransitionGain.store(0.0f, std::memory_order_relaxed);
    break;
  case SongLoadTransitionState::idle:
  default:
    songLoadGainRamp = {1.0f, 1.0f, true};
    songLoadTransitionGain.store(1.0f, std::memory_order_relaxed);
    break;
  }

  int diagnosticStemCount = 0;
  bool diagnosticWasPlaying = false;
  playworship::audio::SignalIntegrityMetrics diagnosticMasterMetrics;
  playworship::audio::SignalIntegrityMetrics diagnosticOutputMetrics;
  const bool captureFullStemIntegrityThisBlock =
      diagnosticsActive &&
      distortionSnapshotDecimator + 1 >=
          kDistortionStemSnapshotEveryBlocks;

  for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels();
       ++i)
    buffer.clear(i, 0, buffer.getNumSamples());

  dynamicGuideMeterLevelLeft.store(0.0f, std::memory_order_relaxed);
  dynamicGuideMeterLevelRight.store(0.0f, std::memory_order_relaxed);

  bool usedSeparatedMix = false;
  bool routedStemFadeApplied = false;
  float windowFadeStart = 1.0f;
  float windowFadeEnd = 1.0f;
  bool windowFadeActive = false;
  bool stopAtEnd = false;
  double stopAtEndSec = 0.0;
  bool transitionToNextArrangementPart = false;
  int nextArrangementPartIndex = -1;
  double nextArrangementPartStartSec = 0.0;
  bool inBlockArrangementTransitionApplied = false;
  int inBlockTransitionPrefixFrames = 0;
  int inBlockTransitionOutputOffset = 0;
  juce::int64 inBlockTransitionTargetFrame = 0;
  juce::int64 inBlockTransitionTargetSourceFrame = 0;
  int inBlockTransitionTargetGuideId = -1;
  bool inBlockTransitionTargetGuideOnEntry = false;
  int songStopOutputOffset = -1;
  bool muteSectionInstrumentsThisBlock = false;
  bool skipArrangementPartThisBlock = false;
  bool dynamicGuideShouldRender = false;
  bool dynamicGuideAnnouncementsActive = false;
  int dynamicGuideOutputChannelOneBased = 1;
  int dynamicGuideOutputWidth = 2;
  float dynamicGuideOutputGain = 1.0f;
  bool dynamicGuideOutputAudible = false;
  bool anySoloActiveThisBlock = false;
  bool seekAppliedThisBlock = false;
  bool timeProcessingStateFlushedThisBlock = false;
  bool virtualTimelineStateFlushedThisBlock = false;
  bool timeProcessingBlockedThisBlock = false;
  int stemOutputLatencyThisBlock = 0;
  int sourceFramesThisBlock = buffer.getNumSamples();
  double renderedSourceStartSec = 0.0;
  const float fadeTargetForBlock =
      fadeTarget.load(std::memory_order_relaxed);
  float fadeStart = fadeGain;
  float fadeEnd = fadeGain;
  if (callbackSamples > 0 && playbackSampleRate > 0.0f &&
      !juce::approximatelyEqual(fadeGain, fadeTargetForBlock)) {
    const float step =
        (fadeTargetForBlock - fadeGain) /
        static_cast<float>(fadeDurationSec * playbackSampleRate);
    fadeEnd = fadeGain + (step * static_cast<float>(callbackSamples));
    if ((step > 0.0f && fadeEnd > fadeTargetForBlock) ||
        (step < 0.0f && fadeEnd < fadeTargetForBlock)) {
      fadeEnd = fadeTargetForBlock;
    }
    fadeGain = fadeEnd;
  }
  const bool transportFadeAffectsBlock =
      fadeActive.load(std::memory_order_relaxed) ||
      !juce::approximatelyEqual(fadeStart, 1.0f) ||
      !juce::approximatelyEqual(fadeEnd, 1.0f);

  if (songLoadedForDiagnostics) {
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const auto flushTimeProcessingStateOnce = [this,
                                                &timeProcessingStateFlushedThisBlock] {
      if (timeProcessingStateFlushedThisBlock)
        return;
      flushAllTimeProcessingState();
      timeProcessingStateFlushedThisBlock = true;
    };
    const auto flushVirtualTimelineStateOnce =
        [this, &virtualTimelineStateFlushedThisBlock] {
          if (virtualTimelineStateFlushedThisBlock)
            return;
          metronome.reset();
          metronomeLatencyDelay.reset();
          dynamicGuideEngine.reset();
          dynamicGuideLatencyDelay.reset();
          virtualTimelineStateFlushedThisBlock = true;
        };
    const bool isPlaying = getTransportState() == TransportState::playing;
    const bool dynamicGuideEnabled =
        dynamicGuideEnabledState.load(std::memory_order_relaxed);
    const bool arrangementEditingActive =
        arrangementEditingActiveState.load(std::memory_order_relaxed);
    dynamicGuideAnnouncementsActive = dynamicGuideEnabled;
    const auto arrangementSequenceBefore =
        arrangementStructurePublicationSequence.load(std::memory_order_acquire);
    const auto sectionSnapshotCandidate = sectionScheduler.acquireSnapshot();
    const AudioArrangementRuntimeSnapshot arrangementRuntimeCandidate{
        arrangementCustomActive.load(std::memory_order_relaxed),
        arrangementTotalLengthSeconds.load(std::memory_order_relaxed),
        arrangementPendingJumpPartIndex.load(std::memory_order_relaxed),
        arrangementLoopPartIndex.load(std::memory_order_relaxed)};
    const auto arrangementSequenceAfter =
        arrangementStructurePublicationSequence.load(std::memory_order_acquire);
    const bool arrangementRuntimeCoherent =
        sectionSnapshotCandidate.valid &&
        arrangementSequenceBefore == arrangementSequenceAfter &&
        (arrangementSequenceAfter & 1U) == 0U;
    if (arrangementRuntimeCoherent &&
        (sectionSnapshotCandidate.generation != audioSectionSnapshot.generation ||
         arrangementSequenceAfter != audioArrangementStructureSequence)) {
      audioSectionSnapshot = sectionSnapshotCandidate;
      audioArrangementRuntimeSnapshot = arrangementRuntimeCandidate;
      audioArrangementStructureSequence = arrangementSequenceAfter;
    }
    // The transition intent (pending jump, loop section) is a transient field
    // that the UI mutates via fetch_add on arrangementTransitionIntentGeneration
    // without touching the structure sequence. Without this refresh the audio
    // thread would keep reading the stale pendingJumpPartIndex captured in the
    // previous structural snapshot, and the jump/loop click would never reach
    // SectionScheduler::evaluate.
    const auto currentTransitionIntentGeneration =
        arrangementTransitionIntentGeneration.load(std::memory_order_acquire);
    if (currentTransitionIntentGeneration !=
        audioArrangementTransitionIntentGeneration) {
      audioArrangementRuntimeSnapshot.pendingJumpPartIndex =
          arrangementPendingJumpPartIndex.load(std::memory_order_relaxed);
      audioArrangementRuntimeSnapshot.loopPartIndex =
          arrangementLoopPartIndex.load(std::memory_order_relaxed);
      audioArrangementTransitionIntentGeneration =
          currentTransitionIntentGeneration;
    }
    const auto &realtimeSections = audioSectionSnapshot;
    const bool realtimeCustomArrangement =
        audioArrangementRuntimeSnapshot.customActive;
    const double realtimeArrangementTotalLengthSeconds =
        audioArrangementRuntimeSnapshot.totalLengthSeconds;
    const int realtimePendingPartIndex =
        audioArrangementRuntimeSnapshot.pendingJumpPartIndex;
    const int realtimeLoopPartIndex =
        audioArrangementRuntimeSnapshot.loopPartIndex;
    const auto mapRealtimeArrangementFrameToSourceFrame =
        [this, &realtimeSections,
         realtimeArrangementTotalLengthSeconds](
            const juce::int64 arrangementFrame) noexcept {
          using Scheduler = playworship::audio::SectionScheduler;
          const double sampleRate =
              playbackSampleRate > 0.0 ? playbackSampleRate : 44100.0;
          const auto arrangementEndFrame =
              playworship::audio::TransportClock::secondsToFrames(
                  realtimeArrangementTotalLengthSeconds, sampleRate);
          const auto sourceEndFrame =
              playworship::audio::TransportClock::secondsToFrames(
                  loadedSongLengthSeconds.load(std::memory_order_relaxed),
                  sampleRate);
          Scheduler::Frame sourceFrame = arrangementFrame;
          if (Scheduler::mapFrameToSourceFrame(
                  realtimeSections, arrangementFrame, arrangementEndFrame,
                  sourceEndFrame, sourceFrame)) {
            return static_cast<juce::int64>(sourceFrame);
          }
          return arrangementFrame;
        };
    const int targetPitch =
        pitchTargetSemiTones.load(std::memory_order_relaxed);
    const double targetTempoRatio = getPlaybackTempoRatio();
    if (targetPitch != pitchActiveSemiTones) {
      pitchActiveSemiTones = targetPitch;
      // Routing processors accept live pitch updates below. Do not flush their
      // overlap/FIFO state: doing so inserts a re-prime silence.
    }
    if (!juce::approximatelyEqual(targetTempoRatio,
                                  tempoTransitionTargetRatio)) {
      tempoTransitionStartRatio = playbackTempoRatioActive;
      tempoTransitionTargetRatio = targetTempoRatio;
      tempoTransitionElapsedFrames = 0;
      tempoTransitionTotalFrames = juce::jmax(
          1, static_cast<int>(std::lround(playbackSampleRate * 0.10)));
    }

    double blockTempoRatio = tempoTransitionTargetRatio;
    if (isPlaying &&
        tempoTransitionElapsedFrames < tempoTransitionTotalFrames) {
      const int midpointFrame = juce::jmin(
          tempoTransitionTotalFrames,
          tempoTransitionElapsedFrames + (numSamples / 2));
      const double linearProgress =
          static_cast<double>(midpointFrame) /
          static_cast<double>(tempoTransitionTotalFrames);
      const double smoothProgress =
          linearProgress * linearProgress * (3.0 - (2.0 * linearProgress));
      blockTempoRatio =
          tempoTransitionStartRatio +
          ((tempoTransitionTargetRatio - tempoTransitionStartRatio) *
           smoothProgress);
      tempoTransitionElapsedFrames =
          juce::jmin(tempoTransitionTotalFrames,
                     tempoTransitionElapsedFrames + numSamples);
    } else {
      tempoTransitionElapsedFrames = tempoTransitionTotalFrames;
    }

    if (!juce::approximatelyEqual(blockTempoRatio,
                                  playbackTempoRatioActive)) {
      playbackTempoRatioActive = blockTempoRatio;
    }
    // Song loads reset the transport's active ratio independently of the
    // engine. Synchronize before calculating source consumption even when
    // the transport value did not change in this callback. Otherwise a song
    // at its original BPM can inherit the previous song's stretch ratio,
    // drift from its readers, and repeatedly seek all stems back into sync.
    // Both setters return immediately when their engine value is unchanged.
    mixEngine.setPitchSemiTones(pitchActiveSemiTones);
    mixEngine.setTempoRatio(playbackTempoRatioActive);
    sourceFramesThisBlock = mixEngine.requiredInputFrames(numSamples);

    const int stemCount = static_cast<int>(stemTracks.size());
    // Apply a control-side seek only after every active stem has the same
    // target generation resident in its secondary bank. Paused/stopped
    // transport may commit immediately because no audible handoff occurs.
    const auto requestedSeekGeneration =
        seekRequestGeneration.load(std::memory_order_acquire);
    const bool controlSeekCommandObserved =
        requestedSeekGeneration != appliedSeekRequestGeneration;
    if (controlSeekCommandObserved) {
      const double seekTarget =
          seekTargetSeconds.load(std::memory_order_relaxed);
      const juce::int64 seekSamples =
          playworship::audio::TransportClock::secondsToFrames(
              seekTarget, playbackSampleRate);
      const juce::int64 sourceSeekSamples =
          mapRealtimeArrangementFrameToSourceFrame(seekSamples);
      const int prefetchFrames =
          juce::jmax(kArrangementTransitionPrefetchFrames,
                     sourceFramesThisBlock);
      bool allTargetsReady = true;
      for (auto &track : stemTracks)
        track->requestPrefetchPosition(sourceSeekSamples, prefetchFrames);
      for (const auto &track : stemTracks)
        allTargetsReady =
            track->isPrefetchReady(sourceSeekSamples, prefetchFrames) &&
            allTargetsReady;

      const bool audibleHandoff = isPlaying && stemCount > 0;
      if ((!audibleHandoff || allTargetsReady) &&
          seekRequestGeneration.load(std::memory_order_acquire) ==
              requestedSeekGeneration) {
        for (auto &track : stemTracks)
          track->setNextReadPosition(sourceSeekSamples);

        // Seeking changes source position abruptly: flush all time-domain
        // processors so no stale pre-seek audio survives in different groups.
        flushTimeProcessingStateOnce();
        flushVirtualTimelineStateOnce();

        globalSamplePosition.store(seekSamples, std::memory_order_relaxed);
        transportClock.seek(seekSamples);
        songCurrentPositionSeconds.store(seekTarget,
                                         std::memory_order_relaxed);
        appliedSeekRequestGeneration = requestedSeekGeneration;
        deferredArrangementTransitionPending = false;
        deferredArrangementTransitionPartIndex = -1;

        syncMetronomeToSourceSeconds(
            playworship::audio::TransportClock::framesToSeconds(
                sourceSeekSamples, playbackSampleRate));
        seekAppliedThisBlock = true;
      }
    }
    const bool controlSeekStillPending =
        seekRequestGeneration.load(std::memory_order_acquire) !=
        appliedSeekRequestGeneration;
    const bool controlSeekOwnsPrefetchBankThisBlock =
        controlSeekCommandObserved || controlSeekStillPending;

    diagnosticStemCount = stemCount;
    diagnosticWasPlaying = isPlaying;
    if (isPlaying && realtimeCustomArrangement &&
        playworship::audio::SectionScheduler::size(realtimeSections) > 0) {
      const auto currentFrame = transportClock.getPositionFrames();
      const int sectionIndex =
          playworship::audio::SectionScheduler::findSection(realtimeSections,
                                                            currentFrame);
      playworship::audio::SectionScheduler::Section currentSection;
      if (playworship::audio::SectionScheduler::getSection(
              realtimeSections, sectionIndex, currentSection) &&
          currentFrame >= currentSection.startFrame &&
          currentFrame < currentSection.endFrame) {
        muteSectionInstrumentsThisBlock = currentSection.muteInstruments;
        skipArrangementPartThisBlock = currentSection.skipPlayback;
      }
    }

    const bool dynamicClickSourceActive =
        isMetronomeEnabled() &&
        clickDivisionEnabledState.load(std::memory_order_relaxed) &&
        (clickDivisionState.load(std::memory_order_relaxed) & 0x07) != 0;
    const bool virtualMetronomeSoloActive =
        dynamicClickSourceActive &&
        metronomeSoloState.load(std::memory_order_relaxed);
    const bool dynamicGuideSoloActive =
        dynamicGuideAnnouncementsActive &&
        dynamicGuideSoloState.load(std::memory_order_relaxed);

    // Every audible source participates in the same global solo domain. Click
    // Dyn remains an independent bus even when the song also contains a click
    // stem, and disabled virtual buses do not silence the song.
    anySoloActiveThisBlock =
        virtualMetronomeSoloActive || dynamicGuideSoloActive;
    for (int i = 0; i < stemCount; ++i) {
      if (stemSoloStates[static_cast<size_t>(i)].load(
              std::memory_order_relaxed)) {
        anySoloActiveThisBlock = true;
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
      track->setGlobalMixState(mixGain, anySoloActiveThisBlock);
      track->setSignalIntegrityCaptureEnabled(
          diagnosticsActive, captureFullStemIntegrityThisBlock);
      
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
      dynamicGuideOutputAudible =
          !guideMuted && (!anySoloActiveThisBlock || guideSoloed);
      dynamicGuideShouldRender = dynamicGuideOutputAudible &&
                                 dynamicGuideOutputGain > 0.0001f;
    }

    for (int i = stemCount; i < maxStemCount; ++i)
      stemLevels[static_cast<size_t>(i)].store(0.0f, std::memory_order_relaxed);

    bool routingActive = false;
    // `routingHomogeneous` is true when every loaded stem resolves to
    // exactly the same (channel, width). When homogeneous AND the
    // destination fits inside the device's available channels, the
    // StemMixEngine grouped mix can be routed in a single StemOutputRouter
    // call instead of N per-stem BungeeProcessor runs. See
    // `// ---- Routing-grouped path` below.
    bool routingHomogeneous = stemCount > 0;
    bool groupedPitchCompatible = true;
    int routingCommonChannelOneBased = 0;
    int routingCommonWidth = 0;
    for (int i = 0; i < stemCount; ++i) {
      const int assignedChannel =
          stemOutputChannels[static_cast<size_t>(i)].load(std::memory_order_relaxed);
      const int assignedWidth = juce::jlimit(
          1, 2, stemOutputWidths[static_cast<size_t>(i)].load(
                    std::memory_order_relaxed));
      // The grouped guide bus is unpitched. Preserve per-stem processing
      // when a user explicitly opts a click/guide into pitch shifting.
      if (pitchActiveSemiTones != 0 &&
          stemClickGuideStates[static_cast<size_t>(i)].load(std::memory_order_relaxed) &&
          stemPitchShiftStates[static_cast<size_t>(i)].load(std::memory_order_relaxed))
        groupedPitchCompatible = false;
      if (i == 0) {
        routingCommonChannelOneBased = juce::jlimit(1, numChannels, assignedChannel);
        routingCommonWidth =
            numChannels < 2 ? 1 : juce::jlimit(1, 2, assignedWidth);
      } else {
        if (assignedChannel != routingCommonChannelOneBased ||
            assignedWidth != routingCommonWidth) {
          routingHomogeneous = false;
        }
      }
      // Engage explicit routing not only when using channels beyond 1,
      // but also when any stem is configured as mono on a stereo-capable bus.
      if (assignedChannel > 1 || (numChannels > 1 && assignedWidth == 1)) {
        routingActive = true;
        // Do NOT break out — keep walking so we still detect a divergence
        // among the remaining stems (which decides between grouped and
        // per-stem routing in the `routingActive` branch further down).
      }
    }
    const int routingCommonMaxBase = juce::jmax(
        1, numChannels - (routingCommonWidth >= 2 ? 1 : 0));
    const int routingCommonChannelOneBasedClamped = juce::jlimit(
        1, routingCommonMaxBase, routingCommonChannelOneBased);
    const int routingCommonChannelIndex =
        juce::jlimit(0, juce::jmax(0, numChannels - 1),
                     routingCommonChannelOneBasedClamped - 1);
    // On a stereo device, route each source into its group BEFORE stretching.
    // Click on L, guide on R and stereo instruments need only three voices.
    const bool routingGroupedStereo = numChannels == 2;
    const bool routingGroupedEligible =
        routingActive && (routingHomogeneous || routingGroupedStereo) &&
        groupedPitchCompatible && stemCount > 0;

    if (isPlaying) {
      const bool customArrangement = realtimeCustomArrangement;
      const double sourceLength =
          loadedSongLengthSeconds.load(std::memory_order_relaxed);
      const double length =
          customArrangement
              ? juce::jmax(sourceLength,
                           realtimeArrangementTotalLengthSeconds)
              : sourceLength;
      double windowStartSec = songStartSeconds.load(std::memory_order_relaxed);
      double windowEndSec =
          songEndSeconds.load(std::memory_order_relaxed);
      if (customArrangement) {
        if (!songEndCustomized.load(std::memory_order_relaxed))
          windowEndSec = length;
      } else if (windowEndSec <= 0.0 || windowEndSec > length) {
        windowEndSec = length;
      }
      double fadeOutSec = songFadeOutSeconds.load(std::memory_order_relaxed);
      const double blockStartSec =
          playbackSampleRate > 0.0
              ? transportClock.getPositionSeconds()
              : 0.0;
      const auto blockStartFrame = transportClock.getPositionFrames();
      const auto blockSourceStartFrame =
          mapRealtimeArrangementFrameToSourceFrame(blockStartFrame);
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
          flushTimeProcessingStateOnce();
          flushVirtualTimelineStateOnce();
          seekAppliedThisBlock = true;
        }
      }

      const int realtimeSectionCount =
          playworship::audio::SectionScheduler::size(realtimeSections);
      int mappedPartIndex = -1;
      if (realtimeSectionCount > 0) {
        mappedPartIndex = playworship::audio::SectionScheduler::findSection(
            realtimeSections, blockStartFrame);
        if (juce::isPositiveAndBelow(mappedPartIndex, realtimeSectionCount)) {
          arrangementCurrentPartIndex.store(mappedPartIndex,
                                            std::memory_order_relaxed);
        }
      }

      const int loopPartIndex = realtimeLoopPartIndex;
      const int pendingPartIndex = realtimePendingPartIndex;
      const auto sectionDecision =
          playworship::audio::SectionScheduler::evaluate(
              realtimeSections, transportClock.getPositionFrames(),
              sourceFramesThisBlock, customArrangement, pendingPartIndex,
              loopPartIndex);
      const bool loopPartActive =
          juce::isPositiveAndBelow(loopPartIndex, realtimeSectionCount);
      bool loopPartEngaged = false;
      double loopStartSec = 0.0;
      double loopEndSec = 0.0;
      if (loopPartActive) {
        playworship::audio::SectionScheduler::Section loopPart;
        if (playworship::audio::SectionScheduler::getSection(
                realtimeSections, loopPartIndex, loopPart)) {
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

      const auto requestArrangementTarget =
          [this, controlSeekOwnsPrefetchBankThisBlock](
              const juce::int64 sourceFrame) noexcept {
            if (controlSeekOwnsPrefetchBankThisBlock)
              return;
            for (auto &track : stemTracks) {
              track->requestPrefetchPosition(
                  sourceFrame, kArrangementTransitionPrefetchFrames);
            }
          };
      const auto arrangementTargetReady =
          [this, controlSeekOwnsPrefetchBankThisBlock](
              const juce::int64 sourceFrame,
              const int requiredFrames) noexcept {
            if (controlSeekOwnsPrefetchBankThisBlock)
              return false;
            bool ready = true;
            for (const auto &track : stemTracks) {
              ready =
                  track->isPrefetchReady(sourceFrame, requiredFrames) && ready;
            }
            return ready;
          };

      const auto currentTransitionIntentGeneration =
          arrangementTransitionIntentGeneration.load(
              std::memory_order_acquire);
      const auto transitionIntentStillCurrent =
          [this, currentTransitionIntentGeneration]() noexcept {
            return arrangementTransitionIntentGeneration.load(
                       std::memory_order_acquire) ==
                   currentTransitionIntentGeneration;
          };
      if (deferredArrangementTransitionPending &&
          deferredArrangementTransitionIntentGeneration !=
              currentTransitionIntentGeneration) {
        deferredArrangementTransitionPending = false;
        deferredArrangementTransitionPartIndex = -1;
      }

      // Arm known destinations while the current section is still audible.
      // A loop has precedence over a queued jump, matching SectionScheduler.
      int anticipatedTargetPart = -1;
      if (loopPartActive)
        anticipatedTargetPart = loopPartIndex;
      else if (juce::isPositiveAndBelow(pendingPartIndex,
                                       realtimeSectionCount))
        anticipatedTargetPart = pendingPartIndex;
      else if (sectionDecision.skippedSection)
        anticipatedTargetPart = sectionDecision.targetSection;
      else if (customArrangement &&
               juce::isPositiveAndBelow(mappedPartIndex,
                                        realtimeSectionCount - 1)) {
        playworship::audio::SectionScheduler::Section currentPart;
        playworship::audio::SectionScheduler::Section nextPart;
        if (playworship::audio::SectionScheduler::getSection(
                realtimeSections, mappedPartIndex, currentPart) &&
            playworship::audio::SectionScheduler::getSection(
                realtimeSections, mappedPartIndex + 1, nextPart) &&
            currentPart.endFrame == nextPart.startFrame &&
            currentPart.sourceEndFrame != nextPart.sourceStartFrame) {
          anticipatedTargetPart = mappedPartIndex + 1;
        }
      }

      playworship::audio::SectionScheduler::Section anticipatedTarget;
      if (!controlSeekOwnsPrefetchBankThisBlock &&
          playworship::audio::SectionScheduler::getSection(
              realtimeSections, anticipatedTargetPart, anticipatedTarget)) {
        requestArrangementTarget(
            mapRealtimeArrangementFrameToSourceFrame(
                anticipatedTarget.startFrame));
      }

      // If disk I/O did not finish before a prior boundary, do not flush DSP
      // or emit a partial mix. Keep the current timeline audible and perform a
      // zero-offset handoff once every stem exposes the same target bank.
      if (!controlSeekOwnsPrefetchBankThisBlock &&
          deferredArrangementTransitionPending) {
        requestArrangementTarget(deferredArrangementTransitionSourceFrame);
        if (arrangementTargetReady(
                deferredArrangementTransitionSourceFrame,
                juce::jmax(1, sourceFramesThisBlock)) &&
            transitionIntentStillCurrent()) {
          for (auto &track : stemTracks) {
            track->scheduleReadDiscontinuity(
                0, deferredArrangementTransitionSourceFrame,
                kArrangementTransitionFadeFrames);
          }
          transitionToNextArrangementPart = true;
          nextArrangementPartIndex =
              deferredArrangementTransitionPartIndex;
          nextArrangementPartStartSec =
              playworship::audio::TransportClock::framesToSeconds(
                  deferredArrangementTransitionFrame, playbackSampleRate);
          inBlockArrangementTransitionApplied = true;
          inBlockTransitionPrefixFrames = 0;
          inBlockTransitionTargetFrame =
              deferredArrangementTransitionFrame;
          inBlockTransitionTargetSourceFrame =
              deferredArrangementTransitionSourceFrame;
          deferredArrangementTransitionPending = false;
          deferredArrangementTransitionPartIndex = -1;
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
              playworship::audio::SectionScheduler::getSection(
                  realtimeSections, mappedPartIndex, part);
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
        const int guidePartIndex =
            playworship::audio::SectionScheduler::findGuideTrigger(
                realtimeSections, transportClock.getPositionFrames(),
                sourceFramesThisBlock);
        if (juce::isPositiveAndBelow(
                guidePartIndex, realtimeSectionCount)) {
          const auto guideIndex = static_cast<size_t>(guidePartIndex);
          playworship::audio::SectionScheduler::Section part;
          const bool hasPart =
              playworship::audio::SectionScheduler::getSection(
                  realtimeSections, guidePartIndex, part);
          const double partStartSec =
              playworship::audio::TransportClock::framesToSeconds(
                  part.startFrame, playbackSampleRate);
          const bool excludedByLoop =
              loopPartEngaged && partStartSec >= windowEndSec;
          const bool entryTriggerOwnedByTransition =
              hasPart && part.triggerGuideOnEntry &&
              sectionDecision.transition &&
              guidePartIndex == sectionDecision.targetSection;
          if (!excludedByLoop && !entryTriggerOwnedByTransition &&
              !dynamicGuideAnnouncedByPart[guideIndex]) {
            if (hasPart)
              triggerPartAnnouncement(part.guideTriggerId, guidePartIndex);
            dynamicGuideAnnouncedByPart[guideIndex] = true;
          }
        }

        // Lead-in announcement for a queued jump: when the playhead is one bar
        // away from the end of the current section, fire the target section's
        // guide voice prompt so the musician hears "going to <target>" before
        // the actual transition. The trigger fires at most once per pending
        // target; consuming the pending jump or pointing to a new target
        // re-arms it.
        if (juce::isPositiveAndBelow(pendingPartIndex, realtimeSectionCount) &&
            pendingPartIndex != lastAnnouncedPendingJumpTarget &&
            juce::isPositiveAndBelow(mappedPartIndex, realtimeSectionCount)) {
          playworship::audio::SectionScheduler::Section currentPart;
          if (playworship::audio::SectionScheduler::getSection(
                  realtimeSections, mappedPartIndex, currentPart)) {
            const double leadInBpm = juce::jmax(1.0, getPlaybackTempoBpm());
            const double leadInTempoRatio = getPlaybackTempoRatio();
            const int leadInBeatsPerBar = guideAnnouncementBeatsPerBar.load(
                std::memory_order_relaxed);
            const auto leadInOneBarFrames =
                playworship::audio::TransportClock::secondsToFrames(
                    (60.0 / leadInBpm) *
                        juce::jmax(1, leadInBeatsPerBar) * leadInTempoRatio,
                    playbackSampleRate);
            const auto leadInFireFrame = std::max<juce::int64>(
                0, currentPart.endFrame - leadInOneBarFrames);
            if (blockStartFrame >= leadInFireFrame) {
              playworship::audio::SectionScheduler::Section targetPart;
              if (playworship::audio::SectionScheduler::getSection(
                      realtimeSections, pendingPartIndex, targetPart)) {
                triggerPartAnnouncement(targetPart.guideTriggerId,
                                        pendingPartIndex);
                dynamicGuideAnnouncedByPart[static_cast<size_t>(
                    pendingPartIndex)] = true;
                lastAnnouncedPendingJumpTarget = pendingPartIndex;
              }
            }
          }
        }

        // Loop lead-in: when the playhead is one bar away from the end of the
        // currently looping section, fire the section's own guide trigger so
        // the musician hears "<section>" before the wrap. Lower priority than
        // the pending-jump lead-in (a queued jump always wins), so the
        // pending target is announced instead when both are armed. Reset
        // happens at the wrap (see transition consumer below).
        if (juce::isPositiveAndBelow(realtimeLoopPartIndex,
                                     realtimeSectionCount) &&
            realtimeLoopPartIndex == mappedPartIndex &&
            realtimeLoopPartIndex != lastAnnouncedLoopPartIndex &&
            realtimeLoopPartIndex != pendingPartIndex) {
          playworship::audio::SectionScheduler::Section loopLeadInPart;
          if (playworship::audio::SectionScheduler::getSection(
                  realtimeSections, realtimeLoopPartIndex, loopLeadInPart)) {
            const double loopLeadInBpm =
                juce::jmax(1.0, getPlaybackTempoBpm());
            const double loopLeadInTempoRatio = getPlaybackTempoRatio();
            const int loopLeadInBeatsPerBar =
                guideAnnouncementBeatsPerBar.load(
                    std::memory_order_relaxed);
            const auto loopLeadInOneBarFrames =
                playworship::audio::TransportClock::secondsToFrames(
                    (60.0 / loopLeadInBpm) *
                        juce::jmax(1, loopLeadInBeatsPerBar) *
                        loopLeadInTempoRatio,
                    playbackSampleRate);
            const auto loopLeadInFireFrame = std::max<juce::int64>(
                0, loopLeadInPart.endFrame - loopLeadInOneBarFrames);
            if (blockStartFrame >= loopLeadInFireFrame) {
              triggerPartAnnouncement(loopLeadInPart.guideTriggerId,
                                      realtimeLoopPartIndex);
              dynamicGuideAnnouncedByPart[static_cast<size_t>(
                  realtimeLoopPartIndex)] = true;
              lastAnnouncedLoopPartIndex = realtimeLoopPartIndex;
            }
          }
        }

        dynamicGuideLastPartIndex = mappedPartIndex;
        dynamicGuideLastBlockStartSec = blockStartSec;
        dynamicGuideWasPlaying = true;
      } else {
        dynamicGuideWasPlaying = false;
      }

      const bool skipTransitionBlockedBySeek =
          (seekAppliedThisBlock || controlSeekOwnsPrefetchBankThisBlock) &&
          sectionDecision.skippedSection;

      if (!controlSeekOwnsPrefetchBankThisBlock &&
          !deferredArrangementTransitionPending &&
          !inBlockArrangementTransitionApplied && loopPartEngaged) {
        windowStartSec = loopStartSec;
        windowEndSec = loopEndSec;
        fadeOutSec = 0.0;
        arrangementCurrentPartIndex.store(loopPartIndex, std::memory_order_relaxed);
        arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
        // Note: do NOT bump arrangementTransitionIntentGeneration here. The
        // transitionIntentStillCurrent() check at the top of this block uses
        // the counter as captured at the start; bumping it here would cancel
        // the very transition we are about to apply. The counter is bumped
        // by the consumer at the end of the block instead, so the snapshot
        // refreshes on the NEXT block.

        if (sectionDecision.transition && !skipTransitionBlockedBySeek) {
          transitionToNextArrangementPart = true;
          nextArrangementPartIndex = sectionDecision.targetSection;
          nextArrangementPartStartSec =
              playworship::audio::TransportClock::framesToSeconds(
                  sectionDecision.targetFrame, playbackSampleRate);
        }
      } else if (!controlSeekOwnsPrefetchBankThisBlock &&
                 !deferredArrangementTransitionPending &&
                 !inBlockArrangementTransitionApplied &&
                 customArrangement) {
        if (sectionDecision.transition && !skipTransitionBlockedBySeek) {
          transitionToNextArrangementPart = true;
          nextArrangementPartIndex = sectionDecision.targetSection;
          nextArrangementPartStartSec =
              playworship::audio::TransportClock::framesToSeconds(
                  sectionDecision.targetFrame, playbackSampleRate);
          arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
        }
      } else if (!controlSeekOwnsPrefetchBankThisBlock &&
                 !deferredArrangementTransitionPending &&
                 !inBlockArrangementTransitionApplied &&
                 !customArrangement && realtimeSectionCount > 0 &&
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

      if (transitionToNextArrangementPart &&
          !inBlockArrangementTransitionApplied) {
        inBlockTransitionPrefixFrames = juce::jlimit(
            0, sourceFramesThisBlock,
            sectionDecision.transitionOffsetFrames);
        inBlockTransitionTargetFrame = sectionDecision.targetFrame;
        inBlockTransitionTargetSourceFrame =
            mapRealtimeArrangementFrameToSourceFrame(
                inBlockTransitionTargetFrame);
        requestArrangementTarget(inBlockTransitionTargetSourceFrame);

        const int targetFramesNeeded = juce::jmax(
            1, sourceFramesThisBlock - inBlockTransitionPrefixFrames);
        if (!transitionIntentStillCurrent()) {
          transitionToNextArrangementPart = false;
        } else if (arrangementTargetReady(inBlockTransitionTargetSourceFrame,
                                          targetFramesNeeded)) {
          for (auto &track : stemTracks) {
            track->scheduleReadDiscontinuity(
                inBlockTransitionPrefixFrames,
                inBlockTransitionTargetSourceFrame,
                kArrangementTransitionFadeFrames);
          }
          inBlockArrangementTransitionApplied = true;
        } else {
          deferredArrangementTransitionPending = true;
          deferredArrangementTransitionPartIndex =
              nextArrangementPartIndex;
          deferredArrangementTransitionFrame =
              inBlockTransitionTargetFrame;
          deferredArrangementTransitionSourceFrame =
              inBlockTransitionTargetSourceFrame;
          deferredArrangementTransitionIntentGeneration =
              currentTransitionIntentGeneration;
          transitionToNextArrangementPart = false;
        }
      }

      if (inBlockArrangementTransitionApplied) {
        inBlockTransitionOutputOffset = sourceFramesThisBlock > 0
            ? juce::jlimit(
                  0, numSamples,
                  static_cast<int>(std::lround(
                      (static_cast<double>(inBlockTransitionPrefixFrames) /
                       static_cast<double>(sourceFramesThisBlock)) *
                      static_cast<double>(numSamples))))
            : 0;
        playworship::audio::SectionScheduler::Section targetSection;
        if (playworship::audio::SectionScheduler::getSection(
                realtimeSections, nextArrangementPartIndex, targetSection)) {
          inBlockTransitionTargetGuideId = targetSection.guideTriggerId;
          inBlockTransitionTargetGuideOnEntry =
              targetSection.triggerGuideOnEntry;
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
        const int sourceOffset = playbackSampleRate > 0.0
            ? juce::jlimit(
                  0, sourceFramesThisBlock,
                  static_cast<int>(std::llround(
                      (windowEndSec - blockStartSec) *
                      playbackSampleRate)))
            : sourceFramesThisBlock;
        songStopOutputOffset = sourceFramesThisBlock > 0
            ? juce::jlimit(
                  0, numSamples,
                  static_cast<int>(std::lround(
                      (static_cast<double>(sourceOffset) /
                       static_cast<double>(sourceFramesThisBlock)) *
                      static_cast<double>(numSamples))))
            : 0;
      }

      // ---- Benchmark: preRead ends here ----------------------------------
      // Everything before this point (state reads, section snapshot, song
      // transition gain, tempo updates, seek coordination, fade math,
      // arrangement transitions, etc.) counts as preRead. The actual
      // audio-producing work starts inside the routingActive branch.
      stagePreReadEndTick = juce::Time::getHighResolutionTicks();

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
        bool timeProcessorsReady = true;
        if (timeProcessActive) {
          for (int i = 0; i < stemCount; ++i) {
            const size_t stemIndex = static_cast<size_t>(i);
            const bool pitchShiftable =
                stemPitchShiftStates[stemIndex].load(
                    std::memory_order_relaxed);
            stemPitchShiftableFlags[stemIndex] = pitchShiftable;
            const bool shouldProcess = tempoActive || pitchShiftable;
            stemNeedsTimeProcess[stemIndex] = shouldProcess;
            if (!shouldProcess || routingGroupedEligible)
              continue;
            auto &proc = stemPitchProcessors[stemIndex];
            proc.setContentType(
                stemContentTypeForPitchShiftable(pitchShiftable));
            proc.setPitchSemiTones(pitchShiftable ? pitchActiveSemiTones : 0);
            proc.setTempoRatio(playbackTempoRatioActive);
            if (!proc.tryResolvePendingReset() || !proc.isPrepared() ||
                proc.getNumChannels() != 2) {
              timeProcessorsReady = false;
              continue;
            }
            stemProcessLatencies[stemIndex] = proc.getLatencySamples();
            targetLatency =
                juce::jmax(targetLatency, stemProcessLatencies[stemIndex]);
          }
        }
        timeProcessingBlockedThisBlock =
            timeProcessActive && !timeProcessorsReady;

        if (timeProcessActive && routingGroupedEligible)
          targetLatency = mixEngine.getOutputLatencySamples();

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

        if (diagnosticsActive &&
            ++stemTimingSnapshotDecimator >=
            kStemTimingSnapshotEveryBlocks) {
          stemTimingSnapshotDecimator = 0;

          AudioDiagnostics::StemTimingEvent stemTimingEvent;
          stemTimingEvent.wallMs = diagnostics.wallTimeForHighResolutionTick(
              juce::Time::getHighResolutionTicks());
          stemTimingEvent.tempoRatio = playbackTempoRatioActive;
          stemTimingEvent.pitchSemiTones = pitchActiveSemiTones;
          stemTimingEvent.stemCount = juce::jlimit(
              0, AudioDiagnostics::StemTimingEvent::kMaxStemMetrics,
              stemCount);
          stemTimingEvent.targetLatencySamples = routingPitchLatencySamples;
          stemTimingEvent.routingActive = true;
          stemTimingEvent.routingGroupedEligible = routingGroupedEligible;
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
            stemTimingEvent.streamingMisses[stemIndex] =
                stemTracks[stemIndex]->getRealtimeReadMissCount();
            stemTimingEvent.streamingMissingFrames[stemIndex] =
                stemTracks[stemIndex]->getRealtimeMissingFrameCount();

            // Capture each destination alongside the selected DSP path.
            const int dbgChannel = stemOutputChannels[stemIndex].load(
                std::memory_order_relaxed);
            const int dbgWidth = juce::jlimit(
                1, 2, stemOutputWidths[stemIndex].load(
                          std::memory_order_relaxed));
            stemTimingEvent.routingChannelOneBased[stemIndex] =
                static_cast<int16_t>(
                    juce::jlimit(-32768, 32767, dbgChannel));
            stemTimingEvent.routingWidth[stemIndex] =
                static_cast<int16_t>(
                    juce::jlimit(-32768, 32767, dbgWidth));

            // ---- Benchmark: sample worker drain for this stem ----------
            // We read the worker's most recent visit atomics and compute
            // the time since it ENDED. A large gap_us means the worker
            // round-robin is taking too long to come back to this stem.
            auto *buffering = stemTracks[stemIndex]->bufferingSourceForBenchmark();
            if (buffering != nullptr) {
              stemTimingEvent.streamingLockMisses[stemIndex] =
                  buffering->getRealtimeLockMissCount();
              const int64_t drainStart =
                  buffering->getLastWorkerDrainStartTick();
              const int64_t drainDur =
                  buffering->getLastWorkerDrainDurationTicks();
              const int64_t nowTick = juce::Time::getHighResolutionTicks();
              if (drainStart > 0) {
                const double drainUs =
                    static_cast<double>(drainDur) * 1e6 /
                    juce::Time::getHighResolutionTicksPerSecond();
                const double gapUs =
                    static_cast<double>(nowTick - drainStart - drainDur) *
                    1e6 /
                    juce::Time::getHighResolutionTicksPerSecond();
                stemTimingEvent.workerDrainUs[stemIndex] =
                    static_cast<int32_t>(
                        juce::jlimit<int64_t>(0, 2000000000L,
                                               static_cast<int64_t>(drainUs)));
                stemTimingEvent.workerGapUs[stemIndex] =
                    static_cast<int32_t>(
                        juce::jlimit<int64_t>(0, 2000000000L,
                                               static_cast<int64_t>(gapUs)));
              }
              stemTimingEvent.workerVisitCount[stemIndex] = static_cast<uint32_t>(
                  buffering->getWorkerVisitCount());

              // ---- Benchmark: sample REAL drain stats ----------------
              // Most useTimeSlice() calls are no-ops (buffer full). The
              // REAL-drain atomics track only the visits that actually
              // pulled data from the disk/decoder pipeline, and the
              // gap between them is computed from the worker's own
              // pair (lastRealDrainEndTick, previousRealDrainEndTick)
              // so it is not biased by the decimation cadence.
              const int64_t lastRealEnd =
                  buffering->getLastRealDrainEndTick();
              const int64_t prevRealEnd =
                  buffering->getPreviousRealDrainEndTick();
              const int64_t realDur =
                  buffering->getLastRealDrainDurationTicks();
              if (lastRealEnd > 0) {
                const double realDrainUs =
                    static_cast<double>(realDur) * 1e6 /
                    juce::Time::getHighResolutionTicksPerSecond();
                stemTimingEvent.realDrainUs[stemIndex] =
                    static_cast<int32_t>(
                        juce::jlimit<int64_t>(0, 2000000000L,
                                               static_cast<int64_t>(realDrainUs)));
              }
              if (lastRealEnd > 0 && prevRealEnd > 0) {
                const double realGapUs =
                    static_cast<double>(lastRealEnd - prevRealEnd) * 1e6 /
                    juce::Time::getHighResolutionTicksPerSecond();
                stemTimingEvent.realDrainGapUs[stemIndex] =
                    static_cast<int32_t>(
                        juce::jlimit<int64_t>(0, 2000000000L,
                                               static_cast<int64_t>(realGapUs)));
              }
              stemTimingEvent.realDrainCount[stemIndex] = static_cast<uint32_t>(
                  buffering->getRealDrainCount());

              // ---- Benchmark: sample AAC decode + resample stats -----
              // ResamplingPositionableSource::getNextAudioBlock is
              // called from the background worker when the buffering
              // source needs to refill; it is the only place where the
              // AAC decode (via AudioFormatReader) and the resampler
              // run. The atomic (last, previous) pair lets the audio
              // thread compute the inter-call gap without any
              // decimation bias.
              auto *resampling =
                  stemTracks[stemIndex]->resamplingSourceForBenchmark();
              if (resampling != nullptr) {
                const int64_t decodeStart =
                    resampling->getLastDecodeStartTick();
                const int64_t decodeDur =
                    resampling->getLastDecodeDurationTicks();
                const int64_t prevDecodeStart =
                    resampling->getPreviousDecodeStartTick();
                if (decodeStart > 0) {
                  const double decodeUs =
                      static_cast<double>(decodeDur) * 1e6 /
                      juce::Time::getHighResolutionTicksPerSecond();
                  stemTimingEvent.decodeUs[stemIndex] =
                      static_cast<int32_t>(
                          juce::jlimit<int64_t>(0, 2000000000L,
                                                 static_cast<int64_t>(decodeUs)));
                }
                if (decodeStart > 0 && prevDecodeStart > 0) {
                  const double decodeGapUs =
                      static_cast<double>(decodeStart - prevDecodeStart) *
                      1e6 /
                      juce::Time::getHighResolutionTicksPerSecond();
                  stemTimingEvent.decodeGapUs[stemIndex] =
                      static_cast<int32_t>(
                          juce::jlimit<int64_t>(0, 2000000000L,
                                                 static_cast<int64_t>(decodeGapUs)));
                }
                stemTimingEvent.decodeCount[stemIndex] =
                    static_cast<uint32_t>(
                        resampling->getDecodeCallCount());
              }

              // ---- Benchmark: Bungee per-stem -----------------------
              // The Bungee timing was written to lastBungeeUsMicros
              // / lastBungeeGapUsMicros / lastBungeeCount on the
              // same audio thread (no other writer). Just copy the
              // most recent values into the snapshot.
              if (lastBungeeUsMicros[stemIndex] > 0.0) {
                stemTimingEvent.bungeeUs[stemIndex] =
                    static_cast<int32_t>(juce::jlimit<double>(
                        0.0, 2000000000.0, lastBungeeUsMicros[stemIndex]));
              }
              if (lastBungeeGapUsMicros[stemIndex] > 0.0) {
                stemTimingEvent.bungeeGapUs[stemIndex] =
                    static_cast<int32_t>(juce::jlimit<double>(
                        0.0, 2000000000.0,
                        lastBungeeGapUsMicros[stemIndex]));
              }
              stemTimingEvent.bungeeCount[stemIndex] =
                  lastBungeeCount[stemIndex];
            }
          }

          diagnostics.pushStemTimingEvent(stemTimingEvent);
        }

        buffer.clear();
        const int routingInputSamples =
            timeProcessActive ? sourceFramesThisBlock : numSamples;
        if (routingGroupedEligible && !timeProcessingBlockedThisBlock &&
            timeProcessActive) {
          // ---- Routing-grouped path ------------------------------------
          // Three voices for melodic, percussion and guide buses. On stereo
          // devices the input mix already contains the physical L/R routing;
          // homogeneous multichannel routing is folded after rendering.
          mixEngine.mixStems(stemTracks, stemClickGuideStates.data(),
                             stemPitchShiftStates.data(), stemCount,
                             sourceFramesThisBlock, numChannels,
                             routingGroupedStereo ? stemOutputChannels.data() : nullptr,
                             routingGroupedStereo ? stemOutputWidths.data() : nullptr);
          // ---- Benchmark: per-stem Bungee timing (grouped) -------------
          // Grouped path makes per-stem Bungee timings non-applicable,
          // so we zero them out so the decimated StemTimingEvent only
          // reports the engine's latency, not stale per-stem reads.
          for (size_t stemIndex = 0;
               stemIndex < static_cast<size_t>(stemCount); ++stemIndex) {
            lastBungeeUsMicros[stemIndex] = 0.0;
            lastBungeeGapUsMicros[stemIndex] = 0.0;
            lastBungeeCount[stemIndex] = 0;
          }
          // ---- Benchmark: decode/render (pitch processor) ---------------
          // Stage bracket reuses stageDecodeRenderEndTick by accumulating
          // the three grouped TimeStretchProcessor.process() calls so
          // the audio_*.csv "decode_render" column still reflects the
          // audio-thread cost of pitch/tempo work.
          int64_t drStart = juce::Time::getHighResolutionTicks();
          // Always pass the real callback length, even when rendering to the
          // larger scratch storage used by homogeneous multichannel routing.
          mixEngine.renderToOutput(routingGroupedStereo ? buffer : routingGroupedRenderStorage,
                                   timeProcessActive, sourceFramesThisBlock, numSamples,
                                   fadeStart, fadeEnd, windowFadeStart,
                                   windowFadeEnd);
          int64_t drEnd = juce::Time::getHighResolutionTicks();
          stageDecodeRenderEndTick += (drEnd - drStart);
          timeProcessingBlockedThisBlock = mixEngine.wasLastRenderBlocked();
          stemOutputLatencyThisBlock = mixEngine.getOutputLatencySamples();
          if (!routingGroupedStereo)
            stemOutputRouter.routeStemToOutputChannels(
                routingGroupedRenderStorage, routingCommonChannelIndex,
                routingCommonWidth, buffer);
        } else if (timeProcessingBlockedThisBlock) {
          // Keep every source and the transport moving as one unit, but do not
          // let a subset of recovered processors or dry stems leak through.
          for (int i = 0; i < stemCount; ++i) {
            stemRoutingInputBuffer.clear(0, routingInputSamples);
            juce::AudioSourceChannelInfo stemInfo(
                &stemRoutingInputBuffer, 0, routingInputSamples);
            stemTracks[static_cast<size_t>(i)]->getNextAudioBlock(stemInfo);
            stemPitchDelayLines[static_cast<size_t>(i)].invalidateHistory();
          }
        } else {
          // ---- Per-stem routing path ------------------------------------
          // Preserve independent DSP for heterogeneous multichannel outputs
          // and explicitly pitched guide stems. With pitch/tempo off, this
          // same loop only reads, applies fades and routes each source.
          for (int i = 0; i < stemCount; ++i) {
            stemRoutingInputBuffer.clear(0, routingInputSamples);
            stemRoutingTempBuffer.clear(0, numSamples);
            const size_t stemIndex = static_cast<size_t>(i);
            if (timeProcessActive && stemNeedsTimeProcess[stemIndex]) {
              juce::AudioSourceChannelInfo stemInfo(
                  &stemRoutingInputBuffer, 0, routingInputSamples);
              stemTracks[stemIndex]->getNextAudioBlock(stemInfo);
              auto &proc = stemPitchProcessors[stemIndex];
              // ---- Benchmark: decode/render (pitch processor) -----------
              int64_t drStartLocal = juce::Time::getHighResolutionTicks();
              proc.process(stemRoutingInputBuffer, routingInputSamples,
                           stemRoutingTempBuffer, numSamples);
              int64_t drEndLocal = juce::Time::getHighResolutionTicks();
              stageDecodeRenderEndTick += (drEndLocal - drStartLocal);
              // ---- Benchmark: per-stem Bungee timing --------------------
              // Captured here (in the timeProcessActive branch) so we
              // only see real work. The previous Bungee start tick
              // for each stem is kept in a static array so we can
              // compute the inter-call gap (decimation cadence bias
              // applies, same caveat as workerGapUs). The values are
              // written into the StemTimingEvent that is created in
              // the decimated snapshot block further down.
              static int64_t prevBungeeStart[AudioDiagnostics::StemTimingEvent::kMaxStemMetrics]{};
              const int64_t gapTicks = drStartLocal - prevBungeeStart[stemIndex];
              prevBungeeStart[stemIndex] = drStartLocal;
              lastBungeeUsMicros[stemIndex] =
                  static_cast<double>(drEndLocal - drStartLocal) * 1e6 /
                  juce::Time::getHighResolutionTicksPerSecond();
              lastBungeeGapUsMicros[stemIndex] =
                  gapTicks > 0
                      ? static_cast<double>(gapTicks) * 1e6 /
                            juce::Time::getHighResolutionTicksPerSecond()
                      : 0.0;
              lastBungeeCount[stemIndex] =
                  lastBungeeCount[stemIndex] + 1;
            } else {
              // No time processing for this stem — clear any residual
              // Bungee timing from a previous active period. Without
              // this reset, the decimated snapshot would keep
              // reporting the last non-zero Bungee duration even
              // after the user has turned pitch/tempo off, which
              // makes the CSV harder to read across scenarios.
              lastBungeeUsMicros[stemIndex] = 0.0;
              lastBungeeGapUsMicros[stemIndex] = 0.0;
              lastBungeeCount[stemIndex] = 0;
              juce::AudioSourceChannelInfo stemInfo(&stemRoutingTempBuffer, 0,
                                                    numSamples);
              stemTracks[stemIndex]->getNextAudioBlock(stemInfo);
            }
            // Run the delay line only when it is actually altering the
            // output. At delay == 0 with no transition in flight the
            // process() call is a transparent memory copy that costs
            // ~40us per stem and adds nothing audible — skipping it
            // drops the per-block cost dramatically when pitch / tempo
            // is off. When the user later enables pitch, the first
            // block's prefill may be silent for up to one block
            // (~11ms); that is below the audibility threshold and
            // strictly better than paying the copy cost every block.
            if (stemPitchDelayLines[stemIndex].isActive()) {
              int64_t dlStart = juce::Time::getHighResolutionTicks();
              stemPitchDelayLines[stemIndex].process(stemRoutingTempBuffer);
              int64_t dlEnd = juce::Time::getHighResolutionTicks();
              stageDecodeRenderEndTick += (dlEnd - dlStart);
            }
            const bool isClickOrGuideStem =
                stemClickGuideStates[stemIndex].load(
                    std::memory_order_relaxed);
            if (!isClickOrGuideStem) {
              applyBufferGainRamp(stemRoutingTempBuffer,
                                  fadeStart * windowFadeStart,
                                  fadeEnd * windowFadeEnd);
            }
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
        }
        routedStemFadeApplied = true;
        usedSeparatedMix = false;
        // ---- Benchmark: read_next ends here -----------------------------
        stageReadNextEndTick = juce::Time::getHighResolutionTicks();
      } else {
        const bool isolateGuideDuringFade =
            transportFadeAffectsBlock;
        const bool useSeparatedMix =
            isolateGuideDuringFade || windowFadeActive ||
            (pitchActiveSemiTones != 0) ||
            !juce::approximatelyEqual(playbackTempoRatioActive, 1.0);
        usedSeparatedMix = useSeparatedMix;

        if (useSeparatedMix) {
          // ---- Benchmark: mix_stems (separated) ------------------------
          int64_t msStart = juce::Time::getHighResolutionTicks();
          mixEngine.mixStems(stemTracks, stemClickGuideStates.data(),
                             stemPitchShiftStates.data(), stemCount,
                             sourceFramesThisBlock, numChannels);
          int64_t msEnd = juce::Time::getHighResolutionTicks();
          stageMixStemsEndTick = msEnd;
          (void)msStart; // start equals preReadEndTick captured above
        } else {
          // ---- Benchmark: mix_stems (direct) ---------------------------
          mixEngine.renderDirect(stemTracks, stemCount, buffer);
          stageMixStemsEndTick = juce::Time::getHighResolutionTicks();
        }
      }

      if (inBlockArrangementTransitionApplied) {
        transportClock.seek(inBlockTransitionTargetFrame);
        transportClock.advance(
            juce::jmax(0, sourceFramesThisBlock -
                              inBlockTransitionPrefixFrames));
      } else {
        transportClock.advance(sourceFramesThisBlock);
      }
      globalSamplePosition.store(transportClock.getPositionFrames(),
                                 std::memory_order_relaxed);
      songCurrentPositionSeconds.store(transportClock.getPositionSeconds(),
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
  if (usedSeparatedMix) {
    const bool timeProcessActive =
        (pitchActiveSemiTones != 0) ||
        !juce::approximatelyEqual(playbackTempoRatioActive, 1.0);
    // ---- Benchmark: mix_output (master bus) -----------------------------
    stageMixOutputStartTick = juce::Time::getHighResolutionTicks();
    mixEngine.renderToOutput(buffer, timeProcessActive, sourceFramesThisBlock, numSamples,
                             fadeStart, fadeEnd, windowFadeStart,
                             windowFadeEnd);
    stageMixOutputEndTick = juce::Time::getHighResolutionTicks();
    timeProcessingBlockedThisBlock = mixEngine.wasLastRenderBlocked();
    stemOutputLatencyThisBlock = mixEngine.getOutputLatencySamples();
  } else if (!routedStemFadeApplied) {
    const float combinedFadeStart = fadeStart * windowFadeStart;
    const float combinedFadeEnd = fadeEnd * windowFadeEnd;
    applyBufferGainRamp(buffer, combinedFadeStart, combinedFadeEnd);
  }

  const bool trimSongBusesAtExactEnd =
      stopAtEnd && !transitionToNextArrangementPart &&
      !deferredArrangementTransitionPending &&
      songStopOutputOffset >= 0 && songStopOutputOffset < numSamples;
  if (trimSongBusesAtExactEnd)
    buffer.clear(songStopOutputOffset, numSamples - songStopOutputOffset);

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

    const bool metronomeAudible =
        !muted && (!anySoloActiveThisBlock || soloed);
    metronome.setVolume(effectiveVolume);
    metronome.setMuted(!metronomeAudible);
    if (inBlockArrangementTransitionApplied) {
      if (inBlockTransitionOutputOffset > 0) {
        syncMetronomeToSourceSeconds(renderedSourceStartSec);
        metronome.process(metronomeRenderBuffer, 0,
                          inBlockTransitionOutputOffset);
      }
      metronome.reset();
      syncMetronomeToSourceSeconds(
          playworship::audio::TransportClock::framesToSeconds(
              inBlockTransitionTargetSourceFrame, playbackSampleRate));
      metronome.triggerImmediateClick();
      if (inBlockTransitionOutputOffset < numSamples) {
        metronome.process(metronomeRenderBuffer,
                          inBlockTransitionOutputOffset,
                          numSamples - inBlockTransitionOutputOffset);
      }
    } else {
      syncMetronomeToSourceSeconds(renderedSourceStartSec);
      metronome.process(metronomeRenderBuffer);
    }
    metronomeLatencyDelay.setDelay(stemOutputLatencyThisBlock);
    if (timeProcessingBlockedThisBlock) {
      // Advance the click voice/phase with the song but do not retain or emit
      // only one side of a time-processor recovery gap.
      metronomeRenderBuffer.clear();
      metronomeLatencyDelay.invalidateHistory();
    } else {
      metronomeLatencyDelay.process(metronomeRenderBuffer);
    }
    if (!juce::approximatelyEqual(effectiveGain, 1.0f))
      metronomeRenderBuffer.applyGain(effectiveGain);
    if (trimSongBusesAtExactEnd) {
      metronomeRenderBuffer.clear(
          songStopOutputOffset, numSamples - songStopOutputOffset);
    }

    const float metronomeMeterLeft =
        metronomeRenderBuffer.getMagnitude(0, 0, numSamples);
    const float metronomeMeterRight =
        outputWidth >= 2
            ? metronomeRenderBuffer.getMagnitude(
                  juce::jmin(1, metronomeRenderBuffer.getNumChannels() - 1), 0,
                  numSamples)
            : 0.0f;
    // A click can begin and end between two UI timer ticks. Accumulate the
    // highest rendered post-latency/post-gain peak until the Click Dyn VU
    // consumes it instead of overwriting it with the next silent block.
    publishMeterPeak(metronomeMeterLevelLeft, metronomeMeterLeft);
    publishMeterPeak(metronomeMeterLevelRight, metronomeMeterRight);

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
        const auto stopMask =
            midiPendingPadStopMask.exchange(0, std::memory_order_acq_rel);
        const auto triggerMask =
            midiPendingPadTriggerMask.exchange(0, std::memory_order_acq_rel);
        for (int note = 0; note < 12; ++note) {
          const auto bit = static_cast<uint16_t>(1U << note);
          if ((stopMask & bit) != 0)
            padsEngine.stopNote(note);
        }
        for (int note = 0; note < 12; ++note) {
          const auto bit = static_cast<uint16_t>(1U << note);
          if ((triggerMask & bit) != 0)
            if (padsEngine.isNoteLoaded(note)) {
              padsEngine.triggerNote(note, 0);
              if (!padsEngine.isNoteFullyLoaded(note))
                padsFullLoadRequested.store(true, std::memory_order_release);
            } else {
              midiPendingPadLoadMask.fetch_or(bit, std::memory_order_release);
              midiPendingPadTriggerMask.fetch_or(bit, std::memory_order_release);
            }
        }
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
      applyBufferGainRamp(padsRenderBuffer, fadeStart, fadeEnd);

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
  if (inBlockArrangementTransitionApplied) {
    if (dynamicGuideShouldRender && dynamicGuideAnnouncementsActive &&
        inBlockTransitionOutputOffset > 0) {
      dynamicGuideEngine.render(dynamicGuideRenderBuffer, 0,
                                inBlockTransitionOutputOffset);
    }
    dynamicGuideEngine.reset();
    dynamicGuideAnnouncedByPart.fill(0);
    if (dynamicGuideAnnouncementsActive &&
        inBlockTransitionTargetGuideOnEntry) {
      if (!dynamicGuideEngine.triggerForId(
              inBlockTransitionTargetGuideId)) {
        dynamicGuideMissingPartIndex.store(
            nextArrangementPartIndex, std::memory_order_release);
      }
      if (juce::isPositiveAndBelow(
              nextArrangementPartIndex,
              playworship::audio::SectionScheduler::maxSections)) {
        dynamicGuideAnnouncedByPart[
            static_cast<size_t>(nextArrangementPartIndex)] = 1;
      }
    }
    dynamicGuideLastPartIndex = nextArrangementPartIndex;
    dynamicGuideLastBlockStartSec =
        playworship::audio::TransportClock::framesToSeconds(
            inBlockTransitionTargetFrame, playbackSampleRate);
    dynamicGuideWasPlaying = true;
    if (dynamicGuideShouldRender && dynamicGuideAnnouncementsActive &&
        inBlockTransitionOutputOffset < numSamples) {
      dynamicGuideEngine.render(
          dynamicGuideRenderBuffer, inBlockTransitionOutputOffset,
          numSamples - inBlockTransitionOutputOffset);
    }
  } else if (dynamicGuideShouldRender &&
             dynamicGuideAnnouncementsActive) {
    dynamicGuideEngine.render(dynamicGuideRenderBuffer);
  }

  // Pitch/tempo processing delays the stems. In normal playback, feed the
  // guide delay every callback (including silence) so its first announcement
  // is aligned too.
  dynamicGuideLatencyDelay.setDelay(stemOutputLatencyThisBlock);
  if (timeProcessingBlockedThisBlock) {
    // The guide voice still advances, but its partial cue and pre-gap delay
    // history are discarded with the blocked song bus.
    dynamicGuideRenderBuffer.clear();
    dynamicGuideLatencyDelay.invalidateHistory();
  } else {
    dynamicGuideLatencyDelay.process(dynamicGuideRenderBuffer);
  }
  if (trimSongBusesAtExactEnd) {
    dynamicGuideRenderBuffer.clear(
        songStopOutputOffset, numSamples - songStopOutputOffset);
  }

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

  if (songLoadedForDiagnostics && diagnosticsActive) {
    diagnosticMasterMetrics = playworship::audio::measureSignalIntegrity(
        buffer, 0, numSamples, masterOutGain, diagnosticMasterSignalState);
  }

  if (!juce::approximatelyEqual(masterOutGain, 1.0f))
    buffer.applyGain(masterOutGain);

  // Song replacement is committed only after this envelope has reached zero.
  // Apply it after all song-related buses so stems, click and guide remain
  // sample-continuous at the handoff.
  // ---- Benchmark: ramp_transition (fade envelope at handoff) -------------
  stageRampTransitionStartTick = juce::Time::getHighResolutionTicks();
  applySongTransitionRamp(buffer, songLoadGainRamp);
  stageRampTransitionEndTick = juce::Time::getHighResolutionTicks();

  if (songLoadedForDiagnostics && diagnosticsActive) {
    diagnosticOutputMetrics = playworship::audio::measureSignalIntegrity(
        buffer, 0, numSamples, 1.0f, diagnosticOutputSignalState);
  }

  if (transitionToNextArrangementPart) {
    // Capture the source section before we overwrite the current part index.
    const int sourceArrangementPartIndex =
        arrangementCurrentPartIndex.load(std::memory_order_relaxed);
    arrangementCurrentPartIndex.store(nextArrangementPartIndex,
                                      std::memory_order_relaxed);
    arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
    // Re-arm the jump and loop lead-ins so a fresh iteration can re-announce.
    lastAnnouncedPendingJumpTarget = -1;
    lastAnnouncedLoopPartIndex = -1;
    // Bump the transition intent generation so the audio-side snapshot
    // refreshes the cleared pending jump / loop fields on the NEXT block.
    // This must happen HERE (after the transitionIntentStillCurrent check at
    // the top of the block has already used the snapshot) and not in the
    // transition setup, otherwise the check would observe its own bump and
    // cancel the transition it was supposed to validate.
    arrangementTransitionIntentGeneration.fetch_add(
        1, std::memory_order_release);
    juce::ignoreUnused(nextArrangementPartStartSec);
    if (inBlockArrangementTransitionApplied) {
      songCurrentPositionSeconds.store(transportClock.getPositionSeconds(),
                                       std::memory_order_relaxed);
      globalSamplePosition.store(transportClock.getPositionFrames(),
                                 std::memory_order_relaxed);
      syncMetronomeToSourceSeconds(
          playworship::audio::TransportClock::framesToSeconds(
              inBlockTransitionTargetSourceFrame +
                  juce::jmax(0, sourceFramesThisBlock -
                                    inBlockTransitionPrefixFrames),
              playbackSampleRate));
    }
  } else if (stopAtEnd && !deferredArrangementTransitionPending) {
    const double stoppedPositionSeconds = juce::jmax(0.0, stopAtEndSec);
    const auto stoppedPositionFrames =
        playworship::audio::TransportClock::secondsToFrames(
            stoppedPositionSeconds, playbackSampleRate);
    setTransportState(TransportState::stopped);
    transportClock.seek(stoppedPositionFrames);
    globalSamplePosition.store(stoppedPositionFrames,
                               std::memory_order_relaxed);
    songCurrentPositionSeconds.store(stoppedPositionSeconds,
                                     std::memory_order_relaxed);
    requestedSeekPosition.store(stoppedPositionFrames,
                                std::memory_order_relaxed);
    arrangementTransitionIntentGeneration.fetch_add(
        1, std::memory_order_release);
  }

  const float left =
      numChannels > 0 ? buffer.getMagnitude(0, 0, numSamples) : 0.0f;
  const float right =
      numChannels > 1 ? buffer.getMagnitude(1, 0, numSamples) : left;
  meterLevelLeft.store(left, std::memory_order_relaxed);
  meterLevelRight.store(right, std::memory_order_relaxed);

  // Record the complete callback, including grouped time processing, virtual
  // buses, master gain and transition handling.
  if (songLoadedForDiagnostics && diagnosticsActive) {
    const double budgetMs =
        playbackSampleRate > 0.0
            ? (static_cast<double>(callbackSamples) / playbackSampleRate) *
                  1000.0
            : 0.0;
    int64_t streamingMisses = 0;
    int64_t streamingMissingFrames = 0;
    for (const auto &track : stemTracks) {
      streamingMisses += track->getRealtimeReadMissCount();
      streamingMissingFrames += track->getRealtimeMissingFrameCount();
    }
    uint64_t timeProcessorResetMisses =
        mixEngine.getTimeProcessorResetMissCount();
    uint64_t timeProcessorServiceFailures =
        mixEngine.getTimeProcessorServiceFailureCount();
    const int preparedProcessorCount =
        preparedRoutingProcessorCount.load(std::memory_order_acquire);
    for (int i = 0; i < preparedProcessorCount; ++i) {
      timeProcessorResetMisses +=
          stemPitchProcessors[static_cast<size_t>(i)].getResetMissCount();
      timeProcessorServiceFailures +=
          stemPitchProcessors[static_cast<size_t>(i)]
              .getServiceFailureCount();
    }
    const auto blockEndTick = juce::Time::getHighResolutionTicks();
    const double blockMs =
        juce::Time::highResolutionTicksToSeconds(blockEndTick -
                                                 blockStartTick) *
        1000.0;

    const auto sourceGeneration =
        diagnosticSourceGeneration.load(std::memory_order_relaxed);
    const auto markerId =
        diagnosticMarkerGeneration.load(std::memory_order_relaxed);
    const double wallMs =
        diagnostics.wallTimeForHighResolutionTick(blockEndTick);
    const double songPositionSec = transportClock.getPositionSeconds();
    const auto pushSignalMetrics =
        [&](const playworship::audio::SignalIntegrityMetrics& metrics,
            const int sourceKind, const int sourceIndex,
            const float controlVolume, const float controlGain,
            const bool muted, const bool soloed, const bool audible) {
          AudioDiagnostics::DistortionEvent event;
          event.wallMs = wallMs;
          event.songPositionSec = songPositionSec;
          event.sourceGeneration = sourceGeneration;
          event.markerId = markerId;
          event.sourceKind = sourceKind;
          event.sourceIndex = sourceIndex;
          event.channels = metrics.channels;
          event.samplesPerChannel = metrics.samplesPerChannel;
          event.inputPeak = metrics.inputPeak;
          event.outputPeak = metrics.outputPeak;
          event.outputRms = metrics.outputRms;
          event.maxDelta = metrics.maxDelta;
          event.boundaryDelta = metrics.boundaryDelta;
          event.nearClipSamples = metrics.nearClipSamples;
          event.overSamples = metrics.overSamples;
          event.largeDeltaSamples = metrics.largeDeltaSamples;
          event.nonFiniteSamples = metrics.nonFiniteSamples;
          event.controlVolume = controlVolume;
          event.controlGain = controlGain;
          event.muted = muted;
          event.soloed = soloed;
          event.audible = audible;
          diagnostics.pushDistortionEvent(event);
        };

    pushSignalMetrics(
        diagnosticMasterMetrics, 1, -1,
        masterVolume.load(std::memory_order_relaxed),
        masterGain.load(std::memory_order_relaxed),
        masterMuted.load(std::memory_order_relaxed), false,
        !masterMuted.load(std::memory_order_relaxed));
    pushSignalMetrics(diagnosticOutputMetrics, 2, -1, 1.0f, 1.0f,
                      false, false, true);

    ++distortionSnapshotDecimator;
    const bool periodicStemSnapshot =
        distortionSnapshotDecimator >= kDistortionStemSnapshotEveryBlocks;
    if (periodicStemSnapshot)
      distortionSnapshotDecimator = 0;
    if (periodicStemSnapshot) {
      const int metricStemCount = juce::jmin(
          diagnosticStemCount, static_cast<int>(stemTracks.size()));
      for (int i = 0; i < metricStemCount; ++i) {
        const auto& track = stemTracks[static_cast<size_t>(i)];
        const bool muted = track->isMuted();
        const bool soloed = track->isSoloed();
        pushSignalMetrics(
            track->getSignalIntegrityMetricsForDiagnostics(), 0, i,
            track->getVolume(), track->getTrim(),
            muted, soloed, !muted && (!anySoloActiveThisBlock || soloed));
      }
    }

    // ---- Benchmark: compute per-stage microseconds ----------------------
    // Each stage delta is the high-resolution tick difference converted to
    // microseconds and saturated to int16_t. A stage that didn't run in
    // this block keeps the default 0 (so the analysis can distinguish
    // "didn't run" from "ran in 0us").
    const double ticksPerSecond =
        juce::Time::getHighResolutionTicksPerSecond();
    const double ticksToUs = 1e6 / ticksPerSecond;
    auto us = [ticksToUs](int64_t delta) {
      return AudioDiagnostics::saturateUsToInt16(
          static_cast<double>(delta) * ticksToUs);
    };
    const int16_t stagePreReadUs =
        stagePreReadEndTick > 0
            ? us(stagePreReadEndTick - blockStartTick)
            : static_cast<int16_t>(0);
    const int16_t stageReadNextUs =
        stageReadNextEndTick > 0
            ? us(stageReadNextEndTick - stagePreReadEndTick)
            : static_cast<int16_t>(0);
    const int16_t stageDecodeRenderUs =
        stageDecodeRenderEndTick > 0
            ? us(stageDecodeRenderEndTick)
            : static_cast<int16_t>(0);
    const int16_t stageMixStemsUs =
        stageMixStemsEndTick > 0
            ? us(stageMixStemsEndTick - stagePreReadEndTick)
            : static_cast<int16_t>(0);
    const int16_t stageMixOutputUs =
        stageMixOutputEndTick > 0
            ? us(stageMixOutputEndTick - stageMixOutputStartTick)
            : static_cast<int16_t>(0);
    const int16_t stageRampTransitionUs =
        stageRampTransitionEndTick > 0
            ? us(stageRampTransitionEndTick - stageRampTransitionStartTick)
            : static_cast<int16_t>(0);

    diagnostics.pushEvent(
        {wallMs, blockMs,
         budgetMs, 0.0,
         diagnosticStemCount, streamingMisses, blockMs > budgetMs,
         diagnosticWasPlaying, streamingMissingFrames,
         static_cast<int64_t>(juce::jmin<uint64_t>(
             timeProcessorResetMisses,
             static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))),
         static_cast<int64_t>(juce::jmin<uint64_t>(
             timeProcessorServiceFailures,
             static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))),
         stagePreReadUs, stageReadNextUs, stageDecodeRenderUs,
         stageMixStemsUs, stageMixOutputUs, stageRampTransitionUs,
         sourceGeneration});

    // ---- Benchmark: store this block's end tick for the next gap check --
    diagnostics.setPreviousBlockEndTick(blockEndTick);
  }
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

void MTControlsAudioProcessor::timerCallback() {
  playworship::telemetry::flushAudioIncidents();
  const double diagnosticNow = juce::Time::getMillisecondCounterHiRes();
  if (diagnosticNow - lastDiagnosticDeviceSampleMs >= 1000.0) {
    lastDiagnosticDeviceSampleMs = diagnosticNow;
    auto* manager = &standaloneAudioDeviceBridge.getAudioDeviceManager();
#if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
      manager = &holder->deviceManager;
#endif
    AudioDiagnostics::DeviceEvent event;
    event.wallMs = static_cast<double>(juce::Time::currentTimeMillis());
    event.monotonicMs = diagnosticNow;
    event.engineGeneration = diagnosticEngineGeneration.load(std::memory_order_relaxed);
    event.sourceGeneration = diagnosticSourceGeneration.load(std::memory_order_relaxed);
    event.songPositionSec = getSongCurrentPositionSeconds();
    event.bpm = getPlaybackTempoBpm();
    event.pitch = pitchTargetSemiTones.load(std::memory_order_relaxed);
    event.playing = isSongPlaying();
    event.clickDynMuted = isMetronomeMuted();
    event.clickDynSolo = isMetronomeSolo();
    if (auto* device = manager->getCurrentAudioDevice(); device != nullptr && device->isOpen()) {
      event.sampleRate = device->getCurrentSampleRate();
      event.bufferFrames = device->getCurrentBufferSizeSamples();
      event.outputLatencyFrames = device->getOutputLatencyInSamples();
      // Query the device, not AudioDeviceManager's combined native/DSP count.
      event.nativeXruns = device->getXRunCount();
    }
    diagnostics.pushDeviceEvent(event);
  }
  int command = 0;
  while (dequeueMidiControlCommand(command))
    executeMidiControlCommand(command);

  if (midiPadsPackLoadRequested.exchange(false,
                                         std::memory_order_acq_rel))
    startPadsPreloadAsync();

  if (midiPendingPadLoadMask.load(std::memory_order_acquire) != 0 ||
      padsFullLoadRequested.load(std::memory_order_acquire) ||
      pendingPadNote.load(std::memory_order_acquire) >= 0)
    startPadsPreloadAsync();
}

uint64_t MTControlsAudioProcessor::markAudioArtifact() {
  const auto markerId =
      diagnosticMarkerGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
  AudioDiagnostics::MarkerEvent event;
  event.wallMs = static_cast<double>(juce::Time::currentTimeMillis());
  event.monotonicMs = juce::Time::getMillisecondCounterHiRes();
  event.songPositionSec = getSongCurrentPositionSeconds();
  event.engineGeneration =
      diagnosticEngineGeneration.load(std::memory_order_relaxed);
  event.sourceGeneration =
      diagnosticSourceGeneration.load(std::memory_order_relaxed);
  event.markerId = markerId;

  auto* manager = &standaloneAudioDeviceBridge.getAudioDeviceManager();
#if JucePlugin_Build_Standalone
  if (auto* holder = juce::StandalonePluginHolder::getInstance())
    manager = &holder->deviceManager;
#endif
  if (auto* device = manager->getCurrentAudioDevice();
      device != nullptr && device->isOpen()) {
    event.nativeXruns = device->getXRunCount();
    event.bufferFrames = device->getCurrentBufferSizeSamples();
    event.outputLatencyFrames = device->getOutputLatencyInSamples();
  }
  diagnostics.pushMarkerEvent(event);
  return markerId;
}

bool MTControlsAudioProcessor::enqueueMidiControlCommand(
    const int command) noexcept {
  int startIndex1 = 0;
  int blockSize1 = 0;
  int startIndex2 = 0;
  int blockSize2 = 0;
  midiControlCommandFifo.prepareToWrite(
      1, startIndex1, blockSize1, startIndex2, blockSize2);
  juce::ignoreUnused(startIndex2, blockSize2);
  if (blockSize1 <= 0) {
    midiControlCommandDropCount.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  midiControlCommandBuffer[static_cast<size_t>(startIndex1)] = command;
  midiControlCommandFifo.finishedWrite(1);
  return true;
}

bool MTControlsAudioProcessor::dequeueMidiControlCommand(
    int &command) noexcept {
  int startIndex1 = 0;
  int blockSize1 = 0;
  int startIndex2 = 0;
  int blockSize2 = 0;
  midiControlCommandFifo.prepareToRead(
      1, startIndex1, blockSize1, startIndex2, blockSize2);
  juce::ignoreUnused(startIndex2, blockSize2);
  if (blockSize1 <= 0)
    return false;

  command =
      midiControlCommandBuffer[static_cast<size_t>(startIndex1)];
  midiControlCommandFifo.finishedRead(1);
  return true;
}

void MTControlsAudioProcessor::executeMidiControlCommand(
    const int command) {
  switch (command) {
  case midiUiCommandTransportPlayPause:
    toggleSongPlayPause();
    break;
  case midiUiCommandTransportStop:
    stopSong();
    break;
  case midiUiCommandTransportSeekBack:
    seekSongRelative(-5.0);
    break;
  case midiUiCommandTransportSeekForward:
    seekSongRelative(5.0);
    break;
  case midiUiCommandTempoDown:
    setPlaybackTempoBpm(getPlaybackTempoBpm() - 1.0);
    break;
  case midiUiCommandTempoUp:
    setPlaybackTempoBpm(getPlaybackTempoBpm() + 1.0);
    break;
  case midiUiCommandPadsEnable:
    setPadsEnabled(true);
    break;
  case midiUiCommandPadsDisable:
    setPadsEnabled(false);
    break;
  case midiUiCommandPadsToggle:
    setPadsEnabled(!isPadsEnabled());
    break;
  case midiUiCommandEnsurePadsPack:
    midiPadsPackLoadRequested.store(true, std::memory_order_release);
    break;
  default:
    break;
  }
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
  const auto queueControlCommand = [this](const int commandId) {
    enqueueMidiControlCommand(commandId);
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
    const auto bit = static_cast<uint16_t>(1U << noteIndex);
    if (mode == MidiBindingMode::Momentary) {
      if (high) {
        midiPendingPadTriggerMask.fetch_or(bit, std::memory_order_release);
      } else {
        midiPendingPadTriggerMask.fetch_and(
            static_cast<uint16_t>(~bit), std::memory_order_acq_rel);
        midiPendingPadStopMask.fetch_or(bit, std::memory_order_release);
      }
    } else if (high) {
      midiPendingPadTriggerMask.fetch_or(bit, std::memory_order_release);
    }
    if (!padsPackLoaded.load(std::memory_order_relaxed))
      midiPadsPackLoadRequested.store(true, std::memory_order_release);
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
      queueControlCommand(high ? midiUiCommandPadsEnable
                               : midiUiCommandPadsDisable);
    else if (targetId >= midiTargetStemMuteBase &&
             targetId < midiTargetStemMuteBase + maxStemCount)
      setStemMute(targetId - midiTargetStemMuteBase, high);
    else if (targetId >= midiTargetStemSoloBase &&
             targetId < midiTargetStemSoloBase + maxStemCount)
      setStemSolo(targetId - midiTargetStemSoloBase, high);
    else if (targetId == midiTargetTransportStop && high)
      queueControlCommand(midiUiCommandTransportStop);
    else if (targetId == midiTargetTransportSeekBack && high)
      queueControlCommand(midiUiCommandTransportSeekBack);
    else if (targetId == midiTargetTransportSeekForward && high)
      queueControlCommand(midiUiCommandTransportSeekForward);
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
      queueControlCommand(midiUiCommandTempoDown);
    else if (targetId == midiTargetTempoUp && high)
      queueControlCommand(midiUiCommandTempoUp);
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
    queueControlCommand(midiUiCommandTransportPlayPause);
  else if (targetId == midiTargetTransportStop)
    queueControlCommand(midiUiCommandTransportStop);
  else if (targetId == midiTargetTransportSeekBack)
    queueControlCommand(midiUiCommandTransportSeekBack);
  else if (targetId == midiTargetTransportSeekForward)
    queueControlCommand(midiUiCommandTransportSeekForward);
  else if (targetId == midiTargetTransportFadeToggle) {
    setFadeStopsPlayback(false);
    setFadeActive(!isFadeActive());
  } else if (targetId == midiTargetPadsPower) {
    queueControlCommand(midiUiCommandPadsToggle);
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
    queueControlCommand(midiUiCommandTempoDown);
  } else if (targetId == midiTargetTempoUp) {
    queueControlCommand(midiUiCommandTempoUp);
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
    const auto midiTick = juce::Time::getHighResolutionTicks();
    const auto timestampMs =
        midiClockTicksPerSecond > 0.0
            ? midiClockBaseWallMs +
                  static_cast<juce::int64>(
                      static_cast<double>(midiTick - midiClockBaseTick) *
                      1000.0 / midiClockTicksPerSecond)
            : midiClockBaseWallMs;
    midiLastMessageMs.store(timestampMs, std::memory_order_relaxed);
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
    float &left, float &right) noexcept {
  left = metronomeMeterLevelLeft.exchange(0.0f, std::memory_order_relaxed);
  right = metronomeMeterLevelRight.exchange(0.0f, std::memory_order_relaxed);
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
    const juce::StringArray &stemPaths, const juce::String &songId,
    const juce::String &songTitle, const juce::String &songKey,
    const double songBpm,
    const std::function<void(double)> &onLoadProgress,
    const bool deferTransitionFadeInUntilConfigured) {
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

  // ---- Benchmark: stem-to-worker assignment strategy -----------------
  // The legacy round-robin (`newTracks.size() % threadCount`) ignores
  // the per-stem decode cost: a 175 kbps stem and a 8 kbps stem were
  // placed in the same worker with no regard for the asymmetry.
  // Benchmarks showed the resulting decode_gap_max clustering around
  // 295ms and 570ms (a bimodal pattern) — some workers spent too long
  // on heavy stems while lighter siblings waited.
  //
  // kUseBinPackingForStemWorkers enables a greedy LPT (Longest
  // Processing Time) bin-packing: each stem is assigned to the worker
  // with the lowest current accumulated weight, using the file size
  // (a strong proxy for compressed-format decode cost) as weight.
  // Stable at load time; no per-callback overhead.
  static constexpr bool kUseBinPackingForStemWorkers = true;

  // Reset the per-worker weight accumulator. We keep a small parallel
  // array on the stack — the load path is not real-time so the
  // allocation is free.
  std::vector<juce::int64> workerLoad;
  if (kUseBinPackingForStemWorkers) {
    workerLoad.assign(stemReadThreads.size(), 0);
  }

  double maxLengthSeconds = 0.0;
  for (int i = 0; i < pathCount; ++i) {
    if (newTracks.size() >= static_cast<size_t>(maxStemCount))
      break;

    if (onLoadProgress)
      onLoadProgress(0.82 * static_cast<double>(i) /
                     static_cast<double>(denom));

    const juce::File file(stemPaths[i]);
    if (!file.existsAsFile())
      continue;

    // ---- StemCache lookup ---------------------------------------------
    // The benchmark showed AAC decode takes 1.2-2.9ms per stem per
    // block — more than the 11.6ms block budget allows for 19 stems.
    // If a valid lossless FLAC cache exists for this source, prefer
    // it: FLAC decodes 5-10x faster on every platform JUCE supports.
    // On a miss, we use the original source (degraded but functional)
    // and enqueue a background precompute so the next load is fast.
    //
    // Cache key is the immutable songId (UUID), not the mutable
    // songTitle. See perf/analysis_20260912/INFORME.md §4.1.
    juce::File playbackFile = file;
    const bool sourceIsFlac = file.hasFileExtension("flac");
    {
      const int64_t lookupStart = juce::Time::getHighResolutionTicks();
      const bool hit = !sourceIsFlac && StemCache::isValid(file, songId);
      const int64_t lookupUs = static_cast<int64_t>(
          (juce::Time::getHighResolutionTicks() - lookupStart) * 1000000.0 /
          juce::Time::getHighResolutionTicksPerSecond());
      SongLoadProbe::instance().recordStemStage(
          i, "cache.lookup", lookupUs,
          juce::String("hit=") + (hit ? "1" : "0") +
              " sourceFlac=" + (sourceIsFlac ? "1" : "0") +
              " title=" + songTitle + " songId=" + songId);
      if (hit) {
        playbackFile = StemCache::getCacheFile(file, songId);
      } else if (!sourceIsFlac) {
        // Fire-and-forget compatibility path for non-canonical callers.  A
        // .pwproj already supplies FLAC and must not be duplicated into a
        // second FLAC cache.
        StemCache::precomputeAsync(file, songId, nullptr);
      }
    }

    // SongLoadProbe: createReaderFor is a per-stem cost driver, especially for
    // non-canonical compressed sources. Time each pass separately so we can
    // confirm the suspicion that we open the same FLAC twice per stem
    // (main reader + prefetch reader). If createReader.main and
    // createReader.prefetch sum to the previous "createReader" row we know
    // the duplicate is real; the cost reduction from sharing one reader
    // is then trivial to estimate.
    const int64_t createReaderMainStart = juce::Time::getHighResolutionTicks();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        playbackFormatManager.createReaderFor(playbackFile));
    const int64_t createReaderMainUs = static_cast<int64_t>(
        (juce::Time::getHighResolutionTicks() - createReaderMainStart) * 1000000.0 /
        juce::Time::getHighResolutionTicksPerSecond());
    if (reader == nullptr)
      continue;
    const int64_t createReaderPrefetchStart =
        juce::Time::getHighResolutionTicks();
    auto prefetchReader = std::unique_ptr<juce::AudioFormatReader>(
        playbackFormatManager.createReaderFor(playbackFile));
    const int64_t createReaderPrefetchUs = static_cast<int64_t>(
        (juce::Time::getHighResolutionTicks() - createReaderPrefetchStart) *
        1000000.0 / juce::Time::getHighResolutionTicksPerSecond());
    if (prefetchReader == nullptr)
      return false;
    SongLoadProbe::instance().recordDurationUs(
        "loadSongStems",
        "stem#" + juce::String(i) + ".createReader.main",
        createReaderMainUs,
        "ext=" + playbackFile.getFileExtension() +
            " size=" + juce::String(playbackFile.getSize()));
    SongLoadProbe::instance().recordDurationUs(
        "loadSongStems",
        "stem#" + juce::String(i) + ".createReader.prefetch",
        createReaderPrefetchUs,
        "ext=" + playbackFile.getFileExtension() +
            " size=" + juce::String(playbackFile.getSize()));

    const double sampleRate = reader->sampleRate;
    const auto lengthInSamples = reader->lengthInSamples;
    const double lengthSeconds =
        sampleRate > 0.0 ? static_cast<double>(lengthInSamples) / sampleRate
                         : 0.0;
    maxLengthSeconds = juce::jmax(maxLengthSeconds, lengthSeconds);

    // Weight = file size in bytes. For compressed formats (AAC) this
    // tracks decoder cost directly (bigger encoded stream = more work
    // per sample of output). For uncompressed PCM it is the raw I/O
    // cost, which is the same dimension. A single scalar is enough
    // for the relative ordering the LPT heuristic needs.
    const juce::int64 stemWeight = file.getSize();

    size_t workerIndex;
    if (kUseBinPackingForStemWorkers && !workerLoad.empty()) {
      workerIndex = 0;
      for (size_t w = 1; w < workerLoad.size(); ++w) {
        if (workerLoad[w] < workerLoad[workerIndex])
          workerIndex = w;
      }
      workerLoad[workerIndex] += stemWeight;
    } else {
      workerIndex =
          newTracks.size() % std::max<size_t>(1, stemReadThreads.size());
    }

    auto track = std::make_unique<StemTrack>(
        std::move(reader), *stemReadThreads[workerIndex], sampleRate, 262144,
        true, std::move(prefetchReader));

    // SongLoadProbe: prepareToPlay chains down into ResamplingPositionableSource
    // + StemBufferingSource and may initialize a decoder; time it.
    const int64_t prepareStart = juce::Time::getHighResolutionTicks();
    track->prepareToPlay(localBlockSize, localSampleRate);
    const int64_t prepareUs = static_cast<int64_t>(
        (juce::Time::getHighResolutionTicks() - prepareStart) * 1000000.0 /
        juce::Time::getHighResolutionTicksPerSecond());
    SongLoadProbe::instance().recordDurationUs(
        "loadSongStems", "stem#" + juce::String(i) + ".prepareToPlay",
        prepareUs,
        "blockSize=" + juce::String(localBlockSize));
    newTracks.push_back(std::move(track));

    if (onLoadProgress)
      onLoadProgress(0.82 * static_cast<double>(i + 1) /
                     static_cast<double>(denom));
  }

  if (newTracks.empty())
    return false;

  // prepareToPlay pre-fills each read-ahead buffer. Verify the first callback
  // can be served before touching the currently playing song.
  // SongLoadProbe: this is the read-ahead warm-up. On cold cache it can
  // take up to 250ms per stem (the timeout). Capture per-stem and total.
  const int64_t waitStart = juce::Time::getHighResolutionTicks();
  for (size_t trackIdx = 0; trackIdx < newTracks.size(); ++trackIdx) {
    const int64_t t0 = juce::Time::getHighResolutionTicks();
    const bool ready =
        newTracks[trackIdx]->waitForNextAudioBlockReady(localBlockSize, 250);
    const int64_t dt = static_cast<int64_t>(
        (juce::Time::getHighResolutionTicks() - t0) * 1000000.0 /
        juce::Time::getHighResolutionTicksPerSecond());
    SongLoadProbe::instance().recordDurationUs(
        "loadSongStems",
        "stem#" + juce::String(trackIdx) + ".waitForReady",
        dt, juce::String("ready=") + (ready ? "1" : "0"));
    if (!ready)
      return false;
  }
  const int64_t waitTotalUs = static_cast<int64_t>(
      (juce::Time::getHighResolutionTicks() - waitStart) * 1000000.0 /
      juce::Time::getHighResolutionTicksPerSecond());
  SongLoadProbe::instance().recordDurationUs(
      "loadSongStems", "waitForReady.total", waitTotalUs,
      "stems=" + juce::String(newTracks.size()));

  if (onLoadProgress) {
    const int64_t progressStart = juce::Time::getHighResolutionTicks();
    onLoadProgress(0.86);
    const int64_t progressUs = static_cast<int64_t>(
        (juce::Time::getHighResolutionTicks() - progressStart) * 1000000.0 /
        juce::Time::getHighResolutionTicksPerSecond());
    SongLoadProbe::instance().recordDurationUs(
        "loadSongStems", "progress.0.86", progressUs, "");
  }

  const bool shouldResumeAfterLoad = wasPlayingBeforeLoad;
  const bool shouldFadeOutCurrentSong =
      shouldResumeAfterLoad && isSongPlaying() &&
      songLoaded.load(std::memory_order_relaxed) && !stemTracks.empty();

  if (shouldFadeOutCurrentSong) {
    songLoadTransitionState.store(
        static_cast<int>(SongLoadTransitionState::fadingOut),
        std::memory_order_release);

    // SongLoadProbe: time the fade-out wait. Up to 1500ms when the previous
    // song was playing — this is one of the dominant UI freezes when a song
    // is in the middle of playback.
    const int64_t fadeStart = juce::Time::getHighResolutionTicks();
    constexpr int maxFadeWaitMs = 1500;
    for (int wait = 0; wait < maxFadeWaitMs; ++wait) {
      const auto state = static_cast<SongLoadTransitionState>(
          songLoadTransitionState.load(std::memory_order_acquire));
      if (state == SongLoadTransitionState::readyToSwap)
        break;
      if (onLoadProgress && wait > 0 && (wait % 25) == 0) {
        onLoadProgress(0.86 +
                       (0.03 * static_cast<double>(wait) / maxFadeWaitMs));
      }
      juce::Thread::sleep(1);
    }
    const int64_t fadeUs = static_cast<int64_t>(
        (juce::Time::getHighResolutionTicks() - fadeStart) * 1000000.0 /
        juce::Time::getHighResolutionTicksPerSecond());
    SongLoadProbe::instance().recordDurationUs(
        "loadSongStems", "fadeOut.wait", fadeUs,
        juce::String("capped=") + (fadeUs >= maxFadeWaitMs * 1000 ? "1" : "0"));

    if (static_cast<SongLoadTransitionState>(
            songLoadTransitionState.load(std::memory_order_acquire)) !=
        SongLoadTransitionState::readyToSwap) {
      // Keep the old deck alive and recover its level if the audio device did
      // not service the fade in time.
      songLoadTransitionState.store(
          static_cast<int>(SongLoadTransitionState::fadingIn),
          std::memory_order_release);
      return false;
    }
  } else if (shouldResumeAfterLoad) {
    // Playback may have ended while the next deck was being prepared. Start
    // the replacement from silence without manufacturing an outgoing deck.
    songLoadTransitionGain.store(0.0f, std::memory_order_relaxed);
    songLoadTransitionState.store(
        static_cast<int>(SongLoadTransitionState::readyToSwap),
        std::memory_order_release);
  } else {
    songLoadTransitionGain.store(1.0f, std::memory_order_relaxed);
    songLoadTransitionState.store(
        static_cast<int>(SongLoadTransitionState::idle),
        std::memory_order_release);
  }

  // The callback is suspended only after the output envelope has reached
  // zero, so an empty host buffer cannot create a waveform discontinuity.
  // SongLoadProbe: this is the audio-thread drain wait. Up to 2000ms — when
  // the host is mid-block this can dominate the visible swap.
  const int64_t suspendStart = juce::Time::getHighResolutionTicks();
  suspendProcessing(true);
  SongLoadProbe::instance().recordDurationUs(
      "loadSongStems", "suspendProcessing.take", static_cast<int64_t>(
          (juce::Time::getHighResolutionTicks() - suspendStart) * 1000000.0 /
          juce::Time::getHighResolutionTicksPerSecond()),
      "");
  int suspendWaits = 0;
  for (int wait = 0;
       wait < 2000 && audioCallbackActive.load(std::memory_order_acquire);
       ++wait) {
    juce::Thread::sleep(1);
    ++suspendWaits;
  }
  const int64_t suspendUs = static_cast<int64_t>(
      (juce::Time::getHighResolutionTicks() - suspendStart) * 1000000.0 /
      juce::Time::getHighResolutionTicksPerSecond());
  SongLoadProbe::instance().recordDurationUs(
      "loadSongStems", "suspendProcessing.wait", suspendUs,
      "capped=" + juce::String(suspendWaits >= 2000 ? 1 : 0) +
          " waitedMs=" + juce::String(suspendWaits));
  if (audioCallbackActive.load(std::memory_order_acquire)) {
    suspendProcessing(false);
    if (shouldResumeAfterLoad) {
      songLoadTransitionState.store(
          static_cast<int>(SongLoadTransitionState::fadingIn),
          std::memory_order_release);
    }
    return false;
  }
  if (onLoadProgress) {
    const int64_t progressStart = juce::Time::getHighResolutionTicks();
    onLoadProgress(0.90);
    const int64_t progressUs = static_cast<int64_t>(
        (juce::Time::getHighResolutionTicks() - progressStart) * 1000000.0 /
        juce::Time::getHighResolutionTicksPerSecond());
    SongLoadProbe::instance().recordDurationUs(
        "loadSongStems", "progress.0.90", progressUs, "");
  }
  // SongLoadProbe: the swap region between waitForReady and Bungee prep
  // historically carries an "other_in_lss" of 400-600ms in warm swaps that
  // is not accounted for by per-stem, fadeOut, suspend, wait or bungee
  // sub-stages. Splitting it here into two pieces (pauseMaintenance +
  // lockRegion) so we can pinpoint the source: removeAllStemSources
  // (which destructs the previous StemTracks, and through that calls
  // TimeSliceThread::removeTimeSliceClient — known to block until the
  // current useTimeSlice completes) is the prime suspect.
  const int64_t pauseMaintenanceStart = juce::Time::getHighResolutionTicks();
  pauseTimeProcessorMaintenance();
  const int64_t pauseMaintenanceUs = static_cast<int64_t>(
      (juce::Time::getHighResolutionTicks() - pauseMaintenanceStart) *
      1000000.0 / juce::Time::getHighResolutionTicksPerSecond());
  SongLoadProbe::instance().recordDurationUs(
      "loadSongStems", "pauseMaintenance", pauseMaintenanceUs, "");

  const int newStemCount = static_cast<int>(newTracks.size());
  const int64_t lockRegionStart = juce::Time::getHighResolutionTicks();
  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    removeAllStemSources();
    clearLoadedSongState();

    stemTracks = std::move(newTracks);
    resetStemControls(static_cast<int>(stemTracks.size()));

    globalSamplePosition.store(0, std::memory_order_relaxed);
    transportClock.reset(0);
    deferredArrangementTransitionPending = false;
    deferredArrangementTransitionPartIndex = -1;
    deferredArrangementTransitionFrame = 0;
    deferredArrangementTransitionSourceFrame = 0;
    songCurrentPositionSeconds.store(0.0, std::memory_order_relaxed);
    masterVolume.store(1.0f, std::memory_order_relaxed);
    masterGain.store(1.0f, std::memory_order_relaxed);
    masterMuted.store(false, std::memory_order_relaxed);
    requestedSeekPosition.store(-1, std::memory_order_relaxed);

    loadedSongLengthSeconds = maxLengthSeconds;
    songStartSeconds.store(0.0, std::memory_order_relaxed);
    songEndSeconds.store(maxLengthSeconds, std::memory_order_relaxed);
    songEndCustomized.store(false, std::memory_order_relaxed);
    songFadeOutSeconds.store(juce::jmin(1.0, maxLengthSeconds),
                             std::memory_order_relaxed);
    const double normalizedSongBpm =
        songBpm > 0.0 ? songBpm : 120.0;
    loadedSongBpm =
        juce::jlimit(kMinPlaybackTempoBpm, kMaxPlaybackTempoBpm,
                     normalizedSongBpm);
    playbackTempoBpm.store(loadedSongBpm.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
    playbackTempoRatioActive = 1.0;
    tempoTransitionStartRatio = 1.0;
    tempoTransitionTargetRatio = 1.0;
    tempoTransitionElapsedFrames = 0;
    tempoTransitionTotalFrames = 0;
    setMetronomeBpm(playbackTempoBpm.load(std::memory_order_relaxed));
    {
      const juce::ScopedWriteLock metaLock(songMetaLock);
      loadedSongKey = songKey;
      loadedSongTitle = songTitle;
    }
    diagnosticSourceGeneration.fetch_add(1, std::memory_order_relaxed);
    songLoaded.store(true, std::memory_order_relaxed);
    {
      ArrangementStructurePublicationGuard publication(
          arrangementStructurePublicationSequence);
      arrangementParts.clear();
      ArrangementRuntimePart initialPart;
      initialPart.id = "part-1";
      initialPart.name = "Part";
      initialPart.sectionKey =
          DynamicGuideEngine::normalizeSectionKey(initialPart.name);
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
      arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
      arrangementCustomActive.store(false, std::memory_order_relaxed);
      arrangementTotalLengthSeconds.store(maxLengthSeconds,
                                          std::memory_order_relaxed);
    }
    dynamicGuideResetGeneration.fetch_add(1, std::memory_order_release);
    pitchTargetSemiTones.store(0, std::memory_order_relaxed);
    fadeActive.store(false, std::memory_order_relaxed);
    fadeTarget.store(1.0f, std::memory_order_relaxed);
    fadeGain = 1.0f;
    setTransportState(shouldResumeAfterLoad ? TransportState::paused
                                            : TransportState::stopped);
    songLoadTransitionGain.store(shouldResumeAfterLoad ? 0.0f : 1.0f,
                                 std::memory_order_relaxed);
    songLoadTransitionState.store(
        static_cast<int>(shouldResumeAfterLoad
                             ? SongLoadTransitionState::holdingAtZero
                             : SongLoadTransitionState::idle),
        std::memory_order_release);
  }
  const int64_t lockRegionUs = static_cast<int64_t>(
      (juce::Time::getHighResolutionTicks() - lockRegionStart) * 1000000.0 /
      juce::Time::getHighResolutionTicksPerSecond());
  SongLoadProbe::instance().recordDurationUs(
      "loadSongStems", "swap.lockRegion", lockRegionUs,
      "stems=" + juce::String(newStemCount));
  if (onLoadProgress) {
    const int64_t progressStart = juce::Time::getHighResolutionTicks();
    onLoadProgress(0.98);
    const int64_t progressUs = static_cast<int64_t>(
        (juce::Time::getHighResolutionTicks() - progressStart) * 1000000.0 /
        juce::Time::getHighResolutionTicksPerSecond());
    SongLoadProbe::instance().recordDurationUs(
        "loadSongStems", "progress.0.98", progressUs, "");
  }
  // P0.4 (perf): el swap de canción no necesita prepararse para el
  // peor caso de block size del host (8192). El host todavía está
  // reportando `localBlockSize` y, si lo cambia, prepareToPlay() se
  // encargará de re-preparar. Usar el block size real baja
  // sustancialmente la alocación y el pre-warm de Bungee durante
  // el cambio de canción (cada stem construye 2 voces Bungee).
  const int routingCapacity = juce::jmax(localBlockSize, 2048);
  // SongLoadProbe: Bungee prep is O(stems) voice allocations + filter state.
  // On a 12-stem project this is real cost.
  const int64_t bungeeStart = juce::Time::getHighResolutionTicks();
  prepareRoutingTimeProcessors(
      newStemCount, localSampleRate, routingCapacity);
  resumeTimeProcessorMaintenance();
  const int64_t bungeeUs = static_cast<int64_t>(
      (juce::Time::getHighResolutionTicks() - bungeeStart) * 1000000.0 /
      juce::Time::getHighResolutionTicksPerSecond());
  SongLoadProbe::instance().recordDurationUs(
      "loadSongStems", "bungee.prepare", bungeeUs,
      "stems=" + juce::String(newStemCount) +
          " capacity=" + juce::String(routingCapacity));
  {
    const int64_t resumeStart = juce::Time::getHighResolutionTicks();
    suspendProcessing(false);
    SongLoadProbe::instance().recordDurationUs(
        "loadSongStems", "suspendProcessing.release", static_cast<int64_t>(
            (juce::Time::getHighResolutionTicks() - resumeStart) * 1000000.0 /
            juce::Time::getHighResolutionTicksPerSecond()),
        "");
  }

  if (onLoadProgress) {
    const int64_t progressStart = juce::Time::getHighResolutionTicks();
    onLoadProgress(1.0);
    const int64_t progressUs = static_cast<int64_t>(
        (juce::Time::getHighResolutionTicks() - progressStart) * 1000000.0 /
        juce::Time::getHighResolutionTicksPerSecond());
    SongLoadProbe::instance().recordDurationUs(
        "loadSongStems", "progress.1.0", progressUs, "");
  }

  if (shouldResumeAfterLoad && !deferTransitionFadeInUntilConfigured)
    finishPendingSongTransition();

  return true;
}

void MTControlsAudioProcessor::finishPendingSongTransition() {
  const auto state = static_cast<SongLoadTransitionState>(
      songLoadTransitionState.load(std::memory_order_acquire));
  if (state != SongLoadTransitionState::holdingAtZero)
    return;

  // Start transport while held at zero. The following state change lets the
  // next callback bring the fully configured song in with a continuous ramp.
  playSong();
  songLoadTransitionState.store(
      static_cast<int>(SongLoadTransitionState::fadingIn),
      std::memory_order_release);
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
    const double sourceLength =
        loadedSongLengthSeconds.load(std::memory_order_relaxed);
    const double playbackEnd = juce::jmax(sourceLength, arrangementEnd);
    end = songEndCustomized.load(std::memory_order_relaxed) ? end : playbackEnd;
    if (end <= start)
      end = playbackEnd;
  }
  const double current =
      songCurrentPositionSeconds.load(std::memory_order_relaxed);
  if (current < start || (end > 0.0 && current >= end))
    seekSongTo(start);
  else if (customArrangement)
    seekSongTo(current);
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
  const double returnPositionSeconds = getSongStartSeconds();
  arrangementCurrentPartIndex.store(
      findArrangementPartIndexForArrangementTime(returnPositionSeconds),
      std::memory_order_relaxed);
  arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
  seekTargetSeconds.store(returnPositionSeconds, std::memory_order_relaxed);
  seekRequestGeneration.fetch_add(1, std::memory_order_release);
  globalSamplePosition.store(
      static_cast<juce::int64>(returnPositionSeconds * playbackSampleRate),
      std::memory_order_relaxed);
  songCurrentPositionSeconds.store(returnPositionSeconds,
                                   std::memory_order_relaxed);
  requestedSeekPosition.store(
      static_cast<juce::int64>(returnPositionSeconds * playbackSampleRate),
      std::memory_order_relaxed);
  arrangementTransitionIntentGeneration.fetch_add(
      1, std::memory_order_release);
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
  if (customArrangement) {
    if (!songEndCustomized.load(std::memory_order_relaxed))
      end = playbackLength;
  } else if (end <= 0.0 || end > playbackLength) {
    end = playbackLength;
  }
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
  if (customArrangement) {
    if (!songEndCustomized.load(std::memory_order_relaxed))
      end = playbackLength;
  } else if (end <= 0.0 || end > playbackLength) {
    end = playbackLength;
  }
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
  seekRequestGeneration.fetch_add(1, std::memory_order_release);

  songCurrentPositionSeconds.store(target, std::memory_order_relaxed);
  if (playbackSampleRate > 0.0) {
    const juce::int64 seekSamples =
        static_cast<juce::int64>(target * playbackSampleRate);
    globalSamplePosition.store(seekSamples, std::memory_order_relaxed);
  }
  arrangementTransitionIntentGeneration.fetch_add(
      1, std::memory_order_release);
}

void MTControlsAudioProcessor::setSongStartSeconds(double seconds) noexcept {
  const double sourceLength =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  const double length =
      arrangementCustomActive.load(std::memory_order_relaxed)
          ? juce::jmax(sourceLength, arrangementTotalLengthSeconds.load(
                                        std::memory_order_relaxed))
          : sourceLength;
  double start = juce::jlimit(0.0, length, seconds);
  double end = songEndSeconds.load(std::memory_order_relaxed);
  if (arrangementCustomActive.load(std::memory_order_relaxed) &&
      !songEndCustomized.load(std::memory_order_relaxed))
    end = length;
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
  const double sourceLength =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  const double length =
      arrangementCustomActive.load(std::memory_order_relaxed)
          ? juce::jmax(sourceLength, arrangementTotalLengthSeconds.load(
                                        std::memory_order_relaxed))
          : sourceLength;
  double start = songStartSeconds.load(std::memory_order_relaxed);
  double end = juce::jlimit(0.0, length, seconds);
  start = juce::jlimit(0.0, end, start);
  end = juce::jlimit(start, length, end);
  songStartSeconds.store(start, std::memory_order_relaxed);
  songEndSeconds.store(end, std::memory_order_relaxed);
  songEndCustomized.store(true, std::memory_order_relaxed);

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
  const double sourceLength =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  const bool customArrangement =
      arrangementCustomActive.load(std::memory_order_relaxed);
  const double length =
      customArrangement
          ? juce::jmax(sourceLength, arrangementTotalLengthSeconds.load(
                                        std::memory_order_relaxed))
          : sourceLength;
  if (customArrangement &&
      !songEndCustomized.load(std::memory_order_relaxed))
    end = length;
  if (end <= 0.0 || end > length)
    end = length;
  const double maxFade = juce::jmax(0.0, end - start);
  songFadeOutSeconds.store(juce::jlimit(0.0, maxFade, seconds),
                           std::memory_order_relaxed);
}

double MTControlsAudioProcessor::getSongStartSeconds() const noexcept {
  const double sourceLength =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  const bool customArrangement =
      arrangementCustomActive.load(std::memory_order_relaxed);
  const double length =
      customArrangement
          ? juce::jmax(sourceLength, arrangementTotalLengthSeconds.load(
                                        std::memory_order_relaxed))
          : sourceLength;
  double end = songEndSeconds.load(std::memory_order_relaxed);
  if (customArrangement &&
      !songEndCustomized.load(std::memory_order_relaxed))
    end = length;
  if (end <= 0.0 || end > length)
    end = length;
  const double start =
      juce::jlimit(0.0, end, songStartSeconds.load(std::memory_order_relaxed));
  return start;
}

double MTControlsAudioProcessor::getSongEndSeconds() const noexcept {
  const double sourceLength =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  const bool customArrangement =
      arrangementCustomActive.load(std::memory_order_relaxed);
  const double length =
      customArrangement
          ? juce::jmax(sourceLength, arrangementTotalLengthSeconds.load(
                                        std::memory_order_relaxed))
          : sourceLength;
  if (customArrangement &&
      !songEndCustomized.load(std::memory_order_relaxed))
    return length;
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
  const double previousArrangementLength =
      arrangementCustomActive.load(std::memory_order_relaxed)
          ? juce::jmax(songLength, arrangementTotalLengthSeconds.load(
                                        std::memory_order_relaxed))
          : songLength;
  const double configuredSongEnd =
      songEndSeconds.load(std::memory_order_relaxed);
  const bool songEndCustomizedBeforeUpdate =
      songEndCustomized.load(std::memory_order_relaxed);
  // A window end that is exactly the prior effective arrangement end is not
  // a real trim. Keep it following arrange edits so an inserted clip beyond
  // the source duration remains playable (including its click and guide).
  const bool songEndFollowsArrangement =
      !songEndCustomizedBeforeUpdate ||
      std::abs(configuredSongEnd - previousArrangementLength) <= 0.0001;
  const bool preserveEditingPlayback =
      arrangementEditingActiveState.load(std::memory_order_relaxed);
  const auto partIdAtIndex = [this](const int index) {
    return juce::isPositiveAndBelow(
               index, static_cast<int>(arrangementParts.size()))
               ? arrangementParts[static_cast<size_t>(index)].id
               : juce::String{};
  };
  const auto pendingPartId = partIdAtIndex(
      arrangementPendingJumpPartIndex.load(std::memory_order_relaxed));
  const auto loopPartId = partIdAtIndex(
      arrangementLoopPartIndex.load(std::memory_order_relaxed));

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
    runtimePart.utilityCue = part.utilityCue;
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

  // Detect structural changes for non-editing arrangement restoration.
  // Playback-state-only updates never reset transport, and structural changes
  // made while the editor is active are published in place below.
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

  {
    ArrangementStructurePublicationGuard publication(
        arrangementStructurePublicationSequence);
    arrangementParts = std::move(next);
    rebuildSectionScheduler();
    dynamicGuideResetGeneration.fetch_add(1, std::memory_order_release);
    arrangementTotalLengthSeconds.store(totalLength, std::memory_order_relaxed);
    arrangementCustomActive.store(isCustomArrangement,
                                  std::memory_order_relaxed);
    if (isCustomArrangement && songEndFollowsArrangement)
      songEndSeconds.store(totalLength, std::memory_order_relaxed);
    const auto findPartIndexById = [this](const juce::String &partId) {
      if (partId.isEmpty())
        return -1;
      for (int i = 0; i < static_cast<int>(arrangementParts.size()); ++i) {
        if (arrangementParts[static_cast<size_t>(i)].id == partId)
          return i;
      }
      return -1;
    };

    if (preserveEditingPlayback) {
      const double currentSec =
          songCurrentPositionSeconds.load(std::memory_order_relaxed);
      const int currentPartIndex =
          findArrangementPartIndexForArrangementTime(currentSec);
      arrangementCurrentPartIndex.store(
          currentPartIndex >= 0 ? currentPartIndex : 0,
          std::memory_order_relaxed);
      arrangementPendingJumpPartIndex.store(findPartIndexById(pendingPartId),
                                            std::memory_order_relaxed);
      arrangementLoopPartIndex.store(findPartIndexById(loopPartId),
                                     std::memory_order_relaxed);
    } else {
      arrangementCurrentPartIndex.store(0, std::memory_order_relaxed);
      arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
      arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
    }

    // Restoring sections must start at the playback window's beginning, not
    // at the first annotation (which may be minutes into the song).
    // A structural update must never stop an active transport:
    // live edits normally set preserveEditingPlayback, but keeping this guard
    // also covers an edit published during a UI mode transition. The audio
    // callback adopts the coherent scheduler snapshot and preserves the
    // current playhead.
    if (isCustomArrangement && arrangementTimelineChanged &&
        !preserveEditingPlayback &&
        getTransportState() != TransportState::playing) {
      const double target = songStartSeconds.load(std::memory_order_relaxed);
      seekTargetSeconds.store(target, std::memory_order_relaxed);
      seekRequestGeneration.fetch_add(1, std::memory_order_release);
      songCurrentPositionSeconds.store(target, std::memory_order_relaxed);
      if (playbackSampleRate > 0.0) {
        globalSamplePosition.store(
            static_cast<juce::int64>(target * playbackSampleRate),
            std::memory_order_relaxed);
      }
    }
  }
  arrangementTransitionIntentGeneration.fetch_add(
      1, std::memory_order_release);
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
        arrangementTransitionIntentGeneration.fetch_add(
            1, std::memory_order_release);
        return;
      }
      if (partIndex == currentPartIndex) {
        arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
        arrangementTransitionIntentGeneration.fetch_add(
            1, std::memory_order_release);
        return;
      }
      arrangementPendingJumpPartIndex.store(partIndex, std::memory_order_relaxed);
      arrangementTransitionIntentGeneration.fetch_add(
          1, std::memory_order_release);
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
      // Toggling the loop off must also drop any pending jump that was queued
      // to enter the loop, otherwise the audio thread will still teleport the
      // playhead to the loop section after the user already cancelled it.
      arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
      arrangementTransitionIntentGeneration.fetch_add(
          1, std::memory_order_release);
      return;
    }

    // Marking a section as loop must NOT auto-jump to it. The loop stays
    // dormant until natural playback reaches the section's range; once there
    // it wraps automatically on every section end. The previous behaviour
    // (queueing a jump when the playhead was outside) interrupted worship
    // flow by teleporting the user away from the section they were on.
    arrangementLoopPartIndex.store(partIndex, std::memory_order_relaxed);
    arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
    arrangementTransitionIntentGeneration.fetch_add(
        1, std::memory_order_release);
  }
}

void MTControlsAudioProcessor::clearArrangementPartLoop() noexcept {
  arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
  arrangementTransitionIntentGeneration.fetch_add(
      1, std::memory_order_release);
}

void MTControlsAudioProcessor::clearPendingArrangementPartJump() noexcept {
  arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
  arrangementTransitionIntentGeneration.fetch_add(
      1, std::memory_order_release);
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
  // The control-side mirror is updated immediately when a seek is requested,
  // including while the device is stopped and no audio callback is available.
  return songCurrentPositionSeconds.load(std::memory_order_relaxed);
}

double MTControlsAudioProcessor::getLoadedSongLengthSeconds() const noexcept {
  return loadedSongLengthSeconds.load(std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isSongEndCustomized() const noexcept {
  return songEndCustomized.load(std::memory_order_relaxed);
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
  const auto loadStartTicks = juce::Time::getHighResolutionTicks();
  const juce::SpinLock::ScopedLockType lock(padsStateLock);
  auto load = padsEngine.loadFromFolder(folderToLoad, playbackFormatManager);
  padsAttacksPrepared.store(false, std::memory_order_release);
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

  juce::Logger::writeToLog(
      "Pads loadFromFolder source=" + folderToLoad.getFullPathName() +
      " ok=" + juce::String(load.ok ? "true" : "false") +
      " notes=" + juce::String(load.loadedNotes) +
      " status=" + load.statusText +
      " elapsedMs=" + juce::String(elapsedMsSince(loadStartTicks), 1));
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
  const auto ensureStartTicks = juce::Time::getHighResolutionTicks();
  refreshPadsRoutingFromSettings();
  if (padsPackLoaded.load(std::memory_order_relaxed))
    return true;

  if (padsPackLoading.exchange(true, std::memory_order_acq_rel))
    return false;

  struct LoadingFlagReset {
    std::atomic<bool> &flag;
    ~LoadingFlagReset() { flag.store(false, std::memory_order_release); }
  } loadingGuard{padsPackLoading};

  {
    const juce::SpinLock::ScopedLockType lock(padsStateLock);
    padsPackStatusText = "Loading...";
  }

  {
    juce::String assetsError;
    const auto managedPadsFolder = AssetBootstrapManager::getPadsPackDirectory();
    const auto assetsStartTicks = juce::Time::getHighResolutionTicks();
    {
      const juce::SpinLock::ScopedLockType lock(padsStateLock);
      padsPackStatusText = "Descargando recursos... / Downloading resources...";
    }
    const bool assetsReady = AssetBootstrapManager::ensurePadsPackReady(&assetsError);
    juce::Logger::writeToLog(
        "Pads ensure assetsReady=" + juce::String(assetsReady ? "true" : "false") +
        " dir=" + managedPadsFolder.getFullPathName() +
        " error=" + assetsError +
        " elapsedMs=" + juce::String(elapsedMsSince(assetsStartTicks), 1));
    if (assetsReady &&
        managedPadsFolder.isDirectory() &&
        loadPadsPackInternal(managedPadsFolder, managedPadsFolder.getFullPathName(), true)) {
      juce::Logger::writeToLog(
          "Pads ensure complete source=managed elapsedMs=" +
          juce::String(elapsedMsSince(ensureStartTicks), 1));
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

  if (shouldPreferRomanImport && loadPadsPackFromFolder(romanSource)) {
    juce::Logger::writeToLog(
        "Pads ensure complete source=roman-import elapsedMs=" +
        juce::String(elapsedMsSince(ensureStartTicks), 1));
    return true;
  }
  if (reloadLastPadsPack()) {
    juce::Logger::writeToLog(
        "Pads ensure complete source=reload-last elapsedMs=" +
        juce::String(elapsedMsSince(ensureStartTicks), 1));
    return true;
  }
  if (romanSource.isDirectory() && loadPadsPackFromFolder(romanSource)) {
    juce::Logger::writeToLog(
        "Pads ensure complete source=roman-fallback elapsedMs=" +
        juce::String(elapsedMsSince(ensureStartTicks), 1));
    return true;
  }

  const auto empirikFallback = getPadsSourceFolderEmpirikFallback();
  if (empirikFallback.isDirectory() && loadPadsPackFromFolder(empirikFallback)) {
    juce::Logger::writeToLog(
        "Pads ensure complete source=empirik-fallback elapsedMs=" +
        juce::String(elapsedMsSince(ensureStartTicks), 1));
    return true;
  }

  const juce::SpinLock::ScopedLockType lock(padsStateLock);
  padsPackStatusText = "Missing files";
  juce::Logger::writeToLog(
      "Pads ensure failed elapsedMs=" +
      juce::String(elapsedMsSince(ensureStartTicks), 1));
  return false;
}

void MTControlsAudioProcessor::startPadsPreloadAsync() {
  if ((padsAttacksPrepared.load(std::memory_order_acquire) &&
       pendingPadNote.load(std::memory_order_acquire) < 0 &&
       !padsFullLoadRequested.load(std::memory_order_acquire) &&
       midiPendingPadLoadMask.load(std::memory_order_acquire) == 0) ||
      padsPreloadRunning.exchange(true, std::memory_order_acq_rel))
    return;

  if (padsPreloadThread.joinable())
    padsPreloadThread.join();

  padsPreloadShouldExit.store(false, std::memory_order_release);
  padsPreloadThread = std::thread([this] {
    if (!ensurePadsPackReady()) {
      pendingPadNote.store(-1, std::memory_order_release);
      midiPendingPadTriggerMask.store(0, std::memory_order_release);
      midiPendingPadLoadMask.store(0, std::memory_order_release);
      padsFullLoadRequested.store(false, std::memory_order_release);
      padsPreloadRunning.store(false, std::memory_order_release);
      return;
    }

    int nextAttack = padsAttacksPrepared.load(std::memory_order_acquire)
                         ? PadsEngine::kNoteCount : 0;
    while (!padsPreloadShouldExit.load(std::memory_order_acquire)) {
      const int requested = pendingPadNote.load(std::memory_order_acquire);
      if (juce::isPositiveAndBelow(requested, PadsEngine::kNoteCount)) {
        const bool ready = ensurePadsNoteReady(requested);
        const juce::SpinLock::ScopedLockType lock(padsStateLock);
        int expected = requested;
        if (pendingPadNote.compare_exchange_strong(
                expected, -1, std::memory_order_acq_rel) && ready &&
            !padsPreloadShouldExit.load(std::memory_order_acquire)) {
          padsEngine.triggerNote(requested,
              pendingPadOctave.load(std::memory_order_acquire));
          padsFullLoadRequested.store(true, std::memory_order_release);
        }
        continue;
      }

      // Complete the sounding note before continuing the bank warmup. Its
      // resident attack covers decoding without changing the voice position.
      if (padsFullLoadRequested.exchange(false, std::memory_order_acq_rel)) {
        const int active = padsEngine.getActiveNoteIndex();
        if (juce::isPositiveAndBelow(active, PadsEngine::kNoteCount))
          ensurePadsNoteReady(active, true);
        continue;
      }

      const auto midiNotes = midiPendingPadLoadMask.exchange(0, std::memory_order_acq_rel);
      if (midiNotes != 0) {
        for (int note = 0; note < PadsEngine::kNoteCount; ++note) {
          const auto bit = static_cast<uint16_t>(1U << note);
          if ((midiNotes & bit) != 0 && !ensurePadsNoteReady(note))
            midiPendingPadTriggerMask.fetch_and(static_cast<uint16_t>(~bit),
                                                std::memory_order_release);
        }
        continue;
      }

      if (nextAttack < PadsEngine::kNoteCount) {
        ensurePadsNoteReady(nextAttack++);
        if (nextAttack == PadsEngine::kNoteCount)
          padsAttacksPrepared.store(true, std::memory_order_release);
        continue;
      }
      break;
    }
    // Requests arriving during shutdown are picked up by the processor timer;
    // never recursively start/join this worker from its own thread.
    padsPreloadRunning.store(false, std::memory_order_release);
  });
}

bool MTControlsAudioProcessor::isPadsPreloadRunning() const noexcept {
  return padsPreloadRunning.load(std::memory_order_acquire) ||
         padsPackLoading.load(std::memory_order_acquire);
}

bool MTControlsAudioProcessor::ensurePadsNoteReady(const int noteIndex, const bool full) {
  if (!padsPackLoaded.load(std::memory_order_relaxed))
    return false;
  if (!juce::isPositiveAndBelow(noteIndex, PadsEngine::kNoteCount))
    return false;

  const auto noteLoadStartTicks = juce::Time::getHighResolutionTicks();
  juce::File sourceFile;
  PadsEngine::RetiredSamples retired;
  {
    const juce::SpinLock::ScopedLockType lock(padsStateLock);
    if (full ? padsEngine.isNoteFullyLoaded(noteIndex) : padsEngine.isNoteLoaded(noteIndex))
      return true;
    retired = padsEngine.releaseUnusedSamples(noteIndex);
    sourceFile = padsEngine.getNoteSourceFile(noteIndex);
    if (!full)
      padsPackStatusText = "Preparing pads...";
  }
  retired = {}; // Free large buffers outside the audio-state lock.

  PadsEngine::DecodedNote decoded;
  const auto cancelled = [this, full, noteIndex] {
    return padsPreloadShouldExit.load(std::memory_order_acquire) ||
           (full && padsEngine.getActiveNoteIndex() != noteIndex);
  };
  const bool decodedOk = PadsEngine::decodeNoteFile(
      sourceFile, playbackFormatManager, decoded, full ? 0.0 : 64.0, cancelled);
  if (cancelled())
    return false;
  bool loaded = false;
  {
    const juce::SpinLock::ScopedLockType lock(padsStateLock);
    // A voice may have completed its fade while the next MP3 was decoding.
    // Trim it again before publishing the new buffer so it does not remain
    // resident until another pad selection.
    retired = padsEngine.releaseUnusedSamples(noteIndex);
    loaded = (full ? padsEngine.isNoteFullyLoaded(noteIndex) : padsEngine.isNoteLoaded(noteIndex)) ||
             (decodedOk && padsEngine.getNoteSourceFile(noteIndex) == sourceFile &&
              padsEngine.installDecodedNote(
                               noteIndex, std::move(decoded)));
    padsPackStatusText = loaded ? "Loaded" : "Pad load failed";
  }
  juce::Logger::writeToLog(
      "Pads ensureNoteLoaded note=" + juce::String(noteIndex) +
      " mode=" + (full ? juce::String("full") : juce::String("attack")) +
      " ok=" + juce::String(loaded ? "true" : "false") +
      " elapsedMs=" + juce::String(elapsedMsSince(noteLoadStartTicks), 1));
  return loaded;
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
  if (!enabled)
    pendingPadNote.store(-1, std::memory_order_release);
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

double
MTControlsAudioProcessor::getPadsCrossfadeDurationSeconds() const noexcept {
  return padsEngine.getCrossfadeDurationSeconds();
}

void MTControlsAudioProcessor::beginPadsSongTransitionFadeOut() noexcept {
  padsEngine.beginSongTransitionFadeOut();
}

void MTControlsAudioProcessor::beginPadsSongTransitionFadeIn() noexcept {
  padsEngine.beginSongTransitionFadeIn();
}

void MTControlsAudioProcessor::triggerPadNote(const int noteIndex,
                                              const int octave) {
  const auto triggerStart = juce::Time::getHighResolutionTicks();
  if (!juce::isPositiveAndBelow(noteIndex, PadsEngine::kNoteCount))
    return;
  bool resident = false;
  if (padsPackLoaded.load(std::memory_order_acquire)) {
    const juce::SpinLock::ScopedLockType lock(padsStateLock);
    if (padsEngine.isNoteLoaded(noteIndex)) {
      pendingPadNote.store(-1, std::memory_order_release);
      padsEngine.triggerNote(noteIndex, octave);
      if (!padsEngine.isNoteFullyLoaded(noteIndex))
        padsFullLoadRequested.store(true, std::memory_order_release);
      resident = true;
    }
  }
  if (resident) {
    juce::Logger::writeToLog("Pads trigger resident note=" + juce::String(noteIndex) +
        " elapsedMs=" + juce::String(elapsedMsSince(triggerStart), 3));
    return;
  }

  pendingPadOctave.store(octave, std::memory_order_release);
  pendingPadNote.store(noteIndex, std::memory_order_release);
  startPadsPreloadAsync();
}

void MTControlsAudioProcessor::triggerPadNoteForSongTransition(
    const int noteIndex, const int octave) {
  triggerPadNote(noteIndex, octave);
}

void MTControlsAudioProcessor::stopPadNote(const int noteIndex) {
  const juce::SpinLock::ScopedLockType lock(padsStateLock);
  int pendingNote = noteIndex;
  pendingPadNote.compare_exchange_strong(pendingNote, -1,
                                         std::memory_order_acq_rel);
  padsEngine.stopNote(noteIndex);
}

int MTControlsAudioProcessor::getActivePadNoteIndex() const noexcept {
  const int pendingNote = pendingPadNote.load(std::memory_order_acquire);
  if (juce::isPositiveAndBelow(pendingNote, PadsEngine::kNoteCount))
    return pendingNote;
  return padsEngine.getActiveNoteIndex();
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
  if (padsPackLoading.load(std::memory_order_relaxed))
    return "Loading...";
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
  metronome.requestReset();
  if (enabled && getTransportState() == TransportState::playing)
    metronome.triggerImmediateClick();
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
  const double previous =
      playbackTempoBpm.exchange(normalized, std::memory_order_relaxed);
  setMetronomeBpm(normalized);
  if (!juce::approximatelyEqual(previous, normalized)) {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    rebuildSectionScheduler();
  }
}

double MTControlsAudioProcessor::getPlaybackTempoBpm() const noexcept {
  return playbackTempoBpm.load(std::memory_order_relaxed);
}

double MTControlsAudioProcessor::getOriginalSongBpm() const noexcept {
  return loadedSongBpm.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMetronomeBeatsPerBar(int beats) noexcept {
  const int normalizedBeats = juce::jmax(1, beats);
  metronome.setBeatsPerBar(normalizedBeats);
  if (guideAnnouncementBeatsPerBar.exchange(
          normalizedBeats, std::memory_order_relaxed) != normalizedBeats) {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    rebuildSectionScheduler();
  }
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
  tempoTransitionStartRatio = 1.0;
  tempoTransitionTargetRatio = 1.0;
  tempoTransitionElapsedFrames = 0;
  tempoTransitionTotalFrames = 0;
  loadedSongKey.clear();
  loadedSongTitle.clear();
  songStartSeconds.store(0.0, std::memory_order_relaxed);
  songEndSeconds.store(0.0, std::memory_order_relaxed);
  songEndCustomized.store(false, std::memory_order_relaxed);
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
  dynamicGuideOutputWidthState.store(1, std::memory_order_relaxed);
  songCurrentPositionSeconds.store(0.0, std::memory_order_relaxed);
  songLoaded.store(false, std::memory_order_relaxed);
  arrangementParts.clear();
  sectionScheduler.clear();
  transportClock.reset(0);
  deferredArrangementTransitionPending = false;
  deferredArrangementTransitionPartIndex = -1;
  deferredArrangementTransitionFrame = 0;
  deferredArrangementTransitionSourceFrame = 0;
  arrangementCustomActive.store(false, std::memory_order_relaxed);
  arrangementCurrentPartIndex.store(0, std::memory_order_relaxed);
  arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
  arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
  arrangementTotalLengthSeconds.store(0.0, std::memory_order_relaxed);
  dynamicGuideResetGeneration.fetch_add(1, std::memory_order_release);
  pitchTargetSemiTones.store(0, std::memory_order_relaxed);
  mixEngine.reset();
  metronome.reset();
  metronomeLatencyDelay.reset();
  metronomeClickStemIndex.store(-1, std::memory_order_relaxed);
  releaseRoutingTimeProcessors();
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
    dynamicGuideResetGeneration.fetch_add(1, std::memory_order_release);
    return;
  }
  // Guide assets are global for the selected app language, not song-specific.
  // Re-decoding the complete pack on every settings restore dominated the
  // measured `settings.restore` phase.  The language setter performs its own
  // reload when the language actually changes; here we only bootstrap a pack
  // that has not been loaded yet (or retry a previous failed load).
  if (dynamicGuideEngine.getLoadedSectionCount() > 0)
    return;
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

  if (editingActive)
    dynamicGuideResetGeneration.fetch_add(1, std::memory_order_release);
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
  }
}

void MTControlsAudioProcessor::setPitchSemiTones(
    const int semitoneDelta) noexcept {
  pitchTargetSemiTones.store(
      juce::jlimit(kMinPitchSemitones, kMaxPitchSemitones, semitoneDelta),
      std::memory_order_relaxed);
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
  rebuildSectionScheduler();
  dynamicGuideResetGeneration.fetch_add(1, std::memory_order_release);

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
  // Anchor musical phase directly in the source-song beat grid. Effective BPM
  // follows the audio-owned 100 ms tempo ramp, so control cannot publish a
  // mismatched target-BPM/phase pair for one callback.
  const double originalBpm =
      juce::jmax(kMinPlaybackTempoBpm,
                 loadedSongBpm.load(std::memory_order_relaxed));
  const double sourceBeatPosition =
      (juce::jmax(0.0, sourceSeconds) * originalBpm) / 60.0;
  const double effectiveBpm = juce::jlimit(
      kMinPlaybackTempoBpm, kMaxPlaybackTempoBpm,
      originalBpm * playbackTempoRatioActive);
  metronome.syncPhaseForBeatPosition(sourceBeatPosition, effectiveBpm);
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
  using SchedulerFrame = playworship::audio::SectionScheduler::Frame;
  const double sampleRate =
      playbackSampleRate > 0.0 ? playbackSampleRate : 44100.0;
  const auto arrangementEndFrame =
      playworship::audio::TransportClock::secondsToFrames(
          arrangementTotalLengthSeconds.load(std::memory_order_relaxed),
          sampleRate);
  const auto sourceEndFrame =
      playworship::audio::TransportClock::secondsToFrames(
          loadedSongLengthSeconds.load(std::memory_order_relaxed), sampleRate);
  const auto schedulerArrangementFrame =
      static_cast<SchedulerFrame>(arrangementFrame);
  SchedulerFrame sourceFrame = schedulerArrangementFrame;
  if (sectionScheduler.mapFrameToSourceFrame(schedulerArrangementFrame,
                                             arrangementEndFrame,
                                             sourceEndFrame, sourceFrame))
    return static_cast<juce::int64>(sourceFrame);
  return arrangementFrame;
}

void MTControlsAudioProcessor::rebuildSectionScheduler() noexcept {
  std::vector<playworship::audio::SectionScheduler::Section> sections;
  sections.reserve(arrangementParts.size());
  const double sampleRate =
      playbackSampleRate > 0.0 ? playbackSampleRate : 44100.0;
  const double bpm = juce::jmax(1.0, getPlaybackTempoBpm());
  const double tempoRatio = getPlaybackTempoRatio();
  const int beatsPerBar = guideAnnouncementBeatsPerBar.load(
      std::memory_order_relaxed);
  const auto oneBarFrames = playworship::audio::TransportClock::secondsToFrames(
      (60.0 / bpm) * juce::jmax(1, beatsPerBar) * tempoRatio, sampleRate);
  for (const auto &part : arrangementParts) {
    const auto startFrame =
        playworship::audio::TransportClock::secondsToFrames(part.startSec,
                                                             sampleRate);
    const bool triggerOnEntry = dynamicGuideAnnouncementTiming(
        part.sectionKey, part.name, part.utilityCue) ==
        DynamicGuideAnnouncementTiming::atSectionEntry;
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
