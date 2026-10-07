#include "TeamsLocalStore.h"

#include "ApplicationDataPaths.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <limits>
#include <mutex>
#include <set>

namespace {
constexpr int schemaVersion = 1;
constexpr juce::int64 maximumCacheBytes = 4 * 1024 * 1024;
std::mutex storeMutex;

bool validId(const juce::String &value) {
  return value.isNotEmpty() && value.length() <= 512 && value == value.trim() &&
         !value.containsChar('\n') && !value.containsChar('\r');
}

juce::String pathHash(const juce::String &value) {
  return juce::SHA256(value.toRawUTF8(), value.getNumBytesAsUTF8()).toHexString();
}

juce::File detailsFile(const juce::String &uid, const juce::String &teamId) {
  return TeamsLocalStore::getAccountDirectory(uid)
      .getChildFile("details-" + pathHash(teamId) + ".json");
}

bool privateField(const juce::Identifier &property) {
  const auto key = property.toString().toLowerCase();
  return key == "idtoken" || key == "refreshtoken" || key == "accesstoken" ||
         key == "authorization" || key == "token" || key == "invitetoken" ||
         key == "url" || key.endsWith("url") || key.endsWith("urls");
}

juce::var metadataOnly(const juce::var &value) {
  if (const auto *object = value.getDynamicObject()) {
    auto copy = juce::var(new juce::DynamicObject());
    for (const auto &property : object->getProperties())
      if (!privateField(property.name))
        copy.getDynamicObject()->setProperty(property.name,
                                             metadataOnly(property.value));
    return copy;
  }
  if (const auto *array = value.getArray()) {
    juce::Array<juce::var> copy;
    for (const auto &entry : *array)
      copy.add(metadataOnly(entry));
    return juce::var(copy);
  }
  return value;
}

bool writeEnvelope(const juce::File &file, const juce::String &uid,
                   const juce::String &teamId, const juce::var &payload) {
  if (!file.getParentDirectory().createDirectory())
    return false;
  auto value = juce::var(new juce::DynamicObject());
  auto *object = value.getDynamicObject();
  object->setProperty("schemaVersion", schemaVersion);
  object->setProperty("uid", uid);
  object->setProperty("teamId", teamId);
  object->setProperty("payload", payload);
  const auto json = juce::JSON::toString(value, true);
  if (static_cast<juce::int64>(json.getNumBytesAsUTF8()) > maximumCacheBytes)
    return false;
  juce::TemporaryFile temporary(file, juce::TemporaryFile::useHiddenFile);
  {
    juce::FileOutputStream output(temporary.getFile());
    if (!output.openedOk() || !output.writeText(json, false, false, "\n"))
      return false;
    output.flush();
    if (output.getStatus().failed())
      return false;
  }
  return temporary.overwriteTargetFileWithTemporary();
}

bool readEnvelope(const juce::File &file, const juce::String &uid,
                  const juce::String &teamId, juce::var &payload) {
  if (!file.existsAsFile() || file.getSize() > maximumCacheBytes)
    return false;
  const auto parsed = juce::JSON::parse(file.loadFileAsString());
  const auto *object = parsed.getDynamicObject();
  if (object == nullptr ||
      static_cast<int>(object->getProperty("schemaVersion")) != schemaVersion ||
      object->getProperty("uid").toString() != uid ||
      object->getProperty("teamId").toString() != teamId ||
      !object->hasProperty("payload"))
    return false;
  payload = object->getProperty("payload");
  return true;
}

bool validLink(const TeamSongLink &link) {
  return validId(link.uid) && validId(link.teamId) && validId(link.songId) &&
         validId(link.localSongId) && link.revision > 0;
}

juce::var linkToVar(const TeamSongLink &link) {
  auto value = juce::var(new juce::DynamicObject());
  auto *object = value.getDynamicObject();
  object->setProperty("uid", link.uid);
  object->setProperty("teamId", link.teamId);
  object->setProperty("songId", link.songId);
  object->setProperty("revision", link.revision);
  object->setProperty("localSongId", link.localSongId);
  return value;
}

bool readAllLinks(const juce::String &uid, std::vector<TeamSongLink> &links) {
  const auto file = TeamsLocalStore::getAccountDirectory(uid)
                        .getChildFile("song-links.json");
  if (!file.exists())
    return true;
  juce::var payload;
  if (!readEnvelope(file, uid, {}, payload) || !payload.isArray())
    return false;
  std::set<juce::String> origins, localIds;
  for (const auto &entry : *payload.getArray()) {
    const auto *object = entry.getDynamicObject();
    if (object == nullptr)
      return false;
    const auto &revision = object->getProperty("revision");
    if (!revision.isInt() && !revision.isInt64())
      return false;
    const auto largeRevision = static_cast<juce::int64>(revision);
    if (largeRevision <= 0 || largeRevision > std::numeric_limits<int>::max())
      return false;
    TeamSongLink link{object->getProperty("uid").toString(),
                      object->getProperty("teamId").toString(),
                      object->getProperty("songId").toString(),
                      static_cast<int>(largeRevision),
                      object->getProperty("localSongId").toString()};
    if (!validLink(link) || link.uid != uid)
      return false;
    // JSON avoids ambiguous delimiters in identifiers received from a server.
    juce::Array<juce::var> origin{link.teamId, link.songId, link.revision};
    if (!origins.insert(juce::JSON::toString(juce::var(origin))).second ||
        !localIds.insert(link.localSongId).second)
      return false;
    links.push_back(std::move(link));
  }
  return true;
}
} // namespace

