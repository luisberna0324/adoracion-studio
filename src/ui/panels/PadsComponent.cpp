#include "PadsComponent.h"
#include <cmath>

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

    powerBtn.setOnToggled([this](bool on) { applyPowerState(on); });
    addAndMakeVisible(powerBtn);

    channelStrip = std::make_unique<ChannelStripCard>(
        "Pads", laf,
        [this]() -> std::pair<float, float> {
            const float lvl = processor.getPadsOutputLevel();
            return { lvl, lvl };
        },
        /*isCompact=*/false);
    channelStrip->setSoloVisible(false);
    channelStrip->setMuteVisible(false);
    channelStrip->setPowerVisible(false);
    channelStrip->setGainVisible(true);
    channelStrip->setCardBgColour(UITheme::cardBg);
    channelStrip->setChannelBandColours(UITheme::accent, UITheme::onAccent);
    channelStrip->setVolume(processor.getPadsVolume());
    channelStrip->setGain(processor.getPadsGain());
    channelStrip->setOnVolumeChanged(
        [this](float value) { processor.setPadsVolume(value); });
    channelStrip->setOnGainChanged(
        [this](float value) { processor.setPadsGain(value); });
    addAndMakeVisible(*channelStrip);

    processor.refreshPadsRoutingFromSettings();
    presetSubtitle = processor.getPadsPackStatusText();
    lastPackReady = processor.isPadsPackLoaded();

    applyPowerState(true);
    setWantsKeyboardFocus(true);
    startPadsLoadingAsync();
    startTimerHz(30);
}

PadsComponent::~PadsComponent() {
    stopTimer();
    stopPadsLoadingThread();
}

void PadsComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(UITheme::panelDark);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(UITheme::cardBorder);
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

    if (!presetCardBounds.isEmpty()) {
        const auto card = presetCardBounds.toFloat();
        g.setColour(UITheme::cardBg);
        g.fillRoundedRectangle(card, 8.0f);
        g.setColour(UITheme::cardBorder);
        g.drawRoundedRectangle(card, 8.0f, 1.0f);

        auto content = presetCardBounds.reduced(10, 8);
        const int artSize = juce::jmin(54, content.getHeight());
        auto art = juce::Rectangle<int>(artSize, artSize)
                       .withCentre({content.getX() + artSize / 2,
                                    content.getCentreY()});
        content.removeFromLeft(artSize + 10);

        juce::ColourGradient artGrad(UITheme::accentSoft.withAlpha(0.95f),
                                     art.toFloat().getTopLeft(),
                                     juce::Colour::fromRGB(29, 95, 230),
                                     art.toFloat().getBottomRight(), false);
        g.setGradientFill(artGrad);
        g.fillRoundedRectangle(art.toFloat(), 7.0f);
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.setFont(juce::FontOptions(28.0f).withStyle("Bold").withKerningFactor(0.05f));
        g.drawFittedText("LA", art, juce::Justification::centred, 1);

        const auto titleArea = content.removeFromTop(content.getHeight() / 2 + 4);
        g.setColour(UITheme::textPrimary);
        g.setFont(juce::FontOptions(17.0f).withStyle("Bold"));
        g.drawFittedText(presetTitle, titleArea,
                         juce::Justification::centredLeft, 1);

        g.setColour(UITheme::textSecondary.withAlpha(subtitleAlpha));
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(presetSubtitle, content,
                         juce::Justification::centredLeft, 1);
    }
}

void PadsComponent::resized() {
    // Channel strip: standard layout is 90 px wide, anchored to right edge
    constexpr int kStripW = 90;
    constexpr int kStripRightInset = 8;
    constexpr int kStripLeftGap = 10;
    channelStrip->setBounds(getWidth() - kStripW - kStripRightInset, 16, kStripW,
                            getHeight() - 32);

    auto gridArea = getLocalBounds().reduced(16);
    gridArea.removeFromRight(kStripW + kStripLeftGap);
    constexpr int kBottomRowHeight = 64;
    constexpr int kBottomRowGap = 10;
    auto padsArea = gridArea;
    auto bottomRow = padsArea.removeFromBottom(kBottomRowHeight);
    padsArea.removeFromBottom(kBottomRowGap);

    constexpr int kCols = 6;
    constexpr int kRows = 2;
    const int contentW = (kCols * kPadSize);
    const int contentH = (kRows * kPadSize);
    const int extraW = juce::jmax(0, padsArea.getWidth() - contentW);
    const int gapX = juce::jmax(8, extraW / juce::jmax(1, kCols - 1));
    // Keep rows visually tight/symmetric; avoid oversized vertical gaps.
    const int maxRowGap = 14;
    const int minRowGap = 8;
    const int gapY = juce::jlimit(minRowGap, maxRowGap,
                                  padsArea.getHeight() - contentH);
    const int gridW = contentW + ((kCols - 1) * gapX);
    const int startX = padsArea.getX() + juce::jmax(0, (padsArea.getWidth() - gridW) / 2);
    const int topInset = 2;
    const int startY = padsArea.getY() + topInset;

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            const int x = startX + col * (kPadSize + gapX);
            const int y = startY + row * (kPadSize + gapY);
            notePads[row * kCols + col]->setBounds(x, y, kPadSize, kPadSize);
        }
    }

    constexpr int kPowerW = 64;
    constexpr int kRowGap = 10;
    powerBtn.setBounds(bottomRow.removeFromLeft(kPowerW));
    bottomRow.removeFromLeft(kRowGap);
    presetCardBounds = bottomRow;
}

