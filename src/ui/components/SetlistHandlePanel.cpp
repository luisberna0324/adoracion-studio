#include "SetlistHandlePanel.h"

SetlistHandlePanel::SetlistHandlePanel(juce::String setlistName)
    : title(std::move(setlistName).toUpperCase()) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void SetlistHandlePanel::setTitle(juce::String setlistName) {
  const auto next = setlistName.trim().toUpperCase();
  if (title == next)
    return;
  title = next;
  repaint();
}

void SetlistHandlePanel::setCollapsed(bool collapsedState) {
  if (isCollapsed == collapsedState)
    return;
  isCollapsed = collapsedState;
  repaint();
}

void SetlistHandlePanel::setPhoneOverlayMode(const bool enabled) {
  if (phoneOverlayMode == enabled)
    return;
  phoneOverlayMode = enabled;
  repaint();
}

void SetlistHandlePanel::setPhoneIcon(const SvgIconName icon) {
  if (phoneIcon == icon)
    return;
  phoneIcon = icon;
  repaint();
}

void SetlistHandlePanel::paint(juce::Graphics &g) {
  auto b = getLocalBounds().toFloat();
  if (phoneOverlayMode) {
    const auto fill = UITheme::panelDark.interpolatedWith(
        UITheme::cardBg, isMouseDown ? 0.52f : (isHovered ? 0.38f : 0.26f));
    g.setColour(fill.withAlpha(0.96f));
    g.fillRoundedRectangle(b.reduced(0.5f), 9.0f);
    g.setColour(UITheme::cardBorder.withAlpha(isHovered ? 0.78f : 0.50f));
    g.drawRoundedRectangle(b.reduced(0.5f), 9.0f, 1.0f);

    auto iconArea = b.withSizeKeepingCentre(24.0f, 24.0f);
    drawSvgIcon(g, iconArea, phoneIcon,
                UITheme::textPrimary.withAlpha(isHovered ? 0.96f : 0.82f));
    return;
  }
  const auto track = b.reduced(0.5f, 0.0f);
  const auto fill = UITheme::panelDark
                        .interpolatedWith(UITheme::cardBg, isMouseDown ? 0.48f : 0.24f)
                        .withAlpha(isHovered ? 0.9f : 0.74f);

  g.setColour(fill);
  g.fillRoundedRectangle(track, 4.0f);

  g.setColour(juce::Colours::black.withAlpha(isMouseDown ? 0.46f : 0.28f));
  g.drawRoundedRectangle(track.reduced(0.5f), 4.0f, 1.0f);

  g.setColour(UITheme::cardBorder.withAlpha(isHovered ? 0.68f : 0.42f));
  g.drawRoundedRectangle(track.reduced(1.5f), 3.0f, 1.0f);

  g.setColour(juce::Colours::white.withAlpha(isHovered ? 0.046f : 0.024f));
  g.drawLine(track.getX() + 3.0f, track.getY() + 5.0f,
             track.getX() + 3.0f, track.getBottom() - 5.0f, 1.0f);

  const float cy = track.getCentreY();
  const float cx = track.getCentreX();
  juce::Path arrow;
  if (isCollapsed) {
    arrow.addTriangle(cx - 2.5f, cy - 5.0f, cx - 2.5f, cy + 5.0f,
                      cx + 3.0f, cy);
  } else {
    arrow.addTriangle(cx + 2.5f, cy - 5.0f, cx + 2.5f, cy + 5.0f,
                      cx - 3.0f, cy);
  }

  const auto arrowColour =
      isHovered ? UITheme::textPrimary.withAlpha(0.82f)
                : UITheme::textSecondary.withAlpha(0.46f);
  g.setColour(arrowColour);
  g.fillPath(arrow);
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
