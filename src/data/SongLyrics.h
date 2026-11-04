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

struct SongLyricsChord {
  juce::String id;
  int offset = 0; // UTF-16 insertion position, including EOF for instrumental chords.
  juce::String symbol;
};

// Instrumental cells have their own identity and position. They never consume
// lyric characters or rely on spaces/pixels to align the symbols.
struct SongLyricsInstrumentalCell {
  juce::String id;
  std::optional<juce::String> symbol;
};
struct SongLyricsInstrumentalRow {
  juce::String id;
  std::vector<SongLyricsInstrumentalCell> cells;
  // Empty means one bar containing the entire row. Otherwise positive lengths
  // partition every cell, preserving ChordPro's internal bar boundaries.
  std::vector<int> barLengths;
};
struct SongLyricsInstrumentalBlock {
  juce::String id;
  juce::String kind;
  juce::String label; // Optional display label, for example kind=instrumental/Solo.
  int anchorUtf16 = 0;
  int order = 0; // Unique among blocks at the same text anchor.
  std::vector<SongLyricsInstrumentalRow> rows;
};

struct SongLyricsDocument {
  int schemaVersion = 1;
  juce::String text;
  std::vector<SongLyricsSection> sections;
  std::vector<SongLyricsChord> chords;
  // Future versions remain opaque and round-trip unchanged through metadata
  // saves. Editing helpers and native editors must refuse these documents.
  juce::var preservedOpaqueDocument;
  std::vector<SongLyricsInstrumentalBlock> instrumentalBlocks;
};

inline constexpr int kSongLyricsMaxUtf16Length = 262144;
inline constexpr int kSongLyricsMaxSections = 4096;
inline constexpr int kSongLyricsMaxChords = 4096;
inline constexpr int kSongLyricsMaxChordSymbolLength = 32;
inline constexpr int kSongLyricsMaxInstrumentalBlocks = 128;
inline constexpr int kSongLyricsMaxInstrumentalRowsPerBlock = 128;
inline constexpr int kSongLyricsMaxInstrumentalCellsPerRow = 32;
inline constexpr int kSongLyricsMaxInstrumentalCells = 4096;
inline constexpr int kSongLyricsMaxInstrumentalLabelLength = 80;
inline constexpr std::array<const char *, 8> songLyricsSectionKinds() {
  return {"intro", "verse", "prechorus", "chorus", "bridge", "instrumental", "outro", "ending"};
}

// Returns -1 for invalid Unicode or an integer overflow. A section boundary must
// never split the two UTF-16 code units of one supplementary Unicode character.
int songLyricsUtf16Length(const juce::String &text);
bool isValidSongLyricsDocument(const SongLyricsDocument &document);
bool isEditableSongLyricsDocument(const SongLyricsDocument &document);
bool isSongLyricsChordBoundary(const juce::String &text, int offset);
bool isValidSongLyricsChordSymbol(const juce::String &symbol);
// Exact positions are unique; updating a symbol at the same position retains ID.
// Chords require schema 2; instrumental blocks require schema 3.
bool setSongLyricsChord(SongLyricsDocument &document, int offset, const juce::String &symbol);
bool removeSongLyricsChord(SongLyricsDocument &document, const juce::String &id);
// Recomputes the minimum current schema without modifying opaque future data.
void normalizeSongLyricsSchema(SongLyricsDocument &document);
// Blocks are inserted between native paragraphs, or at EOF. No placeholder
// character is added. A new block starts with one real row of empty cells.
bool isSongLyricsInstrumentalAnchor(const juce::String &text, int anchorUtf16);
bool addSongLyricsInstrumentalBlock(SongLyricsDocument &document, int anchorUtf16,
    const juce::String &kind, const juce::String &label = {}, int cellCount = 4,
    juce::String *outBlockId = nullptr);
bool removeSongLyricsInstrumentalBlock(SongLyricsDocument &document, const juce::String &blockId);
bool addSongLyricsInstrumentalRow(SongLyricsDocument &document, const juce::String &blockId,
    int cellCount = 4, juce::String *outRowId = nullptr);
bool setSongLyricsInstrumentalCell(SongLyricsDocument &document, const juce::String &blockId,
    const juce::String &rowId, const juce::String &cellId, const juce::String &symbol);
bool clearSongLyricsInstrumentalCell(SongLyricsDocument &document, const juce::String &blockId,
    const juce::String &rowId, const juce::String &cellId);
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

// Reorders existing cards, never their playback/arrangement sections. Lyric
// sources must cover complete paragraphs and contain no instrumental anchor
// strictly inside them. Instrumental sources move only their anchor/order.
// A destination is a non-nested card; an empty ID means after the final card.
// IDs and symbols remain stable. Reordering text preserves internal separators
// and reuses the existing separators between units (including CRLF and EOF).
// Invalid moves are atomic; moving a valid card before itself is a no-op.
// Bulk eligibility validates/scans the document once; UI may cache per revision.
std::vector<juce::String> movableSongLyricsCardIds(const SongLyricsDocument &document);
bool canMoveSongLyricsCard(const SongLyricsDocument &document, const juce::String &id);
bool moveSongLyricsCardBefore(SongLyricsDocument &document, const juce::String &sourceId,
                             const juce::String &beforeId);

// Native text-change callbacks can pass their exact replacement range. Typing
// inside a label keeps it; insertion at a boundary stays outside that label.
// Replacing across multiple labels retains their surviving text and leaves the
// new text unlabelled. Instrumental blocks survive text deletion; insertion at
// their anchor stays after the block. If a paragraph boundary is removed, its
// block moves to the next paragraph start or EOF, retaining relative order.
// setSongLyricsText derives one replacement for a bridge
// that supplies only its final text; identical text is a no-op preserving IDs.
bool replaceSongLyricsText(SongLyricsDocument &document, int start, int length,
                           const juce::String &replacement);
bool setSongLyricsText(SongLyricsDocument &document, const juce::String &text);
