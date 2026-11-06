#pragma once

#include "../data/GuideVocabulary.h"
#include <algorithm>
#include <set>
#include <vector>

namespace playworship::analysis {

// Derived only from the application catalogue. No song-specific spelling,
// expected announcement order, timing or test manifest enters this list.
inline std::vector<juce::String> guideCatalogueUtterances() {
  std::set<juce::String> phrases;
  const auto add = [&](const juce::String &text) {
    const auto normalized = playworship::guide::normalizedWords(text);
    if (normalized.isNotEmpty()) phrases.insert(normalized);
  };
  const char *english[]{"one", "two", "three", "four", "five", "six", "seven"};
  const char *spanish[]{"uno", "dos", "tres", "cuatro", "cinco", "seis", "siete"};
  for (const auto &entry : playworship::guide::sectionDefinitions()) {
    std::vector<juce::String> labels{entry.key, entry.spanishLabel, entry.englishLabel};
    for (const auto *alias : entry.aliases) labels.emplace_back(alias);
    for (const auto &label : labels) {
      add(label);
      for (int ordinal = 1; ordinal <= entry.numberedVariantLimit; ++ordinal) {
        add(label + " " + juce::String(ordinal));
        if (ordinal <= 7) {
          add(label + " " + english[ordinal - 1]);
          add(label + " " + spanish[ordinal - 1]);
        }
      }
    }
  }
  for (const auto &entry : playworship::guide::utilityDefinitions()) {
    add(entry.key); add(entry.spanishLabel); add(entry.englishLabel);
    for (const auto *alias : entry.aliases) add(alias);
  }
  for (const auto *alias : playworship::guide::literalUtilityAliases()) add(alias);
  for (int number = 1; number <= 7; ++number) {
    add(juce::String(number)); add(english[number - 1]); add(spanish[number - 1]);
  }
  return {phrases.begin(), phrases.end()};
}

inline bool isGuideCatalogueUtterance(const juce::String &text) {
  static const auto phrases = guideCatalogueUtterances();
  const auto normalized = playworship::guide::normalizedWords(text);
  return std::binary_search(phrases.begin(), phrases.end(), normalized);
}

#if defined(PLAYWORSHIP_WHISPER_GUIDE) && PLAYWORSHIP_WHISPER_GUIDE
// whisper.h must be included by the provider before this header. Build the
// grammar elements directly rather than depending on upstream example parsers.
class GuideCatalogueGrammar {
public:
  GuideCatalogueGrammar() : rules(4) {
    rules[0] = {{WHISPER_GRETYPE_RULE_REF, 1}, {WHISPER_GRETYPE_RULE_REF, 2},
                {WHISPER_GRETYPE_RULE_REF, 1}, {WHISPER_GRETYPE_RULE_REF, 3},
                {WHISPER_GRETYPE_RULE_REF, 1}, {WHISPER_GRETYPE_END, 0}};
    rules[1] = {{WHISPER_GRETYPE_CHAR, ' '}, {WHISPER_GRETYPE_ALT, 0}, {WHISPER_GRETYPE_END, 0}};
    bool first = true;
    for (const auto &phrase : guideCatalogueUtterances()) {
      if (!first) rules[2].push_back({WHISPER_GRETYPE_ALT, 0});
      first = false;
      for (auto character : phrase) {
        rules[2].push_back({WHISPER_GRETYPE_CHAR, uint32_t(character)});
        const auto upper = juce::CharacterFunctions::toUpperCase(character);
        if (upper != character) rules[2].push_back({WHISPER_GRETYPE_CHAR_ALT, uint32_t(upper)});
      }
    }
    rules[2].push_back({WHISPER_GRETYPE_END, 0});
    rules[3] = {{WHISPER_GRETYPE_CHAR, '.'}, {WHISPER_GRETYPE_ALT, 0},
                {WHISPER_GRETYPE_CHAR, '!'}, {WHISPER_GRETYPE_ALT, 0},
                {WHISPER_GRETYPE_CHAR, '?'}, {WHISPER_GRETYPE_ALT, 0}, {WHISPER_GRETYPE_END, 0}};
    for (const auto &rule : rules) pointers.push_back(rule.data());
  }
  const whisper_grammar_element **rulesData() noexcept { return pointers.data(); }
  size_t size() const noexcept { return pointers.size(); }
private:
  std::vector<std::vector<whisper_grammar_element>> rules;
  std::vector<const whisper_grammar_element *> pointers;
};
#endif

} // namespace playworship::analysis
