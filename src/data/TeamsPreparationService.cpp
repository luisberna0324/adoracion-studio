#include "TeamsPreparationService.h"

#include <algorithm>
#include <set>

juce::String TeamsPreparationService::accountTag(const juce::String &uid) {
  return uid.trim().isEmpty() ? juce::String{} :
      juce::SHA256(uid.toUTF8()).toHexString().substring(0, 16);
}

LibraryState TeamsPreparationService::filterLibraryForAccount(
    LibraryState library, const juce::String &uid) {
  const auto tag = accountTag(uid);
  const auto songPrefix = "team-" + tag + "-";
  const auto setlistPrefix = "team-list-" + tag + "-";
  library.songs.erase(std::remove_if(library.songs.begin(), library.songs.end(),
      [&](const ImportedSongData &song) {
        return song.id.startsWith("team-") &&
               (tag.isEmpty() || !song.id.startsWith(songPrefix));
      }), library.songs.end());
  std::set<juce::String> visibleSongs;
  for (const auto &song : library.songs) visibleSongs.insert(song.id);
  library.setlists.erase(std::remove_if(library.setlists.begin(), library.setlists.end(),
      [&](SetlistData &setlist) {
        if (setlist.id.startsWith("team-list-") &&
            (tag.isEmpty() || !setlist.id.startsWith(setlistPrefix))) return true;
        setlist.items.erase(std::remove_if(setlist.items.begin(), setlist.items.end(),
            [&](const SetlistItem &item) {
              return item.songId.startsWith("team-") &&
                     visibleSongs.find(item.songId) == visibleSongs.end();
            }), setlist.items.end());
        return false;
      }), library.setlists.end());
  if (std::none_of(library.setlists.begin(), library.setlists.end(),
      [&](const SetlistData &setlist) { return setlist.id == library.activeSetlistId; }))
    library.activeSetlistId = library.setlists.empty() ? juce::String{} : library.setlists.front().id;
  return library;
}

bool TeamsPreparationService::isPreparedSongUsable(const ImportedSongData &song) {
  return !song.stems.empty() && std::all_of(song.stems.begin(), song.stems.end(),
      [](const ImportedStemData &stem) {
        return stem.storedPath.isNotEmpty() && juce::File(stem.storedPath).existsAsFile() &&
               juce::File(stem.storedPath).getSize() > 0;
      });
}

