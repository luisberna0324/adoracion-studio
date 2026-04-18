#include "NotePadButton.h"

NotePadButton::NotePadButton(const juce::String& name, int index)
    : noteName(name), noteIndex(index) {}

void NotePadButton::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    constexpr float radius = 6.0f;

    juce::Colour bg;
    if (active)
        bg = UITheme::accent;
    else if (pressed)
        bg = UITheme::cardBgPressed;
    else if (hovered)
        bg = UITheme::cardBgHover;
    else
        bg = UITheme::cardBg;

    g.setColour(bg);
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(UITheme::cardBorder);
    g.drawRoundedRectangle(bounds, radius, 1.0f);

    g.setColour(active ? UITheme::onAccent : UITheme::textPrimary);
    g.setFont(juce::Font(juce::FontOptions(18.0f).withStyle("Bold")));
    g.drawFittedText(noteName, getLocalBounds(), juce::Justification::centred, 1);
}

void NotePadButton::mouseDown(const juce::MouseEvent&) {
    pressed = true;
    repaint();
    if (onPressed) onPressed();
}

void NotePadButton::mouseUp(const juce::MouseEvent&) {
    pressed = false;
    repaint();
    if (onReleased) onReleased();
}

void NotePadButton::mouseEnter(const juce::MouseEvent&) {
    hovered = true;
    repaint();
}

void NotePadButton::mouseExit(const juce::MouseEvent&) {
    hovered = false;
    pressed = false;
    repaint();
}

void NotePadButton::setActive(bool a) {
    if (active == a) return;
    active = a;
    repaint();
}
