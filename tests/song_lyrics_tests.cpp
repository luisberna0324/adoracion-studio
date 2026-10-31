#include "data/SongLyrics.h"
#include "platform/NativeSongLyricsEditing.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void expect(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
juce::String json(const SongLyricsDocument &document) {
  return juce::JSON::toString(songLyricsToVar(document), true);
}
SongLyricsDocument withText(const juce::String &text) { return {1, text, {}}; }

void codec() {
  auto document = withText(juce::String::fromUTF8(u8"Aquí 🎹 cantamos\nTodos juntos"));
  expect(songLyricsUtf16Length(document.text) == document.text.length() + 1,
         "supplementary character counts two UTF16 units as on UIKit and Android");
  expect(applySongLyricsSection(document, 0, 16, "verse"), "assign first native text range");
  expect(applySongLyricsSection(document, 17, 12, "chorus"), "assign second range after newline");
  expect(isValidSongLyricsDocument(document) && hasSongLyrics(document), "native document validates");
  const auto encoded = songLyricsToVar(document);
  expect(encoded.getProperty("offsetEncoding", {}).toString() == "utf16", "JSON declares its offset encoding explicitly");
  const auto decoded = songLyricsFromVar(juce::JSON::parse(json(document)));
  expect(decoded.has_value() && json(*decoded) == json(document), "JSON roundtrip preserves text, ranges, IDs and kinds");
  auto reversed = document;
  std::reverse(reversed.sections.begin(), reversed.sections.end());
  expect(json(reversed) == json(document), "serialization orders independent section ranges canonically");
  expect(!hasSongLyrics(std::nullopt) && !hasSongLyrics(withText(" \n\t")), "empty optional text stays optional");
  expect(isValidSongLyricsDocument(withText("")), "empty editor document is valid");
  const auto invalid = [&](const std::function<void(juce::var &)> &mutate) {
    auto value = juce::JSON::parse(json(document));
    mutate(value);
    expect(!songLyricsFromVar(value), "malformed document rejects atomically");
  };
  const auto section = [](juce::var &value, int index = 0) {
    return value.getProperty("sections", {}).getArray()->getReference(index).getDynamicObject();
  };
  invalid([](auto &v) { v.getDynamicObject()->setProperty("schemaVersion", 2); });
  invalid([](auto &v) { v.getDynamicObject()->setProperty("schemaVersion", true); });
  invalid([](auto &v) { v.getDynamicObject()->setProperty("offsetEncoding", "unicode-codepoints"); });
  invalid([](auto &v) { v.getDynamicObject()->removeProperty("offsetEncoding"); });
  invalid([](auto &v) { v.getDynamicObject()->setProperty("text", 17); });
  invalid([](auto &v) { v.getDynamicObject()->setProperty("sections", "not-array"); });
  invalid([&](auto &v) { section(v)->setProperty("kind", "arbitrary-custom-kind"); });
  invalid([&](auto &v) { section(v)->setProperty("id", " "); });
  invalid([&](auto &v) { section(v, 1)->setProperty("id", section(v)->getProperty("id")); });
  invalid([&](auto &v) { section(v)->setProperty("start", -1); });
  invalid([&](auto &v) { section(v)->setProperty("start", 50000); });
  invalid([&](auto &v) { section(v)->setProperty("start", true); });
  invalid([&](auto &v) { section(v)->setProperty("length", 1.5); });
  invalid([&](auto &v) { section(v)->setProperty("length", juce::int64(0x7fffffffffffffffLL)); });
  invalid([&](auto &v) { section(v)->setProperty("length", 0); });
  invalid([&](auto &v) { section(v, 1)->setProperty("start", 1); });
  expect(!songLyricsFromVar(juce::JSON::parse(R"({"schemaVersion":1,"blocks":[]})")),
         "the discarded chord prototype is never misread as native text ranges");
}

void chordCodec() {
  auto document = withText("esperanza");
  expect(applySongLyricsSection(document, 0, 9, "verse") &&
         setSongLyricsChord(document, 0, "G") && setSongLyricsChord(document, 4, "D/F#"),
         "chords can attach to an exact position inside a word without changing text or sections");
  expect(document.schemaVersion == 2 && document.text == "esperanza" && document.sections.size() == 1,
         "first chord upgrades only the lyrics schema and leaves playback-independent text intact");
  const auto firstId = document.chords[0].id, middleId = document.chords[1].id;
  expect(setSongLyricsChord(document, 4, "  Em7  ") && document.chords.size() == 2 &&
         document.chords[1].symbol == "Em7" && document.chords[1].id == middleId,
         "same-position chord correction retains its stable ID and trims entered whitespace");
  const auto encoded = songLyricsToVar(document);
  expect(encoded.getProperty("schemaVersion", {}).isInt() &&
         static_cast<int>(encoded.getProperty("schemaVersion", {})) == 2 &&
         encoded.getProperty("chords", {}).getArray()->size() == 2,
         "schema two explicitly serializes the chord array");
  const auto decoded = songLyricsFromVar(juce::JSON::parse(json(document)));
  expect(decoded && json(*decoded) == json(document), "chord JSON roundtrip retains IDs, UTF16 positions and symbols");
  auto reversed = document;
  std::reverse(reversed.chords.begin(), reversed.chords.end());
  expect(json(reversed) == json(document), "chord serialization is ordered canonically by exact position");
  const auto invalid = [&](const std::function<void(juce::var &)> &mutate) {
    auto value = juce::JSON::parse(json(document));
    mutate(value);
    expect(!songLyricsFromVar(value), "malformed chord document rejects atomically");
  };
  const auto chord = [](juce::var &value, int index = 0) {
    return value.getProperty("chords", {}).getArray()->getReference(index).getDynamicObject();
  };
  invalid([](auto &v) { v.getDynamicObject()->setProperty("schemaVersion", 1); });
  invalid([](auto &v) { v.getDynamicObject()->removeProperty("chords"); });
  invalid([](auto &v) { v.getDynamicObject()->setProperty("chords", "not-an-array"); });
  invalid([&](auto &v) { chord(v)->setProperty("id", " "); });
  invalid([&](auto &v) { chord(v, 1)->setProperty("id", firstId); });
  invalid([&](auto &v) { chord(v)->setProperty("id", document.sections[0].id); });
  invalid([&](auto &v) { chord(v)->setProperty("symbol", true); });
  invalid([&](auto &v) { chord(v)->setProperty("symbol", "[G]"); });
  invalid([&](auto &v) { chord(v)->setProperty("offset", true); });
  invalid([&](auto &v) { chord(v)->setProperty("offset", 1.5); });
  invalid([&](auto &v) { chord(v)->setProperty("offset", -1); });
  invalid([&](auto &v) { chord(v)->setProperty("offset", juce::int64(0x7fffffffffffffffLL)); });
  invalid([&](auto &v) { chord(v)->setProperty("offset", 10); });
  invalid([&](auto &v) { chord(v, 1)->setProperty("offset", 0); });
  expect(removeSongLyricsChord(document, middleId) && document.schemaVersion == 2 &&
         removeSongLyricsChord(document, firstId) && document.schemaVersion == 1 && document.chords.empty(),
         "removing only the final chord restores the canonical version one schema");
  expect(!songLyricsToVar(document).getDynamicObject()->hasProperty("chords"),
         "unchanged or chord-free documents retain the original version one envelope");
  auto instrumental = withText("");
  expect(setSongLyricsChord(instrumental, 0, "C") && hasSongLyrics(instrumental) &&
         isEditableSongLyricsDocument(instrumental), "a chord-only instrumental is present even without lyric text");
  const auto restored = songLyricsFromVar(juce::JSON::parse(json(instrumental)));
  expect(restored && hasSongLyrics(restored) && restored->chords[0].offset == 0,
         "empty-text instrumental chords survive persistence at the EOF position");
}

