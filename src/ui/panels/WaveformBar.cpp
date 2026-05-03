#include "WaveformBar.h"
#include "../../data/Localization.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kMinPartDurationSec = 0.25;
constexpr double kPartSnapSeconds = 0.1;
constexpr float kMinViewportSpan = 1.0f / 24.0f;
constexpr float kEditToolbarHeight = 24.0f;
constexpr float kEditToolbarBottomGap = 6.0f;
constexpr float kSeekRulerHeight = 28.0f;
constexpr float kSeekRulerGap = 4.0f;
constexpr float kMiniMapHeight = 24.0f;
constexpr float kMiniMapGap = 10.0f;
constexpr float kPartHandleHitWidth = 7.0f;
constexpr float kViewportHandleHitWidth = 9.0f;
constexpr double kPartAnchorAttachThresholdSec = 0.55;
constexpr double kPartAnchorReleaseThresholdSec = 1.00;
constexpr int kSnapModeBarId = 1;
constexpr int kSnapModeBeatId = 2;
constexpr int kSnapModeHalfBeatId = 3;
constexpr int kSnapModeFreeId = 4;

juce::String formatSeconds(double seconds) {
  if (seconds < 0.0)
    seconds = 0.0;
  const int total = static_cast<int>(std::round(seconds));
  const int mins = total / 60;
  const int secs = total % 60;
  return juce::String(mins).paddedLeft('0', 2) + ":" +
         juce::String(secs).paddedLeft('0', 2);
}

bool nearlyEqual(double a, double b, double epsilon = 0.0001) {
  return std::abs(a - b) <= epsilon;
}

juce::Colour partColorOrFallback(const SongArrangementPartSnapshot &part,
                                 int partIndex) {
  if (isArrangementPartColorValid(part.color))
    return juce::Colour::fromString(part.color);
  return UITheme::accent.withRotatedHue(static_cast<float>((partIndex % 10) * 0.08f));
}
} // namespace

WaveformBar::WaveformBar() {
  editButton.setButtonText(L10n::tr("EDITAR", "EDIT"));
  editButton.setTooltip(
      L10n::tr(
          "Arrastra para ajustar; usa el selector para compas/beat/libre (Option/Alt = libre temporal)",
          "Drag snaps to grid; use the selector for bar/beat/free (hold Option/Alt for temporary free move)"));
  editButton.setClickingTogglesState(true);
  editButton.setViewportIgnoreDragFlag(true);
  editButton.onClick = [this] {
    editButton.setButtonText(
        editButton.getToggleState() ? L10n::tr("SALIR", "EXIT")
                                    : L10n::tr("EDITAR", "EDIT"));
    // Cuando el alto del panel no cambia entre modos, resized() no siempre se
    // dispara desde el padre. Forzamos layout local para mostrar/ocultar
    // correctamente los botones de partes.
    resized();
    if (editButton.getToggleState() && viewportSpan >= 0.999f)
      setVisibleWindow(0.0f, 1.0f);
    if (onEditToggle)
      onEditToggle(editButton.getToggleState());
    repaint();
  };
  addAndMakeVisible(editButton);

  snapModeBox.addItem(L10n::tr("Compas", "Bar"), kSnapModeBarId);
  snapModeBox.addItem(L10n::tr("Beat", "Beat"), kSnapModeBeatId);
  snapModeBox.addItem(L10n::tr("1/2 Beat", "1/2 Beat"), kSnapModeHalfBeatId);
  snapModeBox.addItem(L10n::tr("Libre", "Free"), kSnapModeFreeId);
  snapModeBox.setJustificationType(juce::Justification::centredLeft);
  snapModeBox.setTooltip(
      L10n::tr("Modo de ajuste al mover secciones", "Section move snap mode"));
  snapModeBox.setSelectedId(kSnapModeBeatId, juce::dontSendNotification);
  snapModeBox.onChange = [this] {
    const int selectedId = snapModeBox.getSelectedId();
    switch (selectedId) {
    case kSnapModeBarId:
      snapMode = SnapMode::Bar;
      break;
    case kSnapModeHalfBeatId:
      snapMode = SnapMode::HalfBeat;
      break;
    case kSnapModeFreeId:
      snapMode = SnapMode::Free;
      break;
    case kSnapModeBeatId:
    default:
      snapMode = SnapMode::Beat;
      break;
    }
  };
  addAndMakeVisible(snapModeBox);

  rebuildSectionAddButtons();

  refreshThemeColours();
  startTimerHz(30);
}

void WaveformBar::setWaveformBins(std::vector<float> bins) {
  const bool wasEditing = editButton.getToggleState();
  waveformBins = std::move(bins);
  const bool hasLoadedSong = !waveformBins.empty();

  if (!hasLoadedSong && wasEditing) {
    editButton.setToggleState(false, juce::dontSendNotification);
    editButton.setButtonText(L10n::tr("EDITAR", "EDIT"));
    if (onEditToggle)
      onEditToggle(false);
  }

  editButton.setVisible(hasLoadedSong);
  editButton.setEnabled(hasLoadedSong);
  if (!hasLoadedSong)
    cancelDeferredPartJumpFromDoubleClick();
  resized();
  repaint();
}

void WaveformBar::setArrangement(
    const SongArrangementSnapshot &arrangementSnapshot,
    const bool isCustomArrangement) {
  arrangement = arrangementSnapshot;
  customArrangement = isCustomArrangement;
  normalizeArrangement(true);

  dragMode = DragMode::none;
  activeDragPartId.clear();
  dragAnchorActive = false;
  dragAnchorEdgeIsStart = true;
  dragAnchoredBoundarySec = 0.0;
  cancelDeferredPartJumpFromDoubleClick();
  repaint();
}

void WaveformBar::setSongRhythm(const double bpm,
                                const juce::String &timeSignature) {
  songBpm = juce::jmax(0.0, bpm);

  int numerator = timeSignature.trim()
                      .upToFirstOccurrenceOf("/", false, false)
                      .getIntValue();
  if (numerator <= 0)
    numerator = 4;
  songBeatsPerBar = juce::jmax(1, numerator);

  repaint();
}

void WaveformBar::setTransportProviders(PositionProvider positionCb,
                                        LengthProvider lengthCb) {
  positionProvider = std::move(positionCb);
  lengthProvider = std::move(lengthCb);
}

void WaveformBar::setSeekCallback(SeekCallback cb) { onSeekTo = std::move(cb); }

void WaveformBar::setOnEditToggle(EditToggleCallback cb) {
  onEditToggle = std::move(cb);
}

void WaveformBar::setOnArrangementChanged(ArrangementChangedCallback cb) {
  onArrangementChanged = std::move(cb);
}

void WaveformBar::setOnArrangementReset(ArrangementResetCallback cb) {
  onArrangementReset = std::move(cb);
}

void WaveformBar::setOnArrangementPartJumpRequest(
    ArrangementPartJumpRequestCallback cb) {
  onArrangementPartJumpRequest = std::move(cb);
}

void WaveformBar::setOnArrangementPartLoopToggleRequest(
    ArrangementPartLoopToggleRequestCallback cb) {
  onArrangementPartLoopToggleRequest = std::move(cb);
}

void WaveformBar::setPendingArrangementPartProvider(
    PendingArrangementPartProvider cb) {
  pendingArrangementPartProvider = std::move(cb);
}

void WaveformBar::setActiveArrangementLoopPartProvider(
    ActiveArrangementLoopPartProvider cb) {
  activeArrangementLoopPartProvider = std::move(cb);
}

void WaveformBar::refreshThemeColours() {
  editButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  editButton.setColour(juce::TextButton::buttonOnColourId,
                       UITheme::cardBgPressed);
  editButton.setColour(juce::TextButton::textColourOffId,
                       UITheme::textPrimary);
  editButton.setColour(juce::TextButton::textColourOnId,
                       UITheme::textPrimary);
  snapModeBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBgHover);
  snapModeBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
  snapModeBox.setColour(juce::ComboBox::outlineColourId,
                        UITheme::cardBorder.withAlpha(0.75f));
  snapModeBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);

  for (auto &btn : sectionAddButtons) {
    btn->setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
    btn->setColour(juce::TextButton::buttonOnColourId, UITheme::cardBgPressed);
    btn->setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
    btn->setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);
  }
  repaint();
}

