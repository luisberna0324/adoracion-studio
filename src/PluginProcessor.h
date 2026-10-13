#pragma once

#include "AudioDiagnostics.h"
#include "MetronomeEngine.h"
#include "StemMixEngine.h"
#include "StemTrack.h"
#include "audio/PadsEngine.h"
#include "audio/PracticeLoop.h"
#include "audio/DynamicGuideEngine.h"
#include "data/SongData.h"
#include "data/PracticeWaveform.h"
#include "data/AppSettings.h"
#include "data/SongLibraryStore.h"
#include "audio/SimpleDelayLine.h"
#include "audio/SongTransitionEnvelope.h"
#include "audio/StandaloneAudioDeviceBridge.h"
#include "audio/SectionScheduler.h"
#include "audio/SectionAutomation.h"
#include "audio/StemOutputRouter.h"
#include "audio/StemOutputRoutingState.h"
#include "audio/TimeStretchProcessor.h"
#include "audio/TransportClock.h"
#include <JuceHeader.h>
#include <thread>
#include <array>
#include <functional>

class MTControlsAudioProcessor : public juce::AudioProcessor,
                                 private juce::TimeSliceClient,
                                 private juce::Timer {
public:
  static constexpr int maxStemCount = 64;
  MTControlsAudioProcessor();
  ~MTControlsAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
  void setRemoteClientMode(bool enabled) noexcept {
    remoteClientMode.store(enabled, std::memory_order_release);
  }
  AudioDiagnostics::ReportFiles getAudioDiagnosticReportFiles() const {
    return diagnostics.getReportFiles();
  }
  uint64_t markAudioArtifact();

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
  void getMetronomeMeterLevels(float &left, float &right) noexcept;
  void getRemoteMetronomeMeterLevels(float &left, float &right) noexcept;
  static juce::AudioProcessorValueTreeState::ParameterLayout
  createParameterLayout();

  // songId is the stable identifier used by StemCache (UUID in
  // production). songTitle is the human-readable label shown in the
  // UI and used for the SongLoadProbe's "title=" extra. They are
  // separate parameters because the cache must NOT be keyed on the
  // mutable, user-editable title (see perf/analysis_20260912/INFORME.md
  // §4.1).
  bool loadSongStems(
      const juce::StringArray &stemPaths, const juce::String &songId,
      const juce::String &songTitle, const juce::String &songKey,
      double songBpm,
      const std::function<void(double progress01)> &onLoadProgress = {},
      bool deferTransitionFadeInUntilConfigured = false);
  /** Safely releases the active song and leaves the transport stopped. */
  bool unloadSong();
  void finishPendingSongTransition();
  void playSong();
  void pauseSong();
  void stopSong();
  void toggleSongPlayPause();
  void seekSongRelative(double deltaSeconds);
  void seekSongTo(double positionSeconds);
  // Control-side ownership of a temporary practice pause. Audio progression
  // does not invalidate it; explicit transport/window/song changes do.
  uint64_t getTransportControlRevision() const noexcept {
    return transportControlRevision.load(std::memory_order_acquire);
  }
  void markTransportControlIntent() noexcept {
    transportControlRevision.fetch_add(1, std::memory_order_release);
  }
  bool setPracticeLoopRange(double startSeconds, double endSeconds) noexcept;
  bool preparePracticeLoop(double startSeconds) noexcept;
  void clearPracticeLoop() noexcept;
  bool hasPracticeLoopPreparation() const noexcept;
  bool isPracticeLoopActive() const noexcept;
  juce::Range<double> getPracticeLoopRange() const noexcept;
  uint64_t getPracticeLoopRevision() const noexcept;
  double getMinimumPracticeLoopDuration() const noexcept;
  juce::String getPracticeLoopError() const;
  void setSongStartSeconds(double seconds) noexcept;
  void setSongEndSeconds(double seconds) noexcept;
  void setSongFadeOutSeconds(double seconds) noexcept;
  double getSongStartSeconds() const noexcept;
  double getSongEndSeconds() const noexcept;
  double getSongFadeOutSeconds() const noexcept;
  void setSongArrangement(const SongArrangementSnapshot &arrangement,
                          bool isCustomArrangement);
  void setSectionAutomationStemIds(const juce::StringArray &stemIds);
  bool beginSectionAutomationEditing(const juce::String &partId, bool remoteOwned = false);
  bool finishSectionAutomationEditing(bool save);
  bool clearSectionAutomation(const juce::String &partId);
  bool isSectionAutomationEditing() const noexcept;
  void setSectionAutomationRemoteOwned(bool remoteOwned) noexcept {
    sectionAutomationRemoteOwned.store(remoteOwned, std::memory_order_release);
  }
  juce::String getSectionAutomationEditingPartId() const;
  juce::String getSectionAutomationEditingPartName() const;
  SongArrangementSnapshot getSongArrangementSnapshot() const;
  // Control/UI-only immutable view of the actual playback mapping. No audio
  // is decoded and no scheduler/audio state is changed by this accessor.
  playworship::practice::WaveformTimeline getPracticeWaveformTimeline() const;
  bool isStemSectionAutomated(int stemIndex) const noexcept;
  bool isPadsSectionAutomated() const noexcept;
  float getStemBaseVolume(int stemIndex) const noexcept;
  float getStemBaseGain(int stemIndex) const noexcept;
  bool isStemBaseMuted(int stemIndex) const noexcept;
  float getPadsBaseVolume() const noexcept;
  float getPadsBaseGain() const noexcept;
  bool isPadsBaseEnabled() const noexcept;
  int getBasePadNoteIndex() const noexcept;
  int getBasePadOctave() const noexcept;
  int getActivePadOctave() const noexcept;
  bool requestArrangementPartJump(int partIndex);
  bool toggleArrangementPartLoop(int partIndex);
  bool requestArrangementPartJumpById(const juce::String &partId,
                                    const juce::String &expectedRevision = {});
  bool toggleArrangementPartLoopById(const juce::String &partId,
                                   const juce::String &expectedRevision = {});
  juce::String getArrangementPartId(int runtimeIndex) const;
  juce::String getArrangementControlRevision() const;
  void clearArrangementPartLoop() noexcept;
  void clearPendingArrangementPartJump() noexcept;
  int getArrangementPartLoopIndex() const noexcept;
  int getPreparingArrangementPartJumpIndex() const noexcept;
  std::pair<int, int> getArrangementJumpState() const noexcept;
  void setSectionEntryCacheBudget(juce::int64 bytes);
  juce::int64 getSectionEntryCacheBudget() const noexcept { return sectionEntryCacheBudgetBytes; }
  juce::int64 getSectionEntryCacheBytes() const;
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
  bool isDynamicGuideEnabled() const noexcept {
    return dynamicGuideEnabledState.load(std::memory_order_relaxed);
  }
  // Control/message-thread query, like guide loading. Does not touch disk or
  // infer availability from the selected source when its pack failed to load.
  bool hasDynamicGuideAssets() const noexcept {
    return dynamicGuideEngine.getLoadedSectionCount() > 0;
  }
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
  // Duration of the loaded audio source, before any arrangement extensions.
  double getLoadedSongLengthSeconds() const noexcept;
  double getSongLengthSeconds() const;
  bool isSongEndCustomized() const noexcept;
  double getLoadedSongBpm() const;
  juce::String getLoadedSongKey() const;
  juce::String getLoadedSongTitle() const;
  int getLoadedStemCount() const noexcept;
  // 0 restores the mix, 1 emphasizes the stem, 2 reduces it for accompaniment.
  // These listening modes never overwrite saved faders, gain, mute or solo.
  void setPracticeListeningMode(int stemIndex, int mode) noexcept;
  int getPracticeListeningStem() const noexcept;
  int getPracticeListeningMode() const noexcept;
  uint64_t getPracticeListeningRevision() const noexcept;
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
  bool ensurePadsPackReady(bool allowNetworkDownloads = true);
  bool ensurePadsNoteReady(int noteIndex, bool full = false);
  void startPadsPreloadAsync(bool allowNetworkDownloads = true);
  bool isPadsPreloadRunning() const noexcept;
  void refreshPadsRoutingFromSettings() noexcept;
  void setPadsEnabled(bool enabled) noexcept;
  bool isPadsEnabled() const noexcept;
  void setPadsVolume(float volume) noexcept;
  float getPadsVolume() const noexcept;
  void setPadsGain(float gain) noexcept;
  float getPadsGain() const noexcept;
  void setPadsCrossfadePreset(PadsEngine::CrossfadePreset preset) noexcept;
  PadsEngine::CrossfadePreset getPadsCrossfadePreset() const noexcept;
  double getPadsCrossfadeDurationSeconds() const noexcept;
  void beginPadsSongTransitionFadeOut() noexcept;
  void beginPadsSongTransitionFadeIn() noexcept;
  void triggerPadNote(int noteIndex, int octave);
  void triggerPadNoteForSongTransition(int noteIndex, int octave);
  void stopPadNote(int noteIndex);
  int getActivePadNoteIndex() const noexcept;
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
  static constexpr int midiUiCommandTransportPlayPause = 12;
  static constexpr int midiUiCommandTransportStop = 13;
  static constexpr int midiUiCommandTransportSeekBack = 14;
  static constexpr int midiUiCommandTransportSeekForward = 15;
  static constexpr int midiUiCommandTempoDown = 16;
  static constexpr int midiUiCommandTempoUp = 17;
  static constexpr int midiUiCommandPadsEnable = 18;
  static constexpr int midiUiCommandPadsDisable = 19;
  static constexpr int midiUiCommandPadsToggle = 20;
  static constexpr int midiUiCommandEnsurePadsPack = 21;

private:
  friend struct SectionAutomationTestAccess;
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
  std::atomic<float> remoteMetronomeMeterLeft{0.0f}, remoteMetronomeMeterRight{0.0f};
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
  // Both the click and the dynamic guide default to mono on Out 1. The
  // guide render buffer is always 2-channel; the StemOutputRouter folds
  // it to mono (averaging L+R) when outputWidth=1, so the musician gets
  // a centered, dedicated bus that does not collide with the main mix
  // routed to Out 1-2. The user can opt back into stereo from the UI.
  std::atomic<int> dynamicGuideOutputWidthState{1};

  juce::AudioFormatManager playbackFormatManager;
  std::vector<std::unique_ptr<juce::TimeSliceThread>> stemReadThreads;
  std::unique_ptr<juce::TimeSliceThread> timeProcessorMaintenanceThread;
  std::atomic<bool> timeProcessorMaintenanceEnabled{false};
  juce::CriticalSection timeProcessorMaintenanceLock;
  std::vector<std::unique_ptr<StemTrack>> stemTracks;
  struct AutomationStemState {
    std::atomic<unsigned> mask{0};
    std::atomic<float> volume{1.0f}, gain{1.0f};
    std::atomic<bool> mute{false};
  };
  struct AutomationPadsState {
    std::atomic<unsigned> mask{0};
    std::atomic<float> volume{1.0f}, gain{0.75f};
    std::atomic<bool> enabled{true};
    std::atomic<int> note{-1}, octave{0};
  };
  struct AutomationControlState {
    std::array<AutomationStemState, maxStemCount> stems;
    AutomationPadsState pads;
  };
  AutomationControlState sectionAutomationDraft, sectionAutomationActive,
                         sectionAutomationManual;
  playworship::audio::SectionAutomation sectionAutomationTimeline;
  playworship::audio::SectionAutomation::Selection audioAutomationSelection;
  mutable juce::CriticalSection sectionAutomationControlLock;
  SongArrangementSnapshot sectionAutomationArrangement;
  juce::StringArray sectionAutomationStemIds;
  juce::String sectionAutomationEditingPartId;
  std::atomic<bool> sectionAutomationEditing{false};
  std::atomic<bool> sectionAutomationRemoteOwned{false};
  struct AudioAutomationPadsEvent {
    int outputOffset = 0;
    playworship::audio::SectionAutomation::Pads pads;
    uint64_t token = 0;
  };
  std::array<AudioAutomationPadsEvent, StemTrack::maxMixTransitions> audioAutomationPadsEvents{};
  int audioAutomationPadsEventCount = 0;
  playworship::audio::SectionAutomation::Pads audioAutomationInitialPads;
  uint64_t padsMixLastToken = 0;
  uint64_t audioAutomationInitialPadsToken = 0;
  std::atomic<uint64_t> sectionAutomationCurrentToken{0};
  std::atomic<uint64_t> sectionAutomationManualToken{0};
  uint64_t sectionAutomationPublicationRevision = 0;
  std::atomic<int> padsBaseNote{-1}, padsBaseOctave{0};
  std::atomic<bool> padsAwaitingManualAfterAutomation{false};
  std::atomic<int> padsRenderedOctave{0};
  juce::SmoothedValue<float> padsMixSmoother{0.75f};
  bool isStemAutomationEligible(int stemIndex) const noexcept;
  void publishSectionAutomationLocked();
  void refreshSectionAutomation(double seconds) noexcept;
  void publishSectionAutomationSelection(const playworship::audio::SectionAutomation::Selection &selected) noexcept;
  void resetSectionAutomationState();
  void prepareSectionAutomationManual() noexcept;
  void setPadNoteAutomationTarget(int noteIndex, int octave) noexcept;
  void triggerPadNoteInternal(int noteIndex, int octave);
  unsigned getStemAutomationMask(int stemIndex) const noexcept;
  unsigned getPadsAutomationMask() const noexcept;
  playworship::audio::SectionAutomation::Stem resolveSectionAutomationStem(
      int stemIndex, const playworship::audio::SectionAutomation::Selection &selection) const noexcept;
  playworship::audio::SectionAutomation::Pads resolveSectionAutomationPads(
      const playworship::audio::SectionAutomation::Selection &selection) const noexcept;
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
  std::atomic<int> preparedRoutingProcessorCount{0};
  std::atomic<int> loadedStemCount{0};
  std::atomic<int> practiceListeningState{0}; // ((stem + 1) << 2) | mode
  std::atomic<uint64_t> practiceListeningRevision{0};
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
  // Stereo scratch holding the StemMixEngine output before StemOutputRouter
  // copies/folds it into the user-configured (channel, width) destination.
  // Lives in the per-processor storage so prepareToPlay sizes it once and
  // the audio thread never allocates inside the callback.
  juce::AudioBuffer<float> routingGroupedRenderStorage;
  std::atomic<bool> padsEnabledState{true};
  std::atomic<float> padsVolume{1.0f};
  std::atomic<float> padsGain{0.75f};
  std::atomic<bool> padsMuted{false};
  std::atomic<int> padsOutputChannel{1};
  std::atomic<int> padsOutputWidth{1};
  juce::String padsPackStatusText{"Not loaded"};
  std::atomic<bool> padsPackLoaded{false};
  std::atomic<bool> padsPackLoading{false};
  std::atomic<bool> padsPreloadRunning{false};
  // A signed-in UI may request online preparation while startup is still
  // completing a cache-only pass. The message-thread timer retries once.
  std::atomic<bool> padsNetworkPreloadPending{false};
  std::atomic<bool> padsAttacksPrepared{false};
  std::atomic<bool> padsFullLoadRequested{false};
  std::atomic<bool> padsPreloadShouldExit{false};
  std::thread padsPreloadThread;
  std::atomic<int> pendingPadNote{-1};
  std::atomic<int> pendingPadOctave{0};
  mutable juce::SpinLock padsStateLock;
  std::atomic<uint16_t> midiPendingPadTriggerMask{0};
  std::atomic<uint16_t> midiPendingPadStopMask{0};
  std::atomic<uint16_t> midiPendingPadLoadMask{0};
  std::atomic<int> pitchTargetSemiTones{0};
  int pitchActiveSemiTones = 0;
  std::atomic<double> playbackTempoBpm{120.0};
  double playbackTempoRatioActive = 1.0;
  double tempoTransitionStartRatio = 1.0;
  double tempoTransitionTargetRatio = 1.0;
  int tempoTransitionElapsedFrames = 0;
  int tempoTransitionTotalFrames = 0;
  std::atomic<int> transportState{static_cast<int>(TransportState::stopped)};
  std::atomic<uint64_t> transportControlRevision{0};
  std::atomic<double> songCurrentPositionSeconds{0.0};
  std::atomic<bool> audioCallbackActive{false};
  std::atomic<bool> remoteClientMode{false};
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
  std::atomic<bool> songEndCustomized{false};
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
  static constexpr int midiControlCommandQueueCapacity = 64;
  juce::AbstractFifo midiControlCommandFifo{
      midiControlCommandQueueCapacity};
  std::array<int, midiControlCommandQueueCapacity>
      midiControlCommandBuffer{};
  std::atomic<uint64_t> midiControlCommandDropCount{0};
  std::atomic<bool> midiPadsPackLoadRequested{false};
  const juce::int64 midiClockBaseWallMs = juce::Time::currentTimeMillis();
  const int64_t midiClockBaseTick = juce::Time::getHighResolutionTicks();
  const double midiClockTicksPerSecond =
      static_cast<double>(juce::Time::getHighResolutionTicksPerSecond());
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
  // Audio-thread-owned fixed state. Control code only increments the reset
  // generation, so arrangement publication can never resize storage while the
  // callback is indexing it.
  std::array<uint8_t, playworship::audio::SectionScheduler::maxSections>
      dynamicGuideAnnouncedByPart{};
  std::atomic<uint64_t> dynamicGuideResetGeneration{1};
  uint64_t dynamicGuideAppliedResetGeneration = 0;
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
    bool utilityCue = false;
    bool muteInstruments = false;
    bool skipPlayback = false;
  };
  std::vector<ArrangementRuntimePart> arrangementParts;
  struct ArrangementRuntimeCue {
    juce::String name;
    double positionSec = 0.0;
    int guideTriggerId = -1;
    bool skipPlayback = false;
  };
  std::vector<ArrangementRuntimeCue> arrangementCues;
  std::atomic<bool> arrangementCustomActive{false};
  std::atomic<int> arrangementCurrentPartIndex{0};
  std::atomic<int> arrangementPendingJumpPartIndex{-1};
  // Low 16 bits: index+1; bit16: confirmed; high32: request revision.
  // One CAS protects replacement/cancellation against late audio confirmations.
  std::atomic<uint64_t> arrangementJumpCommand{0};
  void setArrangementJumpCommand(int index, bool confirmed) noexcept;
  void configureSectionEntryCache();
  std::atomic<juce::int64> sectionEntryCacheBudgetBytes{0};
  std::atomic<int> arrangementLoopPartIndex{-1};
  std::atomic<uint64_t> arrangementTransitionIntentGeneration{1};
  std::atomic<double> arrangementTotalLengthSeconds{0.0};
  playworship::audio::PracticeLoop practiceLoop;
  playworship::audio::PracticeLoop::Snapshot audioPracticeLoopSnapshot;
  playworship::audio::SectionScheduler::Snapshot audioPracticeSectionSnapshot;
  enum class PracticeLoopError { none, invalidRange, tooShort,
                                 discontinuousArrangement, bufferNotReady };
  std::atomic<PracticeLoopError> practiceLoopError{PracticeLoopError::none};

  struct AudioArrangementRuntimeSnapshot {
    bool customActive = false;
    double totalLengthSeconds = 0.0;
    int pendingJumpPartIndex = -1;
    int loopPartIndex = -1;
  };
  
  // Frame-based source of truth. The legacy atomics remain as UI mirrors
  // during the incremental engine-v2 migration.
  playworship::audio::TransportClock transportClock;
  playworship::audio::SectionScheduler sectionScheduler;
  std::atomic<uint64_t> arrangementStructurePublicationSequence{0};
  const juce::String arrangementControlEpoch{juce::Uuid().toString()};
  playworship::audio::SectionScheduler::Snapshot audioSectionSnapshot;
  AudioArrangementRuntimeSnapshot audioArrangementRuntimeSnapshot;
  uint64_t audioArrangementStructureSequence = 0;
  // Last arrangementTransitionIntentGeneration observed by the audio thread.
  // The processor bumps this generation whenever the pending jump or the loop
  // section changes; the audio thread uses it to refresh the transient fields
  // of audioArrangementRuntimeSnapshot without waiting for a structural
  // publication.
  std::atomic<uint64_t> audioArrangementTransitionIntentGeneration{0};
  // Audio-thread-owned fallback for a section transition whose target bank
  // was not ready at the exact boundary. Playback remains continuous and the
  // jump is applied at offset zero as soon as every stem has the target.
  bool deferredArrangementTransitionPending = false;
  int deferredArrangementTransitionPartIndex = -1;
  juce::int64 deferredArrangementTransitionFrame = 0;
  juce::int64 deferredArrangementTransitionSourceFrame = 0;
  uint64_t deferredArrangementTransitionIntentGeneration = 0;
  // Last pending jump target that already received its one-bar lead-in
  // announcement. Reset whenever the pending jump is consumed, so a fresh
  // jump from the same UI click re-arms the lead-in voice prompt.
  int lastAnnouncedPendingJumpTarget = -1;
  int lastObservedGuidePendingTarget = -1;
  uint64_t lastObservedGuidePendingGeneration = 0;
  // Last loop section that already received its one-bar lead-in
  // announcement. Reset on every loop wrap (or when the loop is cleared /
  // re-targeted) so the next iteration re-arms the voice prompt.
  int lastAnnouncedLoopPartIndex = -1;
  std::atomic<juce::int64> globalSamplePosition{0};
  std::atomic<juce::int64> requestedSeekPosition{-1};
  
  // Lock-free seek command. The audio thread is the only writer of the
  // effective TransportClock; control publishes a target then increments the
  // release generation.
  std::atomic<double> seekTargetSeconds{0.0};
  std::atomic<uint64_t> seekRequestGeneration{0};
  uint64_t appliedSeekRequestGeneration = 0;

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
  std::atomic<int64_t> diagnosticEngineGeneration{0}, diagnosticSourceGeneration{0};
  std::atomic<uint64_t> diagnosticMarkerGeneration{0};
  double lastDiagnosticDeviceSampleMs = 0;
  int stemTimingSnapshotDecimator = 0;
  int distortionSnapshotDecimator = 0;
  static constexpr int kStemTimingSnapshotEveryBlocks = 20;
  static constexpr int kDistortionStemSnapshotEveryBlocks = 5;
  playworship::audio::SignalIntegrityState diagnosticMasterSignalState;
  playworship::audio::SignalIntegrityState diagnosticOutputSignalState;

  // ---- Benchmark: per-stem Bungee timing buffers ---------------------
  // Written from the audio thread on every Bungee process() call, read
  // from the same thread when the decimated StemTimingEvent snapshot is
  // built (every kStemTimingSnapshotEveryBlocks blocks). They live on
  // the audio thread stack across snapshots, so plain double/uint32_t
  // storage is race-free.
  double lastBungeeUsMicros[maxStemCount]{};
  double lastBungeeGapUsMicros[maxStemCount]{};
  uint32_t lastBungeeCount[maxStemCount]{};

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
  void prepareRoutingTimeProcessors(int activeStemCount, double sampleRate,
                                    int maxBlockFrames);
  void releaseRoutingTimeProcessors(int retainedStemCount = 0);
  void flushRoutingTimeProcessors() noexcept;
  void flushAllTimeProcessingState() noexcept;
  void pauseTimeProcessorMaintenance() noexcept;
  void resumeTimeProcessorMaintenance() noexcept;
  int useTimeSlice() override;
  void timerCallback() override;
  bool enqueueMidiControlCommand(int command) noexcept;
  bool dequeueMidiControlCommand(int &command) noexcept;
  void executeMidiControlCommand(int command);
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
