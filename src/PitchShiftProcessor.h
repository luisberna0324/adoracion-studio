#pragma once

#include <JuceHeader.h>
#include <SoundTouch.h>
#include <vector>

class PitchShiftProcessor {
public:
  struct ProcessStats {
    int inputFrames = 0;
    int requestedOutputFrames = 0;
    int receivedFrames = 0;
    int paddedFrames = 0;
    int receiveCalls = 0;
    int maxReceiveFrames = 0;
    int fifoAvailableBeforeFrames = 0;
    int fifoAvailableAfterFrames = 0;
  };

  void prepare(double sampleRate, int samplesPerBlock, int numChannels);
  void reset();
  void setPitchSemiTones(int semitones);
  void setTempoRatio(double ratio);
  // Shift this instance's SoundTouch internal overlap-add window phase by
  // pumping `samples` frames of silence through it on every prime. Used to
  // desynchronize many parallel processors so their per-window CPU peaks
  // don't land on the same processBlock call.
  void setPhaseOffsetSamples(int samples) noexcept;
  int getPhaseOffsetSamples() const noexcept;
  int getPitchSemiTones() const noexcept;
  double getTempoRatio() const noexcept;
  int getLatencySamples() const noexcept;
  int getNumChannels() const noexcept;
  bool isPrepared() const noexcept;
  void process(juce::AudioBuffer<float> &buffer);
  void process(juce::AudioBuffer<float> &buffer, int outputSamples,
               ProcessStats *stats = nullptr);

private:
  void primeLatency();
  void ensureOutputFifoCapacity(size_t minSamples);
  void pushOutputSamples(const float *samples, size_t sampleCount);
  void pushSilenceSamples(size_t sampleCount);
  float popOutputSample();

  soundtouch::SoundTouch soundtouch;
  std::vector<float> interleavedInput;
  std::vector<float> receiveBuffer;
  std::vector<float> outputFifo;
  size_t outputReadPos = 0;
  size_t outputWritePos = 0;
  size_t outputAvailableSamples = 0;

  double currentSampleRate = 0.0;
  int currentChannels = 0;
  int preferredBlockSamples = 0;
  int pitchSemiTones = 0;
  double tempoRatio = 1.0;
  int latencySamples = 0;
  int phaseOffsetSamples = 0;
  bool prepared = false;
};
