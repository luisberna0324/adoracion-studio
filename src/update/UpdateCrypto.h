#pragma once

#include <juce_core/juce_core.h>

namespace PlayWorshipUpdate {

juce::String sha256ForFile(const juce::File &file);
bool constantTimeEqualsIgnoreCase(const juce::String &a, const juce::String &b);

} // namespace PlayWorshipUpdate