bool TeamsPreparationService::registerPreparedDownload(TeamSongLink origin,
    const CloudDownloadResult &result, ImportedSongData &song, juce::String *errorMessage) {
  const auto fail = [errorMessage](const juce::String &message) {
    if (errorMessage) *errorMessage = message;
    return false;
  };
  if (errorMessage) errorMessage->clear();
  if (origin.uid.trim().isEmpty() || origin.teamId.trim().isEmpty() ||
      origin.songId.trim().isEmpty() || origin.revision <= 0 ||
      result.descriptor.songId != origin.songId || result.descriptor.revision != origin.revision)
    return fail("La descarga no corresponde a la revisión solicitada.");
  TeamSongLink existingOrigin;
  const bool alreadyRegistered = TeamsLocalStore::findSongLink(origin.uid, origin.teamId,
      origin.songId, origin.revision, existingOrigin);
  if (alreadyRegistered) origin.localSongId = existingOrigin.localSongId;
  else {
    const auto identity = origin.uid + ":" + origin.teamId + ":" + origin.songId + ":" + juce::String(origin.revision);
    origin.localSongId = "team-" + accountTag(origin.uid) + "-" + juce::SHA256(identity.toUTF8()).toHexString();
  }
  const auto library = SongLibraryStore::loadLibrarySnapshot();
  const auto existing = std::find_if(library.songs.begin(), library.songs.end(),
      [&](const ImportedSongData &entry) { return entry.id == origin.localSongId; });
  if (existing != library.songs.end() && isPreparedSongUsable(*existing)) {
    song = *existing;
    return true;
  }
  if (existing != library.songs.end() &&
      (isProjectBackedSong(*existing) || existing->projectId != origin.localSongId))
    return fail("Repara el proyecto local antes de volver a descargar esta canción.");
  std::vector<float> bins;
  juce::String message;
  ImportedSongData materialized;
  if (!CloudProjectArchive::materializeCachedSong(result, materialized, bins, &message))
    return fail(message);
  if (!isPreparedSongUsable(materialized))
    return fail("Faltan archivos de instrumentos en la descarga.");
  materialized.id = origin.localSongId;
  materialized.projectId = origin.localSongId;
  if (existing != library.songs.end()) {
    // Recover files in place while preserving metadata and include choices the
    // musician changed locally. Setlist mix/tempo/arrangement are kept by import.
    auto retained = *existing;
    for (auto &stem : retained.stems) {
      const auto fresh = std::find_if(materialized.stems.begin(), materialized.stems.end(),
          [&](const ImportedStemData &entry) { return entry.stemId == stem.stemId; });
      if (fresh == materialized.stems.end())
        return fail("El instrumento local no pertenece a esta revisión descargada.");
      stem.storedPath = fresh->storedPath;
      stem.sourcePath = fresh->sourcePath;
      stem.fileName = fresh->fileName;
    }
    retained.artworkPath = materialized.artworkPath;
    materialized = std::move(retained);
  }
  // Keep a durable inventory of downloaded objects (including the manifest).
  // Cleanup can retry filesystem failures even after library rows are gone.
  const auto inventoryId = "files-" + juce::SHA256(origin.teamId.toUTF8()).toHexString();
  auto inventory = TeamsLocalStore::loadDetails(origin.uid, inventoryId);
  if (!inventory.isObject()) inventory = juce::var(new juce::DynamicObject());
  juce::StringArray paths;
  const auto oldFiles = inventory.getProperty("files", {});
  if (const auto *items = oldFiles.getArray()) for (const auto &item : *items) paths.addIfNotAlreadyThere(item.toString());
  for (const auto &object : result.objects) paths.addIfNotAlreadyThere(object.file.getFullPathName());
  juce::Array<juce::var> fileList;
  for (const auto &path : paths) fileList.add(path);
  inventory.getDynamicObject()->setProperty("files", fileList);
  if (!TeamsLocalStore::saveDetails(origin.uid, inventoryId, inventory))
    return fail("No se pudo guardar el inventario de la descarga.");
  // Reserve/check the immutable origin before writing a library row. Failed
  // persistence can be retried with this same ID, never creating a phantom ID.
  if (!TeamsLocalStore::upsertSongLink(origin))
    return fail("No se pudo guardar el vínculo del equipo.");
  if (!SongLibraryStore::registerCloudCachedSong(materialized) ||
      materialized.id != origin.localSongId)
    return fail("No se pudo guardar la canción del equipo.");
  if (!bins.empty()) SongLibraryStore::saveWaveformCache(materialized.id, bins);
  song = std::move(materialized);
  return true;
}

TeamsPreparationService::~TeamsPreparationService() {
  *alive = false;
  transfer.cancelAll();
  pool.removeAllJobs(true, 30000);
}

void TeamsPreparationService::prepareSong(CloudTransferCredentials credentials, juce::String uid,
    juce::String teamId, juce::String songId, int revision,
    CloudTransferClient::ProgressCallback progress, Completion complete) {
  TeamSongLink cached;
  if (TeamsLocalStore::findSongLink(uid, teamId, songId, revision, cached)) {
    for (const auto &song : SongLibraryStore::loadSongs()) {
      if (song.id != cached.localSongId) continue;
      if (isPreparedSongUsable(song)) { complete(song, {}); return; }
    }
  }
  const auto lifetime = alive;
  transfer.download(std::move(credentials), songId,
      SongLibraryStore::getRootDirectory().getChildFile("cloud-content/teams").getChildFile(accountTag(uid))
          .getChildFile(juce::SHA256(teamId.toUTF8()).toHexString()), std::move(progress),
      [this, lifetime, uid, teamId, songId, revision, completion = std::move(complete)]
      (CloudDownloadResult result, CloudTransferError error) mutable {
        if (!*lifetime) return;
        if (error.code.isNotEmpty()) { completion({}, error); return; }
        struct Job final : juce::ThreadPoolJob {
          std::shared_ptr<std::atomic<bool>> lifetime;
          CloudDownloadResult result;
          TeamSongLink link;
          Completion complete;
          Job(std::shared_ptr<std::atomic<bool>> a, CloudDownloadResult r, TeamSongLink l, Completion c)
              : ThreadPoolJob("PrepareTeamSong"), lifetime(std::move(a)), result(std::move(r)),
                link(std::move(l)), complete(std::move(c)) {}
          JobStatus runJob() override {
            if (!*lifetime || shouldExit()) return jobHasFinished;
            juce::MessageManager::callAsync([a = lifetime, downloaded = std::move(result),
                origin = link, done = std::move(complete)]() mutable {
              if (!*a) return;
              ImportedSongData song; juce::String message;
              // SongLibraryStore's read-modify-write mutations are serialized
              // by the message thread alongside the library editing actions.
              const bool ok = registerPreparedDownload(origin, downloaded, song, &message);
              if (!ok) { done({}, {0, "INVALID_TEAM_CONTENT", message}); return; }
              done(std::move(song), {});
            });
            return jobHasFinished;
          }
        };
        pool.addJob(new Job(lifetime, std::move(result), {uid, teamId, songId, revision, {}},
                            std::move(completion)), true);
      }, teamId, revision);
}


