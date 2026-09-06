#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace NativeIpadChoicePicker {

bool isAvailable();
bool show(juce::Component& anchor, const std::vector<juce::String>& items,
          int selectedIndex, std::function<void(int)> completion);

} // namespace NativeIpadChoicePicker
