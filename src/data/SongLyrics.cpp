#include "SongLyrics.h"

#include <algorithm>
#include <set>

namespace {
constexpr int maxBlocks = 256;
constexpr int maxLines = 16384;
constexpr int maxChords = 8192;
constexpr int maxTextCharacters = 262144;

juce::String normalizedText(juce::String text) {
  return text.replace("\r\n", "\n").replace("\r", "\n");
}

std::vector<juce::String> splitLines(const juce::String &text) {
  std::vector<juce::String> result;
  const auto characters = text.toUTF32();
  const int length = text.length();
  int start = 0;
  for (int i = 0; i < length; ++i) {
    if (characters[i] == '\n') {
      result.emplace_back(characters + start, characters + i);
      start = i + 1;
    }
  }
  result.emplace_back(characters + start, characters + length);
  return result;
}

bool validId(const juce::var &value, std::set<juce::String> &seen) {
  if (!value.isString()) return false;
  const auto id = value.toString();
  return id.isNotEmpty() && id.trim() == id && id.length() <= 128 &&
         seen.insert(id).second;
}

bool singleLine(const juce::String &value) {
  return !value.containsChar('\n') && !value.containsChar('\r');
}

int lineAt(const std::vector<int> &starts, const int position) {
  const auto it = std::upper_bound(starts.begin(), starts.end(), position);
  return juce::jmax(0, static_cast<int>(it - starts.begin()) - 1);
}
} // namespace

std::optional<SongLyricsDocument> songLyricsFromVar(const juce::var &value) {
  const auto *root = value.getDynamicObject();
  if (root == nullptr) return std::nullopt;
  const auto schema = root->getProperty("schemaVersion");
  const auto *blocks = root->getProperty("blocks").getArray();
  if (!(schema.isInt() || schema.isInt64()) || static_cast<juce::int64>(schema) != 1 ||
      blocks == nullptr || blocks->size() > maxBlocks) return std::nullopt;
  SongLyricsDocument document;
  std::set<juce::String> ids;
  int lineCount = 0, chordCount = 0, textCount = 0;
  for (const auto &blockValue : *blocks) {
    const auto *object = blockValue.getDynamicObject();
    if (object == nullptr || !validId(object->getProperty("id"), ids) ||
        !object->getProperty("label").isString()) return std::nullopt;
    SongLyricsBlock block;
    block.id = object->getProperty("id").toString();
    block.label = object->getProperty("label").toString();
    if (block.label.length() > 128 || !singleLine(block.label)) return std::nullopt;
    const auto *lines = object->getProperty("lines").getArray();
    if (lines == nullptr || lines->isEmpty() || lines->size() > maxLines - lineCount)
      return std::nullopt;
    lineCount += lines->size();
    for (const auto &lineValue : *lines) {
      const auto *lineObject = lineValue.getDynamicObject();
      if (lineObject == nullptr || !validId(lineObject->getProperty("id"), ids) ||
          !lineObject->getProperty("text").isString()) return std::nullopt;
      SongLyricsLine line;
      line.id = lineObject->getProperty("id").toString();
      line.text = lineObject->getProperty("text").toString();
      const int lineLength = line.text.length();
      if (!singleLine(line.text) || lineLength > maxTextCharacters - textCount)
        return std::nullopt;
      textCount += lineLength;
      const auto *chords = lineObject->getProperty("chords").getArray();
      if (chords == nullptr || chords->size() > maxChords - chordCount)
        return std::nullopt;
      chordCount += chords->size();
      for (const auto &chordValue : *chords) {
        const auto *chordObject = chordValue.getDynamicObject();
        if (chordObject == nullptr || !validId(chordObject->getProperty("id"), ids) ||
            !chordObject->getProperty("symbol").isString()) return std::nullopt;
        const auto position = chordObject->getProperty("position");
        if (!(position.isInt() || position.isInt64())) return std::nullopt;
        const auto offset = static_cast<juce::int64>(position);
        auto symbol = chordObject->getProperty("symbol").toString();
        if (offset < 0 || offset > lineLength || symbol.trim().isEmpty() ||
            symbol.length() > 32 || !singleLine(symbol)) return std::nullopt;
        line.chords.push_back({chordObject->getProperty("id").toString(),
                               static_cast<int>(offset), symbol});
      }
      std::stable_sort(line.chords.begin(), line.chords.end(),
                       [](const auto &a, const auto &b) { return a.position < b.position; });
      block.lines.push_back(std::move(line));
    }
    document.blocks.push_back(std::move(block));
  }
  return document;
}

juce::var songLyricsToVar(const SongLyricsDocument &document) {
  auto *root = new juce::DynamicObject();
  root->setProperty("schemaVersion", document.schemaVersion);
  juce::Array<juce::var> blocks;
  for (const auto &block : document.blocks) {
    auto *blockObject = new juce::DynamicObject();
    blockObject->setProperty("id", block.id);
    blockObject->setProperty("label", block.label);
    juce::Array<juce::var> lines;
    for (const auto &line : block.lines) {
      auto *lineObject = new juce::DynamicObject();
      lineObject->setProperty("id", line.id);
      lineObject->setProperty("text", line.text);
      juce::Array<juce::var> chords;
      auto sortedChords = line.chords;
      std::stable_sort(sortedChords.begin(), sortedChords.end(),
                       [](const auto &a, const auto &b) { return a.position < b.position; });
      for (const auto &chord : sortedChords) {
        auto *chordObject = new juce::DynamicObject();
        chordObject->setProperty("id", chord.id);
        chordObject->setProperty("position", chord.position);
        chordObject->setProperty("symbol", chord.symbol);
        chords.add(juce::var(chordObject));
      }
      lineObject->setProperty("chords", std::move(chords));
      lines.add(juce::var(lineObject));
    }
    blockObject->setProperty("lines", std::move(lines));
    blocks.add(juce::var(blockObject));
  }
  root->setProperty("blocks", std::move(blocks));
  return juce::var(root);
}

