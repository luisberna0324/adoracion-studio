#include "AppleSignInApi.h"

namespace {
using Api = AppleSignInApi;
constexpr int maximumResponseBytes = 64 * 1024;

bool isOpaqueValue(const juce::String& value, int minimum, int maximum) {
  return value.length() >= minimum && value.length() <= maximum &&
         value.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
}

bool isHeaderToken(const juce::String& value) {
  if (value.isEmpty() || value.length() > 16384)
    return false;
  for (const auto c : value)
    if (c < 33 || c > 126)
      return false;
  return true;
}

juce::String httpsOrigin(const juce::String& raw) {
  const auto base = raw.trim().trimCharactersAtEnd("/");
  if (!base.startsWith("https://") || base.containsAnyOf("@?#\\ \t\r\n"))
    return {};
  const juce::URL url(base);
  if (!url.isWellFormed() || url.getDomain().isEmpty() || url.getSubPath().isNotEmpty())
    return {};
  return base;
}

bool validContext(const Api::Context& context) {
  if (httpsOrigin(context.apiBaseUrl).isEmpty() || context.deviceId.isEmpty() ||
      context.deviceId.length() > 256 || context.deviceId.containsAnyOf("\r\n") ||
      (context.platform != Api::Platform::ios && context.platform != Api::Platform::macos) ||
      (context.purpose != Api::Purpose::signIn && context.purpose != Api::Purpose::link))
    return false;
  if (context.purpose == Api::Purpose::link)
    return isHeaderToken(context.firebaseIdToken) && context.expectedUid.isNotEmpty() &&
           context.expectedUid.length() <= 256;
  return context.firebaseIdToken.isEmpty() && context.expectedUid.isEmpty();
}

juce::String platformName(Api::Platform platform) {
  return platform == Api::Platform::ios ? "ios" : "macos";
}

juce::String purposeName(Api::Purpose purpose) {
  return purpose == Api::Purpose::link ? "link" : "signin";
}

Api::Error backendError(const juce::var& body) {
  const auto* object = body.getDynamicObject();
  if (object == nullptr || !object->getProperty("error").isString())
    return Api::Error::backendRejected;
  const auto code = object->getProperty("error").toString();
  if (code == "AppleAuthNotConfigured" || code == "AppleAuthUnavailable")
    return Api::Error::unavailable;
  if (code == "AppleInvalidRequest" || code == "AppleChallengeInvalid")
    return Api::Error::invalidRequest;
  if (code == "AppleChallengeExpired") return Api::Error::expiredChallenge;
  if (code == "AppleChallengeUsed") return Api::Error::challengeUsed;
  if (code == "AppleNonceMismatch" || code == "AppleIdentityTokenInvalid")
    return Api::Error::invalidCredential;
  if (code == "AppleAccountLinkRequired") return Api::Error::linkRequired;
  if (code == "AppleAccountConflict") return Api::Error::identityConflict;
  if (code == "AppleRecentAuthRequired") return Api::Error::reauthenticationRequired;
  if (code == "AppleAuthRequired") return Api::Error::unauthorized;
  if (code == "AppleAuthRateLimited") return Api::Error::rateLimited;
  return Api::Error::backendRejected;
}

bool readText(const juce::DynamicObject& object, const char* key,
              juce::String& out, int maximum, bool required = false) {
  const auto value = object.getProperty(key);
  if (!required && (value.isVoid() || value.isUndefined())) {
    out.clear();
    return true;
  }
  if (!value.isString())
    return false;
  out = value.toString();
  return out.length() <= maximum && (!required || out.isNotEmpty());
}

bool readInteger(const juce::DynamicObject& object, const char* key, juce::int64& out) {
  const auto value = object.getProperty(key);
  if (!value.isInt() && !value.isInt64())
    return false;
  out = static_cast<juce::int64>(value);
  return true;
}

Api::HttpRequest postRequest(const Api::Context& context, const char* path,
                            juce::DynamicObject* body) {
  Api::HttpRequest request;
  request.error = Api::Error::none;
  request.url = httpsOrigin(context.apiBaseUrl) + path;
  request.headers = "Content-Type: application/json\r\nAccept: application/json\r\n";
  if (context.purpose == Api::Purpose::link)
    request.headers += "Authorization: Bearer " + context.firebaseIdToken + "\r\n";
  request.jsonBody = juce::JSON::toString(juce::var(body), true);
  return request;
}

struct Response {
  Api::Error error = Api::Error::none;
  juce::var body;
};

Response send(const Api::HttpRequest& request, int timeoutMs) {
  if (!request.ok())
    return {request.error, {}};
  int statusCode = 0;
  const auto url = request.method == "POST"
      ? juce::URL(request.url).withPOSTData(request.jsonBody) : juce::URL(request.url);
  auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
      .withHttpRequestCmd(request.method)
      .withExtraHeaders(request.headers)
      .withConnectionTimeoutMs(juce::jlimit(1000, 20000, timeoutMs))
      .withNumRedirectsToFollow(0)
      .withStatusCode(&statusCode);
  auto stream = url.createInputStream(std::move(options));
  if (stream == nullptr)
    return {Api::Error::network, {}};
  // Read one extra byte to distinguish a complete response from truncation.
  juce::MemoryOutputStream bytes;
  bytes.writeFromInputStream(*stream, maximumResponseBytes + 1);
  if (bytes.getDataSize() > maximumResponseBytes)
    return {Api::Error::invalidResponse, {}};
  const auto body = juce::JSON::parse(bytes.toUTF8());
  if (!body.isObject())
    return {Api::Error::invalidResponse, {}};
  if (statusCode < 200 || statusCode >= 300) {
    const auto error = backendError(body);
    return {statusCode == 401 && error == Api::Error::backendRejected
                ? Api::Error::unauthorized : error, {}};
  }
  return {Api::Error::none, body};
}
} // namespace

