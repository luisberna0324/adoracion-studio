#pragma once

#include "../UiCommon.h"

// Shared insertion pointer used by the guide waveform and independent lyrics.
// Placing the pointer only chooses a time; the caller owns the subsequent edit.
struct TimelineInsertionAnchor {
  bool valid = false;
  double seconds = 0.0;

  void place(double time) noexcept { seconds = time; valid = true; }
  void clear() noexcept { valid = false; seconds = 0.0; }

  static juce::Rectangle<float> addButtonBounds(float x, float centreY) {
    constexpr float size = 28.0f;
    return juce::Rectangle<float>(size, size).withCentre({x + size * 0.5f, centreY});
  }

  static void paint(juce::Graphics &g, float x, float top, float bottom) {
    const float dashes[] = {4.0f, 3.0f};
    g.setColour(juce::Colours::black.withAlpha(0.42f));
    g.drawLine(x + 1.0f, top, x + 1.0f, bottom, 2.4f);
    g.setColour(UITheme::accentSoft.withAlpha(0.92f));
    g.drawDashedLine(juce::Line<float>(x, top, x, bottom), dashes, 2, 1.35f);
  }
};