juce::File TeamsLocalStore::getAccountDirectory(const juce::String &uid) {
  if (!validId(uid))
    return {};
  return ApplicationDataPaths::applicationDataDirectory()
      .getChildFile("MTControls/teams")
      .getChildFile(pathHash(uid));
}

bool TeamsLocalStore::saveOverview(const juce::String &uid,
                                   const juce::var &overview) {
  if (!validId(uid) || overview.getDynamicObject() == nullptr)
    return false;
  const std::lock_guard<std::mutex> guard(storeMutex);
  return writeEnvelope(getAccountDirectory(uid).getChildFile("overview.json"),
                       uid, {}, metadataOnly(overview));
}

juce::var TeamsLocalStore::loadOverview(const juce::String &uid) {
  if (!validId(uid))
    return {};
  const std::lock_guard<std::mutex> guard(storeMutex);
  juce::var payload;
  if (!readEnvelope(getAccountDirectory(uid).getChildFile("overview.json"),
                    uid, {}, payload) || payload.getDynamicObject() == nullptr)
    return {};
  return payload;
}

bool TeamsLocalStore::saveDetails(const juce::String &uid,
                                  const juce::String &teamId,
                                  const juce::var &details) {
  if (!validId(uid) || !validId(teamId) || details.getDynamicObject() == nullptr)
    return false;
  const std::lock_guard<std::mutex> guard(storeMutex);
  return writeEnvelope(detailsFile(uid, teamId), uid, teamId,
                       metadataOnly(details));
}

juce::var TeamsLocalStore::loadDetails(const juce::String &uid,
                                      const juce::String &teamId) {
  if (!validId(uid) || !validId(teamId))
    return {};
  const std::lock_guard<std::mutex> guard(storeMutex);
  juce::var payload;
  if (!readEnvelope(detailsFile(uid, teamId), uid, teamId, payload) ||
      payload.getDynamicObject() == nullptr)
    return {};
  return payload;
}

bool TeamsLocalStore::upsertSongLink(const TeamSongLink &link) {
  if (!validLink(link))
    return false;
  const std::lock_guard<std::mutex> guard(storeMutex);
  std::vector<TeamSongLink> links;
  if (!readAllLinks(link.uid, links))
    return false;
  for (const auto &existing : links) {
    if (existing.teamId == link.teamId && existing.songId == link.songId &&
        existing.revision == link.revision)
      return existing.localSongId == link.localSongId;
    if (existing.localSongId == link.localSongId)
      return false;
  }
  links.push_back(link);
  juce::Array<juce::var> entries;
  for (const auto &entry : links)
    entries.add(linkToVar(entry));
  return writeEnvelope(getAccountDirectory(link.uid).getChildFile("song-links.json"),
                       link.uid, {}, juce::var(entries));
}

bool TeamsLocalStore::findSongLink(const juce::String &uid,
                                   const juce::String &teamId,
                                   const juce::String &songId, int revision,
                                   TeamSongLink &result) {
  if (!validId(songId) || revision <= 0)
    return false;
  for (const auto &link : loadSongLinks(uid, teamId))
    if (link.songId == songId && link.revision == revision) {
      result = link;
      return true;
    }
  return false;
}

std::vector<TeamSongLink> TeamsLocalStore::loadSongLinks(
    const juce::String &uid, const juce::String &teamId) {
  if (!validId(uid) || !validId(teamId))
    return {};
  const std::lock_guard<std::mutex> guard(storeMutex);
  std::vector<TeamSongLink> links;
  if (!readAllLinks(uid, links))
    return {};
  links.erase(std::remove_if(links.begin(), links.end(),
                            [&teamId](const TeamSongLink &link) {
                              return link.teamId != teamId;
                            }), links.end());
  return links;
}

// Account, source and membership lifetime each scope the welcome/progress.
juce::String TeamsLocalStore::onboardingLessonId(const juce::String &uid,
    const juce::String &teamId, const juce::String &membershipId) {
  if (!validId(uid) || !validId(teamId) || !validId(membershipId)) return {};
  juce::Array<juce::var> parts{juce::var(uid), juce::var(teamId), juce::var(membershipId)};
  return "teams-cloud-" + pathHash(juce::JSON::toString(juce::var(parts), true));
}


bool TeamsLocalStore::removeSongLinks(const juce::String &uid, const juce::String &teamId) {
  if (!validId(uid) || !validId(teamId)) return false;
  const std::lock_guard<std::mutex> guard(storeMutex);
  std::vector<TeamSongLink> links;
  if (!readAllLinks(uid, links)) return false;
  juce::Array<juce::var> kept;
  for (const auto &link : links) if (link.teamId != teamId) kept.add(linkToVar(link));
  return writeEnvelope(getAccountDirectory(uid).getChildFile("song-links.json"), uid, {}, kept);
}
