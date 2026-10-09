#pragma once

#include <JuceHeader.h>
#include "../UiCommon.h"
#include "PracticePlaybackClock.h"
#include "PracticeWaveformGeometry.h"
#include "PracticeViewport.h"
#include "PracticeTimelineRuler.h"
#include "PracticePitchSelector.h"
#include "PracticeTempoSelector.h"
#include "PracticeInstrumentSelector.h"
#include "../learning/LearningGuideModel.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>

// Full-screen, phone-sized rehearsal surface. It owns only presentation state;
// playback remains in the shared local/remote session.
class PracticePanel final : public juce::Component, private juce::Timer {
public:
  juce::Rectangle<int> getLearningTargetBounds(playworship::learning::Anchor) const;
  bool hasLoopMarks() const noexcept { return loopStartSeconds >= 0.0 || loopEndSeconds >= 0.0; }
  using Instrument = PracticeInstrumentSelector::Item;

  struct Sources {
    std::function<juce::String()> songId;
    std::function<juce::String()> title;
    std::function<double()> positionSeconds;
    std::function<double()> lengthSeconds;
    std::function<double()> songBpm;
    std::function<juce::String()> timeSignature;
    std::function<double()> playbackRate;
    std::function<double()> tempoBpm;
    std::function<void(double)> setTempoBpm;
    std::function<int()> pitchSemitones;
    std::function<juce::String()> songKey;
    std::function<void(int)> setPitchSemitones;
    std::function<bool()> isLoopActive;
    std::function<juce::Range<double>()> loopRange;
    std::function<juce::String()> loopIdentity;
    std::function<double(double, bool)> snapLoopPosition;
    std::function<void(double)> prepareLoop;
    std::function<bool(double, double)> setLoopRange;
    std::function<void()> releaseLoop;
    std::function<bool()> ownsLoop;
    std::function<bool()> loopRequestRejected;
    std::function<juce::String()> loopError;
    std::function<double()> minimumLoopDuration;
    std::function<std::vector<PracticeTimelineMark>(juce::Range<double>, float)>
        timelineMarks;
    std::function<juce::Range<double>()> seekRange;
    std::function<void(double)> seekToSeconds;
    std::function<void()> beginScrub;
    std::function<bool(double)> finishScrub;
    std::function<bool()> scrubFinishRejected;
    std::function<void(bool)> cancelScrub;
    std::function<bool()> isPlaying;
    std::function<bool()> canControl;
    std::function<std::vector<Instrument>()> instruments;
    // Changes whenever waveform content or the arrangement changes, even when
    // song ID, duration and instrument IDs stay the same. Optional for callers
    // without a content revision; song, duration and local/remote still apply.
    std::function<juce::String()> waveformRevision;
    std::function<void(const juce::String &,
                       std::shared_ptr<std::atomic<bool>>,
                       std::function<void(std::vector<float>)>)>
        requestInstrumentWaveform;
    std::function<bool()> isRemote;
    std::function<int()> listeningMode;
    std::function<juce::String()> listeningInstrumentId;
    std::function<void(const juce::String &, int)> setListeningMode;
    std::function<bool()> canRetargetListening;
    std::function<void()> finishListening;
    std::function<void()> playPause;
    std::function<void()> stop;
    std::function<void()> close;
  };

  explicit PracticePanel(Sources sources);
  ~PracticePanel() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &key) override;
  void mouseDown(const juce::MouseEvent &event) override;
  void mouseDrag(const juce::MouseEvent &event) override;
  void mouseUp(const juce::MouseEvent &event) override;
  void mouseMagnify(const juce::MouseEvent &event, float scaleFactor) override;

