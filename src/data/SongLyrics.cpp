#include "SongLyrics.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace {

struct UnicodeRange { unsigned int first, last; };
// Unicode 13.0.0 Mark categories (Mn/Mc/Me), generated from unicodedata.
// Together with the conservative join rules below these prevent chord anchors
// from splitting accents, variation selectors, emoji and common conjuncts.
constexpr UnicodeRange unicodeMarkRanges[] = {
    {0x300u, 0x36fu}, {0x483u, 0x489u}, {0x591u, 0x5bdu}, {0x5bfu, 0x5bfu}, {0x5c1u, 0x5c2u},
    {0x5c4u, 0x5c5u}, {0x5c7u, 0x5c7u}, {0x610u, 0x61au}, {0x64bu, 0x65fu}, {0x670u, 0x670u},
    {0x6d6u, 0x6dcu}, {0x6dfu, 0x6e4u}, {0x6e7u, 0x6e8u}, {0x6eau, 0x6edu}, {0x711u, 0x711u},
    {0x730u, 0x74au}, {0x7a6u, 0x7b0u}, {0x7ebu, 0x7f3u}, {0x7fdu, 0x7fdu}, {0x816u, 0x819u},
    {0x81bu, 0x823u}, {0x825u, 0x827u}, {0x829u, 0x82du}, {0x859u, 0x85bu}, {0x8d3u, 0x8e1u},
    {0x8e3u, 0x903u}, {0x93au, 0x93cu}, {0x93eu, 0x94fu}, {0x951u, 0x957u}, {0x962u, 0x963u},
    {0x981u, 0x983u}, {0x9bcu, 0x9bcu}, {0x9beu, 0x9c4u}, {0x9c7u, 0x9c8u}, {0x9cbu, 0x9cdu},
    {0x9d7u, 0x9d7u}, {0x9e2u, 0x9e3u}, {0x9feu, 0x9feu}, {0xa01u, 0xa03u}, {0xa3cu, 0xa3cu},
    {0xa3eu, 0xa42u}, {0xa47u, 0xa48u}, {0xa4bu, 0xa4du}, {0xa51u, 0xa51u}, {0xa70u, 0xa71u},
    {0xa75u, 0xa75u}, {0xa81u, 0xa83u}, {0xabcu, 0xabcu}, {0xabeu, 0xac5u}, {0xac7u, 0xac9u},
    {0xacbu, 0xacdu}, {0xae2u, 0xae3u}, {0xafau, 0xaffu}, {0xb01u, 0xb03u}, {0xb3cu, 0xb3cu},
    {0xb3eu, 0xb44u}, {0xb47u, 0xb48u}, {0xb4bu, 0xb4du}, {0xb55u, 0xb57u}, {0xb62u, 0xb63u},
    {0xb82u, 0xb82u}, {0xbbeu, 0xbc2u}, {0xbc6u, 0xbc8u}, {0xbcau, 0xbcdu}, {0xbd7u, 0xbd7u},
    {0xc00u, 0xc04u}, {0xc3eu, 0xc44u}, {0xc46u, 0xc48u}, {0xc4au, 0xc4du}, {0xc55u, 0xc56u},
    {0xc62u, 0xc63u}, {0xc81u, 0xc83u}, {0xcbcu, 0xcbcu}, {0xcbeu, 0xcc4u}, {0xcc6u, 0xcc8u},
    {0xccau, 0xccdu}, {0xcd5u, 0xcd6u}, {0xce2u, 0xce3u}, {0xd00u, 0xd03u}, {0xd3bu, 0xd3cu},
    {0xd3eu, 0xd44u}, {0xd46u, 0xd48u}, {0xd4au, 0xd4du}, {0xd57u, 0xd57u}, {0xd62u, 0xd63u},
    {0xd81u, 0xd83u}, {0xdcau, 0xdcau}, {0xdcfu, 0xdd4u}, {0xdd6u, 0xdd6u}, {0xdd8u, 0xddfu},
    {0xdf2u, 0xdf3u}, {0xe31u, 0xe31u}, {0xe34u, 0xe3au}, {0xe47u, 0xe4eu}, {0xeb1u, 0xeb1u},
    {0xeb4u, 0xebcu}, {0xec8u, 0xecdu}, {0xf18u, 0xf19u}, {0xf35u, 0xf35u}, {0xf37u, 0xf37u},
    {0xf39u, 0xf39u}, {0xf3eu, 0xf3fu}, {0xf71u, 0xf84u}, {0xf86u, 0xf87u}, {0xf8du, 0xf97u},
    {0xf99u, 0xfbcu}, {0xfc6u, 0xfc6u}, {0x102bu, 0x103eu}, {0x1056u, 0x1059u}, {0x105eu, 0x1060u},
    {0x1062u, 0x1064u}, {0x1067u, 0x106du}, {0x1071u, 0x1074u}, {0x1082u, 0x108du}, {0x108fu, 0x108fu},
    {0x109au, 0x109du}, {0x135du, 0x135fu}, {0x1712u, 0x1714u}, {0x1732u, 0x1734u}, {0x1752u, 0x1753u},
    {0x1772u, 0x1773u}, {0x17b4u, 0x17d3u}, {0x17ddu, 0x17ddu}, {0x180bu, 0x180du}, {0x1885u, 0x1886u},
    {0x18a9u, 0x18a9u}, {0x1920u, 0x192bu}, {0x1930u, 0x193bu}, {0x1a17u, 0x1a1bu}, {0x1a55u, 0x1a5eu},
    {0x1a60u, 0x1a7cu}, {0x1a7fu, 0x1a7fu}, {0x1ab0u, 0x1ac0u}, {0x1b00u, 0x1b04u}, {0x1b34u, 0x1b44u},
    {0x1b6bu, 0x1b73u}, {0x1b80u, 0x1b82u}, {0x1ba1u, 0x1badu}, {0x1be6u, 0x1bf3u}, {0x1c24u, 0x1c37u},
    {0x1cd0u, 0x1cd2u}, {0x1cd4u, 0x1ce8u}, {0x1cedu, 0x1cedu}, {0x1cf4u, 0x1cf4u}, {0x1cf7u, 0x1cf9u},
    {0x1dc0u, 0x1df9u}, {0x1dfbu, 0x1dffu}, {0x20d0u, 0x20f0u}, {0x2cefu, 0x2cf1u}, {0x2d7fu, 0x2d7fu},
    {0x2de0u, 0x2dffu}, {0x302au, 0x302fu}, {0x3099u, 0x309au}, {0xa66fu, 0xa672u}, {0xa674u, 0xa67du},
    {0xa69eu, 0xa69fu}, {0xa6f0u, 0xa6f1u}, {0xa802u, 0xa802u}, {0xa806u, 0xa806u}, {0xa80bu, 0xa80bu},
    {0xa823u, 0xa827u}, {0xa82cu, 0xa82cu}, {0xa880u, 0xa881u}, {0xa8b4u, 0xa8c5u}, {0xa8e0u, 0xa8f1u},
    {0xa8ffu, 0xa8ffu}, {0xa926u, 0xa92du}, {0xa947u, 0xa953u}, {0xa980u, 0xa983u}, {0xa9b3u, 0xa9c0u},
    {0xa9e5u, 0xa9e5u}, {0xaa29u, 0xaa36u}, {0xaa43u, 0xaa43u}, {0xaa4cu, 0xaa4du}, {0xaa7bu, 0xaa7du},
    {0xaab0u, 0xaab0u}, {0xaab2u, 0xaab4u}, {0xaab7u, 0xaab8u}, {0xaabeu, 0xaabfu}, {0xaac1u, 0xaac1u},
    {0xaaebu, 0xaaefu}, {0xaaf5u, 0xaaf6u}, {0xabe3u, 0xabeau}, {0xabecu, 0xabedu}, {0xfb1eu, 0xfb1eu},
    {0xfe00u, 0xfe0fu}, {0xfe20u, 0xfe2fu}, {0x101fdu, 0x101fdu}, {0x102e0u, 0x102e0u}, {0x10376u, 0x1037au},
    {0x10a01u, 0x10a03u}, {0x10a05u, 0x10a06u}, {0x10a0cu, 0x10a0fu}, {0x10a38u, 0x10a3au}, {0x10a3fu, 0x10a3fu},
    {0x10ae5u, 0x10ae6u}, {0x10d24u, 0x10d27u}, {0x10eabu, 0x10eacu}, {0x10f46u, 0x10f50u}, {0x11000u, 0x11002u},
    {0x11038u, 0x11046u}, {0x1107fu, 0x11082u}, {0x110b0u, 0x110bau}, {0x11100u, 0x11102u}, {0x11127u, 0x11134u},
    {0x11145u, 0x11146u}, {0x11173u, 0x11173u}, {0x11180u, 0x11182u}, {0x111b3u, 0x111c0u}, {0x111c9u, 0x111ccu},
    {0x111ceu, 0x111cfu}, {0x1122cu, 0x11237u}, {0x1123eu, 0x1123eu}, {0x112dfu, 0x112eau}, {0x11300u, 0x11303u},
    {0x1133bu, 0x1133cu}, {0x1133eu, 0x11344u}, {0x11347u, 0x11348u}, {0x1134bu, 0x1134du}, {0x11357u, 0x11357u},
    {0x11362u, 0x11363u}, {0x11366u, 0x1136cu}, {0x11370u, 0x11374u}, {0x11435u, 0x11446u}, {0x1145eu, 0x1145eu},
    {0x114b0u, 0x114c3u}, {0x115afu, 0x115b5u}, {0x115b8u, 0x115c0u}, {0x115dcu, 0x115ddu}, {0x11630u, 0x11640u},
    {0x116abu, 0x116b7u}, {0x1171du, 0x1172bu}, {0x1182cu, 0x1183au}, {0x11930u, 0x11935u}, {0x11937u, 0x11938u},
    {0x1193bu, 0x1193eu}, {0x11940u, 0x11940u}, {0x11942u, 0x11943u}, {0x119d1u, 0x119d7u}, {0x119dau, 0x119e0u},
    {0x119e4u, 0x119e4u}, {0x11a01u, 0x11a0au}, {0x11a33u, 0x11a39u}, {0x11a3bu, 0x11a3eu}, {0x11a47u, 0x11a47u},
    {0x11a51u, 0x11a5bu}, {0x11a8au, 0x11a99u}, {0x11c2fu, 0x11c36u}, {0x11c38u, 0x11c3fu}, {0x11c92u, 0x11ca7u},
    {0x11ca9u, 0x11cb6u}, {0x11d31u, 0x11d36u}, {0x11d3au, 0x11d3au}, {0x11d3cu, 0x11d3du}, {0x11d3fu, 0x11d45u},
    {0x11d47u, 0x11d47u}, {0x11d8au, 0x11d8eu}, {0x11d90u, 0x11d91u}, {0x11d93u, 0x11d97u}, {0x11ef3u, 0x11ef6u},
    {0x16af0u, 0x16af4u}, {0x16b30u, 0x16b36u}, {0x16f4fu, 0x16f4fu}, {0x16f51u, 0x16f87u}, {0x16f8fu, 0x16f92u},
    {0x16fe4u, 0x16fe4u}, {0x16ff0u, 0x16ff1u}, {0x1bc9du, 0x1bc9eu}, {0x1d165u, 0x1d169u}, {0x1d16du, 0x1d172u},
    {0x1d17bu, 0x1d182u}, {0x1d185u, 0x1d18bu}, {0x1d1aau, 0x1d1adu}, {0x1d242u, 0x1d244u}, {0x1da00u, 0x1da36u},
    {0x1da3bu, 0x1da6cu}, {0x1da75u, 0x1da75u}, {0x1da84u, 0x1da84u}, {0x1da9bu, 0x1da9fu}, {0x1daa1u, 0x1daafu},
    {0x1e000u, 0x1e006u}, {0x1e008u, 0x1e018u}, {0x1e01bu, 0x1e021u}, {0x1e023u, 0x1e024u}, {0x1e026u, 0x1e02au},
    {0x1e130u, 0x1e136u}, {0x1e2ecu, 0x1e2efu}, {0x1e8d0u, 0x1e8d6u}, {0x1e944u, 0x1e94au}, {0xe0100u, 0xe01efu},
};
bool unicodeMark(unsigned int character) {
  const auto found = std::lower_bound(std::begin(unicodeMarkRanges), std::end(unicodeMarkRanges), character,
      [](const auto &range, unsigned int value) { return range.last < value; });
  return found != std::end(unicodeMarkRanges) && character >= found->first;
}
bool control(unsigned int c) {
  return c <= 0x1fu || (c >= 0x7fu && c <= 0x9fu) || c == 0xadu || c == 0x61cu ||
      c == 0x200bu || (c >= 0x200eu && c <= 0x200fu) || (c >= 0x2028u && c <= 0x202eu) ||
      (c >= 0x2060u && c <= 0x206fu) || c == 0xfeffu;
}
bool regional(unsigned int c) { return c >= 0x1f1e6u && c <= 0x1f1ffu; }
bool prepend(unsigned int c) {
  return (c >= 0x600u && c <= 0x605u) || c == 0x6ddu || c == 0x70fu || c == 0x8e2u ||
      (c >= 0x890u && c <= 0x891u) || c == 0xd4eu || c == 0x110bdu || c == 0x110cdu ||
      (c >= 0x111c2u && c <= 0x111c3u) || c == 0x1193fu || c == 0x11941u || c == 0x11a3au ||
      (c >= 0x11a84u && c <= 0x11a89u) || c == 0x11d46u || c == 0x11f02u;
}
bool virama(unsigned int c) {
  switch (c) {
    case 0x94du: case 0x9cdu: case 0xa4du: case 0xacdu: case 0xb4du: case 0xbcdu: case 0xc4du: case 0xccdu:
    case 0xd3bu: case 0xd3cu: case 0xd4du: case 0xdcau: case 0xe3au: case 0xebau: case 0xf84u: case 0x1039u:
    case 0x103au: case 0x1714u: case 0x1734u: case 0x17d2u: case 0x1a60u: case 0x1b44u: case 0x1baau: case 0x1babu:
    case 0x1bf2u: case 0x1bf3u: case 0x2d7fu: case 0xa806u: case 0xa82cu: case 0xa8c4u: case 0xa953u: case 0xa9c0u:
    case 0xaaf6u: case 0xabedu: case 0x10a3fu: case 0x11046u: case 0x1107fu: case 0x110b9u: case 0x11133u: case 0x11134u:
    case 0x111c0u: case 0x11235u: case 0x112eau: case 0x1134du: case 0x11442u: case 0x114c2u: case 0x115bfu: case 0x1163fu:
    case 0x116b6u: case 0x1172bu: case 0x11839u: case 0x1193du: case 0x1193eu: case 0x119e0u: case 0x11a34u: case 0x11a47u:
    case 0x11a99u: case 0x11c3fu: case 0x11d44u: case 0x11d45u: case 0x11d97u:
      return true;
    default: return false;
  }
}
int hangul(unsigned int c) {
  if ((c >= 0x1100u && c <= 0x115fu) || (c >= 0xa960u && c <= 0xa97cu)) return 1;
  if ((c >= 0x1160u && c <= 0x11a7u) || (c >= 0xd7b0u && c <= 0xd7c6u)) return 2;
  if ((c >= 0x11a8u && c <= 0x11ffu) || (c >= 0xd7cbu && c <= 0xd7fbu)) return 3;
  if (c >= 0xac00u && c <= 0xd7a3u) return (c - 0xac00u) % 28u == 0u ? 4 : 5;
  return 0;
}
bool joins(unsigned int previous, unsigned int next, int precedingRegionals) {
  if (previous == 0xdu && next == 0xau) return true;
  if (control(previous) || control(next)) return false;
  const int a = hangul(previous), b = hangul(next);
  if ((a == 1 && (b == 1 || b == 2 || b == 4 || b == 5)) ||
      ((a == 2 || a == 4) && (b == 2 || b == 3)) || ((a == 3 || a == 5) && b == 3)) return true;
  if (unicodeMark(next) || next == 0x200cu || next == 0x200du ||
      (next >= 0x1f3fbu && next <= 0x1f3ffu) || (next >= 0xe0020u && next <= 0xe007fu)) return true;
  // Conservatively retain both sides of ZWJ/virama, even for unusual sequences.
  if (previous == 0x200du || virama(previous) || prepend(previous)) return true;
  return regional(previous) && regional(next) && precedingRegionals % 2 == 1;
}

