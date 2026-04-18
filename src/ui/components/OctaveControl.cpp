#include "OctaveControl.h"

OctaveControl::OctaveControl() {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<int> OctaveControl::getMinusArea() const {
    return { 0, (getHeight() - 46) / 2, 42, 46 };
}

juce::Rectangle<int> OctaveControl::getValueArea() const {
    return { 50, (getHeight() - 46) / 2, 44, 46 };
}

juce::Rectangle<int> OctaveControl::getPlusArea() const {
    return { 102, (getHeight() - 46) / 2, 42, 46 };
}

juce::String OctaveControl::octaveLabel() const {
    if (octave == 0) return "0";
    return (octave > 0 ? "+" : "") + juce::String(octave);
}

void OctaveControl::paint(juce::Graphics& g) {
    g.setColour(UITheme::panelDark);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);

    auto drawBtn = [&](juce::Rectangle<int> area, const juce::String& label, bool hovered) {
        auto r = area.toFloat().reduced(0.5f);
        if (hovered) {
            g.setColour(UITheme::cardBgHover);
            g.fillRoundedRectangle(r, 4.0f);
        }
        g.setColour(UITheme::cardBorder);
        g.drawRoundedRectangle(r, 4.0f, 1.0f);
        g.setColour(UITheme::textPrimary);
        g.setFont(juce::Font(juce::FontOptions(20.0f).withStyle("Bold")));
        g.drawFittedText(label, area, juce::Justification::centred, 1);
    };

    drawBtn(getMinusArea(), "-", hoveredArea == -1);
    drawBtn(getPlusArea(), "+", hoveredArea == 1);

    g.setColour(UITheme::textPrimary);
    g.setFont(juce::Font(juce::FontOptions(22.0f).withStyle("Bold")));
    g.drawFittedText(octaveLabel(), getValueArea(), juce::Justification::centred, 1);
}

void OctaveControl::mouseDown(const juce::MouseEvent& e) {
    if (getMinusArea().contains(e.getPosition()))
        changeOctave(-1);
    else if (getPlusArea().contains(e.getPosition()))
        changeOctave(1);
}

void OctaveControl::mouseMove(const juce::MouseEvent& e) {
    int prev = hoveredArea;
    if (getMinusArea().contains(e.getPosition()))
        hoveredArea = -1;
    else if (getPlusArea().contains(e.getPosition()))
        hoveredArea = 1;
    else
        hoveredArea = 0;
    if (hoveredArea != prev) repaint();
}

void OctaveControl::mouseExit(const juce::MouseEvent&) {
    hoveredArea = 0;
    repaint();
}

void OctaveControl::setOctave(int newOctave) {
    octave = juce::jlimit(-3, 3, newOctave);
    repaint();
}

void OctaveControl::changeOctave(int delta) {
    setOctave(octave + delta);
    if (onOctaveChanged) onOctaveChanged(octave);
}
