#pragma once

#include "AppSettings.h"
#include <JuceHeader.h>

namespace L10n {
inline bool isEnglish() {
  return AppSettings::getLanguage() == AppLanguage::English;
}

inline juce::String tr(const juce::String &es, const juce::String &en) {
  return isEnglish() ? en : es;
}

inline juce::String tr(const char* es, const char* en) {
  return isEnglish() ? juce::String::fromUTF8(en) : juce::String::fromUTF8(es);
}
} // namespace L10n