bool lyricNewline(unsigned int c) { return c == '\r' || c == '\n' || c == 0x85u || c == 0x2028u || c == 0x2029u; }
bool integer(const juce::var &value) { return value.isInt() || value.isInt64(); }
bool opaqueDocument(const SongLyricsDocument &document) {
  const auto *raw = document.preservedOpaqueDocument.getDynamicObject();
  if (document.schemaVersion <= 3 || raw == nullptr) return false;
  const auto version = raw->getProperty("schemaVersion");
  return integer(version) && static_cast<juce::int64>(version) == document.schemaVersion &&
      juce::JSON::toString(document.preservedOpaqueDocument).getNumBytesAsUTF8() <= 2 * 1024 * 1024;
}

bool knownKind(const juce::String &kind) {
  const auto kinds = songLyricsSectionKinds();
  return std::any_of(kinds.begin(), kinds.end(), [&kind](const auto *candidate) { return kind == candidate; });
}

// UTF-16 boundaries mapped once to JUCE Unicode character indices, making
// validation O(text + sections log sections), never a scan for every range.
struct TextOffsets {
  std::vector<int> units;
  std::vector<int> chordBoundaries;
  std::vector<int> paragraphBoundaries;
  bool valid = true;
  explicit TextOffsets(const juce::String &text) {
    int offset = 0;
    units.push_back(0);
    chordBoundaries.push_back(0);
    paragraphBoundaries.push_back(0);
    unsigned int previous = 0;
    int regionals = 0;
    auto characters = text.getCharPointer();
    while (!characters.isEmpty()) {
      const auto character = static_cast<unsigned int>(characters.getAndAdvance());
      if (character > 0x10ffffu || (character >= 0xd800u && character <= 0xdfffu)) {
        valid = false; return;
      }
      if (offset > 0 && !joins(previous, character, regionals)) chordBoundaries.push_back(offset);
      if (offset > 0 && lyricNewline(previous) && !(previous == '\r' && character == '\n'))
        paragraphBoundaries.push_back(offset);
      regionals = regional(character) ? regionals + 1 : 0;
      previous = character;
      const int count = character > 0xffffu ? 2 : 1;
      if (offset > std::numeric_limits<int>::max() - count) { valid = false; return; }
      offset += count;
      if (offset > kSongLyricsMaxUtf16Length) { valid = false; return; }
      units.push_back(offset);
    }
    if (chordBoundaries.back() != offset) chordBoundaries.push_back(offset);
    if (paragraphBoundaries.back() != offset) paragraphBoundaries.push_back(offset);
  }
  int length() const { return units.back(); }
  int characterIndex(const int offset) const {
    const auto found = std::lower_bound(units.begin(), units.end(), offset);
    return found != units.end() && *found == offset ? static_cast<int>(found - units.begin()) : -1;
  }
  bool chordBoundary(int offset) const {
    return valid && std::binary_search(chordBoundaries.begin(), chordBoundaries.end(), offset);
  }
  bool instrumentalAnchor(int offset) const {
    return valid && std::binary_search(paragraphBoundaries.begin(), paragraphBoundaries.end(), offset);
  }
  int followingParagraph(int offset) const {
    const auto found = std::lower_bound(paragraphBoundaries.begin(), paragraphBoundaries.end(), offset);
    return found != paragraphBoundaries.end() ? *found : length();
  }
  bool range(const int start, const int length, const bool allowEmpty = false) const {
    return valid && start >= 0 && length >= (allowEmpty ? 0 : 1) && start <= this->length() &&
           length <= this->length() - start && characterIndex(start) >= 0 && characterIndex(start + length) >= 0;
  }
};

