#include "ui/panels/LyricsPanel.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#if !JUCE_IOS && !JUCE_ANDROID
using namespace playworship::lyrics;
namespace {
void expect(bool passed, const char *message) { if (!passed) throw std::runtime_error(message); }
juce::MouseEvent event(juce::Component &component, juce::Point<float> point,
                       juce::Point<float> start, bool dragged = false) {
  const juce::Time now(1000000);
  return {juce::Desktop::getInstance().getMainMouseSource(), point,
          juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
          &component, &component, now, start, now, 1, dragged};
}
}
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
  static void picker(LyricsPanel &panel, const juce::String &id) { panel.showDestinationPicker(0.0, id); }
  static void explicitTest(LyricsPanel &panel) { panel.testSelected(); }
  static void refresh(LyricsPanel &panel) { panel.timerCallback(); }
  static juce::String text(const LyricsPanel &panel, const juce::String &id) { return panel.previewText(*panel.findMark(id)); }
  static juce::String review(const LyricsPanel &panel, const juce::String &id) { return panel.reviewReason(*panel.findMark(id)); }
  static void catalogueFailure(LyricsPanel &panel, juce::String message) { panel.catalogueError = std::move(message); panel.updateDetail(); }
  static bool testEnabled(const LyricsPanel &panel) { return panel.testButton.isEnabled(); }
};

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr) { std::cout << "SKIP: display required\n"; return 77; }
#endif
  juce::ScopedJuceInitialiser_GUI gui;
  try {
    applyTheme(ThemePreset::Spotify);
    std::atomic<int> projectionCommands{0};
    FreeShowClient client([&](const ConnectionSettings &, const juce::var &request) {
      const auto action = request.getProperty("action", {}).toString();
      if (action == "index_select_slide" || action == "clear_slide") ++projectionCommands;
      return HttpResponse{200, juce::var(new juce::DynamicObject()), {}};
    });
    std::atomic<bool> connected{false};
    client.testConnection([&](bool ok, juce::String) { connected = ok; });
    for (int i = 0; i < 100 && !connected; ++i) juce::Thread::sleep(5);
    expect(connected, "The deterministic transport establishes a connected client");
    LyricsPanel panel;
    panel.setBounds(0, 0, 1150, 500);
    double transport = 48.0;
    panel.setTransportProviders([&] { return transport; }, [] { return false; });
    panel.setDuration(180.0);
    SongArrangementSnapshot sections;
    for (int i = 0; i < 6; ++i) {
      SongArrangementPartSnapshot p; p.id = "section-" + juce::String(i);
      p.name = juce::StringArray{"Intro", "Verso", "Coro", "Verso", "Coro", "Final"}[i];
      p.startSec = i * 30.0; p.endSec = (i+1) * 30.0; p.color = "ff53ae8b";
      sections.parts.push_back(p);
    }
    panel.setArrangement(sections);
    SongLyrics song; song.showId = "own-test-show"; song.showName = "Canto de prueba propio";
    song.layoutId = "main"; song.layoutName = "Principal";
    const double times[] = {0, 27, 57, 60, 87, 117, 120, 150, 172};
    for (int i = 0; i < 9; ++i) {
      Mark mark; mark.id = "mark-" + juce::String(i); mark.seconds = times[i];
      mark.target.showId = song.showId; mark.target.layoutId = song.layoutId;
      const bool chorus = i == 2 || i == 3 || i == 5 || i == 6;
      mark.target.groupId = chorus ? "coro" : i == 7 ? "coro2-literal" : "group-" + juce::String(i);
      mark.target.slideId = "screen-" + juce::String(i == 3 || i == 6 ? 2 : 1);
      mark.target.occurrence = i == 5 || i == 6 ? 2 : 1;
      mark.target.layoutSignature = "own-layout-signature";
      mark.target.title = chorus ? "Coro" : i == 7 ? "Coro2" : i == 0 ? "Intro" : "Verso";
      mark.target.text = juce::String::fromUTF8("Tu luz nos guía al caminar\nTu paz nos invita a cantar");
      song.marks.push_back(mark);
    }
    song.marks.back().target.hide = true;
    panel.setSongLyrics(song);
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
    LyricsPanelTestAccess::select(panel, "mark-8");
    LyricsPanelTestAccess::explicitTest(panel);
    for (int i = 0; i < 100 && projectionCommands == 0; ++i) juce::Thread::sleep(5);
    expect(projectionCommands == 1, "The explicit Test action sends the selected hide target once");
    LyricsPanelTestAccess::select(panel, "mark-2");
    LyricsPanelTestAccess::refresh(panel);
    LyricsPanelTestAccess::checkPreviews(panel);
    panel.setSize(650, 430); LyricsPanelTestAccess::checkPreviews(panel);
    panel.setSize(1450, 600); LyricsPanelTestAccess::checkPreviews(panel);
    panel.setSize(1150, 500);
    const auto dir = juce::SystemStats::getEnvironmentVariable("PW_LYRICS_EVIDENCE_DIR", {});
    if (dir.isNotEmpty()) {
      const juce::File folder(dir); folder.createDirectory();
      auto snapshot = panel.createComponentSnapshot(panel.getLocalBounds(), true, 1.0f);
      if (auto stream = folder.getChildFile("lyrics-native-panel.png").createOutputStream())
        juce::PNGImageFormat().writeImageToStream(snapshot, *stream);
    }
    std::cout << "PASS: native lyrics preview ownership, collision lanes at 3 widths, selection safety, drag-only time edit, section and transport regression\n";
  } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
  return 0;
}
#else
int main() { return 77; }
#endif
