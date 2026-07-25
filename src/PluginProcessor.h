#pragma once

#include "AudioDiagnostics.h"
#include "MetronomeEngine.h"
#include "StemMixEngine.h"
#include "StemTrack.h"
#include "audio/PadsEngine.h"
#include "audio/DynamicGuideEngine.h"
#include "data/SongData.h"
#include "data/AppSettings.h"
#include "data/SongLibraryStore.h"
#include "audio/SimpleDelayLine.h"
#include "audio/SongTransitionEnvelope.h"
#include "audio/StandaloneAudioDeviceBridge.h"
#include "audio/SectionScheduler.h"
#include "audio/StemOutputRouter.h"
#include "audio/StemOutputRoutingState.h"
#include "audio/TimeStretchProcessor.h"
#include "audio/TransportClock.h"
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
  void getDynamicGuideMeterLevels(float &left, float &right) const noexcept;
  void getMetronomeMeterLevels(float &left, float &right) const noexcept;
  static juce::AudioProcessorValueTreeState::ParameterLayout
  createParameterLayout();

  bool loadSongStems(
      const juce::StringArray &stemPaths, const juce::String &songTitle,
      const juce::String &songKey, double songBpm,
      const std::function<void(double progress01)> &onLoadProgress = {},
      bool deferTransitionFadeInUntilConfigured = false);
  void finishPendingSongTransition();
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
  void clearPendingArrangementPartJump() noexcept;
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
  int getMetronomeOutputChannel() const noexcept;
  int getMetronomeOutputWidth() const noexcept;
  void setMetronomeClickStemIndex(int stemIndexOrMinusOne) noexcept;
  bool loadMetronomeClickSamplesFromFolder(const juce::File &folder,
                                           const juce::String &style,
                                           juce::String *errorMessage = nullptr);
  void clearMetronomeClickSamples() noexcept;
  bool hasMetronomeClickSamples() const noexcept;
  void setClickDivision(int division) noexcept;  // bitmask: 1=1/4, 2=1/8, 4=1/16
  int getClickDivision() const noexcept;
  void setClickDivisionEnabled(bool enabled) noexcept;
  bool isClickDivisionEnabled() const noexcept;
  void setStemClickGuideMask(const std::vector<bool> &mask) noexcept;
  void setStemGuideMask(const std::vector<bool> &mask) noexcept;
  void setStemPitchShiftMask(const std::vector<bool> &mask) noexcept;
  void setDynamicGuideSettings(const DynamicGuideSettings &settings);
  DynamicGuideSettings getDynamicGuideSettings() const;
  void setDynamicGuideLanguage(AppLanguage language);
  void setArrangementEditingActive(bool editingActive) noexcept;
  bool consumeDynamicGuideMissingSectionWarning(juce::String &outSectionName);
  void setDynamicGuideVolume(float volume) noexcept;
  float getDynamicGuideVolume() const noexcept;
  void setDynamicGuideGain(float gainValue) noexcept;
  float getDynamicGuideGain() const noexcept;
  void setDynamicGuideMuted(bool muted) noexcept;
  bool isDynamicGuideMuted() const noexcept;
  void setDynamicGuideSolo(bool soloed) noexcept;
  bool isDynamicGuideSolo() const noexcept;
  void setDynamicGuideOutputRouting(int outputChannel, int width) noexcept;
  int getDynamicGuideOutputChannel() const noexcept;
  int getDynamicGuideOutputWidth() const noexcept;
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
  struct MidiInputProbe {
    juce::int64 timestampMs = 0;
    int channel = 0;
    int data1 = -1;
    int data2 = -1;
    int type = 0; // 1=CC, 2=NoteOn, 3=NoteOff, 4=Other
  };
  std::vector<MidiTargetDescriptor> getMidiTargets() const;
  std::vector<MidiBinding> getMidiBindings() const;
  MidiInputProbe getLastMidiInputProbe() const noexcept;
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
  static constexpr int midiTargetMasterGain = 12;
  static constexpr int midiTargetUiWindowModeToggle = 13;
  static constexpr int midiTargetUiSettingsToggle = 14;
  static constexpr int midiTargetUiSongWindowPopover = 15;
  static constexpr int midiTargetUiKeyPopover = 16;
  static constexpr int midiTargetUiMetronomePopover = 17;
  static constexpr int midiTargetSongStartMinus = 18;
  static constexpr int midiTargetSongStartPlus = 19;
  static constexpr int midiTargetPadsPower = 20;
  static constexpr int midiTargetPadsVolume = 21;
  static constexpr int midiTargetPadsGain = 22;
  static constexpr int midiTargetSongEndMinus = 23;
  static constexpr int midiTargetSongEndPlus = 24;
  static constexpr int midiTargetSongFadeMinus = 25;
  static constexpr int midiTargetSongFadePlus = 26;
  static constexpr int midiTargetTempoDown = 27;
  static constexpr int midiTargetTempoUp = 28;
  static constexpr int midiTargetPitchDown = 29;
  static constexpr int midiTargetPitchUp = 30;
  static constexpr int midiTargetMetronomeGain = 31;
  static constexpr int midiTargetMetronomeSolo = 32;
  static constexpr int midiTargetMixerNudgeUp = 33;
  static constexpr int midiTargetMixerNudgeDown = 34;
  static constexpr int midiTargetMixerApplyRouting = 35;
  static constexpr int midiTargetWaveformEditToggle = 36;
  static constexpr int midiTargetClickDynDivisionQuarter = 37;
  static constexpr int midiTargetClickDynDivisionEighth = 38;
  static constexpr int midiTargetClickDynDivisionSixteenth = 39;
  static constexpr int midiTargetStemVolumeBase = 100;
  static constexpr int midiTargetStemGainBase = 300;
  static constexpr int midiTargetStemMuteBase = 400;
  static constexpr int midiTargetStemSoloBase = 500;
  static constexpr int midiTargetPadsNoteBase = 200;
  static constexpr int midiUiCommandPrevSong = 1;
  static constexpr int midiUiCommandNextSong = 2;
  static constexpr int midiUiCommandToggleWindowMode = 3;
  static constexpr int midiUiCommandToggleSettings = 4;
  static constexpr int midiUiCommandShowSongWindowPopover = 5;
  static constexpr int midiUiCommandShowKeyPopover = 6;
  static constexpr int midiUiCommandShowMetronomePopover = 7;
  static constexpr int midiUiCommandMixerNudgeUp = 8;
  static constexpr int midiUiCommandMixerNudgeDown = 9;
  static constexpr int midiUiCommandMixerApplyRouting = 10;
  static constexpr int midiUiCommandWaveformEditToggle = 11;