bool validWithOffsets(const SongLyricsDocument &document, const TextOffsets &offsets) {
  if ((document.schemaVersion < 1 || document.schemaVersion > 3) ||
      !document.preservedOpaqueDocument.isVoid() || !offsets.valid ||
      document.sections.size() > kSongLyricsMaxSections || document.chords.size() > kSongLyricsMaxChords ||
      document.instrumentalBlocks.size() > kSongLyricsMaxInstrumentalBlocks ||
      (document.schemaVersion == 1 && !document.chords.empty()) ||
      (document.schemaVersion < 3 && !document.instrumentalBlocks.empty())) return false;
  std::set<juce::String> ids;
  std::vector<const SongLyricsSection *> ordered;
  ordered.reserve(document.sections.size());
  for (const auto &section : document.sections) {
    if (section.id.isEmpty() || section.id.trim() != section.id || section.id.length() > 128 ||
        !ids.insert(section.id).second || !knownKind(section.kind) ||
        !offsets.range(section.start, section.length)) return false;
    ordered.push_back(&section);
  }
  std::sort(ordered.begin(), ordered.end(), [](const auto *a, const auto *b) { return a->start < b->start; });
  int end = 0;
  for (const auto *section : ordered) {
    if (section->start < end) return false;
    end = section->start + section->length;
  }
  std::set<int> chordPositions;
  for (const auto &chord : document.chords) {
    if (chord.id.isEmpty() || chord.id.trim() != chord.id || chord.id.length() > 128 ||
        !ids.insert(chord.id).second || !chordPositions.insert(chord.offset).second ||
        !offsets.chordBoundary(chord.offset) || !isValidSongLyricsChordSymbol(chord.symbol)) return false;
  }
  size_t totalCells = 0, totalChords = document.chords.size();
  std::set<std::pair<int, int>> blockPositions;
  const auto identity = [&](const juce::String &id) {
    return id.isNotEmpty() && id.trim() == id && id.length() <= 128 && ids.insert(id).second;
  };
  for (const auto &block : document.instrumentalBlocks) {
    if (!identity(block.id) || (block.kind != "intro" && block.kind != "instrumental") ||
        block.label.trim() != block.label || songLyricsUtf16Length(block.label) < 0 ||
        block.label.length() > kSongLyricsMaxInstrumentalLabelLength ||
        !offsets.instrumentalAnchor(block.anchorUtf16) || block.order < 0 ||
        block.order >= kSongLyricsMaxInstrumentalBlocks ||
        !blockPositions.insert({block.anchorUtf16, block.order}).second ||
        block.rows.empty() || block.rows.size() > kSongLyricsMaxInstrumentalRowsPerBlock) return false;
    auto label = block.label.getCharPointer();
    while (!label.isEmpty()) if (control(static_cast<unsigned int>(label.getAndAdvance()))) return false;
    for (const auto &row : block.rows) {
      if (!identity(row.id) || row.cells.empty() || row.cells.size() > kSongLyricsMaxInstrumentalCellsPerRow)
        return false;
      totalCells += row.cells.size();
      if (totalCells > kSongLyricsMaxInstrumentalCells) return false;
      int barCells = 0;
      for (const int length : row.barLengths) {
        if (length <= 0 || length > static_cast<int>(row.cells.size()) - barCells) return false;
        barCells += length;
      }
      if (!row.barLengths.empty() && barCells != static_cast<int>(row.cells.size())) return false;
      for (const auto &cell : row.cells) {
        if (!identity(cell.id) || (cell.symbol && !isValidSongLyricsChordSymbol(*cell.symbol))) return false;
        if (cell.symbol && ++totalChords > kSongLyricsMaxChords) return false;
      }
    }
  }
  return true;
}

void sortInstrumentalBlocks(std::vector<SongLyricsInstrumentalBlock> &blocks) {
  std::stable_sort(blocks.begin(), blocks.end(), [](const auto &a, const auto &b) {
    return a.anchorUtf16 != b.anchorUtf16 ? a.anchorUtf16 < b.anchorUtf16 : a.order < b.order;
  });
}
void renumberInstrumentalBlocks(std::vector<SongLyricsInstrumentalBlock> &blocks) {
  int previous = -1, order = 0;
  for (auto &block : blocks) {
    if (block.anchorUtf16 != previous) { previous = block.anchorUtf16; order = 0; }
    block.order = order++;
  }
}
SongLyricsInstrumentalRow emptyInstrumentalRow(int cells) {
  SongLyricsInstrumentalRow row;
  row.id = juce::Uuid().toString();
  for (int i = 0; i < cells; ++i) row.cells.push_back({juce::Uuid().toString(), std::nullopt});
  return row;
}

void sortSections(std::vector<SongLyricsSection> &sections) {
  std::sort(sections.begin(), sections.end(), [](const auto &a, const auto &b) { return a.start < b.start; });
}

