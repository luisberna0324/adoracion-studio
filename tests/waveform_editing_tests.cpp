#include "ui/panels/WaveformBar.h"
#include "ui/panels/TopBar.h"
#include "ui/LiveLoopBorder.h"
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
  static void clickLiveToggle(TopBar &bar) {
    expect(bar.liveModeButton.isEnabled(), "topbar live toggle is enabled");
    bar.selectTimelineView(!(bar.liveModeState && bar.liveModeState()));
  }
  static void expectTopbarLiveLayout(TopBar &bar) {
    expect(bar.liveModeButton.isVisible() && bar.tabletMoreButton.isVisible(),
           "live toggle and principal More are visible on every layout");
    expect(bar.liveModeButton.getRight() <= bar.tabletMoreButton.getX() &&
               bar.liveModeButton.getY() == bar.tabletMoreButton.getY(),
           "live toggle sits immediately left of the principal More button");
    expect(bar.liveModeButton.getWidth() >= 38 && bar.liveModeButton.getHeight() >= 38,
           "desktop toggle keeps normal control dimensions");
    for (int i = 0; i < bar.getNumChildComponents(); ++i) {
      const auto *child = bar.getChildComponent(i);
      if (!child->isVisible() || child == &bar.liveModeButton) continue;
      expect(!child->getBounds().intersects(bar.liveModeButton.getBounds()),
             "live toggle does not overlap any visible topbar control");
    }
    expect(bar.getLocalBounds().contains(bar.liveModeButton.getBounds()), "live toggle fits the topbar");
    expect(bar.liveModeButton.getTitle().isNotEmpty() && bar.liveModeButton.getTooltip().isNotEmpty(),
           "toggle has an accessible title and contextual tooltip");
  }
  static auto morePanel(TopBar &bar) { return bar.createMorePopoverContent(); }
  static bool directMidi(const TopBar &bar) { return bar.midiLearnButton.isVisible(); }
  static bool directSettings(const TopBar &bar) { return bar.settingsButton.isVisible(); }
  static void midiEnabled(TopBar &bar, bool enabled) { bar.midiLearnButton.setEnabled(enabled); if (bar.refreshMorePopover) bar.refreshMorePopover(); }
  static void moreRefresh(TopBar &bar) { if (bar.refreshMorePopover) bar.refreshMorePopover(); }
  static bool topbarLiveSelected(const TopBar &bar) { return bar.liveModeButton.getToggleState(); }
  static void chooseTimeline(TopBar &bar, bool sections) { bar.selectTimelineView(sections); }
  static auto timelineCaption(const TopBar &bar) { return bar.liveModeButton.getButtonText(); }
  static auto sectionState(const WaveformBar &wave, int index) { return wave.liveSectionState(index); }
  static auto pendingPulse(double now, bool reduced) { return WaveformBar::livePendingPulse(now, reduced); }
  static void refreshSectionActions(WaveformBar &wave) { wave.refreshLiveSectionActions(); }
  static juce::Component *sectionAction(WaveformBar &wave, const juce::String &id, int action) {
    const auto componentId = "section-action:" + id + ":" + juce::String(action);
    for (int i = 0; i < wave.getNumChildComponents(); ++i)
      if (wave.getChildComponent(i)->getComponentID() == componentId) return wave.getChildComponent(i);
    return nullptr;
  }
  static bool waveMoreVisible(const WaveformBar &wave) { return wave.moreButton.isVisible(); }
  static auto liveBounds(WaveformBar &waveform) {
    waveform.rebuildLiveSectionBounds();
    return waveform.partOverlayBounds;
  }
  static float liveProgress(const WaveformBar &waveform, int index) {
    return waveform.liveSectionProgress(index);
  }
  static auto liveText(WaveformBar &wave, const WaveformBar::PartOverlayBounds &item) { return wave.getLiveTextLayout(item); }
  static float liveScroll(const WaveformBar &waveform) { return waveform.liveScrollOffset; }
  static void followAt(WaveformBar &waveform, double now) { waveform.updateLiveFollow(now); }
  static void vblank(WaveformBar &waveform, double now) { waveform.updatePlaybackAnimation(now); }
  static double resumeAt(const WaveformBar &waveform) { return waveform.liveFollowResumeMs; }
  static void setLiveOffset(WaveformBar &waveform, float offset) { waveform.setLiveScrollOffset(offset); }
  static bool moving(const WaveformBar &wave) { return wave.liveScrollCoasting || wave.liveScrollSettling; }
  static float velocity(const WaveformBar &wave) { return wave.liveScrollVelocity; }
  static void motionAt(WaveformBar &wave, double now, bool reduced = false) { wave.updateLiveScrollMotion(now, reduced); }
  static void finishMotion(WaveformBar &wave) { wave.updateLiveScrollMotion(wave.liveMotionTimeMs + 10000.0, false); }
  static void fling(WaveformBar &wave, float offset, float delta, double now, bool reduced = false) {
    wave.stopLiveScrollMotion(); wave.suspendLiveFollow(); wave.setLiveScrollOffset(offset);
    wave.liveDragSampleOffset = offset; wave.liveDragSampleMs = 1000.0;
    wave.setLiveScrollOffset(offset + delta); wave.sampleLiveDrag(1020.0);
    wave.startLiveScrollMotion(now, 1020.0, reduced);
  }
  static void expectNoDesktopPlaybackActions(WaveformBar &waveform) {
    waveform.rebuildPartOverlayBounds(waveBody(waveform));
    expect(waveform.sharedSectionOverflowBounds.isEmpty(), "desktop has no shared section ellipsis");
    for (const auto &part : waveform.partOverlayBounds) {
      expect(!part.showsJumpButton && !part.showsLoopButton && !part.showsMuteButton &&
                 !part.showsOverflowButton, "normal desktop has no inline playback actions");
      for (const auto target : {part.jumpButton, part.loopButton, part.muteButton, part.overflowButton})
        expect(target.isEmpty(), "removed desktop actions have no invisible hit targets");
    }
  }

  static auto liveRow(const WaveformBar &waveform) { return waveform.getLiveRowBounds(); }
  static void scrollLiveToEnd(WaveformBar &waveform) {
    waveform.setLiveScrollOffset(1000000.0f);
    waveform.snapLiveScrollOffset();
  }
  static void settleLiveWheelScroll(WaveformBar &waveform) {
    expect(waveform.liveScrollSnapDueMs > 0.0,
           "smooth wheel navigation schedules its settled card alignment");
    waveform.liveScrollSnapDueMs = juce::Time::getMillisecondCounterHiRes() - 1.0;
    waveform.timerCallback();
    finishMotion(waveform);
  }
  static void enableFollowForLiveTest(WaveformBar &waveform) {
    waveform.followPlaybackEnabled = true;
    waveform.followButton.setToggleState(true, juce::dontSendNotification);
  }
  static bool followEnabled(const WaveformBar &waveform) { return waveform.followPlaybackEnabled; }
  static void expectPlaybackActionsClearGlobalControls(WaveformBar &waveform) {
    waveform.rebuildPartOverlayBounds(waveBody(waveform));
    for (const auto *control : {&waveform.followButton, &waveform.moreButton,
                                &waveform.editButton, &waveform.waveHeightButton}) {
      if (!control->isVisible()) continue;
      const auto occupied = control->getBounds().toFloat();
      expect(!occupied.intersects(waveform.sharedSectionOverflowBounds),
             "shared section menu remains clear of global waveform controls");
      for (const auto &part : waveform.partOverlayBounds)
        for (const auto target : {part.jumpButton, part.loopButton, part.muteButton,
                                  part.overflowButton})
          expect(!occupied.intersects(target),
                 "section playback actions remain clear of global waveform controls");
    }
  }
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

void testSectionActionsClearGlobalControls() {
  Fixture f;
  f.waveform.setMinimalPhoneMode(true);
  for (const bool phone : {true, false}) {
    f.waveform.setMinimalPhoneMode(phone);
    for (const int width : {360, 390, 768, 1200}) {
      f.waveform.setSize(width, 250);
      WaveformEditingTestAccess::expectPlaybackActionsClearGlobalControls(f.waveform);
    }
  }
}

void testLoopBorderPhaseAndCorners() {
  const juce::Rectangle<float> border(3.0f, 3.0f, 170.0f, 244.0f);
  const float period = liveLoopBorder::period(border);
  expectNear(liveLoopBorder::phase(border, 250.0, false), 1.5,
             "Loop border advances six points per second independently of frame count");
  expectNear(liveLoopBorder::phase(border, 333.0, false), 1.998,
             "irregular rendering timestamps retain wall-clock phase");
  expectNear(liveLoopBorder::phase(border, 1000.0 * period / 6.0, false), 0.0,
             "phase wraps smoothly after one dash period");
  expectNear(liveLoopBorder::phase(border, 123456.0, true), 0.0,
             "reduced motion holds the border phase stationary");
  const float length = liveLoopBorder::perimeter(border);
  expectNear(length / period, std::round(length / period), "closed border fits whole periods without a seam gap");
  for (const float corner : {border.getWidth(), border.getWidth() + border.getHeight(),
                             2.0f * border.getWidth() + border.getHeight()}) {
    const float phase = std::fmod(corner - 2.0f, period);
    int ending = 0, starting = 0;
    const auto point = liveLoopBorder::point(border, corner);
    liveLoopBorder::segments(border, phase, [&](juce::Line<float> line) {
      if (line.getEnd().getDistanceFrom(point) < 0.001f) ++ending;
      if (line.getStart().getDistanceFrom(point) < 0.001f) ++starting;
    });
    expect(ending == 1 && starting == 1, "dash crosses each corner without resetting its phase");
  }
  expect(liveLoopBorder::point(border, 0.0f) == liveLoopBorder::point(border, length),
         "perimeter seam closes at the same position");
  Fixture f; auto &wave = f.waveform;
  wave.setMinimalPhoneMode(true); wave.setSize(390, 250); wave.setLiveMode(true);
  int loop = -1; wave.setActiveArrangementLoopPartProvider([&] { return loop; });
  const auto baseline = wave.createComponentSnapshot(wave.getLocalBounds());
  const auto geometry = WaveformEditingTestAccess::liveBounds(wave)[0];
  loop = 0; const auto moving = wave.createComponentSnapshot(wave.getLocalBounds());
  loop = -1; const auto stopped = wave.createComponentSnapshot(wave.getLocalBounds());
  expect(WaveformEditingTestAccess::liveBounds(wave)[0].body == geometry.body &&
             WaveformEditingTestAccess::liveBounds(wave)[0].loopButton == geometry.loopButton,
         "animation never alters section or action geometry");
  int differences = 0;
  for (int x = 5; x < 180; ++x) {
    expect(baseline.getPixelAt(x, 3) == stopped.getPixelAt(x, 3), "removing real Loop removes the border effect");
    if (moving.getPixelAt(x, 3) != stopped.getPixelAt(x, 3)) ++differences;
  }
  expect(differences > 20, "real Loop activates the animated border in the production renderer");
}

