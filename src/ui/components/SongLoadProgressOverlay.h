#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>

/** Panel modal ligero con barra de progreso durante la carga de una canción. */
class SongLoadProgressOverlay : public juce::Component {
public:
  SongLoadProgressOverlay();

  void setProgress(double progressZeroToOne);
  void setPhaseText(const juce::String &text);
  /** Si no se llama, se usa el titulo por defecto de carga de cancion. */
  void setTitleText(const juce::String &text);
  void refreshThemeColours();

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  juce::Rectangle<int> cardBounds() const;
  double progressValue = 0.0;
  juce::ProgressBar progressBar;
  juce::Label titleLabel;
  juce::Label phaseLabel;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongLoadProgressOverlay)
};
