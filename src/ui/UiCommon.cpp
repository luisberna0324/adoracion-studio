#include "UiCommon.h"

#include <cmath>

juce::Colour UITheme::appBg = juce::Colour::fromRGB(50, 46, 43); // #322E2B
juce::Colour UITheme::appBgTop = juce::Colour::fromRGB(56, 52, 49); // #383431
juce::Colour UITheme::appBgBottom = juce::Colour::fromRGB(70, 65, 61); // #46413D
juce::Colour UITheme::panelDark = juce::Colour::fromRGB(38, 34, 32); // #262220
juce::Colour UITheme::cardBg = juce::Colour::fromRGB(212, 212, 212);
juce::Colour UITheme::cardBgHover = juce::Colour::fromRGB(218, 218, 218);
juce::Colour UITheme::cardBgPressed = juce::Colour::fromRGB(201, 201, 201);
juce::Colour UITheme::cardBorder = juce::Colour::fromRGB(189, 189, 189);
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
juce::Colour UITheme::dividerSoft = juce::Colour::fromRGBA(255, 255, 255, 20);

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
    UITheme::cardBorder = juce::Colour::fromRGB(64, 64, 64); // #404040
    UITheme::textPrimary = juce::Colour::fromRGB(245, 245, 245);
    UITheme::textSecondary = juce::Colour::fromRGB(190, 190, 190);
    UITheme::accent = juce::Colour::fromRGB(29, 185, 84); // #1DB954
    UITheme::accentSoft = juce::Colour::fromRGB(30, 215, 96); // #1ED760
    UITheme::accentMuted = juce::Colour::fromRGB(102, 150, 102);
    UITheme::accentMaster = juce::Colour::fromRGB(29, 185, 84);
    UITheme::cardBgMaster = juce::Colour::fromRGB(44, 44, 44); // #2C2C2C
    UITheme::cardBgClickGuide = juce::Colour::fromRGB(52, 52, 52); // #343434
    UITheme::onAccent = juce::Colours::black;
    UITheme::dividerSoft = juce::Colour::fromRGBA(255, 255, 255, 16);
    break;
  case ThemePreset::Ableton:
    UITheme::appBg = juce::Colour::fromRGB(36, 36, 36); // #242424
    UITheme::appBgTop = juce::Colour::fromRGB(42, 42, 42); // #2A2A2A
    UITheme::appBgBottom = juce::Colour::fromRGB(52, 52, 52); // #343434
    UITheme::panelDark = juce::Colour::fromRGB(30, 30, 30); // #1E1E1E
    UITheme::cardBg = juce::Colour::fromRGB(52, 52, 52); // #343434
    UITheme::cardBgHover = juce::Colour::fromRGB(66, 66, 66); // #424242
    UITheme::cardBgPressed = juce::Colour::fromRGB(76, 76, 76); // #4C4C4C
    UITheme::cardBorder = juce::Colour::fromRGB(82, 82, 82); // #525252
    UITheme::textPrimary = juce::Colour::fromRGB(235, 235, 235);
    UITheme::textSecondary = juce::Colour::fromRGB(184, 184, 184);
    UITheme::accent = juce::Colour::fromRGB(255, 179, 71); // warm
    UITheme::accentSoft = juce::Colour::fromRGB(255, 198, 112); // warm soft
    UITheme::accentMuted = juce::Colour::fromRGB(192, 152, 108);
    UITheme::accentMaster = juce::Colour::fromRGB(255, 179, 71);
    UITheme::cardBgMaster = juce::Colour::fromRGB(58, 58, 58); // #3A3A3A
    UITheme::cardBgClickGuide = juce::Colour::fromRGB(64, 64, 64); // #404040
    UITheme::onAccent = juce::Colours::black;
    UITheme::dividerSoft = juce::Colour::fromRGBA(255, 255, 255, 16);
    break;
  case ThemePreset::Default:
  default:
    UITheme::appBg = juce::Colour::fromRGB(50, 46, 43);
    UITheme::appBgTop = juce::Colour::fromRGB(56, 52, 49);
    UITheme::appBgBottom = juce::Colour::fromRGB(70, 65, 61);
    UITheme::panelDark = juce::Colour::fromRGB(38, 34, 32);
    UITheme::cardBg = juce::Colour::fromRGB(212, 212, 212);
    UITheme::cardBgHover = juce::Colour::fromRGB(218, 218, 218);
    UITheme::cardBgPressed = juce::Colour::fromRGB(201, 201, 201);
    UITheme::cardBorder = juce::Colour::fromRGB(189, 189, 189);
    UITheme::textPrimary = juce::Colour::fromRGB(78, 78, 78);
    UITheme::textSecondary = juce::Colour::fromRGB(118, 118, 118);
    UITheme::accent = juce::Colour::fromRGB(193, 122, 90);
    UITheme::accentSoft = juce::Colour::fromRGB(196, 154, 108);
    UITheme::accentMuted = juce::Colour::fromRGB(181, 124, 124);
    UITheme::accentMaster = juce::Colour::fromRGB(163, 108, 74);
    UITheme::cardBgMaster = juce::Colour::fromRGB(232, 216, 200);
    UITheme::cardBgClickGuide = juce::Colour::fromRGB(200, 205, 214);
    UITheme::onAccent = juce::Colours::white;
    UITheme::dividerSoft = juce::Colour::fromRGBA(255, 255, 255, 20);
    break;
  }
}

