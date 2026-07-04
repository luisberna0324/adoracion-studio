#pragma once

#include "../../data/SongData.h"
#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

class WaveformIconButton final : public juce::Button {
public:
  WaveformIconButton(const juce::String &name, SvgIconName iconIn);
  void setIcon(SvgIconName iconIn);
  void paintButton(juce::Graphics &g, bool highlighted,
                   bool down) override;

private:
  SvgIconName icon;
};

class WaveformBar final : public juce::Component, private juce::Timer {
public:
  using PlayingProvider = std::function<bool()>;
  using PositionProvider = std::function<double()>;
  using LengthProvider = std::function<double()>;
  using SongWindowProvider = std::function<double()>;
  using SeekCallback = std::function<void(double)>;
  using EditToggleCallback = std::function<void(bool)>;
  using ExpandToggleCallback = std::function<void(bool)>;
  using ArrangementChangedCallback = std::function<void(
      const SongArrangementSnapshot &, bool isCustomArrangement)>;
  using ArrangementResetCallback = std::function<void()>;
  using ArrangementPartJumpRequestCallback = std::function<void(int)>;
  using ArrangementPartLoopToggleRequestCallback = std::function<void(int)>;
  using TempoMapChangedCallback = std::function<void(const SongTempoMapSnapshot &)>;
  using ActiveTempoRegionCallback =
      std::function<void(const SongTempoRegionSnapshot &)>;
  using PendingArrangementPartProvider = std::function<int()>;
  using ActiveArrangementLoopPartProvider = std::function<int()>;
  using CurrentArrangementPartProvider = std::function<int()>;
  using CurrentArrangementPartIdProvider = std::function<juce::String()>;

  WaveformBar();

  void setWaveformBins(std::vector<float> bins);
  void setArrangement(const SongArrangementSnapshot &arrangementSnapshot,
                      bool isCustomArrangement);
  void setTempoMap(const SongTempoMapSnapshot &tempoMapSnapshot);
  void setSongRhythm(double bpm, const juce::String &timeSignature);
  void setTransportProviders(PositionProvider positionCb,
                             LengthProvider lengthCb,
                             PlayingProvider playingCb = {});
  void setSongWindowProviders(SongWindowProvider startCb,
                              SongWindowProvider endCb);
  void setSeekCallback(SeekCallback cb);
  void setOnEditToggle(EditToggleCallback cb);
  void setOnExpandToggle(ExpandToggleCallback cb);
  void setOnArrangementChanged(ArrangementChangedCallback cb);
  void setOnArrangementReset(ArrangementResetCallback cb);
  void setOnArrangementPartJumpRequest(ArrangementPartJumpRequestCallback cb);
  void setOnArrangementPartLoopToggleRequest(
      ArrangementPartLoopToggleRequestCallback cb);
  void setOnTempoMapChanged(TempoMapChangedCallback cb);
  void setOnActiveTempoRegionChanged(ActiveTempoRegionCallback cb);
  void setPendingArrangementPartProvider(PendingArrangementPartProvider cb);
  void setActiveArrangementLoopPartProvider(
      ActiveArrangementLoopPartProvider cb);
  void setCurrentArrangementPartProvider(CurrentArrangementPartProvider cb);
  void setCurrentArrangementPartIdProvider(CurrentArrangementPartIdProvider cb);
  void setMidiLearnActive(bool active);
  void setMidiLearnPendingTarget(int targetId);
  void setMidiLearnBindingLabelProvider(std::function<juce::String(int)> cb);
  void setOnMidiLearnTargetSelected(std::function<void(int)> cb);
  void triggerEditToggleFromMidi();
  void refreshThemeColours();
  void setSongLoadingState(bool active, double progress01,
                           const juce::String &phaseText);
  void clearSongLoadingState();

  void paint(juce::Graphics &g) override;
  void paintOverChildren(juce::Graphics &g) override;
  void resized() override;
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

private:
  enum class SnapMode { Bar = 1, Beat, HalfBeat, Free };
  enum class TimelineViewMode { GuideMap = 1, TempoMap };

  enum class DragMode {
    none,
    seek,
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
    juce::Rectangle<float> leftHandle;
    juce::Rectangle<float> rightHandle;
    juce::Rectangle<float> deleteButton;
    juce::Rectangle<float> muteButton;
    juce::Rectangle<float> addButton;
    juce::Rectangle<float> addIcon;
    juce::Rectangle<float> loopButton;
    juce::Rectangle<float> loopIcon;
    juce::Rectangle<float> jumpButton;
    juce::Rectangle<float> jumpIcon;
    bool showsDeleteButton = false;
    bool showsMuteButton = false;
    bool showsAddButton = false;
    bool showsLoopButton = false;
    bool showsJumpButton = false;
  };

