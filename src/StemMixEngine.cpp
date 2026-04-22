#include "StemMixEngine.h"

void StemMixEngine::SimpleDelayLine::prepare(int channels,
                                             int maxDelaySamplesToUse) {
  numChannels = juce::jmax(0, channels);
  maxDelaySamples = juce::jmax(0, maxDelaySamplesToUse);
  delaySamples = juce::jmin(delaySamples, maxDelaySamples);
  delayBuffer.setSize(numChannels, juce::jmax(1, maxDelaySamples + 1));
  delayBuffer.clear();
  writePos = 0;
}

void StemMixEngine::SimpleDelayLine::setDelay(int delaySamplesToUse) {
  // Keep buffered history when delay changes to avoid introducing
  // audible gaps from clearing mid-playback.
  delaySamples = juce::jlimit(0, maxDelaySamples, delaySamplesToUse);
}

void StemMixEngine::SimpleDelayLine::reset() {
  delaySamples = 0;
  delayBuffer.clear();
  writePos = 0;
}

void StemMixEngine::SimpleDelayLine::process(
    juce::AudioBuffer<float> &buffer) {
  if (delaySamples <= 0 || numChannels <= 0)
    return;

  if (buffer.getNumChannels() != numChannels) {
    prepare(buffer.getNumChannels(), maxDelaySamples);
  }

  const int numSamples = buffer.getNumSamples();
  const int bufferSize = delayBuffer.getNumSamples();
  if (bufferSize <= delaySamples || numSamples <= 0)
    return;

  int localWrite = writePos;
  for (int sample = 0; sample < numSamples; ++sample) {
    int readPos = localWrite - delaySamples;
    if (readPos < 0)
      readPos += bufferSize;

    for (int ch = 0; ch < numChannels; ++ch) {
      auto *delayData = delayBuffer.getWritePointer(ch);
      auto *outData = buffer.getWritePointer(ch);
      const float delayed = delayData[readPos];
      delayData[localWrite] = outData[sample];
      outData[sample] = delayed;
    }

    ++localWrite;
    if (localWrite >= bufferSize)
      localWrite = 0;
  }

  writePos = localWrite;
}

void StemMixEngine::prepare(double sampleRate, int samplesPerBlock,
                            int numChannels) {
  currentSampleRate = sampleRate;
  currentBlockSize = samplesPerBlock;
  currentChannels = numChannels;

  // Desynchronize SoundTouch overlap-add window phases between the three
  // internal processors so their per-window CPU peaks don't land on the same
  // processBlock call. Offsets picked as distinct primes spanning ~80 ms at
  // 48 kHz (SoundTouch default sequenceMs ≈ 82 ms).
  pitchShift.setPitchSemiTones(pitchSemiTones);
  pitchShift.setTempoRatio(tempoRatio);
  pitchShift.setPhaseOffsetSamples(0);
  pitchShift.prepare(sampleRate, samplesPerBlock, numChannels);
  tempoShiftPercussion.setPitchSemiTones(0);
  tempoShiftPercussion.setTempoRatio(tempoRatio);
  tempoShiftPercussion.setPhaseOffsetSamples(1283);
  tempoShiftPercussion.prepare(sampleRate, samplesPerBlock, numChannels);
  tempoShiftGuide.setPitchSemiTones(0);
  tempoShiftGuide.setTempoRatio(tempoRatio);
  tempoShiftGuide.setPhaseOffsetSamples(2567);
  tempoShiftGuide.prepare(sampleRate, samplesPerBlock, numChannels);

  stemMixBuffer.setSize(numChannels, samplesPerBlock);
  stemTempBuffer.setSize(numChannels, samplesPerBlock);
  percussionBuffer.setSize(numChannels, samplesPerBlock);
  dryBuffer.setSize(numChannels, samplesPerBlock);

  pitchDelayMelodic.prepare(numChannels, kMaxPitchLatencySamples);
  pitchDelayPercussion.prepare(numChannels, kMaxPitchLatencySamples);
  pitchDelayGuide.prepare(numChannels, kMaxPitchLatencySamples);
  lastMelodicDelaySamples = 0;
  lastPercussionDelaySamples = 0;
  lastGuideDelaySamples = 0;
}

void StemMixEngine::reset() {
  pitchShift.reset();
  tempoShiftPercussion.reset();
  tempoShiftGuide.reset();
  stemMixBuffer.clear();
  stemTempBuffer.clear();
  percussionBuffer.clear();
  dryBuffer.clear();
  pitchDelayMelodic.reset();
  pitchDelayPercussion.reset();
  pitchDelayGuide.reset();
  lastMelodicDelaySamples = 0;
  lastPercussionDelaySamples = 0;
  lastGuideDelaySamples = 0;
}

