#include "SessionPersistence.h"
#include "SessionAuthLogger.h"
#include "SongLibraryStore.h"

namespace {
juce::CriticalSection snapshotLock;
juce::File getMtControlsDirectory() {
  // Reuse the library's root, including its explicitly isolated test root.
  auto dir = SongLibraryStore::getRootDirectory();
  dir.createDirectory();
  return dir;
}

constexpr int kSnapshotVersion = 1;

juce::String getObfuscationKey() {
  juce::String hwid = juce::SystemStats::getUniqueDeviceID().trim();
  if (hwid.isEmpty())
    hwid = juce::SystemStats::getComputerName().trim();
  if (hwid.isEmpty())
    hwid = "PlayWorshipBackupKey";
  return hwid + "_MTControlsSalt2024";
}

juce::String obfuscateToOblivion(const juce::String &cleartext) {
  juce::MemoryBlock mb;
  mb.append(cleartext.toRawUTF8(), cleartext.getNumBytesAsUTF8());

  juce::String key = getObfuscationKey();
  auto *data = static_cast<char *>(mb.getData());
  for (size_t i = 0; i < mb.getSize(); ++i) {
    data[i] ^= key[static_cast<int>(i) % key.length()];
  }

  return mb.toBase64Encoding();
}

juce::String deobfuscateFromOblivion(const juce::String &base64Text) {
  juce::MemoryBlock mb;
  if (!mb.fromBase64Encoding(base64Text))
    return {};

  juce::String key = getObfuscationKey();
  auto *data = static_cast<char *>(mb.getData());
  for (size_t i = 0; i < mb.getSize(); ++i) {
    data[i] ^= key[static_cast<int>(i) % key.length()];
  }

  return juce::String::fromUTF8(data, static_cast<int>(mb.getSize()));
}

} // namespace

juce::File SessionPersistence::getSnapshotFile() {
  return getMtControlsDirectory().getChildFile("session_snapshot.json");
}

bool SessionPersistence::load(PersistedSessionSnapshot &out) {
  const juce::ScopedLock lock(snapshotLock);
  const auto f = getSnapshotFile();
  if (!f.existsAsFile()) {
    SessionAuthLogger::write("SessionPersistence::load: no snapshot file at " +
                             f.getFullPathName());
    return false;
  }

  const juce::String rawText = f.loadFileAsString();
  juce::String jsonText = deobfuscateFromOblivion(rawText);
  if (jsonText.isEmpty() || !jsonText.startsWithChar('{'))
    jsonText = rawText; // Fallback para snapshots viejos legibles

  const auto parsed = juce::JSON::parse(jsonText);
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
  if (isAccountDeletionPending(out.uid)) { out = {}; f.deleteFile(); return false; }
  out.authProvider = o->getProperty("authProvider").toString();
  out.authProviderUser = o->getProperty("authProviderUser").toString();
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
  const juce::ScopedLock lock(snapshotLock);
  if (isAccountDeletionPending(snapshot.uid)) return false;
  auto *o = new juce::DynamicObject();
  o->setProperty("version", kSnapshotVersion);
  o->setProperty("uid", snapshot.uid);
  o->setProperty("authProvider", snapshot.authProvider);
  o->setProperty("authProviderUser", snapshot.authProviderUser);
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
  const juce::String jsonText = juce::JSON::toString(root, true);
  const juce::String obfuscatedText = obfuscateToOblivion(jsonText);

  const bool ok =
      getSnapshotFile().replaceWithText(obfuscatedText);
  SessionAuthLogger::write(
      juce::String("SessionPersistence::save: ") + (ok ? "ok" : "FAILED") +
      " path=" + getSnapshotFile().getFullPathName());
  return ok;
}

void SessionPersistence::clear() {
  const juce::ScopedLock lock(snapshotLock);
  SessionAuthLogger::write("SessionPersistence::clear: deleting snapshot");
  getSnapshotFile().deleteFile();
}

namespace {
juce::File deletionMarker(const juce::String &uid) {
  const auto bytes = uid.toUTF8();
  return getMtControlsDirectory().getChildFile("account-deletion-blocks")
    .getChildFile(juce::SHA256(bytes.getAddress(), bytes.sizeInBytes() - 1).toHexString());
}
}
bool SessionPersistence::isAccountDeletionPending(const juce::String &uid) {
  const juce::ScopedLock lock(snapshotLock);
  return uid.isNotEmpty() && deletionMarker(uid).exists();
}
bool SessionPersistence::markAccountDeletionPending(const juce::String &uid) {
  const juce::ScopedLock lock(snapshotLock);
  if (uid.isEmpty()) return false;
  PersistedSessionSnapshot current; const bool loaded = load(current);
  const auto marker = deletionMarker(uid);
  if (!marker.getParentDirectory().createDirectory() || !marker.replaceWithText("pending")) return false;
  if (loaded && current.uid == uid) clear();
  return true;
}
