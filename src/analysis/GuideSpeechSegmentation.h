#pragma once

#include "GuideTranscription.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <vector>

namespace playworship::analysis {

struct GuideSpeechWindow {
  size_t beginSample = 0, endSample = 0;
  size_t speechBeginSample = 0, speechEndSample = 0;
};

struct GuideCompoundCount {
  juce::String sectionPrefix;
  int prefixWordCount = 0;
  std::vector<int> counts;
};

// Whisper may assign zero duration to a lexical subtoken. A word is useful
// only when at least one of its subtokens has a positive source interval;
// zero-duration pieces contribute spelling and confidence, never extra time.
class GuideWordTimingCollector {
public:
  GuideWordTimingCollector(double sourceBegin, double sourceEnd)
      : begin(sourceBegin), end(sourceEnd), valid(std::isfinite(begin) &&
          std::isfinite(end) && begin >= 0.0 && end > begin) {}

  void append(const juce::String &text, double start, double finish, double logProbability) {
    if (!valid || text.isEmpty()) return;
    bool lexical = false;
    for (auto character : text)
      lexical = lexical || juce::CharacterFunctions::isLetterOrDigit(character);
    if (!lexical) {
      if (!words.empty()) words.back().text += text;
      if (text.containsAnyOf(",;:.!? \t\r\n")) newWord = true;
      return;
    }
    if (!std::isfinite(start) || !std::isfinite(finish) || finish < start ||
        start < begin || finish > end || !std::isfinite(logProbability) || logProbability > 0.0) {
      valid = false; return;
    }
    if (newWord || words.empty() || juce::CharacterFunctions::isWhitespace(text[0])) {
      if ((!words.empty() && !hasPositiveRange) || words.size() >= 128) { valid = false; return; }
      words.push_back({text, 0.0, 0.0, {}});
      hasPositiveRange = false; logSum = 0.0; tokens = 0;
    } else words.back().text += text;
    if (finish > start) {
      if ((hasPositiveRange && (start < lastPositiveStart || finish < words.back().endSeconds)) ||
          (!hasPositiveRange && words.size() > 1 && start <= words[words.size() - 2].startSeconds)) {
        valid = false; return;
      }
      if (!hasPositiveRange) words.back().startSeconds = start;
      words.back().endSeconds = finish;
      lastPositiveStart = start; hasPositiveRange = true;
    }
    logSum += std::clamp(logProbability, -100.0, 0.0); ++tokens;
    words.back().confidence = std::exp(logSum / tokens);
    newWord = juce::String(",;:.!?").containsChar(text[text.length() - 1]);
  }

