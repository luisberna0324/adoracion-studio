#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

class SimpleDelayLine {
public:
  // A short, fixed transition keeps delay changes deterministic and avoids
  // allocating transition buffers in the realtime path.
  static constexpr int kDelayTransitionSamples = 256;

  void prepare(int channels, int maxDelaySamples);
  // Non-RT lifecycle operation that returns the ring allocation.
  void releaseResources();
  void setDelay(int delaySamplesToUse);
  void reset();
  // O(1) realtime invalidation. Existing ring contents remain allocated but
  // are treated as unavailable until enough new samples have been written.
  void invalidateHistory() noexcept {
    validHistorySamples = 0;
    currentDelaySamples = delaySamples;
    transitionFromDelaySamples = delaySamples;
    transitionToDelaySamples = delaySamples;
    transitionActive = false;
    transitionSample = 0;
  }
  // A clean ring emits zero for the complete configured delay. Delay zero
  // remains transparent while still recording history for later live changes.
  // Live delay changes crossfade between the old and new read taps.
  void process(juce::AudioBuffer<float> &buffer);
  int getDelaySamples() const noexcept { return delaySamples; }

  /**
   * True while the delay line is actually altering the output. The audio
   * thread uses this to skip the per-block call when the delay line would
   * be a pure transparent memory copy (delay == 0 and no transition in
   * flight). Skipping the transparent copy is a measurable win when many
   * stems are routed but pitch / time-stretch is off.
   */
  bool isActive() const noexcept {
    return delaySamples > 0 || transitionActive ||
           currentDelaySamples > 0 || transitionFromDelaySamples > 0 ||
           transitionToDelaySamples > 0;
  }

private:
  juce::AudioBuffer<float> delayBuffer;
  int writePos = 0;
  // delaySamples is the latest requested value. currentDelaySamples is the
  // stable read tap before/after a transition.
  int delaySamples = 0;
  int currentDelaySamples = 0;
  int transitionFromDelaySamples = 0;
  int transitionToDelaySamples = 0;
  int transitionSample = 0;
  int maxDelaySamples = 0;
  int numChannels = 0;
  int validHistorySamples = 0;
  bool transitionActive = false;
};