  struct PartHitResult {
    int partIndex = -1;
    bool leftEdge = false;
    bool rightEdge = false;
    bool deleteButton = false;
    bool muteButton = false;
    bool addButton = false;
    bool loopButton = false;
    bool jumpButton = false;
  };

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

  struct VisualTimelineSegment {
    int partIndex = -1;
    double sourceStartSec = 0.0;
    double sourceEndSec = 0.0;
    double playbackStartSec = 0.0;
    double playbackEndSec = 0.0;
    bool primaryInstance = true;
  };

  void timerCallback() override;
  void handleSeek(float x);
  void handleSeek(float x, const juce::Rectangle<float> &seekBounds);

  std::vector<float> waveformBins;
  PlayingProvider playingProvider;
  PositionProvider positionProvider;
  LengthProvider lengthProvider;
  SongWindowProvider songWindowStartProvider;
  SongWindowProvider songWindowEndProvider;
  SeekCallback onSeekTo;
  EditToggleCallback onEditToggle;
  ExpandToggleCallback onExpandToggle;
  ArrangementChangedCallback onArrangementChanged;
  ArrangementResetCallback onArrangementReset;
  ArrangementPartJumpRequestCallback onArrangementPartJumpRequest;
  ArrangementPartLoopToggleRequestCallback onArrangementPartLoopToggleRequest;
  TempoMapChangedCallback onTempoMapChanged;
  ActiveTempoRegionCallback onActiveTempoRegionChanged;
  PendingArrangementPartProvider pendingArrangementPartProvider;
  ActiveArrangementLoopPartProvider activeArrangementLoopPartProvider;
  CurrentArrangementPartProvider currentArrangementPartProvider;
  CurrentArrangementPartIdProvider currentArrangementPartIdProvider;
  std::unique_ptr<juce::LookAndFeel> insertSectionMenuLookAndFeel;
  WaveformIconButton followButton{"follow", SvgIconName::CornerDownRight};
  WaveformIconButton expandButton{"expand", SvgIconName::Maximize};
  WaveformIconButton editButton{"edit", SvgIconName::ImageEdit};
  WaveformIconButton resetButton{"reset", SvgIconName::SkipBack};
  juce::TextButton guideMapButton;
  juce::TextButton tempoMapButton;
  juce::Label snapModeLabel;
  juce::ComboBox snapModeBox;
  juce::TextButton sectionMenuButton;
  juce::TextButton splitTempoRegionButton;
  juce::Label tempoBpmLabel;
  juce::TextEditor tempoBpmEditor;
  juce::Label tempoSignatureLabel;
  juce::ComboBox tempoSignatureBox;
  std::vector<std::unique_ptr<juce::TextButton>> sectionAddButtons;
  SnapMode snapMode = SnapMode::Bar;
  TimelineViewMode timelineViewMode = TimelineViewMode::GuideMap;
  double songBpm = 0.0;
  juce::String songTimeSignature = "4/4";
  int songBeatsPerBar = 4;
  float viewportStart = 0.0f;
  float viewportSpan = 1.0f;
  SongArrangementSnapshot arrangement;
  std::vector<SongArrangementPartSnapshot> sourceArrangementParts;
  SongArrangementSnapshot lastCommittedArrangement;
  std::vector<SongArrangementSnapshot> arrangementUndoStack;
  std::vector<SongArrangementSnapshot> arrangementRedoStack;
  SongArrangementSnapshot arrangementClipboard;
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
  float dragViewportStart = 0.0f;
  float dragViewportSpan = 1.0f;
  float dragAnchorNormalized = 0.0f;
  juce::Point<float> selectionDragStart;
  juce::Point<float> selectionDragCurrent;
  std::vector<juce::String> selectionDragBaseIds;
  bool selectionDragAdditive = false;
  std::vector<PartOverlayBounds> partOverlayBounds;
  int hoveredActionPartIndex = -1;
  int hoveredAddPartIndex = -1;
  int hoveredMutePartIndex = -1;
  int hoveredLoopPartIndex = -1;
  int hoveredJumpPartIndex = -1;
  int pendingJumpDoubleClickPartIndex = -1;
  double pendingJumpDoubleClickDispatchAtMs = 0.0;
  bool arrangementDragPendingCommit = false;
  bool tempoDragPendingCommit = false;
  bool midiLearnActive = false;
  int midiLearnPendingTargetId = -1;
  std::function<juce::String(int)> midiLearnBindingLabelProvider;
  std::function<void(int)> onMidiLearnTargetSelected;
  bool songLoadingActive = false;
  double songLoadingProgress01 = 0.0;
  juce::String songLoadingPhaseText;
  double songLoadingShownAtMs = 0.0;
  double songLoadingHideAfterMs = 0.0;
  bool followPlaybackEnabled = false;
  bool waveformExpanded = false;
  bool deferActiveResizeTimelineUpdate = false;
  double followSuspendedUntilMs = 0.0;
  bool followPendingJumpWasActive = false;
  bool followLastSongProgressValid = false;
  float followLastSongProgressRatio = 0.0f;

