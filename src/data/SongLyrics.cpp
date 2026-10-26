#include "SongLyrics.h"

#include <algorithm>
#include <limits>
#include <set>

namespace {
bool knownKind(const juce::String &kind) {
  const auto kinds = songLyricsSectionKinds();
  return std::any_of(kinds.begin(), kinds.end(), [&kind](const auto *candidate) { return kind == candidate; });
}

// UTF-16 boundaries mapped once to JUCE Unicode character indices, making
// validation O(text + sections log sections), never a scan for every range.
struct TextOffsets {
  std::vector<int> units;
  bool valid = true;
  explicit TextOffsets(const juce::String &text) {
    int offset = 0;
    units.push_back(0);
    auto characters = text.getCharPointer();
    while (!characters.isEmpty()) {
      const auto character = static_cast<unsigned int>(characters.getAndAdvance());
      if (character > 0x10ffffu || (character >= 0xd800u && character <= 0xdfffu)) {
        valid = false; return;
      }
      const int count = character > 0xffffu ? 2 : 1;
      if (offset > std::numeric_limits<int>::max() - count) { valid = false; return; }
      offset += count;
      if (offset > kSongLyricsMaxUtf16Length) { valid = false; return; }
      units.push_back(offset);
    }
  }
  int length() const { return units.back(); }
  int characterIndex(const int offset) const {
    const auto found = std::lower_bound(units.begin(), units.end(), offset);
    return found != units.end() && *found == offset ? static_cast<int>(found - units.begin()) : -1;
  }
  bool range(const int start, const int length, const bool allowEmpty = false) const {
    return valid && start >= 0 && length >= (allowEmpty ? 0 : 1) && start <= this->length() &&
           length <= this->length() - start && characterIndex(start) >= 0 && characterIndex(start + length) >= 0;
  }
};

bool validWithOffsets(const SongLyricsDocument &document, const TextOffsets &offsets) {
  if (document.schemaVersion != 1 || !offsets.valid || document.sections.size() > kSongLyricsMaxSections)
    return false;
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
  return true;
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
  return validWithOffsets(document, TextOffsets(document.text));
}

bool hasSongLyrics(const std::optional<SongLyricsDocument> &document) {
  return document.has_value() && (document->text.trim().isNotEmpty() || !document->sections.empty());
}

std::optional<SongLyricsDocument> songLyricsFromVar(const juce::var &value) {
  const auto *object = value.getDynamicObject();
  if (object == nullptr) return std::nullopt;
  const auto schema = object->getProperty("schemaVersion");
  const auto *sections = object->getProperty("sections").getArray();
  if (!(schema.isInt() || schema.isInt64()) || static_cast<juce::int64>(schema) != 1 ||
      !object->getProperty("offsetEncoding").isString() || object->getProperty("offsetEncoding").toString() != "utf16" ||
      !object->getProperty("text").isString() || sections == nullptr || sections->size() > kSongLyricsMaxSections)
    return std::nullopt;
  SongLyricsDocument document;
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
  if (!validWithOffsets(document, offsets)) return std::nullopt;
  sortSections(document.sections);
  return document;
}

juce::var songLyricsToVar(const SongLyricsDocument &document) {
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
  if (!isValidSongLyricsDocument(document)) return false;
  const auto found = std::find_if(document.sections.begin(), document.sections.end(),
                                 [&id](const auto &section) { return section.id == id; });
  if (found == document.sections.end()) return false;
  document.sections.erase(found);
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
  document.text = newText;
  document.sections = std::move(sections);
  sortSections(document.sections);
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
