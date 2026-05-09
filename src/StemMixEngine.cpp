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
  delaySamples = juce::jlimit(0, maxDelaySamples, delaySamplesToUse);
  delayBuffer.clear();
  writePos = 0;
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

  pitchShiftMelodic.prepare(sampleRate, samplesPerBlock, numChannels);
  pitchShiftMelodic.reset();
  pitchShiftMelodic.setPitchSemiTones(pitchSemiTones);
  pitchShiftMelodic.setTempoRatio(tempoRatio);

  tempoShiftPercussion.prepare(sampleRate, samplesPerBlock, numChannels);
  tempoShiftPercussion.reset();
  tempoShiftPercussion.setPitchSemiTones(0);
  tempoShiftPercussion.setTempoRatio(tempoRatio);

  tempoShiftGuide.prepare(sampleRate, samplesPerBlock, numChannels);
  tempoShiftGuide.reset();
  tempoShiftGuide.setPitchSemiTones(0);
  tempoShiftGuide.setTempoRatio(tempoRatio);

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
  pitchShiftMelodic.reset();
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
  pitchShiftMelodic.setPitchSemiTones(semitones);
  lastMelodicDelaySamples = -1;
  lastPercussionDelaySamples = -1;
  lastGuideDelaySamples = -1;
}

void StemMixEngine::setTempoRatio(double ratio) {
  const double normalized = juce::jlimit(0.5, 2.0, ratio);
  if (juce::approximatelyEqual(tempoRatio, normalized))
    return;
  tempoRatio = normalized;
  pitchShiftMelodic.setTempoRatio(tempoRatio);
  tempoShiftPercussion.setTempoRatio(tempoRatio);
  tempoShiftGuide.setTempoRatio(tempoRatio);
  lastMelodicDelaySamples = -1;
  lastPercussionDelaySamples = -1;
  lastGuideDelaySamples = -1;
}

void StemMixEngine::ensureBuffers(int numChannels, int numSamples) {
  if (numSamples > stemTempBuffer.getNumSamples() ||
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

void StemMixEngine::updateLatencyDelays(int melodicLatency, int percussionLatency,
                                        int dryLatency) {
  const int clampedMelodic = juce::jmax(0, melodicLatency);
  const int clampedPercussion = juce::jmax(0, percussionLatency);
  const int clampedDry = juce::jmax(0, dryLatency);
  const int maxLatency =
      juce::jmax(clampedMelodic, juce::jmax(clampedPercussion, clampedDry));

  const int melodicDelay = maxLatency - clampedMelodic;
  const int percussionDelay = maxLatency - clampedPercussion;
  const int dryDelay = maxLatency - clampedDry;

  if (melodicDelay != lastMelodicDelaySamples) {
    pitchDelayMelodic.setDelay(melodicDelay);
    lastMelodicDelaySamples = melodicDelay;
  }
  if (percussionDelay != lastPercussionDelaySamples) {
    pitchDelayPercussion.setDelay(percussionDelay);
    lastPercussionDelaySamples = percussionDelay;
  }
  if (dryDelay != lastGuideDelaySamples) {
    pitchDelayGuide.setDelay(dryDelay);
    lastGuideDelaySamples = dryDelay;
  }
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
                                   float windowFadeEnd) {
  const int numSamples = output.getNumSamples();
  const int numChannels = output.getNumChannels();
  const float instrumentFadeStart = fadeStart * windowFadeStart;
  const float instrumentFadeEnd = fadeEnd * windowFadeEnd;
  applyFade(stemMixBuffer, instrumentFadeStart, instrumentFadeEnd);
  applyFade(percussionBuffer, instrumentFadeStart, instrumentFadeEnd);

  if (timeProcessActive) {
    const bool tempoActive = !juce::approximatelyEqual(tempoRatio, 1.0);

    if (!pitchShiftMelodic.isPrepared() ||
        pitchShiftMelodic.getNumChannels() != numChannels) {
      pitchShiftMelodic.prepare(currentSampleRate, currentBlockSize,
                                numChannels);
      pitchShiftMelodic.setPitchSemiTones(pitchSemiTones);
      pitchShiftMelodic.setTempoRatio(tempoRatio);
    }
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

    pitchShiftMelodic.process(stemMixBuffer);
    if (tempoActive) {
      tempoShiftPercussion.process(percussionBuffer);
      tempoShiftGuide.process(dryBuffer);
    }

    const int melodicLatency = pitchShiftMelodic.getLatencySamples();
    const int percussionLatency =
        tempoActive ? tempoShiftPercussion.getLatencySamples() : 0;
    const int dryLatency = tempoActive ? tempoShiftGuide.getLatencySamples() : 0;
    updateLatencyDelays(melodicLatency, percussionLatency, dryLatency);
    pitchDelayMelodic.process(stemMixBuffer);
    pitchDelayPercussion.process(percussionBuffer);
    pitchDelayGuide.process(dryBuffer);
  } else {
    updateLatencyDelays(0, 0, 0);
  }

  output.clear();
  for (int ch = 0; ch < numChannels; ++ch) {
    output.addFrom(ch, 0, stemMixBuffer, ch, 0, numSamples);
    output.addFrom(ch, 0, percussionBuffer, ch, 0, numSamples);
    output.addFrom(ch, 0, dryBuffer, ch, 0, numSamples);
  }
}