bool PadsComponent::keyPressed(const juce::KeyPress& key) {
    const auto c = static_cast<char>(
        juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getTextCharacter())));
    for (int i = 0; i < kNoteCount; ++i)
        if (c == kQwertyKeys[i]) return true;
    return false;
}

bool PadsComponent::keyStateChanged(bool) {
    if (!padsPoweredOn) return false;
    for (int i = 0; i < kNoteCount; ++i) {
        const bool down = juce::KeyPress::isKeyCurrentlyDown(kQwertyKeys[i]);
        if (down && !qwertyWasDown[static_cast<size_t>(i)])
            onPadPressed(i);
        qwertyWasDown[static_cast<size_t>(i)] = down;
    }
    return true;
}

void PadsComponent::mouseDown(const juce::MouseEvent&) {
    grabKeyboardFocus();
}

void PadsComponent::refreshThemeColours() {
    presetSubtitle = processor.getPadsPackStatusText();
    applyPowerState(padsPoweredOn);
    if (channelStrip) {
        channelStrip->setCardBgColour(UITheme::cardBg);
        channelStrip->setChannelBandColours(UITheme::accent, UITheme::onAccent);
        channelStrip->refreshThemeColours();
    }
    for (auto& pad : notePads) pad->repaint();
    powerBtn.repaint();
    repaint();
}

void PadsComponent::timerCallback() {
    if (padsLoading.load(std::memory_order_relaxed)) {
        loadingBeatPhase += 0.24f;
        const float pulse = 0.66f + (0.34f * (0.5f + 0.5f * std::sin(loadingBeatPhase)));
        subtitleAlpha = juce::jlimit(0.45f, 1.0f, pulse);
        const auto runtimeStatus = processor.getPadsPackStatusText();
        presetSubtitle = runtimeStatus.isNotEmpty() ? runtimeStatus : "Descargando recursos... / Downloading resources...";
        repaint();
        return;
    }

    subtitleAlpha = 1.0f;
    const bool packReady = processor.isPadsPackLoaded();
    if (packReady != lastPackReady) {
        lastPackReady = packReady;
        applyPowerState(padsPoweredOn);
    }

    const auto status = processor.getPadsPackStatusText();
    if (status != presetSubtitle) {
        presetSubtitle = status;
        repaint();
    }
}

void PadsComponent::startPadsLoadingAsync() {
    if (processor.isPadsPackLoaded())
        return;
    if (padsLoading.exchange(true, std::memory_order_acq_rel))
        return;

    stopPadsLoadingThread();
    presetSubtitle = "Loading pads...";
    subtitleAlpha = 0.7f;
    loadingBeatPhase = 0.0f;

    padsLoaderThread = std::thread([this] {
        processor.ensurePadsPackReady();
        padsLoading.store(false, std::memory_order_release);
    });
}

void PadsComponent::stopPadsLoadingThread() {
    if (padsLoaderThread.joinable())
        padsLoaderThread.join();
}

void PadsComponent::onPadPressed(int noteIndex) {
    if (padsLoading.load(std::memory_order_relaxed)) {
        presetSubtitle = "Loading pads...";
        return;
    }

    if (!processor.isPadsPackLoaded()) {
        startPadsLoadingAsync();
        presetSubtitle = "Loading pads...";
        applyPowerState(padsPoweredOn);
        repaint();
        return;
    }

    if (!processor.ensurePadsPackReady()) {
        presetSubtitle = processor.getPadsPackStatusText();
        applyPowerState(padsPoweredOn);
        repaint();
        return;
    }
    presetSubtitle = processor.getPadsPackStatusText();
    applyPowerState(padsPoweredOn);

    if (activeNoteIndex == noteIndex) {
        processor.stopPadNote(noteIndex);
        notePads[static_cast<size_t>(noteIndex)]->setActive(false);
        activeNoteIndex = -1;
        return;
    }

    if (juce::isPositiveAndBelow(activeNoteIndex, kNoteCount))
        notePads[static_cast<size_t>(activeNoteIndex)]->setActive(false);

    notePads[static_cast<size_t>(noteIndex)]->setActive(true);
    activeNoteIndex = noteIndex;
    processor.triggerPadNote(noteIndex, currentOctave);
}

void PadsComponent::onPadReleased(int noteIndex) {
    juce::ignoreUnused(noteIndex);
}

void PadsComponent::applyPowerState(bool on) {
    padsPoweredOn = on;
    processor.setPadsEnabled(on);
    const bool interactionEnabled = on;
    const bool packReady = processor.isPadsPackLoaded();
    for (auto& pad : notePads) {
        pad->setInterceptsMouseClicks(interactionEnabled, false);
        pad->setAlpha(interactionEnabled ? (packReady ? 1.0f : 0.82f) : 0.4f);
        if (!interactionEnabled)
            pad->setActive(false);
    }
    if (!interactionEnabled)
        activeNoteIndex = -1;
    qwertyWasDown.fill(false);
    powerBtn.setPoweredOn(on);
    if (channelStrip) {
        channelStrip->setMuteState(!on);
    }
}
