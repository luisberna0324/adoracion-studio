#include "UiCommon.h"
#include "BinaryData.h"

#include <cmath>
#include <map>

juce::Colour UITheme::appBg = juce::Colour::fromRGB(50, 46, 43); // #322E2B
juce::Colour UITheme::appBgTop = juce::Colour::fromRGB(56, 52, 49); // #383431
juce::Colour UITheme::appBgBottom = juce::Colour::fromRGB(70, 65, 61); // #46413D
juce::Colour UITheme::panelDark = juce::Colour::fromRGB(38, 34, 32); // #262220
juce::Colour UITheme::cardBg = juce::Colour::fromRGB(212, 212, 212);
juce::Colour UITheme::cardBgHover = juce::Colour::fromRGB(218, 218, 218);
juce::Colour UITheme::cardBgPressed = juce::Colour::fromRGB(201, 201, 201);
juce::Colour UITheme::cardBorder = juce::Colour::fromRGB(48, 48, 48);
juce::Colour UITheme::textPrimary = juce::Colour::fromRGB(78, 78, 78);
juce::Colour UITheme::textSecondary = juce::Colour::fromRGB(118, 118, 118);
juce::Colour UITheme::accent = juce::Colour::fromRGB(193, 122, 90);
juce::Colour UITheme::accentSoft = juce::Colour::fromRGB(196, 154, 108);
juce::Colour UITheme::accentMuted = juce::Colour::fromRGB(181, 124, 124);
juce::Colour UITheme::accentMaster = juce::Colour::fromRGB(163, 108, 74);
juce::Colour UITheme::cardBgMaster =
    juce::Colour::fromRGB(232, 216, 200); // #E8D8C8
juce::Colour UITheme::cardBgClickGuide =
    juce::Colour::fromRGB(200, 205, 214); // #C8CDD6
juce::Colour UITheme::onAccent = juce::Colours::white;
juce::Colour UITheme::dividerSoft = juce::Colour::fromRGB(42, 42, 42);

juce::String formatTimeSeconds(double seconds) {
  const int total = juce::jmax(0, static_cast<int>(std::floor(seconds + 0.5)));
  const int mm = total / 60;
  const int ss = total % 60;
  return juce::String(mm).paddedLeft('0', 2) + ":" +
         juce::String(ss).paddedLeft('0', 2);
}

void applyTheme(ThemePreset preset) {
  switch (preset) {
  case ThemePreset::Spotify:
    UITheme::appBg = juce::Colour::fromRGB(30, 30, 30); // #1E1E1E
    UITheme::appBgTop = juce::Colour::fromRGB(36, 31, 31); // #241F1F
    UITheme::appBgBottom = juce::Colour::fromRGB(42, 42, 42); // #2A2A2A
    UITheme::panelDark = juce::Colour::fromRGB(24, 24, 24); // #181818
    UITheme::cardBg = juce::Colour::fromRGB(38, 38, 38); // #262626
    UITheme::cardBgHover = juce::Colour::fromRGB(50, 50, 50); // #323232
    UITheme::cardBgPressed = juce::Colour::fromRGB(60, 60, 60); // #3C3C3C
    UITheme::cardBorder = juce::Colour::fromRGB(48, 48, 48); // #303030
    UITheme::textPrimary = juce::Colour::fromRGB(245, 245, 245);
    UITheme::textSecondary = juce::Colour::fromRGB(190, 190, 190);
    UITheme::accent = juce::Colour::fromRGB(29, 185, 84); // #1DB954
    UITheme::accentSoft = juce::Colour::fromRGB(30, 215, 96); // #1ED760
    UITheme::accentMuted = juce::Colour::fromRGB(102, 150, 102);
    UITheme::accentMaster = juce::Colour::fromRGB(29, 185, 84);
    UITheme::cardBgMaster = juce::Colour::fromRGB(44, 44, 44); // #2C2C2C
    UITheme::cardBgClickGuide = juce::Colour::fromRGB(52, 52, 52); // #343434
    UITheme::onAccent = juce::Colours::black;
    UITheme::dividerSoft = juce::Colour::fromRGB(42, 42, 42); // #2A2A2A
    break;
  case ThemePreset::Ableton:
    UITheme::appBg = juce::Colour::fromRGB(36, 36, 36); // #242424
    UITheme::appBgTop = juce::Colour::fromRGB(42, 42, 42); // #2A2A2A
    UITheme::appBgBottom = juce::Colour::fromRGB(52, 52, 52); // #343434
    UITheme::panelDark = juce::Colour::fromRGB(30, 30, 30); // #1E1E1E
    UITheme::cardBg = juce::Colour::fromRGB(52, 52, 52); // #343434
    UITheme::cardBgHover = juce::Colour::fromRGB(66, 66, 66); // #424242
    UITheme::cardBgPressed = juce::Colour::fromRGB(76, 76, 76); // #4C4C4C
    UITheme::cardBorder = juce::Colour::fromRGB(48, 48, 48); // #303030
    UITheme::textPrimary = juce::Colour::fromRGB(235, 235, 235);
    UITheme::textSecondary = juce::Colour::fromRGB(184, 184, 184);
    UITheme::accent = juce::Colour::fromRGB(255, 179, 71); // warm
    UITheme::accentSoft = juce::Colour::fromRGB(255, 198, 112); // warm soft
    UITheme::accentMuted = juce::Colour::fromRGB(192, 152, 108);
    UITheme::accentMaster = juce::Colour::fromRGB(255, 179, 71);
    UITheme::cardBgMaster = juce::Colour::fromRGB(58, 58, 58); // #3A3A3A
    UITheme::cardBgClickGuide = juce::Colour::fromRGB(64, 64, 64); // #404040
    UITheme::onAccent = juce::Colours::black;
    UITheme::dividerSoft = juce::Colour::fromRGB(42, 42, 42); // #2A2A2A
    break;
  case ThemePreset::Default:
  default:
    // Legacy alias: "Classic" is deprecated and now resolves to Night Green.
    applyTheme(ThemePreset::Spotify);
    break;
  }
}

