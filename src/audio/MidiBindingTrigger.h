#pragma once

#include "../data/AppSettings.h"

#include <optional>

namespace playworship::midi {

// Resolves the value that the audio callback should apply for one matching
// binding. A null result means that this message must not trigger its target.
inline std::optional<int> mappedValue(const MidiBindingSetting &binding,
                                      const bool continuousTarget,
                                      const bool noteOn,
                                      const bool noteOff,
                                      const int sourceValue,
                                      int *lastCcValue) noexcept {
  if (binding.mode == MidiBindingMode::Absolute || continuousTarget)
    return sourceValue;

  if (binding.sourceType == MidiBindingSourceType::Note) {
    if (noteOn && (binding.mode == MidiBindingMode::Toggle ||
                   binding.mode == MidiBindingMode::Momentary))
      return 127;
    if (binding.mode == MidiBindingMode::Momentary && noteOff)
      return 0;
    return std::nullopt;
  }

  if (lastCcValue == nullptr)
    return std::nullopt;
  const bool wasHigh = *lastCcValue >= 64;
  const bool isHigh = sourceValue >= 64;
  *lastCcValue = sourceValue;
  if (!wasHigh && isHigh)
    return sourceValue;
  if (binding.mode == MidiBindingMode::Momentary && wasHigh && !isHigh)
    return sourceValue;
  return std::nullopt;
}

} // namespace playworship::midi
