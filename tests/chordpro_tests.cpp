#include "data/ChordProCodec.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void expect(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
void roundtrip(const juce::String &source) {
  const auto imported = ChordProCodec::importText(source);
  expect(static_cast<bool>(imported), "fixture imports");
  juce::String error;
  const auto output = ChordProCodec::exportText(*imported.document, imported.metadata, &error);
  expect(output.isNotEmpty() && error.isEmpty(), "fixture exports");
  const auto reopened = ChordProCodec::importText(output);
  expect(static_cast<bool>(reopened), "exported file reopens");
  expect(reopened.document->text == imported.document->text &&
             reopened.document->chords.size() == imported.document->chords.size() &&
             reopened.document->sections.size() == imported.document->sections.size() &&
             reopened.document->instrumentalBlocks.size() == imported.document->instrumentalBlocks.size(),
         "roundtrip preserves text, chords, sections and instrumental blocks");
  for (size_t i = 0; i < imported.document->chords.size(); ++i)
    expect(reopened.document->chords[i].offset == imported.document->chords[i].offset &&
               reopened.document->chords[i].symbol == imported.document->chords[i].symbol,
           "roundtrip preserves exact chord anchor and symbol");
  for (size_t i = 0; i < imported.document->sections.size(); ++i)
    expect(reopened.document->sections[i].kind == imported.document->sections[i].kind &&
               reopened.document->sections[i].start == imported.document->sections[i].start &&
               reopened.document->sections[i].length == imported.document->sections[i].length,
           "roundtrip preserves lyric section types and exact global text ranges");
  for (size_t i = 0; i < imported.document->instrumentalBlocks.size(); ++i) {
    const auto &before = imported.document->instrumentalBlocks[i];
    const auto &after = reopened.document->instrumentalBlocks[i];
    expect(before.kind == after.kind && before.label == after.label && before.anchorUtf16 == after.anchorUtf16 &&
               before.order == after.order && before.rows.size() == after.rows.size(),
           "roundtrip preserves block kind, label, anchor and order");
    for (size_t rowIndex = 0; rowIndex < before.rows.size(); ++rowIndex) {
      const auto &beforeRow = before.rows[rowIndex];
      const auto &afterRow = after.rows[rowIndex];
      expect(beforeRow.cells.size() == afterRow.cells.size(), "roundtrip preserves every grid cell");
      const auto beforeBars = beforeRow.barLengths.empty() ? std::vector<int>{static_cast<int>(beforeRow.cells.size())} : beforeRow.barLengths;
      const auto afterBars = afterRow.barLengths.empty() ? std::vector<int>{static_cast<int>(afterRow.cells.size())} : afterRow.barLengths;
      expect(beforeBars == afterBars, "roundtrip preserves plain bar partitions");
      for (size_t cellIndex = 0; cellIndex < beforeRow.cells.size(); ++cellIndex)
        expect(beforeRow.cells[cellIndex].symbol == afterRow.cells[cellIndex].symbol,
               "roundtrip preserves chord symbols and explicit empty cells");
    }
  }
}
} // namespace
int main() {
  try {
    const auto imported = ChordProCodec::importText(
        "{title: Luz de esperanza}\n{artist: PlayWorship}\n{key: D}\n{tempo: 96.5}\n"
        "{start_of_chorus: label=\"Coro 1\"}\nLuz de espe[Am7]ranza\n{end_of_chorus}\n");
    expect(static_cast<bool>(imported), "metadata and paired chorus import");
    expect(imported.document->text == "Luz de esperanza" && imported.document->chords.size() == 1 &&
               imported.document->chords[0].offset == 11 && imported.document->chords[0].symbol == "Am7",
           "intra-word chord anchors before the exact UTF16 letter");
    expect(imported.document->sections.size() == 1 && imported.document->sections[0].kind == "chorus" &&
               imported.document->sections[0].length == 16 && imported.document->schemaVersion == 2,
           "section maps into the native structured document");
    expect(imported.metadata.title == "Luz de esperanza" && imported.metadata.artist == "PlayWorship" &&
               imported.metadata.key == "D" && imported.metadata.tempoBpm == 96.5,
           "metadata remains separate from song/audio state");
    expect(!imported.warnings.isEmpty(), "section label normalization is explicit");
    const auto duplicates = ChordProCodec::importText("{title: Uno}\n{t: Dos}\n[C]Texto");
    expect(duplicates && duplicates.metadata.title == "Dos" && !duplicates.warnings.isEmpty(),
           "repeated metadata keeps the last value with an explicit warning");
    roundtrip(juce::String::fromUTF8("{sov}\nes[D]pe[Am7]ranza\n{eov}\n{soc}\n[G/B]Otra línea\n{eoc}\n"));
    roundtrip("{comment: Intro}\n[D] [A/C#] [Bm7] [G]\n{comment: Verso 1}\nCancio[C]nes para cantar\n");
    roundtrip("[D]\n");
    roundtrip("[D] [A] [Bm7] [G]\n");
    roundtrip(juce::String::fromUTF8("[D]Hola\n\n[A]Adiós\n\n"));
    const juce::String gridFixture = juce::String::fromUTF8(
        "{title: Instrumental}\n{start_of_grid shape=\"2x2\" label=\"Intro\"}\n"
        "| D . | A/C# . |\n| Bm7 . | G . |\n{end_of_grid}\n"
        "{sov}\n[C]Una línea\n{eov}\n"
        "{sog shape=\"4\" label=\"Solo\"}\n| Em . | C . |\n{eog}\n");
    const auto grid = ChordProCodec::importText(gridFixture);
    expect(grid && grid.document->schemaVersion == 3 && grid.document->instrumentalBlocks.size() == 2,
           "official grids become schema3 instrumental blocks");
    expect(grid.document->text == juce::String::fromUTF8("Una línea") && grid.document->chords[0].offset == 0 &&
               grid.document->sections.size() == 1,
           "grids consume no lyric characters and leave inline anchors and lyric sections unchanged");
    expect(grid.document->instrumentalBlocks[0].kind == "intro" && grid.document->instrumentalBlocks[0].label == "Intro" &&
               grid.document->instrumentalBlocks[0].anchorUtf16 == 0 &&
               grid.document->instrumentalBlocks[1].kind == "instrumental" && grid.document->instrumentalBlocks[1].label == "Solo" &&
               grid.document->instrumentalBlocks[1].anchorUtf16 == 9,
           "intro and solo keep their labels and their position before lyrics or at EOF");
    const auto &row = grid.document->instrumentalBlocks[0].rows[0];
    expect(row.cells.size() == 4 && row.barLengths == std::vector<int>({2, 2}) &&
               row.cells[0].symbol == "D" && !row.cells[1].symbol && row.cells[2].symbol == "A/C#",
           "chords, dots and internal bars remain structured rather than text spaces");
    roundtrip(gridFixture);
    roundtrip("{sov}\n[C]Uno\n{sog shape=2 label=Solo}\n| D . |\n{eog}\n[D]Dos\n{eov}\n");
    roundtrip("Texto\n{sog: 2 Solo}\n| D . |\n{eog}\n{sog: 2 Instrumental}\n| G . |\n{eog}\n");
    roundtrip("{sog label=Intro}\n| . . . . |\n{eog}\n{sog label=Solo}\n| C G Am F |\n{eog}\n");
    roundtrip("Texto\n\n{sog shape=1}\n| . |\n{eog}\n");
    const auto between = ChordProCodec::importText("[C]Uno\n{sog shape=2 label=Solo}\n| D . |\n{eog}\n[D]Dos");
    expect(between && between.document->text == "Uno\nDos" && between.document->chords[1].offset == 4 &&
               between.document->instrumentalBlocks[0].anchorUtf16 == 4,
           "a grid between lyric paragraphs anchors before the following paragraph without shifting chords");
    const auto onlyGrid = ChordProCodec::importText("{sog shape=2 label=Intro}\n| . . |\n{eog}");
    expect(onlyGrid && onlyGrid.document->text.isEmpty() && onlyGrid.document->chords.empty() && hasSongLyrics(onlyGrid.document),
           "an all-empty grid is real instrumental content without a phantom lyric line");
    const auto inferred = ChordProCodec::importText("{sog}\n| C . |\n{eog}");
    expect(inferred && !inferred.warnings.isEmpty(), "inferred grid width is disclosed before replacing an existing document");
    for (const auto &invalidGrid : {
             juce::String("{sog}\n| C . |"), juce::String("{sog}\n{eog}"),
             juce::String("{sog}\nC .\n{eog}"), juce::String("{sog}\n|| C . ||\n{eog}"),
             juce::String("{sog}\n| C % |\n{eog}"), juce::String("{sog}\n| C / |\n{eog}"),
             juce::String("{sog}\n| C~G . |\n{eog}"), juce::String("{sog}\nCoda | C . |\n{eog}"),
             juce::String("{sog}\n| C . | repeat\n{eog}"), juce::String("{sog}\n| S C . |\n{eog}"),
             juce::String("{sog shape=33}\n| C . |\n{eog}"),
             juce::String("{sog shape=4xx4}\n| C . |\n{eog}"),
             juce::String("{sog shape=1+2+1}\n| C . |\n{eog}"),
             juce::String("{sog shape=4}\n| C . |\n{eog}"),
             juce::String("{sog}\n| C . |\n| G . . |\n{eog}"),
             juce::String("{sog font=large}\n| C . |\n{eog}"),
             juce::String("{sog label=Intro label=Solo}\n| C . |\n{eog}"),
             juce::String("{sog}\n{tempo: 120}\n| C . |\n{eog}"),
             juce::String("{sog}\n{sog}\n| C . |\n{eog}\n{eog}"),
             juce::String("{eog}")}) {
      const auto result = ChordProCodec::importText(invalidGrid);
      expect(!result && result.error.isNotEmpty(), "unsupported grid data fails instead of losing cells or notation");
    }
    const auto instrument = ChordProCodec::importText("[D]");
    expect(instrument && instrument.document->text.isEmpty() && instrument.document->chords[0].offset == 0 &&
               hasSongLyrics(instrument.document), "a single instrumental chord survives empty lyric text");
    const auto unicode = ChordProCodec::importText(juce::String::fromUTF8(u8"🕊️ espe[A]ranza"));
    expect(unicode && unicode.document->chords[0].offset == 8, "emoji prefix produces UTF16 rather than codepoint offsets");
    const auto emoji = ChordProCodec::importText(juce::String::fromUTF8(u8"👩🏽‍🎤[D]canción"));
    expect(emoji && emoji.document->chords[0].offset == 7, "complete compound emoji precedes the chord");
    const auto combining = ChordProCodec::importText(juce::String::fromUTF8(u8"canci[Dm]ó n"));
    expect(static_cast<bool>(combining), "chord may precede a complete accented grapheme");
    for (const auto &invalid : {juce::String("{image: https://example.test/x}\n[C]Texto"),
                                juce::String("{start_of_abc}\n[C]Texto\n{end_of_abc}"),
                                juce::String("{transpose: 3}\n[C]Texto"),
                                juce::String("{soc: transpose=3}\n[C]Texto\n{eoc}"),
                                juce::String("{soc: label=\"Coro\" fontsize=16}\n[C]Texto\n{eoc}"),
                                juce::String("{soc}\n[C]Texto\n{eoc: otro}"),
                                juce::String("{soc}\n[C]Texto\n{eov}"),
                                juce::String("{soc}\n[C]Texto"),
                                juce::String("{c: drums in}\n[C]Texto"),
                                juce::String("[C][G]"), juce::String("[C"), juce::String("[not a chord]Texto"),
                                juce::String("<b>[C]Texto</b>"), juce::String("{tempo: 120..5}\n[C]Texto"),
                                juce::String::fromUTF8(u8"cancio[Dm]́n"), juce::String::fromUTF8(u8"👩[C]🏽‍🎤")}) {
      const auto result = ChordProCodec::importText(invalid);
      expect(!result && result.error.isNotEmpty(), "unsupported/malformed input fails without dropping content");
    }
    const auto crlf = ChordProCodec::importText("{sov}\r\n[C]Uno\r\n[D]Dos\r\n{eov}\r\n");
    expect(crlf && crlf.document->text == "Uno\nDos" && crlf.document->chords[1].offset == 4,
           "CRLF normalizes with correct native anchor positions");
    SongLyricsDocument partial;
    partial.text = "Una palabra";
    expect(applySongLyricsSection(partial, 4, 7, "chorus"), "create valid native partial-line selection");
    juce::String error;
    expect(ChordProCodec::exportText(partial, {}, &error).isEmpty() && error.isNotEmpty(),
           "export rejects a partial-line section instead of inserting lyric line breaks");
    SongLyricsDocument terminalNewline;
    terminalNewline.text = "Uno\nDos";
    expect(applySongLyricsSection(terminalNewline, 0, 4, "verse"),
           "native section may include the terminal newline before a different paragraph");
    expect(ChordProCodec::exportText(terminalNewline, {}, &error).isEmpty() && error.isNotEmpty(),
           "export rejects a terminal newline instead of silently shortening the labelled range");
    SongLyricsDocument newlineOnly;
    newlineOnly.text = "\nDos";
    expect(applySongLyricsSection(newlineOnly, 0, 1, "verse"), "native section may label a newline alone");
    expect(ChordProCodec::exportText(newlineOnly, {}, &error).isEmpty() && error.isNotEmpty(),
           "export rejects an unrepresentable newline-only section instead of dropping its label");
    SongLyricsDocument commentLike;
    commentLike.text = "#Esto es letra";
    expect(ChordProCodec::exportText(commentLike, {}, &error).isEmpty() && error.isNotEmpty(),
           "export rejects literal comments instead of silently losing lyric content");
    auto unlabelledIntro = *onlyGrid.document;
    unlabelledIntro.instrumentalBlocks[0].label.clear();
    expect(isValidSongLyricsDocument(unlabelledIntro) &&
               ChordProCodec::exportText(unlabelledIntro, {}, &error).isEmpty() && error.isNotEmpty(),
           "an unrepresentable intro label fails explicitly instead of silently changing kind");
    auto unevenGrid = *grid.document;
    unevenGrid.instrumentalBlocks[0].rows[1].cells.pop_back();
    unevenGrid.instrumentalBlocks[0].rows[1].barLengths.clear();
    expect(isValidSongLyricsDocument(unevenGrid) &&
               ChordProCodec::exportText(unevenGrid, {}, &error).isEmpty() && error.isNotEmpty(),
           "unequal native row widths fail export without adding or removing cells");
    std::cout << "ChordPro codec: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
