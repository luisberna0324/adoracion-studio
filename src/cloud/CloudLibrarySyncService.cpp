#include "CloudLibrarySyncService.h"

#include <cstring>

namespace {
constexpr const char *kRootFolderName = "PlayWorship";
constexpr const char *kLibraryFolderName = "library";
constexpr const char *kStemsFolderName = "stems";
constexpr const char *kSnapshotFileName = "library_snapshot_v1.json";
constexpr const char *kSnapshotVersionPrefix = "drive-v1-";

bool ensureDriveLayout(DriveApiClient &client, DriveApiClient::DriveFile &root,
                       DriveApiClient::DriveFile &library,
                       DriveApiClient::DriveFile &stems,
                       juce::String &errorMessage) {
  if (!client.findOrCreateFolder(kRootFolderName, "root", root, errorMessage))
    return false;
  if (!client.findOrCreateFolder(kLibraryFolderName, root.id, library, errorMessage))
    return false;
  if (!client.findOrCreateFolder(kStemsFolderName, library.id, stems, errorMessage))
    return false;
  return true;
}
} // namespace

CloudSyncResult CloudLibrarySyncService::pushToDrive() {
  CloudSyncResult result;
  auto credentials = DriveAuthSession::load();
  if (!DriveAuthSession::hasValidAccessToken(credentials)) {
    result.message = "Drive session expired. Reconnect with Google.";
    return result;
  }

  DriveApiClient client;
  client.setAccessToken(credentials.accessToken);

  DriveApiClient::DriveFile rootFolder;
  DriveApiClient::DriveFile libraryFolder;
  DriveApiClient::DriveFile stemsFolder;
  juce::String errorMessage;
  if (!ensureDriveLayout(client, rootFolder, libraryFolder, stemsFolder,
                         errorMessage)) {
    result.message = errorMessage;
    return result;
  }

  auto library = SongLibraryStore::loadLibrarySnapshot();
  const auto nowMs = juce::Time::currentTimeMillis();
  int stemsUploaded = 0;
  int songsUploaded = 0;

  for (auto &song : library.songs) {
    if (song.id.trim().isEmpty())
      continue;

    DriveApiClient::DriveFile songFolder;
    if (!client.findOrCreateFolder(song.id, stemsFolder.id, songFolder,
                                   errorMessage)) {
      result.message = errorMessage;
      return result;
    }

    int songStemCount = 0;
    for (auto &stem : song.stems) {
      if (!stem.include)
        continue;
      const juce::File sourceFile(stem.storedPath);
      if (!sourceFile.existsAsFile())
        continue;

      if (stem.stemId.isEmpty())
        stem.stemId = juce::Uuid().toString();
      const auto ext = sourceFile.getFileExtension();
      const auto remoteName =
          juce::File::createLegalFileName(stem.stemId) +
          (ext.isNotEmpty() ? ext : ".bin");
      const auto mime = guessMimeTypeForFile(sourceFile);
      juce::String uploadedFileId;
      if (!client.uploadBinaryFile(remoteName, songFolder.id, mime, sourceFile,
                                   uploadedFileId, errorMessage,
                                   stem.remoteFileId)) {
        result.message = errorMessage;
        return result;
      }

      stem.remoteFileId = uploadedFileId;
      stem.contentHash = fileSha256(sourceFile);
      stem.updatedMs = nowMs;
      ++songStemCount;
      ++stemsUploaded;
    }

    song.updatedMs = nowMs;
    song.snapshotVersion =
        juce::String(kSnapshotVersionPrefix) + juce::String(nowMs);
    if (songStemCount > 0)
      ++songsUploaded;
  }

  if (!SongLibraryStore::saveLibrarySnapshot(library)) {
    result.message = "Couldn't persist local metadata after Drive upload.";
    return result;
  }

  juce::String existingSnapshotId = credentials.remoteSnapshotFileId;
  if (existingSnapshotId.isEmpty()) {
    DriveApiClient::DriveFile snapshotFile;
    if (client.findFileByName(kSnapshotFileName, libraryFolder.id, snapshotFile,
                              errorMessage))
      existingSnapshotId = snapshotFile.id;
  }

  juce::String snapshotFileId;
  const auto snapshotJson = SongLibraryStore::exportLibraryJson(library);
  if (!client.uploadTextFile(kSnapshotFileName, libraryFolder.id,
                             "application/json; charset=utf-8", snapshotJson,
                             snapshotFileId, errorMessage, existingSnapshotId)) {
    result.message = errorMessage;
    return result;
  }

  DriveApiClient::DriveFile snapshotInfo;
  if (client.findFileByName(kSnapshotFileName, libraryFolder.id, snapshotInfo,
                            errorMessage)) {
    credentials.remoteSnapshotModifiedMs =
        parseDriveTimeMs(snapshotInfo.modifiedTime);
  }
  credentials.remoteSnapshotFileId = snapshotFileId;
  credentials.lastSyncMs = juce::Time::currentTimeMillis();
  DriveAuthSession::save(credentials);

  result.ok = true;
  result.songsProcessed = songsUploaded;
  result.stemsProcessed = stemsUploaded;
  result.message = "Uploaded " + juce::String(songsUploaded) + " songs and " +
                   juce::String(stemsUploaded) + " stems to Drive.";
  return result;
}