bool TeamsPreparationService::removeTeamDownloads(const juce::String &uid, const juce::String &teamId) {
  if (uid.trim().isEmpty() || teamId.trim().isEmpty()) return false;
  const auto links = TeamsLocalStore::loadSongLinks(uid, teamId);
  const auto inventoryId = "files-" + juce::SHA256(teamId.toUTF8()).toHexString();
  auto inventory = TeamsLocalStore::loadDetails(uid, inventoryId);
  if (!inventory.isObject()) inventory = juce::var(new juce::DynamicObject());
  juce::StringArray paths;
  const auto oldFiles = inventory.getProperty("files", {});
  if (const auto *items = oldFiles.getArray()) for (const auto &item : *items) paths.addIfNotAlreadyThere(item.toString());
  const auto sourceCache = SongLibraryStore::getRootDirectory().getChildFile("cloud-content/teams")
      .getChildFile(accountTag(uid)).getChildFile(juce::SHA256(teamId.toUTF8()).toHexString());
  for (const auto &file : sourceCache.findChildFiles(juce::File::findFiles, true))
    paths.addIfNotAlreadyThere(file.getFullPathName());
  const auto before = SongLibraryStore::loadLibrarySnapshot();
  for (const auto &link : links) {
    if (!link.localSongId.startsWith("team-" + accountTag(uid) + "-")) return false;
    for (const auto &song : before.songs) if (song.id == link.localSongId) {
      paths.addIfNotAlreadyThere(song.artworkPath);
      for (const auto &stem : song.stems) {
        paths.addIfNotAlreadyThere(stem.storedPath);
        paths.addIfNotAlreadyThere(stem.sourcePath);
      }
    }
  }
  juce::Array<juce::var> files;
  for (const auto &path : paths) if (path.isNotEmpty()) files.add(path);
  inventory.getDynamicObject()->setProperty("files", files);
  if (!TeamsLocalStore::saveDetails(uid, inventoryId, inventory)) return false;
  for (const auto &link : links) {
    if (std::any_of(before.songs.begin(), before.songs.end(),
        [&](const auto &s) { return s.id == link.localSongId; }) &&
        !SongLibraryStore::deleteSong(link.localSongId)) return false;
  }
  juce::StringArray retained;
  for (const auto &song : SongLibraryStore::loadLibrarySnapshot().songs) {
    retained.add(song.artworkPath);
    for (const auto &stem : song.stems) { retained.add(stem.storedPath); retained.add(stem.sourcePath); }
  }
  const auto root = SongLibraryStore::getRootDirectory().getChildFile("cloud-content");
  for (const auto &path : paths) {
    if (path.isEmpty() || !juce::File::isAbsolutePath(path) || retained.contains(path)) continue;
    const juce::File file(path);
    // Externally imported or exported files never belong to this cleanup.
    if (file.isAChildOf(root) && file.existsAsFile() && !file.deleteFile()) return false;
  }
  inventory.getDynamicObject()->setProperty("files", juce::Array<juce::var>());
  return TeamsLocalStore::saveDetails(uid, inventoryId, inventory) && TeamsLocalStore::removeSongLinks(uid, teamId);
}
