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

juce::String withMidiAssignmentTooltip(const juce::String& baseText,
                                       const juce::String& midiCode) {
    const auto trimmedCode = midiCode.trim();
    if (trimmedCode.isEmpty())
        return baseText;
    if (baseText.trim().isEmpty())
        return "MIDI: " + trimmedCode;
    return baseText + " | MIDI: " + trimmedCode;
}

juce::String idlePadsStatusText() {
    return "Tap a pad to load";
}
} // namespace

PadsComponent::PadsComponent(MTControlsAudioProcessor& proc, ChannelStripLookAndFeel& laf, PlaybackSession *sessionToUse)
    : processor(proc), localSession(proc), session(sessionToUse ? *sessionToUse : localSession) {
    for (int i = 0; i < kNoteCount; ++i) {
        const auto index = static_cast<size_t>(i);
        notePads[index] = std::make_unique<NotePadButton>(kNoteNames[index], i);
        notePads[index]->setOnPressed([this, i] { onPadPressed(i); });
        notePads[index]->setOnReleased([this, i] { onPadReleased(i); });
        addAndMakeVisible(*notePads[index]);
    }

    octaveCtrl.setOnOctaveChanged([this](int oct) { currentOctave = oct; });

    powerBtn.setOnToggled([this](bool on) { applyPowerState(on); });
    powerBtn.setTooltip("Pads power");
    addAndMakeVisible(powerBtn);

    channelStrip = std::make_unique<ChannelStripCard>(
        "Pads", laf,
        [this]() -> std::pair<float, float> {
            const float lvl = session.getPadsOutputLevel();
            return { lvl, lvl };
        },
        /*isCompact=*/false);
    channelStrip->setSoloVisible(false);
    channelStrip->setMuteVisible(false);
    channelStrip->setPowerVisible(false);
    channelStrip->setGainVisible(true);
    channelStrip->setCardBgColour(UITheme::cardBg);
    channelStrip->setChannelBandColours(UITheme::accent, UITheme::onAccent);
    channelStrip->setVolume(session.getPadsVolume());
    channelStrip->setGain(session.getPadsGain());
    channelStrip->setOnVolumeChanged(
        [this](float value) { session.setPadsVolume(value); });
    channelStrip->setOnGainChanged(
        [this](float value) { session.setPadsGain(value); });
    channelStrip->setMidiLearnTargetIds(processor.midiTargetPadsVolume,
                                        processor.midiTargetPadsGain, -1, -1,
                                        -1);
    channelStrip->setOnMidiLearnTargetSelected([this](const int targetId) {
      if (onMidiLearnTargetSelected)
        onMidiLearnTargetSelected(targetId);
    });
    addAndMakeVisible(*channelStrip);

    session.refreshPadsRoutingFromSettings();
    presetSubtitle = session.getPadsPackStatusText();
    lastPackReady = session.isPadsPackLoaded();
    if (!lastPackReady && presetSubtitle.trim().isEmpty())
        presetSubtitle = idlePadsStatusText();

    applyPowerState(true);
    refreshMidiLearnTooltips();
    setWantsKeyboardFocus(true);
    startTimerHz(20);
}

PadsComponent::~PadsComponent() {
    stopTimer();
}

void PadsComponent::paint(juce::Graphics& g) {
    const bool compact = (responsiveProfile == ResponsiveLayoutProfile::Compact);
    const bool large = (responsiveProfile == ResponsiveLayoutProfile::Large);
    if (!popoverPresentation) {
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.24f));
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(UITheme::cardBorder.withAlpha(0.88f));
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
    }

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
        const float artFont = compact ? 22.0f : (large ? 30.0f : 28.0f);
        g.setFont(juce::FontOptions(artFont).withStyle("Bold").withKerningFactor(0.05f));
        g.drawFittedText("LA", art, juce::Justification::centred, 1);

        const auto titleArea = content.removeFromTop(content.getHeight() / 2 + 4);
        g.setColour(UITheme::textPrimary);
        const float titleFont = compact ? 15.0f : (large ? 18.0f : 17.0f);
        g.setFont(juce::FontOptions(titleFont).withStyle("Bold"));
        g.drawFittedText(presetTitle, titleArea,
                         juce::Justification::centredLeft, 1);

        g.setColour(UITheme::textSecondary.withAlpha(subtitleAlpha));
        const float subtitleFont = compact ? 11.0f : 12.0f;
        g.setFont(juce::FontOptions(subtitleFont));
        g.drawFittedText(presetSubtitle, content,
                         juce::Justification::centredLeft, 1);
    }

    if (midiLearnActive) {
        const auto selectableColour = juce::Colour::fromRGB(78, 170, 255);
        const auto pendingColour = juce::Colour::fromRGB(255, 196, 92);
        auto powerFrame = powerBtn.getBounds().toFloat().reduced(1.0f);
        const bool powerPending = (midiLearnPendingTargetId == processor.midiTargetPadsPower);
        const float blink = powerPending ? midiLearnBlinkStrength()
                                         : midiLearnSoftBlinkStrength();
        const auto frameColour = powerPending ? pendingColour : selectableColour;
        g.setColour(frameColour.withAlpha(powerPending ? (0.20f + (0.16f * blink))
                                                       : (0.13f + (0.06f * blink))));
        g.fillRoundedRectangle(powerFrame, 7.0f);
        g.setColour(frameColour.withAlpha(powerPending ? (0.54f + (0.40f * blink))
                                                       : (0.52f + (0.20f * blink))));
        g.drawRoundedRectangle(powerFrame, 7.0f,
                               powerPending ? (1.5f + (0.9f * blink))
                                            : (1.0f + (0.35f * blink)));
    }
}