CloudSyncResult CloudLibrarySyncService::pushSongToDrive(const juce::String &songIdIn) {
  CloudSyncResult result;
  const juce::String songId = songIdIn.trim();
  if (songId.isEmpty()) {
    result.message = "No song id.";
    return result;
  }

  auto credentials = DriveAuthSession::load();
  if (!DriveAuthSession::hasValidAccessToken(credentials)) {
    result.message = "Drive session expired. Reconnect with Google.";
    return result;
  }

  DriveApiClient client;
  client.setAccessToken(credentials.accessToken);

  DriveApiClient::DriveFile rootFolder;
  DriveApiClient::DriveFile libraryFolder;
  DriveApiClient::DriveFile stemsFolder;
  juce::String errorMessage;
  if (!ensureDriveLayout(client, rootFolder, libraryFolder, stemsFolder,
                         errorMessage)) {
    result.message = errorMessage;
    return result;
  }

  auto library = SongLibraryStore::loadLibrarySnapshot();
  ImportedSongData *songPtr = nullptr;
  for (auto &s : library.songs) {
    if (s.id == songId) {
      songPtr = &s;
      break;
    }
  }
  if (songPtr == nullptr) {
    result.message = "Song not found in local library.";
    return result;
  }

  DriveApiClient::DriveFile songFolder;
  if (!client.findOrCreateFolder(songId, stemsFolder.id, songFolder,
                                 errorMessage)) {
    result.message = errorMessage;
    return result;
  }

  const auto nowMs = juce::Time::currentTimeMillis();
  int stemsUploaded = 0;
  for (auto &stem : songPtr->stems) {
    if (!stem.include)
      continue;
    const juce::File sourceFile(stem.storedPath);
    if (!sourceFile.existsAsFile())
      continue;

    if (stem.stemId.isEmpty())
      stem.stemId = juce::Uuid().toString();
    const auto ext = sourceFile.getFileExtension();
    const auto remoteName =
        juce::File::createLegalFileName(stem.stemId) +
        (ext.isNotEmpty() ? ext : ".bin");
    const auto mime = guessMimeTypeForFile(sourceFile);
    juce::String uploadedFileId;
    if (!client.uploadBinaryFile(remoteName, songFolder.id, mime, sourceFile,
                                 uploadedFileId, errorMessage,
                                 stem.remoteFileId)) {
      result.message = errorMessage;
      return result;
    }

    stem.remoteFileId = uploadedFileId;
    stem.contentHash = fileSha256(sourceFile);
    stem.updatedMs = nowMs;
    ++stemsUploaded;
  }

  songPtr->updatedMs = nowMs;
  songPtr->snapshotVersion =
      juce::String(kSnapshotVersionPrefix) + juce::String(nowMs);

  if (!SongLibraryStore::saveLibrarySnapshot(library)) {
    result.message = "Couldn't persist local metadata after Drive upload.";
    return result;
  }

  library = SongLibraryStore::loadLibrarySnapshot();
  const ImportedSongData *pushedSong = nullptr;
  for (const auto &s : library.songs) {
    if (s.id == songId) {
      pushedSong = &s;
      break;
    }
  }
  if (pushedSong == nullptr) {
    result.message = "Couldn't re-load song after upload.";
    return result;
  }

  juce::String existingSnapshotId = credentials.remoteSnapshotFileId;
  if (existingSnapshotId.isEmpty()) {
    DriveApiClient::DriveFile snapshotLookup;
    if (client.findFileByName(kSnapshotFileName, libraryFolder.id, snapshotLookup,
                              errorMessage))
      existingSnapshotId = snapshotLookup.id;
  }

  LibraryState merged;
  juce::String remoteJson;
  bool haveRemote = false;
  DriveApiClient::DriveFile remoteSnapshotFile;
  if (client.findFileByName(kSnapshotFileName, libraryFolder.id, remoteSnapshotFile,
                            errorMessage) &&
      remoteSnapshotFile.id.isNotEmpty()) {
    if (client.downloadFileToString(remoteSnapshotFile.id, remoteJson,
                                    errorMessage)) {
      juce::String parseErr;
      if (SongLibraryStore::parseLibraryFromJson(remoteJson, merged, &parseErr))
        haveRemote = true;
    }
  }

  if (!haveRemote)
    merged = SongLibraryStore::loadLibrarySnapshot();
  else {
    bool replaced = false;
    for (auto &s : merged.songs) {
      if (s.id == songId) {
        s = *pushedSong;
        replaced = true;
        break;
      }
    }
    if (!replaced)
      merged.songs.push_back(*pushedSong);
  }

  juce::String snapshotFileId;
  const auto snapshotJson = SongLibraryStore::exportLibraryJson(merged);
  if (!client.uploadTextFile(kSnapshotFileName, libraryFolder.id,
                             "application/json; charset=utf-8", snapshotJson,
                             snapshotFileId, errorMessage, existingSnapshotId)) {
    result.message = errorMessage;
    return result;
  }

  DriveApiClient::DriveFile snapshotInfo;
  if (client.findFileByName(kSnapshotFileName, libraryFolder.id, snapshotInfo,
                            errorMessage)) {
    credentials.remoteSnapshotModifiedMs =
        parseDriveTimeMs(snapshotInfo.modifiedTime);
  }
  credentials.remoteSnapshotFileId = snapshotFileId;
  credentials.lastSyncMs = juce::Time::currentTimeMillis();
  DriveAuthSession::save(credentials);

  result.ok = true;
  result.songsProcessed = stemsUploaded > 0 ? 1 : 0;
  result.stemsProcessed = stemsUploaded;
  result.message = "Synced song to Drive.";
  return result;
}

