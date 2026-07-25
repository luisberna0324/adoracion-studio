#pragma once

#include "DynamicGuideEngine.h"

#include <array>

namespace playworship::audio {

enum class DynamicGuideAnnouncementTiming {
  atSectionEntry,
  oneBarBeforeSection,
};

inline juce::String
arrangementCountSectionKey(const juce::String &partName) {
  const auto key = DynamicGuideEngine::normalizeSectionKey(partName);
  if (key.isEmpty())
    return {};

  bool allDigits = true;
  for (int i = 0; i < key.length(); ++i) {
    if (!juce::CharacterFunctions::isDigit(key[i])) {
      allDigits = false;
      break;
    }
  }
  if (allDigits)
    return key;

  if (key == "one" || key == "uno")
    return "1";
  if (key == "two" || key == "dos")
    return "2";
  if (key == "three" || key == "tres")
    return "3";
  if (key == "four" || key == "cuatro")
    return "4";
  if (key == "five" || key == "cinco")
    return "5";
  if (key == "six" || key == "seis")
    return "6";
  if (key == "seven" || key == "siete")
    return "7";
  return {};
}

inline bool isDynamicGuideUtilityCueKey(const juce::String &rawKey) {
  const auto key = DynamicGuideEngine::normalizeSectionKey(rawKey);
  static constexpr std::array<const char *, 25> utilityCueKeys = {{
      "a capella", "ad lib", "all", "all in", "bass", "big", "big ending",
      "break", "build", "drums", "drums in", "guitar", "hits", "hold", "key",
      "key change down", "key change up", "keys", "last", "last time",
      "slowly", "slowly build", "softly", "swell", "worship freely"}};
  for (const auto *candidate : utilityCueKeys) {
    if (key == candidate)
      return true;
  }
  return false;
}

inline DynamicGuideAnnouncementTiming dynamicGuideAnnouncementTiming(
    const juce::String &sectionKey, const juce::String &sectionName) {
  // Counts and utility cues are immediate instructions. Musical sections are
  // announced ahead of their downbeat so the band can prepare.
  if (arrangementCountSectionKey(sectionKey).isNotEmpty() ||
      arrangementCountSectionKey(sectionName).isNotEmpty() ||
      isDynamicGuideUtilityCueKey(sectionKey) ||
      isDynamicGuideUtilityCueKey(sectionName)) {
    return DynamicGuideAnnouncementTiming::atSectionEntry;
  }
  return DynamicGuideAnnouncementTiming::oneBarBeforeSection;
}

} // namespace playworship::audio
