#include "ui/panels/WaveformBar.h"
#include "PluginProcessor.h"
#include "data/ApplicationDataPaths.h"
#include "data/AssetBootstrapManager.h"
#include "ui/components/ChannelStripCard.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void expect(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void expectNear(const double actual, const double expected,
                const std::string &message) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > 0.0001)
    throw std::runtime_error(message + " (actual " + std::to_string(actual) +
                             ", expected " + std::to_string(expected) + ")");
}

juce::MouseEvent pointerEvent(WaveformBar &waveform,
                              const juce::Point<float> position,
                              const juce::Point<float> start,
                              const bool dragged = false,
                              const int clicks = 1) {
  const juce::Time now(1000000);
  // Alt selects the existing free-drag mode so exact source-range assertions
  // do not also depend on the current song's musical snap setting.
  return {juce::Desktop::getInstance().getMainMouseSource(), position,
          juce::ModifierKeys::leftButtonModifier |
              juce::ModifierKeys::altModifier,
          1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &waveform, &waveform,
          now, start, now, clicks, dragged};
}

SongArrangementPartSnapshot original(const char *id, const double start,
                                     const double end) {
  SongArrangementPartSnapshot part;
  part.id = id;
  part.name = id;
  part.startSec = start;
  part.endSec = end;
  return part;
}
} // namespace

// Access hit geometry and published presentation state. Copy, movement,
// resizing, selection and history all use production input handlers.
struct WaveformEditingTestAccess {
  static void expectBackgroundPoint(WaveformBar &waveform, juce::Point<float> point) {
    waveform.rebuildPartOverlayBounds(waveBody(waveform));
    const auto hit = waveform.hitTestArrangementPart(point);
    expect(!hit.jumpButton && !hit.loopButton && !hit.muteButton && !hit.overflowButton,
           "gesture fixture starts on waveform background, outside every action");
  }
  static juce::Rectangle<float> arrangementLane(const WaveformBar &waveform) {
    return waveform.getArrangementLaneBounds(waveBody(waveform));
  }
  static juce::Rectangle<float> miniMap(const WaveformBar &waveform) {
    return waveform.getMiniMapBounds(waveform.getLocalBounds().toFloat());
  }
  static void expectSharedOverflow(WaveformBar &waveform) {
    waveform.rebuildPartOverlayBounds(waveBody(waveform));
    const auto shared = waveform.sharedSectionOverflowBounds;
    expect(shared.getWidth() >= 44.0f && shared.getHeight() >= 44.0f,
           "sub-52-pixel regions have a shared full-size action menu");
    const auto hit = waveform.hitTestArrangementPart(shared.getCentre());
    expect(hit.overflowButton && hit.partIndex == -2,
           "shared ellipsis hit routes to the section chooser");
    for (const auto &part : waveform.partOverlayBounds)
      for (const auto target : {part.jumpButton, part.loopButton, part.muteButton,
                                part.overflowButton})
        expect(target.isEmpty() || !shared.intersects(target),
               "shared target never overlaps individual section actions");
  }
  static void expectPersistentControls(WaveformBar &waveform, const juce::String &id) {
    const auto initial = overlay(waveform, id);
    waveform.hoveredActionPartIndex = waveform.findPartIndexById(id);
    waveform.hoveredLoopPartIndex = waveform.hoveredActionPartIndex;
    waveform.playbackControlFocusPartIndex = waveform.hoveredActionPartIndex;
    waveform.playbackControlFocus = WaveformBar::PlaybackControlFocus::Loop;
    waveform.playbackControlFocusChangedAtMs = juce::Time::getMillisecondCounterHiRes();
    waveform.setSingleSelectedPart(waveform.hoveredActionPartIndex);
    const auto focused = overlay(waveform, id);
    waveform.hoveredActionPartIndex = waveform.hoveredLoopPartIndex = -1;
    waveform.playbackControlFocusPartIndex = -1;
    waveform.playbackControlFocus = WaveformBar::PlaybackControlFocus::None;
    const auto unfocused = overlay(waveform, id);
    for (const auto &state : {focused, unfocused})
      expect(initial.jumpButton == state.jumpButton && initial.loopButton == state.loopButton &&
                 initial.muteButton == state.muteButton && initial.overflowButton == state.overflowButton &&
                 initial.showsJumpButton == state.showsJumpButton &&
                 initial.showsLoopButton == state.showsLoopButton &&
                 initial.showsMuteButton == state.showsMuteButton,
             "selection, hover and playback focus never move or hide playback actions");
  }

  static void addCue(WaveformBar &waveform, const char *name, double seconds,
                     int countEnd = 0) {
    waveform.sectionInsertionAnchorValid = true;
    waveform.sectionInsertionAnchorSec = seconds;
    waveform.snapMode = WaveformBar::SnapMode::Free;
    waveform.addGuideCue(name, countEnd, countEnd > 0 ? 0.5 : 0.0);
  }
  static SongArrangementCueSnapshot cue(const WaveformBar &waveform, const juce::String &id) {
    const int index = waveform.findCueIndexById(id);
    expect(index >= 0,"cue exists");
    return waveform.arrangement.cues[static_cast<size_t>(index)];
  }
  static juce::Point<float> cuePoint(WaveformBar &waveform, const juce::String &id) {
    waveform.rebuildCueMarkers();
    for (const auto &marker : waveform.cueMarkerBounds)
      if (marker.id == id)
        return {marker.pointX, marker.y};
    throw std::runtime_error("missing visible cue point");
  }
  static double nextPulse(const WaveformBar &waveform, double seconds) {
    return waveform.adjacentCuePulse(seconds,true);
  }
  static size_t markerCount(WaveformBar &waveform) {
    waveform.rebuildCueMarkers(); return waveform.cueMarkerBounds.size();
  }
  static void expectCueLabels(WaveformBar &waveform) {
    waveform.rebuildCueMarkers();
    const auto body = waveBody(waveform);
    for (size_t i = 0; i < waveform.cueMarkerBounds.size(); ++i) {
      const auto &marker = waveform.cueMarkerBounds[i];
      if (waveform.isEditingArrangement())
        expect(body.contains(marker.label), "cue label stays inside the waveform");
      else {
        expect(marker.label.isEmpty(), "viewing mode hides secondary labels");
        expect(marker.hit == marker.pointHit, "viewing mode only captures the point");
      }
      expect(body.contains(juce::Point<float>{marker.pointX, marker.y}), "cue point is on the waveform");
      expect(!ruler(waveform).intersects(marker.label), "cue label leaves ruler clear");
      expect(!waveform.isEditingArrangement() ||
                 waveform.cueMarkerButtons[i]->getTooltip().contains("eliminar") ||
                 waveform.cueMarkerButtons[i]->getTooltip().contains("delete"),
             "point tooltip explains double-click deletion");
      for (size_t j = 0; j < i; ++j)
        expect(!marker.label.intersects(waveform.cueMarkerBounds[j].label),
               "colliding labels stack without overlap");
    }
  }
  static void doubleClickCue(WaveformBar &waveform, const juce::String &id) {
    const auto point = cuePoint(waveform, id);
    for (size_t i = 0; i < waveform.cueMarkerBounds.size(); ++i) {
      if (waveform.cueMarkerBounds[i].id != id) continue;
      auto &button = *waveform.cueMarkerButtons[i];
      button.mouseDoubleClick(pointerEvent(waveform, point, point, false, 2)
                                  .getEventRelativeTo(&button));
      return;
    }
  }
  static void showCueTooltip(WaveformBar &waveform, const juce::String &id) {
    waveform.cueTooltipWindow->displayTip(
        waveform.localPointToGlobal(cuePoint(waveform, id).toInt()),
        "Doble clic en el punto para eliminar");
  }
  static juce::Image renderRuler(WaveformBar &waveform) {
    const auto bounds = ruler(waveform);
    juce::Image image(juce::Image::ARGB, juce::roundToInt(bounds.getWidth()),
                      juce::roundToInt(bounds.getHeight()), true);
    juce::Graphics g(image);
    g.addTransform(juce::AffineTransform::translation(-bounds.getX(), -bounds.getY()));
    const auto window = waveform.getVisibleWindow();
    waveform.drawSeekRuler(g, bounds, window.first, window.second);
    return image;
  }
  static void duplicate(WaveformBar &waveform, const juce::String &id) {
    waveform.createDuplicatedSection(waveform.findPartIndexById(id));
  }
  static void split(WaveformBar &waveform, double seconds) {
    waveform.sectionInsertionAnchorValid = true; waveform.sectionInsertionAnchorSec = seconds;
    waveform.snapMode = WaveformBar::SnapMode::Free;
    waveform.addArrangementPart("Verse 2");
  }
  static const SongArrangementSnapshot &snapshot(const WaveformBar &waveform) {
    return waveform.arrangement;
  }

  static juce::String selectedId(const WaveformBar &waveform) {
    expect(juce::isPositiveAndBelow(waveform.selectedPartIndex,
                                   static_cast<int>(waveform.arrangement.parts.size())),
           "an arrangement part is selected");
    return waveform.arrangement.parts[static_cast<size_t>(waveform.selectedPartIndex)].id;
  }

  static juce::Rectangle<float> waveBody(const WaveformBar &waveform) {
    return waveform.getMainWaveBodyBounds(
        waveform.getMainWaveBounds(waveform.getLocalBounds().toFloat()));
  }

  static juce::Rectangle<float> ruler(const WaveformBar &waveform) {
    return waveform.getSeekRulerBounds(
        waveform.getMainWaveBounds(waveform.getLocalBounds().toFloat()));
  }

  static void expectInsertionPointerStarted(const WaveformBar &waveform) {
    expect(waveform.dragMode == WaveformBar::DragMode::seek,
           "dragging the ruler uses the real insertion-pointer seek handler");
    expect(waveform.sectionInsertionAnchorValid,
           "the ruler gesture establishes a section insertion anchor");
  }

  static void publishArrangement(WaveformBar &waveform,
                                 const SongArrangementSnapshot &snapshot) {
    // Publish a partial annotation snapshot through the same callback used
    // after adding a section, without displaying the section picker in CI.
    waveform.setArrangement(snapshot, true);
    waveform.notifyArrangementChanged(true);
  }

  static juce::Point<float> pointAt(const WaveformBar &waveform,
                                   const double seconds,
                                   const float y) {
    return {waveform.sourceSecondsToWindowX(waveBody(waveform), seconds), y};
  }

