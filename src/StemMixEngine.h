#pragma once

#include "StemTrack.h"
#include "audio/TimeStretchProcessor.h"
#include <JuceHeader.h>
#include <atomic>
#include <vector>

class StemMixEngine {
public:
  void prepare(double sampleRate, int samplesPerBlock, int numChannels);
  // Non-RT lifecycle operation. Releases grouped DSP voices, delay rings and
  // scratch buffers while preserving logical pitch/tempo settings.
  void releaseResources() noexcept;
  void reset();
  void setPitchSemiTones(int semitones);
  void setTempoRatio(double ratio);

  void mixStems(const std::vector<std::unique_ptr<StemTrack>> &stems,
                const std::atomic<bool> *clickGuideStates,
                const std::atomic<bool> *pitchShiftStates, int stemCount,
                int inputSamples, int numChannels,
                const std::atomic<int> *outputChannels = nullptr,
                const std::atomic<int> *outputWidths = nullptr);
  void renderDirect(const std::vector<std::unique_ptr<StemTrack>> &stems,
                    int stemCount, juce::AudioBuffer<float> &output);

  void renderToOutput(juce::AudioBuffer<float> &output, bool timeProcessActive,
                      int inputSamples, int outputSamples,
                      float fadeStart, float fadeEnd, float windowFadeStart,
                      float windowFadeEnd);
  int requiredInputFrames(int outputFrames) noexcept;
  int getOutputLatencySamples() const noexcept {
    return outputLatencySamples;
  }
  // Rebuilds retired Bungee spare voices. Control/background thread only.
  int servicePendingProcessorRebuilds() noexcept;
  uint64_t getTimeProcessorResetMissCount() const noexcept;
  uint64_t getTimeProcessorServiceFailureCount() const noexcept;
  bool wasLastRenderBlocked() const noexcept { return lastRenderBlocked; }

private:
  class SimpleDelayLine {
  public:
    static constexpr int kDelayTransitionSamples = 256;

    void prepare(int channels, int maxDelaySamples);
    void releaseResources() noexcept;
    void setDelay(int delaySamplesToUse);
    void reset();
    void invalidateHistory() noexcept {
      validHistorySamples = 0;
      currentDelaySamples = delaySamples;
      transitionFromDelaySamples = delaySamples;
      transitionToDelaySamples = delaySamples;
      transitionActive = false;
      transitionSample = 0;
    }
    // Clean state yields the full leading delay; delay zero still records.
    // Live changes crossfade between two ring-buffer read taps.
    void process(juce::AudioBuffer<float> &buffer, int numSamples);
    int getDelaySamples() const noexcept { return delaySamples; }

  private:
    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
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

  void ensureBuffers(int numChannels, int numSamples);
  void applyFade(juce::AudioBuffer<float> &buffer, int numSamples, float start,
                 float end);
  void updateLatencyDelays(int melodicOutputLatencySamples,
                           int percussionOutputLatencySamples,
                           int dryOutputLatencySamples);
  bool tryResolveRequiredProcessors(bool tempoActive) noexcept;
  void invalidateLatencyDelayHistory() noexcept;

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
  int outputLatencySamples = 0;
  bool lastRenderBlocked = false;

  double currentSampleRate = 0.0;
  int currentBlockSize = 0;
  int currentChannels = 0;
  int pitchSemiTones = 0;
  double tempoRatio = 1.0;

  static constexpr int kMaxPitchLatencySamples = 65536;
};
