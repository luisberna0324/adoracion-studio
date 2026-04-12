#pragma once

#include "AudioDiagnostics.h"
#include "MetronomeEngine.h"
#include "StemMixEngine.h"
#include "StemTrack.h"
#include "data/SongData.h"
#include "audio/SimpleDelayLine.h"
#include "audio/StandaloneAudioDeviceBridge.h"
#include "audio/StemOutputRouter.h"
#include "audio/StemOutputRoutingState.h"
#include <JuceHeader.h>
#include <array>
#include <functional>

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

  bool loadSongStems(
      const juce::StringArray &stemPaths, const juce::String &songTitle,
      const juce::String &songKey, double songBpm,
      const std::function<void(double progress01)> &onLoadProgress = {});
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
  void setSongArrangement(const SongArrangementSnapshot &arrangement,
                          bool isCustomArrangement);
  void requestArrangementPartJump(int partIndex);
  int getPendingArrangementPartJumpIndex() const noexcept;
  bool isCustomArrangementActive() const noexcept;
  void setFadeActive(bool shouldFadeOut) noexcept;
  void setFadeStopsPlayback(bool shouldStop) noexcept;
  bool isFadeStopsPlayback() const noexcept;
  bool isFadeActive() const noexcept;
  void setMetronomeEnabled(bool enabled) noexcept;
  bool isMetronomeEnabled() const noexcept;
  void setMetronomeBpm(double bpm) noexcept;
  double getMetronomeBpm() const noexcept;
  void setMetronomeBeatsPerBar(int beats) noexcept;
  void setMetronomeVolume(float volume) noexcept;
  float getMetronomeVolume() const noexcept;
  void setMetronomeMuted(bool muted) noexcept;
  bool isMetronomeMuted() const noexcept;
  void setMetronomeGain(float gainValue) noexcept;
  float getMetronomeGain() const noexcept;
  void setMetronomeSolo(bool soloed) noexcept;
  bool isMetronomeSolo() const noexcept;
  void setMetronomeOutputRouting(int outputChannel, int width) noexcept;
  void setMetronomeClickStemIndex(int stemIndexOrMinusOne) noexcept;
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
  void setStemOutputChannel(int stemIndex, int outputChannel) noexcept;
  int getStemOutputChannel(int stemIndex) const noexcept;
  void setStemOutputWidth(int stemIndex, int width) noexcept;
  int getStemOutputWidth(int stemIndex) const noexcept;
  juce::StringArray getAvailableOutputChannelNames() const;
  int getAvailableOutputChannelCount() const noexcept;
  AppAudioMode getCurrentAudioMode() const noexcept;
  StandaloneAudioDeviceBridge &getAudioDeviceBridge() noexcept;
  const StandaloneAudioDeviceBridge &getAudioDeviceBridge() const noexcept;
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

  juce::AudioProcessorValueTreeState apvts;
  juce::dsp::Gain<float> gain;
  juce::AudioBuffer<float> dryMixBuffer;
  std::atomic<float> meterLevelLeft{0.0f};
  std::atomic<float> meterLevelRight{0.0f};
  std::atomic<float> metronomeVolumeState{1.0f};
  std::atomic<bool> metronomeMutedState{false};
  std::atomic<float> metronomeGainState{1.0f};
  std::atomic<bool> metronomeSoloState{false};
  std::atomic<int> metronomeOutputChannel{1};
  std::atomic<int> metronomeOutputWidth{1};
  std::atomic<int> metronomeClickStemIndex{-1};

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
  std::array<std::atomic<int>, maxStemCount> stemOutputChannels;
  std::array<std::atomic<int>, maxStemCount> stemOutputWidths;
  std::array<PitchShiftProcessor, maxStemCount> stemPitchProcessors;
  std::array<SimpleDelayLine, maxStemCount> stemPitchDelayLines;
  std::atomic<int> loadedStemCount{0};
  std::atomic<float> masterVolume{1.0f};
  std::atomic<float> masterGain{1.0f};
  std::atomic<bool> masterMuted{false};
  StemMixEngine mixEngine;
  StemOutputRouter stemOutputRouter;
  StemOutputRoutingState stemOutputRoutingState;
  StandaloneAudioDeviceBridge standaloneAudioDeviceBridge;
  juce::AudioBuffer<float> stemRoutingTempBuffer;
  juce::AudioBuffer<float> metronomeRenderBuffer;
  std::atomic<int> pitchTargetSemiTones{0};
  int pitchActiveSemiTones = 0;
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

  struct ArrangementRuntimePart {
    double startSec = 0.0;
    double endSec = 0.0;
  };
  std::vector<ArrangementRuntimePart> arrangementParts;
  std::atomic<bool> arrangementCustomActive{false};
  std::atomic<int> arrangementCurrentPartIndex{0};
  std::atomic<int> arrangementPendingJumpPartIndex{-1};
  std::atomic<double> arrangementTotalLengthSeconds{0.0};
  
  // Custom hybrid transport state
  std::atomic<juce::int64> globalSamplePosition{0};
  std::atomic<juce::int64> requestedSeekPosition{-1};
  
  // Lock-free seek atomics
  std::atomic<bool> seekPending{false};
  std::atomic<double> seekTargetSeconds{0.0};

  MetronomeEngine metronome;

  // Master fade (0..1) multiplier
  std::atomic<bool> fadeActive{false};
  std::atomic<float> fadeTarget{1.0f};
  float fadeGain = 1.0f;
  const double fadeDurationSec = 2.0;
  std::atomic<bool> fadeStopsPlayback{true};
  int routingPitchLatencySamples = 0;
  static constexpr int kMaxRoutingPitchLatencySamples = 65536;

  // Audio performance diagnostics (lock-free ring buffer + background logger)
  AudioDiagnostics diagnostics;

  void clearLoadedSongState();
  void removeAllStemSources();
  void resetStemControls(int activeStemCount) noexcept;
  int findArrangementPartIndexForSourceTime(double sourceSeconds) const noexcept;
  double getArrangementStartSeconds() const noexcept;
  TransportState getTransportState() const noexcept;
  void setTransportState(TransportState state) noexcept;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MTControlsAudioProcessor)
};
