#pragma once

#include "../../data/SongData.h"
#include "../UiCommon.h"
#include "../components/AppModal.h"
#include "PracticeTimelineRuler.h"
#include <JuceHeader.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

class SvgToolbarButton final : public juce::TextButton {
public:
  SvgToolbarButton(const juce::String &name, SvgIconName icon);

  void setIcon(SvgIconName nextIcon);
  void paintButton(juce::Graphics &g, bool isMouseOverButton,
                   bool isButtonDown) override;

private:
  SvgIconName iconName;
};

struct WaveformSongActionsState {
  bool cloudLinked = false;
  bool settingsPending = false;
  bool settingsActionBusy = false;
};

class WaveformBar final : public juce::Component, private juce::Timer {
public:
  enum class RemoteIndicator { Hidden, Searching, Connecting, Reconnecting, Connected };
  using PlayingProvider = std::function<bool()>;
  using PositionProvider = std::function<double()>;
  using LengthProvider = std::function<double()>;
  using SongWindowProvider = std::function<double()>;
  using SeekCallback = std::function<void(double)>;
  using EditToggleCallback = std::function<void(bool)>;
  using WaveformHeightToggleCallback = std::function<void(bool)>;
  using ArrangementChangedCallback = std::function<void(
      const SongArrangementSnapshot &, bool isCustomArrangement)>;
  using ArrangementResetCallback = std::function<void()>;
  using ArrangementPartJumpRequestCallback = std::function<void(int)>;
  using ArrangementPartLoopToggleRequestCallback = std::function<void(int)>;
  using ArrangementPendingJumpClearCallback = std::function<void()>;
  using TempoMapChangedCallback = std::function<void(const SongTempoMapSnapshot &)>;
  using ActiveTempoRegionCallback =
      std::function<void(const SongTempoRegionSnapshot &)>;
  using PendingArrangementPartProvider = std::function<int()>;
  using ActiveArrangementLoopPartProvider = std::function<int()>;

  WaveformBar();
  ~WaveformBar() override;
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
  void setLiveMode(bool enabled);
  bool isLiveMode() const { return liveMode; }
  bool canToggleLiveMode() const {
    return !blankSongTimeline && (liveMode || (!isEditingArrangement() && sectionAutomationEditingPartId.isEmpty()));
  }

  void setWaveformBins(std::vector<float> bins);
  void setBlankSong(bool enabled);
  std::vector<float> getWaveformBinsCopy() const { return waveformBins; }
  void resetPlaybackViewForSong();
  void setArrangement(const SongArrangementSnapshot &arrangementSnapshot,
                      bool isCustomArrangement);
  void setTempoMap(const SongTempoMapSnapshot &tempoMapSnapshot);
  juce::Range<double> getArrangementPartTimeRange(int index) const;
  juce::String getArrangementPartId(int index) const;
  int getArrangementPartIndex(const juce::String &id) const;
  void setArrangementControlFeedbackProviders(
      std::function<juce::String(const juce::String &)> error,
      std::function<bool(const juce::String &)> pending,
      std::function<juce::String(const juce::String &)> status = {});
  std::vector<PracticeTimelineMark> getPracticeTimelineMarks(
      juce::Range<double> visibleRange, float width) const;
  double getPracticeSnappedPosition(double position,
                                    juce::Range<double> allowed) const;
  void setSongRhythm(double bpm, const juce::String &timeSignature);
  void setTransportProviders(PositionProvider positionCb,
                             LengthProvider lengthCb,
                             PlayingProvider playingCb = {});
  void setSongSourceLengthProvider(LengthProvider lengthCb);
  void setSongEndCustomizedProvider(std::function<bool()> provider);
  void setSongWindowProviders(SongWindowProvider startCb,
                              SongWindowProvider endCb);
  void setSeekCallback(SeekCallback cb);
  void setOnEditToggle(EditToggleCallback cb);
  void setOnWaveformHeightToggle(WaveformHeightToggleCallback cb);
  void setMinimalPhoneMode(bool enabled);
  void setOnArrangementChanged(ArrangementChangedCallback cb);
  void setOnArrangementReset(ArrangementResetCallback cb);
  void setSectionAutomationCallbacks(
      std::function<bool(const juce::String &)> begin,
      std::function<void(bool)> finish,
      std::function<void(const juce::String &)> clear,
      std::function<juce::String()> editingPartProvider,
      std::function<bool()> canEditProvider = {});
  void setSectionAutomationState(const juce::String &partId);
  void setCommittedSectionAutomation(const SongArrangementSnapshot &snapshot);
  void setOnArrangementPartJumpRequest(ArrangementPartJumpRequestCallback cb);
  void setOnArrangementPartLoopToggleRequest(
      ArrangementPartLoopToggleRequestCallback cb);
  void setOnArrangementPendingJumpClearRequest(
      ArrangementPendingJumpClearCallback cb);
  void setOnTempoMapChanged(TempoMapChangedCallback cb);
  void setOnActiveTempoRegionChanged(ActiveTempoRegionCallback cb);
  void setOnExportMixRequested(std::function<void(juce::String)> cb);
  void setSongActionsStateProvider(
      std::function<WaveformSongActionsState()> provider);
  void setOnCloudSettingsSyncRequested(std::function<void()> cb);
  void setOnCloudSettingsVersionsRequested(
      std::function<void(juce::Rectangle<int>)> cb);
  void setExportBusy(bool busy);
  void setPendingArrangementPartProvider(PendingArrangementPartProvider cb);
  void setPreparingArrangementPartProvider(PendingArrangementPartProvider cb);
  void setActiveArrangementLoopPartProvider(
      ActiveArrangementLoopPartProvider cb);
  void setMidiLearnActive(bool active);
  void setMidiLearnPendingTarget(int targetId);
  void setMidiLearnBindingLabelProvider(std::function<juce::String(int)> cb);
  void setOnMidiLearnTargetSelected(std::function<void(int)> cb);
  void triggerEditToggleFromMidi();
  void refreshThemeColours();
  void setSongLoadingState(bool active, double progress01,
                           const juce::String &phaseText);
  void clearSongLoadingState();
  void setRemoteIndicator(RemoteIndicator state);
  void setPhoneSafeAreaInsets(juce::BorderSize<int> insets);

