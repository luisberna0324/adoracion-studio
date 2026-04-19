#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

class PitchShiftBackend;

class PitchShiftProcessor {
public:
  void prepare(double sampleRate, int samplesPerBlock, int numChannels);
  void reset();
  void setPitchSemiTones(int semitones);
  void setTempoRatio(double ratio);
  int getPitchSemiTones() const noexcept;
  double getTempoRatio() const noexcept;
  int getLatencySamples() const noexcept;
  int getNumChannels() const noexcept;
  bool isPrepared() const noexcept;
  void process(juce::AudioBuffer<float> &buffer);

private:
  std::unique_ptr<PitchShiftBackend> backend;

  void primeLatency();

  double currentSampleRate = 0.0;
  int currentBlockSize = 0;
  int currentChannels = 0;
  int pitchSemiTones = 0;
  double tempoRatio = 1.0;
  int latencySamples = 0;
  bool prepared = false;
};
