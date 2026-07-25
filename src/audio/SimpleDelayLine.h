#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

class SimpleDelayLine {
public:
  void prepare(int channels, int maxDelaySamples);
  void setDelay(int delaySamplesToUse);
  void reset();
  // O(1) realtime invalidation. Existing ring contents remain allocated but
  // are treated as unavailable until enough new samples have been written.
  void invalidateHistory() noexcept { validHistorySamples = 0; }
  // A clean ring emits zero for the complete configured delay. Delay zero
  // remains transparent while still recording history for later live changes.
  void process(juce::AudioBuffer<float> &buffer);
  int getDelaySamples() const noexcept { return delaySamples; }

private:
  juce::AudioBuffer<float> delayBuffer;
  int writePos = 0;
  int delaySamples = 0;
  int maxDelaySamples = 0;
  int numChannels = 0;
  int validHistorySamples = 0;
};