void testDesktopMoreDeduplicatesVisibleActions() {
  ProcessorFixture f; TopBar bar(f.processor); bar.setMinimalPhoneMode(false); bar.setSize(1280, 64);
  auto panel = WaveformEditingTestAccess::morePanel(bar);
  const auto visibleButtons = [](juce::Component &content) {
    int count = 0;
    for (int i = 0; i < content.getNumChildComponents(); ++i)
      if (auto *button = dynamic_cast<juce::Button *>(content.getChildComponent(i)); button && button->isVisible()) {
        expect(!button->getBounds().isEmpty(), "every visible menu action has an accessible target"); ++count;
      }
    return count;
  };
  const auto verify = [&] {
    WaveformEditingTestAccess::moreRefresh(bar);
    int expected = 1 + (WaveformEditingTestAccess::directMidi(bar) ? 0 : 1) + (WaveformEditingTestAccess::directSettings(bar) ? 0 : 1);
    expect(visibleButtons(*panel) >= expected && visibleButtons(*panel) <= expected + 1, "desktop overflow omits direct actions and keeps learning/window actions");
    expect(visibleButtons(*panel) > 0, "learning keeps the menu nonempty");
    const auto image = panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f);
    expect(image.isValid(), "deduplicated menu renders from the production component");
  };
  expect(WaveformEditingTestAccess::directMidi(bar) && WaveformEditingTestAccess::directSettings(bar), "wide desktop exposes primary toolbar actions"); verify();
  for (int width : {768, 1200, 956, 1280, 600, 1280}) { bar.setSize(width, 64); verify(); }
  bar.setSize(768, 64); bar.setMidiLearnActive(true); WaveformEditingTestAccess::moreRefresh(bar);
  auto *midiRow = dynamic_cast<juce::Button *>(panel->getChildComponent(0));
  expect(midiRow && midiRow->isVisible() && midiRow->getToggleState(), "responsive MIDI menu preserves active state");
  bar.setMidiLearnActive(false); WaveformEditingTestAccess::moreRefresh(bar);
  expect(!midiRow->getToggleState(), "open menu reflects MIDI changes without reopening");
  bar.setSize(1280, 64); verify();

  WaveformEditingTestAccess::midiEnabled(bar, false);
  expect(visibleButtons(*panel) >= 2, "an inaccessible direct control returns to overflow");
  WaveformEditingTestAccess::midiEnabled(bar, true); verify();
  for (int iteration = 0; iteration < 4; ++iteration) { panel.reset(); WaveformEditingTestAccess::moreRefresh(bar); panel = WaveformEditingTestAccess::morePanel(bar); verify(); }
  const auto capture = juce::SystemStats::getEnvironmentVariable("PW_DESKTOP_MENU_CAPTURE", {});
  if (capture.isNotEmpty()) {
    juce::Image evidence(juce::Image::RGB, 640, 420, true); juce::Graphics g(evidence); g.fillAll(UITheme::appBg);
    int x = 0;
    for (int width : {1280, 768}) {
      bar.setSize(width, 64); verify();
      g.setColour(juce::Colours::white); g.setFont(makeAppFont(16.0f));
      g.drawText(juce::String(width) + " px", x + 10, 8, 280, 24, juce::Justification::centredLeft);
      g.drawImageAt(panel->createComponentSnapshot(panel->getLocalBounds(), true, 1.0f), x + 10, 38); x += 320;
    }
    const juce::File file(capture); auto output = file.createOutputStream();
    expect(output && output->setPosition(0) && output->truncate().wasOk() && juce::PNGImageFormat().writeImageToStream(evidence, *output),
           "desktop overflow evidence saves from real production menu content");
  }
  bar.setMinimalPhoneMode(true); bar.setSize(390, 64);
  panel.reset(); panel = WaveformEditingTestAccess::morePanel(bar); verify();
  expect(visibleButtons(*panel) >= 3, "phone retains its existing menu actions");
}

void testSectionViewStatesAccessibilityAndKeyboard() {
  Fixture f; auto &wave = f.waveform;
  wave.setMinimalPhoneMode(true); wave.setSize(568, 180);
  auto arrangement = WaveformEditingTestAccess::snapshot(wave);
  arrangement.parts[1].name = arrangement.parts[2].name = "Coro";
  wave.setArrangement(arrangement, true);
  double position = 15.0; bool playing = true;
  int preparing = -1, pending = -1, loop = -1, jumps = 0, loops = 0, seeks = 0;
  wave.setTransportProviders([&] { return position; }, [] { return 30.0; }, [&] { return playing; });
  wave.setPreparingArrangementPartProvider([&] { return preparing; });
  wave.setPendingArrangementPartProvider([&] { return pending; });
  wave.setActiveArrangementLoopPartProvider([&] { return loop; });
  wave.setOnArrangementPartJumpRequest([&](int index) { ++jumps; pending = pending == index ? -1 : index; });
  wave.setOnArrangementPartLoopToggleRequest([&](int index) { ++loops; loop = loop == index ? -1 : index; });
  wave.setSeekCallback([&](double) { ++seeks; });
  wave.setLiveMode(true);
  // JUCE only exposes its native accessibility bridge with a window handle.
  // Keep this synthetic fixture peer hidden; no app/session is launched.
  wave.addToDesktop(juce::ComponentPeer::windowIsTemporary);
  expect(WaveformEditingTestAccess::sectionState(wave, 1) == "Sonando", "section under transport reports sounding only during playback");
  const auto before = WaveformEditingTestAccess::liveText(wave, WaveformEditingTestAccess::liveBounds(wave)[1]);
  playing = false;
  expect(WaveformEditingTestAccess::sectionState(wave, 1) == "Pausada", "paused transport has an explicit paused section state");
  const auto paused = WaveformEditingTestAccess::liveText(wave, WaveformEditingTestAccess::liveBounds(wave)[1]);
  expect(before.fontSize == paused.fontSize && before.title == paused.title && before.state == paused.state,
         "playing and paused state never shrink or reposition the title");
  playing = true; preparing = 2;
  expect(WaveformEditingTestAccess::sectionState(wave, 2) == "Preparando", "preparation remains distinct from confirmed schedule");
  preparing = -1; pending = 2;
  expect(WaveformEditingTestAccess::sectionState(wave, 2) == "Salto programado", "confirmed pending destination reports a scheduled jump");
  loop = 0;
  expect(WaveformEditingTestAccess::sectionState(wave, 0) == "Loop armado", "future loop is armed rather than sounding");
  loop = 1;
  expect(WaveformEditingTestAccess::sectionState(wave, 1) == juce::String::fromUTF8("Sonando · Loop"), "current loop remains distinct from future armed loop");
  expect(before.stateFontSize >= 14.0f && before.stateLines == 2, "state text has fixed readable size and reserved space");
  for (double now : {0.0, 450.0, 900.0, 1600.0})
    expectNear(WaveformEditingTestAccess::pendingPulse(now, true), 0.5, "reduced-motion destination signal stays static across animation time");
  expect(WaveformEditingTestAccess::pendingPulse(450.0, false) != WaveformEditingTestAccess::pendingPulse(900.0, false),
         "ordinary scheduled destination retains the existing pulse");
  WaveformEditingTestAccess::refreshSectionActions(wave);
  auto *go = WaveformEditingTestAccess::sectionAction(wave, "B", 0);
  auto *loopAction = WaveformEditingTestAccess::sectionAction(wave, "A", 1);
  auto *mute = WaveformEditingTestAccess::sectionAction(wave, "A", 2);
  expect(go && loopAction && mute && go->getWidth() == 44 && go->getHeight() == 44, "individual action surfaces retain touch dimensions");
  auto *goHandler = go->getAccessibilityHandler();
  expect(goHandler && goHandler->getRole() == juce::AccessibilityRole::button, "Go exposes an accessible button role");
  expect(goHandler->getTitle().contains("3. Coro"), "Go identifies destination ordinal: " + goHandler->getTitle().toStdString());
  expect(goHandler->getDescription().contains("Salto programado"), "Go reports its real schedule: " + goHandler->getDescription().toStdString());
  expect(goHandler->getHelp().contains("Cancelar"), "Go explains existing cancellation: " + goHandler->getHelp().toStdString());
  expect(goHandler->getValueInterface() && goHandler->getValueInterface()->isReadOnly() &&
             goHandler->getValueInterface()->getCurrentValueAsString().contains("Salto programado") &&
             !goHandler->getActions().contains(juce::AccessibilityActionType::toggle),
         "scheduled Go exposes a read-only real state and only explicit press activation");
  auto *loopHandler = loopAction->getAccessibilityHandler();
  expect(loopHandler && loopHandler->getRole() == juce::AccessibilityRole::toggleButton &&
             loopHandler->getCurrentState().isCheckable() && loopHandler->getCurrentState().isChecked(),
         "Loop exposes its actual checked state");
  auto *muteHandler = mute->getAccessibilityHandler();
  expect(muteHandler && !muteHandler->getCurrentState().isChecked() && muteHandler->getHelp().contains("click"),
         "Mute exposes instrument state and keeps click exception explicit");
  const auto tree = wave.getAccessibilityHandler()->getChildren();
  expect(tree.size() == 9, "accessibility group exposes all three actions for each section");
  const int beforeFocus = jumps + loops + f.commits;
  goHandler->grabFocus();
  expect(goHandler->getCurrentState().isFocused() && jumps + loops + f.commits == beforeFocus,
         "accessible focus is explicit and sends no musical command");
  const auto treePath = juce::SystemStats::getEnvironmentVariable("PW_SECTION_UX_ACCESSIBILITY_PATH", {});
  if (treePath.isNotEmpty()) {
    juce::Array<juce::var> nodes;
    for (const auto *handler : tree) {
      auto node = std::make_unique<juce::DynamicObject>();
      node->setProperty("id", handler->getComponent().getComponentID());
      node->setProperty("role", handler->getRole() == juce::AccessibilityRole::button ? "button" : "toggleButton");
      node->setProperty("name", handler->getTitle()); node->setProperty("state", handler->getDescription());
      node->setProperty("help", handler->getHelp());
      node->setProperty("checked", handler->getCurrentState().isChecked());
      node->setProperty("focused", handler->getCurrentState().isFocused());
      node->setProperty("bounds", handler->getComponent().getBounds().toString());
      node->setProperty("press", handler->getActions().contains(juce::AccessibilityActionType::press));
      node->setProperty("toggle", handler->getActions().contains(juce::AccessibilityActionType::toggle));
      nodes.add(juce::var(node.release()));
    }
    expect(juce::File(treePath).replaceWithText(juce::JSON::toString(juce::var(nodes))), "native hidden-peer accessibility evidence saves");
  }
  const auto focusCapture = juce::SystemStats::getEnvironmentVariable("PW_SECTION_UX_CAPTURE_DIR", {});
  if (focusCapture.isNotEmpty()) {
    auto output = juce::File(focusCapture).getChildFile("accessible-focus.png").createOutputStream();
    expect(output && output->setPosition(0) && output->truncate().wasOk() &&
               juce::PNGImageFormat().writeImageToStream(wave.createComponentSnapshot(wave.getLocalBounds(), true, 2.0f), *output),
           "accessible focus indicator renders on the real production component");
  }
  expect(loopAction->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)) && loops == 1 && loop == -1,
         "Return dispatches the existing Loop toggle once");
  expect(mute->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)) && muteHandler->getCurrentState().isChecked(),
         "Space dispatches real Mute and updates accessible state");
  expect(muteHandler->getActions().invoke(juce::AccessibilityActionType::toggle) && !muteHandler->getCurrentState().isChecked(),
         "accessible toggle dispatches explicit unmute through the same path");
  expect(goHandler->getActions().invoke(juce::AccessibilityActionType::press) && jumps == 1 && pending == -1,
         "accessible press retains cancellation of the already scheduled destination");
  expect(go->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)) && jumps == 2 && pending == 2,
         "Space uses the unchanged Go callback instead of seek");
  wave.setSize(390, 180); WaveformEditingTestAccess::setLiveOffset(wave, 0.0f);
  auto *firstGo = WaveformEditingTestAccess::sectionAction(wave, "Intro", 0);
  const int commands = jumps + loops + f.commits;
  expect(firstGo->keyPressed(juce::KeyPress(juce::KeyPress::endKey)) && WaveformEditingTestAccess::liveScroll(wave) > 0.0f,
         "End reveals an offscreen action without changing transport");
  expect(jumps + loops + f.commits == commands && seeks == 0, "keyboard navigation sends no musical commands");
  auto skipped = WaveformEditingTestAccess::snapshot(wave); skipped.parts[1].skipPlayback = true;
  wave.setArrangement(skipped, true); WaveformEditingTestAccess::setLiveOffset(wave, 0.0f);
  expect(firstGo->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)) && WaveformEditingTestAccess::liveScroll(wave) > 0.0f,
         "arrow navigation passes a section without actions instead of trapping keyboard focus");
  expect(jumps + loops + f.commits == commands && seeks == 0, "passing a skipped section changes only viewport/focus");
  wave.setMidiLearnActive(true); WaveformEditingTestAccess::refreshSectionActions(wave);
  goHandler->getActions().invoke(juce::AccessibilityActionType::press);
  expect(jumps == 2, "accessibility cannot bypass MIDI Learn action gating");
  wave.setMidiLearnActive(false);
  wave.setLiveMode(false);
  goHandler->getActions().invoke(juce::AccessibilityActionType::press);
  expect(jumps == 2 && !go->isVisible(), "inactive section view exposes no active action surfaces");
}