void WaveformBar::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();
  if (bounds.getWidth() < 2.0f)
    return;

  drawStudioBackground(g, bounds);
  {
    juce::ColourGradient base(UITheme::panelDark.withAlpha(0.42f),
                              bounds.getCentreX(), bounds.getY(),
                              UITheme::panelDark.withAlpha(0.65f),
                              bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(base);
    g.fillRect(bounds);
  }

  const auto mainWaveBounds = getMainWaveBounds(bounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const auto miniMapBounds = getMiniMapBounds(bounds);

  if (mainWaveBodyBounds.getWidth() < 2.0f ||
      mainWaveBodyBounds.getHeight() < 2.0f)
    return;

  if (waveformBins.empty()) {
    g.setColour(UITheme::cardBg.withAlpha(0.55f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText(L10n::tr("Doble click en una cancion para cargar waveform",
                              "Double-click a song to load stems waveform"),
                     mainWaveBodyBounds.toNearestInt().reduced(20),
                     juce::Justification::centred, 2);
    if (miniMapBounds.getHeight() > 0.0f) {
      g.setColour(UITheme::cardBg.withAlpha(0.45f));
      g.fillRoundedRectangle(miniMapBounds, 4.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.7f));
      g.drawRoundedRectangle(miniMapBounds, 4.0f, 1.0f);
    }
    g.setColour(UITheme::accent);
    g.fillRect(0.0f, 0.0f, 2.0f, bounds.getHeight());
    return;
  }

  const auto window = getVisibleWindow();
  drawWaveform(g, mainWaveBodyBounds, window.first, window.second,
               isEditingArrangement(), true);
  drawArrangementOverlay(g, mainWaveBodyBounds);

  if (seekRulerBounds.getWidth() > 2.0f && seekRulerBounds.getHeight() > 2.0f)
    drawSeekRuler(g, seekRulerBounds, window.first, window.second);

  if (miniMapBounds.getWidth() > 2.0f && miniMapBounds.getHeight() > 2.0f)
    drawMiniMap(g, miniMapBounds);
}

void WaveformBar::resized() {
  const auto localBounds = getLocalBounds().toFloat();
  const auto mainWaveBounds = getMainWaveBounds(localBounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  auto toolbarBounds = getEditToolbarBounds(localBounds).toNearestInt();
  const int editButtonW = 72;
  const int snapModeBoxW = 132;
  const bool hasLoadedSong = !waveformBins.empty();

  if (!hasLoadedSong) {
    editButton.setVisible(false);
    editButton.setEnabled(false);
    snapModeBox.setVisible(false);
    snapModeBox.setEnabled(false);
    for (auto &btn : sectionAddButtons) {
      btn->setVisible(false);
      btn->setEnabled(false);
    }
    return;
  }

  editButton.setVisible(true);
  editButton.setEnabled(true);
  const bool editing = isEditingArrangement();

  if (editing) {
    auto right = toolbarBounds.removeFromRight(editButtonW).reduced(1);
    editButton.setBounds(right);
    toolbarBounds.removeFromRight(6);

    auto left =
        toolbarBounds.removeFromLeft(juce::jmin(snapModeBoxW,
                                                juce::jmax(92, toolbarBounds.getWidth() / 3)))
            .reduced(1);
    snapModeBox.setBounds(left);
    snapModeBox.setVisible(true);
    snapModeBox.setEnabled(true);
    toolbarBounds.removeFromLeft(6);

    auto buttonsArea = toolbarBounds.reduced(0, 1);
    const int buttonGap = 4;

    std::vector<int> widths;
    widths.reserve(sectionAddButtons.size());
    int totalWidth = 0;
    for (const auto &btn : sectionAddButtons) {
      const auto text = btn->getButtonText();
      const int roughTextPx = text.length() * 7;
      const int width = juce::jlimit(56, 96, 20 + roughTextPx);
      widths.push_back(width);
      totalWidth += width;
    }

    if (!widths.empty())
      totalWidth += buttonGap * (static_cast<int>(widths.size()) - 1);

    int x = buttonsArea.getX();
    if (buttonsArea.getWidth() > totalWidth)
      x += (buttonsArea.getWidth() - totalWidth) / 2;

    for (size_t i = 0; i < sectionAddButtons.size(); ++i) {
      const int width = widths[i];
      const bool fits = (x + width) <= buttonsArea.getRight();
      sectionAddButtons[i]->setBounds(x, buttonsArea.getY(), width,
                                      buttonsArea.getHeight());
      sectionAddButtons[i]->setVisible(fits);
      sectionAddButtons[i]->setEnabled(fits);
      x += width + buttonGap;
    }
    return;
  }

  for (auto &btn : sectionAddButtons) {
    btn->setVisible(false);
    btn->setEnabled(false);
  }
  snapModeBox.setVisible(false);
  snapModeBox.setEnabled(false);

  auto floating = mainWaveBodyBounds.toNearestInt();
  if (floating.getWidth() > 4 && floating.getHeight() > 4) {
    editButton.setBounds(
        floating.removeFromTop(28).removeFromRight(editButtonW).reduced(1));
  } else {
    editButton.setBounds(toolbarBounds.removeFromRight(editButtonW).reduced(1));
  }
}

void WaveformBar::mouseDown(const juce::MouseEvent &event) {
  dragMode = DragMode::none;
  activeDragPartId.clear();
  dragAnchorActive = false;
  dragAnchorEdgeIsStart = true;
  dragAnchoredBoundarySec = 0.0;
  arrangementDragPendingCommit = false;

  const auto bounds = getLocalBounds().toFloat();
  const auto mainWaveBounds = getMainWaveBounds(bounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const auto miniMapBounds = getMiniMapBounds(bounds);

  if (miniMapBounds.getWidth() > 2.0f && miniMapBounds.contains(event.position)) {
    if (event.mods.isPopupMenu())
      return;

    const auto viewportBounds = getViewportBoundsInMiniMap(miniMapBounds);
    const float normalized = juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - miniMapBounds.getX()) /
            juce::jmax(1.0f, miniMapBounds.getWidth()));

    dragViewportStart = viewportStart;
    dragViewportSpan = viewportSpan;
    dragAnchorNormalized = normalized;

    const bool nearLeft =
        std::abs(event.position.x - viewportBounds.getX()) <=
        kViewportHandleHitWidth;
    const bool nearRight =
        std::abs(event.position.x - viewportBounds.getRight()) <=
        kViewportHandleHitWidth;

    if (nearLeft)
      dragMode = DragMode::resizeViewportLeft;
    else if (nearRight)
      dragMode = DragMode::resizeViewportRight;
    else {
      if (!viewportBounds.contains(event.position))
        setVisibleWindow(normalized - (viewportSpan * 0.5f), viewportSpan);
      dragMode = DragMode::panViewport;
      dragViewportStart = viewportStart;
      dragViewportSpan = viewportSpan;
      dragAnchorNormalized = normalized;
    }

    if (dragMode == DragMode::resizeViewportLeft ||
        dragMode == DragMode::resizeViewportRight)
      setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
      setMouseCursor(juce::MouseCursor::DraggingHandCursor);

    repaint();
    return;
  }

  if (seekRulerBounds.contains(event.position)) {
    if (event.mods.isPopupMenu())
      return;
    dragMode = DragMode::seek;
    handleSeek(event.position.x, seekRulerBounds);
    return;
  }

  if (!mainWaveBounds.contains(event.position))
    return;

  if (isEditingArrangement() && mainWaveBodyBounds.contains(event.position)) {
    rebuildPartOverlayBounds(mainWaveBodyBounds);
    const auto hit = hitTestArrangementPart(event.position);
    if (juce::isPositiveAndBelow(hit.partIndex,
                                 static_cast<int>(arrangement.parts.size()))) {
      selectedPartIndex = hit.partIndex;
      if (hit.deleteButton) {
        confirmDeleteArrangementPart(hit.partIndex);
        repaint();
        return;
      }
      const auto &part = arrangement.parts[static_cast<size_t>(hit.partIndex)];
      activeDragPartId = part.id;
      dragOriginPartStartSec = part.startSec;
      dragOriginPartEndSec = part.endSec;
      dragAnchorSourceSec =
          windowXToSourceSeconds(mainWaveBodyBounds, event.position.x);

      if (event.mods.isPopupMenu()) {
        openPartActionMenu(hit.partIndex);
        repaint();
        return;
      }

      if (hit.leftEdge)
        dragMode = DragMode::resizePartLeft;
      else if (hit.rightEdge)
        dragMode = DragMode::resizePartRight;
      else
        dragMode = DragMode::movePart;

      if (dragMode == DragMode::resizePartLeft ||
          dragMode == DragMode::resizePartRight)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
      else
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);

      repaint();
      return;
    }
  }

  if (event.mods.isPopupMenu())
    return;
}

void WaveformBar::mouseDrag(const juce::MouseEvent &event) {
  const auto bounds = getLocalBounds().toFloat();
  const auto mainWaveBounds = getMainWaveBounds(bounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto miniMapBounds = getMiniMapBounds(bounds);
  const auto seekBounds =
      mainWaveBodyBounds.getWidth() > 2.0f ? mainWaveBodyBounds : mainWaveBounds;

  if (dragMode == DragMode::seek) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    handleSeek(event.position.x, seekBounds);
    return;
  }

  if (dragMode == DragMode::panViewport ||
      dragMode == DragMode::resizeViewportLeft ||
      dragMode == DragMode::resizeViewportRight) {
    if (dragMode == DragMode::panViewport)
      setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
      setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);

    if (miniMapBounds.getWidth() <= 2.0f)
      return;

    const float normalized = juce::jlimit(
        0.0f, 1.0f,
        (event.position.x - miniMapBounds.getX()) /
            juce::jmax(1.0f, miniMapBounds.getWidth()));

    if (dragMode == DragMode::panViewport) {
      const float delta = normalized - dragAnchorNormalized;
      setVisibleWindow(dragViewportStart + delta, dragViewportSpan);
      return;
    }

    if (dragMode == DragMode::resizeViewportLeft) {
      const float right = dragViewportStart + dragViewportSpan;
      const float nextStart =
          juce::jlimit(0.0f, right - kMinViewportSpan, normalized);
      setVisibleWindow(nextStart, right - nextStart);
      return;
    }

    const float minEnd = dragViewportStart + kMinViewportSpan;
    const float nextEnd = juce::jlimit(minEnd, 1.0f, normalized);
    setVisibleWindow(dragViewportStart, nextEnd - dragViewportStart);
    return;
  }

  if (dragMode != DragMode::movePart && dragMode != DragMode::resizePartLeft &&
      dragMode != DragMode::resizePartRight)
    return;

  if (dragMode == DragMode::movePart)
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
  else
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);

  if (!isEditingArrangement() || !mainWaveBodyBounds.contains(event.position))
    return;

  const int partIndex = findPartIndexById(activeDragPartId);
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0)
    return;

  const double pointerSec =
      windowXToSourceSeconds(mainWaveBodyBounds, event.position.x);
  const bool bypassSnap = isSnapBypassed(event.mods);
  const double deltaSec =
      snapSeconds(pointerSec - dragAnchorSourceSec, bypassSnap);

  struct AnchorBoundaryResult {
    bool found = false;
    double boundarySec = 0.0;
    double distanceSec = std::numeric_limits<double>::max();
  };

  const auto findNearestBoundary =
      [this, partIndex](const double candidateSec,
                        const double maxDistanceSec) -> AnchorBoundaryResult {
    AnchorBoundaryResult best;
    for (int i = 0; i < static_cast<int>(arrangement.parts.size()); ++i) {
      if (i == partIndex)
        continue;
      const auto &otherPart = arrangement.parts[static_cast<size_t>(i)];
      const double boundaries[2] = {otherPart.startSec, otherPart.endSec};
      for (const auto boundarySec : boundaries) {
        const double distance = std::abs(boundarySec - candidateSec);
        if (distance <= maxDistanceSec && distance < best.distanceSec) {
          best.found = true;
          best.distanceSec = distance;
          best.boundarySec = boundarySec;
        }
      }
    }
    return best;
  };

  const auto applyStickyAnchorToEdge =
      [this, &findNearestBoundary, bypassSnap](const double candidateEdgeSec,
                                               const bool edgeIsStart) -> double {
    if (bypassSnap) {
      dragAnchorActive = false;
      dragAnchoredBoundarySec = 0.0;
      return candidateEdgeSec;
    }

    if (dragAnchorActive && dragAnchorEdgeIsStart == edgeIsStart) {
      if (std::abs(candidateEdgeSec - dragAnchoredBoundarySec) <=
          kPartAnchorReleaseThresholdSec) {
        return dragAnchoredBoundarySec;
      }
      dragAnchorActive = false;
      dragAnchoredBoundarySec = 0.0;
    }

    const auto nearest =
        findNearestBoundary(candidateEdgeSec, kPartAnchorAttachThresholdSec);
    if (nearest.found) {
      dragAnchorActive = true;
      dragAnchorEdgeIsStart = edgeIsStart;
      dragAnchoredBoundarySec = nearest.boundarySec;
      return nearest.boundarySec;
    }

    return candidateEdgeSec;
  };

  auto &part = arrangement.parts[static_cast<size_t>(partIndex)];
  const double previousStart = part.startSec;
  const double previousEnd = part.endSec;

  if (dragMode == DragMode::movePart) {
    double duration = juce::jmax(kMinPartDurationSec,
                                 dragOriginPartEndSec - dragOriginPartStartSec);
    duration = juce::jmin(duration, songLength);

    const double maxStart = juce::jmax(0.0, songLength - duration);
    double nextStart = snapSeconds(dragOriginPartStartSec + deltaSec, bypassSnap);
    nextStart = juce::jlimit(0.0, maxStart, nextStart);

    double nextEnd = juce::jlimit(nextStart, songLength, nextStart + duration);

    if (!bypassSnap && dragAnchorActive) {
      const double activeEdge = dragAnchorEdgeIsStart ? nextStart : nextEnd;
      if (std::abs(activeEdge - dragAnchoredBoundarySec) <=
          kPartAnchorReleaseThresholdSec) {
        const double offset = dragAnchoredBoundarySec - activeEdge;
        nextStart += offset;
        nextEnd += offset;
      } else {
        dragAnchorActive = false;
        dragAnchoredBoundarySec = 0.0;
      }
    }

    if (!bypassSnap && !dragAnchorActive) {
      const auto startAnchor =
          findNearestBoundary(nextStart, kPartAnchorAttachThresholdSec);
      const auto endAnchor =
          findNearestBoundary(nextEnd, kPartAnchorAttachThresholdSec);
      if (startAnchor.found || endAnchor.found) {
        const bool anchorStart =
            !endAnchor.found ||
            (startAnchor.found &&
             startAnchor.distanceSec <= endAnchor.distanceSec);
        dragAnchorActive = true;
        dragAnchorEdgeIsStart = anchorStart;
        dragAnchoredBoundarySec =
            anchorStart ? startAnchor.boundarySec : endAnchor.boundarySec;

        const double activeEdge = anchorStart ? nextStart : nextEnd;
        const double offset = dragAnchoredBoundarySec - activeEdge;
        nextStart += offset;
        nextEnd += offset;
      }
    }

    if (nextStart < 0.0) {
      nextStart = 0.0;
      nextEnd = juce::jmin(songLength, duration);
    } else if (nextEnd > songLength) {
      nextEnd = songLength;
      nextStart = juce::jmax(0.0, songLength - duration);
    }

    part.startSec = nextStart;
    part.endSec = nextEnd;
  } else if (dragMode == DragMode::resizePartLeft) {
    double nextStart = snapSeconds(dragOriginPartStartSec + deltaSec, bypassSnap);
    nextStart = applyStickyAnchorToEdge(nextStart, true);
    const double maxStart =
        juce::jmax(0.0, dragOriginPartEndSec - kMinPartDurationSec);
    part.startSec = juce::jlimit(0.0, maxStart, nextStart);
    part.endSec = dragOriginPartEndSec;

    if (dragAnchorActive && dragAnchorEdgeIsStart &&
        !nearlyEqual(part.startSec, dragAnchoredBoundarySec, 0.001)) {
      dragAnchorActive = false;
      dragAnchoredBoundarySec = 0.0;
    }
  } else if (dragMode == DragMode::resizePartRight) {
    double nextEnd = snapSeconds(dragOriginPartEndSec + deltaSec, bypassSnap);
    nextEnd = applyStickyAnchorToEdge(nextEnd, false);
    const double minEnd = dragOriginPartStartSec + kMinPartDurationSec;
    part.startSec = dragOriginPartStartSec;
    part.endSec = juce::jlimit(minEnd, songLength, nextEnd);

    if (dragAnchorActive && !dragAnchorEdgeIsStart &&
        !nearlyEqual(part.endSec, dragAnchoredBoundarySec, 0.001)) {
      dragAnchorActive = false;
      dragAnchoredBoundarySec = 0.0;
    }
  }

  if (part.endSec < part.startSec)
    std::swap(part.endSec, part.startSec);

  if ((part.endSec - part.startSec) < kMinPartDurationSec) {
    part.endSec = juce::jmin(songLength, part.startSec + kMinPartDurationSec);
    if ((part.endSec - part.startSec) < kMinPartDurationSec)
      part.startSec = juce::jmax(0.0, part.endSec - kMinPartDurationSec);
  }

  if (nearlyEqual(previousStart, part.startSec) &&
      nearlyEqual(previousEnd, part.endSec))
    return;

  selectedPartIndex = partIndex;
  customArrangement = true;
  arrangementDragPendingCommit = true;
  repaint();
}

