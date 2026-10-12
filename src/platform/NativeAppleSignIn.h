#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

namespace juce { class Component; }

/** Obtains an Apple proof only. The backend must verify it before login/linking. */
namespace NativeAppleSignIn {

constexpr juce::int64 maximumChallengeLifetimeMs = 10 * 60 * 1000;

enum class Error {
  none,
  invalidRequest,
  expiredChallenge,
  stateMismatch,
  invalidCredential,
  unavailable,
  failed
};

struct Request {
  juce::String state;
  juce::String nonceHash;
  juce::int64 expiresAtMs = 0;
  bool requestFullName = false;
};

struct Credential {
  juce::String state;
  juce::String identityToken;
  juce::String authorizationCode;
  juce::String appleUser;
  // Initial profile data is optional and is not a verified account identity.
  juce::String email;
  juce::String fullName;
};

enum class Outcome { succeeded, cancelled, failed };

struct Result {
  Outcome outcome = Outcome::failed;
  Error error = Error::failed;
  Credential credential;
};

using Completion = std::function<void(Result)>;

// The raw nonce comes from a fresh HTTPS backend challenge, never from a UUID.
Request prepareRequest(const juce::String& state, const juce::String& rawNonce,
                       juce::int64 expiresAtMs, bool requestFullName = false);
Error validateRequest(const Request&, juce::int64 nowMs);
Error validateCredential(const Request&, const Credential&, juce::int64 nowMs);

enum class CredentialState { authorized, revoked, notFound, transferred, unavailable };
using CredentialStateCompletion = std::function<void(CredentialState)>;
// A confirmed revoked/missing/transferred credential requires fresh login;
// temporary OS errors preserve the existing session policy.
bool requiresFreshLogin(CredentialState);
class CredentialMonitor {
public:
  virtual ~CredentialMonitor() = default;
  virtual void check() = 0;
};
// Checks immediately and on foreground/revocation notifications. RAII removes
// observers and invalidates pending completions on the message/main thread.
std::unique_ptr<CredentialMonitor> monitorCredential(const juce::String& appleUser,
                                                    CredentialStateCompletion);

#if defined(MTCONTROLS_NATIVE_APPLE_LIFECYCLE_TESTS)
// Test-only injection, absent from application builds. Queries no Apple account.
using TestCredentialQuery = std::function<void(CredentialStateCompletion)>;
void setCredentialQueryForTests(TestCredentialQuery);
#endif

// OS support does not imply portal entitlements or backend configuration.
bool isSupported();
// A real ASAuthorizationAppleIDButton. Creating it does not start authorization.
// The caller must gate visibility using the backend's enabled configuration.
std::unique_ptr<juce::Component> createButton(std::function<void()> onClick,
                                            bool darkBackground = true);
// Message/main thread only. False means no flow started and no callback queued.
// Rejects concurrent starts. Completion is delivered once on the JUCE thread.
bool start(const Request&, juce::Component& presentationAnchor, Completion);
// Ends the pending attempt; its completion receives Outcome::cancelled once.
void cancel();

} // namespace NativeAppleSignIn
