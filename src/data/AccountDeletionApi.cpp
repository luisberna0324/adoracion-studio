#include "AccountDeletionApi.h"
#include "AuthSessionApi.h"
AccountDeletionPreview AccountDeletionApi::parsePreview(const juce::var &body, const juce::String &uid) {
  AccountDeletionPreview result;
  if (!body.isObject() || body.getProperty("ok", {}).isBool() == false ||
      !bool(body["ok"]) || !body["uid"].isString() || body["uid"].toString() != uid || uid.isEmpty() ||
      !body["executionAvailable"].isBool()) return result;
  const auto impact = body["impact"];
  if (!impact.isObject() || !impact["cloudSongs"].isInt() || !impact["sharedMembers"].isInt() ||
      !impact["ownsSharedCloud"].isBool() || !impact["localFilesPreserved"].isBool() || !bool(impact["localFilesPreserved"])) return result;
  result.cloudSongs = int(impact["cloudSongs"]); result.sharedMembers = int(impact["sharedMembers"]);
  if (result.cloudSongs < 0 || result.sharedMembers < 0) return {};
  result.ownsSharedCloud = bool(impact["ownsSharedCloud"]);
  result.executionAvailable = bool(body["executionAvailable"]); result.ok = true;
  return result;
}
AccountDeletionPreview AccountDeletionApi::preview(const juce::String &rawBase, const juce::String &key,
    const juce::String &refresh, const juce::String &uid) {
  const auto base = rawBase.trim().trimCharactersAtEnd("/");
  const juce::URL origin(base);
  if (!base.startsWith("https://") || base.containsAnyOf("@?#\\ \t\r\n") ||
      !origin.isWellFormed() || origin.getDomain().isEmpty() || origin.getSubPath().isNotEmpty() ||
      key.isEmpty() || refresh.isEmpty() || uid.isEmpty()) return {};
  const auto token = AuthSessionApi::refreshFirebaseIdToken(key, refresh);
  if (!token.ok || token.idToken.isEmpty() || token.idToken.containsAnyOf("\r\n")) return {};
  int status = 0;
  auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
    .withHttpRequestCmd("GET").withNumRedirectsToFollow(0).withConnectionTimeoutMs(20000)
    .withExtraHeaders("Authorization: Bearer " + token.idToken + "\r\nAccept: application/json\r\n")
    .withStatusCode(&status);
  auto stream = juce::URL(base + "/api/account/deletion/preview").createInputStream(options);
  if (!stream || status != 200) return {};
  juce::MemoryOutputStream bytes; bytes.writeFromInputStream(*stream, 64 * 1024 + 1);
  if (bytes.getDataSize() > 64 * 1024) return {};
  return parsePreview(juce::JSON::parse(bytes.toUTF8()), uid);
}

namespace {
juce::var deletionPost(const juce::String &rawBase, const juce::String &path,
                       const juce::var &body, const juce::String &token = {}) {
  const auto base = rawBase.trim().trimCharactersAtEnd("/"); const juce::URL origin(base);
  if (!base.startsWith("https://") || base.containsAnyOf("@?#\\ \t\r\n") ||
      !origin.isWellFormed() || origin.getDomain().isEmpty() || origin.getSubPath().isNotEmpty() || token.containsAnyOf("\r\n")) return {};
  int code = 0;
  auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
    .withHttpRequestCmd("POST").withNumRedirectsToFollow(0).withConnectionTimeoutMs(20000)
    .withExtraHeaders("Content-Type: application/json\r\nAccept: application/json\r\n" +
      (token.isNotEmpty() ? "Authorization: Bearer " + token + "\r\n" : juce::String{})).withStatusCode(&code);
  auto stream = juce::URL(base + path).withPOSTData(juce::JSON::toString(body)).createInputStream(options);
  if (!stream || code != 200) return {};
  juce::MemoryOutputStream bytes; bytes.writeFromInputStream(*stream, 64 * 1024 + 1);
  if (bytes.getDataSize() > 64 * 1024) return {};
  return juce::JSON::parse(bytes.toUTF8());
}
bool validStatusReceipt(const juce::String &receipt) {
  return receipt.length() == 43 && receipt.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") &&
    juce::String("AEIMQUYcgkosw048").containsChar(receipt.getLastCharacter());
}
}
AccountDeletionReauthentication AccountDeletionApi::parseReauthentication(const juce::var &body, juce::int64 now) {
  AccountDeletionReauthentication result;
  if (!body.isObject() || !body["ok"].isBool() || !bool(body["ok"]) ||
      !body["proofId"].isString() || !body["receipt"].isString() || !body["executionAvailable"].isBool() ||
      !(body["expiresAtMs"].isInt64() || body["expiresAtMs"].isInt())) return result;
  result.proofId = body["proofId"].toString(); result.receipt = body["receipt"].toString();
  result.expiresAtMs = juce::int64(body["expiresAtMs"]);
  if (result.proofId.length() != 36 || !result.proofId.containsOnly("0123456789abcdef-") || !validStatusReceipt(result.receipt) ||
      result.expiresAtMs <= now || result.expiresAtMs - now > 305000) return {};
  result.executionAvailable = bool(body["executionAvailable"]); result.ok = true; return result;
}
AccountDeletionStatus AccountDeletionApi::parseStatus(const juce::var &body) {
  AccountDeletionStatus result;
  if (!body.isObject() || !body["ok"].isBool() || !bool(body["ok"]) ||
      !body["state"].isString() || !body["retryAfterSeconds"].isInt()) return result;
  result.state = body["state"].toString(); result.retryAfterSeconds = int(body["retryAfterSeconds"]);
  if (result.state != "prepared" && result.state != "pending" && result.state != "complete" && result.state != "not-accepted") return {};
  if (result.retryAfterSeconds < 0 || result.retryAfterSeconds > 60 ||
      ((result.state == "complete" || result.state == "prepared" || result.state == "not-accepted") && result.retryAfterSeconds != 0)) return {};
  result.ok = true; return result;
}
AccountDeletionReauthentication AccountDeletionApi::reauthenticate(const juce::String &base, const juce::String &current,
                                                                 const juce::String &fresh) {
  if (current.isEmpty() || fresh.isEmpty() || current.length() > 16384 || fresh.length() > 16384) return {};
  auto *body = new juce::DynamicObject(); body->setProperty("idToken", fresh);
  return parseReauthentication(deletionPost(base, "/api/account/deletion/reauthenticate", juce::var(body), current), juce::Time::currentTimeMillis());
}
AccountDeletionStatus AccountDeletionApi::status(const juce::String &base, const juce::String &receipt) {
  if (!validStatusReceipt(receipt)) return {};
  auto *body = new juce::DynamicObject(); body->setProperty("receipt", receipt);
  return parseStatus(deletionPost(base, "/api/account/deletion/status", juce::var(body)));
}

AccountDeletionStatus AccountDeletionApi::request(const juce::String &base, const juce::String &token,
                                                const juce::String &proofId, const juce::String &receipt) {
  if (token.isEmpty() || token.length() > 16384 || proofId.length() != 36 || !validStatusReceipt(receipt)) return {};
  auto *body = new juce::DynamicObject(); body->setProperty("proofId", proofId); body->setProperty("receipt", receipt);
  return parseStatus(deletionPost(base, "/api/account/deletion/request", juce::var(body), token));
}
