#include "SongSyncService.h"

#include "ProjectStore.h"
#include "SongLibraryStore.h"

namespace {
void deliver(SongSyncService::CompletionCallback callback, ImportedSongData song,
             CloudTransferError error) {
  if (!callback) return;
  juce::MessageManager::callAsync([done = std::move(callback), result = std::move(song), failure = std::move(error)]() mutable {
    done(std::move(result), std::move(failure));
  });
}

void deliverProgress(SongSyncService::ProgressCallback callback,
                     CloudTransferProgress progress) {
  if (!callback) return;
  juce::MessageManager::callAsync(
      [callback = std::move(callback), progress = std::move(progress)]() mutable {
        callback(std::move(progress));
      });
}

CloudTransferError localError(const juce::String& message) {
  return {0, "LOCAL_PROJECT_ERROR", message};
}

}  // namespace

SongSyncService::SongSyncService() = default;
SongSyncService::~SongSyncService() { cancelAll(); }
void SongSyncService::cancelAll() { archivePool.removeAllJobs(true, 3000); transfer.cancelAll(); }

void SongSyncService::upload(CloudTransferCredentials credentials, juce::File projectFile,
                             ProgressCallback progress, CompletionCallback callback) {
  struct Job final : juce::ThreadPoolJob {
    SongSyncService& owner; CloudTransferCredentials credentials; juce::File projectFile;
    ProgressCallback progress; CompletionCallback callback;
    Job(SongSyncService& service, CloudTransferCredentials c, juce::File file,
        ProgressCallback p, CompletionCallback done)
        : ThreadPoolJob("CloudProjectExport"), owner(service), credentials(std::move(c)),
          projectFile(std::move(file)), progress(std::move(p)), callback(std::move(done)) {}
    JobStatus runJob() override {
      ProjectDocument document; juce::String error;
      deliverProgress(progress, {"opening-project", {}, 0, 100, 0.02});
      if (!ProjectStore::open(projectFile, document, &error)) {
        deliver(std::move(callback), {}, localError(error)); return jobHasFinished;
      }
      CloudProjectLink link;
      CloudSyncIndexStore::findByProjectId(document.song.projectId, link);
      CloudUploadRequest request;
      if (!CloudProjectArchive::buildUpload(projectFile, link.remoteSongId, link.remoteRevision,
                                            request, &error,
                                            [progress = progress](double fraction,
                                                                  const juce::String& phase) {
                                              deliverProgress(progress, {phase, {}, 0, 100, fraction});
                                            })) {
        deliver(std::move(callback), {}, localError(error)); return jobHasFinished;
      }
      const auto projectId = document.song.projectId;
      const auto uploadedSong = document.song;
      const auto fingerprint = CloudSyncJson::fingerprint(request.descriptor);
      if (link.remoteSongId.isNotEmpty() && link.descriptorFingerprint.isNotEmpty() &&
          link.descriptorFingerprint == fingerprint) {
        link.lastSyncedAtMs = juce::Time::currentTimeMillis();
        CloudSyncIndexStore::upsert(std::move(link));
        deliver(std::move(callback), {}, {0, "NO_CHANGES", "Sin nada que sincronizar."});
        return jobHasFinished;
      }
      auto completeUpload = [projectId, uploadedSong, fingerprint,
                             downloadedAtMs = link.downloadedAtMs,
                             done = std::move(callback)](CloudUploadResult result, CloudTransferError failure) mutable {
            if (failure.code.isNotEmpty()) { deliver(std::move(done), {}, std::move(failure)); return; }
            const auto now = juce::Time::currentTimeMillis();
            if (!CloudSyncIndexStore::upsert(
                    {projectId, result.songId, result.revision, downloadedAtMs, now, fingerprint})) {
              deliver(std::move(done), {}, localError("Couldn't save cloud sync link.")); return;
            }
            deliver(std::move(done), uploadedSong, {});
          };
      auto uploadProgress = [progress = std::move(progress)](CloudTransferProgress update) {
        update.fraction = 0.50 + 0.50 * juce::jlimit(0.0, 1.0, update.fraction);
        deliverProgress(progress, std::move(update));
      };
      owner.transfer.upload(std::move(credentials), std::move(request), std::move(uploadProgress),
                            std::move(completeUpload));
      return jobHasFinished;
    }
  };
  archivePool.addJob(new Job(*this, std::move(credentials), std::move(projectFile),
                             std::move(progress), std::move(callback)), true);
}