void WaveformBar::mouseUp(const juce::MouseEvent &) {
  const bool wasArrangementDrag =
      dragMode == DragMode::movePart || dragMode == DragMode::resizePartLeft ||
      dragMode == DragMode::resizePartRight;
  dragMode = DragMode::none;
  activeDragPartId.clear();
  dragAnchorActive = false;
  dragAnchorEdgeIsStart = true;
  dragAnchoredBoundarySec = 0.0;
  if (wasArrangementDrag && arrangementDragPendingCommit) {
    arrangementDragPendingCommit = false;
    notifyArrangementChanged(true);
    repaint();
  } else {
    arrangementDragPendingCommit = false;
  }
  setMouseCursor(juce::MouseCursor::NormalCursor);
}

void WaveformBar::mouseMove(const juce::MouseEvent &event) {
  juce::MouseCursor nextCursor = juce::MouseCursor::NormalCursor;
  const auto bounds = getLocalBounds().toFloat();
  const auto mainWaveBounds = getMainWaveBounds(bounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const auto miniMapBounds = getMiniMapBounds(bounds);

  if (miniMapBounds.contains(event.position) && miniMapBounds.getWidth() > 2.0f) {
    const auto viewportBounds = getViewportBoundsInMiniMap(miniMapBounds);
    const bool nearLeft =
        std::abs(event.position.x - viewportBounds.getX()) <=
        kViewportHandleHitWidth;
    const bool nearRight =
        std::abs(event.position.x - viewportBounds.getRight()) <=
        kViewportHandleHitWidth;

    if (nearLeft || nearRight)
      nextCursor = juce::MouseCursor::LeftRightResizeCursor;
    else if (viewportBounds.contains(event.position))
      nextCursor = juce::MouseCursor::DraggingHandCursor;
  } else if (seekRulerBounds.contains(event.position)) {
    nextCursor = juce::MouseCursor::PointingHandCursor;
  } else if (isEditingArrangement() &&
             mainWaveBodyBounds.contains(event.position)) {
    rebuildPartOverlayBounds(mainWaveBodyBounds);
    const auto hit = hitTestArrangementPart(event.position);
    if (juce::isPositiveAndBelow(hit.partIndex,
                                 static_cast<int>(arrangement.parts.size()))) {
      if (hit.deleteButton)
        nextCursor = juce::MouseCursor::PointingHandCursor;
      else if (hit.leftEdge || hit.rightEdge)
        nextCursor = juce::MouseCursor::LeftRightResizeCursor;
      else
        nextCursor = juce::MouseCursor::DraggingHandCursor;
    }
  }

  setMouseCursor(nextCursor);
}

void WaveformBar::mouseExit(const juce::MouseEvent &) {
  setMouseCursor(juce::MouseCursor::NormalCursor);
}

void WaveformBar::mouseDoubleClick(const juce::MouseEvent &event) {
  const auto mainWaveBounds = getMainWaveBounds(getLocalBounds().toFloat());
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  if (!mainWaveBodyBounds.contains(event.position))
    return;

  rebuildPartOverlayBounds(mainWaveBodyBounds);
  const auto hit = hitTestArrangementPart(event.position);
  if (juce::isPositiveAndBelow(hit.partIndex,
                               static_cast<int>(arrangement.parts.size()))) {
    selectedPartIndex = hit.partIndex;
    if (isEditingArrangement()) {
      cancelDeferredPartJumpFromDoubleClick();
      openPartActionMenu(hit.partIndex);
    } else {
      const int clicks = event.getNumberOfClicks();
      if (clicks >= 3) {
        cancelDeferredPartJumpFromDoubleClick();
        if (onArrangementPartLoopToggleRequest)
          onArrangementPartLoopToggleRequest(hit.partIndex);
      } else if (clicks == 2) {
        scheduleDeferredPartJumpFromDoubleClick(hit.partIndex);
      }
    }
    repaint();
  }
}

void WaveformBar::mouseWheelMove(const juce::MouseEvent &event,
                                 const juce::MouseWheelDetails &wheel) {
  const auto mainWaveBounds = getMainWaveBounds(getLocalBounds().toFloat());
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const bool onWaveBody = mainWaveBodyBounds.contains(event.position);
  const bool onSeekRuler = seekRulerBounds.contains(event.position);
  if (!onWaveBody && !onSeekRuler)
    return;

  const float dir = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
  if (dir == 0.0f)
    return;

  const float anchor =
      mainWaveBodyBounds.getWidth() > 0.0f
          ? juce::jlimit(0.0f, 1.0f,
                         (event.position.x - mainWaveBodyBounds.getX()) /
                             mainWaveBodyBounds.getWidth())
          : 0.5f;

  const float intensity = juce::jlimit(1.0f, 3.5f, 1.0f + std::abs(dir) * 2.4f);
  const float step = 1.06f * intensity;
  applyZoom(dir > 0.0f ? step : 1.0f / step, anchor);
}

void WaveformBar::mouseMagnify(const juce::MouseEvent &event,
                               float magnifyAmount) {
  if (magnifyAmount == 0.0f)
    return;

  const auto mainWaveBounds = getMainWaveBounds(getLocalBounds().toFloat());
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const bool onWaveBody = mainWaveBodyBounds.contains(event.position);
  const bool onSeekRuler = seekRulerBounds.contains(event.position);
  if (!onWaveBody && !onSeekRuler)
    return;

  const float anchor =
      mainWaveBodyBounds.getWidth() > 0.0f
          ? juce::jlimit(0.0f, 1.0f,
                         (event.position.x - mainWaveBodyBounds.getX()) /
                             mainWaveBodyBounds.getWidth())
          : 0.5f;
  applyZoom(1.0f + magnifyAmount, anchor);
}

void WaveformBar::timerCallback() {
  flushDeferredPartJumpFromDoubleClickIfDue();
  repaint();
}

void WaveformBar::scheduleDeferredPartJumpFromDoubleClick(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size()))) {
    cancelDeferredPartJumpFromDoubleClick();
    return;
  }

  pendingJumpDoubleClickPartIndex = partIndex;
  const double timeoutMs = juce::jlimit(
      140.0, 260.0, static_cast<double>(juce::MouseEvent::getDoubleClickTimeout()));
  pendingJumpDoubleClickDispatchAtMs =
      juce::Time::getMillisecondCounterHiRes() + timeoutMs;
}

