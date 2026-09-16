#pragma once

#include <algorithm>
#include <cmath>

namespace playworship::ui {
struct WaveformCacheWindow {
  double start = 0.0;
  double span = 0.0;
  int width = 0;
};

// Cache columns share a song-anchored two-pixel grid. Rebuilding ahead of
// the viewport must not resample every peak at a new phase or scale.
inline WaveformCacheWindow makeWaveformCacheWindow(
    double windowStart, double windowSpan, double viewportWidth) {
  if (!std::isfinite(windowStart) || !std::isfinite(windowSpan) ||
      !std::isfinite(viewportWidth) || windowSpan <= 0.0 ||
      windowSpan > 1.0 || viewportWidth <= 2.0 || viewportWidth > 32768.0)
    return {};
  windowStart = std::clamp(windowStart, 0.0, 1.0 - windowSpan);
  const double ratioPerPixel = windowSpan / viewportWidth;
  const double overscan = std::min(2.0, 1.0 / windowSpan);
  // Extra columns cover fractional viewport widths and origin alignment.
  const int width = static_cast<int>(std::ceil(viewportWidth * overscan / 2.0)) * 2 + 4;
  const double span = width * ratioPerPixel;
  const double columnSpan = 2.0 * ratioPerPixel;
  const double lastStart = std::max(0.0, std::ceil((1.0 - span) / columnSpan) * columnSpan);
  // Keep most spare pixels ahead of forward playback, while retaining some
  // history for camera smoothing and short backward movements.
  const double desired = windowStart - (span - windowSpan) * 0.25;
  const double start = std::floor(std::clamp(desired, 0.0, lastStart) / columnSpan) * columnSpan;
  return {start, span, width};
}
} // namespace playworship::ui