private:
  static constexpr int maxStemCount = 64;
  static constexpr int maxRealtimeBlockSize = 8192;
  static constexpr int midiSourceKinds = 2; // CC + Note
  static constexpr int midiSourceStateSlots = midiSourceKinds * 16 * 128;
  enum class TransportState : int { stopped = 0, playing, paused };
  enum class SongLoadTransitionState : int {
    idle = 0,
    fadingOut,
    readyToSwap,
    holdingAtZero,
    fadingIn
  };

  juce::AudioProcessorValueTreeState apvts;
  juce::dsp::Gain<float> gain;
  juce::AudioBuffer<float> dryMixBuffer;
  std::atomic<float> meterLevelLeft{0.0f};
  std::atomic<float> meterLevelRight{0.0f};
  std::atomic<float> dynamicGuideMeterLevelLeft{0.0f};
  std::atomic<float> dynamicGuideMeterLevelRight{0.0f};
  std::atomic<float> metronomeMeterLevelLeft{0.0f};
  std::atomic<float> metronomeMeterLevelRight{0.0f};
  std::atomic<float> metronomeVolumeState{1.0f};
  std::atomic<bool> metronomeMutedState{false};
  std::atomic<float> metronomeGainState{1.0f};
  std::atomic<bool> metronomeSoloState{false};
  std::atomic<int> metronomeOutputChannel{1};
  std::atomic<int> metronomeOutputWidth{1};
  std::atomic<int> metronomeClickStemIndex{-1};
  std::atomic<int> clickDivisionState{1}; // bitmask: 1/4, 1/8, 1/16
  std::atomic<bool> clickDivisionEnabledState{true};
  std::atomic<float> dynamicGuideVolumeState{1.0f};
  std::atomic<float> dynamicGuideGainState{1.0f};
  std::atomic<bool> dynamicGuideMutedState{false};
  std::atomic<bool> dynamicGuideSoloState{false};
  std::atomic<int> dynamicGuideOutputChannelState{1};
  std::atomic<int> dynamicGuideOutputWidthState{2};

  juce::AudioFormatManager playbackFormatManager;
  std::vector<std::unique_ptr<juce::TimeSliceThread>> stemReadThreads;
  std::vector<std::unique_ptr<StemTrack>> stemTracks;
  std::array<std::atomic<float>, maxStemCount> stemLevels;
  std::array<std::atomic<float>, maxStemCount> stemVolumes;
  std::array<std::atomic<float>, maxStemCount> stemGains;
  std::array<std::atomic<bool>, maxStemCount> stemMuteStates;
  std::array<std::atomic<bool>, maxStemCount> stemSoloStates;
  std::array<std::atomic<bool>, maxStemCount> stemClickGuideStates;
  std::array<std::atomic<bool>, maxStemCount> stemGuideStates;
  std::array<std::atomic<bool>, maxStemCount> stemPitchShiftStates;
  std::array<std::atomic<int>, maxStemCount> stemOutputChannels;
  std::array<std::atomic<int>, maxStemCount> stemOutputWidths;
  std::array<TimeStretchProcessor, maxStemCount> stemPitchProcessors;
  std::array<SimpleDelayLine, maxStemCount> stemPitchDelayLines;
  std::atomic<int> loadedStemCount{0};
  std::atomic<float> masterVolume{1.0f};
  std::atomic<float> masterGain{1.0f};
  std::atomic<bool> masterMuted{false};
  StemMixEngine mixEngine;
  StemOutputRouter stemOutputRouter;
  StemOutputRoutingState stemOutputRoutingState;
  StandaloneAudioDeviceBridge standaloneAudioDeviceBridge;
  PadsEngine padsEngine;
  juce::AudioBuffer<float> stemRoutingInputBuffer;
  juce::AudioBuffer<float> stemRoutingTempStorage;
  juce::AudioBuffer<float> metronomeRenderStorage;
  juce::AudioBuffer<float> padsRenderStorage;
  juce::AudioBuffer<float> dynamicGuideRenderStorage;
  std::atomic<bool> padsEnabledState{true};
  std::atomic<float> padsVolume{1.0f};
  std::atomic<float> padsGain{0.75f};
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
  std::atomic<int> transportState{static_cast<int>(TransportState::stopped)};
  std::atomic<double> songCurrentPositionSeconds{0.0};
  std::atomic<bool> audioCallbackActive{false};
  std::atomic<int> songLoadTransitionState{
      static_cast<int>(SongLoadTransitionState::idle)};
  std::atomic<float> songLoadTransitionGain{1.0f};
  static constexpr double songLoadFadeOutDurationSec = 0.10;
  static constexpr double songLoadFadeInDurationSec = 0.15;
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
  std::atomic<int> guideAnnouncementBeatsPerBar{4};
  std::atomic<double> songStartSeconds{0.0};
  std::atomic<double> songEndSeconds{0.0};
  std::atomic<double> songFadeOutSeconds{0.0};
  juce::String loadedSongKey;
  juce::String loadedSongTitle;
  std::atomic<bool> songLoaded{false};
  mutable juce::CriticalSection midiBindingsLock;
  std::vector<MidiBinding> midiBindings;
  static constexpr int maxRealtimeMidiBindings = 512;
  struct MidiBindingSnapshot {
    int count = 0;
    std::array<MidiBinding, maxRealtimeMidiBindings> bindings{};
  };
  std::array<MidiBindingSnapshot, 3> midiBindingSnapshots{};
  std::atomic<int> publishedMidiBindingSnapshot{0};
  mutable std::atomic<int> readingMidiBindingSnapshot{-1};
  std::array<int, midiSourceStateSlots> midiLastSourceValues{};
  std::atomic<int> midiLearnTargetId{-1};
  std::atomic<int> midiLearnPendingTargetId{-1};
  std::atomic<int> midiLearnPendingChannel{1};
  std::atomic<int> midiLearnPendingSourceType{
      static_cast<int>(MidiBindingSourceType::CC)};
  std::atomic<int> midiLearnPendingCc{0};
  std::atomic<int> midiLearnPendingMode{0};
  std::atomic<int> midiPendingUiCommand{0};
  std::atomic<juce::int64> midiLastMessageMs{0};
  std::atomic<int> midiLastMessageChannel{0};
  std::atomic<int> midiLastMessageData1{-1};
  std::atomic<int> midiLastMessageData2{-1};
  std::atomic<int> midiLastMessageType{0};

  DynamicGuideEngine dynamicGuideEngine;
  mutable juce::SpinLock dynamicGuideStateLock;
  std::atomic<bool> dynamicGuideEnabledState{false};
  std::atomic<bool> arrangementEditingActiveState{false};
  std::atomic<int> dynamicGuideLanguageCode{0}; // 0=es,1=en
  DynamicGuideSettings dynamicGuideSettings;
  std::vector<bool> dynamicGuideAnnouncedByPart;
  double dynamicGuideLastBlockStartSec = -1.0;
  int dynamicGuideLastPartIndex = -1;
  bool dynamicGuideWasPlaying = false;
  juce::String dynamicGuideLastMissingSection;
  std::atomic<int> dynamicGuideMissingPartIndex{-1};
  mutable juce::SpinLock dynamicGuideWarningLock;

  struct ArrangementRuntimePart {
    juce::String id;
    juce::String name;
    juce::String sectionKey;
    int guideTriggerId = -1;
    double startSec = 0.0;
    double endSec = 0.0;
    double sourceStartSec = 0.0;
    double sourceEndSec = 0.0;
    bool insertedClip = false;
    bool muteInstruments = false;
    bool skipPlayback = false;
  };
  std::vector<ArrangementRuntimePart> arrangementParts;
  std::atomic<bool> arrangementCustomActive{false};
  std::atomic<int> arrangementCurrentPartIndex{0};
  std::atomic<int> arrangementPendingJumpPartIndex{-1};
  std::atomic<int> arrangementLoopPartIndex{-1};
  std::atomic<double> arrangementTotalLengthSeconds{0.0};
  
  // Frame-based source of truth. The legacy atomics remain as UI mirrors
  // during the incremental engine-v2 migration.
  playworship::audio::TransportClock transportClock;
  playworship::audio::SectionScheduler sectionScheduler;
  std::atomic<juce::int64> globalSamplePosition{0};
  std::atomic<juce::int64> requestedSeekPosition{-1};
  
  // Lock-free seek atomics
  std::atomic<bool> seekPending{false};
  std::atomic<double> seekTargetSeconds{0.0};

  MetronomeEngine metronome;
  SimpleDelayLine metronomeLatencyDelay;
  SimpleDelayLine dynamicGuideLatencyDelay;

  // Master fade (0..1) multiplier
  std::atomic<bool> fadeActive{false};
  std::atomic<float> fadeTarget{1.0f};
  float fadeGain = 1.0f;
  const double fadeDurationSec = 2.0;
  std::atomic<bool> fadeStopsPlayback{true};
  int routingPitchLatencySamples = 0;
  static constexpr int kMaxRoutingPitchLatencySamples = 65536;
  void syncMetronomeToSourceSeconds(double sourceSeconds) noexcept;

  // Audio performance diagnostics (lock-free ring buffer + background logger)
  AudioDiagnostics diagnostics;
  int stemTimingSnapshotDecimator = 0;
  static constexpr int kStemTimingSnapshotEveryBlocks = 20;

  void clearLoadedSongState();
  void removeAllStemSources();
  void resetStemControls(int activeStemCount) noexcept;
  bool loadPadsPackInternal(const juce::File &folderToLoad,
                            const juce::String &sourcePathForSettings,
                            bool persistSettings);
  static juce::String buildPadsPackHash(const juce::Array<juce::File> &files);
  bool reloadDynamicGuideAssets(juce::String *errorMessage = nullptr);
  juce::String currentDynamicGuideLanguageCode() const noexcept;
  int findFirstGuideStemIndex(int stemCount) const noexcept;
  void flushRoutingTimeProcessors() noexcept;
  void flushAllTimeProcessingState() noexcept;
  double getPlaybackTempoRatio() const noexcept;
  int findArrangementPartIndexForArrangementTime(double arrangementSeconds) const noexcept;
  juce::int64 mapArrangementFrameToSourceFrame(
      juce::int64 arrangementFrame) const noexcept;
  void rebuildSectionScheduler() noexcept;
  TransportState getTransportState() const noexcept;
  void setTransportState(TransportState state) noexcept;
  void processMidiMapping(const juce::MidiBuffer &midiMessages);
  void publishMidiBindingsLocked() noexcept;
  int acquireMidiBindingSnapshot() const noexcept;
  void releaseMidiBindingSnapshot() const noexcept;
  static int midiSourceStateIndex(MidiBindingSourceType sourceType, int channel,
                                  int data1) noexcept;
  static bool isMidiTargetValid(int targetId) noexcept;
  static MidiBindingMode defaultModeForTarget(int targetId) noexcept;
  static juce::String midiTargetNameForId(int targetId);
  void applyMidiBindingValue(int targetId, int ccValue, MidiBindingMode mode);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MTControlsAudioProcessor)
};
