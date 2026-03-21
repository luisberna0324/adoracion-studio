#pragma once

#include "AudioDiagnostics.h"
#include "PitchShiftProcessor.h"
#include "StemTrack.h"
#include <JuceHeader.h>
#include <array>

class MTControlsAudioProcessor : public juce::AudioProcessor {
public:
  MTControlsAudioProcessor();
  ~MTControlsAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;
  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String &newName) override;

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  juce::AudioProcessorValueTreeState &getValueTreeState();
  void getOutputMeterLevels(float &left, float &right) const;
  static juce::AudioProcessorValueTreeState::ParameterLayout
  createParameterLayout();

  bool loadSongStems(const juce::StringArray &stemPaths,
                     const juce::String &songTitle, const juce::String &songKey,
                     double songBpm);
  void playSong();
  void pauseSong();
  void stopSong();
  void toggleSongPlayPause();
  void seekSongRelative(double deltaSeconds);
  void seekSongTo(double positionSeconds);
  void setSongStartSeconds(double seconds) noexcept;
  void setSongEndSeconds(double seconds) noexcept;
  void setSongFadeOutSeconds(double seconds) noexcept;
  double getSongStartSeconds() const noexcept;
  double getSongEndSeconds() const noexcept;
  double getSongFadeOutSeconds() const noexcept;
  void setFadeActive(bool shouldFadeOut) noexcept;
  void setFadeStopsPlayback(bool shouldStop) noexcept;
  bool isFadeStopsPlayback() const noexcept;
  bool isFadeActive() const noexcept;
  void setMetronomeEnabled(bool enabled) noexcept;
  bool isMetronomeEnabled() const noexcept;
  void setMetronomeBpm(double bpm) noexcept;
  double getMetronomeBpm() const noexcept;
  void setMetronomeBeatsPerBar(int beats) noexcept;
  void setMonoClickMode(bool enabled) noexcept;
  bool isMonoClickMode() const noexcept;
  void setStemClickGuideMask(const std::vector<bool> &mask) noexcept;
  void setStemPitchShiftMask(const std::vector<bool> &mask) noexcept;
  void setPitchSemiTones(int semitoneDelta) noexcept;
  int getPitchSemiTones() const noexcept;

  bool hasLoadedSong() const noexcept;
  bool isSongPlaying() const noexcept;
  double getSongCurrentPositionSeconds() const;
  double getSongLengthSeconds() const;
  double getLoadedSongBpm() const;
  juce::String getLoadedSongKey() const;
  juce::String getLoadedSongTitle() const;
  int getLoadedStemCount() const noexcept;
  float getStemLevel(int stemIndex) const noexcept;
  void setStemVolume(int stemIndex, float volume) noexcept;
  float getStemVolume(int stemIndex) const noexcept;
  void setStemGain(int stemIndex, float gain) noexcept;
  float getStemGain(int stemIndex) const noexcept;
  void setStemMute(int stemIndex, bool muted) noexcept;
  bool isStemMuted(int stemIndex) const noexcept;
  void setStemSolo(int stemIndex, bool soloed) noexcept;
  bool isStemSoloed(int stemIndex) const noexcept;
  void setMasterVolume(float volume) noexcept;
  float getMasterVolume() const noexcept;
  void setMasterGain(float gain) noexcept;
  float getMasterGain() const noexcept;
  void setMasterMute(bool muted) noexcept;
  bool isMasterMuted() const noexcept;

  static constexpr const char *inputGainId = "inputGain";
  static constexpr const char *mixId = "mix";
  static constexpr const char *bypassId = "bypass";

