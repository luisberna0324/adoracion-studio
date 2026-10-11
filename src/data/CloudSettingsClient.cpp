#include "CloudSettingsClient.h"

#include "AuthSessionApi.h"
#include "SessionAuthLogger.h"

namespace {
constexpr int kTimeoutMs = 20000;

void deliverProgress(const CloudSettingsClient::ProgressCallback &callback,
                     const double fraction, juce::String phase) {
  if (!callback)
    return;
  CloudTransferProgress progress;
  progress.phase = std::move(phase);
  progress.totalObjects = 1;
  progress.completedObjects = fraction >= 1.0 ? 1 : 0;
  progress.fraction = juce::jlimit(0.0, 1.0, fraction);
  juce::MessageManager::callAsync(
      [done = callback, update = std::move(progress)]() mutable {
        done(std::move(update));
      });
}

CloudTransferError parseError(const juce::String &body, int status) {
  const auto parsed = juce::JSON::parse(body);
  const auto *object = parsed.getDynamicObject();
  const auto code = object != nullptr
                        ? object->getProperty("error").toString().trim()
                        : juce::String("INVALID_RESPONSE");
  return {status, code, code};
}

bool post(const CloudTransferCredentials &credentials, const juce::var &payload,
          juce::var &response, CloudTransferError &error,
          const CloudSettingsClient::ProgressCallback &progress) {
  deliverProgress(progress, 0.35, "authenticating");
  const auto token = AuthSessionApi::refreshFirebaseIdToken(
      credentials.firebaseApiKey, credentials.refreshToken, kTimeoutMs);
  if (!token.ok) {
    error = {0, "AUTH_REFRESH_FAILED", token.errorMessage};
    return false;
  }
  deliverProgress(progress, 0.55, "uploading-settings");
  const auto endpoint = credentials.apiBaseUrl.trim().trimCharactersAtEnd("/") +
                        "/api/song-sync/settings";
  auto url = juce::URL(endpoint).withPOSTData(juce::JSON::toString(payload));
  int status = 0;
  auto stream = url.createInputStream(
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withExtraHeaders("Authorization: Bearer " + token.idToken +
                            "\r\nContent-Type: application/json\r\nX-PlayWorship-Cloud-Consent: cloud-terms-v1\r\n")
          .withConnectionTimeoutMs(kTimeoutMs)
          .withHttpRequestCmd("POST")
          .withStatusCode(&status));
  if (stream == nullptr) {
    error = {0, "NETWORK", "Cloud settings unavailable"};
    return false;
  }
  deliverProgress(progress, 0.85, "processing-response");
  const auto body = stream->readEntireStreamAsString();
  if (status < 200 || status >= 300) {
    error = parseError(body, status);
    return false;
  }
  response = juce::JSON::parse(body);
  if (response.getDynamicObject() == nullptr) {
    error = {status, "INVALID_RESPONSE", "Invalid cloud settings response"};
    return false;
  }
  deliverProgress(progress, 0.95, "finalizing");
  return true;
}

bool parseVersion(const juce::var &value, CloudSettingsVersion &version) {
  const auto *object = value.getDynamicObject();
  if (object == nullptr)
    return false;
  version.versionId = object->getProperty("versionId").toString();
  version.songId = object->getProperty("songId").toString();
  version.projectId = object->getProperty("projectId").toString();
  version.revision = static_cast<int>(object->getProperty("revision"));
  version.snapshotHash = object->getProperty("snapshotHash").toString();
  version.source = object->getProperty("source").toString();
  version.createdAtMs = juce::Time::fromISO8601(
                            object->getProperty("createdAt").toString())
                            .toMilliseconds();
  const auto *snapshot = object->getProperty("snapshot").getDynamicObject();
  if (snapshot == nullptr ||
      !ProjectStore::deserializeSettings(snapshot->getProperty("settings"),
                                         version.settings))
    return false;
  if (const auto *summary = object->getProperty("summary").getDynamicObject()) {
    version.summary.mixChanges = static_cast<int>(summary->getProperty("mixChanges"));
    version.summary.muteSoloChanges = static_cast<int>(summary->getProperty("muteSoloChanges"));
    version.summary.routingChanges = static_cast<int>(summary->getProperty("routingChanges"));
    version.summary.playbackChanged = static_cast<bool>(summary->getProperty("playbackChanged"));
    version.summary.guideClickChanged = static_cast<bool>(summary->getProperty("guideClickChanged"));
    version.summary.arrangementChanged = static_cast<bool>(summary->getProperty("arrangementChanged"));
    if (const auto *details = summary->getProperty("details").getArray())
      for (const auto &detail : *details)
        version.summary.details.add(detail.toString());
  }
  return version.revision > 0 && version.songId.isNotEmpty() &&
         version.projectId.isNotEmpty();
}

void deliverPublish(CloudSettingsClient::PublishCallback callback, bool created,
                    CloudSettingsVersion version, CloudTransferError error) {
  if (!callback)
    return;
  juce::MessageManager::callAsync(
      [done = std::move(callback), created, resultVersion = std::move(version),
       resultError = std::move(error)]() mutable {
        done(created, std::move(resultVersion), std::move(resultError));
      });
}

void deliverList(CloudSettingsClient::ListCallback callback,
                 std::vector<CloudSettingsVersion> versions,
                 CloudTransferError error) {
  if (!callback)
    return;
  juce::MessageManager::callAsync(
      [done = std::move(callback), resultVersions = std::move(versions),
       resultError = std::move(error)]() mutable {
        done(std::move(resultVersions), std::move(resultError));
      });
}
} // namespace

