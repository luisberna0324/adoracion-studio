#pragma once

#include <JuceHeader.h>

// The editor changes its limits between welcome/login and the main workspace.
// Query its current constrainer for each resize instead of copying launch limits.
class NativeEditorBoundsConstrainer final : public juce::BorderedComponentBoundsConstrainer {
public:
  NativeEditorBoundsConstrainer(juce::DocumentWindow &windowToUse,
                               juce::AudioProcessor &processorToUse)
      : window(windowToUse), processor(processorToUse) {}

  juce::ComponentBoundsConstrainer *getWrappedConstrainer() const override {
    auto *editor = processor.getActiveEditor();
    return editor != nullptr ? editor->getConstrainer() : nullptr;
  }

  juce::BorderSize<int> getAdditionalBorder() const override {
    auto border = window.getContentComponentBorder();
    if (const auto *peer = window.getPeer())
      if (const auto nativeFrame = peer->getFrameSizeIfPresent())
        border = border.addedTo(*nativeFrame);
    return border;
  }

private:
  juce::DocumentWindow &window;
  juce::AudioProcessor &processor;
};