void WaveformBar::cancelDeferredPartJumpFromDoubleClick() {
  pendingJumpDoubleClickPartIndex = -1;
  pendingJumpDoubleClickDispatchAtMs = 0.0;
}

void WaveformBar::flushDeferredPartJumpFromDoubleClickIfDue(const bool force) {
  if (!juce::isPositiveAndBelow(pendingJumpDoubleClickPartIndex,
                                static_cast<int>(arrangement.parts.size()))) {
    cancelDeferredPartJumpFromDoubleClick();
    return;
  }

  if (!force) {
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs < pendingJumpDoubleClickDispatchAtMs)
      return;
  }

  const int partIndex = pendingJumpDoubleClickPartIndex;
  cancelDeferredPartJumpFromDoubleClick();
  if (onArrangementPartJumpRequest != nullptr)
    onArrangementPartJumpRequest(partIndex);
}

void WaveformBar::handleSeek(float x) {
  const auto mainWaveBounds = getMainWaveBounds(getLocalBounds().toFloat());
  const auto seekRulerBounds = getSeekRulerBounds(mainWaveBounds);
  const auto mainWaveBodyBounds = getMainWaveBodyBounds(mainWaveBounds);
  if (seekRulerBounds.getWidth() > 2.0f)
    handleSeek(x, seekRulerBounds);
  else if (mainWaveBodyBounds.getWidth() > 2.0f)
    handleSeek(x, mainWaveBodyBounds);
  else
    handleSeek(x, mainWaveBounds);
}

void WaveformBar::handleSeek(float x, const juce::Rectangle<float> &seekBounds) {
  if (!onSeekTo || !lengthProvider)
    return;

  const double songLength = juce::jmax(0.0, lengthProvider());
  if (songLength <= 0.0 || seekBounds.getWidth() <= 0.0f)
    return;

  const float clampedX =
      juce::jlimit(seekBounds.getX(), seekBounds.getRight(), x);
  const float ratio = (clampedX - seekBounds.getX()) / seekBounds.getWidth();
  const auto window = getVisibleWindow();
  const double target =
      songLength * juce::jlimit(0.0f, 1.0f, window.first + (ratio * window.second));
  onSeekTo(target);
}

std::pair<float, float> WaveformBar::getVisibleWindow() const {
  const float span = juce::jlimit(kMinViewportSpan, 1.0f, viewportSpan);
  float start = juce::jlimit(0.0f, 1.0f, viewportStart);
  if ((start + span) > 1.0f)
    start = 1.0f - span;
  return {start, span};
}

void WaveformBar::setVisibleWindow(float start, float span) {
  span = juce::jlimit(kMinViewportSpan, 1.0f, span);
  start = juce::jlimit(0.0f, 1.0f, start);
  if ((start + span) > 1.0f)
    start = 1.0f - span;

  const bool changed = !nearlyEqual(start, viewportStart) ||
                       !nearlyEqual(span, viewportSpan);
  viewportStart = start;
  viewportSpan = span;
  if (changed)
    repaint();
}

void WaveformBar::applyZoom(float factor, float anchorRatio) {
  if (!std::isfinite(factor) || factor <= 0.0f)
    return;

  anchorRatio = juce::jlimit(0.0f, 1.0f, anchorRatio);
  const float oldSpan = viewportSpan;
  const float newSpan =
      juce::jlimit(kMinViewportSpan, 1.0f, oldSpan / factor);
  const float anchorSource = viewportStart + (anchorRatio * oldSpan);
  const float newStart = anchorSource - (anchorRatio * newSpan);
  setVisibleWindow(newStart, newSpan);
}

double WaveformBar::getSongLengthSeconds() const {
  return lengthProvider ? juce::jmax(0.0, lengthProvider()) : 0.0;
}

double WaveformBar::getBarDurationSeconds() const {
  if (songBpm <= 0.0 || songBeatsPerBar <= 0)
    return 0.0;
  return (60.0 / songBpm) * static_cast<double>(songBeatsPerBar);
}

double WaveformBar::getSnapStepSeconds() const {
  const double beatDurationSec = songBpm > 0.0 ? 60.0 / songBpm : 0.0;
  const double barDurationSec = getBarDurationSeconds();

  switch (snapMode) {
  case SnapMode::Bar:
    return barDurationSec > 0.0
               ? barDurationSec
               : (beatDurationSec > 0.0 ? beatDurationSec : kPartSnapSeconds);
  case SnapMode::HalfBeat:
    return beatDurationSec > 0.0 ? (beatDurationSec * 0.5)
                                 : (kPartSnapSeconds * 0.5);
  case SnapMode::Free:
    return 0.0;
  case SnapMode::Beat:
  default:
    return beatDurationSec > 0.0 ? beatDurationSec : kPartSnapSeconds;
  }
}

bool WaveformBar::isSnapBypassed(const juce::ModifierKeys &mods) const {
  return snapMode == SnapMode::Free || mods.isAltDown();
}

double WaveformBar::snapSeconds(const double seconds,
                                const bool bypassSnap) const {
  if (!std::isfinite(seconds))
    return 0.0;
  if (bypassSnap)
    return seconds;

  const double snapStepSec = getSnapStepSeconds();
  if (snapStepSec <= 0.0 || !std::isfinite(snapStepSec))
    return seconds;
  return std::round(seconds / snapStepSec) * snapStepSec;
}

