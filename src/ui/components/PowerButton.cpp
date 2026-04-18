#include "PowerButton.h"

PowerButton::PowerButton() {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void PowerButton::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    constexpr float radius = 6.0f;

    juce::Colour bg = on ? UITheme::accent.withAlpha(0.12f)
                         : (hovered ? UITheme::cardBgHover : UITheme::cardBg);
    g.setColour(bg);
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(UITheme::cardBorder);
    g.drawRoundedRectangle(bounds, radius, 1.0f);

    drawPowerIcon(g, bounds.reduced(18.0f), on ? UITheme::accent : UITheme::textSecondary);
}

void PowerButton::drawPowerIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) {
    g.setColour(colour);

    const float cx = area.getCentreX();
    const float cy = area.getCentreY() + area.getHeight() * 0.06f;
    const float r = juce::jmin(area.getWidth(), area.getHeight()) * 0.44f;
    const float stroke = r * 0.22f;
    const float gapHalf = 0.42f;

    juce::Path ring;
    const float startA = -juce::MathConstants<float>::halfPi + gapHalf;
    const float sweepA = juce::MathConstants<float>::twoPi - gapHalf * 2.0f;
    ring.addCentredArc(cx, cy, r, r, 0.0f, startA, startA + sweepA, true);
    g.strokePath(ring, juce::PathStrokeType(stroke, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

    const float barTop = cy - r - stroke * 0.5f;
    const float barLen = r * 0.72f + stroke;
    g.fillRoundedRectangle(cx - stroke / 2.0f, barTop, stroke, barLen, stroke / 2.0f);
}

void PowerButton::mouseDown(const juce::MouseEvent&) {
    setPoweredOn(!on);
    if (onToggled) onToggled(on);
}

void PowerButton::mouseEnter(const juce::MouseEvent&) {
    hovered = true;
    repaint();
}

void PowerButton::mouseExit(const juce::MouseEvent&) {
    hovered = false;
    repaint();
}

void PowerButton::setPoweredOn(bool powered) {
    if (on == powered) return;
    on = powered;
    repaint();
}