void PadsComponent::resized() {
    const bool compact = (responsiveProfile == ResponsiveLayoutProfile::Compact);
    const bool large = (responsiveProfile == ResponsiveLayoutProfile::Large);
    const int basePadSize = compact ? 62 : (large ? 78 : 70);
    const int gridInset = compact ? 12 : (large ? 18 : 16);
    const int stripW = compact ? 78 : (large ? 98 : 90);
    const int stripRightInset = compact ? 6 : 8;
    const int stripLeftGap = compact ? 8 : (large ? 12 : 10);
    const int bottomRowHeight = compact ? 56 : (large ? 68 : 64);
    const int preferredPadGap = compact ? 8 : (large ? 12 : 10);
    const int bottomRowGap = preferredPadGap;
    const int powerW = compact ? 58 : 64;
    const int rowGap = compact ? 8 : 10;
    channelStrip->setLayoutScale(compact ? 0.9f : (large ? 1.06f : 1.0f));

    auto contentArea = getLocalBounds().reduced(gridInset);
    contentArea.removeFromRight(stripRightInset);
    auto stripArea = contentArea.removeFromRight(stripW);
    auto gridArea = contentArea;
    gridArea.removeFromRight(stripLeftGap);
    auto padsArea = gridArea;
    auto bottomRow = padsArea.removeFromBottom(bottomRowHeight);
    padsArea.removeFromBottom(bottomRowGap);

    constexpr int kCols = 6;
    constexpr int kRows = 2;
    const int minGapX = compact ? 6 : (large ? 10 : 8);
    const int maxGapX = compact ? 12 : (large ? 16 : 14);
    const int maxPadSizeForWidth =
        juce::jmax(36, (padsArea.getWidth() - (minGapX * (kCols - 1))) / kCols);
    const int maxPadSizeForHeight =
        juce::jmax(28, (padsArea.getHeight() - preferredPadGap) / kRows);
    const int padSize = juce::jmin(basePadSize, maxPadSizeForWidth,
                                   maxPadSizeForHeight);
    const int contentW = (kCols * padSize);
    const int contentH = (kRows * padSize);
    const int availableGapX = juce::jmax(0, padsArea.getWidth() - contentW);
    const int rawGapX = (kCols > 1) ? (availableGapX / (kCols - 1)) : 0;
    const int gapX = juce::jlimit(minGapX, maxGapX, rawGapX);

    // Keep visual rhythm consistent between horizontal and vertical spacing.
    const int availableGapY = juce::jmax(0, padsArea.getHeight() - contentH);
    const int gapY = juce::jmin(preferredPadGap, availableGapY);
    const int gridW = contentW + ((kCols - 1) * gapX);
    const int gridH = contentH + ((kRows - 1) * gapY);
    const int startX = padsArea.getX() + juce::jmax(0, (padsArea.getWidth() - gridW) / 2);
    const int startY = padsArea.getY() +
                       juce::jmax(0, (padsArea.getHeight() - gridH) / 2);

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            const int x = startX + col * (padSize + gapX);
            const int y = startY + row * (padSize + gapY);
            const auto padIndex = static_cast<size_t>(row * kCols + col);
            notePads[padIndex]->setBounds(x, y, padSize, padSize);
        }
    }

    powerBtn.setBounds(bottomRow.removeFromLeft(powerW));
    bottomRow.removeFromLeft(rowGap);
    presetCardBounds = bottomRow;

    const int stripTop = juce::jmax(gridArea.getY(), startY - (compact ? 2 : 3));
    const int stripBottom = juce::jmin(gridArea.getBottom(), presetCardBounds.getBottom());
    channelStrip->setBounds(stripArea.getX(), stripTop, stripArea.getWidth(),
                            juce::jmax(40, stripBottom - stripTop));
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
    presetSubtitle = session.getPadsPackStatusText();
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