NativeAppleSignIn::Request AppleSignInApi::Challenge::nativeRequest(bool fullName) const {
  return NativeAppleSignIn::prepareRequest(state, nonce, expiresAtMs, fullName);
}

AppleSignInApi::HttpRequest AppleSignInApi::makeChallengeRequest(const Context& context) {
  if (!validContext(context))
    return {};
  auto* body = new juce::DynamicObject();
  body->setProperty("purpose", purposeName(context.purpose));
  body->setProperty("platform", platformName(context.platform));
  body->setProperty("deviceId", context.deviceId);
  return postRequest(context, "/api/auth/apple/challenge", body);
}

AppleSignInApi::HttpRequest AppleSignInApi::makeNativeRequest(
    const Context& context, const Challenge& challenge,
    const NativeAppleSignIn::Credential& credential, juce::int64 nowMs) {
  if (!validContext(context) || !isOpaqueValue(challenge.challengeId, 16, 256) ||
      challenge.purpose != context.purpose || challenge.platform != context.platform ||
      challenge.deviceId != context.deviceId || challenge.expectedUid != context.expectedUid)
    return {};
  const auto error = NativeAppleSignIn::validateCredential(challenge.nativeRequest(),
                                                          credential, nowMs);
  if (error != NativeAppleSignIn::Error::none) {
    HttpRequest request;
    request.error = error == NativeAppleSignIn::Error::expiredChallenge
        ? Error::expiredChallenge : error == NativeAppleSignIn::Error::stateMismatch
        ? Error::stateMismatch : Error::invalidRequest;
    return request;
  }
  auto* body = new juce::DynamicObject();
  body->setProperty("challengeId", challenge.challengeId);
  body->setProperty("state", credential.state);
  body->setProperty("identityToken", credential.identityToken);
  body->setProperty("authorizationCode", credential.authorizationCode);
  body->setProperty("platform", platformName(context.platform));
  body->setProperty("deviceId", context.deviceId);
  if (credential.email.isNotEmpty() || credential.fullName.isNotEmpty()) {
    auto* profile = new juce::DynamicObject();
    if (credential.email.isNotEmpty()) profile->setProperty("email", credential.email);
    if (credential.fullName.isNotEmpty()) profile->setProperty("fullName", credential.fullName);
    body->setProperty("profile", juce::var(profile));
  }
  // No raw nonce, client UID, purpose, Firebase token, or tokens in the URL.
  return postRequest(context, "/api/auth/apple/native", body);
}

AppleSignInApi::ChallengeResult AppleSignInApi::parseChallengeResponse(
    const juce::var& body, const Context& context, juce::int64 nowMs) {
  ChallengeResult result;
  if (!validContext(context)) {
    result.error = Error::invalidRequest;
    return result;
  }
  const auto* object = body.getDynamicObject();
  if (object == nullptr || !object->getProperty("ok").isBool())
    return result;
  if (!static_cast<bool>(object->getProperty("ok"))) {
    result.error = backendError(body);
    return result;
  }
  Challenge challenge;
  if (!readText(*object, "challengeId", challenge.challengeId, 256, true) ||
      !isOpaqueValue(challenge.challengeId, 16, 256) ||
      !readText(*object, "state", challenge.state, 256, true) ||
      !readText(*object, "nonce", challenge.nonce, 256, true) ||
      !readInteger(*object, "expiresAtMs", challenge.expiresAtMs))
    return result;
  const auto error = NativeAppleSignIn::validateRequest(challenge.nativeRequest(), nowMs);
  if (error != NativeAppleSignIn::Error::none) {
    result.error = error == NativeAppleSignIn::Error::expiredChallenge
                       ? Error::expiredChallenge : Error::invalidResponse;
    return result;
  }
  challenge.purpose = context.purpose;
  challenge.platform = context.platform;
  challenge.deviceId = context.deviceId;
  challenge.expectedUid = context.expectedUid;
  result.challenge = std::move(challenge);
  result.error = Error::none;
  return result;
}

