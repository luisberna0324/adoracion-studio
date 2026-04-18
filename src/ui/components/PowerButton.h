#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>
#include <functional>

class PowerButton : public juce::Component {
public:
    PowerButton();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    bool isPoweredOn() const { return on; }
    void setPoweredOn(bool powered);
    void setOnToggled(std::function<void(bool)> cb) { onToggled = std::move(cb); }

private:
    bool on = true;
    bool hovered = false;
    std::function<void(bool)> onToggled;

    static void drawPowerIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PowerButton)
};
