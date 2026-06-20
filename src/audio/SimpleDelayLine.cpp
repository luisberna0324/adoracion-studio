#include "SimpleDelayLine.h"

void SimpleDelayLine::prepare(int channels, int maxDelaySamplesToUse) {
  numChannels = juce::jmax(0, channels);
  maxDelaySamples = juce::jmax(0, maxDelaySamplesToUse);
  delaySamples = juce::jmin(delaySamples, maxDelaySamples);
  delayBuffer.setSize(numChannels, juce::jmax(1, maxDelaySamples + 1));
  delayBuffer.clear();
  writePos = 0;
  historySamples = 0;
}

void SimpleDelayLine::setDelay(int delaySamplesToUse) {
  delaySamples = juce::jlimit(0, maxDelaySamples, delaySamplesToUse);
}

void SimpleDelayLine::reset() {
  delaySamples = 0;
  delayBuffer.clear();
  writePos = 0;
  historySamples = 0;
}

void SimpleDelayLine::process(juce::AudioBuffer<float> &buffer) {
  if (numChannels <= 0)
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