  std::pair<float, float> getVisibleWindow() const;
  void setVisibleWindow(float start, float span);
  void applyZoom(float factor, float anchorRatio = 0.5f);
  bool isFollowTrackingActive() const;
  bool getPendingJumpProgressRatio(float &ratioOut) const;
  double getSongLengthSeconds() const;
  std::vector<VisualTimelineSegment> buildVisualTimelineSegments() const;
  double getVisualTimelineLengthSeconds() const;
  double visualSecondsForPartStart(int partIndex) const;
  double visualSecondsForPartEnd(int partIndex) const;
  double visualSecondsForPartPlaybackStart(int partIndex) const;
  double visualSecondsForPartPlaybackEnd(int partIndex) const;
  double sourceSecondsFromVisualSeconds(double visualSeconds) const;
  double visualSecondsFromSourceSeconds(double sourceSeconds) const;
  std::pair<double, double> getPlayableRangeSeconds() const;
  double getBarDurationSeconds() const;
  double getSnapStepSeconds() const;
  double getAnchorAttachThresholdSeconds() const;
  double getAnchorReleaseThresholdSeconds() const;
  bool isSnapBypassed(const juce::ModifierKeys &mods) const;
  double snapSeconds(double seconds, bool bypassSnap = false) const;
  double snapSecondsForward(double seconds) const;
  bool usesVirtualClips() const;
  void rebuildPartsFromClips();
  void rebuildClipsFromParts();
  void captureSourceArrangementParts(const SongArrangementSnapshot &snapshot);
  void showInsertSectionMenu(int afterPartIndex,
                             const juce::Rectangle<float> &anchorBounds);
  int findRestoreIndexForSourcePart(const juce::String &sourcePartId) const;
  int findInsertIndexForCurrentSeek() const;
  double getCurrentSeekSourceSeconds() const;
  bool arrangementsEqual(const SongArrangementSnapshot &a,
                         const SongArrangementSnapshot &b) const;
  void rememberArrangementForUndo();
  bool undoLastArrangementChange();
  bool redoLastArrangementChange();
  bool copySelectedArrangementParts();
  bool pasteArrangementClipboard();
  void normalizeArrangement(bool sortByStart, bool snapToGrid);
  SongArrangementSnapshot buildArrangementSnapshotForPersistence() const;
  int findPartIndexById(const juce::String &id) const;
  bool isPartSelected(int partIndex) const;
  void pruneSelectedPartIds();
  void setSingleSelectedPart(int partIndex);
  void togglePartSelection(int partIndex);
  void ensurePartSelectedForDrag(int partIndex);
  void captureDragPartOrigins();
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
  void drawSeekRuler(juce::Graphics &g, const juce::Rectangle<float> &bounds,
                     float windowStart, float windowSpan) const;
  void drawArrangementOverlay(juce::Graphics &g,
                              const juce::Rectangle<float> &mainWaveBounds);
  void drawTempoOverlay(juce::Graphics &g,
                        const juce::Rectangle<float> &mainWaveBounds);
  void drawMiniMap(juce::Graphics &g,
                   const juce::Rectangle<float> &miniMapBounds) const;
  void rebuildSectionAddButtons();
  void addArrangementPart(const juce::String &preferredName = {});
  void addCountSequenceParts(int firstCount, int lastCount);
  void addUtilityCuePart(const juce::String &cueName);
  void duplicateArrangementPart(int partIndex);
  void renameArrangementPart(int partIndex);
  void editArrangementPartRange(int partIndex);
  void toggleArrangementPartMute(int partIndex);
  void deleteArrangementPart(int partIndex);
  void deleteSelectedArrangementParts();
  void notifyArrangementChanged(bool isCustom);
  void resetArrangementToDefault();
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
