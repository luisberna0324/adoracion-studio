#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>
#include <functional>

class NotePadButton : public juce::Component {
public:
    NotePadButton(const juce::String& noteName, int noteIndex);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    void setActive(bool active);
    bool isActive() const { return active; }

    const juce::String& getNoteName() const { return noteName; }
    int getNoteIndex() const { return noteIndex; }

    void setOnPressed(std::function<void()> cb) { onPressed = std::move(cb); }
    void setOnReleased(std::function<void()> cb) { onReleased = std::move(cb); }

private:
    juce::String noteName;
    int noteIndex;
    bool hovered = false;
    bool pressed = false;
    bool active = false;
    std::function<void()> onPressed;
    std::function<void()> onReleased;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NotePadButton)
};
