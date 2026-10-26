#include "data/SongLyrics.h"

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
  try { codec(); assignment(); edits(); unicodeAndLimits(); }
  catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
  std::cout << "Song lyrics: " << checks << " checks passed\n";
}
