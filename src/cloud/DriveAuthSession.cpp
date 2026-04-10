#include "DriveAuthSession.h"

namespace {
juce::File getMtControlsDirectory() {
  auto dir =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("MTControls");
  dir.createDirectory();
  return dir;
}

juce::File getSessionFile() {
  return getMtControlsDirectory().getChildFile("drive_session.json");
}

juce::var toVar(const DriveAuthCredentials &credentials) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("accessToken", credentials.accessToken);
  obj->setProperty("tokenType", credentials.tokenType);
  obj->setProperty("grantedScopes", credentials.grantedScopes);
  obj->setProperty("accountEmail", credentials.accountEmail);
  obj->setProperty("accessTokenExpiresAtMs", credentials.accessTokenExpiresAtMs);
  obj->setProperty("updatedMs", credentials.updatedMs);
  obj->setProperty("remoteSnapshotFileId", credentials.remoteSnapshotFileId);
  obj->setProperty("remoteSnapshotModifiedMs",
                   credentials.remoteSnapshotModifiedMs);
  obj->setProperty("lastSyncMs", credentials.lastSyncMs);
  return juce::var(obj);
}

DriveAuthCredentials fromVar(const juce::var &v) {
  DriveAuthCredentials out;
  const auto *obj = v.getDynamicObject();
  if (obj == nullptr)
    return out;
  out.accessToken = obj->getProperty("accessToken").toString();
  out.tokenType = obj->getProperty("tokenType").toString();
  out.grantedScopes = obj->getProperty("grantedScopes").toString();
  out.accountEmail = obj->getProperty("accountEmail").toString();
  out.accessTokenExpiresAtMs =
      static_cast<juce::int64>(obj->getProperty("accessTokenExpiresAtMs"));
  out.updatedMs = static_cast<juce::int64>(obj->getProperty("updatedMs"));
  out.remoteSnapshotFileId = obj->getProperty("remoteSnapshotFileId").toString();
  out.remoteSnapshotModifiedMs =
      static_cast<juce::int64>(obj->getProperty("remoteSnapshotModifiedMs"));
  out.lastSyncMs = static_cast<juce::int64>(obj->getProperty("lastSyncMs"));
  return out;
}
} // namespace

DriveAuthCredentials DriveAuthSession::load() {
  const auto file = getSessionFile();
  if (!file.existsAsFile())
    return {};
  return fromVar(juce::JSON::parse(file));
}

bool DriveAuthSession::save(const DriveAuthCredentials &credentials) {
  auto next = credentials;
  if (next.updatedMs <= 0)
    next.updatedMs = juce::Time::currentTimeMillis();
  return getSessionFile().replaceWithText(juce::JSON::toString(toVar(next), true));
}

void DriveAuthSession::clear() {
  const auto file = getSessionFile();
  if (file.existsAsFile())
    file.deleteFile();
}

bool DriveAuthSession::hasValidAccessToken(const DriveAuthCredentials &credentials,
                                           const juce::int64 nowMs) {
  if (credentials.accessToken.trim().isEmpty())
    return false;
  if (credentials.accessTokenExpiresAtMs <= 0)
    return true;
  return nowMs + 30'000 < credentials.accessTokenExpiresAtMs;
}
