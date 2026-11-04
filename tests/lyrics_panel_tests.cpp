#include "ui/panels/LyricsPanel.h"
#include "ui/panels/FreeShowConnectionPanel.h"
#include "ui/panels/WaveformBar.h"
#include "data/ApplicationDataPaths.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

#if !JUCE_IOS && !JUCE_ANDROID
using namespace playworship::lyrics;
namespace {
void expect(bool passed, const char *message) { if (!passed) throw std::runtime_error(message); }
juce::MouseEvent event(juce::Component &component, juce::Point<float> point,
                       juce::Point<float> start, bool dragged = false,
                       int modifiers = juce::ModifierKeys::leftButtonModifier) {
  const juce::Time now(1000000);
  return {juce::Desktop::getInstance().getMainMouseSource(), point,
          modifiers, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
          &component, &component, now, start, now, 1, dragged};
}
}
struct WaveformEditingTestAccess {
  static auto window(const WaveformBar &bar) { return bar.getVisibleWindow(); }
  static void window(WaveformBar &bar, float start, float span) { bar.setVisibleWindow(start, span); }
  static juce::Point<float> navigationPoint(const WaveformBar &bar, float ratio, bool ruler) {
    const auto main = bar.getMainWaveBounds(bar.getLocalBounds().toFloat());
    const auto body = bar.getMainWaveBodyBounds(main);
    return {body.getX() + ratio * body.getWidth(), ruler ? bar.getSeekRulerBounds(main).getCentreY() : body.getCentreY()};
  }
  static void checkNavigation(WaveformBar &bar) {
    bar.setVisibleWindow(0.2f, 0.25f);
    const auto body = bar.getMainWaveBodyBounds(bar.getMainWaveBounds(bar.getLocalBounds().toFloat()));
    const auto point = body.getRelativePoint(0.37f, 0.5f);
    const auto pointer = event(bar, point, point);
    auto window = bar.getVisibleWindow();
    const float anchor = window.first + 0.37f * window.second;
    bar.mouseMagnify(pointer, 0.0f);
    expect(bar.getVisibleWindow() == window, "Waveform preserves its existing zero-magnify no-op");
    bar.mouseMagnify(pointer, 0.25f);
    window = bar.getVisibleWindow();
    expect(std::abs(window.second - 0.2f) < 0.00001f
        && std::abs(window.first + 0.37f * window.second - anchor) < 0.00001f,
           "Waveform preserves its existing 1 + amount magnify contract around the cursor");
    bar.mouseMagnify(pointer, -0.2f);
    expect(std::abs(bar.getVisibleWindow().second - 0.25f) < 0.00001f,
           "Negative magnification retains the waveform's existing zoom-out behavior");
    bar.mouseMagnify(pointer, 1.0f);
    expect(std::abs(bar.getVisibleWindow().second - 0.125f) < 0.00001f,
           "Compatibility: magnify input 1 still produces factor 2 in the existing waveform");
    bar.setVisibleWindow(0.2f, 0.25f);
    juce::MouseWheelDetails wheel{}; wheel.deltaY = 0.06f;
    bar.mouseWheelMove(pointer, wheel);
    window = bar.getVisibleWindow();
    expect(window.second < 0.25f
        && std::abs(window.first + 0.37f * window.second - anchor) < 0.00001f,
           "Unmodified vertical wheel zooms the waveform around the cursor");
    expect(std::abs(window.first - 0.216220148f) < 0.000001f
        && std::abs(window.second - 0.206161763f) < 0.000001f,
           "Frozen pre-change waveform viewport preserves its original wheel sensitivity");
    wheel.deltaX = -0.08f; wheel.deltaY = 0.06f;
    bar.mouseWheelMove(pointer, wheel);
    expect(bar.getVisibleWindow().first > window.first
        && std::abs(bar.getVisibleWindow().second - window.second) < 0.00001f,
           "The shared diagonal classification pans the waveform without changing zoom");
    expect(std::abs(bar.getVisibleWindow().first - 0.220429146f) < 0.000001f,
           "Frozen pre-change waveform pan preserves its original sensitivity and multiplication order");
    bar.setVisibleWindow(0.0f, 1.0f);
  }
};
struct LyricsPanelTestAccess {
  static juce::Component &map(LyricsPanel &panel) { return *panel.viewport.getViewedComponent(); }
  static juce::Point<float> point(const LyricsPanel &panel, const juce::String &id) {
    for (const auto &p : panel.placements) if (p.id == id) return p.point;
    throw std::runtime_error("Missing mark geometry");
  }
  static void checkPreviews(const LyricsPanel &panel) {
    expect(panel.placements.size() == panel.lyrics.marks.size(), "Every mark has a visible preview");
    for (size_t i = 0; i < panel.placements.size(); ++i) {
      const auto &p = panel.placements[i];
      expect(p.preview.getX() <= p.point.x && p.preview.getRight() >= p.point.x,
             "Connector belongs to the preview directly below its exact time point");
      for (size_t j = i + 1; j < panel.placements.size(); ++j)
        expect(!p.preview.intersects(panel.placements[j].preview), "Preview cards never overlap");
    }
  }
  static bool sameSections(const LyricsPanel &panel, const SongArrangementSnapshot &before) {
    if (panel.arrangement.parts.size() != before.parts.size()) return false;
    for (size_t i = 0; i < before.parts.size(); ++i)
      if (panel.arrangement.parts[i].startSec != before.parts[i].startSec
          || panel.arrangement.parts[i].endSec != before.parts[i].endSec
          || panel.arrangement.parts[i].id != before.parts[i].id) return false;
    return true;
  }
  static void select(LyricsPanel &panel, const juce::String &id) { panel.selectMark(id); }
  static void catalog(LyricsPanel &panel, ShowCatalog catalog) { panel.catalogue = std::move(catalog); panel.loadingCatalogue = false; panel.catalogueError.clear(); panel.rebuildShowSelectors(); }
  static void menu(LyricsPanel &panel, const juce::String &id) { panel.showMapMenu(point(panel, id), id); }
  static void picker(LyricsPanel &panel, const juce::String &id) { panel.showDestinationMenu(0.0, id); }
  static void explicitTest(LyricsPanel &panel) { panel.testSelected(); }
  static void refresh(LyricsPanel &panel) { panel.timerCallback(); }
  static juce::String text(const LyricsPanel &panel, const juce::String &id) { return panel.previewText(*panel.findMark(id)); }
  static juce::String review(const LyricsPanel &panel, const juce::String &id) { return panel.reviewReason(*panel.findMark(id)); }
  static void catalogueFailure(LyricsPanel &panel, juce::String message) { panel.catalogueError = std::move(message); panel.repaint(); }
  static bool testEnabled(const LyricsPanel &panel) { const auto *mark = panel.findMark(panel.selectedId); return mark != nullptr && panel.canTestMark(*mark); }
  static double insertionTime(const LyricsPanel &panel) {
    expect(panel.insertionAnchor.valid && panel.addButton.isVisible(), "The pointer exposes its contextual +");
    expect(panel.addButton.getParentComponent() == panel.viewport.getViewedComponent(), "The + belongs to the timeline, not its header");
    return panel.insertionAnchor.seconds;
  }
  static juce::Point<float> timelinePoint(const LyricsPanel &panel, double seconds, float y = 36.0f) {
    return {panel.timeToX(seconds), y};
  }
  static void checkHeader(const LyricsPanel &panel) {
    expect(panel.showCombo.getY() == panel.connectionButton.getY()
        && panel.showCombo.getBottom() <= panel.viewport.getY(), "Show and connection occupy one compact row");
    expect(!panel.showCombo.getBounds().intersects(panel.connectionButton.getBounds())
        && !panel.showCombo.getBounds().intersects(panel.enabledToggle.getBounds()), "Header controls do not overlap");
    expect(panel.showCombo.getWidth() <= 220, "Show selector has a compact fixed maximum width");
    expect(panel.viewport.getBottom() == panel.getHeight() - 6, "The map uses the removed detail panel space even when a mark is selected");
    expect(panel.viewport.getY() == 44, "No zoom control row consumes map height at any width");
    int buttons = 0;
    for (auto *child : panel.getChildren())
      if (dynamic_cast<juce::TextButton *>(child) != nullptr && child->isVisible()) ++buttons;
    expect(buttons == 2, "Only connection and catalogue refresh remain as header buttons");
  }
  static void checkShowNames(LyricsPanel &panel) {
    panel.shows = {{panel.lyrics.showId, panel.lyrics.showName}, {"opaque-other-id", "Noche de ensayo"}};
    panel.rebuildShowSelectors();
    expect(panel.showCombo.getText() == panel.lyrics.showName
        && panel.showCombo.getItemText(1) == "Noche de ensayo", "Selected and dropdown shows display names without technical IDs");
    const auto shows = panel.shows; panel.shows.clear(); panel.rebuildShowSelectors();
    expect(panel.showCombo.getText() == panel.lyrics.showName, "Saved show fallback also displays only its name");
    panel.shows = shows; panel.rebuildShowSelectors();
  }
  static void checkMarkMenu(LyricsPanel &panel, const juce::String &id, bool expectedTest) {
    auto menu = panel.buildMarkMenu(*panel.findMark(id));
    juce::PopupMenu::MenuItemIterator iterator(menu); juce::StringArray actions;
    while (iterator.next()) {
      const auto &item = iterator.getItem();
      if (item.itemID <= 0) continue;
      actions.add(item.text);
      if (item.itemID == 7) expect(item.isEnabled == expectedTest, "Contextual Test preserves destination/connection validation");
    }
    expect(actions == juce::StringArray{juce::String::fromUTF8("Cambiar destino FreeShow…"), "Probar en FreeShow", "Eliminar marca"},
           "Mark menu contains exactly destination, explicit test and delete");
  }
  static void checkGestureParity(LyricsPanel &panel, WaveformBar &waveform, int &changed) {
    const auto saved = juce::JSON::toString(toVar(panel.lyrics), true);
    const auto selected = panel.selectedId;
    const int edits = changed;
    auto &m = map(panel);
    struct WheelCase { float x, y; bool smooth, inertial, reversed; };
    const WheelCase wheels[] = {
      {0, 0, false, false, false}, {0, 0.06f, false, false, false},
      {0, -0.06f, true, false, true}, {-0.08f, 0, true, true, false},
      {0.08f, 0, false, false, true}, {0.0629f, 0.06f, true, false, false},
      {0.0631f, 0.06f, true, false, true}, {-0.08f, 0.06f, true, true, true},
      {0.08f, 0.08f, false, false, false}, {0.02f, -0.08f, true, false, false},
      {0, 2.0f, false, false, false}, {0, -2.0f, false, true, true},
      {-4.0f, 0, false, false, false}, {4.0f, 0, true, true, true}
    };
    const int modifiers[] = {0, juce::ModifierKeys::shiftModifier, juce::ModifierKeys::ctrlModifier,
      juce::ModifierKeys::commandModifier, juce::ModifierKeys::altModifier,
      juce::ModifierKeys::shiftModifier | juce::ModifierKeys::commandModifier};
    const std::pair<float, float> initialWindows[] = {{0.0f, 1.0f}, {0.2f, 0.25f}, {17.0f / 18.0f, 1.0f / 18.0f}};
    int comparisons = 0;
    float largestErrorPixels = 0.0f;
    const auto timeWidth = [&] { return panel.timeToX(panel.duration) - panel.timeToX(0.0); };
    const auto lyricsWindow = [&] {
      const float left = panel.viewport.getViewPositionX() + 18.0f;
      const float right = panel.viewport.getViewPositionX() + panel.viewport.getViewWidth() - 18.0f;
      const auto start = panel.xToTime(left) / panel.duration;
      return std::pair<float, float>{static_cast<float>(start), static_cast<float>(panel.xToTime(right) / panel.duration - start)};
    };
    for (int width : {650, 1150}) {
      panel.setSize(width, 500);
      for (const auto initial : initialWindows) for (bool ruler : {false, true})
        for (float anchor : {-0.01f, 0.0f, 0.37f, 0.999f, 1.01f}) for (int mods : modifiers) {
          const auto reset = [&] {
            panel.setTimelineWindow(initial.first, initial.second);
            const auto actual = lyricsWindow();
            WaveformEditingTestAccess::window(waveform, actual.first, actual.second);
          };
          const auto mapPointer = [&] {
            const float inset = 18.0f;
            return juce::Point<float>{panel.viewport.getViewPositionX() + inset
                + anchor * (panel.viewport.getViewWidth() - 2.0f * inset), ruler ? 10.0f : 200.0f};
          };
          const auto compare = [&](const char *kind, float x, float y) {
            const auto actual = lyricsWindow(), expected = WaveformEditingTestAccess::window(waveform);
            const float error = juce::jmax(std::abs(actual.first - expected.first), std::abs(actual.second - expected.second)) * timeWidth();
            largestErrorPixels = juce::jmax(largestErrorPixels, error);
            if (error > 1.25f) {
              std::cerr << "Parity " << kind << " input=" << x << ',' << y << " width=" << width
                  << " ruler=" << ruler << " anchor=" << anchor << " mods=" << mods
                  << " actual=" << actual.first << ',' << actual.second << " waveform=" << expected.first
                  << ',' << expected.second << " errorPixels=" << error << '\n';
              expect(false, "Identical gesture events must produce the waveform viewport within pixel rounding");
            }
            ++comparisons;
          };
          for (const auto input : wheels) {
            reset();
            juce::MouseWheelDetails wheel{};
            wheel.deltaX = input.x; wheel.deltaY = input.y; wheel.isSmooth = input.smooth;
            wheel.isInertial = input.inertial; wheel.isReversed = input.reversed;
            const auto p = mapPointer(), w = WaveformEditingTestAccess::navigationPoint(waveform, anchor, ruler);
            waveform.mouseWheelMove(event(waveform, w, w, false, mods), wheel);
            m.mouseWheelMove(event(m, p, p, false, mods), wheel);
            compare("wheel", input.x, input.y);
          }
          for (float amount : {0.0f, 0.1f, -0.1f, 0.8f, 1.0f, -0.5f, 100.0f, -0.99f}) {
            reset();
            const auto p = mapPointer(), w = WaveformEditingTestAccess::navigationPoint(waveform, anchor, ruler);
            waveform.mouseMagnify(event(waveform, w, w, false, mods), amount);
            m.mouseMagnify(event(m, p, p, false, mods), amount);
            compare("magnify", amount, 0.0f);
          }
        }
    }
    for (int width : {650, 1150}) {
      panel.setSize(width, 500); panel.setTimelineWindow(0.3f, 0.25f);
      const auto initial = lyricsWindow();
      WaveformEditingTestAccess::window(waveform, initial.first, initial.second);
      for (int i = 0; i < 600; ++i) {
        juce::MouseWheelDetails wheel{}; wheel.isSmooth = true;
        wheel.isInertial = i % 2 != 0; wheel.isReversed = i % 3 == 0;
        if (i < 400 || i % 3 == 0) wheel.deltaX = -0.0005f;
        else wheel.deltaY = i % 3 == 1 ? 0.002f : -0.002f;
        const auto w = WaveformEditingTestAccess::navigationPoint(waveform, 0.37f, false);
        const juce::Point<float> p{panel.viewport.getViewPositionX() + 18.0f + 0.37f * (panel.viewport.getViewWidth() - 36.0f), 200.0f};
        const int mods = i % 2 == 0 ? 0 : juce::ModifierKeys::commandModifier;
        waveform.mouseWheelMove(event(waveform, w, w, false, mods), wheel);
        m.mouseWheelMove(event(m, p, p, false, mods), wheel);
        const auto actual = lyricsWindow(), expected = WaveformEditingTestAccess::window(waveform);
        const float error = juce::jmax(std::abs(actual.first - expected.first), std::abs(actual.second - expected.second)) * timeWidth();
        largestErrorPixels = juce::jmax(largestErrorPixels, error);
        if (error > 1.25f) {
          std::cerr << "Sequence parity step=" << i << " width=" << width << " errorPixels=" << error << '\n';
          expect(false, "Small consecutive trackpad events must retain fractional motion and zoom/pan parity");
        }
        ++comparisons;
      }
    }
    expect(juce::JSON::toString(toVar(panel.lyrics), true) == saved && changed == edits && panel.selectedId == selected,
           "Gesture parity preserves all marks, selection and edit count");
    panel.setSize(1150, 500); panel.setTimelineWindow(0.0f, 1.0f); panel.viewport.setViewPosition(0, 0);
    WaveformEditingTestAccess::window(waveform, 0.0f, 1.0f);
    std::cout << "PASS: " << comparisons << " waveform/Lyrics gesture parity events; max error " << largestErrorPixels << " logical pixels\n";
  }
  static void checkZoom(LyricsPanel &panel, const Target &target, int &changed) {
    const auto before = panel.lyrics;
    const auto saved = juce::JSON::toString(toVar(before), true);
    const int edits = changed;
    auto &m = map(panel);
    const float anchor = panel.timeToX(90.0);
    const float screenAnchor = anchor - panel.viewport.getViewPositionX();
    const juce::Point<float> rulerAnchor{anchor, 10.0f};
    m.mouseMagnify(event(m, rulerAnchor, rulerAnchor), 3.0f);
    expect(panel.zoomFactor == 4.0 && panel.viewport.getViewPositionX() > 0,
           "Horizontal zoom enlarges the map and reveals navigation without moving marks");
    expect(std::abs(panel.timeToX(90.0) - panel.viewport.getViewPositionX() - screenAnchor) <= 0.51f,
           "Pinch keeps the musical time under the same physical cursor pixel");
    const auto cursor = [&] { return juce::Point<float>{panel.viewport.getViewPositionX() + screenAnchor, 210.0f}; };
    juce::MouseWheelDetails wheel{}; wheel.deltaY = 0.06f;
    auto p = cursor(); const auto cursorTime = panel.xToTime(p.x);
    m.mouseWheelMove(event(m, p, p), wheel);
    expect(panel.zoomFactor > 4.0
        && std::abs(panel.timeToX(cursorTime) - panel.viewport.getViewPositionX() - screenAnchor) <= 0.51f,
           "Vertical wheel over the map body zooms around the cursor without a modifier");
    wheel.deltaY = -0.06f; p = cursor(); m.mouseWheelMove(event(m, p, p), wheel);
    expect(std::abs(panel.zoomFactor - 4.0) < 0.00001, "Reversing the wheel zooms back out");
    wheel = {}; p = cursor(); const auto beforeZero = panel.viewport.getViewPosition();
    m.mouseWheelMove(event(m, p, p), wheel);
    expect(panel.viewport.getViewPosition() == beforeZero, "Zero wheel input does not move the viewport");
    panel.viewport.setViewPosition(juce::roundToInt(panel.timeToX(80)), 0);
    for (double seconds : {0.0, 45.123, 96.5, 179.999, 180.0})
      expect(std::abs(panel.xToTime(panel.timeToX(seconds)) - seconds) < 0.00005, "Zoomed pixel-to-time conversion remains precise at both endpoints and fractional times");
    expect(juce::JSON::toString(toVar(panel.lyrics), true) == saved && changed == edits,
           "Zoom and horizontal scrolling never persist an edit");
    double requestedPlay = -1.0;
    panel.onTimelinePlayRequested = [&](double seconds) { requestedPlay = seconds; };
    const auto playPoint = timelinePoint(panel, 90.0, 10.0f);
    m.mouseDown(event(m, playPoint, playPoint));
    m.mouseUp(event(m, playPoint, playPoint));
    expect(std::abs(requestedPlay - 90.0) < 0.001 && !panel.insertionAnchor.valid,
           "Ruler click requests playback at its time without an insertion pointer");
    panel.onTimelinePlayRequested = {};
    const int beforePan = panel.viewport.getViewPositionX();
    wheel.deltaX = -0.25f;
    const auto rulerPoint = timelinePoint(panel, 90.0, 10.0f);
    m.mouseWheelMove(event(m, rulerPoint, rulerPoint), wheel);
    expect(panel.viewport.getViewPositionX() > beforePan && std::abs(panel.zoomFactor - 4.0) < 0.00001,
           "Horizontal wheel navigation also pans over the ruler without changing zoom");
    wheel.isSmooth = true; wheel.isInertial = true;
    wheel.deltaX = -0.08f; wheel.deltaY = 0.06f;
    const int beforeInertia = panel.viewport.getViewPositionX();
    m.mouseWheelMove(event(m, rulerPoint, rulerPoint), wheel);
    expect(panel.viewport.getViewPositionX() > beforeInertia && std::abs(panel.zoomFactor - 4.0) < 0.00001,
           "Horizontal smooth/inertial diagonal events pan using the waveform's classification");
    panel.viewport.setViewPosition(juce::roundToInt(panel.timeToX(80)), 0);
    const auto mapPoint = timelinePoint(panel, 100.25);
    const auto visiblePoint = panel.viewport.getLocalPoint(&m, mapPoint);
    const auto roundTrip = m.getLocalPoint(&panel.viewport, visiblePoint);
    expect(std::abs(panel.xToTime(roundTrip.x) - 100.25) < 0.00005,
           "Viewport scrolling transforms physical pointer coordinates into the correct musical time");
    m.mouseDown(event(m, roundTrip, roundTrip));
    const auto editingView = panel.viewport.getViewPosition();
    const double editingZoom = panel.zoomFactor;
    m.mouseWheelMove(event(m, roundTrip, roundTrip), wheel);
    m.mouseMagnify(event(m, roundTrip, roundTrip), 1.5f);
    expect(panel.viewport.getViewPosition() == editingView && panel.zoomFactor == editingZoom
        && std::abs(insertionTime(panel) - 100.25) < 0.00005,
           "Navigation cannot move the insertion anchor while its pointer gesture is held");
    m.mouseUp(event(m, roundTrip, roundTrip));
    m.mouseMagnify(event(m, roundTrip, roundTrip), 0.25f);
    expect(std::abs(insertionTime(panel) - 100.25) < 0.00005
        && panel.addButton.isVisible() && juce::JSON::toString(toVar(panel.lyrics), true) == saved && changed == edits,
           "Zoom preserves the contextual + at its exact musical time without adding or editing marks");
    choose(panel, target);
    const auto id = panel.lyrics.marks.back().id;
    expect(std::abs(panel.lyrics.marks.back().seconds - 100.25) < 0.00005, "Creating a mark after zoom/pan uses the displayed time");
    const auto start = point(panel, id);
    auto end = start; end.x = panel.timeToX(101.375);
    m.mouseDown(event(m, start, start));
    const auto dragView = panel.viewport.getViewPosition();
    const double dragZoom = panel.zoomFactor;
    m.mouseWheelMove(event(m, start, start), wheel);
    m.mouseMagnify(event(m, start, start), 0.8f);
    expect(panel.viewport.getViewPosition() == dragView && panel.zoomFactor == dragZoom,
           "Navigation cannot alter the coordinate transform during a mark drag");
    m.mouseDrag(event(m, end, start, true)); m.mouseUp(event(m, end, start, true));
    expect(panel.lyrics.marks.back().id == id && panel.lyrics.marks.back().target.slideId == target.slideId
        && std::abs(panel.lyrics.marks.back().seconds - 101.375) < 0.00005,
           "Dragging at nonzero horizontal scroll preserves destination and exact new time");
    m.mouseMagnify(event(m, end, end), 0.0f);
    expect(panel.zoomFactor == dragZoom, "Zero magnification keeps zoom, matching the existing waveform");
    panel.setSize(1150, 180);
    panel.viewport.setViewPosition(juce::roundToInt(panel.timeToX(80)), 0);
    const int horizontalBeforeSelection = panel.viewport.getViewPositionX();
    panel.selectMark("mark-3");
    expect(panel.viewport.getViewPositionY() > 0 && panel.viewport.getViewPositionX() == horizontalBeforeSelection,
           "Revealing a selected preview vertically preserves horizontal navigation");
    panel.setSize(1150, 500);
    p = cursor();
    m.mouseMagnify(event(m, p, p), std::numeric_limits<float>::quiet_NaN());
    expect(panel.zoomFactor == dragZoom, "Invalid magnification cannot corrupt navigation");
    m.mouseMagnify(event(m, p, p), 100.0f);
    expect(panel.zoomFactor == 18.0, "Gesture zoom has the same 18x limit as the existing waveform");
    wheel = {}; wheel.deltaY = -1.0f;
    for (int i = 0; i < 10; ++i) { p = cursor(); m.mouseWheelMove(event(m, p, p), wheel); }
    panel.setSongLyrics(before);
    expect(panel.zoomFactor == 1.0 && panel.viewport.getViewPositionX() == 0,
           "Zooming out with the wheel restores the full song without a Fit control");
  }
  static void captureMarkMenu(LyricsPanel &panel, const juce::File &folder) {
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) {
      std::cerr << "CAPTURE UNAVAILABLE: native context menu requires a macOS display\n";
      return;
    }
    panel.showMapMenu(point(panel, "mark-2"), "mark-2");
    const auto image = panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f);
    if (auto stream = folder.getChildFile("lyrics-context-menu.png").createOutputStream())
      juce::PNGImageFormat().writeImageToStream(image, *stream);
    juce::PopupMenu::dismissAllActiveMenus();
  }
  static void captureZoom(LyricsPanel &panel, const juce::File &folder) {
    const auto pointer = timelinePoint(panel, 90.0);
    map(panel).mouseMagnify(event(map(panel), pointer, pointer), 3.0f);
    panel.viewport.setViewPosition(juce::roundToInt(panel.timeToX(78)), 0);
    const auto image = panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f);
    if (auto stream = folder.getChildFile("lyrics-zoom.png").createOutputStream())
      juce::PNGImageFormat().writeImageToStream(image, *stream);
    const juce::Point<float> centre{panel.viewport.getViewPositionX() + panel.viewport.getViewWidth() * 0.5f, 10.0f};
    map(panel).mouseMagnify(event(map(panel), centre, centre), -0.99f);
  }
  static Target chorusDestination(const LyricsPanel &panel) {
    std::vector<Target> targets;
    auto menu = panel.buildDestinationMenu(targets);
    juce::PopupMenu::MenuItemIterator groups(menu);
    while (groups.next()) {
      const auto &group = groups.getItem();
      if (group.text != "Coro" || group.subMenu == nullptr) continue;
      juce::PopupMenu::MenuItemIterator screens(*group.subMenu);
      while (screens.next()) {
        const auto &screen = screens.getItem();
        if (screen.itemID > 0 && screen.text.startsWith("Pantalla 2"))
          return targets[static_cast<size_t>(screen.itemID - 1)];
      }
    }
    throw std::runtime_error("The contextual + must expose Coro > Pantalla 2");
  }
  static void choose(LyricsPanel &panel, const Target &target) {
    panel.applyDestination(target, insertionTime(panel), {});
  }
  static void captureCascade(LyricsPanel &panel, const juce::File &folder) {
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) {
      std::cerr << "CAPTURE UNAVAILABLE: native destination menus require a macOS display\n";
      return;
    }
    panel.addButton.onClick();
    juce::Component *menu = nullptr;
    for (auto *child : panel.getChildren())
      if (child->getName() == "menu" && child->isVisible()) menu = child;
    expect(menu != nullptr, "The real contextual + opens the embedded native menu");
    auto save = [&](const char *name) {
      const auto snapshot = panel.createComponentSnapshot(panel.getLocalBounds(), true, 1.0f);
      if (auto stream = folder.getChildFile(name).createOutputStream())
        juce::PNGImageFormat().writeImageToStream(snapshot, *stream);
    };
    save("lyrics-group-menu.png");
    std::vector<Target> targets;
    auto items = panel.buildDestinationMenu(targets);
    juce::PopupMenu::MenuItemIterator iterator(items);
    int steps = 0;
    while (iterator.next()) { ++steps; if (iterator.getItem().text == "Coro") break; }
    for (int i = 0; i < steps; ++i) menu->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    menu->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    int menus = 0;
    for (auto *child : panel.getChildren())
      if (child->getName() == "menu" && child->isVisible()) ++menus;
    expect(menus == 2, "The group opens its real native slide submenu");
    save("lyrics-slide-submenu.png");
    juce::PopupMenu::dismissAllActiveMenus();
  }
};

