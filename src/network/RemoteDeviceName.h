#pragma once

#include <JuceHeader.h>

namespace playworship::remote {

// Returns the user-visible device name when the platform makes it available.
juce::String getRemoteDeviceName();

inline juce::String makeRemoteServerName(const juce::String &alias = {}) {
  auto name = alias.trim();
  if (name.isEmpty())
    name = getRemoteDeviceName().trim();
  if (name.isEmpty() || name.equalsIgnoreCase("localhost") ||
      name.startsWithIgnoreCase("localhost."))
    name = "Device";
  name = name.removeCharacters("\r\n\t").replaceCharacter('.', '-').trim();
  return name.substring(0, 28) + " PlayWorship";
}

} // namespace playworship::remote