bool isValidSongLyricsDocument(const SongLyricsDocument &document) {
  if (document.schemaVersion != 1 || document.blocks.size() > maxBlocks) return false;
  std::set<juce::String> ids;
  const auto uniqueId = [&ids](const juce::String &id) {
    return id.isNotEmpty() && id.trim() == id && id.length() <= 128 && ids.insert(id).second;
  };
  int lineCount = 0, chordCount = 0, textCount = 0;
  for (const auto &block : document.blocks) {
    if (!uniqueId(block.id) || block.label.length() > 128 || !singleLine(block.label) ||
        block.lines.empty() || block.lines.size() > static_cast<size_t>(maxLines - lineCount))
      return false;
    lineCount += static_cast<int>(block.lines.size());
    for (const auto &line : block.lines) {
      const int lineLength = line.text.length();
      if (!uniqueId(line.id) || !singleLine(line.text) || lineLength > maxTextCharacters - textCount ||
          line.chords.size() > static_cast<size_t>(maxChords - chordCount)) return false;
      textCount += lineLength;
      chordCount += static_cast<int>(line.chords.size());
      for (const auto &chord : line.chords)
        if (!uniqueId(chord.id) || chord.position < 0 || chord.position > lineLength ||
            chord.symbol.trim().isEmpty() || chord.symbol.length() > 32 || !singleLine(chord.symbol))
          return false;
    }
  }
  return true;
}

bool hasSongLyrics(const std::optional<SongLyricsDocument> &document) {
  if (!document.has_value()) return false;
  for (const auto &block : document->blocks)
    for (const auto &line : block.lines)
      if (line.text.trim().isNotEmpty() || !line.chords.empty()) return true;
  return false;
}

juce::String lyricsBlockText(const SongLyricsBlock &block) {
  juce::StringArray lines;
  lines.ensureStorageAllocated(static_cast<int>(block.lines.size()));
  for (const auto &line : block.lines) lines.add(line.text);
  return lines.joinIntoString("\n");
}

SongLyricsDocument lyricsFromPlainText(const juce::String &text) {
  SongLyricsDocument document;
  SongLyricsBlock block;
  block.id = juce::Uuid().toString();
  for (const auto &line : splitLines(normalizedText(text)))
    block.lines.push_back({juce::Uuid().toString(), line, {}});
  document.blocks.push_back(std::move(block));
  return document;
}

void replaceLyricsBlockText(SongLyricsBlock &block, const juce::String &text) {
  const auto previous = lyricsBlockText(block);
  const auto replacement = normalizedText(text);
  if (previous == replacement && !block.lines.empty()) return;
  const auto oldCharacters = previous.toUTF32();
  const auto newCharacters = replacement.toUTF32();
  const int oldLength = previous.length(), newLength = replacement.length();
  int prefix = 0;
  while (prefix < oldLength && prefix < newLength &&
         oldCharacters[prefix] == newCharacters[prefix]) ++prefix;
  int suffix = 0;
  while (suffix < oldLength - prefix && suffix < newLength - prefix &&
         oldCharacters[oldLength - suffix - 1] == newCharacters[newLength - suffix - 1])
    ++suffix;
  const int oldEnd = oldLength - suffix;
  const int delta = newLength - oldLength;
  const auto mapPosition = [prefix, oldEnd, delta, newLength](const int position) {
    const int mapped = position < prefix ? position :
                       position >= oldEnd ? position + delta : prefix;
    return juce::jlimit(0, newLength, mapped);
  };
  std::vector<SongLyricsLine> lines;
  std::vector<int> starts;
  int position = 0;
  for (const auto &line : splitLines(replacement)) {
    starts.push_back(position);
    lines.push_back({{}, line, {}});
    position += line.length() + 1;
  }
  int oldStart = 0;
  for (const auto &line : block.lines) {
    const int oldLineLength = line.text.length();
    auto &idTarget = lines[static_cast<size_t>(lineAt(starts, mapPosition(oldStart)))];
    if (idTarget.id.isEmpty()) idTarget.id = line.id;
    for (auto chord : line.chords) {
      const auto mapped = mapPosition(oldStart + juce::jlimit(0, oldLineLength, chord.position));
      const auto targetIndex = lineAt(starts, mapped);
      auto &target = lines[static_cast<size_t>(targetIndex)];
      chord.position = juce::jlimit(0, target.text.length(), mapped - starts[static_cast<size_t>(targetIndex)]);
      target.chords.push_back(std::move(chord));
    }
    oldStart += oldLineLength + 1;
  }
  for (auto &line : lines) {
    if (line.id.isEmpty()) line.id = juce::Uuid().toString();
    std::stable_sort(line.chords.begin(), line.chords.end(),
                     [](const auto &a, const auto &b) { return a.position < b.position; });
  }
  block.lines = std::move(lines);
}
