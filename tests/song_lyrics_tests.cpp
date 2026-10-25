#include "data/SongLyrics.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void expect(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

juce::String json(const SongLyricsDocument &document) {
  return juce::JSON::toString(songLyricsToVar(document), true);
}

void codec() {
  auto document = lyricsFromPlainText(juce::String::fromUTF8(u8"Aquí 🎹 va la canción\r\nOtra línea\rÚltima\n"));
  document.blocks.front().label = "Verso 1";
  auto &line = document.blocks.front().lines.front();
  line.chords = {{"chord-end", line.text.length(), "C/E"}, {"chord-start", 0, "G"}, {"chord-emoji", 5, "Am7"}};
  auto chorus = lyricsFromPlainText("[Texto pegado sin interpretar]").blocks.front();
  chorus.label = "Coro";
  document.blocks.push_back(chorus);
  expect(isValidSongLyricsDocument(document), "native validation accepts the same structured document");
  expect(hasSongLyrics(document), "text and chords make a meaningful document");
  expect(document.blocks.front().lines.size() == 4 && document.blocks.front().lines.back().text.isEmpty(),
         "paste normalizes all newline variants and preserves a trailing blank line");
  expect(document.blocks.front().lines.front().text.length() == 20,
         "accent and supplementary Unicode codepoint use JUCE character indices");
  const auto encoded = juce::JSON::parse(json(document));
  const auto decoded = songLyricsFromVar(encoded);
  expect(decoded.has_value(), "structured lyrics decode");
  expect(json(*decoded) == json(document), "JSON canonical roundtrip retains IDs, labels, text and chords");
  expect(decoded->blocks.front().lines.front().chords.front().position == 0 &&
             decoded->blocks.front().lines.front().chords[1].position == 5,
         "chords serialize in stable position order without altering anchors");
  expect(lyricsBlockText(decoded->blocks.back()) == "[Texto pegado sin interpretar]",
         "ordinary pasted brackets never become markup or lose text");
  expect(!hasSongLyrics(std::nullopt) && !hasSongLyrics(lyricsFromPlainText(" \n\t")),
         "optional and whitespace-only text do not block song creation");
  auto chordOnly = lyricsFromPlainText("");
  chordOnly.blocks.front().lines.front().chords.push_back({"instrumental", 0, "D"});
  expect(hasSongLyrics(chordOnly) && songLyricsFromVar(songLyricsToVar(chordOnly)).has_value(),
         "instrumental chord-only lines remain meaningful");

  const auto invalid = [&](const std::function<void(juce::var&)> &mutate) {
    auto value = juce::JSON::parse(json(document));
    mutate(value);
    expect(!songLyricsFromVar(value).has_value(), "malformed lyric data rejects the entire document");
  };
  invalid([](auto &v) { v.getDynamicObject()->setProperty("schemaVersion", 2); });
  invalid([](auto &v) { v.getDynamicObject()->setProperty("schemaVersion", true); });
  invalid([](auto &v) { v.getDynamicObject()->setProperty("blocks", "not an array"); });
  const auto chord = [](juce::var &v) -> juce::DynamicObject* {
    return v.getProperty("blocks", {}).getArray()->getReference(0)
        .getProperty("lines", {}).getArray()->getReference(0)
        .getProperty("chords", {}).getArray()->getReference(0).getDynamicObject();
  };
  invalid([&](auto &v) { chord(v)->setProperty("position", -1); });
  invalid([&](auto &v) { chord(v)->setProperty("position", 500); });
  invalid([&](auto &v) { chord(v)->setProperty("position", juce::int64(0x7fffffffffffffffLL)); });
  invalid([&](auto &v) { chord(v)->setProperty("position", 0.5); });
  invalid([&](auto &v) { chord(v)->setProperty("position", true); });
  invalid([&](auto &v) { chord(v)->setProperty("symbol", " \t"); });
  invalid([&](auto &v) { chord(v)->setProperty("symbol", "C\nD"); });
  invalid([&](auto &v) { chord(v)->setProperty("id", document.blocks.front().id); });
  invalid([](auto &v) {
    v.getProperty("blocks", {}).getArray()->getReference(0).getDynamicObject()->setProperty("lines", juce::Array<juce::var>{});
  });
  auto huge = lyricsFromPlainText(juce::String::repeatedString("a", 262145));
  expect(!songLyricsFromVar(songLyricsToVar(huge)), "text budget rejects oversized documents");
}

