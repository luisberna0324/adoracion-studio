#pragma once

#include <JuceHeader.h>

#include <functional>

namespace NativeAuthSession {

using Completion =
    std::function<void(const juce::String &callbackUrl,
                       const juce::String &errorMessage)>;

bool start(const juce::String &loginUrl, const juce::String &callbackScheme,
           Completion completion);
void cancel();

} // namespace NativeAuthSession