bool CloudLibrarySyncService::fetchRemoteLibraryPreview(LibraryState &outLibrary,
                                                        juce::String &errorMessage) {
  outLibrary = {};
  if (!hasValidDriveSession()) {
    errorMessage = "Drive session expired. Reconnect with Google.";
    return false;
  }

  auto credentials = DriveAuthSession::load();
  DriveApiClient client;
  client.setAccessToken(credentials.accessToken);

  DriveApiClient::DriveFile rootFolder;
  DriveApiClient::DriveFile libraryFolder;
  DriveApiClient::DriveFile stemsFolder;
  if (!ensureDriveLayout(client, rootFolder, libraryFolder, stemsFolder,
                         errorMessage)) {
    return false;
  }

  DriveApiClient::DriveFile snapshotFile;
  if (!client.findFileByName(kSnapshotFileName, libraryFolder.id, snapshotFile,
                             errorMessage) ||
      snapshotFile.id.isEmpty()) {
    if (errorMessage.isEmpty())
      errorMessage = "No cloud snapshot found in Drive.";
    return false;
  }

  juce::String snapshotJson;
  if (!client.downloadFileToString(snapshotFile.id, snapshotJson, errorMessage))
    return false;

  juce::String parseErr;
  if (!SongLibraryStore::parseLibraryFromJson(snapshotJson, outLibrary,
                                              &parseErr)) {
    errorMessage = parseErr.isNotEmpty() ? parseErr
                                         : "Couldn't parse cloud library.";
    return false;
  }

  return true;
}

