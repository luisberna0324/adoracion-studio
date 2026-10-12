#pragma once

// Fail closed for every non-Debug configuration, including simulators.
namespace DevelopmentFeatures {
#if JUCE_DEBUG
inline constexpr bool testLogin = true;
#else
inline constexpr bool testLogin = false;
#endif
}
