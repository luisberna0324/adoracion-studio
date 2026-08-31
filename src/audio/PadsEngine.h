#pragma once

#include <JuceHeader.h>
#include <array>

class PadsEngine {
public:
  static constexpr int kNoteCount = 12;
  static constexpr const char *kNoteNames[kNoteCount] = {"C",  "C#", "D",  "D#",
                                                          "E",  "F",  "F#", "G",
                                                          "G#", "A",  "A#", "B"};

  /** Convierte tonalidades como C, F#, Bb o Am al índice cromático 0..11. */
  static int noteIndexForSongKey(const juce::String &songKey) noexcept;

  struct LoadResult {
    bool ok = false;
    juce::String statusText;
    int loadedNotes = 0;
  };

  enum class CrossfadePreset : int { Fast = 0, Medium, Long };

  void prepare(double sampleRate, int maxBlockSize);
  void releaseResources();

  void setEnabled(bool shouldEnable) noexcept;
  bool isEnabled() const noexcept { return enabled.load(std::memory_order_relaxed); }
  void setCrossfadePreset(CrossfadePreset preset) noexcept;
  CrossfadePreset getCrossfadePreset() const noexcept {
    return crossfadePreset.load(std::memory_order_relaxed);
  }
  double getCrossfadeDurationSeconds() const noexcept;

  /** Fades the complete pads bus around an automatic song transition. */
  void beginSongTransitionFadeOut() noexcept;
  void beginSongTransitionFadeIn() noexcept;

  LoadResult loadFromFolder(const juce::File &folder,
                            juce::AudioFormatManager &formatManager);
  bool ensureNoteLoaded(int noteIndex,
                        juce::AudioFormatManager &formatManager);
  bool isNoteLoaded(int noteIndex) const noexcept;

  void triggerNote(int noteIndex, int octave);
  /** Compatibility alias for a continuous song-key retune. */
  void triggerNoteForSongTransition(int noteIndex, int octave);
  void stopNote(int noteIndex);
  void stopAll();
  int getActiveNoteIndex() const noexcept {
    return activeNoteIndex.load(std::memory_order_relaxed);
  }

  void process(juce::AudioBuffer<float> &outputBuffer);
  float getOutputLevel() const noexcept { return outputLevel.load(std::memory_order_relaxed); }

private:
  friend struct PadsEngineTestPeer;

  enum class VoiceState : int { stopped = 0, fadingIn, playing, fadingOut };

  struct NoteSample {
    juce::AudioBuffer<float> data;
    juce::File sourceFile;
    double sourceSampleRate = 44100.0;
    bool valid = false;
  };

  struct Voice {
    VoiceState state = VoiceState::stopped;
    int noteIndex = -1;
    int octave = 0;
    double position = 0.0;
    double increment = 1.0;
    float gain = 0.0f;
    float fadeOutStartGain = 0.0f;
    float fadeOutProgress = 0.0f;
  };

  std::array<NoteSample, kNoteCount> noteSamples;
  std::array<Voice, kNoteCount> voices;
  double outputSampleRate = 44100.0;
  int maxBlock = 0;
  std::atomic<bool> enabled{true};
  std::atomic<float> outputLevel{0.0f};
  std::atomic<int> activeNoteIndex{-1};
  int fadeInSamples = 1;
  int fadeOutSamples = 1;
  std::atomic<CrossfadePreset> crossfadePreset{CrossfadePreset::Medium};
  std::atomic<float> songTransitionTargetGain{1.0f};
  float songTransitionGain = 1.0f;

  static int parseNoteIndexFromFilename(const juce::String &name);
  static int cleanlinessScore(const juce::String &name);
  static bool loadNoteSampleFromFile(NoteSample &target, const juce::File &file,
                                     juce::AudioFormatManager &formatManager);
  void updateFadeDurations() noexcept;
  void updateVoiceIncrement(Voice &voice);
  static void beginFadeOut(Voice &voice);
  void fadeOutAllActive();
  Voice *findBestVoiceForTrigger();
  bool hasActiveVoices() const noexcept;
  void applySongTransitionGain(juce::AudioBuffer<float> &outputBuffer);
  void renderVoice(Voice &voice, juce::AudioBuffer<float> &outputBuffer);
};
