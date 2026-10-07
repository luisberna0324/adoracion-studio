#pragma once

#include "SessionPersistence.h"
#include "platform/NativeAppleSignIn.h"

/** HTTPS transport and strict parsing. Does not apply/persist a session. */
class AppleSignInApi {
public:
  enum class Purpose { signIn, link };
  enum class Platform { ios, macos };
  enum class Error {
    none, invalidRequest, unavailable, network, invalidResponse,
    expiredChallenge, stateMismatch, identityMismatch, linkRequired,
    identityConflict, reauthenticationRequired, unauthorized, challengeUsed,
    invalidCredential, rateLimited, backendRejected
  };

  struct Context {
    juce::String apiBaseUrl;
    juce::String deviceId;
    Platform platform = Platform::ios;
    Purpose purpose = Purpose::signIn;
    // Required only for link, on both requests. Never sent in the JSON body.
    juce::String firebaseIdToken;
    // Extra client guard; server must derive and bind UID from verified Bearer.
    juce::String expectedUid;
  };

  struct Challenge {
    juce::String challengeId;
    juce::String state;
    juce::String nonce;
    juce::int64 expiresAtMs = 0;
    // Local binding copied from Context, not an authoritative server identity.
    Purpose purpose = Purpose::signIn;
    Platform platform = Platform::ios;
    juce::String deviceId;
    juce::String expectedUid;

    NativeAppleSignIn::Request nativeRequest(bool requestFullName = false) const;
  };

  struct HttpRequest {
    Error error = Error::invalidRequest;
    juce::String method = "POST";
    juce::String url;
    juce::String headers;
    juce::String jsonBody;
    bool ok() const { return error == Error::none; }
  };
  struct ChallengeResult {
    Error error = Error::invalidResponse;
    Challenge challenge;
    bool ok() const { return error == Error::none; }
  };
  struct SessionResult {
    Error error = Error::invalidResponse;
    PersistedSessionSnapshot session;
    bool ok() const { return error == Error::none; }
  };
  struct Configuration {
    Error error = Error::invalidResponse;
    bool enabled = false;
    juce::String nativeClientId;
    bool ok() const { return error == Error::none; }
  };

  // Pure helpers also define the wire contract and permit isolated tests.
  static HttpRequest makeChallengeRequest(const Context&);
  static HttpRequest makeNativeRequest(const Context&, const Challenge&,
                                      const NativeAppleSignIn::Credential&,
                                      juce::int64 nowMs);
  static ChallengeResult parseChallengeResponse(const juce::var&, const Context&,
                                               juce::int64 nowMs);
  static SessionResult parseSessionResponse(const juce::var&, const Context&,
                                           juce::int64 nowMs);
  static Configuration parseConfiguration(const juce::var&);

  // Blocking calls: run on a background thread. No URLs/payloads are logged.
  // Redirects are disabled; responses are limited to 64 KiB.
  static ChallengeResult requestChallenge(const Context&, int timeoutMs = 20000);
  static Configuration requestConfiguration(const juce::String& apiBaseUrl,
                                            int timeoutMs = 20000);
  static SessionResult exchangeCredential(const Context&, const Challenge&,
                                         const NativeAppleSignIn::Credential&,
                                         int timeoutMs = 20000);
};
