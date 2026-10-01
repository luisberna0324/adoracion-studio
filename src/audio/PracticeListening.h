#pragma once

namespace playworship::audio::practiceListening {
// Transient audition gains, independent from the song's saved mix.
constexpr float focusedGain = 1.2589254f; // +2 dB
constexpr float reducedGain = 0.2511886f; // -12 dB
constexpr double rampSeconds = 0.2;

inline float gainFor(const int mode, const bool selected,
                     const bool clickGuide) noexcept {
  if (clickGuide)
    return 1.0f;
  if (mode == 1)
    return selected ? focusedGain : reducedGain;
  if (mode == 2 && selected)
    return reducedGain;
  return 1.0f;
}
} // namespace playworship::audio::practiceListening
