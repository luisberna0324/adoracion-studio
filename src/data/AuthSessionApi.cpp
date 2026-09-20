#include "AuthSessionApi.h"
#include "SessionAuthLogger.h"
#include "telemetry/Telemetry.h"

namespace {

juce::String trimApiBase(const juce::String &apiBaseUrl) {
  return apiBaseUrl.trim().trimEnd();
}

juce::String readStreamToString(juce::InputStream &stream) {
  juce::String out;
  const juce::int64 len = stream.getTotalLength();
  if (len > 0 && len < 1024 * 1024) {
    juce::HeapBlock<char> buf(static_cast<size_t>(len));
    const auto r = stream.read(buf.getData(), static_cast<int>(len));
    if (r == len)
      out = juce::String::fromUTF8(buf.getData(), static_cast<int>(len));
  } else {
    out = stream.readEntireStreamAsString();
  }
  return out;
}

} // namespace

FirebaseRefreshResult AuthSessionApi::refreshFirebaseIdToken(
    const juce::String &firebaseApiKey, const juce::String &refreshToken,
    const int timeoutMs) {
  FirebaseRefreshResult result;
  if (firebaseApiKey.isEmpty() || refreshToken.isEmpty())
    return result;

  const juce::String postBody =
      "grant_type=refresh_token&refresh_token=" +
      juce::URL::addEscapeChars(refreshToken, true);

  juce::URL url("https://securetoken.googleapis.com/v1/token");
  url = url.withParameter("key", firebaseApiKey).withPOSTData(postBody);

  auto options =
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withExtraHeaders(
              "Content-Type: application/x-www-form-urlencoded\r\n")
          .withConnectionTimeoutMs(timeoutMs)
          .withHttpRequestCmd("POST");

  auto stream = url.createInputStream(std::move(options));
  if (stream == nullptr) {
    result.errorMessage = "network";
    SessionAuthLogger::write(
        "AuthSessionApi::refreshFirebaseIdToken: stream null (network/timeout)");
    playworship::telemetry::capture(
        playworship::telemetry::Incident::authRefreshFailed);
    return result;
  }

  const auto body = readStreamToString(*stream);
  const auto parsed = juce::JSON::parse(body);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty("id_token")) {
      result.idToken = o->getProperty("id_token").toString().trim();
      result.ok = result.idToken.isNotEmpty();
    }
    if (o->hasProperty("refresh_token")) {
      const auto rt = o->getProperty("refresh_token").toString().trim();
      if (rt.isNotEmpty())
        result.refreshToken = rt;
    }
    if (!result.ok && o->hasProperty("error")) {
      const auto err = o->getProperty("error");
      if (const auto *eo = err.getDynamicObject()) {
        result.errorMessage =
            eo->getProperty("message").toString().trim();
      } else {
        result.errorMessage = err.toString();
      }
    }
  }

  if (!result.ok && result.errorMessage.isEmpty())
    result.errorMessage = "parse";

  SessionAuthLogger::write(
      juce::String("AuthSessionApi::refreshFirebaseIdToken: ok=") +
      juce::String(static_cast<int>(result.ok)) + " idToken " +
      SessionAuthLogger::tokenPresence(result.idToken) +
      (result.ok ? juce::String()
                 : juce::String(" error=") + result.errorMessage));
  if (!result.ok)
    playworship::telemetry::capture(
        playworship::telemetry::Incident::authRefreshFailed);

  return result;
}

ValidateAccessResult AuthSessionApi::fetchValidateAccess(
    const juce::String &apiBaseUrl, const juce::String &idToken,
    const juce::String &deviceId, const juce::String &licenseKey,
    const int timeoutMs) {
  ValidateAccessResult out;
  if (idToken.isEmpty() || deviceId.isEmpty())
    return out;

  juce::String base = trimApiBase(apiBaseUrl);
  if (base.isEmpty())
    return out;

  juce::URL url(base + "/api/validate-access");
  url = url.withParameter("deviceId", deviceId);
  if (licenseKey.isNotEmpty())
    url = url.withParameter("licenseKey", licenseKey);

  const juce::String headers =
      "Authorization: Bearer " + idToken + "\r\n";

  auto options =
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withExtraHeaders(headers)
          .withConnectionTimeoutMs(timeoutMs)
          .withHttpRequestCmd("GET");

  const juce::String urlForLog = url.toString(false);

  auto stream = url.createInputStream(std::move(options));
  if (stream == nullptr) {
    out.rawError = "network";
    SessionAuthLogger::write(
        "AuthSessionApi::fetchValidateAccess: stream null GET " + urlForLog);
    playworship::telemetry::capture(
        playworship::telemetry::Incident::accessValidationFailed);
    return out;
  }

  out.httpOk = true;
  const auto body = readStreamToString(*stream);
  const auto parsed = juce::JSON::parse(body);
  const auto *o = parsed.getDynamicObject();
  if (o == nullptr) {
    out.rawError = "parse";
    SessionAuthLogger::write(
        "AuthSessionApi::fetchValidateAccess: non-JSON body head=" +
        body.substring(0, juce::jmin(180, body.length())));
    playworship::telemetry::capture(
        playworship::telemetry::Incident::accessValidationFailed);
    return out;
  }

  out.allowed = static_cast<bool>(o->getProperty("allowed"));
  out.plan = o->getProperty("plan").toString().trim();
  out.subscriptionStatus =
      o->getProperty("subscriptionStatus").toString().trim();
  if (o->getProperty("expiresAt").isVoid() ||
      o->getProperty("expiresAt").isUndefined())
    out.expiresAt = {};
  else
    out.expiresAt = o->getProperty("expiresAt").toString().trim();
  out.licenseKey = o->getProperty("licenseKey").toString().trim();
  out.reasonCode = o->getProperty("reasonCode").toString().trim();
  out.reason = o->getProperty("reason").toString().trim();
  SessionAuthLogger::write(
      "AuthSessionApi::fetchValidateAccess: GET " + urlForLog + " allowed=" +
      juce::String(static_cast<int>(out.allowed)) + " plan=" + out.plan +
      " status=" + out.subscriptionStatus + " reasonCode=" + out.reasonCode);
  return out;
}
