#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

// A wall-clock pulse keeps the edit target visible while transport is paused
// and gives the waveform, strips and Pads the same animation phase.
inline float sectionAutomationHighlightPulse() {
  const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
  return static_cast<float>(0.5 + 0.5 * std::sin(
      seconds * juce::MathConstants<double>::twoPi * 1.2 -
      juce::MathConstants<double>::halfPi));
}