void WaveformBar::normalizeArrangement(const bool sortByStart) {
  const double songLength = getSongLengthSeconds();
  juce::String selectedId;
  if (juce::isPositiveAndBelow(selectedPartIndex,
                               static_cast<int>(arrangement.parts.size()))) {
    selectedId = arrangement.parts[static_cast<size_t>(selectedPartIndex)].id;
  }

  for (auto &part : arrangement.parts) {
    if (part.id.isEmpty())
      part.id = juce::Uuid().toString();

    const auto trimmedName = part.name.trim();
    part.name = trimmedName.isEmpty() ? L10n::tr("Parte", "Part") : trimmedName;
    if (!isArrangementPartColorValid(part.color))
      part.color = makeRandomArrangementPartColor();

    part.startSec = juce::jlimit(0.0, songLength, part.startSec);
    part.endSec = juce::jlimit(0.0, songLength, part.endSec);
    if (part.endSec < part.startSec)
      std::swap(part.endSec, part.startSec);

    if ((part.endSec - part.startSec) < kMinPartDurationSec) {
      part.endSec = juce::jmin(songLength, part.startSec + kMinPartDurationSec);
      if ((part.endSec - part.startSec) < kMinPartDurationSec)
        part.startSec = juce::jmax(0.0, part.endSec - kMinPartDurationSec);
    }

    part.startSec = snapSeconds(part.startSec);
    part.endSec = snapSeconds(part.endSec);

    part.startSec = juce::jlimit(0.0, songLength, part.startSec);
    part.endSec = juce::jlimit(0.0, songLength, part.endSec);
    if ((part.endSec - part.startSec) < kMinPartDurationSec) {
      part.endSec = juce::jmin(songLength, part.startSec + kMinPartDurationSec);
      if ((part.endSec - part.startSec) < kMinPartDurationSec)
        part.startSec = juce::jmax(0.0, part.endSec - kMinPartDurationSec);
    }
  }

  if (sortByStart && arrangement.parts.size() > 1) {
    std::stable_sort(
        arrangement.parts.begin(), arrangement.parts.end(),
        [](const SongArrangementPartSnapshot &a,
           const SongArrangementPartSnapshot &b) {
          if (nearlyEqual(a.startSec, b.startSec))
            return a.endSec < b.endSec;
          return a.startSec < b.startSec;
        });
  }

  if (selectedId.isNotEmpty())
    selectedPartIndex = findPartIndexById(selectedId);

  if (!juce::isPositiveAndBelow(selectedPartIndex,
                                static_cast<int>(arrangement.parts.size()))) {
    selectedPartIndex = arrangement.parts.empty() ? -1 : 0;
  }
}

int WaveformBar::findPartIndexById(const juce::String &id) const {
  if (id.isEmpty())
    return -1;
  for (size_t i = 0; i < arrangement.parts.size(); ++i) {
    if (arrangement.parts[i].id == id)
      return static_cast<int>(i);
  }
  return -1;
}

bool WaveformBar::isEditingArrangement() const {
  return editButton.getToggleState();
}

juce::Rectangle<float>
WaveformBar::getEditToolbarBounds(const juce::Rectangle<float> &bounds) const {
  const auto area = bounds.reduced(8.0f, 6.0f);
  const float height = juce::jmin(kEditToolbarHeight, area.getHeight());
  return {area.getX(), area.getY(), area.getWidth(), height};
}

juce::Rectangle<float>
WaveformBar::getMainWaveBounds(const juce::Rectangle<float> &bounds) const {
  auto area = bounds.reduced(8.0f, 6.0f);
  if (isEditingArrangement())
    area = area.withTrimmedTop(kEditToolbarHeight + kEditToolbarBottomGap);
  area = area.withTrimmedBottom(kMiniMapHeight + kMiniMapGap);
  return area;
}

juce::Rectangle<float>
WaveformBar::getSeekRulerBounds(
    const juce::Rectangle<float> &mainWaveBounds) const {
  if (mainWaveBounds.getWidth() <= 2.0f || mainWaveBounds.getHeight() <= 8.0f)
    return {};

  const float rulerHeight =
      juce::jmin(kSeekRulerHeight, mainWaveBounds.getHeight() - 6.0f);
  return {mainWaveBounds.getX(), mainWaveBounds.getY(), mainWaveBounds.getWidth(),
          rulerHeight};
}

juce::Rectangle<float> WaveformBar::getMainWaveBodyBounds(
    const juce::Rectangle<float> &mainWaveBounds) const {
  const auto ruler = getSeekRulerBounds(mainWaveBounds);
  if (ruler.getHeight() <= 0.0f)
    return mainWaveBounds;

  auto body = mainWaveBounds.withTrimmedTop(ruler.getHeight() + kSeekRulerGap);
  if (body.getHeight() < 2.0f)
    body.setHeight(0.0f);
  return body;
}

juce::Rectangle<float>
WaveformBar::getMiniMapBounds(const juce::Rectangle<float> &bounds) const {
  auto area = bounds.reduced(8.0f, 6.0f);
  if (isEditingArrangement())
    area = area.withTrimmedTop(kEditToolbarHeight + kEditToolbarBottomGap);
  if (area.getHeight() <= kMiniMapHeight)
    return {};

  return {area.getX(), area.getBottom() - kMiniMapHeight, area.getWidth(),
          kMiniMapHeight};
}

juce::Rectangle<float> WaveformBar::getArrangementLaneBounds(
    const juce::Rectangle<float> &mainWaveBounds) const {
  return mainWaveBounds.reduced(2.0f, 2.0f);
}

juce::Rectangle<float> WaveformBar::getViewportBoundsInMiniMap(
    const juce::Rectangle<float> &miniMapBounds) const {
  const auto window = getVisibleWindow();
  const float x = miniMapBounds.getX() + (window.first * miniMapBounds.getWidth());
  const float w = window.second * miniMapBounds.getWidth();
  return {x, miniMapBounds.getY(), w, miniMapBounds.getHeight()};
}

double WaveformBar::windowXToSourceSeconds(
    const juce::Rectangle<float> &windowBounds, const float x) const {
  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0 || windowBounds.getWidth() <= 0.0f)
    return 0.0;

  const float clampedX =
      juce::jlimit(windowBounds.getX(), windowBounds.getRight(), x);
  const float ratio = (clampedX - windowBounds.getX()) / windowBounds.getWidth();
  const auto window = getVisibleWindow();
  const float sourceRatio =
      juce::jlimit(0.0f, 1.0f, window.first + (ratio * window.second));
  return static_cast<double>(sourceRatio) * songLength;
}

float WaveformBar::sourceSecondsToWindowX(
    const juce::Rectangle<float> &windowBounds, const double sourceSeconds) const {
  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0 || windowBounds.getWidth() <= 0.0f)
    return windowBounds.getX();

  const auto window = getVisibleWindow();
  const float normalizedSource =
      static_cast<float>(juce::jlimit(0.0, 1.0, sourceSeconds / songLength));
  const float normalizedWindow =
      (normalizedSource - window.first) / juce::jmax(0.0001f, window.second);
  return windowBounds.getX() +
         (juce::jlimit(0.0f, 1.0f, normalizedWindow) * windowBounds.getWidth());
}

WaveformBar::PartHitResult
WaveformBar::hitTestArrangementPart(const juce::Point<float> pos) const {
  for (int i = static_cast<int>(partOverlayBounds.size()) - 1; i >= 0; --i) {
    const auto &bounds = partOverlayBounds[static_cast<size_t>(i)];
    if (!bounds.body.contains(pos))
      continue;

    PartHitResult hit;
    hit.partIndex = bounds.partIndex;
    hit.deleteButton = bounds.showsDeleteButton && bounds.deleteButton.contains(pos);
    if (hit.deleteButton)
      return hit;
    hit.leftEdge = bounds.leftHandle.contains(pos);
    hit.rightEdge = bounds.rightHandle.contains(pos);
    return hit;
  }
  return {};
}

void WaveformBar::rebuildPartOverlayBounds(
    const juce::Rectangle<float> &mainWaveBounds) {
  partOverlayBounds.clear();

  if (arrangement.parts.empty())
    return;

  const auto lane = getArrangementLaneBounds(mainWaveBounds);
  if (lane.getWidth() <= 2.0f || lane.getHeight() <= 2.0f)
    return;

  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0)
    return;

  const auto window = getVisibleWindow();
  for (int i = 0; i < static_cast<int>(arrangement.parts.size()); ++i) {
    const auto &part = arrangement.parts[static_cast<size_t>(i)];
    const float startRatio =
        static_cast<float>(juce::jlimit(0.0, 1.0, part.startSec / songLength));
    const float endRatio =
        static_cast<float>(juce::jlimit(0.0, 1.0, part.endSec / songLength));

    const float x1 = lane.getX() +
                     ((startRatio - window.first) /
                      juce::jmax(0.0001f, window.second)) *
                         lane.getWidth();
    const float x2 = lane.getX() +
                     ((endRatio - window.first) /
                      juce::jmax(0.0001f, window.second)) *
                         lane.getWidth();

    if (x2 < lane.getX() || x1 > lane.getRight())
      continue;

    const float left = juce::jlimit(lane.getX(), lane.getRight(), x1);
    const float right = juce::jlimit(lane.getX(), lane.getRight(), x2);
    float width = right - left;
    if (width < 2.0f)
      width = 2.0f;

    juce::Rectangle<float> body(left, lane.getY(), width, lane.getHeight());
    if (body.getRight() > lane.getRight())
      body.setRight(lane.getRight());

    const float handleWidth = juce::jmin(kPartHandleHitWidth, body.getWidth() * 0.4f);
    PartOverlayBounds overlay;
    overlay.partIndex = i;
    overlay.body = body;
    overlay.leftHandle =
        juce::Rectangle<float>(body.getX(), body.getY(), handleWidth, body.getHeight());
    overlay.rightHandle = juce::Rectangle<float>(body.getRight() - handleWidth,
                                                 body.getY(), handleWidth,
                                                 body.getHeight());
    constexpr float deleteSize = 13.0f;
    overlay.showsDeleteButton =
        body.getWidth() >= (deleteSize + 16.0f) && body.getHeight() >= (deleteSize + 8.0f);
    if (overlay.showsDeleteButton) {
      overlay.deleteButton = juce::Rectangle<float>(
          body.getRight() - deleteSize - 3.0f, body.getY() + 3.0f, deleteSize,
          deleteSize);
    } else {
      overlay.deleteButton = {};
    }
    partOverlayBounds.push_back(overlay);
  }
}

