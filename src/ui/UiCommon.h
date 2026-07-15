#pragma once

#include <JuceHeader.h>

enum class ThemePreset { Default = 1, Spotify, Ableton };
enum class ResponsiveLayoutProfile { Compact = 0, Standard, Large };
enum class AppFontWeight { Regular = 0, Medium, SemiBold, Bold };

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
juce::Font makeAppFont(float size, AppFontWeight weight = AppFontWeight::Regular);

enum class IconGlyph {
  ArrowLeft,
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
  Search,
  Clock,
  MusicNote,
  Waveform,
  More,
  Add,
  Check,
  Remove,
  Metronome,
  /** Cuadrado (maximizar ventana). */
  WindowMaximize,
  /** Dos rectangulos superpuestos (restaurar ventana). */
  WindowRestore
};

enum class SvgIconName {
  SkipBack,
  Rewind,
  Play,
  Pause,
  Square,
  FastForward,
  SkipForward,
  FadeIn,
  FadeOut,
  Settings,
  Search,
  Plus,
  Check,
  Minus,
  MoreVertical,
  Clock,
  Music,
  Midi,
  AudioWaveform,
  Metronome,
  Maximize,
  Copy,
  Repeat,
  CornerDownRight,
  ImageEdit,
  Pencil,
  VolumeX,
  FileAudio
};

void drawIconGlyph(juce::Graphics &g, juce::Rectangle<float> area,
                   IconGlyph glyph, juce::Colour colour, float stroke = 1.7f);
bool drawSvgIcon(juce::Graphics &g, juce::Rectangle<float> area,
                 SvgIconName icon, juce::Colour colour);
void drawStudioBackground(juce::Graphics &g, juce::Rectangle<float> area);

/** Busca la ventana JUCE contenedora (Standalone / host) y alterna maximizado. */
void toggleTopLevelWindowMaximised(juce::Component *fromComponent);
/** `true` si la ventana contenedora esta maximizada (o no hay ventana). */
bool isTopLevelWindowMaximised(juce::Component *fromComponent);
/** Expande la ventana al area de trabajo del monitor principal (guarda tamano previo). */
void expandTopLevelWindowToPrimaryWorkArea(juce::Component *fromComponent);