std::vector<SongLyricsSection> withoutRange(const SongLyricsDocument &document, int start, int end) {
  std::vector<SongLyricsSection> result;
  for (const auto &section : document.sections) {
    const int sectionEnd = section.start + section.length;
    if (sectionEnd <= start || section.start >= end) {
      result.push_back(section); continue;
    }
    const bool hasLeft = section.start < start;
    if (hasLeft) {
      auto left = section;
      left.length = start - section.start;
      result.push_back(std::move(left));
    }
    if (sectionEnd > end) {
      auto right = section;
      right.start = end;
      right.length = sectionEnd - end;
      if (hasLeft) right.id = juce::Uuid().toString();
      result.push_back(std::move(right));
    }
  }
  return result;
}
struct LyricsParagraph { int start = 0, contentEnd = 0, end = 0; };
struct LyricsMoveUnit { int start = 0, end = 0; std::vector<size_t> sections; };
struct LyricsMovePlan {
  std::vector<LyricsMoveUnit> units;
  std::vector<int> sectionUnits;
  std::vector<bool> complete;
  bool valid = true;
};
int sectionIndex(const SongLyricsDocument &document, const juce::String &id) {
  for (size_t i = 0; i < document.sections.size(); ++i) if (document.sections[i].id == id) return static_cast<int>(i);
  return -1;
}
int blockIndex(const SongLyricsDocument &document, const juce::String &id) {
  for (size_t i = 0; i < document.instrumentalBlocks.size(); ++i) if (document.instrumentalBlocks[i].id == id) return static_cast<int>(i);
  return -1;
}
bool insideLyricSection(const SongLyricsDocument &document, int anchor) {
  for (const auto &section : document.sections)
    if (anchor > section.start && anchor < section.start + section.length) return true;
  return false;
}
LyricsMovePlan lyricsMovePlan(const SongLyricsDocument &document, const TextOffsets &offsets) {
  LyricsMovePlan plan;
  plan.sectionUnits.resize(document.sections.size(), -1); plan.complete.resize(document.sections.size(), false);
  const auto *text = document.text.toUTF16().getAddress();
  std::vector<LyricsParagraph> paragraphs;
  int start = 0;
  for (int i = 0; i < offsets.length();) {
    if (!lyricNewline(static_cast<unsigned int>(text[i]))) { ++i; continue; }
    const int contentEnd = i++;
    if (text[contentEnd] == '\r' && i < offsets.length() && text[i] == '\n') ++i;
    paragraphs.push_back({start, contentEnd, i}); start = i;
  }
  paragraphs.push_back({start, offsets.length(), offsets.length()});
  const auto paragraphAt = [&](int point) {
    const auto found = std::upper_bound(paragraphs.begin(), paragraphs.end(), point,
        [](int value, const auto &paragraph) { return value < paragraph.start; });
    return static_cast<int>(std::max<std::ptrdiff_t>(0, found - paragraphs.begin() - 1));
  };
  struct Group { int first, last; std::vector<size_t> sections; };
  std::vector<Group> groups;
  for (size_t i = 0; i < document.sections.size(); ++i) {
    const auto &section = document.sections[i]; const int end = section.start + section.length;
    const int first = paragraphAt(section.start), last = paragraphAt(end - 1);
    plan.complete[i] = section.start == paragraphs[static_cast<size_t>(first)].start &&
        paragraphs[static_cast<size_t>(last)].contentEnd > section.start &&
        (end == paragraphs[static_cast<size_t>(last)].contentEnd || end == paragraphs[static_cast<size_t>(last)].end);
    groups.push_back({first, last, {i}});
  }
  std::sort(groups.begin(), groups.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  std::vector<Group> merged;
  for (auto group : groups) {
    if (!merged.empty() && group.first <= merged.back().last) {
      merged.back().last = std::max(merged.back().last, group.last);
      merged.back().sections.insert(merged.back().sections.end(), group.sections.begin(), group.sections.end());
    } else merged.push_back(std::move(group));
  }
  size_t group = 0;
  for (int paragraph = 0; paragraph < static_cast<int>(paragraphs.size()); ++paragraph) {
    LyricsMoveUnit unit;
    unit.start = paragraphs[static_cast<size_t>(paragraph)].start;
    if (group < merged.size() && merged[group].first == paragraph) {
      unit.end = paragraphs[static_cast<size_t>(merged[group].last)].contentEnd;
      unit.sections = merged[group].sections; paragraph = merged[group++].last;
    } else {
      unit.end = paragraphs[static_cast<size_t>(paragraph)].contentEnd;
      const auto content = document.text.substring(offsets.characterIndex(unit.start), offsets.characterIndex(unit.end));
      if (content.trim().isEmpty()) continue;
    }
    if (unit.end <= unit.start) { plan.valid = false; return plan; }
    for (const auto section : unit.sections) {
      const auto &range = document.sections[section];
      // Historical partial selections may not own half of an external CRLF.
      // Preserve such a document, but do not reorder through that ambiguity.
      if (range.start >= unit.end ||
          (range.start + range.length > unit.end && !plan.complete[section])) { plan.valid = false; return plan; }
      plan.sectionUnits[section] = static_cast<int>(plan.units.size());
    }
    plan.units.push_back(std::move(unit));
  }
  return plan;
}
bool movableLyric(const SongLyricsDocument &document, const LyricsMovePlan &plan, int section) {
  if (!plan.valid || section < 0 || !plan.complete[static_cast<size_t>(section)]) return false;
  const int owner = plan.sectionUnits[static_cast<size_t>(section)];
  if (owner < 0 || plan.units[static_cast<size_t>(owner)].sections.size() != 1) return false;
  const auto &unit = plan.units[static_cast<size_t>(owner)];
  const auto *text = document.text.toUTF16().getAddress();
  if (juce::String(juce::CharPointer_UTF16(text + unit.start), juce::CharPointer_UTF16(text + unit.end)).trim().isEmpty()) return false;
  const auto &source = document.sections[static_cast<size_t>(section)];
  for (const auto &block : document.instrumentalBlocks)
    if (block.anchorUtf16 > source.start && block.anchorUtf16 < source.start + source.length) return false;
  return true;
}
int firstParagraphSeparatorLength(const juce::String &text, int offset, int length) {
  if (offset >= length) return 0;
  const auto *units = text.toUTF16().getAddress();
  if (!lyricNewline(static_cast<unsigned int>(units[offset]))) return 0;
  return units[offset] == '\r' && offset + 1 < length && units[offset + 1] == '\n' ? 2 : 1;
}
} // namespace

int songLyricsUtf16Length(const juce::String &text) {
  int units = 0;
  auto characters = text.getCharPointer();
  while (!characters.isEmpty()) {
    const auto character = static_cast<unsigned int>(characters.getAndAdvance());
    if (character > 0x10ffffu || (character >= 0xd800u && character <= 0xdfffu)) return -1;
    const int count = character > 0xffffu ? 2 : 1;
    if (units > std::numeric_limits<int>::max() - count) return -1;
    units += count;
  }
  return units;
}

bool isValidSongLyricsDocument(const SongLyricsDocument &document) {
  return opaqueDocument(document) || validWithOffsets(document, TextOffsets(document.text));
}

bool isEditableSongLyricsDocument(const SongLyricsDocument &document) {
  return validWithOffsets(document, TextOffsets(document.text));
}

bool isSongLyricsChordBoundary(const juce::String &text, int offset) {
  return TextOffsets(text).chordBoundary(offset);
}

bool isValidSongLyricsChordSymbol(const juce::String &symbol) {
  if (symbol.isEmpty() || symbol.trim() != symbol || symbol.length() > kSongLyricsMaxChordSymbolLength) return false;
  if (symbol == "N.C." || symbol == "NC") return true;
  auto note = [](const juce::String &part) {
    return part.length() >= 1 && part.length() <= 2 && part[0] >= 'A' && part[0] <= 'G' &&
        (part.length() == 1 || part[1] == '#' || part[1] == 'b' || part[1] == 0x266f || part[1] == 0x266d);
  };
  const int slash = symbol.indexOfChar('/');
  auto main = slash >= 0 ? symbol.substring(0, slash) : symbol;
  if (slash >= 0 && !note(symbol.substring(slash + 1))) return false;
  if (main.isEmpty() || main[0] < 'A' || main[0] > 'G') return false;
  int position = 1, parentheses = 0;
  if (position < main.length() && (main[position] == '#' || main[position] == 'b' ||
      main[position] == 0x266f || main[position] == 0x266d)) ++position;
  for (; position < main.length(); ++position) {
    const auto c = main[position];
    if (c == '(') { if (++parentheses > 2) return false; }
    else if (c == ')') { if (--parentheses < 0) return false; }
    else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '#' || c == '+' || c == '-' || c == '^' || c == 0xb0 || c == 0xf8 || c == 0x394)) return false;
  }
  return parentheses == 0;
}