void testSectionViewReadableStatesPreview() {
  const auto capturePath = juce::SystemStats::getEnvironmentVariable("PW_SECTION_UX_CAPTURE_DIR", {});
  for (const auto size : {juce::Point<int>(568, 108), {844, 180}, {1024, 140}, {1280, 180}}) {
    Fixture f; auto &wave = f.waveform;
    wave.setMinimalPhoneMode(true); wave.setMinimalPhoneMode(size.x < 900); wave.setSize(size.x, size.y);
    auto arrangement = WaveformEditingTestAccess::snapshot(wave);
    arrangement.parts[0].name = "Vamp"; arrangement.parts[1].name = "Coro";
    arrangement.parts[2].name = juce::String::fromUTF8("Último coro y oración congregacional");
    wave.setArrangement(arrangement, true);
    std::vector<float> bins(512);
    for (size_t i = 0; i < bins.size(); ++i)
      bins[i] = 0.15f + 0.5f * std::abs(std::sin(i * 0.13f)) * std::abs(std::cos(i * 0.033f));
    wave.setWaveformBins(bins);
    bool playing = true; int preparing = -1, pending = -1, loop = -1;
    double position = 15.0;
    wave.setTransportProviders([&] { return position; }, [] { return 30.0; }, [&] { return playing; });
    wave.setPreparingArrangementPartProvider([&] { return preparing; });
    wave.setPendingArrangementPartProvider([&] { return pending; });
    wave.setActiveArrangementLoopPartProvider([&] { return loop; });
    wave.setLiveMode(true);
    const auto baseline = WaveformEditingTestAccess::liveText(wave, WaveformEditingTestAccess::liveBounds(wave)[1]);
    const auto capture = [&](const char *state) {
      WaveformEditingTestAccess::refreshSectionActions(wave);
      const auto text = WaveformEditingTestAccess::liveText(wave, WaveformEditingTestAccess::liveBounds(wave)[1]);
      expect(text.fontSize == baseline.fontSize && text.lines == baseline.lines && text.title.getWidth() == baseline.title.getWidth() && text.title.getHeight() == baseline.title.getHeight(),
             "every musical state preserves title size and line layout");
      expect(text.stateFontSize == 14.0f && text.state.getBottom() <= WaveformEditingTestAccess::liveBounds(wave)[1].jumpButton.getY(),
             "readable state never overlaps touch actions, including 108-point height");
      const auto image = wave.createComponentSnapshot(wave.getLocalBounds(), true, 2.0f);
      expect(image.isValid(), "readable section states render from the production component");
      if (capturePath.isNotEmpty()) {
        const juce::File directory(capturePath); expect(directory.createDirectory().wasOk(), "section UX evidence directory opens");
        auto output = directory.getChildFile(juce::String(state) + "-" + juce::String(size.x) + ".png").createOutputStream();
        expect(output && output->setPosition(0) && output->truncate().wasOk() && juce::PNGImageFormat().writeImageToStream(image, *output),
               "section UX state evidence writes complete PNG bytes");
      }
    };
    capture("sonando"); playing = false; capture("pausada"); playing = true;
    preparing = 2; capture("preparando"); preparing = -1; pending = 2; capture("programado");
    pending = -1; loop = 1; arrangement.parts[1].muteInstruments = true; wave.setArrangement(arrangement, true); capture("loop-mute");
    loop = -1; position = 25.0; capture("nombre-largo-activo");
    WaveformEditingTestAccess::setLiveOffset(wave, WaveformEditingTestAccess::liveBounds(wave)[0].body.getWidth() * 0.4f);
    capture("recorte-parcial");
  }
}

void testLiveTitleLayoutInvariantDuringScroll() {
  for (const int width : {390, 768, 1200}) {
    Fixture f; auto &wave = f.waveform;
    wave.setMinimalPhoneMode(true); wave.setMinimalPhoneMode(width == 390); wave.setSize(width, 108);
    SongArrangementSnapshot arrangement;
    for (int i = 0; i < 12; ++i) arrangement.parts.push_back(original(("Section" + std::to_string(i)).c_str(), i * 3.0, (i + 1) * 3.0));
    arrangement.parts[1].name = "Long section title with several words";
    wave.setArrangement(arrangement, true); wave.setLiveMode(true);
    const auto baseline = WaveformEditingTestAccess::liveBounds(wave);
    const float stride = baseline[1].body.getX() - baseline[0].body.getX();
    std::vector<decltype(WaveformEditingTestAccess::liveText(wave, baseline[0]))> textBaseline;
    for (const auto &item : baseline) textBaseline.push_back(WaveformEditingTestAccess::liveText(wave, item));
    const auto verify = [&] {
      const auto items = WaveformEditingTestAccess::liveBounds(wave);
      for (size_t i = 0; i < items.size(); ++i) {
        const auto text = WaveformEditingTestAccess::liveText(wave, items[i]);
        const auto &old = textBaseline[i];
        expect(text.fontSize == old.fontSize && text.lines == old.lines && text.title.getWidth() == old.title.getWidth() && text.title.getHeight() == old.title.getHeight(),
               "scroll never changes title font, line count or layout dimensions");
        expectNear(text.title.getX() - items[i].body.getX(), old.title.getX() - baseline[i].body.getX(),
                   "title remains at the same position relative to its complete block");
        expect(text.title.getY() == old.title.getY() && text.state.getHeight() == old.state.getHeight(),
               "hidden edge controls never change title or indicator vertical layout");
      }
      const auto image = wave.createComponentSnapshot(wave.getLocalBounds(), true, 2.0f);
      expect(image.isValid(), "production renderer clips constant-size edge titles naturally");
    };
    for (const float offset : {0.0f, stride * 0.15f, stride * 0.75f, stride * 3.40f, 1000000.0f}) {
      WaveformEditingTestAccess::setLiveOffset(wave, offset); verify();
    }
    WaveformEditingTestAccess::fling(wave, stride, 40.0f, 2000.0);
    for (const double now : {2016.0, 2050.0, 2150.0, 2500.0, 4000.0}) {
      WaveformEditingTestAccess::motionAt(wave, now); verify();
    }
    WaveformEditingTestAccess::fling(wave, stride * 3, -40.0f, 2000.0);
    for (const double now : {2016.0, 2100.0, 2500.0, 4000.0}) {
      WaveformEditingTestAccess::motionAt(wave, now); verify();
    }
  }
}

void testAdaptiveLiveTitles() {
  const auto capturePath = juce::SystemStats::getEnvironmentVariable("PW_LIVE_TITLE_CAPTURE_DIR", {});
  juce::Image evidence(juce::Image::RGB, 1200, 1000, true); juce::Graphics sheet(evidence);
  sheet.fillAll(juce::Colour(0xff262220)); int evidenceY = 0;
  for (const auto size : {juce::Point<int>(390, 108), {768, 140}, {1200, 180}, {390, 108}}) {
    Fixture f; auto &wave = f.waveform;
    wave.setMinimalPhoneMode(true); wave.setMinimalPhoneMode(size.x == 390);
    wave.setSize(size.x, size.y);
    SongArrangementSnapshot arrangement;
    arrangement.parts = {original("Coro", 0, 10), original("Bridge", 10, 20),
                          original("Voice", 20, 30), original("INTERLUDIOINSTRUMENTALEXTENSOSINESPACIOS", 30, 40)};
    arrangement.parts[1].name = juce::String::fromUTF8("Puente instrumental y oración congregacional");
    arrangement.parts[2].name = juce::String::fromUTF8("Voz y adoración sin final");
    expect(arrangementPartDisplayLabelForName(arrangement.parts[1].name).contains(juce::String::fromUTF8("oración")),
           "adaptive title retains accented UTF-8 section names");
    arrangement.parts[0].muteInstruments = true;
    wave.setArrangement(arrangement, true); wave.setWaveformBins(std::vector<float>(512, 0.8f));
    wave.setTransportProviders([] { return 5.0; }, [] { return 40.0; }, [] { return true; });
    wave.setPendingArrangementPartProvider([] { return 1; });
    wave.setPreparingArrangementPartProvider([] { return 2; });
    wave.setActiveArrangementLoopPartProvider([] { return 0; });
    wave.setLiveMode(true);
    const bool partial = evidenceY >= 600;
    const auto initial = WaveformEditingTestAccess::liveBounds(wave);
    const float stride = initial[1].body.getX() - initial[0].body.getX();
    if (partial) WaveformEditingTestAccess::setLiveOffset(wave, stride * 0.40f);
    else if (size.x == 390) WaveformEditingTestAccess::setLiveOffset(wave, stride * 0.15f);
    const auto image = wave.createComponentSnapshot(wave.getLocalBounds(), true, 2.0f);
    for (const auto &item : WaveformEditingTestAccess::liveBounds(wave)) {
      if (!item.body.intersects(WaveformEditingTestAccess::liveRow(wave))) continue;
      const auto text = WaveformEditingTestAccess::liveText(wave, item);
      expect(!text.title.isEmpty(), "visible Live section always reserves a name region");
      expect(item.body.contains(text.title), "title layout stays anchored inside its complete section");
      expect(!text.title.intersects(item.jumpButton) && !text.title.intersects(item.loopButton) && !text.title.intersects(item.muteButton) &&
                 !text.title.intersects(text.state), "title never overlaps actions or state indicators");
      expect(text.fontSize >= 14.0f && text.fontSize <= 30.0f, "normal Live heights preserve readable adaptive font bounds");
      const auto pixels = text.title.transformedBy(juce::AffineTransform::scale(2.0f)).toNearestInt().getIntersection(image.getBounds());
      int white = 0;
      for (int y = pixels.getY(); y < pixels.getBottom(); ++y)
        for (int x = pixels.getX(); x < pixels.getRight(); ++x) {
          const auto colour = image.getPixelAt(x, y);
          if (colour.getRed() > 245 && colour.getGreen() > 245 && colour.getBlue() > 245) ++white;
        }
      if (WaveformEditingTestAccess::liveRow(wave).contains(text.title))
        expect(white > 12, "fully visible title keeps high-contrast glyphs under musical states");
      expectNear(text.title.getCentreX(), item.body.getCentreX(), "title remains centered in the complete section");
    }
    // The same renderer is exercised at 1x and 2x, with unchanged action geometry.
    const auto single = wave.createComponentSnapshot(wave.getLocalBounds(), true, 1.0f);
    expect(single.getWidth() == size.x && image.getWidth() == size.x * 2, "component captures honor display scale");
    sheet.setColour(juce::Colours::white); sheet.setFont(juce::Font(juce::FontOptions(16.0f)));
    sheet.drawText(juce::String(size.x) + " × " + juce::String(size.y) + (partial ? " · desplazamiento parcial" : " · estados combinados"),
                   8, evidenceY, 1180, 24, juce::Justification::centredLeft);
    const float scale = size.x == 390 ? 2.0f : 1.0f;
    sheet.drawImage(image, juce::Rectangle<float>(0.0f, static_cast<float>(evidenceY + 26), size.x * scale, size.y * scale));
    evidenceY += juce::roundToInt(size.y * scale) + 34;
  }
  if (capturePath.isNotEmpty()) {
    const juce::File file(capturePath); expect(file.getParentDirectory().createDirectory().wasOk(), "title evidence folder opens");
    auto stream = file.createOutputStream();
    expect(stream && stream->setPosition(0) && stream->truncate().wasOk() && juce::PNGImageFormat().writeImageToStream(evidence, *stream),
           "real Live title render evidence saves");
  }
}