  static WaveformBar::PartOverlayBounds overlay(WaveformBar &waveform,
                                                const juce::String &id) {
    waveform.rebuildPartOverlayBounds(waveBody(waveform));
    const int index = waveform.findPartIndexById(id);
    for (const auto &bounds : waveform.partOverlayBounds)
      if (bounds.partIndex == index)
        return bounds;
    throw std::runtime_error("missing arrangement hit bounds: " + id.toStdString());
  }

  static juce::Point<float> bodyPoint(WaveformBar &waveform,
                                     const juce::String &id) {
    const auto bounds = overlay(waveform, id).body;
    // The edit toolbar is near the top; the lower body starts a part move.
    waveform.rebuildCueMarkers();
    for (const float ratio : {0.5f, 0.75f, 0.25f}) {
      const juce::Point<float> point(bounds.getX() + bounds.getWidth() * ratio,
                                     bounds.getBottom() - 20.0f);
      if (waveform.hitTestCueMarker(point) < 0) return point;
    }
    throw std::runtime_error("no free section body point");
  }

  static juce::Point<float> rightHandle(WaveformBar &waveform,
                                       const juce::String &id) {
    const auto bounds = overlay(waveform, id);
    return {bounds.rightHandle.getCentreX(), bounds.body.getBottom() - 20.0f};
  }

  static void expectMoveStarted(const WaveformBar &waveform) {
    expect(waveform.dragMode == WaveformBar::DragMode::movePart,
           "mouse-down in the section body starts the real move handler");
  }

  static void expectResizeStarted(const WaveformBar &waveform) {
    expect(waveform.dragMode == WaveformBar::DragMode::resizePartRight,
           "mouse-down on the right handle starts the real resize handler");
  }

  static double mappedSource(const WaveformBar &waveform, const double seconds) {
    double source = -1.0;
    expect(waveform.mapArrangementSecondsToSourceSeconds(seconds, 30.0, source),
           "arrangement position maps to source audio");
    return source;
  }

  static void saveRange(WaveformBar &waveform, const juce::String &id,
                        const double start, const double end) {
    const int index = waveform.findPartIndexById(id);
    expect(index >= 0, "the range editor target exists");
    waveform.snapModeBox.setSelectedId(
        static_cast<int>(WaveformBar::SnapMode::Free), juce::sendNotificationSync);
    // Exercise the same save operation as the dialog without creating a native
    // window: console CI may have no display for AlertWindow to attach to.
    waveform.applyArrangementPartRange(id, start, end);
  }

  static bool automationActionsVisible(const WaveformBar &waveform) {
    return waveform.sectionAutomationLabel.isVisible() &&
           waveform.sectionAutomationDoneButton.isVisible() &&
           waveform.sectionAutomationMoreButton.isVisible();
  }
  static void expectAutomationLayout(const WaveformBar &waveform) {
    const auto lane = waveBody(waveform).toNearestInt();
    const auto safeBounds = waveform.minimalPhoneMode
        ? waveform.phoneSafeAreaInsets.subtractedFrom(waveform.getLocalBounds())
        : waveform.getLocalBounds();
    const auto toolbar = waveform.sectionAutomationToolbarBounds;
    expect(!toolbar.isEmpty() && lane.contains(toolbar) && safeBounds.contains(toolbar),
           "the floating automation toolbar remains in the waveform and safe area");
    expect(toolbar.getY() <= lane.getY() + waveform.phoneSafeAreaInsets.getTop() + 32,
           "the automation toolbar stays at the top edge of the waveform lane");
    expect(waveform.sectionAutomationCancelButton.isVisible() ==
               !waveform.sectionAutomationCompactLayout,
           "Cancel is a direct action only in the desktop toolbar");
    std::vector<juce::Rectangle<int>> actions;
    for (const auto *component : std::initializer_list<const juce::Component *>{
        &waveform.sectionAutomationLabel, &waveform.sectionAutomationDoneButton,
        &waveform.sectionAutomationMoreButton, &waveform.sectionAutomationCancelButton}) {
      if (!component->isVisible()) continue;
      const auto bounds = component->getBounds();
      expect(!bounds.isEmpty() && toolbar.contains(bounds),
             "each visible automation action stays inside its floating toolbar");
      for (const auto &previous : actions)
        expect(!previous.intersects(bounds), "automation labels and actions never overlap");
      actions.push_back(bounds);
    }
  }
  static void expectAutomationTouchTargets(const WaveformBar &waveform) {
    auto lane = waveform.getArrangementLaneBounds(waveBody(waveform)).toNearestInt();
    lane = lane.getIntersection(waveform.phoneSafeAreaInsets
                                   .subtractedFrom(waveform.getLocalBounds())).reduced(4);
    expect(lane.getHeight() >= 44,
           "the compressed phone fixture leaves at least one full touch target in its safe lane");
    expect(automationActionsVisible(waveform),
           "automation actions remain visible when the phone lane can fit 44-pixel targets");
    for (const auto *button : std::initializer_list<const juce::Button *>{
             &waveform.sectionAutomationDoneButton, &waveform.sectionAutomationMoreButton})
      expect(button->getHeight() >= 44 && button->getWidth() >= 44,
             "phone Done and More keep full 44-pixel touch targets as the waveform height shrinks");
  }
  static juce::Rectangle<int> automationToolbar(const WaveformBar &waveform) {
    return waveform.sectionAutomationToolbarBounds;
  }
  static juce::String automationLabel(const WaveformBar &waveform) {
    return waveform.sectionAutomationLabel.getText();
  }
  static bool automationTargetOffscreen(const WaveformBar &waveform) {
    return waveform.sectionAutomationTargetOffscreen;
  }
  static float automationAnchorX(const WaveformBar &waveform) {
    return waveform.sectionAutomationToolbarAnchorX;
  }
  static void expectAutomationAnchor(const WaveformBar &waveform,
                                     const double sectionStart,
                                     const double sectionEnd) {
    const auto projectionLane = waveform.getArrangementLaneBounds(waveBody(waveform));
    auto safeLane = projectionLane.toNearestInt();
    if (waveform.minimalPhoneMode)
      safeLane = safeLane.getIntersection(waveform.phoneSafeAreaInsets
                                             .subtractedFrom(waveform.getLocalBounds()));
    safeLane = safeLane.reduced(4);
    const auto window = waveform.getVisibleWindow();
    const double length = waveform.getSongLengthSeconds();
    const auto project = [&](const double seconds) {
      const auto ratio = juce::jlimit(0.0, 1.0,
          (seconds / length - window.first) / window.second);
      return juce::jlimit(static_cast<float>(safeLane.getX()),
          static_cast<float>(safeLane.getRight()),
          projectionLane.getX() + static_cast<float>(ratio) * projectionLane.getWidth());
    };
    expect(std::abs(waveform.sectionAutomationToolbarAnchorX -
                    (project(sectionStart) + project(sectionEnd)) * 0.5f) < 1.0f,
           "the floating toolbar points to the visible range of its stable section ID");
  }
  static bool automationCompact(const WaveformBar &waveform) {
    return waveform.sectionAutomationCompactLayout;
  }
  static void setVisibleWindow(WaveformBar &waveform, float start, float span) {
    waveform.setVisibleWindow(start, span);
  }
  static std::pair<float, float> visibleWindow(const WaveformBar &waveform) {
    return waveform.getVisibleWindow();
  }
  static void selectWithoutGesture(WaveformBar &waveform, const juce::String &id) {
    waveform.setSingleSelectedPart(waveform.findPartIndexById(id));
  }
  static void tick(WaveformBar &waveform) {
    waveform.timerCallback();
  }
  static void focusAutomationTarget(WaveformBar &waveform) {
    waveform.focusSectionAutomationTarget();
  }
  static bool automationActionsEnabled(const WaveformBar &waveform) {
    return waveform.sectionAutomationDoneButton.isEnabled() &&
           waveform.sectionAutomationMoreButton.isEnabled() &&
           (!waveform.sectionAutomationCancelButton.isVisible() ||
            waveform.sectionAutomationCancelButton.isEnabled());
  }
  static bool automationMoreEnabled(const WaveformBar &waveform) {
    return waveform.sectionAutomationMoreButton.isEnabled();
  }
  static bool hasDeferredSectionJump(const WaveformBar &waveform) {
    return waveform.pendingJumpDoubleClickPartIndex >= 0;
  }
  static void automationDone(WaveformBar &waveform) {
    waveform.sectionAutomationDoneButton.onClick();
  }
  static void automationCancel(WaveformBar &waveform) {
    waveform.sectionAutomationCancelButton.onClick();
  }
  static void automationClear(WaveformBar &waveform) {
    // Use the same action as the More menu without opening a native popup in CI.
    waveform.clearSectionAutomationFromToolbar();
  }
};

namespace {
struct Fixture {
  static constexpr double sourceLength = 30.0;
  double timelineLength = sourceLength;
  int commits = 0;
  SongArrangementSnapshot published;
  WaveformBar waveform;

  explicit Fixture(const bool utility = false) {
    waveform.setTransportProviders([] { return 0.0; },
                                   [this] { return timelineLength; },
                                   [] { return false; });
    waveform.setSongSourceLengthProvider([] { return sourceLength; });
    waveform.setSongRhythm(120.0, "4/4");
    waveform.setWaveformBins(std::vector<float>(512, 0.5f));
    SongArrangementSnapshot initial;
    initial.parts = {original("Intro", 0.0, 10.0),
                     original("A", 10.0, 20.0),
                     original("B", 20.0, 30.0)};
    initial.parts[1].utilityCue = utility;
    waveform.setArrangement(initial, false);
    waveform.setSize(1200, 500);
    waveform.setOnArrangementChanged(
        [this](const SongArrangementSnapshot &snapshot, const bool) {
          published = snapshot;
          ++commits;
          double inserted = 0.0;
          timelineLength = sourceLength;
          for (const auto &part : snapshot.parts) {
            timelineLength = std::max(timelineLength, part.endSec);
            if (part.sourcePartId.isNotEmpty())
              inserted += part.endSec - part.startSec;
          }
          timelineLength = std::max(timelineLength, sourceLength + inserted);
        });
    waveform.triggerEditToggleFromMidi();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  }

  SongArrangementPartSnapshot part(const juce::String &id) const {
    for (const auto &candidate : WaveformEditingTestAccess::snapshot(waveform).parts)
      if (candidate.id == id)
        return candidate;
    throw std::runtime_error("missing section: " + id.toStdString());
  }

  void click(const juce::Point<float> point) {
    waveform.mouseDown(pointerEvent(waveform, point, point));
    waveform.mouseUp(pointerEvent(waveform, point, point));
  }

