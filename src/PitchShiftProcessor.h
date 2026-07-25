#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <SoundTouch.h>
#include <cstdint>
#include <vector>

class PitchShiftProcessor {
public:
  enum class ContentType : uint8_t { Melodic = 0, Percussive = 1 };

  void prepare(double sampleRate, int samplesPerBlock, int numChannels);
  void reset();
  // SoundTouch resets in place and does not need an off-thread replacement
  // voice. This keeps the TimeStretchProcessor maintenance API uniform.
  bool servicePendingRebuild() noexcept { return false; }
  bool tryResolvePendingReset() noexcept { return isPrepared(); }
  uint64_t getResetMissCount() const noexcept { return 0; }
  uint64_t getServiceFailureCount() const noexcept { return 0; }
  void setPitchSemiTones(int semitones);
  void setTempoRatio(double ratio);
  void setContentType(ContentType type);
  int getPitchSemiTones() const noexcept;
  double getTempoRatio() const noexcept;
  int getLatencySamples() const noexcept;
  int getNumChannels() const noexcept;
  bool isPrepared() const noexcept;
  void process(juce::AudioBuffer<float> &buffer);
  int requiredInputFrames(int outputFrames) noexcept;
  int process(const juce::AudioBuffer<float> &input, int inputFrames,
              juce::AudioBuffer<float> &output, int outputFrames);

private:
  void configureQualitySettings();
  void refreshLatencyFromSettings();
  void primeLatency();
  int fifoFramesAvailable() const noexcept;
  void fifoClear() noexcept;
  void fifoPush(const float *samples, int frames) noexcept;
  int fifoPop(float *destination, int frames) noexcept;
  void fifoPushSilence(int frames) noexcept;

  soundtouch::SoundTouch soundtouch;
  std::vector<float> interleavedInput;
  std::vector<float> dryFallback;
  std::vector<float> receiveBuffer;
  std::vector<float> outputFifo;
  size_t outputReadPos = 0;
  size_t outputWritePos = 0;
  size_t outputSampleCount = 0;

  double currentSampleRate = 0.0;
  int currentChannels = 0;
  int maxBlockFrames = 0;
  int maxInputFrames = 0;
  int pitchSemiTones = 0;
  double tempoRatio = 1.0;
  ContentType contentType = ContentType::Melodic;
  int latencySamples = 0;
  int pitchTransitionFramesRemaining = 0;
  int pitchTransitionFramesTotal = 0;
  double inputFrameRemainder = 0.0;
  bool prepared = false;
};