  void paint(juce::Graphics &g) override;
  void paintOverChildren(juce::Graphics &g) override;
  void resized() override;
  void visibilityChanged() override;
  void enablementChanged() override;
  void mouseDown(const juce::MouseEvent &event) override;
  void mouseDrag(const juce::MouseEvent &event) override;
  void mouseUp(const juce::MouseEvent &event) override;
  void mouseMove(const juce::MouseEvent &event) override;
  void mouseExit(const juce::MouseEvent &event) override;
  void mouseDoubleClick(const juce::MouseEvent &event) override;
  bool keyPressed(const juce::KeyPress &key) override;
  void mouseWheelMove(const juce::MouseEvent &event,
                      const juce::MouseWheelDetails &wheel) override;
  void mouseMagnify(const juce::MouseEvent &, float magnifyAmount) override;

  /** Extra height the host layout should reserve so the parts toolbar drawn
   *  on top of the waveform doesn't compress the waveform / minimap when the
   *  arrangement editor is active. Returns 0 when the editor is closed.
   */
  int getExtraHeightForHostLayout() const;

private:
  friend struct WaveformEditingTestAccess;
  juce::Rectangle<float> getLiveRowBounds() const;
  juce::Rectangle<float> getLiveSectionContentBounds(juce::Rectangle<float> body) const;
  float getLiveBlockWidth() const;
  float getLiveBlockStride() const;
  void rebuildLiveSectionBounds();
  void drawLiveSections(juce::Graphics &g);
  class SectionActionButton;
  std::vector<std::unique_ptr<SectionActionButton>> liveSectionActions;
  void refreshLiveSectionActions(bool updateState = true);
  void activateLiveSectionAction(const juce::String &partId, int action);
  void revealLiveSectionAction(const juce::String &partId);
  bool navigateLiveSectionAction(const juce::String &partId, int action, const juce::KeyPress &);
  juce::String liveSectionState(int index) const;
  static float livePendingPulse(double nowMs, bool reducedMotion);
  void setLiveScrollOffset(float offset);
  void snapLiveScrollOffset();
  void suspendLiveFollow();
  void stopLiveScrollMotion();
  void sampleLiveDrag(double eventMs);
  void startLiveScrollMotion(double nowMs, double releaseEventMs, bool reducedMotion);
  void updateLiveScrollMotion(double nowMs, bool reducedMotion);
  bool liveScrollCoasting = false, liveScrollSettling = false;
  bool liveSuppressNativeMomentum = false;
  double liveMotionTimeMs = 0.0, liveDragSampleMs = 0.0;
  float liveDragSampleOffset = 0.0f, liveScrollVelocity = 0.0f;
  float liveSettleFrom = 0.0f, liveSettleTarget = 0.0f;
  void updateLiveFollow(double nowMs, bool reducedMotion);
  double liveFollowResumeMs = 0.0;
  double liveFollowAnimationStartMs = 0.0;
  float liveFollowFrom = 0.0f, liveFollowTarget = 0.0f;
  bool liveFollowAnimating = false;
  juce::String liveFollowPartId;
  float liveSectionProgress(int index) const;
  bool liveMode = false;
  float liveScrollOffset = 0.0f;
  int livePaintPreparing = -1;
  int livePaintActive = -1, livePaintPending = -1, livePaintLoop = -1;
  float livePaintProgress = -1.0f;
  bool livePaintReducedMotion = false;
  bool livePaintPlaying = false;
  double liveScrollSnapDueMs = 0.0;
  juce::Rectangle<float> liveLayoutRow;
  juce::BorderSize<int> liveLayoutInsets;
  float liveLayoutBlockWidth = 0.0f;
  bool livePointerActive = false;
  bool livePointerMoved = false;
  int livePointerSource = -1;
  juce::Point<float> livePointerStart;
  float livePointerStartOffset = 0.0f;
  juce::String liveDownPartId;
  enum class SnapMode { Bar = 1, Beat, HalfBeat, Free };
  enum class TimelineViewMode { GuideMap = 1, TempoMap };
  enum class PlaybackControlFocus { None, Mute, Loop, Jump };

