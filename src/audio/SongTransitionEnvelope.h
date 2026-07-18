#pragma once

#include <algorithm>
#include <cmath>

namespace playworship::audio {

struct SongTransitionGainRamp {
  float startGain = 1.0f;
  float endGain = 1.0f;
  bool reachedTarget = true;
};

inline SongTransitionGainRamp
advanceSongTransitionGain(const float currentGain, const float targetGain,
                          const int numSamples, const double sampleRate,
                          const double durationSeconds) noexcept {
  SongTransitionGainRamp result;
  result.startGain = std::clamp(currentGain, 0.0f, 1.0f);
  const float target = std::clamp(targetGain, 0.0f, 1.0f);

  if (numSamples <= 0) {
    result.endGain = result.startGain;
    result.reachedTarget = std::abs(result.startGain - target) <= 1.0e-6f;
    return result;
  }

  if (!std::isfinite(sampleRate) || sampleRate <= 0.0 ||
      !std::isfinite(durationSeconds) || durationSeconds <= 0.0) {
    result.endGain = target;
    result.reachedTarget = true;
    return result;
  }

  const float maxDelta = static_cast<float>(
      static_cast<double>(numSamples) / (sampleRate * durationSeconds));
  if (target < result.startGain)
    result.endGain = std::max(target, result.startGain - maxDelta);
  else
    result.endGain = std::min(target, result.startGain + maxDelta);

  result.reachedTarget = std::abs(result.endGain - target) <= 1.0e-6f;
  if (result.reachedTarget)
    result.endGain = target;
  return result;
}

} // namespace playworship::audio
