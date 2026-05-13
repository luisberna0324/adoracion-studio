#include "NotePadButton.h"
#include <cmath>

namespace {
float midiLearnBlinkStrength() {
    const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double pulse =
        0.5 + (0.5 * std::sin(seconds * juce::MathConstants<double>::twoPi * 2.4));
    return static_cast<float>(juce::jmap(pulse, 0.36, 1.0));
}

float midiLearnSoftBlinkStrength() {
    const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double pulse =
        0.5 + (0.5 * std::sin(seconds * juce::MathConstants<double>::twoPi * 1.1));
    return static_cast<float>(juce::jmap(pulse, 0.82, 1.0));
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
        const bool assigned =
            midiLearnPending && midiLearnAssignedLabel.trim().isNotEmpty();
        const float pendingBlink =
            midiLearnPending ? midiLearnBlinkStrength()
                            : midiLearnSoftBlinkStrength();
        const auto frameColour =
            assigned ? selectableColour
                     : (midiLearnPending ? pendingColour : selectableColour);
        g.setColour(frameColour.withAlpha(
            midiLearnPending ? (0.20f + (0.16f * pendingBlink))
                            : (0.13f + (0.06f * pendingBlink))));
        g.fillRoundedRectangle(bounds.reduced(1.0f), radius - 1.0f);
        g.setColour(frameColour.withAlpha(
            midiLearnPending ? (0.52f + (0.43f * pendingBlink))
                            : (0.50f + (0.20f * pendingBlink))));
        g.drawRoundedRectangle(bounds.reduced(1.0f), radius - 1.0f,
                               midiLearnPending ? (1.5f + (0.9f * pendingBlink))
                                              : (1.0f + (0.35f * pendingBlink)));
    }

    g.setColour(active ? UITheme::onAccent : UITheme::textPrimary);
    const float fontSize = juce::jlimit(13.0f, 20.0f,
                                        static_cast<float>(getHeight()) * 0.26f);
    g.setFont(juce::Font(juce::FontOptions(fontSize).withStyle("Bold")));
    g.drawFittedText(noteName, getLocalBounds(), juce::Justification::centred, 1);

    if (midiLearnSelectable && midiLearnPending &&
        midiLearnAssignedLabel.trim().isNotEmpty()) {
        const juce::String label = midiLearnAssignedLabel.trim();
        const juce::Font labelFont(
            juce::FontOptions(11.0f).withStyle("Bold").withKerningFactor(0.03f));
        const auto textBounds = bounds.reduced(5.0f, 5.0f);
        const float textWidth =
            static_cast<float>(label.length()) * labelFont.getHeight() * 0.62f;
        const float pillW = juce::jlimit(22.0f, textBounds.getWidth(),
                                         textWidth + 12.0f);
        const float pillH = juce::jlimit(12.0f, textBounds.getHeight(), 18.0f);
        auto pill = juce::Rectangle<float>(pillW, pillH)
                        .withCentre(textBounds.getCentre());
        g.setColour(juce::Colours::black.withAlpha(0.58f));
        g.fillRoundedRectangle(pill, pillH * 0.45f);
        g.setColour(juce::Colour::fromRGB(78, 170, 255).withAlpha(0.90f));
        g.drawRoundedRectangle(pill, pillH * 0.45f, 0.9f);
        g.setColour(UITheme::textPrimary);
        g.setFont(labelFont);
        g.drawFittedText(label, pill.toNearestInt(), juce::Justification::centred, 1);
    }
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

void NotePadButton::setMidiLearnAssignedLabel(const juce::String& label) {
    midiLearnAssignedLabel = label;
    repaint();
}

void NotePadButton::timerCallback() {
    repaint();
}

void NotePadButton::updateMidiLearnBlinkTimer() {
    const bool shouldBlink = midiLearnSelectable;
    if (shouldBlink) {
        if (!isTimerRunning())
            startTimerHz(24);
    } else if (isTimerRunning()) {
        stopTimer();
    }
}
