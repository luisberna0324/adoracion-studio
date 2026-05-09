#include "NotePadButton.h"
#include <cmath>

namespace {
float midiLearnBlinkStrength() {
    const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double pulse =
        0.5 + (0.5 * std::sin(seconds * juce::MathConstants<double>::twoPi * 2.4));
    return static_cast<float>(juce::jmap(pulse, 0.36, 1.0));
}
} // namespace

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

    if (midiLearnSelectable) {
        const auto selectableColour = juce::Colour::fromRGB(78, 170, 255);
        const auto pendingColour = juce::Colour::fromRGB(255, 196, 92);
        const float pendingBlink = midiLearnPending ? midiLearnBlinkStrength() : 1.0f;
        g.setColour((midiLearnPending ? pendingColour : selectableColour)
                        .withAlpha(0.52f + (0.43f * pendingBlink)));
        g.drawRoundedRectangle(bounds.reduced(1.0f), radius - 1.0f,
                               midiLearnPending ? (1.5f + (0.9f * pendingBlink)) : 1.2f);
    }

    g.setColour(active ? UITheme::onAccent : UITheme::textPrimary);
    const float fontSize = juce::jlimit(13.0f, 20.0f,
                                        static_cast<float>(getHeight()) * 0.26f);
    g.setFont(juce::Font(juce::FontOptions(fontSize).withStyle("Bold")));
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

void NotePadButton::setMidiLearnSelectable(const bool selectable) {
    midiLearnSelectable = selectable;
    updateMidiLearnBlinkTimer();
    repaint();
}

void NotePadButton::setMidiLearnPending(const bool pending) {
    midiLearnPending = pending;
    updateMidiLearnBlinkTimer();
    repaint();
}

void NotePadButton::timerCallback() {
    repaint();
}

void NotePadButton::updateMidiLearnBlinkTimer() {
    const bool shouldBlink = midiLearnSelectable && midiLearnPending;
    if (shouldBlink) {
        if (!isTimerRunning())
            startTimerHz(24);
    } else if (isTimerRunning()) {
        stopTimer();
    }
}
