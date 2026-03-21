#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

MTControlsAudioProcessor::MTControlsAudioProcessor()
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()) {
  playbackFormatManager.registerBasicFormats();
  resetStemControls(0);
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
  dryBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock);
  stemMixBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock);
  stemTempBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock);
  percussionBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock);
  pitchShift.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
  pitchShift.reset();

  {
    const juce::SpinLock::ScopedLockType lock(stemListLock);
    playbackSampleRate = sampleRate;
    playbackBlockSize = samplesPerBlock;
    metronomePhase = 0.0;
    metronomeClickSamplesRemaining = 0;
    metronomeBeatIndex =
        juce::jmax(1, metronomeBeatsPerBar.load(std::memory_order_relaxed)) -
        1;
    metronomeClickGain = 0.12f;
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
  pitchShift.reset();
  const juce::SpinLock::ScopedLockType lock(stemListLock);
  mixerSource.releaseResources();
  for (auto &track : stemTracks)
    track->releaseResources();
}

bool MTControlsAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  const auto input = layouts.getMainInputChannelSet();
  const auto output = layouts.getMainOutputChannelSet();

  return input == output && (input == juce::AudioChannelSet::mono() ||
                             input == juce::AudioChannelSet::stereo());
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
      pitchShift.setPitchSemiTones(pitchActiveSemiTones);
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

      const double bpm = metronomeBpm.load(std::memory_order_relaxed);
      if (bpm > 0.0) {
        const double beatPos = (seekTarget * bpm) / 60.0;
        metronomePhase = beatPos - std::floor(beatPos);
      }
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

    const float fadeTargetLocalForMix =
        fadeTarget.load(std::memory_order_relaxed);
    if (isPlaying) {
      const bool monoClick = monoClickMode.load(std::memory_order_relaxed);
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

      const bool isolateGuideDuringFade =
          fadeActive.load(std::memory_order_relaxed) ||
          !juce::approximatelyEqual(fadeGain, fadeTargetLocalForMix);
      const bool useSeparatedMix =
          (monoClick && numChannels >= 2) || isolateGuideDuringFade ||
          windowFadeActive || (pitchActiveSemiTones != 0);
      usedSeparatedMix = useSeparatedMix;
      usedMonoClickMix = monoClick && numChannels >= 2;

      if (useSeparatedMix) {
        if (numSamples > stemTempBuffer.getNumSamples()) {
          stemTempBuffer.setSize(numChannels, numSamples, false, false, true);
          stemMixBuffer.setSize(numChannels, numSamples, false, false, true);
          percussionBuffer.setSize(numChannels, numSamples, false, false, true);
          dryBuffer.setSize(numChannels, numSamples, false, false, true);
        }

        stemMixBuffer.clear();
        percussionBuffer.clear();
        dryBuffer.clear();

        for (int i = 0; i < stemCount; ++i) {
          stemTempBuffer.clear();
          juce::AudioSourceChannelInfo info(&stemTempBuffer, 0, numSamples);
          stemTracks[static_cast<size_t>(i)]->getNextAudioBlock(info);

          const bool isClickGuide =
              stemClickGuideStates[static_cast<size_t>(i)].load(
                  std::memory_order_relaxed);
          const bool pitchShiftable =
              stemPitchShiftStates[static_cast<size_t>(i)].load(
                  std::memory_order_relaxed);
          auto &target = isClickGuide
                             ? dryBuffer
                             : (pitchShiftable ? stemMixBuffer
                                               : percussionBuffer);
          for (int ch = 0; ch < numChannels; ++ch)
            target.addFrom(ch, 0, stemTempBuffer, ch, 0, numSamples);
        }
      } else {
        // MixerAudioSource will call getNextAudioBlock on all StemTracks, apply their internal volumes, and sum into `buffer`.
        juce::AudioSourceChannelInfo info(&buffer, 0, numSamples);
        mixerSource.getNextAudioBlock(info);
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

      dryBuffer.makeCopyOf(buffer, true);

      gain.setGainDecibels(inputGain->load());
      juce::dsp::AudioBlock<float> block(buffer);
      gain.process(juce::dsp::ProcessContextReplacing<float>(block));

      const float wet = juce::jlimit(0.0f, 1.0f, mix->load() * 0.01f);
      const float dry = 1.0f - wet;

      for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        auto *wetData = buffer.getWritePointer(channel);
        const auto *dryData = dryBuffer.getReadPointer(channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
          wetData[sample] = (wetData[sample] * wet) + (dryData[sample] * dry);
      }
    }
  }

  const int numSamples = buffer.getNumSamples();
  const int numChannels = buffer.getNumChannels();

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
    const float instrumentFadeStart = fadeStart * windowFadeStart;
    const float instrumentFadeEnd = fadeEnd * windowFadeEnd;
    if (!juce::approximatelyEqual(instrumentFadeStart, 1.0f) ||
        !juce::approximatelyEqual(instrumentFadeEnd, 1.0f)) {
      if (juce::approximatelyEqual(instrumentFadeStart, instrumentFadeEnd)) {
        stemMixBuffer.applyGain(instrumentFadeEnd);
        percussionBuffer.applyGain(instrumentFadeEnd);
      } else {
        stemMixBuffer.applyGainRamp(0, numSamples, instrumentFadeStart,
                                    instrumentFadeEnd);
        percussionBuffer.applyGainRamp(0, numSamples, instrumentFadeStart,
                                       instrumentFadeEnd);
      }
    }

    if (pitchActiveSemiTones != 0) {
      if (!pitchShift.isPrepared() ||
          pitchShift.getNumChannels() != numChannels) {
        pitchShift.prepare(playbackSampleRate, playbackBlockSize, numChannels);
      }
      pitchShift.process(stemMixBuffer);
    }

    buffer.clear();
    if (usedMonoClickMix && numChannels >= 2) {
      auto *left = buffer.getWritePointer(0);
      auto *right = buffer.getWritePointer(1);
      for (int s = 0; s < numSamples; ++s) {
        float musicSum = 0.0f;
        float guideSum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
          musicSum += stemMixBuffer.getSample(ch, s);
          musicSum += percussionBuffer.getSample(ch, s);
          guideSum += dryBuffer.getSample(ch, s);
        }
        left[s] = musicSum / static_cast<float>(numChannels);
        right[s] = guideSum / static_cast<float>(numChannels);
      }
      for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear(ch, 0, numSamples);
    } else {
      for (int ch = 0; ch < numChannels; ++ch) {
        buffer.addFrom(ch, 0, stemMixBuffer, ch, 0, numSamples);
        buffer.addFrom(ch, 0, percussionBuffer, ch, 0, numSamples);
        buffer.addFrom(ch, 0, dryBuffer, ch, 0, numSamples);
      }
    }

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

  if (metronomeEnabled.load(std::memory_order_relaxed) && numSamples > 0 &&
      playbackSampleRate > 0.0) {
    const double bpm = metronomeBpm.load(std::memory_order_relaxed);
    const int beatsPerBar =
        juce::jmax(1, metronomeBeatsPerBar.load(std::memory_order_relaxed));
    if (bpm > 0.0) {
      const double phaseInc = bpm / (60.0 * playbackSampleRate);
      const int clickLength =
          juce::jmax(8, static_cast<int>(playbackSampleRate * 0.025));
      const double clickFreq = 900.0;
      const double twoPi = juce::MathConstants<double>::twoPi;

      for (int sample = 0; sample < numSamples; ++sample) {
        metronomePhase += phaseInc;
        if (metronomePhase >= 1.0) {
          metronomePhase -= 1.0;
          metronomeClickSamplesRemaining = clickLength;
          metronomeBeatIndex = (metronomeBeatIndex + 1) % beatsPerBar;
          metronomeClickGain =
              (metronomeBeatIndex == 0) ? 0.18f : 0.12f;
        }

        float clickSample = 0.0f;
        if (metronomeClickSamplesRemaining > 0) {
          const int idx = clickLength - metronomeClickSamplesRemaining;
          const float env =
              std::exp(-6.0f * static_cast<float>(idx) /
                       static_cast<float>(clickLength));
          clickSample =
              static_cast<float>(std::sin(twoPi * clickFreq *
                                          (static_cast<double>(idx) /
                                           playbackSampleRate))) *
              env * metronomeClickGain;
          --metronomeClickSamplesRemaining;
        }

        if (clickSample != 0.0f) {
          const bool monoClick =
              monoClickMode.load(std::memory_order_relaxed);
          if (monoClick && numChannels >= 2) {
            buffer.addSample(1, sample, clickSample);
          } else {
            for (int ch = 0; ch < numChannels; ++ch)
              buffer.addSample(ch, sample, clickSample);
          }
        }
      }
    }
  }

  if (!juce::approximatelyEqual(masterOutGain, 1.0f))
    buffer.applyGain(masterOutGain);

  if (stopAtEnd) {
    setTransportState(TransportState::stopped);
    songCurrentPositionSeconds.store(stopAtEndSec, std::memory_order_relaxed);
    if (playbackSampleRate > 0.0) {
      const juce::int64 endSamples =
          static_cast<juce::int64>(stopAtEndSec * playbackSampleRate);
      globalSamplePosition.store(endSamples, std::memory_order_relaxed);
    }
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
  if (const auto state = apvts.copyState(); state.isValid()) {
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
  if (tree.isValid())
    apvts.replaceState(tree);
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
  metronomePhase = 0.0;
  metronomeClickSamplesRemaining = 0;
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
                                           const float gain) noexcept {
  if (!juce::isPositiveAndBelow(stemIndex, maxStemCount))
    return;

  stemGains[static_cast<size_t>(stemIndex)].store(
      juce::jlimit(0.0f, 2.0f, gain), std::memory_order_relaxed);
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

void MTControlsAudioProcessor::setMasterVolume(const float volume) noexcept {
  masterVolume.store(juce::jlimit(0.0f, 1.0f, volume),
                     std::memory_order_relaxed);
}

float MTControlsAudioProcessor::getMasterVolume() const noexcept {
  return masterVolume.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMasterGain(const float gain) noexcept {
  masterGain.store(juce::jlimit(0.0f, 2.0f, gain),
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
  metronomeEnabled.store(enabled, std::memory_order_relaxed);
  if (enabled) {
    metronomeBeatIndex =
        juce::jmax(1, metronomeBeatsPerBar.load(std::memory_order_relaxed)) -
        1;
  } else {
    metronomePhase = 0.0;
    metronomeClickSamplesRemaining = 0;
    metronomeBeatIndex = 0;
    metronomeClickGain = 0.12f;
  }
}

bool MTControlsAudioProcessor::isMetronomeEnabled() const noexcept {
  return metronomeEnabled.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMetronomeBpm(double bpm) noexcept {
  const double clamped = juce::jlimit(20.0, 300.0, bpm);
  metronomeBpm.store(clamped, std::memory_order_relaxed);
}

double MTControlsAudioProcessor::getMetronomeBpm() const noexcept {
  return metronomeBpm.load(std::memory_order_relaxed);
}

void MTControlsAudioProcessor::setMetronomeBeatsPerBar(int beats) noexcept {
  const int clamped = juce::jmax(1, beats);
  metronomeBeatsPerBar.store(clamped, std::memory_order_relaxed);
  metronomeBeatIndex = clamped - 1;
}

void MTControlsAudioProcessor::setMonoClickMode(bool enabled) noexcept {
  monoClickMode.store(enabled, std::memory_order_relaxed);
}

bool MTControlsAudioProcessor::isMonoClickMode() const noexcept {
  return monoClickMode.load(std::memory_order_relaxed);
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