void chordEdits() {
  auto document = withText("abcdef");
  expect(setSongLyricsChord(document, 0, "G") && setSongLyricsChord(document, 2, "D") &&
         setSongLyricsChord(document, 4, "Em") && setSongLyricsChord(document, 6, "C"),
         "prepare four independent chord anchors including EOF");
  const auto first = document.chords[0].id, second = document.chords[1].id,
      third = document.chords[2].id, eof = document.chords[3].id;
  expect(replaceSongLyricsText(document, 2, 0, "XY") && document.text == "abXYcdef" &&
         document.chords[0].offset == 0 && document.chords[1].offset == 4 &&
         document.chords[2].offset == 6 && document.chords[3].offset == 8 &&
         document.chords[1].id == second && document.chords[3].id == eof,
         "insertion at an anchor has right affinity and keeps chord identity on original text");
  expect(replaceSongLyricsText(document, 1, 5, "Z") && document.text == "aZef" &&
         document.chords.size() == 3 && document.chords[0].id == first &&
         document.chords[1].id == third && document.chords[1].offset == 2 &&
         document.chords[2].id == eof && document.chords[2].offset == 4,
         "replacement removes only anchors in its half-open range while the range-end anchor survives");
  const auto same = json(document);
  expect(setSongLyricsText(document, document.text) && json(document) == same,
         "full-text bridge no-op preserves every chord identity");
  expect(setSongLyricsText(document, "Before aZef") && document.chords[0].offset == 7 &&
         document.chords[1].offset == 9 && document.chords[2].offset == 11 &&
         document.chords[1].id == third,
         "full-text bridge shifts surviving chords using UTF16 units");
  const auto stable = json(document);
  expect(!setSongLyricsChord(document, -1, "G") && !setSongLyricsChord(document, 200, "G") &&
         !setSongLyricsChord(document, 1, "not a chord") && !removeSongLyricsChord(document, "missing") &&
         json(document) == stable, "invalid chord changes leave the entire draft unchanged");
  auto only = withText("hello");
  expect(setSongLyricsChord(only, 0, "Am") && replaceSongLyricsText(only, 0, 5, "") &&
         only.text.isEmpty() && only.chords.empty() && only.schemaVersion == 1 && !hasSongLyrics(only),
         "deleting the entire anchor text removes its chord without retaining an empty version two document");
  auto unicode = withText(juce::String::fromUTF8(u8"A🎹B"));
  expect(setSongLyricsChord(unicode, 3, "F") &&
         replaceSongLyricsText(unicode, 0, 1, juce::String::fromUTF8(u8"🎸")) && unicode.chords[0].offset == 4,
         "supplementary Unicode text edits shift chord positions by code units rather than scalar count");
  auto joined = withText(juce::String::fromUTF8(u8"🇨x🇴"));
  expect(setSongLyricsChord(joined, 3, "G"), "prepare chord on an independent regional indicator");
  const auto joinedBefore = json(joined);
  expect(!replaceSongLyricsText(joined, 2, 1, "") && json(joined) == joinedBefore,
         "an edit that would strand a chord inside a newly joined grapheme rejects atomically");
  auto native = withText("Chorus");
  native.sections = {{"card", "chorus", 0, 6}};
  expect(setSongLyricsChord(native, 1, "C") && NativeSongLyricsEditing::replaceText(native, 6, 0, "\n") &&
         native.sections[0].length == 7 && native.chords.size() == 1 && native.chords[0].offset == 1,
         "native paragraph ownership updates preserve chord metadata while Enter expands the card");
}

void chordUnicodeAndLimits() {
  const auto accent = juce::String::fromUTF8(u8"cancio\u0301n");
  expect(isSongLyricsChordBoundary(accent, 5) && !isSongLyricsChordBoundary(accent, 6) &&
         isSongLyricsChordBoundary(accent, 7), "chord placement cannot split a decomposed Spanish accent");
  const auto singer = juce::String::fromUTF8(u8"👩🏽‍🎤canción");
  bool singerBoundaries = isSongLyricsChordBoundary(singer, 0) && isSongLyricsChordBoundary(singer, 7);
  for (int offset = 1; offset < 7; ++offset) singerBoundaries = singerBoundaries && !isSongLyricsChordBoundary(singer, offset);
  expect(singerBoundaries, "skin modifier, surrogate pairs and joined emoji remain one chord placement unit");
  const auto flag = juce::String::fromUTF8(u8"🇨🇴🇺🇸");
  expect(isSongLyricsChordBoundary(flag, 0) && !isSongLyricsChordBoundary(flag, 2) &&
         isSongLyricsChordBoundary(flag, 4) && !isSongLyricsChordBoundary(flag, 6) &&
         isSongLyricsChordBoundary(flag, 8), "regional indicators stay paired into flags");
  expect(isSongLyricsChordBoundary("A\r\nB", 1) && !isSongLyricsChordBoundary("A\r\nB", 2) &&
         isSongLyricsChordBoundary("A\r\nB", 3), "CRLF is indivisible for a chord anchor");
  const auto variation = juce::String::fromUTF8(u8"✝️A");
  expect(!isSongLyricsChordBoundary(variation, 1) && isSongLyricsChordBoundary(variation, 2),
         "variation selectors stay with their displayed character");
  const auto conjunct = juce::String::fromUTF8(u8"क्षA");
  expect(!isSongLyricsChordBoundary(conjunct, 1) && !isSongLyricsChordBoundary(conjunct, 2) &&
         isSongLyricsChordBoundary(conjunct, 3), "common virama conjuncts cannot receive an interior chord anchor");
  const juce::StringArray valid{"C", "Cm7", "F#sus4", "Bbmaj7", "G/B", "D7(b9)", "N.C.", "NC",
      juce::String::fromUTF8(u8"F♯m7/C♯"), juce::String::fromUTF8(u8"CΔ7"), juce::String::fromUTF8(u8"Bø7")};
  expect(std::all_of(valid.begin(), valid.end(), [](const auto &symbol) { return isValidSongLyricsChordSymbol(symbol); }),
         "common roots, extensions, inversions and Unicode accidentals are valid symbols");
  const juce::StringArray invalid{"", " C", "G ", "G D", "H", "[D]", "C\nG", "C/", "C/H", "C/G/B", "C(b9", "C)b9(", "C\\G"};
  expect(std::none_of(invalid.begin(), invalid.end(), [](const auto &symbol) { return isValidSongLyricsChordSymbol(symbol); }),
         "unsafe or malformed notation never becomes a stored chord symbol");
  auto many = withText(juce::String::repeatedString("x", kSongLyricsMaxChords));
  many.schemaVersion = 2;
  for (int offset = 0; offset < kSongLyricsMaxChords; ++offset)
    many.chords.push_back({"chord-" + juce::String(offset), offset, "G"});
  expect(isEditableSongLyricsDocument(many), "exactly 4096 distinct chord anchors fit the model budget");
  const auto stable = json(many);
  expect(!setSongLyricsChord(many, kSongLyricsMaxChords, "C") && json(many) == stable,
         "adding a chord beyond the budget rejects without deleting existing anchors");
  expect(setSongLyricsChord(many, 0, "Em7") && many.chords[0].id == "chord-0",
         "replacing an existing symbol is still allowed at the chord count budget");
  many.chords.push_back({"overflow", kSongLyricsMaxChords, "C"});
  expect(!isValidSongLyricsDocument(many) && !songLyricsFromVar(songLyricsToVar(many)),
         "native and JSON chord counts share the same strict limit");
}

