#include "platform/NativeAppleSignIn.h"
#include <juce_gui_extra/juce_gui_extra.h>
#import <AuthenticationServices/AuthenticationServices.h>
#import <AppKit/AppKit.h>
#include <iostream>
#include <stdexcept>
#include <vector>

void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
int main() {
  try {
    juce::ScopedJuceInitialiser_GUI gui;
    std::vector<NativeAppleSignIn::CredentialStateCompletion> queries;
    std::vector<NativeAppleSignIn::CredentialState> delivered;
    NativeAppleSignIn::setCredentialQueryForTests([&](auto callback) { queries.push_back(std::move(callback)); });
    auto monitor = NativeAppleSignIn::monitorCredential("synthetic-user-no-apple-account", [&](auto state) { delivered.push_back(state); });
    expect(monitor != nullptr && queries.size() == 1, "native monitor queries immediately");
    [NSNotificationCenter.defaultCenter postNotificationName:ASAuthorizationAppleIDProviderCredentialRevokedNotification object:nil];
    expect(queries.size() == 2, "native revocation notification queries monitored credential");
    queries[0](NativeAppleSignIn::CredentialState::authorized);
    queries[1](NativeAppleSignIn::CredentialState::revoked);
    queries[1](NativeAppleSignIn::CredentialState::revoked);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    expect(delivered.size() == 1 && delivered[0] == NativeAppleSignIn::CredentialState::revoked,
           "stale and duplicate native completions cannot deliver more than the latest result once");
    [NSNotificationCenter.defaultCenter postNotificationName:NSApplicationDidBecomeActiveNotification object:nil];
    expect(queries.size() == 3, "foreground notification checks credential again");
    queries[2](NativeAppleSignIn::CredentialState::unavailable);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    expect(delivered.size() == 2 && delivered.back() == NativeAppleSignIn::CredentialState::unavailable,
           "temporary OS errors remain distinct from revocation");
    monitor->check();
    expect(queries.size() == 4, "explicit recheck is available after an OS error");
    monitor.reset();
    queries[3](NativeAppleSignIn::CredentialState::revoked);
    [NSNotificationCenter.defaultCenter postNotificationName:ASAuthorizationAppleIDProviderCredentialRevokedNotification object:nil];
    [NSNotificationCenter.defaultCenter postNotificationName:NSApplicationDidBecomeActiveNotification object:nil];
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    expect(queries.size() == 4 && delivered.size() == 2, "destruction removes native observers and discards late OS callbacks");
    NativeAppleSignIn::setCredentialQueryForTests({});
    std::cout << "Native Apple lifecycle passed: Foundation notifications, query ordering, duplicates and RAII; no Apple account queried\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Native Apple lifecycle failed: " << error.what() << '\n';
    return 1;
  }
}
