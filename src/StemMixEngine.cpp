#include "StemMixEngine.h"

void StemMixEngine::SimpleDelayLine::prepare(int channels,
                                             int maxDelaySamplesToUse) {
  numChannels = juce::jmax(0, channels);
  maxDelaySamples = juce::jmax(0, maxDelaySamplesToUse);
  delaySamples = juce::jmin(delaySamples, maxDelaySamples);
  delayBuffer.setSize(numChannels, juce::jmax(1, maxDelaySamples + 1));
  delayBuffer.clear();
  writePos = 0;
  historySamples = 0;
}

void StemMixEngine::SimpleDelayLine::setDelay(int delaySamplesToUse) {
  delaySamples = juce::jlimit(0, maxDelaySamples, delaySamplesToUse);
}

void StemMixEngine::SimpleDelayLine::reset() {
  delaySamples = 0;
  delayBuffer.clear();
  writePos = 0;
  historySamples = 0;
}

void StemMixEngine::SimpleDelayLine::process(
    juce::AudioBuffer<float> &buffer, const int numSamples) {
  if (numChannels <= 0)
    return;
  const int bufferSize = delayBuffer.getNumSamples();
  if (bufferSize <= delaySamples || numSamples <= 0)
    return;

  int localWrite = writePos;
  for (int sample = 0; sample < numSamples; ++sample) {
    const int effectiveDelay = juce::jmin(delaySamples, historySamples);
    int readPos = localWrite - effectiveDelay;
    if (readPos < 0)
      readPos += bufferSize;

    for (int ch = 0; ch < numChannels; ++ch) {
      auto *delayData = delayBuffer.getWritePointer(ch);
      auto *outData = buffer.getWritePointer(ch);
      const float input = outData[sample];
      const float delayed = effectiveDelay > 0 ? delayData[readPos] : input;
      delayData[localWrite] = input;
      outData[sample] = delayed;
    }

    ++localWrite;
    if (localWrite >= bufferSize)
      localWrite = 0;
    historySamples = juce::jmin(maxDelaySamples, historySamples + 1);
  }

  writePos = localWrite;
}

void StemMixEngine::prepare(double sampleRate, int samplesPerBlock,
                            int numChannels) {
  currentSampleRate = sampleRate;
  currentBlockSize = samplesPerBlock;
  currentChannels = numChannels;

  pitchShiftMelodic.prepare(sampleRate, samplesPerBlock, numChannels);
  pitchShiftMelodic.setContentType(TimeStretchProcessor::ContentType::Melodic);
  pitchShiftMelodic.reset();
  pitchShiftMelodic.setPitchSemiTones(pitchSemiTones);
  pitchShiftMelodic.setTempoRatio(tempoRatio);

  tempoShiftPercussion.prepare(sampleRate, samplesPerBlock, numChannels);
  tempoShiftPercussion.setContentType(
      TimeStretchProcessor::ContentType::Percussive);
  tempoShiftPercussion.reset();
  tempoShiftPercussion.setPitchSemiTones(0);
  tempoShiftPercussion.setTempoRatio(tempoRatio);

  tempoShiftGuide.prepare(sampleRate, samplesPerBlock, numChannels);
  tempoShiftGuide.setContentType(TimeStretchProcessor::ContentType::Percussive);
  tempoShiftGuide.reset();
  tempoShiftGuide.setPitchSemiTones(0);
  tempoShiftGuide.setTempoRatio(tempoRatio);

  const int maxInputSamples = (samplesPerBlock * 2) + 8;
  stemMixBuffer.setSize(numChannels, maxInputSamples);
  stemTempBuffer.setSize(numChannels, maxInputSamples);
  percussionBuffer.setSize(numChannels, maxInputSamples);
  dryBuffer.setSize(numChannels, maxInputSamples);
  processedBuffer.setSize(numChannels, samplesPerBlock);

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
  processedBuffer.clear();
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
  // Preserve processor and delay history. Resetting here forces the stretcher
  // to re-prime and produces an audible dropout on every key change.
}

void StemMixEngine::setTempoRatio(double ratio) {
  const double normalized = juce::jlimit(0.75, 2.0, ratio);
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
  jassert(numChannels <= stemTempBuffer.getNumChannels());
  jassert(numSamples <= stemTempBuffer.getNumSamples());
  juce::ignoreUnused(numChannels, numSamples);
}

