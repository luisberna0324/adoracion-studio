#include "SessionPersistence.h"
#include "SessionAuthLogger.h"

namespace {
juce::File getMtControlsDirectory() {
  auto dir =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("MTControls");
  dir.createDirectory();
  return dir;
}

constexpr int kSnapshotVersion = 1;
} // namespace

juce::File SessionPersistence::getSnapshotFile() {
  return getMtControlsDirectory().getChildFile("session_snapshot.json");
}

bool SessionPersistence::load(PersistedSessionSnapshot &out) {
  const auto f = getSnapshotFile();
  if (!f.existsAsFile()) {
    SessionAuthLogger::write("SessionPersistence::load: no snapshot file at " +
                             f.getFullPathName());
    return false;
  }

  const auto parsed = juce::JSON::parse(f.loadFileAsString());
  const auto *o = parsed.getDynamicObject();
  if (o == nullptr || !o->hasProperty("version")) {
    SessionAuthLogger::write(
        "SessionPersistence::load: invalid JSON or missing version");
    return false;
  }

  const int ver = static_cast<int>(o->getProperty("version"));
  if (ver != kSnapshotVersion) {
    SessionAuthLogger::write("SessionPersistence::load: version mismatch got " +
                             juce::String(ver) + " expected " +
                             juce::String(kSnapshotVersion));
    return false;
  }

  out.uid = o->getProperty("uid").toString().trim();
  out.email = o->getProperty("email").toString().trim();
  out.plan = o->getProperty("plan").toString().trim();
  out.subscriptionStatus =
      o->getProperty("subscriptionStatus").toString().trim();
  out.expiresAt = o->getProperty("expiresAt").toString().trim();
  out.licenseKey = o->getProperty("licenseKey").toString().trim();
  out.allowed = static_cast<bool>(o->getProperty("allowed"));
  out.reasonCode = o->getProperty("reasonCode").toString().trim();
  out.reason = o->getProperty("reason").toString().trim();
  out.refreshToken = o->getProperty("refreshToken").toString().trim();
  out.firebaseApiKey = o->getProperty("firebaseApiKey").toString().trim();
  out.idTokenExpiresAtMs =
      static_cast<juce::int64>(o->getProperty("idTokenExpiresAtMs"));
  out.lastOnlineValidationMs =
      static_cast<juce::int64>(o->getProperty("lastOnlineValidationMs"));

  if (!out.hasCredentials()) {
    SessionAuthLogger::write(
        "SessionPersistence::load: missing refreshToken or firebaseApiKey (refresh " +
        SessionAuthLogger::tokenPresence(out.refreshToken) + " apiKey " +
        SessionAuthLogger::tokenPresence(out.firebaseApiKey) + ")");
    return false;
  }

  SessionAuthLogger::write(
      "SessionPersistence::load: ok uid=" + out.uid + " plan=" + out.plan +
      " status=" + out.subscriptionStatus + " allowed=" +
      juce::String(static_cast<int>(out.allowed)) + " lastOnlineMs=" +
      juce::String(out.lastOnlineValidationMs));
  return true;
}

bool SessionPersistence::save(const PersistedSessionSnapshot &snapshot) {
  auto *o = new juce::DynamicObject();
  o->setProperty("version", kSnapshotVersion);
  o->setProperty("uid", snapshot.uid);
  o->setProperty("email", snapshot.email);
  o->setProperty("plan", snapshot.plan);
  o->setProperty("subscriptionStatus", snapshot.subscriptionStatus);
  o->setProperty("expiresAt", snapshot.expiresAt);
  o->setProperty("licenseKey", snapshot.licenseKey);
  o->setProperty("allowed", snapshot.allowed);
  o->setProperty("reasonCode", snapshot.reasonCode);
  o->setProperty("reason", snapshot.reason);
  o->setProperty("refreshToken", snapshot.refreshToken);
  o->setProperty("firebaseApiKey", snapshot.firebaseApiKey);
  o->setProperty("idTokenExpiresAtMs", snapshot.idTokenExpiresAtMs);
  o->setProperty("lastOnlineValidationMs", snapshot.lastOnlineValidationMs);

  const juce::var root(o);
  const bool ok =
      getSnapshotFile().replaceWithText(juce::JSON::toString(root, true));
  SessionAuthLogger::write(
      juce::String("SessionPersistence::save: ") + (ok ? "ok" : "FAILED") +
      " path=" + getSnapshotFile().getFullPathName());
  return ok;
}

void SessionPersistence::clear() {
  SessionAuthLogger::write("SessionPersistence::clear: deleting snapshot");
  getSnapshotFile().deleteFile();
}