private:
  void timerCallback() override;
  void refreshWaveform(const juce::String &wheelSelectionId = {});
  void cancelWaveformRequest();
  bool waveformContextMatchesSources() const;
  void selectInstrument(int index, bool reloadWaveform = false);
  void applyListeningGesture(const juce::Point<float> &movement);
  void updateWaveformGesture(juce::Point<float> position, juce::int64 eventTimeMs);
  bool canFinishWaveformGesture() const;
  double getDisplayedPositionSeconds() const;
  double clampSeekPosition(double seconds) const;
  double getBarDurationSeconds() const;
  void commitSeek(double seconds);
  void updateScrubAnimation(juce::uint32 now);
  void settleScrub(bool withMomentum = false);
  void recordScrubMotion(double x, juce::int64 eventTimeMs);
  double getScrubReleaseVelocityPixelsPerSecond() const;
  void beginPendingSeek(double target, juce::uint32 now);
  void updatePendingSeek(juce::uint32 now);
  void updateAnimationFrame(double frameTimestampSec, bool fromVBlank = false);
  void cancelScrub(bool restorePlayback);
  void showBarFeedback(int direction, double previousPosition, double target);
  void selectInstrumentFromWheel();
  void cancelInstrumentGesture();
  bool routeInstrumentPointer(const juce::MouseEvent &event,
                              PracticeInstrumentSelector::PointerPhase phase);
  void changePitch(int delta);
  void changeTempo(double bpm);
  void markLoopStart();
  void markLoopEnd();
  void togglePracticeLoop();
  bool activateMarkedLoop();
  void clearLoopSelection();
  void clearPracticeRegion();
  void releaseOwnedLoop();
  double getLoopMarkPosition(bool isEnd) const;
  void updatePracticeControls();
  bool handleTouchDown(int index, juce::Point<float> position);
  bool handleTouchMove(int index, juce::Point<float> position);
  bool handleTouchUp(int index);
  void applyZoom(double scaleFactor);
  double getVisibleSpanSeconds() const;
  double getMinimumLoopDuration() const;
  void showPracticeFeedback(juce::String message);
  juce::Rectangle<int> safeContentBounds() const;
  struct WaveformLayout {
    juce::Rectangle<int> plot, ruler, hints, times, help;
  };
  WaveformLayout getWaveformLayout() const;
  static juce::String formatTime(double seconds);

  class GestureAwareButton final : public juce::TextButton {
  public:
    using juce::TextButton::TextButton;
    void paintButton(juce::Graphics &g, bool over, bool down) override;
    bool isGestureRelease() const { return gestureRelease; }
    void mouseUp(const juce::MouseEvent &event) override {
      gestureRelease = event.mouseWasDraggedSinceMouseDown() ||
                       event.getDistanceFromDragStart() >= 12;
      juce::TextButton::mouseUp(event);
      gestureRelease = false;
    }

  private:
    bool gestureRelease = false;
  };

  Sources sources;
  HeaderIconButton closeButton;
  TransportIconButton playPauseButton;
  TransportIconButton stopButton;
  GestureAwareButton restoreMixButton;
  PracticePitchSelector pitchSelector;
  PracticeTempoSelector tempoSelector;
  PracticeInstrumentSelector instrumentSelector;
  GestureAwareButton loopStartButton;
  GestureAwareButton loopEndButton;
  TransportIconButton loopToggleButton;
  TransportIconButton loopClearButton;
  PracticeViewport viewport;
  std::map<int, juce::Point<float>> touchPositions;
  bool pinchLatched = false;
  double pinchInitialDistance = 1.0;
  double pinchInitialSpan = 12.0;
  double loopStartSeconds = -1.0;
  double loopEndSeconds = -1.0;
  bool markingLoop = false;
  bool loopActivatedHere = false;
  bool loopPending = false;
  juce::uint32 loopRequestedAtMs = 0;
  bool suppressClearedLoop = false;
  juce::Range<double> clearedLoopRange;
  juce::String clearedLoopIdentity;
  juce::StringArray releasedLoopIdentities;
  juce::String practiceFeedback;
  juce::String lastLoopError;
  juce::uint32 practiceFeedbackUntilMs = 0;
  bool displayedWasRemote = false;
  int requestedPitch = 0;
  bool pitchPending = false;
  juce::uint32 pitchRequestedAtMs = 0;
  double requestedTempo = 120.0;
  bool tempoPending = false;
  juce::uint32 tempoRequestedAtMs = 0;
  juce::Rectangle<int> waveformBounds;
  juce::Rectangle<int> instrumentHeaderBounds;
  juce::Rectangle<int> listeningStatusBounds;
  PracticePlaybackClock playbackClock;
  juce::VBlankAttachment playbackVBlankAttachment;
  juce::uint32 lastVBlankAtMs = 0;
  bool animationUsesVBlank = false;
  double lastPresentedPositionSeconds = -1.0;
  double lastClockSourceSeconds = -1.0;
  int displayedListeningMode = -1;
  juce::String displayedListeningInstrument;
  bool displayedBarFeedback = false;
  std::vector<float> waveformBins;
  PracticeWaveformGeometry waveformGeometry;
  std::vector<Instrument> instruments;
  std::map<juce::String, std::vector<float>> waveformCache;
  juce::String selectedInstrumentId;
  juce::String displayedSongId;
  juce::String displayedWaveformRevision;
  double displayedWaveformLengthSeconds = 0.0;
  juce::uint32 nextWaveformProbeMs = 0;
  juce::Point<float> dragStart;
  enum class DragAxis { Undecided, Horizontal, Vertical };
  DragAxis dragAxis = DragAxis::Undecided;
  double dragOriginSeconds = 0.0;
  double dragSecondsPerPixel = 0.0;
  double previewPositionSeconds = 0.0;
  double visualPositionSeconds = 0.0;
  double settleOriginSeconds = 0.0;
  struct MotionSample {
    double x;
    juce::int64 timeMs;
  };
  std::vector<MotionSample> motionSamples;
  juce::int64 lastMotionAtMs = 0;
  juce::uint32 scrubVisualAtMs = 0;
  juce::uint32 settleStartedMs = 0;
  juce::uint32 settleDurationMs = 180;
  double pendingSeekTargetSeconds = 0.0;
  double pendingSeekPositionSeconds = 0.0;
  juce::uint32 pendingSeekAtMs = 0;
  juce::uint32 pendingSeekVisualAtMs = 0;
  int pendingSeekMatchingFrames = 0;
  double handoffOffsetSeconds = 0.0;
  juce::uint32 handoffOffsetAtMs = 0;
  juce::uint32 barFeedbackUntilMs = 0;
  juce::String gestureSongId;
  juce::String barFeedback;
  int lastTapSide = -1;
  int barFeedbackSide = 0;
  int accumulatedBarSteps = 0;
  bool gestureWasRemote = false;
  bool pendingSeek = false;
  bool pendingScrubFinish = false;
  bool scrubActive = false;
  bool scrubSettling = false;
  bool instrumentSelectorPressed = false;
  bool waveformPressed = false;
  bool listeningSwipeHandled = false;
  bool listeningActivatedHere = false;
  int requestedListeningMode = 0;
  unsigned int waveformRequestGeneration = 0;
  std::shared_ptr<std::atomic<bool>> activeWaveformCancellation;
  friend struct PracticePanelGestureTestAccess;
};