void instrumentalCodec() {
  auto document = withText("");
  juce::String introId, soloId;
  expect(addSongLyricsInstrumentalBlock(document, 0, "intro", "Intro", 4, &introId) &&
         document.schemaVersion == 3 && document.text.isEmpty() && document.sections.empty() &&
         document.chords.empty() && hasSongLyrics(document),
         "a truly empty Intro card is structured content without synthetic lyric characters");
  const auto rowId = document.instrumentalBlocks[0].rows[0].id;
  const auto cellId = document.instrumentalBlocks[0].rows[0].cells[0].id;
  expect(document.instrumentalBlocks[0].rows[0].cells.size() == 4 &&
         !document.instrumentalBlocks[0].rows[0].cells[0].symbol &&
         addSongLyricsInstrumentalBlock(document, 0, "instrumental", "Solo", 4, &soloId) &&
         document.instrumentalBlocks[0].order == 0 && document.instrumentalBlocks[1].order == 1,
         "independent Intro and Solo cards coexist at offset zero with ordered empty cells");
  expect(setSongLyricsInstrumentalCell(document, introId, rowId, cellId, " Cmaj7 ") &&
         document.instrumentalBlocks[0].rows[0].cells[0].id == cellId &&
         document.instrumentalBlocks[0].rows[0].cells[0].symbol == "Cmaj7" &&
         setSongLyricsInstrumentalCell(document, introId, rowId, cellId, "Am7") &&
         document.instrumentalBlocks[0].rows[0].cells[0].id == cellId,
         "choosing and correcting an instrumental chord preserves the exact cell identity");
  juce::String nextRowId;
  expect(addSongLyricsInstrumentalRow(document, introId, 4, &nextRowId) &&
         document.instrumentalBlocks[0].rows.size() == 2 &&
         document.instrumentalBlocks[0].rows[1].id == nextRowId && document.text.isEmpty(),
         "adding a real instrumental row never inserts a newline into the lyric document");
  document.instrumentalBlocks[0].rows[0].barLengths = {2, 2};
  expect(isEditableSongLyricsDocument(document), "instrumental rows preserve explicit internal bar boundaries");
  const auto roundtrip = songLyricsFromVar(juce::JSON::parse(json(document)));
  expect(roundtrip && json(*roundtrip) == json(document),
         "version three roundtrip retains block, row and cell IDs, empty cells, symbols and bars");
  auto reversed = document;
  std::reverse(reversed.instrumentalBlocks.begin(), reversed.instrumentalBlocks.end());
  expect(json(reversed) == json(document), "instrumental cards serialize canonically by anchor and order");
  const auto invalid = [&](const std::function<void(juce::var &)> &mutate) {
    auto value = juce::JSON::parse(json(document));
    mutate(value);
    expect(!songLyricsFromVar(value), "malformed instrumental JSON rejects the whole document atomically");
  };
  const auto block = [](juce::var &v, int index = 0) {
    return v.getProperty("instrumentalBlocks", {}).getArray()->getReference(index).getDynamicObject();
  };
  const auto row = [&](juce::var &v) { return block(v)->getProperty("rows").getArray()->getReference(0).getDynamicObject(); };
  const auto cell = [&](juce::var &v, int index = 0) { return row(v)->getProperty("cells").getArray()->getReference(index).getDynamicObject(); };
  invalid([](auto &v) { v.getDynamicObject()->setProperty("schemaVersion", 2); });
  invalid([](auto &v) { v.getDynamicObject()->removeProperty("instrumentalBlocks"); });
  invalid([](auto &v) { v.getDynamicObject()->removeProperty("chords"); });
  invalid([&](auto &v) { block(v)->setProperty("anchorUtf16", true); });
  invalid([&](auto &v) { block(v)->setProperty("anchorUtf16", 1); });
  invalid([&](auto &v) { block(v)->setProperty("order", -1); });
  invalid([&](auto &v) { block(v)->setProperty("order", 0.5); });
  invalid([&](auto &v) { block(v, 1)->setProperty("order", 0); });
  invalid([&](auto &v) { block(v)->setProperty("kind", "verse"); });
  invalid([&](auto &v) { block(v)->setProperty("label", "Intro\nSolo"); });
  invalid([&](auto &v) { block(v)->setProperty("rows", juce::Array<juce::var>{}); });
  invalid([&](auto &v) { row(v)->setProperty("id", introId); });
  invalid([&](auto &v) { row(v)->setProperty("cells", juce::Array<juce::var>{}); });
  invalid([&](auto &v) { cell(v, 1)->setProperty("id", cellId); });
  invalid([&](auto &v) { cell(v)->setProperty("symbol", ""); });
  invalid([&](auto &v) { cell(v)->setProperty("symbol", juce::var()); });
  invalid([&](auto &v) { cell(v)->setProperty("symbol", "[C]"); });
  invalid([&](auto &v) { row(v)->setProperty("barLengths", "not-array"); });
  invalid([&](auto &v) { row(v)->getProperty("barLengths").getArray()->set(0, 3); });
  invalid([&](auto &v) { row(v)->getProperty("barLengths").getArray()->set(0, 0); });
  invalid([&](auto &v) { row(v)->getProperty("barLengths").getArray()->set(0, 2.0); });
  expect(clearSongLyricsInstrumentalCell(document, introId, rowId, cellId) &&
         !document.instrumentalBlocks[0].rows[0].cells[0].symbol && document.schemaVersion == 3 &&
         hasSongLyrics(document), "clearing the last cell symbol preserves the empty instrumental card and its schema");
  expect(setSongLyricsChord(document, 0, "G") && removeSongLyricsChord(document, document.chords[0].id) &&
         document.schemaVersion == 3, "removing the last inline chord never downgrades a document containing instrumental cards");
  expect(removeSongLyricsInstrumentalBlock(document, introId) && removeSongLyricsInstrumentalBlock(document, soloId) &&
         document.schemaVersion == 1 && !hasSongLyrics(document),
         "only explicitly removing both empty cards clears the optional lyric document");
}

