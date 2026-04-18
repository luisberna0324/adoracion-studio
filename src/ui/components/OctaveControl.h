#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>
#include <functional>

class OctaveControl : public juce::Component {
public:
    OctaveControl();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    int getOctave() const { return octave; }
    void setOctave(int newOctave);
    void setOnOctaveChanged(std::function<void(int)> cb) { onOctaveChanged = std::move(cb); }

private:
    int octave = 0;
    int hoveredArea = 0; // -1 minus, 0 none, 1 plus
    std::function<void(int)> onOctaveChanged;

    juce::Rectangle<int> getMinusArea() const;
    juce::Rectangle<int> getValueArea() const;
    juce::Rectangle<int> getPlusArea() const;

    juce::String octaveLabel() const;
    void changeOctave(int delta);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OctaveControl)
};
