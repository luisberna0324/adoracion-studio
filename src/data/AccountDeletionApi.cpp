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