struct FreeShowConnectionPanelTestAccess {
  static void run() {
    std::atomic<int> probes{0};
    FreeShowClient client([&](const ConnectionSettings &, const juce::var &request) {
      if (request.getProperty("action", {}).toString() == "get_shows") ++probes;
      return HttpResponse{200, juce::var(new juce::DynamicObject()), {}};
    });
    client.configure({"192.168.50.20", 5506, "example-session-token"});
    const auto before = client.getSettings(); const auto epoch = client.getStatus().connectionEpoch;
    const auto defaults = AppSettings::getFreeShowEndpoint();
    expect(defaults.host == "127.0.0.1" && defaults.port == 5506, "Missing local endpoint retains FreeShow defaults");
    FreeShowConnectionPanel panel; panel.setSize(760, 500); panel.setClient(&client);
    expect(client.getSettings().host == before.host && client.getStatus().connectionEpoch == epoch && probes == 0,
           "Opening connection settings reads the active endpoint without connecting or reconfiguring");
    expect(AppSettings::getFreeShowEndpoint().host == defaults.host, "Opening settings never overwrites saved values");
    panel.hostEditor.setText("192.168.50.30", false); panel.portEditor.setText("5510", false);
    expect(probes == 0 && client.getSettings().host == before.host, "Editing IP does not touch the active connection before Apply");
    panel.applyButton.onClick();
    for (int i = 0; i < 100 && probes == 0; ++i) juce::Thread::sleep(5);
    const auto saved = AppSettings::getFreeShowEndpoint();
    expect(saved.host == "192.168.50.30" && saved.port == 5510 && probes == 1,
           "Explicit Save and test persists this computer's endpoint and probes only the injected transport");
    const auto file = ApplicationDataPaths::applicationDataDirectory().getChildFile("MTControls/app_settings.json");
    expect(!file.loadFileAsString().contains("example-session-token")
        && !juce::JSON::parse(file)["freeShow"].getDynamicObject()->hasProperty("authToken"),
           "Session token never enters local endpoint preferences");
    const auto appliedEpoch = client.getStatus().connectionEpoch;
    panel.hostEditor.setText("example.com", false); panel.applyButton.onClick();
    expect(client.getStatus().connectionEpoch == appliedEpoch && client.getSettings().host == saved.host
        && AppSettings::getFreeShowEndpoint().host == saved.host && probes == 1,
           "Invalid public host leaves both the active client and saved endpoint unchanged");
    // Capture only a separate example panel, never the user's active settings.
    const auto dir = juce::SystemStats::getEnvironmentVariable("PW_LYRICS_EVIDENCE_DIR", {});
    if (dir.isNotEmpty()) {
      panel.setClient(&client);
      const juce::File folder(dir); folder.createDirectory();
      if (auto stream = folder.getChildFile("freeshow-connection-settings.png").createOutputStream())
        juce::PNGImageFormat().writeImageToStream(panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f), *stream);
    }
  }
};

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr) { std::cout << "SKIP: display required\n"; return 77; }
#endif
  juce::ScopedJuceInitialiser_GUI gui;
  try {
    applyTheme(ThemePreset::Spotify);
    expect(juce::Desktop::getInstance().getNumComponents() == 0, "Isolated UI validation starts without desktop windows");
    FreeShowConnectionPanelTestAccess::run();
    std::atomic<int> projectionCommands{0};
    std::atomic<int> catalogueQueries{0};
    FreeShowClient client([&](const ConnectionSettings &, const juce::var &request) {
      const auto action = request.getProperty("action", {}).toString();
      if (action == "get_shows") ++catalogueQueries;
      if (action == "index_select_slide" || action == "clear_slide") ++projectionCommands;
      return HttpResponse{200, juce::var(new juce::DynamicObject()), {}};
    });
    std::atomic<bool> connected{false};
    client.testConnection([&](bool ok, juce::String) { connected = ok; });
    for (int i = 0; i < 100 && !connected; ++i) juce::Thread::sleep(5);
    expect(connected, "The deterministic transport establishes a connected client");
    WaveformBar waveform;
    waveform.setBounds(0, 0, 1150, 280);
    waveform.setTransportProviders([] { return 48.0; }, [] { return 180.0; }, [] { return false; });
    waveform.setSongRhythm(96.0, "4/4");
    std::vector<float> bins(2048);
    for (size_t i = 0; i < bins.size(); ++i) bins[i] = static_cast<float>(0.15 + 0.7 * std::abs(std::sin(i * 0.045)));
    waveform.setWaveformBins(std::move(bins));
    WaveformEditingTestAccess::checkNavigation(waveform);
    LyricsPanel panel;
    panel.setBounds(0, 0, 1150, 500);
    double transport = 48.0;
    panel.setTransportProviders([&] { return transport; }, [] { return false; });
    panel.setDuration(180.0);
    int rulerCalls = 0;
    panel.setTimelineMarksProvider([&](juce::Range<double> range, float width) {
      ++rulerCalls; return waveform.getPracticeTimelineMarks(range, width);
    });
    SongArrangementSnapshot sections;
    for (int i = 0; i < 6; ++i) {
      SongArrangementPartSnapshot p; p.id = "section-" + juce::String(i);
      p.name = juce::StringArray{"Intro", "Verso", "Coro", "Verso", "Coro", "Final"}[i];
      p.startSec = i * 30.0; p.endSec = (i+1) * 30.0; p.color = "ff53ae8b";
      sections.parts.push_back(p);
    }
    panel.setArrangement(sections);
    waveform.setArrangement(sections, false);
    SongLyrics song; song.showId = "own-test-show"; song.showName = "Forever Rain";
    song.layoutId = "main"; song.layoutName = "Principal";
    const double times[] = {0, 27, 57, 60, 87, 117, 120, 150, 172};
    const juce::StringArray slideTexts{
      juce::String::fromUTF8("Comienza nuestro canto\nCon gratitud y esperanza"),
      juce::String::fromUTF8("Al despertar buscamos tu voz\nEn cada paso seguimos tu luz"),
      juce::String::fromUTF8("Cantamos juntos tu amor\nNuestra esperanza vive en ti"),
      juce::String::fromUTF8("Tu paz nos invita a seguir\nTu gracia nos sostiene aquí"),
      juce::String::fromUTF8("En el camino crece la fe\nCada mañana volvemos a creer"),
      juce::String::fromUTF8("Cantamos juntos tu amor\nNuestra esperanza vive en ti"),
      juce::String::fromUTF8("Tu paz nos invita a seguir\nTu gracia nos sostiene aquí"),
      juce::String::fromUTF8("Levantamos hoy nuestras manos\nUn nuevo canto nace aquí"),
      juce::String{}
    };
    for (int i = 0; i < 9; ++i) {
      Mark mark; mark.id = "mark-" + juce::String(i); mark.seconds = times[i];
      mark.target.showId = song.showId; mark.target.layoutId = song.layoutId;
      const bool chorus = i == 2 || i == 3 || i == 5 || i == 6;
      mark.target.groupId = chorus ? "coro" : i == 7 ? "coro2-literal" : "group-" + juce::String(i);
      mark.target.slideId = "screen-" + juce::String(i == 3 || i == 6 ? 2 : 1);
      mark.target.occurrence = i == 5 || i == 6 ? 2 : 1;
      mark.target.layoutSignature = "own-layout-signature";
      mark.target.title = chorus ? "Coro" : i == 7 ? "Coro2" : i == 0 ? "Intro" : "Verso";
      mark.target.text = slideTexts[i];
      song.marks.push_back(mark);
    }
    song.marks.back().target.hide = true;
    panel.setSongLyrics(song);
    LyricsPanelTestAccess::checkShowNames(panel);
    panel.setClient(&client);
    ShowCatalog catalogue; catalogue.id = song.showId; catalogue.name = song.showName;
    catalogue.activeLayoutId = song.layoutId;
    Layout layout; layout.id = song.layoutId; layout.name = song.layoutName; layout.signature = "own-layout-signature";
    for (const auto &mark : song.marks) if (!mark.target.hide) {
      SlideDestination d; d.target = mark.target; d.screen = mark.target.slideId.endsWith("2") ? 2 : 1;
      d.absoluteIndex = static_cast<int>(layout.slides.size()) + 1;
      d.displayName = mark.target.title + juce::String::fromUTF8(" · aparición ") + juce::String(mark.target.occurrence)
          + juce::String::fromUTF8(" · pantalla ") + juce::String(d.screen);
      layout.slides.push_back(d);
    }
    catalogue.layouts.push_back(layout);
    for (const auto &slide : layout.slides) {
      auto group = std::find_if(catalogue.groups.begin(), catalogue.groups.end(),
          [&](const SlideGroup &g) { return g.id == slide.target.groupId; });
      if (group == catalogue.groups.end()) {
        catalogue.groups.push_back({slide.target.groupId, slide.target.title, {}});
        group = std::prev(catalogue.groups.end());
      }
      if (std::any_of(group->slides.begin(), group->slides.end(),
          [&](const SlideDestination &d) { return d.target.slideId == slide.target.slideId; })) continue;
      auto destination = slide;
      destination.target.useActiveLayout = true;
      destination.target.layoutId.clear(); destination.target.layoutSignature.clear();
      destination.target.occurrence = 1;
      group->slides.push_back(destination);
    }
    LyricsPanelTestAccess::catalog(panel, catalogue);
    catalogue.layouts[0].slides[2].target.text = "Texto actualizado desde FreeShow";
    LyricsPanelTestAccess::catalog(panel, catalogue);
    expect(LyricsPanelTestAccess::text(panel, "mark-2") == "Texto actualizado desde FreeShow",
           "The exact current API text replaces the saved offline preview after external edits");
    auto otherBinding = song; otherBinding.layoutId = "alternative";
    panel.setSongLyrics(otherBinding);
    expect(LyricsPanelTestAccess::review(panel, "mark-2").isNotEmpty(),
           "Changing linked arrangement requires reviewing the original destinations");
    panel.setSongLyrics(song);
    LyricsPanelTestAccess::catalog(panel, catalogue);
    LyricsPanelTestAccess::select(panel, "mark-2");
    LyricsPanelTestAccess::catalogueFailure(panel, "El arreglo externo no se pudo verificar");
    LyricsPanelTestAccess::checkMarkMenu(panel, "mark-2", false);
    expect(LyricsPanelTestAccess::review(panel, "mark-2").isNotEmpty()
           && !LyricsPanelTestAccess::testEnabled(panel),
           "A failed catalogue refresh marks the retained target for review and disables Test");
    LyricsPanelTestAccess::catalogueFailure(panel, {});
    LyricsPanelTestAccess::select(panel, {});
    int changed = 0;
    panel.onChanged = [&](const SongLyrics &) { ++changed; };
    LyricsPanelTestAccess::checkPreviews(panel);
    auto &map = LyricsPanelTestAccess::map(panel);
    auto start = LyricsPanelTestAccess::point(panel, "mark-2");
    map.mouseDown(event(map, start, start));
    expect(changed == 0, "Selecting never mutates or projects a mark");
    expect(panel.getSongLyrics().marks[2].seconds == 57.0, "Selecting keeps time");
    start = LyricsPanelTestAccess::point(panel, "mark-2");
    map.mouseDown(event(map, start, start));
    const auto destination = start.translated(63.0f, 0.0f);
    map.mouseDrag(event(map, destination, start, true));
    map.mouseUp(event(map, destination, start, true));
    expect(changed == 1, "A drag publishes exactly one edit");
    const auto &moved = panel.getSongLyrics().marks[2];
    expect(moved.seconds > 57.0, "Dragging moves the musical time");
    expect(moved.target.showId == song.marks[2].target.showId
        && moved.target.slideId == song.marks[2].target.slideId
        && moved.target.occurrence == song.marks[2].target.occurrence,
        "Dragging preserves the exact target identity and occurrence");
    expect(LyricsPanelTestAccess::sameSections(panel, sections), "Dragging preserves all musical sections");
    expect(transport == 48.0, "Dragging and selecting never seek transport");
    expect(projectionCommands == 0, "Selecting and dragging a connected mark never project it");
    const auto retained = panel.getSongLyrics().marks;
    auto anchor = LyricsPanelTestAccess::timelinePoint(panel, 91.25);
    map.mouseDown(event(map, anchor, anchor));
    auto anchorEnd = LyricsPanelTestAccess::timelinePoint(panel, 96.5, 36.0f);
    map.mouseDrag(event(map, anchorEnd, anchor, true));
    map.mouseUp(event(map, anchorEnd, anchor, true));
    expect(std::abs(LyricsPanelTestAccess::insertionTime(panel) - 96.5) < 0.001,
           "Dragging on the ruler/map chooses the exact insertion time");
    expect(changed == 1 && panel.getSongLyrics().marks.size() == retained.size(),
           "Placing the pointer does not create or persist a mark");
    const auto chosenTarget = LyricsPanelTestAccess::chorusDestination(panel);
    LyricsPanelTestAccess::choose(panel, chosenTarget);
    auto second = LyricsPanelTestAccess::timelinePoint(panel, 140.0);
    map.mouseDown(event(map, second, second)); map.mouseUp(event(map, second, second));
    LyricsPanelTestAccess::choose(panel, chosenTarget);
    const auto &newMarks = panel.getSongLyrics().marks;
    expect(newMarks.size() == retained.size() + 2 && changed == 3,
           "Choosing a slide creates exactly one independent mark each time");
    expect(newMarks[retained.size()].id != newMarks.back().id
        && std::abs(newMarks[retained.size()].seconds - 96.5) < 0.001
        && std::abs(newMarks.back().seconds - 140.0) < 0.001
        && newMarks[retained.size()].target.slideId == newMarks.back().target.slideId,
           "The same destination may be repeated at different times with independent IDs");
    for (size_t i = 0; i < retained.size(); ++i)
      expect(newMarks[i].id == retained[i].id && newMarks[i].seconds == retained[i].seconds
          && newMarks[i].target.layoutId == retained[i].target.layoutId
          && newMarks[i].target.slideId == retained[i].target.slideId,
             "Creating lyrics preserves every existing mark and its saved binding");
    expect(transport == 48.0 && projectionCommands == 0
        && LyricsPanelTestAccess::sameSections(panel, sections),
           "Creating lyrics never seeks, projects, or edits musical sections");
    LyricsPanelTestAccess::select(panel, "mark-8");
    LyricsPanelTestAccess::explicitTest(panel);
    for (int i = 0; i < 100 && projectionCommands == 0; ++i) juce::Thread::sleep(5);
    expect(projectionCommands == 1, "The explicit Test action sends the selected hide target once");
    const int queriesBeforeReconnect = catalogueQueries.load();
    client.configure(client.getSettings()); client.testConnection();
    for (int i = 0; i < 100 && client.getStatus().state != ConnectionStatus::State::connected; ++i) juce::Thread::sleep(5);
    LyricsPanelTestAccess::refresh(panel);
    for (int i = 0; i < 100 && catalogueQueries < queriesBeforeReconnect + 2; ++i) juce::Thread::sleep(5);
    expect(catalogueQueries >= queriesBeforeReconnect + 2,
           "A connection applied from Settings refreshes the show catalogue without sending a cue");
    LyricsPanelTestAccess::catalog(panel, catalogue);
    LyricsPanelTestAccess::checkMarkMenu(panel, "mark-8", true);
    LyricsPanelTestAccess::checkGestureParity(panel, waveform, changed);
    LyricsPanelTestAccess::checkZoom(panel, chosenTarget, changed);
    expect(transport == 48.0 && projectionCommands == 1 && LyricsPanelTestAccess::sameSections(panel, sections),
           "Zoom, pan, creation and drag preserve transport, projection and section durations");
    LyricsPanelTestAccess::select(panel, "mark-2");
    LyricsPanelTestAccess::refresh(panel);
    LyricsPanelTestAccess::checkPreviews(panel);
    panel.setSize(650, 430); LyricsPanelTestAccess::checkPreviews(panel); LyricsPanelTestAccess::checkHeader(panel);
    panel.setSize(1450, 600); LyricsPanelTestAccess::checkPreviews(panel); LyricsPanelTestAccess::checkHeader(panel);
    panel.setSize(1150, 500);
    LyricsPanelTestAccess::checkHeader(panel);
    const auto dir = juce::SystemStats::getEnvironmentVariable("PW_LYRICS_EVIDENCE_DIR", {});
    if (dir.isNotEmpty()) {
      // A concise demonstration fixture excludes temporary regression edits.
      // Repeated chorus targets remain valid at different musical times.
      auto demo = song;
      demo.marks.clear();
      const int sourceMarks[] = {0, 1, 2, 4, 5, 7, 8};
      const double demoTimes[] = {0, 30, 60, 90, 120, 150, 172};
      for (int i = 0; i < 7; ++i) {
        auto mark = song.marks[static_cast<size_t>(sourceMarks[i])];
        mark.seconds = demoTimes[i];
        if (i == 3) mark.target.title = "Puente";
        if (i == 5) {
          mark.target.title = "Final";
          mark.target.text = juce::String::fromUTF8("Terminamos dando gracias\nLlevamos tu paz al caminar");
        }
        demo.marks.push_back(std::move(mark));
      }
      auto demoCatalogue = catalogue;
      demoCatalogue.layouts[0].slides = layout.slides;
      for (auto &slide : demoCatalogue.layouts[0].slides)
        for (const auto &mark : demo.marks)
          if (slide.target.groupId == mark.target.groupId
              && slide.target.slideId == mark.target.slideId) {
            slide.target.title = mark.target.title;
            slide.target.text = mark.target.text;
          }
      for (auto &group : demoCatalogue.groups)
        for (auto &slide : group.slides)
          for (const auto &mark : demo.marks)
            if (slide.target.groupId == mark.target.groupId
                && slide.target.slideId == mark.target.slideId) {
              group.name = mark.target.title;
              slide.target.title = mark.target.title;
              slide.target.text = mark.target.text;
            }
      auto demoSections = sections;
      demoSections.parts[3].name = "Puente";
      panel.setSongLyrics(demo);
      panel.setArrangement(demoSections);
      LyricsPanelTestAccess::catalog(panel, demoCatalogue);
      LyricsPanelTestAccess::select(panel, "mark-2");
      const juce::File folder(dir); folder.createDirectory();
      auto snapshot = panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f);
      if (auto stream = folder.getChildFile("lyrics-native-panel.png").createOutputStream())
        juce::PNGImageFormat().writeImageToStream(snapshot, *stream);
      const auto pointer = LyricsPanelTestAccess::timelinePoint(panel, 110.0);
      map.mouseDown(event(map, pointer, pointer)); map.mouseUp(event(map, pointer, pointer));
      snapshot = panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f);
      if (auto stream = folder.getChildFile("lyrics-insertion-anchor.png").createOutputStream())
        juce::PNGImageFormat().writeImageToStream(snapshot, *stream);
      LyricsPanelTestAccess::captureCascade(panel, folder);
      LyricsPanelTestAccess::captureMarkMenu(panel, folder);
      LyricsPanelTestAccess::captureZoom(panel, folder);
      if (auto stream = folder.getChildFile("waveform-shared-ruler.png").createOutputStream())
        juce::PNGImageFormat().writeImageToStream(waveform.createComponentSnapshot(waveform.getLocalBounds(), true, 2.0f), *stream);
    }
    panel.createComponentSnapshot(panel.getLocalBounds());
    expect(rulerCalls > 0, "Lyrics renders the shared musical ruler using the waveform's current tempo source");
    expect(juce::Desktop::getInstance().getNumComponents() == 0, "Validation and captures never create a desktop window or activate an app");
    std::cout << "PASS: native lyrics preview ownership, collision lanes at 3 widths, selection safety, drag-only time edit, section and transport regression\n";
  } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
  return 0;
}
#else
int main() { return 77; }
#endif