bool setSongLyricsChord(SongLyricsDocument &document, int offset, const juce::String &symbol) {
  TextOffsets offsets(document.text);
  const auto normalized = symbol.trim();
  if (!validWithOffsets(document, offsets) || !offsets.chordBoundary(offset) ||
      !isValidSongLyricsChordSymbol(normalized)) return false;
  auto existing = std::find_if(document.chords.begin(), document.chords.end(),
                               [offset](const auto &chord) { return chord.offset == offset; });
  if (existing != document.chords.end()) { existing->symbol = normalized; return true; }
  if (document.chords.size() >= kSongLyricsMaxChords) return false;
  auto candidate = document;
  candidate.chords.push_back({juce::Uuid().toString(), offset, normalized});
  std::sort(candidate.chords.begin(), candidate.chords.end(), [](const auto &a, const auto &b) { return a.offset < b.offset; });
  normalizeSongLyricsSchema(candidate);
  if (!isEditableSongLyricsDocument(candidate)) return false;
  document = std::move(candidate);
  return true;
}

bool removeSongLyricsChord(SongLyricsDocument &document, const juce::String &id) {
  if (!isEditableSongLyricsDocument(document)) return false;
  const auto found = std::find_if(document.chords.begin(), document.chords.end(),
                                [&id](const auto &chord) { return chord.id == id; });
  if (found == document.chords.end()) return false;
  document.chords.erase(found);
  normalizeSongLyricsSchema(document);
  return true;
}

void normalizeSongLyricsSchema(SongLyricsDocument &document) {
  if (document.schemaVersion < 1 || document.schemaVersion > 3 || !document.preservedOpaqueDocument.isVoid()) return;
  document.schemaVersion = !document.instrumentalBlocks.empty() ? 3 : !document.chords.empty() ? 2 : 1;
}

bool isSongLyricsInstrumentalAnchor(const juce::String &text, int anchorUtf16) {
  return TextOffsets(text).instrumentalAnchor(anchorUtf16);
}

bool addSongLyricsInstrumentalBlock(SongLyricsDocument &document, int anchorUtf16,
    const juce::String &kind, const juce::String &label, int cellCount, juce::String *outBlockId) {
  if (!isEditableSongLyricsDocument(document) || cellCount < 1 || cellCount > kSongLyricsMaxInstrumentalCellsPerRow ||
      !isSongLyricsInstrumentalAnchor(document.text, anchorUtf16)) return false;
  auto candidate = document;
  sortInstrumentalBlocks(candidate.instrumentalBlocks);
  renumberInstrumentalBlocks(candidate.instrumentalBlocks);
  int order = 0;
  for (const auto &block : candidate.instrumentalBlocks) if (block.anchorUtf16 == anchorUtf16) ++order;
  SongLyricsInstrumentalBlock block;
  block.id = juce::Uuid().toString(); block.kind = kind; block.label = label.trim();
  block.anchorUtf16 = anchorUtf16; block.order = order; block.rows.push_back(emptyInstrumentalRow(cellCount));
  const auto id = block.id;
  candidate.instrumentalBlocks.push_back(std::move(block));
  sortInstrumentalBlocks(candidate.instrumentalBlocks);
  normalizeSongLyricsSchema(candidate);
  if (!isEditableSongLyricsDocument(candidate)) return false;
  document = std::move(candidate);
  if (outBlockId) *outBlockId = id;
  return true;
}

bool removeSongLyricsInstrumentalBlock(SongLyricsDocument &document, const juce::String &blockId) {
  if (!isEditableSongLyricsDocument(document)) return false;
  const auto found = std::find_if(document.instrumentalBlocks.begin(), document.instrumentalBlocks.end(),
      [&](const auto &block) { return block.id == blockId; });
  if (found == document.instrumentalBlocks.end()) return false;
  document.instrumentalBlocks.erase(found);
  normalizeSongLyricsSchema(document);
  return true;
}

bool addSongLyricsInstrumentalRow(SongLyricsDocument &document, const juce::String &blockId,
                                  int cellCount, juce::String *outRowId) {
  if (!isEditableSongLyricsDocument(document) || cellCount < 1 || cellCount > kSongLyricsMaxInstrumentalCellsPerRow) return false;
  auto candidate = document;
  auto found = std::find_if(candidate.instrumentalBlocks.begin(), candidate.instrumentalBlocks.end(),
      [&](const auto &block) { return block.id == blockId; });
  if (found == candidate.instrumentalBlocks.end()) return false;
  auto row = emptyInstrumentalRow(cellCount);
  const auto id = row.id;
  found->rows.push_back(std::move(row));
  if (!isEditableSongLyricsDocument(candidate)) return false;
  document = std::move(candidate);
  if (outRowId) *outRowId = id;
  return true;
}

bool setSongLyricsInstrumentalCell(SongLyricsDocument &document, const juce::String &blockId,
    const juce::String &rowId, const juce::String &cellId, const juce::String &symbol) {
  const auto normalized = symbol.trim();
  if (!isEditableSongLyricsDocument(document) || !isValidSongLyricsChordSymbol(normalized)) return false;
  auto candidate = document;
  for (auto &block : candidate.instrumentalBlocks) if (block.id == blockId)
    for (auto &row : block.rows) if (row.id == rowId)
      for (auto &cell : row.cells) if (cell.id == cellId) {
        cell.symbol = normalized;
        if (!isEditableSongLyricsDocument(candidate)) return false;
        document = std::move(candidate); return true;
      }
  return false;
}

bool clearSongLyricsInstrumentalCell(SongLyricsDocument &document, const juce::String &blockId,
    const juce::String &rowId, const juce::String &cellId) {
  if (!isEditableSongLyricsDocument(document)) return false;
  for (auto &block : document.instrumentalBlocks) if (block.id == blockId)
    for (auto &row : block.rows) if (row.id == rowId)
      for (auto &cell : row.cells) if (cell.id == cellId) { cell.symbol.reset(); return true; }
  return false;
}

bool hasSongLyrics(const std::optional<SongLyricsDocument> &document) {
  return document.has_value() && (document->text.trim().isNotEmpty() || !document->sections.empty() ||
      !document->chords.empty() || !document->instrumentalBlocks.empty() || opaqueDocument(*document));
}

