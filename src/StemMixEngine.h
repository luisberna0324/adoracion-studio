#pragma once

#include "PitchShiftProcessor.h"
#include "StemTrack.h"
#include <JuceHeader.h>
#include <atomic>
#include <vector>

class StemMixEngine {
public:
  void prepare(double sampleRate, int samplesPerBlock, int numChannels);
  void reset();
  void setPitchSemiTones(int semitones);
  void setTempoRatio(double ratio);

  void mixStems(const std::vector<std::unique_ptr<StemTrack>> &stems,
                const std::atomic<bool> *clickGuideStates,
                const std::atomic<bool> *pitchShiftStates, int stemCount,
                int numSamples, int numChannels);

  void renderToOutput(juce::AudioBuffer<float> &output, bool timeProcessActive,
                      float fadeStart, float fadeEnd, float windowFadeStart,
                      float windowFadeEnd);

private:
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
  };

  void ensureBuffers(int numChannels, int numSamples);
  void applyFade(juce::AudioBuffer<float> &buffer, float start, float end);
  void updateLatencyDelays(int latencySamples);

  juce::AudioBuffer<float> stemMixBuffer;
  juce::AudioBuffer<float> stemTempBuffer;
  juce::AudioBuffer<float> percussionBuffer;
  juce::AudioBuffer<float> dryBuffer;

  PitchShiftProcessor pitchShift;
  SimpleDelayLine pitchDelayPercussion;
  SimpleDelayLine pitchDelayGuide;
  int lastLatencySamples = 0;

  double currentSampleRate = 0.0;
  int currentBlockSize = 0;
  int currentChannels = 0;
  int pitchSemiTones = 0;
  double tempoRatio = 1.0;

  static constexpr int kMaxPitchLatencySamples = 65536;
};
