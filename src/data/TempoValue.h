#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

// BPM text uses either decimal separator; JSON and engine values stay numeric.
namespace TempoValue {
inline bool parseBpm(const juce::String &text, double &bpm) {
  const auto value = text.trim();
  bool hasDigit = false;
  bool hasSeparator = false;
  for (const auto character : value) {
    if (character >= '0' && character <= '9') {
      hasDigit = true;
    } else if ((character == '.' || character == ',') && !hasSeparator) {
      hasSeparator = true;
    } else {
      return false;
    }
  }
  if (!hasDigit)
    return false;
  const double parsed = value.replaceCharacter(',', '.').getDoubleValue();
  if (!std::isfinite(parsed) || parsed <= 0.0)
    return false;
  bpm = parsed;
  return true;
}

inline juce::String formatBpm(double bpm) {
  if (!std::isfinite(bpm))
    return {};
  auto text = juce::String(bpm, 6);
  while (text.endsWithChar('0'))
    text = text.dropLastCharacters(1);
  if (text.endsWithChar('.'))
    text = text.dropLastCharacters(1);
  return text;
}
} // namespace TempoValue
