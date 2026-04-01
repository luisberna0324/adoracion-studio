#pragma once

#include "AppSettings.h"
#include <JuceHeader.h>

namespace L10n {
inline bool isEnglish() {
  return AppSettings::getLanguage() == AppLanguage::English;
}

inline juce::String normalizeSpanishAscii(const juce::String &text) {
  juce::String out(text);
  out = out.replaceCharacters("áéíóúÁÉÍÓÚñÑüÜ¿¡", "aeiouAEIOUnNuU??");
  out = out.replaceCharacters("àèìòùÀÈÌÒÙ", "aeiouAEIOU");
  out = out.replaceCharacters("äëïöüÄËÏÖÜ", "aeiouAEIOU");
  return out;
}

inline juce::String tr(const juce::String &es, const juce::String &en) {
  return isEnglish() ? en : normalizeSpanishAscii(es);
}
} // namespace L10n