void WaveformBar::drawWaveform(juce::Graphics &g,
                               const juce::Rectangle<float> &bounds,
                               const float windowStart, const float windowSpan,
                               const bool showGrid,
                               const bool showPlayhead) const {
  if (bounds.getWidth() <= 2.0f || bounds.getHeight() <= 2.0f)
    return;
  if (waveformBins.empty())
    return;

  const int bins = static_cast<int>(waveformBins.size());
  if (bins < 2)
    return;

  const auto waveformBg = juce::Colour::fromRGB(0x1E, 0x1E, 0x1E);
  g.setColour(waveformBg);
  g.fillRoundedRectangle(bounds, 4.0f);

  if (showGrid) {
    const double songLength = getSongLengthSeconds();
    if (songBpm > 0.0 && songLength > 0.0 && songBeatsPerBar > 0) {
      const double barDurationSec = getBarDurationSeconds();
      if (barDurationSec > 0.0) {
        const double visibleStartSec = songLength * windowStart;
        const double visibleEndSec = songLength * (windowStart + windowSpan);
        if (visibleEndSec > visibleStartSec) {
          int64_t firstBar = static_cast<int64_t>(
              std::ceil((visibleStartSec - 1.0e-9) / barDurationSec));
          if (firstBar < 0)
            firstBar = 0;
          int64_t lastBar = static_cast<int64_t>(
              std::floor((visibleEndSec + 1.0e-9) / barDurationSec));

          if (lastBar >= firstBar) {
            for (int64_t bar = firstBar; bar <= lastBar; ++bar) {
              const double barSec = static_cast<double>(bar) * barDurationSec;
              const double normalized = juce::jlimit(0.0, 1.0, barSec / songLength);
              const float windowNorm = static_cast<float>(
                  (normalized - windowStart) / juce::jmax(0.0001f, windowSpan));
              if (windowNorm < 0.0f || windowNorm > 1.0f)
                continue;
              const float x = bounds.getX() + (windowNorm * bounds.getWidth());
              const bool isEveryFourBars = (bar % 4) == 0;
              g.setColour(juce::Colours::white.withAlpha(isEveryFourBars ? 0.34f
                                                                          : 0.16f));
              g.drawLine(x, bounds.getY(), x, bounds.getBottom(),
                         isEveryFourBars ? 1.25f : 0.8f);

              const float anchorRadius = isEveryFourBars ? 2.4f : 1.7f;
              const float anchorY = bounds.getY() + 5.0f;
              g.setColour(juce::Colours::white.withAlpha(isEveryFourBars ? 0.75f
                                                                          : 0.45f));
              g.fillEllipse(x - anchorRadius, anchorY - anchorRadius,
                            anchorRadius * 2.0f, anchorRadius * 2.0f);
            }
          }
        }
      }
    }
  }

  const float midY = bounds.getCentreY();
  const float ampScale = bounds.getHeight() * 0.495f;
  const int widthPixels = juce::jmax(2, static_cast<int>(bounds.getWidth()));

  float progressRatio = 0.0f;
  if (positionProvider && lengthProvider) {
    const double songLength = juce::jmax(0.0, lengthProvider());
    if (songLength > 0.0) {
      const double songPosition = juce::jmax(0.0, positionProvider());
      progressRatio = static_cast<float>(
          juce::jlimit(0.0, 1.0, songPosition / songLength));
    }
  }

  const float playheadRatio = juce::jlimit(
      0.0f, 1.0f,
      (progressRatio - windowStart) / juce::jmax(0.0001f, windowSpan));
  const float playheadX = bounds.getX() + (playheadRatio * bounds.getWidth());

  g.setColour(juce::Colours::white.withAlpha(0.22f));
  const float centerDashes[] = {2.0f, 4.0f};
  g.drawDashedLine({bounds.getX(), midY, bounds.getRight(), midY},
                   centerDashes, 2, 1.0f);

  const int step = 2;
  for (int x = 0; x < widthPixels; x += step) {
    const float t = static_cast<float>(x) / static_cast<float>(widthPixels - 1);
    const float index =
        (windowStart + (t * windowSpan)) * static_cast<float>(bins - 1);
    const int i0 = static_cast<int>(index);
    const int i1 = juce::jmin(i0 + 1, bins - 1);
    const float frac = index - static_cast<float>(i0);
    const float amp = juce::jmap(frac, waveformBins[static_cast<size_t>(i0)],
                                 waveformBins[static_cast<size_t>(i1)]);
    const float lineHalf = juce::jmax(1.2f, amp * ampScale);
    const float xPos = bounds.getX() + static_cast<float>(x);

    juce::Colour lineColour = juce::Colours::white.withAlpha(0.70f);
    if (showPlayhead && xPos <= playheadX)
      lineColour = juce::Colours::white.withAlpha(0.92f);
    g.setColour(lineColour);
    g.drawLine(xPos, midY - lineHalf, xPos, midY + lineHalf, 1.0f);
  }

  g.setColour(waveformBg.brighter(0.16f).withAlpha(0.9f));
  g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

  if (showPlayhead) {
    g.setColour(UITheme::accent);
    g.drawLine(playheadX, bounds.getY(), playheadX, bounds.getBottom(), 1.4f);
  }
}

void WaveformBar::drawSeekRuler(juce::Graphics &g,
                                const juce::Rectangle<float> &bounds,
                                const float windowStart,
                                const float windowSpan) const {
  if (bounds.getWidth() <= 2.0f || bounds.getHeight() <= 2.0f)
    return;

  g.setColour(UITheme::cardBg.withAlpha(0.58f));
  g.fillRoundedRectangle(bounds, 3.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.78f));
  g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

  const double songLength = getSongLengthSeconds();
  if (songLength > 0.0) {
    const float topY = bounds.getY() + 2.0f;
    const float minorBottom = bounds.getBottom() - 6.0f;
    const float majorBottom = bounds.getBottom() - 2.5f;

    if (songBpm > 0.0 && songBeatsPerBar > 0) {
      const double barDurationSec = getBarDurationSeconds();
      if (barDurationSec > 0.0) {
        const double visibleStartSec = songLength * windowStart;
        const double visibleEndSec = songLength * (windowStart + windowSpan);
        int64_t firstBar = static_cast<int64_t>(
            std::ceil((visibleStartSec - 1.0e-9) / barDurationSec));
        if (firstBar < 0)
          firstBar = 0;
        int64_t lastBar = static_cast<int64_t>(
            std::floor((visibleEndSec + 1.0e-9) / barDurationSec));

        for (int64_t bar = firstBar; bar <= lastBar; ++bar) {
          const double barSec = static_cast<double>(bar) * barDurationSec;
          const float x = sourceSecondsToWindowX(bounds, barSec);
          if (x < bounds.getX() || x > bounds.getRight())
            continue;

          const bool isEveryFourBars = (bar % 4) == 0;
          g.setColour(juce::Colours::white.withAlpha(isEveryFourBars ? 0.82f
                                                                      : 0.52f));
          g.drawLine(x, topY, x, isEveryFourBars ? majorBottom : minorBottom,
                     isEveryFourBars ? 1.2f : 0.9f);

          if (isEveryFourBars) {
            g.setColour(juce::Colours::white.withAlpha(0.78f));
            g.setFont(juce::FontOptions(9.5f));
            g.drawText(juce::String(bar + 1),
                       juce::Rectangle<float>(x + 3.0f, bounds.getY() + 1.0f,
                                              34.0f, 10.0f),
                       juce::Justification::left, false);
          }
        }
      }
    } else {
      const int tickCount = 12;
      for (int i = 0; i <= tickCount; ++i) {
        const float ratio = static_cast<float>(i) / static_cast<float>(tickCount);
        const float x = bounds.getX() + ratio * bounds.getWidth();
        const bool major = (i % 2) == 0;
        g.setColour(juce::Colours::white.withAlpha(major ? 0.56f : 0.35f));
        g.drawLine(x, topY, x, major ? majorBottom : minorBottom,
                   major ? 1.05f : 0.8f);
      }
    }
  }

  if (positionProvider && lengthProvider) {
    const double length = juce::jmax(0.0, lengthProvider());
    if (length > 0.0) {
      const double position = juce::jmax(0.0, positionProvider());
      const float progress = static_cast<float>(juce::jlimit(0.0, 1.0, position / length));
      const float playheadNorm =
          (progress - windowStart) / juce::jmax(0.0001f, windowSpan);
      if (playheadNorm >= 0.0f && playheadNorm <= 1.0f) {
        const float x = bounds.getX() + playheadNorm * bounds.getWidth();
        g.setColour(UITheme::accent.withAlpha(0.95f));
        g.drawLine(x, bounds.getY() + 1.0f, x, bounds.getBottom() - 1.0f, 1.2f);
      }
    }
  }
}