  std::vector<GuideTranscriptWord> takeWords() {
    if (!valid || !hasPositiveRange) return {};
    for (auto &word : words) word.text = word.text.trim();
    return std::move(words);
  }

private:
  double begin, end, logSum = 0.0, lastPositiveStart = 0.0;
  int tokens = 0;
  bool valid = true, newWord = true, hasPositiveRange = false;
  std::vector<GuideTranscriptWord> words;
};

// Only counts literally present in a complete phrase qualify. An unnumbered
// catalogue prefix may introduce them; numbered Verse/Chorus variants remain
// ambiguous rather than borrowing an occurrence number as a count.
inline std::optional<GuideCompoundCount> guideCompoundCount(const juce::String &text) {
  const auto normalized = playworship::guide::normalizedWords(text);
  const auto words = juce::StringArray::fromTokens(normalized, " ", "");
  if (words.size() < 2 || words.size() > 16) return {};
  for (int prefixSize = 0; prefixSize <= words.size() - 2; ++prefixSize) {
    GuideCompoundCount result;
    if (prefixSize > 0) {
      result.sectionPrefix = words.joinIntoString(" ", 0, prefixSize);
      const auto *definition = playworship::guide::findSectionDefinition(result.sectionPrefix);
      if (!definition || definition->numberedVariantLimit != 0) continue;
      result.prefixWordCount = prefixSize;
    }
    bool individualCounts = true;
    for (int i = prefixSize; i < words.size(); ++i) {
      const auto count = normalizeGuideCountCue(0.0, words[i]);
      if (!count || count->name.length() != 1 || count->name[0] < '1' || count->name[0] > '7') {
        individualCounts = false; break;
      }
      result.counts.push_back(int(count->name[0] - '0'));
    }
    if (individualCounts && result.counts.size() >= 2 && result.counts.size() <= 8) return result;
  }
  return {};
}

// A whole catalogue section label, optionally with one spoken ordinal. The
// general label parser also accepts prefixes and utility suffixes; those must
// not masquerade as atomic parts when classifying an explicit compound.
inline bool isExactGuideSectionDirective(const juce::String &text) {
  const auto cue = normalizeGuideCue(0.0, text);
  if (!cue) return false;
  const auto normalized = playworship::guide::normalizedWords(text);
  if (playworship::guide::findSectionDefinition(normalized)) return true;
  if (!cue->spokenOrdinal) return false;
  const auto *definition = playworship::guide::findSectionDefinition(cue->normalizedType);
  if (!definition) return false;
  const auto ordinalOnly = [](const juce::String &words) {
    const auto tokens = juce::StringArray::fromTokens(words, " ", "");
    return tokens.size() == 1 || (tokens.size() == 2 &&
        (tokens[0] == "numero" || tokens[0] == "number"));
  };
  const auto matches = [&](const juce::String &label) {
    const auto alias = playworship::guide::normalizedWords(label);
    return (normalized.startsWith(alias + " ") && ordinalOnly(normalized.substring(alias.length() + 1))) ||
        (normalized.endsWith(" " + alias) && ordinalOnly(normalized.dropLastCharacters(alias.length() + 1)));
  };
  if (matches(definition->key)) return true;
  for (const auto *alias : definition->aliases) if (matches(alias)) return true;
  return false;
}

struct GuideCompoundDirectives {
  juce::StringArray phrases;
};

// Commas or semicolons can separate independently spoken section/instrument
// announcements. Only complete, exact catalogue parts qualify, with at least
// one utility and no more than one musical section. This is not a parser for
// narration, presumed song structure or arbitrary lists of section names.
inline std::optional<GuideCompoundDirectives> guideCompoundDirectives(const juce::String &text) {
  if (text.length() > 512 || (!text.containsChar(',') && !text.containsChar(';'))) return {};
  if (playworship::guide::isKnownUtilityInstruction(text)) return {};
  juce::StringArray parts;
  int start = 0;
  for (int i = 0; i <= text.length(); ++i) {
    if (i != text.length() && text[i] != ',' && text[i] != ';') continue;
    const auto part = text.substring(start, i).trim();
    if (part.isEmpty() || parts.size() >= 16) return {};
    parts.add(part); start = i + 1;
  }
  if (parts.size() < 2) return {};
  GuideCompoundDirectives result;
  int sections = 0, utilities = 0;
  for (int i = 0; i < parts.size(); ++i) {
    auto phrase = parts[i];
    if (playworship::guide::isKnownUtilityInstruction(phrase)) {
      ++utilities;
    } else {
      // Whisper commonly punctuates an ordinal as "Verso, dos". Keep that
      // one section together; it is not a second instruction or a count-in.
      if (i + 1 < parts.size() && isExactGuideSectionDirective(phrase + ", " + parts[i + 1]))
        phrase += ", " + parts[++i];
      if (!isExactGuideSectionDirective(phrase) || ++sections > 1) return {};
    }
    result.phrases.add(std::move(phrase));
    if (result.phrases.size() > 8) return {};
  }
  if (utilities == 0 || result.phrases.size() < 2) return {};
  return result;
}

inline bool sameGuideDirectiveMeaning(const juce::String &expected, const juce::String &actual) {
  if (isExactGuideSectionDirective(expected)) {
    if (!isExactGuideSectionDirective(actual)) return false;
    const auto first = normalizeGuideCue(0.0, expected), second = normalizeGuideCue(0.0, actual);
    return first && second && first->normalizedType == second->normalizedType &&
        first->spokenOrdinal == second->spokenOrdinal;
  }
  if (!playworship::guide::isKnownUtilityInstruction(expected) ||
      !playworship::guide::isKnownUtilityInstruction(actual)) return false;
  const auto first = normalizeGuideUtilityCue(0.0, expected), second = normalizeGuideUtilityCue(0.0, actual);
  return first && second && playworship::guide::normalizedWords(first->name) ==
      playworship::guide::normalizedWords(second->name);
}

// A retry only: source silence supplies the boundaries, never the number of
// decoded words or a presumed beat. Normal multiword directives are decoded
// intact before this helper is considered. Context may overlap silence only.
inline std::vector<GuideSpeechWindow> guideSpeechRetryWindowsAtGap(
    const std::vector<float> &samples, size_t minimumGapFrames) {
  constexpr size_t frame = 160; // 10 ms, at the transcriber's fixed 16 kHz.
  constexpr size_t minimumActiveFrames = 8;
  constexpr size_t maximumChildren = 8, context = 3200;
  if (minimumGapFrames < 6 || minimumGapFrames > 30) return {};
  struct Region { size_t begin, end, active; };
  std::vector<Region> regions;
  size_t quietFrames = 0;
  for (size_t begin = 0; begin < samples.size(); begin += frame) {
    const auto end = std::min(samples.size(), begin + frame);
    double energy = 0.0; float peak = 0.0f;
    for (size_t i = begin; i < end; ++i) {
      if (!std::isfinite(samples[i])) return {};
      energy += double(samples[i]) * samples[i];
      peak = std::max(peak, std::abs(samples[i]));
    }
    const bool active = std::sqrt(energy / double(end - begin)) >= 0.0025 && peak >= 0.006f;
    if (!active) { ++quietFrames; continue; }
    if (regions.empty() || quietFrames >= minimumGapFrames) {
      if (regions.size() == maximumChildren) return {};
      regions.push_back({begin, end, 1});
    } else { regions.back().end = end; ++regions.back().active; }
    quietFrames = 0;
  }
  if (regions.size() < 2) return {};
  std::vector<GuideSpeechWindow> windows;
  for (size_t i = 0; i < regions.size(); ++i) {
    const auto &region = regions[i];
    if (region.active < minimumActiveFrames) return {};
    const auto previousEnd = i == 0 ? size_t(0) : regions[i - 1].end;
    const auto nextBegin = i + 1 == regions.size() ? samples.size() : regions[i + 1].begin;
    windows.push_back({std::max(previousEnd, region.begin > context ? region.begin - context : size_t(0)),
        std::min(nextBegin, region.end + context), region.begin, region.end});
  }
  return windows;
}

inline std::vector<GuideSpeechWindow> guideSpeechRetryWindows(
    const std::vector<float> &samples) {
  return guideSpeechRetryWindowsAtGap(samples, 8);
}

// A catalogue phrase such as "All In" may contain its own measured silence.
// Enumerate only complete contiguous groupings of the observed regions; each
// grouping still needs independent decoder evidence for every parent part.
// Eight regions imply at most 35 partitions for one requested part count.
inline std::vector<std::vector<GuideSpeechWindow>> guideSpeechRetryPartitions(
    const std::vector<GuideSpeechWindow> &regions, size_t parts) {
  if (parts < 2 || regions.size() < parts || regions.size() > 8) return {};
  for (size_t i = 0; i < regions.size(); ++i) {
    const auto &region = regions[i];
    if (region.beginSample > region.speechBeginSample ||
        region.speechBeginSample >= region.speechEndSample ||
        region.speechEndSample > region.endSample ||
        (i > 0 && (regions[i - 1].speechEndSample > region.beginSample ||
                   regions[i - 1].endSample > region.speechBeginSample))) return {};
  }
  std::vector<std::vector<GuideSpeechWindow>> result;
  std::vector<GuideSpeechWindow> current;
  const std::function<void(size_t, size_t)> visit = [&](size_t first, size_t remaining) {
    const auto maximumLast = regions.size() - remaining;
    for (size_t last = first; last <= maximumLast; ++last) {
      if (remaining == 1 && last + 1 != regions.size()) continue;
      current.push_back({regions[first].beginSample, regions[last].endSample,
                        regions[first].speechBeginSample, regions[last].speechEndSample});
      if (remaining == 1) result.push_back(current);
      else visit(last + 1, remaining - 1);
      current.pop_back();
    }
  };
  visit(0, parts);
  return result;
}

inline bool isSingleGuideDirective(const GuideTranscriptSegment &segment) {
  if (guideCompoundDirectives(segment.text)) return false;
  if (normalizeGuideCue(0.0, segment.text) || normalizeGuideUtilityCue(0.0, segment.text)) return true;
  const auto count = normalizeGuideCountCue(0.0, segment.text);
  return count && count->name.length() == 1 && count->name[0] >= '1' && count->name[0] <= '7';
}

// All children must have independent decoder evidence and one known meaning;
// the caller retains the original whole result if any one child fails.
inline bool isSupportedGuideRetryChild(const GuideTranscriptSegment &segment) {
  if (!isSingleGuideDirective(segment) || !segment.confidence ||
      !std::isfinite(*segment.confidence) || *segment.confidence < std::exp(-1.2) ||
      *segment.confidence > 1.0 || !std::isfinite(segment.startSeconds) ||
      !std::isfinite(segment.endSeconds) || segment.startSeconds < 0.0 ||
      segment.endSeconds <= segment.startSeconds) return false;
  for (const auto &candidate : segment.alternatives)
    if (candidate.text == segment.text && candidate.language == segment.language &&
        std::isfinite(candidate.avgLogProbability) && candidate.avgLogProbability >= -1.2 &&
        candidate.avgLogProbability <= 0.0 && std::isfinite(candidate.noSpeechProbability) && candidate.noSpeechProbability >= 0.0 &&
        candidate.noSpeechProbability <= 0.4) return true;
  return false;
}

struct GuideSpeechRetryResult {
  bool completed = true, ambiguous = false, exhausted = false;
  std::vector<GuideTranscriptSegment> children;
};

// No child is published until every candidate decomposition has been checked.
// The decoder callback receives one PCM window and a monotone attempt index;
// false means cancellation/backend failure, not an unsupported hypothesis.
template <typename Decode>
inline GuideSpeechRetryResult guideUniqueSpeechRetry(
    const std::vector<std::vector<GuideSpeechWindow>> &partitions,
    const juce::StringArray &phrases, Decode decode, size_t maximumDecodes = 16) {
  GuideSpeechRetryResult result;
  if (maximumDecodes == 0 || maximumDecodes > 16) { result.exhausted = true; return result; }
  if (phrases.size() < 2 || phrases.size() > 8) return result;
  std::map<std::pair<size_t, size_t>, std::optional<GuideTranscriptSegment>> decoded;
  std::optional<std::vector<GuideTranscriptSegment>> unique;
  for (const auto &partition : partitions) {
    if (partition.size() != size_t(phrases.size())) return result;
    std::vector<GuideTranscriptSegment> candidate;
    bool valid = true;
    for (size_t i = 0; valid && i < partition.size(); ++i) {
      const auto &window = partition[i];
      const auto key = std::make_pair(window.beginSample, window.endSample);
      auto found = decoded.find(key);
      if (found == decoded.end()) {
        if (decoded.size() == maximumDecodes) { result.exhausted = true; return result; }
        std::optional<GuideTranscriptSegment> child;
        if (!decode(window, decoded.size(), child)) { result.completed = false; return result; }
        found = decoded.emplace(key, std::move(child)).first;
      }
      const auto &child = found->second;
      valid = child && isSupportedGuideRetryChild(*child) && sameGuideDirectiveMeaning(phrases[int(i)], child->text);
      if (valid) candidate.push_back(*child);
    }
    if (!valid) continue;
    if (unique) { result.ambiguous = true; return result; }
    unique = std::move(candidate);
  }
  if (unique) result.children = std::move(*unique);
  return result;
}

} // namespace playworship::analysis