  juce::String duplicate(const juce::String &id) {
    const auto bounds = WaveformEditingTestAccess::overlay(waveform, id);
    expect(bounds.showsDuplicateButton, "Copy is available in the section toolbar");
    const auto oldCount = WaveformEditingTestAccess::snapshot(waveform).parts.size();
    click(bounds.duplicateButton.getCentre());
    expect(WaveformEditingTestAccess::snapshot(waveform).parts.size() == oldCount + 1,
           "the Copy button adds one section");
    const auto copyId = WaveformEditingTestAccess::selectedId(waveform);
    expect(copyId != id, "the new copy becomes selected");
    return copyId;
  }

  bool command(const char key) {
    return waveform.keyPressed(
        juce::KeyPress(key, juce::ModifierKeys::commandModifier, 0));
  }

  void select(const juce::String &id) {
    click(WaveformEditingTestAccess::bodyPoint(waveform, id));
    expect(WaveformEditingTestAccess::selectedId(waveform) == id,
           "click selects the requested section");
  }

  juce::String pasteCopyOf(const juce::String &id) {
    select(id);
    expect(command('C'), "copy shortcut is handled");
    expect(command('V'), "paste shortcut is handled");
    return WaveformEditingTestAccess::selectedId(waveform);
  }

  void move(const juce::String &id, const double targetStart) {
    const auto section = part(id);
    const auto start = WaveformEditingTestAccess::bodyPoint(waveform, id);
    const auto body = WaveformEditingTestAccess::waveBody(waveform);
    const auto delta = (targetStart - section.startSec) / timelineLength * body.getWidth();
    const auto end = start.translated(static_cast<float>(delta), 0.0f);
    waveform.mouseDown(pointerEvent(waveform, start, start));
    WaveformEditingTestAccess::expectMoveStarted(waveform);
    waveform.mouseDrag(pointerEvent(waveform, end, start, true));
    waveform.mouseUp(pointerEvent(waveform, end, start, true));
  }

  void resizeRight(const juce::String &id, const double targetEnd) {
    const auto section = part(id);
    const auto start = WaveformEditingTestAccess::rightHandle(waveform, id);
    const auto body = WaveformEditingTestAccess::waveBody(waveform);
    const auto delta = (targetEnd - section.endSec) / timelineLength * body.getWidth();
    const auto end = start.translated(static_cast<float>(delta), 0.0f);
    waveform.mouseDown(pointerEvent(waveform, start, start));
    WaveformEditingTestAccess::expectResizeStarted(waveform);
    waveform.mouseDrag(pointerEvent(waveform, end, start, true));
    waveform.mouseUp(pointerEvent(waveform, end, start, true));
  }

  void editRange(const juce::String &id, const double start, const double end) {
    WaveformEditingTestAccess::saveRange(waveform, id, start, end);
  }

  void expectPosition(const juce::String &id, const double start,
                      const double end) const {
    const auto section = part(id);
    expectNear(section.startSec, start, id.toStdString() + " arrangement start");
    expectNear(section.endSec, end, id.toStdString() + " arrangement end");
  }

  void expectSource(const juce::String &id, const double start,
                    const double end) const {
    const auto section = part(id);
    expect(section.sourcePartId == "A", "copy retains the original source ID");
    expectNear(section.sourceStartSec, start, "copy source start remains anchored");
    expectNear(section.sourceEndSec, end, "copy source end remains anchored");
    expectNear(WaveformEditingTestAccess::mappedSource(
                   waveform, (section.startSec + section.endSec) * 0.5),
               (start + end) * 0.5, "waveform samples the expected original audio");
  }
};

void writeSyntheticWav(const juce::File &source, const int frameCount) {
  auto stream = std::unique_ptr<juce::OutputStream>(source.createOutputStream());
  juce::WavAudioFormat wav;
  auto writer = wav.createWriterFor(
      stream, juce::AudioFormatWriterOptions{}
                  .withSampleRate(44100.0)
                  .withNumChannels(1)
                  .withBitsPerSample(16));
  expect(writer != nullptr, "the isolated synthetic WAV writer opens");
  juce::AudioBuffer<float> samples(1, 4096);
  samples.clear();
  for (int frame = 0; frame < samples.getNumSamples(); ++frame)
    samples.setSample(0, frame, 0.1f);
  int remaining = frameCount;
  while (remaining > 0) {
    const int frames = std::min(remaining, samples.getNumSamples());
    expect(writer->writeFromAudioSampleBuffer(samples, 0, frames),
           "the isolated source audio is written");
    remaining -= frames;
  }
}

juce::File writeProcessorSongSource() {
  const auto root = ApplicationDataPaths::isolatedTestRoot();
  // Processor startup preloads pads asynchronously. Satisfy its local asset
  // check before constructing it, so this transport test cannot download a
  // pack or depend on network access.
  const auto pads = AssetBootstrapManager::getPadsPackDirectory();
  expect(pads.isAChildOf(root), "processor test pad assets are isolated");
  const auto firstPad = pads.getChildFile("fixture-pad-0.wav");
  if (!firstPad.existsAsFile()) {
    writeSyntheticWav(firstPad, 2205);
    for (int index = 1; index < 20; ++index)
      expect(firstPad.copyFileTo(pads.getChildFile(
                 "fixture-pad-" + juce::String(index) + ".wav")),
             "the temporary local pads pack is pre-seeded");
  }
  const auto source = root.getChildFile(
      "waveform-transport-" + juce::Uuid().toString() + ".wav");
  writeSyntheticWav(source, static_cast<int>(Fixture::sourceLength * 44100.0));
  return source;
}

// Use the public processor loading API so duration comes from decoded audio,
// rather than a mock that could reproduce the same incorrect end calculation.
// The CTest wrapper and isolated storage translation units keep constructor
// migrations, background decode cache and app settings in the temporary root.
struct ProcessorFixture {
  juce::File source = writeProcessorSongSource();
  MTControlsAudioProcessor processor;
  Fixture edit;

  ProcessorFixture() {
    juce::StringArray paths;
    paths.add(source.getFullPathName());
    expect(processor.loadSongStems(paths, juce::Uuid().toString(),
                                   "Waveform duration regression", "C", 120.0),
           "the real processor loads the isolated source song");
    expectNear(processor.getLoadedSongLengthSeconds(), Fixture::sourceLength,
               "decoded source duration is 30 seconds");
    processor.setSongArrangement(
        WaveformEditingTestAccess::snapshot(edit.waveform), false);
    processor.setArrangementEditingActive(true);
    edit.waveform.setTransportProviders(
        [this] { return processor.getSongCurrentPositionSeconds(); },
        [this] { return processor.getSongLengthSeconds(); },
        [this] { return processor.isSongPlaying(); });
    edit.waveform.setSongSourceLengthProvider(
        [this] { return processor.getLoadedSongLengthSeconds(); });
    edit.waveform.setSongEndCustomizedProvider(
        [this] { return processor.isSongEndCustomized(); });
    edit.waveform.setSongWindowProviders(
        [this] { return processor.getSongStartSeconds(); },
        [this] { return processor.getSongEndSeconds(); });
    edit.waveform.setSeekCallback(
        [this](const double seconds) { processor.seekSongTo(seconds); });
    edit.waveform.setOnArrangementChanged(
        [this](const SongArrangementSnapshot &snapshot, const bool custom) {
          edit.published = snapshot;
          ++edit.commits;
          processor.setSongArrangement(snapshot, custom);
          edit.timelineLength = processor.getSongLengthSeconds();
        });
  }

  void publishPartialAnnotation() {
    SongArrangementSnapshot partial;
    partial.parts = {original("A", 5.0, 10.0)};
    WaveformEditingTestAccess::publishArrangement(edit.waveform, partial);
  }

  void dragInsertionPointer(const double startSeconds,
                            const double endSeconds) {
    const auto ruler = WaveformEditingTestAccess::ruler(edit.waveform);
    expect(ruler.getWidth() > 2.0f && ruler.getHeight() > 2.0f,
           "the edit ruler is available for insertion-pointer input");
    const auto start = WaveformEditingTestAccess::pointAt(
        edit.waveform, startSeconds, ruler.getCentreY());
    const auto end = WaveformEditingTestAccess::pointAt(
        edit.waveform, endSeconds, ruler.getCentreY());
    const int commitsBefore = edit.commits;
    edit.waveform.mouseDown(pointerEvent(edit.waveform, start, start));
    WaveformEditingTestAccess::expectInsertionPointerStarted(edit.waveform);
    edit.waveform.mouseDrag(pointerEvent(edit.waveform, end, start, true));
    edit.waveform.mouseUp(pointerEvent(edit.waveform, end, start, true));
    expect(edit.commits == commitsBefore,
           "moving the insertion pointer does not publish an arrangement edit");
  }
};

void testPartialAnnotationsPreserveRestoredFullSongEnd() {
  ProcessorFixture f;
  // Restoring a saved window calls this even if its end matches full audio.
  f.processor.setSongEndSeconds(30.0);
  expect(f.processor.isSongEndCustomized(), "the restored Fin flag is explicit");
  f.dragInsertionPointer(2.0, 8.0);
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "moving the insertion pointer preserves Fin before adding a section");
  f.publishPartialAnnotation();
  expectNear(f.processor.getSongLengthSeconds(), 30.0,
             "a partial annotation does not shorten decoded song duration");
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "a first partial section must not replace restored full-song Fin");
  f.dragInsertionPointer(17.0, 24.0);
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "moving the insertion pointer after adding a section preserves Fin");
  expectNear(f.processor.getSongCurrentPositionSeconds(), 24.0,
             "the insertion pointer can seek beyond the last annotation");
}

void testPartialAnnotationsPreserveDefaultFullSongEnd() {
  ProcessorFixture f;
  expect(!f.processor.isSongEndCustomized(), "a fresh song has default Fin");
  f.publishPartialAnnotation();
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "partial annotations preserve the default full-song Fin");
  f.dragInsertionPointer(18.0, 26.0);
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "default Fin stays at the source end after ruler seeking");
  expectNear(f.processor.getSongCurrentPositionSeconds(), 26.0,
             "a default song can seek past the final annotation");
}

void testPartialAnnotationsPreserveExplicitWindowTrim() {
  ProcessorFixture f;
  f.processor.setSongEndSeconds(20.0);
  f.publishPartialAnnotation();
  expectNear(f.processor.getSongEndSeconds(), 20.0,
             "publishing partial annotations preserves an explicit earlier Fin");
  f.dragInsertionPointer(12.0, 25.0);
  expectNear(f.processor.getSongEndSeconds(), 20.0,
             "ruler seeking preserves the explicit window trim");
  expectNear(f.processor.getSongCurrentPositionSeconds(), 20.0,
             "the explicit trim still limits the insertion pointer and seek");
}