CloudSyncResult CloudLibrarySyncService::pullFromDrive() {
  CloudSyncResult result;
  auto credentials = DriveAuthSession::load();
  if (!DriveAuthSession::hasValidAccessToken(credentials)) {
    result.message = "Drive session expired. Reconnect with Google.";
    return result;
  }

  DriveApiClient client;
  client.setAccessToken(credentials.accessToken);

  DriveApiClient::DriveFile rootFolder;
  DriveApiClient::DriveFile libraryFolder;
  DriveApiClient::DriveFile stemsFolder;
  juce::String errorMessage;
  if (!ensureDriveLayout(client, rootFolder, libraryFolder, stemsFolder,
                         errorMessage)) {
    result.message = errorMessage;
    return result;
  }

  DriveApiClient::DriveFile snapshotFile;
  if (!client.findFileByName(kSnapshotFileName, libraryFolder.id, snapshotFile,
                             errorMessage) ||
      snapshotFile.id.isEmpty()) {
    result.message = "No cloud snapshot found in Drive.";
    return result;
  }

  juce::String snapshotJson;
  if (!client.downloadFileToString(snapshotFile.id, snapshotJson, errorMessage)) {
    result.message = errorMessage;
    return result;
  }

  const auto dbFile = SongLibraryStore::getLibraryDbFile();
  const auto localModifiedMs =
      dbFile.existsAsFile()
          ? dbFile.getLastModificationTime().toMilliseconds()
          : 0;
  const auto remoteModifiedMs = parseDriveTimeMs(snapshotFile.modifiedTime);
  if (credentials.lastSyncMs > 0 && localModifiedMs > credentials.lastSyncMs &&
      remoteModifiedMs > credentials.lastSyncMs && dbFile.existsAsFile()) {
    const auto backup = dbFile.getSiblingFile(
        "songs.local-conflict." + juce::String(juce::Time::currentTimeMillis()) +
        ".json");
    dbFile.copyFileTo(backup);
  }

  if (!dbFile.replaceWithText(snapshotJson)) {
    result.message = "Couldn't write local snapshot file.";
    return result;
  }

  auto library = SongLibraryStore::loadLibrarySnapshot();
  const auto audioRoot = SongLibraryStore::getAudioStorageRoot();

  int downloadedSongs = 0;
  int downloadedStems = 0;
  for (auto &song : library.songs) {
    if (song.id.isEmpty())
      continue;

    bool songHadDownloads = false;
    const auto songDir = audioRoot.getChildFile(song.id);
    songDir.createDirectory();

    for (auto &stem : song.stems) {
      if (!stem.include || stem.remoteFileId.trim().isEmpty())
        continue;

      if (stem.stemId.isEmpty())
        stem.stemId = juce::Uuid().toString();
      const auto legalStemId = juce::File::createLegalFileName(stem.stemId);
      const auto ext = stemExtensionFor(stem);
      auto destination = songDir.getChildFile(legalStemId + "." + ext);

      if (!client.downloadFileToFile(stem.remoteFileId, destination,
                                     errorMessage)) {
        result.message = errorMessage;
        return result;
      }

      stem.storedPath = destination.getFullPathName();
      stem.sourcePath = destination.getFullPathName();
      stem.fileName = destination.getFileName();
      stem.contentHash = fileSha256(destination);
      stem.updatedMs = juce::Time::currentTimeMillis();
      ++downloadedStems;
      songHadDownloads = true;
    }

    if (songHadDownloads)
      ++downloadedSongs;
  }

  if (!SongLibraryStore::saveLibrarySnapshot(library)) {
    result.message = "Couldn't persist local library after cloud pull.";
    return result;
  }

  credentials.remoteSnapshotFileId = snapshotFile.id;
  credentials.remoteSnapshotModifiedMs = remoteModifiedMs;
  credentials.lastSyncMs = juce::Time::currentTimeMillis();
  DriveAuthSession::save(credentials);

  result.ok = true;
  result.songsProcessed = downloadedSongs;
  result.stemsProcessed = downloadedStems;
  result.message = "Downloaded " + juce::String(downloadedSongs) + " songs and " +
                   juce::String(downloadedStems) + " stems from Drive.";
  return result;
}