void instrumentalEdits() {
  auto document = withText("Verse\nChorus\n");
  juce::String intro, solo, ending;
  expect(addSongLyricsInstrumentalBlock(document, 0, "intro", "Intro", 4, &intro) &&
         addSongLyricsInstrumentalBlock(document, 6, "instrumental", "Solo", 4, &solo) &&
         addSongLyricsInstrumentalBlock(document, 13, "instrumental", "Instrumental", 4, &ending),
         "instrumental cards attach to paragraph boundaries and EOF");
  const auto stable = json(document);
  expect(!addSongLyricsInstrumentalBlock(document, 2, "intro", "Intro") &&
         !addSongLyricsInstrumentalBlock(document, 0, "chorus", "Coro") &&
         !addSongLyricsInstrumentalRow(document, intro, 0) &&
         !addSongLyricsInstrumentalRow(document, intro, kSongLyricsMaxInstrumentalCellsPerRow + 1) &&
         !setSongLyricsInstrumentalCell(document, intro, "missing", "missing", "C") &&
         !clearSongLyricsInstrumentalCell(document, intro, "missing", "missing") &&
         !removeSongLyricsInstrumentalBlock(document, "missing") && json(document) == stable,
         "invalid instrumental commands do not alter cards, content or identities");
  expect(replaceSongLyricsText(document, 0, 0, "Hoy ") && document.instrumentalBlocks[0].anchorUtf16 == 0 &&
         document.instrumentalBlocks[1].anchorUtf16 == 10 && document.instrumentalBlocks[2].anchorUtf16 == 17,
         "typing at the Intro anchor keeps it before the new text while later cards follow their paragraphs");
  expect(replaceSongLyricsText(document, 9, 1, "") && document.text == "Hoy VerseChorus\n" &&
         document.instrumentalBlocks[1].anchorUtf16 == 16 && document.instrumentalBlocks[1].id == solo &&
         document.instrumentalBlocks[2].anchorUtf16 == 16 && document.instrumentalBlocks[2].id == ending &&
         document.instrumentalBlocks[1].order < document.instrumentalBlocks[2].order,
         "removing a paragraph boundary moves its independent card forward while preserving merged card order");
  expect(replaceSongLyricsText(document, 0, 16, "") && document.text.isEmpty() &&
         document.instrumentalBlocks.size() == 3 && document.schemaVersion == 3 &&
         document.instrumentalBlocks[0].id == intro && document.instrumentalBlocks[1].id == solo &&
         document.instrumentalBlocks[2].id == ending &&
         std::all_of(document.instrumentalBlocks.begin(), document.instrumentalBlocks.end(), [](const auto &b) { return b.anchorUtf16 == 0; }),
         "deleting all lyric text preserves all independent instrumental cards in their original order");
  expect(isSongLyricsInstrumentalAnchor("A\r\nB", 0) && !isSongLyricsInstrumentalAnchor("A\r\nB", 1) &&
         !isSongLyricsInstrumentalAnchor("A\r\nB", 2) && isSongLyricsInstrumentalAnchor("A\r\nB", 3) &&
         isSongLyricsInstrumentalAnchor("A\r\nB", 4), "card anchors respect complete CRLF separators and EOF");
  const auto unicode = juce::String::fromUTF8(u8"🎹\nLetra");
  expect(!isSongLyricsInstrumentalAnchor(unicode, 1) && !isSongLyricsInstrumentalAnchor(unicode, 2) &&
         isSongLyricsInstrumentalAnchor(unicode, 3), "instrumental anchors count UTF16 and never split an emoji or attach inside a line");
  auto mixed = withText("A");
  expect(setSongLyricsChord(mixed, 0, "C") && addSongLyricsInstrumentalBlock(mixed, 1, "instrumental") &&
         removeSongLyricsInstrumentalBlock(mixed, mixed.instrumentalBlocks[0].id) && mixed.schemaVersion == 2,
         "removing the final instrumental card returns to version two when inline chords remain");
}

void instrumentalLimits() {
  auto full = withText("");
  full.schemaVersion = 3;
  SongLyricsInstrumentalBlock block;
  block.id = "block"; block.kind = "intro";
  for (int row = 0; row < kSongLyricsMaxInstrumentalRowsPerBlock; ++row) {
    SongLyricsInstrumentalRow entry; entry.id = "row-" + juce::String(row);
    for (int cell = 0; cell < kSongLyricsMaxInstrumentalCellsPerRow; ++cell)
      entry.cells.push_back({"cell-" + juce::String(row) + "-" + juce::String(cell), std::nullopt});
    block.rows.push_back(std::move(entry));
  }
  full.instrumentalBlocks.push_back(std::move(block));
  expect(isEditableSongLyricsDocument(full), "a complete 4096-cell instrumental chart fits the bounded model");
  const auto fullBefore = json(full);
  expect(!addSongLyricsInstrumentalRow(full, "block", 1) &&
         !addSongLyricsInstrumentalBlock(full, 0, "instrumental", "Solo", 1) && json(full) == fullBefore,
         "row and total cell budget overflow are rejected without truncating existing instrumental content");
  auto many = withText(""); many.schemaVersion = 3;
  for (int i = 0; i < kSongLyricsMaxInstrumentalBlocks; ++i) {
    SongLyricsInstrumentalBlock b; b.id = "b-" + juce::String(i); b.kind = "instrumental"; b.order = i;
    SongLyricsInstrumentalRow r; r.id = "r-" + juce::String(i);
    r.cells.push_back({"c-" + juce::String(i), std::nullopt}); b.rows.push_back(std::move(r));
    many.instrumentalBlocks.push_back(std::move(b));
  }
  const auto manyBefore = json(many);
  expect(isEditableSongLyricsDocument(many) && !addSongLyricsInstrumentalBlock(many, 0, "intro", "Intro", 1) &&
         json(many) == manyBefore, "the instrumental card budget is enforced atomically");
  auto chords = withText(juce::String::repeatedString("x", kSongLyricsMaxChords));
  chords.schemaVersion = 2;
  for (int i = 0; i < kSongLyricsMaxChords; ++i) chords.chords.push_back({"inline-" + juce::String(i), i, "C"});
  expect(addSongLyricsInstrumentalBlock(chords, 0, "intro", "Intro", 1),
         "empty cells can coexist with the maximum number of inline chords");
  const auto blockId = chords.instrumentalBlocks[0].id, rowId = chords.instrumentalBlocks[0].rows[0].id,
      cellId = chords.instrumentalBlocks[0].rows[0].cells[0].id;
  const auto before = json(chords);
  expect(!setSongLyricsInstrumentalCell(chords, blockId, rowId, cellId, "G") && json(chords) == before,
         "instrumental symbols and inline symbols share one combined chord budget");
  expect(removeSongLyricsChord(chords, "inline-0") &&
         setSongLyricsInstrumentalCell(chords, blockId, rowId, cellId, "G"), "freeing one inline slot admits one instrumental chord");
  const auto combined = json(chords);
  expect(!setSongLyricsChord(chords, kSongLyricsMaxChords, "D") && json(chords) == combined,
         "an inline insertion cannot bypass the combined chord budget after instrumental editing");
}

