#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>

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
  void setDivisionMask(int mask) noexcept;
  int getDivisionMask() const noexcept;

  void syncPhaseForTimeSeconds(double seconds) noexcept;
  void process(juce::AudioBuffer<float> &buffer) noexcept;

  bool loadClickSamplesFromFolder(const juce::File &folder,
                                  const juce::String &style,
                                  juce::AudioFormatManager &formatManager,
                                  juce::String *errorMessage = nullptr);
  void clearClickSamples() noexcept;
  bool hasClickSamples() const noexcept;

private:
  enum class SampleRole { downbeat = 0, quarter, eighth, sixteenth };

  struct ClickSample {
    juce::AudioBuffer<float> data;
    double sourceSampleRate = 44100.0;
    float normalizationGain = 1.0f;

    bool isValid() const noexcept {
      return data.getNumSamples() > 0 && data.getNumChannels() > 0 &&
             sourceSampleRate > 0.0;
    }
  };

  struct ClickSampleSet {
    juce::String style;
    ClickSample downbeat;
    ClickSample quarter;
    ClickSample eighth;
    ClickSample sixteenth;
  };

  struct SampleVoice {
    std::shared_ptr<const ClickSampleSet> sampleSet;
    const ClickSample *sample = nullptr;
    double position = 0.0;
    double increment = 1.0;
    float gain = 1.0f;

    bool isActive() const noexcept {
      return sample != nullptr && sample->isValid();
    }
  };

  enum class ClickLayer { downbeat, eighth, sixteenth };
  enum class ClickTone { downbeat, eighth, sixteenth };

  std::atomic<bool> enabled{false};
  std::atomic<double> bpm{120.0};
  std::atomic<int> beatsPerBar{4};
  std::atomic<float> volume{1.0f};
  std::atomic<bool> muted{false};
  std::atomic<bool> pendingImmediateClick{false};
  std::atomic<int> divisionMask{1};
  std::atomic<int> subdivision{1};
  std::atomic<int> pendingSubdivision{-1}; // -1 = no pending change

  double phase = 0.0;
  double subPhase = 0.0;
  int clickSamplesRemaining = 0;
  int beatIndex = 0;
  int subIndex = 0;
  bool currentClickIsMainBeat = true;
  float clickGain = 0.22f;
  ClickTone currentClickTone = ClickTone::downbeat;
  double sampleRate = 0.0;

  std::shared_ptr<const ClickSampleSet> clickSamples;
  std::array<SampleVoice, 16> sampleVoices;

  static int normalizeDivisionMask(int mask) noexcept;
  static int normalizeSubdivision(int subdiv) noexcept;
  static bool loadSampleFile(const juce::File &file,
                             juce::AudioFormatManager &formatManager,
                             ClickSample &sample);
  static bool isLayerEnabled(int mask, ClickLayer layer) noexcept;
  void triggerClick(ClickLayer layer, bool mainBeat, float triggerGain) noexcept;
  const ClickSample *selectSampleForTrigger(
      const ClickSampleSet &sampleSet, ClickLayer layer) const noexcept;
  void triggerSyntheticClick(ClickLayer layer, bool mainBeat,
                             float triggerGain) noexcept;
  bool hasActiveSampleVoices() const noexcept;
  bool triggerSampleClick(std::shared_ptr<const ClickSampleSet> sampleSet,
                          const ClickSample *sample,
                          float triggerGain) noexcept;
  float renderSyntheticClick() noexcept;
  float renderSampleVoices() noexcept;
};
