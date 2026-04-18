#pragma once

#include "../../PluginProcessor.h"
#include "../UiCommon.h"
#include "../components/ChannelStripCard.h"
#include "../components/ChannelStripLookAndFeel.h"
#include "../components/NotePadButton.h"
#include "../components/OctaveControl.h"
#include "../components/PowerButton.h"
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>

class PadsComponent : public juce::Component {
public:
    PadsComponent(MTControlsAudioProcessor& processor, ChannelStripLookAndFeel& laf);
    ~PadsComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;

    void mouseDown(const juce::MouseEvent&) override;

    void refreshThemeColours();

private:
    static constexpr int kNoteCount = 12;
    static constexpr int kPadSize = 70;
    static constexpr int kStride = 78; // kPadSize + 8 gap

    MTControlsAudioProcessor& processor;

    std::array<std::unique_ptr<NotePadButton>, kNoteCount> notePads;
    OctaveControl octaveCtrl;
    PowerButton powerBtn;
    std::unique_ptr<ChannelStripCard> channelStrip;

    int currentOctave = 0;
    std::atomic<float> padLevel { 0.0f };

    void onPadPressed(int noteIndex);
    void onPadReleased(int noteIndex);
    void applyPowerState(bool on);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadsComponent)
};
