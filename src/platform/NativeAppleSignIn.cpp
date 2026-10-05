#include "NativeAppleSignIn.h"
#include <juce_cryptography/juce_cryptography.h>
#if !JUCE_IOS && !JUCE_MAC
#include <juce_gui_basics/juce_gui_basics.h>
#endif

namespace NativeAppleSignIn {
namespace {

bool isOpaqueValue(const juce::String& value, int minimum, int maximum) {
  return value.length() >= minimum && value.length() <= maximum &&
         value.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
}

bool isProof(const juce::String& value) {
  if (value.isEmpty() || value.length() > 16384)
    return false;
  for (const auto c : value)
    if (c < 33 || c > 126)
      return false;
  return true;
}

} // namespace

Request prepareRequest(const juce::String& state, const juce::String& rawNonce,
                       juce::int64 expiresAtMs, bool requestFullName) {
  Request request;
  if (!isOpaqueValue(state, 16, 256) || !isOpaqueValue(rawNonce, 32, 256))
    return request;
  request.state = state;
  request.nonceHash = juce::SHA256(rawNonce.toRawUTF8(),
                                 rawNonce.getNumBytesAsUTF8()).toHexString();
  request.expiresAtMs = expiresAtMs;
  request.requestFullName = requestFullName;
  return request;
}

Error validateRequest(const Request& request, juce::int64 nowMs) {
  if (!isOpaqueValue(request.state, 16, 256) ||
      request.nonceHash.length() != 64 ||
      !request.nonceHash.containsOnly("0123456789abcdef") || nowMs <= 0)
    return Error::invalidRequest;
  if (request.expiresAtMs <= nowMs)
    return Error::expiredChallenge;
  if (request.expiresAtMs - nowMs > maximumChallengeLifetimeMs)
    return Error::invalidRequest;
  return Error::none;
}

Error validateCredential(const Request& request, const Credential& credential,
                         juce::int64 nowMs) {
  if (const auto error = validateRequest(request, nowMs); error != Error::none)
    return error;
  if (credential.state != request.state)
    return Error::stateMismatch;
  if (!isProof(credential.identityToken) || !isProof(credential.authorizationCode) ||
      credential.appleUser.isEmpty() || credential.appleUser.length() > 256 ||
      credential.email.length() > 320 || credential.fullName.length() > 200)
    return Error::invalidCredential;
  return Error::none;
}

#if !JUCE_IOS && !JUCE_MAC
bool isSupported() { return false; }
std::unique_ptr<juce::Component> createButton(std::function<void()>, bool) { return nullptr; }
bool start(const Request&, juce::Component&, Completion) { return false; }
void cancel() {}
#endif

} // namespace NativeAppleSignIn