CloudSyncResult CloudLibrarySyncService::pullSongFromDrive(const juce::String &songIdIn,
                                                           std::function<void(float)> progressCallback) {
  CloudSyncResult result;
  const juce::String songId = songIdIn.trim();
  if (songId.isEmpty()) {
    result.message = "No song id.";
    return result;
  }

  auto credentials = DriveAuthSession::load();
  if (!DriveAuthSession::hasValidAccessToken(credentials)) {
    result.message = "Drive session expired. Reconnect with Google.";
    return result;
  }

  DriveApiClient client;
  client.setAccessToken(credentials.accessToken);

  DriveApiClient::DriveFile rootFolder;
  DriveApiClient::DriveFile libraryFolder;
  DriveApiClient::DriveFile stemsFolder;
  juce::String errorMessage;
  if (!ensureDriveLayout(client, rootFolder, libraryFolder, stemsFolder,
                         errorMessage)) {
    result.message = errorMessage;
    return result;
  }

  DriveApiClient::DriveFile snapshotFile;
  if (!client.findFileByName(kSnapshotFileName, libraryFolder.id, snapshotFile,
                             errorMessage) ||
      snapshotFile.id.isEmpty()) {
    result.message = "No cloud snapshot found in Drive.";
    return result;
  }

  juce::String snapshotJson;
  if (!client.downloadFileToString(snapshotFile.id, snapshotJson, errorMessage)) {
    result.message = errorMessage;
    return result;
  }

  LibraryState remoteLibrary;
  juce::String parseErr;
  if (!SongLibraryStore::parseLibraryFromJson(snapshotJson, remoteLibrary,
                                              &parseErr)) {
    result.message =
        parseErr.isNotEmpty() ? parseErr : "Couldn't parse cloud library.";
    return result;
  }

  const ImportedSongData *remoteSongPtr = nullptr;
  for (const auto &s : remoteLibrary.songs) {
    if (s.id == songId) {
      remoteSongPtr = &s;
      break;
    }
  }
  if (remoteSongPtr == nullptr) {
    result.message = "Song not found in cloud library snapshot.";
    return result;
  }

  ImportedSongData song = *remoteSongPtr;
  const auto audioRoot = SongLibraryStore::getAudioStorageRoot();
  const auto songDir = audioRoot.getChildFile(songId);
  if (songDir.exists())
    (void)songDir.deleteRecursively();
  songDir.createDirectory();

  // Count how many stems will actually be downloaded so we can report progress.
  int totalStemsToDownload = 0;
  for (const auto &stem : song.stems)
    if (stem.include && stem.remoteFileId.trim().isNotEmpty())
      ++totalStemsToDownload;

  int downloadedStems = 0;
  for (auto &stem : song.stems) {
    if (!stem.include || stem.remoteFileId.trim().isEmpty())
      continue;

    if (stem.stemId.isEmpty())
      stem.stemId = juce::Uuid().toString();
    const auto legalStemId = juce::File::createLegalFileName(stem.stemId);
    const auto ext = stemExtensionFor(stem);
    auto destination = songDir.getChildFile(legalStemId + "." + ext);

    if (!client.downloadFileToFile(stem.remoteFileId, destination,
                                   errorMessage)) {
      result.message = errorMessage;
      return result;
    }

    stem.storedPath = destination.getFullPathName();
    stem.sourcePath = destination.getFullPathName();
    stem.fileName = destination.getFileName();
    stem.contentHash = fileSha256(destination);
    stem.updatedMs = juce::Time::currentTimeMillis();
    ++downloadedStems;

    if (progressCallback && totalStemsToDownload > 0)
      progressCallback(static_cast<float>(downloadedStems) /
                       static_cast<float>(totalStemsToDownload));
  }

  juce::String upsertErr;
  if (!SongLibraryStore::upsertLibrarySong(std::move(song), &upsertErr)) {
    result.message = upsertErr.isNotEmpty() ? upsertErr
                                            : "Couldn't save song to local library.";
    return result;
  }

  result.ok = true;
  result.songsProcessed = 1;
  result.stemsProcessed = downloadedStems;
  result.message = "Downloaded song from Drive.";
  return result;
}

bool CloudLibrarySyncService::hasValidDriveSession() {
  return DriveAuthSession::hasValidAccessToken(DriveAuthSession::load());
}

juce::String CloudLibrarySyncService::guessMimeTypeForFile(const juce::File &file) {
  const auto ext = file.getFileExtension().toLowerCase();
  if (ext == ".m4a")
    return "audio/mp4";
  if (ext == ".flac")
    return "audio/flac";
  if (ext == ".wav")
    return "audio/wav";
  if (ext == ".mp3")
    return "audio/mpeg";
  if (ext == ".aac")
    return "audio/aac";
  return "application/octet-stream";
}

juce::String CloudLibrarySyncService::stemExtensionFor(const ImportedStemData &stem) {
  juce::File fromName(stem.fileName);
  if (fromName.getFileExtension().isNotEmpty())
    return fromName.getFileExtension().substring(1);
  juce::File fromPath(stem.storedPath);
  if (fromPath.getFileExtension().isNotEmpty())
    return fromPath.getFileExtension().substring(1);
#if JUCE_WINDOWS
  return "flac";
#else
  return "m4a";
#endif
}

juce::String CloudLibrarySyncService::fileSha256(const juce::File &file) {
  juce::MemoryBlock bytes;
  if (!file.loadFileAsData(bytes))
    return {};
  const auto *data = static_cast<const unsigned char *>(bytes.getData());
  uint64_t hash = 1469598103934665603ULL;
  for (size_t i = 0; i < bytes.getSize(); ++i) {
    hash ^= static_cast<uint64_t>(data[i]);
    hash *= 1099511628211ULL;
  }
  return juce::String::toHexString(static_cast<juce::int64>(hash));
}

juce::int64 CloudLibrarySyncService::parseDriveTimeMs(const juce::String &iso8601) {
  if (iso8601.trim().isEmpty())
    return 0;
  return juce::Time::fromISO8601(iso8601).toMilliseconds();
}
