#include "PowerButton.h"

PowerButton::PowerButton() {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void PowerButton::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto button = bounds.reduced(1.0f);
    const float radius = juce::jlimit(4.0f, 8.0f, button.getHeight() * 0.22f);

    const auto onFill = UITheme::accent;
    const auto onGlow = UITheme::accentSoft.withAlpha(0.22f);
    const auto offFill = UITheme::cardBg.interpolatedWith(UITheme::cardBgHover, 0.22f);
    const auto onBorder = UITheme::accentSoft;
    const auto offBorder = UITheme::cardBorder.brighter(0.08f);

    auto fill = on ? onFill : offFill;
    if (hovered)
        fill = fill.brighter(on ? 0.05f : 0.08f);

    if (on) {
        g.setColour(onGlow);
        g.fillRoundedRectangle(button.expanded(3.0f), radius + 3.0f);
    }

    g.setColour(juce::Colours::black.withAlpha(0.24f));
    g.fillRoundedRectangle(button.translated(0.0f, 1.0f), radius);
    g.setColour(fill);
    g.fillRoundedRectangle(button, radius);
    g.setColour(on ? onBorder : offBorder);
    g.drawRoundedRectangle(button, radius, 1.0f);

    drawPowerIcon(g, button.reduced(button.getWidth() * 0.23f,
                                    button.getHeight() * 0.18f),
                  on ? UITheme::onAccent : UITheme::textSecondary);
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
