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

    auto fill = UITheme::accent.withMultipliedAlpha(isHovered ? 0.95f : 0.88f);
    if (isMouseDown) fill = UITheme::accentSoft;

    g.setColour(fill);
    g.fillRoundedRectangle(b.reduced(0.5f), 4.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.72f));
    g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);

    // Label
    g.setColour(UITheme::onAccent.withAlpha(0.92f));
    g.setFont(juce::Font(juce::FontOptions(10.0f).withKerningFactor(0.12f)));
    g.drawFittedText("PADS", b.toNearestInt(), juce::Justification::centred, 1);

    // Arrow (▲ when collapsed to expand, ▼ when expanded to collapse)
    const float cx = b.getX() + 18.0f;
    const float cy = b.getCentreY();
    juce::Path arrow;
    if (isCollapsed) {
        arrow.addTriangle(cx - 5.0f, cy + 3.0f, cx + 5.0f, cy + 3.0f, cx, cy - 3.0f);
    } else {
        arrow.addTriangle(cx - 5.0f, cy - 3.0f, cx + 5.0f, cy - 3.0f, cx, cy + 3.0f);
    }
    g.setColour(UITheme::onAccent.withAlpha(0.9f));
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
