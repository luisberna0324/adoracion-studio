#include "EmailPasswordAuthApi.h"

namespace {
using Api = EmailPasswordAuthApi;

juce::String secureOrigin(const juce::String& raw) {
  const auto base = raw.trim().trimCharactersAtEnd("/");
  if (!base.startsWith("https://") || base.containsAnyOf("@?#\\ \t\r\n"))
    return {};
  const juce::URL url(base);
  return url.isWellFormed() && url.getDomain().isNotEmpty() &&
                 url.getSubPath().isEmpty() ? base : juce::String{};
}

juce::String normalizedEmail(const juce::String& raw) {
  const auto email = raw.trim().toLowerCase();
  const auto at = email.indexOfChar('@');
  if (email.length() > 254 || at <= 0 || at != email.lastIndexOfChar('@') ||
      at >= email.length() - 1 || email.containsAnyOf(" \t\r\n"))
    return {};
  return email;
}

bool readString(const juce::DynamicObject& object, const char* key,
                juce::String& out, int maximum, bool required = false) {
  const auto value = object.getProperty(key);
  if (!required && (value.isVoid() || value.isUndefined()))
    return true;
  if (!value.isString()) return false;
  out = value.toString();
  return out.length() <= maximum && (!required || out.isNotEmpty());
}

struct Response {
  Api::Error error;
  juce::var body;
};
Response send(const Api::Request& request, int timeoutMs) {
  if (!request.ok()) return {request.error, {}};
  int status = 0;
  auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
      .withHttpRequestCmd("POST")
      .withExtraHeaders("Content-Type: application/json\r\nAccept: application/json\r\n")
      .withConnectionTimeoutMs(juce::jlimit(1000, 20000, timeoutMs))
      .withNumRedirectsToFollow(0).withStatusCode(&status);
  auto stream = juce::URL(request.url).withPOSTData(request.jsonBody)
                    .createInputStream(std::move(options));
  if (!stream) return {status == 429 ? Api::Error::rateLimited :
                      status == 401 ? Api::Error::invalidCredentials : Api::Error::network, {}};
  juce::MemoryOutputStream bytes;
  bytes.writeFromInputStream(*stream, 64 * 1024 + 1);
  if (bytes.getDataSize() > 64 * 1024) return {Api::Error::invalidResponse, {}};
  const auto body = juce::JSON::parse(bytes.toUTF8());
  if (status < 200 || status >= 300) return {Api::responseError(body, status), {}};
  if (!body.isObject()) return {Api::Error::invalidResponse, {}};
  return {Api::Error::none, body};
}
}

EmailPasswordAuthApi::Request EmailPasswordAuthApi::makeLoginRequest(
    const juce::String& base, const juce::String& rawEmail,
    const juce::String& password, const juce::String& deviceId) {
  const auto origin = secureOrigin(base);
  const auto email = normalizedEmail(rawEmail);
  if (origin.isEmpty() || email.isEmpty() || password.isEmpty() ||
      password.length() > 1024 || deviceId.isEmpty() || deviceId.length() > 256 ||
      deviceId.containsAnyOf("\r\n")) return {};
  auto* body = new juce::DynamicObject();
  body->setProperty("email", email);
  body->setProperty("password", password); // Password whitespace is significant.
  body->setProperty("deviceId", deviceId);
  return {Error::none, origin + "/api/auth/login", juce::JSON::toString(juce::var(body), true)};
}

EmailPasswordAuthApi::Request EmailPasswordAuthApi::makeResetRequest(
    const juce::String& base, const juce::String& rawEmail) {
  const auto origin = secureOrigin(base);
  const auto email = normalizedEmail(rawEmail);
  if (origin.isEmpty() || email.isEmpty()) return {};
  auto* body = new juce::DynamicObject();
  body->setProperty("email", email);
  return {Error::none, origin + "/api/auth/request-set-password",
          juce::JSON::toString(juce::var(body), true)};
}

EmailPasswordAuthApi::Error EmailPasswordAuthApi::responseError(
    const juce::var& body, int status) {
  if (status == 429) return Error::rateLimited;
  if (status == 401) return Error::invalidCredentials;
  const auto code = body.getProperty("error", {}).toString();
  if (code == "InvalidCredentials") return Error::invalidCredentials;
  if (code == "RateLimited") return Error::rateLimited;
  if (code == "InvalidRequest" || status == 400) return Error::invalidRequest;
  return Error::unavailable; // Never display arbitrary backend strings.
}

EmailPasswordAuthApi::Result EmailPasswordAuthApi::parseLoginResponse(
    const juce::var& body, juce::int64 nowMs) {
  Result result;
  const auto* object = body.getDynamicObject();
  if (!object || !object->getProperty("ok").isBool() || nowMs <= 0) return result;
  if (!static_cast<bool>(object->getProperty("ok"))) {
    result.error = responseError(body, 0);
    return result;
  }
  const auto* session = object->getProperty("session").getDynamicObject();
  PersistedSessionSnapshot snapshot;
  if (!session || !session->getProperty("allowed").isBool() ||
      !readString(*session, "uid", snapshot.uid, 256, true) ||
      !readString(*session, "email", snapshot.email, 320, true) ||
      !readString(*session, "plan", snapshot.plan, 96) ||
      !readString(*session, "subscriptionStatus", snapshot.subscriptionStatus, 96) ||
      !readString(*session, "expiresAt", snapshot.expiresAt, 96) ||
      !readString(*session, "licenseKey", snapshot.licenseKey, 256) ||
      !readString(*session, "reasonCode", snapshot.reasonCode, 128) ||
      !readString(*session, "reason", snapshot.reason, 1024) ||
      !readString(*session, "refreshToken", snapshot.refreshToken, 16384, true) ||
      !readString(*session, "firebaseApiKey", snapshot.firebaseApiKey, 256, true)) return result;
  const auto expires = session->getProperty("idTokenExpiresAtMs");
  if (!expires.isInt64() && !expires.isInt()) return result;
  snapshot.idTokenExpiresAtMs = static_cast<juce::int64>(expires);
  if (snapshot.idTokenExpiresAtMs <= nowMs ||
      snapshot.idTokenExpiresAtMs > nowMs + 24 * 60 * 60 * 1000LL) return result;
  snapshot.allowed = static_cast<bool>(session->getProperty("allowed"));
  if (snapshot.allowed && (snapshot.plan.isEmpty() || snapshot.subscriptionStatus.isEmpty()))
    return result;
  result.error = Error::none;
  result.session = std::move(snapshot);
  return result;
}

EmailPasswordAuthApi::ResetResult EmailPasswordAuthApi::parseResetResponse(const juce::var& body) {
  const auto* object = body.getDynamicObject();
  if (!object || !object->getProperty("ok").isBool()) return {};
  return {static_cast<bool>(object->getProperty("ok")) ? Error::none : responseError(body, 0)};
}

EmailPasswordAuthApi::Result EmailPasswordAuthApi::signIn(
    const juce::String& base, const juce::String& email, const juce::String& password,
    const juce::String& deviceId, int timeoutMs) {
  const auto response = send(makeLoginRequest(base, email, password, deviceId), timeoutMs);
  if (response.error != Error::none) return {response.error, {}};
  return parseLoginResponse(response.body, juce::Time::currentTimeMillis());
}

EmailPasswordAuthApi::ResetResult EmailPasswordAuthApi::requestPasswordLink(
    const juce::String& base, const juce::String& email, int timeoutMs) {
  const auto response = send(makeResetRequest(base, email), timeoutMs);
  if (response.error != Error::none) return {response.error};
  return parseResetResponse(response.body);
}
