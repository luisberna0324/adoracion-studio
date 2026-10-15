#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

class MetronomeEngine {
public:
  void prepare(double sampleRate);
  // Direct reset is only valid while stopped or from the audio thread.
  void reset();
  // Control-thread-safe reset request consumed by the next audio-thread sync.
  void requestReset() noexcept;

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
  // Audio-thread API used by the transport to publish one coherent musical
  // beat position and effective playback BPM.
  void syncPhaseForBeatPosition(double beatPosition,
                                double effectiveBpm) noexcept;
  void process(juce::AudioBuffer<float> &buffer) noexcept;
  void process(juce::AudioBuffer<float> &buffer, int startSample,
               int numSamples) noexcept;

  bool loadClickSamplesFromFolder(const juce::File &folder,
                                  const juce::String &style,
                                  juce::AudioFormatManager &formatManager,
                                  juce::String *errorMessage = nullptr);
  void clearClickSamples() noexcept;
  bool hasClickSamples() const noexcept;

private:
#if defined(MTCONTROLS_ENABLE_TEST_HOOKS) && MTCONTROLS_ENABLE_TEST_HOOKS
  friend struct MetronomeEngineTestPeer;
#endif

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
  static_assert(std::atomic<const ClickSampleSet *>::is_always_lock_free,
                "Metronome sample publication requires lock-free pointers");

  struct SampleVoice {
    const ClickSampleSet *sampleSet = nullptr;
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
  std::atomic<int> requestedSubdivision{1};
  std::atomic<uint64_t> requestedResetGeneration{0};
  uint64_t appliedResetGeneration = 0;

  double phase = 0.0;
  double subPhase = 0.0;
  double activeBpm = 120.0;
  int clickSamplesRemaining = 0;
  int beatIndex = 0;
  int subIndex = 0;
  int activeSubdivision = 1;
  bool boundaryClickPending = false;
  bool subdivisionClickPending = false;
  int boundarySubdivision = 1;
  bool currentClickIsMainBeat = true;
  float clickGain = 0.22f;
  ClickTone currentClickTone = ClickTone::downbeat;
  double sampleRate = 0.0;

  // Published generations remain owned until engine destruction, so audio
  // voices only keep raw pointers to immutable buffers. Reload/clear never
  // destroys sample data from the realtime thread.
  juce::CriticalSection clickSamplesGenerationLock;
  std::vector<std::shared_ptr<const ClickSampleSet>> clickSampleGenerations;
  std::atomic<const ClickSampleSet *> publishedClickSamples{nullptr};
  std::array<SampleVoice, 16> sampleVoices;

  static int normalizeDivisionMask(int mask) noexcept;
  static int normalizeSubdivision(int subdiv) noexcept;
  static bool loadSampleFile(const juce::File &file,
                             juce::AudioFormatManager &formatManager,
                             ClickSample &sample);
  static bool isLayerEnabled(int mask, ClickLayer layer) noexcept;
  void resetAudioState() noexcept;
  void applyPendingReset() noexcept;
  void triggerClick(ClickLayer layer, bool mainBeat, float triggerGain) noexcept;
  const ClickSample *selectSampleForTrigger(
      const ClickSampleSet &sampleSet, ClickLayer layer) const noexcept;
  void triggerSyntheticClick(ClickLayer layer, bool mainBeat,
                             float triggerGain) noexcept;
  bool hasActiveSampleVoices() const noexcept;
  bool publishClickSamples(std::shared_ptr<const ClickSampleSet> nextSamples)
      noexcept;
  bool triggerSampleClick(const ClickSampleSet *sampleSet,
                          const ClickSample *sample,
                          float triggerGain) noexcept;
  float renderSyntheticClick() noexcept;
  float renderSampleVoices() noexcept;
};
