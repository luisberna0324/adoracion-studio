#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <Security/Security.h>
#endif
#endif
#include "IntegrationTokenStore.h"
#include <mutex>

// Desktop integration credentials are scoped to provider + server. Never stored
// in app settings, song metadata or portable projects. Unsupported platforms use
// session-only credentials until a native credential-store adapter is available.
namespace IntegrationTokenStore {
#if JUCE_MAC
namespace {
std::mutex interactionMutex;
// The legacy macOS Keychain ignores kSecUseAuthenticationUI on some ACL
// checks. Explicitly prohibit prompts while reading during app construction.
struct NonInteractiveRead {
  std::lock_guard<std::mutex> lock{interactionMutex};
  Boolean previous = true;
  NonInteractiveRead() { SecKeychainGetUserInteractionAllowed(&previous); SecKeychainSetUserInteractionAllowed(false); }
  ~NonInteractiveRead() { SecKeychainSetUserInteractionAllowed(previous); }
};
}
#endif
bool available() {
#if JUCE_MAC
  return true;
#else
  return false;
#endif
}
juce::String account(const juce::String& provider, const juce::String& host, int port) {
  return provider + ":" + (host == "localhost" ? juce::String("127.0.0.1") : host.trim()) + ":" + juce::String(port);
}
#if JUCE_MAC
CFMutableDictionaryRef query(const juce::String& provider, const juce::String& host, int port) {
  auto result = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  const auto key = account(provider, host, port);
  auto value = CFStringCreateWithCString(nullptr, key.toRawUTF8(), kCFStringEncodingUTF8);
  CFDictionarySetValue(result, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(result, kSecAttrService, CFSTR("com.multitracks.playworship.external-integrations"));
  CFDictionarySetValue(result, kSecAttrAccount, value);
  CFRelease(value);
  return result;
}
#endif
juce::String load(const juce::String& provider, const juce::String& host, int port) {
#if JUCE_MAC
  NonInteractiveRead noPrompt;
  auto q = query(provider, host, port);
  CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
  CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
  // Startup must not block on a hidden Keychain access dialog. Explicit Save can
  // request access; a denied or locked item simply requires the user to paste it.
  CFDictionarySetValue(q, kSecUseAuthenticationUI, kSecUseAuthenticationUIFail);
  CFTypeRef result = nullptr;
  auto code = SecItemCopyMatching(q, &result); CFRelease(q);
  juce::String token;
  if (code == errSecSuccess && result && CFGetTypeID(result) == CFDataGetTypeID()) {
    auto data = static_cast<CFDataRef>(result);
    token = juce::String::fromUTF8(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), (int) CFDataGetLength(data));
  }
  if (result) CFRelease(result);
  return token;
#else
  juce::ignoreUnused(provider, host, port); return {};
#endif
}
bool save(const juce::String& provider, const juce::String& host, int port, const juce::String& token) {
#if JUCE_MAC
  std::lock_guard<std::mutex> lock(interactionMutex);
  auto q = query(provider, host, port);
  auto values = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(token.toRawUTF8()), (CFIndex) token.getNumBytesAsUTF8());
  CFDictionarySetValue(values, kSecValueData, data);
  auto code = SecItemUpdate(q, values);
  if (code == errSecItemNotFound) {
    CFDictionarySetValue(q, kSecValueData, data);
    CFDictionarySetValue(q, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
    code = SecItemAdd(q, nullptr);
  }
  CFRelease(data); CFRelease(values); CFRelease(q); return code == errSecSuccess;
#else
  juce::ignoreUnused(provider, host, port, token); return false;
#endif
}
bool forget(const juce::String& provider, const juce::String& host, int port) {
#if JUCE_MAC
  std::lock_guard<std::mutex> lock(interactionMutex);
  auto q = query(provider, host, port); const auto code = SecItemDelete(q); CFRelease(q);
  return code == errSecSuccess || code == errSecItemNotFound;
#else
  juce::ignoreUnused(provider, host, port); return true;
#endif
}
}
