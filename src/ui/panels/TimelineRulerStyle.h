#pragma once

#include "../UiCommon.h"
#include "PracticeTimelineRuler.h"

// Shared visual reference for Practice and the desktop/tablet waveform. The
// caller owns musical marks, viewport and interaction; painting never seeks.
inline void drawMusicalTimelineRuler(
    juce::Graphics &g, const juce::Rectangle<float> &bounds,
    const juce::Range<double> visible, const double songLength,
    const std::vector<PracticeTimelineMark> &marks) {
  if (bounds.isEmpty() || !std::isfinite(visible.getLength()) ||
      visible.getLength() <= 0.0)
    return;

  const juce::Graphics::ScopedSaveState saved(g);
  g.reduceClipRegion(bounds.toNearestInt());
  g.setFont(makeAppFont(10.0f));
  for (const auto &mark : marks) {
    if (!std::isfinite(mark.timeSeconds) || mark.timeSeconds < 0.0 ||
        mark.timeSeconds > songLength || mark.timeSeconds < visible.getStart() ||
        mark.timeSeconds > visible.getEnd())
      continue;
    const auto x = bounds.getX() + static_cast<float>(
        (mark.timeSeconds - visible.getStart()) * bounds.getWidth() /
        visible.getLength());
    g.setColour(UITheme::textSecondary.withMultipliedAlpha(mark.strong ? 0.7f : 0.3f));
    g.drawVerticalLine(juce::roundToInt(x),
                       bounds.getBottom() - (mark.strong ? 7.0f : 3.0f),
                       bounds.getBottom());
    if (mark.label.isNotEmpty()) {
      auto labelBounds = juce::Rectangle<float>(x - 25.0f, bounds.getY(), 50.0f, 14.0f);
      auto alignment = juce::Justification::centred;
      // Keep first/last bar numbers readable at the viewport edges.
      if (labelBounds.getX() < bounds.getX()) {
        labelBounds.setX(bounds.getX() + 2.0f);
        alignment = juce::Justification::centredLeft;
      } else if (labelBounds.getRight() > bounds.getRight()) {
        labelBounds.setRight(bounds.getRight() - 2.0f);
        alignment = juce::Justification::centredRight;
      }
      g.drawText(mark.label, labelBounds, alignment, true);
    }
  }
}
