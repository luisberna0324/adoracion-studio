#pragma once

#include "SongLyrics.h"
#include <optional>

// Local interchange only. Importing this metadata never changes SongData,
// playback tempo, key, audio, FreeShow, or arrangement state.
namespace ChordProCodec {
struct Metadata {
  juce::String title;
  juce::String artist;
  juce::String key;
  std::optional<double> tempoBpm;
  juce::String subtitle;
  juce::String timeSignature;
};

struct ImportResult {
  std::optional<SongLyricsDocument> document;
  Metadata metadata;
  juce::String error;
  juce::StringArray warnings;
  explicit operator bool() const noexcept { return document.has_value(); }
};

// UTF-8/Unicode text supplied by the caller; normalizes CRLF/CR to LF. Supports
// chord anchors (including inside words), title/artist/key/tempo/subtitle/time,
// the eight native section environments and common aliases. Grids support
// labels, an optional shape width (N or measures x beats), chord/empty cells
// and plain bar partitions; margins, repeats, strums and special cells fail.
// One grid may occur inside a lyric section; grids never nest within grids
// and never add placeholder characters or change lyric-section ranges.
// Unsupported directives/markup fail before any existing document is replaced.
ImportResult importText(const juce::String &source);

// Writes ordinary ChordPro (.cho). Native sections must start at a line start
// and end at a line's content boundary. A terminal LF may only be included when
// an actual empty lyric line ends there; otherwise export fails without dropping
// the labelled code unit.
// Instrumental blocks are interleaved at paragraph/EOF anchors in order. Grid
// rows must have equal widths and labels must encode their native kind.
// Empty/future documents and unrepresentable partial-line sections fail with
// an empty string and an explanation. No input document is modified.
juce::String exportText(const SongLyricsDocument &document,
                         const Metadata &metadata = {},
                         juce::String *error = nullptr);
} // namespace ChordProCodec
