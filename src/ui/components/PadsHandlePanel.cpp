#include "PadsHandlePanel.h"

PadsHandlePanel::PadsHandlePanel() {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void PadsHandlePanel::setCollapsed(bool collapsed) {
    if (isCollapsed == collapsed) return;
    isCollapsed = collapsed;
    repaint();
}

void PadsHandlePanel::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    const auto track = b.reduced(0.0f, 0.5f);

    auto fill = UITheme::panelDark
                    .interpolatedWith(UITheme::cardBg, isMouseDown ? 0.48f : 0.24f)
                    .withAlpha(isHovered ? 0.9f : 0.74f);

    g.setColour(fill);
    g.fillRoundedRectangle(track, 4.0f);

    g.setColour(juce::Colours::black.withAlpha(isMouseDown ? 0.46f : 0.28f));
    g.drawRoundedRectangle(track.reduced(0.5f), 4.0f, 1.0f);
    g.setColour(UITheme::cardBorder.withAlpha(isHovered ? 0.68f : 0.42f));
    g.drawRoundedRectangle(track.reduced(1.5f), 3.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(isHovered ? 0.046f : 0.024f));
    g.drawLine(track.getX() + 8.0f, track.getY() + 3.0f,
               track.getRight() - 8.0f, track.getY() + 3.0f, 1.0f);

    // Arrow (▲ when collapsed to expand, ▼ when expanded to collapse)
    const float cx = track.getCentreX();
    const float cy = track.getCentreY();
    juce::Path arrow;
    if (isCollapsed) {
        arrow.addTriangle(cx - 4.5f, cy + 2.5f, cx + 4.5f, cy + 2.5f, cx, cy - 3.0f);
    } else {
        arrow.addTriangle(cx - 4.5f, cy - 2.5f, cx + 4.5f, cy - 2.5f, cx, cy + 3.0f);
    }
    const auto arrowColour =
        isHovered ? UITheme::textPrimary.withAlpha(0.82f)
                  : UITheme::textSecondary.withAlpha(0.46f);
    g.setColour(arrowColour);
    g.fillPath(arrow);
}

void PadsHandlePanel::mouseEnter(const juce::MouseEvent&) { isHovered = true;  repaint(); }
void PadsHandlePanel::mouseExit(const juce::MouseEvent&)  { isHovered = false; isMouseDown = false; repaint(); }
void PadsHandlePanel::mouseDown(const juce::MouseEvent&)  { isMouseDown = true; repaint(); }

void PadsHandlePanel::mouseUp(const juce::MouseEvent&) {
    const bool trigger = isMouseDown;
    isMouseDown = false;
    repaint();
    if (trigger && onToggle) onToggle();
}
