#include "NativeIpadChoicePicker.h"

namespace NativeIpadChoicePicker {

bool isAvailable() { return false; }

bool show(juce::Component&, const std::vector<juce::String>&, int,
          std::function<void(int)>) {
  return false;
}

} // namespace NativeIpadChoicePicker