namespace {
juce::Typeface::Ptr createTypeface(const void *data, const int sizeBytes) {
  return juce::Typeface::createSystemTypefaceFor(data,
                                                  static_cast<size_t>(sizeBytes));
}

juce::Typeface::Ptr getInterTypeface(const AppFontWeight weight) {
  static auto regular =
      createTypeface(BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
  static auto medium =
      createTypeface(BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
  static auto semiBold = createTypeface(BinaryData::InterSemiBold_ttf,
                                        BinaryData::InterSemiBold_ttfSize);
  static auto bold =
      createTypeface(BinaryData::InterBold_ttf, BinaryData::InterBold_ttfSize);

  switch (weight) {
  case AppFontWeight::Medium:
    return medium;
  case AppFontWeight::SemiBold:
    return semiBold;
  case AppFontWeight::Bold:
    return bold;
  case AppFontWeight::Regular:
  default:
    return regular;
  }
}

juce::String fallbackStyleForWeight(const AppFontWeight weight) {
  switch (weight) {
  case AppFontWeight::Medium:
    return "Medium";
  case AppFontWeight::SemiBold:
    return "SemiBold";
  case AppFontWeight::Bold:
    return "Bold";
  case AppFontWeight::Regular:
  default:
    return {};
  }
}
} // namespace

juce::Font makeAppFont(const float size, const AppFontWeight weight) {
  if (const auto typeface = getInterTypeface(weight); typeface != nullptr)
    return juce::Font(juce::FontOptions(size).withTypeface(typeface));

  auto options = juce::FontOptions(size);
  const auto style = fallbackStyleForWeight(weight);
  if (style.isNotEmpty())
    options = options.withStyle(style);
  return juce::Font(options);
}

namespace {
class RoundedComboLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  void drawComboBox(juce::Graphics &g, const int width, const int height,
                    const bool isButtonDown, const int, const int, const int,
                    const int, juce::ComboBox &box) override {
    auto bounds = juce::Rectangle<float>(0.5f, 0.5f,
                                         static_cast<float>(width) - 1.0f,
                                         static_cast<float>(height) - 1.0f);
    auto fill = box.findColour(juce::ComboBox::backgroundColourId);
    if (isButtonDown)
      fill = fill.brighter(0.08f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 8.0f);
    const auto arrowColour = box.findColour(juce::ComboBox::arrowColourId);
    const auto arrowArea = juce::Rectangle<float>(
        static_cast<float>(width - 24), static_cast<float>(height) * 0.5f - 4.0f,
        12.0f, 8.0f);
    juce::Path arrow;
    arrow.startNewSubPath(arrowArea.getX() + 1.0f, arrowArea.getCentreY() - 1.5f);
    arrow.lineTo(arrowArea.getCentreX(), arrowArea.getCentreY() + 2.0f);
    arrow.lineTo(arrowArea.getRight() - 1.0f, arrowArea.getCentreY() - 1.5f);
    g.setColour(arrowColour);
    g.strokePath(arrow, juce::PathStrokeType(1.8f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
  }

  juce::Font getComboBoxFont(juce::ComboBox &) override {
    return makeAppFont(useTouchOptimisedControls() ? 17.0f : 15.0f,
                       AppFontWeight::Medium);
  }

  void positionComboBoxText(juce::ComboBox &box, juce::Label &label) override {
    label.setBounds(12, 1, juce::jmax(1, box.getWidth() - box.getHeight() - 18),
                    juce::jmax(1, box.getHeight() - 2));
    label.setFont(getComboBoxFont(box));
  }

  juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(
      juce::ComboBox &box, juce::Label &label) override {
    return juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label)
        .withMinimumWidth(box.getWidth())
        .withStandardItemHeight(useTouchOptimisedControls() ? 50 : 38);
  }
};

RoundedComboLookAndFeel &getRoundedComboLookAndFeel() {
  static RoundedComboLookAndFeel lookAndFeel;
  return lookAndFeel;
}
} // namespace

namespace {
class SwitchLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button,
                        const bool highlighted, const bool pressed) override {
    const float trackW = useTouchOptimisedControls() ? 48.0f : 42.0f;
    const float trackH = useTouchOptimisedControls() ? 28.0f : 24.0f;
    const float x = 4.0f;
    const float y = (static_cast<float>(button.getHeight()) - trackH) * 0.5f;
    auto track = juce::Rectangle<float>(x, y, trackW, trackH);
    auto trackColour = button.getToggleState() ? UITheme::accent
                                                : UITheme::cardBgPressed;
    if (pressed || highlighted)
      trackColour = trackColour.brighter(0.08f);

    g.setColour(trackColour);
    g.fillRoundedRectangle(track, trackH * 0.5f);
    const float thumbSize = trackH - 4.0f;
    const float thumbX = button.getToggleState()
                             ? track.getRight() - thumbSize - 2.0f
                             : track.getX() + 2.0f;
    g.setColour(UITheme::textPrimary.brighter(0.35f));
    g.fillEllipse(thumbX, track.getY() + 2.0f, thumbSize, thumbSize);

    g.setColour(button.findColour(juce::ToggleButton::textColourId));
    g.setFont(makeAppFont(useTouchOptimisedControls() ? 14.0f : 13.0f,
                          AppFontWeight::Medium));
    auto textArea = button.getLocalBounds().withTrimmedLeft(
        juce::roundToInt(track.getRight()) + 12);
    g.drawFittedText(button.getButtonText(), textArea,
                     juce::Justification::centredLeft, 2);
  }
};

SwitchLookAndFeel &getSwitchLookAndFeel() {
  static SwitchLookAndFeel lookAndFeel;
  return lookAndFeel;
}
} // namespace