void StemMixEngine::applyFade(juce::AudioBuffer<float> &buffer,
                              const int numSamples, float start, float end) {
  if (juce::approximatelyEqual(start, 1.0f) &&
      juce::approximatelyEqual(end, 1.0f)) {
    return;
  }
  if (juce::approximatelyEqual(start, end)) {
    buffer.applyGain(0, numSamples, end);
  } else {
    buffer.applyGainRamp(0, numSamples, start, end);
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
    const std::atomic<bool> *pitchShiftStates, int stemCount, int inputSamples,
    int numChannels) {
  ensureBuffers(numChannels, inputSamples);
  stemMixBuffer.clear(0, inputSamples);
  percussionBuffer.clear(0, inputSamples);
  dryBuffer.clear(0, inputSamples);

  for (int i = 0; i < stemCount; ++i) {
    stemTempBuffer.clear(0, inputSamples);
    juce::AudioSourceChannelInfo info(&stemTempBuffer, 0, inputSamples);
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
      target.addFrom(ch, 0, stemTempBuffer, ch, 0, inputSamples);
  }
}

void StemMixEngine::renderDirect(
    const std::vector<std::unique_ptr<StemTrack>> &stems,
    const int stemCount, juce::AudioBuffer<float> &output) {
  const int numSamples = output.getNumSamples();
  const int numChannels = output.getNumChannels();
  ensureBuffers(numChannels, numSamples);
  output.clear();
  for (int i = 0; i < stemCount; ++i) {
    stemTempBuffer.clear(0, numSamples);
    juce::AudioSourceChannelInfo info(&stemTempBuffer, 0, numSamples);
    stems[static_cast<size_t>(i)]->getNextAudioBlock(info);
    for (int ch = 0; ch < numChannels; ++ch)
      output.addFrom(ch, 0, stemTempBuffer, ch, 0, numSamples);
  }
}

void StemMixEngine::renderToOutput(juce::AudioBuffer<float> &output,
                                   bool timeProcessActive,
                                   const int inputSamples, float fadeStart,
                                   float fadeEnd, float windowFadeStart,
                                   float windowFadeEnd) {
  const int numSamples = output.getNumSamples();
  const int numChannels = output.getNumChannels();
  const float instrumentFadeStart = fadeStart * windowFadeStart;
  const float instrumentFadeEnd = fadeEnd * windowFadeEnd;
  output.clear();

  if (timeProcessActive) {
    const bool tempoActive = !juce::approximatelyEqual(tempoRatio, 1.0);

    const int melodicLatency = pitchShiftMelodic.getLatencySamples();
    const int percussionLatency =
        tempoActive ? tempoShiftPercussion.getLatencySamples() : 0;
    const int dryLatency = tempoActive ? tempoShiftGuide.getLatencySamples() : 0;
    updateLatencyDelays(melodicLatency, percussionLatency, dryLatency);

    processedBuffer.clear(0, numSamples);
    pitchShiftMelodic.process(stemMixBuffer, inputSamples, processedBuffer,
                              numSamples);
    pitchDelayMelodic.process(processedBuffer, numSamples);
    applyFade(processedBuffer, numSamples, instrumentFadeStart,
              instrumentFadeEnd);
    for (int ch = 0; ch < numChannels; ++ch)
      output.addFrom(ch, 0, processedBuffer, ch, 0, numSamples);

    if (tempoActive) {
      processedBuffer.clear(0, numSamples);
      tempoShiftPercussion.process(percussionBuffer, inputSamples,
                                   processedBuffer, numSamples);
      pitchDelayPercussion.process(processedBuffer, numSamples);
      applyFade(processedBuffer, numSamples, instrumentFadeStart,
                instrumentFadeEnd);
      for (int ch = 0; ch < numChannels; ++ch)
        output.addFrom(ch, 0, processedBuffer, ch, 0, numSamples);

      processedBuffer.clear(0, numSamples);
      tempoShiftGuide.process(dryBuffer, inputSamples, processedBuffer,
                              numSamples);
      pitchDelayGuide.process(processedBuffer, numSamples);
      for (int ch = 0; ch < numChannels; ++ch)
        output.addFrom(ch, 0, processedBuffer, ch, 0, numSamples);
    } else {
      applyFade(percussionBuffer, numSamples, instrumentFadeStart,
                instrumentFadeEnd);
      for (int ch = 0; ch < numChannels; ++ch) {
        output.addFrom(ch, 0, percussionBuffer, ch, 0, numSamples);
        output.addFrom(ch, 0, dryBuffer, ch, 0, numSamples);
      }
    }

  } else {
    updateLatencyDelays(0, 0, 0);
    applyFade(stemMixBuffer, numSamples, instrumentFadeStart,
              instrumentFadeEnd);
    applyFade(percussionBuffer, numSamples, instrumentFadeStart,
              instrumentFadeEnd);
    for (int ch = 0; ch < numChannels; ++ch) {
      output.addFrom(ch, 0, stemMixBuffer, ch, 0, numSamples);
      output.addFrom(ch, 0, percussionBuffer, ch, 0, numSamples);
      output.addFrom(ch, 0, dryBuffer, ch, 0, numSamples);
    }
  }
}

int StemMixEngine::requiredInputFrames(const int outputFrames) noexcept {
  return pitchShiftMelodic.requiredInputFrames(outputFrames);
}
