#include "PadsComponent.h"

namespace {
constexpr const char* kNoteNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
// QWERTY map: Z=C, S=C#, X=D, D=D#, C=E, V=F, G=F#, B=G, H=G#, N=A, J=A#, M=B
constexpr char kQwertyKeys[] = {
    'z', 's', 'x', 'd', 'c', 'v', 'g', 'b', 'h', 'n', 'j', 'm'
};
} // namespace

PadsComponent::PadsComponent(MTControlsAudioProcessor& proc, ChannelStripLookAndFeel& laf)
    : processor(proc) {
    for (int i = 0; i < kNoteCount; ++i) {
        notePads[i] = std::make_unique<NotePadButton>(kNoteNames[i], i);
        notePads[i]->setOnPressed([this, i] { onPadPressed(i); });
        notePads[i]->setOnReleased([this, i] { onPadReleased(i); });
        addAndMakeVisible(*notePads[i]);
    }

    octaveCtrl.setOnOctaveChanged([this](int oct) { currentOctave = oct; });
    addAndMakeVisible(octaveCtrl);

    powerBtn.setOnToggled([this](bool on) { applyPowerState(on); });
    addAndMakeVisible(powerBtn);

    channelStrip = std::make_unique<ChannelStripCard>(
        "Pads", laf,
        [this]() -> std::pair<float, float> {
            const float lvl = padLevel.load();
            return { lvl, lvl };
        },
        /*isCompact=*/true);
    channelStrip->setGainVisible(false);
    channelStrip->setSoloVisible(false);
    addAndMakeVisible(*channelStrip);

    setWantsKeyboardFocus(true);
}

PadsComponent::~PadsComponent() = default;

void PadsComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(UITheme::panelDark);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(UITheme::cardBorder);
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
}

void PadsComponent::resized() {
    // Grid 6 cols × 2 rows: x = 16 + col*78, y = 16 + row*78
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 6; ++col)
            notePads[row * 6 + col]->setBounds(16 + col * kStride, 16 + row * kStride, kPadSize, kPadSize);

    powerBtn.setBounds(16, 172, 70, 70);
    octaveCtrl.setBounds(332, 172, 144, 70);

    // Channel strip fills remaining width at right, full inner height
    const int stripX = 484;
    channelStrip->setBounds(stripX, 16, getWidth() - stripX - 4, getHeight() - 32);
}

bool PadsComponent::keyPressed(const juce::KeyPress& key) {
    const auto c = static_cast<char>(
        juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getTextCharacter())));
    for (int i = 0; i < kNoteCount; ++i)
        if (c == kQwertyKeys[i]) return true;
    return false;
}

bool PadsComponent::keyStateChanged(bool) {
    if (!powerBtn.isPoweredOn()) return false;
    for (int i = 0; i < kNoteCount; ++i) {
        const bool down = juce::KeyPress::isKeyCurrentlyDown(kQwertyKeys[i]);
        if (down && !notePads[i]->isActive())
            onPadPressed(i);
        else if (!down && notePads[i]->isActive())
            onPadReleased(i);
    }
    return true;
}

void PadsComponent::mouseDown(const juce::MouseEvent&) {
    grabKeyboardFocus();
}

void PadsComponent::refreshThemeColours() {
    if (channelStrip) channelStrip->refreshThemeColours();
    for (auto& pad : notePads) pad->repaint();
    octaveCtrl.repaint();
    powerBtn.repaint();
    repaint();
}

void PadsComponent::onPadPressed(int noteIndex) {
    notePads[noteIndex]->setActive(true);
    // TODO: trigger sample for MIDI note (60 + noteIndex + currentOctave * 12)
}

void PadsComponent::onPadReleased(int noteIndex) {
    notePads[noteIndex]->setActive(false);
    // TODO: stop sample for noteIndex
}

void PadsComponent::applyPowerState(bool on) {
    for (auto& pad : notePads) {
        pad->setInterceptsMouseClicks(on, false);
        pad->setAlpha(on ? 1.0f : 0.4f);
        if (!on) pad->setActive(false);
    }
    octaveCtrl.setInterceptsMouseClicks(on, false);
    octaveCtrl.setAlpha(on ? 1.0f : 0.4f);
    if (channelStrip) channelStrip->setMuteState(!on);
}
