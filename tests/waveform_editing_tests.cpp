#include "ui/panels/WaveformBar.h"
#include "ui/panels/TopBar.h"
#include "ui/LiveLoopBorder.h"
#include "PluginProcessor.h"
#include "data/ApplicationDataPaths.h"
#include "data/AssetBootstrapManager.h"
#include "data/Localization.h"
#include "ui/components/ChannelStripCard.h"
#include "ui/components/MetronomePanel.h"

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
  static juce::Point<float> guideOrigin(WaveformBar &wave) {
    wave.rebuildGuideAlignmentMarkers(); return wave.guideOriginPill.getCentre();
  }
  static juce::Point<float> guideMarker(WaveformBar &wave, const juce::String &id) {
    wave.rebuildGuideAlignmentMarkers();
    for (const auto &marker : wave.guideAlignmentMarkers) if (marker.id == id) return marker.pill.getCentre();
    throw std::runtime_error("guide marker missing from viewport");
  }
  static int guideMarkerCount(WaveformBar &wave) { wave.rebuildGuideAlignmentMarkers(); return int(wave.guideAlignmentMarkers.size()); }
  static juce::Rectangle<float> guideMarkerPill(WaveformBar &wave, const juce::String &id) {
    wave.rebuildGuideAlignmentMarkers();
    for (const auto &marker : wave.guideAlignmentMarkers) if (marker.id == id) return marker.pill;
    throw std::runtime_error("guide label missing from viewport");
  }
  static juce::Point<float> guideSecondaryPoint(WaveformBar &wave, const juce::String &id) {
    wave.rebuildGuideAlignmentMarkers();
    for (const auto &marker : wave.guideAlignmentMarkers)
      if (marker.id == id && marker.secondary) return marker.point;
    throw std::runtime_error("secondary guide point missing from viewport");
  }
  static juce::String guideHitId(WaveformBar &wave, juce::Point<float> position) {
    wave.rebuildGuideAlignmentMarkers();
    const int hit = wave.hitGuideAlignmentMarker(position);
    return hit >= 0 ? wave.guideAlignmentMarkers[size_t(hit)].id : juce::String{};
  }
  static auto &guideAccept(WaveformBar &wave) { return wave.guideAlignmentAcceptButton; }
  static auto &guideCancel(WaveformBar &wave) { return wave.guideAlignmentCancelButton; }
  static auto &guideRename(WaveformBar &wave) { return wave.guideNameEditor; }
  static auto guideCatalogItems(WaveformBar &wave, const juce::String &id) {
    WaveformBar::GuideCatalogMenuItems items;
    const auto menu = wave.buildGuideCatalogMenu(id, items);
    expect(menu.getNumItems() >= 3, "catalogue picker includes the existing marker families");
    return items;
  }
  static auto guideSectionAddItems(WaveformBar &wave) {
    WaveformBar::GuideCatalogMenuItems items;
    const auto menu = wave.buildGuideCatalogMenu({}, items, true);
    expect(menu.getNumItems() > 0, "the manual section picker uses the native section catalogue");
    return items;
  }
  static void guideSectionAdd(WaveformBar &wave,
      const playworship::analysis::GuideCatalogChoice &choice, double seconds) {
    wave.dispatchGuideSectionAdd(choice, seconds);
  }
  static void guideCatalogChoose(WaveformBar &wave, const juce::String &id,
      const playworship::analysis::GuideCatalogChoice &choice) {
    wave.dispatchGuideCatalogSelection(id, choice);
  }
  static bool guidePendingPoint(WaveformBar &wave, const juce::String &id) {
    wave.rebuildGuideAlignmentMarkers();
    for (const auto &marker : wave.guideAlignmentMarkers)
      if (marker.id == id) return marker.pending && marker.secondary && marker.point.x == marker.x;
    return false;
  }
  static void guideDiscard(WaveformBar &wave, const juce::String &id) {
    if (wave.onGuideAlignmentRemove) wave.onGuideAlignmentRemove(id);
  }
  static auto &suggestSections(WaveformBar &wave) { return wave.suggestSectionsButton; }
  static juce::String bpmText(TopBar &bar) {
    bar.updateBpmDisplay();
    return bar.bpmSegmentButton.getButtonText();
  }
  static juce::TextEditor &tempoEditor(WaveformBar &bar) { return bar.tempoBpmEditor; }
  static const SongTempoMapSnapshot &tempoMap(const WaveformBar &bar) { return bar.tempoMap; }
  static void selectTempoRegion(WaveformBar &bar, int index) { bar.selectTempoRegion(index); }
  static bool hasNoWaveform(const WaveformBar &bar) { return bar.waveformBins.empty(); }
  static bool blankEditEnabled(const WaveformBar &bar) { return bar.editButton.isEnabled(); }

  static bool isEditing(const WaveformBar &waveform) {
    return waveform.isEditingArrangement();
  }
  static void clickLiveToggle(TopBar &bar) {
    if (bar.sectionsViewButton.isVisible()) {
      auto &button = bar.liveModeState && bar.liveModeState() ? bar.waveformViewButton : bar.sectionsViewButton;
      expect(button.isEnabled(), "desktop view selector is enabled");
      button.onClick();
      return;
    }
    auto menu = bar.createMorePopoverContent();
    auto *toggle = dynamic_cast<juce::Button *>(menu->findChildWithID("timeline-sections"));
    expect(toggle && toggle->isEnabled(), "section menu toggle is enabled");
    toggle->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  }
  static void expectTopbarLiveLayout(TopBar &bar) {
    const bool desktop = !useTouchOptimisedControls() && !bar.minimalPhoneMode;
    expect(bar.tabletMoreButton.isVisible() != desktop, "only touch layouts retain More");
    expect(bar.sectionsViewButton.isVisible() == desktop && bar.helpButton.isVisible() == desktop,
           "desktop exposes view selection and help directly");
    for (int i = 0; i < bar.getNumChildComponents(); ++i) {
      const auto *child = bar.getChildComponent(i);
      if (!child->isVisible() || child->getBounds().isEmpty()) continue;
      expect(bar.getLocalBounds().contains(child->getBounds()), "visible controls fit topbar");
      if (child != &bar.songPickerButton)
        expect(!child->getBounds().intersects(bar.songInfoBounds), "controls never overlap the song title");
      for (int j = i + 1; j < bar.getNumChildComponents(); ++j) {
        const auto *other = bar.getChildComponent(j);
        if (other->isVisible()) expect(!child->getBounds().intersects(other->getBounds()), "topbar controls do not overlap");
      }
    }
    expect(bar.songInfoBounds.getWidth() >= 80,
           "song title retains usable width (bar=" + std::to_string(bar.getWidth()) +
               ", title=" + std::to_string(bar.songInfoBounds.getWidth()) + ")");
    if (desktop) {
      expect(bar.waveformViewButton.getToggleState() != bar.sectionsViewButton.getToggleState(), "exactly one view is selected");
      expect(bar.sectionsViewButton.getToggleState() == (bar.liveModeState && bar.liveModeState()), "desktop state follows the real view");
      expect(bar.getLearningTargetBounds(playworship::learning::Anchor::More) == bar.sectionsViewButton.getBounds(), "demo guidance targets Sections directly");
      return;
    }
    expect(bar.getLocalBounds().contains(bar.tabletMoreButton.getBounds()), "More fits topbar");
    auto panel = bar.createMorePopoverContent();
    auto *toggle = dynamic_cast<juce::Button *>(panel->findChildWithID("timeline-sections"));
    expect(toggle && toggle->getWidth() >= 44 && toggle->getHeight() >= 44, "menu option keeps accessible touch target");
    expect(toggle->getTitle().isNotEmpty() && toggle->getDescription().isNotEmpty(), "menu option exposes accessible name and state");
    expect(toggle->getToggleState() == (bar.liveModeState && bar.liveModeState()), "menu check reflects existing presentation state");
  }
  static auto moreBounds(TopBar &bar) { return bar.tabletMoreButton.getBounds(); }
  static auto songBounds(TopBar &bar) { return bar.songInfoBounds; }
  static juce::Button &waveformChoice(TopBar &bar) { return bar.waveformViewButton; }
  static juce::Button &sectionsChoice(TopBar &bar) { return bar.sectionsViewButton; }
  static juce::Button &help(TopBar &bar) { return bar.helpButton; }
  static juce::Button &settings(TopBar &bar) { return bar.settingsButton; }
  static juce::Button &midi(TopBar &bar) { return bar.midiLearnButton; }
  static bool timeVisible(TopBar &bar) { return bar.timeDisplay.isVisible(); }
  static bool midiActive(TopBar &bar) { return bar.midiLearnActive; }
  static void refreshTimeline(TopBar &bar) { bar.refreshTimelineMenu(); }
  static auto morePanel(TopBar &bar) { return bar.createMorePopoverContent(); }
  static bool directMidi(const TopBar &bar) { return bar.midiLearnButton.isVisible(); }
  static bool directSettings(const TopBar &bar) { return bar.settingsButton.isVisible(); }
  static void midiEnabled(TopBar &bar, bool enabled) { bar.midiLearnButton.setEnabled(enabled); if (bar.refreshMorePopover) bar.refreshMorePopover(); }
  static void moreRefresh(TopBar &bar) { if (bar.refreshMorePopover) bar.refreshMorePopover(); }
  static bool topbarLiveSelected(const TopBar &bar) { return bar.liveModeState && bar.liveModeState(); }
  static void chooseTimeline(TopBar &bar, bool sections) { bar.selectTimelineView(sections); }
  static auto timelineCaption(const TopBar &bar) { return bar.liveModeState && bar.liveModeState() ? juce::String("Secciones") : juce::String("Onda"); }
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
  static void followAt(WaveformBar &waveform, double now, bool reduced = false) { waveform.updateLiveFollow(now, reduced); }
  static void vblank(WaveformBar &waveform, double now) { waveform.updatePlaybackAnimation(now); }
  static auto playbackRepaintRegions(const WaveformBar &waveform, float from, float to) {
    return waveform.getPlaybackRepaintRegions(from, to);
  }
  static void fixedPlaybackViewport(WaveformBar &waveform, float start, float span) {
    waveform.followPlaybackEnabled = false;
    waveform.setVisibleWindow(start, span);
  }
  static juce::Image playbackWaveformCache(const WaveformBar &waveform) {
    return waveform.followWaveformBaseCache;
  }
  static void invalidatePlaybackWaveformCache(WaveformBar &waveform) {
    waveform.invalidateWaveformRenderCaches();
  }
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
    waveform.sectionInsertionAnchor.valid = true;
    waveform.sectionInsertionAnchor.seconds = seconds;
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
  static juce::Button &nativeCueButton(WaveformBar &waveform, const juce::String &id) {
    waveform.rebuildCueMarkers();
    for (size_t i = 0; i < waveform.cueMarkerBounds.size(); ++i) {
      const auto &marker = waveform.cueMarkerBounds[i];
      if (marker.id != id) continue;
      const auto point = juce::Point<float>{marker.pointX, marker.y};
      auto &button = *waveform.cueMarkerButtons[i];
      expect(!marker.hit.isEmpty() && marker.hit.contains(point) &&
                 waveform.hitTestCueMarker(point) == int(i),
             "each saved cue retains an independent waveform hit target");
      expect(button.isVisible() && button.isEnabled() &&
                 button.getBounds().contains(point.toInt()) &&
                 waveform.getComponentAt(point.toInt()) == &button,
             "the native cue button remains visible and receives its point gesture");
      return button;
    }
    throw std::runtime_error("missing native cue button");
  }
  static juce::Rectangle<float> cueLabel(WaveformBar &waveform, const juce::String &id) {
    waveform.rebuildCueMarkers();
    for (const auto &marker : waveform.cueMarkerBounds)
      if (marker.id == id) return marker.label;
    throw std::runtime_error("missing saved cue label");
  }
  static double cueSourceX(WaveformBar &waveform, const juce::String &id) {
    waveform.rebuildCueMarkers();
    for (const auto &marker : waveform.cueMarkerBounds)
      if (marker.id == id) return marker.x;
    throw std::runtime_error("missing saved cue temporal reference");
  }
  static void clickNativeCue(WaveformBar &waveform, const juce::String &id) {
    juce::Component &button = nativeCueButton(waveform, id);
    const auto point = cuePoint(waveform, id);
    button.mouseDown(pointerEvent(waveform, point, point).getEventRelativeTo(&button));
    button.mouseUp(pointerEvent(waveform, point, point).getEventRelativeTo(&button));
    expect(waveform.selectedCueId == id, "a native point click selects only its saved cue");
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
    waveform.sectionInsertionAnchor.valid = true; waveform.sectionInsertionAnchor.seconds = seconds;
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
    expect(waveform.sectionInsertionAnchor.valid,
           "the ruler gesture establishes a section insertion anchor (editing=" +
               std::to_string(waveform.isEditingArrangement()) + ", map=" +
               std::to_string(static_cast<int>(waveform.timelineViewMode)) +
               ", length=" + std::to_string(waveform.getSongLengthSeconds()) + ")");
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
    // Prefer the lower body, then the area below the section toolbar. A dense
    // guide arrangement may legitimately fill the available lower cue rows.
    waveform.rebuildCueMarkers();
    for (const float y : {bounds.getBottom() - 20.0f, bounds.getCentreY(),
                          bounds.getY() + 60.0f, bounds.getY() + 16.0f}) {
      for (const float ratio : {0.5f, 0.75f, 0.25f}) {
        const juce::Point<float> point(bounds.getX() + bounds.getWidth() * ratio, y);
        const auto hit = waveform.hitTestArrangementPart(point);
        if (waveform.hitTestCueMarker(point) < 0 &&
            hit.partIndex == waveform.findPartIndexById(id) &&
            !hit.leftEdge && !hit.rightEdge && !hit.deleteButton && !hit.loopButton &&
            !hit.jumpButton && !hit.duplicateButton && !hit.addButton && !hit.muteButton &&
            !hit.overflowButton && !hit.skipButton && !hit.automateButton)
          return point;
      }
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
    // MIDI posts the real button click to JUCE's message queue. Processor
    // startup can keep that queue busy beyond the initial dispatch interval.
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
    while (!WaveformEditingTestAccess::isEditing(waveform) &&
           juce::Time::getMillisecondCounterHiRes() < deadline)
      juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    expect(WaveformEditingTestAccess::isEditing(waveform),
           "the real Edit action completes before arrange gestures begin");
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
  juce::MouseWheelDetails wheel{};
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
    WaveformEditingTestAccess::expectNoDesktopPlaybackActions(f.waveform);
    f.waveform.setLiveMode(true);
    const auto sections = WaveformEditingTestAccess::liveBounds(f.waveform);
    expect(!sections[1].jumpButton.isEmpty() && !sections[1].loopButton.isEmpty() &&
               !sections[1].muteButton.isEmpty(), "Sections retains all playback actions");
    f.waveform.setLiveMode(false);
    WaveformEditingTestAccess::expectNoDesktopPlaybackActions(f.waveform);
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
  WaveformEditingTestAccess::expectNoDesktopPlaybackActions(f.waveform);
  const auto button = overlay.body.getCentre().withY(overlay.body.getBottom() - 24.0f);
  f.waveform.mouseDown(pointerEvent(f.waveform, button, button));
  f.waveform.mouseUp(pointerEvent(f.waveform, button, button));
  expect(loops == 0 && jumps == 0 && seeks == 2,
         "former action lane is ordinary waveform: tap seeks without musical actions");
  f.waveform.mouseDown(pointerEvent(f.waveform, button, button));
  const auto buttonDrag = button.translated(35.0f, 0.0f);
  f.waveform.mouseDrag(pointerEvent(f.waveform, buttonDrag, button, true));
  f.waveform.mouseUp(pointerEvent(f.waveform, buttonDrag, button, true));
  expect(loops == 0 && jumps == 0 && seeks == 2,
         "drag in former action lane only pans and never seeks or acts");
}

void testSectionActionsClearGlobalControls() {
  Fixture f;
  f.waveform.setMinimalPhoneMode(true);
  for (const bool phone : {true, false}) {
    f.waveform.setMinimalPhoneMode(phone);
    for (const int width : {360, 390, 768, 1200}) {
      f.waveform.setSize(width, 250);
      WaveformEditingTestAccess::expectPlaybackActionsClearGlobalControls(f.waveform);
      WaveformEditingTestAccess::expectNoDesktopPlaybackActions(f.waveform);
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

juce::TextEditor *findTempoEditor(juce::Component &parent) {
  for (auto *child : parent.getChildren()) {
    if (auto *editor = dynamic_cast<juce::TextEditor *>(child))
      return editor;
    if (auto *editor = findTempoEditor(*child))
      return editor;
  }
  return nullptr;
}

void typeTempo(juce::TextEditor &editor, const juce::String &value) {
  editor.selectAll();
  editor.insertTextAtCaret(value);
  expect(editor.getText() == value,
         "the actual tempo field accepts every digit and decimal separator");
}

void testTopbarDecimalTempoEditing() {
  ProcessorFixture fixture;
  struct TempoHostEditor final : juce::AudioProcessorEditor {
    explicit TempoHostEditor(MTControlsAudioProcessor &processor)
        : juce::AudioProcessorEditor(processor) {}
  };
  TempoHostEditor host(fixture.processor);
  TopBar bar(fixture.processor);
  host.setSize(1200, 700);
  host.addAndMakeVisible(bar);
  bar.setBounds(0, 0, 1200, 80);
  fixture.processor.setPlaybackTempoBpm(120.125);
  expect(WaveformEditingTestAccess::bpmText(bar) == "120.125",
         "the main tempo button displays the fractional engine value");

  const auto open = [&]() -> juce::TextEditor & {
    bar.triggerMetronomePopoverFromMidi();
    auto *editor = findTempoEditor(host);
    expect(editor != nullptr, "opening the real BPM popover exposes its text field");
    expect(editor->getKeyboardType() == juce::TextInputTarget::decimalKeyboard,
           "the player requests the platform decimal keyboard");
    return *editor;
  };
  auto &editor = open();
  expect(editor.getText() == "120.125",
         "opening the BPM field preserves the displayed fractional value");
  typeTempo(editor, "120.5");
  editor.onReturnKey();
  expectNear(fixture.processor.getPlaybackTempoBpm(), 120.5,
             "Return submits a point-separated decimal to the real audio processor");
  expect(WaveformEditingTestAccess::bpmText(bar) == "120.5" && editor.getText() == "120.5",
         "the button and field agree after decimal submission");
  typeTempo(editor, "121,375");
  editor.onFocusLost();
  expectNear(fixture.processor.getPlaybackTempoBpm(), 121.375,
             "focus loss submits a comma-separated decimal without truncation");
  editor.onFocusLost();
  expectNear(fixture.processor.getPlaybackTempoBpm(), 121.375,
             "repeated focus loss cannot round the current tempo");
  for (const auto *invalid : {"121..5", "121,5.2", "", "."}) {
    typeTempo(editor, invalid);
    editor.onReturnKey();
    expectNear(fixture.processor.getPlaybackTempoBpm(), 121.375,
               "an invalid decimal cannot partially replace the playback tempo");
    expect(editor.getText() == "121.375", "invalid playback input restores the current value");
  }
  typeTempo(editor, "1.5");
  editor.onReturnKey();
  expectNear(fixture.processor.getPlaybackTempoBpm(), 90.0,
             "decimal input retains the engine minimum of 0.75 times the original BPM");
  typeTempo(editor, "999.5");
  editor.onReturnKey();
  expectNear(fixture.processor.getPlaybackTempoBpm(), 240.0,
             "decimal input retains the engine maximum of twice the original BPM");
  typeTempo(editor, "120.123456");
  editor.onReturnKey();
  expect(std::abs(fixture.processor.getPlaybackTempoBpm() - 120.123456) < 1.0e-9,
         "the player accepts a full six-place fractional BPM");
  dismissActiveActionPopover();
  juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  auto &reopened = open();
  expect(reopened.getText() == "120.123456",
         "closing and reopening the BPM editor retains all displayed decimal places");
  reopened.onFocusLost();
  expectNear(fixture.processor.getPlaybackTempoBpm(), 120.123456,
             "opening and leaving the field keeps the engine tempo unchanged");
  dismissActiveActionPopover();
  juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

void testMetronomePopoverDecimalTempoEditing() {
  double bpm = 120.125;
  int changes = 0;
  MetronomePanel::MetronomePopover panel("4/4", [] {}, [](const auto &) {},
      [&](double value) { bpm = value; ++changes; },
      [&](double delta) { bpm += delta; }, [&] { return bpm; });
  auto *editor = findTempoEditor(panel);
  expect(editor != nullptr && editor->getText() == "120.125",
         "the metronome popover opens with its exact fractional BPM");
  expect(editor->getKeyboardType() == juce::TextInputTarget::decimalKeyboard,
         "the metronome field requests the decimal keyboard");
  typeTempo(*editor, "120,75");
  editor->onReturnKey();
  expectNear(bpm, 120.75, "metronome Return accepts comma-separated decimals");
  typeTempo(*editor, "121.625");
  editor->onFocusLost();
  expectNear(bpm, 121.625, "metronome focus loss keeps point-separated decimals");
  const int validChanges = changes;
  for (const auto *invalid : {"121..5", "121,5.2", "", "."}) {
    typeTempo(*editor, invalid);
    editor->onReturnKey();
    expectNear(bpm, 121.625, "invalid metronome input leaves the previous tempo intact");
    expect(changes == validChanges && editor->getText() == "121.625",
           "invalid metronome input neither submits a prefix nor clears the display");
  }
}

void testRegionalDecimalTempoEditing() {
  Fixture fixture;
  auto &wave = fixture.waveform;
  SongTempoMapSnapshot map;
  SongTempoRegionSnapshot first;
  first.id = "fractional-first";
  first.startSec = 0.0;
  first.endSec = 15.0;
  first.bpm = 120.125;
  first.timeSignature = "4/4";
  auto second = first;
  second.id = "fractional-second";
  second.startSec = 15.0;
  second.endSec = 30.0;
  second.bpm = 96.75;
  map.regions = {first, second};
  wave.setTempoMap(map);
  SongTempoMapSnapshot published;
  wave.setOnTempoMapChanged([&](const auto &updated) { published = updated; });
  auto &editor = WaveformEditingTestAccess::tempoEditor(wave);
  expect(editor.getText() == "120.125", "regional tempo selection shows its fractional BPM");
  expect(editor.getKeyboardType() == juce::TextInputTarget::decimalKeyboard,
         "the regional tempo field requests the platform decimal keyboard");
  typeTempo(editor, "123,625");
  editor.onReturnKey();
  expect(published.regions.size() == 2, "regional Return publishes the existing tempo map");
  expectNear(published.regions[0].bpm, 123.625,
             "regional tempo submission preserves a comma-separated decimal");
  expectNear(published.regions[1].bpm, 96.75,
             "editing one regional tempo preserves the other region");
  WaveformEditingTestAccess::selectTempoRegion(wave, 1);
  expect(editor.getText() == "96.75", "switching regions retains each exact fractional value");
  WaveformEditingTestAccess::selectTempoRegion(wave, 0);
  expect(editor.getText() == "123.625", "returning to an edited region retains its decimal tempo");
  editor.onFocusLost();
  expectNear(WaveformEditingTestAccess::tempoMap(wave).regions[0].bpm, 123.625,
             "regional focus loss cannot round an unchanged fractional value");
  for (const auto *invalid : {"123..5", "123,5.2", "", "."}) {
    typeTempo(editor, invalid);
    editor.onReturnKey();
    expectNear(WaveformEditingTestAccess::tempoMap(wave).regions[0].bpm, 123.625,
               "invalid regional decimal input cannot replace a valid tempo with a prefix");
  }
}

void testDesktopToolbarPreservesActionsAndTitle() {
  ProcessorFixture f; TopBar bar(f.processor);
  bool sections = false, enabled = true;
  int changes = 0, helpRequests = 0, settingsRequests = 0, midiRequests = 0;
  bar.setLiveModeControls([&] { sections = !sections; ++changes; }, [&] { return sections; }, [&] { return enabled; });
  bar.setOnLearningRequested([&] { ++helpRequests; });
  bar.setOnSettings([&] { ++settingsRequests; });
  bar.setOnMidiLearnModeChanged([&](bool) { ++midiRequests; });
  ImportedSongData song; song.id = "desktop-toolbar";
  song.title = juce::String::fromUTF8("Una canción con un título especialmente largo");
  song.artist = "Datos aislados";
  bar.setTransportProviders([] { return false; }, [] { return 15.0; }, [] { return 30.0; },
      [] { return juce::String("C"); }, [&] { return song.id; }, [&] { return &song; });
  for (auto profile : {ResponsiveLayoutProfile::Compact, ResponsiveLayoutProfile::Standard, ResponsiveLayoutProfile::Large}) {
    bar.setResponsiveProfile(profile);
    for (int width : {940, 956, 1024, 1100, 1120, 1280, 1600}) {
      bar.setSize(width, 64);
      WaveformEditingTestAccess::expectTopbarLiveLayout(bar);
      expect(WaveformEditingTestAccess::midi(bar).isVisible() && WaveformEditingTestAccess::settings(bar).isVisible() && WaveformEditingTestAccess::timeVisible(bar), "MIDI, Settings and Song window remain directly available");
      const auto title = WaveformEditingTestAccess::songBounds(bar);
      WaveformEditingTestAccess::sectionsChoice(bar).onClick();
      expect(sections && WaveformEditingTestAccess::sectionsChoice(bar).getToggleState(), "Sections button selects the real view");
      const int before = changes; WaveformEditingTestAccess::sectionsChoice(bar).onClick();
      expect(changes == before, "selected view is idempotent");
      WaveformEditingTestAccess::waveformChoice(bar).onClick();
      expect(!sections && WaveformEditingTestAccess::waveformChoice(bar).getToggleState(), "Waveform button restores the real view");
      expect(title == WaveformEditingTestAccess::songBounds(bar), "view changes keep the title in place");
      enabled = false; WaveformEditingTestAccess::refreshTimeline(bar); WaveformEditingTestAccess::sectionsChoice(bar).onClick();
      expect(!sections && !WaveformEditingTestAccess::sectionsChoice(bar).isEnabled() && !WaveformEditingTestAccess::waveformChoice(bar).isEnabled(), "editing lock disables both direct choices without changing view");
      enabled = true; sections = true; WaveformEditingTestAccess::refreshTimeline(bar);
      expect(WaveformEditingTestAccess::sectionsChoice(bar).getToggleState(), "external view updates reconcile the selection");
      WaveformEditingTestAccess::waveformChoice(bar).onClick();
    }
  }
  WaveformEditingTestAccess::help(bar).onClick(); expect(helpRequests == 1, "question mark opens the existing learning catalog once");
  WaveformEditingTestAccess::settings(bar).onClick(); expect(settingsRequests == 1, "direct Settings keeps its callback");
  WaveformEditingTestAccess::midi(bar).triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  expect(midiRequests == 1 && WaveformEditingTestAccess::midiActive(bar), "direct MIDI Learn preserves toggle behavior");
  WaveformEditingTestAccess::midi(bar).triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  expect(midiRequests == 2 && !WaveformEditingTestAccess::midiActive(bar), "MIDI Learn can leave learn mode");
  for (auto *button : {&WaveformEditingTestAccess::waveformChoice(bar), &WaveformEditingTestAccess::sectionsChoice(bar), &WaveformEditingTestAccess::help(bar)})
    expect(button->getTitle().isNotEmpty() && button->getTooltip().isNotEmpty(), "direct controls have accessible names and help");
  bar.setMinimalPhoneMode(true); bar.setSize(700, 64);
  WaveformEditingTestAccess::expectTopbarLiveLayout(bar);
  auto panel = WaveformEditingTestAccess::morePanel(bar);
  expect(panel->findChildWithID("timeline-sections") && panel->findChildWithID("midi-learn")->isVisible(), "phone preserves its original menu actions");
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
  go->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
  expect(jumps == 2 && !goHandler->getCurrentState().isFocusable(),
         "hidden section actions cannot activate by keyboard or expose accessible focus");
  wave.setLiveMode(true);
  expect(go->isVisible(), "returning to Sections restores action surfaces");
  wave.setLiveMode(false);
  expect(!go->isVisible(), "repeated view switching clears every action surface");
  for (const bool phone : {false, true}) {
    wave.setMinimalPhoneMode(phone); wave.setSize(phone ? 568 : 1280, 180);
    wave.setLiveMode(true); WaveformEditingTestAccess::setLiveOffset(wave, 0.0f);
    go = WaveformEditingTestAccess::sectionAction(wave, "Intro", 0);
    auto *handler = go->getAccessibilityHandler();
    expect(go->isVisible() && handler != nullptr, "both desktop and phone restore accessible section actions");
    wave.setLiveMode(false);
    const int before = jumps;
    handler->getActions().invoke(juce::AccessibilityActionType::press);
    go->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    expect(!go->isVisible() && handler->getCurrentState().isIgnored() &&
               !handler->getCurrentState().isFocusable() && jumps == before,
           "Onda hides and disables section actions for accessibility and keyboard on both layouts");
    WaveformEditingTestAccess::expectNoDesktopPlaybackActions(wave);
  }
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

void testStableSectionIndicatorMapping() {
  WaveformBar wave;
  wave.setSize(768, 250);
  wave.setTransportProviders([] { return 0.0; }, [] { return 30.0; }, [] { return false; });
  SongArrangementPartSnapshot longChorus, shortChorus;
  longChorus.id = "long"; longChorus.name = "Coro"; longChorus.startSec = 10; longChorus.endSec = 30;
  shortChorus = longChorus; shortChorus.id = "short"; shortChorus.endSec = 20;
  wave.setArrangement({{longChorus, shortChorus}}, true);
  expect(wave.getArrangementPartId(0) == "long" && wave.getArrangementPartIndex("short") == 1,
         "real UI retains same-start order while resolving occurrences by ID");
  wave.setActiveArrangementLoopPartProvider([&] { return wave.getArrangementPartIndex("long"); });
  wave.setArrangementControlFeedbackProviders(
      [](const juce::String &id) { return id == "short" ? juce::String("Rejected") : juce::String{}; },
      [](const juce::String &) { return false; });
  wave.setLiveMode(true);
  expect(WaveformEditingTestAccess::sectionState(wave, 0).contains("Loop") &&
         WaveformEditingTestAccess::sectionState(wave, 1).contains(L10n::tr("No aplicado", "Not applied")),
         "confirmed Loop and rejection feedback belong to their own cards");
  wave.setArrangementControlFeedbackProviders(
      [](const juce::String &id) { return id == "short" ? juce::String("Update the server app to control sections safely") : juce::String{}; },
      [](const juce::String &) { return false; },
      [](const juce::String &) { return L10n::tr("Actualiza servidor", "Update server"); });
  expect(WaveformEditingTestAccess::sectionState(wave, 1).contains(L10n::tr("Actualiza servidor", "Update server")),
         "update requirement is visible in the real card, not only in a tooltip");
  const auto capture = juce::SystemStats::getEnvironmentVariable("PW_LIVE_CAPTURE_DIR", {});
  if (capture.isNotEmpty()) {
    const juce::File directory(capture); expect(directory.createDirectory().wasOk(), "section feedback capture directory");
    auto stream = directory.getChildFile("remote-section-update-required.png").createOutputStream();
    expect(stream != nullptr && juce::PNGImageFormat().writeImageToStream(
        wave.createComponentSnapshot(wave.getLocalBounds(), true, 2.0f), *stream), "section update feedback capture");
  }
  shortChorus.id = longChorus.id;
  wave.setArrangement({{longChorus, shortChorus}}, true);
  expect(wave.getArrangementPartId(0).isEmpty() && wave.getArrangementPartIndex("long") == -1,
         "real UI rejects ambiguous IDs rather than selecting the first occurrence");
}

void testLiveActionsReachRealTransport() {
  ProcessorFixture fixture;
  auto &wave = fixture.edit.waveform;
  auto &processor = fixture.processor;
  processor.setArrangementEditingActive(false);
  wave.setMinimalPhoneMode(true);
  wave.setSize(390, 250);
  wave.setLiveMode(true);
  wave.setOnArrangementPartJumpRequest([&](int index) { processor.requestArrangementPartJumpById(wave.getArrangementPartId(index)); });
  wave.setOnArrangementPartLoopToggleRequest([&](int index) {
    processor.toggleArrangementPartLoopById(wave.getArrangementPartId(index));
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
    juce::MouseWheelDetails wheel{};
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

void testReducedMotionLiveFollow() {
  Fixture f;
  f.waveform.setMinimalPhoneMode(true);
  f.waveform.setSize(390, 250);
  f.waveform.setTransportProviders([] { return 25.0; }, [] { return 30.0; }, [] { return true; });
  f.waveform.setLiveMode(true);
  WaveformEditingTestAccess::followAt(f.waveform, 100.0, true);
  const auto end = WaveformEditingTestAccess::liveScroll(f.waveform);
  expect(end > 0.0f, "reduced motion centers final section immediately");
  const auto blocks = WaveformEditingTestAccess::liveBounds(f.waveform);
  expectNear(blocks.back().body.getRight(), WaveformEditingTestAccess::liveRow(f.waveform).getRight(),
             "reduced motion clamps final section without trailing space");
  WaveformEditingTestAccess::followAt(f.waveform, 240.0, true);
  expectNear(WaveformEditingTestAccess::liveScroll(f.waveform), end,
             "reduced motion has no delayed centering animation");
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

void testSectionToggleMoreMenu() {
  ProcessorFixture fixture; TopBar top(fixture.processor);
  bool sections = false, enabled = true;
  int changes = 0;
  top.setLiveModeControls([&] { sections = !sections; ++changes; }, [&] { return sections; }, [&] { return enabled; });
  top.setMinimalPhoneMode(true); top.setSize(700, 64);
  auto panel = WaveformEditingTestAccess::morePanel(top);
  auto *toggle = dynamic_cast<juce::Button *>(panel->findChildWithID("timeline-sections"));
  expect(toggle && !toggle->getToggleState(), "normal waveform starts with section option unchecked");
  sections = true; WaveformEditingTestAccess::moreRefresh(top);
  expect(toggle->getToggleState(), "open touch menu synchronizes external view changes");
  enabled = false; WaveformEditingTestAccess::moreRefresh(top);
  expect(!toggle->isEnabled(), "disabled view capability disables menu action");
  WaveformEditingTestAccess::chooseTimeline(top, false);
  expect(sections && changes == 0, "blocked view cannot alter presentation");
  enabled = true; WaveformEditingTestAccess::moreRefresh(top);
  toggle->onClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  expect(!sections && changes == 1, "same menu action returns to normal waveform once");
  panel.reset(); panel = WaveformEditingTestAccess::morePanel(top);
  toggle = dynamic_cast<juce::Button *>(panel->findChildWithID("timeline-sections"));
  expect(toggle && !toggle->getToggleState(), "reopening preserves actual normal-waveform state");
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
  demo.title = juce::String::fromUTF8("Una canción de adoración con un título especialmente largo");
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
  for (const int width : {568, 700, 844, 956, 1024, 1280}) {
    const bool phone = width <= 844;
    top.setMinimalPhoneMode(phone);
    wave.setMinimalPhoneMode(phone);
    preview.setSize(width, 244);
    top.setBounds(0, 0, width, 64);
    wave.setBounds(0, 64, width, 180);
    WaveformEditingTestAccess::expectTopbarLiveLayout(top);
    const auto moreBounds = WaveformEditingTestAccess::moreBounds(top);
    const auto titleBounds = WaveformEditingTestAccess::songBounds(top);
    for (int repeat = 0; repeat < 4; ++repeat) {
      WaveformEditingTestAccess::clickLiveToggle(top);
      WaveformEditingTestAccess::expectTopbarLiveLayout(top);
      expect(WaveformEditingTestAccess::moreBounds(top) == moreBounds && WaveformEditingTestAccess::songBounds(top) == titleBounds,
             "repeated view toggles preserve title and controls geometry");
    }
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
    if (capturePath.isNotEmpty()) {
      auto menu = WaveformEditingTestAccess::morePanel(top);
      auto stream = juce::File(capturePath).getChildFile("sections-menu-" + juce::String(width) + ".png").createOutputStream();
      expect(stream && stream->setPosition(0) && stream->truncate().wasOk() &&
                 juce::PNGImageFormat().writeImageToStream(menu->createComponentSnapshot(menu->getLocalBounds(), true, 2.0f), *stream),
             "checked section menu captures production components");
    }
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

struct PlaybackPaintFixture {
  static constexpr double length = 30.0;
  double position = 6.0;
  WaveformBar waveform;

  PlaybackPaintFixture() {
    waveform.setTransportProviders([this] { return position; },
                                   [] { return length; }, [] { return true; });
    waveform.setSongSourceLengthProvider([] { return length; });
    waveform.setSongRhythm(120.0, "4/4");
    std::vector<float> bins(2048);
    for (size_t i = 0; i < bins.size(); ++i)
      bins[i] = 0.08f + 0.72f * std::abs(std::sin(i * 0.037f)) *
                                      std::abs(std::cos(i * 0.013f));
    waveform.setWaveformBins(std::move(bins));
    SongArrangementSnapshot arrangement;
    arrangement.parts = {original("Intro", 0.0, 10.0),
                         original("Verse", 10.0, 20.0),
                         original("Chorus", 20.0, 30.0)};
    waveform.setArrangement(arrangement, true);
    waveform.setSize(1200, 420);
    WaveformEditingTestAccess::fixedPlaybackViewport(waveform, 0.0f, 1.0f);
  }

  juce::Image paint() {
    juce::Image image(juce::Image::ARGB, waveform.getWidth(), waveform.getHeight(), true);
    juce::Graphics g(image);
    g.fillAll(juce::Colours::black);
    waveform.paint(g);
    return image;
  }

  void paintProgressChange(juce::Image &image, float from, float to) {
    juce::RectangleList<int> dirty;
    for (const auto &region : WaveformEditingTestAccess::playbackRepaintRegions(waveform, from, to))
      if (!region.isEmpty()) dirty.add(region);
    expect(!dirty.isEmpty(), "moving playback invalidates visible transport regions");
    juce::Graphics g(image);
    g.reduceClipRegion(dirty);
    // A nonopaque component receives its parent's freshly painted background
    // in each invalidated area, rather than accumulating translucent layers.
    g.fillAll(juce::Colours::black);
    waveform.paint(g);
  }
};

void expectSamePlaybackPixels(const juce::Image &actual, const juce::Image &expected,
                              const std::string &message,
                              juce::Rectangle<int> exactArea = {},
                              int backgroundTolerance = 0) {
  expect(actual.getBounds() == expected.getBounds(), message + " dimensions");
  for (int y = 0; y < actual.getHeight(); ++y)
    for (int x = 0; x < actual.getWidth(); ++x)
      if (actual.getPixelAt(x, y) != expected.getPixelAt(x, y))
      {
        const auto a = actual.getPixelAt(x, y), b = expected.getPixelAt(x, y);
        const int delta = std::max({std::abs(int(a.getRed()) - int(b.getRed())),
                                    std::abs(int(a.getGreen()) - int(b.getGreen())),
                                    std::abs(int(a.getBlue()) - int(b.getBlue())),
                                    std::abs(int(a.getAlpha()) - int(b.getAlpha()))});
        if (!exactArea.contains(x, y) && delta <= backgroundTolerance) continue;
        throw std::runtime_error(message + " at " + std::to_string(x) + "," + std::to_string(y) +
          " actual=" + a.toString().toStdString() + " expected=" + b.toString().toStdString() +
          " channel delta=" + std::to_string(delta));
      }
}

void testPlaybackDirtyRegionsMatchFullPaint() {
  // Both unrestricted and trimmed playback reuse the waveform raster; limits
  // are composed separately and must survive partial playhead updates.
  for (const bool trimmedWindow : {false, true}) {
    PlaybackPaintFixture f;
    if (trimmedWindow)
      f.waveform.setSongWindowProviders([] { return 2.0; }, [] { return 27.0; });
    const auto verifyMove = [&](float start, float span, float from, float to,
                                const char *description) {
      WaveformEditingTestAccess::fixedPlaybackViewport(f.waveform, start, span);
      f.position = static_cast<double>(from) * PlaybackPaintFixture::length;
      auto partial = f.paint();
      f.position = static_cast<double>(to) * PlaybackPaintFixture::length;
      f.paintProgressChange(partial, from, to);
      // JUCE's software gradient compositing rounds background channels by up
      // to 3/255 when its clip changes. Keep the opaque waveform exact; ruler
      // and minimap still reject a stale needle or changed geometry by far.
      expectSamePlaybackPixels(partial, f.paint(),
          std::string(trimmedWindow ? "cached trimmed " : "cached unrestricted ") + description,
          WaveformEditingTestAccess::waveBody(f.waveform).toNearestInt(), 3);
    };
    verifyMove(0.0f, 1.0f, 0.20f, 0.24f, "forward repaint updates wave, ruler and minimap");
    verifyMove(0.0f, 1.0f, 0.74f, 0.18f, "backward repaint removes old playheads and played shading");
    verifyMove(0.25f, 0.5f, 0.38f, 0.43f, "zoomed repaint uses each timeline's coordinates");
  }
}

void testPlaybackCacheReuseAndInvalidation() {
  PlaybackPaintFixture f;
  f.paint();
  const auto firstCache = WaveformEditingTestAccess::playbackWaveformCache(f.waveform);
  expect(firstCache.isValid(), "playback builds the waveform cache with Follow disabled");
  f.position += 1.0;
  f.paint();
  expect(firstCache == WaveformEditingTestAccess::playbackWaveformCache(f.waveform),
         "moving only playback reuses the rasterized waveform");

  const auto verifyRebuild = [&](const juce::Image &previousCache, const char *description) {
    const auto actual = f.paint();
    const auto rebuilt = WaveformEditingTestAccess::playbackWaveformCache(f.waveform);
    expect(rebuilt.isValid() && rebuilt != previousCache,
           std::string(description) + " rebuilds cached image data");
    WaveformEditingTestAccess::invalidatePlaybackWaveformCache(f.waveform);
    expectSamePlaybackPixels(actual, f.paint(),
                             std::string(description) + " matches a fresh render");
  };

  std::vector<float> replacementBins(2048);
  for (size_t i = 0; i < replacementBins.size(); ++i)
    replacementBins[i] = 0.10f + 0.30f * std::abs(std::sin(i * 0.021f));
  f.waveform.setWaveformBins(std::move(replacementBins));
  verifyRebuild(firstCache, "replacing waveform bins");
  const auto binsCache = WaveformEditingTestAccess::playbackWaveformCache(f.waveform);
  f.waveform.setSize(1024, 360);
  verifyRebuild(binsCache, "resizing the waveform");
  const auto resizedCache = WaveformEditingTestAccess::playbackWaveformCache(f.waveform);
  auto arrangement = WaveformEditingTestAccess::snapshot(f.waveform);
  arrangement.parts[1].sourcePartId = "Intro";
  arrangement.parts[1].sourceStartSec = 0.0;
  arrangement.parts[1].sourceEndSec = 10.0;
  f.waveform.setArrangement(arrangement, true);
  verifyRebuild(resizedCache, "changing arrangement source mapping");
}

void testPlaybackLimitsReuseCacheWithoutBakingShading() {
  PlaybackPaintFixture f;
  f.position = 24.0;
  double start = 0.0, end = PlaybackPaintFixture::length;
  f.waveform.setSongWindowProviders([&] { return start; }, [&] { return end; });
  const auto unrestricted = f.paint();
  const auto firstCache = WaveformEditingTestAccess::playbackWaveformCache(f.waveform);
  expect(firstCache.isValid(), "unrestricted playback starts with a valid waveform raster");

  // Mutable providers simulate a live trim without publishing new bins or
  // resetting the waveform component and its existing raster.
  start = 6.0; end = 18.0;
  const auto trimmed = f.paint();
  expect(firstCache == WaveformEditingTestAccess::playbackWaveformCache(f.waveform),
         "changing playback limits reuses the existing waveform raster");
  const auto body = WaveformEditingTestAccess::waveBody(f.waveform);
  const auto sampleArea = [&](float progress) {
    return juce::Rectangle<int>(12, 12).withCentre(
        juce::Point<int>(juce::roundToInt(body.getX() + progress * body.getWidth()),
                         juce::roundToInt(body.getCentreY() - 12.0f)));
  };
  const auto brightness = [](const juce::Image &image, juce::Rectangle<int> area) {
    double sum = 0.0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
      for (int x = area.getX(); x < area.getRight(); ++x)
        sum += image.getPixelAt(x, y).getBrightness();
    return sum / static_cast<double>(area.getWidth() * area.getHeight());
  };
  for (const float outside : {0.10f, 0.70f})
    expect(brightness(trimmed, sampleArea(outside)) <
               brightness(unrestricted, sampleArea(outside)),
           "trimmed audio is darkened even where played-waveform highlighting is active");
  const auto interior = sampleArea(0.40f);
  for (int y = interior.getY(); y < interior.getBottom(); ++y)
    for (int x = interior.getX(); x < interior.getRight(); ++x)
      expect(trimmed.getPixelAt(x, y) == unrestricted.getPixelAt(x, y),
             "playable interior retains its original waveform and played highlight");

  WaveformEditingTestAccess::invalidatePlaybackWaveformCache(f.waveform);
  expectSamePlaybackPixels(trimmed, f.paint(),
                           "live limits match a fresh cache built while already trimmed");
  const auto trimmedCache = WaveformEditingTestAccess::playbackWaveformCache(f.waveform);
  start = 0.0; end = PlaybackPaintFixture::length;
  const auto restored = f.paint();
  expect(trimmedCache == WaveformEditingTestAccess::playbackWaveformCache(f.waveform),
         "removing playback limits still reuses the waveform raster");
  expectSamePlaybackPixels(restored, unrestricted,
                           "a cache rebuilt during a trim never retains its shading or boundary guides");
}

void testDetectedSectionsReviewAndHistory() {
  Fixture f;
  SongArrangementSnapshot base;
  base.parts = {original("manual-verse", 10.0, 20.0)};
  base.parts.front().name = "Verso corregido";
  base.parts.front().muteInstruments = true;
  base.parts.front().automation.pads.volume = 0.25f;
  SongArrangementCueSnapshot cue;
  cue.id = "independent-count";
  cue.name = "1";
  cue.positionSec = 2.0;
  base.cues.push_back(cue);
  f.waveform.setArrangement(base, true);
  base = f.waveform.getArrangementSnapshot();
  const auto unmodified = f.waveform.createComponentSnapshot(
      f.waveform.getLocalBounds());
  auto proposed = original("suggested-intro", 0.0, 10.0);
  proposed.name = "Sección sugerida 1";
  f.waveform.setSectionDetectionPreview({proposed});
  expect(f.commits == 0 && f.waveform.getArrangementSnapshot().parts.size() == 1,
         "provisional timeline blocks never commit or enter the arrangement");
  const auto preview = f.waveform.createComponentSnapshot(
      f.waveform.getLocalBounds());
  bool changed = false;
  for (int y = 0; y < preview.getHeight() && !changed; ++y)
    for (int x = 0; x < preview.getWidth() && !changed; ++x)
      changed = preview.getPixelAt(x, y) != unmodified.getPixelAt(x, y);
  expect(changed, "review proposals render over the actual waveform");
  f.waveform.clearSectionDetectionPreview();
  expectSamePlaybackPixels(unmodified, f.waveform.createComponentSnapshot(
                              f.waveform.getLocalBounds()),
                           "discarding proposals restores the original timeline");
  expect(f.commits == 0, "discard never calls the persistence callback");

  auto merged = base;
  merged.parts.push_back(proposed);
  merged.parts.push_back(original("suggested-outro", 20.0, 30.0));
  auto invalid = merged;
  invalid.parts.front().name = "Replaced by analysis";
  expect(!f.waveform.applyDetectedSections(invalid) && f.commits == 0,
         "confirmation cannot rename a manually corrected section");
  invalid = merged;
  invalid.parts.back().startSec = 19.0;
  expect(!f.waveform.applyDetectedSections(invalid) && f.commits == 0,
         "an overlapping suggestion is rejected atomically");
  expect(f.waveform.applyDetectedSections(merged) && f.commits == 1,
         "confirmation uses the ordinary persistence callback exactly once");
  const auto accepted = f.waveform.getArrangementSnapshot();
  const auto manual = f.part("manual-verse");
  expect(manual.name == "Verso corregido" && manual.muteInstruments &&
             manual.automation == base.parts.front().automation &&
             accepted.cues == base.cues,
         "confirmation preserves manual name, automation, actions and cue marks");
  expect(!f.waveform.applyDetectedSections(accepted) && f.commits == 1,
         "reapplying accepted proposals never duplicates sections");
  expect(f.command('Z') && f.commits == 2 &&
             f.waveform.getArrangementSnapshot().parts.size() == 1,
         "one ordinary undo restores all pre-analysis sections");
  expect(f.command('Y') && f.commits == 3 &&
             f.waveform.getArrangementSnapshot().parts.size() == 3,
         "ordinary redo restores the accepted detection in one operation");

  Fixture empty;
  empty.waveform.setArrangement({}, false);
  expect(empty.waveform.getArrangementSnapshot().parts.empty(),
         "an untouched song has no synthetic full-song annotation");
  SongArrangementSnapshot initialDetection;
  initialDetection.parts = {original("first-suggestion", 0.0, 30.0)};
  expect(empty.waveform.applyDetectedSections(initialDetection),
         "a song with no manual sections accepts its first detection");
  expect(empty.command('Z') && empty.waveform.getArrangementSnapshot().parts.empty(),
         "undo of the first detection restores the original empty arrangement");
}

playworship::analysis::GuideAlignmentDraft guideDraftFixture() {
  playworship::analysis::GuideAlignmentDraft draft;
  draft.options.originSeconds = 1.0; draft.options.bpm = 120;
  draft.durationSeconds = 30;
  for (int pulse = 0; pulse < 59; ++pulse)
    draft.grid.push_back({1.0 + pulse * 0.5, pulse / 4 + 1, pulse % 4 + 1, pulse % 4 == 0});
  for (int i = 0; i < 3; ++i) {
    playworship::analysis::SectionSuggestion suggestion;
    suggestion.id = "guide-preview-" + juce::String(i);
    suggestion.displayName = i == 0 ? "Cordo" : i == 1 ? "Verso 1" : "Final";
    suggestion.startSeconds = 5.0 + i * 8; suggestion.endSeconds = i == 2 ? 30 : 13.0 + i * 8;
    suggestion.matchedCueTimeSeconds = suggestion.startSeconds - 2;
    draft.suggestions.push_back(suggestion);
  }
  return draft;
}

juce::MouseEvent guidePointer(WaveformBar &wave, juce::Point<float> position,
                              juce::Point<float> start, bool dragged, bool free = false) {
  const juce::Time now(1000000);
  return {juce::Desktop::getInstance().getMainMouseSource(), position,
      juce::ModifierKeys::leftButtonModifier | (free ? juce::ModifierKeys::altModifier : 0),
      1, 0, 0, 0, 0, &wave, &wave, now, start, now, 1, dragged};
}

void testGuideReviewRulerSpaceReachesTransport() {
  ProcessorFixture fixture;
  auto &wave = fixture.edit.waveform;
  auto &processor = fixture.processor;
  wave.setArrangement({}, false);
  wave.setGuideAlignmentPreview(guideDraftFixture());

  // Use the real JUCE focus/bubbling path. The host is the transport receiver
  // at this component boundary; playback itself uses the production processor.
  struct TransportHost final : juce::Component {
    explicit TransportHost(MTControlsAudioProcessor &value) : processor(value) {}
    MTControlsAudioProcessor &processor;
    int commands = 0;
    bool keyPressed(const juce::KeyPress &key) override {
      if (!key.isKeyCode(juce::KeyPress::spaceKey)) return false;
      ++commands;
      if (key.getModifiers().isAltDown()) processor.stopSong();
      else processor.toggleSongPlayPause();
      return true;
    }
  } host(processor);
  host.setBounds(-2000, -2000, 1200, 500);
  host.addAndMakeVisible(wave);
  wave.setBounds(host.getLocalBounds());
  host.addToDesktop(juce::ComponentPeer::windowIsTemporary);
  host.setVisible(true);
  auto *peer = host.getPeer();
  expect(peer != nullptr, "guide keyboard regression has a native JUCE peer");
  // Normal peer mouse dispatch focuses the clicked component before invoking
  // mouseDown. Establish that same focus before the production ruler handlers.
  wave.grabKeyboardFocus();
  const auto ruler = WaveformEditingTestAccess::ruler(wave);
  expect(ruler.getHeight() > 0, "the review ruler remains interactive");
  const auto click = WaveformEditingTestAccess::pointAt(wave, 12.0, ruler.getCentreY());
  wave.mouseDown(guidePointer(wave, click, click, false));
  wave.mouseUp(guidePointer(wave, click, click, false));
  expect(wave.hasKeyboardFocus(false), "the clicked review timeline owns keyboard focus");
  expectNear(processor.getSongCurrentPositionSeconds(), 12.0,
             "clicking the review ruler seeks through the real transport callback");
  expect(!processor.isSongPlaying(), "the ruler seek remains paused before Space");
  expect(peer->handleKeyPress(juce::KeyPress(juce::KeyPress::spaceKey)) &&
             processor.isSongPlaying() && host.commands == 1,
         "Space bubbles out of focused guide review and starts the real transport");
  expect(peer->handleKeyPress(juce::KeyPress(juce::KeyPress::spaceKey)) &&
             !processor.isSongPlaying() && host.commands == 2,
         "a second Space pauses playback through the same existing command");
  expect(peer->handleKeyPress(juce::KeyPress(juce::KeyPress::spaceKey,
             juce::ModifierKeys::altModifier, 0)) && host.commands == 3 &&
             !processor.isSongPlaying(),
         "Alt+Space remains available to the existing stop command during review");
  expectNear(processor.getSongCurrentPositionSeconds(), processor.getSongStartSeconds(),
             "Alt+Space uses the real stop behavior");
  expect(fixture.edit.command('Z') && wave.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)) &&
             fixture.edit.commits == 0 && wave.getArrangementSnapshot().parts.empty() &&
             wave.hasGuideAlignmentPreview(),
         "allowing transport keys keeps committed-arrangement shortcuts blocked and the review open");
}

void testGuideReviewManualSectionInsertionAndHistory() {
  using namespace playworship::analysis;
  Fixture fixture;
  auto &wave = fixture.waveform;
  wave.setArrangement({}, false);
  wave.setSize(1200, 360);
  GuideTranscript transcript;
  transcript.provider = "manual-section-ui-fixture";
  transcript.audioFingerprint = "manual-section-ui-source";
  transcript.durationSeconds = 30;
  transcript.segments = {{.05, .25, "Uno"}, {.5, .8, "Intro"},
      {3.1, 3.5, "Toda la banda"}, {9.1, 9.5, "Verso 1"}, {17.1, 17.5, "Final"}};
  const auto initial = alignGuideSections(transcript, {});
  auto draft = initial;
  expect(draft.error.isEmpty() && draft.suggestions.size() == 3 &&
             draft.secondaryCues.size() == 2,
         "manual insertion starts with real calculated sections, count and instruction");
  auto *add = dynamic_cast<juce::Button *>(wave.findChildWithID("guide-alignment-add"));
  expect(add && !add->isVisible(), "manual section action stays hidden outside suggestion review");
  int additions = 0, confirmations = 0, cancellations = 0;
  double selectedTime = -1;
  wave.setOnGuideSectionAdd([&](GuideCatalogChoice choice, double seconds) {
    ++additions;
    selectedTime = seconds;
    juce::String error;
    expect(addGuideSectionAt(draft, choice, seconds, true, error),
           "the native catalogue choice inserts only into the transient review");
    wave.setGuideAlignmentPreview(draft);
  });
  wave.setGuideAlignmentCallbacks({}, {}, [&] {
    const auto plan = appendConfirmedGuideSuggestions(wave.getArrangementSnapshot(), draft);
    expect(plan.error.isEmpty() && wave.applyDetectedSections(plan.arrangement),
           "automatic and manual proposals confirm through one ordinary arrange transaction");
    ++confirmations;
    wave.clearGuideAlignmentPreview();
  }, [&] {
    ++cancellations;
    wave.clearGuideAlignmentPreview();
  });
  wave.setGuideAlignmentPreview(draft);
  expect(add->isVisible() && add->isEnabled(),
         "review exposes an enabled add-section action beside confirmation");
  const auto items = WaveformEditingTestAccess::guideSectionAddItems(wave);
  const auto catalogue = guideCatalogEntries();
  for (const auto &entry : catalogue) {
    if (entry.kind != GuideAnnouncementKind::section) continue;
    expect(std::any_of(items.begin(), items.end(), [&](const auto &item) {
      return item.second.kind == entry.kind && item.second.key == entry.key &&
             item.second.type == entry.type && item.second.ordinal == entry.ordinal;
    }), "manual picker retains every existing musical section and numbered variant");
  }
  expect(std::all_of(items.begin(), items.end(), [](const auto &item) {
    return item.second.kind == GuideAnnouncementKind::section;
  }), "manual section insertion keeps counts and instructions in their existing roles");
  const auto chorus = std::find_if(items.begin(), items.end(), [](const auto &item) {
    return item.second.type == SectionType::chorus && item.second.ordinal == 2;
  });
  expect(chorus != items.end(), "the native picker exposes the existing Coro 2 variant");
  WaveformEditingTestAccess::guideSectionAdd(wave, chorus->second, 6.0);
  expect(additions == 1 && selectedTime == 6.0 && draft.suggestions.size() == 4 &&
             fixture.commits == 0 && wave.getArrangementSnapshot().parts.empty(),
         "adding at the selected musical position leaves the committed song untouched");
  const auto manual = std::find_if(draft.suggestions.begin(), draft.suggestions.end(),
      [&](const auto &mark) { return isManualGuideSection(draft, mark.id); });
  expect(manual != draft.suggestions.end() && manual->type == SectionType::chorus &&
             manual->userEdited && !manual->matchedCueTimeSeconds,
         "the inserted section is a manual catalogue choice without invented speech timing");
  expectNear(manual->startSeconds, 6.0, "manual placement uses the chosen position without announcement anticipation");
  expectNear(manual->endSeconds, 10.0, "the missing section ends at the next existing proposal");
  expectNear(draft.suggestions.front().endSeconds, 6.0, "the preceding proposal closes at the inserted section");
  for (size_t i = 0; i < initial.secondaryCues.size(); ++i)
    expect(draft.secondaryCues[i].id == initial.secondaryCues[i].id &&
               draft.secondaryCues[i].name == initial.secondaryCues[i].name &&
               draft.secondaryCues[i].timeSeconds == initial.secondaryCues[i].timeSeconds,
           "adding a section preserves every count and secondary point");
  const auto manualId = manual->id;
  expect(WaveformEditingTestAccess::guideMarkerCount(wave) == 6,
         "the inserted section appears on the same waveform with its automatic and secondary proposals");
  const auto capturePath = juce::SystemStats::getEnvironmentVariable("PW_GUIDE_MANUAL_CAPTURE_PATH", {});
  if (capturePath.isNotEmpty()) {
    const juce::File file(capturePath);
    expect(file.getParentDirectory().createDirectory().wasOk(), "manual review capture directory opens");
    auto output = file.createOutputStream();
    expect(output && juce::PNGImageFormat().writeImageToStream(
        wave.createComponentSnapshot(wave.getLocalBounds(), true, 1.5f), *output),
        "the production waveform renders the inserted manual section and compact review actions");
  }
  for (const int width : {320, 390, 768, 1200}) {
    wave.setSize(width, 360);
    expect(wave.getLocalBounds().contains(add->getBounds()) &&
               !add->getBounds().intersects(WaveformEditingTestAccess::guideAccept(wave).getBounds()) &&
               !add->getBounds().intersects(WaveformEditingTestAccess::guideCancel(wave).getBounds()),
           "manual insertion fits the existing review toolbar at phone and desktop widths");
  }
  WaveformEditingTestAccess::guideCancel(wave).onClick();
  expect(cancellations == 1 && fixture.commits == 0 && !wave.hasGuideAlignmentPreview() &&
             !add->isVisible() && wave.getArrangementSnapshot().parts.empty(),
         "cancel discards the visual review without publishing the manual insertion");
  WaveformEditingTestAccess::guideSectionAdd(wave, chorus->second, 6.0);
  expect(additions == 1, "a stale picker cannot insert after review cancellation");
  draft = initial;
  wave.setSize(1200, 360);
  wave.setGuideAlignmentPreview(draft);
  WaveformEditingTestAccess::guideSectionAdd(wave, chorus->second, 6.0);
  WaveformEditingTestAccess::guideAccept(wave).onClick();
  expect(confirmations == 1 && fixture.commits == 1 &&
             wave.getArrangementSnapshot().parts.size() == 4 &&
             wave.getArrangementSnapshot().cues.size() == 2 && !wave.hasGuideAlignmentPreview(),
         "one confirmation publishes the missing section together with all recognized marks");
  const auto accepted = wave.getArrangementSnapshot();
  expect(std::none_of(accepted.parts.begin(), accepted.parts.end(),
      [&](const auto &part) { return part.id == manualId; }),
      "a cancelled manual proposal never leaks into the later confirmed review");
  expect(fixture.command('Z') && wave.getArrangementSnapshot().parts.empty() &&
             wave.getArrangementSnapshot().cues.empty(),
         "one ordinary undo restores the complete pre-review arrangement");
  expect(fixture.command('Y') && wave.getArrangementSnapshot().parts.size() == 4 &&
             wave.getArrangementSnapshot().cues == accepted.cues,
         "one redo restores both manual and automatic review marks");
}

void testCalculatedGuideAlignmentReview() {
  using namespace playworship::analysis;
  Fixture fixture;
  auto &wave = fixture.waveform;
  wave.setArrangement({}, false); wave.setSize(1200, 320);
  std::vector<float> bins(4096);
  for (size_t i = 0; i < bins.size(); ++i)
    bins[i] = float(0.08 + 0.72 * std::abs(std::sin(i * 0.137)) * std::abs(std::cos(i * 0.019)));
  wave.setWaveformBins(std::move(bins));
  GuideTranscript transcript;
  transcript.audioFingerprint = "calculated-review-fixture";
  transcript.durationSeconds = 30;
  // This fixture isolates origin/boundary gestures. Use one independently
  // timed count; unresolved compound-count catalogue review is covered below.
  transcript.segments = {{0, .44, "Uno"}, {3.1, 3.4, "Coro"},
      {9.1, 9.5, "Verso uno"}, {17.1, 17.5, "Final"}};
  GuideAlignmentOptions options;
  auto draft = alignGuideSections(transcript, options);
  expect(draft.error.isEmpty() && draft.pendingAnnouncements.empty() && draft.suggestions.size() == 3,
         "supported spoken cues create the real calculated review draft without unrelated pending catalogue choices");
  wave.setGuideAlignmentCallbacks([&](double origin) {
    options.originSeconds = origin;
    draft = alignGuideSections(transcript, options);
    expect(draft.error.isEmpty(), "dragged origin rebuilds a valid musical grid");
    wave.setGuideAlignmentPreview(draft);
  }, [&](const juce::String &id, double seconds, bool snap) {
    juce::String error;
    expect(moveGuideSectionBoundary(draft, id, seconds, snap, error), "dragged marker uses the real boundary calculation");
    wave.setGuideAlignmentPreview(draft);
  }, [&] {
    const auto plan = appendConfirmedSuggestions(wave.getArrangementSnapshot(), draft.suggestions, draft.durationSeconds);
    expect(plan.error.isEmpty() && wave.applyDetectedSections(plan.arrangement), "calculated review confirms through domain acceptance and ordinary history");
    wave.clearGuideAlignmentPreview();
  }, [&] { wave.clearGuideAlignmentPreview(); });
  wave.setGuideAlignmentPreview(draft);
  const auto body = WaveformEditingTestAccess::waveBody(wave);
  const auto origin = WaveformEditingTestAccess::guideOrigin(wave);
  const auto targetOrigin = origin.translated(body.getWidth() * 0.5f / 30.0f, 0);
  wave.mouseDown(guidePointer(wave, origin, origin, false));
  wave.mouseDrag(guidePointer(wave, targetOrigin, origin, true));
  wave.mouseUp(guidePointer(wave, targetOrigin, origin, true));
  expectNear(draft.options.originSeconds, 0.5, "origin gesture reaches musical alignment");
  expectNear(draft.suggestions[0].startSeconds, 4.5, "origin correction recomputes the first proposed entry");
  expectNear(draft.suggestions[1].startSeconds, 10.5, "origin correction recomputes the following entry");
  const auto marker = WaveformEditingTestAccess::guideMarker(wave, draft.suggestions[1].id);
  const auto targetMarker = marker.translated(body.getWidth() * 0.8f / 30.0f, 0);
  wave.mouseDown(guidePointer(wave, marker, marker, false, true));
  wave.mouseDrag(guidePointer(wave, targetMarker, marker, true, true));
  wave.mouseUp(guidePointer(wave, targetMarker, marker, true, true));
  expectNear(draft.suggestions[1].startSeconds, 11.3, "local marker correction retains the precise dragged source time");
  expectNear(draft.suggestions[0].endSeconds, 11.3, "local correction recalculates the preceding section end");
  expectNear(draft.suggestions[2].startSeconds, 18.5, "local correction preserves the later section start");
  expect(fixture.commits == 0 && wave.getArrangementSnapshot().parts.empty(), "calculated origin and boundary edits remain provisional");
  expectNear(draft.suggestions[0].startSeconds, 4.5, "automatic suggestions keep the following-bar policy");
  expectNear(draft.suggestions[1].startSeconds, 11.3, "explicit marker correction remains available with fixed automatic anticipation");
  const auto capturePath = juce::SystemStats::getEnvironmentVariable("PW_GUIDE_CALCULATED_CAPTURE_PATH", {});
  if (capturePath.isNotEmpty()) {
    const juce::File file(capturePath);
    expect(file.getParentDirectory().createDirectory().wasOk(), "calculated preview capture directory opens");
    auto output = file.createOutputStream();
    expect(output && juce::PNGImageFormat().writeImageToStream(wave.createComponentSnapshot(wave.getLocalBounds(), true, 1.5f), *output),
           "production waveform renders the actual alignment calculation");
  }
  WaveformEditingTestAccess::guideAccept(wave).onClick();
  expect(fixture.commits == 1 && wave.getArrangementSnapshot().parts.size() == 3, "one confirmation publishes all calculated sections together");
  expect(fixture.command('Z') && wave.getArrangementSnapshot().parts.empty(), "one undo reverts the calculated confirmation");
}

void testClosedGuideCatalogueReviewAndRoleSelection() {
  using namespace playworship::analysis;
  Fixture fixture;
  auto &wave = fixture.waveform;
  wave.setArrangement({}, false);
  wave.setSize(1200, 360);
  GuideTranscript transcript;
  transcript.provider = "catalogue-ui-fixture";
  transcript.audioFingerprint = "catalogue-ui-source";
  transcript.durationSeconds = 30;
  transcript.segments = {{.5,.9,"Intro"}, {3.15,3.7,"uncertain spoken fragment"},
      {9.1,9.6,"another uncertain fragment"}};
  transcript.segments[1].language = "en";
  transcript.segments[1].confidence = .7;
  transcript.segments[1].alternatives = {{"uncertain spoken fragment","en",-.35,.01},
      {"subir intensidad","es",-.8,.01}};
  auto draft = alignGuideSections(transcript, {});
  expect(draft.suggestions.size() == 1 && draft.pendingAnnouncements.size() == 2 && draft.secondaryCues.empty(),
         "unresolved speech remains review points beside the known Intro rather than splitting new musical sections");
  const auto firstId = draft.pendingAnnouncements[0].id;
  const auto secondId = draft.pendingAnnouncements[1].id;
  int selections = 0, accepted = 0, discarded = 0;
  wave.setOnGuideCatalogSelection([&](juce::String id, GuideCatalogChoice choice) {
    ++selections;
    juce::String error;
    expect(chooseGuideCatalogMark(draft, id, choice, error), "catalogue selection uses the role-aware domain operation");
    wave.setGuideAlignmentPreview(draft);
  });
  wave.setOnGuideAlignmentRemove([&](const juce::String &id) {
    juce::String error;
    expect(discardGuidePendingAnnouncement(draft, id, error), "discard removes exactly the unresolved source point");
    ++discarded;
    wave.setGuideAlignmentPreview(draft);
  });
  wave.setGuideAlignmentCallbacks({}, {}, [&] {
    const auto plan = appendConfirmedGuideSuggestions(wave.getArrangementSnapshot(), draft);
    expect(plan.error.isEmpty() && wave.applyDetectedSections(plan.arrangement),
           "resolved catalogue choices use the existing atomic confirmation and history");
    ++accepted;
    wave.clearGuideAlignmentPreview();
  }, [&] { wave.clearGuideAlignmentPreview(); });
  wave.setGuideAlignmentPreview(draft);
  expect(!WaveformEditingTestAccess::guideAccept(wave).isEnabled() &&
         WaveformEditingTestAccess::guideMarkerCount(wave) == 3 &&
         WaveformEditingTestAccess::guidePendingPoint(wave, firstId) &&
         WaveformEditingTestAccess::guidePendingPoint(wave, secondId),
         "pending labels are compact temporal points and block confirmation without creating unknown principal blocks");
  WaveformEditingTestAccess::guideAccept(wave).onClick();
  expect(accepted == 0 && fixture.commits == 0, "a pending catalogue choice cannot publish even through a direct button callback");
  const auto menu = WaveformEditingTestAccess::guideCatalogItems(wave, firstId);
  const auto catalogue = guideCatalogEntries();
  for (const auto &entry : catalogue)
    expect(std::any_of(menu.begin(), menu.end(), [&](const auto &item) {
      return item.second.kind == entry.kind && item.second.key == entry.key &&
          item.second.type == entry.type && item.second.ordinal == entry.ordinal;
    }), "all supported catalogue choices remain available alongside ranked candidates");
  for (size_t i = 0; i < menu.size(); ++i)
    for (size_t j = i + 1; j < menu.size(); ++j)
      expect(menu[i].first != menu[j].first, "native menu IDs never collide between suggestions and the full catalogue");
  const auto findChoice = [&](GuideAnnouncementKind kind, const juce::String &key) {
    const auto found = std::find_if(catalogue.begin(), catalogue.end(), [&](const auto &choice) {
      return choice.kind == kind && choice.key == key;
    });
    expect(found != catalogue.end(), "requested UI test choice is part of the live shared catalogue");
    return *found;
  };
  const auto build = findChoice(GuideAnnouncementKind::instruction, "Build");
  const auto verse = findChoice(GuideAnnouncementKind::section, "Verse 2");
  const auto chorus = findChoice(GuideAnnouncementKind::section, "Chorus");
  const auto count = findChoice(GuideAnnouncementKind::count, "3");
  auto invalid = build; invalid.key = "not-a-catalogue-mark";
  WaveformEditingTestAccess::guideCatalogChoose(wave, firstId, invalid);
  WaveformEditingTestAccess::guideCatalogChoose(wave, "obsolete-source-id", build);
  expect(selections == 0, "the picker rejects arbitrary labels and stale asynchronous source IDs");
  const auto capturePath = juce::SystemStats::getEnvironmentVariable("PW_GUIDE_CATALOGUE_CAPTURE_PATH", {});
  if (capturePath.isNotEmpty()) {
    const juce::File file(capturePath);
    expect(file.getParentDirectory().createDirectory().wasOk(), "catalogue preview capture folder is available");
    auto output = file.createOutputStream();
    expect(output && juce::PNGImageFormat().writeImageToStream(wave.createComponentSnapshot(wave.getLocalBounds(), true, 1.5f), *output),
           "pending catalogue choices render in the existing native waveform style");
  }
  WaveformEditingTestAccess::guideCatalogChoose(wave, firstId, build);
  expect(draft.pendingAnnouncements.size() == 1 && draft.secondaryCues.size() == 1 &&
         draft.secondaryCues[0].name == "Build" && draft.suggestions.size() == 1 &&
         !WaveformEditingTestAccess::guideAccept(wave).isEnabled(),
         "choosing an instruction resolves one point without manufacturing a principal boundary");
  expectNear(draft.secondaryCues[0].timeSeconds, 3.15, "chosen instruction retains its spoken source instant");
  WaveformEditingTestAccess::guideCatalogChoose(wave, secondId, verse);
  expect(draft.pendingAnnouncements.empty() && draft.suggestions.size() == 2 &&
         WaveformEditingTestAccess::guideAccept(wave).isEnabled(), "a named musical choice clears the final pending point");
  expectNear(draft.suggestions[1].startSeconds, 10, "chosen principal uses the following musical bar rather than instruction timing");
  WaveformEditingTestAccess::guideCatalogChoose(wave, firstId, chorus);
  expect(draft.secondaryCues.empty() && draft.suggestions.size() == 3,
         "the catalogue also corrects an already resolved instruction into a principal with the selected role");
  expectNear(draft.suggestions[1].startSeconds, 4, "role conversion recomputes the principal's musical destination");
  WaveformEditingTestAccess::guideCatalogChoose(wave, secondId, count);
  expect(draft.secondaryCues.size() == 1 && draft.secondaryCues[0].name == "3" && draft.suggestions.size() == 2,
         "a resolved principal can be corrected into an individual count from the same catalogue");
  expectNear(draft.secondaryCues[0].timeSeconds, 9.1, "changing to a count restores the measured spoken time");
  expect(fixture.commits == 0 && wave.getArrangementSnapshot().parts.empty(),
         "catalogue choices remain provisional until the ordinary confirmation");
  WaveformEditingTestAccess::guideAccept(wave).onClick();
  expect(accepted == 1 && fixture.commits == 1 && wave.getArrangementSnapshot().parts.size() == 2 &&
         wave.getArrangementSnapshot().cues.size() == 1, "resolved roles publish once through the existing arrangement transaction");
  expect(fixture.command('Z') && wave.getArrangementSnapshot().parts.empty() && wave.getArrangementSnapshot().cues.empty(),
         "one undo removes the entire catalogue-reviewed confirmation");
  const auto selectionCount = selections;
  WaveformEditingTestAccess::guideCatalogChoose(wave, firstId, build);
  expect(selections == selectionCount, "a dismissed review cannot accept a late popup result");

  draft = alignGuideSections(transcript, {});
  wave.setGuideAlignmentPreview(draft);
  WaveformEditingTestAccess::guideDiscard(wave, firstId);
  expect(discarded == 1 && draft.pendingAnnouncements.size() == 1 && draft.suggestions.size() == 1 &&
         !WaveformEditingTestAccess::guideAccept(wave).isEnabled(), "discarding one pending mark preserves the others and their confirmation requirement");
  WaveformEditingTestAccess::guideDiscard(wave, secondId);
  expect(discarded == 2 && draft.pendingAnnouncements.empty() && WaveformEditingTestAccess::guideAccept(wave).isEnabled(),
         "discarding unresolved speech is an explicit review decision rather than an unknown musical section");
  wave.clearGuideAlignmentPreview();
}

void testPendingGuideReviewFocusesOnlyOnEntry() {
  using namespace playworship::analysis;
  Fixture fixture;
  auto &wave = fixture.waveform;
  wave.setArrangement({}, false);
  wave.setSize(1200, 360);
  double playbackSeconds = 24.0;
  int seeks = 0;
  wave.setTransportProviders([&] { return playbackSeconds; }, [] { return 30.0; }, [] { return false; });
  wave.setSeekCallback([&](double seconds) { ++seeks; playbackSeconds = seconds; });
  auto draft = guideDraftFixture();
  // Source order need not be display order: the earliest unresolved call must
  // become visible when arriving from a zoomed region near the song's end.
  draft.pendingAnnouncements = {{"later-pending", "later call", 22.3, 22.8, {}},
      {"first-pending", "count interval", 1.88, 3.78, {}}};
  WaveformEditingTestAccess::setVisibleWindow(wave, .6f, .2f);
  const auto previousWindow = WaveformEditingTestAccess::visibleWindow(wave);
  wave.setGuideAlignmentPreview(draft);
  const auto focused = WaveformEditingTestAccess::visibleWindow(wave);
  expectNear(focused.second, previousWindow.second, "initial pending focus preserves the user's zoom span");
  expectNear(focused.first, 1.88 / 30.0 - double(previousWindow.second) * .25,
             "the first pending source point is placed a quarter of the way into the existing viewport");
  expect(WaveformEditingTestAccess::guidePendingPoint(wave, "first-pending") &&
         !WaveformEditingTestAccess::guideAccept(wave).isEnabled(),
         "the pending marker responsible for disabled confirmation is visible on arrival");
  expect(seeks == 0 && playbackSeconds == 24.0 && fixture.commits == 0,
         "review focus changes only the viewport, never playback, seeking or arrangement persistence");

  WaveformEditingTestAccess::setVisibleWindow(wave, .55f, .2f);
  const auto manuallyPanned = WaveformEditingTestAccess::visibleWindow(wave);
  draft.options.originSeconds = .25;
  wave.setGuideAlignmentPreview(draft);
  expect(WaveformEditingTestAccess::visibleWindow(wave) == manuallyPanned,
         "rebuilding an active review respects the user's pan rather than repeatedly centring pending marks");
  expect(seeks == 0 && playbackSeconds == 24.0 && fixture.commits == 0,
         "rebuilds after manual navigation also preserve playback and the saved arrangement");

  wave.clearGuideAlignmentPreview();
  WaveformEditingTestAccess::setVisibleWindow(wave, .04f, .2f);
  const auto alreadyVisible = WaveformEditingTestAccess::visibleWindow(wave);
  wave.setGuideAlignmentPreview(draft);
  expect(WaveformEditingTestAccess::visibleWindow(wave) == alreadyVisible,
         "entering review does not pan when the earliest pending marker is already visible");
  wave.clearGuideAlignmentPreview();
  draft.pendingAnnouncements[1].startSeconds = .02;
  WaveformEditingTestAccess::setVisibleWindow(wave, .7f, .2f);
  wave.setGuideAlignmentPreview(draft);
  expectNear(WaveformEditingTestAccess::visibleWindow(wave).first, 0,
             "initial pending focus clamps at source zero while preserving the zoom span");
  expectNear(WaveformEditingTestAccess::visibleWindow(wave).second, .2,
             "source-edge focus never widens the viewport");
  wave.clearGuideAlignmentPreview();
}

void testGuideAlignmentWaveformReview() {
  Fixture fixture;
  auto &wave = fixture.waveform;
  wave.setArrangement({}, false);
  wave.setSize(1200, 320);
  std::vector<float> bins(4096);
  for (size_t i = 0; i < bins.size(); ++i) {
    const double t = double(i) / double(bins.size());
    const double swell = t < 0.12 ? 0.27 : t < 0.42 ? 0.82 : t < 0.70 ? 0.48 : 0.95;
    bins[i] = float((0.07 + 0.65 * std::abs(std::sin(i * 0.137)) * std::abs(std::cos(i * 0.019))) * swell);
  }
  wave.setWaveformBins(std::move(bins));
  int entryCalls = 0;
  wave.setSectionDetectionAvailability(true);
  wave.setOnSectionDetectionRequest([&] { ++entryCalls; });
  auto &entry = WaveformEditingTestAccess::suggestSections(wave);
  expect(entry.isVisible() && entry.isEnabled() && entry.getButtonText() == "Detectar secciones",
         "detection entry uses the existing edit toolbar and the user-facing action label");
  entry.onClick(); expect(entryCalls == 1, "edit suggestion button dispatches the existing entry callback");
  wave.triggerEditToggleFromMidi();
  juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  expect(!entry.isVisible(), "suggestions entry disappears outside edit mode");
  wave.triggerEditToggleFromMidi();
  juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

  auto draft = guideDraftFixture();
  double origin = -1, moved = -1; juce::String movedId; bool snapped = false;
  int accepted = 0, cancelled = 0;
  wave.setGuideAlignmentCallbacks([&](double value) {
    origin = value;
    const auto delta = value - draft.options.originSeconds;
    draft.options.originSeconds = value;
    for (auto &mark : draft.grid) mark.timeSeconds += delta;
    wave.setGuideAlignmentPreview(draft);
  }, [&](const juce::String &id, double value, bool snap) {
    movedId = id; moved = value; snapped = snap;
    for (auto &suggestion : draft.suggestions) if (suggestion.id == id) suggestion.startSeconds = value;
    wave.setGuideAlignmentPreview(draft);
  }, [&] {
    SongArrangementSnapshot snapshot = wave.getArrangementSnapshot();
    for (const auto &suggestion : draft.suggestions) {
      auto part = original(suggestion.id.toRawUTF8(), suggestion.startSeconds, suggestion.endSeconds);
      part.name = suggestion.displayName; snapshot.parts.push_back(part);
    }
    expect(wave.applyDetectedSections(snapshot), "confirm applies suggestions through ordinary arrangement transaction");
    ++accepted; wave.clearGuideAlignmentPreview();
  }, [&] { ++cancelled; wave.clearGuideAlignmentPreview(); });
  wave.setOnGuideAlignmentRename([&](juce::String id, juce::String name) {
    for (auto &suggestion : draft.suggestions) if (suggestion.id == id) suggestion.displayName = name;
    wave.setGuideAlignmentPreview(draft);
  });
  wave.setGuideAlignmentPreview(draft);
  expect(wave.hasGuideAlignmentPreview() && fixture.commits == 0 && wave.getArrangementSnapshot().parts.empty(),
         "visual markers never enter committed arrangement or persistence");
  expect(!entry.isVisible(), "review toolbar replaces editing actions while proposals are transient");
  auto start = WaveformEditingTestAccess::guideOrigin(wave);
  auto body = WaveformEditingTestAccess::waveBody(wave);
  auto target = start.translated(body.getWidth() * 0.375f / 30.0f, 0);
  wave.mouseDown(guidePointer(wave, start, start, false));
  wave.mouseDrag(guidePointer(wave, target, start, true));
  expectNear(origin, 1.375, "bar one handle supports free sub-beat origin correction");
  target = start.translated(body.getWidth() * 0.75f / 30.0f, 0);
  wave.mouseDrag(guidePointer(wave, target, start, true));
  wave.mouseUp(guidePointer(wave, target, start, true));
  expectNear(origin, 1.75, "synchronous draft refresh preserves drag anchor without accumulating movement");

  start = WaveformEditingTestAccess::guideMarker(wave, "guide-preview-1");
  target = start.translated(body.getWidth() * 0.25f / 30.0f, 0);
  wave.mouseDown(guidePointer(wave, start, start, false));
  wave.mouseDrag(guidePointer(wave, target, start, true));
  wave.mouseUp(guidePointer(wave, target, start, true));
  expect(movedId == "guide-preview-1" && snapped, "section marker requests bar snapping by default");
  expectNear(moved, 13.25, "marker forwards source seconds with pill grab offset preserved");
  start = WaveformEditingTestAccess::guideMarker(wave, "guide-preview-1");
  target = start.translated(body.getWidth() * 0.125f / 30.0f, 0);
  wave.mouseDown(guidePointer(wave, start, start, false, true));
  wave.mouseDrag(guidePointer(wave, target, start, true, true));
  wave.mouseUp(guidePointer(wave, target, start, true, true));
  expect(!snapped, "Alt requests a free local boundary correction");
  expectNear(moved, 13.375, "free boundary edit retains sub-beat precision");

  start = WaveformEditingTestAccess::guideMarker(wave, "guide-preview-0");
  wave.mouseDoubleClick(guidePointer(wave, start, start, false));
  auto &rename = WaveformEditingTestAccess::guideRename(wave);
  expect(rename.isVisible(), "double click edits a doubtful marker name inline");
  rename.setText("Coro", false); rename.onReturnKey();
  expect(draft.suggestions.front().displayName == "Coro" && fixture.commits == 0,
         "renaming updates only the transient draft via its callback");
  expect(fixture.command('Z') && wave.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)) && fixture.commits == 0,
         "review absorbs old arrangement undo and delete shortcuts without mutating the base");

  WaveformEditingTestAccess::fixedPlaybackViewport(wave, 0.4f, 0.25f);
  expect(WaveformEditingTestAccess::guideMarkerCount(wave) == 1,
         "zoomed preview excludes offscreen markers instead of pinning them to viewport edges");
  WaveformEditingTestAccess::fixedPlaybackViewport(wave, 0, 1);
  const auto capturePath = juce::SystemStats::getEnvironmentVariable("PW_GUIDE_ALIGNMENT_CAPTURE_PATH", {});
  if (capturePath.isNotEmpty()) {
    expect(juce::File::isAbsolutePath(capturePath), "guide review screenshot path is absolute");
    const juce::File file(capturePath);
    expect(file.getParentDirectory().createDirectory().wasOk(), "guide screenshot output directory opens");
    auto output = file.createOutputStream();
    expect(output && juce::PNGImageFormat().writeImageToStream(wave.createComponentSnapshot(wave.getLocalBounds(), true, 1.5f), *output),
           "deterministic waveform and draggable markers render in the actual component");
  }
  WaveformEditingTestAccess::guideAccept(wave).onClick();
  expect(accepted == 1 && fixture.commits == 1 && fixture.published.parts.size() == 3 && !wave.hasGuideAlignmentPreview(),
         "Confirm publishes one complete arrangement transaction and closes preview");
  expect(fixture.command('Z') && wave.getArrangementSnapshot().parts.empty(), "one ordinary undo restores the pre-review arrangement");
  expect(fixture.command('Y') && wave.getArrangementSnapshot().parts.size() == 3, "ordinary redo restores accepted IDs and boundaries");
  const auto commits = fixture.commits;
  wave.setGuideAlignmentPreview(draft);
  WaveformEditingTestAccess::guideCancel(wave).onClick();
  expect(cancelled == 1 && !wave.hasGuideAlignmentPreview() && fixture.commits == commits,
         "Cancel clears markers and toolbar without persistence or history changes");
  wave.setGuideAlignmentPreview(draft); wave.resetPlaybackViewForSong();
  expect(!wave.hasGuideAlignmentPreview(), "song view reset removes review markers and toolbar");
}

void testGuideSecondaryWaveformReview() {
  using namespace playworship::analysis;
  Fixture fixture;
  auto &wave = fixture.waveform;
  wave.setArrangement({}, false); wave.setSize(1200, 360);
  auto draft = guideDraftFixture();
  for (size_t i = 0; i < draft.suggestions.size(); ++i)
    draft.suggestions[i].displayName = "Refrain " + juce::String(int(i) + 1);
  const double positions[]{2, 6, 9, 12, 16, 19, 24, 27};
  const char *names[]{"All In", "Build", "Drums In", "Breakdown", "Hold", "Bass", "Softly", "Last Time"};
  for (int i = 0; i < 8; ++i) {
    GuideSecondaryCueSuggestion cue;
    cue.id = "secondary-review-" + juce::String(i); cue.name = cue.rawText = names[i];
    cue.timeSeconds = cue.rawTimeSeconds = positions[i];
    draft.secondaryCues.push_back(cue);
  }
  int seeks = 0, moves = 0, cancels = 0;
  bool snapped = false;
  wave.setSeekCallback([&](double) { ++seeks; });
  wave.setGuideAlignmentCallbacks({}, {}, [&] {
    const auto plan = appendConfirmedGuideSuggestions(wave.getArrangementSnapshot(), draft);
    expect(plan.error.isEmpty() && wave.applyDetectedSections(plan.arrangement),
           "secondary review confirms using the ordinary additive arrangement transaction");
    wave.clearGuideAlignmentPreview();
  }, [&] { ++cancels; wave.clearGuideAlignmentPreview(); });
  wave.setOnGuideSecondaryCueMove([&](const juce::String &id, double seconds, bool snap) {
    ++moves; snapped = snap; juce::String error;
    expect(moveGuideSecondaryCue(draft, id, seconds, snap, error), "secondary gesture uses the actual pulse calculation");
    wave.setGuideAlignmentPreview(draft);
  });
  wave.setOnGuideAlignmentRename([&](juce::String id, juce::String name) {
    for (auto &cue : draft.secondaryCues) if (cue.id == id) { cue.name = name; cue.userEdited = true; }
    wave.setGuideAlignmentPreview(draft);
  });
  wave.setOnGuideAlignmentRemove([&](const juce::String &id) {
    draft.secondaryCues.erase(std::remove_if(draft.secondaryCues.begin(), draft.secondaryCues.end(),
        [&](const auto &cue) { return cue.id == id; }), draft.secondaryCues.end());
    wave.setGuideAlignmentPreview(draft);
  });
  wave.setGuideAlignmentPreview(draft);
  expect(WaveformEditingTestAccess::guideMarkerCount(wave) == 11 && fixture.commits == 0 &&
             wave.getArrangementSnapshot().parts.empty() && wave.getArrangementSnapshot().cues.empty(),
         "three corrected musical names and eight secondary points render without changing saved arrangement");
  const auto capturePath = juce::SystemStats::getEnvironmentVariable("PW_GUIDE_SECONDARY_CAPTURE_PATH", {});
  if (capturePath.isNotEmpty()) {
    const juce::File file(capturePath);
    expect(file.getParentDirectory().createDirectory().wasOk(), "secondary preview capture directory opens");
    auto stream = file.createOutputStream();
    expect(stream && juce::PNGImageFormat().writeImageToStream(
        wave.createComponentSnapshot(wave.getLocalBounds(), true, 1.5f), *stream),
        "the production waveform renders eight secondary instructions in its existing timeline");
  }
  const auto body = WaveformEditingTestAccess::waveBody(wave);
  auto start = WaveformEditingTestAccess::guideSecondaryPoint(wave, "secondary-review-0");
  auto target = start.translated(body.getWidth() * 0.3f / 30.0f, 0);
  wave.mouseDown(guidePointer(wave, start, start, false));
  wave.mouseDrag(guidePointer(wave, target, start, true));
  wave.mouseUp(guidePointer(wave, target, start, true));
  expect(moves == 1 && snapped && seeks == 0, "secondary dragging requests pulse snap without seeking");
  expectNear(draft.secondaryCues.front().timeSeconds, 2.5, "secondary marker snaps to its nearest proposed pulse");
  start = WaveformEditingTestAccess::guideSecondaryPoint(wave, "secondary-review-0");
  target = start.translated(body.getWidth() * 0.125f / 30.0f, 0);
  wave.mouseDown(guidePointer(wave, start, start, false, true));
  wave.mouseDrag(guidePointer(wave, target, start, true, true));
  wave.mouseUp(guidePointer(wave, target, start, true, true));
  expect(!snapped && seeks == 0 && fixture.commits == 0, "Alt adjusts only the provisional secondary marker freely");
  expectNear(draft.secondaryCues.front().timeSeconds, 2.625, "free secondary gesture retains sub-pulse source time");
  expectNear(draft.secondaryCues.front().rawTimeSeconds, 2.0, "secondary editing preserves the original spoken evidence time");
  start = WaveformEditingTestAccess::guideMarker(wave, "secondary-review-0");
  wave.mouseDoubleClick(guidePointer(wave, start, start, false));
  auto &rename = WaveformEditingTestAccess::guideRename(wave);
  expect(rename.isVisible() && rename.getText() == "All In", "secondary names use the same inline review editor");
  rename.setText("Build", false); rename.onReturnKey();
  expect(draft.secondaryCues.front().name == "Build" && draft.secondaryCues.front().rawText == "All In" && fixture.commits == 0,
         "secondary renaming changes only the draft while retaining literal ASR evidence");
  start = WaveformEditingTestAccess::guideSecondaryPoint(wave, "secondary-review-7");
  wave.mouseDown(guidePointer(wave, start, start, false)); wave.mouseUp(guidePointer(wave, start, start, false));
  expect(wave.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)) && draft.secondaryCues.size() == 7,
         "deleting the selected secondary discards only that suggestion");
  for (size_t i = 0; i < draft.suggestions.size(); ++i) {
    expect(draft.suggestions[i].displayName == "Refrain " + juce::String(int(i) + 1),
           "secondary edits preserve every manually corrected Refrain name");
    expectNear(draft.suggestions[i].startSeconds, 5.0 + double(i) * 8.0,
               "secondary edits never shift a musical boundary");
  }
  WaveformEditingTestAccess::guideCancel(wave).onClick();
  expect(cancels == 1 && fixture.commits == 0 && !wave.hasGuideAlignmentPreview() &&
             wave.getArrangementSnapshot().cues.empty(), "cancel discards secondary edits without persistence or history");
  draft.suggestions.clear(); wave.setGuideAlignmentPreview(draft);
  expect(WaveformEditingTestAccess::guideAccept(wave).isEnabled(), "a secondary-only draft can be confirmed");
  WaveformEditingTestAccess::guideAccept(wave).onClick();
  expect(fixture.commits == 1 && wave.getArrangementSnapshot().parts.empty() &&
             wave.getArrangementSnapshot().cues.size() == 7, "secondary-only confirmation publishes one complete transaction");
  expect(fixture.command('Z') && wave.getArrangementSnapshot().cues.empty(), "one undo reverses the full secondary-only confirmation");
  expect(fixture.command('Y') && wave.getArrangementSnapshot().cues.size() == 7, "redo restores all accepted secondary point IDs");
}

void testDenseGuideLabelsWithSecondaryLane() {
  using namespace playworship::analysis;
  Fixture fixture;
  auto &wave = fixture.waveform;
  wave.setSize(1200, 320);
  auto draft = guideDraftFixture();
  for (int i = 0; i < 8; ++i) {
    GuideSecondaryCueSuggestion cue;
    cue.id = "compact-secondary-" + juce::String(i); cue.name = cue.rawText = "All In";
    cue.timeSeconds = cue.rawTimeSeconds = 2.0 + i * 3.5;
    draft.secondaryCues.push_back(cue);
  }
  wave.setGuideAlignmentPreview(draft);
  const auto initialBody = WaveformEditingTestAccess::waveBody(wave);
  wave.setSize(wave.getWidth(), wave.getHeight() + juce::roundToInt(130.0f - initialBody.getHeight()));
  const auto body = WaveformEditingTestAccess::waveBody(wave);
  expectNear(body.getHeight(), 130.0, "compact review fixture has a 130-point waveform body");
  const double secondsPerLabel = 74.0 * 30.0 / body.getWidth();
  for (size_t i = 0; i < draft.suggestions.size(); ++i) {
    auto &suggestion = draft.suggestions[i];
    suggestion.displayName = "Revisar: anuncio largo de muestra " + juce::String(int(i) + 1);
    suggestion.startSeconds = 15.0 + double(i) * secondsPerLabel;
  }
  wave.setGuideAlignmentPreview(draft);
  const auto first = WaveformEditingTestAccess::guideMarkerPill(wave, draft.suggestions[0].id);
  const auto second = WaveformEditingTestAccess::guideMarkerPill(wave, draft.suggestions[1].id);
  const auto third = WaveformEditingTestAccess::guideMarkerPill(wave, draft.suggestions[2].id);
  expect(first.getWidth() >= 139.0f && second.getWidth() >= 139.0f,
         "dense review fixture exercises full-width unknown-section labels");
  expect(first.getY() != second.getY() && !first.intersects(second) &&
             !second.intersects(third) && !first.intersects(third),
         "compact primary space retains two rows and keeps three dense unknown labels separately readable");
  for (const auto &cue : draft.secondaryCues) {
    const auto label = WaveformEditingTestAccess::guideMarkerPill(wave, cue.id);
    expect(!label.intersects(first) && !label.intersects(second) && !label.intersects(third),
           "the compact secondary lane remains clear of both musical label rows");
  }
  expect(fixture.commits == 0, "responsive guide layout never changes the arrangement");
}

void testViLaLuzSecondaryAlongsideSectionDuringPlayback() {
  using namespace playworship::analysis;
  Fixture fixture;
  auto &wave = fixture.waveform;
  constexpr double length = 244.1578231292517;
  double position = 52.6;
  bool playing = false;
  wave.setVisible(true);
  wave.setTransportProviders([&] { return position; }, [] { return length; }, [&] { return playing; });
  wave.setSongSourceLengthProvider([] { return length; });
  wave.setSongRhythm(116, "4/4");
  wave.setArrangement({}, false); wave.setSize(1200, 320);

  GuideTranscript transcript;
  transcript.provider = "synthetic-secondary-fixture"; transcript.providerVersion = "1";
  transcript.audioFingerprint = "vi-secondary-waveform-fixture";
  transcript.durationSeconds = length;
  transcript.segments = {{35.2,35.64,"Coro"}, {49.68,50.52,"Interludio"},
      {50.71,51.56,"Toda la banda."}, {57.96,58.84,"Verso 2"}};
  GuideAlignmentOptions options; options.originSeconds = .02; options.bpm = 116;
  auto draft = alignGuideSections(transcript, options);
  expect(draft.error.isEmpty() && draft.suggestions.size() == 3 && draft.secondaryCues.size() == 1,
         "separate spoken All In and Interlude produce both a secondary point and a musical boundary");
  const auto &allIn = draft.secondaryCues.front();
  expect(allIn.name == "All In", "Toda la banda resolves to the existing native All In instruction");
  expectNear(allIn.rawTimeSeconds, 50.71, "All In retains its own observed voice onset");
  expectNear(allIn.timeSeconds, 50.71,
             "All In remains at its measured spoken onset without a following-bar delay");
  const auto interlude = std::find_if(draft.suggestions.begin(), draft.suggestions.end(),
      [](const auto &suggestion) { return suggestion.type == SectionType::interlude; });
  expect(interlude != draft.suggestions.end(), "the Interlude proposal remains present beside All In");
  expectNear(interlude->startSeconds, .02 + 25 * 240.0 / 116,
             "Interlude starts on bar 26 after its own announcement");
  wave.setGuideAlignmentCallbacks({}, {}, [&] {
    const auto plan = appendConfirmedGuideSuggestions({}, draft);
    expect(plan.error.isEmpty() && wave.applyDetectedSections(plan.arrangement),
           "All In and its surrounding sections confirm through one existing arrangement transaction");
    wave.clearGuideAlignmentPreview();
  }, [&] { wave.clearGuideAlignmentPreview(); });
  wave.setGuideAlignmentPreview(draft);
  WaveformEditingTestAccess::fixedPlaybackViewport(wave, float(35.0 / length), float(27.0 / length));
  const auto expectedX = [&](double seconds) {
    const auto body = WaveformEditingTestAccess::waveBody(wave);
    const auto window = WaveformEditingTestAccess::visibleWindow(wave);
    return body.getX() + float((seconds / length - window.first) / window.second) * body.getWidth();
  };
  const auto expectVisibleSecondary = [&] {
    const auto body = WaveformEditingTestAccess::waveBody(wave);
    const auto point = WaveformEditingTestAccess::guideSecondaryPoint(wave, allIn.id);
    const auto label = WaveformEditingTestAccess::guideMarkerPill(wave, allIn.id);
    const auto sectionLabel = WaveformEditingTestAccess::guideMarkerPill(wave, interlude->id);
    expect(WaveformEditingTestAccess::guideMarkerCount(wave) == 4 &&
               body.contains(point) && body.contains(label) && body.contains(sectionLabel),
           "All In remains visible in the screenshot's bar range alongside Interlude and the other sections");
    expectNear(point.x, expectedX(allIn.timeSeconds), "the secondary point uses its exact measured source-time coordinate");
    expect(WaveformEditingTestAccess::guideHitId(wave, point) == allIn.id &&
               WaveformEditingTestAccess::guideHitId(wave, label.getCentre()) == allIn.id,
           "the All In point and label both retain their independent editable target");
  };
  expectVisibleSecondary();
  playing = true;
  WaveformEditingTestAccess::vblank(wave, juce::Time::getMillisecondCounterHiRes() + 40.0);
  expectVisibleSecondary();
  expect(fixture.commits == 0 && wave.getArrangementSnapshot().cues.empty(),
         "displaying All In during playback never implicitly accepts the review");
  const auto capturePath = juce::SystemStats::getEnvironmentVariable("PW_GUIDE_VI_SECONDARY_CAPTURE_PATH", {});
  if (capturePath.isNotEmpty()) {
    const juce::File file(capturePath);
    expect(file.getParentDirectory().createDirectory().wasOk(), "Vi la luz secondary capture directory opens");
    auto output = file.createOutputStream();
    expect(output && juce::PNGImageFormat().writeImageToStream(
        wave.createComponentSnapshot(wave.getLocalBounds(), true, 1.5f), *output),
        "native waveform renders the All In instruction beside Interlude during playback");
  }

  WaveformEditingTestAccess::guideAccept(wave).onClick();
  const auto accepted = wave.getArrangementSnapshot();
  expect(fixture.commits == 1 && accepted.parts.size() == 3 && accepted.cues.size() == 1,
         "one confirmation saves the main labels and independent All In together");
  const auto &saved = accepted.cues.front();
  expect(saved.id == allIn.id && saved.name == "All In" && saved.partId.isNotEmpty(),
         "the saved secondary retains its identity and anchors to the containing section");
  expectNear(resolveArrangementCueSeconds(accepted, saved), allIn.timeSeconds,
             "resolving a confirmed secondary's section-relative offset preserves its absolute source time");
  expectNear(WaveformEditingTestAccess::cuePoint(wave, saved.id).x, expectedX(allIn.timeSeconds),
             "saved editing renders All In at the same temporal coordinate as the provisional review");
  WaveformEditingTestAccess::nativeCueButton(wave, saved.id);
  expect(fixture.command('Z') && wave.getArrangementSnapshot().cues.empty() &&
             wave.getArrangementSnapshot().parts.empty(),
         "one undo restores the untouched song after this isolated secondary confirmation");
}

void testViLaLuzCountTemporalPositions() {
  using namespace playworship::analysis;
  Fixture fixture;
  auto &wave = fixture.waveform;
  constexpr double length = 244.1578231292517;
  wave.setVisible(true);
  wave.setTransportProviders([] { return 0.0; }, [] { return length; }, [] { return false; });
  wave.setSongSourceLengthProvider([] { return length; });
  wave.setSongRhythm(116, "4/4");
  wave.setArrangement({}, false); wave.setSize(1200, 320);
  GuideAlignmentDraft draft;
  draft.durationSeconds = length; draft.options.originSeconds = .02; draft.options.bpm = 116;
  for (int pulse = 0; .02 + pulse * 60.0 / 116 <= length; ++pulse)
    draft.grid.push_back({.02 + pulse * 60.0 / 116, pulse / 4 + 1, pulse % 4 + 1, pulse % 4 == 0});
  SectionSuggestion intro;
  intro.id = "vi-intro"; intro.type = SectionType::intro; intro.displayName = "Intro";
  intro.rawStartSeconds = 2.16; intro.startSeconds = .02 + 8 * 60.0 / 116;
  intro.endSeconds = length; draft.suggestions.push_back(intro);
  // First two observed source onsets and representative subsequent onsets.
  // This component test does not infer numbers or times from musical beats.
  const double times[]{.02, 1.06, 2.62, 3.13, 3.65};
  const char *names[]{"1", "2", "2", "3", "4"};
  for (int i = 0; i < 5; ++i) {
    GuideSecondaryCueSuggestion cue;
    cue.id = "vi-count-" + juce::String(i); cue.name = cue.rawText = names[i];
    cue.timeSeconds = cue.rawTimeSeconds = times[i]; draft.secondaryCues.push_back(cue);
  }
  wave.setGuideAlignmentCallbacks({}, {}, [&] {
    const auto plan = appendConfirmedGuideSuggestions({}, draft);
    expect(plan.error.isEmpty() && wave.applyDetectedSections(plan.arrangement),
           "timed count review confirms through the existing arrangement transaction");
    wave.clearGuideAlignmentPreview();
  }, [&] { wave.clearGuideAlignmentPreview(); });
  wave.setGuideAlignmentPreview(draft);
  // Keep the first count visible exactly at the viewport's left edge despite
  // the ratio's float representation. The packed chip must not shift its dot.
  WaveformEditingTestAccess::fixedPlaybackViewport(wave,
      std::nextafter(float(times[0] / length), 0.0f), float(9.0 / length));
  const auto expectedX = [&](double seconds) {
    const auto body = WaveformEditingTestAccess::waveBody(wave);
    const auto window = WaveformEditingTestAccess::visibleWindow(wave);
    return body.getX() + float((seconds / length - window.first) / window.second) * body.getWidth();
  };
  for (const auto &cue : draft.secondaryCues) {
    const auto point = WaveformEditingTestAccess::guideSecondaryPoint(wave, cue.id);
    const auto label = WaveformEditingTestAccess::guideMarkerPill(wave, cue.id);
    expectNear(point.x, expectedX(cue.timeSeconds),
               "count dots retain source-time x even when label packing shifts a chip");
    expect(WaveformEditingTestAccess::guideHitId(wave, point) == cue.id &&
               WaveformEditingTestAccess::guideHitId(wave, label.getCentre()) == cue.id,
           "both the real temporal point and its packed count chip select the same cue");
  }
  const auto first = WaveformEditingTestAccess::guideSecondaryPoint(wave, draft.secondaryCues[0].id);
  expectNear(first.x, WaveformEditingTestAccess::waveBody(wave).getX(),
             "a count at the viewport edge never acquires an artificial leading time offset");
  const auto capturePath = juce::SystemStats::getEnvironmentVariable("PW_GUIDE_VI_COUNT_CAPTURE_PATH", {});
  if (capturePath.isNotEmpty()) {
    const juce::File file(capturePath);
    expect(file.getParentDirectory().createDirectory().wasOk(), "timed count capture directory opens");
    auto output = file.createOutputStream();
    expect(output && juce::PNGImageFormat().writeImageToStream(
        wave.createComponentSnapshot(wave.getLocalBounds(), true, 1.5f), *output),
           "native waveform renders the 116 BPM count timing regression");
  }
  WaveformEditingTestAccess::guideAccept(wave).onClick();
  expect(wave.getArrangementSnapshot().cues.size() == 5 && fixture.commits == 1,
         "confirmation saves the exact five independent count points");
  for (const auto &cue : wave.getArrangementSnapshot().cues) {
    const auto point = WaveformEditingTestAccess::cuePoint(wave, cue.id);
    expectNear(point.x, expectedX(resolveArrangementCueSeconds(wave.getArrangementSnapshot(), cue)),
               "saved editing keeps count dots at persisted source times too");
    WaveformEditingTestAccess::nativeCueButton(wave, cue.id);
  }
  expect(fixture.command('Z') && wave.getArrangementSnapshot().cues.empty(),
         "one undo restores the original song after timed count confirmation");
}

void testClusteredGuideCountsWithInstructions() {
  using namespace playworship::analysis;
  Fixture fixture;
  auto &wave = fixture.waveform;
  constexpr double length = 337.391;
  // Native component hit dispatch requires a visible root. No desktop peer is
  // created; this remains an isolated component regression fixture.
  wave.setVisible(true);
  wave.setTransportProviders([] { return 0.0; }, [] { return 337.391; }, [] { return false; });
  wave.setSongSourceLengthProvider([] { return 337.391; });
  wave.setArrangement({}, false); wave.setSize(1200, 320);
  GuideAlignmentDraft draft;
  draft.durationSeconds = length; draft.options.originSeconds = 0; draft.options.bpm = 69;
  for (int pulse = 0; pulse * 60.0 / 69.0 <= length; ++pulse)
    draft.grid.push_back({pulse * 60.0 / 69.0, pulse / 4 + 1, pulse % 4 + 1, pulse % 4 == 0});
  for (int i = 0; i < 3; ++i) {
    SectionSuggestion section;
    section.id = "corrected-count-review-refrain-" + juce::String(i);
    section.displayName = "Refrain " + juce::String(i + 1);
    section.startSeconds = 219 + i * 14.0; section.endSeconds = i == 2 ? length : 233 + i * 14.0;
    draft.suggestions.push_back(section);
  }
  const double countTimes[]{0.0, 1.74, 4.34, 5.2, 6.08};
  for (int i = 0; i < 5; ++i) {
    GuideSecondaryCueSuggestion count;
    count.id = "clustered-guide-count-" + juce::String(i); count.name = juce::String(i + 1);
    count.rawText = "spoken count " + juce::String(i + 1);
    count.timeSeconds = count.rawTimeSeconds = countTimes[i]; draft.secondaryCues.push_back(count);
  }
  const char *names[]{"All In", "Build", "Drums In", "Breakdown", "Hold", "Bass", "Softly", "Last Time"};
  for (int i = 0; i < 8; ++i) {
    GuideSecondaryCueSuggestion instruction;
    instruction.id = "clustered-guide-instruction-" + juce::String(i);
    instruction.name = instruction.rawText = names[i];
    instruction.timeSeconds = instruction.rawTimeSeconds = 74.78 + i * 26.0;
    draft.secondaryCues.push_back(instruction);
  }
  wave.setGuideAlignmentCallbacks({}, {}, [&] {
    const auto plan = appendConfirmedGuideSuggestions(wave.getArrangementSnapshot(), draft);
    expect(plan.error.isEmpty() && wave.applyDetectedSections(plan.arrangement),
           "five counts and eight instructions share the existing atomic confirmation");
    wave.clearGuideAlignmentPreview();
  }, [&] { wave.clearGuideAlignmentPreview(); });
  int seeks = 0;
  wave.setSeekCallback([&](double) { ++seeks; });
  wave.setOnGuideSecondaryCueMove([&](const juce::String &id, double seconds, bool snap) {
    juce::String error;
    expect(moveGuideSecondaryCue(draft, id, seconds, snap, error), "a compact count chip uses the ordinary secondary gesture callback");
    wave.setGuideAlignmentPreview(draft);
  });
  wave.setGuideAlignmentPreview(draft);
  const auto initialBody = WaveformEditingTestAccess::waveBody(wave);
  wave.setSize(wave.getWidth(), wave.getHeight() + juce::roundToInt(130.0f - initialBody.getHeight()));
  WaveformEditingTestAccess::fixedPlaybackViewport(wave, 0, 1);
  const auto body = WaveformEditingTestAccess::waveBody(wave);
  expectNear(body.getHeight(), 130.0, "count overview fixture retains the compact 130-point body");
  expect(WaveformEditingTestAccess::guideMarkerCount(wave) == 16,
         "all five clustered counts, eight instructions and three corrected musical labels stay visible");
  auto image = wave.createComponentSnapshot(wave.getLocalBounds());
  const auto blue = juce::Colour::fromRGB(91, 169, 218);
  const auto amber = juce::Colour::fromRGB(224, 171, 91);
  for (size_t i = 0; i < draft.secondaryCues.size(); ++i) {
    const auto &cue = draft.secondaryCues[i];
    const auto point = WaveformEditingTestAccess::guideSecondaryPoint(wave, cue.id);
    const auto pill = WaveformEditingTestAccess::guideMarkerPill(wave, cue.id);
    expect(WaveformEditingTestAccess::guideHitId(wave, point) == cue.id,
           "every count and instruction can be selected independently at overview zoom");
    expect(image.getPixelAt(juce::roundToInt(point.x), juce::roundToInt(point.y)).withAlpha(1.0f) ==
               (i < 5 ? blue : amber), "counts use native blue while utility instructions retain amber");
    if (i < 5) {
      expect(pill.getWidth() <= 24.1f, "individual numeric counts use compact pills");
      expectNear(cue.timeSeconds, countTimes[i], "count layout preserves the exact spoken timeline position");
    }
    for (size_t j = 0; j < i; ++j)
      expect(!pill.intersects(WaveformEditingTestAccess::guideMarkerPill(wave, draft.secondaryCues[j].id)),
             "horizontal packing prevents dense count or instruction labels from overlapping");
  }
  for (const auto *literal : {"one two three four", "eight"}) {
    draft.secondaryCues.front().name = literal; wave.setGuideAlignmentPreview(draft);
    image = wave.createComponentSnapshot(wave.getLocalBounds());
    const auto point = WaveformEditingTestAccess::guideSecondaryPoint(wave, draft.secondaryCues.front().id);
    expect(image.getPixelAt(juce::roundToInt(point.x), juce::roundToInt(point.y)).withAlpha(1.0f) == blue &&
               WaveformEditingTestAccess::guideMarkerCount(wave) == 16,
           "literal compound or unsupported counts remain one editable blue point without invented beats");
  }
  draft.secondaryCues.front().name = "1"; wave.setGuideAlignmentPreview(draft);
  const auto start = WaveformEditingTestAccess::guideSecondaryPoint(wave, draft.secondaryCues[4].id);
  const auto target = start.translated(body.getWidth() * 0.25f / float(length), 0);
  wave.mouseDown(guidePointer(wave, start, start, false, true));
  wave.mouseDrag(guidePointer(wave, target, start, true, true));
  wave.mouseUp(guidePointer(wave, target, start, true, true));
  expectNear(draft.secondaryCues[4].timeSeconds, 6.33, "dragging a packed count chip moves only its source-time position");
  expect(fixture.commits == 0 && seeks == 0, "count review gestures neither seek nor publish provisional changes");
  WaveformEditingTestAccess::guideAccept(wave).onClick();
  const auto accepted = wave.getArrangementSnapshot();
  expect(fixture.commits == 1 && accepted.parts.size() == 3 && accepted.cues.size() == 13 &&
             arrangementCueEventCount(accepted) == 13, "confirmation persists exactly thirteen native individual cues with the musical sections");
  expect(accepted.cues.front().name == "1" && accepted.cues.front().positionSec == 0.0 &&
             accepted.cues.front().countEnd == 0, "the initial count remains a native source-zero point rather than a fabricated sequence");
  for (size_t i = 0; i < accepted.parts.size(); ++i)
    expect(accepted.parts[i].name == "Refrain " + juce::String(int(i) + 1),
           "adding and confirming count points retains all three manually corrected Refrains");
  const auto expectUnchanged = [&accepted](const SongArrangementSnapshot &actual,
                                           const std::vector<SongArrangementCueSnapshot> &cues) {
    expect(actual.cues == cues && actual.parts.size() == accepted.parts.size(),
           "saved cue presentation preserves every cue field and musical section");
    for (size_t i = 0; i < accepted.parts.size(); ++i) {
      const auto &a = actual.parts[i], &b = accepted.parts[i];
      expect(a.id == b.id && a.name == b.name && a.color == b.color &&
                 a.startSec == b.startSec && a.endSec == b.endSec &&
                 a.sourcePartId == b.sourcePartId && a.sourceStartSec == b.sourceStartSec &&
                 a.sourceEndSec == b.sourceEndSec && a.utilityCue == b.utilityCue &&
                 a.muteInstruments == b.muteInstruments && a.skipPlayback == b.skipPlayback &&
                 a.automation == b.automation,
             "compact native counts leave the corrected Refrains and all source/automation data intact");
    }
  };
  const auto expectSavedTargets = [&](WaveformBar &saved) {
    const auto savedBody = WaveformEditingTestAccess::waveBody(saved);
    expectNear(savedBody.getHeight(), 130.0, "saved count overview retains the 130-point body");
    expect(WaveformEditingTestAccess::markerCount(saved) == 13,
           "all thirteen confirmed cues retain native markers at overview zoom");
    WaveformEditingTestAccess::expectCueLabels(saved);
    for (size_t i = 0; i < accepted.cues.size(); ++i) {
      const auto &cue = accepted.cues[i];
      auto &button = WaveformEditingTestAccess::nativeCueButton(saved, cue.id);
      if (i < 5) {
        expect(WaveformEditingTestAccess::cueLabel(saved, cue.id).getWidth() <= 24.1f,
               "confirmed individual counts retain compact numeric chips");
        expect(button.getButtonText() == L10n::tr("Cuenta ", "Count ") + cue.name &&
                   button.getTitle() == button.getButtonText(),
               "compact count chips retain the full native accessible count label");
      }
      expectNear(WaveformEditingTestAccess::cueSourceX(saved, cue.id),
                 savedBody.getX() + resolveArrangementCueSeconds(accepted, cue) /
                     length * savedBody.getWidth(),
                 "packing keeps the connector at the exact persisted source time");
      WaveformEditingTestAccess::clickNativeCue(saved, cue.id);
    }
    expectUnchanged(saved.getArrangementSnapshot(), accepted.cues);
  };
  // The saved editor must retain the same five dense points that were visible
  // during review, rather than assigning the later counts rows below the body.
  expectSavedTargets(wave);
  expect(fixture.commits == 1 && seeks == 0,
         "selecting every confirmed native point adds no history, persistence or seek");
  expect(fixture.command('Z') && wave.getArrangementSnapshot().parts.empty() && wave.getArrangementSnapshot().cues.empty(),
         "one undo reverses counts, instructions and musical suggestions together");
  expect(fixture.command('Y') && wave.getArrangementSnapshot().cues == accepted.cues,
         "redo restores the native count and instruction identities without duplication");
  expectSavedTargets(wave);

  Fixture reopened;
  auto &saved = reopened.waveform;
  saved.setVisible(true);
  saved.setTransportProviders([] { return 0.0; }, [] { return 337.391; }, [] { return false; });
  saved.setSongSourceLengthProvider([] { return 337.391; });
  saved.setArrangement(accepted, true);
  const auto reopenedBody = WaveformEditingTestAccess::waveBody(saved);
  saved.setSize(saved.getWidth(), saved.getHeight() + juce::roundToInt(130.0f - reopenedBody.getHeight()));
  WaveformEditingTestAccess::fixedPlaybackViewport(saved, 0, 1);
  int savedSeeks = 0;
  saved.setSeekCallback([&](double) { ++savedSeeks; });
  expectSavedTargets(saved);
  expect(reopened.commits == 0 && savedSeeks == 0,
         "reopening and selecting all saved count and utility buttons never modifies the song");

  const auto countId = accepted.cues[4].id;
  juce::Component &native = WaveformEditingTestAccess::nativeCueButton(saved, countId);
  const auto sourcePoint = WaveformEditingTestAccess::cuePoint(saved, countId);
  const auto movedPoint = sourcePoint.translated(
      WaveformEditingTestAccess::waveBody(saved).getWidth() * 2.0f / float(length), 0.0f);
  native.mouseDown(pointerEvent(saved, sourcePoint, sourcePoint).getEventRelativeTo(&native));
  native.mouseDrag(pointerEvent(saved, movedPoint, sourcePoint, true).getEventRelativeTo(&native));
  native.mouseUp(pointerEvent(saved, movedPoint, sourcePoint, true).getEventRelativeTo(&native));
  auto expectedMoved = accepted.cues;
  expectedMoved[4].positionSec += 2.0;
  expectNear(WaveformEditingTestAccess::cue(saved, countId).positionSec,
             expectedMoved[4].positionSec,
             "dragging a saved packed count uses its original time plus pointer delta");
  // Pointer coordinates are floats; retain their exact resolved value for the
  // all-fields comparison while the independent time assertion checks intent.
  expectedMoved[4].positionSec = WaveformEditingTestAccess::cue(saved, countId).positionSec;
  expectUnchanged(saved.getArrangementSnapshot(), expectedMoved);
  expect(reopened.commits == 1 && savedSeeks == 0,
         "one native count drag creates one ordinary edit without seeking");
  expect(reopened.command('Z'), "the native packed count edit can be undone");
  expectUnchanged(saved.getArrangementSnapshot(), accepted.cues);
  expectSavedTargets(saved);
  expect(reopened.command('Y'), "the native packed count edit can be redone");
  expectUnchanged(saved.getArrangementSnapshot(), expectedMoved);
}

void testDetectedSecondaryCueTransactions() {
  Fixture fixture;
  SongArrangementSnapshot base;
  for (int i = 0; i < 3; ++i) {
    auto part = original(("protected-refrain-" + juce::String(i)).toRawUTF8(), i * 10.0, (i + 1) * 10.0);
    part.name = "Refrain " + juce::String(i + 1); part.automation.pads.volume = 0.25f;
    base.parts.push_back(part);
  }
  SongArrangementCueSnapshot count;
  count.id = "protected-count"; count.name = "1"; count.partId = base.parts.front().id;
  count.positionSec = count.offsetSec = 1.0; count.countEnd = 4; count.countIntervalSec = 0.5;
  count.countOffsetsSec = {0.0, 0.5, 1.0, 1.5};
  base.cues.push_back(count);
  fixture.waveform.setArrangement(base, true); base = fixture.waveform.getArrangementSnapshot();
  auto candidate = base;
  SongArrangementCueSnapshot added;
  added.id = "accepted-secondary"; added.name = "All In";
  added.positionSec = 15; added.partId = base.parts[1].id; added.offsetSec = 5;
  candidate.cues.push_back(added);
  auto invalid = candidate; invalid.cues.front().name = "2";
  expect(!fixture.waveform.applyDetectedSections(invalid) && fixture.commits == 0,
         "detected secondary confirmation cannot modify an existing anchored count");
  invalid = candidate; invalid.cues.back().id = base.parts.front().id;
  expect(!fixture.waveform.applyDetectedSections(invalid), "new secondary IDs cannot collide with musical IDs");
  invalid = candidate; invalid.cues.back().positionSec = std::numeric_limits<double>::quiet_NaN();
  expect(!fixture.waveform.applyDetectedSections(invalid), "nonfinite secondary positions reject the complete transaction");
  invalid = candidate; invalid.cues.back().positionSec = 30; invalid.cues.back().partId.clear(); invalid.cues.back().offsetSec = 0;
  expect(!fixture.waveform.applyDetectedSections(invalid), "a secondary cannot be placed at the exclusive song end");
  invalid = candidate; invalid.cues.back().offsetSec = 4;
  expect(!fixture.waveform.applyDetectedSections(invalid), "secondary anchors must resolve to their explicit source position");
  invalid = candidate; invalid.cues.back().partId = "missing-parent";
  expect(!fixture.waveform.applyDetectedSections(invalid), "a missing secondary parent cannot silently become absolute");
  invalid = candidate; std::reverse(invalid.cues.begin(), invalid.cues.end());
  expect(!fixture.waveform.applyDetectedSections(invalid), "unordered new cue points reject confirmation atomically");
  expect(fixture.waveform.applyDetectedSections(candidate) && fixture.commits == 1,
         "an additive secondary confirms while all existing musical ranges stay untouched");
  expect(fixture.waveform.getArrangementSnapshot().cues.front() == base.cues.front(),
         "accepting a secondary preserves prior count offsets, anchor and metadata exactly");
  const auto confirmed = fixture.waveform.getArrangementSnapshot();
  for (size_t i = 0; i < base.parts.size(); ++i) {
    const auto &part = confirmed.parts[i];
    expect(part.id == base.parts[i].id && part.name == base.parts[i].name &&
               part.startSec == base.parts[i].startSec && part.endSec == base.parts[i].endSec &&
               part.automation == base.parts[i].automation,
           "cue-only confirmation preserves all three corrected Refrains and their automation");
  }
  expect(!fixture.waveform.applyDetectedSections(candidate) && fixture.commits == 1,
         "reconfirming identical secondary points creates no duplicates or extra history");
  expect(fixture.command('Z') && fixture.waveform.getArrangementSnapshot().cues == base.cues,
         "one undo restores all prior anchored cues without changing protected sections");
  expect(fixture.command('Y') && fixture.waveform.getArrangementSnapshot().cues == confirmed.cues,
         "redo restores the newly accepted secondary and every original count offset");

  Fixture mixed;
  SongArrangementSnapshot middle; middle.parts = {original("manual-middle", 10, 20)};
  mixed.waveform.setArrangement(middle, true); middle = mixed.waveform.getArrangementSnapshot();
  auto combined = middle;
  combined.parts.insert(combined.parts.begin(), original("detected-intro", 0, 10));
  combined.parts.push_back(original("detected-ending", 20, 30));
  added.id = "detected-band"; added.positionSec = 5; added.partId = "detected-intro"; added.offsetSec = 5;
  combined.cues.push_back(added);
  expect(mixed.waveform.applyDetectedSections(combined) && mixed.commits == 1,
         "musical sections and secondary points publish together in one transaction");
  expect(mixed.command('Z') && mixed.waveform.getArrangementSnapshot().parts.size() == 1 &&
             mixed.waveform.getArrangementSnapshot().cues.empty(), "one undo reverses both parts and cues together");
  expect(mixed.command('Y') && mixed.waveform.getArrangementSnapshot().parts.size() == 3 &&
             mixed.waveform.getArrangementSnapshot().cues.size() == 1, "redo restores the combined detection once");

  Fixture capacity;
  auto many = base; many.cues.clear();
  for (int i = 0; i < 73; ++i) {
    auto sequence = count; sequence.id = "protected-sequence-" + juce::String(i);
    sequence.positionSec = sequence.offsetSec = 1.0 + i * 0.05; sequence.countEnd = 7;
    sequence.countOffsetsSec = {0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6};
    many.cues.push_back(sequence);
  }
  capacity.waveform.setArrangement(many, true); many = capacity.waveform.getArrangementSnapshot();
  expect(arrangementCueEventCount(many) == 511, "capacity fixture counts expanded events rather than markers");
  for (int i = 0; i < 2; ++i) {
    SongArrangementCueSnapshot extra; extra.id = "overflow-secondary-" + juce::String(i);
    extra.name = "Build"; extra.positionSec = 20.0 + i; many.cues.push_back(extra);
  }
  expect(!capacity.waveform.applyDetectedSections(many) && capacity.commits == 0,
         "secondary additions exceeding 512 expanded events are rejected without partial publication");
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

void testBlankTimelineWithoutWaveform() {
  WaveformBar bar;
  double position = 3600.0;
  bar.setTransportProviders([&] { return position; }, [] { return 0.0; }, [] { return true; });
  bar.setGuideAlignmentPreview(guideDraftFixture());
  expect(bar.hasGuideAlignmentPreview(), "normal timeline accepts a transient guide review");
  bar.setBlankSong(true); bar.setWaveformBins({0.1f, 0.8f, 0.2f});
  expect(!bar.hasGuideAlignmentPreview(), "loading a blank song discards a transient guide review");
  bar.setGuideAlignmentPreview(guideDraftFixture());
  expect(!bar.hasGuideAlignmentPreview(), "stale guide results cannot reopen review for a blank song");
  SongArrangementSnapshot stale;
  stale.parts = {original("old-blank-intro", 0.0, 8.0)};
  bar.setArrangement(stale, true);
  for (int width : {320, 390, 768, 1200}) {
    bar.setSize(width, 180);
    bar.setLiveMode(true);
    expect(!bar.isVisible() && !bar.canToggleLiveMode() &&
        WaveformEditingTestAccess::hasNoWaveform(bar) &&
        WaveformEditingTestAccess::liveBounds(bar).empty(),
        "blank exposes no waveform, sections, ruler or section-view action at any width");
    for (auto *child : bar.getChildren())
      expect(!child->isVisible(), "blank hides every waveform and section action");
    // Paint directly as well: no empty-state text, free/bar label, progress
    // needle or remote badge may leak into this deliberately empty area.
    const auto image = bar.createComponentSnapshot(bar.getLocalBounds());
    expect(image.isValid(), "blank area snapshot is valid");
    for (int y = 0; y < image.getHeight(); y += 3)
      for (int x = 0; x < image.getWidth(); x += 3)
        expect(image.getPixelAt(x, y) == UITheme::panelDark,
               "blank area contains only its background");
  }
  bar.setBlankSong(false); bar.setWaveformBins({0.1f, 0.8f, 0.2f});
  bar.setArrangement(stale, true); bar.setLiveMode(true);
  expect(bar.isVisible() && bar.canToggleLiveMode() &&
      WaveformEditingTestAccess::liveBounds(bar).size() == 1,
      "loading a normal song restores waveform visibility and existing section controls");

  ProcessorFixture fixture; TopBar top(fixture.processor);
  ImportedSongData song; song.id = "blank-menu"; song.title = "Blank";
  song.blankSong = true; song.bpm = 120; song.key = "C";
  top.setTransportProviders([] { return true; }, [] { return 3600.0; },
      [] { return 0.0; }, [] { return juce::String("C"); },
      [&] { return song.id; }, [&] { return &song; });
  int toggles = 0;
  top.setLiveModeControls([&] { ++toggles; }, [] { return false; }, [] { return true; });
  for (bool phone : {false, true}) {
    top.setMinimalPhoneMode(phone); top.setSize(phone ? 390 : 1200, 80);
    expect(!WaveformEditingTestAccess::sectionsChoice(top).isVisible() &&
        !WaveformEditingTestAccess::waveformChoice(top).isVisible(),
        "blank hides desktop Waveform/Sections choices");
    auto panel = WaveformEditingTestAccess::morePanel(top);
    expect(!panel->findChildWithID("timeline-sections")->isVisible(),
        "blank hides the section action in the phone/tablet More menu");
    WaveformEditingTestAccess::chooseTimeline(top, true);
    expect(toggles == 0, "blank cannot enter section view through stale callbacks");
  }
  song.blankSong = false;
  top.setMinimalPhoneMode(false); top.setSize(1280, 80);
  expect(WaveformEditingTestAccess::sectionsChoice(top).isVisible(),
      "normal song restores the standard desktop section selector");
}

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
  run("topbar decimal tempo entry and reopening", testTopbarDecimalTempoEditing);
  run("metronome decimal tempo entry", testMetronomePopoverDecimalTempoEditing);
  run("regional decimal tempo entry", testRegionalDecimalTempoEditing);
  run("playback dirty regions match complete waveform paint", testPlaybackDirtyRegionsMatchFullPaint);
  run("playback cache reuse and source invalidation", testPlaybackCacheReuseAndInvalidation);
  run("playback limits reuse cache without baked shading", testPlaybackLimitsReuseCacheWithoutBakingShading);
  run("cue counts follow regional tempo grid", testCueSequenceUsesRegionalPulseGrid);
  run("blank area stays empty and hides section actions", testBlankTimelineWithoutWaveform);
  run("cue capacity rejects copy and paste atomically", testCueCapacityRejectsCopyWithoutChangingTimeline);
  run("cue component previews", testCueVisualPreview);
  run("section detection preview, safe confirmation and undo", testDetectedSectionsReviewAndHistory);
  run("guide waveform review, origin and marker gestures, confirmation and undo", testGuideAlignmentWaveformReview);
  run("guide review ruler and focused Space reach real transport", testGuideReviewRulerSpaceReachesTransport);
  run("manual guide section insertion, cancellation, confirmation and history", testGuideReviewManualSectionInsertionAndHistory);
  run("calculated guide grid, live origin and boundary edits, atomic confirmation", testCalculatedGuideAlignmentReview);
  run("closed guide catalogue, pending points, role selection and atomic history", testClosedGuideCatalogueReviewAndRoleSelection);
  run("pending guide review focuses once without changing zoom or playback", testPendingGuideReviewFocusesOnlyOnEntry);
  run("secondary guide points, isolated gestures and provisional review", testGuideSecondaryWaveformReview);
  run("dense primary guide labels remain readable above compact secondary lane", testDenseGuideLabelsWithSecondaryLane);
  run("All In stays independently visible alongside Interlude while playing and after confirmation", testViLaLuzSecondaryAlongsideSectionDuringPlayback);
  run("116 BPM counts retain temporal dots while chips pack at viewport edge", testViLaLuzCountTemporalPositions);
  run("five clustered guide counts and eight instructions remain visible and confirm atomically", testClusteredGuideCountsWithInstructions);
  run("additive detected secondary cues and atomic arrangement history", testDetectedSecondaryCueTransactions);
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
  run("desktop toolbar preserves actions and title", testDesktopToolbarPreservesActionsAndTitle);
  run("Live titles remain invariant through scroll and inertia", testLiveTitleLayoutInvariantDuringScroll);
  run("section view states, accessibility tree and keyboard", testSectionViewStatesAccessibilityAndKeyboard);
  run("section view readable states and responsive previews", testSectionViewReadableStatesPreview);
  run("adaptive centered Live titles", testAdaptiveLiveTitles);
  run("live inertial navigation", testLiveInertialNavigation);
  run("desktop normal actions and discrete live follow", testDesktopNormalAndLiveFollow);
  run("reduced motion centers Live immediately", testReducedMotionLiveFollow);
  run("live actions survive layout refresh", testLiveActionsSurviveLayoutRefresh);
  run("pending Live priority and combined state visuals", testPendingLivePriorityAndCombinedStates);
  run("Loop border phase and corner continuity", testLoopBorderPhaseAndCorners);
  run("live actions reach real transport", testLiveActionsReachRealTransport);
  run("stable section indicator mapping and rejected feedback", testStableSectionIndicatorMapping);
  run("Master leading boundary", testMasterLeadingBoundary);
  run("live originals preserve waveform beyond source duration", testLiveWaveformForOriginalsShiftedBeyondSource);
  run("live responsive complete cards and settled navigation", testLiveResponsiveCompleteCardsAndNavigation);
  run("More section toggle synchronizes state and capability", testSectionToggleMoreMenu);
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
