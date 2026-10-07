#pragma once

#include <JuceHeader.h>
#include "BinaryData.h"

// Shared visual hierarchy for welcome, password recovery and session renewal.
namespace AuthUi {
inline const juce::Colour surface{0xff1d2320};
inline const juce::Colour text{0xfff4f7f5};
inline const juce::Colour muted{0xffb7c2bb};
inline const juce::Colour border{0xff414d46};
inline const juce::Colour accent{0xff35d47f};
enum class Role { primary, secondary, link, google, eye };

inline void label(juce::Label& control, float size, bool bold = false) {
  control.setFont(juce::FontOptions(size).withStyle(bold ? "Bold" : "Regular"));
  control.setColour(juce::Label::textColourId, bold ? text : muted);
  control.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
  control.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
  control.setMinimumHorizontalScale(1.0f);
  control.setInterceptsMouseClicks(false, false);
}

inline void drawSurface(juce::Graphics& g, juce::Rectangle<float> bounds) {
  g.setColour(surface);
  g.fillRoundedRectangle(bounds, 22.0f);
  g.setColour(border.withAlpha(0.7f));
  g.drawRoundedRectangle(bounds.reduced(0.5f), 22.0f, 1.0f);
}

// Measure and draw explanatory copy with the same wrapping and line spacing.
// This avoids oversized fixed text boxes and clipped copy on narrow phones.
class Paragraph final : public juce::Label {
public:
  int heightForWidth(int width) const {
    return juce::jmax(1, juce::roundToInt(std::ceil(layoutForWidth(width).getHeight())));
  }

  void paint(juce::Graphics& g) override {
    auto layout = layoutForWidth(getWidth());
    layout.draw(g, getLocalBounds().toFloat());
  }

private:
  juce::TextLayout layoutForWidth(int width) const {
    juce::AttributedString copy;
    copy.setJustification(getJustificationType());
    copy.setLineSpacing(3.0f);
    copy.append(getText(), getFont(), findColour(juce::Label::textColourId));
    juce::TextLayout layout;
    layout.createLayout(copy, static_cast<float>(juce::jmax(1, width)));
    return layout;
  }
};

class FieldLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  void fillTextEditorBackground(juce::Graphics& g, int width, int height,
                               juce::TextEditor&) override {
    g.setColour(juce::Colour(0xff151b18));
    g.fillRoundedRectangle(juce::Rectangle<float>(0, 0, (float)width, (float)height), 10);
  }
  void drawTextEditorOutline(juce::Graphics& g, int width, int height,
                            juce::TextEditor& editor) override {
    const bool focused = editor.hasKeyboardFocus(true);
    g.setColour((focused ? accent : border).withMultipliedAlpha(editor.isEnabled() ? 1.0f : 0.55f));
    g.drawRoundedRectangle(juce::Rectangle<float>(0, 0, (float)width, (float)height).reduced(1),
                           9, focused ? 2.0f : 1.0f);
  }
};

class Button final : public juce::TextButton {
public:
  explicit Button(Role roleToUse) : role(roleToUse) {
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    if (role == Role::google)
      googleIcon = juce::ImageFileFormat::loadFrom(BinaryData::googleg_png, BinaryData::googleg_pngSize);
  }
  void paintButton(juce::Graphics& g, bool hover, bool down) override {
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const bool flat = role == Role::link || role == Role::eye;
    const auto opacity = isEnabled() ? 1.0f : 0.48f;
    auto fill = role == Role::primary ? accent : role == Role::google
                    ? juce::Colour(0xfff2f2f2) : juce::Colour(0xff26312b);
    if (!flat || hover || down) {
      if (flat) fill = juce::Colours::white.withAlpha(down ? 0.12f : 0.06f);
      else if (down) fill = fill.darker(0.12f);
      else if (hover) fill = fill.brighter(0.06f);
      g.setColour(fill.withMultipliedAlpha(opacity));
      g.fillRoundedRectangle(bounds, 10);
    }
    if (role == Role::secondary) {
      g.setColour(border.withMultipliedAlpha(opacity));
      g.drawRoundedRectangle(bounds.reduced(0.5f), 9.5f, 1);
    }
    if (hasKeyboardFocus(true)) {
      g.setColour(accent);
      g.drawRoundedRectangle(bounds.reduced(1), 9, 2);
    }
    auto foreground = role == Role::primary ? juce::Colour(0xff092416)
                      : role == Role::google ? juce::Colour(0xff1f1f1f)
                      : role == Role::link ? accent : AuthUi::text;
    g.setColour(foreground.withMultipliedAlpha(opacity));
    if (role == Role::eye) {
      auto r = bounds.withSizeKeepingCentre(22.0f, 16.0f);
      juce::Path eye;
      eye.startNewSubPath(r.getX(), r.getCentreY());
      eye.cubicTo(r.getX() + 5, r.getY() - 1, r.getRight() - 5, r.getY() - 1, r.getRight(), r.getCentreY());
      eye.cubicTo(r.getRight() - 5, r.getBottom() + 1, r.getX() + 5, r.getBottom() + 1, r.getX(), r.getCentreY());
      g.strokePath(eye, juce::PathStrokeType(1.7f));
      g.drawEllipse(r.withSizeKeepingCentre(6.0f, 6.0f), 1.7f);
      if (getToggleState()) g.drawLine(r.getX(), r.getBottom() + 2, r.getRight(), r.getY() - 2, 1.7f);
      return;
    }
    auto textArea = getLocalBounds().reduced(12, 0);
    if (role == Role::google && googleIcon.isValid()) {
      g.setOpacity(opacity);
      g.drawImageWithin(googleIcon, 16, (getHeight() - 20) / 2, 20, 20, juce::RectanglePlacement::centred);
      textArea.removeFromLeft(30);
      textArea.removeFromRight(8);
    }
    g.setColour(foreground.withMultipliedAlpha(opacity));
    g.setFont(juce::FontOptions(role == Role::link ? 14.0f : 16.0f)
                  .withStyle(role == Role::primary ? "Bold" : "Regular"));
    g.drawFittedText(getButtonText(), textArea, juce::Justification::centred, 1, 1.0f);
  }
private:
  Role role;
  juce::Image googleIcon;
};

class MailIcon final : public juce::Component {
public:
  void paint(juce::Graphics& g) override {
    const auto size = juce::jmin(56.0f, static_cast<float>(juce::jmin(getWidth(), getHeight())));
    auto circle = getLocalBounds().toFloat().withSizeKeepingCentre(size, size);
    g.setColour(accent.withAlpha(0.12f));
    g.fillEllipse(circle);
    auto envelope = circle.withSizeKeepingCentre(30, 22);
    g.setColour(accent);
    g.drawRoundedRectangle(envelope, 3, 2);
    juce::Path flap;
    flap.startNewSubPath(envelope.getTopLeft().translated(1, 2));
    flap.lineTo(envelope.getCentre().translated(0, 2));
    flap.lineTo(envelope.getTopRight().translated(-1, 2));
    g.strokePath(flap, juce::PathStrokeType(2));
  }
};
} // namespace AuthUi
