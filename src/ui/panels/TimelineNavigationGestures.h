#pragma once

#include <algorithm>
#include <cmath>

// Preserve the existing waveform gesture contract. Modifier and OS direction
// flags are deliberately not reinterpreted; JUCE already supplies the deltas.
namespace timelineNavigation {
constexpr float maximumZoom = 18.0f;
constexpr float minimumSpan = 1.0f / maximumZoom;
enum class WheelAction { none, pan, zoom };
struct WheelGesture {
  WheelAction action = WheelAction::none;
  float value = 0.0f;
};
inline WheelGesture fromWheel(float dx, float dy) {
  const float absDx = std::abs(dx), absDy = std::abs(dy);
  if (absDx > 0.0f && absDx > absDy * 1.05f) {
    return {WheelAction::pan, dx};
  }
  const float dir = dy != 0.0f ? dy : dx;
  if (dir == 0.0f) return {};
  const float intensity = std::clamp(1.0f + std::abs(dir) * 2.4f, 1.0f, 3.5f);
  const float step = 1.06f * intensity;
  return {WheelAction::zoom, dir > 0.0f ? step : 1.0f / step};
}
inline float panDelta(float dx, float span) {
  const float intensity = std::clamp(1.0f + std::abs(dx) * 2.0f, 1.0f, 3.0f);
  // Retain the waveform's multiplication order as well as its sensitivity.
  return -dx * intensity * std::max(minimumSpan, span) * 0.22f;
}
inline float magnifyFactor(float amount) { return 1.0f + amount; }
struct Window { float start = 0.0f, span = 1.0f; };
inline Window constrainWindow(float start, float span) {
  span = std::clamp(span, minimumSpan, 1.0f);
  start = std::clamp(start, 0.0f, 1.0f);
  if (start + span > 1.0f) start = 1.0f - span;
  return {start, span};
}
inline Window zoomWindow(Window window, float factor, float anchor) {
  anchor = std::clamp(anchor, 0.0f, 1.0f);
  const float span = std::clamp(window.span / factor, minimumSpan, 1.0f);
  const float source = window.start + anchor * window.span;
  return constrainWindow(source - anchor * span, span);
}
} // namespace timelineNavigation