void SongSyncService::download(CloudTransferCredentials credentials, juce::String remoteSongId,
                               juce::File cacheDirectory, ProgressCallback progress,
                               CompletionCallback callback) {
  // Keep the request ID independent from the callback capture.  Moving
  // remoteSongId in the lambda capture in the same expression as the call to
  // transfer.download() leaves the argument evaluation order unspecified and
  // can send an empty songId to the API.
  const auto requestSongId = remoteSongId;
  const auto legacyDownloadDirectory = cacheDirectory.getParentDirectory()
                                           .getChildFile("projects")
                                           .getChildFile(".cloud-downloads");
  transfer.download(std::move(credentials), requestSongId, cacheDirectory,
      std::move(progress), [this, cloudSongId = std::move(remoteSongId),
                            legacyDownloadDirectory,
                            done = std::move(callback)](CloudDownloadResult result, CloudTransferError failure) mutable {
        if (failure.code.isNotEmpty()) { deliver(std::move(done), {}, std::move(failure)); return; }
        struct RebuildJob final : juce::ThreadPoolJob {
          CloudDownloadResult downloaded; juce::String remoteSongId;
          juce::File legacyDownloadDirectory; CompletionCallback callback;
          RebuildJob(CloudDownloadResult value, juce::String id,
                     juce::File legacyDirectory, CompletionCallback done)
              : ThreadPoolJob("CloudProjectImport"), downloaded(std::move(value)), remoteSongId(std::move(id)),
                legacyDownloadDirectory(std::move(legacyDirectory)),
                callback(std::move(done)) {}
          JobStatus runJob() override {
            ImportedSongData song; std::vector<float> waveformBins; juce::String error;
            if (!CloudProjectArchive::materializeCachedSong(
                    downloaded, song, waveformBins, &error)) {
              deliver(std::move(callback), {}, localError(error)); return jobHasFinished;
            }
            bool hasIncludedStem = false;
            bool allIncludedStemsUsable = true;
            for (const auto& stem : song.stems) {
              if (!stem.include) continue;
              hasIncludedStem = true;
              const juce::File file(stem.storedPath);
              if (!file.existsAsFile() || file.getSize() <= 0) {
                allIncludedStemsUsable = false;
                break;
              }
            }
            if (!hasIncludedStem || !allIncludedStemsUsable) {
              deliver(std::move(callback), {}, localError(
                  "Downloaded cloud song has no usable local stems."));
              return jobHasFinished;
            }
            if (!SongLibraryStore::registerCloudCachedSong(song)) {
              deliver(std::move(callback), {}, localError("Couldn't register downloaded cloud song.")); return jobHasFinished;
            }
            if (!waveformBins.empty())
              SongLibraryStore::saveWaveformCache(song.id, waveformBins);
            const auto now = juce::Time::currentTimeMillis();
            if (!CloudSyncIndexStore::upsert(
                    {song.projectId, remoteSongId, downloaded.descriptor.revision, now, now,
                     CloudSyncJson::fingerprint(downloaded.descriptor)})) {
              deliver(std::move(callback), {}, localError("Couldn't save cloud sync link.")); return jobHasFinished;
            }
            if (legacyDownloadDirectory.isDirectory())
              legacyDownloadDirectory.deleteRecursively();
            deliver(std::move(callback), std::move(song), {}); return jobHasFinished;
          }
        };
        archivePool.addJob(new RebuildJob(std::move(result), std::move(cloudSongId),
                                          legacyDownloadDirectory,
                                          std::move(done)), true);
      });
}


void SongSyncService::deleteRemoteSong(CloudTransferCredentials credentials,
                                       juce::String remoteSongId,
                                       DeleteCallback callback) {
  transfer.deleteSong(std::move(credentials), std::move(remoteSongId),
                      std::move(callback));
}