  enum class DragMode {
    none,
    seek,
    moveCue,
    movePart,
    resizePartLeft,
    resizePartRight,
    selectTempoRegion,
    moveTempoRegion,
    resizeTempoRegionLeft,
    resizeTempoRegionRight,
    selectParts,
    panViewport,
    resizeViewportLeft,
    resizeViewportRight
  };

  struct PartOverlayBounds {
    int partIndex = -1;
    juce::Rectangle<float> body;
    juce::Rectangle<float> editToolbar;
    juce::Rectangle<float> leftHandle;
    juce::Rectangle<float> rightHandle;
    juce::Rectangle<float> deleteButton;
    juce::Rectangle<float> loopButton;
    juce::Rectangle<float> loopIcon;
    juce::Rectangle<float> jumpButton;
    juce::Rectangle<float> jumpIcon;
    juce::Rectangle<float> duplicateButton;
    juce::Rectangle<float> duplicateIcon;
    juce::Rectangle<float> addButton;
    juce::Rectangle<float> addIcon;
    juce::Rectangle<float> muteButton;
    juce::Rectangle<float> muteIcon;
    juce::Rectangle<float> overflowButton;
    juce::Rectangle<float> skipButton;
    juce::Rectangle<float> skipIcon;
    juce::Rectangle<float> automateButton;
    bool showsDeleteButton = false;
    bool showsLoopButton = false;
    bool showsJumpButton = false;
    bool showsDuplicateButton = false;
    bool showsAddButton = false;
    bool showsMuteButton = false;
    bool showsOverflowButton = false;
    bool showsSkipButton = false;
    bool showsAutomateButton = false;
  };

  struct LiveTextLayout {
    juce::Rectangle<float> title, state;
    float fontSize = 14.0f;
    float stateFontSize = 14.0f;
    int stateLines = 1;
    int lines = 1;
  };
  LiveTextLayout getLiveTextLayout(const PartOverlayBounds &item) const;

  struct PartHitResult {
    int partIndex = -1;
    bool leftEdge = false;
    bool rightEdge = false;
    bool deleteButton = false;
    bool loopButton = false;
    bool jumpButton = false;
    bool duplicateButton = false;
    bool addButton = false;
    bool muteButton = false;
    bool overflowButton = false;
    bool skipButton = false;
    bool automateButton = false;
  };
  PartHitResult liveDownHit;

  struct TempoRegionHitResult {
    int regionIndex = -1;
    bool leftEdge = false;
    bool rightEdge = false;
    bool deleteButton = false;
  };

  struct DragPartOrigin {
    juce::String id;
    double startSec = 0.0;
    double endSec = 0.0;
  };

