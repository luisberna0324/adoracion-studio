#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

class SimpleDelayLine {
public:
  void prepare(int channels, int maxDelaySamples);
  void setDelay(int delaySamplesToUse);
  void reset();
  void process(juce::AudioBuffer<float> &buffer);
  int getDelaySamples() const noexcept { return delaySamples; }

private:
  juce::AudioBuffer<float> delayBuffer;
  int writePos = 0;
  int delaySamples = 0;
  int maxDelaySamples = 0;
  int numChannels = 0;
  int historySamples = 0;
};
