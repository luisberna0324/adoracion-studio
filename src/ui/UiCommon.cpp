#include "UiCommon.h"
#include "BinaryData.h"

#include <cmath>

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

void drawIconGlyph(juce::Graphics &g, juce::Rectangle<float> area,
                   IconGlyph glyph, juce::Colour colour, float stroke) {
  juce::ignoreUnused(stroke);
  g.setColour(colour);
  area = area.reduced(area.getWidth() * 0.17f, area.getHeight() * 0.17f);

  switch (glyph) {
  case IconGlyph::ArrowLeft: {
    const float midY = area.getCentreY();
    const float leftX = area.getX() + area.getWidth() * 0.20f;
    const float rightX = area.getRight() - area.getWidth() * 0.12f;
    juce::Path path;
    path.startNewSubPath(rightX, area.getY() + area.getHeight() * 0.12f);
    path.lineTo(leftX, midY);
    path.lineTo(rightX, area.getBottom() - area.getHeight() * 0.12f);
    g.strokePath(path, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved,
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
