#pragma once

#include <JuceHeader.h>
#include "../platform/NativeIpadChoicePicker.h"

enum class ThemePreset { Default = 1, Spotify, Ableton };
enum class ResponsiveLayoutProfile { Compact = 0, Standard, Large };
enum class AppFontWeight { Regular = 0, Medium, SemiBold, Bold };

enum class ActionPopoverPresentation {
  Automatic = 0,
  Anchored,
  CompactAnchored
};

struct ActionPopoverOptions {
  ActionPopoverPresentation presentation =
      ActionPopoverPresentation::Automatic;
  int compactMaximumWidth = 560;
  int edgeInset = 12;
};

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

namespace AppColours {
inline const juce::Colour background{0xff0D1013};
inline const juce::Colour card{0xff1C1E24};
inline const juce::Colour circleButton{0xff2A2C32};
inline const juce::Colour separator{0x14FFFFFF};
inline const juce::Colour textPrimary{0xffF2F2F2};
inline const juce::Colour textSecondary{0xff8A8D93};
inline const juce::Colour textSubtitle{0xff7A7D83};
inline const juce::Colour accentBrand{0xffFF7A00};
inline const juce::Colour accentAction{0xff25D366};
inline const juce::Colour accentSuccess{0xff6DDE8A};
inline const juce::Colour accentDestructive{0xffB23B3B};
} // namespace AppColours

juce::String formatTimeSeconds(double seconds);
void applyTheme(ThemePreset preset);
juce::Font makeAppFont(float size, AppFontWeight weight = AppFontWeight::Regular);

class IpadNativeComboBox final : public juce::ComboBox {
public:
  using juce::ComboBox::ComboBox;

  void showPopup() override;
};

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
  Close,
  Metronome,
  /** Cuadrado (maximizar ventana). */
  WindowMaximize,
  /** Dos rectangulos superpuestos (restaurar ventana). */
  WindowRestore
};

enum class SvgIconName {
  ArrowLeft,
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
  Trash,
  VolumeX,
  FileAudio
};

void drawIconGlyph(juce::Graphics &g, juce::Rectangle<float> area,
                   IconGlyph glyph, juce::Colour colour, float stroke = 1.7f);
bool drawSvgIcon(juce::Graphics &g, juce::Rectangle<float> area,
                 SvgIconName icon, juce::Colour colour);
juce::LookAndFeel &getActionCalloutLookAndFeel();
juce::CallOutBox &presentActionPopover(
    std::unique_ptr<juce::Component> content,
    juce::Rectangle<int> anchor,
    juce::Component *parentComponent,
    ActionPopoverOptions options = {});
void dismissActiveActionPopover();
void drawStudioBackground(juce::Graphics &g, juce::Rectangle<float> area);
bool useTouchOptimisedControls();
bool isPhoneSizedTouchLayout(const juce::Component &component);
int touchOptimisedControlHeight();
int touchOptimisedPopupItemHeight();
void applyComboBoxTheme(IpadNativeComboBox &combo,
                        juce::Colour background = UITheme::cardBgHover,
                        juce::Colour outline =
                            UITheme::cardBorder.withAlpha(0.7f));
void applyTextEditorTheme(juce::TextEditor &editor,
                          juce::Colour background = UITheme::cardBgHover,
                          juce::Colour outline =
                              UITheme::cardBorder.withAlpha(0.7f));
void applyToggleSwitchTheme(juce::ToggleButton &toggle);
juce::PopupMenu::Options makeTouchPopupMenuOptions(juce::Component *target);

/**
 * Botón circular reutilizable para el header de paneles (header icons).
 * Soporta dos modos visuales:
 *  - Estilo "highlight sutil" para los iconos Music/ArrowLeft (fondo claro, borde
 *    blanco a baja opacidad, gradiente suave).
 *  - Estilo "accent" para acciones primarias (tinte de UITheme::accent).
 * También permite pintar una etiqueta SVG opcional junto al icono.
 */
class HeaderIconButton final : public juce::Button {
public:
  HeaderIconButton(const juce::String &name, IconGlyph glyphToUse);
  void paintButton(juce::Graphics &g, bool isMouseOverButton,
                   bool isButtonDown) override;
  void setAccent(bool shouldAccent);
  void setSubtleHighlight(bool shouldHighlight);
  void setSvgLabel(SvgIconName iconToUse, juce::String textToUse);

private:
  IconGlyph glyph;
  SvgIconName svgIcon = SvgIconName::Music;
  juce::String labelText;
  bool accent = false;
  bool forceSubtleHighlight = false;
  bool useSvgLabel = false;
};

/** Busca la ventana JUCE contenedora (Standalone / host) y alterna maximizado. */
void toggleTopLevelWindowMaximised(juce::Component *fromComponent);
/** `true` si la ventana contenedora esta maximizada (o no hay ventana). */
bool isTopLevelWindowMaximised(juce::Component *fromComponent);
/** Expande la ventana al area de trabajo del monitor principal (guarda tamano previo). */
void expandTopLevelWindowToPrimaryWorkArea(juce::Component *fromComponent);