void StemMixEngine::setPitchSemiTones(int semitones) {
  if (pitchSemiTones == semitones)
    return;
  pitchSemiTones = semitones;
  pitchShift.setPitchSemiTones(semitones);
}

void StemMixEngine::setTempoRatio(double ratio) {
  const double normalized = juce::jlimit(0.5, 2.0, ratio);
  if (juce::approximatelyEqual(tempoRatio, normalized))
    return;
  tempoRatio = normalized;
  pitchShift.setTempoRatio(tempoRatio);
  tempoShiftPercussion.setTempoRatio(tempoRatio);
  tempoShiftGuide.setTempoRatio(tempoRatio);
}

void StemMixEngine::ensureBuffers(int numChannels, int numSamples) {
  if (numSamples != stemTempBuffer.getNumSamples() ||
      numChannels != stemTempBuffer.getNumChannels()) {
    stemTempBuffer.setSize(numChannels, numSamples, false, false, true);
    stemMixBuffer.setSize(numChannels, numSamples, false, false, true);
    percussionBuffer.setSize(numChannels, numSamples, false, false, true);
    dryBuffer.setSize(numChannels, numSamples, false, false, true);
  }
}

void StemMixEngine::applyFade(juce::AudioBuffer<float> &buffer, float start,
                              float end) {
  if (juce::approximatelyEqual(start, 1.0f) &&
      juce::approximatelyEqual(end, 1.0f)) {
    return;
  }
  if (juce::approximatelyEqual(start, end)) {
    buffer.applyGain(end);
  } else {
    buffer.applyGainRamp(0, buffer.getNumSamples(), start, end);
  }
}

int StemMixEngine::updateLatencyDelays(int melodicDelaySamples,
                                       int percussionDelaySamples,
                                       int guideDelaySamples) {
  int changed = 0;
  const int melodicClamped = juce::jmax(0, melodicDelaySamples);
  const int percussionClamped = juce::jmax(0, percussionDelaySamples);
  const int guideClamped = juce::jmax(0, guideDelaySamples);

  if (melodicClamped != lastMelodicDelaySamples) {
    lastMelodicDelaySamples = melodicClamped;
    pitchDelayMelodic.setDelay(melodicClamped);
    ++changed;
  }
  if (percussionClamped != lastPercussionDelaySamples) {
    lastPercussionDelaySamples = percussionClamped;
    pitchDelayPercussion.setDelay(percussionClamped);
    ++changed;
  }
  if (guideClamped != lastGuideDelaySamples) {
    lastGuideDelaySamples = guideClamped;
    pitchDelayGuide.setDelay(guideClamped);
    ++changed;
  }
  return changed;
}

void StemMixEngine::mixStems(
    const std::vector<std::unique_ptr<StemTrack>> &stems,
    const std::atomic<bool> *clickGuideStates,
    const std::atomic<bool> *pitchShiftStates, int stemCount, int numSamples,
    int numChannels) {
  ensureBuffers(numChannels, numSamples);
  stemMixBuffer.clear();
  percussionBuffer.clear();
  dryBuffer.clear();

  for (int i = 0; i < stemCount; ++i) {
    stemTempBuffer.clear();
    juce::AudioSourceChannelInfo info(&stemTempBuffer, 0, numSamples);
    stems[static_cast<size_t>(i)]->getNextAudioBlock(info);

    const bool isClickGuide =
        clickGuideStates[static_cast<size_t>(i)].load(
            std::memory_order_relaxed);
    const bool pitchShiftable =
        pitchShiftStates[static_cast<size_t>(i)].load(
            std::memory_order_relaxed);

    auto &target = isClickGuide
                       ? dryBuffer
                       : (pitchShiftable ? stemMixBuffer : percussionBuffer);

    for (int ch = 0; ch < numChannels; ++ch)
      target.addFrom(ch, 0, stemTempBuffer, ch, 0, numSamples);
  }
}

