#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Use the attached scene after its rotation, then propagate that size through
// the standalone content component to the editor. Call on the message thread.
void synchroniseIOSFullscreenWindow(juce::ResizableWindow &window);
