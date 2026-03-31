#pragma once

#include <JuceHeader.h>
#include <atomic>

class MetronomeEngine {
public:
  void prepare(double sampleRate);
  void reset();

  void setEnabled(bool enabled) noexcept;
  bool isEnabled() const noexcept;

  void setBpm(double bpm) noexcept;
  double getBpm() const noexcept;

  void setBeatsPerBar(int beats) noexcept;
  int getBeatsPerBar() const noexcept;

  void syncPhaseForTimeSeconds(double seconds) noexcept;
  void process(juce::AudioBuffer<float> &buffer) noexcept;

private:
  std::atomic<bool> enabled{false};
  std::atomic<double> bpm{120.0};
  std::atomic<int> beatsPerBar{4};

  double phase = 0.0;
  int clickSamplesRemaining = 0;
  int beatIndex = 0;
  float clickGain = 0.12f;
  double sampleRate = 0.0;
};
