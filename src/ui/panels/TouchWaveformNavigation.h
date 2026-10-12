#pragma once

#include <cmath>
#include <cstddef>

// Shared, platform-independent decisions for JUCE touch gesture arbitration.
// Coordinates are component-local logical pixels on every supported platform.
namespace touchWaveformNavigation {
constexpr float tapSlop = 6.0f;

inline bool crossedTapSlop(float startX, float startY, float x, float y) {
  const float dx = x - startX;
  const float dy = y - startY;
  return dx * dx + dy * dy >= tapSlop * tapSlop;
}

struct ReleaseState {
  bool cancelled = false;
  bool moved = false;
  bool pinching = false;
  std::size_t pointerCount = 0;
  int primarySource = -1;
  int releasedSource = -1;
  bool withinSlop = false;
  bool insideOriginalTarget = false;
  bool enabled = false;
};

inline bool isValidTap(const ReleaseState &state) {
  return !state.cancelled && !state.moved && !state.pinching &&
         state.pointerCount == 1 && state.primarySource >= 0 &&
         state.primarySource == state.releasedSource && state.withinSlop &&
         state.insideOriginalTarget && state.enabled;
}
} // namespace touchWaveformNavigation
