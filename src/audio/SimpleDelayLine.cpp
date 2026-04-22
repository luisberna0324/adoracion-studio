#include "SimpleDelayLine.h"

void SimpleDelayLine::prepare(int channels, int maxDelaySamplesToUse) {
  numChannels = juce::jmax(0, channels);
  maxDelaySamples = juce::jmax(0, maxDelaySamplesToUse);
  delaySamples = juce::jmin(delaySamples, maxDelaySamples);
  delayBuffer.setSize(numChannels, juce::jmax(1, maxDelaySamples + 1));
  delayBuffer.clear();
  writePos = 0;
}

void SimpleDelayLine::setDelay(int delaySamplesToUse) {
  // Keep buffered history when delay changes to avoid introducing
  // audible gaps from clearing mid-playback.
  delaySamples = juce::jlimit(0, maxDelaySamples, delaySamplesToUse);
}

void SimpleDelayLine::reset() {
  delaySamples = 0;
  delayBuffer.clear();
  writePos = 0;
}

void SimpleDelayLine::process(juce::AudioBuffer<float> &buffer) {
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
