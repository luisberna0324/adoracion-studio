#include "NativeSongLyricsEditing.h"
#include <algorithm>

namespace NativeSongLyricsEditing {
namespace {
bool newline(juce::CharPointer_UTF16::CharType c) {
  return c == '\r' || c == '\n' || c == 0x85 || c == 0x2028 || c == 0x2029;
}
bool boundary(const juce::CharPointer_UTF16::CharType *units, int size, int offset) {
  if (offset < 0 || offset > size) return false;
  if (offset == 0 || offset == size) return true;
  const auto previous = static_cast<juce::uint16>(units[offset - 1]);
  const auto next = static_cast<juce::uint16>(units[offset]);
  return !(previous >= 0xd800 && previous <= 0xdbff && next >= 0xdc00 && next <= 0xdfff);
}
// A complete range starts at a paragraph boundary and ends immediately
// before or after a complete separator (or at EOF). Inspect only neighboring
// units, never scan the document again for every card. The final touched
// paragraph must have a content end after the range start, matching the
// nonempty enclosingParagraphs(..., trimFinalSeparator=true) contract even
// when the range consists of blank paragraphs.
bool completeParagraphs(const juce::CharPointer_UTF16::CharType *units,
                        int size, int start, int length) {
  if (size < 0 || start < 0 || length <= 0 || start > size || length > size - start ||
      !boundary(units, size, start) || !boundary(units, size, start + length)) return false;
  if (start > 0 && (!newline(units[start - 1]) ||
      (units[start - 1] == '\r' && units[start] == '\n'))) return false;
  const int end = start + length;
  // The position between CR and LF is not a paragraph boundary.
  if (end < size && units[end - 1] == '\r' && units[end] == '\n') return false;
  const bool afterSeparator = newline(units[end - 1]);
  if (end < size && !newline(units[end]) && !afterSeparator) return false;
  int contentEnd = end;
  if (afterSeparator) {
    --contentEnd;
    if (units[end - 1] == '\n' && contentEnd > start && units[contentEnd - 1] == '\r') --contentEnd;
  }
  return contentEnd > start;
}
} // namespace

Range enclosingParagraphs(const juce::String &text, int start, int length, bool trimFinalSeparator) {
  const int size = songLyricsUtf16Length(text);
  if (size < 0 || start < 0 || length <= 0 || start > size || length > size - start) return {};
  const auto *units = text.toUTF16().getAddress();
  if (!boundary(units, size, start) || !boundary(units, size, start + length)) return {};
  const int last = start + length - 1;
  int paragraphStart = 0, first = -1;
  while (paragraphStart < size) {
    int contentEnd = paragraphStart;
    while (contentEnd < size && !newline(units[contentEnd])) ++contentEnd;
    int end = contentEnd;
    if (end < size) {
      ++end;
      if (units[contentEnd] == '\r' && end < size && units[end] == '\n') ++end;
    }
    if (start >= paragraphStart && start < end) first = paragraphStart;
    if (last >= paragraphStart && last < end) {
      const int chosenEnd = trimFinalSeparator ? contentEnd : end;
      return first >= 0 && chosenEnd > first ? Range{first, chosenEnd - first} : Range{};
    }
    paragraphStart = end;
  }
  return {};
}

bool isCompleteParagraphs(const juce::String &text, int start, int length) {
  const int size = songLyricsUtf16Length(text);
  return completeParagraphs(text.toUTF16().getAddress(), size, start, length);
}

std::vector<bool> completeParagraphsForSections(const SongLyricsDocument &document) {
  const int size = songLyricsUtf16Length(document.text);
  const auto *units = document.text.toUTF16().getAddress();
  std::vector<bool> result;
  result.reserve(document.sections.size());
  for (const auto &section : document.sections)
    result.push_back(completeParagraphs(units, size, section.start, section.length));
  return result;
}

bool replaceText(SongLyricsDocument &document, int start, int length, const juce::String &replacement) {
  if (!isEditableSongLyricsDocument(document)) return false;
  auto candidate = document;
  juce::String owner;
  int ownerStart = 0;
  bool ownsStart = false;
  const int inserted = songLyricsUtf16Length(replacement);
  if (length == 0 && inserted > 0) {
    for (const auto &section : candidate.sections) {
      if (section.start != start && section.start + section.length != start) continue;
      if (!isCompleteParagraphs(candidate.text, section.start, section.length)) continue;
      if (section.start == start) {
        owner = section.id; ownerStart = section.start; ownsStart = true; break;
      }
      owner = section.id; ownerStart = section.start;
    }
  }
  if (!replaceSongLyricsText(candidate, start, length, replacement)) return false;
  if (owner.isNotEmpty()) {
    for (auto &section : candidate.sections) {
      if (section.id != owner) continue;
      if (ownsStart) section.start = ownerStart;
      section.length += inserted;
      break;
    }
  }
  if (!isValidSongLyricsDocument(candidate)) return false;
  document = std::move(candidate);
  return true;
}

bool setText(SongLyricsDocument &document, const juce::String &text) {
  if (!isEditableSongLyricsDocument(document)) return false;
  if (document.text == text) return true;
  const int oldSize = songLyricsUtf16Length(document.text), newSize = songLyricsUtf16Length(text);
  if (oldSize < 0 || newSize < 0 || newSize > kSongLyricsMaxUtf16Length) return false;
  const auto *before = document.text.toUTF16().getAddress();
  const auto *after = text.toUTF16().getAddress();
  int prefix = 0;
  while (prefix < oldSize && prefix < newSize && before[prefix] == after[prefix]) ++prefix;
  while (prefix > 0 && (!boundary(before, oldSize, prefix) || !boundary(after, newSize, prefix))) --prefix;
  int suffix = 0;
  while (suffix < oldSize - prefix && suffix < newSize - prefix &&
      before[oldSize - suffix - 1] == after[newSize - suffix - 1]) ++suffix;
  while (suffix > 0 && (!boundary(before, oldSize, oldSize - suffix) ||
      !boundary(after, newSize, newSize - suffix))) --suffix;
  const auto replacement = juce::String(juce::CharPointer_UTF16(after + prefix),
                                       juce::CharPointer_UTF16(after + newSize - suffix));
  return replaceText(document, prefix, oldSize - prefix - suffix, replacement);
}
} // namespace NativeSongLyricsEditing