std::optional<SongLyricsDocument> songLyricsFromVar(const juce::var &value) {
  const auto *object = value.getDynamicObject();
  if (object == nullptr) return std::nullopt;
  const auto schema = object->getProperty("schemaVersion");
  if (!integer(schema)) return std::nullopt;
  const auto version = static_cast<juce::int64>(schema);
  if (version > 3 && version <= std::numeric_limits<int>::max()) {
    SongLyricsDocument future;
    future.schemaVersion = static_cast<int>(version);
    future.text = object->getProperty("text").isString() ? object->getProperty("text").toString() : juce::String();
    future.preservedOpaqueDocument = value.clone();
    return opaqueDocument(future) ? std::optional<SongLyricsDocument>(std::move(future)) : std::nullopt;
  }
  const auto *sections = object->getProperty("sections").getArray();
  if ((version < 1 || version > 3) ||
      !object->getProperty("offsetEncoding").isString() || object->getProperty("offsetEncoding").toString() != "utf16" ||
      !object->getProperty("text").isString() || sections == nullptr || sections->size() > kSongLyricsMaxSections)
    return std::nullopt;
  SongLyricsDocument document;
  document.schemaVersion = static_cast<int>(version);
  document.text = object->getProperty("text").toString();
  TextOffsets offsets(document.text);
  if (!offsets.valid) return std::nullopt;
  for (const auto &valueSection : *sections) {
    const auto *section = valueSection.getDynamicObject();
    if (section == nullptr || !section->getProperty("id").isString() ||
        !section->getProperty("kind").isString()) return std::nullopt;
    const auto start = section->getProperty("start"), length = section->getProperty("length");
    if (!(start.isInt() || start.isInt64()) || !(length.isInt() || length.isInt64())) return std::nullopt;
    const auto wideStart = static_cast<juce::int64>(start), wideLength = static_cast<juce::int64>(length);
    if (wideStart < 0 || wideStart > kSongLyricsMaxUtf16Length || wideLength <= 0 ||
        wideLength > kSongLyricsMaxUtf16Length) return std::nullopt;
    document.sections.push_back({section->getProperty("id").toString(), section->getProperty("kind").toString(),
                                 static_cast<int>(wideStart), static_cast<int>(wideLength)});
  }
  const auto chordValue = object->getProperty("chords");
  const auto *chords = chordValue.getArray();
  if (version >= 2 && (chords == nullptr || chords->size() > kSongLyricsMaxChords)) return std::nullopt;
  if (version == 1 && !chordValue.isVoid()) return std::nullopt;
  if (chords != nullptr) for (const auto &valueChord : *chords) {
    const auto *chord = valueChord.getDynamicObject();
    if (chord == nullptr || !chord->getProperty("id").isString() ||
        !chord->getProperty("symbol").isString() || !integer(chord->getProperty("offset"))) return std::nullopt;
    const auto offset = static_cast<juce::int64>(chord->getProperty("offset"));
    if (offset < 0 || offset > kSongLyricsMaxUtf16Length) return std::nullopt;
    document.chords.push_back({chord->getProperty("id").toString(), static_cast<int>(offset), chord->getProperty("symbol").toString()});
  }
  const auto blocksValue = object->getProperty("instrumentalBlocks");
  const auto *blocks = blocksValue.getArray();
  if (version == 3 && (blocks == nullptr || blocks->size() > kSongLyricsMaxInstrumentalBlocks)) return std::nullopt;
  if (version < 3 && object->hasProperty("instrumentalBlocks")) return std::nullopt;
  size_t cellCount = 0;
  if (blocks) for (const auto &valueBlock : *blocks) {
    const auto *encoded = valueBlock.getDynamicObject();
    if (encoded == nullptr || !encoded->getProperty("id").isString() || !encoded->getProperty("kind").isString() ||
        (encoded->hasProperty("label") && !encoded->getProperty("label").isString()) ||
        !integer(encoded->getProperty("anchorUtf16")) || !integer(encoded->getProperty("order"))) return std::nullopt;
    const auto anchor = static_cast<juce::int64>(encoded->getProperty("anchorUtf16"));
    const auto order = static_cast<juce::int64>(encoded->getProperty("order"));
    if (anchor < 0 || anchor > kSongLyricsMaxUtf16Length || order < 0 || order >= kSongLyricsMaxInstrumentalBlocks) return std::nullopt;
    const auto *rows = encoded->getProperty("rows").getArray();
    if (rows == nullptr || rows->isEmpty() || rows->size() > kSongLyricsMaxInstrumentalRowsPerBlock) return std::nullopt;
    SongLyricsInstrumentalBlock block;
    block.id = encoded->getProperty("id").toString(); block.kind = encoded->getProperty("kind").toString();
    block.label = encoded->getProperty("label").toString(); block.anchorUtf16 = static_cast<int>(anchor); block.order = static_cast<int>(order);
    for (const auto &valueRow : *rows) {
      const auto *encodedRow = valueRow.getDynamicObject();
      if (encodedRow == nullptr || !encodedRow->getProperty("id").isString()) return std::nullopt;
      const auto *cells = encodedRow->getProperty("cells").getArray();
      if (cells == nullptr || cells->isEmpty() || cells->size() > kSongLyricsMaxInstrumentalCellsPerRow) return std::nullopt;
      cellCount += static_cast<size_t>(cells->size());
      if (cellCount > kSongLyricsMaxInstrumentalCells) return std::nullopt;
      SongLyricsInstrumentalRow row;
      row.id = encodedRow->getProperty("id").toString();
      if (encodedRow->hasProperty("barLengths")) {
        const auto *lengths = encodedRow->getProperty("barLengths").getArray();
        if (lengths == nullptr || lengths->size() > cells->size()) return std::nullopt;
        for (const auto &length : *lengths) {
          if (!integer(length) || static_cast<juce::int64>(length) < 1 ||
              static_cast<juce::int64>(length) > cells->size()) return std::nullopt;
          row.barLengths.push_back(static_cast<int>(length));
        }
      }
      for (const auto &valueCell : *cells) {
        const auto *encodedCell = valueCell.getDynamicObject();
        if (encodedCell == nullptr || !encodedCell->getProperty("id").isString() ||
            (encodedCell->hasProperty("symbol") && !encodedCell->getProperty("symbol").isString())) return std::nullopt;
        SongLyricsInstrumentalCell cell;
        cell.id = encodedCell->getProperty("id").toString();
        if (encodedCell->hasProperty("symbol")) cell.symbol = encodedCell->getProperty("symbol").toString();
        row.cells.push_back(std::move(cell));
      }
      block.rows.push_back(std::move(row));
    }
    document.instrumentalBlocks.push_back(std::move(block));
  }
  if (!validWithOffsets(document, offsets)) return std::nullopt;
  sortInstrumentalBlocks(document.instrumentalBlocks);
  std::sort(document.chords.begin(), document.chords.end(), [](const auto &a, const auto &b) { return a.offset < b.offset; });
  sortSections(document.sections);
  return document;
}

juce::var songLyricsToVar(const SongLyricsDocument &document) {
  if (opaqueDocument(document)) return document.preservedOpaqueDocument.clone();
  auto *object = new juce::DynamicObject();
  object->setProperty("schemaVersion", document.schemaVersion);
  object->setProperty("offsetEncoding", "utf16");
  object->setProperty("text", document.text);
  juce::Array<juce::var> sections;
  auto ordered = document.sections;
  sortSections(ordered);
  for (const auto &section : ordered) {
    auto *encoded = new juce::DynamicObject();
    encoded->setProperty("id", section.id);
    encoded->setProperty("kind", section.kind);
    encoded->setProperty("start", section.start);
    encoded->setProperty("length", section.length);
    sections.add(juce::var(encoded));
  }
  object->setProperty("sections", std::move(sections));
  if (document.schemaVersion >= 2 && document.schemaVersion <= 3) {
    juce::Array<juce::var> chords;
    auto orderedChords = document.chords;
    std::sort(orderedChords.begin(), orderedChords.end(), [](const auto &a, const auto &b) { return a.offset < b.offset; });
    for (const auto &chord : orderedChords) {
      auto *encoded = new juce::DynamicObject();
      encoded->setProperty("id", chord.id); encoded->setProperty("offset", chord.offset); encoded->setProperty("symbol", chord.symbol);
      chords.add(juce::var(encoded));
    }
    object->setProperty("chords", std::move(chords));
  }
  if (document.schemaVersion == 3) {
    juce::Array<juce::var> blocks;
    auto orderedBlocks = document.instrumentalBlocks;
    sortInstrumentalBlocks(orderedBlocks);
    for (const auto &block : orderedBlocks) {
      auto *encodedBlock = new juce::DynamicObject();
      encodedBlock->setProperty("id", block.id); encodedBlock->setProperty("kind", block.kind);
      if (block.label.isNotEmpty()) encodedBlock->setProperty("label", block.label);
      encodedBlock->setProperty("anchorUtf16", block.anchorUtf16); encodedBlock->setProperty("order", block.order);
      juce::Array<juce::var> rows;
      for (const auto &row : block.rows) {
        auto *encodedRow = new juce::DynamicObject(); encodedRow->setProperty("id", row.id);
        juce::Array<juce::var> cells;
        for (const auto &cell : row.cells) {
          auto *encodedCell = new juce::DynamicObject(); encodedCell->setProperty("id", cell.id);
          if (cell.symbol) encodedCell->setProperty("symbol", *cell.symbol);
          cells.add(juce::var(encodedCell));
        }
        encodedRow->setProperty("cells", std::move(cells));
        if (!row.barLengths.empty()) {
          juce::Array<juce::var> lengths;
          for (const int length : row.barLengths) lengths.add(length);
          encodedRow->setProperty("barLengths", std::move(lengths));
        }
        rows.add(juce::var(encodedRow));
      }
      encodedBlock->setProperty("rows", std::move(rows)); blocks.add(juce::var(encodedBlock));
    }
    object->setProperty("instrumentalBlocks", std::move(blocks));
  }
  return juce::var(object);
}