AppleSignInApi::SessionResult AppleSignInApi::parseSessionResponse(
    const juce::var& body, const Context& context, juce::int64 nowMs) {
  SessionResult result;
  if (!validContext(context) || nowMs <= 0) {
    result.error = Error::invalidRequest;
    return result;
  }
  const auto* object = body.getDynamicObject();
  if (object == nullptr || !object->getProperty("ok").isBool())
    return result;
  if (!static_cast<bool>(object->getProperty("ok"))) {
    result.error = backendError(body);
    return result;
  }
  const auto sessionValue = object->getProperty("session");
  const auto* session = sessionValue.getDynamicObject();
  PersistedSessionSnapshot snapshot;
  if (session == nullptr || !session->getProperty("allowed").isBool() ||
      !readText(*session, "uid", snapshot.uid, 256, true) ||
      !readText(*session, "email", snapshot.email, 320) ||
      !readText(*session, "plan", snapshot.plan, 96, true) ||
      !readText(*session, "subscriptionStatus", snapshot.subscriptionStatus, 96, true) ||
      !readText(*session, "expiresAt", snapshot.expiresAt, 96) ||
      !readText(*session, "licenseKey", snapshot.licenseKey, 256) ||
      !readText(*session, "reasonCode", snapshot.reasonCode, 128) ||
      !readText(*session, "reason", snapshot.reason, 1024) ||
      !readText(*session, "refreshToken", snapshot.refreshToken, 16384, true) ||
      !readText(*session, "firebaseApiKey", snapshot.firebaseApiKey, 256, true) ||
      !readInteger(*session, "idTokenExpiresAtMs", snapshot.idTokenExpiresAtMs) ||
      snapshot.idTokenExpiresAtMs <= nowMs)
    return result;
  if (context.purpose == Purpose::link && snapshot.uid != context.expectedUid) {
    result.error = Error::identityMismatch;
    return result;
  }
  snapshot.allowed = static_cast<bool>(session->getProperty("allowed"));
  // No grants are computed here. A denial is preserved for the caller to show.
  result.session = std::move(snapshot);
  result.error = Error::none;
  return result;
}

AppleSignInApi::ChallengeResult AppleSignInApi::requestChallenge(
    const Context& context, int timeoutMs) {
  const auto response = send(makeChallengeRequest(context), timeoutMs);
  if (response.error != Error::none)
    return {response.error, {}};
  return parseChallengeResponse(response.body, context, juce::Time::currentTimeMillis());
}

AppleSignInApi::Configuration AppleSignInApi::parseConfiguration(const juce::var& body) {
  Configuration result;
  const auto* object = body.getDynamicObject();
  if (object == nullptr || !object->getProperty("enabled").isBool())
    return result;
  result.enabled = static_cast<bool>(object->getProperty("enabled"));
  if (!readText(*object, "nativeClientId", result.nativeClientId, 256, result.enabled) ||
      (result.enabled && !result.nativeClientId.containsOnly(
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-"))) {
    result.enabled = false;
    result.nativeClientId.clear();
    return result;
  }
  result.error = Error::none;
  return result;
}

AppleSignInApi::Configuration AppleSignInApi::requestConfiguration(
    const juce::String& apiBaseUrl, int timeoutMs) {
  const auto base = httpsOrigin(apiBaseUrl);
  if (base.isEmpty())
    return {Error::invalidRequest, false, {}};
  HttpRequest request;
  request.error = Error::none;
  request.method = "GET";
  request.url = base + "/api/auth/apple/config";
  request.headers = "Accept: application/json\r\n";
  const auto response = send(request, timeoutMs);
  if (response.error != Error::none)
    return {response.error, false, {}};
  return parseConfiguration(response.body);
}

AppleSignInApi::SessionResult AppleSignInApi::exchangeCredential(
    const Context& context, const Challenge& challenge,
    const NativeAppleSignIn::Credential& credential, int timeoutMs) {
  const auto response = send(makeNativeRequest(context, challenge, credential,
                                              juce::Time::currentTimeMillis()), timeoutMs);
  if (response.error != Error::none)
    return {response.error, {}};
  return parseSessionResponse(response.body, context, juce::Time::currentTimeMillis());
}
