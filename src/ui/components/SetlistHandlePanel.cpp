#include "SetlistHandlePanel.h"

SetlistHandlePanel::SetlistHandlePanel(juce::String setlistName)
    : title(std::move(setlistName).toUpperCase()) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void SetlistHandlePanel::setCollapsed(bool collapsedState) {
  if (isCollapsed == collapsedState)
    return;
  isCollapsed = collapsedState;
  repaint();
}

void SetlistHandlePanel::paint(juce::Graphics &g) {
  auto b = getLocalBounds().toFloat();
  auto fill = UITheme::accent.withMultipliedAlpha(isHovered ? 0.95f : 0.88f);
  if (isMouseDown)
    fill = UITheme::accentSoft;

  g.setColour(fill);
  g.fillRoundedRectangle(b.reduced(0.5f), 4.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.72f));
  g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);

  const float cy = b.getCentreY();
  const float cx = b.getCentreX();
  juce::Path arrow;
  if (isCollapsed) {
    arrow.addTriangle(cx - 4.0f, cy - 26.0f, cx - 4.0f, cy - 18.0f, cx + 1.5f,
                      cy - 22.0f);
  } else {
    arrow.addTriangle(cx + 4.0f, cy - 26.0f, cx + 4.0f, cy - 18.0f, cx - 1.5f,
                      cy - 22.0f);
  }
  g.setColour(UITheme::onAccent.withAlpha(0.9f));
  g.fillPath(arrow);

  juce::Graphics::ScopedSaveState ss(g);
  const auto center = b.getCentre();
  g.addTransform(juce::AffineTransform::rotation(
      -juce::MathConstants<float>::halfPi, center.x, center.y));
  g.setColour(UITheme::onAccent.withAlpha(0.92f));
  g.setFont(juce::FontOptions(10.0f).withKerningFactor(0.12f));

  const int rw = static_cast<int>(b.getHeight()) - 8;
  const int rh = static_cast<int>(b.getWidth()) - 6;
  g.drawFittedText(title, static_cast<int>(center.x) - (rw / 2),
                   static_cast<int>(center.y) - (rh / 2), rw, rh,
                   juce::Justification::centred, 1);
}

void SetlistHandlePanel::mouseEnter(const juce::MouseEvent &) {
  isHovered = true;
  repaint();
}

void SetlistHandlePanel::mouseExit(const juce::MouseEvent &) {
  isHovered = false;
  isMouseDown = false;
  repaint();
}

void SetlistHandlePanel::mouseDown(const juce::MouseEvent &) {
  isMouseDown = true;
  repaint();
}

void SetlistHandlePanel::mouseUp(const juce::MouseEvent &) {
  const bool trigger = isMouseDown;
  isMouseDown = false;
  repaint();
  if (trigger && onToggle)
    onToggle();
}
