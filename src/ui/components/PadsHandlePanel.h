#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>
#include <functional>

class PadsHandlePanel final : public juce::Component {
public:
    PadsHandlePanel();

    std::function<void()> onToggle;

    void setCollapsed(bool collapsed);
    bool isCollapsedState() const { return isCollapsed; }

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    bool isCollapsed = true;
    bool isHovered = false;
    bool isMouseDown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadsHandlePanel)
};
