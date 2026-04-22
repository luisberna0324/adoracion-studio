#pragma once

#include "PitchShiftProcessor.h"
#include "StemTrack.h"
#include <JuceHeader.h>
#include <atomic>
#include <vector>

class StemMixEngine {
public:
  struct RenderStats {
    int processedPaths = 0;
    int inputFrames = 0;
    int paddedFrames = 0;
    int receivedFrames = 0;
    int receiveCalls = 0;
    int maxReceiveFrames = 0;
    int fifoAvailableBeforeFrames = 0;
    int fifoAvailableAfterFrames = 0;
    int targetLatencySamples = 0;
    int delayLineChanges = 0;
  };

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
                      float windowFadeEnd, RenderStats *stats = nullptr);

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
  int updateLatencyDelays(int melodicDelaySamples, int percussionDelaySamples,
                          int guideDelaySamples);

  juce::AudioBuffer<float> stemMixBuffer;
  juce::AudioBuffer<float> stemTempBuffer;
  juce::AudioBuffer<float> percussionBuffer;
  juce::AudioBuffer<float> dryBuffer;

  PitchShiftProcessor pitchShift;
  PitchShiftProcessor tempoShiftPercussion;
  PitchShiftProcessor tempoShiftGuide;
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
