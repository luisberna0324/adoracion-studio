#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

// Lyric positions are Unicode character indices, matching juce::String and
// TextEditor, never UTF-8 bytes or UTF-16 code units. End-of-line is permitted.
struct SongLyricsChord {
  juce::String id;
  int position = 0;
  juce::String symbol;
};

struct SongLyricsLine {
  juce::String id;
  juce::String text;
  std::vector<SongLyricsChord> chords;
};

struct SongLyricsBlock {
  juce::String id;
  juce::String label;
  std::vector<SongLyricsLine> lines;
};

struct SongLyricsDocument {
  int schemaVersion = 1;
  std::vector<SongLyricsBlock> blocks;
};

// Invalid/unsupported JSON is rejected as a whole, without partially applying
// chords. An absent document remains compatible with older songs/projects.
std::optional<SongLyricsDocument> songLyricsFromVar(const juce::var &value);
juce::var songLyricsToVar(const SongLyricsDocument &document);
// Allocation-light native validation for persistence, before JSON encoding.
bool isValidSongLyricsDocument(const SongLyricsDocument &document);
bool hasSongLyrics(const std::optional<SongLyricsDocument> &document);
SongLyricsDocument lyricsFromPlainText(const juce::String &text);
juce::String lyricsBlockText(const SongLyricsBlock &block);

// Applies the editor's contiguous text replacement to a block. Splitting and
// joining lines remaps chord anchors and preserves chord IDs. Existing line IDs
// follow their original text; newly created lines receive new IDs. Chords in a
// removed range remain at its start until explicitly removed by the musician.
void replaceLyricsBlockText(SongLyricsBlock &block, const juce::String &text);
