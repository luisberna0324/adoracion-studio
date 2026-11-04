#pragma once

#include "PracticeWheelSelector.h"
#include "../../data/Localization.h"
#include "../../data/TempoValue.h"

class PracticeTempoSelector final : public PracticeWheelSelector {
public:
  static constexpr float tempoSpacing = itemSpacing;

  PracticeTempoSelector() {
    configureRange(90.0, 240.0, 0.0);
    setSessionValue(originalTempo);
    cancelDrag();
    setTitle(L10n::tr("Tempo de la canción", "Song tempo"));
    setDescription(L10n::tr("Arrastra para cambiar el tempo. Doble toque para restaurar el original.",
                            "Drag to change tempo. Double-tap to restore the original."));
    setTooltip(getDescription());
  }

  void setOriginalTempo(double bpm) {
    const double next = std::isfinite(bpm) && bpm > 0.0 ? juce::jmax(20.0, bpm) : 120.0;
    if (juce::approximatelyEqual(next, originalTempo))
      return;
    originalTempo = next;
    configureRange(juce::jmax(20.0, next * 0.75), juce::jmin(300.0, next * 2.0), 0.0);
    repaint();
  }

  void setSessionTempo(double bpm) { setSessionValue(bpm); }

  float getItemSpacing() const override {
    return juce::jmax(tempoSpacing,
        12.0f + 8.5f * static_cast<float>(TempoValue::formatBpm(getValue()).length()));
  }

  juce::String getTextFromValue(double value) override {
    return TempoValue::formatBpm(value) + " BPM";
  }

private:
  double originalValue() const override { return originalTempo; }

  // Preserve fractional BPM from song metadata and remote state. A step is
  // one BPM relative to the current value, without rounding the engine state.
  double detentOrigin() const override { return getValue() - std::floor(getValue()); }

  juce::String itemText(double value) override {
    return TempoValue::formatBpm(value);
  }

  double originalTempo = 120.0;
};