  struct ArrangementHistoryEntry {
    SongArrangementSnapshot arrangement;
    bool customArrangement = false;
    int selectedPartIndex = -1;
    std::vector<juce::String> selectedPartIds;
    juce::String selectedCueId;
  };

  struct CopiedArrangementPart {
    SongArrangementPartSnapshot part;
    std::vector<SongArrangementCueSnapshot> cues;
    juce::String sourcePartId;
    double sourceStartSec = 0.0;
    double sourceEndSec = 0.0;
    double relativeStartSec = 0.0;
  };

  struct PlaybackVisualState {
    float progressRatio = 0.0f;
    float transportProgressRatio = 0.0f;
    bool showPendingTravelSeek = false;
  };

  bool dispatchInlineSectionAction(const PartHitResult &hit);
  void showInlineSectionMenu(int partIndex, juce::Rectangle<float> anchor);
  juce::Rectangle<float> sharedSectionOverflowBounds;
  void timerCallback() override;
  void updatePlaybackAnimation(double frameTimestampSec);
  void handleSeek(float x);
  void handleSeek(float x, const juce::Rectangle<float> &seekBounds);
  void showExportFormatPopover();
  void layoutSectionAutomationToolbar();
  void paintSectionAutomationToolbar(juce::Graphics &g);
  void showSectionAutomationActionsPopover();
  void finishSectionAutomationFromToolbar(bool save);
  void clearSectionAutomationFromToolbar();
  void focusSectionAutomationTarget();