void WaveformBar::drawArrangementOverlay(
    juce::Graphics &g, const juce::Rectangle<float> &mainWaveBounds) {
  rebuildPartOverlayBounds(mainWaveBounds);
  const bool editing = isEditingArrangement();
  const int pendingJumpPartIndex =
      pendingArrangementPartProvider ? pendingArrangementPartProvider() : -1;
  const int activeLoopPartIndex =
      activeArrangementLoopPartProvider ? activeArrangementLoopPartProvider() : -1;

  float pendingPulse = 0.0f;
  if (pendingJumpPartIndex >= 0) {
    if (songBpm > 0.0 && positionProvider != nullptr) {
      const double currentSec = juce::jmax(0.0, positionProvider());
      const double beats = (currentSec * songBpm) / 60.0;
      const double phase = beats - std::floor(beats);
      pendingPulse = static_cast<float>(
          0.5 + 0.5 *
                    std::sin((phase * juce::MathConstants<double>::twoPi) -
                             juce::MathConstants<double>::halfPi));
    } else {
      pendingPulse = 0.35f;
    }
  }

  const auto lane = getArrangementLaneBounds(mainWaveBounds);
  if (lane.getWidth() <= 2.0f || lane.getHeight() <= 2.0f)
    return;

  if (editing && dragAnchorActive) {
    const float anchorX = sourceSecondsToWindowX(lane, dragAnchoredBoundarySec);
    if (anchorX >= lane.getX() && anchorX <= lane.getRight()) {
      g.setColour(UITheme::accent.withAlpha(0.85f));
      g.drawLine(anchorX, lane.getY() + 2.0f, anchorX, lane.getBottom() - 2.0f,
                 2.0f);
    }
  }

  for (const auto &overlay : partOverlayBounds) {
    const bool selected = editing && (overlay.partIndex == selectedPartIndex);
    const bool loopActive = overlay.partIndex == activeLoopPartIndex;
    const bool pendingJump = !loopActive && overlay.partIndex == pendingJumpPartIndex;
    if (!juce::isPositiveAndBelow(overlay.partIndex,
                                  static_cast<int>(arrangement.parts.size())))
      continue;

    const auto &part = arrangement.parts[static_cast<size_t>(overlay.partIndex)];
    auto fill = partColorOrFallback(part, overlay.partIndex);
    fill = fill.withAlpha(editing ? (selected ? 0.28f : 0.18f) : 0.16f);

    g.setColour(fill);
    g.fillRoundedRectangle(overlay.body, 2.5f);

    if (pendingJump) {
      const float darkAlpha = juce::jlimit(
          0.16f, 0.40f, 0.22f + (pendingPulse * (editing ? 0.18f : 0.22f)));
      g.setColour(juce::Colours::black.withAlpha(darkAlpha));
      g.fillRoundedRectangle(overlay.body, 2.5f);
      g.setColour(juce::Colours::white.withAlpha(0.78f));
      g.drawRoundedRectangle(overlay.body, 2.5f, 1.05f);
    }

    if (loopActive) {
      g.setColour(UITheme::accent.withAlpha(0.98f));
      g.drawRoundedRectangle(overlay.body.reduced(0.6f), 2.5f, 2.2f);

      const float badgeW = 36.0f;
      const float badgeH = 13.0f;
      if (overlay.body.getWidth() >= (badgeW + 16.0f) &&
          overlay.body.getHeight() >= (badgeH + 8.0f)) {
        const juce::Rectangle<float> badge(overlay.body.getRight() - badgeW - 4.0f,
                                           overlay.body.getY() + 4.0f, badgeW,
                                           badgeH);
        g.setColour(UITheme::accent);
        g.fillRoundedRectangle(badge, badgeH * 0.5f);
        g.setColour(UITheme::onAccent);
        g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
        g.drawFittedText("LOOP", badge.toNearestInt(),
                         juce::Justification::centred, 1);
      }
    }

    const auto partName =
        part.name.isNotEmpty() ? part.name : L10n::tr("Parte", "Part");
    auto textArea = overlay.body.toNearestInt().reduced(6, 4);
    g.setColour(juce::Colours::white.withAlpha(
        pendingJump ? 0.96f : (selected ? 0.94f : 0.86f)));
    g.setFont(juce::FontOptions(11.2f).withStyle("Bold"));
    g.drawFittedText(partName, textArea.removeFromTop(13),
                     juce::Justification::topLeft, 1);

    if (editing) {
      g.setFont(juce::FontOptions(10.2f));
      g.drawFittedText(L10n::tr("Inicio ", "Start ") + formatSeconds(part.startSec),
                       textArea.removeFromTop(12), juce::Justification::topLeft, 1);
      g.drawFittedText(L10n::tr("Fin ", "End ") + formatSeconds(part.endSec),
                       textArea.removeFromTop(12), juce::Justification::topLeft, 1);
    }

    if (editing && overlay.showsDeleteButton) {
      g.setColour(juce::Colours::white.withAlpha(0.16f));
      g.fillRoundedRectangle(overlay.deleteButton, 2.0f);
      g.setColour(juce::Colours::white.withAlpha(0.92f));
      const auto db = overlay.deleteButton.reduced(3.2f, 3.2f);
      g.drawLine(db.getX(), db.getY(), db.getRight(), db.getBottom(), 1.2f);
      g.drawLine(db.getRight(), db.getY(), db.getX(), db.getBottom(), 1.2f);
    }
  }
}

void WaveformBar::drawMiniMap(juce::Graphics &g,
                              const juce::Rectangle<float> &miniMapBounds) const {
  if (miniMapBounds.getWidth() <= 2.0f || miniMapBounds.getHeight() <= 2.0f)
    return;

  g.setColour(UITheme::panelDark.withAlpha(0.45f));
  g.fillRoundedRectangle(miniMapBounds, 4.0f);

  drawWaveform(g, miniMapBounds, 0.0f, 1.0f, false, false);

  const double songLength = getSongLengthSeconds();
  if (songLength > 0.0) {
    for (int i = 0; i < static_cast<int>(arrangement.parts.size()); ++i) {
      const auto &part = arrangement.parts[static_cast<size_t>(i)];
      const float x = miniMapBounds.getX() +
                      static_cast<float>(
                          juce::jlimit(0.0, 1.0, part.startSec / songLength)) *
                          miniMapBounds.getWidth();
      g.setColour(partColorOrFallback(part, i).withAlpha(0.7f));
      g.drawLine(x, miniMapBounds.getY(), x, miniMapBounds.getBottom(), 0.7f);
    }
  }

  const auto viewport = getViewportBoundsInMiniMap(miniMapBounds);

  g.setColour(UITheme::panelDark.withAlpha(0.6f));
  g.fillRect(miniMapBounds.withRight(viewport.getX()));
  g.fillRect(miniMapBounds.withLeft(viewport.getRight()));

  g.setColour(UITheme::accent.withAlpha(0.2f));
  g.fillRect(viewport);
  g.setColour(UITheme::accent.withAlpha(0.9f));
  g.drawRect(viewport, 1.5f);

  const float handleWidth = juce::jmin(8.0f, juce::jmax(4.0f, viewport.getWidth() * 0.2f));
  g.setColour(UITheme::accentSoft.withAlpha(0.95f));
  g.fillRect(juce::Rectangle<float>(viewport.getX(), viewport.getY(), handleWidth,
                                    viewport.getHeight()));
  g.fillRect(juce::Rectangle<float>(viewport.getRight() - handleWidth,
                                    viewport.getY(), handleWidth,
                                    viewport.getHeight()));

  if (positionProvider && lengthProvider) {
    const double length = juce::jmax(0.0, lengthProvider());
    if (length > 0.0) {
      const double current = juce::jmax(0.0, positionProvider());
      const float progress =
          static_cast<float>(juce::jlimit(0.0, 1.0, current / length));
      const float x = miniMapBounds.getX() + progress * miniMapBounds.getWidth();
      g.setColour(UITheme::accent);
      g.drawLine(x, miniMapBounds.getY(), x, miniMapBounds.getBottom(), 1.2f);
    }
  }

  g.setColour(UITheme::cardBorder.withAlpha(0.85f));
  g.drawRoundedRectangle(miniMapBounds, 4.0f, 1.0f);
}

void WaveformBar::rebuildSectionAddButtons() {
  sectionAddButtons.clear();
  sectionAddButtons.reserve(kSongSectionSuggestions.size());

  for (const auto *section : kSongSectionSuggestions) {
    auto btn = std::make_unique<juce::TextButton>();
    const juce::String sectionName = section != nullptr
                                         ? juce::String(section)
                                         : L10n::tr("Parte", "Part");
    btn->setButtonText("+ " + sectionName);
    btn->setTooltip(L10n::tr("Agregar ", "Add ") + sectionName);
    btn->setViewportIgnoreDragFlag(true);
    btn->onClick = [this, sectionName] { addArrangementPart(sectionName); };
    addAndMakeVisible(*btn);
    btn->setVisible(isEditingArrangement());
    sectionAddButtons.push_back(std::move(btn));
  }
}

