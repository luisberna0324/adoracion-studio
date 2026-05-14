#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>

class BottomStatusBar final : public juce::Component {
public:
  BottomStatusBar();

  void setResourceUsage(double cpuUsage01, double memoryUsage01,
                        juce::String cpuText, juce::String memoryText);
  void setResponsiveProfile(ResponsiveLayoutProfile profile);
  void refreshThemeColours();

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  struct MeterState {
    juce::String title;
    juce::String valueText;
    double value01 = 0.0;
    juce::Rectangle<float> bounds;
    juce::Rectangle<float> labelBounds;
    juce::Rectangle<float> barBounds;
    juce::Rectangle<float> valueBounds;
  };

  ResponsiveLayoutProfile responsiveProfile = ResponsiveLayoutProfile::Standard;
  MeterState cpuMeter;
  MeterState memoryMeter;
  juce::Rectangle<float> compactStripBounds;

  void drawMeter(juce::Graphics &g, const MeterState &meter,
                 juce::Colour accent) const;
  void updateTitlesFromLanguage();
};
