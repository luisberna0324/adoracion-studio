#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "data/AppSettings.h"
#include "data/AssetBootstrapManager.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
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
  for (auto &value : midiLastCcValues)
    value = -1;
}

MTControlsAudioProcessor::~MTControlsAudioProcessor() {
}

void MTControlsAudioProcessor::prepareToPlay(double sampleRate,
                                             int samplesPerBlock) {
  juce::dsp::ProcessSpec spec;
  spec.sampleRate = sampleRate;
  spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
  spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());
  gain.prepare(spec);
  gain.setRampDurationSeconds(0.02);
  dryMixBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock);
  mixEngine.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
  outgoingMixEngine.prepare(sampleRate, samplesPerBlock,
                            getTotalNumOutputChannels());
  stemRoutingTempBuffer.setSize(2, samplesPerBlock);
  metronomeRenderBuffer.setSize(2, samplesPerBlock);
  padsRenderBuffer.setSize(2, samplesPerBlock);
  outgoingStemRenderBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock);
  padsEngine.prepare(sampleRate, samplesPerBlock);
  padsEngine.setEnabled(padsEnabledState.load(std::memory_order_relaxed));
  standaloneAudioDeviceBridge.setAvailableOutputChannelCount(getTotalNumOutputChannels());
  refreshPadsRoutingFromSettings();
  stemOutputRoutingState.normalizeForDevice(getTotalNumOutputChannels());
  setMetronomeOutputRouting(
      metronomeOutputChannel.load(std::memory_order_relaxed),
      metronomeOutputWidth.load(std::memory_order_relaxed));
  for (int i = 0; i < maxStemCount; ++i) {
    const int channel = stemOutputRoutingState.getStemOutputChannel(i);
    stemOutputChannels[static_cast<size_t>(i)].store(channel, std::memory_order_relaxed);
    const int width = stemOutputRoutingState.getStemOutputWidth(i);
    stemOutputWidths[static_cast<size_t>(i)].store(width, std::memory_order_relaxed);
    stemPitchProcessors[static_cast<size_t>(i)].prepare(sampleRate, samplesPerBlock, 2);
    stemPitchProcessors[static_cast<size_t>(i)].setPitchSemiTones(pitchActiveSemiTones);
    stemPitchProcessors[static_cast<size_t>(i)].setTempoRatio(playbackTempoRatioActive);
    stemPitchDelayLines[static_cast<size_t>(i)].prepare(2, kMaxRoutingPitchLatencySamples);
    stemPitchDelayLines[static_cast<size_t>(i)].setDelay(0);
    outgoingStemPitchProcessors[static_cast<size_t>(i)].prepare(sampleRate, samplesPerBlock, 2);
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
    metronome.prepare(sampleRate);
    mixerSource.prepareToPlay(samplesPerBlock, sampleRate);
    outgoingMixerSource.prepareToPlay(samplesPerBlock, sampleRate);
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
  const juce::SpinLock::ScopedLockType lock(stemListLock);
  mixerSource.releaseResources();
  outgoingMixerSource.releaseResources();
  for (auto &track : stemTracks)
    track->releaseResources();
  for (auto &track : outgoingStemTracks)
    track->releaseResources();
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
  juce::ScopedNoDenormals noDenormals;
  processMidiMapping(midiMessages);

  // --- Diagnostics: overall block start time ---
  const int64_t blockStartTick = juce::Time::getHighResolutionTicks();

  for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels();
       ++i)
    buffer.clear(i, 0, buffer.getNumSamples());

  bool usedSeparatedMix = false;
  float windowFadeStart = 1.0f;
  float windowFadeEnd = 1.0f;
  bool windowFadeActive = false;
  bool stopAtEnd = false;
  double stopAtEndSec = 0.0;
  bool transitionToNextArrangementPart = false;
  int nextArrangementPartIndex = -1;
  double nextArrangementPartStartSec = 0.0;

  if (songLoaded.load(std::memory_order_relaxed)) {
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const bool isPlaying = getTransportState() == TransportState::playing;
    const int targetPitch =
        pitchTargetSemiTones.load(std::memory_order_relaxed);
    const double targetTempoRatio = getPlaybackTempoRatio();
    if (targetPitch != pitchActiveSemiTones) {
      pitchActiveSemiTones = targetPitch;
      mixEngine.setPitchSemiTones(pitchActiveSemiTones);
      outgoingMixEngine.setPitchSemiTones(pitchActiveSemiTones);
      routingPitchLatencySamples = 0;
      outgoingRoutingPitchLatencySamples = 0;
    }
    if (!juce::approximatelyEqual(targetTempoRatio, playbackTempoRatioActive)) {
      playbackTempoRatioActive = targetTempoRatio;
      mixEngine.setTempoRatio(playbackTempoRatioActive);
      outgoingMixEngine.setTempoRatio(playbackTempoRatioActive);
      routingPitchLatencySamples = 0;
      outgoingRoutingPitchLatencySamples = 0;
    }

    // --- Diagnostics: measure time until lock is acquired ---
    const int64_t lockStartTick = juce::Time::getHighResolutionTicks();

    // SpinLock only guards the brief period where we read the stems vector.
    // It does NOT block the audio thread for the actual disk reads below.
    const juce::SpinLock::ScopedLockType lock(stemListLock);

    const double lockWaitMs =
        juce::Time::highResolutionTicksToSeconds(
            juce::Time::getHighResolutionTicks() - lockStartTick) * 1000.0;

    // Apply pending seek
    if (seekPending.exchange(false, std::memory_order_acquire)) {
      const double seekTarget = seekTargetSeconds.load(std::memory_order_relaxed);
      const juce::int64 seekSamples = static_cast<juce::int64>(seekTarget * playbackSampleRate);
      for (auto& track : stemTracks)
        track->setNextReadPosition(seekSamples);
      globalSamplePosition.store(seekSamples, std::memory_order_relaxed);
      songCurrentPositionSeconds.store(seekTarget, std::memory_order_relaxed);

      metronome.syncPhaseForTimeSeconds(seekTarget);
    }

    const int stemCount = static_cast<int>(stemTracks.size());

    const int clickStemIndexLocal =
        metronomeClickStemIndex.load(std::memory_order_relaxed);
    const bool hasRealClickStem = juce::isPositiveAndBelow(clickStemIndexLocal,
                                                           stemCount);
    const bool virtualMetronomeSoloActive =
        !hasRealClickStem &&
        metronomeSoloState.load(std::memory_order_relaxed);

    // Evaluate global solo state (includes virtual metronome solo when no real
    // click stem exists so stems mute and only click remains audible).
    bool anySoloActive = virtualMetronomeSoloActive;
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
      track->setMuted(stemMuteStates[static_cast<size_t>(i)].load(std::memory_order_relaxed));
      track->setSoloed(stemSoloStates[static_cast<size_t>(i)].load(std::memory_order_relaxed));
      track->setVolume(juce::jlimit(0.0f, 1.0f, stemVolumes[static_cast<size_t>(i)].load(std::memory_order_relaxed)));
      track->setTrim(juce::jlimit(0.0f, 2.0f, stemGains[static_cast<size_t>(i)].load(std::memory_order_relaxed)));
      track->setGlobalMixState(mixGain, anySoloActive);
      
      // Update UI meter level (computed in the PREVIOUS getNextAudioBlock call)
      stemLevels[static_cast<size_t>(i)].store(isPlaying ? track->getMagnitude() : 0.0f,
                                               std::memory_order_relaxed);
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
      const double length =
          loadedSongLengthSeconds.load(std::memory_order_relaxed);
      const bool customArrangement =
          arrangementCustomActive.load(std::memory_order_relaxed);
      double windowStartSec = songStartSeconds.load(std::memory_order_relaxed);
      double windowEndSec =
          songEndSeconds.load(std::memory_order_relaxed);
      if (windowEndSec <= 0.0 || windowEndSec > length)
        windowEndSec = length;
      double fadeOutSec = songFadeOutSeconds.load(std::memory_order_relaxed);
      const double blockStartSec =
          playbackSampleRate > 0.0
              ? static_cast<double>(
                    globalSamplePosition.load(std::memory_order_relaxed)) /
                    playbackSampleRate
              : 0.0;
      const double blockEndSec =
          playbackSampleRate > 0.0
              ? blockStartSec +
                    (static_cast<double>(numSamples) / playbackSampleRate)
              : blockStartSec;

      int mappedPartIndex = -1;
      if (!arrangementParts.empty()) {
        mappedPartIndex = findArrangementPartIndexForSourceTime(blockStartSec);
        if (juce::isPositiveAndBelow(mappedPartIndex,
                                     static_cast<int>(arrangementParts.size()))) {
          arrangementCurrentPartIndex.store(mappedPartIndex,
                                            std::memory_order_relaxed);
        }
      }
      const int loopPartIndex =
          arrangementLoopPartIndex.load(std::memory_order_relaxed);
      const bool loopPartActive =
          juce::isPositiveAndBelow(loopPartIndex,
                                   static_cast<int>(arrangementParts.size()));

      bool loopPartEngaged = false;
      if (loopPartActive) {
        const auto &loopPart = arrangementParts[static_cast<size_t>(loopPartIndex)];
        loopPartEngaged =
            blockStartSec >= loopPart.startSec && blockStartSec < loopPart.endSec;
      }

      if (loopPartEngaged) {
        const auto &loopPart = arrangementParts[static_cast<size_t>(loopPartIndex)];
        windowStartSec = loopPart.startSec;
        windowEndSec = loopPart.endSec;
        fadeOutSec = 0.0;
        arrangementCurrentPartIndex.store(loopPartIndex, std::memory_order_relaxed);
        arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);

        if (windowEndSec > 0.0 && blockEndSec >= windowEndSec) {
          transitionToNextArrangementPart = true;
          nextArrangementPartIndex = loopPartIndex;
          nextArrangementPartStartSec = loopPart.startSec;
        }
      } else if (customArrangement && !arrangementParts.empty() &&
                 juce::isPositiveAndBelow(mappedPartIndex,
                                          static_cast<int>(arrangementParts.size()))) {
        const int partIndex = mappedPartIndex;
        const auto &part = arrangementParts[static_cast<size_t>(partIndex)];
        windowStartSec = part.startSec;
        windowEndSec = part.endSec;
        fadeOutSec = 0.0;

        if (windowEndSec > 0.0 && blockEndSec >= windowEndSec) {
          const int pendingIndex =
              arrangementPendingJumpPartIndex.load(std::memory_order_relaxed);
          if (juce::isPositiveAndBelow(pendingIndex,
                                       static_cast<int>(arrangementParts.size())) &&
              pendingIndex != partIndex) {
            transitionToNextArrangementPart = true;
            nextArrangementPartIndex = pendingIndex;
            nextArrangementPartStartSec =
                arrangementParts[static_cast<size_t>(pendingIndex)].startSec;
            arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
          } else {
            const int nextIndex = partIndex + 1;
            if (juce::isPositiveAndBelow(
                    nextIndex, static_cast<int>(arrangementParts.size()))) {
              transitionToNextArrangementPart = true;
              nextArrangementPartIndex = nextIndex;
              nextArrangementPartStartSec =
                  arrangementParts[static_cast<size_t>(nextIndex)].startSec;
            } else {
              stopAtEnd = true;
              stopAtEndSec = windowEndSec;
            }
          }
        }
      } else if (!customArrangement && !arrangementParts.empty() &&
                 juce::isPositiveAndBelow(mappedPartIndex,
                                          static_cast<int>(arrangementParts.size()))) {
        const auto &part = arrangementParts[static_cast<size_t>(mappedPartIndex)];
        const int pendingIndex =
            arrangementPendingJumpPartIndex.load(std::memory_order_relaxed);
        if (juce::isPositiveAndBelow(pendingIndex,
                                     static_cast<int>(arrangementParts.size())) &&
            pendingIndex != mappedPartIndex && blockEndSec >= part.endSec) {
          transitionToNextArrangementPart = true;
          nextArrangementPartIndex = pendingIndex;
          nextArrangementPartStartSec =
              arrangementParts[static_cast<size_t>(pendingIndex)].startSec;
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

      if (!customArrangement && windowEndSec > 0.0 && blockEndSec >= windowEndSec) {
        stopAtEnd = true;
        stopAtEndSec = windowEndSec;
      }

      if (routingActive) {
        const bool timeProcessActive =
            (pitchActiveSemiTones != 0) ||
            !juce::approximatelyEqual(playbackTempoRatioActive, 1.0);
        int targetLatency = 0;
        if (timeProcessActive) {
          for (int i = 0; i < stemCount; ++i) {
            const bool pitchShiftable =
                stemPitchShiftStates[static_cast<size_t>(i)].load(
                    std::memory_order_relaxed);
            if (!pitchShiftable)
              continue;
            auto &proc = stemPitchProcessors[static_cast<size_t>(i)];
            if (!proc.isPrepared() || proc.getNumChannels() != 2)
              proc.prepare(playbackSampleRate, numSamples, 2);
            proc.setPitchSemiTones(pitchActiveSemiTones);
            proc.setTempoRatio(playbackTempoRatioActive);
            targetLatency = proc.getLatencySamples();
            break;
          }
        }

        if (targetLatency != routingPitchLatencySamples) {
          routingPitchLatencySamples = targetLatency;
          for (auto &delay : stemPitchDelayLines)
            delay.setDelay(targetLatency);
        }

        if (stemRoutingTempBuffer.getNumSamples() < numSamples)
          stemRoutingTempBuffer.setSize(2, numSamples, false, false, true);

        buffer.clear();
        for (int i = 0; i < stemCount; ++i) {
          stemRoutingTempBuffer.clear();
          juce::AudioSourceChannelInfo stemInfo(&stemRoutingTempBuffer, 0,
                                                numSamples);
          stemTracks[static_cast<size_t>(i)]->getNextAudioBlock(stemInfo);
          const bool pitchShiftable =
              stemPitchShiftStates[static_cast<size_t>(i)].load(
                  std::memory_order_relaxed);
          if (timeProcessActive && pitchShiftable) {
            auto &proc = stemPitchProcessors[static_cast<size_t>(i)];
            if (!proc.isPrepared() || proc.getNumChannels() != 2)
              proc.prepare(playbackSampleRate, numSamples, 2);
            proc.setPitchSemiTones(pitchActiveSemiTones);
            proc.setTempoRatio(playbackTempoRatioActive);
            proc.process(stemRoutingTempBuffer);
          } else if (routingPitchLatencySamples > 0) {
            stemPitchDelayLines[static_cast<size_t>(i)].process(
                stemRoutingTempBuffer);
          }
          int assignedWidth = juce::jlimit(
              1, 2, stemOutputWidths[static_cast<size_t>(i)].load(
                        std::memory_order_relaxed));
          if (numChannels < 2)
            assignedWidth = 1;

          const int maxBaseChannel = juce::jmax(
              1, numChannels - (assignedWidth >= 2 ? 1 : 0));
          const int assignedChannelOneBased = juce::jlimit(
              1, maxBaseChannel,
              stemOutputChannels[static_cast<size_t>(i)].load(
                  std::memory_order_relaxed));
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
                             stemPitchShiftStates.data(), stemCount, numSamples,
                             numChannels);
        } else {
          // MixerAudioSource will call getNextAudioBlock on all StemTracks, apply
          // their internal volumes, and sum into `buffer`.
          juce::AudioSourceChannelInfo info(&buffer, 0, numSamples);
          mixerSource.getNextAudioBlock(info);
        }
      }

      const double currentSec =
          playbackSampleRate > 0.0
              ? static_cast<double>(
                    globalSamplePosition.load(std::memory_order_relaxed)) /
                    playbackSampleRate
              : 0.0;
      songCurrentPositionSeconds.store(currentSec, std::memory_order_relaxed);
      globalSamplePosition.fetch_add(numSamples, std::memory_order_relaxed);
    } else {
      const auto state = getTransportState();
      if (state == TransportState::paused) {
        const double currentSec =
            playbackSampleRate > 0.0
                ? static_cast<double>(globalSamplePosition.load(std::memory_order_relaxed)) /
                      playbackSampleRate
                : 0.0;
        songCurrentPositionSeconds.store(currentSec, std::memory_order_relaxed);
      } else {
        const double currentSec =
            playbackSampleRate > 0.0
                ? static_cast<double>(globalSamplePosition.load(std::memory_order_relaxed)) /
                      playbackSampleRate
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
      diagnostics.pushEvent({juce::Time::currentTimeMillis() * 1.0, blockMs,
                             budgetMs, lockWaitMs, stemCount,
                             blockMs > budgetMs, isPlaying});
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
    mixEngine.renderToOutput(buffer, timeProcessActive, fadeStart, fadeEnd,
                             windowFadeStart, windowFadeEnd);
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
    {
      const juce::SpinLock::ScopedLockType lock(stemListLock);
      if (!outgoingStemTracks.empty()) {
        if (outgoingStemRenderBuffer.getNumSamples() < numSamples ||
            outgoingStemRenderBuffer.getNumChannels() < numChannels) {
          outgoingStemRenderBuffer.setSize(numChannels, numSamples, false, false,
                                           true);
        }
        outgoingStemRenderBuffer.clear();
        juce::AudioSourceChannelInfo info(&outgoingStemRenderBuffer, 0, numSamples);
        outgoingMixerSource.getNextAudioBlock(info);
        renderedOutgoing = true;
      }
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
      const juce::SpinLock::ScopedLockType lock(stemListLock);
      outgoingMixerSource.removeAllInputs();
      outgoingStemTracks.clear();
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

  if (isMetronomeEnabled()) {
    if (metronomeRenderBuffer.getNumSamples() < numSamples)
      metronomeRenderBuffer.setSize(2, numSamples, false, false, true);
    metronomeRenderBuffer.clear();

    const int activeStemCount = loadedStemCount.load(std::memory_order_relaxed);
    const int clickStemIndex =
        metronomeClickStemIndex.load(std::memory_order_relaxed);
    const bool hasRealClickStem =
        juce::isPositiveAndBelow(clickStemIndex, activeStemCount) &&
        juce::isPositiveAndBelow(clickStemIndex, maxStemCount);

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
    int outputChannelOneBased =
        metronomeOutputChannel.load(std::memory_order_relaxed);
    int outputWidth = metronomeOutputWidth.load(std::memory_order_relaxed);

    if (hasRealClickStem) {
      const auto idx = static_cast<size_t>(clickStemIndex);
      effectiveVolume = stemVolumes[idx].load(std::memory_order_relaxed);
      effectiveGain = stemGains[idx].load(std::memory_order_relaxed);
      muted = stemMuteStates[idx].load(std::memory_order_relaxed);
      soloed = stemSoloStates[idx].load(std::memory_order_relaxed);
      outputChannelOneBased =
          stemOutputChannels[idx].load(std::memory_order_relaxed);
      outputWidth = stemOutputWidths[idx].load(std::memory_order_relaxed);
    } else if (soloed) {
      // Virtual click solo behaves like a regular solo source.
      anySoloActive = true;
    }

    effectiveVolume = juce::jlimit(0.0f, 1.0f, effectiveVolume);
    effectiveGain = juce::jlimit(0.0f, 2.0f, effectiveGain);

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
    metronome.process(metronomeRenderBuffer);
    if (!juce::approximatelyEqual(effectiveGain, 1.0f))
      metronomeRenderBuffer.applyGain(effectiveGain);

    const int outputIndex = juce::jlimit(
        0, juce::jmax(0, numChannels - 1), outputChannelOneBased - 1);
    stemOutputRouter.routeStemToOutputChannels(metronomeRenderBuffer,
                                               outputIndex, outputWidth,
                                               buffer);
  }

  if (padsPackLoaded.load(std::memory_order_relaxed)) {
    if (padsRenderBuffer.getNumSamples() < numSamples ||
        padsRenderBuffer.getNumChannels() < numChannels) {
      padsRenderBuffer.setSize(juce::jmax(2, numChannels), numSamples, false, false, true);
    }
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
    if (playbackSampleRate > 0.0) {
      const juce::int64 nextSamples =
          static_cast<juce::int64>(nextArrangementPartStartSec *
                                   playbackSampleRate);
      globalSamplePosition.store(nextSamples, std::memory_order_relaxed);
    }
    metronome.syncPhaseForTimeSeconds(nextArrangementPartStartSec);
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

bool MTControlsAudioProcessor::isMidiTargetValid(const int targetId) noexcept {
  if (targetId >= midiTargetPadsNoteBase &&
      targetId < midiTargetPadsNoteBase + 12)
    return true;
  if (targetId >= midiTargetStemVolumeBase &&
      targetId < midiTargetStemVolumeBase + 8)
    return true;
  return targetId >= midiTargetMasterVolume &&
         targetId <= midiTargetPadsVolume;
}

MidiBindingMode
MTControlsAudioProcessor::defaultModeForTarget(const int targetId) noexcept {
  if (targetId == midiTargetMasterVolume || targetId == midiTargetMetronomeVolume ||
      targetId == midiTargetPadsVolume ||
      (targetId >= midiTargetStemVolumeBase &&
       targetId < midiTargetStemVolumeBase + 8)) {
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
  case midiTargetPadsPower:
    return "Pads Power";
  case midiTargetPadsVolume:
    return "Pads Volume";
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
      targetId < midiTargetStemVolumeBase + 8) {
    const int stemOneBased = targetId - midiTargetStemVolumeBase + 1;
    return "Stem " + juce::String(stemOneBased) + " Volume";
  }
  return {};
}

std::vector<MTControlsAudioProcessor::MidiTargetDescriptor>
MTControlsAudioProcessor::getMidiTargets() const {
  std::vector<MidiTargetDescriptor> out;
  out.push_back({midiTargetMasterVolume, midiTargetNameForId(midiTargetMasterVolume),
                 defaultModeForTarget(midiTargetMasterVolume)});
  out.push_back({midiTargetMasterMute, midiTargetNameForId(midiTargetMasterMute),
                 defaultModeForTarget(midiTargetMasterMute)});
  out.push_back(
      {midiTargetTransportPlayPause, midiTargetNameForId(midiTargetTransportPlayPause),
       defaultModeForTarget(midiTargetTransportPlayPause)});
  out.push_back({midiTargetTransportStop, midiTargetNameForId(midiTargetTransportStop),
                 defaultModeForTarget(midiTargetTransportStop)});
  out.push_back(
      {midiTargetMetronomeEnabled, midiTargetNameForId(midiTargetMetronomeEnabled),
       defaultModeForTarget(midiTargetMetronomeEnabled)});
  out.push_back(
      {midiTargetMetronomeVolume, midiTargetNameForId(midiTargetMetronomeVolume),
       defaultModeForTarget(midiTargetMetronomeVolume)});
  out.push_back({midiTargetMetronomeMute, midiTargetNameForId(midiTargetMetronomeMute),
                 defaultModeForTarget(midiTargetMetronomeMute)});
  out.push_back(
      {midiTargetTransportPrevSong, midiTargetNameForId(midiTargetTransportPrevSong),
       defaultModeForTarget(midiTargetTransportPrevSong)});
  out.push_back(
      {midiTargetTransportSeekBack, midiTargetNameForId(midiTargetTransportSeekBack),
       defaultModeForTarget(midiTargetTransportSeekBack)});
  out.push_back(
      {midiTargetTransportSeekForward,
       midiTargetNameForId(midiTargetTransportSeekForward),
       defaultModeForTarget(midiTargetTransportSeekForward)});
  out.push_back(
      {midiTargetTransportNextSong, midiTargetNameForId(midiTargetTransportNextSong),
       defaultModeForTarget(midiTargetTransportNextSong)});
  out.push_back(
      {midiTargetTransportFadeToggle,
       midiTargetNameForId(midiTargetTransportFadeToggle),
       defaultModeForTarget(midiTargetTransportFadeToggle)});
  out.push_back(
      {midiTargetPadsPower, midiTargetNameForId(midiTargetPadsPower),
       defaultModeForTarget(midiTargetPadsPower)});
  out.push_back(
      {midiTargetPadsVolume, midiTargetNameForId(midiTargetPadsVolume),
       defaultModeForTarget(midiTargetPadsVolume)});
  for (int i = 0; i < 12; ++i) {
    const int targetId = midiTargetPadsNoteBase + i;
    out.push_back(
        {targetId, midiTargetNameForId(targetId), defaultModeForTarget(targetId)});
  }
  for (int i = 0; i < 8; ++i) {
    const int targetId = midiTargetStemVolumeBase + i;
    out.push_back(
        {targetId, midiTargetNameForId(targetId), defaultModeForTarget(targetId)});
  }
  return out;
}

std::vector<MTControlsAudioProcessor::MidiBinding>
MTControlsAudioProcessor::getMidiBindings() const {
  const juce::ScopedLock lock(midiBindingsLock);
  return midiBindings;
}

void MTControlsAudioProcessor::setMidiBinding(const MidiBinding &binding) {
  if (!isMidiTargetValid(binding.targetId))
    return;
  const int safeChannel = juce::jlimit(1, 16, binding.midiChannel);
  const int safeCc = juce::jlimit(0, 127, binding.ccNumber);

  const juce::ScopedLock lock(midiBindingsLock);
  midiBindings.erase(
      std::remove_if(midiBindings.begin(), midiBindings.end(),
                     [&binding](const MidiBinding &current) {
                       return current.targetId == binding.targetId ||
                              (current.midiChannel == binding.midiChannel &&
                               current.ccNumber == binding.ccNumber);
                     }),
      midiBindings.end());
  MidiBinding next = binding;
  next.midiChannel = safeChannel;
  next.ccNumber = safeCc;
  midiBindings.push_back(next);
}

void MTControlsAudioProcessor::clearMidiBindingForTarget(const int targetId) {
  const juce::ScopedLock lock(midiBindingsLock);
  midiBindings.erase(
      std::remove_if(midiBindings.begin(), midiBindings.end(),
                     [targetId](const MidiBinding &current) {
                       return current.targetId == targetId;
                     }),
      midiBindings.end());
}

void MTControlsAudioProcessor::beginMidiLearnForTarget(const int targetId) {
  midiLearnTargetId.store(isMidiTargetValid(targetId) ? targetId : -1,
                          std::memory_order_release);
}

bool MTControlsAudioProcessor::consumePendingMidiLearnBinding(
    MidiBinding &outBinding) {
  const int targetId = midiLearnPendingTargetId.exchange(-1, std::memory_order_acq_rel);
  if (targetId < 0)
    return false;
  outBinding.targetId = targetId;
  outBinding.midiChannel =
      juce::jlimit(1, 16, midiLearnPendingChannel.load(std::memory_order_relaxed));
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
  const float normalized =
      juce::jlimit(0.0f, 1.0f, static_cast<float>(ccValue) / 127.0f);
  const bool high = ccValue >= 64;

  if (targetId == midiTargetMasterVolume) {
    setMasterVolume(normalized);
    return;
  }
  if (targetId == midiTargetMetronomeVolume) {
    setMetronomeVolume(normalized);
    return;
  }
  if (targetId == midiTargetPadsVolume) {
    setPadsVolume(normalized);
    return;
  }
  if (targetId >= midiTargetStemVolumeBase &&
      targetId < midiTargetStemVolumeBase + 8) {
    const int stemIndex = targetId - midiTargetStemVolumeBase;
    setStemVolume(stemIndex, normalized);
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
    else if (targetId == midiTargetMetronomeMute)
      setMetronomeMuted(high);
    else if (targetId == midiTargetPadsPower)
      setPadsEnabled(high);
    else if (targetId == midiTargetTransportStop && high)
      stopSong();
    return;
  }

  if (targetId == midiTargetMasterMute)
    setMasterMute(!isMasterMuted());
  else if (targetId == midiTargetMetronomeEnabled)
    setMetronomeEnabled(!isMetronomeEnabled());
  else if (targetId == midiTargetMetronomeMute)
    setMetronomeMuted(!isMetronomeMuted());
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
  } else if (targetId == midiTargetTransportPrevSong) {
    midiPendingUiCommand.store(midiUiCommandPrevSong, std::memory_order_release);
  } else if (targetId == midiTargetTransportNextSong) {
    midiPendingUiCommand.store(midiUiCommandNextSong, std::memory_order_release);
  }
}

void MTControlsAudioProcessor::processMidiMapping(
    const juce::MidiBuffer &midiMessages) {
  if (midiMessages.isEmpty())
    return;

  std::vector<MidiBinding> currentBindings;
  {
    const juce::ScopedLock lock(midiBindingsLock);
    currentBindings = midiBindings;
  }

  for (const auto metadata : midiMessages) {
    const auto msg = metadata.getMessage();
    if (!msg.isController())
      continue;
    const int channel = msg.getChannel();
    const int cc = msg.getControllerNumber();
    const int value = msg.getControllerValue();

    const int learnTarget = midiLearnTargetId.exchange(-1, std::memory_order_acq_rel);
    if (isMidiTargetValid(learnTarget)) {
      MidiBinding learned;
      learned.targetId = learnTarget;
      learned.midiChannel = channel;
      learned.ccNumber = cc;
      learned.mode = defaultModeForTarget(learnTarget);
      setMidiBinding(learned);
      midiLearnPendingChannel.store(channel, std::memory_order_relaxed);
      midiLearnPendingCc.store(cc, std::memory_order_relaxed);
      midiLearnPendingMode.store(static_cast<int>(learned.mode),
                                 std::memory_order_relaxed);
      midiLearnPendingTargetId.store(learnTarget, std::memory_order_release);
      currentBindings = getMidiBindings();
    }

    for (const auto &binding : currentBindings) {
      if (binding.midiChannel != channel || binding.ccNumber != cc)
        continue;

      const int index = binding.targetId;
      const int safeIndex = juce::jlimit(0, static_cast<int>(midiLastCcValues.size()) - 1,
                                         index);
      const int previous = midiLastCcValues[static_cast<size_t>(safeIndex)];
      midiLastCcValues[static_cast<size_t>(safeIndex)] = value;
      if (binding.mode != MidiBindingMode::Absolute) {
        const bool prevHigh = previous >= 64;
        const bool nowHigh = value >= 64;
        if (!prevHigh && nowHigh)
          applyMidiBindingValue(binding.targetId, value, binding.mode);
        else if (binding.mode == MidiBindingMode::Momentary && prevHigh && !nowHigh)
          applyMidiBindingValue(binding.targetId, value, binding.mode);
      } else {
        applyMidiBindingValue(binding.targetId, value, binding.mode);
      }
    }
  }
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

    auto track = std::make_unique<StemTrack>(
        std::move(reader), sampleRate, 262144);
        
    track->prepareToPlay(localBlockSize, localSampleRate);
    track->setNextReadPosition(0);
    newTracks.push_back(std::move(track));
  }

  if (newTracks.empty())
    return false;

  if (onLoadProgress)
    onLoadProgress(1.0);

  // Briefly acquire the lock to swap in the new stem list.
  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    const bool canCrossfadeDecks =
        wasPlayingBeforeLoad && songLoaded.load(std::memory_order_relaxed) &&
        !stemTracks.empty();

    if (canCrossfadeDecks) {
      outgoingMixerSource.removeAllInputs();
      outgoingStemTracks = std::move(stemTracks);
      for (auto &track : outgoingStemTracks)
        outgoingMixerSource.addInputSource(track.get(), false);
      outgoingMixerSource.prepareToPlay(localBlockSize, localSampleRate);
      outgoingStemRenderBuffer.setSize(getTotalNumOutputChannels(), localBlockSize, false, false, true);
      songCrossfadeActive.store(true, std::memory_order_relaxed);
      songCrossfadeProgress.store(0.0f, std::memory_order_relaxed);
      songCrossfadeIncomingGain.store(0.0f, std::memory_order_relaxed);
      songCrossfadeOutgoingGain.store(1.0f, std::memory_order_relaxed);
    } else {
      outgoingMixerSource.removeAllInputs();
      outgoingStemTracks.clear();
      songCrossfadeActive.store(false, std::memory_order_relaxed);
      songCrossfadeProgress.store(1.0f, std::memory_order_relaxed);
      songCrossfadeIncomingGain.store(1.0f, std::memory_order_relaxed);
      songCrossfadeOutgoingGain.store(0.0f, std::memory_order_relaxed);
    }
    removeAllStemSources();
    clearLoadedSongState();

    for (auto& track : newTracks) {
      mixerSource.addInputSource(track.get(), false);
    }
    mixerSource.prepareToPlay(localBlockSize, localSampleRate);

    stemTracks = std::move(newTracks);
    resetStemControls(static_cast<int>(stemTracks.size()));
    
    globalSamplePosition.store(0, std::memory_order_relaxed);
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
    loadedSongBpm = juce::jlimit(20.0, 300.0, normalizedSongBpm);
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
    arrangementParts.push_back({0.0, maxLengthSeconds});
    arrangementCurrentPartIndex.store(0, std::memory_order_relaxed);
    arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
    arrangementCustomActive.store(false, std::memory_order_relaxed);
    arrangementTotalLengthSeconds.store(maxLengthSeconds,
                                        std::memory_order_relaxed);
    pitchTargetSemiTones.store(0, std::memory_order_relaxed);
    setTransportState(wasPlayingBeforeLoad ? TransportState::playing
                                           : TransportState::stopped);
  }

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
    int partIndex = findArrangementPartIndexForSourceTime(current);
    if (!juce::isPositiveAndBelow(partIndex,
                                  static_cast<int>(arrangementParts.size()))) {
      partIndex = 0;
      arrangementCurrentPartIndex.store(partIndex, std::memory_order_relaxed);
      seekTargetSeconds.store(arrangementParts.front().startSec,
                              std::memory_order_relaxed);
      seekPending.store(true, std::memory_order_release);
    } else {
      arrangementCurrentPartIndex.store(partIndex, std::memory_order_relaxed);
      const auto &part = arrangementParts[static_cast<size_t>(partIndex)];
      if (current >= part.endSec || current < part.startSec) {
        seekTargetSeconds.store(part.startSec, std::memory_order_relaxed);
        seekPending.store(true, std::memory_order_release);
      }
    }
  }
  const double start = songStartSeconds.load(std::memory_order_relaxed);
  const double end = songEndSeconds.load(std::memory_order_relaxed);
  const double current =
      songCurrentPositionSeconds.load(std::memory_order_relaxed);
  if (current < start || (end > 0.0 && current >= end))
    seekSongTo(start);
  metronome.syncPhaseForTimeSeconds(
      songCurrentPositionSeconds.load(std::memory_order_relaxed));
  setTransportState(TransportState::playing);
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
  double start = songStartSeconds.load(std::memory_order_relaxed);
  double end = songEndSeconds.load(std::memory_order_relaxed);
  if (end <= 0.0 || end > length)
    end = length;
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
  double start = songStartSeconds.load(std::memory_order_relaxed);
  double end = songEndSeconds.load(std::memory_order_relaxed);
  if (end <= 0.0 || end > length)
    end = length;
  start = juce::jlimit(0.0, end, start);
  double target = juce::jlimit(start, end, positionSeconds);
  arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
  const int loopPartIndex =
      arrangementLoopPartIndex.load(std::memory_order_relaxed);
  const bool customArrangement =
      arrangementCustomActive.load(std::memory_order_relaxed);
  if (loopPartIndex >= 0 || customArrangement) {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    if (juce::isPositiveAndBelow(loopPartIndex,
                                 static_cast<int>(arrangementParts.size()))) {
      const auto &loopPart = arrangementParts[static_cast<size_t>(loopPartIndex)];
      arrangementCurrentPartIndex.store(loopPartIndex, std::memory_order_relaxed);
      target = juce::jlimit(loopPart.startSec, loopPart.endSec, target);
    } else if (customArrangement && !arrangementParts.empty()) {
      int partIndex = findArrangementPartIndexForSourceTime(target);
      if (!juce::isPositiveAndBelow(partIndex,
                                    static_cast<int>(arrangementParts.size())))
        partIndex = 0;
      const auto &part = arrangementParts[static_cast<size_t>(partIndex)];
      arrangementCurrentPartIndex.store(partIndex, std::memory_order_relaxed);
      if (target < part.startSec || target > part.endSec)
        target = part.startSec;
      else
        target = juce::jlimit(part.startSec, part.endSec, target);
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
  for (const auto &part : arrangement.parts) {
    double start = juce::jlimit(0.0, songLength, part.startSec);
    double end = juce::jlimit(0.0, songLength, part.endSec);
    if (end < start)
      std::swap(start, end);
    if (songLength > 0.0 && (end - start) < kRuntimeMinPartDurationSec)
      continue;
    next.push_back({start, end});
    totalLength += juce::jmax(0.0, end - start);
  }

  if (next.size() > 1) {
    std::stable_sort(
        next.begin(), next.end(),
        [](const ArrangementRuntimePart &a, const ArrangementRuntimePart &b) {
          if (std::abs(a.startSec - b.startSec) <= 0.0001)
            return a.endSec < b.endSec;
          return a.startSec < b.startSec;
        });
  }

  if (next.empty()) {
    next.push_back({0.0, songLength});
    totalLength = songLength;
    isCustomArrangement = false;
  }

  arrangementParts = std::move(next);
  arrangementTotalLengthSeconds.store(totalLength, std::memory_order_relaxed);
  arrangementCustomActive.store(isCustomArrangement, std::memory_order_relaxed);
  arrangementCurrentPartIndex.store(0, std::memory_order_relaxed);
  arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
  arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);

  if (isCustomArrangement) {
    const double target = arrangementParts.front().startSec;
    seekTargetSeconds.store(target, std::memory_order_relaxed);
    seekPending.store(true, std::memory_order_release);
    songCurrentPositionSeconds.store(target, std::memory_order_relaxed);
    if (playbackSampleRate > 0.0) {
      globalSamplePosition.store(
          static_cast<juce::int64>(target * playbackSampleRate),
          std::memory_order_relaxed);
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
    const int currentPartIndex = findArrangementPartIndexForSourceTime(currentSec);
    const int pendingPartIndex =
        arrangementPendingJumpPartIndex.load(std::memory_order_relaxed);

    if (playing) {
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

    arrangementLoopPartIndex.store(partIndex, std::memory_order_relaxed);
    arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
    // No hacemos seek ni play automático: el loop queda armado y entra
    // en efecto cuando el transporte llegue al rango de la sección.
  }
}

void MTControlsAudioProcessor::clearArrangementPartLoop() noexcept {
  arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
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
  return songCurrentPositionSeconds.load(std::memory_order_relaxed);
}

double MTControlsAudioProcessor::getSongLengthSeconds() const {
  return loadedSongLengthSeconds.load(std::memory_order_relaxed);
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
  padsGain.store(juce::jlimit(0.0f, 2.0f, gainValue),
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
  if (enabled) {
    metronome.reset();
    const double currentSec =
        songCurrentPositionSeconds.load(std::memory_order_relaxed);
    metronome.syncPhaseForTimeSeconds(currentSec);
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
  const double normalized = juce::jlimit(20.0, 300.0, bpm);
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
  metronomeGainState.store(juce::jlimit(0.0f, 2.0f, gainValue),
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

void MTControlsAudioProcessor::setMetronomeClickStemIndex(
    const int stemIndexOrMinusOne) noexcept {
  if (stemIndexOrMinusOne < 0 || stemIndexOrMinusOne >= maxStemCount) {
    metronomeClickStemIndex.store(-1, std::memory_order_relaxed);
    return;
  }
  metronomeClickStemIndex.store(stemIndexOrMinusOne,
                                std::memory_order_relaxed);
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
  songCurrentPositionSeconds.store(0.0, std::memory_order_relaxed);
  songLoaded.store(false, std::memory_order_relaxed);
  arrangementParts.clear();
  arrangementCustomActive.store(false, std::memory_order_relaxed);
  arrangementCurrentPartIndex.store(0, std::memory_order_relaxed);
  arrangementPendingJumpPartIndex.store(-1, std::memory_order_relaxed);
  arrangementLoopPartIndex.store(-1, std::memory_order_relaxed);
  arrangementTotalLengthSeconds.store(0.0, std::memory_order_relaxed);
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
  mixerSource.removeAllInputs();
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
      stemPitchShiftStates[static_cast<size_t>(i)].store(
          true, std::memory_order_relaxed);
      stemOutputChannels[static_cast<size_t>(i)].store(
          stemOutputRoutingState.getStemOutputChannel(i), std::memory_order_relaxed);
      stemOutputWidths[static_cast<size_t>(i)].store(
          stemOutputRoutingState.getStemOutputWidth(i), std::memory_order_relaxed);
      stemPitchDelayLines[static_cast<size_t>(i)].setDelay(0);
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
  pitchTargetSemiTones.store(semitoneDelta, std::memory_order_relaxed);
}

int MTControlsAudioProcessor::getPitchSemiTones() const noexcept {
  return pitchTargetSemiTones.load(std::memory_order_relaxed);
}

double MTControlsAudioProcessor::getPlaybackTempoRatio() const noexcept {
  const double originalBpm = juce::jmax(
      20.0, loadedSongBpm.load(std::memory_order_relaxed));
  const double playbackBpm = juce::jmax(
      20.0, playbackTempoBpm.load(std::memory_order_relaxed));
  return juce::jlimit(0.5, 2.0, playbackBpm / originalBpm);
}

int MTControlsAudioProcessor::findArrangementPartIndexForSourceTime(
    const double sourceSeconds) const noexcept {
  if (arrangementParts.empty())
    return -1;

  for (size_t i = 0; i < arrangementParts.size(); ++i) {
    const auto &part = arrangementParts[i];
    if (sourceSeconds >= part.startSec && sourceSeconds < part.endSec)
      return static_cast<int>(i);
  }

  int bestIndex = 0;
  double bestDistance = std::numeric_limits<double>::max();
  for (size_t i = 0; i < arrangementParts.size(); ++i) {
    const auto &part = arrangementParts[i];
    const double distance = sourceSeconds < part.startSec
                                ? (part.startSec - sourceSeconds)
                                : (sourceSeconds - part.endSec);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = static_cast<int>(i);
    }
  }
  return bestIndex;
}

double MTControlsAudioProcessor::getArrangementStartSeconds() const noexcept {
  const juce::SpinLock::ScopedLockType lock(stemListLock);
  if (arrangementParts.empty())
    return songStartSeconds.load(std::memory_order_relaxed);
  return arrangementParts.front().startSec;
}

MTControlsAudioProcessor::TransportState
MTControlsAudioProcessor::getTransportState() const noexcept {
  return static_cast<TransportState>(
      transportState.load(std::memory_order_relaxed));
}

void MTControlsAudioProcessor::setTransportState(
    const TransportState state) noexcept {
  transportState.store(static_cast<int>(state), std::memory_order_relaxed);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new MTControlsAudioProcessor();
}
