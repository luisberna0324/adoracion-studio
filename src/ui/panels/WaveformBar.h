#pragma once

#include "../../data/SongData.h"
#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

class WaveformBar final : public juce::Component, private juce::Timer {
public:
  using PositionProvider = std::function<double()>;
  using LengthProvider = std::function<double()>;
  using SeekCallback = std::function<void(double)>;
  using EditToggleCallback = std::function<void(bool)>;
  using ArrangementChangedCallback = std::function<void(
      const SongArrangementSnapshot &, bool isCustomArrangement)>;
  using ArrangementResetCallback = std::function<void()>;
  using ArrangementPartJumpRequestCallback = std::function<void(int)>;
  using PendingArrangementPartProvider = std::function<int()>;

  WaveformBar();

  void setWaveformBins(std::vector<float> bins);
  void setArrangement(const SongArrangementSnapshot &arrangementSnapshot,
                      bool isCustomArrangement);
  void setSongRhythm(double bpm, const juce::String &timeSignature);
  void setTransportProviders(PositionProvider positionCb,
                             LengthProvider lengthCb);
  void setSeekCallback(SeekCallback cb);
  void setOnEditToggle(EditToggleCallback cb);
  void setOnArrangementChanged(ArrangementChangedCallback cb);
  void setOnArrangementReset(ArrangementResetCallback cb);
  void setOnArrangementPartJumpRequest(ArrangementPartJumpRequestCallback cb);
  void setPendingArrangementPartProvider(PendingArrangementPartProvider cb);
  void refreshThemeColours();

  void paint(juce::Graphics &g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &event) override;
  void mouseDrag(const juce::MouseEvent &event) override;
  void mouseUp(const juce::MouseEvent &event) override;
  void mouseMove(const juce::MouseEvent &event) override;
  void mouseExit(const juce::MouseEvent &event) override;
  void mouseDoubleClick(const juce::MouseEvent &event) override;
  void mouseWheelMove(const juce::MouseEvent &event,
                      const juce::MouseWheelDetails &wheel) override;
  void mouseMagnify(const juce::MouseEvent &, float magnifyAmount) override;

private:
  enum class DragMode {
    none,
    seek,
    movePart,
    resizePartLeft,
    resizePartRight,
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
    bool showsDeleteButton = false;
  };

  struct PartHitResult {
    int partIndex = -1;
    bool leftEdge = false;
    bool rightEdge = false;
    bool deleteButton = false;
  };

  void timerCallback() override;
  void handleSeek(float x);
  void handleSeek(float x, const juce::Rectangle<float> &seekBounds);

  std::vector<float> waveformBins;
  PositionProvider positionProvider;
  LengthProvider lengthProvider;
  SeekCallback onSeekTo;
  EditToggleCallback onEditToggle;
  ArrangementChangedCallback onArrangementChanged;
  ArrangementResetCallback onArrangementReset;
  ArrangementPartJumpRequestCallback onArrangementPartJumpRequest;
  PendingArrangementPartProvider pendingArrangementPartProvider;
  juce::TextButton editButton;
  std::vector<std::unique_ptr<juce::TextButton>> sectionAddButtons;
  double songBpm = 0.0;
  int songBeatsPerBar = 4;
  float viewportStart = 0.0f;
  float viewportSpan = 1.0f;
  SongArrangementSnapshot arrangement;
  bool customArrangement = false;
  int selectedPartIndex = -1;
  DragMode dragMode = DragMode::none;
  juce::String activeDragPartId;
  double dragOriginPartStartSec = 0.0;
  double dragOriginPartEndSec = 0.0;
  double dragAnchorSourceSec = 0.0;
  bool dragAnchorActive = false;
  bool dragAnchorEdgeIsStart = true;
  double dragAnchoredBoundarySec = 0.0;
  float dragViewportStart = 0.0f;
  float dragViewportSpan = 1.0f;
  float dragAnchorNormalized = 0.0f;
  std::vector<PartOverlayBounds> partOverlayBounds;

  std::pair<float, float> getVisibleWindow() const;
  void setVisibleWindow(float start, float span);
  void applyZoom(float factor, float anchorRatio = 0.5f);
  double getSongLengthSeconds() const;
  double getBarDurationSeconds() const;
  double snapSeconds(double seconds) const;
  void normalizeArrangement(bool sortByStart);
  int findPartIndexById(const juce::String &id) const;
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
  void rebuildPartOverlayBounds(const juce::Rectangle<float> &mainWaveBounds);
  void drawWaveform(juce::Graphics &g, const juce::Rectangle<float> &bounds,
                    float windowStart, float windowSpan, bool showGrid,
                    bool showPlayhead) const;
  void drawSeekRuler(juce::Graphics &g, const juce::Rectangle<float> &bounds,
                     float windowStart, float windowSpan) const;
  void drawArrangementOverlay(juce::Graphics &g,
                              const juce::Rectangle<float> &mainWaveBounds);
  void drawMiniMap(juce::Graphics &g,
                   const juce::Rectangle<float> &miniMapBounds) const;
  void rebuildSectionAddButtons();
  void openPartActionMenu(int partIndex);
  void addArrangementPart(const juce::String &preferredName = {});
  void duplicateArrangementPart(int partIndex);
  void renameArrangementPart(int partIndex);
  void editArrangementPartRange(int partIndex);
  void confirmDeleteArrangementPart(int partIndex);
  void deleteArrangementPart(int partIndex);
  void notifyArrangementChanged(bool isCustom);
  void resetArrangementToDefault();
};