void cardReordering() {
  auto document = withText("Uno\nDos");
  document.sections = {{"a", "verse", 0, 3}, {"b", "chorus", 4, 3}};
  expect(setSongLyricsChord(document, 0, "C") && setSongLyricsChord(document, 3, "G") &&
         setSongLyricsChord(document, 4, "Dm") && setSongLyricsChord(document, 7, "F"),
         "prepare exact lyric and end-of-line chord anchors for reordering");
  const auto original = json(document);
  const auto c = document.chords[0].id, g = document.chords[1].id, dm = document.chords[2].id, f = document.chords[3].id;
  expect(canMoveSongLyricsCard(document, "a") && canMoveSongLyricsCard(document, "b") &&
         moveSongLyricsCardBefore(document, "b", "a") && document.text == "Dos\nUno" &&
         document.sections[0].id == "b" && document.sections[0].start == 0 && document.sections[0].kind == "chorus" &&
         document.sections[1].id == "a" && document.sections[1].start == 4 && document.sections[1].kind == "verse",
         "moving the last card before the first preserves IDs and types without joining their lines at EOF");
  expect(document.chords[0].id == dm && document.chords[0].offset == 0 &&
         document.chords[1].id == f && document.chords[1].offset == 3 &&
         document.chords[2].id == c && document.chords[2].offset == 4 &&
         document.chords[3].id == g && document.chords[3].offset == 7,
         "the excluded LF-end chord and EOF chord follow their exact lyric bodies instead of being deleted");
  expect(moveSongLyricsCardBefore(document, "a", "b") && json(document) == original,
         "reversing a card move restores the exact text, ranges, chord symbols and stable IDs");
  expect(moveSongLyricsCardBefore(document, "a", "") && document.text == "Dos\nUno",
         "the empty destination places a card after the final card");
  const auto last = json(document);
  expect(moveSongLyricsCardBefore(document, "a", "a") && moveSongLyricsCardBefore(document, "a", "") &&
         json(document) == last, "self and already-final card moves are identity-preserving no-ops");
  expect(!moveSongLyricsCardBefore(document, "missing", "a") && !moveSongLyricsCardBefore(document, "a", "missing") &&
         json(document) == last, "unknown move identities reject without changing any document field");
  auto crlf = withText("A\r\n\r\nB\r\nC");
  crlf.sections = {{"a", "verse", 0, 1}, {"b", "chorus", 5, 1}, {"c", "bridge", 8, 1}};
  expect(setSongLyricsChord(crlf, 6, "D") && moveSongLyricsCardBefore(crlf, "c", "a") &&
         crlf.text == "C\r\n\r\nA\r\nB" && crlf.sections[0].id == "c" && crlf.sections[1].start == 5 &&
         crlf.sections[2].start == 8 && crlf.chords[0].offset == 9,
         "CRLF connectors and blank gaps survive while a last-line chord moves to its new EOF");
  auto owned = withText("One\nTwo\n");
  owned.sections = {{"one", "verse", 0, 4}, {"two", "chorus", 4, 4}};
  expect(moveSongLyricsCardBefore(owned, "two", "one") && owned.text == "Two\nOne\n" &&
         owned.sections[0].length == 4 && owned.sections[1].start == 4 && owned.sections[1].length == 4,
         "cards which own their terminal LF remain complete and retain the real trailing document newline");
  auto unicode = withText(juce::String::fromUTF8(u8"🎹 canta\nCoro"));
  unicode.sections = {{"piano", "verse", 0, 8}, {"coro", "chorus", 9, 4}};
  expect(setSongLyricsChord(unicode, 3, "Am") && moveSongLyricsCardBefore(unicode, "coro", "piano") &&
         unicode.text == juce::String::fromUTF8(u8"Coro\n🎹 canta") && unicode.chords[0].offset == 8 &&
         unicode.sections[1].start == 5 && unicode.sections[1].length == 8,
         "card permutation maps Unicode code-unit offsets without splitting a supplementary character");
  auto multiline = withText("A1\r\nA2\nB\nTail");
  multiline.sections = {{"a", "verse", 0, 6}, {"b", "chorus", 7, 1}, {"tail", "ending", 9, 4}};
  expect(setSongLyricsChord(multiline, 4, "Am") && moveSongLyricsCardBefore(multiline, "a", "") &&
         multiline.text == "B\nTail\nA1\r\nA2" && multiline.sections.back().id == "a" &&
         multiline.sections.back().start == 7 && multiline.sections.back().length == 6 && multiline.chords[0].offset == 11,
         "a multi-paragraph card retains its exact internal CRLF and chord positions while external connectors are reused");
  auto repeated = withText("Same\nSame");
  repeated.sections = {{"first", "verse", 0, 4}, {"second", "verse", 5, 4}};
  expect(moveSongLyricsCardBefore(repeated, "second", "first") && repeated.text == "Same\nSame" &&
         repeated.sections[0].id == "second" && repeated.sections[1].id == "first",
         "equal-looking lyric cards still reorder stable identities even when their visible text remains identical");
  auto historical = withText("Before X after\nB");
  historical.sections = {{"part", "verse", 7, 1}, {"whole", "chorus", 15, 1}};
  juce::String marker;
  expect(addSongLyricsInstrumentalBlock(historical, 0, "intro", "Intro", 1, &marker) &&
         !canMoveSongLyricsCard(historical, "part") && canMoveSongLyricsCard(historical, "whole") &&
         !moveSongLyricsCardBefore(historical, "whole", "part") &&
         moveSongLyricsCardBefore(historical, "whole", marker) && historical.text == "B\nBefore X after" &&
         historical.sections[1].id == "part" && historical.sections[1].start == 9 && historical.sections[1].length == 1,
         "historical inline labels cannot be dragged or targeted but survive other card moves with their exact text");
  auto metadata = withText("A\nB\nC");
  metadata.sections = {{"a", "verse", 0, 1}, {"b", "chorus", 2, 1}, {"c", "bridge", 4, 1}};
  juce::String intro, solo, footer;
  expect(addSongLyricsInstrumentalBlock(metadata, 0, "intro", "Intro", 1, &intro) &&
         addSongLyricsInstrumentalBlock(metadata, 2, "instrumental", "Solo", 1, &solo) &&
         addSongLyricsInstrumentalBlock(metadata, 5, "instrumental", "Outro", 1, &footer), "prepare independent legacy cards between lyric cards");
  const auto row = metadata.instrumentalBlocks[1].rows[0].id, cell = metadata.instrumentalBlocks[1].rows[0].cells[0].id;
  expect(setSongLyricsInstrumentalCell(metadata, solo, row, cell, "D/F#") &&
         moveSongLyricsCardBefore(metadata, "c", "a") && metadata.text == "C\nA\nB" &&
         metadata.instrumentalBlocks[0].id == intro && metadata.instrumentalBlocks[0].anchorUtf16 == 0 &&
         metadata.instrumentalBlocks[1].id == solo && metadata.instrumentalBlocks[1].anchorUtf16 == 4 &&
         metadata.instrumentalBlocks[1].rows[0].cells[0].id == cell &&
         metadata.instrumentalBlocks[1].rows[0].cells[0].symbol == "D/F#" &&
         metadata.instrumentalBlocks[2].id == footer && metadata.instrumentalBlocks[2].anchorUtf16 == 5,
         "moving one lyric card leaves independent instrumental cards in card order and preserves their complete data");
  const auto text = metadata.text;
  expect(canMoveSongLyricsCard(metadata, solo) && moveSongLyricsCardBefore(metadata, solo, intro) && metadata.text == text &&
         metadata.instrumentalBlocks[0].id == solo && metadata.instrumentalBlocks[0].anchorUtf16 == 0 &&
         metadata.instrumentalBlocks[0].order == 0 && metadata.instrumentalBlocks[1].id == intro &&
         metadata.instrumentalBlocks[1].order == 1 && metadata.instrumentalBlocks[0].rows[0].cells[0].id == cell,
         "moving an inherited instrumental before another changes only anchor and stable ordering");
  expect(moveSongLyricsCardBefore(metadata, solo, "b") && metadata.text == text &&
         metadata.instrumentalBlocks[1].id == solo && metadata.instrumentalBlocks[1].anchorUtf16 == 4 &&
         moveSongLyricsCardBefore(metadata, solo, "") && metadata.instrumentalBlocks.back().id == solo &&
         metadata.instrumentalBlocks.back().anchorUtf16 == 5,
         "inherited instrumental cards can move before a lyric card or after the final card without synthetic text");
  auto blank = withText("\n\nB"); blank.sections = {{"blank", "verse", 0, 2}, {"b", "chorus", 2, 1}};
  expect(!canMoveSongLyricsCard(blank, "blank"), "a whitespace-only historical mark is preserved without exposing an ambiguous reorder handle");
  auto nested = withText("One\nTwo\nEnd");
  nested.sections = {{"verse", "verse", 0, 7}, {"end", "chorus", 8, 3}};
  juce::String nestedId;
  expect(addSongLyricsInstrumentalBlock(nested, 4, "instrumental", "Solo", 1, &nestedId) &&
         !canMoveSongLyricsCard(nested, "verse") && canMoveSongLyricsCard(nested, nestedId),
         "an instrumental strictly inside a lyric card makes only that lyric card non-draggable");
  const auto beforeNestedFailure = json(nested);
  expect(!moveSongLyricsCardBefore(nested, "verse", "end") &&
         !moveSongLyricsCardBefore(nested, "end", nestedId) && json(nested) == beforeNestedFailure,
         "nested lyric sources and nested drop destinations reject atomically");
  expect(moveSongLyricsCardBefore(nested, nestedId, "verse") && nested.text == "One\nTwo\nEnd" &&
         nested.instrumentalBlocks[0].anchorUtf16 == 0 && canMoveSongLyricsCard(nested, "verse"),
         "an inherited nested instrumental can be moved out before its verse without changing the lyric");
  const auto ids = movableSongLyricsCardIds(nested);
  expect(ids.size() == 3 && std::all_of(ids.begin(), ids.end(), [&](const auto &id) { return canMoveSongLyricsCard(nested, id); }) &&
         std::find(ids.begin(), ids.end(), "verse") != ids.end() && std::find(ids.begin(), ids.end(), nestedId) != ids.end(),
         "bulk cached eligibility matches individual card eligibility after moving a legacy nested card out");
  const auto future = songLyricsFromVar(juce::JSON::parse(R"({"schemaVersion":4,"future":true})"));
  expect(future && movableSongLyricsCardIds(*future).empty() && !canMoveSongLyricsCard(*future, "a"),
         "future opaque documents never expose reorder handles");
}