void drawIconGlyph(juce::Graphics &g, juce::Rectangle<float> area,
                   IconGlyph glyph, juce::Colour colour, float stroke) {
  g.setColour(colour);
  area = area.reduced(area.getWidth() * 0.22f, area.getHeight() * 0.22f);

  switch (glyph) {
  case IconGlyph::Play: {
    juce::Path p;
    p.addTriangle(area.getX(), area.getY(), area.getX(), area.getBottom(),
                  area.getRight(), area.getCentreY());
    g.fillPath(p);
    break;
  }
  case IconGlyph::Pause: {
    const float w = area.getWidth();
    const float h = area.getHeight();
    g.fillRoundedRectangle(area.getX() + (w * 0.20f), area.getY(), w * 0.22f,
                           h, 1.2f);
    g.fillRoundedRectangle(area.getRight() - (w * 0.42f), area.getY(),
                           w * 0.22f, h, 1.2f);
    break;
  }
  case IconGlyph::Stop:
    g.fillRoundedRectangle(area.withSizeKeepingCentre(area.getWidth() * 0.72f,
                                                      area.getHeight() * 0.72f),
                           1.5f);
    break;
  case IconGlyph::Prev: {
    const float lineX = area.getX() + 1.0f;
    g.drawLine(lineX, area.getY(), lineX, area.getBottom(), stroke);
    juce::Path p;
    p.addTriangle(area.getRight(), area.getY(), area.getRight(),
                  area.getBottom(), area.getX() + 2.5f, area.getCentreY());
    g.fillPath(p);
    break;
  }
  case IconGlyph::SeekBack: {
    const float lineX = area.getX() + 2.0f;
    g.drawLine(lineX, area.getY(), lineX, area.getBottom(), stroke);
    juce::Path p1;
    p1.addTriangle(area.getCentreX(), area.getY(), area.getCentreX(),
                   area.getBottom(), area.getX() + 4.5f, area.getCentreY());
    g.fillPath(p1);
    juce::Path p2;
    p2.addTriangle(area.getRight() - 1.0f, area.getY(),
                   area.getRight() - 1.0f, area.getBottom(),
                   area.getCentreX() + 2.5f, area.getCentreY());
    g.fillPath(p2);
    break;
  }
  case IconGlyph::Next: {
    const float lineX = area.getRight() - 1.0f;
    g.drawLine(lineX, area.getY(), lineX, area.getBottom(), stroke);
    juce::Path p;
    p.addTriangle(area.getX(), area.getY(), area.getX(), area.getBottom(),
                  area.getRight() - 2.5f, area.getCentreY());
    g.fillPath(p);
    break;
  }
  case IconGlyph::SeekForward: {
    const float lineX = area.getRight() - 2.0f;
    g.drawLine(lineX, area.getY(), lineX, area.getBottom(), stroke);
    juce::Path p1;
    p1.addTriangle(area.getX() + 1.0f, area.getY(), area.getX() + 1.0f,
                   area.getBottom(), area.getCentreX() - 2.5f,
                   area.getCentreY());
    g.fillPath(p1);
    juce::Path p2;
    p2.addTriangle(area.getCentreX(), area.getY(), area.getCentreX(),
                   area.getBottom(), area.getRight() - 4.5f,
                   area.getCentreY());
    g.fillPath(p2);
    break;
  }
  case IconGlyph::Loop: {
    juce::Path arc;
    arc.addCentredArc(area.getCentreX(), area.getCentreY(),
                      area.getWidth() * 0.45f, area.getHeight() * 0.45f, 0.0f,
                      0.35f, 5.85f, true);
    g.strokePath(arc, juce::PathStrokeType(stroke, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
    juce::Path arrow;
    const float tx = area.getRight() - 1.0f;
    const float ty = area.getCentreY() - area.getHeight() * 0.20f;
    arrow.addTriangle(tx - 4.5f, ty - 2.0f, tx + 0.5f, ty, tx - 4.5f, ty + 2.0f);
    g.fillPath(arrow);
    break;
  }
  case IconGlyph::FadeOut: {
    g.drawLine(area.getX(), area.getY(), area.getRight(), area.getBottom(),
               stroke);
    break;
  }
  case IconGlyph::FadeIn: {
    g.drawLine(area.getX(), area.getBottom(), area.getRight(), area.getY(),
               stroke);
    break;
  }
  case IconGlyph::Pencil: {
    juce::Path body;
    body.startNewSubPath(area.getX() + 2.0f, area.getBottom() - 2.0f);
    body.lineTo(area.getX() + 4.5f, area.getBottom() - 4.5f);
    body.lineTo(area.getRight() - 2.5f, area.getY() + 2.5f);
    body.lineTo(area.getRight() - 5.0f, area.getY());
    body.lineTo(area.getX() + 2.0f, area.getBottom() - 7.0f);
    body.closeSubPath();
    g.fillPath(body);
    break;
  }
  case IconGlyph::Settings: {
    const auto ring = area.withSizeKeepingCentre(area.getWidth() * 0.64f,
                                                 area.getHeight() * 0.64f);
    g.drawEllipse(ring, stroke);
    for (int i = 0; i < 6; ++i) {
      const float a =
          juce::MathConstants<float>::twoPi * (static_cast<float>(i) / 6.0f);
      const float x1 =
          ring.getCentreX() + std::cos(a) * (ring.getWidth() * 0.50f);
      const float y1 =
          ring.getCentreY() + std::sin(a) * (ring.getHeight() * 0.50f);
      const float x2 =
          ring.getCentreX() + std::cos(a) * (ring.getWidth() * 0.72f);
      const float y2 =
          ring.getCentreY() + std::sin(a) * (ring.getHeight() * 0.72f);
      g.drawLine(x1, y1, x2, y2, stroke);
    }
    break;
  }
  case IconGlyph::More: {
    const float dot = 2.0f;
    const float y = area.getCentreY() - dot;
    g.fillEllipse(area.getX(), y, dot * 2.0f, dot * 2.0f);
    g.fillEllipse(area.getCentreX() - dot, y, dot * 2.0f, dot * 2.0f);
    g.fillEllipse(area.getRight() - dot * 2.0f, y, dot * 2.0f, dot * 2.0f);
    break;
  }
  case IconGlyph::Add: {
    const auto ring = area.withSizeKeepingCentre(area.getWidth() * 0.86f,
                                                 area.getHeight() * 0.86f);
    g.drawEllipse(ring, stroke);
    g.drawLine(ring.getCentreX(), ring.getY() + 2.0f, ring.getCentreX(),
               ring.getBottom() - 2.0f, stroke);
    g.drawLine(ring.getX() + 2.0f, ring.getCentreY(), ring.getRight() - 2.0f,
               ring.getCentreY(), stroke);
    break;
  }
  case IconGlyph::Metronome: {
    juce::Path body;
    body.startNewSubPath(area.getX() + 1.0f, area.getBottom());
    body.lineTo(area.getCentreX(), area.getY() + 1.5f);
    body.lineTo(area.getRight() - 1.0f, area.getBottom());
    body.closeSubPath();
    g.strokePath(body, juce::PathStrokeType(stroke));
    g.drawLine(area.getCentreX(), area.getY() + 6.0f, area.getCentreX(),
               area.getBottom() - 3.0f, stroke * 0.9f);
    g.drawLine(area.getCentreX(), area.getCentreY() + 1.0f,
               area.getRight() - 3.0f, area.getY() + 4.0f, stroke * 0.9f);
    break;
  }
  case IconGlyph::WindowMaximize: {
    const auto r = area.reduced(area.getWidth() * 0.12f, area.getHeight() * 0.12f);
    g.drawRect(r, stroke);
    break;
  }
  case IconGlyph::WindowRestore: {
    const float w = area.getWidth();
    const float h = area.getHeight();
    const float s = juce::jmin(w, h) * 0.42f;
    const float ox = area.getX() + w * 0.08f;
    const float oy = area.getY() + h * 0.18f;
    g.drawRect(ox + s * 0.35f, oy, s, s, stroke);
    g.drawRect(ox, oy + s * 0.35f, s, s, stroke);
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
