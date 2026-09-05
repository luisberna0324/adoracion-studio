#include "CloudSyncIndexStore.h"

#include "ProjectStore.h"
#include "SongLibraryStore.h"

#include <algorithm>

namespace {
CloudProjectLink readLink(const juce::var& value) {
  CloudProjectLink link;
  const auto* object = value.getDynamicObject();
  if (object == nullptr) return link;
  link.projectId = object->getProperty("projectId").toString();
  link.remoteSongId = object->getProperty("remoteSongId").toString();
  link.remoteRevision = static_cast<int>(object->getProperty("remoteRevision"));
  link.downloadedAtMs = object->getProperty("downloadedAtMs").toString().getLargeIntValue();
  link.lastSyncedAtMs = object->getProperty("lastSyncedAtMs").toString().getLargeIntValue();
  link.descriptorFingerprint = object->getProperty("descriptorFingerprint").toString().trim();
  return link;
}

juce::var writeLink(const CloudProjectLink& link) {
  auto* object = new juce::DynamicObject();
  object->setProperty("projectId", link.projectId);
  object->setProperty("remoteSongId", link.remoteSongId);
  object->setProperty("remoteRevision", link.remoteRevision);
  object->setProperty("downloadedAtMs", link.downloadedAtMs);
  object->setProperty("lastSyncedAtMs", link.lastSyncedAtMs);
  object->setProperty("descriptorFingerprint", link.descriptorFingerprint);
  return juce::var(object);
}

bool removeDirectoryIfPresent(const juce::File& directory) {
  return !directory.exists() || directory.deleteRecursively();
}

bool removeFileIfPresent(const juce::File& file) {
  return !file.existsAsFile() || file.deleteFile();
}
} // namespace

juce::File CloudSyncIndexStore::getFile() {
  return SongLibraryStore::getRootDirectory().getChildFile("cloud-sync-index.json");
}

std::vector<CloudProjectLink> CloudSyncIndexStore::loadAll() {
  std::vector<CloudProjectLink> links;
  const auto file = getFile();
  if (!file.existsAsFile()) return links;
  const auto parsed = juce::JSON::parse(file.loadFileAsString());
  const auto* root = parsed.getDynamicObject();
  if (root == nullptr) return links;
  const auto* entries = root->getProperty("links").getArray();
  if (entries == nullptr) return links;
  links.reserve(static_cast<size_t>(entries->size()));
  for (const auto& entry : *entries) {
    auto link = readLink(entry);
    if (link.projectId.isNotEmpty() && link.remoteSongId.isNotEmpty()) links.push_back(std::move(link));
  }
  return links;
}

bool CloudSyncIndexStore::saveAll(const std::vector<CloudProjectLink>& links) {
  juce::Array<juce::var> entries;
  for (const auto& link : links) entries.add(writeLink(link));
  auto* root = new juce::DynamicObject();
  root->setProperty("version", 1);
  root->setProperty("links", juce::var(entries));
  return getFile().replaceWithText(juce::JSON::toString(juce::var(root), true));
}

bool CloudSyncIndexStore::findByProjectId(const juce::String& projectId, CloudProjectLink& result) {
  for (const auto& link : loadAll()) if (link.projectId == projectId) { result = link; return true; }
  return false;
}

bool CloudSyncIndexStore::findByRemoteSongId(const juce::String& remoteSongId, CloudProjectLink& result) {
  for (const auto& link : loadAll()) if (link.remoteSongId == remoteSongId) { result = link; return true; }
  return false;
}

bool CloudSyncIndexStore::upsert(CloudProjectLink link) {
  if (link.projectId.trim().isEmpty() || link.remoteSongId.trim().isEmpty()) return false;
  auto links = loadAll();
  for (auto& current : links) {
    if (current.projectId == link.projectId || current.remoteSongId == link.remoteSongId) {
      current = std::move(link);
      return saveAll(links);
    }
  }
  links.push_back(std::move(link));
  return saveAll(links);
}

bool CloudSyncIndexStore::removeByProjectId(const juce::String& projectId) {
  auto links = loadAll();
  const auto oldSize = links.size();
  links.erase(std::remove_if(links.begin(), links.end(), [&projectId](const auto& link) {
    return link.projectId == projectId;
  }), links.end());
  return oldSize == links.size() || saveAll(links);
}

CloudLocalPurgeResult CloudSyncIndexStore::purgeLocalState() {
  CloudLocalPurgeResult result;
  const auto root = SongLibraryStore::getRootDirectory();
  result.removedLibrarySongs =
      static_cast<int>(SongLibraryStore::loadSongs().size());

  // Make pending sidecar writers quiescent before the complete local reset.
  ProjectStore::flushAllPendingSettings();

  // `projects/` is deliberately excluded: it can contain the user's .pwproj
  // packages. All entries that make those packages appear in the library are
  // removed, so they can only return by explicitly opening them again.
  for (const auto& directory : {"audio", "waveforms", "decoded",
                                "cloud-artwork", "cloud-content", "project-cache",
                                "migration-staging", "project-staging",
                                "cloud-sync-staging"}) {
    result.clearedLocalLibrary =
        removeDirectoryIfPresent(root.getChildFile(directory)) &&
        result.clearedLocalLibrary;
  }
  for (const auto& file : {"songs.json", "projects.json",
                           "cloud-sync-index.json"}) {
    result.clearedLocalLibrary =
        removeFileIfPresent(root.getChildFile(file)) &&
        result.clearedLocalLibrary;
  }
  result.clearedLocalLibrary =
      removeDirectoryIfPresent(root.getChildFile("projects")
                                   .getChildFile(".cloud-downloads")) &&
      result.clearedLocalLibrary;
  return result;
}