void opaqueFutureLyrics() {
  auto value = juce::JSON::parse(R"({"schemaVersion":4,"offsetEncoding":"future-position","text":"Future lyric","sections":[],"chords":[{"position":{"token":2},"symbol":"G"}],"unknown":{"nested":[1,true,"retained"]}})");
  const auto original = juce::JSON::toString(value, true);
  auto future = songLyricsFromVar(value);
  expect(future && hasSongLyrics(future) && isValidSongLyricsDocument(*future) && !isEditableSongLyricsDocument(*future),
         "a future lyrics schema is present and persistable but explicitly noneditable");
  expect(juce::JSON::toString(songLyricsToVar(*future), true) == original,
         "future schema payload including unknown fields roundtrips without interpreting it");
  value.getDynamicObject()->setProperty("text", "Changed outside the document");
  expect(juce::JSON::toString(songLyricsToVar(*future), true) == original,
         "opaque document owns a deep copy of the received data");
  auto returned = songLyricsToVar(*future);
  returned.getDynamicObject()->setProperty("text", "Changed outside the serializer");
  expect(juce::JSON::toString(songLyricsToVar(*future), true) == original,
         "returning opaque JSON never exposes mutable storage owned by the document");
  expect(!replaceSongLyricsText(*future, 0, 1, "x") && !setSongLyricsText(*future, "x") &&
         !applySongLyricsSection(*future, 0, 1, "verse") && !clearSongLyricsSections(*future, 0, 1) &&
         !removeSongLyricsSection(*future, "anything") && !setSongLyricsChord(*future, 0, "C") &&
         !removeSongLyricsChord(*future, "anything") &&
         !addSongLyricsInstrumentalBlock(*future, 0, "intro") &&
         !addSongLyricsInstrumentalRow(*future, "anything") &&
         !setSongLyricsInstrumentalCell(*future, "b", "r", "c", "G") &&
         !clearSongLyricsInstrumentalCell(*future, "b", "r", "c") &&
         !removeSongLyricsInstrumentalBlock(*future, "anything") &&
         juce::JSON::toString(songLyricsToVar(*future), true) == original,
         "every public editing helper refuses future data without modifying the raw payload");
  normalizeSongLyricsSchema(*future);
  expect(future->schemaVersion == 4 && juce::JSON::toString(songLyricsToVar(*future), true) == original,
         "minimum-schema normalization never downgrades an opaque future document");
  const auto unknownWithoutText = songLyricsFromVar(juce::JSON::parse(R"({"schemaVersion":5,"futureBlocks":[]})"));
  expect(unknownWithoutText && hasSongLyrics(unknownWithoutText) && isValidSongLyricsDocument(*unknownWithoutText),
         "unknown optional content is never erased merely because its future schema has no current text field");
  expect(!songLyricsFromVar(juce::JSON::parse(R"({"schemaVersion":2,"futureBlocks":[]})")),
         "malformed known versions do not bypass validation through the opaque future branch");
  auto huge = juce::JSON::parse(R"({"schemaVersion":4})");
  huge.getDynamicObject()->setProperty("futureText", juce::String::repeatedString("x", 2 * 1024 * 1024));
  expect(!songLyricsFromVar(huge), "opaque future payload size is bounded rather than retained without a limit");
}