void PadsComponent::setResponsiveProfile(const ResponsiveLayoutProfile profile) {
    if (responsiveProfile == profile)
        return;
    responsiveProfile = profile;
    resized();
    repaint();
}

void PadsComponent::setPopoverPresentation(const bool enabled) {
    if (popoverPresentation == enabled)
        return;
    popoverPresentation = enabled;
    repaint();
}

void PadsComponent::timerCallback() {
    if (padsLoading.load(std::memory_order_relaxed)) {
        if (!session.isPadsPreloadRunning()) {
            padsLoading.store(false, std::memory_order_release);
            lastPackReady = session.isPadsPackLoaded();
            presetSubtitle = session.getPadsPackStatusText();
            subtitleAlpha = 1.0f;
            applyPowerState(padsPoweredOn);
            repaint();
        } else {
            loadingBeatPhase += 0.24f;
            const float pulse = 0.66f + (0.34f * (0.5f + 0.5f * std::sin(loadingBeatPhase)));
            subtitleAlpha = juce::jlimit(0.45f, 1.0f, pulse);
            const auto runtimeStatus = session.getPadsPackStatusText();
            presetSubtitle = runtimeStatus.isNotEmpty() ? runtimeStatus : "Descargando recursos... / Downloading resources...";
            repaint();
            return;
        }
    }

    subtitleAlpha = 1.0f;
    const bool packReady = session.isPadsPackLoaded();
    if (packReady != lastPackReady) {
        lastPackReady = packReady;
        applyPowerState(padsPoweredOn);
    }

    const auto status = session.getPadsPackStatusText();
    if (status != presetSubtitle) {
        presetSubtitle = status.isNotEmpty()
                             ? status
                             : (packReady ? juce::String{} : idlePadsStatusText());
        repaint();
    }

    if (channelStrip != nullptr && !channelStrip->isMouseButtonDown()) {
        channelStrip->setVolume(session.getPadsVolume());
        channelStrip->setGain(session.getPadsGain());
    }

    if (!midiLearnActive) {
        const bool runtimePadsEnabled = session.isPadsEnabled();
        if (runtimePadsEnabled != padsPoweredOn)
            applyPowerState(runtimePadsEnabled);

        const int runtimeActiveNote = session.getActivePadNoteIndex();
        if (runtimeActiveNote != activeNoteIndex) {
            if (juce::isPositiveAndBelow(activeNoteIndex, kNoteCount))
                notePads[static_cast<size_t>(activeNoteIndex)]->setActive(false);
            if (juce::isPositiveAndBelow(runtimeActiveNote, kNoteCount))
                notePads[static_cast<size_t>(runtimeActiveNote)]->setActive(true);
            activeNoteIndex = runtimeActiveNote;
        }
    }

    if (midiLearnActive)
        repaint();
}

void PadsComponent::startPadsLoadingAsync() {
    if (session.isPadsPackLoaded()) {
        padsLoading.store(false, std::memory_order_release);
        return;
    }
    padsLoading.store(true, std::memory_order_release);
    presetSubtitle = "Loading pads...";
    subtitleAlpha = 0.7f;
    loadingBeatPhase = 0.0f;
    session.startPadsPreloadAsync();
}

void PadsComponent::onPadPressed(int noteIndex) {
    if (midiLearnActive) {
        const int targetId = processor.midiTargetPadsNoteBase +
                             juce::jlimit(0, kNoteCount - 1, noteIndex);
        if (onMidiLearnTargetSelected)
            onMidiLearnTargetSelected(targetId);
        return;
    }
    if (padsLoading.load(std::memory_order_relaxed)) {
        session.triggerPadNote(noteIndex, currentOctave);
        presetSubtitle = "Loading pads...";
        return;
    }

    if (!session.isPadsPackLoaded()) {
        startPadsLoadingAsync();
        session.triggerPadNote(noteIndex, currentOctave);
        presetSubtitle = "Loading pads...";
        applyPowerState(padsPoweredOn);
        repaint();
        return;
    }

    if (!session.ensurePadsPackReady()) {
        presetSubtitle = session.getPadsPackStatusText();
        applyPowerState(padsPoweredOn);
        repaint();
        return;
    }
    presetSubtitle = session.getPadsPackStatusText();
    applyPowerState(padsPoweredOn);

    if (activeNoteIndex == noteIndex) {
        session.stopPadNote(noteIndex);
        notePads[static_cast<size_t>(noteIndex)]->setActive(false);
        activeNoteIndex = -1;
        return;
    }

    if (juce::isPositiveAndBelow(activeNoteIndex, kNoteCount))
        notePads[static_cast<size_t>(activeNoteIndex)]->setActive(false);

    notePads[static_cast<size_t>(noteIndex)]->setActive(true);
    activeNoteIndex = noteIndex;
    session.triggerPadNote(noteIndex, currentOctave);
}

