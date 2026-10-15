#include "AppIdentity.h"
#include "PluginProcessor.h"
#include "audio/MidiBindingTrigger.h"
#include "PluginEditor.h"
#include "SongLoadProbe.h"
#include "audio/DynamicGuideTiming.h"
#include "audio/DynamicGuideEvents.h"
#include "audio/PracticeListening.h"
#include "audio/StemCache.h"
#include "data/AppSettings.h"
#include "data/ArrangementIdentity.h"
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

int arrangementCueTriggerId(const DynamicGuideEngine &engine, const juce::String &name) {
  // Utility names have their own spoken identity. In particular Break (Pausa)
  // is not the musical-section alias Breakdown (Baja intensidad).
  int trigger = engine.resolveTriggerId(name);
  if (trigger >= 0)
    return trigger;
  const auto countKey = arrangementCountSectionKey(name);
  return engine.resolveTriggerId(countKey.isNotEmpty() ? countKey : arrangementSectionKey(name));
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
  // Welcome has not selected an authenticated or local-demo session yet.
  // Prepare existing local Pads without starting a download before login.
  startPadsPreloadAsync(false);
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
  padsMixSmoother.reset(sampleRate, 0.030);
  padsMixSmoother.setCurrentAndTargetValue(padsMuted.load() ? 0.0f : 1.0f);
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
    if (const int confirmed = getPendingArrangementPartJumpIndex(); confirmed >= 0) {
      setArrangementJumpCommand(confirmed, false); // Re-prepare after device/rate invalidates resident buffers.
      arrangementTransitionIntentGeneration.fetch_add(1, std::memory_order_release);
    }
    playbackSampleRate = sampleRate;
    playbackBlockSize = samplesPerBlock;
    const auto practiceRange = getPracticeLoopRange();
    if (!practiceRange.isEmpty() &&
        practiceRange.getLength() < getMinimumPracticeLoopDuration()) {
      clearPracticeLoop();
      practiceLoopError.store(PracticeLoopError::tooShort, std::memory_order_release);
    }
    transportClock.prepare(sampleRate);
    rebuildSectionScheduler();
    configureSectionEntryCache();
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
  if (!input.isDisabled() && input != juce::AudioChannelSet::mono() &&
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

  if (remoteClientMode.load(std::memory_order_acquire)) {
    buffer.clear();
    return;
  }

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
  audioAutomationPadsEventCount = 0;
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
    lastAnnouncedPendingJumpTarget = -1;
    lastObservedGuidePendingTarget = -1;
    lastObservedGuidePendingGeneration = 0;
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
  bool pauseAtPracticeBoundary = false;
  double stopAtEndSec = 0.0;
  uint64_t jumpCommandThisBlock = 0;
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
  playworship::audio::DynamicGuideEvents guideEvents;
  int pendingGuideLeadOffsetThisBlock = -1;
  juce::int64 guideBlockStartFrame = 0;
  bool songTimelinePlaying = false;
  const playworship::audio::SectionScheduler::Snapshot *guideTimelineSnapshot = &audioSectionSnapshot;
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
    const bool isPlaying = getTransportState() == TransportState::playing &&
                           transportClock.isPlaying();
    songTimelinePlaying = isPlaying;
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
        getPendingArrangementPartJumpIndex(),
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
          getPendingArrangementPartJumpIndex();
      audioArrangementRuntimeSnapshot.loopPartIndex =
          arrangementLoopPartIndex.load(std::memory_order_relaxed);
      audioArrangementTransitionIntentGeneration =
          currentTransitionIntentGeneration;
    }
    const bool realtimeCustomArrangement =
        audioArrangementRuntimeSnapshot.customActive;
    const double realtimeArrangementTotalLengthSeconds =
        audioArrangementRuntimeSnapshot.totalLengthSeconds;
    const auto practiceLoopCandidate = practiceLoop.snapshot();
    if (practiceLoopCandidate.valid)
      audioPracticeLoopSnapshot = practiceLoopCandidate;
    else if (audioPracticeLoopSnapshot.revision != practiceLoop.revision())
      audioPracticeLoopSnapshot = {};
    const auto &realtimePracticeLoop = audioPracticeLoopSnapshot;
    if (realtimePracticeLoop.active &&
        (!audioPracticeSectionSnapshot.valid ||
         audioPracticeSectionSnapshot.generation != audioSectionSnapshot.generation)) {
      audioPracticeSectionSnapshot = audioSectionSnapshot;
      for (int index = 0; index < audioPracticeSectionSnapshot.count; ++index)
        audioPracticeSectionSnapshot.sections[static_cast<size_t>(index)].skipPlayback = false;
      for (int index = 0; index < audioPracticeSectionSnapshot.cueCount; ++index)
        audioPracticeSectionSnapshot.cues[static_cast<size_t>(index)].skipPlayback = false;
    }
    const auto &realtimeSections = realtimePracticeLoop.active
        ? audioPracticeSectionSnapshot : audioSectionSnapshot;
    guideTimelineSnapshot = &realtimeSections;
    int realtimePendingPartIndex = realtimePracticeLoop.active
        ? -1 : audioArrangementRuntimeSnapshot.pendingJumpPartIndex;
    int realtimeLoopPartIndex = realtimePracticeLoop.active
        ? -1 : audioArrangementRuntimeSnapshot.loopPartIndex;
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

    if (!isPlaying) {
      playworship::audio::SectionScheduler::Section section;
      const int preparing = getPreparingArrangementPartJumpIndex();
      const int requested = preparing >= 0 ? preparing : getPendingArrangementPartJumpIndex();
      const auto jump = playworship::audio::SectionScheduler::getSection(realtimeSections, requested, section)
          ? mapRealtimeArrangementFrameToSourceFrame(section.startFrame) : -1;
      const auto loop = playworship::audio::SectionScheduler::getSection(realtimeSections, getArrangementPartLoopIndex(), section)
          ? mapRealtimeArrangementFrameToSourceFrame(section.startFrame) : -1;
      for (auto &track : stemTracks) track->protectSectionTargets(jump, loop);
    }
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

    // Automation is selected from the effective audio position after seek
    // acceptance, independent of the arrangement playback-mode flag.
    const auto automationCandidate = sectionAutomationTimeline.select(transportClock.getPositionSeconds());
    if (automationCandidate.valid) audioAutomationSelection = automationCandidate;
    publishSectionAutomationSelection(audioAutomationSelection);
    audioAutomationInitialPads = resolveSectionAutomationPads(audioAutomationSelection);
    audioAutomationInitialPadsToken = audioAutomationSelection.token;

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
    const int listeningState =
        practiceListeningState.load(std::memory_order_acquire);
    const int listeningMode = listeningState & 3;
    const int listeningStem = (listeningState >> 2) - 1;
    for (int i = 0; i < stemCount; ++i) {
      auto& track = stemTracks[static_cast<size_t>(i)];
      track->clearMixStateTransitions();
      const bool isClickGuideStem =
          stemClickGuideStates[static_cast<size_t>(i)].load(
              std::memory_order_relaxed);
      const bool mutedBySection =
          skipArrangementPartThisBlock ||
          (muteSectionInstrumentsThisBlock && !isClickGuideStem);
      const auto automatedMix = resolveSectionAutomationStem(i, audioAutomationSelection);
      track->setMuted(mutedBySection || automatedMix.mute);
      track->setSoloed(stemSoloStates[static_cast<size_t>(i)].load(std::memory_order_relaxed));
      track->setVolume(automatedMix.volume);
      track->setTrim(automatedMix.gain);
      track->setPracticeListeningGain(
          playworship::audio::practiceListening::gainFor(
              listeningMode, i == listeningStem, isClickGuideStem));
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
      guideBlockStartFrame = blockStartFrame;
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

      jumpCommandThisBlock = arrangementJumpCommand.load(std::memory_order_acquire);
      const int preparingIndex = (jumpCommandThisBlock & 0x10000u) == 0
          ? static_cast<int>(jumpCommandThisBlock & 0xffffu) - 1 : -1;
      playworship::audio::SectionScheduler::Section preparingSection, loopSection;
      const bool preparingValid = playworship::audio::SectionScheduler::getSection(realtimeSections, preparingIndex, preparingSection);
      const auto jumpSource = preparingValid ? mapRealtimeArrangementFrameToSourceFrame(preparingSection.startFrame)
          : (realtimePendingPartIndex >= 0 && playworship::audio::SectionScheduler::getSection(realtimeSections, realtimePendingPartIndex, preparingSection)
              ? mapRealtimeArrangementFrameToSourceFrame(preparingSection.startFrame) : -1);
      const auto loopSource = playworship::audio::SectionScheduler::getSection(realtimeSections, realtimeLoopPartIndex, loopSection)
          ? mapRealtimeArrangementFrameToSourceFrame(loopSection.startFrame) : -1;
      const int targetGuardFrames = juce::jmax(kArrangementTransitionPrefetchFrames, sourceFramesThisBlock);
      bool preparingReady = preparingValid && !controlSeekOwnsPrefetchBankThisBlock;
      for (auto &track : stemTracks) {
        track->protectSectionTargets(jumpSource, loopSource);
        preparingReady = preparingReady && track->isPrefetchReady(jumpSource, targetGuardFrames);
      }
      if (preparingValid && !controlSeekOwnsPrefetchBankThisBlock && !preparingReady) {
        for (auto &track : stemTracks) {
          if (loopSource >= 0 && !track->isPrefetchReady(loopSource, juce::jmax(1, sourceFramesThisBlock)))
            track->requestPrefetchPosition(loopSource, kArrangementTransitionPrefetchFrames);
          else
            track->requestPrefetchPosition(jumpSource, targetGuardFrames);
        }
      }
      if (preparingReady) {
        // The natural timeline may already have reached the chosen section
        // while it was Preparing. Fulfil it rather than leave a same-section
        // pending command which the scheduler cannot consume.
        const bool reachedNaturally = preparingIndex == mappedPartIndex;
        const auto confirmed = reachedNaturally ? (((jumpCommandThisBlock >> 32) + 1) << 32)
                                               : jumpCommandThisBlock | 0x10000u;
        if (arrangementJumpCommand.compare_exchange_strong(jumpCommandThisBlock, confirmed)) {
          jumpCommandThisBlock = confirmed;
          arrangementPendingJumpPartIndex.store(reachedNaturally ? -1 : preparingIndex);
          int expectedLoop = realtimeLoopPartIndex;
          arrangementLoopPartIndex.compare_exchange_strong(expectedLoop, -1);
          realtimePendingPartIndex = reachedNaturally ? -1 : preparingIndex; realtimeLoopPartIndex = -1;
          audioArrangementRuntimeSnapshot.pendingJumpPartIndex = realtimePendingPartIndex;
          audioArrangementRuntimeSnapshot.loopPartIndex = -1;
          arrangementTransitionIntentGeneration.fetch_add(1, std::memory_order_release);
          for (auto &track : stemTracks) track->protectSectionTargets(jumpSource, -1);
        }
      }
      const int loopPartIndex = realtimeLoopPartIndex;
      const int pendingPartIndex = realtimePendingPartIndex;
      auto sectionDecision =
          playworship::audio::SectionScheduler::evaluate(
              realtimeSections, transportClock.getPositionFrames(),
              sourceFramesThisBlock, customArrangement, pendingPartIndex,
              loopPartIndex);
      const auto practiceBoundary = playworship::audio::PracticeLoop::evaluate(
          realtimePracticeLoop, blockStartFrame, sourceFramesThisBlock,
          playbackSampleRate);
      bool practiceLoopBoundary = false;
      const bool practiceLoopTooShort = realtimePracticeLoop.active &&
          (realtimePracticeLoop.endSeconds - realtimePracticeLoop.startSeconds) *
              playbackSampleRate < sourceFramesThisBlock;
      if (realtimePracticeLoop.active) {
        // Section mapping still runs inside A/B, including inserted clips.
        // A skip may not escape the temporary listening region.
        const double targetSeconds =
            playworship::audio::TransportClock::framesToSeconds(
                sectionDecision.targetFrame, playbackSampleRate);
        if (sectionDecision.transition &&
            (targetSeconds < realtimePracticeLoop.startSeconds ||
             targetSeconds >= realtimePracticeLoop.endSeconds))
          sectionDecision = {};
        if (practiceBoundary.wrap &&
            (!sectionDecision.transition ||
             practiceBoundary.offsetFrames <= sectionDecision.transitionOffsetFrames)) {
          sectionDecision.transition = true;
          sectionDecision.stop = false;
          sectionDecision.skippedSection = false;
          sectionDecision.targetFrame = practiceBoundary.targetFrame;
          sectionDecision.targetSection =
              playworship::audio::SectionScheduler::findSection(
                  realtimeSections, practiceBoundary.targetFrame);
          sectionDecision.transitionOffsetFrames = practiceBoundary.offsetFrames;
          practiceLoopBoundary = true;
        }
      }
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
          [this, controlSeekOwnsPrefetchBankThisBlock, jumpSource](
              const juce::int64 sourceFrame) noexcept {
            if (controlSeekOwnsPrefetchBankThisBlock || (jumpSource >= 0 && sourceFrame != jumpSource))
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
          [this, currentTransitionIntentGeneration, &realtimePracticeLoop]() noexcept {
            return arrangementTransitionIntentGeneration.load(
                       std::memory_order_acquire) ==
                   currentTransitionIntentGeneration &&
                   (!realtimePracticeLoop.active ||
                    practiceLoop.revision() == realtimePracticeLoop.revision);
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
      if (realtimePracticeLoop.active) {
        requestArrangementTarget(mapRealtimeArrangementFrameToSourceFrame(
            playworship::audio::TransportClock::secondsToFrames(
                realtimePracticeLoop.startSeconds, playbackSampleRate)));
      }
      if (practiceLoopBoundary)
        anticipatedTargetPart = -1;
      else if (realtimePracticeLoop.active)
        anticipatedTargetPart = -1;
      else if (juce::isPositiveAndBelow(pendingPartIndex,
                                       realtimeSectionCount))
        anticipatedTargetPart = pendingPartIndex;
      else if (loopPartActive)
        anticipatedTargetPart = loopPartIndex;
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
      if (realtimePracticeLoop.prepared && !sectionDecision.transition &&
          playworship::audio::SectionScheduler::getSection(
              realtimeSections, anticipatedTargetPart, anticipatedTarget) &&
          playworship::audio::TransportClock::framesToSeconds(
              anticipatedTarget.startFrame, playbackSampleRate) - blockStartSec > 1.0)
        anticipatedTargetPart = -1;
      if (realtimePracticeLoop.prepared && anticipatedTargetPart < 0 &&
          !deferredArrangementTransitionPending && !controlSeekOwnsPrefetchBankThisBlock &&
          loopPartIndex < 0 && pendingPartIndex < 0) {
        requestArrangementTarget(mapRealtimeArrangementFrameToSourceFrame(
            playworship::audio::TransportClock::secondsToFrames(
                realtimePracticeLoop.startSeconds, playbackSampleRate)));
      }
      if (!controlSeekOwnsPrefetchBankThisBlock &&
          playworship::audio::SectionScheduler::getSection(
              realtimeSections, anticipatedTargetPart, anticipatedTarget)) {
        requestArrangementTarget(
            mapRealtimeArrangementFrameToSourceFrame(
                anticipatedTarget.startFrame));
      }

      // Existing non-jump arrangement handoffs can defer; Ir only confirms a protected resident target.
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
            [this, &guideEvents, blockStartFrame, sourceFramesThisBlock, numSamples](
                const int guideTriggerId, const int partIndex,
                const juce::int64 triggerFrame) {
          if (guideTriggerId < 0) {
            dynamicGuideMissingPartIndex.store(partIndex, std::memory_order_release);
            return -1;
          }
          const int offset = sourceFramesThisBlock > 0
              ? juce::jlimit(0, juce::jmax(0, numSamples - 1),
                  static_cast<int>(std::llround(static_cast<double>(
                      std::max<juce::int64>(0, triggerFrame - blockStartFrame)) *
                      numSamples / sourceFramesThisBlock))) : 0;
          guideEvents.add(offset, guideTriggerId);
          return offset;
        };

        const bool guideJumpCancelled = lastObservedGuidePendingTarget >= 0 && pendingPartIndex < 0;
        const bool hasPendingGuideJump = juce::isPositiveAndBelow(pendingPartIndex, realtimeSectionCount);
        if (pendingPartIndex != lastObservedGuidePendingTarget ||
            audioArrangementTransitionIntentGeneration != lastObservedGuidePendingGeneration) {
          // Cancel/change/requeue rearms the intent, without marking a
          // suppressed sequential announcement as already heard.
          if (juce::isPositiveAndBelow(lastAnnouncedPendingJumpTarget, realtimeSectionCount) &&
              lastAnnouncedPendingJumpTarget != mappedPartIndex)
            dynamicGuideAnnouncedByPart[static_cast<size_t>(lastAnnouncedPendingJumpTarget)] = false;
          lastAnnouncedPendingJumpTarget = -1;
          lastObservedGuidePendingTarget = pendingPartIndex;
          lastObservedGuidePendingGeneration = audioArrangementTransitionIntentGeneration;
        }
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
            const bool shouldTriggerNow = triggerOnEntry && !alreadyAnnounced && !hasPendingGuideJump;
            if (shouldTriggerNow)
              triggerPartAnnouncement(part.guideTriggerId, mappedPartIndex, blockStartFrame);
            if (shouldTriggerNow)
              dynamicGuideAnnouncedByPart[partIdx] = true;
          }
        }

        juce::ignoreUnused(arrangementEditingActive);
        // Inspect every announcement in the callback, preserving exact
        // offsets even when several counts/sections share a block.
        for (int guidePartIndex = 0; guidePartIndex < realtimeSectionCount; ++guidePartIndex) {
          const auto &part = realtimeSections.sections[static_cast<size_t>(guidePartIndex)];
          const bool cancellationCatchup = guideJumpCancelled &&
              part.guideTriggerFrame < blockStartFrame && part.startFrame > blockStartFrame;
          if ((!cancellationCatchup && part.guideTriggerFrame < blockStartFrame) ||
              part.guideTriggerFrame >= blockStartFrame + sourceFramesThisBlock ||
              part.skipPlayback)
            continue;
          const double partStartSec = playworship::audio::TransportClock::framesToSeconds(
              part.startFrame, playbackSampleRate);
          const bool excludedByLoop = loopPartEngaged && partStartSec >= loopEndSec;
          const bool transitionOwnsEntry = part.triggerGuideOnEntry && sectionDecision.transition &&
              guidePartIndex == sectionDecision.targetSection;
          auto &announced = dynamicGuideAnnouncedByPart[static_cast<size_t>(guidePartIndex)];
          if (!hasPendingGuideJump && !excludedByLoop && !transitionOwnsEntry && !announced) {
            triggerPartAnnouncement(part.guideTriggerId, guidePartIndex, part.guideTriggerFrame);
            announced = 1;
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
            if (blockStartFrame + sourceFramesThisBlock > leadInFireFrame) {
              playworship::audio::SectionScheduler::Section targetPart;
              if (playworship::audio::SectionScheduler::getSection(
                      realtimeSections, pendingPartIndex, targetPart)) {
                pendingGuideLeadOffsetThisBlock = triggerPartAnnouncement(targetPart.guideTriggerId,
                                        pendingPartIndex, leadInFireFrame);
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
            !hasPendingGuideJump) {
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
            if (blockStartFrame + sourceFramesThisBlock > loopLeadInFireFrame) {
              triggerPartAnnouncement(loopLeadInPart.guideTriggerId,
                                      realtimeLoopPartIndex, loopLeadInFireFrame);
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

      if (practiceLoopBoundary && controlSeekStillPending) {
        // A previous seek keeps old audio playing while its target warms. It
        // may continue inside A/B, but must never run beyond the new B.
        stopAtEnd = true;
        pauseAtPracticeBoundary = true;
        stopAtEndSec = realtimePracticeLoop.endSeconds;
        songStopOutputOffset = sourceFramesThisBlock > 0
            ? juce::jlimit(0, numSamples, static_cast<int>(std::lround(
                  static_cast<double>(practiceBoundary.offsetFrames) /
                  sourceFramesThisBlock * numSamples))) : 0;
        if (seekRequestGeneration.load(std::memory_order_acquire) == requestedSeekGeneration)
          appliedSeekRequestGeneration = requestedSeekGeneration;
        practiceLoop.clear();
        practiceLoopError.store(PracticeLoopError::bufferNotReady,
                                std::memory_order_release);
        arrangementTransitionIntentGeneration.fetch_add(1, std::memory_order_release);
      }

      if (!controlSeekOwnsPrefetchBankThisBlock &&
          !deferredArrangementTransitionPending &&
          !inBlockArrangementTransitionApplied && practiceLoopBoundary) {
        transitionToNextArrangementPart = true;
        nextArrangementPartIndex = sectionDecision.targetSection;
        nextArrangementPartStartSec = realtimePracticeLoop.startSeconds;
      } else if (!controlSeekOwnsPrefetchBankThisBlock &&
          !deferredArrangementTransitionPending &&
          !inBlockArrangementTransitionApplied && loopPartEngaged) {
        windowStartSec = loopStartSec;
        windowEndSec = loopEndSec;
        fadeOutSec = 0.0;
        arrangementCurrentPartIndex.store(loopPartIndex, std::memory_order_relaxed);

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
        } else if (!practiceLoopTooShort &&
                   arrangementTargetReady(inBlockTransitionTargetSourceFrame,
                                          targetFramesNeeded)) {
          for (auto &track : stemTracks) {
            track->scheduleReadDiscontinuity(
                inBlockTransitionPrefixFrames,
                inBlockTransitionTargetSourceFrame,
                kArrangementTransitionFadeFrames);
          }
          inBlockArrangementTransitionApplied = true;
        } else if (practiceLoopBoundary) {
          // Repeating outside B is never a valid fallback for an exact A/B
          // region. On exceptional buffering failure, retain the prefix and
          // pause at B; the UI receives an explicit recoverable error.
          transitionToNextArrangementPart = false;
          stopAtEnd = true;
          pauseAtPracticeBoundary = true;
          stopAtEndSec = realtimePracticeLoop.endSeconds;
          songStopOutputOffset = sourceFramesThisBlock > 0
              ? juce::jlimit(0, numSamples,
                    static_cast<int>(std::lround(
                        static_cast<double>(inBlockTransitionPrefixFrames) /
                        sourceFramesThisBlock * numSamples))) : 0;
          practiceLoop.clear();
          practiceLoopError.store(practiceLoopTooShort
                                      ? PracticeLoopError::tooShort
                                      : PracticeLoopError::bufferNotReady,
                                  std::memory_order_release);
          arrangementTransitionIntentGeneration.fetch_add(1, std::memory_order_release);
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
              targetSection.triggerGuideOnEntry &&
              inBlockTransitionTargetFrame == targetSection.startFrame;
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

      if (!realtimePracticeLoop.active &&
          windowEndSec > 0.0 && blockEndSec >= windowEndSec) {
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

      // Enumerate every source section edge in the actual rendered spans.
      // A long callback may cross more than one section (or an annotation
      // gap); loops/jumps contribute a prefix and a destination suffix.
      if (!isSectionAutomationEditing()) {
        uint64_t previousAutomationToken = audioAutomationSelection.token;
        const auto enqueueAutomationBoundary = [&](const int sourceOffset, const double destinationSeconds) {
          if (sourceOffset < 0 || sourceOffset >= sourceFramesThisBlock) return;
          const auto selected = sectionAutomationTimeline.select(destinationSeconds,
              audioAutomationSelection.bank, audioAutomationSelection.sequence);
          if (!selected.valid) return;
          if (previousAutomationToken != selected.token) {
            auto previousManualToken = previousAutomationToken;
            sectionAutomationManualToken.compare_exchange_strong(previousManualToken, 0, std::memory_order_acq_rel);
          }
          previousAutomationToken = selected.token;
          bool destinationSectionMuted = false, destinationSectionSkipped = false;
          if (realtimeCustomArrangement) {
            const auto destinationFrame = playworship::audio::TransportClock::secondsToFrames(destinationSeconds, playbackSampleRate);
            const int sectionIndex = playworship::audio::SectionScheduler::findSection(realtimeSections, destinationFrame);
            playworship::audio::SectionScheduler::Section section;
            if (playworship::audio::SectionScheduler::getSection(realtimeSections, sectionIndex, section) &&
                destinationFrame >= section.startFrame && destinationFrame < section.endFrame) {
              destinationSectionMuted = section.muteInstruments;
              destinationSectionSkipped = section.skipPlayback;
            }
          }
          for (int index = 0; index < stemCount; ++index) {
            const auto next = resolveSectionAutomationStem(index, selected);
            const bool clickGuide = stemClickGuideStates[static_cast<size_t>(index)].load(std::memory_order_relaxed);
            stemTracks[static_cast<size_t>(index)]->scheduleMixStateTransition(
                sourceOffset, next.volume, next.gain,
                next.mute || destinationSectionSkipped || (destinationSectionMuted && !clickGuide));
          }
          const int outputOffset = juce::jlimit(0, numSamples,
              static_cast<int>(std::lround(static_cast<double>(sourceOffset) / sourceFramesThisBlock * numSamples)));
          if (audioAutomationPadsEventCount > 0 &&
              audioAutomationPadsEvents[static_cast<size_t>(audioAutomationPadsEventCount - 1)].outputOffset == outputOffset)
            --audioAutomationPadsEventCount;
          if (audioAutomationPadsEventCount < static_cast<int>(audioAutomationPadsEvents.size()))
            audioAutomationPadsEvents[static_cast<size_t>(audioAutomationPadsEventCount++)] =
                {outputOffset, resolveSectionAutomationPads(selected), selected.token};
        };
        const auto enqueueSpan = [&](const double startSeconds, const int sourceOffset, const int frameCount) {
          std::array<double, playworship::audio::SectionAutomation::maxSections * 2> boundaries{};
          const double endSeconds = startSeconds + static_cast<double>(frameCount) / playbackSampleRate;
          const int count = sectionAutomationTimeline.collectBoundaries(audioAutomationSelection,
              startSeconds, endSeconds, boundaries.data(), static_cast<int>(boundaries.size()));
          for (int index = 0; index < count; ++index)
            enqueueAutomationBoundary(sourceOffset + static_cast<int>(std::llround(
                (boundaries[static_cast<size_t>(index)] - startSeconds) * playbackSampleRate)),
                boundaries[static_cast<size_t>(index)]);
        };
        if (inBlockArrangementTransitionApplied) {
          enqueueSpan(blockStartSec, 0, inBlockTransitionPrefixFrames);
          const double destinationSeconds = playworship::audio::TransportClock::framesToSeconds(inBlockTransitionTargetFrame, playbackSampleRate);
          enqueueAutomationBoundary(inBlockTransitionPrefixFrames, destinationSeconds);
          enqueueSpan(destinationSeconds, inBlockTransitionPrefixFrames,
                      sourceFramesThisBlock - inBlockTransitionPrefixFrames);
        } else {
          enqueueSpan(blockStartSec, 0, sourceFramesThisBlock);
        }
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
      // Play can arrive after this callback took its timeline snapshot.
      // Never emit its first beat until the same callback advances the clock.
      isMetronomeEnabled() && songTimelinePlaying &&
      getTransportState() == TransportState::playing;
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
    publishMeterPeak(remoteMetronomeMeterLeft, metronomeMeterLeft);
    publishMeterPeak(remoteMetronomeMeterRight, metronomeMeterRight);

    const int outputIndex = juce::jlimit(
        0, juce::jmax(0, numChannels - 1), outputChannelOneBased - 1);
    stemOutputRouter.routeStemToOutputChannels(metronomeRenderBuffer,
                                               outputIndex, outputWidth,
                                               buffer);
  } else {
    metronomeMeterLevelLeft.store(0.0f, std::memory_order_relaxed);
    metronomeMeterLevelRight.store(0.0f, std::memory_order_relaxed);
    remoteMetronomeMeterLeft.store(0.0f, std::memory_order_relaxed);
    remoteMetronomeMeterRight.store(0.0f, std::memory_order_relaxed);
  }

  if (!songLoadedForDiagnostics) {
    const auto automationCandidate = sectionAutomationTimeline.select(0.0);
    if (automationCandidate.valid) audioAutomationSelection = automationCandidate;
    audioAutomationInitialPads = resolveSectionAutomationPads(audioAutomationSelection);
    audioAutomationInitialPadsToken = audioAutomationSelection.token;
  }
  if (padsPackLoaded.load(std::memory_order_relaxed)) {
    padsRenderBuffer.clear();
    bool renderedPads = false;
    {
      const juce::SpinLock::ScopedTryLockType lock(padsStateLock);
      if (lock.isLocked()) {
        const auto stopMask =
            midiPendingPadStopMask.exchange(0, std::memory_order_acq_rel);
        auto triggerMask = midiPendingPadTriggerMask.exchange(0, std::memory_order_acq_rel);
        auto guardedStopMask = stopMask;
        if (isSectionAutomationEditing() && sectionAutomationRemoteOwned.load(std::memory_order_acquire)) {
          triggerMask = 0;
          guardedStopMask = 0;
        }
        for (int note = 0; note < 12; ++note) {
          const auto bit = static_cast<uint16_t>(1U << note);
          if ((guardedStopMask & bit) != 0) {
            if (getActivePadNoteIndex() == note) setPadNoteAutomationTarget(-1, getActivePadOctave());
            padsEngine.stopNote(note);
          }
        }
        for (int note = 0; note < 12; ++note) {
          const auto bit = static_cast<uint16_t>(1U << note);
          if ((triggerMask & bit) != 0) {
            setPadNoteAutomationTarget(note, 0);
            if (padsEngine.isNoteLoaded(note)) {
              padsEngine.triggerNote(note, 0);
              padsRenderedOctave.store(0, std::memory_order_relaxed);
              if (!padsEngine.isNoteFullyLoaded(note))
                padsFullLoadRequested.store(true, std::memory_order_release);
            } else {
              midiPendingPadLoadMask.fetch_or(bit, std::memory_order_release);
            }
          }
        }
        const auto reconcilePads = [this](const playworship::audio::SectionAutomation::Pads &desired, uint64_t token) {
          padsEngine.setMixGain(desired.volume * desired.gain, token != padsMixLastToken);
          padsMixLastToken = token;
          if (padsEngine.isEnabled() != desired.enabled) padsEngine.setEnabled(desired.enabled);
          const int active = padsEngine.getActiveNoteIndex();
          if (!desired.enabled || desired.note < 0) {
            if (active >= 0) padsEngine.stopNote(active);
          } else if (active != desired.note || padsRenderedOctave.load(std::memory_order_relaxed) != desired.octave) {
            const bool automaticNote = (desired.mask & playworship::audio::SectionAutomation::note) != 0;
            if (automaticNote && active < 0 && !isSongPlaying() && !isSectionAutomationEditing()) return;
            if (padsEngine.isNoteLoaded(desired.note)) {
              padsEngine.triggerNote(desired.note, desired.octave);
              padsRenderedOctave.store(desired.octave, std::memory_order_relaxed);
              if (!padsEngine.isNoteFullyLoaded(desired.note))
                padsFullLoadRequested.store(true, std::memory_order_release);
            } else {
              midiPendingPadLoadMask.fetch_or(static_cast<uint16_t>(1U << desired.note), std::memory_order_release);
            }
          }
        };
        reconcilePads(audioAutomationInitialPads, audioAutomationInitialPadsToken);
        int renderedThrough = 0;
        for (int index = 0; index < audioAutomationPadsEventCount; ++index) {
          const auto &event = audioAutomationPadsEvents[static_cast<size_t>(index)];
          if (event.outputOffset > renderedThrough) {
            juce::AudioBuffer<float> span(padsRenderBuffer.getArrayOfWritePointers(),
                padsRenderBuffer.getNumChannels(), renderedThrough, event.outputOffset - renderedThrough);
            padsEngine.process(span);
          }
          reconcilePads(event.pads, event.token);
          renderedThrough = event.outputOffset;
        }
        if (renderedThrough < numSamples) {
          juce::AudioBuffer<float> span(padsRenderBuffer.getArrayOfWritePointers(),
              padsRenderBuffer.getNumChannels(), renderedThrough, numSamples - renderedThrough);
          padsEngine.process(span);
        }
        renderedPads = true;
      }
    }
    if (renderedPads) {
      // Mute remains a whole-bus control; volume/gain live on each voice so
      // restoring a section/song mix cannot boost an outgoing pad's tail.
      const bool mutedPads = padsMuted.load(std::memory_order_relaxed);
      padsMixSmoother.setTargetValue(mutedPads ? 0.0f : 1.0f);
      if (!padsMixSmoother.isSmoothing()) {
        padsRenderBuffer.applyGain(padsMixSmoother.getCurrentValue());
      } else for (int sample = 0; sample < numSamples; ++sample) {
        const float gain = padsMixSmoother.getNextValue();
        for (int channel = 0; channel < padsRenderBuffer.getNumChannels(); ++channel)
          padsRenderBuffer.getWritePointer(channel)[sample] *= gain;
      }
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

  if (songLoadedForDiagnostics)
    refreshSectionAutomation(transportClock.getPositionSeconds());

  dynamicGuideRenderBuffer.clear();
  if (dynamicGuideAnnouncementsActive && sourceFramesThisBlock > 0) {
    const int prefixOutput = inBlockArrangementTransitionApplied
        ? inBlockTransitionOutputOffset : numSamples;
    const int prefixSource = inBlockArrangementTransitionApplied
        ? inBlockTransitionPrefixFrames : (sourceFramesThisBlock);
    if (songTimelinePlaying)
      guideEvents.addCues(*guideTimelineSnapshot, guideBlockStartFrame,
                          prefixSource, 0, prefixOutput);
    guideEvents.render(dynamicGuideEngine, dynamicGuideRenderBuffer, 0, prefixOutput);
    if (inBlockArrangementTransitionApplied) {
      // A seek/loop/jump owns the boundary. No outgoing guide or queued cue
      // may continue into the new timeline, and cues at the destination fire
      // only if that exact point is traversed by the suffix.
      const bool destinationAlreadyAnnounced = juce::isPositiveAndBelow(nextArrangementPartIndex,
          playworship::audio::SectionScheduler::maxSections) &&
          lastAnnouncedPendingJumpTarget == nextArrangementPartIndex &&
          (pendingGuideLeadOffsetThisBlock < 0 || pendingGuideLeadOffsetThisBlock < prefixOutput);
      if (destinationAlreadyAnnounced)
        dynamicGuideEngine.resetExceptTrigger(inBlockTransitionTargetGuideId);
      else
        dynamicGuideEngine.reset();
      dynamicGuideAnnouncedByPart.fill(0);
      if (destinationAlreadyAnnounced)
        dynamicGuideAnnouncedByPart[static_cast<size_t>(nextArrangementPartIndex)] = 1;
      playworship::audio::DynamicGuideEvents suffix;
      const bool clippedPendingLead = lastAnnouncedPendingJumpTarget == nextArrangementPartIndex &&
          pendingGuideLeadOffsetThisBlock >= prefixOutput;
      if ((inBlockTransitionTargetGuideOnEntry || clippedPendingLead) && !destinationAlreadyAnnounced && prefixOutput < numSamples) {
        suffix.add(prefixOutput, inBlockTransitionTargetGuideId);
        if (juce::isPositiveAndBelow(nextArrangementPartIndex,
                playworship::audio::SectionScheduler::maxSections))
          dynamicGuideAnnouncedByPart[static_cast<size_t>(nextArrangementPartIndex)] = 1;
      }
      const int suffixSource = sourceFramesThisBlock - prefixSource;
      const int suffixOutput = numSamples - prefixOutput;
      for (int i = 0; i < guideTimelineSnapshot->count; ++i) {
        const auto &part = guideTimelineSnapshot->sections[static_cast<size_t>(i)];
        const auto relative = part.guideTriggerFrame - inBlockTransitionTargetFrame;
        if (!part.skipPlayback && relative >= 0 && relative < suffixSource &&
            !dynamicGuideAnnouncedByPart[static_cast<size_t>(i)]) {
          suffix.add(prefixOutput + juce::jlimit(0, juce::jmax(0, suffixOutput - 1),
              static_cast<int>(std::llround(static_cast<double>(relative) * suffixOutput / suffixSource))),
              part.guideTriggerId);
          dynamicGuideAnnouncedByPart[static_cast<size_t>(i)] = 1;
        }
      }
      suffix.addCues(*guideTimelineSnapshot, inBlockTransitionTargetFrame,
                     suffixSource, prefixOutput, suffixOutput);
      suffix.render(dynamicGuideEngine, dynamicGuideRenderBuffer, prefixOutput, suffixOutput);
      dynamicGuideLastPartIndex = nextArrangementPartIndex;
      dynamicGuideLastBlockStartSec = playworship::audio::TransportClock::framesToSeconds(
          inBlockTransitionTargetFrame, playbackSampleRate);
      dynamicGuideWasPlaying = true;
    }
  }

  // Muted/silent guides still consume their voices on time. Keep their
  // scratch audio out of delay history so unmuting cannot recover an old cue.
  if (!dynamicGuideShouldRender)
    dynamicGuideRenderBuffer.clear();

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
    if ((jumpCommandThisBlock & 0x10000u) != 0) {
      const auto consumed = ((jumpCommandThisBlock >> 32) + 1) << 32;
      arrangementJumpCommand.compare_exchange_strong(jumpCommandThisBlock, consumed);
    }
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
    setTransportState(pauseAtPracticeBoundary ? TransportState::paused
                                             : TransportState::stopped);
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
  return playworship::appName;
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
    startPadsPreloadAsync(!blankSongLoaded.load(std::memory_order_relaxed));

  if (!isPadsPreloadRunning() &&
      padsNetworkPreloadPending.exchange(false, std::memory_order_acq_rel))
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
  if (isSectionAutomationEditing() && sectionAutomationRemoteOwned.load(std::memory_order_acquire) &&
      (command == midiUiCommandPadsEnable || command == midiUiCommandPadsDisable || command == midiUiCommandPadsToggle)) return;
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
  if (isSectionAutomationEditing() && sectionAutomationRemoteOwned.load(std::memory_order_acquire)) {
    const bool padsTarget = targetId == midiTargetPadsVolume || targetId == midiTargetPadsGain ||
        targetId == midiTargetPadsPower || (targetId >= midiTargetPadsNoteBase && targetId < midiTargetPadsNoteBase + 12);
    const auto eligibleRange = [this, targetId](int base) {
      return targetId >= base && targetId < base + maxStemCount && isStemAutomationEligible(targetId - base);
    };
    if (padsTarget || eligibleRange(midiTargetStemVolumeBase) || eligibleRange(midiTargetStemGainBase) || eligibleRange(midiTargetStemMuteBase)) return;
  }

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

      const bool continuousTarget = isContinuousMidiTargetId(binding.targetId);
      int *lastCcValue = nullptr;
      if (binding.sourceType == MidiBindingSourceType::CC &&
          binding.mode != MidiBindingMode::Absolute && !continuousTarget) {
        const int stateIndex =
            midiSourceStateIndex(binding.sourceType, channel, sourceData1);
        lastCcValue = &midiLastSourceValues[static_cast<size_t>(stateIndex)];
      }
      if (const auto value = playworship::midi::mappedValue(
              binding, continuousTarget, isNoteOn, isNoteOff, sourceValue,
              lastCcValue)) {
        applyMidiBindingValue(binding.targetId, *value, binding.mode);
      }
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
    const bool deferTransitionFadeInUntilConfigured,
    const ImportedSongData *blankSong) {
  const bool isBlank = blankSong != nullptr && isValidBlankSong(*blankSong);
  if (blankSong != nullptr && (!isBlank || !stemPaths.isEmpty()))
    return false;
  clearPracticeLoop();
  transportControlRevision.fetch_add(1, std::memory_order_release);
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
  if (stemPaths.isEmpty() && !isBlank)
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

  if (newTracks.empty() && !isBlank)
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

    blankSongLoaded.store(isBlank, std::memory_order_relaxed);
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
      arrangementCues.clear();
      if (!isBlank) {
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
      }
      rebuildSectionScheduler();
      configureSectionEntryCache();
      arrangementCurrentPartIndex.store(isBlank ? -1 : 0, std::memory_order_relaxed);
      setArrangementJumpCommand(-1, false);
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

  if (isBlank) {
    setMetronomeBeatsPerBar(timeSignatureBeatsPerBar(blankSong->timeSignature));
    setSongArrangement({}, false);
  }
  if (shouldResumeAfterLoad && !deferTransitionFadeInUntilConfigured)
    finishPendingSongTransition();

  return true;
}

bool MTControlsAudioProcessor::unloadSong() {
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
    return false;
  }

  pauseTimeProcessorMaintenance();
  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    removeAllStemSources();
    clearLoadedSongState();
    diagnosticSourceGeneration.fetch_add(1, std::memory_order_relaxed);
  }
  resumeTimeProcessorMaintenance();

  if (!wasSuspended)
    suspendProcessing(false);
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
  auto &dm = standaloneAudioDeviceBridge.getAudioDeviceManager();
  if (dm.getCurrentAudioDevice() == nullptr)
    dm.restartLastAudioDevice();
  if (!songLoaded.load(std::memory_order_relaxed))
    return;
  transportControlRevision.fetch_add(1, std::memory_order_release);
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
  // The audio callback owns grid attacks. A control-thread preview trigger
  // can arrive after the first callback or just before the next beat.
}

void MTControlsAudioProcessor::pauseSong() {
  if (!songLoaded.load(std::memory_order_relaxed))
    return;
  transportControlRevision.fetch_add(1, std::memory_order_release);
  setTransportState(TransportState::paused);
}

void MTControlsAudioProcessor::stopSong() {
  if (!songLoaded.load(std::memory_order_relaxed))
    return;
  transportControlRevision.fetch_add(1, std::memory_order_release);
  setTransportState(TransportState::stopped);
  const auto practiceRange = getPracticeLoopRange();
  const double returnPositionSeconds = practiceRange.isEmpty()
      ? getSongStartSeconds() : practiceRange.getStart();
  arrangementCurrentPartIndex.store(
      findArrangementPartIndexForArrangementTime(returnPositionSeconds),
      std::memory_order_relaxed);
  setArrangementJumpCommand(-1, false);
  seekTargetSeconds.store(returnPositionSeconds, std::memory_order_relaxed);
  seekRequestGeneration.fetch_add(1, std::memory_order_release);
  globalSamplePosition.store(
      static_cast<juce::int64>(returnPositionSeconds * playbackSampleRate),
      std::memory_order_relaxed);
  songCurrentPositionSeconds.store(returnPositionSeconds,
                                   std::memory_order_relaxed);
  refreshSectionAutomation(returnPositionSeconds);
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
  const double target = blankSongLoaded.load() && playbackLength == 0.0
      ? juce::jmax(0.0, current + deltaSeconds)
      : juce::jlimit(start, end, current + deltaSeconds);

  seekSongTo(target);
}

void MTControlsAudioProcessor::seekSongTo(const double positionSeconds) {
  if (!songLoaded.load(std::memory_order_relaxed))
    return;
  transportControlRevision.fetch_add(1, std::memory_order_release);

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
  double target = blankSongLoaded.load(std::memory_order_relaxed) && playbackLength == 0.0
      ? juce::jmax(0.0, positionSeconds) : juce::jlimit(start, end, positionSeconds);
  setArrangementJumpCommand(-1, false);
  const auto practiceRange = getPracticeLoopRange();
  const int loopPartIndex = practiceRange.isEmpty()
      ? arrangementLoopPartIndex.load(std::memory_order_relaxed) : -1;
  if (!practiceRange.isEmpty()) {
    const double lastFrame = juce::jmax(
        practiceRange.getStart(), practiceRange.getEnd() -
            1.0 / juce::jmax(1.0, playbackSampleRate));
    target = juce::jlimit(practiceRange.getStart(), lastFrame, target);
  }
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
  refreshSectionAutomation(target);
  if (playbackSampleRate > 0.0) {
    const juce::int64 seekSamples =
        static_cast<juce::int64>(target * playbackSampleRate);
    globalSamplePosition.store(seekSamples, std::memory_order_relaxed);
  }
  arrangementTransitionIntentGeneration.fetch_add(
      1, std::memory_order_release);
}

double MTControlsAudioProcessor::getMinimumPracticeLoopDuration() const noexcept {
  const double sampleRate = playbackSampleRate > 0.0 ? playbackSampleRate : 44100.0;
  // One discontinuity per callback is supported by StemTrack. Leave two
  // frames for rounding of the maximum 2x input requirement.
  return juce::jmax(playworship::audio::PracticeLoop::minimumDurationSeconds,
                   (juce::jmax(1, playbackBlockSize) * kMaxPlaybackTempoRatio +
                    2.0) / sampleRate);
}

bool MTControlsAudioProcessor::setPracticeLoopRange(double startSeconds,
                                                    double endSeconds) noexcept {
  if (!hasLoadedSong() || !std::isfinite(startSeconds) ||
      !std::isfinite(endSeconds) || startSeconds < getSongStartSeconds() ||
      endSeconds > getSongEndSeconds() || endSeconds <= startSeconds) {
    practiceLoopError.store(PracticeLoopError::invalidRange, std::memory_order_release);
    return false;
  }
  if (endSeconds - startSeconds < getMinimumPracticeLoopDuration()) {
    practiceLoopError.store(PracticeLoopError::tooShort, std::memory_order_release);
    return false;
  }
  if (isCustomArrangementActive() &&
      !playworship::audio::PracticeLoop::isSourceContinuous(
          sectionScheduler.acquireSnapshot(), startSeconds, endSeconds,
          playbackSampleRate > 0.0 ? playbackSampleRate : 44100.0)) {
    practiceLoopError.store(PracticeLoopError::discontinuousArrangement,
                            std::memory_order_release);
    return false;
  }
  practiceLoopError.store(PracticeLoopError::none, std::memory_order_release);
  practiceLoop.setRange(startSeconds, endSeconds);
  markTransportControlIntent();
  setArrangementJumpCommand(-1, false);
  arrangementTransitionIntentGeneration.fetch_add(1, std::memory_order_release);
  const double current = getSongCurrentPositionSeconds();
  if (!isSongPlaying() && (current < startSeconds || current >= endSeconds))
    seekSongTo(startSeconds);
  return isPracticeLoopActive();
}

bool MTControlsAudioProcessor::preparePracticeLoop(double startSeconds) noexcept {
  if (!hasLoadedSong() || !std::isfinite(startSeconds) ||
      startSeconds < getSongStartSeconds() || startSeconds >= getSongEndSeconds()) {
    practiceLoopError.store(PracticeLoopError::invalidRange, std::memory_order_release);
    return false;
  }
  if (isPracticeLoopActive() || getArrangementPartLoopIndex() >= 0 ||
      getPendingArrangementPartJumpIndex() >= 0) {
    practiceLoopError.store(PracticeLoopError::invalidRange, std::memory_order_release);
    return false;
  }
  practiceLoopError.store(PracticeLoopError::none, std::memory_order_release);
  practiceLoop.setRange(startSeconds, startSeconds);
  return hasPracticeLoopPreparation();
}

void MTControlsAudioProcessor::clearPracticeLoop() noexcept {
  const bool wasActive = practiceLoop.snapshot().active;
  practiceLoop.clear();
  practiceLoopError.store(PracticeLoopError::none, std::memory_order_release);
  if (wasActive) {
    markTransportControlIntent();
    arrangementTransitionIntentGeneration.fetch_add(1, std::memory_order_release);
  }
}

bool MTControlsAudioProcessor::isPracticeLoopActive() const noexcept {
  return practiceLoop.snapshot().active;
}

bool MTControlsAudioProcessor::hasPracticeLoopPreparation() const noexcept {
  return practiceLoop.snapshot().prepared;
}

juce::Range<double> MTControlsAudioProcessor::getPracticeLoopRange() const noexcept {
  const auto range = practiceLoop.snapshot();
  return range.active ? juce::Range<double>(range.startSeconds, range.endSeconds)
                      : juce::Range<double>{};
}

uint64_t MTControlsAudioProcessor::getPracticeLoopRevision() const noexcept {
  return practiceLoop.revision();
}

juce::String MTControlsAudioProcessor::getPracticeLoopError() const {
  switch (practiceLoopError.load(std::memory_order_acquire)) {
  case PracticeLoopError::invalidRange: return "invalid-range";
  case PracticeLoopError::tooShort: return "too-short";
  case PracticeLoopError::discontinuousArrangement: return "discontinuous-arrangement";
  case PracticeLoopError::bufferNotReady: return "buffer-not-ready";
  case PracticeLoopError::none: return {};
  }
  return {};
}

void MTControlsAudioProcessor::setSongStartSeconds(double seconds) noexcept {
  if (isBlankSongLoaded()) return;
  clearPracticeLoop();
  transportControlRevision.fetch_add(1, std::memory_order_release);
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
  if (isBlankSongLoaded()) return;
  clearPracticeLoop();
  transportControlRevision.fetch_add(1, std::memory_order_release);
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
  if (isBlankSongLoaded()) return;
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
    const SongArrangementSnapshot &incomingArrangement, bool isCustomArrangement) {
  const bool isBlank = isBlankSongLoaded();
  auto arrangement = isBlank ? SongArrangementSnapshot{} : incomingArrangement;
  if (isBlank) isCustomArrangement = false;
  // All entry points (local, restored project and remote) must compile the
  // same safely migrated structure that the waveform shows.
  if (!blankSongLoaded.load(std::memory_order_relaxed)) migrateLegacyArrangementCues(arrangement);
  {
    const juce::ScopedLock automationLock(sectionAutomationControlLock);
    sectionAutomationEditing.store(false, std::memory_order_release);
    sectionAutomationRemoteOwned.store(false, std::memory_order_release);
    sectionAutomationEditingPartId.clear();
    sectionAutomationArrangement = arrangement;
    publishSectionAutomationLocked();
    refreshSectionAutomation(getSongCurrentPositionSeconds());
  }
  clearPracticeLoop();
  transportControlRevision.fetch_add(1, std::memory_order_release);
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
  const bool editingArrangement =
      arrangementEditingActiveState.load(std::memory_order_relaxed);
  const auto partIdAtIndex = [this](const int index) {
    return juce::isPositiveAndBelow(
               index, static_cast<int>(arrangementParts.size()))
               ? arrangementParts[static_cast<size_t>(index)].id
               : juce::String{};
  };
  const auto preparingPartId = partIdAtIndex(getPreparingArrangementPartJumpIndex());
  const auto pendingPartId = partIdAtIndex(
      getPendingArrangementPartJumpIndex());
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
  // Guide annotations may cover only part of the source song. Their last
  // edge is not a playback trim: keep the full source plus any insertions
  // available when updating a Fin that follows the arrangement's end.
  if (isCustomArrangement && songLength > 0.0)
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
  // A mute/skip/metadata update keeps the same section boundaries. Preserve
  // the active loop and pending jump even when the arrangement editor is closed.
  const bool preservePlaybackState =
      editingArrangement || !arrangementTimelineChanged;

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

  if (next.empty() && !isBlank) {
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

  std::vector<ArrangementRuntimeCue> nextCues;
  nextCues.reserve(arrangement.cues.size());
  for (const auto &cue : arrangement.cues) {
    const double position = resolveArrangementCueSeconds(arrangement, cue);
    if (!std::isfinite(position) || position < 0.0 || position >= juce::jmax(songLength, totalLength))
      continue;
    const auto parent = std::find_if(arrangement.parts.begin(), arrangement.parts.end(),
        [&cue](const auto &part) { return part.id == cue.partId; });
    const bool skipCue = parent != arrangement.parts.end() && parent->skipPlayback;
    const auto countKey = arrangementCountSectionKey(cue.name);
    const int firstCount = countKey.getIntValue();
    const int requestedLength = firstCount >= 1 && firstCount <= 7 &&
        cue.countEnd >= firstCount && cue.countEnd <= 7 ? cue.countEnd - firstCount + 1 : 0;
    const bool validOffsets = requestedLength > 0 &&
        cue.countOffsetsSec.size() == static_cast<size_t>(requestedLength) &&
        cue.countOffsetsSec.front() == 0.0 &&
        std::adjacent_find(cue.countOffsetsSec.begin(), cue.countOffsetsSec.end(),
            [](double left, double right) { return right <= left; }) == cue.countOffsetsSec.end() &&
        std::all_of(cue.countOffsetsSec.begin(), cue.countOffsetsSec.end(), [](double offset) {
          return std::isfinite(offset) && offset >= 0.0;
        });
    const bool sequence = firstCount >= 1 && cue.countEnd >= firstCount && cue.countEnd <= 7 &&
        (validOffsets || (std::isfinite(cue.countIntervalSec) && cue.countIntervalSec > 0.0));
    const int countLength = sequence ? requestedLength : 1;
    for (int count = 0; count < countLength; ++count) {
      const double offset = sequence ? (validOffsets ? cue.countOffsetsSec[static_cast<size_t>(count)]
          : count * cue.countIntervalSec) : 0.0;
      const double announcementPosition = position + offset;
      if (announcementPosition >= juce::jmax(songLength, totalLength))
        break;
      const auto name = sequence ? juce::String(firstCount + count) : cue.name;
      const int trigger = arrangementCueTriggerId(dynamicGuideEngine, name);
      nextCues.push_back({name, announcementPosition, trigger, skipCue});
    }
  }
  std::stable_sort(nextCues.begin(), nextCues.end(), [](const auto &a, const auto &b) {
    return a.positionSec < b.positionSec;
  });

  {
    ArrangementStructurePublicationGuard publication(
        arrangementStructurePublicationSequence);
    arrangementParts = std::move(next);
    arrangementCues = std::move(nextCues);
    rebuildSectionScheduler();
    configureSectionEntryCache();
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

    if (preservePlaybackState) {
      const double currentSec =
          songCurrentPositionSeconds.load(std::memory_order_relaxed);
      const int currentPartIndex =
          findArrangementPartIndexForArrangementTime(currentSec);
      arrangementCurrentPartIndex.store(
          currentPartIndex >= 0 ? currentPartIndex : (isBlank ? -1 : 0),
          std::memory_order_relaxed);
      if (preparingPartId.isNotEmpty()) setArrangementJumpCommand(findPartIndexById(preparingPartId), false);
      else setArrangementJumpCommand(findPartIndexById(pendingPartId), true);
      arrangementLoopPartIndex.store(findPartIndexById(loopPartId),
                                     std::memory_order_relaxed);
    } else {
      arrangementCurrentPartIndex.store(isBlank ? -1 : 0, std::memory_order_relaxed);
      setArrangementJumpCommand(-1, false);
      arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
    }

    // Restoring sections must start at the playback window's beginning, not
    // at the first annotation (which may be minutes into the song).
    // A structural update must never stop an active transport:
    // live edits normally set preservePlaybackState, but keeping this guard
    // also covers an edit published during a UI mode transition. The audio
    // callback adopts the coherent scheduler snapshot and preserves the
    // current playhead.
    if (isCustomArrangement && arrangementTimelineChanged &&
        !preservePlaybackState &&
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

bool MTControlsAudioProcessor::requestArrangementPartJump(const int partIndex) {
  return requestArrangementPartJumpById(getArrangementPartId(partIndex));
}

juce::String MTControlsAudioProcessor::getArrangementControlRevision() const {
  return arrangementControlEpoch + ":" + juce::String(static_cast<juce::int64>(
      arrangementStructurePublicationSequence.load(std::memory_order_acquire)));
}

juce::String MTControlsAudioProcessor::getArrangementPartId(const int runtimeIndex) const {
  const juce::SpinLock::ScopedLockType lock(stemListLock);
  if (!juce::isPositiveAndBelow(runtimeIndex, static_cast<int>(arrangementParts.size())) ||
      runtimeIndex >= playworship::audio::SectionScheduler::maxSections) return {};
  const auto &id = arrangementParts[static_cast<size_t>(runtimeIndex)].id;
  return playworship::arrangement::uniquePartIndex(arrangementParts, id) == runtimeIndex ? id : juce::String{};
}

bool MTControlsAudioProcessor::requestArrangementPartJumpById(
    const juce::String &partId, const juce::String &expectedRevision) {
  double target = 0.0;
  bool seek = false;
  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    const int partIndex = playworship::arrangement::uniquePartIndex(
        arrangementParts, partId, playworship::audio::SectionScheduler::maxSections);
    if (!songLoaded.load() || partIndex < 0 ||
        (expectedRevision.isNotEmpty() && expectedRevision != getArrangementControlRevision())) return false;
    clearPracticeLoop();
    transportControlRevision.fetch_add(1, std::memory_order_release);
    if (getTransportState() == TransportState::playing) {
      if (getPendingArrangementPartJumpIndex() == partIndex || getPreparingArrangementPartJumpIndex() == partIndex)
        setArrangementJumpCommand(-1, false);
      else if (partIndex == findArrangementPartIndexForArrangementTime(getSongCurrentPositionSeconds())) {
        setArrangementJumpCommand(-1, false); arrangementLoopPartIndex.store(-1);
      } else
        setArrangementJumpCommand(partIndex, false); // Keep Loop until all stems are ready.
      arrangementTransitionIntentGeneration.fetch_add(1, std::memory_order_release);
      return true;
    }
    setArrangementJumpCommand(-1, false); arrangementLoopPartIndex.store(-1);
    target = arrangementParts[static_cast<size_t>(partIndex)].startSec; seek = true;
  }
  if (seek) seekSongTo(target);
  return true;
}

bool MTControlsAudioProcessor::toggleArrangementPartLoop(const int partIndex) {
  return toggleArrangementPartLoopById(getArrangementPartId(partIndex));
}

bool MTControlsAudioProcessor::toggleArrangementPartLoopById(
    const juce::String &partId, const juce::String &expectedRevision) {

  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    const int partIndex = playworship::arrangement::uniquePartIndex(
        arrangementParts, partId, playworship::audio::SectionScheduler::maxSections);
    if (!songLoaded.load(std::memory_order_relaxed) || partIndex < 0 ||
        (expectedRevision.isNotEmpty() && expectedRevision != getArrangementControlRevision())) return false;
    clearPracticeLoop();
    transportControlRevision.fetch_add(1, std::memory_order_release);

    const int activeLoopIndex =
        arrangementLoopPartIndex.load(std::memory_order_relaxed);
    if (activeLoopIndex == partIndex) {
      arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
      // Toggling the loop off must also drop any pending jump that was queued
      // to enter the loop, otherwise the audio thread will still teleport the
      // playhead to the loop section after the user already cancelled it.
      setArrangementJumpCommand(-1, false);
      arrangementTransitionIntentGeneration.fetch_add(
          1, std::memory_order_release);
      return true;
    }

    // Marking a section as loop must NOT auto-jump to it. The loop stays
    // dormant until natural playback reaches the section's range; once there
    // it wraps automatically on every section end. The previous behaviour
    // (queueing a jump when the playhead was outside) interrupted worship
    // flow by teleporting the user away from the section they were on.
    arrangementLoopPartIndex.store(partIndex, std::memory_order_relaxed);
    setArrangementJumpCommand(-1, false);
    arrangementTransitionIntentGeneration.fetch_add(
        1, std::memory_order_release);
  }
  return true;
}

void MTControlsAudioProcessor::clearArrangementPartLoop() noexcept {
  clearPracticeLoop();
  transportControlRevision.fetch_add(1, std::memory_order_release);
  arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
  arrangementTransitionIntentGeneration.fetch_add(
      1, std::memory_order_release);
}

void MTControlsAudioProcessor::clearPendingArrangementPartJump() noexcept {
  setArrangementJumpCommand(-1, false);
  arrangementTransitionIntentGeneration.fetch_add(
      1, std::memory_order_release);
}

int MTControlsAudioProcessor::getArrangementPartLoopIndex() const noexcept {
  return arrangementLoopPartIndex.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setArrangementJumpCommand(int index, bool confirmed) noexcept {
  auto old = arrangementJumpCommand.load();
  uint64_t next;
  do {
    next = (((old >> 32) + 1) << 32) | static_cast<uint64_t>(index + 1) | (confirmed && index >= 0 ? 0x10000u : 0u);
  } while (!arrangementJumpCommand.compare_exchange_weak(old, next));
  arrangementPendingJumpPartIndex.store(confirmed ? index : -1);
}
std::pair<int, int> MTControlsAudioProcessor::getArrangementJumpState() const noexcept {
  const auto command = arrangementJumpCommand.load();
  const int index = static_cast<int>(command & 0xffffu) - 1;
  return (command & 0x10000u) != 0 ? std::pair<int, int>{index, -1} : std::pair<int, int>{-1, index};
}
int MTControlsAudioProcessor::getPendingArrangementPartJumpIndex() const noexcept { return getArrangementJumpState().first; }
int MTControlsAudioProcessor::getPreparingArrangementPartJumpIndex() const noexcept { return getArrangementJumpState().second; }
void MTControlsAudioProcessor::configureSectionEntryCache() {
  if (sectionEntryCacheBudgetBytes == 0) {
#if JUCE_IOS || JUCE_ANDROID
    const juce::int64 cap = 16 * 1024 * 1024;
#else
    const juce::int64 cap = 64 * 1024 * 1024;
#endif
    const auto physical = static_cast<juce::int64>(juce::SystemStats::getMemorySizeInMegabytes()) * 1024 * 1024;
    sectionEntryCacheBudgetBytes = physical > 0 ? juce::jmin(cap, physical / 128) : cap;
  }
  std::vector<juce::int64> targets;
  for (const auto &part : arrangementParts) {
    const auto frame = playworship::audio::TransportClock::secondsToFrames(part.sourceStartSec, playbackSampleRate > 0 ? playbackSampleRate : 44100);
    if (std::find(targets.begin(), targets.end(), frame) == targets.end()) targets.push_back(frame);
  }
  const auto frames = juce::jmin(262144, juce::jmax(32768, playbackBlockSize * 4));
  const auto bytesPerSlot = static_cast<juce::int64>(frames) * 2 * sizeof(float);
  const int slots = stemTracks.empty() ? 0 : static_cast<int>(sectionEntryCacheBudgetBytes / (bytesPerSlot * static_cast<juce::int64>(stemTracks.size())));
  for (auto &track : stemTracks) track->configureSectionEntries(targets, slots);
}
void MTControlsAudioProcessor::setSectionEntryCacheBudget(juce::int64 bytes) {
  const juce::SpinLock::ScopedLockType lock(stemListLock);
  sectionEntryCacheBudgetBytes = juce::jmax<juce::int64>(1, bytes);
  configureSectionEntryCache();
}
juce::int64 MTControlsAudioProcessor::getSectionEntryCacheBytes() const {
  const juce::SpinLock::ScopedLockType lock(stemListLock);
  juce::int64 bytes = 0; for (const auto &track : stemTracks) bytes += track->getSectionCacheBytes(); return bytes;
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

void MTControlsAudioProcessor::setPracticeListeningMode(
    const int stemIndex, const int mode) noexcept {
  if (mode < 0 || mode > 2)
    return;
  if (mode != 0 &&
      (!juce::isPositiveAndBelow(stemIndex, getLoadedStemCount()) ||
       !juce::isPositiveAndBelow(stemIndex, maxStemCount) ||
       stemClickGuideStates[static_cast<size_t>(stemIndex)].load(
           std::memory_order_relaxed)))
    return;
  practiceListeningState.store(mode == 0 ? 0 : ((stemIndex + 1) << 2) | mode,
                               std::memory_order_release);
  // Repeated gestures are new ownership intentions, with the same fixed gain.
  practiceListeningRevision.fetch_add(1, std::memory_order_relaxed);
}

int MTControlsAudioProcessor::getPracticeListeningStem() const noexcept {
  return (practiceListeningState.load(std::memory_order_acquire) >> 2) - 1;
}

int MTControlsAudioProcessor::getPracticeListeningMode() const noexcept {
  return practiceListeningState.load(std::memory_order_acquire) & 3;
}

uint64_t MTControlsAudioProcessor::getPracticeListeningRevision() const noexcept {
  return practiceListeningRevision.load(std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getStemLevel(
    const int stemIndex) const noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return 0.0f;

  return stemLevels[static_cast<size_t>(stemIndex)].load(
      std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isStemAutomationEligible(const int stemIndex) const noexcept {
  return juce::isPositiveAndBelow(stemIndex, getLoadedStemCount()) &&
      !stemClickGuideStates[static_cast<size_t>(stemIndex)].load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setSectionAutomationStemIds(const juce::StringArray &ids) {
  const juce::ScopedLock lock(sectionAutomationControlLock);
  sectionAutomationStemIds = ids;
  publishSectionAutomationLocked();
  refreshSectionAutomation(getSongCurrentPositionSeconds());
}

void MTControlsAudioProcessor::publishSectionAutomationLocked() {
  using Automation = playworship::audio::SectionAutomation;
  std::vector<Automation::Part> compiled;
  compiled.reserve(sectionAutomationArrangement.parts.size());
  ++sectionAutomationPublicationRevision;
  for (const auto &part : sectionAutomationArrangement.parts) {
    if (!std::isfinite(part.startSec) || !std::isfinite(part.endSec) || part.endSec <= part.startSec)
      continue;
    Automation::Part out;
    out.start = part.startSec;
    out.end = part.endSec;
    out.token = static_cast<uint64_t>(part.id.hashCode64()) ^
                (sectionAutomationPublicationRevision * 0x9e3779b97f4a7c15ULL);
    if (out.token == 0) out.token = 1;
    for (const auto &stem : part.automation.stems) {
      const int index = sectionAutomationStemIds.indexOf(stem.stemId);
      if (!juce::isPositiveAndBelow(index, maxStemCount)) continue;
      auto &target = out.stems[static_cast<size_t>(index)];
      if (stem.volume && std::isfinite(*stem.volume)) { target.mask |= Automation::volume; target.volume = juce::jlimit(0.0f, 1.0f, *stem.volume); }
      if (stem.gain && std::isfinite(*stem.gain)) { target.mask |= Automation::gain; target.gain = juce::jlimit(0.0f, 2.0f, *stem.gain); }
      if (stem.mute) { target.mask |= Automation::mute; target.mute = *stem.mute; }
    }
    const auto &pads = part.automation.pads;
    if (pads.volume && std::isfinite(*pads.volume)) { out.pads.mask |= Automation::volume; out.pads.volume = juce::jlimit(0.0f, 1.0f, *pads.volume); }
    if (pads.gain && std::isfinite(*pads.gain)) { out.pads.mask |= Automation::gain; out.pads.gain = juce::jlimit(0.0f, 1.2f, *pads.gain); }
    if (pads.enabled) { out.pads.mask |= Automation::enabled; out.pads.enabled = *pads.enabled; }
    if (pads.noteIndex && *pads.noteIndex >= -1 && *pads.noteIndex < 12) { out.pads.mask |= Automation::note; out.pads.note = *pads.noteIndex; }
    if (pads.octave && *pads.octave >= -3 && *pads.octave <= 3) { out.pads.mask |= Automation::octave; out.pads.octave = *pads.octave; }
    compiled.push_back(out);
  }
  std::stable_sort(compiled.begin(), compiled.end(), [](const auto &a, const auto &b) {
    return a.start == b.start ? a.end < b.end : a.start < b.start;
  });
  const bool padsScoped = std::any_of(compiled.begin(), compiled.end(),
      [](const auto &part) { return part.pads.mask != 0; });
  sectionAutomationTimeline.publish(compiled, padsScoped);
  sectionAutomationManualToken.store(0, std::memory_order_release);
  if (std::any_of(compiled.begin(), compiled.end(), [](const auto &part) {
        return (part.pads.mask & (Automation::note | Automation::enabled)) != 0;
      }))
    startPadsPreloadAsync();
}

void MTControlsAudioProcessor::refreshSectionAutomation(const double seconds) noexcept {
  const auto selected = sectionAutomationTimeline.select(seconds);
  publishSectionAutomationSelection(selected);
}

void MTControlsAudioProcessor::publishSectionAutomationSelection(
    const playworship::audio::SectionAutomation::Selection &selected) noexcept {
  if (!selected.valid) return;
  const auto previousToken = sectionAutomationCurrentToken.load(std::memory_order_acquire);
  if (previousToken != selected.token) {
    auto oldManualToken = previousToken;
    sectionAutomationManualToken.compare_exchange_strong(oldManualToken, 0, std::memory_order_acq_rel);
  }
  for (int index = 0; index < maxStemCount; ++index) {
    const auto &value = selected.stems[static_cast<size_t>(index)];
    auto &target = sectionAutomationActive.stems[static_cast<size_t>(index)];
    target.volume.store(value.volume, std::memory_order_relaxed);
    target.gain.store(value.gain, std::memory_order_relaxed);
    target.mute.store(value.mute, std::memory_order_relaxed);
    target.mask.store(value.mask, std::memory_order_release);
  }
  auto &target = sectionAutomationActive.pads;
  target.volume.store(selected.pads.volume, std::memory_order_relaxed);
  target.gain.store(selected.pads.gain, std::memory_order_relaxed);
  target.enabled.store(selected.pads.enabled, std::memory_order_relaxed);
  target.note.store(selected.pads.note, std::memory_order_relaxed);
  target.octave.store(selected.pads.octave, std::memory_order_relaxed);
  target.mask.store(selected.pads.mask, std::memory_order_release);
  sectionAutomationCurrentToken.store(selected.token, std::memory_order_release);
}

void MTControlsAudioProcessor::prepareSectionAutomationManual() noexcept {
  const auto token = sectionAutomationCurrentToken.load(std::memory_order_acquire);
  if (sectionAutomationManualToken.load(std::memory_order_acquire) == token) return;
  for (auto &stem : sectionAutomationManual.stems) stem.mask.store(0, std::memory_order_relaxed);
  sectionAutomationManual.pads.mask.store(0, std::memory_order_relaxed);
  sectionAutomationManualToken.store(token, std::memory_order_release);
}

unsigned MTControlsAudioProcessor::getStemAutomationMask(const int stemIndex) const noexcept {
  if (!isStemAutomationEligible(stemIndex)) return 0;
  const auto &source = sectionAutomationEditing.load(std::memory_order_acquire)
      ? sectionAutomationDraft : sectionAutomationActive;
  return source.stems[static_cast<size_t>(stemIndex)].mask.load(std::memory_order_acquire);
}

unsigned MTControlsAudioProcessor::getPadsAutomationMask() const noexcept {
  return (sectionAutomationEditing.load(std::memory_order_acquire)
      ? sectionAutomationDraft : sectionAutomationActive).pads.mask.load(std::memory_order_acquire);
}

bool MTControlsAudioProcessor::isStemSectionAutomated(const int index) const noexcept {
  return getStemAutomationMask(index) != 0;
}
bool MTControlsAudioProcessor::isPadsSectionAutomated() const noexcept {
  return getPadsAutomationMask() != 0;
}
bool MTControlsAudioProcessor::isSectionAutomationEditing() const noexcept {
  return sectionAutomationEditing.load(std::memory_order_acquire);
}
juce::String MTControlsAudioProcessor::getSectionAutomationEditingPartId() const {
  const juce::ScopedLock lock(sectionAutomationControlLock);
  return isSectionAutomationEditing() ? sectionAutomationEditingPartId : juce::String{};
}
juce::String MTControlsAudioProcessor::getSectionAutomationEditingPartName() const {
  const juce::ScopedLock lock(sectionAutomationControlLock);
  if (!isSectionAutomationEditing()) return {};
  for (const auto &part : sectionAutomationArrangement.parts)
    if (part.id == sectionAutomationEditingPartId) return part.name;
  return {};
}
SongArrangementSnapshot MTControlsAudioProcessor::getSongArrangementSnapshot() const {
  const juce::ScopedLock lock(sectionAutomationControlLock);
  return sectionAutomationArrangement;
}

playworship::practice::WaveformTimeline
MTControlsAudioProcessor::getPracticeWaveformTimeline() const {
  // Source/range/rate writers already own this control-side lock. Guide-only
  // republication may still occur, so acquire the scheduler's coherent atomic
  // snapshot. Copy only bounded data here; decoding/projection/identity work
  // happens after releasing the lock, never in the realtime callback.
  const juce::SpinLock::ScopedLockType lock(stemListLock);
  playworship::practice::WaveformTimeline timeline;
  timeline.sections = sectionScheduler.acquireSnapshot();
  timeline.sampleRate = playbackSampleRate > 0.0 ? playbackSampleRate : 44100.0;
  timeline.sourceLengthSeconds =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  timeline.customArrangement =
      arrangementCustomActive.load(std::memory_order_relaxed);
  timeline.lengthSeconds = timeline.customArrangement
      ? juce::jmax(timeline.sourceLengthSeconds,
                  arrangementTotalLengthSeconds.load(std::memory_order_relaxed))
      : timeline.sourceLengthSeconds;
  timeline.sourceRevision =
      diagnosticSourceGeneration.load(std::memory_order_relaxed);
  return timeline;
}

bool MTControlsAudioProcessor::beginSectionAutomationEditing(const juce::String &partId, const bool remoteOwned) {
  const juce::ScopedLock lock(sectionAutomationControlLock);
  if (isSectionAutomationEditing()) return false;
  const auto found = std::find_if(sectionAutomationArrangement.parts.begin(),
      sectionAutomationArrangement.parts.end(), [&partId](const auto &part) { return part.id == partId; });
  if (found == sectionAutomationArrangement.parts.end()) return false;
  pauseSong();
  sectionAutomationEditing.store(false, std::memory_order_release);
  sectionAutomationRemoteOwned.store(remoteOwned, std::memory_order_release);
  for (auto &stem : sectionAutomationDraft.stems) stem.mask.store(0, std::memory_order_relaxed);
  sectionAutomationDraft.pads.mask.store(0, std::memory_order_relaxed);
  using Automation = playworship::audio::SectionAutomation;
  for (const auto &stem : found->automation.stems) {
    const int index = sectionAutomationStemIds.indexOf(stem.stemId);
    if (!isStemAutomationEligible(index)) continue;
    auto &target = sectionAutomationDraft.stems[static_cast<size_t>(index)];
    unsigned mask = 0;
    if (stem.volume) { mask |= Automation::volume; target.volume.store(*stem.volume); }
    if (stem.gain) { mask |= Automation::gain; target.gain.store(*stem.gain); }
    if (stem.mute) { mask |= Automation::mute; target.mute.store(*stem.mute); }
    target.mask.store(mask, std::memory_order_relaxed);
  }
  const auto &pads = found->automation.pads;
  auto &target = sectionAutomationDraft.pads;
  unsigned mask = 0;
  if (pads.volume) { mask |= Automation::volume; target.volume.store(*pads.volume); }
  if (pads.gain) { mask |= Automation::gain; target.gain.store(*pads.gain); }
  if (pads.enabled) { mask |= Automation::enabled; target.enabled.store(*pads.enabled); }
  if (pads.noteIndex) { mask |= Automation::note; target.note.store(*pads.noteIndex); }
  if (pads.octave) { mask |= Automation::octave; target.octave.store(*pads.octave); }
  target.mask.store(mask, std::memory_order_relaxed);
  sectionAutomationEditingPartId = partId;
  sectionAutomationEditing.store(true, std::memory_order_release);
  return true;
}

bool MTControlsAudioProcessor::finishSectionAutomationEditing(const bool save) {
  const juce::ScopedLock lock(sectionAutomationControlLock);
  if (!isSectionAutomationEditing()) return false;
  bool found = false;
  for (auto &part : sectionAutomationArrangement.parts) {
    if (part.id != sectionAutomationEditingPartId) continue;
    found = true;
    if (save) {
      using Automation = playworship::audio::SectionAutomation;
      std::vector<SectionStemAutomation> retained;
      for (const auto &existing : part.automation.stems) {
        const int index = sectionAutomationStemIds.indexOf(existing.stemId);
        if (!isStemAutomationEligible(index)) retained.push_back(existing);
      }
      part.automation.stems = std::move(retained);
      for (int index = 0; index < getLoadedStemCount(); ++index) {
        const auto &source = sectionAutomationDraft.stems[static_cast<size_t>(index)];
        const auto mask = source.mask.load(std::memory_order_acquire);
        if (mask == 0 || !isStemAutomationEligible(index) || index >= sectionAutomationStemIds.size()) continue;
        SectionStemAutomation stem;
        stem.stemId = sectionAutomationStemIds[index];
        if ((mask & Automation::volume) != 0) stem.volume = source.volume.load();
        if ((mask & Automation::gain) != 0) stem.gain = source.gain.load();
        if ((mask & Automation::mute) != 0) stem.mute = source.mute.load();
        part.automation.stems.push_back(std::move(stem));
      }
      const auto &source = sectionAutomationDraft.pads;
      const auto mask = source.mask.load(std::memory_order_acquire);
      if ((mask & Automation::volume) != 0) part.automation.pads.volume = source.volume.load();
      if ((mask & Automation::gain) != 0) part.automation.pads.gain = source.gain.load();
      if ((mask & Automation::enabled) != 0) part.automation.pads.enabled = source.enabled.load();
      if ((mask & Automation::note) != 0) part.automation.pads.noteIndex = source.note.load();
      if ((mask & Automation::octave) != 0) part.automation.pads.octave = source.octave.load();
    }
    break;
  }
  sectionAutomationEditing.store(false, std::memory_order_release);
  sectionAutomationEditingPartId.clear();
  sectionAutomationRemoteOwned.store(false, std::memory_order_release);
  if (save && found) publishSectionAutomationLocked();
  refreshSectionAutomation(getSongCurrentPositionSeconds());
  return found;
}

bool MTControlsAudioProcessor::clearSectionAutomation(const juce::String &partId) {
  const juce::ScopedLock lock(sectionAutomationControlLock);
  if (isSectionAutomationEditing() && sectionAutomationEditingPartId != partId) return false;
  for (auto &part : sectionAutomationArrangement.parts) {
    if (part.id != partId) continue;
    if (isSectionAutomationEditing()) {
      sectionAutomationEditing.store(false, std::memory_order_release);
      sectionAutomationEditingPartId.clear();
    }
    part.automation = {};
    sectionAutomationRemoteOwned.store(false, std::memory_order_release);
    publishSectionAutomationLocked();
    refreshSectionAutomation(getSongCurrentPositionSeconds());
    return true;
  }
  return false;
}

void MTControlsAudioProcessor::resetSectionAutomationState() {
  const juce::ScopedLock lock(sectionAutomationControlLock);
  if (sectionAutomationActive.pads.mask.load(std::memory_order_acquire) != 0)
    padsAwaitingManualAfterAutomation.store(true, std::memory_order_release);
  sectionAutomationEditing.store(false, std::memory_order_release);
  sectionAutomationEditingPartId.clear();
  sectionAutomationArrangement = {};
  sectionAutomationRemoteOwned.store(false, std::memory_order_release);
  sectionAutomationStemIds.clear();
  sectionAutomationTimeline.publish({});
  sectionAutomationCurrentToken.store(0, std::memory_order_release);
  sectionAutomationManualToken.store(0, std::memory_order_release);
  for (auto &stem : sectionAutomationActive.stems) stem.mask.store(0, std::memory_order_release);
  sectionAutomationActive.pads.mask.store(0, std::memory_order_release);
}

playworship::audio::SectionAutomation::Stem MTControlsAudioProcessor::resolveSectionAutomationStem(
    const int index, const playworship::audio::SectionAutomation::Selection &selection) const noexcept {
  using Automation = playworship::audio::SectionAutomation;
  Automation::Stem result;
  result.volume = getStemBaseVolume(index);
  result.gain = getStemBaseGain(index);
  result.mute = isStemBaseMuted(index);
  if (!isStemAutomationEligible(index)) return result;
  auto override = selection.stems[static_cast<size_t>(index)];
  if (sectionAutomationEditing.load(std::memory_order_acquire)) {
    const auto &draft = sectionAutomationDraft.stems[static_cast<size_t>(index)];
    override.mask = draft.mask.load(std::memory_order_acquire);
    override.volume = draft.volume.load(); override.gain = draft.gain.load(); override.mute = draft.mute.load();
  }
  if ((override.mask & Automation::volume) != 0) result.volume = override.volume;
  if ((override.mask & Automation::gain) != 0) result.gain = override.gain;
  if ((override.mask & Automation::mute) != 0) result.mute = override.mute;
  if (!isSectionAutomationEditing() && selection.token != 0 && sectionAutomationManualToken.load(std::memory_order_acquire) == selection.token) {
    const auto &manual = sectionAutomationManual.stems[static_cast<size_t>(index)];
    const auto mask = manual.mask.load(std::memory_order_acquire);
    if ((mask & Automation::volume) != 0) result.volume = manual.volume.load();
    if ((mask & Automation::gain) != 0) result.gain = manual.gain.load();
    if ((mask & Automation::mute) != 0) result.mute = manual.mute.load();
  }
  return result;
}

playworship::audio::SectionAutomation::Pads MTControlsAudioProcessor::resolveSectionAutomationPads(
    const playworship::audio::SectionAutomation::Selection &selection) const noexcept {
  using Automation = playworship::audio::SectionAutomation;
  Automation::Pads result;
  result.volume = getPadsBaseVolume(); result.gain = getPadsBaseGain();
  result.enabled = isPadsBaseEnabled(); result.note = getBasePadNoteIndex(); result.octave = getBasePadOctave();
  auto override = selection.pads;
  if (isSectionAutomationEditing()) {
    const auto &draft = sectionAutomationDraft.pads;
    override.mask = draft.mask.load(std::memory_order_acquire);
    override.volume = draft.volume.load(); override.gain = draft.gain.load(); override.enabled = draft.enabled.load();
    override.note = draft.note.load(); override.octave = draft.octave.load();
  }
  const auto apply = [&result](unsigned mask, float volume, float gain, bool enabled, int note, int octave) {
    if ((mask & Automation::volume) != 0) result.volume = volume;
    if ((mask & Automation::gain) != 0) result.gain = gain;
    if ((mask & Automation::enabled) != 0) result.enabled = enabled;
    if ((mask & Automation::note) != 0) result.note = note;
    if ((mask & Automation::octave) != 0) result.octave = octave;
  };
  result.mask = override.mask;
  apply(override.mask, override.volume, override.gain, override.enabled, override.note, override.octave);
  if (override.mask == 0 && !isSectionAutomationEditing() &&
      padsAwaitingManualAfterAutomation.load(std::memory_order_acquire)) {
    result.enabled = false;
    result.note = -1;
  }
  if (!isSectionAutomationEditing() && selection.token != 0 && sectionAutomationManualToken.load(std::memory_order_acquire) == selection.token) {
    const auto &manual = sectionAutomationManual.pads;
    apply(manual.mask.load(std::memory_order_acquire), manual.volume.load(), manual.gain.load(), manual.enabled.load(), manual.note.load(), manual.octave.load());
  }
  return result;
}

void MTControlsAudioProcessor::setStemVolume(const int index, const float value) noexcept {
  if (!juce::isPositiveAndBelow(index, maxStemCount) || !std::isfinite(value)) return;
  using Automation = playworship::audio::SectionAutomation;
  const auto normalized = juce::jlimit(0.0f, 1.0f, value);
  if (isSectionAutomationEditing() && isStemAutomationEligible(index)) {
    auto &target = sectionAutomationDraft.stems[static_cast<size_t>(index)];
    target.volume.store(normalized, std::memory_order_relaxed);
    target.mask.fetch_or(Automation::volume, std::memory_order_release);
  } else if ((getStemAutomationMask(index) & Automation::volume) != 0) {
    prepareSectionAutomationManual();
    auto &target = sectionAutomationManual.stems[static_cast<size_t>(index)];
    target.volume.store(normalized, std::memory_order_relaxed);
    target.mask.fetch_or(Automation::volume, std::memory_order_release);
  } else {
    stemVolumes[static_cast<size_t>(index)].store(normalized, std::memory_order_relaxed);
  }
}

float MTControlsAudioProcessor::getStemBaseVolume(const int index) const noexcept {
  return juce::isPositiveAndBelow(index, maxStemCount)
      ? stemVolumes[static_cast<size_t>(index)].load(std::memory_order_relaxed) : 1.0f;
}

float MTControlsAudioProcessor::getStemVolume(const int index) const noexcept {
  if (!juce::isPositiveAndBelow(index, maxStemCount)) return 1.0f;
  using Automation = playworship::audio::SectionAutomation;
  if ((getStemAutomationMask(index) & Automation::volume) == 0) return getStemBaseVolume(index);
  if (isSectionAutomationEditing())
    return sectionAutomationDraft.stems[static_cast<size_t>(index)].volume.load(std::memory_order_relaxed);
  const auto &manual = sectionAutomationManual.stems[static_cast<size_t>(index)];
  if (sectionAutomationManualToken.load(std::memory_order_acquire) ==
          sectionAutomationCurrentToken.load(std::memory_order_acquire) &&
      (manual.mask.load(std::memory_order_acquire) & Automation::volume) != 0)
    return manual.volume.load(std::memory_order_relaxed);
  return sectionAutomationActive.stems[static_cast<size_t>(index)].volume.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setStemGain(const int index, const float value) noexcept {
  if (!juce::isPositiveAndBelow(index, maxStemCount) || !std::isfinite(value)) return;
  using Automation = playworship::audio::SectionAutomation;
  const auto normalized = juce::jlimit(0.0f, 2.0f, value);
  if (isSectionAutomationEditing() && isStemAutomationEligible(index)) {
    auto &target = sectionAutomationDraft.stems[static_cast<size_t>(index)];
    target.gain.store(normalized, std::memory_order_relaxed);
    target.mask.fetch_or(Automation::gain, std::memory_order_release);
  } else if ((getStemAutomationMask(index) & Automation::gain) != 0) {
    prepareSectionAutomationManual();
    auto &target = sectionAutomationManual.stems[static_cast<size_t>(index)];
    target.gain.store(normalized, std::memory_order_relaxed);
    target.mask.fetch_or(Automation::gain, std::memory_order_release);
  } else {
    stemGains[static_cast<size_t>(index)].store(normalized, std::memory_order_relaxed);
  }
}

float MTControlsAudioProcessor::getStemBaseGain(const int index) const noexcept {
  return juce::isPositiveAndBelow(index, maxStemCount)
      ? stemGains[static_cast<size_t>(index)].load(std::memory_order_relaxed) : 1.0f;
}

float MTControlsAudioProcessor::getStemGain(const int index) const noexcept {
  if (!juce::isPositiveAndBelow(index, maxStemCount)) return 1.0f;
  using Automation = playworship::audio::SectionAutomation;
  if ((getStemAutomationMask(index) & Automation::gain) == 0) return getStemBaseGain(index);
  if (isSectionAutomationEditing())
    return sectionAutomationDraft.stems[static_cast<size_t>(index)].gain.load(std::memory_order_relaxed);
  const auto &manual = sectionAutomationManual.stems[static_cast<size_t>(index)];
  if (sectionAutomationManualToken.load(std::memory_order_acquire) ==
          sectionAutomationCurrentToken.load(std::memory_order_acquire) &&
      (manual.mask.load(std::memory_order_acquire) & Automation::gain) != 0)
    return manual.gain.load(std::memory_order_relaxed);
  return sectionAutomationActive.stems[static_cast<size_t>(index)].gain.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setStemMute(const int index, const bool value) noexcept {
  if (!juce::isPositiveAndBelow(index, maxStemCount)) return;
  using Automation = playworship::audio::SectionAutomation;
  const auto normalized = value;
  if (isSectionAutomationEditing() && isStemAutomationEligible(index)) {
    auto &target = sectionAutomationDraft.stems[static_cast<size_t>(index)];
    target.mute.store(normalized, std::memory_order_relaxed);
    target.mask.fetch_or(Automation::mute, std::memory_order_release);
  } else if ((getStemAutomationMask(index) & Automation::mute) != 0) {
    prepareSectionAutomationManual();
    auto &target = sectionAutomationManual.stems[static_cast<size_t>(index)];
    target.mute.store(normalized, std::memory_order_relaxed);
    target.mask.fetch_or(Automation::mute, std::memory_order_release);
  } else {
    stemMuteStates[static_cast<size_t>(index)].store(normalized, std::memory_order_relaxed);
  }
}

bool MTControlsAudioProcessor::isStemBaseMuted(const int index) const noexcept {
  return juce::isPositiveAndBelow(index, maxStemCount)
      ? stemMuteStates[static_cast<size_t>(index)].load(std::memory_order_relaxed) : false;
}

bool MTControlsAudioProcessor::isStemMuted(const int index) const noexcept {
  if (!juce::isPositiveAndBelow(index, maxStemCount)) return false;
  using Automation = playworship::audio::SectionAutomation;
  if ((getStemAutomationMask(index) & Automation::mute) == 0) return isStemBaseMuted(index);
  if (isSectionAutomationEditing())
    return sectionAutomationDraft.stems[static_cast<size_t>(index)].mute.load(std::memory_order_relaxed);
  const auto &manual = sectionAutomationManual.stems[static_cast<size_t>(index)];
  if (sectionAutomationManualToken.load(std::memory_order_acquire) ==
          sectionAutomationCurrentToken.load(std::memory_order_acquire) &&
      (manual.mask.load(std::memory_order_acquire) & Automation::mute) != 0)
    return manual.mute.load(std::memory_order_relaxed);
  return sectionAutomationActive.stems[static_cast<size_t>(index)].mute.load(std::memory_order_relaxed);
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

void MTControlsAudioProcessor::applySongOutputRoutingDefaults(
    const ImportedSongData &song,
    const std::vector<CategoryRoutingDefaults> &defaults) {
  const auto routeForCategory = [&defaults](const juce::String &category) {
    const auto normalizedCategory = category.trim();
    if (normalizedCategory.isEmpty())
      return CategoryRoutingDefaults{};
    int fallback = -1;
    for (size_t i = 0; i < kTrackCategories.size(); ++i) {
      const juce::String name = kTrackCategories[i];
      if (name.equalsIgnoreCase("Other"))
        fallback = static_cast<int>(i);
      if (name.equalsIgnoreCase(normalizedCategory))
        return i < defaults.size() ? defaults[i] : CategoryRoutingDefaults{};
    }
    return juce::isPositiveAndBelow(fallback, static_cast<int>(defaults.size()))
               ? defaults[static_cast<size_t>(fallback)]
               : CategoryRoutingDefaults{};
  };

  // Dynamic sources also need the device routes when no corresponding stem
  // exists. Saved song snapshots must not override the current routing panel.
  const auto click = routeForCategory("Click");
  const auto guide = routeForCategory("Cues");
  const bool hasStereoOutputs = getAvailableOutputChannelCount() >= 2;
  setMetronomeOutputRouting(click.baseChannel, hasStereoOutputs ? click.width : 1);
  setDynamicGuideOutputRouting(guide.baseChannel, hasStereoOutputs ? guide.width : 1);

  int stemIndex = 0;
  const int stemCount = getLoadedStemCount();
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;
    if (stemIndex >= stemCount)
      break;
    const int priority = stemPriorityForStripOrder(stem);
    const auto category = priority == 0 ? juce::String("Click")
                         : priority == 1 ? juce::String("Cues")
                                         : stem.category;
    const auto route = routeForCategory(category);
    const int previousWidth = getStemOutputWidth(stemIndex);
    const int previousChannel = getStemOutputChannel(stemIndex);
    setStemOutputWidth(stemIndex, hasStereoOutputs ? route.width : 1);
    setStemOutputChannel(stemIndex, route.baseChannel);
    SongLoadProbe::instance().recordEvent(
        "routing", "apply.stem",
        "idx=" + juce::String(stemIndex) + " cat=" + category +
            " prevW=" + juce::String(previousWidth) +
            " newW=" + juce::String(route.width) +
            " prevC=" + juce::String(previousChannel) +
            " newC=" + juce::String(route.baseChannel));
    ++stemIndex;
  }
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

bool MTControlsAudioProcessor::ensurePadsPackReady(const bool allowNetworkDownloads) {
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
      padsPackStatusText = "Preparando recursos... / Preparing resources...";
    }
    const bool assetsReady = AssetBootstrapManager::ensurePadsPackReady(
        &assetsError, allowNetworkDownloads);
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

void MTControlsAudioProcessor::startPadsPreloadAsync(const bool allowNetworkDownloads) {
  if (padsAttacksPrepared.load(std::memory_order_acquire) &&
       pendingPadNote.load(std::memory_order_acquire) < 0 &&
       !padsFullLoadRequested.load(std::memory_order_acquire) &&
       midiPendingPadLoadMask.load(std::memory_order_acquire) == 0)
    return;
  if (padsPreloadRunning.exchange(true, std::memory_order_acq_rel)) {
    if (allowNetworkDownloads)
      padsNetworkPreloadPending.store(true, std::memory_order_release);
    return;
  }
  if (allowNetworkDownloads)
    padsNetworkPreloadPending.store(false, std::memory_order_release);

  if (padsPreloadThread.joinable())
    padsPreloadThread.join();

  padsPreloadShouldExit.store(false, std::memory_order_release);
  padsPreloadThread = std::thread([this, allowNetworkDownloads] {
    if (!ensurePadsPackReady(allowNetworkDownloads)) {
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
            !padsPreloadShouldExit.load(std::memory_order_acquire) &&
            getActivePadNoteIndex() == requested) {
          padsEngine.setEnabled(isPadsEnabled());
          padsRenderedOctave.store(pendingPadOctave.load(std::memory_order_acquire), std::memory_order_relaxed);
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
  const auto deviceKey = buildDeviceKeyFromManager(
      standaloneAudioDeviceBridge.getAudioDeviceManager());

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

void MTControlsAudioProcessor::setPadsEnabled(const bool value) noexcept {
  using Automation = playworship::audio::SectionAutomation;
  const auto normalized = value;
  if (value && !isSectionAutomationEditing())
    padsAwaitingManualAfterAutomation.store(false, std::memory_order_release);
  if (isSectionAutomationEditing()) {
    sectionAutomationDraft.pads.enabled.store(normalized, std::memory_order_relaxed);
    sectionAutomationDraft.pads.mask.fetch_or(Automation::enabled, std::memory_order_release);
  } else if ((getPadsAutomationMask() & Automation::enabled) != 0) {
    prepareSectionAutomationManual();
    sectionAutomationManual.pads.enabled.store(normalized, std::memory_order_relaxed);
    sectionAutomationManual.pads.mask.fetch_or(Automation::enabled, std::memory_order_release);
  } else {
    padsEnabledState.store(normalized, std::memory_order_relaxed);
  }
}

bool MTControlsAudioProcessor::isPadsBaseEnabled() const noexcept {
  return padsEnabledState.load(std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isPadsEnabled() const noexcept {
  using Automation = playworship::audio::SectionAutomation;
  if (!isSectionAutomationEditing() && getPadsAutomationMask() == 0 &&
      padsAwaitingManualAfterAutomation.load(std::memory_order_acquire)) return false;
  if ((getPadsAutomationMask() & Automation::enabled) == 0) return isPadsBaseEnabled();
  if (isSectionAutomationEditing()) return sectionAutomationDraft.pads.enabled.load(std::memory_order_relaxed);
  const auto &manual = sectionAutomationManual.pads;
  if (sectionAutomationManualToken.load(std::memory_order_acquire) ==
          sectionAutomationCurrentToken.load(std::memory_order_acquire) &&
      (manual.mask.load(std::memory_order_acquire) & Automation::enabled) != 0)
    return manual.enabled.load(std::memory_order_relaxed);
  return sectionAutomationActive.pads.enabled.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setPadsVolume(const float value) noexcept {
  if (!std::isfinite(value)) return;
  using Automation = playworship::audio::SectionAutomation;
  const auto normalized = juce::jlimit(0.0f, 1.0f, value);
  if (isSectionAutomationEditing()) {
    sectionAutomationDraft.pads.volume.store(normalized, std::memory_order_relaxed);
    sectionAutomationDraft.pads.mask.fetch_or(Automation::volume, std::memory_order_release);
  } else if ((getPadsAutomationMask() & Automation::volume) != 0) {
    prepareSectionAutomationManual();
    sectionAutomationManual.pads.volume.store(normalized, std::memory_order_relaxed);
    sectionAutomationManual.pads.mask.fetch_or(Automation::volume, std::memory_order_release);
  } else {
    padsVolume.store(normalized, std::memory_order_relaxed);
  }
}

float MTControlsAudioProcessor::getPadsBaseVolume() const noexcept {
  return padsVolume.load(std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getPadsVolume() const noexcept {
  using Automation = playworship::audio::SectionAutomation;
  if ((getPadsAutomationMask() & Automation::volume) == 0) return getPadsBaseVolume();
  if (isSectionAutomationEditing()) return sectionAutomationDraft.pads.volume.load(std::memory_order_relaxed);
  const auto &manual = sectionAutomationManual.pads;
  if (sectionAutomationManualToken.load(std::memory_order_acquire) ==
          sectionAutomationCurrentToken.load(std::memory_order_acquire) &&
      (manual.mask.load(std::memory_order_acquire) & Automation::volume) != 0)
    return manual.volume.load(std::memory_order_relaxed);
  return sectionAutomationActive.pads.volume.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setPadsGain(const float value) noexcept {
  if (!std::isfinite(value)) return;
  using Automation = playworship::audio::SectionAutomation;
  const auto normalized = juce::jlimit(0.0f, 1.2f, value);
  if (isSectionAutomationEditing()) {
    sectionAutomationDraft.pads.gain.store(normalized, std::memory_order_relaxed);
    sectionAutomationDraft.pads.mask.fetch_or(Automation::gain, std::memory_order_release);
  } else if ((getPadsAutomationMask() & Automation::gain) != 0) {
    prepareSectionAutomationManual();
    sectionAutomationManual.pads.gain.store(normalized, std::memory_order_relaxed);
    sectionAutomationManual.pads.mask.fetch_or(Automation::gain, std::memory_order_release);
  } else {
    padsGain.store(normalized, std::memory_order_relaxed);
  }
}

float MTControlsAudioProcessor::getPadsBaseGain() const noexcept {
  return padsGain.load(std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getPadsGain() const noexcept {
  using Automation = playworship::audio::SectionAutomation;
  if ((getPadsAutomationMask() & Automation::gain) == 0) return getPadsBaseGain();
  if (isSectionAutomationEditing()) return sectionAutomationDraft.pads.gain.load(std::memory_order_relaxed);
  const auto &manual = sectionAutomationManual.pads;
  if (sectionAutomationManualToken.load(std::memory_order_acquire) ==
          sectionAutomationCurrentToken.load(std::memory_order_acquire) &&
      (manual.mask.load(std::memory_order_acquire) & Automation::gain) != 0)
    return manual.gain.load(std::memory_order_relaxed);
  return sectionAutomationActive.pads.gain.load(std::memory_order_relaxed);
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

int MTControlsAudioProcessor::getBasePadNoteIndex() const noexcept { return padsBaseNote.load(std::memory_order_relaxed); }
int MTControlsAudioProcessor::getBasePadOctave() const noexcept { return padsBaseOctave.load(std::memory_order_relaxed); }
int MTControlsAudioProcessor::getActivePadOctave() const noexcept {
  using Automation = playworship::audio::SectionAutomation;
  if ((getPadsAutomationMask() & Automation::octave) == 0) return getBasePadOctave();
  if (isSectionAutomationEditing()) return sectionAutomationDraft.pads.octave.load();
  if (sectionAutomationManualToken.load(std::memory_order_acquire) == sectionAutomationCurrentToken.load(std::memory_order_acquire) &&
      (sectionAutomationManual.pads.mask.load(std::memory_order_acquire) & Automation::octave) != 0)
    return sectionAutomationManual.pads.octave.load();
  return sectionAutomationActive.pads.octave.load();
}

void MTControlsAudioProcessor::setPadNoteAutomationTarget(const int note, const int octave) noexcept {
  using Automation = playworship::audio::SectionAutomation;
  if (note >= 0 && !isSectionAutomationEditing())
    padsAwaitingManualAfterAutomation.store(false, std::memory_order_release);
  if (isSectionAutomationEditing()) {
    sectionAutomationDraft.pads.note.store(note, std::memory_order_relaxed);
    sectionAutomationDraft.pads.octave.store(octave, std::memory_order_relaxed);
    sectionAutomationDraft.pads.mask.fetch_or(Automation::note | Automation::octave, std::memory_order_release);
  } else if ((getPadsAutomationMask() & (Automation::note | Automation::octave)) != 0) {
    prepareSectionAutomationManual();
    sectionAutomationManual.pads.note.store(note, std::memory_order_relaxed);
    sectionAutomationManual.pads.octave.store(octave, std::memory_order_relaxed);
    sectionAutomationManual.pads.mask.fetch_or(Automation::note | Automation::octave, std::memory_order_release);
  } else {
    padsBaseNote.store(note, std::memory_order_relaxed);
    padsBaseOctave.store(octave, std::memory_order_relaxed);
  }
}

void MTControlsAudioProcessor::triggerPadNote(const int noteIndex, const int octave) {
  if (!juce::isPositiveAndBelow(noteIndex, PadsEngine::kNoteCount)) return;
  if (!isSectionAutomationEditing())
    padsAwaitingManualAfterAutomation.store(false, std::memory_order_release);
  setPadNoteAutomationTarget(noteIndex, juce::jlimit(-3, 3, octave));
  triggerPadNoteInternal(noteIndex, octave);
}

void MTControlsAudioProcessor::triggerPadNoteInternal(const int noteIndex, const int octave) {
  const auto triggerStart = juce::Time::getHighResolutionTicks();
  if (!juce::isPositiveAndBelow(noteIndex, PadsEngine::kNoteCount))
    return;
  bool resident = false;
  if (padsPackLoaded.load(std::memory_order_acquire)) {
    const juce::SpinLock::ScopedLockType lock(padsStateLock);
    if (padsEngine.isNoteLoaded(noteIndex)) {
      pendingPadNote.store(-1, std::memory_order_release);
      padsEngine.setEnabled(isPadsEnabled());
      if (padsEngine.getActiveNoteIndex() != noteIndex || padsRenderedOctave.load(std::memory_order_relaxed) != octave)
        padsEngine.triggerNote(noteIndex, octave);
      padsRenderedOctave.store(octave, std::memory_order_relaxed);
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
  startPadsPreloadAsync(!blankSongLoaded.load(std::memory_order_relaxed));
}

void MTControlsAudioProcessor::triggerPadNoteForSongTransition(
    const int noteIndex, const int octave) {
  if (!juce::isPositiveAndBelow(noteIndex, PadsEngine::kNoteCount)) return;
  padsBaseNote.store(noteIndex, std::memory_order_relaxed);
  padsBaseOctave.store(juce::jlimit(-3, 3, octave), std::memory_order_relaxed);
  const int effectiveNote = getActivePadNoteIndex();
  const bool heldAutomatedNote = (getPadsAutomationMask() & playworship::audio::SectionAutomation::note) != 0 &&
      padsEngine.getActiveNoteIndex() < 0 && !isSongPlaying() && !isSectionAutomationEditing();
  if (effectiveNote >= 0 && !heldAutomatedNote) triggerPadNoteInternal(effectiveNote, getActivePadOctave());
}

void MTControlsAudioProcessor::stopPadNote(const int noteIndex) {
  if (getActivePadNoteIndex() == noteIndex) setPadNoteAutomationTarget(-1, getActivePadOctave());
  const juce::SpinLock::ScopedLockType lock(padsStateLock);
  int pendingNote = noteIndex;
  pendingPadNote.compare_exchange_strong(pendingNote, -1,
                                         std::memory_order_acq_rel);
  padsEngine.stopNote(noteIndex);
}

int MTControlsAudioProcessor::getActivePadNoteIndex() const noexcept {
  using Automation = playworship::audio::SectionAutomation;
  if (!isPadsEnabled()) return -1;
  if ((getPadsAutomationMask() & Automation::note) == 0) return getBasePadNoteIndex();
  if (isSectionAutomationEditing()) return sectionAutomationDraft.pads.note.load();
  if (sectionAutomationManualToken.load(std::memory_order_acquire) == sectionAutomationCurrentToken.load(std::memory_order_acquire) &&
      (sectionAutomationManual.pads.mask.load(std::memory_order_acquire) & Automation::note) != 0)
    return sectionAutomationManual.pads.note.load();
  return sectionAutomationActive.pads.note.load();
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
  resetSectionAutomationState();
  clearPracticeLoop();
  transportControlRevision.fetch_add(1, std::memory_order_release);
  blankSongLoaded.store(false, std::memory_order_relaxed);
  loadedSongLengthSeconds = 0.0;
  loadedSongBpm = 120.0;
  playbackTempoBpm.store(120.0, std::memory_order_relaxed);
  playbackTempoRatioActive = 1.0;
  tempoTransitionStartRatio = 1.0;
  tempoTransitionTargetRatio = 1.0;
  tempoTransitionElapsedFrames = 0;
  tempoTransitionTotalFrames = 0;
  {
    const juce::ScopedWriteLock metaLock(songMetaLock);
    loadedSongKey.clear();
    loadedSongTitle.clear();
  }
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
  globalSamplePosition.store(0, std::memory_order_relaxed);
  requestedSeekPosition.store(-1, std::memory_order_relaxed);
  seekTargetSeconds.store(0.0, std::memory_order_relaxed);
  seekRequestGeneration.fetch_add(1, std::memory_order_release);
  songLoaded.store(false, std::memory_order_relaxed);
  arrangementParts.clear();
  arrangementCues.clear();
  sectionScheduler.clear();
  transportClock.reset(0);
  deferredArrangementTransitionPending = false;
  deferredArrangementTransitionPartIndex = -1;
  deferredArrangementTransitionFrame = 0;
  deferredArrangementTransitionSourceFrame = 0;
  arrangementCustomActive.store(false, std::memory_order_relaxed);
  arrangementCurrentPartIndex.store(0, std::memory_order_relaxed);
  setArrangementJumpCommand(-1, false);
  arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
  arrangementTotalLengthSeconds.store(0.0, std::memory_order_relaxed);
  dynamicGuideResetGeneration.fetch_add(1, std::memory_order_release);
  pitchTargetSemiTones.store(0, std::memory_order_relaxed);
  fadeActive.store(false, std::memory_order_relaxed);
  fadeTarget.store(1.0f, std::memory_order_relaxed);
  fadeGain = 1.0f;
  songLoadTransitionGain.store(1.0f, std::memory_order_relaxed);
  songLoadTransitionState.store(
      static_cast<int>(SongLoadTransitionState::idle),
      std::memory_order_release);
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
  setPracticeListeningMode(-1, 0);
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
  std::vector<playworship::audio::SectionScheduler::Cue> cues;
  cues.reserve(arrangementCues.size());
  for (auto &cue : arrangementCues) {
    cue.guideTriggerId = arrangementCueTriggerId(dynamicGuideEngine, cue.name);
    cues.push_back({playworship::audio::TransportClock::secondsToFrames(cue.positionSec, sampleRate),
                    cue.guideTriggerId, cue.skipPlayback});
  }
  sectionScheduler.publish(sections, cues);
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



void MTControlsAudioProcessor::getRemoteMetronomeMeterLevels(float &left, float &right) noexcept {
  left = remoteMetronomeMeterLeft.exchange(0.0f, std::memory_order_relaxed);
  right = remoteMetronomeMeterRight.exchange(0.0f, std::memory_order_relaxed);
}
