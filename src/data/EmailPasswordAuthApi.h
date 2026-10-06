#pragma once

#include "SessionPersistence.h"

/** Native email access. Blocking HTTPS calls; never logs or stores passwords. */
class EmailPasswordAuthApi {
public:
  enum class Error {
    none, invalidRequest, invalidCredentials, rateLimited, unavailable,
    network, invalidResponse
  };
  struct Request {
    Error error = Error::invalidRequest;
    juce::String url, jsonBody;
    bool ok() const { return error == Error::none; }
  };
  struct Result {
    Error error = Error::invalidResponse;
    PersistedSessionSnapshot session;
    bool ok() const { return error == Error::none; }
  };
  struct ResetResult {
    Error error = Error::invalidResponse;
    bool ok() const { return error == Error::none; }
  };

  static Request makeLoginRequest(const juce::String& apiBase,
                                 const juce::String& email,
                                 const juce::String& password,
                                 const juce::String& deviceId);
  static Request makeResetRequest(const juce::String& apiBase,
                                 const juce::String& email);
  static Result parseLoginResponse(const juce::var&, juce::int64 nowMs);
  static ResetResult parseResetResponse(const juce::var&);
  static Error responseError(const juce::var&, int statusCode);
  static Result signIn(const juce::String& apiBase, const juce::String& email,
                       const juce::String& password, const juce::String& deviceId,
                       int timeoutMs = 20000);
  static ResetResult requestPasswordLink(const juce::String& apiBase,
                                        const juce::String& email,
                                        int timeoutMs = 20000);
};
