#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

MTControlsAudioProcessor::MTControlsAudioProcessor()
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::discreteChannels(maxStemCount), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
  playbackFormatManager.registerBasicFormats();
  resetStemControls(0);
  standaloneAudioDeviceBridge.setAvailableOutputChannelCount(2);
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
  stemRoutingTempBuffer.setSize(2, samplesPerBlock);
  standaloneAudioDeviceBridge.setAvailableOutputChannelCount(getTotalNumOutputChannels());
  stemOutputRoutingState.normalizeForDevice(getTotalNumOutputChannels());
  for (int i = 0; i < maxStemCount; ++i) {
    const int channel = stemOutputRoutingState.getStemOutputChannel(i);
    stemOutputChannels[static_cast<size_t>(i)].store(channel, std::memory_order_relaxed);
    const int width = stemOutputRoutingState.getStemOutputWidth(i);
    stemOutputWidths[static_cast<size_t>(i)].store(width, std::memory_order_relaxed);
    stemPitchProcessors[static_cast<size_t>(i)].prepare(sampleRate, samplesPerBlock, 2);
    stemPitchProcessors[static_cast<size_t>(i)].setPitchSemiTones(pitchActiveSemiTones);
    stemPitchDelayLines[static_cast<size_t>(i)].prepare(2, kMaxRoutingPitchLatencySamples);
    stemPitchDelayLines[static_cast<size_t>(i)].setDelay(0);
  }
  routingPitchLatencySamples = 0;

  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    playbackSampleRate = sampleRate;
    playbackBlockSize = samplesPerBlock;
    metronome.prepare(sampleRate);
    mixerSource.prepareToPlay(samplesPerBlock, sampleRate);
    for (auto &track : stemTracks)
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
  const juce::SpinLock::ScopedLockType lock(stemListLock);
  mixerSource.releaseResources();
  for (auto &track : stemTracks)
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
                                            juce::MidiBuffer &) {
  juce::ScopedNoDenormals noDenormals;

  // --- Diagnostics: overall block start time ---
  const int64_t blockStartTick = juce::Time::getHighResolutionTicks();

  for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels();
       ++i)
    buffer.clear(i, 0, buffer.getNumSamples());

  bool usedSeparatedMix = false;
  bool usedMonoClickMix = false;
  float windowFadeStart = 1.0f;
  float windowFadeEnd = 1.0f;
  bool windowFadeActive = false;
  bool stopAtEnd = false;
  double stopAtEndSec = 0.0;

  if (songLoaded.load(std::memory_order_relaxed)) {
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const bool isPlaying = getTransportState() == TransportState::playing;
    const int targetPitch =
        pitchTargetSemiTones.load(std::memory_order_relaxed);
    if (targetPitch != pitchActiveSemiTones) {
      pitchActiveSemiTones = targetPitch;
      mixEngine.setPitchSemiTones(pitchActiveSemiTones);
      routingPitchLatencySamples = 0;
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

    // Evaluate global solo state
    bool anySoloActive = false;
    for (int i = 0; i < stemCount; ++i) {
      if (stemSoloStates[static_cast<size_t>(i)].load(std::memory_order_relaxed)) {
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
      if (assignedChannel > 1) {
        routingActive = true;
        break;
      }
    }

    const float fadeTargetLocalForMix =
        fadeTarget.load(std::memory_order_relaxed);
    if (isPlaying) {
      const bool monoClick = metronome.isMonoClickMode();
      const double length =
          loadedSongLengthSeconds.load(std::memory_order_relaxed);
      const double windowStartSec =
          songStartSeconds.load(std::memory_order_relaxed);
      double windowEndSec =
          songEndSeconds.load(std::memory_order_relaxed);
      if (windowEndSec <= 0.0 || windowEndSec > length)
        windowEndSec = length;
      const double fadeOutSec =
          songFadeOutSeconds.load(std::memory_order_relaxed);
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
        const bool pitchActive = pitchActiveSemiTones != 0;
        int targetLatency = 0;
        if (pitchActive) {
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
          if (pitchActive && pitchShiftable) {
            auto &proc = stemPitchProcessors[static_cast<size_t>(i)];
            if (!proc.isPrepared() || proc.getNumChannels() != 2)
              proc.prepare(playbackSampleRate, numSamples, 2);
            proc.setPitchSemiTones(pitchActiveSemiTones);
            proc.process(stemRoutingTempBuffer);
          } else if (routingPitchLatencySamples > 0) {
            stemPitchDelayLines[static_cast<size_t>(i)].process(
                stemRoutingTempBuffer);
          }
          const int assignedChannelOneBased =
              stemOutputChannels[static_cast<size_t>(i)].load(
                  std::memory_order_relaxed);
          const int assignedWidth =
              stemOutputWidths[static_cast<size_t>(i)].load(
                  std::memory_order_relaxed);
          const int outputIndex =
              juce::jlimit(0, juce::jmax(0, numChannels - 1),
                           assignedChannelOneBased - 1);
          stemOutputRouter.routeStemToOutputChannels(
              stemRoutingTempBuffer, outputIndex, assignedWidth, buffer);
        }
        usedSeparatedMix = false;
        usedMonoClickMix = false;
      } else {
        const bool isolateGuideDuringFade =
            fadeActive.load(std::memory_order_relaxed) ||
            !juce::approximatelyEqual(fadeGain, fadeTargetLocalForMix);
        const bool useSeparatedMix =
            (monoClick && numChannels >= 2) || isolateGuideDuringFade ||
            windowFadeActive || (pitchActiveSemiTones != 0);
        usedSeparatedMix = useSeparatedMix;
        usedMonoClickMix = monoClick && numChannels >= 2;

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
          static_cast<double>(
              globalSamplePosition.load(std::memory_order_relaxed)) /
          playbackSampleRate;
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
    // No song loaded — apply bypass/gain processing on raw input
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
    stemOutputRoutingState.normalizeForDevice(numChannels);
    for (int i = 0; i < maxStemCount; ++i) {
      stemOutputChannels[static_cast<size_t>(i)].store(
          stemOutputRoutingState.getStemOutputChannel(i),
          std::memory_order_relaxed);
      stemOutputWidths[static_cast<size_t>(i)].store(
          stemOutputRoutingState.getStemOutputWidth(i),
          std::memory_order_relaxed);
    }
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
    usedMonoClickMix = mixEngine.renderToOutput(
        buffer, usedMonoClickMix, pitchActiveSemiTones != 0, fadeStart, fadeEnd,
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

  if (fadeStopsPlayback.load(std::memory_order_relaxed) &&
      fadeActive.load(std::memory_order_relaxed) &&
      juce::approximatelyEqual(fadeGain, 0.0f) &&
      getTransportState() == TransportState::playing) {
    stopSong();
  }

  metronome.process(buffer);

  if (!juce::approximatelyEqual(masterOutGain, 1.0f))
    buffer.applyGain(masterOutGain);

  if (stopAtEnd) {
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

bool MTControlsAudioProcessor::acceptsMidi() const { return false; }

bool MTControlsAudioProcessor::producesMidi() const { return false; }

bool MTControlsAudioProcessor::isMidiEffect() const { return false; }

double MTControlsAudioProcessor::getTailLengthSeconds() const { return 0.0; }

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
bool MTControlsAudioProcessor::loadSongStems(const juce::StringArray &stemPaths,
                                             const juce::String &songTitle,
                                             const juce::String &songKey,
                                             const double songBpm) {
  // Snapshot playback configuration safely before we start building transports.
  double localSampleRate = 0.0;
  int localBlockSize = 0;
  {
    const juce::SpinLock::ScopedLockType cfgLock(stemListLock);
    localSampleRate = playbackSampleRate > 0.0 ? playbackSampleRate : 44100.0;
    localBlockSize = playbackBlockSize > 0 ? playbackBlockSize : 512;
  }

  std::vector<std::unique_ptr<StemTrack>> newTracks;
  if (stemPaths.isEmpty())
    return false;

  double maxLengthSeconds = 0.0;
  for (const auto &path : stemPaths) {
    if (newTracks.size() >= static_cast<size_t>(maxStemCount))
      break;

    const juce::File file(path);
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

  // Briefly acquire the lock to swap in the new stem list.
  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    clearLoadedSongState();
    removeAllStemSources();

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
    loadedSongBpm = juce::jmax(0.0, songBpm);
    if (loadedSongBpm > 0.0)
      setMetronomeBpm(loadedSongBpm.load(std::memory_order_relaxed));
    {
      const juce::ScopedWriteLock metaLock(songMetaLock);
      loadedSongKey = songKey;
      loadedSongTitle = songTitle;
    }
    songLoaded.store(true, std::memory_order_relaxed);
    pitchTargetSemiTones.store(0, std::memory_order_relaxed);
    setTransportState(TransportState::stopped);
  }

  return true;
}

void MTControlsAudioProcessor::playSong() {
  if (!songLoaded.load(std::memory_order_relaxed))
    return;
  const double start = songStartSeconds.load(std::memory_order_relaxed);
  const double end = songEndSeconds.load(std::memory_order_relaxed);
  const double current =
      songCurrentPositionSeconds.load(std::memory_order_relaxed);
  if (current < start || (end > 0.0 && current >= end))
    seekSongTo(start);
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
  const double start = songStartSeconds.load(std::memory_order_relaxed);
  seekTargetSeconds.store(start, std::memory_order_relaxed);
  seekPending.store(true, std::memory_order_release);
  globalSamplePosition.store(
      static_cast<juce::int64>(start * playbackSampleRate),
      std::memory_order_relaxed);
  songCurrentPositionSeconds.store(start, std::memory_order_relaxed);
  metronome.reset();
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

  const double length =
      loadedSongLengthSeconds.load(std::memory_order_relaxed);
  double start = songStartSeconds.load(std::memory_order_relaxed);
  double end = songEndSeconds.load(std::memory_order_relaxed);
  if (end <= 0.0 || end > length)
    end = length;
  start = juce::jlimit(0.0, end, start);
  const double target = juce::jlimit(start, end, positionSeconds);

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

void MTControlsAudioProcessor::setMetronomeBeatsPerBar(int beats) noexcept {
  metronome.setBeatsPerBar(beats);
}

void MTControlsAudioProcessor::setMonoClickMode(bool enabled) noexcept {
  metronome.setMonoClickMode(enabled);
}

bool MTControlsAudioProcessor::isMonoClickMode() const noexcept {
  return metronome.isMonoClickMode();
}

void MTControlsAudioProcessor::clearLoadedSongState() {
  loadedSongLengthSeconds = 0.0;
  loadedSongBpm = 0.0;
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
  pitchTargetSemiTones.store(0, std::memory_order_relaxed);
  mixEngine.reset();
  metronome.reset();
  routingPitchLatencySamples = 0;
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
