#pragma once

#include "StemTrack.h"
#include "audio/TimeStretchProcessor.h"
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
                int inputSamples, int numChannels);
  void renderDirect(const std::vector<std::unique_ptr<StemTrack>> &stems,
                    int stemCount, juce::AudioBuffer<float> &output);

  void renderToOutput(juce::AudioBuffer<float> &output, bool timeProcessActive,
                      int inputSamples,
                      float fadeStart, float fadeEnd, float windowFadeStart,
                      float windowFadeEnd);
  int requiredInputFrames(int outputFrames) noexcept;

private:
  class SimpleDelayLine {
  public:
    void prepare(int channels, int maxDelaySamples);
    void setDelay(int delaySamplesToUse);
    void reset();
    void process(juce::AudioBuffer<float> &buffer, int numSamples);
    int getDelaySamples() const noexcept { return delaySamples; }

  private:
    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
    int delaySamples = 0;
    int maxDelaySamples = 0;
    int numChannels = 0;
  };

  void ensureBuffers(int numChannels, int numSamples);
  void applyFade(juce::AudioBuffer<float> &buffer, int numSamples, float start,
                 float end);
  void updateLatencyDelays(int melodicLatency, int percussionLatency,
                           int dryLatency);

  juce::AudioBuffer<float> stemMixBuffer;
  juce::AudioBuffer<float> stemTempBuffer;
  juce::AudioBuffer<float> percussionBuffer;
  juce::AudioBuffer<float> dryBuffer;
  juce::AudioBuffer<float> processedBuffer;

  TimeStretchProcessor pitchShiftMelodic;
  TimeStretchProcessor tempoShiftPercussion;
  TimeStretchProcessor tempoShiftGuide;
  SimpleDelayLine pitchDelayMelodic;
  SimpleDelayLine pitchDelayPercussion;
  SimpleDelayLine pitchDelayGuide;
  int lastMelodicDelaySamples = 0;
  int lastPercussionDelaySamples = 0;
  int lastGuideDelaySamples = 0;

  double currentSampleRate = 0.0;
  int currentBlockSize = 0;
  int currentChannels = 0;
  int pitchSemiTones = 0;
  double tempoRatio = 1.0;

  static constexpr int kMaxPitchLatencySamples = 65536;
};
