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

  pitchShift.prepare(sampleRate, samplesPerBlock, numChannels);
  pitchShift.reset();
  pitchShift.setPitchSemiTones(pitchSemiTones);

  stemMixBuffer.setSize(numChannels, samplesPerBlock);
  stemTempBuffer.setSize(numChannels, samplesPerBlock);
  percussionBuffer.setSize(numChannels, samplesPerBlock);
  dryBuffer.setSize(numChannels, samplesPerBlock);

  pitchDelayPercussion.prepare(numChannels, kMaxPitchLatencySamples);
  pitchDelayGuide.prepare(numChannels, kMaxPitchLatencySamples);
  lastLatencySamples = 0;
}

void StemMixEngine::reset() {
  pitchShift.reset();
  stemMixBuffer.clear();
  stemTempBuffer.clear();
  percussionBuffer.clear();
  dryBuffer.clear();
  pitchDelayPercussion.reset();
  pitchDelayGuide.reset();
  lastLatencySamples = 0;
}

void StemMixEngine::setPitchSemiTones(int semitones) {
  if (pitchSemiTones == semitones)
    return;
  pitchSemiTones = semitones;
  pitchShift.setPitchSemiTones(semitones);
  lastLatencySamples = 0;
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

void StemMixEngine::updateLatencyDelays(int latencySamples) {
  const int clamped = juce::jmax(0, latencySamples);
  if (clamped == lastLatencySamples)
    return;
  lastLatencySamples = clamped;
  pitchDelayPercussion.setDelay(clamped);
  pitchDelayGuide.setDelay(clamped);
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
                                   bool pitchActive, float fadeStart,
                                   float fadeEnd, float windowFadeStart,
                                   float windowFadeEnd) {
  const int numSamples = output.getNumSamples();
  const int numChannels = output.getNumChannels();
  const float instrumentFadeStart = fadeStart * windowFadeStart;
  const float instrumentFadeEnd = fadeEnd * windowFadeEnd;
  applyFade(stemMixBuffer, instrumentFadeStart, instrumentFadeEnd);
  applyFade(percussionBuffer, instrumentFadeStart, instrumentFadeEnd);

  if (pitchActive) {
    if (!pitchShift.isPrepared() || pitchShift.getNumChannels() != numChannels)
      pitchShift.prepare(currentSampleRate, currentBlockSize, numChannels);
    pitchShift.process(stemMixBuffer);
    updateLatencyDelays(pitchShift.getLatencySamples());
    pitchDelayPercussion.process(percussionBuffer);
    pitchDelayGuide.process(dryBuffer);
  } else {
    updateLatencyDelays(0);
  }

  output.clear();
  for (int ch = 0; ch < numChannels; ++ch) {
    output.addFrom(ch, 0, stemMixBuffer, ch, 0, numSamples);
    output.addFrom(ch, 0, percussionBuffer, ch, 0, numSamples);
    output.addFrom(ch, 0, dryBuffer, ch, 0, numSamples);
  }
}