void testMovingAndTrimmingAnnotationPreservesSongEnd() {
  ProcessorFixture f;
  f.processor.setSongEndSeconds(30.0);
  f.publishPartialAnnotation();
  f.edit.move("A", 1.0);
  f.edit.expectPosition("A", 1.0, 6.0);
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "moving an annotation earlier never moves Fin to its right edge");
  f.edit.resizeRight("A", 4.0);
  f.edit.expectPosition("A", 1.0, 4.0);
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "trimming an annotation never trims full-song Fin");
  expect(f.edit.command('Z'), "annotation resize can be undone");
  f.edit.expectPosition("A", 1.0, 6.0);
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "annotation undo also preserves full-song Fin");
}

void testCopiedInsertionExtendsAndRestoresFullSongEnd() {
  ProcessorFixture f;
  f.processor.setSongEndSeconds(30.0);
  const auto copy = f.edit.duplicate("A");
  expectNear(f.processor.getSongLengthSeconds(), 40.0,
             "a copied ten-second insertion extends the real arrange duration");
  expectNear(f.processor.getSongEndSeconds(), 40.0,
             "a restored full Fin follows the copied insertion extension");
  f.edit.move(copy, 0.0);
  expectNear(f.processor.getSongEndSeconds(), 40.0,
             "relocating an inserted copy keeps the extended Fin");
  f.edit.select(copy);
  expect(f.edit.waveform.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)),
         "Delete removes the selected insertion through the real editor handler");
  expectNear(f.processor.getSongLengthSeconds(), 30.0,
             "removing the copied insertion restores decoded source duration");
  expectNear(f.processor.getSongEndSeconds(), 30.0,
             "removing the insertion returns following Fin to full source end");
}