void testLiveInertialNavigation() {
  for (const int width : {390, 768, 1200}) {
    Fixture f; auto &wave = f.waveform;
    wave.setMinimalPhoneMode(true); // Leave the fixture arrangement editor first.
    wave.setMinimalPhoneMode(width == 390); wave.setSize(width, 250);
    SongArrangementSnapshot arrangement;
    for (int i = 0; i < 12; ++i) arrangement.parts.push_back(original(("p" + std::to_string(i)).c_str(), i * 3.0, (i + 1) * 3.0));
    wave.setArrangement(arrangement, true); wave.setLiveMode(true);
    expect(wave.isLiveMode(), "inertia fixture activates the production Live presentation");
    int pending = -1, jumps = 0, loops = 0, mutes = 0, seeks = 0;
    wave.setTransportProviders([] { return 1.0; }, [] { return 36.0; }, [] { return true; });
    wave.setPendingArrangementPartProvider([&] { return pending; });
    wave.setOnArrangementPartJumpRequest([&](int) { ++jumps; });
    wave.setOnArrangementPartLoopToggleRequest([&](int) { ++loops; });
    wave.setOnArrangementChanged([&](const SongArrangementSnapshot &, bool) { ++mutes; });
    wave.setSeekCallback([&](double) { ++seeks; });
    WaveformEditingTestAccess::fling(wave, 200.0f, 40.0f, 2000.0);
    const float initialSpeed = WaveformEditingTestAccess::velocity(wave);
    expect(initialSpeed > 35.0f && initialSpeed <= 2400.0f, "release carries a bounded sampled drag velocity");
    WaveformEditingTestAccess::motionAt(wave, 2100.0);
    const float coarse = WaveformEditingTestAccess::liveScroll(wave);
    expect(coarse > 240.0f && WaveformEditingTestAccess::velocity(wave) < initialSpeed,
           "released drag continues and decelerates");
    WaveformEditingTestAccess::fling(wave, 200.0f, 40.0f, 2000.0);
    for (int frame = 1; frame <= 10; ++frame) WaveformEditingTestAccess::motionAt(wave, 2000.0 + frame * 10.0);
    expectNear(WaveformEditingTestAccess::liveScroll(wave), coarse, "coast distance is independent of animation frame rate");
    WaveformEditingTestAccess::followAt(wave, 100000.0);
    expectNear(WaveformEditingTestAccess::liveScroll(wave), coarse, "follow cannot compete with a moving row even after an old timeout");
    WaveformEditingTestAccess::motionAt(wave, 4000.0);
    expect(!WaveformEditingTestAccess::moving(wave), "coast and final alignment finish naturally");
    const double resume = WaveformEditingTestAccess::resumeAt(wave);
    const float explored = WaveformEditingTestAccess::liveScroll(wave);
    pending = 11;
    WaveformEditingTestAccess::followAt(wave, resume - 1.0);
    expectNear(WaveformEditingTestAccess::liveScroll(wave), explored, "grace starts after the last inertial movement");
    WaveformEditingTestAccess::followAt(wave, resume); WaveformEditingTestAccess::followAt(wave, resume + 280.0);
    expect(WaveformEditingTestAccess::liveScroll(wave) > explored, "confirmed destination keeps priority after inertia finishes");
    pending = -1;
    WaveformEditingTestAccess::followAt(wave, resume + 300.0); WaveformEditingTestAccess::followAt(wave, resume + 580.0);
    expectNear(WaveformEditingTestAccess::liveScroll(wave), 0.0, "without pending jump the active section resumes follow");
    WaveformEditingTestAccess::fling(wave, 0.0f, -40.0f, 2000.0);
    WaveformEditingTestAccess::motionAt(wave, 4000.0);
    expectNear(WaveformEditingTestAccess::liveScroll(wave), 0.0, "outward motion clamps at the first edge");
    WaveformEditingTestAccess::scrollLiveToEnd(wave);
    const float end = WaveformEditingTestAccess::liveScroll(wave);
    WaveformEditingTestAccess::fling(wave, end - 40.0f, 40.0f, 2000.0);
    WaveformEditingTestAccess::motionAt(wave, 4000.0);
    expectNear(WaveformEditingTestAccess::liveScroll(wave), end, "coasting clamps at the last edge without overscroll");
    WaveformEditingTestAccess::fling(wave, 200.0f, 40.0f, 2000.0, true);
    expect(!WaveformEditingTestAccess::moving(wave), "Reduce Motion disables generated coasting and alignment animation");
    WaveformEditingTestAccess::fling(wave, 200.0f, 40.0f, 2000.0);
    WaveformEditingTestAccess::motionAt(wave, 2050.0, true);
    expect(!WaveformEditingTestAccess::moving(wave), "enabling Reduce Motion stops an existing inertial tail");
    for (int action = 0; action < 3; ++action) {
      WaveformEditingTestAccess::fling(wave, 0.0f, 20.0f, 2000.0);
      const auto bounds = WaveformEditingTestAccess::liveBounds(wave)[0];
      const auto point = (action == 0 ? bounds.jumpButton : action == 1 ? bounds.loopButton : bounds.muteButton).getCentre();
      const float stopped = WaveformEditingTestAccess::liveScroll(wave);
      wave.mouseDown(pointerEvent(wave, point, point)); wave.mouseUp(pointerEvent(wave, point, point));
      WaveformEditingTestAccess::motionAt(wave, 9000.0);
      expect(!WaveformEditingTestAccess::moving(wave), "control contact stops inertia immediately");
      expectNear(WaveformEditingTestAccess::liveScroll(wave), stopped, "stopping tap leaves targets stationary");
    }
    WaveformEditingTestAccess::fling(wave, 200.0f, 40.0f, 2000.0);
    const auto dragStart = WaveformEditingTestAccess::liveRow(wave).getCentre();
    const float beforeNewDrag = WaveformEditingTestAccess::liveScroll(wave);
    wave.mouseDown(pointerEvent(wave, dragStart, dragStart));
    const auto dragEnd = dragStart.translated(-60.0f, 0.0f);
    wave.mouseDrag(pointerEvent(wave, dragEnd, dragStart, true));
    expectNear(WaveformEditingTestAccess::liveScroll(wave), beforeNewDrag + 60.0f,
               "new drag takes ownership without inherited velocity or a starting jump");
    wave.mouseUp(pointerEvent(wave, dragEnd, dragStart, true));
    WaveformEditingTestAccess::finishMotion(wave);
    expect(jumps == 0 && loops == 0 && mutes == 0 && seeks == 0, "stopping moving controls never sends actions or seek");
    WaveformEditingTestAccess::fling(wave, 200.0f, 40.0f, 2000.0);
    juce::MouseWheelDetails wheel{}; wheel.isSmooth = true; wheel.deltaX = -0.05f;
    const auto centre = WaveformEditingTestAccess::liveRow(wave).getCentre();
    wave.mouseWheelMove(pointerEvent(wave, centre, centre), wheel);
    wheel.isSmooth = false; wheel.isInertial = true;
    wave.mouseWheelMove(pointerEvent(wave, centre, centre), wheel);
    expect(!WaveformEditingTestAccess::moving(wave), "native trackpad momentum replaces generated inertia without stacking velocities");
    const auto contact = centre.withY(WaveformEditingTestAccess::liveRow(wave).getY() + 10.0f);
    wave.mouseDown(pointerEvent(wave, contact, contact)); wave.mouseUp(pointerEvent(wave, contact, contact));
    const float stoppedNative = WaveformEditingTestAccess::liveScroll(wave);
    wave.mouseWheelMove(pointerEvent(wave, centre, centre), wheel);
    expectNear(WaveformEditingTestAccess::liveScroll(wave), stoppedNative,
               "new contact rejects residual native momentum after pointer release");

    wave.setLiveMode(false);
    expect(!WaveformEditingTestAccess::moving(wave), "presentation change clears inertial state");
  }
}