void PadsComponent::onPadReleased(int noteIndex) {
    juce::ignoreUnused(noteIndex);
}

bool PadsComponent::followSongKey(const juce::String& songKey,
                                  const int semitoneOffset) {
    const int runtimeActiveNote = session.getActivePadNoteIndex();
    if (!padsPoweredOn ||
        !juce::isPositiveAndBelow(runtimeActiveNote, kNoteCount))
        return false;

    if (juce::isPositiveAndBelow(activeNoteIndex, kNoteCount) &&
        activeNoteIndex != runtimeActiveNote)
        notePads[static_cast<size_t>(activeNoteIndex)]->setActive(false);
    activeNoteIndex = runtimeActiveNote;

    const int songNoteIndex = PadsEngine::noteIndexForSongKey(songKey);
    if (songNoteIndex < 0)
        return false;
    const int targetNoteIndex =
        (songNoteIndex + (semitoneOffset % kNoteCount) + kNoteCount) % kNoteCount;
    if (targetNoteIndex == activeNoteIndex)
        return true;

    notePads[static_cast<size_t>(activeNoteIndex)]->setActive(false);
    notePads[static_cast<size_t>(targetNoteIndex)]->setActive(true);
    activeNoteIndex = targetNoteIndex;
    session.triggerPadNoteForSongTransition(targetNoteIndex, currentOctave);
    return true;
}

void PadsComponent::applyPowerState(bool on) {
    if (midiLearnActive) {
        if (onMidiLearnTargetSelected)
            onMidiLearnTargetSelected(processor.midiTargetPadsPower);
        powerBtn.setPoweredOn(padsPoweredOn);
        return;
    }
    padsPoweredOn = on;
    session.setPadsEnabled(on);
    const bool interactionEnabled = on;
    const bool packReady = session.isPadsPackLoaded();
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
    if (interactionEnabled && !packReady && !padsLoading.load(std::memory_order_relaxed))
        presetSubtitle = idlePadsStatusText();
}

void PadsComponent::setMidiLearnActive(const bool active) {
    midiLearnActive = active;
    if (!midiLearnActive)
        midiLearnPendingTargetId = -1;
    for (int i = 0; i < kNoteCount; ++i)
        notePads[static_cast<size_t>(i)]->setMidiLearnSelectable(midiLearnActive);
    if (channelStrip != nullptr)
        channelStrip->setMidiLearnSelectable(midiLearnActive);
    setMidiLearnPendingTarget(midiLearnPendingTargetId);
    refreshMidiLearnTooltips();
    repaint();
}

void PadsComponent::setMidiLearnPendingTarget(const int targetId) {
    midiLearnPendingTargetId = targetId;
    for (int i = 0; i < kNoteCount; ++i) {
        const int noteTargetId = processor.midiTargetPadsNoteBase + i;
        const bool pending = (noteTargetId == targetId);
        notePads[static_cast<size_t>(i)]->setMidiLearnPending(pending);
        notePads[static_cast<size_t>(i)]->setMidiLearnAssignedLabel(
            midiLearnBindingLabelProvider != nullptr
                ? midiLearnBindingLabelProvider(noteTargetId).trim()
                : juce::String{});
    }
    if (channelStrip != nullptr)
        channelStrip->setMidiLearnPendingTarget(targetId);
    refreshMidiLearnTooltips();
    repaint();
}

void PadsComponent::setMidiLearnBindingLabelProvider(
    std::function<juce::String(int)> cb) {
    midiLearnBindingLabelProvider = std::move(cb);
    if (channelStrip != nullptr)
        channelStrip->setMidiLearnBindingLabelProvider(midiLearnBindingLabelProvider);
    setMidiLearnPendingTarget(midiLearnPendingTargetId);
    refreshMidiLearnTooltips();
}

void PadsComponent::setOnMidiLearnTargetSelected(std::function<void(int)> cb) {
    onMidiLearnTargetSelected = std::move(cb);
}

void PadsComponent::refreshMidiLearnTooltips() {
    const juce::String midiCode =
        midiLearnBindingLabelProvider != nullptr
            ? midiLearnBindingLabelProvider(processor.midiTargetPadsPower).trim()
            : juce::String{};
    powerBtn.setTooltip(withMidiAssignmentTooltip("Pads power", midiCode));
}
