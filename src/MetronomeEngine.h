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
  void setVolume(float volume) noexcept;
  float getVolume() const noexcept;
  void setMuted(bool muted) noexcept;
  bool isMuted() const noexcept;
  void triggerImmediateClick() noexcept;

  /** Set click subdivision: 1 = quarter, 2 = eighth, 4 = sixteenth. */
  void setSubdivision(int subdiv) noexcept;
  int getSubdivision() const noexcept;

  void syncPhaseForTimeSeconds(double seconds) noexcept;
  void process(juce::AudioBuffer<float> &buffer) noexcept;

private:
  std::atomic<bool> enabled{false};
  std::atomic<double> bpm{120.0};
  std::atomic<int> beatsPerBar{4};
  std::atomic<float> volume{1.0f};
  std::atomic<bool> muted{false};
  std::atomic<bool> pendingImmediateClick{false};
  std::atomic<int> subdivision{1};

  double phase = 0.0;
  double subPhase = 0.0;
  int clickSamplesRemaining = 0;
  int beatIndex = 0;
  int subIndex = 0;
  bool currentClickIsMainBeat = true;
  float clickGain = 0.12f;
  double sampleRate = 0.0;
};