void testPendingLivePriorityAndCombinedStates() {
  Fixture f; auto &wave = f.waveform;
  wave.setMinimalPhoneMode(true); wave.setSize(390, 250);
  int pending = 2, loop = -1;
  double position = 1.0;
  wave.setTransportProviders([&] { return position; }, [] { return 30.0; }, [] { return true; });
  wave.setPendingArrangementPartProvider([&] { return pending; });
  wave.setActiveArrangementLoopPartProvider([&] { return loop; });
  wave.setLiveMode(true);
  WaveformEditingTestAccess::followAt(wave, 1000.0);
  WaveformEditingTestAccess::followAt(wave, 1280.0);
  const auto end = WaveformEditingTestAccess::liveScroll(wave);
  expect(end > 0.0f, "pending destination is visible ahead of current playback");
  WaveformEditingTestAccess::followAt(wave, 10000.0);
  expectNear(WaveformEditingTestAccess::liveScroll(wave), end, "timeout never recenters current section while a destination waits");
  const auto point = WaveformEditingTestAccess::liveRow(wave).getCentre();
  wave.mouseDown(pointerEvent(wave, point, point));
  const auto moved = point.translated(150.0f, 0.0f);
  wave.mouseDrag(pointerEvent(wave, moved, point, true));
  wave.mouseUp(pointerEvent(wave, moved, point, true));
  WaveformEditingTestAccess::finishMotion(wave);
  const double resume = WaveformEditingTestAccess::resumeAt(wave);
  const float explored = WaveformEditingTestAccess::liveScroll(wave);
  WaveformEditingTestAccess::followAt(wave, resume - 1.0);
  expectNear(WaveformEditingTestAccess::liveScroll(wave), explored, "pending priority respects manual exploration grace");
  WaveformEditingTestAccess::followAt(wave, resume);
  WaveformEditingTestAccess::followAt(wave, resume + 280.0);
  expectNear(WaveformEditingTestAccess::liveScroll(wave), end, "exploration timeout resumes the pending destination instead of current section");
  pending = 0;
  WaveformEditingTestAccess::followAt(wave, resume + 500.0);
  WaveformEditingTestAccess::followAt(wave, resume + 780.0);
  expectNear(WaveformEditingTestAccess::liveScroll(wave), 0.0, "changing destination updates follow target");
  pending = -1; position = 25.0;
  WaveformEditingTestAccess::followAt(wave, resume + 1000.0);
  WaveformEditingTestAccess::followAt(wave, resume + 1280.0);
  expectNear(WaveformEditingTestAccess::liveScroll(wave), end, "cancellation resumes current section follow");
  pending = 0;
  WaveformEditingTestAccess::followAt(wave, resume + 1500.0);
  WaveformEditingTestAccess::followAt(wave, resume + 1780.0);
  position = 1.0; pending = -1;
  WaveformEditingTestAccess::followAt(wave, resume + 2000.0);
  expectNear(WaveformEditingTestAccess::liveScroll(wave), 0.0, "executing queued destination keeps its now-active block stationary");

  const auto capture = [&] { return wave.createComponentSnapshot(wave.getLocalBounds()); };
  const auto baseline = capture();
  loop = 0; const auto loopOnly = capture();
  int changed = 0;
  for (int x = 5; x < 180; ++x) if (baseline.getPixelAt(x, 3) != loopOnly.getPixelAt(x, 3)) ++changed;
  expect(changed > 20 && changed < 170, "Loop renders a dashed border distinct from a continuous progress edge");
  auto arrangement = WaveformEditingTestAccess::snapshot(wave);
  arrangement.parts[0].muteInstruments = true; wave.setArrangement(arrangement, true);
  const auto muted = capture();
  expect(muted.getPixelAt(80, 70).getBrightness() < loopOnly.getPixelAt(80, 70).getBrightness(),
         "Mute attenuates section background and waveform");
  pending = 0; const auto combined = capture();
  const auto block = WaveformEditingTestAccess::liveBounds(wave)[0];
  for (const auto target : {block.jumpButton, block.loopButton, block.muteButton}) {
    float brightest = 0.0f;
    const auto area = target.toNearestInt();
    for (int y = area.getY(); y < area.getBottom(); ++y)
      for (int x = area.getX(); x < area.getRight(); ++x)
        brightest = std::max(brightest, combined.getPixelAt(x, y).getBrightness());
    expect(brightest > 0.6f, "combined pending/loop/mute state keeps every control legible");
  }
  pending = -1; arrangement.parts[0].muteInstruments = false; wave.setArrangement(arrangement, true);
  const auto restored = capture();
  expect(restored.getPixelAt(80, 70) == loopOnly.getPixelAt(80, 70), "unmute restores the original waveform appearance while Loop remains");
  int preparing = 0;
  wave.setPreparingArrangementPartProvider([&] { return preparing; });
  const auto preparingImage = capture();
  expect(preparingImage.getPixelAt(80, 70) == restored.getPixelAt(80, 70),
         "Preparing leaves the section body free of confirmed-jump pulse");
  expect(!WaveformEditingTestAccess::liveBounds(wave)[0].jumpButton.isEmpty(),
         "Preparing preserves the approved action target geometry");
  preparing = -1;
  int clearRequests = 0, jumpRequests = 0;
  pending = 0; loop = 0;
  wave.setOnArrangementPendingJumpClearRequest([&] { ++clearRequests; pending = -1; });
  wave.setOnArrangementPartJumpRequest([&](int index) { ++jumpRequests; pending = index; });
  const auto click = [&](juce::Point<float> point) {
    wave.mouseDown(pointerEvent(wave, point, point));
    wave.mouseUp(pointerEvent(wave, point, point));
  };
  click(block.muteButton.getCentre());
  expect(pending == 0 && loop == 0 && clearRequests == 0 &&
             WaveformEditingTestAccess::snapshot(wave).parts[0].muteInstruments,
         "Mute preserves pending Ir and Loop");
  click(block.jumpButton.getCentre());
  expect(jumpRequests == 1 && pending == 0 &&
             WaveformEditingTestAccess::snapshot(wave).parts[0].muteInstruments,
         "Ir preserves destination mute");
  click(block.muteButton.getCentre());
  expect(!WaveformEditingTestAccess::snapshot(wave).parts[0].muteInstruments && pending == 0 && clearRequests == 0,
         "explicit unmute preserves pending Ir");
  click(block.muteButton.getCentre());
  expect(WaveformEditingTestAccess::snapshot(wave).parts[0].muteInstruments && pending == 0 && loop == 0 && clearRequests == 0,
         "repeated Mute remains independent of transport intents");
}

void testLiveActionsSurviveLayoutRefresh() {
  for (const bool phone : {false, true}) {
    Fixture f;
    f.waveform.setMinimalPhoneMode(true);
    f.waveform.setMinimalPhoneMode(phone);
    f.waveform.setSize(390, 250);
    f.waveform.setLiveMode(true);
    int jumps = 0, loops = 0, seeks = 0;
    f.waveform.setOnArrangementPartJumpRequest([&](int) { ++jumps; });
    f.waveform.setOnArrangementPartLoopToggleRequest([&](int) { ++loops; });
    f.waveform.setSeekCallback([&](double) { ++seeks; });
    for (const bool jump : {true, false}) {
      const auto block = WaveformEditingTestAccess::liveBounds(f.waveform)[0];
      const auto point = (jump ? block.jumpButton : block.loopButton).getCentre();
      f.waveform.mouseDown(pointerEvent(f.waveform, point, point));
      // The editor reapplies its responsive profile during a layout refresh.
      f.waveform.setMinimalPhoneMode(phone);
      f.waveform.resized();
      f.waveform.mouseUp(pointerEvent(f.waveform, point, point));
    }
    expect(jumps == 1 && loops == 1 && seeks == 0,
           "valid live Go/Loop survive unchanged responsive layout between down and up");
    WaveformEditingTestAccess::setLiveOffset(f.waveform, 40.0f);
    const auto point = WaveformEditingTestAccess::liveBounds(f.waveform)[1].loopButton.getCentre();
    const auto window = WaveformEditingTestAccess::visibleWindow(f.waveform);
    for (int repeat = 0; repeat < 4; ++repeat) {
      f.waveform.mouseDown(pointerEvent(f.waveform, point, point));
      f.waveform.setWaveformBins(std::vector<float>(512, 0.6f));
      f.waveform.resized();
      WaveformEditingTestAccess::vblank(f.waveform, repeat + 1.0);
      f.waveform.mouseUp(pointerEvent(f.waveform, point, point));
      expectNear(WaveformEditingTestAccess::liveScroll(f.waveform), 40.0f,
                 "tap and preview refresh retain fractional centered row position");
    }
    expect(loops == 5 && seeks == 0, "every repeated valid loop press invokes exactly once");
    expect(WaveformEditingTestAccess::visibleWindow(f.waveform) == window,
           "normal waveform VBlank leaves the saved viewport alone in Live");
    f.waveform.mouseDown(pointerEvent(f.waveform, point, point));
    const auto dragged = point.translated(-30.0f, 0.0f);
    f.waveform.mouseDrag(pointerEvent(f.waveform, dragged, point, true));
    f.waveform.mouseDrag(pointerEvent(f.waveform, point, point, true));
    f.waveform.mouseUp(pointerEvent(f.waveform, point, point, true));
    expect(loops == 5 && seeks == 0, "deliberate drag returning to its button never acts");
    const auto changedPoint = WaveformEditingTestAccess::liveBounds(f.waveform)[1].loopButton.getCentre();
    f.waveform.mouseDown(pointerEvent(f.waveform, changedPoint, changedPoint));
    f.waveform.setSize(420, 250);
    f.waveform.mouseUp(pointerEvent(f.waveform, changedPoint, changedPoint));
    expect(loops == 5, "real geometry change cancels an obsolete button capture");
  }
}

void testLiveActionsReachRealTransport() {
  ProcessorFixture fixture;
  auto &wave = fixture.edit.waveform;
  auto &processor = fixture.processor;
  processor.setArrangementEditingActive(false);
  wave.setMinimalPhoneMode(true);
  wave.setSize(390, 250);
  wave.setLiveMode(true);
  wave.setOnArrangementPartJumpRequest([&](int index) { processor.requestArrangementPartJump(index); });
  wave.setOnArrangementPartLoopToggleRequest([&](int index) {
    processor.clearPendingArrangementPartJump();
    processor.toggleArrangementPartLoop(index);
  });
  const auto block = WaveformEditingTestAccess::liveBounds(wave)[1];
  const auto press = [&](juce::Point<float> point) {
    wave.mouseDown(pointerEvent(wave, point, point));
    wave.setMinimalPhoneMode(true);
    wave.resized();
    wave.mouseUp(pointerEvent(wave, point, point));
  };
  press(block.jumpButton.getCentre());
  expectNear(processor.getSongCurrentPositionSeconds(), 10.0,
             "stopped Go reaches the real target using existing transport semantics");
  processor.seekSongTo(0.0);
  processor.playSong();
  expect(processor.isSongPlaying(), "isolated transport enters playback");
  press(block.jumpButton.getCentre());
  expect(processor.getPreparingArrangementPartJumpIndex() == 1 && processor.getPendingArrangementPartJumpIndex() == -1,
         "playing Go starts preparation without a phantom confirmed destination");
  press(block.jumpButton.getCentre());
  expect(processor.getPendingArrangementPartJumpIndex() == -1 && processor.getPreparingArrangementPartJumpIndex() == -1,
         "repeated Go cancels the real preparation as specified");
  press(block.jumpButton.getCentre());
  press(block.loopButton.getCentre());
  expect(processor.getArrangementPartLoopIndex() == 1 &&
             processor.getPendingArrangementPartJumpIndex() == -1,
         "Loop reaches the real processor and replaces pending Go");
  expectNear(processor.getSongCurrentPositionSeconds(), 0.0, "marking Loop never seeks immediately");
  press(block.loopButton.getCentre());
  expect(processor.getArrangementPartLoopIndex() == -1, "repeated Loop toggles off exactly once");
  processor.pauseSong();
}