private:
  static constexpr int maxStemCount = 64;
  enum class TransportState : int { stopped = 0, playing, paused };

  struct SimpleDelayLine {
    void prepare(int channels, int maxDelaySamples);
    void setDelay(int delaySamplesToUse);
    void reset();
    void process(juce::AudioBuffer<float> &buffer);
    int getDelaySamples() const noexcept { return delaySamples; }

  private:
    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
    int delaySamples = 0;
    int maxDelaySamples = 0;
    int numChannels = 0;
  };

  juce::AudioProcessorValueTreeState apvts;
  juce::dsp::Gain<float> gain;
  juce::AudioBuffer<float> dryBuffer;
  juce::AudioBuffer<float> stemMixBuffer;
  juce::AudioBuffer<float> stemTempBuffer;
  juce::AudioBuffer<float> percussionBuffer;
  std::atomic<float> meterLevelLeft{0.0f};
  std::atomic<float> meterLevelRight{0.0f};

  juce::AudioFormatManager playbackFormatManager;
  std::vector<std::unique_ptr<StemTrack>> stemTracks;
  juce::MixerAudioSource mixerSource;
  std::array<std::atomic<float>, maxStemCount> stemLevels;
  std::array<std::atomic<float>, maxStemCount> stemVolumes;
  std::array<std::atomic<float>, maxStemCount> stemGains;
  std::array<std::atomic<bool>, maxStemCount> stemMuteStates;
  std::array<std::atomic<bool>, maxStemCount> stemSoloStates;
  std::array<std::atomic<bool>, maxStemCount> stemClickGuideStates;
  std::array<std::atomic<bool>, maxStemCount> stemPitchShiftStates;
  std::atomic<int> loadedStemCount{0};
  std::atomic<float> masterVolume{1.0f};
  std::atomic<float> masterGain{1.0f};
  std::atomic<bool> masterMuted{false};
  PitchShiftProcessor pitchShift;
  std::atomic<int> pitchTargetSemiTones{0};
  int pitchActiveSemiTones = 0;
  SimpleDelayLine pitchDelayPercussion;
  SimpleDelayLine pitchDelayGuide;
  std::atomic<int> transportState{static_cast<int>(TransportState::stopped)};
  std::atomic<double> songCurrentPositionSeconds{0.0};
  // SpinLock protects stemTransportSources vector modifications.
  // Unlike CriticalSection, SpinLock does NOT make kernel calls, so it cannot
  // cause priority inversion or sleep the audio thread.
  mutable juce::SpinLock stemListLock;
  // ReadWriteLock for the non-atomic Song metadata strings.
  mutable juce::ReadWriteLock songMetaLock;
  double playbackSampleRate = 0.0;
  int playbackBlockSize = 0;
  std::atomic<double> loadedSongLengthSeconds{0.0};
  std::atomic<double> loadedSongBpm{0.0};
  std::atomic<double> songStartSeconds{0.0};
  std::atomic<double> songEndSeconds{0.0};
  std::atomic<double> songFadeOutSeconds{0.0};
  juce::String loadedSongKey;
  juce::String loadedSongTitle;
  std::atomic<bool> songLoaded{false};
  
  // Custom hybrid transport state
  std::atomic<juce::int64> globalSamplePosition{0};
  std::atomic<juce::int64> requestedSeekPosition{-1};
  
  // Lock-free seek atomics
  std::atomic<bool> seekPending{false};
  std::atomic<double> seekTargetSeconds{0.0};

  // Metronome
  std::atomic<bool> metronomeEnabled{false};
  std::atomic<double> metronomeBpm{120.0};
  std::atomic<int> metronomeBeatsPerBar{4};
  std::atomic<bool> monoClickMode{false};
  double metronomePhase = 0.0;
  int metronomeClickSamplesRemaining = 0;
  int metronomeBeatIndex = 0;
  float metronomeClickGain = 0.12f;

  // Master fade (0..1) multiplier
  std::atomic<bool> fadeActive{false};
  std::atomic<float> fadeTarget{1.0f};
  float fadeGain = 1.0f;
  const double fadeDurationSec = 2.0;
  std::atomic<bool> fadeStopsPlayback{true};

  // Audio performance diagnostics (lock-free ring buffer + background logger)
  AudioDiagnostics diagnostics;

  void clearLoadedSongState();
  void removeAllStemSources();
  void resetStemControls(int activeStemCount) noexcept;
  TransportState getTransportState() const noexcept;
  void setTransportState(TransportState state) noexcept;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MTControlsAudioProcessor)
};