CloudSettingsClient::CloudSettingsClient() = default;
CloudSettingsClient::~CloudSettingsClient() { cancelAll(); }
void CloudSettingsClient::cancelAll() { pool.removeAllJobs(true, 3000); }

void CloudSettingsClient::publish(CloudTransferCredentials credentials,
                                  juce::String remoteSongId,
                                  juce::String projectId,
                                  ImportedSongData song,
                                  ProjectSongSettings settings,
                                  ProgressCallback progress,
                                  PublishCallback callback) {
  struct Job final : juce::ThreadPoolJob {
    CloudTransferCredentials credentials;
    juce::String remoteSongId, projectId;
    ImportedSongData song;
    ProjectSongSettings settings;
    ProgressCallback progress;
    PublishCallback callback;
    Job(CloudTransferCredentials c, juce::String remoteId, juce::String project,
        ImportedSongData songData, ProjectSongSettings snapshot,
        ProgressCallback onProgress, PublishCallback done)
        : ThreadPoolJob("CloudSettingsPublish"), credentials(std::move(c)),
          remoteSongId(std::move(remoteId)), projectId(std::move(project)),
          song(std::move(songData)), settings(std::move(snapshot)),
          progress(std::move(onProgress)),
          callback(std::move(done)) {}
    JobStatus runJob() override {
      CloudTransferError consentError;
      if (!ensureCloudTermsForJob(credentials, *this, consentError)) {
        deliverPublish(std::move(callback), false, {}, consentError); return jobHasFinished;
      }
      deliverProgress(progress, 0.08, "preparing-settings");
      auto *snapshot = new juce::DynamicObject();
      snapshot->setProperty("schemaVersion", 1);
      snapshot->setProperty("settings", ProjectStore::serializeSettings(settings));
      auto *stemLabels = new juce::DynamicObject();
      for (const auto &stem : song.stems)
        if (stem.stemId.isNotEmpty())
          stemLabels->setProperty(stem.stemId, stem.displayName);
      snapshot->setProperty("stemLabels", juce::var(stemLabels));
      auto *request = new juce::DynamicObject();
      request->setProperty("operation", "publish");
      request->setProperty("songId", remoteSongId);
      request->setProperty("projectId", projectId);
      request->setProperty("source", "manual");
      request->setProperty("snapshot", juce::var(snapshot));
      deliverProgress(progress, 0.25, "preparing-settings");
      juce::var response;
      CloudTransferError error;
      if (!post(credentials, juce::var(request), response, error, progress)) {
        deliverPublish(std::move(callback), false, CloudSettingsVersion{}, std::move(error));
        return jobHasFinished;
      }
      const auto *root = response.getDynamicObject();
      CloudSettingsVersion version;
      if (root == nullptr || !parseVersion(root->getProperty("version"), version)) {
        deliverPublish(std::move(callback), false, CloudSettingsVersion{},
                       CloudTransferError{200, "INVALID_RESPONSE", "Invalid settings version"});
        return jobHasFinished;
      }
      deliverPublish(std::move(callback), static_cast<bool>(root->getProperty("created")),
                     std::move(version), CloudTransferError{});
      return jobHasFinished;
    }
  };
  pool.addJob(new Job(std::move(credentials), std::move(remoteSongId),
                      std::move(projectId), std::move(song), std::move(settings),
                      std::move(progress), std::move(callback)), true);
}

void CloudSettingsClient::list(CloudTransferCredentials credentials,
                               juce::String remoteSongId,
                               ListCallback callback) {
  struct Job final : juce::ThreadPoolJob {
    CloudTransferCredentials credentials;
    juce::String remoteSongId;
    ListCallback callback;
    Job(CloudTransferCredentials c, juce::String song, ListCallback done)
        : ThreadPoolJob("CloudSettingsList"), credentials(std::move(c)),
          remoteSongId(std::move(song)), callback(std::move(done)) {}
    JobStatus runJob() override {
      auto *request = new juce::DynamicObject();
      request->setProperty("operation", "list");
      request->setProperty("songId", remoteSongId);
      request->setProperty("limit", 50);
      juce::var response;
      CloudTransferError error;
      if (!post(credentials, juce::var(request), response, error, {})) {
        deliverList(std::move(callback), std::vector<CloudSettingsVersion>{},
                    std::move(error));
        return jobHasFinished;
      }
      std::vector<CloudSettingsVersion> versions;
      const auto *root = response.getDynamicObject();
      const auto *array = root != nullptr ? root->getProperty("versions").getArray() : nullptr;
      if (array == nullptr) {
        deliverList(std::move(callback), std::move(versions),
                    CloudTransferError{200, "INVALID_RESPONSE", "Invalid settings history"});
        return jobHasFinished;
      }
      for (const auto &value : *array) {
        CloudSettingsVersion version;
        if (parseVersion(value, version))
          versions.push_back(std::move(version));
      }
      deliverList(std::move(callback), std::move(versions), CloudTransferError{});
      return jobHasFinished;
    }
  };
  pool.addJob(new Job(std::move(credentials), std::move(remoteSongId),
                      std::move(callback)), true);
}
