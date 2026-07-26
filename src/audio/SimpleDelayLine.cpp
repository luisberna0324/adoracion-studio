#include "SimpleDelayLine.h"

void SimpleDelayLine::prepare(int channels, int maxDelaySamplesToUse) {
  numChannels = juce::jmax(0, channels);
  maxDelaySamples = juce::jmax(0, maxDelaySamplesToUse);
  delaySamples = juce::jmin(delaySamples, maxDelaySamples);
  currentDelaySamples = delaySamples;
  transitionFromDelaySamples = delaySamples;
  transitionToDelaySamples = delaySamples;
  transitionSample = 0;
  transitionActive = false;
  delayBuffer.setSize(numChannels, juce::jmax(1, maxDelaySamples + 1));
  delayBuffer.clear();
  writePos = 0;
  validHistorySamples = 0;
}

void SimpleDelayLine::releaseResources() {
  reset();
  delayBuffer.setSize(0, 0);
  numChannels = 0;
  maxDelaySamples = 0;
}

void SimpleDelayLine::setDelay(int delaySamplesToUse) {
  delaySamples = juce::jlimit(0, maxDelaySamples, delaySamplesToUse);
  // There is nothing meaningful to crossfade after prepare/reset/invalidate.
  // Applying the requested tap immediately also preserves the full leading
  // silence of a clean delay line.
  if (validHistorySamples == 0) {
    currentDelaySamples = delaySamples;
    transitionFromDelaySamples = delaySamples;
    transitionToDelaySamples = delaySamples;
    transitionSample = 0;
    transitionActive = false;
  }
}

void SimpleDelayLine::reset() {
  delaySamples = 0;
  currentDelaySamples = 0;
  transitionFromDelaySamples = 0;
  transitionToDelaySamples = 0;
  transitionSample = 0;
  transitionActive = false;
  writePos = 0;
  validHistorySamples = 0;
}

void SimpleDelayLine::process(juce::AudioBuffer<float> &buffer) {
  if (numChannels <= 0)
    return;

  if (buffer.getNumChannels() != numChannels) {
    // Channel layout changes must be handled by prepare(), never by allocating
    // from the audio callback.
    jassertfalse;
    return;
  }

  const int numSamples = buffer.getNumSamples();
  const int bufferSize = delayBuffer.getNumSamples();
  if (bufferSize <= 0 || numSamples <= 0)
    return;

  int localWrite = writePos;
  for (int sample = 0; sample < numSamples; ++sample) {
    if (!transitionActive && currentDelaySamples != delaySamples) {
      transitionFromDelaySamples = currentDelaySamples;
      transitionToDelaySamples = delaySamples;
      transitionSample = 0;
      transitionActive = true;
    }

    int currentReadPos = localWrite - currentDelaySamples;
    if (currentReadPos < 0)
      currentReadPos += bufferSize;

    int fromReadPos = localWrite - transitionFromDelaySamples;
    if (fromReadPos < 0)
      fromReadPos += bufferSize;

    int toReadPos = localWrite - transitionToDelaySamples;
    if (toReadPos < 0)
      toReadPos += bufferSize;

    float transitionGain = 0.0f;
    if (transitionActive) {
      const float position =
          static_cast<float>(transitionSample) /
          static_cast<float>(kDelayTransitionSamples - 1);
      // Smoothstep reaches both taps exactly and has zero slope at either end.
      transitionGain = position * position * (3.0f - (2.0f * position));
    }

    for (int ch = 0; ch < numChannels; ++ch) {
      auto *delayData = delayBuffer.getWritePointer(ch);
      auto *outData = buffer.getWritePointer(ch);
      const float input = outData[sample];
      const auto readTap = [&](const int tapDelay,
                               const int readPosition) noexcept {
        if (tapDelay <= 0)
          return input;
        return validHistorySamples >= tapDelay ? delayData[readPosition]
                                               : 0.0f;
      };

      float delayed = readTap(currentDelaySamples, currentReadPos);
      if (transitionActive) {
        const float from = readTap(transitionFromDelaySamples, fromReadPos);
        const float to = readTap(transitionToDelaySamples, toReadPos);
        delayed = from + ((to - from) * transitionGain);
      }

      delayData[localWrite] = input;
      outData[sample] = delayed;
    }

    if (transitionActive) {
      ++transitionSample;
      if (transitionSample >= kDelayTransitionSamples) {
        currentDelaySamples = transitionToDelaySamples;
        transitionSample = 0;
        transitionActive = false;
      }
    }

    ++localWrite;
    if (localWrite >= bufferSize)
      localWrite = 0;
    validHistorySamples =
        juce::jmin(maxDelaySamples, validHistorySamples + 1);
  }

  writePos = localWrite;
}