void testMasterLeadingBoundary() {
  ChannelStripLookAndFeel look;
  ChannelStripCard master("MASTER", look, [] { return std::pair<float,float>{0.0f, 0.0f}; });
  master.setSize(90, 244);
  master.setCardBgColour(UITheme::cardBgMaster);
  master.setChannelBandColours(UITheme::accentMaster, UITheme::onAccent);
  master.setLeadingBoundaryVisible(true);
  const auto image = master.createComponentSnapshot(master.getLocalBounds());
  for (const int y : {8, 100, 240})
    expect(image.getPixelAt(0, y) == UITheme::cardBorder,
           "Master leading boundary remains defined across body and footer");
  const auto path = juce::SystemStats::getEnvironmentVariable("PW_LIVE_CAPTURE_DIR", {});
  if (!path.isEmpty()) {
    juce::File directory(path);
    expect(directory.createDirectory().wasOk(), "Master capture directory opens");
    juce::FileOutputStream output(directory.getChildFile("master-left-boundary.png"));
    juce::PNGImageFormat png;
    expect(png.writeImageToStream(image, output), "real Master component render writes");
  }
}

void testDesktopNormalAndLiveFollow() {
  for (const bool phone : {false, true}) {
    Fixture f;
    f.waveform.setMinimalPhoneMode(true); // Leave arrangement editing.
    f.waveform.setMinimalPhoneMode(phone);
    f.waveform.setSize(390, 250);
#if !JUCE_IOS && !JUCE_ANDROID
    if (!phone) {
      for (const int width : {390, 768, 1280}) {
        f.waveform.setSize(width, 250);
        WaveformEditingTestAccess::expectNoDesktopPlaybackActions(f.waveform);
      }
      f.waveform.setSize(390, 250);
    }
#endif
    double position = 25.0;
    bool playing = true;
    int seeks = 0, jumps = 0, loops = 0;
    f.waveform.setTransportProviders([&] { return position; }, [] { return 30.0; }, [&] { return playing; });
    f.waveform.setSeekCallback([&](double) { ++seeks; });
    f.waveform.setOnArrangementPartJumpRequest([&](int) { ++jumps; });
    f.waveform.setOnArrangementPartLoopToggleRequest([&](int) { ++loops; });
    f.waveform.setLiveMode(true);
    WaveformEditingTestAccess::followAt(f.waveform, 100.0);
    WaveformEditingTestAccess::followAt(f.waveform, 240.0);
    const float midway = WaveformEditingTestAccess::liveScroll(f.waveform);
    expect(midway > 0.0f, "active final section centers with a smooth transition");
    WaveformEditingTestAccess::followAt(f.waveform, 380.0);
    const float end = WaveformEditingTestAccess::liveScroll(f.waveform);
    expect(end > midway, "centering finishes at the clamped final edge");
    auto blocks = WaveformEditingTestAccess::liveBounds(f.waveform);
    expectNear(blocks.back().body.getRight(), WaveformEditingTestAccess::liveRow(f.waveform).getRight(),
               "final section centering exposes no trailing blank space");
    position = 26.0;
    WaveformEditingTestAccess::followAt(f.waveform, 1000.0);
    expectNear(WaveformEditingTestAccess::liveScroll(f.waveform), end,
               "progress within the same section never scrolls");
    position = 1.0;
    WaveformEditingTestAccess::followAt(f.waveform, 1100.0);
    WaveformEditingTestAccess::followAt(f.waveform, 1380.0);
    expectNear(WaveformEditingTestAccess::liveScroll(f.waveform), 0.0,
               "transition to first section clamps at the initial edge");
    const auto start = WaveformEditingTestAccess::liveRow(f.waveform).getCentre();
    f.waveform.mouseDown(pointerEvent(f.waveform, start, start));
    const auto moved = start.translated(-120.0f, 0.0f);
    f.waveform.mouseDrag(pointerEvent(f.waveform, moved, start, true));
    const float dragged = WaveformEditingTestAccess::liveScroll(f.waveform);
    WaveformEditingTestAccess::followAt(f.waveform, WaveformEditingTestAccess::resumeAt(f.waveform) + 10000.0);
    expectNear(WaveformEditingTestAccess::liveScroll(f.waveform), dragged,
               "held drag blocks centering even beyond inactivity deadline");
    f.waveform.mouseUp(pointerEvent(f.waveform, moved, start, true));
    WaveformEditingTestAccess::finishMotion(f.waveform);
    const float explored = WaveformEditingTestAccess::liveScroll(f.waveform);
    const double resume = WaveformEditingTestAccess::resumeAt(f.waveform);
    position = 25.0;
    WaveformEditingTestAccess::followAt(f.waveform, resume - 1.0);
    expectNear(WaveformEditingTestAccess::liveScroll(f.waveform), explored,
               "section transition preserves exploration during three-second grace");
    WaveformEditingTestAccess::followAt(f.waveform, resume);
    WaveformEditingTestAccess::followAt(f.waveform, resume + 280.0);
    expectNear(WaveformEditingTestAccess::liveScroll(f.waveform), end,
               "inactivity resumes centering on the current active section");
    position = 1.0;
    WaveformEditingTestAccess::followAt(f.waveform, resume + 300.0);
    WaveformEditingTestAccess::followAt(f.waveform, resume + 440.0);
    const float interrupted = WaveformEditingTestAccess::liveScroll(f.waveform);
    blocks = WaveformEditingTestAccess::liveBounds(f.waveform);
    const auto button = blocks[1].loopButton.getCentre();
    f.waveform.mouseDown(pointerEvent(f.waveform, button, button));
    WaveformEditingTestAccess::followAt(f.waveform, resume + 10000.0);
    expectNear(WaveformEditingTestAccess::liveScroll(f.waveform), interrupted,
               "button down interrupts a centering animation without moving its hit target");
    f.waveform.mouseUp(pointerEvent(f.waveform, button, button));
    expect(seeks == 0 && jumps == 0 && loops == 1,
           "follow never seeks and interrupted button retains its existing action");
    const double afterButton = WaveformEditingTestAccess::resumeAt(f.waveform);
    WaveformEditingTestAccess::followAt(f.waveform, afterButton);
    WaveformEditingTestAccess::followAt(f.waveform, afterButton + 100.0);
    juce::MouseWheelDetails wheel;
    wheel.deltaY = -1.0f;
    f.waveform.mouseWheelMove(pointerEvent(f.waveform, start, start), wheel);
    const float wheelOffset = WaveformEditingTestAccess::liveScroll(f.waveform);
    WaveformEditingTestAccess::followAt(f.waveform, WaveformEditingTestAccess::resumeAt(f.waveform) - 1.0);
    expectNear(WaveformEditingTestAccess::liveScroll(f.waveform), wheelOffset,
               "wheel exploration cancels an in-flight centering transition");
    playing = false;
    WaveformEditingTestAccess::setLiveOffset(f.waveform, end);
    WaveformEditingTestAccess::followAt(f.waveform, WaveformEditingTestAccess::resumeAt(f.waveform) + 10000.0);
    expectNear(WaveformEditingTestAccess::liveScroll(f.waveform), end, "paused transport does not recenter");
  }
}

void testLiveSectionsAndNavigation() {
  Fixture f;
  expect(!f.waveform.isLiveMode(), "live presentation is opt-in");
  f.waveform.setMinimalPhoneMode(true);
  f.waveform.setSize(390, 250);
  auto arrangement = WaveformEditingTestAccess::snapshot(f.waveform);
  arrangement.parts[0].endSec = 2.0;
  arrangement.parts[1].startSec = 2.0; arrangement.parts[1].endSec = 20.0;
  f.waveform.setArrangement(arrangement, false);
  double position = 1.0;
  bool playing = true;
  int seeks = 0, jumps = 0, loops = 0;
  f.waveform.setTransportProviders([&] { return position; }, [] { return 30.0; }, [&] { return playing; });
  f.waveform.setSeekCallback([&](double) { ++seeks; });
  f.waveform.setOnArrangementPartJumpRequest([&](int) { ++jumps; });
  f.waveform.setOnArrangementPartLoopToggleRequest([&](int) { ++loops; });
  int pending = 2;
  f.waveform.setPendingArrangementPartProvider([&] { return pending; });
  f.waveform.setActiveArrangementLoopPartProvider([] { return 1; });
  const auto normalWindow = WaveformEditingTestAccess::visibleWindow(f.waveform);
  WaveformEditingTestAccess::enableFollowForLiveTest(f.waveform);
  f.waveform.setLiveMode(true);
  auto blocks = WaveformEditingTestAccess::liveBounds(f.waveform);
  for (const auto &block : blocks) {
    expect(block.body.getWidth() >= 176.0f, "live cards retain the minimum usable width");
    expectNear(block.body.getWidth(), blocks[0].body.getWidth(),
               "different musical durations have equal live block width");
    if (!block.body.intersects(WaveformEditingTestAccess::liveRow(f.waveform))) continue;
    for (const auto target : {block.jumpButton, block.loopButton, block.muteButton})
      expect(target.getWidth() >= 44.0f && target.getHeight() >= 44.0f,
             "live exposes three persistent full-size actions");
  }
  for (const int height : {108, 140}) {
    f.waveform.setSize(390, height);
    for (const auto &block : WaveformEditingTestAccess::liveBounds(f.waveform)) {
      if (!block.body.intersects(WaveformEditingTestAccess::liveRow(f.waveform))) continue;
      for (const auto target : {block.jumpButton, block.loopButton, block.muteButton})
        expect(target.getWidth() == 44.0f && target.getHeight() == 44.0f && block.body.contains(target),
               "compact live rows preserve all three complete touch targets");
    }
  }
  f.waveform.setSize(390, 250);
  expectNear(WaveformEditingTestAccess::liveProgress(f.waveform, 0), 0.5, "two-second section has relative progress");
  expectNear(WaveformEditingTestAccess::liveProgress(f.waveform, 2), -1.0, "pending destination is not the active section");
  position = 11.0;
  expectNear(WaveformEditingTestAccess::liveProgress(f.waveform, 1), 0.5, "eighteen-second section has relative progress");
  position = 2.0;
  expectNear(WaveformEditingTestAccess::liveProgress(f.waveform, 1), 0.0, "loop restart resets relative progress");
  position = 25.0;
  expectNear(WaveformEditingTestAccess::liveProgress(f.waveform, 2), 0.5, "confirmed jump activates destination progress");
  expect(WaveformEditingTestAccess::liveBounds(f.waveform)[0].body == blocks[0].body,
         "transport progress and jumps never move the live row");
  const auto background = blocks[0].body.getCentre().withY(blocks[0].body.getY() + 45.0f);
  f.click(background);
  expect(seeks == 0 && jumps == 0 && loops == 0, "live background taps cannot seek or act");
  const auto loopPoint = blocks[0].loopButton.getCentre();
  f.click(loopPoint);
  expect(loops == 1 && seeks == 0, "explicit live Loop uses existing callback");
  f.click(blocks[0].jumpButton.getCentre());
  expect(jumps == 1 && position == 25.0, "live Go requests a jump without seeking immediately");
  f.click(blocks[0].muteButton.getCentre());
  expect(WaveformEditingTestAccess::snapshot(f.waveform).parts[0].muteInstruments,
         "live Mute uses the existing instruments-only arrangement flag");
  f.waveform.mouseDown(pointerEvent(f.waveform, loopPoint, loopPoint));
  const auto dragged = loopPoint.translated(-140.0f, 0.0f);
  f.waveform.mouseDrag(pointerEvent(f.waveform, dragged, loopPoint, true));
  f.waveform.mouseUp(pointerEvent(f.waveform, dragged, loopPoint, true));
  expect(loops == 1 && jumps == 1 && seeks == 0, "drag starting on live control never triggers it or seek");
  WaveformEditingTestAccess::finishMotion(f.waveform);
  const float offset = WaveformEditingTestAccess::liveScroll(f.waveform);
  expect(offset > 0.0f, "live gesture manually scrolls blocks");
  for (const double next : {1.0, 11.0, 2.0, 25.0}) {
    position = next;
    WaveformEditingTestAccess::tick(f.waveform);
    expectNear(WaveformEditingTestAccess::liveScroll(f.waveform), offset,
               "recent interaction suspends centering across section transitions");
  }
  f.waveform.setLiveMode(false);
  expect(seeks == 0 && playing && position == 25.0 &&
             WaveformEditingTestAccess::visibleWindow(f.waveform) == normalWindow &&
             WaveformEditingTestAccess::followEnabled(f.waveform),
         "mode switch preserves musical position, playing state and normal viewport");
  expectNear(WaveformEditingTestAccess::snapshot(f.waveform).parts[0].endSec, 2.0,
             "live layout never changes real section duration");
  f.waveform.setLiveMode(true);
  const auto capturePath = juce::SystemStats::getEnvironmentVariable("PW_LIVE_CAPTURE_DIR", {});
  if (!capturePath.isEmpty()) {
    const juce::File directory(capturePath);
    expect(directory.createDirectory().wasOk(), "live capture directory opens");
    position = 11.0;
    for (const int width : {390, 768, 1200}) {
      f.waveform.setMinimalPhoneMode(width <= 390);
      f.waveform.setSize(width, 250);
      for (const bool live : {true, false}) {
        f.waveform.setLiveMode(live);
        auto stream = directory.getChildFile((live ? "live-" : "normal-") + juce::String(width) + ".png").createOutputStream();
        expect(stream && stream->setPosition(0) && stream->truncate().wasOk(), "live capture file opens");
        expect(juce::PNGImageFormat().writeImageToStream(f.waveform.createComponentSnapshot(f.waveform.getLocalBounds(), true, 2.0f), *stream),
               "both modes render real production components");
      }
    }
  }
}