void testMoveLeftKeepsSourceAndHistory() {
  Fixture f;
  const auto copy = f.duplicate("A");
  f.expectPosition(copy, 20.0, 30.0);
  f.expectSource(copy, 10.0, 20.0);
  const auto before = f.commits;
  f.move(copy, 0.0);
  expect(f.commits == before + 1, "a completed move publishes exactly once");
  f.expectPosition(copy, 0.0, 10.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("Intro", 10.0, 20.0);
  f.expectPosition("A", 20.0, 30.0);
  f.expectPosition("B", 30.0, 40.0);
  expectNear(f.timelineLength, 40.0, "moving a copy does not change song duration");

  expect(f.command('Z'), "move undo is available");
  f.expectPosition(copy, 20.0, 30.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("A", 10.0, 20.0);
  expect(f.command('Y'), "move redo is available");
  f.expectPosition(copy, 0.0, 10.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("A", 20.0, 30.0);
}

void testReturningDragDoesNotAccumulateRipple() {
  Fixture f;
  const auto copy = f.duplicate("A");
  const auto start = WaveformEditingTestAccess::bodyPoint(f.waveform, copy);
  const auto left = WaveformEditingTestAccess::pointAt(f.waveform, 5.0, start.y);
  const auto middle = WaveformEditingTestAccess::pointAt(f.waveform, 15.0, start.y);
  const int before = f.commits;
  f.waveform.mouseDown(pointerEvent(f.waveform, start, start));
  WaveformEditingTestAccess::expectMoveStarted(f.waveform);
  for (const auto point : {left, middle, left, start}) {
    f.waveform.mouseDrag(pointerEvent(f.waveform, point, start, true));
    f.expectSource(copy, 10.0, 20.0);
    const auto section = f.part(copy);
    expectNear(section.endSec - section.startSec, 10.0,
               "every drag preview preserves the full copy duration");
    expect(f.commits == before, "drag previews do not publish intermediate arrangements");
  }
  f.waveform.mouseUp(pointerEvent(f.waveform, start, start, true));
  f.expectPosition(copy, 20.0, 30.0);
  f.expectPosition("Intro", 0.0, 10.0);
  f.expectPosition("A", 10.0, 20.0);
  f.expectPosition("B", 30.0, 40.0);
  f.expectSource(copy, 10.0, 20.0);
}

void testCopyAgainAfterMoving() {
  for (const bool useClipboard : {false, true}) {
    Fixture f;
    const auto copy = f.duplicate("A");
    f.move(copy, 0.0);
    const auto nested = useClipboard ? f.pasteCopyOf(copy) : f.duplicate(copy);
    f.expectPosition(nested, 10.0, 20.0);
    f.expectSource(nested, 10.0, 20.0);
    f.expectSource(copy, 10.0, 20.0);
    f.expectPosition("A", 30.0, 40.0);
    expectNear(f.timelineLength, 50.0, "copying after relocation inserts one duration");
  }
}

void testTrimAndExpandRestoresGap() {
  Fixture f;
  const auto copy = f.duplicate("A");
  f.move(copy, 0.0);
  f.resizeRight(copy, 5.0);
  f.expectPosition(copy, 0.0, 5.0);
  f.expectSource(copy, 10.0, 15.0);
  f.expectPosition("Intro", 5.0, 15.0);
  f.expectPosition("A", 15.0, 25.0);
  f.expectPosition("B", 25.0, 35.0);
  expectNear(f.timelineLength, 35.0, "trimming closes the removed insertion gap");
  f.resizeRight(copy, 10.0);
  f.expectPosition(copy, 0.0, 10.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("Intro", 10.0, 20.0);
  f.expectPosition("A", 20.0, 30.0);
  f.expectPosition("B", 30.0, 40.0);
  expectNear(f.timelineLength, 40.0, "expanding reopens the insertion gap without overlap");
  expect(f.command('Z'), "expansion undo is available");
  f.expectPosition(copy, 0.0, 5.0);
  f.expectSource(copy, 10.0, 15.0);
  f.expectPosition("A", 15.0, 25.0);
  expect(f.command('Y'), "expansion redo is available");
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("A", 20.0, 30.0);
}

void testCopyTrimmedSectionAndMove() {
  for (const bool useClipboard : {false, true}) {
    Fixture f;
    const auto copy = f.duplicate("A");
    f.resizeRight(copy, 25.0);
    f.expectSource(copy, 10.0, 15.0);
    f.expectPosition("B", 25.0, 35.0);
    f.move(copy, 0.0);
    f.expectPosition(copy, 0.0, 5.0);
    f.expectSource(copy, 10.0, 15.0);
    const auto nested = useClipboard ? f.pasteCopyOf(copy) : f.duplicate(copy);
    f.expectPosition(nested, 5.0, 10.0);
    f.expectSource(nested, 10.0, 15.0);
    f.expectPosition("A", 20.0, 30.0);
  }
}

void testTailCopyCanRegainItsFullLength() {
  Fixture f;
  const auto copy = f.duplicate("A");
  f.move(copy, 30.0);
  f.expectPosition(copy, 30.0, 40.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("B", 20.0, 30.0);
  f.resizeRight(copy, 35.0);
  f.expectPosition(copy, 30.0, 35.0);
  f.expectSource(copy, 10.0, 15.0);
  expectNear(f.timelineLength, 35.0, "trimming the tail shortens arrange length");
  f.resizeRight(copy, 40.0);
  f.expectPosition(copy, 30.0, 40.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("B", 20.0, 30.0);
  expectNear(f.timelineLength, 40.0,
             "a tail copy expands beyond the shortened arrange length");
}

void testUtilityCopyRetainsGuideTimingIdentity() {
  for (const bool useClipboard : {false, true}) {
    Fixture f(true);
    const auto copy = useClipboard ? f.pasteCopyOf("A") : f.duplicate("A");
    expect(f.part(copy).utilityCue,
           "Copy and keyboard paste preserve utility guide announcements at entry");
  }
}

void testRangeDialogPreservesSourceAndRipple() {
  Fixture f;
  const auto copy = f.duplicate("A");
  f.move(copy, 0.0);
  const int before = f.commits;
  f.editRange(copy, 0.0, 5.0);
  expect(f.commits == before + 1, "saving a range publishes exactly one change");
  f.expectPosition(copy, 0.0, 5.0);
  f.expectSource(copy, 10.0, 15.0);
  f.expectPosition("Intro", 5.0, 15.0);
  f.expectPosition("A", 15.0, 25.0);
  f.expectPosition("B", 25.0, 35.0);
  f.editRange(copy, 0.0, 10.0);
  f.expectPosition(copy, 0.0, 10.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("Intro", 10.0, 20.0);
  f.expectPosition("A", 20.0, 30.0);
  f.expectPosition("B", 30.0, 40.0);
  expect(f.command('Z'), "range dialog edit can be undone");
  f.expectPosition(copy, 0.0, 5.0);
  f.expectSource(copy, 10.0, 15.0);
  f.expectPosition("A", 15.0, 25.0);
  expect(f.command('Y'), "range dialog edit can be redone");
  f.expectPosition(copy, 0.0, 10.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("A", 20.0, 30.0);

  f.editRange(copy, 30.0, 40.0);
  f.expectPosition(copy, 30.0, 40.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("Intro", 0.0, 10.0);
  f.expectPosition("A", 10.0, 20.0);
  f.expectPosition("B", 20.0, 30.0);
  expectNear(f.timelineLength, 40.0,
             "moving both range endpoints preserves insertion duration");
  f.editRange(copy, 30.0, 35.0);
  f.expectPosition(copy, 30.0, 35.0);
  f.expectSource(copy, 10.0, 15.0);
  expectNear(f.timelineLength, 35.0, "range edit trims the copied tail");
  f.editRange(copy, 30.0, 40.0);
  f.expectPosition(copy, 30.0, 40.0);
  f.expectSource(copy, 10.0, 20.0);
  f.expectPosition("B", 20.0, 30.0);
  expectNear(f.timelineLength, 40.0,
             "range edit can re-extend the copied tail beyond the old end");
}
} // namespace

namespace {
void testSectionAutomationResponsiveLayout() {
  Fixture f;
  f.waveform.setSectionAutomationState("A");
  const auto capturePath = juce::SystemStats::getEnvironmentVariable(
      "PW_AUTOMATION_CAPTURE_DIR", {});
  const auto capture = [&capturePath](juce::Component &component, const juce::String &name) {
    if (capturePath.isEmpty()) return;
    expect(juce::File::isAbsolutePath(capturePath), "visual capture path is absolute");
    const juce::File directory(capturePath);
    expect(directory.createDirectory().wasOk(), "visual capture directory opens");
    auto stream = directory.getChildFile(name + ".png").createOutputStream();
    expect(stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk(),
        "visual capture replaces the previous PNG rather than appending to it");
    expect(juce::PNGImageFormat().writeImageToStream(
        component.createComponentSnapshot(component.getLocalBounds(), true, 2.0f), *stream),
        "production component visual capture writes");
  };
  for (const int width : {320, 360, 768, 1200}) {
    const bool phone = width <= 360;
    f.waveform.setMinimalPhoneMode(phone);
    f.waveform.setPhoneSafeAreaInsets(phone ? juce::BorderSize<int>(24, 18, 20, 28)
                                          : juce::BorderSize<int>{});
    f.waveform.setSize(width, 260);
    WaveformEditingTestAccess::expectAutomationLayout(f.waveform);
    expect(WaveformEditingTestAccess::automationCompact(f.waveform) == phone,
           "phones keep the compact actions and wider views expose Cancel");
    if (phone)
      expect(WaveformEditingTestAccess::automationLabel(f.waveform).startsWith("A"),
             "the compact automation label starts with its short A indicator");
    if (phone)
      WaveformEditingTestAccess::expectAutomationTouchTargets(f.waveform);
    capture(f.waveform, "automation-wave-" + juce::String(width));
  }
  f.waveform.setMinimalPhoneMode(true);
  for (const int height : {108, 140}) {
    f.waveform.setSize(360, height);
    for (const bool verticalInsets : {true, false}) {
      f.waveform.setPhoneSafeAreaInsets(verticalInsets
          ? juce::BorderSize<int>(24, 18, 20, 28)
          : juce::BorderSize<int>(0, 18, 0, 28));
      WaveformEditingTestAccess::expectAutomationLayout(f.waveform);
      WaveformEditingTestAccess::expectAutomationTouchTargets(f.waveform);
      WaveformEditingTestAccess::expectAutomationAnchor(f.waveform, 10.0, 20.0);
      capture(f.waveform, "automation-phone-" + juce::String(height) +
                             (verticalInsets ? "-with-insets" : ""));
    }
  }
  // Forty-four safe pixels are the boundary where vertical padding must give
  // way before any action's touch target shrinks.
  f.waveform.setPhoneSafeAreaInsets(juce::BorderSize<int>(24, 18, 20, 28));
  f.waveform.setSize(360, 96);
  WaveformEditingTestAccess::expectAutomationLayout(f.waveform);
  WaveformEditingTestAccess::expectAutomationTouchTargets(f.waveform);
  capture(f.waveform, "automation-phone-minimum-44");
  // A narrow floating section uses the same compact actions in a desktop lane.
  f.waveform.setMinimalPhoneMode(false);
  f.waveform.setSize(400, 260);
  WaveformEditingTestAccess::expectAutomationLayout(f.waveform);
  expect(WaveformEditingTestAccess::automationCompact(f.waveform),
         "a narrow desktop lane keeps the actions within its available width");
  ChannelStripLookAndFeel look;
  ChannelStripCard strip("GUITAR", look, [] { return std::make_pair(0.25f, 0.22f); });
  strip.setSize(100, 310);
  strip.setVolume(0.45f);
  strip.setSectionAutomationState(true, true);
  capture(strip, "automation-strip");
}

void testSectionAutomationToolbarAndHistory() {
  Fixture f;
  juce::String editingId;
  bool saved = false, cancelled = false, cleared = false;
  f.waveform.setSectionAutomationCallbacks(
      [&editingId](const juce::String &id) { editingId = id; return true; },
      [&](bool save) {
        saved = save; cancelled = !save;
        editingId.clear(); f.waveform.setSectionAutomationState({});
      },
      [&](const juce::String &id) { cleared = id == "A"; },
      [&editingId] { return editingId; });
  const auto toolbar = WaveformEditingTestAccess::overlay(f.waveform, "A");
  expect(toolbar.showsAutomateButton, "selected section exposes Automate in its real toolbar");
  f.click(toolbar.automateButton.getCentre());
  expect(editingId == "A", "Automate targets the stable ID of the clicked section");
  expect(WaveformEditingTestAccess::automationActionsVisible(f.waveform),
         "automation exposes its label, Done and More actions");
  const int before = f.commits;
  f.click(WaveformEditingTestAccess::bodyPoint(f.waveform, "B"));
  expect(editingId == "A" && f.commits == before,
         "editing remains pinned and a wave gesture cannot move another section");
  expect(f.command('Z') && f.command('V') &&
         f.waveform.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)),
         "structural keyboard commands are consumed while editing automation");
  expect(editingId == "A" && f.commits == before,
         "keyboard commands preserve the automation draft and arrangement");
  WaveformEditingTestAccess::automationClear(f.waveform);
  expect(cleared, "Clear targets the edited section");
  WaveformEditingTestAccess::automationCancel(f.waveform);
  expect(cancelled && !WaveformEditingTestAccess::automationActionsVisible(f.waveform),
         "Cancel restores the regular toolbar");
  auto snapshot = WaveformEditingTestAccess::snapshot(f.waveform);
  SectionStemAutomation guitar;
  guitar.stemId = "guitar"; guitar.volume = 0.35f;
  snapshot.parts[1].automation.stems.push_back(guitar);
  f.waveform.setCommittedSectionAutomation(snapshot);
  expect(f.part("A").automation.hasOverrides(), "committed automation updates the section");
  expect(f.command('Z'), "automation participates in arrangement undo");
  expect(!f.part("A").automation.hasOverrides(), "Undo restores the previous section automation");
  // Use a fresh edit to verify Done separately from the history navigation.
  const auto nextToolbar = WaveformEditingTestAccess::overlay(f.waveform, "A");
  f.click(nextToolbar.automateButton.getCentre());
  WaveformEditingTestAccess::automationDone(f.waveform);
  expect(saved && editingId.isEmpty(), "Done completes the targeted edit");
}

void testSectionAutomationToolbarTracksStableId() {
  double position = 15.0;
  Fixture f;
  f.waveform.setTransportProviders([&position] { return position; },
                                  [&f] { return f.timelineLength; },
                                  [] { return false; });
  juce::String editingId;
  juce::String clearedId;
  f.waveform.setSectionAutomationCallbacks(
      [&editingId](const juce::String &id) { editingId = id; return true; },
      [](bool) {},
      [&clearedId](const juce::String &id) { clearedId = id; },
      [&editingId] { return editingId; });
  f.click(WaveformEditingTestAccess::overlay(f.waveform, "A").automateButton.getCentre());
  WaveformEditingTestAccess::expectAutomationAnchor(f.waveform, 10.0, 20.0);
  const auto pinnedBounds = WaveformEditingTestAccess::automationToolbar(f.waveform);

  position = 25.0;
  WaveformEditingTestAccess::selectWithoutGesture(f.waveform, "B");
  WaveformEditingTestAccess::tick(f.waveform);
  expect(WaveformEditingTestAccess::selectedId(f.waveform) == "B" && editingId == "A",
         "playback cursor and arrange selection can differ from the pinned draft ID");
  expect(WaveformEditingTestAccess::automationToolbar(f.waveform) == pinnedBounds,
         "changing cursor and selection leaves the floating toolbar on the edited section");

  auto refreshed = WaveformEditingTestAccess::snapshot(f.waveform);
  refreshed.parts[0].endSec = 5.0;
  refreshed.parts[1].name = "Pinned verse";
  refreshed.parts.push_back(original("Lead", 5.0, 10.0));
  f.waveform.setArrangement(refreshed, true);
  WaveformEditingTestAccess::selectWithoutGesture(f.waveform, "B");
  WaveformEditingTestAccess::tick(f.waveform);
  expect(editingId == "A" &&
             WaveformEditingTestAccess::automationLabel(f.waveform).contains("Pinned verse"),
         "a refreshed snapshot resolves the edited name by ID after section indices shift");
  WaveformEditingTestAccess::expectAutomationAnchor(f.waveform, 10.0, 20.0);
  WaveformEditingTestAccess::automationClear(f.waveform);
  expect(clearedId == "A", "the More menu Clear action follows the draft ID after selection changes");
}

void testSectionAutomationToolbarTracksViewport() {
  Fixture f;
  f.waveform.setSectionAutomationState("A");
  WaveformEditingTestAccess::setVisibleWindow(f.waveform, 0.0f, 0.9f);
  WaveformEditingTestAccess::expectAutomationLayout(f.waveform);
  WaveformEditingTestAccess::expectAutomationAnchor(f.waveform, 10.0, 20.0);
  const auto initialWindow = WaveformEditingTestAccess::visibleWindow(f.waveform);
  const auto zoomPoint = WaveformEditingTestAccess::pointAt(
      f.waveform, 8.0, WaveformEditingTestAccess::waveBody(f.waveform).getCentreY());
  f.waveform.mouseMagnify(pointerEvent(f.waveform, zoomPoint, zoomPoint), 0.5f);
  expect(WaveformEditingTestAccess::visibleWindow(f.waveform).second < initialWindow.second,
         "the production magnify handler changes the viewport during automation");
  WaveformEditingTestAccess::expectAutomationLayout(f.waveform);
  WaveformEditingTestAccess::expectAutomationAnchor(f.waveform, 10.0, 20.0);
  const auto magnifiedAnchor = WaveformEditingTestAccess::automationAnchorX(f.waveform);
  juce::MouseWheelDetails wheel;
  wheel.deltaX = -0.06f;
  const auto panPoint = WaveformEditingTestAccess::waveBody(f.waveform).getCentre();
  f.waveform.mouseWheelMove(pointerEvent(f.waveform, panPoint, panPoint), wheel);
  expect(std::abs(WaveformEditingTestAccess::automationAnchorX(f.waveform) - magnifiedAnchor) > 1.0f,
         "the floating anchor moves immediately with the production horizontal scroll handler");
  WaveformEditingTestAccess::expectAutomationLayout(f.waveform);
  WaveformEditingTestAccess::expectAutomationAnchor(f.waveform, 10.0, 20.0);

  for (const float start : {0.75f, 0.0f}) {
    WaveformEditingTestAccess::setVisibleWindow(f.waveform, start, 0.2f);
    expect(WaveformEditingTestAccess::automationTargetOffscreen(f.waveform) &&
               WaveformEditingTestAccess::automationActionsVisible(f.waveform),
           "the floating actions remain visible when the edited section leaves either viewport edge");
    WaveformEditingTestAccess::expectAutomationLayout(f.waveform);
    const auto label = WaveformEditingTestAccess::automationLabel(f.waveform);
    expect(label.contains(start > 0.5f ? juce::String::fromUTF8("←")
                                      : juce::String::fromUTF8("→")),
           "the offscreen label indicates the direction of the pinned section");
    WaveformEditingTestAccess::focusAutomationTarget(f.waveform);
    expect(!WaveformEditingTestAccess::automationTargetOffscreen(f.waveform),
           "the More menu return action brings the pinned section back into view");
    WaveformEditingTestAccess::expectAutomationAnchor(f.waveform, 10.0, 20.0);
  }
}

void testSectionAutomationReadonlyToolbar() {
  Fixture f;
  juce::String editingId = "A";
  bool canEdit = false;
  int finishes = 0, clears = 0;
  f.waveform.setSectionAutomationCallbacks(
      [](const juce::String &) { return false; },
      [&finishes](bool) { ++finishes; },
      [&clears](const juce::String &) { ++clears; },
      [&editingId] { return editingId; },
      [&canEdit] { return canEdit; });
  f.waveform.setSectionAutomationState(editingId);
  WaveformEditingTestAccess::expectAutomationLayout(f.waveform);
  expect(WaveformEditingTestAccess::automationActionsVisible(f.waveform) &&
             !WaveformEditingTestAccess::automationActionsEnabled(f.waveform),
         "a remote draft remains visible with editing actions disabled for its observer");
  WaveformEditingTestAccess::automationDone(f.waveform);
  WaveformEditingTestAccess::automationCancel(f.waveform);
  WaveformEditingTestAccess::automationClear(f.waveform);
  expect(f.waveform.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),
         "Escape is consumed while observing the remote draft");
  expect(finishes == 0 && clears == 0 && editingId == "A",
         "readonly toolbar callbacks and Escape cannot finish or clear another client's draft");

  WaveformEditingTestAccess::setVisibleWindow(f.waveform, 0.75f, 0.2f);
  expect(WaveformEditingTestAccess::automationTargetOffscreen(f.waveform) &&
             WaveformEditingTestAccess::automationMoreEnabled(f.waveform) &&
             !WaveformEditingTestAccess::automationActionsEnabled(f.waveform),
         "an observer can open More to return to an offscreen section while editing actions stay disabled");
  WaveformEditingTestAccess::focusAutomationTarget(f.waveform);
  expect(!WaveformEditingTestAccess::automationTargetOffscreen(f.waveform) &&
             finishes == 0 && clears == 0,
         "returning to a remote section changes only the local viewport");

  canEdit = true;
  WaveformEditingTestAccess::tick(f.waveform);
  expect(WaveformEditingTestAccess::automationActionsEnabled(f.waveform),
         "a same-ID ownership update enables the owner's toolbar without reopening the draft");
  WaveformEditingTestAccess::automationDone(f.waveform);
  expect(finishes == 1, "the owner can finish through the guarded production action");
}

void testPersistentResponsiveSectionControls() {
  Fixture f;
  f.waveform.setMinimalPhoneMode(true);
  f.waveform.setSize(1200, 500);
  WaveformEditingTestAccess::setVisibleWindow(f.waveform, 0.0f, 1.0f);
  const auto lane = WaveformEditingTestAccess::arrangementLane(f.waveform);
  expect(lane.getHeight() >= 94.0f, "responsive fixture can fit compact controls vertically");
  for (const float requestedWidth : {220.0f, 120.0f, 76.0f, 32.0f}) {
    auto snapshot = WaveformEditingTestAccess::snapshot(f.waveform);
    const double duration = requestedWidth / lane.getWidth() * Fixture::sourceLength;
    snapshot.parts[0].startSec = 0.0; snapshot.parts[0].endSec = 5.0;
    snapshot.parts[1].startSec = 5.0; snapshot.parts[1].endSec = 5.0 + duration;
    snapshot.parts[2].startSec = 5.0 + duration; snapshot.parts[2].endSec = Fixture::sourceLength;
    f.waveform.setArrangement(snapshot, false);
    const auto target = WaveformEditingTestAccess::overlay(f.waveform, "A");
    expect(std::abs(target.body.getWidth() - requestedWidth) < 0.01f,
           "controls preserve section width proportional to its musical duration");
    const auto intro = WaveformEditingTestAccess::overlay(f.waveform, "Intro");
    const auto outro = WaveformEditingTestAccess::overlay(f.waveform, "B");
    expect(std::abs(intro.body.getRight() - target.body.getX()) < 0.01f &&
               std::abs(target.body.getRight() - outro.body.getX()) < 0.01f,
           "responsive controls neither widen sections nor shift adjacent boundaries");
    for (const auto rect : {target.jumpButton, target.loopButton, target.muteButton,
                            target.overflowButton})
      if (!rect.isEmpty()) {
        expect(rect.getWidth() >= 44.0f && rect.getHeight() >= 44.0f,
               "all visible playback actions retain at least 44 by 44 logical pixels");
        expect(target.body.contains(rect), "individual actions remain inside their section");
      }
    if (requestedWidth >= 200.0f) {
      expect(target.showsJumpButton && target.showsLoopButton && target.showsMuteButton &&
                 !target.showsOverflowButton,
             "wide sections persistently expose Go, Loop and Mute");
      expect(target.jumpButton.getCentreX() < target.loopButton.getCentreX() &&
                 target.loopButton.getCentreX() < target.muteButton.getCentreX(),
             "full actions retain their spatial order");
      WaveformEditingTestAccess::expectPersistentControls(f.waveform, "A");
    } else if (requestedWidth >= 100.0f) {
      expect(target.showsJumpButton && target.showsLoopButton && !target.showsMuteButton &&
                 target.showsOverflowButton,
             "compact sections expose Go and Loop plus an explicit overflow menu");
      expect(!target.overflowButton.intersects(target.jumpButton) &&
                 !target.overflowButton.intersects(target.loopButton),
             "compact overflow never overlaps the primary actions");
    } else if (requestedWidth >= 52.0f) {
      expect(!target.showsJumpButton && !target.showsLoopButton && !target.showsMuteButton &&
                 target.showsOverflowButton,
             "narrow sections replace crowded actions with one full-size ellipsis");
    } else {
      expect(!target.showsJumpButton && !target.showsLoopButton && !target.showsMuteButton &&
                 !target.showsOverflowButton,
             "very narrow sections do not shrink or overlap action targets");
      WaveformEditingTestAccess::expectSharedOverflow(f.waveform);
    }
  }
  expect(WaveformEditingTestAccess::miniMap(f.waveform).isEmpty(),
         "phone navigation omits the desktop minimap");
#if !JUCE_IOS && !JUCE_ANDROID
  f.waveform.setMinimalPhoneMode(false);
  expect(!WaveformEditingTestAccess::miniMap(f.waveform).isEmpty(),
         "desktop navigation retains its minimap");
#endif
}

void testMobileWaveformGestureArbitration() {
  Fixture f;
  f.waveform.setMinimalPhoneMode(true);
  f.waveform.setSize(390, 260);
  int seeks = 0, loops = 0, jumps = 0;
  f.waveform.setSeekCallback([&](double) { ++seeks; });
  f.waveform.setOnArrangementPartLoopToggleRequest([&](int) { ++loops; });
  f.waveform.setOnArrangementPartJumpRequest([&](int) { ++jumps; });
  WaveformEditingTestAccess::setVisibleWindow(f.waveform, 0.1f, 0.6f);
  auto body = WaveformEditingTestAccess::waveBody(f.waveform);
  const juce::Point<float> start(body.getCentreX(), body.getCentreY());
  WaveformEditingTestAccess::expectBackgroundPoint(f.waveform, start);
  f.waveform.mouseDown(pointerEvent(f.waveform, start, start));
  expect(seeks == 0, "mobile pointer down never seeks");
  f.waveform.mouseUp(pointerEvent(f.waveform, start, start));
  expect(seeks == 1, "stationary mobile release seeks exactly once");

  const auto before = WaveformEditingTestAccess::visibleWindow(f.waveform);
  f.waveform.mouseDown(pointerEvent(f.waveform, start, start));
  const auto moved = start.translated(-40.0f, 0.0f);
  f.waveform.mouseDrag(pointerEvent(f.waveform, moved, start, true));
  expect(seeks == 1, "mobile drag never seeks while moving");
  expect(WaveformEditingTestAccess::visibleWindow(f.waveform).first > before.first,
         "mobile drag pans the visible waveform");
  f.waveform.mouseDrag(pointerEvent(f.waveform, start, start, true));
  f.waveform.mouseUp(pointerEvent(f.waveform, start, start, true));
  expect(seeks == 1, "returning a drag to its start never resurrects a tap");

  f.waveform.mouseDown(pointerEvent(f.waveform, start, start));
  const auto vertical = start.translated(0.0f, -20.0f);
  f.waveform.mouseDrag(pointerEvent(f.waveform, vertical, start, true));
  f.waveform.mouseUp(pointerEvent(f.waveform, vertical, start, true));
  expect(seeks == 1, "vertical motion suppresses a mobile tap");

  f.waveform.mouseDown(pointerEvent(f.waveform, start, start));
  f.waveform.mouseUp(pointerEvent(f.waveform, {start.x, body.getBottom() + 3.0f}, start));
  expect(seeks == 1, "release outside the waveform never seeks");

  f.waveform.mouseDown(pointerEvent(f.waveform, start, start));
  f.waveform.mouseMagnify(pointerEvent(f.waveform, start, start), 0.1f);
  f.waveform.mouseUp(pointerEvent(f.waveform, start, start));
  expect(seeks == 1, "magnification cancels a pending seek");

  f.waveform.setSize(1200, 260);
  WaveformEditingTestAccess::setVisibleWindow(f.waveform, 0.0f, 1.0f);
  const auto overlay = WaveformEditingTestAccess::overlay(f.waveform, "A");
  expect(overlay.showsLoopButton, "mobile section offers its loop target");
  const auto button = overlay.loopButton.getCentre();
  f.waveform.mouseDown(pointerEvent(f.waveform, button, button));
  expect(loops == 0 && seeks == 1, "inline actions are deferred on mobile down");
  f.waveform.mouseUp(pointerEvent(f.waveform, button, button));
  expect(loops == 1 && seeks == 1, "valid inline release invokes only its action");
  f.waveform.mouseDown(pointerEvent(f.waveform, button, button));
  const auto buttonDrag = button.translated(35.0f, 0.0f);
  f.waveform.mouseDrag(pointerEvent(f.waveform, buttonDrag, button, true));
  f.waveform.mouseUp(pointerEvent(f.waveform, buttonDrag, button, true));
  expect(loops == 1 && jumps == 0 && seeks == 1,
         "drag beginning on an inline action invokes neither action nor seek");
}

void testSectionAutomationMobileNavigation() {
  Fixture f;
  f.waveform.setMinimalPhoneMode(true);
  f.waveform.setSize(360, 260);
  WaveformEditingTestAccess::selectWithoutGesture(f.waveform, "A");
  juce::String editingId = "A";
  int seeks = 0, jumps = 0;
  f.waveform.setSeekCallback([&seeks](double) { ++seeks; });
  f.waveform.setOnArrangementPartJumpRequest([&jumps](int) { ++jumps; });
  f.waveform.setSectionAutomationCallbacks(
      [](const juce::String &) { return false; }, [](bool) {},
      [](const juce::String &) {}, [&editingId] { return editingId; });
  f.waveform.setSectionAutomationState(editingId);
  const int before = f.commits;
  const auto otherSection = WaveformEditingTestAccess::bodyPoint(f.waveform, "B");
  f.click(otherSection);
  f.waveform.mouseDoubleClick(pointerEvent(f.waveform, otherSection, otherSection, false, 2));
  WaveformEditingTestAccess::tick(f.waveform);
  expect(!WaveformEditingTestAccess::hasDeferredSectionJump(f.waveform) && jumps == 0,
         "double tapping the phone waveform cannot schedule a section jump during automation");
  expect(WaveformEditingTestAccess::selectedId(f.waveform) == "A" &&
             editingId == "A" && seeks == 0 && f.commits == before,
         "phone taps preserve the pinned draft, selection, playback cursor and arrangement");

  WaveformEditingTestAccess::setVisibleWindow(f.waveform, 0.2f, 0.5f);
  const auto beforePan = WaveformEditingTestAccess::visibleWindow(f.waveform);
  const auto body = WaveformEditingTestAccess::waveBody(f.waveform);
  const juce::Point<float> start(body.getCentreX(), body.getBottom() - 20.0f);
  const auto end = start.translated(-60.0f, 0.0f);
  f.waveform.mouseDown(pointerEvent(f.waveform, start, start));
  f.waveform.mouseDrag(pointerEvent(f.waveform, end, start, true));
  f.waveform.mouseUp(pointerEvent(f.waveform, end, start, true));
  expect(WaveformEditingTestAccess::visibleWindow(f.waveform).first > beforePan.first,
         "dragging the phone waveform still pans its visible window during automation");
  expect(editingId == "A" && seeks == 0 && jumps == 0 && f.commits == before,
         "phone navigation changes the viewport without seeking or modifying the draft");
  WaveformEditingTestAccess::expectAutomationLayout(f.waveform);
  WaveformEditingTestAccess::expectAutomationAnchor(f.waveform, 10.0, 20.0);
}
void testIndependentCueEditing() {
  Fixture f;
  WaveformEditingTestAccess::addCue(f.waveform,"Build",15.0);
  const auto cueId = WaveformEditingTestAccess::snapshot(f.waveform).cues.front().id;
  expect(WaveformEditingTestAccess::snapshot(f.waveform).parts.size() == 3,
         "adding a cue does not split the primary verse");
  expectNear(f.part("A").startSec,10.0,"verse start unchanged");
  expectNear(f.part("A").endSec,20.0,"verse end unchanged");
  expect(WaveformEditingTestAccess::cue(f.waveform,cueId).partId == "A",
         "cue attaches to containing verse");
  int seeks = 0; f.waveform.setSeekCallback([&](double) { ++seeks; });
  const auto start = WaveformEditingTestAccess::cuePoint(f.waveform,cueId);
  const auto end = WaveformEditingTestAccess::pointAt(f.waveform,17.0,start.y);
  const int before = f.commits;
  f.waveform.mouseDown(pointerEvent(f.waveform,start,start));
  f.waveform.mouseDrag(pointerEvent(f.waveform,end,start,true));
  expect(f.commits == before,"cue drag previews do not publish partial positions");
  f.waveform.mouseUp(pointerEvent(f.waveform,end,start,true));
  expect(seeks == 0 && f.commits == before+1,"drag commits only the cue without seeking");
  expectNear(resolveArrangementCueSeconds(WaveformEditingTestAccess::snapshot(f.waveform),
              WaveformEditingTestAccess::cue(f.waveform,cueId)),17.0,"cue moved to requested point");
  expect(f.command('Z'),"cue drag is undoable");
  expectNear(WaveformEditingTestAccess::cue(f.waveform,cueId).offsetSec,5.0,"undo restores cue offset");
  expect(f.command('Y'),"cue drag is redoable");
  expectNear(WaveformEditingTestAccess::cue(f.waveform,cueId).offsetSec,7.0,"redo restores cue offset");
  f.waveform.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
  expect(WaveformEditingTestAccess::snapshot(f.waveform).cues.empty(),"delete removes selected cue");
  expect(WaveformEditingTestAccess::snapshot(f.waveform).parts.size()==3,"cue delete preserves underlying verse");
  expect(f.command('Z'),"cue deletion undo restores mark");
}

void testCueCopyMoveAndTrim() {
  Fixture f;
  WaveformEditingTestAccess::addCue(f.waveform,"All In",15.0);
  const auto originalCue = WaveformEditingTestAccess::snapshot(f.waveform).cues.front().id;
  const auto copyId = f.duplicate("A");
  const auto &afterCopy = WaveformEditingTestAccess::snapshot(f.waveform);
  expect(afterCopy.cues.size()==2,"section copy carries cue once");
  const auto copied = std::find_if(afterCopy.cues.begin(),afterCopy.cues.end(),
                                  [&](const auto &cue) { return cue.partId == copyId; });
  expect(copied != afterCopy.cues.end() && copied->id != originalCue,"copy receives fresh mark ID");
  const auto copiedCue = copied->id;
  expectNear(resolveArrangementCueSeconds(afterCopy,*copied),25.0,"copied cue uses relative musical offset");
  f.move(copyId,5.0);
  expectNear(resolveArrangementCueSeconds(WaveformEditingTestAccess::snapshot(f.waveform),
              WaveformEditingTestAccess::cue(f.waveform,copiedCue)),10.0,"cue travels with copy moved left");
  expectNear(WaveformEditingTestAccess::cue(f.waveform,originalCue).offsetSec,5.0,"source cue stays anchored independently");
  WaveformEditingTestAccess::saveRange(f.waveform,copyId,5.0,9.0);
  const auto &trimmed = WaveformEditingTestAccess::snapshot(f.waveform);
  expect(std::none_of(trimmed.cues.begin(),trimmed.cues.end(),
                     [&](const auto &cue) { return cue.id==copiedCue; }),"trim removes excluded cue");
  expect(f.command('Z'),"trim undo restores mark and audio range");
  expect(WaveformEditingTestAccess::cue(f.waveform,copiedCue).partId==copyId,"undo restores copied cue owner");
  Fixture clipboard;
  WaveformEditingTestAccess::addCue(clipboard.waveform,"Build",13.0);
  const auto pasted = clipboard.pasteCopyOf("A");
  expect(WaveformEditingTestAccess::snapshot(clipboard.waveform).cues.size()==2,"clipboard paste includes cue");
  const auto &pastedSnapshot = WaveformEditingTestAccess::snapshot(clipboard.waveform);
  expect(std::any_of(pastedSnapshot.cues.begin(),pastedSnapshot.cues.end(),
                     [&](const auto &cue) { return cue.partId==pasted; }),"pasted mark binds to fresh section ID");
}

void testCueDoubleClickDeletesOnlyItsPoint() {
  Fixture f;
  WaveformEditingTestAccess::addCue(f.waveform,"Build",15.0);
  WaveformEditingTestAccess::addCue(f.waveform,"All In",15.0);
  WaveformEditingTestAccess::addCue(f.waveform,"Softly",15.0);
  const auto before = WaveformEditingTestAccess::snapshot(f.waveform);
  expect(WaveformEditingTestAccess::markerCount(f.waveform)==3,
         "coincident cues keep three independent points");
  WaveformEditingTestAccess::expectCueLabels(f.waveform);
  int seeks=0, jumps=0;
  f.waveform.setSeekCallback([&](double) { ++seeks; });
  f.waveform.setOnArrangementPartJumpRequest([&](int) { ++jumps; });
  const auto id = before.cues[1].id;
  f.click(WaveformEditingTestAccess::cuePoint(f.waveform,id));
  for (int i=0;i<f.waveform.getNumChildComponents();++i)
    expect(dynamic_cast<juce::CallOutBox *>(f.waveform.getChildComponent(i)) == nullptr,
           "single cue click selects without a popover");
  WaveformEditingTestAccess::doubleClickCue(f.waveform,id);
  const auto after = WaveformEditingTestAccess::snapshot(f.waveform);
  expect(after.cues.size()==2 && after.cues[0].id==before.cues[0].id &&
             after.cues[1].id==before.cues[2].id,
         "double click through the real cue button removes only that stacked cue");
  expect(seeks==0 && jumps==0 && after.parts.size()==before.parts.size(),
         "cue deletion neither seeks nor deletes or jumps the primary section");
  expect(f.command('Z'), "point deletion can be undone");
  expect(WaveformEditingTestAccess::snapshot(f.waveform).cues==before.cues,
         "undo restores the exact point, order and anchor");
  f.waveform.triggerEditToggleFromMidi();
  juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  WaveformEditingTestAccess::doubleClickCue(f.waveform,id);
  expect(WaveformEditingTestAccess::snapshot(f.waveform).cues==before.cues,
         "view mode protects cues from deletion");
}

void testCueOverlayPreservesMusicalRuler() {
  Fixture f;
  const auto before = WaveformEditingTestAccess::renderRuler(f.waveform);
  WaveformEditingTestAccess::addCue(f.waveform,"Build",15.0);
  const auto after = WaveformEditingTestAccess::renderRuler(f.waveform);
  expect(before.getWidth()==after.getWidth() && before.getHeight()==after.getHeight(),
         "adding a cue does not resize the musical ruler");
  for (int y=0;y<before.getHeight();++y)
    for (int x=0;x<before.getWidth();++x)
      expect(before.getPixelAt(x,y)==after.getPixelAt(x,y),
             "adding a cue preserves visible bar numbers, ticks and cursor");
}

void testCueSplitAndResponsiveMarkers() {
  Fixture f;
  WaveformEditingTestAccess::addCue(f.waveform,"1",16.0,4);
  const auto cueId = WaveformEditingTestAccess::snapshot(f.waveform).cues.front().id;
  expect(WaveformEditingTestAccess::snapshot(f.waveform).cues.size()==1,"count sequence uses one mark");
  WaveformEditingTestAccess::split(f.waveform,15.0);
  const auto cue = WaveformEditingTestAccess::cue(f.waveform,cueId);
  expect(cue.partId != "A","splitting a verse reattaches mark to new containing section");
  expectNear(resolveArrangementCueSeconds(WaveformEditingTestAccess::snapshot(f.waveform),cue),16.0,
             "split retains absolute musical moment");
  WaveformEditingTestAccess::addCue(f.waveform,"Softly",16.1);
  expect(WaveformEditingTestAccess::markerCount(f.waveform)==2,"nearby cues retain individual points");
  WaveformEditingTestAccess::expectCueLabels(f.waveform);
  WaveformEditingTestAccess::setVisibleWindow(f.waveform,0.0f,0.3f);
  expect(WaveformEditingTestAccess::markerCount(f.waveform)==0,"offscreen marks do not pin to viewport edge");
  f.waveform.setMinimalPhoneMode(true);
  f.waveform.setSize(390,250);
  WaveformEditingTestAccess::setVisibleWindow(f.waveform,0.4f,0.5f);
  expectNear(WaveformEditingTestAccess::ruler(f.waveform).getHeight(),0.0,
             "phone viewing mode keeps the ruler hidden even with cues");
  expect(WaveformEditingTestAccess::markerCount(f.waveform)==2,"phone keeps overlapping cues individually accessible");
  WaveformEditingTestAccess::expectCueLabels(f.waveform);
  Fixture edge;
  WaveformEditingTestAccess::addCue(edge.waveform,"Build",0.0);
  WaveformEditingTestAccess::addCue(edge.waveform,"All In",3.8);
  edge.waveform.setMinimalPhoneMode(true); edge.waveform.setSize(390,250);
  expect(WaveformEditingTestAccess::markerCount(edge.waveform)==2,
         "edge cues remain independent after clamping their visible points");
  WaveformEditingTestAccess::expectCueLabels(edge.waveform);
}

void testCueSequenceUsesRegionalPulseGrid() {
  Fixture f;
  SongTempoMapSnapshot map;
  SongTempoRegionSnapshot slow; slow.id="slow"; slow.startSec=0; slow.endSec=16;
  slow.bpm=90; slow.timeSignature="4/4";
  auto fast = slow; fast.id="fast"; fast.startSec=16; fast.endSec=30; fast.bpm=120;
  map.regions={slow,fast}; f.waveform.setTempoMap(map);
  WaveformEditingTestAccess::addCue(f.waveform,"1",15.333333333,4);
  const auto &snapshot = WaveformEditingTestAccess::snapshot(f.waveform);
  expect(snapshot.cues.size()==1,"regional count sequence remains one mark");
  const auto &cue=snapshot.cues.front();
  const double first = resolveArrangementCueSeconds(snapshot,cue);
  expectNear(first,15.333333333,"sequence begins on regional pulse");
  expect(cue.countOffsetsSec.size()==4,"one explicit offset per spoken pulse");
  expectNear(cue.countOffsetsSec[1],2.0/3.0,"first interval follows regional90BPM");
  expectNear(cue.countOffsetsSec[2],2.0/3.0+0.5,"next interval follows new120BPM region");
  expectNear(WaveformEditingTestAccess::nextPulse(f.waveform,first),16.0,"move one pulse follows ruler across region");
}

void testCueCapacityRejectsCopyWithoutChangingTimeline() {
  Fixture f;
  auto snapshot = WaveformEditingTestAccess::snapshot(f.waveform);
  for (int i = 0; i < kMaxArrangementCueEvents; ++i) {
    SongArrangementCueSnapshot cue;
    cue.id = "cue-" + juce::String(i); cue.name = "Build";
    cue.partId = "A"; cue.offsetSec = 3.0; cue.positionSec = 13.0;
    snapshot.cues.push_back(cue);
  }
  f.waveform.setArrangement(snapshot,true);
  snapshot = WaveformEditingTestAccess::snapshot(f.waveform);
  const auto unchanged = [&] {
    const auto &now=WaveformEditingTestAccess::snapshot(f.waveform);
    return now.cues == snapshot.cues && now.parts.size() == snapshot.parts.size() &&
      std::equal(now.parts.begin(),now.parts.end(),snapshot.parts.begin(),
        [](const auto &a,const auto &b) {
          return a.id==b.id && a.startSec==b.startSec && a.endSec==b.endSec &&
                 a.sourcePartId==b.sourcePartId && a.sourceStartSec==b.sourceStartSec &&
                 a.sourceEndSec==b.sourceEndSec;
        });
  };
  WaveformEditingTestAccess::duplicate(f.waveform,"A");
  expect(f.commits == 0 && unchanged(),
         "copy exceeding cue capacity leaves all section times and cue IDs unchanged");
  f.select("A"); expect(f.command('C'),"capacity test captures clipboard");
  expect(!f.command('V'),"paste exceeding announcement capacity is rejected");
  expect(f.commits == 0 && unchanged(),
         "rejected paste publishes nothing and does not ripple following sections");
}

void testCueVisualPreview() {
  const auto path = juce::SystemStats::getEnvironmentVariable("PW_CUE_CAPTURE_DIR",{});
  if (path.isEmpty()) return;
  expect(juce::File::isAbsolutePath(path),"cue preview directory is absolute");
  const juce::File directory(path);
  expect(directory.createDirectory().wasOk(),"cue preview directory opens");
  const auto capture = [&directory](juce::Component &component, const char *name) {
    auto stream=directory.getChildFile(juce::String(name)+".png").createOutputStream();
    expect(stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk(),
           "cue preview file opens");
    expect(juce::PNGImageFormat().writeImageToStream(
        component.createComponentSnapshot(component.getLocalBounds(),true,2.0f),*stream),
           "cue preview renders production component");
  };
  Fixture f;
  auto arrangement=WaveformEditingTestAccess::snapshot(f.waveform);
  arrangement.parts[1].name="Verse"; arrangement.parts[2].name="Chorus";
  f.waveform.setArrangement(arrangement,false);
  std::vector<float> bins(512);
  for (size_t i=0;i<bins.size();++i)
    bins[i]=0.15f+0.5f*std::abs(std::sin(i*0.13f))*std::abs(std::cos(i*0.033f));
  f.waveform.setWaveformBins(std::move(bins));
  WaveformEditingTestAccess::addCue(f.waveform,"1",5.0,4);
  WaveformEditingTestAccess::addCue(f.waveform,"Build",14.0);
  const auto selected=WaveformEditingTestAccess::snapshot(f.waveform).cues.back().id;
  WaveformEditingTestAccess::addCue(f.waveform,"All In",25.0);
  WaveformEditingTestAccess::addCue(f.waveform,"Softly",25.15);
  f.waveform.setSize(1200,320);
  capture(f.waveform,"cue-desktop");
  f.click(WaveformEditingTestAccess::cuePoint(f.waveform,selected));
  for (int i=0;i<f.waveform.getNumChildComponents();++i)
    expect(dynamic_cast<juce::CallOutBox *>(f.waveform.getChildComponent(i)) == nullptr,
           "single click never opens a cue popover");
  WaveformEditingTestAccess::showCueTooltip(f.waveform, selected);
  capture(f.waveform,"cue-tooltip");
  f.waveform.setMinimalPhoneMode(true);
  f.waveform.setSize(390,250);
  WaveformEditingTestAccess::expectCueLabels(f.waveform);
  capture(f.waveform,"cue-phone");
}

} // namespace

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr)
    return 77;
#endif
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  int failures = 0;
  const auto run = [&failures](const char *name, auto test) {
    try {
      test();
      std::cout << "PASS: " << name << '\n';
    } catch (const std::exception &error) {
      ++failures;
      std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
    }
  };
  run("cue counts follow regional tempo grid", testCueSequenceUsesRegionalPulseGrid);
  run("cue capacity rejects copy and paste atomically", testCueCapacityRejectsCopyWithoutChangingTimeline);
  run("cue component previews", testCueVisualPreview);
  run("independent cue drag, history and deletion", testIndependentCueEditing);
  run("stacked cue double-click deletion and undo", testCueDoubleClickDeletesOnlyItsPoint);
  run("wave cues preserve rendered musical ruler", testCueOverlayPreservesMusicalRuler);
  run("cue copy, move left and trim", testCueCopyMoveAndTrim);
  run("cue split, count sequence and responsive stacking", testCueSplitAndResponsiveMarkers);
  run("move left preserves source and undo/redo", testMoveLeftKeepsSourceAndHistory);
  run("section automation toolbar, fixed target and undo", testSectionAutomationToolbarAndHistory);
  run("section automation responsive layout", testSectionAutomationResponsiveLayout);
  run("section automation toolbar tracks stable ID", testSectionAutomationToolbarTracksStableId);
  run("section automation toolbar follows zoom and scroll", testSectionAutomationToolbarTracksViewport);
  run("section automation readonly remote toolbar", testSectionAutomationReadonlyToolbar);
  run("persistent responsive section controls", testPersistentResponsiveSectionControls);
  run("mobile waveform gesture arbitration", testMobileWaveformGestureArbitration);
  run("section automation phone navigation preserves draft", testSectionAutomationMobileNavigation);
  run("returning drag preserves duration and ripple", testReturningDragDoesNotAccumulateRipple);
  run("copy again after moving", testCopyAgainAfterMoving);
  run("trim and expand reopen insertion gap", testTrimAndExpandRestoresGap);
  run("copy trimmed section and move", testCopyTrimmedSectionAndMove);
  run("tail copy regains full length", testTailCopyCanRegainItsFullLength);
  run("utility copy preserves guide timing", testUtilityCopyRetainsGuideTimingIdentity);
  run("range dialog preserves source and ripple", testRangeDialogPreservesSourceAndRipple);
  run("partial annotations preserve restored full Fin",
      testPartialAnnotationsPreserveRestoredFullSongEnd);
  run("partial annotations preserve default full Fin",
      testPartialAnnotationsPreserveDefaultFullSongEnd);
  run("partial annotations preserve explicit trimmed Fin",
      testPartialAnnotationsPreserveExplicitWindowTrim);
  run("moving and trimming annotations preserve Fin",
      testMovingAndTrimmingAnnotationPreservesSongEnd);
  run("copied insertion extends and restores following Fin",
      testCopiedInsertionExtendsAndRestoresFullSongEnd);
  return failures == 0 ? 0 : 1;
}
