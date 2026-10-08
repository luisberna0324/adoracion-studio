#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace liveLoopBorder {
inline float perimeter(juce::Rectangle<float> r) { return 2.0f * (r.getWidth() + r.getHeight()); }
inline float period(juce::Rectangle<float> r) {
  const float length = perimeter(r);
  return length / static_cast<float>(juce::jmax(1, static_cast<int>(std::round(length / 7.0f))));
}
inline float phase(juce::Rectangle<float> r, double nowMs, bool reducedMotion) {
  return reducedMotion || perimeter(r) <= 0.0f ? 0.0f
      : static_cast<float>(std::fmod(nowMs * 0.006, period(r)));
}
inline juce::Point<float> point(juce::Rectangle<float> r, float distance) {
  if (distance <= r.getWidth()) return {r.getX() + distance, r.getY()};
  distance -= r.getWidth();
  if (distance <= r.getHeight()) return {r.getRight(), r.getY() + distance};
  distance -= r.getHeight();
  if (distance <= r.getWidth()) return {r.getRight() - distance, r.getBottom()};
  return {r.getX(), r.getBottom() - (distance - r.getWidth())};
}
// A single distance domain crosses all four corners. Fit whole dash periods
// around the closed perimeter so the seam has the same spacing as every edge.
// Splitting at corners emits bounded line segments without allocating paths.
template <typename Emit>
void segments(juce::Rectangle<float> r, float offset, Emit emit) {
  if (r.getWidth() <= 0.0f || r.getHeight() <= 0.0f) return;
  const float length = perimeter(r), cycle = period(r);
  const float edges[] = {0.0f, r.getWidth(), r.getWidth() + r.getHeight(),
                         2.0f * r.getWidth() + r.getHeight(), length};
  for (float start = offset - cycle; start < length; start += cycle) {
    const float end = start + cycle * (4.0f / 7.0f);
    for (int edge = 0; edge < 4; ++edge) {
      const float a = juce::jmax(start, edges[edge]);
      const float b = juce::jmin(end, edges[edge + 1]);
      if (b > a) emit(juce::Line<float>(point(r, a), point(r, b)));
    }
  }
}
} // namespace liveLoopBorder