void testLiveWaveformForOriginalsShiftedBeyondSource() {
  Fixture f;
  std::vector<float> bins(512);
  for (size_t i = 0; i < bins.size(); ++i)
    bins[i] = 0.2f + 0.6f * std::abs(std::sin(static_cast<float>(i) * 0.073f));
  f.waveform.setWaveformBins(bins);
  f.waveform.setTransportProviders([] { return -1.0; },
                                   [&] { return f.timelineLength; },
                                   [] { return false; });
  f.waveform.setMinimalPhoneMode(true);
  // Five integral-width cards keep sampling and antialiasing identical when
  // the source sections move to different card positions after insertion.
  f.waveform.setSize(1028, 250);
  f.waveform.setLiveMode(true);
  const auto beforeBlocks = WaveformEditingTestAccess::liveBounds(f.waveform);
  const auto before = f.waveform.createComponentSnapshot(f.waveform.getLocalBounds());
  f.waveform.setWaveformBins(std::vector<float>(bins.size(), 0.0f));
  const auto beforeSilent = f.waveform.createComponentSnapshot(f.waveform.getLocalBounds());
  f.waveform.setWaveformBins(bins);

  f.waveform.setLiveMode(false);
  f.waveform.setMinimalPhoneMode(false);
  f.waveform.triggerEditToggleFromMidi();
  juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  f.duplicate("Intro");
  f.duplicate("Intro");
  f.expectPosition("A", 30.0, 40.0);
  f.expectPosition("B", 40.0, 50.0);
  expectNear(WaveformEditingTestAccess::mappedSource(f.waveform, 35.0), 15.0,
             "displaced original A keeps its source coordinate");
  expectNear(WaveformEditingTestAccess::mappedSource(f.waveform, 45.0), 25.0,
             "displaced original B keeps its source coordinate");
  expect(f.part("A").sourcePartId.isEmpty() && f.part("B").sourcePartId.isEmpty(),
         "the displaced tail cards remain original sections rather than explicit source clips");
  f.waveform.setMinimalPhoneMode(true);
  f.waveform.setLiveMode(true);
  const auto afterBlocks = WaveformEditingTestAccess::liveBounds(f.waveform);
  const auto after = f.waveform.createComponentSnapshot(f.waveform.getLocalBounds());
  f.waveform.setWaveformBins(std::vector<float>(bins.size(), 0.0f));
  const auto silent = f.waveform.createComponentSnapshot(f.waveform.getLocalBounds());

  for (const auto indices : {std::pair<int, int>{1, 3}, {2, 4}}) {
    const auto originalBody = beforeBlocks[static_cast<size_t>(indices.first)].body;
    const auto shiftedBody = afterBlocks[static_cast<size_t>(indices.second)].body;
    expect(WaveformEditingTestAccess::liveRow(f.waveform).contains(shiftedBody),
           "the regression renders the complete displaced original card");
    expectNear(originalBody.getWidth(), shiftedBody.getWidth(),
               "source comparison uses the same rendering scale");
    int actualSignalPixels = 0;
    for (int x = 8; x < static_cast<int>(shiftedBody.getWidth()) - 8; ++x) {
      int originalTop = -1, originalBottom = -1;
      int shiftedTop = -1, shiftedBottom = -1;
      for (int y = 40; y < static_cast<int>(shiftedBody.getHeight()) - 48; ++y) {
        const int originalX = static_cast<int>(originalBody.getX()) + x;
        const int shiftedX = static_cast<int>(shiftedBody.getX()) + x;
        const int originalY = static_cast<int>(originalBody.getY()) + y;
        const int shiftedY = static_cast<int>(shiftedBody.getY()) + y;
        // The studio background varies across the viewport. Compare signal
        // masks against each position's own silent rendering instead of RGB.
        const bool originalSignal = before.getPixelAt(originalX, originalY) !=
                                    beforeSilent.getPixelAt(originalX, originalY);
        const bool shiftedSignal = after.getPixelAt(shiftedX, shiftedY) !=
                                   silent.getPixelAt(shiftedX, shiftedY);
        if (originalSignal) {
          if (originalTop < 0) originalTop = y;
          originalBottom = y;
        }
        if (shiftedSignal) {
          if (shiftedTop < 0) shiftedTop = y;
          shiftedBottom = y;
          ++actualSignalPixels;
        }
      }
      const bool bothEmpty = originalTop < 0 && shiftedTop < 0;
      const bool sameSilhouette = originalTop >= 0 && shiftedTop >= 0 &&
                                 std::abs(originalTop - shiftedTop) <= 1 &&
                                 std::abs(originalBottom - shiftedBottom) <= 1;
      // Position-dependent tint rounding can change an antialiased edge by
      // one pixel; every column must still preserve the waveform silhouette.
      expect(bothEmpty || sameSilhouette,
             "displaced original preserves its source waveform silhouette (section " +
                 std::to_string(indices.first) + ", column " + std::to_string(x) +
                 ", original " + std::to_string(originalTop) + ".." + std::to_string(originalBottom) +
                 ", shifted " + std::to_string(shiftedTop) + ".." + std::to_string(shiftedBottom) + ")");
    }
    expect(actualSignalPixels > 200,
           "each displaced original renders its supplied waveform bins and becomes flat with silent bins");
  }
}

