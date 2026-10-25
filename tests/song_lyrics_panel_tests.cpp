#include "ui/panels/SongLyricsPanel.h"
#include "data/Localization.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void expect(bool ok, const char *message) {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}
void drain() { juce::MessageManager::getInstance()->runDispatchLoopUntil(40); }
void capture(juce::Component &panel, const juce::String &name) {
  const auto path = juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_LYRICS_SNAPSHOTS", {});
  if (path.isEmpty()) return;
  const juce::File directory(path);
  expect(directory.createDirectory().wasOk(), "create isolated capture directory");
  auto stream = directory.getChildFile(name + ".png").createOutputStream();
  expect(stream != nullptr && juce::PNGImageFormat().writeImageToStream(
      panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f), *stream), "capture real lyric components");
}
}
struct SongLyricsPanelTestAccess {
  static void testDraftEditing() {
    auto source = lyricsFromPlainText(juce::String::fromUTF8(u8"Camino de prueba\nUna segunda línea original"));
    const auto original = juce::JSON::toString(songLyricsToVar(source));
    int applies = 0, closes = 0;
    std::optional<SongLyricsDocument> result;
    SongLyricsPanel panel(juce::String::fromUTF8(u8"Canción de ejemplo"), source, false,
        [&](auto doc) { ++applies; result = std::move(doc); }, [&] { ++closes; });
    panel.setSize(1100, 740); panel.setVisible(true);
    panel.bodyEditor.setText(juce::String::fromUTF8(u8"Un nuevo camino de prueba\nUna segunda línea original"), true); drain();
    expect(lyricsBlockText(panel.getDocument().blocks.front()).startsWith("Un nuevo"), "native editable text updates the private draft");
    expect(juce::JSON::toString(songLyricsToVar(source)) == original && applies == 0, "editing never mutates source or persists before Done");
    panel.selectPosition(0, 0, 9, {});
    panel.chordEditor.setText("Am7", true); drain();
    panel.chordApplyButton.onClick();
    expect(panel.selectedChord() != nullptr && panel.selectedChord()->position == 9 && panel.selectedChord()->symbol == "Am7", "a chord anchors at the selected word position");
    const auto chordId = panel.selectedChord()->id;
    panel.chordRightButton.onClick();
    expect(panel.selectedChord()->position == 10 && panel.selectedChord()->id == chordId, "position controls move the existing chord without duplicating it");
    panel.chordEditor.setText("G/B", true); drain(); panel.chordApplyButton.onClick();
    expect(panel.selectedChord()->symbol == "G/B" && panel.getDocument().blocks.front().lines.front().chords.size() == 1, "editing a selected symbol replaces its chord");
    panel.bodyEditor.setText(juce::String::fromUTF8(u8"Más: Un nuevo camino de prueba\nUna segunda línea original"), true); drain();
    const auto &reanchored = panel.getDocument().blocks.front().lines.front().chords.front();
    if (reanchored.id != chordId || reanchored.position != 15)
      std::cerr << "Reanchor: expected position=15 actual=" << reanchored.position
                << " expectedId=" << chordId << " actualId=" << reanchored.id
                << " text=" << panel.getDocument().blocks.front().lines.front().text << '\n';
    expect(reanchored.id == chordId && reanchored.position == 15, "text insertion keeps the same chord with its original lyric position");
    panel.selectPosition(0, 0, reanchored.position, chordId); panel.chordDeleteButton.onClick();
    expect(panel.getDocument().blocks.front().lines.front().chords.empty(), "selected chord can be deleted explicitly");
    panel.addBlockButton.onClick(); panel.blockName.setText("Coro", true);
    panel.bodyEditor.setText("Mi coro de ejemplo", true); drain();
    expect(panel.getDocument().blocks.size() == 2 && panel.getDocument().blocks.back().label == "Coro", "a second editable block has its own label and text");
    panel.doneButton.onClick();
    expect(applies == 1 && closes == 0 && hasSongLyrics(result) && result->blocks.size() == 2, "Done applies one validated structured document");
    panel.doneButton.onClick(); expect(applies == 1, "duplicate Done does not apply twice");
    SongLyricsPanel cancelled("Example", source, false, [&](auto) { ++applies; }, [&] { ++closes; });
    cancelled.bodyEditor.setText("Unsaved replacement", true); drain(); cancelled.closeButton.onClick();
    expect(applies == 1 && closes == 1 && juce::JSON::toString(songLyricsToVar(source)) == original, "Cancel discards the private draft");
    SongLyricsPanel empty("Example", source, false, [&](auto doc) { result = std::move(doc); }, [] {});
    empty.bodyEditor.clear(); drain(); empty.doneButton.onClick();
    expect(!result.has_value(), "clearing all lyrics removes the optional document");
    SongLyricsPanel previewImmediately("Example", source, false, [](auto) {}, [] {});
    previewImmediately.setSize(390, 844);
    previewImmediately.bodyEditor.setText("Pasted immediately before preview", true);
    previewImmediately.previewButton.onClick();
    expect(lyricsBlockText(previewImmediately.getDocument().blocks.front()) == "Pasted immediately before preview",
           "entering chord preview flushes the latest paste without waiting for text notifications");
    std::optional<SongLyricsDocument> immediateResult;
    SongLyricsPanel immediate("Example", source, false, [&](auto doc) { immediateResult = std::move(doc); }, [] {});
    immediate.bodyEditor.setText("Pasted immediately before Done", true);
    immediate.doneButton.onClick();
    expect(immediateResult && lyricsBlockText(immediateResult->blocks.front()) == "Pasted immediately before Done",
           "Done flushes native text synchronously before its queued change notification");
    SongLyricsPanel switched("Example", source, false, [](auto) {}, [] {});
    switched.bodyEditor.setText("Pending text belongs to the first block", true);
    switched.addBlockButton.onClick();
    expect(lyricsBlockText(switched.getDocument().blocks.front()) == "Pending text belongs to the first block" &&
               lyricsBlockText(switched.getDocument().blocks.back()).isEmpty(),
           "adding a block flushes pending text into its original block");
    switched.bodyEditor.setText("Second block before switching", true);
    switched.selectBlock(0);
    drain();
    expect(lyricsBlockText(switched.getDocument().blocks.back()) == "Second block before switching" &&
               lyricsBlockText(switched.getDocument().blocks.front()) == "Pending text belongs to the first block",
           "block switching preserves pending edits and stale notifications cannot overwrite another block");
    int rejectedApply = 0;
    SongLyricsPanel invalid("Example", source, false, [&](auto) { ++rejectedApply; }, [] {});
    invalid.setSize(390, 700);
    invalid.draft.blocks.front().lines.front().chords.push_back({juce::Uuid().toString(), -1, "C"});
    invalid.doneButton.onClick();
    expect(rejectedApply == 0 && !invalid.finished && invalid.hint.isVisible() && invalid.hint.getText().isNotEmpty(),
           "an invalid document stays editable with an inline error and never applies");
  }
  static void testPreviewPointerGestures() {
    const juce::String words("Camino abierto");
    SongLyricsPanel panel("Pointer fixture", lyricsFromPlainText(words), false, [](auto) {}, [] {});
    panel.setVisible(true); panel.setSize(390, 700);
    panel.previewButton.onClick();
    auto *canvas = panel.previewViewport.getViewedComponent();
    expect(canvas != nullptr && canvas->isVisible(), "actual chord canvas is available through the viewport");
    const auto font = makeAppFont(18.0f, AppFontWeight::Regular);
    const auto characters = words.toUTF32();
    const auto characterPoint = [&](int index) {
      float x = 14.0f;
      for (int i = 0; i < index; ++i)
        x += juce::GlyphArrangement::getStringWidth(font, juce::String::charToString(characters[i]));
      const auto width = juce::GlyphArrangement::getStringWidth(font, juce::String::charToString(characters[index]));
      return juce::Point<float>{x + width * 0.5f, 89.0f};
    };
    const auto pointer = [&](juce::Point<float> position, juce::Point<float> down, bool dragged) {
      const auto now = juce::Time::getCurrentTime();
      return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
          juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f,
          0.0f, 0.0f, 0.0f, 0.0f, canvas, canvas, now, down, now, 1, dragged);
    };
    // Hit the rendered first character of 'abierto', rather than selecting an
    // anchor directly. These events execute the production hit-testing code.
    const auto wordPoint = characterPoint(7);
    canvas->mouseDown(pointer(wordPoint, wordPoint, false));
    canvas->mouseUp(pointer(wordPoint, wordPoint, false));
    expect(panel.selectedLine == 0 && panel.selectedPosition == 7 && panel.selectedChordId.isEmpty(),
           "clicking a rendered word selects its actual character anchor");
    panel.chordEditor.setText("Am7", true);
    panel.chordApplyButton.onClick();
    const auto &added = panel.getDocument().blocks.front().lines.front().chords;
    expect(added.size() == 1 && added.front().position == 7 && added.front().symbol == "Am7",
           "the toolbar creates a chord at the word selected by a real pointer event");
    const auto identity = added.front().id;
    const auto chipPoint = juce::Point<float>{wordPoint.x + 8.0f, 61.0f};
    canvas->mouseDown(pointer(chipPoint, chipPoint, false));
    expect(panel.selectedChordId == identity && canvas->getViewportIgnoreDragFlag(),
           "pressing the actual chord chip selects it and reserves drag handling from viewport scrolling");
    const auto originalScroll = panel.previewViewport.getViewPosition();
    const auto destination = characterPoint(11);
    canvas->mouseDrag(pointer(destination, chipPoint, true));
    const auto &moved = panel.getDocument().blocks.front().lines.front().chords;
    expect(moved.size() == 1 && moved.front().id == identity && moved.front().position == 11,
           "dragging a rendered chord moves its existing identity to the nearest rendered character");
    canvas->mouseUp(pointer(destination, chipPoint, true));
    expect(!canvas->getViewportIgnoreDragFlag() && panel.previewViewport.getViewPosition() == originalScroll,
           "releasing a chord restores normal scrolling without scrolling the lyric canvas during its drag");
  }

  static void testLayoutsAndCaptures() {
    auto doc = lyricsFromPlainText(juce::String::fromUTF8(u8"Una frase de ejemplo que continúa y se adapta al ancho de la pantalla.\nOtro renglón para practicar."));
    doc.blocks.front().label = "Verso";
    for (const auto &symbol : {juce::String("C"), juce::String("Am7"), juce::String("G/B")}) {
      SongLyricsChord chord; chord.id = juce::Uuid().toString(); chord.position = symbol == "C" ? 0 : 12; chord.symbol = symbol;
      doc.blocks.front().lines.front().chords.push_back(chord);
    }
    auto chorus = lyricsFromPlainText(juce::String::fromUTF8(u8"Este coro es sólo una demostración."));
    chorus.blocks.front().label = "Coro"; doc.blocks.push_back(chorus.blocks.front());
    for (const auto size : {juce::Point<int>{390, 844}, {390, 360}, {320, 250}, {390, 280}, {820, 1000}, {1200, 760}}) {
      for (const bool readOnly : {false, true}) {
        SongLyricsPanel panel(juce::String::fromUTF8(u8"Canción de ejemplo"), doc, readOnly, [](auto) {}, [] {});
        panel.setVisible(true); panel.setSize(size.x, size.y);
        for (const bool preview : {false, true}) {
          panel.previewMode = preview; panel.resized();
          expect(panel.getLocalBounds().contains(panel.closeButton.getBounds()), "close stays reachable at every supported size");
          if (!readOnly) {
            expect(panel.getLocalBounds().contains(panel.doneButton.getBounds()) && panel.doneButton.getHeight() >= 44, "Done stays reachable above short content");
            expect(panel.getLocalBounds().contains(panel.bodyEditor.getBounds()), "native text editor remains within the surface");
          }
          for (auto *child : panel.getChildren()) {
            if (!child->isVisible()) continue;
            expect(!child->getBounds().isEmpty() && panel.getLocalBounds().contains(child->getBounds()), "visible lyric controls fit short, phone, tablet and desktop surfaces");
          }
          if (panel.bodyEditor.isVisible()) expect(panel.bodyEditor.getHeight() >= 70, "short keyboard layout retains a usable native text editing area");
          if (preview || readOnly) {
            expect(panel.previewViewport.getHeight() >= 70, "lyric reading retains a scrollable area");
            const auto name = juce::String(readOnly ? "read" : "edit") + "-" + juce::String(size.x) + "x" + juce::String(size.y);
            capture(panel, name);
          }
        }
        if (readOnly) expect(!panel.bodyEditor.isVisible() && !panel.doneButton.isVisible() && !panel.chordApplyButton.isVisible(), "reader exposes no editing actions");
      }
    }
  }
};
int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr) return 77;
#endif
  if (juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_CREATION_TEST_ROOT", {}).isEmpty()) return 77;
  try {
    juce::ScopedJuceInitialiser_GUI gui; applyTheme(ThemePreset::Spotify);
    SongLyricsPanelTestAccess::testDraftEditing();
    SongLyricsPanelTestAccess::testPreviewPointerGestures();
    SongLyricsPanelTestAccess::testLayoutsAndCaptures();
    std::cout << "Song lyrics panel: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
