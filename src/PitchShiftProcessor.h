#pragma once

#include <JuceHeader.h>
#include <SoundTouch.h>
#include <vector>

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
  void configureQualitySettings();
  void refreshLatencyFromSettings();
  void primeLatency();

  soundtouch::SoundTouch soundtouch;
  std::vector<float> interleavedInput;
  std::vector<float> receiveBuffer;
  std::vector<float> outputFifo;
  size_t outputReadPos = 0;

  double currentSampleRate = 0.0;
  int currentChannels = 0;
  int pitchSemiTones = 0;
  double tempoRatio = 1.0;
  int latencySamples = 0;
  bool prepared = false;
};