bool applySongLyricsSection(SongLyricsDocument &document, const int start, const int length,
                            const juce::String &kind) {
  TextOffsets offsets(document.text);
  if (!validWithOffsets(document, offsets) || !offsets.range(start, length) || !knownKind(kind)) return false;
  for (const auto &section : document.sections)
    if (section.start == start && section.length == length && section.kind == kind) return true;
  auto result = withoutRange(document, start, start + length);
  if (result.size() >= kSongLyricsMaxSections) return false;
  result.push_back({juce::Uuid().toString(), kind, start, length});
  sortSections(result);
  document.sections = std::move(result);
  return true;
}

bool clearSongLyricsSections(SongLyricsDocument &document, const int start, const int length) {
  TextOffsets offsets(document.text);
  if (!validWithOffsets(document, offsets) || !offsets.range(start, length)) return false;
  auto result = withoutRange(document, start, start + length);
  if (result.size() > kSongLyricsMaxSections) return false;
  sortSections(result);
  document.sections = std::move(result);
  return true;
}

bool removeSongLyricsSection(SongLyricsDocument &document, const juce::String &id) {
  if (!isEditableSongLyricsDocument(document)) return false;
  const auto found = std::find_if(document.sections.begin(), document.sections.end(),
                                 [&id](const auto &section) { return section.id == id; });
  if (found == document.sections.end()) return false;
  document.sections.erase(found);
  return true;
}

std::vector<juce::String> movableSongLyricsCardIds(const SongLyricsDocument &document) {
  TextOffsets offsets(document.text);
  if (!validWithOffsets(document, offsets)) return {};
  const auto plan = lyricsMovePlan(document, offsets);
  std::vector<juce::String> result;
  result.reserve(document.sections.size() + document.instrumentalBlocks.size());
  for (size_t i = 0; i < document.sections.size(); ++i)
    if (movableLyric(document, plan, static_cast<int>(i))) result.push_back(document.sections[i].id);
  for (const auto &block : document.instrumentalBlocks) result.push_back(block.id);
  return result;
}

bool canMoveSongLyricsCard(const SongLyricsDocument &document, const juce::String &id) {
  if (!isEditableSongLyricsDocument(document)) return false;
  if (blockIndex(document, id) >= 0) return true;
  const int section = sectionIndex(document, id);
  if (section < 0) return false;
  return movableLyric(document, lyricsMovePlan(document, TextOffsets(document.text)), section);
}

bool moveSongLyricsCardBefore(SongLyricsDocument &document, const juce::String &sourceId,
                              const juce::String &beforeId) {
  TextOffsets offsets(document.text);
  if (!validWithOffsets(document, offsets)) return false;
  const int sourceSection = sectionIndex(document, sourceId), sourceBlock = blockIndex(document, sourceId);
  if (sourceSection < 0 && sourceBlock < 0) return false;
  if (sourceId == beforeId) return true;
  const int targetSection = sectionIndex(document, beforeId), targetBlock = blockIndex(document, beforeId);
  if (beforeId.isNotEmpty() && targetSection < 0 && targetBlock < 0) return false;
  const auto plan = lyricsMovePlan(document, offsets);
  if (targetSection >= 0 && !plan.complete[static_cast<size_t>(targetSection)]) return false;
  if (targetBlock >= 0 && insideLyricSection(document, document.instrumentalBlocks[static_cast<size_t>(targetBlock)].anchorUtf16))
    return false;
  auto candidate = document;
  if (sourceBlock >= 0) {
    auto moved = candidate.instrumentalBlocks[static_cast<size_t>(sourceBlock)];
    candidate.instrumentalBlocks.erase(candidate.instrumentalBlocks.begin() + sourceBlock);
    sortInstrumentalBlocks(candidate.instrumentalBlocks);
    moved.anchorUtf16 = targetSection >= 0 ? document.sections[static_cast<size_t>(targetSection)].start :
        targetBlock >= 0 ? document.instrumentalBlocks[static_cast<size_t>(targetBlock)].anchorUtf16 : offsets.length();
    auto insertion = candidate.instrumentalBlocks.end();
    if (targetBlock >= 0) insertion = std::find_if(candidate.instrumentalBlocks.begin(), candidate.instrumentalBlocks.end(),
        [&](const auto &block) { return block.id == beforeId; });
    else insertion = std::upper_bound(candidate.instrumentalBlocks.begin(), candidate.instrumentalBlocks.end(), moved.anchorUtf16,
        [](int anchor, const auto &block) { return anchor < block.anchorUtf16; });
    candidate.instrumentalBlocks.insert(insertion, std::move(moved));
    renumberInstrumentalBlocks(candidate.instrumentalBlocks);
    normalizeSongLyricsSchema(candidate);
    if (!isEditableSongLyricsDocument(candidate)) return false;
    document = std::move(candidate); return true;
  }
  if (!movableLyric(document, plan, sourceSection) || plan.units.empty()) return false;
  const int sourceUnit = plan.sectionUnits[static_cast<size_t>(sourceSection)];
  struct Item { int point, unit, block; };
  std::vector<Item> items;
  std::vector<int> blockOwners(document.instrumentalBlocks.size(), -1), originalNext(document.instrumentalBlocks.size(), -1);
  const auto unitBefore = [&](int point) {
    const auto found = std::upper_bound(plan.units.begin(), plan.units.end(), point,
        [](int value, const auto &unit) { return value < unit.start; });
    return static_cast<int>(found - plan.units.begin()) - 1;
  };
  for (size_t i = 0; i < plan.units.size(); ++i) items.push_back({plan.units[i].start, static_cast<int>(i), -1});
  for (size_t i = 0; i < document.instrumentalBlocks.size(); ++i) {
    const auto &block = document.instrumentalBlocks[i]; const int owner = unitBefore(block.anchorUtf16);
    if (owner >= 0 && block.anchorUtf16 > plan.units[static_cast<size_t>(owner)].start &&
        block.anchorUtf16 < plan.units[static_cast<size_t>(owner)].end) blockOwners[i] = owner;
    else items.push_back({block.anchorUtf16, -1, static_cast<int>(i)});
  }
  std::sort(items.begin(), items.end(), [&](const auto &a, const auto &b) {
    if (a.point != b.point) return a.point < b.point;
    if (a.block < 0 || b.block < 0) return a.block >= 0;
    return document.instrumentalBlocks[static_cast<size_t>(a.block)].order <
           document.instrumentalBlocks[static_cast<size_t>(b.block)].order;
  });
  int next = -1;
  for (auto i = items.rbegin(); i != items.rend(); ++i)
    if (i->unit >= 0) next = i->unit; else originalNext[static_cast<size_t>(i->block)] = next;
  const auto source = std::find_if(items.begin(), items.end(), [&](const auto &item) { return item.unit == sourceUnit; });
  const auto moved = *source; items.erase(source);
  auto insertion = items.end();
  if (targetSection >= 0) {
    const int targetUnit = plan.sectionUnits[static_cast<size_t>(targetSection)];
    if (targetUnit < 0 || plan.units[static_cast<size_t>(targetUnit)].sections.size() != 1) return false;
    insertion = std::find_if(items.begin(), items.end(), [&](const auto &item) { return item.unit == targetUnit; });
  } else if (targetBlock >= 0) {
    insertion = std::find_if(items.begin(), items.end(), [&](const auto &item) { return item.block == targetBlock; });
  }
  if (beforeId.isNotEmpty() && insertion == items.end()) return false;
  items.insert(insertion, moved);
  std::vector<int> permutation;
  for (const auto &item : items) if (item.unit >= 0) permutation.push_back(item.unit);
  std::vector<int> starts(plan.units.size()), ends(plan.units.size());
  const auto slice = [&](int start, int end) {
    return document.text.substring(offsets.characterIndex(start), offsets.characterIndex(end));
  };
  candidate.text = slice(0, plan.units.front().start);
  int position = plan.units.front().start;
  for (size_t i = 0; i < permutation.size(); ++i) {
    const int unit = permutation[i]; const auto &body = plan.units[static_cast<size_t>(unit)];
    starts[static_cast<size_t>(unit)] = position;
    candidate.text += slice(body.start, body.end); position += body.end - body.start;
    ends[static_cast<size_t>(unit)] = position;
    const int gapEnd = i + 1 < plan.units.size() ? plan.units[i + 1].start : offsets.length();
    candidate.text += slice(plan.units[i].end, gapEnd); position += gapEnd - plan.units[i].end;
  }
  TextOffsets reordered(candidate.text);
  for (size_t i = 0; i < candidate.sections.size(); ++i) {
    auto &section = candidate.sections[i]; const int owner = plan.sectionUnits[i];
    if (owner < 0) return false;
    const auto &unit = plan.units[static_cast<size_t>(owner)]; const int oldEnd = section.start + section.length;
    const int newStart = starts[static_cast<size_t>(owner)] + section.start - unit.start;
    int newEnd = starts[static_cast<size_t>(owner)] + oldEnd - unit.start;
    if (oldEnd > unit.end) newEnd = ends[static_cast<size_t>(owner)] +
        firstParagraphSeparatorLength(candidate.text, ends[static_cast<size_t>(owner)], reordered.length());
    section.start = newStart; section.length = newEnd - newStart;
  }
  const auto mapPoint = [&](int point) {
    const int owner = unitBefore(point);
    if (owner < 0) return point; // The leading blank prefix is unchanged.
    const auto &unit = plan.units[static_cast<size_t>(owner)];
    // The content end is a real end-of-line chord, even if its LF was outside
    // the section range. It follows its body, not an arbitrary connector.
    if (point <= unit.end) return starts[static_cast<size_t>(owner)] + point - unit.start;
    return ends[static_cast<size_t>(permutation[static_cast<size_t>(owner)])] + point - unit.end;
  };
  for (auto &chord : candidate.chords) chord.offset = mapPoint(chord.offset);
  for (size_t i = 0; i < candidate.instrumentalBlocks.size(); ++i) if (blockOwners[i] >= 0) {
    const int owner = blockOwners[i]; auto &block = candidate.instrumentalBlocks[i];
    block.anchorUtf16 = starts[static_cast<size_t>(owner)] + block.anchorUtf16 - plan.units[static_cast<size_t>(owner)].start;
  }
  std::map<int, int> orders;
  std::vector<int> pending;
  int previousUnit = -1;
  const auto flushBlocks = [&](int following) {
    const int end = following >= 0 ? starts[static_cast<size_t>(following)] : reordered.length();
    const int begin = previousUnit >= 0 ? ends[static_cast<size_t>(previousUnit)] : 0;
    bool retainGapPositions = true;
    std::vector<int> anchors;
    for (const int index : pending) {
      const auto &original = document.instrumentalBlocks[static_cast<size_t>(index)];
      const int oldEnd = following >= 0 ? plan.units[static_cast<size_t>(following)].start : offsets.length();
      const int anchor = end + original.anchorUtf16 - oldEnd;
      if (originalNext[static_cast<size_t>(index)] != following || anchor < begin || anchor > end ||
          !reordered.instrumentalAnchor(anchor)) retainGapPositions = false;
      anchors.push_back(anchor);
    }
    for (size_t i = 0; i < pending.size(); ++i) {
      auto &block = candidate.instrumentalBlocks[static_cast<size_t>(pending[i])];
      block.anchorUtf16 = retainGapPositions ? anchors[i] : end;
      block.order = orders[block.anchorUtf16]++;
    }
    pending.clear();
  };
  for (const auto &item : items) {
    if (item.block >= 0) pending.push_back(item.block);
    else { flushBlocks(item.unit); previousUnit = item.unit; }
  }
  flushBlocks(-1);
  sortSections(candidate.sections);
  std::sort(candidate.chords.begin(), candidate.chords.end(), [](const auto &a, const auto &b) { return a.offset < b.offset; });
  sortInstrumentalBlocks(candidate.instrumentalBlocks); renumberInstrumentalBlocks(candidate.instrumentalBlocks);
  normalizeSongLyricsSchema(candidate);
  if (!isEditableSongLyricsDocument(candidate)) return false;
  document = std::move(candidate);
  return true;
}