void assignment() {
  auto document = withText("abcdefghij");
  expect(applySongLyricsSection(document, 0, 10, "verse"), "assign whole text");
  const auto originalId = document.sections.front().id;
  const auto stable = json(document);
  expect(applySongLyricsSection(document, 0, 10, "verse") && json(document) == stable,
         "reapplying the identical label does not regenerate an identity");
  expect(applySongLyricsSection(document, 3, 4, "chorus") && document.sections.size() == 3,
         "overlapping assignment splits only the intersected region");
  expect(document.sections[0].start == 0 && document.sections[0].length == 3 && document.sections[0].id == originalId &&
             document.sections[1].start == 3 && document.sections[1].length == 4 && document.sections[1].kind == "chorus" &&
             document.sections[2].start == 7 && document.sections[2].length == 3 && document.sections[2].id != originalId,
         "unaffected left and right text remains labelled with independent IDs");
  const auto chorusId = document.sections[1].id;
  expect(clearSongLyricsSections(document, 4, 2) && document.sections.size() == 4 &&
             document.sections[1].id == chorusId && document.sections[1].length == 1 &&
             document.sections[2].start == 6 && document.sections[2].length == 1,
         "clearing a selection retains both surviving parts of its label");
  expect(document.text == "abcdefghij" && isValidSongLyricsDocument(document), "labelling never changes lyric text");
  expect(removeSongLyricsSection(document, chorusId) && !removeSongLyricsSection(document, chorusId),
         "removing one stable label does not remove adjacent labels");
  const auto beforeFailure = json(document);
  expect(!applySongLyricsSection(document, 0, 0, "verse") &&
             !clearSongLyricsSections(document, 2, 100) &&
             !applySongLyricsSection(document, 1, 1, "unknown") && json(document) == beforeFailure,
         "invalid selection actions are transactional");
  expect(clearSongLyricsSections(document, 0, 10) && document.sections.empty() && document.text == "abcdefghij",
         "clearing every label preserves the original text");
  expect(applySongLyricsSection(document, 0, 4, "verse") &&
             applySongLyricsSection(document, 6, 4, "chorus"), "assign separated neighbouring regions");
  const auto firstId = document.sections[0].id, lastId = document.sections[1].id;
  expect(applySongLyricsSection(document, 2, 6, "bridge") && document.sections.size() == 3 &&
             document.sections[0].id == firstId && document.sections[0].length == 2 &&
             document.sections[2].id == lastId && document.sections[2].start == 8,
         "a selection crossing several regions preserves all text outside the selection");
}

void edits() {
  auto document = withText("abcdefghij");
  expect(applySongLyricsSection(document, 0, 10, "verse"), "prepare labelled text for typing");
  const auto id = document.sections.front().id;
  expect(replaceSongLyricsText(document, 2, 3, "Z") && document.text == "abZfghij" &&
             document.sections.front().id == id && document.sections.front().length == 8,
         "replacing text within a region retains its label and adjusts its length");
  expect(replaceSongLyricsText(document, 0, 0, "X") && document.sections.front().start == 1 &&
             document.sections.front().length == 8,
         "insertion at the left boundary stays outside the old label");
  expect(replaceSongLyricsText(document, 9, 0, "Y") && document.sections.front().start == 1 &&
             document.sections.front().length == 8,
         "insertion at the right boundary stays outside the old label");
  expect(replaceSongLyricsText(document, 4, 0, "\n") && document.sections.front().length == 9 &&
             document.sections.front().id == id,
         "adding a line inside a label keeps its identity and range");
  const auto snapshot = json(document);
  expect(!replaceSongLyricsText(document, 100, 0, "x") && json(document) == snapshot,
         "invalid edits do not mutate the draft");
  auto crossed = withText("abcdefghij");
  applySongLyricsSection(crossed, 0, 4, "verse");
  applySongLyricsSection(crossed, 6, 4, "chorus");
  const auto leftId = crossed.sections.front().id, rightId = crossed.sections.back().id;
  expect(replaceSongLyricsText(crossed, 2, 6, "XYZ") && crossed.text == "abXYZij" && crossed.sections.size() == 2 &&
             crossed.sections[0].id == leftId && crossed.sections[0].length == 2 &&
             crossed.sections[1].id == rightId && crossed.sections[1].start == 5 && crossed.sections[1].length == 2,
         "replacing across labels preserves the surviving fragments without assigning an arbitrary label to new text");
  expect(isValidSongLyricsDocument(crossed), "editing several regions never creates overlaps");
  auto full = withText("abc");
  applySongLyricsSection(full, 0, 3, "intro");
  const auto fullId = full.sections.front().id;
  expect(replaceSongLyricsText(full, 0, 3, "nuevo") && full.sections.front().id == fullId && full.sections.front().length == 5,
         "replacing exactly one labelled range retains its label");
  expect(replaceSongLyricsText(full, 0, 5, "") && full.text.isEmpty() && full.sections.empty(),
         "deleting all labelled text removes empty labels");
}

void nativeMobileBlocks() {
  namespace native = NativeSongLyricsEditing;
  const auto lf = native::enclosingParagraphs("One\nTwo\nThree", 1, 3);
  expect(lf.start == 0 && lf.length == 3,
         "selection ending at next paragraph start does not label that paragraph");
  const auto multiple = native::enclosingParagraphs("One\nTwo\nThree", 2, 3);
  expect(multiple.start == 0 && multiple.length == 7,
         "selection touching two paragraphs labels complete contents without final separator");
  const auto crlf = native::enclosingParagraphs("One\r\nTwo\r\nThree", 4, 2);
  expect(crlf.start == 0 && crlf.length == 8,
         "CRLF is one paragraph separator even when selection starts on LF");
  const auto blank = native::enclosingParagraphs("One\n\nThree", 4, 1);
  expect(!blank.valid(), "a wholly empty paragraph cannot create an empty section");
  expect(native::isCompleteParagraphs("One\nTwo", 0, 3) &&
         native::isCompleteParagraphs("One\nTwo", 0, 4) &&
         !native::isCompleteParagraphs("One\nTwo", 1, 2),
         "whole blocks accept with or without final separator while partial fragments remain partial");
  auto end = withText("Chorus");
  end.sections.push_back({"same-id", "chorus", 0, 6});
  expect(native::replaceText(end, 6, 0, "\n") && end.text == "Chorus\n" &&
         end.sections[0].id == "same-id" && end.sections[0].length == 7,
         "Enter at EOF belongs to the current whole-paragraph card");
  expect(native::replaceText(end, 7, 0, "\n  ") && end.sections[0].length == 10 &&
         native::replaceText(end, 10, 0, juce::String::fromUTF8(u8"🎹")) && end.sections[0].length == 12,
         "consecutive empty lines and spaces retain card ownership before Unicode continuation");
  const auto encoded = json(end);
  expect(!native::replaceText(end, 11, 0, "x") && json(end) == encoded,
         "native block insertion cannot split a UTF16 surrogate pair");
  auto middle = withText("First\n\nSecond");
  middle.sections = {{"a", "verse", 0, 5}, {"b", "chorus", 7, 6}};
  expect(native::replaceText(middle, 5, 0, "\n") && middle.sections[0].length == 6 &&
         middle.sections[1].start == 8 && middle.sections[1].length == 6 &&
         middle.sections[1].id == "b" && middle.text == "First\n\n\nSecond",
         "exact LF insertion before equal separators retains owner and shifts following card by one");
  expect(native::replaceText(middle, 6, 0, "\n") && middle.sections[0].length == 7 &&
         middle.sections[1].start == 9 && isValidSongLyricsDocument(middle),
         "repeated Enter on an intermediate empty line never absorbs or overlaps following card");
  auto shared = withText("One\nTwo");
  shared.sections = {{"left", "verse", 0, 4}, {"right", "chorus", 4, 3}};
  expect(native::replaceText(shared, 4, 0, "X") && shared.sections[0].length == 4 &&
         shared.sections[1].start == 4 && shared.sections[1].length == 4 &&
         shared.sections[1].id == "right" && isValidSongLyricsDocument(shared),
         "following card exclusively owns insertion at shared border");
  auto partial = withText("Before fragment after");
  partial.sections = {{"part", "verse", 7, 8}};
  expect(native::replaceText(partial, 15, 0, "!") && partial.sections[0].length == 8 &&
         partial.sections[0].id == "part", "legacy partial range keeps exact-border insertion outside");
  auto unicode = withText(juce::String::fromUTF8(u8"🎹 One\nNext"));
  const auto wholeUnicode = native::enclosingParagraphs(unicode.text, 2, 1);
  expect(wholeUnicode.start == 0 && wholeUnicode.length == 6 &&
         !native::enclosingParagraphs(unicode.text, 1, 1).valid(),
         "paragraph expansion counts UTF16 units and rejects selected half emoji");
  unicode.sections = {{"unicode", "verse", 0, 6}};
  expect(native::setText(unicode, juce::String::fromUTF8(u8"🎸 One\nNext")) &&
         unicode.sections[0].id == "unicode" && unicode.sections[0].length == 6,
         "full-text IME fallback expands changed surrogate pair and retains section identity");
  auto invalid = withText("One");
  invalid.sections = {{"bad", "verse", 0, 100}};
  const auto bad = json(invalid);
  expect(!native::replaceText(invalid, 0, 0, "x") && !native::setText(invalid, "One") &&
         json(invalid) == bad, "native block helper rejects invalid source documents transactionally");
  const auto stable = json(shared);
  expect(!native::replaceText(shared, 99, 0, "x") && json(shared) == stable,
         "out-of-bounds exact edit leaves native document unchanged");
}