void testLiveResponsiveCompleteCardsAndNavigation() {
  for (const int width : {568, 844, 956, 1024, 1200}) {
    for (const bool notchOnLeft : {true, false}) {
      const bool phone = width <= 956;
      WaveformBar wave;
      wave.setTransportProviders([] { return -1.0; }, [] { return 30.0; }, [] { return false; });
      wave.setSongSourceLengthProvider([] { return 30.0; });
      wave.setWaveformBins(std::vector<float>(512, 0.5f));
      SongArrangementSnapshot arrangement;
      for (int index = 0; index < 12; ++index) {
        const auto id = juce::String("live-section-") + juce::String(index);
        arrangement.parts.push_back(original(id.toRawUTF8(), index * 2.5, (index + 1) * 2.5));
      }
      wave.setArrangement(arrangement, false);
      wave.setMinimalPhoneMode(phone);
      wave.setSize(width, 220);
      const int cutout = width == 956 ? 62 : 44;
      const juce::BorderSize<int> insets(12, notchOnLeft ? cutout : 12,
                                         18, notchOnLeft ? 12 : cutout);
      wave.setPhoneSafeAreaInsets(insets);
      int seeks = 0, jumps = 0, loops = 0;
      wave.setSeekCallback([&](double) { ++seeks; });
      wave.setOnArrangementPartJumpRequest([&](int) { ++jumps; });
      wave.setOnArrangementPartLoopToggleRequest([&](int) { ++loops; });
      wave.setLiveMode(true);

      int alignmentCheck = 0;
      const auto expectCompleteCards = [&](const int expectedFirst) {
        const auto context = " (width " + std::to_string(width) +
                             ", notch " + (notchOnLeft ? "left" : "right") +
                             ", check " + std::to_string(++alignmentCheck) +
                             ", expected first " + std::to_string(expectedFirst) +
                             ", offset " + std::to_string(WaveformEditingTestAccess::liveScroll(wave));
        const auto row = WaveformEditingTestAccess::liveRow(wave);
        const auto safe = insets.subtractedFrom(wave.getLocalBounds()).toFloat();
        if (phone)
          expect(row == wave.getLocalBounds().toFloat(),
                 "phone live sections use the full surface, like the normal waveform");
        else
          expect(safe.contains(row), "desktop live viewport retains its safe-area layout");
        const auto cards = WaveformEditingTestAccess::liveBounds(wave);
        int first = -1, visible = 0;
        for (const auto &card : cards) {
          if (!card.body.intersects(row)) continue;
          if (first < 0) first = card.partIndex;
          ++visible;
          expect(card.body.getX() >= row.getX() - 0.001f &&
                     card.body.getRight() <= row.getRight() + 0.001f,
                 "settled live navigation leaves neither edge card clipped" + context +
                     ", actual first " + std::to_string(first) +
                     ", card " + std::to_string(card.partIndex) + ")");
          expect(card.body.getWidth() >= 176.0f,
                 "responsive live cards preserve their minimum usable width");
          expectNear(card.body.getWidth(), cards[0].body.getWidth(),
                     "all responsive live cards have equal width");
          for (const auto target : {card.jumpButton, card.loopButton, card.muteButton})
            expect(card.body.contains(target) && target.getWidth() == 44.0f && target.getHeight() == 44.0f,
                   "every visible card retains all complete touch actions inside its bounds");
          if (phone)
            for (const auto target : {card.jumpButton, card.loopButton, card.muteButton})
              expect(safe.contains(target),
                     "edge-to-edge phone sections keep every action clear of both cutout orientations");
        }
        expect(first == expectedFirst, "live navigation settles on the requested first section" +
                                          context + ", actual first " + std::to_string(first) + ")");
        expect(visible >= 2, "landscape viewport shows multiple complete cards");
        const auto &last = cards[static_cast<size_t>(first + visible - 1)];
        expect(std::abs(last.body.getRight() - row.getRight()) <= 0.001f,
               "the last complete visible card fills the visual viewport edge");
        return visible;
      };
      const auto expectEdgeWaveform = [&](const juce::String &name) {
        if (!phone) return;
        const auto withSignal = wave.createComponentSnapshot(wave.getLocalBounds());
        wave.setWaveformBins(std::vector<float>(512, 0.0f));
        const auto silent = wave.createComponentSnapshot(wave.getLocalBounds());
        wave.setWaveformBins(std::vector<float>(512, 0.5f));
        for (const int x : {0, width - 1}) {
          bool signalAtEdge = false;
          // Waveform columns have a two-point stride; their normal gaps can
          // occupy the outermost pixel. Check that stride at each physical edge.
          for (int offset = 0; offset <= 2; ++offset)
            for (int y = 70; y < 150; ++y) {
              const int column = x == 0 ? offset : x - offset;
              signalAtEdge |= withSignal.getPixelAt(column, y) != silent.getPixelAt(column, y);
            }
          expect(signalAtEdge, "real section waveform reaches both physical phone edges");
        }
        const auto capturePath = juce::SystemStats::getEnvironmentVariable("PW_LIVE_CAPTURE_DIR", {});
        if (width == 956 && capturePath.isNotEmpty()) {
          const juce::File directory(capturePath);
          expect(directory.createDirectory().wasOk(), "full-width live preview directory opens");
          auto stream = directory.getChildFile("live-full-width-" + name +
              (notchOnLeft ? "-left" : "-right") + ".png").createOutputStream();
          expect(stream && juce::PNGImageFormat().writeImageToStream(withSignal, *stream),
                 "full-width phone preview captures the real production component");
        }
      };
      const int visible = expectCompleteCards(0);
      expectEdgeWaveform("start");
      const auto initialCards = WaveformEditingTestAccess::liveBounds(wave);
      const float stride = initialCards[1].body.getX() - initialCards[0].body.getX();
      const auto row = WaveformEditingTestAccess::liveRow(wave);
      const auto point = row.getCentre().withY(row.getY() + 45.0f);
      juce::MouseWheelDetails smooth{};
      smooth.isSmooth = true;
      smooth.deltaX = -0.25f;
      wave.mouseWheelMove(pointerEvent(wave, point, point), smooth);
      wave.mouseDown(pointerEvent(wave, point, point));
      wave.mouseUp(pointerEvent(wave, point, point));
      expectNear(WaveformEditingTestAccess::liveScroll(wave), stride * 0.25f,
                 "tap interrupts wheel settle without moving targets to another card");
      const auto pan = [&](const float delta) {
        const auto dragged = point.translated(delta, 0.0f);
        wave.mouseDown(pointerEvent(wave, point, point));
        wave.mouseDrag(pointerEvent(wave, dragged, point, true));
        wave.mouseUp(pointerEvent(wave, dragged, point, true));
        WaveformEditingTestAccess::finishMotion(wave);
      };
      pan(-0.75f * stride);
      expectCompleteCards(1);
      pan(-0.25f * stride);
      expectCompleteCards(1);
      pan(0.75f * stride);
      expectCompleteCards(0);

      const int finalFirst = static_cast<int>(arrangement.parts.size()) - visible;
      WaveformEditingTestAccess::scrollLiveToEnd(wave);
      expectCompleteCards(finalFirst);
      expectEdgeWaveform("end");
      pan(0.75f * stride);
      expectCompleteCards(finalFirst - 1);
      smooth.deltaX = -0.75f;
      wave.mouseWheelMove(pointerEvent(wave, point, point), smooth);
      expect(WaveformEditingTestAccess::liveScroll(wave) > (finalFirst - 1) * stride &&
                 WaveformEditingTestAccess::liveScroll(wave) < finalFirst * stride,
             "smooth horizontal wheel navigation tracks intermediate finger movement");
      WaveformEditingTestAccess::settleLiveWheelScroll(wave);
      expectCompleteCards(finalFirst);

      pan(1000000.0f);
      expectCompleteCards(0);
      juce::MouseWheelDetails discrete{};
      discrete.isSmooth = false;
      discrete.deltaY = -0.1f;
      wave.mouseWheelMove(pointerEvent(wave, point, point), discrete);
      expectCompleteCards(1);
      WaveformEditingTestAccess::tick(wave);
      wave.mouseWheelMove(pointerEvent(wave, point, point), discrete);
      expectCompleteCards(2);
      discrete.deltaY = 0.1f;
      wave.mouseWheelMove(pointerEvent(wave, point, point), discrete);
      expectCompleteCards(1);
      expect(seeks == 0 && jumps == 0 && loops == 0,
             "live drags and both wheel types only navigate cards without transport actions");
      if (phone) {
        // Some rotations only exchange safe-area insets, without changing size.
        wave.setPhoneSafeAreaInsets({insets.getTop(), insets.getRight(),
                                    insets.getBottom(), insets.getLeft()});
        const auto rotatedSafe = juce::BorderSize<int>(insets.getTop(), insets.getRight(),
            insets.getBottom(), insets.getLeft()).subtractedFrom(wave.getLocalBounds()).toFloat();
        for (const auto &card : WaveformEditingTestAccess::liveBounds(wave)) {
          if (!card.body.intersects(WaveformEditingTestAccess::liveRow(wave))) continue;
          for (const auto target : {card.jumpButton, card.loopButton, card.muteButton})
            expect(rotatedSafe.contains(target),
                   "changing only cutout orientation relayouts complete accessible actions");
        }
      }
    }
  }
}

void testTopbarLiveToggleAndWaveformBackground() {
  ProcessorFixture fixture;
  auto &wave = fixture.edit.waveform;
  wave.setMinimalPhoneMode(true);
  int seeks = 0;
  wave.setSeekCallback([&](double) { ++seeks; });
  std::vector<float> bins(512);
  for (size_t i = 0; i < bins.size(); ++i)
    bins[i] = 0.15f + 0.5f * std::abs(std::sin(i * 0.13f)) * std::abs(std::cos(i * 0.033f));
  wave.setWaveformBins(bins);
  TopBar top(fixture.processor);
  ImportedSongData demo;
  demo.id = "isolated-live-preview";
  demo.title = "Demo de prueba";
  demo.artist = "Datos aislados";
  demo.key = "C";
  demo.bpm = 120;
  top.setTransportProviders([] { return false; }, [] { return 15.0; }, [] { return 30.0; },
                            [] { return juce::String("C"); }, [&] { return demo.id; }, [&] { return &demo; });
  wave.setTransportProviders([] { return 15.0; }, [] { return 30.0; }, [] { return false; });
  top.setLiveModeControls([&] { wave.setLiveMode(!wave.isLiveMode()); },
                          [&] { return wave.isLiveMode(); }, [&] { return wave.canToggleLiveMode(); });
  juce::Component preview;
  preview.addAndMakeVisible(top);
  preview.addAndMakeVisible(wave);
  const auto capturePath = juce::SystemStats::getEnvironmentVariable("PW_LIVE_CAPTURE_DIR", {});
  const auto capture = [&](const juce::String &name) {
    if (capturePath.isEmpty()) return;
    const juce::File directory(capturePath);
    expect(directory.createDirectory().wasOk(), "topbar preview directory opens");
    auto stream = directory.getChildFile(name + ".png").createOutputStream();
    expect(stream && stream->setPosition(0) && stream->truncate().wasOk(), "topbar preview opens");
    expect(juce::PNGImageFormat().writeImageToStream(preview.createComponentSnapshot(preview.getLocalBounds(), true, 2.0f), *stream),
           "topbar and waveform render together from production components");
  };
  for (const int width : {568, 700, 844, 1024, 1280}) {
    const bool phone = width <= 844;
    top.setMinimalPhoneMode(phone);
    wave.setMinimalPhoneMode(phone);
    preview.setSize(width, 244);
    top.setBounds(0, 0, width, 64);
    wave.setBounds(0, 64, width, 180);
    WaveformEditingTestAccess::expectTopbarLiveLayout(top);
    WaveformEditingTestAccess::clickLiveToggle(top);
    expect(wave.isLiveMode() && WaveformEditingTestAccess::topbarLiveSelected(top),
           "topbar toggle enters live and reflects its state");
    expect(WaveformEditingTestAccess::timelineCaption(top) == "Secciones", "timeline selection names the actual visual surface without LIVE");
    WaveformEditingTestAccess::chooseTimeline(top, true);
    expect(wave.isLiveMode(), "choosing the already selected presentation is idempotent");
    expect(!WaveformEditingTestAccess::waveMoreVisible(wave) &&
               WaveformEditingTestAccess::liveRow(wave).getY() < 4.0f,
           "live removes the duplicate menu and reclaims all header height");
    const auto withWave = wave.createComponentSnapshot(wave.getLocalBounds(), true, 1.0f);
    wave.setWaveformBins(std::vector<float>(512, 0.0f));
    const auto withoutWave = wave.createComponentSnapshot(wave.getLocalBounds(), true, 1.0f);
    bool waveformVisible = false;
    for (int y = 30; y < 120 && !waveformVisible; ++y)
      for (int x = 10; x < 170; ++x)
        if (withWave.getPixelAt(x, y) != withoutWave.getPixelAt(x, y)) { waveformVisible = true; break; }
    expect(waveformVisible, "live background visibly reflects actual waveform bins under the section tint");
    wave.setWaveformBins(bins);
    capture("topbar-live-" + juce::String(width));
    WaveformEditingTestAccess::clickLiveToggle(top);
    expect(WaveformEditingTestAccess::timelineCaption(top) == "Onda", "return selection names the normal waveform view");
    expect(!wave.isLiveMode() && !WaveformEditingTestAccess::topbarLiveSelected(top) &&
               WaveformEditingTestAccess::waveMoreVisible(wave),
           "topbar toggle returns to normal with its original waveform menu available");
    capture("topbar-normal-" + juce::String(width));
    expect(seeks == 0, "topbar mode changes never seek");
  }
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
  run("section actions avoid global waveform controls", testSectionActionsClearGlobalControls);
  run("live section progress and protected manual navigation", testLiveSectionsAndNavigation);
  run("desktop overflow deduplicates visible actions", testDesktopMoreDeduplicatesVisibleActions);
  run("Live titles remain invariant through scroll and inertia", testLiveTitleLayoutInvariantDuringScroll);
  run("section view states, accessibility tree and keyboard", testSectionViewStatesAccessibilityAndKeyboard);
  run("section view readable states and responsive previews", testSectionViewReadableStatesPreview);
  run("adaptive centered Live titles", testAdaptiveLiveTitles);
  run("live inertial navigation", testLiveInertialNavigation);
  run("desktop normal actions and discrete live follow", testDesktopNormalAndLiveFollow);
  run("live actions survive layout refresh", testLiveActionsSurviveLayoutRefresh);
  run("pending Live priority and combined state visuals", testPendingLivePriorityAndCombinedStates);
  run("Loop border phase and corner continuity", testLoopBorderPhaseAndCorners);
  run("live actions reach real transport", testLiveActionsReachRealTransport);
  run("Master leading boundary", testMasterLeadingBoundary);
  run("live originals preserve waveform beyond source duration", testLiveWaveformForOriginalsShiftedBeyondSource);
  run("live responsive complete cards and settled navigation", testLiveResponsiveCompleteCardsAndNavigation);
  run("topbar live toggle and real waveform background", testTopbarLiveToggleAndWaveformBackground);
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