bool replaceSongLyricsText(SongLyricsDocument &document, const int start, const int length,
                           const juce::String &replacement) {
  TextOffsets oldOffsets(document.text), replacementOffsets(replacement);
  if (!validWithOffsets(document, oldOffsets) || !oldOffsets.range(start, length, true) || !replacementOffsets.valid)
    return false;
  const int insertedLength = replacementOffsets.length();
  if (oldOffsets.length() - length > kSongLyricsMaxUtf16Length - insertedLength) return false;
  const int end = start + length, delta = insertedLength - length;
  const auto newText = document.text.substring(0, oldOffsets.characterIndex(start)) + replacement +
                       document.text.substring(oldOffsets.characterIndex(end));
  if (newText == document.text) return true;
  std::vector<SongLyricsSection> sections;
  for (auto section : document.sections) {
    const int sectionEnd = section.start + section.length;
    if (sectionEnd <= start) { sections.push_back(std::move(section)); continue; }
    if (section.start >= end) { section.start += delta; sections.push_back(std::move(section)); continue; }
    if (section.start <= start && sectionEnd >= end) {
      section.length += delta;
    } else if (section.start < start) {
      section.length = start - section.start;
    } else if (sectionEnd > end) {
      section.start = start + insertedLength;
      section.length = sectionEnd - end;
    } else {
      continue;
    }
    if (section.length > 0) sections.push_back(std::move(section));
  }
  auto candidate = document;
  candidate.text = newText;
  candidate.sections = std::move(sections);
  candidate.chords.clear();
  for (auto chord : document.chords) {
    // Right affinity: inserted characters remain before the original syllable.
    // Removing the syllable removes only anchors in the replaced half-open range.
    if (length > 0 && chord.offset >= start && chord.offset < end) continue;
    if (chord.offset >= end) chord.offset += delta;
    candidate.chords.push_back(std::move(chord));
  }
  // Blocks are independent content. Retain all of them even when their
  // anchoring text is removed, snapping only forward to a real paragraph/EOF.
  sortInstrumentalBlocks(candidate.instrumentalBlocks);
  TextOffsets newOffsets(candidate.text);
  for (auto &block : candidate.instrumentalBlocks) {
    if (length == 0) {
      if (block.anchorUtf16 > start) block.anchorUtf16 += insertedLength;
    } else if (block.anchorUtf16 >= end) block.anchorUtf16 += delta;
    else if (block.anchorUtf16 > start) block.anchorUtf16 = start;
    block.anchorUtf16 = newOffsets.followingParagraph(block.anchorUtf16);
  }
  renumberInstrumentalBlocks(candidate.instrumentalBlocks);
  normalizeSongLyricsSchema(candidate);
  sortSections(candidate.sections);
  if (!isEditableSongLyricsDocument(candidate)) return false;
  document = std::move(candidate);
  return true;
}

bool setSongLyricsText(SongLyricsDocument &document, const juce::String &text) {
  TextOffsets oldOffsets(document.text), newOffsets(text);
  if (!validWithOffsets(document, oldOffsets) || !newOffsets.valid) return false;
  if (document.text == text) return true;
  // CharPointer_UTF16::operator[] counts Unicode scalars, not UTF16 units.
  // Compare the actual units directly in O(n), matching the native offsets.
  const auto *before = document.text.toUTF16().getAddress();
  const auto *after = text.toUTF16().getAddress();
  int prefix = 0;
  while (prefix < oldOffsets.length() && prefix < newOffsets.length() && before[prefix] == after[prefix]) ++prefix;
  while (prefix > 0 && (oldOffsets.characterIndex(prefix) < 0 || newOffsets.characterIndex(prefix) < 0)) --prefix;
  int suffix = 0;
  while (suffix < oldOffsets.length() - prefix && suffix < newOffsets.length() - prefix &&
         before[oldOffsets.length() - suffix - 1] == after[newOffsets.length() - suffix - 1]) ++suffix;
  while (suffix > 0 && (oldOffsets.characterIndex(oldOffsets.length() - suffix) < 0 ||
                        newOffsets.characterIndex(newOffsets.length() - suffix) < 0)) --suffix;
  const int newEnd = newOffsets.length() - suffix;
  return replaceSongLyricsText(document, prefix, oldOffsets.length() - prefix - suffix,
                               text.substring(newOffsets.characterIndex(prefix), newOffsets.characterIndex(newEnd)));
}
