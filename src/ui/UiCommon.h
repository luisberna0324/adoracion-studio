#pragma once

#include <JuceHeader.h>

enum class ThemePreset { Default = 1, Spotify, Ableton };

struct UITheme {
  static juce::Colour appBg;
  static juce::Colour appBgTop;
  static juce::Colour appBgBottom;
  static juce::Colour panelDark;
  static juce::Colour cardBg;
  static juce::Colour cardBgHover;
  static juce::Colour cardBgPressed;
  static juce::Colour cardBorder;
  static juce::Colour textPrimary;
  static juce::Colour textSecondary;
  static juce::Colour accent;
  static juce::Colour accentSoft;
  static juce::Colour accentMuted;
  static juce::Colour accentMaster;
  static juce::Colour cardBgMaster;
  static juce::Colour cardBgClickGuide;
  static juce::Colour onAccent;
  static juce::Colour dividerSoft;
};

juce::String formatTimeSeconds(double seconds);
void applyTheme(ThemePreset preset);

enum class IconGlyph {
  Prev,
  SeekBack,
  Play,
  Pause,
  Stop,
  SeekForward,
  Next,
  Loop,
  FadeOut,
  FadeIn,
  Pencil,
  Settings,
  More,
  Add,
  Metronome
};

void drawIconGlyph(juce::Graphics &g, juce::Rectangle<float> area,
                   IconGlyph glyph, juce::Colour colour, float stroke = 1.7f);
void drawStudioBackground(juce::Graphics &g, juce::Rectangle<float> area);
