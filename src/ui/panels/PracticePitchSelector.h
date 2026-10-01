#pragma once

#include "PracticeWheelSelector.h"
#include "../../audio/PadsEngine.h"
#include "../../data/Localization.h"

class PracticePitchSelector final : public PracticeWheelSelector {
public:
  static constexpr float noteSpacing = itemSpacing;

  PracticePitchSelector() {
    configureRange(-12.0, 12.0, 1.0);
    setTitle(L10n::tr("Tono de la canción", "Song key"));
    setDescription(L10n::tr("Arrastra para cambiar el tono. Doble toque para restaurar el original.",
                            "Drag to change key. Double-tap to restore the original."));
    setTooltip(getDescription());
  }

  void setOriginalKey(const juce::String &key) {
    if (originalKey == key)
      return;
    cancelDrag();
    originalKey = key;
    baseNote = PadsEngine::noteIndexForSongKey(key);
    auto token = key.trim().replace("KEY OF ", "", true);
    if (token.isNotEmpty()) {
      token = token.substring(1);
      if (token.startsWithChar('#') || token.startsWithChar('b') ||
          token.startsWith(juce::String::fromUTF8("♯")) ||
          token.startsWith(juce::String::fromUTF8("♭")))
        token = token.substring(1);
    }
    token = token.trim();
    minor = token == "m" || token.equalsIgnoreCase("min") ||
            token.equalsIgnoreCase("minor") || token.equalsIgnoreCase("menor");
    repaint();
  }

  juce::String getTextFromValue(double value) override {
    const int semitones = juce::jlimit(-12, 12, juce::roundToInt(value));
    if (baseNote < 0)
      return semitoneText(semitones);
    return juce::String(PadsEngine::kNoteNames[(baseNote + semitones + 24) % 12]) +
           (minor ? "m" : "");
  }

  void setSessionPitch(int semitones) { setSessionValue(semitones); }

private:
  double originalValue() const override { return 0.0; }

  juce::String itemText(double value) override {
    const int semitones = juce::roundToInt(value);
    return baseNote < 0 ? juce::String(semitones > 0 ? "+" : "") + juce::String(semitones)
                        : getTextFromValue(value);
  }

  static juce::String semitoneText(int value) {
    return value == 0 ? L10n::tr("Original", "Original")
                     : (value > 0 ? juce::String("+") : juce::String{}) + juce::String(value) + " st";
  }

  juce::String originalKey;
  int baseNote = -1;
  bool minor = false;
};