bool useTouchOptimisedControls() {
#if JUCE_IOS || JUCE_ANDROID
  return true;
#else
  return false;
#endif
}

int touchOptimisedControlHeight() {
  return useTouchOptimisedControls() ? 44 : 30;
}

int touchOptimisedPopupItemHeight() {
  return useTouchOptimisedControls() ? 40 : 28;
}

void applyComboBoxTheme(juce::ComboBox &combo, const juce::Colour background,
                        const juce::Colour) {
  combo.setLookAndFeel(&getRoundedComboLookAndFeel());
  combo.setColour(juce::ComboBox::backgroundColourId,
                  background.withAlpha(useTouchOptimisedControls() ? 0.96f
                                                                    : 0.88f));
  combo.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
  combo.setColour(juce::ComboBox::outlineColourId,
                  juce::Colours::transparentBlack);
  combo.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
  combo.setJustificationType(juce::Justification::centredLeft);
}

void applyTextEditorTheme(juce::TextEditor &editor,
                          const juce::Colour background,
                          const juce::Colour outline) {
  editor.setMultiLine(false);
  editor.setReturnKeyStartsNewLine(false);
  editor.setScrollbarsShown(false);
  editor.setPopupMenuEnabled(true);
  editor.setJustification(juce::Justification::centredLeft);
  editor.setColour(juce::TextEditor::backgroundColourId, background);
  editor.setColour(juce::TextEditor::textColourId, UITheme::textPrimary);
  editor.setColour(juce::TextEditor::highlightedTextColourId,
                   UITheme::textPrimary);
  editor.setColour(juce::TextEditor::outlineColourId, outline);
  editor.setColour(juce::TextEditor::focusedOutlineColourId,
                   UITheme::accent.withAlpha(useTouchOptimisedControls() ? 0.82f
                                                                         : 0.64f));
  editor.setIndents(useTouchOptimisedControls() ? 12 : 8, 0);
}

void applyToggleSwitchTheme(juce::ToggleButton &toggle) {
  toggle.setLookAndFeel(&getSwitchLookAndFeel());
  toggle.setColour(juce::ToggleButton::textColourId, UITheme::textPrimary);
  toggle.setColour(juce::ToggleButton::tickColourId, UITheme::accent);
  toggle.setColour(juce::ToggleButton::tickDisabledColourId,
                   UITheme::cardBgPressed);
}

