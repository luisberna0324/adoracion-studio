#pragma once

#include "AudioDiagnostics.h"
#include "MetronomeEngine.h"
#include "StemMixEngine.h"
#include "StemTrack.h"
#include "audio/PadsEngine.h"
#include "data/SongData.h"
#include "data/AppSettings.h"
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
  void toggleArrangementPartLoop(int partIndex);
  void clearArrangementPartLoop() noexcept;
  int getArrangementPartLoopIndex() const noexcept;
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
  void setPlaybackTempoBpm(double bpm) noexcept;
  double getPlaybackTempoBpm() const noexcept;
  double getOriginalSongBpm() const noexcept;
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
  bool loadPadsPackFromFolder(const juce::File &sourceFolder);
  bool reloadLastPadsPack();
  bool ensurePadsPackReady();
  void refreshPadsRoutingFromSettings() noexcept;
  void setPadsEnabled(bool enabled) noexcept;
  bool isPadsEnabled() const noexcept;
  void setPadsVolume(float volume) noexcept;
  float getPadsVolume() const noexcept;
  void setPadsGain(float gain) noexcept;
  float getPadsGain() const noexcept;
  void setPadsCrossfadePreset(PadsEngine::CrossfadePreset preset) noexcept;
  PadsEngine::CrossfadePreset getPadsCrossfadePreset() const noexcept;
  void triggerPadNote(int noteIndex, int octave);
  void stopPadNote(int noteIndex);
  float getPadsOutputLevel() const noexcept;
  bool isPadsPackLoaded() const noexcept;
  juce::String getPadsPackStatusText() const;
  struct MidiTargetDescriptor {
    int id = -1;
    juce::String name;
    MidiBindingMode mode = MidiBindingMode::Absolute;
  };
  using MidiBinding = MidiBindingSetting;
  std::vector<MidiTargetDescriptor> getMidiTargets() const;
  std::vector<MidiBinding> getMidiBindings() const;
  void setMidiBinding(const MidiBinding &binding);
  void clearMidiBindingForTarget(int targetId);
  void beginMidiLearnForTarget(int targetId);
  bool consumePendingMidiLearnBinding(MidiBinding &outBinding);
  bool consumePendingMidiUiCommand(int &outCommand);

  static constexpr const char *inputGainId = "inputGain";
  static constexpr const char *mixId = "mix";
  static constexpr const char *bypassId = "bypass";
  static constexpr int midiTargetMasterVolume = 0;
  static constexpr int midiTargetMasterMute = 1;
  static constexpr int midiTargetTransportPlayPause = 2;
  static constexpr int midiTargetTransportStop = 3;
  static constexpr int midiTargetMetronomeEnabled = 4;
  static constexpr int midiTargetMetronomeVolume = 5;
  static constexpr int midiTargetMetronomeMute = 6;
  static constexpr int midiTargetTransportPrevSong = 7;
  static constexpr int midiTargetTransportSeekBack = 8;
  static constexpr int midiTargetTransportSeekForward = 9;
  static constexpr int midiTargetTransportNextSong = 10;
  static constexpr int midiTargetTransportFadeToggle = 11;
  static constexpr int midiTargetPadsPower = 20;
  static constexpr int midiTargetPadsVolume = 21;
  static constexpr int midiTargetStemVolumeBase = 100;
  static constexpr int midiTargetPadsNoteBase = 200;
  static constexpr int midiUiCommandPrevSong = 1;
  static constexpr int midiUiCommandNextSong = 2;

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
  std::array<PitchShiftProcessor, maxStemCount> outgoingStemPitchProcessors;
  std::array<SimpleDelayLine, maxStemCount> outgoingStemPitchDelayLines;
  std::atomic<int> loadedStemCount{0};
  std::atomic<float> masterVolume{1.0f};
  std::atomic<float> masterGain{1.0f};
  std::atomic<bool> masterMuted{false};
  StemMixEngine mixEngine;
  StemMixEngine outgoingMixEngine;
  StemOutputRouter stemOutputRouter;
  StemOutputRoutingState stemOutputRoutingState;
  StandaloneAudioDeviceBridge standaloneAudioDeviceBridge;
  PadsEngine padsEngine;
  juce::AudioBuffer<float> stemRoutingTempBuffer;
  juce::AudioBuffer<float> metronomeRenderBuffer;
  juce::AudioBuffer<float> padsRenderBuffer;
  std::atomic<bool> padsEnabledState{true};
  std::atomic<float> padsVolume{1.0f};
  std::atomic<float> padsGain{1.0f};
  std::atomic<bool> padsMuted{false};
  std::atomic<int> padsOutputChannel{1};
  std::atomic<int> padsOutputWidth{1};
  juce::String padsPackStatusText{"Not loaded"};
  std::atomic<bool> padsPackLoaded{false};
  mutable juce::SpinLock padsStateLock;
  std::atomic<int> pitchTargetSemiTones{0};
  int pitchActiveSemiTones = 0;
  std::atomic<double> playbackTempoBpm{120.0};
  double playbackTempoRatioActive = 1.0;
  std::atomic<bool> songCrossfadeActive{false};
  std::atomic<float> songCrossfadeProgress{1.0f};
  std::atomic<float> songCrossfadeIncomingGain{1.0f};
  std::atomic<float> songCrossfadeOutgoingGain{0.0f};
  std::vector<std::unique_ptr<StemTrack>> outgoingStemTracks;
  juce::MixerAudioSource outgoingMixerSource;
  juce::AudioBuffer<float> outgoingStemRenderBuffer;
  const double songCrossfadeDurationSec = 2.5;
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
  mutable juce::CriticalSection midiBindingsLock;
  std::vector<MidiBinding> midiBindings;
  std::array<int, 512> midiLastCcValues{};
  std::atomic<int> midiLearnTargetId{-1};
  std::atomic<int> midiLearnPendingTargetId{-1};
  std::atomic<int> midiLearnPendingChannel{1};
  std::atomic<int> midiLearnPendingCc{0};
  std::atomic<int> midiLearnPendingMode{0};
  std::atomic<int> midiPendingUiCommand{0};

  struct ArrangementRuntimePart {
    double startSec = 0.0;
    double endSec = 0.0;
  };
  std::vector<ArrangementRuntimePart> arrangementParts;
  std::atomic<bool> arrangementCustomActive{false};
  std::atomic<int> arrangementCurrentPartIndex{0};
  std::atomic<int> arrangementPendingJumpPartIndex{-1};
  std::atomic<int> arrangementLoopPartIndex{-1};
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
  int outgoingRoutingPitchLatencySamples = 0;
  static constexpr int kMaxRoutingPitchLatencySamples = 65536;

  // Audio performance diagnostics (lock-free ring buffer + background logger)
  AudioDiagnostics diagnostics;

  void clearLoadedSongState();
  void removeAllStemSources();
  void resetStemControls(int activeStemCount) noexcept;
  bool loadPadsPackInternal(const juce::File &folderToLoad,
                            const juce::String &sourcePathForSettings,
                            bool persistSettings);
  static juce::String buildPadsPackHash(const juce::Array<juce::File> &files);
  void flushRoutingTimeProcessors() noexcept;
  void flushAllTimeProcessingState() noexcept;
  double getPlaybackTempoRatio() const noexcept;
  int findArrangementPartIndexForSourceTime(double sourceSeconds) const noexcept;
  double getArrangementStartSeconds() const noexcept;
  TransportState getTransportState() const noexcept;
  void setTransportState(TransportState state) noexcept;
  void processMidiMapping(const juce::MidiBuffer &midiMessages);
  static bool isMidiTargetValid(int targetId) noexcept;
  static MidiBindingMode defaultModeForTarget(int targetId) noexcept;
  static juce::String midiTargetNameForId(int targetId);
  void applyMidiBindingValue(int targetId, int ccValue, MidiBindingMode mode);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MTControlsAudioProcessor)
};