  std::vector<float> waveformBins;
  PlayingProvider playingProvider;
  bool blankSongTimeline = false;
  PositionProvider positionProvider;
  LengthProvider lengthProvider;
  LengthProvider sourceLengthProvider;
  std::function<bool()> songEndCustomizedProvider;
  SongWindowProvider songWindowStartProvider;
  SongWindowProvider songWindowEndProvider;
  SeekCallback onSeekTo;
  EditToggleCallback onEditToggle;
  WaveformHeightToggleCallback onWaveformHeightToggle;
  ArrangementChangedCallback onArrangementChanged;
  ArrangementResetCallback onArrangementReset;
  ArrangementPartJumpRequestCallback onArrangementPartJumpRequest;
  ArrangementPartLoopToggleRequestCallback onArrangementPartLoopToggleRequest;
  ArrangementPendingJumpClearCallback onArrangementPendingJumpClearRequest;
  TempoMapChangedCallback onTempoMapChanged;
  ActiveTempoRegionCallback onActiveTempoRegionChanged;
  std::function<void(juce::String)> onExportMixRequested;
  std::function<WaveformSongActionsState()> songActionsStateProvider;
  std::function<void()> onCloudSettingsSyncRequested;
  std::function<void(juce::Rectangle<int>)>
      onCloudSettingsVersionsRequested;
  PendingArrangementPartProvider pendingArrangementPartProvider;
  PendingArrangementPartProvider preparingArrangementPartProvider;
  ActiveArrangementLoopPartProvider activeArrangementLoopPartProvider;
  std::function<juce::String(const juce::String &)> arrangementControlErrorProvider;
  std::function<bool(const juce::String &)> arrangementControlPendingProvider;
  std::function<juce::String(const juce::String &)> arrangementControlStatusProvider;
  std::function<bool(const juce::String &)> onSectionAutomationBegin;
  std::function<void(bool)> onSectionAutomationFinish;
  std::function<void(const juce::String &)> onSectionAutomationClear;
  std::function<juce::String()> sectionAutomationEditingPartProvider;
  std::function<bool()> sectionAutomationCanEditProvider;
  juce::String sectionAutomationEditingPartId;
  double lastSectionAutomationAnimationMs = 0.0;
  juce::Label sectionAutomationLabel;
  juce::TextButton sectionAutomationDoneButton;
  juce::TextButton sectionAutomationCancelButton;
  SvgToolbarButton sectionAutomationMoreButton{"sectionAutomationMore", SvgIconName::MoreHorizontal};
  juce::Rectangle<int> sectionAutomationToolbarBounds;
  float sectionAutomationToolbarAnchorX = 0.0f;
  bool sectionAutomationCompactLayout = false;
  bool sectionAutomationTargetOffscreen = false;
  uint64_t sectionAutomationUiRevision = 0;
  SvgToolbarButton followButton{"follow", SvgIconName::AudioWaveform};
  SvgToolbarButton moreButton{"more", SvgIconName::MoreHorizontal};
  SvgToolbarButton editButton{"edit", SvgIconName::Pencil};
  SvgToolbarButton waveHeightButton{"waveHeight", SvgIconName::Maximize};
  SvgToolbarButton guideMapButton{"guideMap", SvgIconName::Music};
  SvgToolbarButton tempoMapButton{"tempoMap", SvgIconName::Metronome};
  juce::Label snapModeLabel;
  AdaptiveComboBox snapModeBox;
  juce::TextButton sectionMenuButton;
  juce::TextButton splitTempoRegionButton;
  juce::Label tempoBpmLabel;
  juce::TextEditor tempoBpmEditor;
  juce::Label tempoSignatureLabel;
  AdaptiveComboBox tempoSignatureBox;
  std::vector<std::unique_ptr<juce::TextButton>> sectionAddButtons;
  SnapMode snapMode = SnapMode::Bar;
  TimelineViewMode timelineViewMode = TimelineViewMode::GuideMap;
  double songBpm = 0.0;
  juce::String songTimeSignature = "4/4";
  int songBeatsPerBar = 4;
  float viewportStart = 0.0f;
  float viewportSpan = 1.0f;
  SongArrangementSnapshot arrangement;
  SongArrangementSnapshot cueEditBaseline;
  std::unique_ptr<AppModal> cueInformationModal;
  struct CueMarkerBounds {
    juce::String id;
    juce::Rectangle<float> hit;
    juce::Rectangle<float> pointHit;
    juce::Rectangle<float> label;
    float x = 0.0f;
    float pointX = 0.0f;
    float y = 0.0f;
  };
  std::vector<CueMarkerBounds> cueMarkerBounds;
  std::vector<std::unique_ptr<juce::Button>> cueMarkerButtons;
  std::unique_ptr<juce::TooltipWindow> cueTooltipWindow;
  double cueTooltipHideAtMs = 0.0;
  juce::String selectedCueId;
  juce::String activeDragCueId;
  double cueDragOriginSec = 0.0;
  juce::Point<float> cueDragStart;
  bool cueDragMoved = false;
  SongTempoMapSnapshot tempoMap;
  bool customArrangement = false;
  int selectedPartIndex = -1;
  int selectedTempoRegionIndex = -1;
  int lastAppliedTempoRegionIndex = -1;
  std::vector<juce::String> selectedPartIds;
  DragMode dragMode = DragMode::none;
  juce::String activeDragPartId;
  std::vector<DragPartOrigin> dragPartOrigins;
  double dragOriginPartStartSec = 0.0;
  double dragOriginPartEndSec = 0.0;
  int activeDragTempoRegionIndex = -1;
  double dragOriginTempoStartSec = 0.0;
  double dragOriginTempoEndSec = 0.0;
  double dragAnchorSourceSec = 0.0;
  bool dragAnchorActive = false;
  bool dragAnchorEdgeIsStart = true;
  double dragAnchoredBoundarySec = 0.0;
  bool sectionInsertionAnchorValid = false;
  double sectionInsertionAnchorSec = 0.0;
  float dragViewportStart = 0.0f;
  float dragViewportSpan = 1.0f;
  float dragAnchorNormalized = 0.0f;
  juce::Point<float> viewportDragStartPosition;
  bool viewportDragMoved = false;
  bool usesTouchWaveformNavigation() const;
  void resetTouchWaveformNavigation();
  bool touchWaveformNavigationActive = false;
  bool touchWaveformTapCancelled = false;
  bool touchWaveformStartedOnRuler = false;
  int touchWaveformPrimarySource = -1;
  PartHitResult touchWaveformDownHit;
  juce::String touchWaveformDownPartId;
  std::map<int, juce::Point<float>> phoneTouchPositions;
  bool phonePinchActive = false;
  float phonePinchStartDistance = 1.0f;
  float phonePinchStartViewportSpan = 1.0f;
  float phonePinchAnchorRatio = 0.5f;
  float phonePinchAnchorSource = 0.5f;
  juce::Point<float> selectionDragStart;
  juce::Point<float> selectionDragCurrent;
  std::vector<juce::String> selectionDragBaseIds;
  bool selectionDragAdditive = false;
  std::vector<PartOverlayBounds> partOverlayBounds;
  int hoveredActionPartIndex = -1;
  int hoveredLoopPartIndex = -1;
  int hoveredJumpPartIndex = -1;
  int hoveredMutePartIndex = -1;
  int hoveredSkipPartIndex = -1;
  int hoveredDuplicatePartIndex = -1;
  int hoveredAddPartIndex = -1;
  int hoveredDeletePartIndex = -1;
  int contextualSectionInsertPartIndex = -1;
  int playbackControlFocusPartIndex = -1;
  PlaybackControlFocus playbackControlFocus = PlaybackControlFocus::None;
  double playbackControlFocusChangedAtMs = 0.0;
  int pendingJumpDoubleClickPartIndex = -1;
  double pendingJumpDoubleClickDispatchAtMs = 0.0;
  bool arrangementDragPendingCommit = false;
  bool hasArrangementDragUndoSnapshot = false;
  ArrangementHistoryEntry arrangementDragUndoSnapshot;
  std::vector<ArrangementHistoryEntry> arrangementUndoStack;
  std::vector<ArrangementHistoryEntry> arrangementRedoStack;
  std::vector<CopiedArrangementPart> arrangementClipboard;
  bool tempoDragPendingCommit = false;
  bool midiLearnActive = false;
  int midiLearnPendingTargetId = -1;
  std::function<juce::String(int)> midiLearnBindingLabelProvider;
  std::function<void(int)> onMidiLearnTargetSelected;
  bool songLoadingActive = false;
  RemoteIndicator remoteIndicator = RemoteIndicator::Hidden;
  double songLoadingProgress01 = 0.0;
  juce::String songLoadingPhaseText;
  double songLoadingShownAtMs = 0.0;
  double songLoadingHideAfterMs = 0.0;
  bool followPlaybackEnabled = false;
  double followSuspendedUntilMs = 0.0;
  bool followPendingJumpWasActive = false;
  bool followLastSongProgressValid = false;
  float followLastSongProgressRatio = 0.0f;
  double followLastFrameTimestampSec = 0.0;
#if JUCE_ANDROID
  bool androidVisualProgressValid = false;
  float androidVisualProgressRatio = 0.0f;
  double androidVisualProgressTimestampSec = 0.0;
#endif
  int playbackPaintPreparingPart = -1;
  int playbackPaintPendingPart = -1;
  int playbackPaintLoopPart = -1;
  bool playbackWasPlaying = false;
  float lastFixedPlaybackProgress = -1.0f;
  float lastFixedTransportProgress = -1.0f;
  bool lastTimerSongProgressValid = false;
  float lastTimerSongProgressRatio = 0.0f;
  bool lastTimerEndValid = false;
  double lastTimerEndSeconds = 0.0;
  bool lastTimerStartValid = false;
  double lastTimerStartSeconds = 0.0;
  bool waveformVerticalExpanded = false;
  bool minimalPhoneMode = false;
  juce::BorderSize<int> phoneSafeAreaInsets;
  bool exportBusy = false;
  bool phonePanInertiaActive = false;
  float phonePanVelocity = 0.0f;
  float phonePanLastNormalized = 0.0f;
  double phonePanLastTimestampMs = 0.0;