juce::PopupMenu::Options makeTouchPopupMenuOptions(juce::Component *target) {
  auto options = juce::PopupMenu::Options()
                     .withStandardItemHeight(touchOptimisedPopupItemHeight());
  if (target != nullptr)
    options = options.withTargetComponent(target);
  return options;
}

namespace {
struct SvgIconData {
  const void *data = nullptr;
  int size = 0;
};

SvgIconData getSvgIconData(const SvgIconName icon) {
  switch (icon) {
  case SvgIconName::SkipBack:
    return {BinaryData::skip_back_svg, BinaryData::skip_back_svgSize};
  case SvgIconName::Rewind:
    return {BinaryData::rewind_svg, BinaryData::rewind_svgSize};
  case SvgIconName::Play:
    return {BinaryData::play_svg, BinaryData::play_svgSize};
  case SvgIconName::Pause:
    return {BinaryData::pause_svg, BinaryData::pause_svgSize};
  case SvgIconName::Square:
    return {BinaryData::square_svg, BinaryData::square_svgSize};
  case SvgIconName::FastForward:
    return {BinaryData::fast_forward_svg, BinaryData::fast_forward_svgSize};
  case SvgIconName::SkipForward:
    return {BinaryData::skip_forward_svg, BinaryData::skip_forward_svgSize};
  case SvgIconName::FadeIn:
    return {BinaryData::fade_in_svg, BinaryData::fade_in_svgSize};
  case SvgIconName::FadeOut:
    return {BinaryData::fade_out_svg, BinaryData::fade_out_svgSize};
  case SvgIconName::Settings:
    return {BinaryData::settings_svg, BinaryData::settings_svgSize};
  case SvgIconName::Search:
    return {BinaryData::search_svg, BinaryData::search_svgSize};
  case SvgIconName::Plus:
    return {BinaryData::plus_svg, BinaryData::plus_svgSize};
  case SvgIconName::Check:
    return {BinaryData::check_svg, BinaryData::check_svgSize};
  case SvgIconName::Minus:
    return {BinaryData::minus_svg, BinaryData::minus_svgSize};
  case SvgIconName::MoreVertical:
    return {BinaryData::more_vertical_svg, BinaryData::more_vertical_svgSize};
  case SvgIconName::Clock:
    return {BinaryData::clock_svg, BinaryData::clock_svgSize};
  case SvgIconName::Music:
    return {BinaryData::music_svg, BinaryData::music_svgSize};
  case SvgIconName::Midi:
    return {BinaryData::midi_svg, BinaryData::midi_svgSize};
  case SvgIconName::AudioWaveform:
    return {BinaryData::audio_waveform_svg, BinaryData::audio_waveform_svgSize};
  case SvgIconName::Metronome:
    return {BinaryData::metronome_svg, BinaryData::metronome_svgSize};
  case SvgIconName::Maximize:
    return {BinaryData::maximize_svg, BinaryData::maximize_svgSize};
  case SvgIconName::Copy:
    return {BinaryData::copy_svg, BinaryData::copy_svgSize};
  case SvgIconName::Repeat:
    return {BinaryData::repeat_svg, BinaryData::repeat_svgSize};
  case SvgIconName::CornerDownRight:
    return {BinaryData::corner_down_right_svg, BinaryData::corner_down_right_svgSize};
  case SvgIconName::ImageEdit:
    return {BinaryData::image_edit_svg, BinaryData::image_edit_svgSize};
  case SvgIconName::Pencil:
    return {BinaryData::pencil_svg, BinaryData::pencil_svgSize};
  case SvgIconName::VolumeX:
    return {BinaryData::volume_x_svg, BinaryData::volume_x_svgSize};
  case SvgIconName::FileAudio:
    return {BinaryData::file_audio_svg, BinaryData::file_audio_svgSize};
  }
  return {};
}
} // namespace

bool drawSvgIcon(juce::Graphics &g, juce::Rectangle<float> area,
                 const SvgIconName icon, const juce::Colour colour) {
  using CacheKey = std::pair<int, juce::uint32>;
  static std::map<CacheKey, std::unique_ptr<juce::Drawable>> iconCache;

  const CacheKey key{static_cast<int>(icon), colour.getARGB()};
  auto it = iconCache.find(key);
  if (it == iconCache.end()) {
    const auto data = getSvgIconData(icon);
    if (data.data == nullptr || data.size <= 0)
      return false;

    auto drawable =
        juce::Drawable::createFromImageData(data.data,
                                            static_cast<size_t>(data.size));
    if (drawable == nullptr)
      return false;

    drawable->replaceColour(juce::Colours::black, colour);
    it = iconCache.emplace(key, std::move(drawable)).first;
  }

  if (it->second == nullptr)
    return false;

  it->second->drawWithin(g, area, juce::RectanglePlacement::centred, 1.0f);
  return true;
}