void edits() {
  auto block = lyricsFromPlainText("Hola mundo").blocks.front();
  const auto blockId = block.id;
  const auto lineId = block.lines.front().id;
  block.lines.front().chords = {{"g", 0, "G"}, {"d", 5, "D"}, {"end", 10, "C"}};
  replaceLyricsBlockText(block, "Mi Hola mundo");
  expect(block.id == blockId && block.lines.front().id == lineId &&
             block.lines.front().chords[0].position == 3 && block.lines.front().chords[1].position == 8,
         "inserting before lyrics preserves IDs and follows the original words");
  replaceLyricsBlockText(block, "Hola mundo");
  expect(block.lines.front().chords[1].position == 5, "deleting prefix restores the original anchors");
  replaceLyricsBlockText(block, "Hola \nmundo");
  expect(block.lines.size() == 2 && block.lines[0].id == lineId &&
             block.lines[1].chords[0].id == "d" && block.lines[1].chords[0].position == 0 &&
             block.lines[1].chords[1].id == "end" && block.lines[1].chords[1].position == 5,
         "split moves annotated words into the new line without losing chords");
  const auto splitId = block.lines[1].id;
  replaceLyricsBlockText(block, "Hola \nmundo!");
  expect(block.lines[1].id == splitId && block.lines[1].chords[1].position == 6,
         "line identity is stable while typing and end chord follows inserted text");
  replaceLyricsBlockText(block, "Hola mundo!");
  expect(block.lines.size() == 1 && block.lines[0].id == lineId && block.lines[0].chords.size() == 3 &&
             block.lines[0].chords[1].position == 5 && block.lines[0].chords[2].position == 11,
         "merging lines retains every chord identity and absolute musical position");
  replaceLyricsBlockText(block, "Hola !");
  expect(block.lines[0].chords[1].id == "d" && block.lines[0].chords[1].position == 5,
         "removing annotated words preserves their chord at the deletion boundary");
  replaceLyricsBlockText(block, "");
  expect(block.lines.size() == 1 && block.lines.front().chords.size() == 3 &&
             std::all_of(block.lines.front().chords.begin(), block.lines.front().chords.end(),
                         [](const auto &c) { return c.position == 0; }),
         "clearing lyrics does not silently destroy musician-entered chords");
  auto unicode = lyricsFromPlainText(juce::String::fromUTF8(u8"Al 🎹 cantar")).blocks.front();
  unicode.lines.front().chords.push_back({"unicode", 5, "Em"});
  replaceLyricsBlockText(unicode, juce::String::fromUTF8(u8"Al 🎹\ncantar"));
  expect(unicode.lines[1].chords.front().position == 0 && unicode.lines[1].text == "cantar",
         "supplementary Unicode remains aligned when splitting a line");
  const auto stable = json({1, {unicode}});
  replaceLyricsBlockText(unicode, lyricsBlockText(unicode));
  expect(json({1, {unicode}}) == stable, "no-op editing preserves all identities");
  auto multiline = lyricsFromPlainText("Primera\nSegunda").blocks.front();
  const auto first = multiline.lines[0].id, second = multiline.lines[1].id;
  replaceLyricsBlockText(multiline, "Nueva\nPrimera\nSegunda");
  expect(multiline.lines[1].id == first && multiline.lines[2].id == second,
         "prepending complete lines retains the identities of existing lines");
  auto longBlock = lyricsFromPlainText(juce::String::repeatedString(juce::String::fromUTF8(u8"á🎹 "), 20000)).blocks.front();
  longBlock.lines.front().chords.push_back({"long-tail", 59999, "F#"});
  replaceLyricsBlockText(longBlock, "Inicio " + lyricsBlockText(longBlock));
  expect(longBlock.lines.front().chords.front().position == 60006,
         "long Unicode lyrics rebase anchors with one linear character scan");
}
} // namespace

int main() {
  try { codec(); edits(); }
  catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
  std::cout << "Song lyrics: " << checks << " checks passed\n";
}