  // Playback moves the needle (and optionally the viewport), while its data
  // stays static. Keep a bounded overscanned raster around the visible window so each frame only
  // composites an image slice plus the moving playback overlay.
  mutable juce::Image followWaveformBaseCache;
  mutable juce::Image followWaveformHighlightMaskCache;
  mutable int followWaveformCacheWidth = 0;
  mutable int followWaveformCacheHeight = 0;
  mutable float followWaveformCacheStart = 0.0f;
  mutable float followWaveformCacheSpan = 0.0f;
  mutable float followWaveformCacheScale = 0.0f;
  mutable juce::Image miniMapWaveformCache;
  mutable int miniMapWaveformCacheWidth = 0;
  mutable int miniMapWaveformCacheHeight = 0;
  juce::VBlankAttachment playbackVBlankAttachment;

  std::pair<float, float> getVisibleWindow() const;
  void setVisibleWindow(float start, float span);
  void applyZoom(float factor, float anchorRatio = 0.5f);
  bool isFollowTrackingActive() const;
  PlaybackVisualState getPlaybackVisualState() const;
  void invalidateWaveformRenderCaches();
  std::array<juce::Rectangle<int>, 3>
  getPlaybackRepaintRegions(float fromProgress, float toProgress) const;
  bool getPendingJumpProgressRatio(float &ratioOut) const;
  double getSongLengthSeconds() const;
  bool getPlaybackEndGuideSeconds(double &endSecondsOut) const;
  bool getPlaybackStartGuideSeconds(double &startSecondsOut) const;
  std::pair<double, double> getPlayableRangeSeconds() const;
  double getBarDurationSeconds() const;
  double getSnapStepSeconds() const;
  double getAnchorAttachThresholdSeconds() const;
  double getAnchorReleaseThresholdSeconds() const;
  bool isSnapBypassed(const juce::ModifierKeys &mods) const;
  double snapSeconds(double seconds, bool bypassSnap = false) const;
  double snapSecondsForward(double seconds) const;
  void normalizeArrangement(bool sortByStart, bool snapToGrid);
  int findPartIndexById(const juce::String &id) const;
  bool isPartSelected(int partIndex) const;
  void pruneSelectedPartIds();
  void setSingleSelectedPart(int partIndex);
  void togglePartSelection(int partIndex);
  void ensurePartSelectedForDrag(int partIndex);
  void captureDragPartOrigins();
  ArrangementHistoryEntry makeArrangementHistoryEntry() const;
  void pushArrangementUndoSnapshot();
  void pushArrangementUndoSnapshot(const ArrangementHistoryEntry &entry);
  void restoreArrangementHistoryEntry(const ArrangementHistoryEntry &entry);
  bool undoArrangementEdit();
  bool redoArrangementEdit();
  juce::String resolveRealSourcePartId(
      const SongArrangementPartSnapshot &part) const;
  bool copySelectedArrangementParts();
  bool pasteCopiedArrangementParts();
  juce::Rectangle<float> getSelectionDragBounds() const;
  void updateSelectionFromDragBounds();
  bool isEditingArrangement() const;
  juce::Rectangle<float>
  getEditToolbarBounds(const juce::Rectangle<float> &bounds) const;
  juce::Rectangle<float>
  getMainWaveBounds(const juce::Rectangle<float> &bounds) const;
  juce::Rectangle<float>
  getSeekRulerBounds(const juce::Rectangle<float> &mainWaveBounds) const;
  juce::Rectangle<float>
  getMainWaveBodyBounds(const juce::Rectangle<float> &mainWaveBounds) const;
  juce::Rectangle<float>
  getMiniMapBounds(const juce::Rectangle<float> &bounds) const;
  juce::Rectangle<float>
  getArrangementLaneBounds(const juce::Rectangle<float> &mainWaveBounds) const;
  juce::Rectangle<float>
  getViewportBoundsInMiniMap(const juce::Rectangle<float> &miniMapBounds) const;
  double windowXToSourceSeconds(const juce::Rectangle<float> &windowBounds,
                                float x) const;
  float sourceSecondsToWindowX(const juce::Rectangle<float> &windowBounds,
                               double sourceSeconds) const;
  PartHitResult hitTestArrangementPart(juce::Point<float> pos) const;
  TempoRegionHitResult hitTestTempoRegion(juce::Point<float> pos) const;
  void rebuildPartOverlayBounds(const juce::Rectangle<float> &mainWaveBounds);
  void drawWaveform(juce::Graphics &g, const juce::Rectangle<float> &bounds,
                    float windowStart, float windowSpan, bool showGrid,
                    bool showPlayhead) const;
  void drawWaveformDirect(juce::Graphics &g,
                          const juce::Rectangle<float> &bounds,
                          float windowStart, float windowSpan, bool showGrid,
                          bool showPlayhead, bool showPlayableWindow = true) const;
  void drawPlayableWindowOverlay(juce::Graphics &g,
                                 const juce::Rectangle<float> &bounds,
                                 float windowStart, float windowSpan) const;
  bool drawCachedFollowWaveform(juce::Graphics &g,
                                const juce::Rectangle<float> &bounds,
                                float windowStart, float windowSpan) const;
  void drawWaveformHighlightMask(juce::Graphics &g,
                                 const juce::Rectangle<float> &bounds,
                                 float windowStart, float windowSpan) const;
  float sampleWaveformAmplitude(double sourceSec, double sourceLength,
                                double sourceSecondsPerColumn) const;
  void drawCachedMiniMapWaveform(juce::Graphics &g,
                                 const juce::Rectangle<float> &bounds) const;
  void drawWaveformSegment(juce::Graphics &g,
                           const juce::Rectangle<float> &bounds,
                           double sourceStartSec,
                           double sourceEndSec,
                           float waveformAlpha = 0.72f) const;
  double getWaveformSourceLengthSeconds() const;
  std::pair<double, double>
  getPartSourceRange(const SongArrangementPartSnapshot &part,
                     double sourceLength) const;
  std::pair<double, double>
  getPartOriginalSourceRange(const SongArrangementPartSnapshot &part,
                             double sourceLength) const;
  double getPartSourceDurationSeconds(
      const SongArrangementPartSnapshot &part) const;
  void clampDuplicatedPartToSourceDuration(SongArrangementPartSnapshot &part,
                                           bool anchorEnd,
                                           const SongArrangementSnapshot &sourceTimeline);
  void rippleCloseGapAfterDuplicatedPart(double gapStartSec, double gapEndSec,
                                         const juce::String &excludedPartId = {});
  bool mapArrangementSecondsToSourceSeconds(double arrangementSeconds,
                                            double sourceLength,
                                            double &sourceSecondsOut) const;
  void drawPlaybackWindowGuides(juce::Graphics &g,
                                const juce::Rectangle<float> &bounds,
                                float windowStart, float windowSpan) const;
  void drawSeekRuler(juce::Graphics &g, const juce::Rectangle<float> &bounds,
                     float windowStart, float windowSpan) const;
  void drawSectionInsertionAnchor(
      juce::Graphics &g, const juce::Rectangle<float> &waveBounds,
      const juce::Rectangle<float> &rulerBounds, float windowStart,
      float windowSpan) const;
  void drawArrangementOverlay(juce::Graphics &g,
                              const juce::Rectangle<float> &mainWaveBounds);
  void drawTempoOverlay(juce::Graphics &g,
                        const juce::Rectangle<float> &mainWaveBounds);
  void drawMiniMap(juce::Graphics &g,
                   const juce::Rectangle<float> &miniMapBounds) const;
  void rebuildSectionAddButtons();
  void openSectionPicker(const juce::Rectangle<float> &anchor,
                         int insertionPartIndex = -1);
  void addArrangementPart(const juce::String &preferredName = {});
  bool addArrangementPartAtInsertionAnchor(const juce::String &name,
                                            double playableStartSec,
                                            double playableEndSec,
                                            double preferredDurationSec = 0.0,
                                            bool utilityCue = false);
  void clearSectionInsertionAnchor();
  void addCountSequenceParts(int firstCount, int lastCount);
  void addUtilityCuePart(const juce::String &cueName);
  void addGuideCue(const juce::String &name, int countEnd = 0,
                   double countIntervalSec = 0.0);
  void rebuildCueMarkers();
  void drawCueMarkers(juce::Graphics &g) const;
  int hitTestCueMarker(juce::Point<float> position) const;
  int findCueIndexById(const juce::String &id) const;
  void moveGuideCue(const juce::String &id, double seconds);
  void deleteGuideCue(const juce::String &id);
  void reconcileCueAnchors();
  double adjacentCuePulse(double seconds, bool forward) const;
  bool updateCueCountOffsets(SongArrangementCueSnapshot &cue) const;
  void showCueInformation(const juce::String &message);
  bool ensureGuideCueCapacity(int additionalEvents);
  double getArrangementAppendStartSec(double fallbackStartSec) const;
  void duplicateArrangementPart(int partIndex);
  void createDuplicatedSection(int sourcePartIndex);
  void toggleArrangementPartMute(int partIndex);
  void toggleArrangementPartSkip(int partIndex);
  void renameArrangementPart(int partIndex);
  void editArrangementPartRange(int partIndex);
  void applyArrangementPartRange(const juce::String &partId, double start,
                                 double end);
  void deleteArrangementPart(int partIndex);
  void deleteSelectedArrangementParts();
  void notifyArrangementChanged(bool isCustom);
  void resetArrangementToDefault(bool preserveIndependentCues = false);
  void normalizeTempoMap();
  void selectTempoRegion(int regionIndex);
  void updateTempoControlsFromSelection();
  void applyTempoControlsToSelection();
  void deleteTempoRegion(int regionIndex);
  void notifyTempoMapChanged();
  void updateActiveTempoRegionFromTransport();
  void scheduleDeferredPartJumpFromDoubleClick(int partIndex);
  void cancelDeferredPartJumpFromDoubleClick();
  void flushDeferredPartJumpFromDoubleClickIfDue(bool force = false);
};