void drawIconGlyph(juce::Graphics &g, juce::Rectangle<float> area,
                   IconGlyph glyph, juce::Colour colour, float stroke) {
  juce::ignoreUnused(stroke);
  g.setColour(colour);
  area = area.reduced(area.getWidth() * 0.17f, area.getHeight() * 0.17f);

  switch (glyph) {
  case IconGlyph::ArrowLeft: {
    const auto a = area.expanded(area.getWidth() * 0.12f, area.getHeight() * 0.12f);
    const float midY = a.getCentreY();
    const float leftX = a.getX() + a.getWidth() * 0.26f;
    const float rightX = a.getRight() - a.getWidth() * 0.12f;
    juce::Path chevron;
    chevron.startNewSubPath(rightX, a.getY() + a.getHeight() * 0.14f);
    chevron.lineTo(leftX, midY);
    chevron.lineTo(rightX, a.getBottom() - a.getHeight() * 0.14f);
    g.strokePath(chevron, juce::PathStrokeType(
                              2.9f, juce::PathStrokeType::curved,
                              juce::PathStrokeType::rounded));
    break;
  }
  case IconGlyph::Play: {
    juce::Path p;
    p.startNewSubPath(area.getX() + 1.4f, area.getY() + 0.8f);
    p.lineTo(area.getRight() - 0.8f, area.getCentreY());
    p.lineTo(area.getX() + 1.4f, area.getBottom() - 0.8f);
    p.closeSubPath();
    g.fillPath(p);
    break;
  }
  case IconGlyph::Pause: {
    const float w = area.getWidth();
    const float h = area.getHeight();
    g.fillRoundedRectangle(area.getX() + (w * 0.20f), area.getY() + 0.4f, w * 0.24f, h - 0.8f,
                           1.4f);
    g.fillRoundedRectangle(area.getRight() - (w * 0.44f), area.getY() + 0.4f, w * 0.24f,
                           h - 0.8f, 1.4f);
    break;
  }
  case IconGlyph::Stop: {
    g.fillRoundedRectangle(area.withSizeKeepingCentre(area.getWidth() * 0.72f,
                                                      area.getHeight() * 0.72f),
                           2.4f);
    break;
  }
  case IconGlyph::Prev: {
    g.fillRoundedRectangle(area.getX(), area.getY(), 2.2f, area.getHeight(), 1.1f);
    juce::Path p;
    p.startNewSubPath(area.getRight() - 0.8f, area.getY() + 0.8f);
    p.lineTo(area.getRight() - 0.8f, area.getBottom() - 0.8f);
    p.lineTo(area.getX() + 3.5f, area.getCentreY());
    p.closeSubPath();
    g.fillPath(p);
    break;
  }
  case IconGlyph::SeekBack: {
    g.fillRoundedRectangle(area.getX(), area.getY(), 2.0f, area.getHeight(), 1.0f);
    juce::Path p1;
    p1.startNewSubPath(area.getCentreX() + 1.0f, area.getY() + 0.9f);
    p1.lineTo(area.getCentreX() + 1.0f, area.getBottom() - 0.9f);
    p1.lineTo(area.getX() + 4.5f, area.getCentreY());
    p1.closeSubPath();
    g.fillPath(p1);
    juce::Path p2;
    p2.startNewSubPath(area.getRight() - 0.8f, area.getY() + 0.9f);
    p2.lineTo(area.getRight() - 0.8f, area.getBottom() - 0.9f);
    p2.lineTo(area.getCentreX() + 2.2f, area.getCentreY());
    p2.closeSubPath();
    g.fillPath(p2);
    break;
  }
  case IconGlyph::Next: {
    g.fillRoundedRectangle(area.getRight() - 2.2f, area.getY(), 2.2f, area.getHeight(), 1.1f);
    juce::Path p;
    p.startNewSubPath(area.getX() + 0.8f, area.getY() + 0.8f);
    p.lineTo(area.getX() + 0.8f, area.getBottom() - 0.8f);
    p.lineTo(area.getRight() - 3.5f, area.getCentreY());
    p.closeSubPath();
    g.fillPath(p);
    break;
  }
  case IconGlyph::SeekForward: {
    g.fillRoundedRectangle(area.getRight() - 2.0f, area.getY(), 2.0f, area.getHeight(), 1.0f);
    juce::Path p1;
    p1.startNewSubPath(area.getX() + 0.8f, area.getY() + 0.9f);
    p1.lineTo(area.getX() + 0.8f, area.getBottom() - 0.9f);
    p1.lineTo(area.getCentreX() - 2.2f, area.getCentreY());
    p1.closeSubPath();
    g.fillPath(p1);
    juce::Path p2;
    p2.startNewSubPath(area.getCentreX() - 1.0f, area.getY() + 0.9f);
    p2.lineTo(area.getCentreX() - 1.0f, area.getBottom() - 0.9f);
    p2.lineTo(area.getRight() - 4.5f, area.getCentreY());
    p2.closeSubPath();
    g.fillPath(p2);
    break;
  }
  case IconGlyph::Loop: {
    const auto ring = area.withSizeKeepingCentre(area.getWidth() * 0.70f,
                                                 area.getHeight() * 0.70f);
    g.fillEllipse(ring);
    g.setColour(UITheme::panelDark.withAlpha(0.88f));
    g.fillEllipse(ring.reduced(ring.getWidth() * 0.26f, ring.getHeight() * 0.26f));
    g.setColour(colour);
    juce::Path arrow;
    const float tx = ring.getRight();
    const float ty = ring.getCentreY() - ring.getHeight() * 0.28f;
    arrow.addTriangle(tx - 5.6f, ty - 2.2f, tx + 0.2f, ty, tx - 5.6f, ty + 2.2f);
    g.fillPath(arrow);
    break;
  }
  case IconGlyph::FadeOut: {
    juce::Path p;
    p.startNewSubPath(area.getX(), area.getBottom());
    p.lineTo(area.getRight(), area.getY());
    p.lineTo(area.getRight(), area.getBottom());
    p.closeSubPath();
    g.fillPath(p);
    break;
  }
  case IconGlyph::FadeIn: {
    juce::Path p;
    p.startNewSubPath(area.getX(), area.getY());
    p.lineTo(area.getRight(), area.getBottom());
    p.lineTo(area.getX(), area.getBottom());
    p.closeSubPath();
    g.fillPath(p);
    break;
  }
  case IconGlyph::Pencil: {
    juce::Path body;
    body.startNewSubPath(area.getX() + 2.0f, area.getBottom() - 3.2f);
    body.lineTo(area.getX() + 4.8f, area.getBottom() - 0.8f);
    body.lineTo(area.getRight() - 1.8f, area.getY() + 2.2f);
    body.lineTo(area.getRight() - 4.6f, area.getY() - 0.2f);
    body.closeSubPath();
    g.fillPath(body);
    break;
  }
  case IconGlyph::Settings: {
    const auto ring = area.withSizeKeepingCentre(area.getWidth() * 0.55f,
                                                 area.getHeight() * 0.55f);
    g.fillEllipse(ring);
    g.setColour(UITheme::panelDark.withAlpha(0.90f));
    g.fillEllipse(ring.reduced(ring.getWidth() * 0.35f, ring.getHeight() * 0.35f));
    g.setColour(colour);
    for (int i = 0; i < 6; ++i) {
      const float a =
          juce::MathConstants<float>::twoPi * (static_cast<float>(i) / 6.0f);
      const float tx = ring.getCentreX() + std::cos(a) * (ring.getWidth() * 0.92f);
      const float ty = ring.getCentreY() + std::sin(a) * (ring.getHeight() * 0.92f);
      g.fillRoundedRectangle(tx - 1.2f, ty - 1.2f, 2.4f, 2.4f, 0.8f);
    }
    break;
  }
  case IconGlyph::Search: {
    const auto lens = area.withSizeKeepingCentre(area.getWidth() * 0.70f,
                                                 area.getHeight() * 0.70f);
    g.drawEllipse(lens, 1.45f);
    g.drawLine(lens.getRight() - 1.4f, lens.getBottom() - 1.4f,
               lens.getRight() + 2.8f, lens.getBottom() + 2.8f, 1.45f);
    break;
  }
  case IconGlyph::Check: {
    juce::Path path;
    path.startNewSubPath(area.getX() + area.getWidth() * 0.14f,
                         area.getCentreY() + area.getHeight() * 0.02f);
    path.lineTo(area.getX() + area.getWidth() * 0.42f,
                area.getBottom() - area.getHeight() * 0.16f);
    path.lineTo(area.getRight() - area.getWidth() * 0.12f,
                area.getY() + area.getHeight() * 0.16f);
    g.strokePath(path, juce::PathStrokeType(2.3f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    break;
  }
  case IconGlyph::Clock: {
    const auto dial = area.withSizeKeepingCentre(area.getWidth() * 0.78f,
                                                 area.getHeight() * 0.78f);
    g.drawEllipse(dial, 1.55f);
    g.drawLine(dial.getCentreX(), dial.getCentreY(), dial.getCentreX(),
               dial.getY() + dial.getHeight() * 0.26f, 1.5f);
    g.drawLine(dial.getCentreX(), dial.getCentreY(), dial.getRight() - 3.0f,
               dial.getCentreY(), 1.5f);
    break;
  }
  case IconGlyph::MusicNote: {
    const float x = area.getX();
    const float y = area.getY();
    const float w = area.getWidth();
    const float h = area.getHeight();

    juce::Path stem;
    stem.startNewSubPath(x + w * 0.62f, y + h * 0.16f);
    stem.lineTo(x + w * 0.62f, y + h * 0.70f);
    stem.lineTo(x + w * 0.34f, y + h * 0.76f);
    stem.lineTo(x + w * 0.34f, y + h * 0.30f);
    stem.closeSubPath();
    g.fillPath(stem);

    g.fillEllipse(x + w * 0.10f, y + h * 0.62f, w * 0.26f, h * 0.26f);
    g.fillEllipse(x + w * 0.48f, y + h * 0.56f, w * 0.26f, h * 0.26f);
    break;
  }
  case IconGlyph::Waveform: {
    const float x = area.getX();
    const float y = area.getY();
    const float w = area.getWidth();
    const float h = area.getHeight();
    const float cx = x + w * 0.5f;

    g.fillRoundedRectangle(x + w * 0.10f, y + h * 0.40f, 2.0f, h * 0.20f, 1.0f);
    g.fillRoundedRectangle(x + w * 0.26f, y + h * 0.30f, 2.0f, h * 0.40f, 1.0f);
    g.fillRoundedRectangle(x + w * 0.42f, y + h * 0.18f, 2.0f, h * 0.64f, 1.0f);
    g.fillRoundedRectangle(cx - 1.0f, y + h * 0.08f, 2.0f, h * 0.84f, 1.0f);
    g.fillRoundedRectangle(x + w * 0.58f, y + h * 0.18f, 2.0f, h * 0.64f, 1.0f);
    g.fillRoundedRectangle(x + w * 0.74f, y + h * 0.30f, 2.0f, h * 0.40f, 1.0f);
    g.fillRoundedRectangle(x + w * 0.90f - 2.0f, y + h * 0.40f, 2.0f, h * 0.20f,
                           1.0f);
    break;
  }
  case IconGlyph::More: {
    const float dot = 1.55f;
    const float cx = area.getCentreX() - dot;
    const float midY = area.getCentreY();
    const float gap = dot * 2.8f;
    g.fillEllipse(cx, midY - gap - dot, dot * 2.0f, dot * 2.0f);
    g.fillEllipse(cx, midY - dot, dot * 2.0f, dot * 2.0f);
    g.fillEllipse(cx, midY + gap - dot, dot * 2.0f, dot * 2.0f);
    break;
  }
  case IconGlyph::Add: {
    const auto ring = area.withSizeKeepingCentre(area.getWidth() * 0.86f,
                                                 area.getHeight() * 0.86f);
    g.fillEllipse(ring);
    g.setColour(UITheme::panelDark.withAlpha(0.90f));
    g.fillRoundedRectangle(ring.getCentreX() - 1.0f, ring.getY() + 3.0f, 2.0f,
                           ring.getHeight() - 6.0f, 0.8f);
    g.fillRoundedRectangle(ring.getX() + 3.0f, ring.getCentreY() - 1.0f,
                           ring.getWidth() - 6.0f, 2.0f, 0.8f);
    break;
  }
  case IconGlyph::Remove: {
    const auto ring = area.withSizeKeepingCentre(area.getWidth() * 0.86f,
                                                 area.getHeight() * 0.86f);
    g.fillEllipse(ring);
    g.setColour(UITheme::panelDark.withAlpha(0.90f));
    g.fillRoundedRectangle(ring.getX() + 3.0f, ring.getCentreY() - 1.0f,
                           ring.getWidth() - 6.0f, 2.0f, 0.8f);
    break;
  }
  case IconGlyph::Metronome: {
    juce::Path body;
    body.startNewSubPath(area.getX() + 1.2f, area.getBottom() - 0.6f);
    body.lineTo(area.getCentreX(), area.getY() + 1.2f);
    body.lineTo(area.getRight() - 1.2f, area.getBottom() - 0.6f);
    body.closeSubPath();
    g.fillPath(body);
    break;
  }
  case IconGlyph::WindowMaximize: {
    const auto r = area.reduced(area.getWidth() * 0.12f, area.getHeight() * 0.12f);
    g.fillRoundedRectangle(r, 1.8f);
    g.setColour(UITheme::panelDark.withAlpha(0.88f));
    g.fillRoundedRectangle(r.reduced(2.2f, 2.2f), 1.2f);
    break;
  }
  case IconGlyph::WindowRestore: {
    const float w = area.getWidth();
    const float h = area.getHeight();
    const float s = juce::jmin(w, h) * 0.42f;
    const float ox = area.getX() + w * 0.08f;
    const float oy = area.getY() + h * 0.18f;
    g.fillRoundedRectangle(ox + s * 0.35f, oy, s, s, 1.2f);
    g.fillRoundedRectangle(ox, oy + s * 0.35f, s, s, 1.2f);
    break;
  }
  }
}

namespace {

bool fillsPrimaryWorkArea(const juce::Component *top,
                          const juce::Rectangle<int> &userArea) {
  if (top == nullptr)
    return false;
  const auto b = top->getBounds();
  return b.getWidth() >= userArea.getWidth() - 4 &&
         b.getHeight() >= userArea.getHeight() - 4 &&
         std::abs(b.getX() - userArea.getX()) <= 2 &&
         std::abs(b.getY() - userArea.getY()) <= 2;
}

/** Ultimo tamano antes de expandir (JUCE 8+ ya no expone setMaximised en la API publica). */
juce::Rectangle<int> g_boundsBeforeExpand{100, 100, 1200, 760};

} // namespace

void expandTopLevelWindowToPrimaryWorkArea(juce::Component *fromComponent) {
  if (fromComponent == nullptr)
    return;
  auto *top = fromComponent->getTopLevelComponent();
  if (top == nullptr)
    return;
  const auto *display =
      juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
  if (display == nullptr)
    return;
  g_boundsBeforeExpand = top->getBounds();
  top->setBounds(display->userArea);
}

void toggleTopLevelWindowMaximised(juce::Component *fromComponent) {
  if (fromComponent == nullptr)
    return;
  auto *top = fromComponent->getTopLevelComponent();
  if (top == nullptr)
    return;

  const auto *display =
      juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
  if (display == nullptr)
    return;
  const auto userArea = display->userArea;

  if (fillsPrimaryWorkArea(top, userArea)) {
    top->setBounds(g_boundsBeforeExpand);
  } else {
    g_boundsBeforeExpand = top->getBounds();
    top->setBounds(userArea);
  }
}

bool isTopLevelWindowMaximised(juce::Component *fromComponent) {
  if (fromComponent == nullptr)
    return false;
  auto *top = fromComponent->getTopLevelComponent();
  if (top == nullptr)
    return false;
  const auto *display =
      juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
  if (display == nullptr)
    return false;
  return fillsPrimaryWorkArea(top, display->userArea);
}

namespace {
juce::Image createNoiseTile() {
  juce::Image img(juce::Image::ARGB, 64, 64, true);
  juce::Random rng(0x4D5458);

  for (int y = 0; y < img.getHeight(); ++y) {
    for (int x = 0; x < img.getWidth(); ++x) {
      const float roll = rng.nextFloat();
      if (roll < 0.10f) {
        const auto alpha = static_cast<juce::uint8>(4 + rng.nextInt(7));
        img.setPixelAt(x, y, juce::Colours::white.withAlpha(alpha / 255.0f));
      }
    }
  }

  return img;
}

const juce::Image &getNoiseTile() {
  static const juce::Image noise = createNoiseTile();
  return noise;
}
} // namespace

void drawStudioBackground(juce::Graphics &g, juce::Rectangle<float> area) {
  juce::ColourGradient base(UITheme::appBgTop, area.getCentreX(), area.getY(),
                            UITheme::appBgBottom, area.getCentreX(),
                            area.getBottom(), false);
  g.setGradientFill(base);
  g.fillRect(area);

  juce::ColourGradient warmHalo(
      UITheme::accent.withAlpha(0.06f), area.getX() + area.getWidth() * 0.16f,
      area.getY() + area.getHeight() * 0.18f, UITheme::accent.withAlpha(0.0f),
      area.getX() + area.getWidth() * 0.68f,
      area.getY() + area.getHeight() * 0.80f, true);
  g.setGradientFill(warmHalo);
  g.fillRect(area);

  const float edge = 84.0f;
  const auto vignette = juce::Colours::black.withAlpha(0.08f);
  g.setGradientFill(juce::ColourGradient(
      vignette, area.getX(), area.getCentreY(), juce::Colours::transparentBlack,
      area.getX() + edge, area.getCentreY(), false));
  g.fillRect(area.getX(), area.getY(), edge, area.getHeight());
  g.setGradientFill(
      juce::ColourGradient(vignette, area.getRight(), area.getCentreY(),
                           juce::Colours::transparentBlack,
                           area.getRight() - edge, area.getCentreY(), false));
  g.fillRect(area.getRight() - edge, area.getY(), edge, area.getHeight());

  g.setTiledImageFill(getNoiseTile(), static_cast<int>(area.getX()),
                      static_cast<int>(area.getY()), 0.14f);
  g.fillRect(area);
}
