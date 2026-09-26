#import <TargetConditionals.h>

#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <SystemConfiguration/SystemConfiguration.h>
#include <vector>
#endif

#include "RemoteDeviceName.h"

namespace playworship::remote {

juce::String getRemoteDeviceName() {
#if JUCE_IOS
  auto *device = [UIDevice currentDevice];
  auto *name = device.name;
  if (name.length == 0)
    name = device.model;
  return name != nil ? juce::String(name.UTF8String) : juce::String("iPhone");
#elif JUCE_MAC
  auto *name = SCDynamicStoreCopyComputerName(nullptr, nullptr);
  if (name != nullptr) {
    std::vector<char> utf8(static_cast<size_t>(CFStringGetLength(name)) * 4 + 1);
    const auto result = CFStringGetCString(name, utf8.data(),
                                          static_cast<CFIndex>(utf8.size()),
                                          kCFStringEncodingUTF8)
                            ? juce::String::fromUTF8(utf8.data())
                            : juce::String{};
    CFRelease(name);
    if (result.trim().isNotEmpty())
      return result;
  }
  return juce::SystemStats::getComputerName();
#else
  return juce::SystemStats::getComputerName();
#endif
}

} // namespace playworship::remote
