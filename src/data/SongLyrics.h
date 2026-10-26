#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <optional>
#include <vector>

// UIKit NSRange and Android selection offsets both count UTF-16 code units.
// These ranges belong to lyric text only, never playback/arrangement sections.
struct SongLyricsSection {
  juce::String id;
  juce::String kind;
  int start = 0;
  int length = 0;
};

struct SongLyricsDocument {
  int schemaVersion = 1;
  juce::String text;
  std::vector<SongLyricsSection> sections;
};

inline constexpr int kSongLyricsMaxUtf16Length = 262144;
inline constexpr int kSongLyricsMaxSections = 4096;
inline constexpr std::array<const char *, 8> songLyricsSectionKinds() {
  return {"intro", "verse", "prechorus", "chorus", "bridge", "instrumental", "outro", "ending"};
}

// Returns -1 for invalid Unicode or an integer overflow. A section boundary must
// never split the two UTF-16 code units of one supplementary Unicode character.
int songLyricsUtf16Length(const juce::String &text);
bool isValidSongLyricsDocument(const SongLyricsDocument &document);
bool hasSongLyrics(const std::optional<SongLyricsDocument> &document);
std::optional<SongLyricsDocument> songLyricsFromVar(const juce::var &value);
juce::var songLyricsToVar(const SongLyricsDocument &document);

// Mutations are transactional: invalid input returns false without changing the
// document. Applying/clearing a selection trims or splits intersecting labels,
// retaining every unaffected fragment. The first survivor keeps the old ID.
bool applySongLyricsSection(SongLyricsDocument &document, int start, int length,
                            const juce::String &kind);
bool clearSongLyricsSections(SongLyricsDocument &document, int start, int length);
bool removeSongLyricsSection(SongLyricsDocument &document, const juce::String &id);

// Native text-change callbacks can pass their exact replacement range. Typing
// inside a label keeps it; insertion at a boundary stays outside that label.
// Replacing across multiple labels retains their surviving text and leaves the
// new text unlabelled. setSongLyricsText derives one replacement for a bridge
// that supplies only its final text; identical text is a no-op preserving IDs.
bool replaceSongLyricsText(SongLyricsDocument &document, int start, int length,
                           const juce::String &replacement);
bool setSongLyricsText(SongLyricsDocument &document, const juce::String &text);