void StemMixEngine::renderToOutput(juce::AudioBuffer<float> &output,
                                   bool timeProcessActive, float fadeStart,
                                   float fadeEnd, float windowFadeStart,
                                   float windowFadeEnd,
                                   RenderStats *stats) {
  if (stats != nullptr)
    *stats = {};

  const int numSamples = output.getNumSamples();
  const int numChannels = output.getNumChannels();
  const float instrumentFadeStart = fadeStart * windowFadeStart;
  const float instrumentFadeEnd = fadeEnd * windowFadeEnd;
  applyFade(stemMixBuffer, instrumentFadeStart, instrumentFadeEnd);
  applyFade(percussionBuffer, instrumentFadeStart, instrumentFadeEnd);

  const bool pitchProcessActive = (pitchSemiTones != 0);
  const bool tempoProcessActive = !juce::approximatelyEqual(tempoRatio, 1.0);
  const bool shouldTimeProcess =
      timeProcessActive && (pitchProcessActive || tempoProcessActive);

  if (shouldTimeProcess) {
    if (!pitchShift.isPrepared() || pitchShift.getNumChannels() != numChannels)
      pitchShift.prepare(currentSampleRate, currentBlockSize, numChannels);
    if (!tempoShiftPercussion.isPrepared() ||
        tempoShiftPercussion.getNumChannels() != numChannels) {
      tempoShiftPercussion.prepare(currentSampleRate, currentBlockSize,
                                   numChannels);
      tempoShiftPercussion.setPitchSemiTones(0);
      tempoShiftPercussion.setTempoRatio(tempoRatio);
    }
    if (!tempoShiftGuide.isPrepared() ||
        tempoShiftGuide.getNumChannels() != numChannels) {
      tempoShiftGuide.prepare(currentSampleRate, currentBlockSize, numChannels);
      tempoShiftGuide.setPitchSemiTones(0);
      tempoShiftGuide.setTempoRatio(tempoRatio);
    }

    PitchShiftProcessor::ProcessStats melodicStats;
    PitchShiftProcessor::ProcessStats percussionStats;
    PitchShiftProcessor::ProcessStats guideStats;
    const auto accumulateStats =
        [stats](const PitchShiftProcessor::ProcessStats &local) {
          if (stats == nullptr)
            return;
          stats->processedPaths += 1;
          stats->inputFrames += local.inputFrames;
          stats->paddedFrames += local.paddedFrames;
          stats->receivedFrames += local.receivedFrames;
          stats->receiveCalls += local.receiveCalls;
          stats->maxReceiveFrames =
              juce::jmax(stats->maxReceiveFrames, local.maxReceiveFrames);
          stats->fifoAvailableBeforeFrames += local.fifoAvailableBeforeFrames;
          stats->fifoAvailableAfterFrames += local.fifoAvailableAfterFrames;
        };

    if (pitchProcessActive || tempoProcessActive) {
      pitchShift.process(stemMixBuffer, numSamples,
                         stats != nullptr ? &melodicStats : nullptr);
      accumulateStats(melodicStats);
    }
    if (tempoProcessActive) {
      tempoShiftPercussion.process(
          percussionBuffer, numSamples,
          stats != nullptr ? &percussionStats : nullptr);
      accumulateStats(percussionStats);
      tempoShiftGuide.process(
          dryBuffer, numSamples, stats != nullptr ? &guideStats : nullptr);
      accumulateStats(guideStats);
    }

    if (stemMixBuffer.getNumSamples() != numSamples) {
      stemMixBuffer.setSize(numChannels, numSamples, true, false, true);
    }
    if (percussionBuffer.getNumSamples() != numSamples) {
      percussionBuffer.setSize(numChannels, numSamples, true, false, true);
    }
    if (dryBuffer.getNumSamples() != numSamples) {
      dryBuffer.setSize(numChannels, numSamples, true, false, true);
    }

    const int melodicLatency = pitchShift.getLatencySamples();
    const int percussionLatency =
        tempoProcessActive ? tempoShiftPercussion.getLatencySamples() : 0;
    const int guideLatency =
        tempoProcessActive ? tempoShiftGuide.getLatencySamples() : 0;
    const int targetLatency = juce::jmax(
        melodicLatency, juce::jmax(percussionLatency, guideLatency));
    if (stats != nullptr)
      stats->targetLatencySamples = targetLatency;

    const int delayChanges = updateLatencyDelays(targetLatency - melodicLatency,
                                                 targetLatency -
                                                     percussionLatency,
                                                 targetLatency - guideLatency);
    if (stats != nullptr)
      stats->delayLineChanges += delayChanges;
    pitchDelayMelodic.process(stemMixBuffer);
    pitchDelayPercussion.process(percussionBuffer);
    pitchDelayGuide.process(dryBuffer);
  } else {
    if (stats != nullptr)
      stats->targetLatencySamples = 0;
    const int delayChanges = updateLatencyDelays(0, 0, 0);
    if (stats != nullptr)
      stats->delayLineChanges += delayChanges;
  }

  output.clear();
  for (int ch = 0; ch < numChannels; ++ch) {
    output.addFrom(ch, 0, stemMixBuffer, ch, 0, numSamples);
    output.addFrom(ch, 0, percussionBuffer, ch, 0, numSamples);
    output.addFrom(ch, 0, dryBuffer, ch, 0, numSamples);
  }
}