void nativeMobileCompletionClassification() {
  namespace native = NativeSongLyricsEditing;
  const juce::StringArray fixtures{
      "A\nB\n", "\n\n", "\r\n\r\n", "One\r\nTwo\rThree\n",
      juce::String::fromUTF8(u8"🎹 One\nNext"),
      juce::String::fromUTF8(u8"A\u0085B\u2028C\u2029D")};
  bool matches = true;
  for (const auto &text : fixtures) {
    const int size = songLyricsUtf16Length(text);
    SongLyricsDocument ranges{1, text, {}};
    for (int start = 0; start < size; ++start) {
      for (int length = 1; length <= size - start; ++length) {
        const auto content = native::enclosingParagraphs(text, start, length, true);
        const auto all = native::enclosingParagraphs(text, start, length, false);
        const bool reference = content.valid() && start == content.start &&
            (start + length == content.start + content.length ||
             (all.valid() && start + length == all.start + all.length));
        matches = matches && native::isCompleteParagraphs(text, start, length) == reference;
        ranges.sections.push_back({juce::String(ranges.sections.size()), "verse", start, length});
      }
    }
    const auto flags = native::completeParagraphsForSections(ranges);
    matches = matches && flags.size() == ranges.sections.size();
    for (size_t i = 0; i < flags.size(); ++i)
      matches = matches && flags[i] == native::isCompleteParagraphs(text, ranges.sections[i].start,
                                                                  ranges.sections[i].length);
  }
  expect(matches, "constant-time and batched card classification matches paragraph reference for every mixed-newline and Unicode range");
  auto maximum = withText(juce::String::repeatedString(juce::String::repeatedString("x", 62) + "\r\n", 4096));
  for (int i = 0; i < 4096; ++i)
    maximum.sections.push_back({"block-" + juce::String(i), "verse", i * 64, i % 2 == 0 ? 62 : 64});
  expect(songLyricsUtf16Length(maximum.text) == kSongLyricsMaxUtf16Length &&
         isValidSongLyricsDocument(maximum), "pathological classification fixture reaches both document and section budgets");
  const auto flags = native::completeParagraphsForSections(maximum);
  expect(flags.size() == 4096 && std::all_of(flags.begin(), flags.end(), [](bool value) { return value; }),
         "single document scan classifies all 4096 full-paragraph cards with and without CRLF terminators");
}

void unicodeAndLimits() {
  auto emoji = withText(juce::String::fromUTF8(u8"A🎹B"));
  expect(songLyricsUtf16Length(emoji.text) == 4 && !applySongLyricsSection(emoji, 1, 1, "verse") &&
             !applySongLyricsSection(emoji, 2, 1, "verse") && applySongLyricsSection(emoji, 1, 2, "verse"),
         "native selection ranges cannot split a supplementary Unicode character");
  const auto stable = json(emoji), id = emoji.sections.front().id;
  expect(!replaceSongLyricsText(emoji, 2, 0, "X") && json(emoji) == stable,
         "text replacement cannot insert inside a surrogate pair");
  expect(setSongLyricsText(emoji, juce::String::fromUTF8(u8"A🎸B")) && emoji.sections.front().id == id &&
             emoji.sections.front().start == 1 && emoji.sections.front().length == 2,
         "full-text bridge diff expands its edit to Unicode scalar boundaries");
  expect(setSongLyricsText(emoji, juce::String::fromUTF8(u8"Antes A🎸B")) && emoji.sections.front().start == 7,
         "full-text bridge diff shifts labels using UTF16 units");
  const auto noOp = json(emoji);
  expect(setSongLyricsText(emoji, emoji.text) && json(emoji) == noOp, "native text no-op preserves every identity");
  auto maximum = withText(juce::String::repeatedString(juce::String::fromUTF8(u8"🎹"), 131072));
  expect(isValidSongLyricsDocument(maximum), "exact UTF16 text budget is accepted with supplementary characters");
  const auto original = json(maximum);
  expect(!replaceSongLyricsText(maximum, 0, 0, "x") && json(maximum) == original,
         "text budget overflow is rejected without truncating lyrics");
  maximum.text += "x";
  expect(!isValidSongLyricsDocument(maximum) && !songLyricsFromVar(songLyricsToVar(maximum)),
         "oversized native and JSON text share the same UTF16 budget");
  auto many = withText(juce::String::repeatedString("x", 16384));
  for (int i = 0; i < kSongLyricsMaxSections; ++i)
    many.sections.push_back({"section-" + juce::String(i), "verse", i * 4, 3});
  expect(isValidSongLyricsDocument(many), "maximum section count validates");
  const auto manyBefore = json(many);
  expect(!clearSongLyricsSections(many, 1, 1) && !applySongLyricsSection(many, 1, 1, "chorus") && json(many) == manyBefore,
         "splitting at the section budget is atomic rather than dropping an existing label");
}
} // namespace

int main() {
  try { codec(); chordCodec(); chordEdits(); chordUnicodeAndLimits(); instrumentalCodec(); instrumentalEdits(); instrumentalLimits(); cardReordering(); opaqueFutureLyrics(); assignment(); edits(); nativeMobileBlocks(); nativeMobileCompletionClassification(); unicodeAndLimits(); }
  catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
  std::cout << "Song lyrics: " << checks << " checks passed\n";
}