void WaveformBar::openPartActionMenu(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  auto safe = juce::Component::SafePointer<WaveformBar>(this);
  juce::PopupMenu menu;
  menu.addItem(L10n::tr("Duplicar parte", "Duplicate part"), [safe, partIndex] {
    if (safe != nullptr)
      safe->duplicateArrangementPart(partIndex);
  });
  menu.addItem(L10n::tr("Renombrar parte", "Rename part"), [safe, partIndex] {
    if (safe != nullptr)
      safe->renameArrangementPart(partIndex);
  });
  menu.addItem(L10n::tr("Editar rango", "Edit range"), [safe, partIndex] {
    if (safe != nullptr)
      safe->editArrangementPartRange(partIndex);
  });
  menu.addItem(L10n::tr("Eliminar parte", "Delete part"),
               arrangement.parts.size() > 1, false,
               [safe, partIndex] {
                 if (safe != nullptr)
                   safe->confirmDeleteArrangementPart(partIndex);
               });
  menu.addSeparator();
  menu.addItem(L10n::tr("Resetear arreglo", "Reset arrangement"), [safe] {
    if (safe != nullptr)
      safe->resetArrangementToDefault();
  });
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
}

void WaveformBar::addArrangementPart(const juce::String &preferredName) {
  const double songLength = getSongLengthSeconds();
  if (songLength <= 0.0)
    return;

  juce::String nextName = preferredName.trim();
  if (nextName.isEmpty()) {
    const int suggestionIndex =
        static_cast<int>(arrangement.parts.size() % kSongSectionSuggestions.size());
    nextName = kSongSectionSuggestions[static_cast<size_t>(suggestionIndex)];
  }
  if (nextName.isEmpty())
    nextName = L10n::tr("Parte", "Part");

  double startSec = 0.0;
  if (juce::isPositiveAndBelow(selectedPartIndex,
                               static_cast<int>(arrangement.parts.size()))) {
    startSec = arrangement.parts[static_cast<size_t>(selectedPartIndex)].endSec;
  } else if (!arrangement.parts.empty()) {
    startSec = arrangement.parts.back().endSec;
  }

  startSec = juce::jlimit(0.0, songLength, snapSeconds(startSec));
  double endSec = juce::jmin(songLength, startSec + juce::jmax(8.0, kMinPartDurationSec));
  endSec = snapSeconds(endSec);
  endSec = juce::jlimit(0.0, songLength, endSec);
  if ((endSec - startSec) < kMinPartDurationSec) {
    endSec = juce::jmin(songLength, startSec + kMinPartDurationSec);
    if ((endSec - startSec) < kMinPartDurationSec)
      startSec = juce::jmax(0.0, endSec - kMinPartDurationSec);
  }

  SongArrangementPartSnapshot part;
  part.id = juce::Uuid().toString();
  part.name = nextName;
  part.color = makeRandomArrangementPartColor();
  part.startSec = startSec;
  part.endSec = endSec;

  arrangement.parts.push_back(std::move(part));
  selectedPartIndex = static_cast<int>(arrangement.parts.size()) - 1;
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::duplicateArrangementPart(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  auto copy = arrangement.parts[static_cast<size_t>(partIndex)];
  copy.id = juce::Uuid().toString();
  copy.color = makeRandomArrangementPartColor();
  copy.name = copy.name.trim().isEmpty() ? L10n::tr("Parte", "Part")
                                         : (copy.name + L10n::tr(" Copia", " Copy"));
  arrangement.parts.push_back(std::move(copy));
  selectedPartIndex = static_cast<int>(arrangement.parts.size()) - 1;
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::renameArrangementPart(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  auto *dialog = new juce::AlertWindow(L10n::tr("Renombrar parte", "Rename part"),
                                       L10n::tr("Nuevo nombre", "New name"),
                                       juce::AlertWindow::NoIcon);
  dialog->addTextEditor("name",
                        arrangement.parts[static_cast<size_t>(partIndex)].name,
                        L10n::tr("Nombre", "Name"));
  dialog->addButton(L10n::tr("Cancelar", "Cancel"), 0);
  dialog->addButton(L10n::tr("Guardar", "Save"), 1);

  auto safe = juce::Component::SafePointer<WaveformBar>(this);
  dialog->enterModalState(
      true,
      juce::ModalCallbackFunction::create([safe, dialog, partIndex](int result) {
        if (result != 1 || safe == nullptr)
          return;
        if (!juce::isPositiveAndBelow(
                partIndex, static_cast<int>(safe->arrangement.parts.size())))
          return;
        const auto nextName = dialog->getTextEditorContents("name").trim();
        safe->arrangement.parts[static_cast<size_t>(partIndex)].name =
            nextName.isEmpty() ? L10n::tr("Parte", "Part") : nextName;
        safe->selectedPartIndex = partIndex;
        safe->customArrangement = true;
        safe->notifyArrangementChanged(true);
        safe->repaint();
      }),
      true);
}

void WaveformBar::editArrangementPartRange(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  const auto &part = arrangement.parts[static_cast<size_t>(partIndex)];
  auto *dialog = new juce::AlertWindow(L10n::tr("Editar rango", "Edit range"),
                                       L10n::tr("Ajusta inicio y fin",
                                                "Adjust start and end"),
                                       juce::AlertWindow::NoIcon);
  dialog->addTextEditor("start", juce::String(part.startSec, 2),
                        L10n::tr("Inicio (seg)", "Start (sec)"));
  dialog->addTextEditor("end", juce::String(part.endSec, 2),
                        L10n::tr("Fin (seg)", "End (sec)"));
  dialog->addButton(L10n::tr("Cancelar", "Cancel"), 0);
  dialog->addButton(L10n::tr("Guardar", "Save"), 1);

  auto safe = juce::Component::SafePointer<WaveformBar>(this);
  dialog->enterModalState(
      true,
      juce::ModalCallbackFunction::create([safe, dialog, partIndex](int result) {
        if (result != 1 || safe == nullptr)
          return;
        if (!juce::isPositiveAndBelow(
                partIndex, static_cast<int>(safe->arrangement.parts.size())))
          return;

        const double songLength = safe->getSongLengthSeconds();
        double start =
            safe->snapSeconds(dialog->getTextEditorContents("start").getDoubleValue());
        double end =
            safe->snapSeconds(dialog->getTextEditorContents("end").getDoubleValue());

        start = juce::jlimit(0.0, songLength, start);
        end = juce::jlimit(0.0, songLength, end);
        if (end < start)
          std::swap(start, end);

        if ((end - start) < kMinPartDurationSec) {
          end = juce::jmin(songLength, start + kMinPartDurationSec);
          if ((end - start) < kMinPartDurationSec)
            start = juce::jmax(0.0, end - kMinPartDurationSec);
        }

        auto &editedPart = safe->arrangement.parts[static_cast<size_t>(partIndex)];
        editedPart.startSec = start;
        editedPart.endSec = end;
        safe->selectedPartIndex = partIndex;
        safe->customArrangement = true;
        safe->notifyArrangementChanged(true);
        safe->repaint();
      }),
      true);
}

void WaveformBar::confirmDeleteArrangementPart(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  if (arrangement.parts.size() <= 1) {
    resetArrangementToDefault();
    return;
  }

  const auto partName = arrangement.parts[static_cast<size_t>(partIndex)].name;
  auto *dialog = new juce::AlertWindow(L10n::tr("Eliminar parte", "Delete part"),
                                       L10n::tr("Se eliminara \"", "This will delete \"") +
                                           partName +
                                           L10n::tr("\". Confirmas?", "\". Confirm?"),
                                       juce::AlertWindow::WarningIcon);
  dialog->addButton(L10n::tr("Cancelar", "Cancel"), 0);
  dialog->addButton(L10n::tr("Eliminar", "Delete"), 1);

  auto safe = juce::Component::SafePointer<WaveformBar>(this);
  dialog->enterModalState(
      true,
      juce::ModalCallbackFunction::create([safe, partIndex](int result) {
        if (result != 1 || safe == nullptr)
          return;
        safe->deleteArrangementPart(partIndex);
      }),
      true);
}

void WaveformBar::deleteArrangementPart(const int partIndex) {
  if (!juce::isPositiveAndBelow(partIndex,
                                static_cast<int>(arrangement.parts.size())))
    return;

  if (arrangement.parts.size() <= 1) {
    resetArrangementToDefault();
    return;
  }

  arrangement.parts.erase(arrangement.parts.begin() + partIndex);
  selectedPartIndex =
      juce::jlimit(0, static_cast<int>(arrangement.parts.size()) - 1, partIndex);
  customArrangement = true;
  notifyArrangementChanged(true);
  repaint();
}

void WaveformBar::notifyArrangementChanged(const bool isCustom) {
  normalizeArrangement(true);
  if (onArrangementChanged)
    onArrangementChanged(arrangement, isCustom);
}

void WaveformBar::resetArrangementToDefault() {
  arrangement.parts.clear();

  customArrangement = false;
  selectedPartIndex = -1;
  setVisibleWindow(0.0f, 1.0f);

  if (onArrangementReset)
    onArrangementReset();
  notifyArrangementChanged(false);
  repaint();
}
