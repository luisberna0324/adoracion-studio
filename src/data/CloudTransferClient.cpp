#include "CloudTransferClient.h"

#include "AuthSessionApi.h"
#include "CloudSignedUpload.h"
#include "SessionAuthLogger.h"

#include <map>

namespace {
constexpr int kTimeoutMs = 30000;
constexpr int kUploadTimeoutMs = 30 * 60 * 1000;
#if !JUCE_MAC && !JUCE_IOS
constexpr juce::int64 kMaxInMemoryUploadBytes = 128 * 1024 * 1024;
#endif

using JsonObject = juce::DynamicObject;

juce::String normalizedBase(const juce::String& base) {
  return base.trim().trimCharactersAtEnd("/");
}

CloudTransferError makeError(const juce::String& code, const juce::String& message,
                             int status = 0) {
  return {status, code, message.isNotEmpty() ? message : code};
}

CloudTransferError responseError(const juce::String& body, int status) {
  const auto parsed = juce::JSON::parse(body);
  if (const auto* object = parsed.getDynamicObject()) {
    const auto code = object->getProperty("error").toString().trim();
    return makeError(code.isNotEmpty() ? code : "HTTP_ERROR", code, status);
  }
  return makeError("HTTP_ERROR", "Cloud request failed", status);
}

void deliverUpload(CloudTransferClient::UploadCallback callback,
                   CloudUploadResult result, CloudTransferError error) {
  if (!callback) return;
  juce::MessageManager::callAsync(
      [done = std::move(callback), uploadResult = std::move(result), transferError = std::move(error)]() mutable {
        done(std::move(uploadResult), std::move(transferError));
      });
}

void deliverDownload(CloudTransferClient::DownloadCallback callback,
                     CloudDownloadResult result, CloudTransferError error) {
  if (!callback) return;
  juce::MessageManager::callAsync(
      [done = std::move(callback), downloadResult = std::move(result), transferError = std::move(error)]() mutable {
        done(std::move(downloadResult), std::move(transferError));
      });
}

void deliverDelete(CloudTransferClient::DeleteCallback callback,
                   CloudTransferError error) {
  if (!callback) return;
  juce::MessageManager::callAsync(
      [done = std::move(callback), transferError = std::move(error)]() mutable {
        done(std::move(transferError));
      });
}

void deliverProgress(CloudTransferClient::ProgressCallback callback,
                     CloudTransferProgress progress) {
  if (!callback) return;
  juce::MessageManager::callAsync(
      [done = std::move(callback), update = std::move(progress)]() mutable {
        done(std::move(update));
      });
}

bool postJson(const juce::String& urlText, const juce::String& idToken,
              const juce::String& body, juce::String& response,
              CloudTransferError& error) {
  int status = 0;
  auto url = juce::URL(urlText).withPOSTData(body);
  auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
      .withExtraHeaders("Authorization: Bearer " + idToken + "\r\nContent-Type: application/json\r\n")
      .withConnectionTimeoutMs(kTimeoutMs).withHttpRequestCmd("POST").withStatusCode(&status);
  auto stream = url.createInputStream(options);
  if (!stream) {
    error = makeError("NETWORK", "Cloud API unavailable");
    return false;
  }
  response = stream->readEntireStreamAsString();
  if (status < 200 || status >= 300) {
    error = responseError(response, status);
    return false;
  }
  return true;
}

bool putFile(const juce::String& urlText, const CloudUploadObject& object,
             CloudTransferError& error) {
  if (!object.file.existsAsFile() || object.file.getSize() != object.reference.sizeBytes) {
    error = makeError("LOCAL_OBJECT_INVALID", object.file.getFileName());
    return false;
  }
#if !JUCE_MAC && !JUCE_IOS
  if (object.file.getSize() > kMaxInMemoryUploadBytes) {
    error = makeError("UPLOAD_REQUIRES_STREAMING",
                      "This platform does not provide a streaming upload backend");
    return false;
  }
#endif
  if (juce::SHA256(object.file).toHexString() != object.reference.sha256.toLowerCase()) {
    error = makeError("LOCAL_OBJECT_HASH_MISMATCH", object.file.getFileName());
    return false;
  }
  const auto upload = uploadFileToSignedUrl(urlText, object.file,
                                            object.reference.contentType, kUploadTimeoutMs);
  if (!upload.ok) {
    error = upload.httpCode > 0 ? responseError(upload.responseBody, upload.httpCode)
                                : makeError("NETWORK", upload.errorMessage);
    return false;
  }
  return true;
}

bool downloadFileOnce(const juce::String& urlText, const CloudObjectRef& reference,
                  const juce::File& destination, CloudTransferError& error) {
  if (destination.existsAsFile() && destination.getSize() == reference.sizeBytes &&
      juce::SHA256(destination).toHexString() == reference.sha256.toLowerCase())
    return true;

  int status = 0;
  auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
      .withConnectionTimeoutMs(kTimeoutMs).withHttpRequestCmd("GET").withStatusCode(&status);
  auto input = juce::URL(urlText).createInputStream(options);
  if (!input) {
    error = makeError("NETWORK", "Signed download unavailable");
    return false;
  }
  if (status < 200 || status >= 300) {
    error = responseError(input->readEntireStreamAsString(), status);
    return false;
  }
  const auto temporary = destination.getSiblingFile(destination.getFileName() + ".part");
  temporary.deleteFile();
  destination.getParentDirectory().createDirectory();
  auto output = temporary.createOutputStream();
  if (!output) {
    error = makeError("LOCAL_WRITE_FAILED", destination.getFileName());
    return false;
  }
  if (output->writeFromInputStream(*input, -1) < 0 || !output->getStatus().wasOk()) {
    output.reset();
    temporary.deleteFile();
    error = makeError("LOCAL_WRITE_FAILED", destination.getFileName());
    return false;
  }
  output.reset();
  if (temporary.getSize() != reference.sizeBytes ||
      juce::SHA256(temporary).toHexString() != reference.sha256.toLowerCase()) {
    temporary.deleteFile();
    error = makeError("DOWNLOADED_OBJECT_HASH_MISMATCH", destination.getFileName());
    return false;
  }
  if (destination.existsAsFile() && !destination.deleteFile()) {
    temporary.deleteFile();
    error = makeError("LOCAL_WRITE_FAILED", destination.getFileName());
    return false;
  }
  if (!temporary.moveFileTo(destination)) {
    temporary.deleteFile();
    error = makeError("LOCAL_WRITE_FAILED", destination.getFileName());
    return false;
  }
  return true;
}

bool downloadFile(const juce::String& urlText, const CloudObjectRef& reference,
                  const juce::File& destination, CloudTransferError& error) {
  // Mobile networks and signed object URLs can occasionally yield a truncated
  // response. Retry from scratch before surfacing a hash mismatch to the UI.
  constexpr int maxAttempts = 3;
  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (downloadFileOnce(urlText, reference, destination, error))
      return true;
    if (error.code != "DOWNLOADED_OBJECT_HASH_MISMATCH" &&
        error.code != "NETWORK" && error.code != "LOCAL_WRITE_FAILED")
      break;
    if (attempt < maxAttempts)
      juce::Thread::sleep(250 * attempt);
  }
  return false;
}

bool refreshToken(const CloudTransferCredentials& credentials, juce::String& idToken,
                  CloudTransferError& error) {
  const auto refreshed = AuthSessionApi::refreshFirebaseIdToken(
      credentials.firebaseApiKey, credentials.refreshToken, kTimeoutMs);
  if (!refreshed.ok) {
    error = makeError("AUTH_REFRESH_FAILED", refreshed.errorMessage);
    return false;
  }
  idToken = refreshed.idToken;
  return true;
}
}  // namespace

CloudTransferClient::CloudTransferClient() = default;
CloudTransferClient::~CloudTransferClient() { cancelAll(); }
void CloudTransferClient::cancelAll() { pool.removeAllJobs(true, 3000); }

void CloudTransferClient::upload(CloudTransferCredentials credentials,
                                 CloudUploadRequest request,
                                 ProgressCallback progress, UploadCallback callback) {
  struct Job final : juce::ThreadPoolJob {
    CloudTransferCredentials credentials;
    CloudUploadRequest request;
    ProgressCallback progress;
    UploadCallback callback;
    Job(CloudTransferCredentials c, CloudUploadRequest r, ProgressCallback p, UploadCallback done)
        : ThreadPoolJob("CloudUpload"), credentials(std::move(c)), request(std::move(r)),
          progress(std::move(p)), callback(std::move(done)) {}
    JobStatus runJob() override {
      CloudTransferError error;
      juce::String idToken;
      if (!refreshToken(credentials, idToken, error)) {
        SessionAuthLogger::write("[CloudUpload] token refresh failed code=" +
                                 error.code + " message=" + error.message);
        deliverUpload(std::move(callback), {}, error); return jobHasFinished;
      }
      if (request.descriptor.songId.isEmpty())
        request.descriptor.songId = juce::Uuid().toString();
      if (request.descriptor.projectId.isEmpty() || request.objects.empty()) {
        deliverUpload(std::move(callback), {}, makeError("UPLOAD_INVALID", "Descriptor or objects missing"));
        return jobHasFinished;
      }
      juce::Array<juce::var> objects;
      std::map<juce::String, const CloudUploadObject*> objectByHash;
      for (const auto& object : request.objects) {
        const auto hash = object.reference.sha256.trim().toLowerCase();
        if (hash.isEmpty() || objectByHash.find(hash) != objectByHash.end()) {
          deliverUpload(std::move(callback), {}, makeError("UPLOAD_INVALID", "Duplicate or empty object hash"));
          return jobHasFinished;
        }
        objectByHash.emplace(hash, &object);
        auto* value = new JsonObject();
        value->setProperty("sha256", hash);
        value->setProperty("sizeBytes", object.reference.sizeBytes);
        value->setProperty("contentType", object.reference.contentType);
        objects.add(juce::var(value));
      }
      auto* prepare = new JsonObject();
      prepare->setProperty("songId", request.descriptor.songId);
      prepare->setProperty("baseRevision", request.baseRevision);
      prepare->setProperty("objects", juce::var(objects));
      juce::String response;
      SessionAuthLogger::write("[CloudUpload] prepare request api=" +
                               normalizedBase(credentials.apiBaseUrl) +
                               " objects=" + juce::String(request.objects.size()));
      if (!postJson(normalizedBase(credentials.apiBaseUrl) + "/api/song-sync/prepare-upload", idToken,
                    juce::JSON::toString(juce::var(prepare)), response, error)) {
        SessionAuthLogger::write("[CloudUpload] prepare failed status=" +
                                 juce::String(error.httpCode) + " code=" +
                                 error.code + " message=" + error.message);
        deliverUpload(std::move(callback), {}, error); return jobHasFinished;
      }
      // Keep the parsed var alive while its DynamicObject and Array views are used.
      const auto preparedJson = juce::JSON::parse(response);
      const auto* prepared = preparedJson.getDynamicObject();
      if (!prepared) { deliverUpload(std::move(callback), {}, makeError("INVALID_RESPONSE", "Prepare response is not JSON")); return jobHasFinished; }
      const auto songId = prepared->getProperty("songId").toString().trim();
      const auto* descriptorUpload = prepared->getProperty("descriptor").getDynamicObject();
      const auto* uploads = prepared->getProperty("uploads").getArray();
      if (songId.isEmpty() || !descriptorUpload || !uploads) {
        deliverUpload(std::move(callback), {}, makeError("INVALID_RESPONSE", "Prepare response is incomplete")); return jobHasFinished;
      }
      request.descriptor.songId = songId;
      request.descriptor.revision = static_cast<int>(descriptorUpload->getProperty("revision"));
      if (request.descriptor.revision <= 0) {
        deliverUpload(std::move(callback), {}, makeError("INVALID_RESPONSE", "Invalid prepared revision")); return jobHasFinished;
      }
      const int total = uploads->size() + 1;
      int completed = 0;
      for (const auto& uploadValue : *uploads) {
        const auto* upload = uploadValue.getDynamicObject();
        if (!upload) { deliverUpload(std::move(callback), {}, makeError("INVALID_RESPONSE", "Invalid upload entry")); return jobHasFinished; }
        const auto hash = upload->getProperty("sha256").toString().trim().toLowerCase();
        const auto it = objectByHash.find(hash);
        if (it == objectByHash.end()) { deliverUpload(std::move(callback), {}, makeError("INVALID_RESPONSE", "Unexpected upload object")); return jobHasFinished; }
        const auto signedUrl = upload->getProperty("uploadUrl").toString().trim();
        if (signedUrl.isNotEmpty() && !putFile(signedUrl, *it->second, error)) {
          SessionAuthLogger::write("[CloudUpload] object upload failed file=" +
                                   it->second->file.getFileName() + " status=" +
                                   juce::String(error.httpCode) + " code=" +
                                   error.code + " message=" + error.message);
          deliverUpload(std::move(callback), {}, error); return jobHasFinished;
        }
        ++completed;
        deliverProgress(progress, {"uploading", hash, completed, total, static_cast<double>(completed) / total});
      }
      const auto descriptorUrl = descriptorUpload->getProperty("uploadUrl").toString().trim();
      const auto descriptorJson = CloudSyncJson::toJsonString(request.descriptor);
      juce::MemoryBlock descriptorBody(descriptorJson.toRawUTF8(), static_cast<size_t>(descriptorJson.getNumBytesAsUTF8()));
      int descriptorStatus = 0;
      auto descriptorRequest = juce::URL(descriptorUrl).withPOSTData(descriptorBody);
      auto descriptorOptions = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withExtraHeaders("Content-Type: application/json\r\n").withConnectionTimeoutMs(kTimeoutMs)
          .withHttpRequestCmd("PUT").withStatusCode(&descriptorStatus);
      auto descriptorStream = descriptorRequest.createInputStream(descriptorOptions);
      if (!descriptorStream || descriptorStatus < 200 || descriptorStatus >= 300) {
        const auto body = descriptorStream ? descriptorStream->readEntireStreamAsString() : juce::String();
        SessionAuthLogger::write("[CloudUpload] descriptor upload failed status=" +
                                 juce::String(descriptorStatus));
        deliverUpload(std::move(callback), {}, descriptorStream ? responseError(body, descriptorStatus) : makeError("NETWORK", "Descriptor upload unavailable"));
        return jobHasFinished;
      }
      ++completed;
      deliverProgress(progress, {"uploading-descriptor", {}, completed, total, 1.0});
      auto* commit = new JsonObject();
      juce::Array<juce::var> hashes;
      for (const auto& object : request.objects) hashes.add(object.reference.sha256.toLowerCase());
      commit->setProperty("songId", songId); commit->setProperty("baseRevision", request.baseRevision);
      commit->setProperty("title", request.descriptor.title); commit->setProperty("artist", request.descriptor.artist);
      commit->setProperty("bpm", request.descriptor.bpm); commit->setProperty("key", request.descriptor.key);
      commit->setProperty("durationSeconds", request.descriptor.durationSeconds); commit->setProperty("storageBytes", 0);
      commit->setProperty("objectHashes", juce::var(hashes));
      if (!postJson(normalizedBase(credentials.apiBaseUrl) + "/api/song-sync/commit", idToken,
                    juce::JSON::toString(juce::var(commit)), response, error)) {
        SessionAuthLogger::write("[CloudUpload] commit failed status=" +
                                 juce::String(error.httpCode) + " code=" +
                                 error.code + " message=" + error.message);
        deliverUpload(std::move(callback), {}, error); return jobHasFinished;
      }
      const auto committedJson = juce::JSON::parse(response);
      const auto* committed = committedJson.getDynamicObject();
      const auto* song = committed ? committed->getProperty("song").getDynamicObject() : nullptr;
      if (!song) { deliverUpload(std::move(callback), {}, makeError("INVALID_RESPONSE", "Commit response is incomplete")); return jobHasFinished; }
      deliverUpload(std::move(callback), {songId, static_cast<int>(song->getProperty("revision"))}, {});
      return jobHasFinished;
    }
  };
  pool.addJob(new Job(std::move(credentials), std::move(request), std::move(progress), std::move(callback)), true);
}

void CloudTransferClient::download(CloudTransferCredentials credentials, juce::String songId,
                                   juce::File destinationDirectory, ProgressCallback progress,
                                   DownloadCallback callback) {
  struct Job final : juce::ThreadPoolJob {
    CloudTransferCredentials credentials; juce::String songId; juce::File destination; ProgressCallback progress; DownloadCallback callback;
    Job(CloudTransferCredentials c, juce::String id, juce::File d, ProgressCallback p, DownloadCallback done)
        : ThreadPoolJob("CloudDownload"), credentials(std::move(c)), songId(std::move(id)), destination(std::move(d)), progress(std::move(p)), callback(std::move(done)) {}
    JobStatus runJob() override {
      CloudTransferError error; juce::String idToken;
      if (!refreshToken(credentials, idToken, error)) {
        SessionAuthLogger::write("[CloudDownload] token refresh failed code=" +
                                 error.code + " message=" + error.message);
        deliverDownload(std::move(callback), {}, error); return jobHasFinished;
      }
      auto* request = new JsonObject(); request->setProperty("songId", songId);
      juce::String response;
      SessionAuthLogger::write("[CloudDownload] request songId=" + songId +
                               " api=" + normalizedBase(credentials.apiBaseUrl));
      if (!postJson(normalizedBase(credentials.apiBaseUrl) + "/api/song-sync/download", idToken,
                    juce::JSON::toString(juce::var(request)), response, error)) {
        SessionAuthLogger::write("[CloudDownload] request failed status=" +
                                 juce::String(error.httpCode) + " code=" +
                                 error.code + " message=" + error.message);
        deliverDownload(std::move(callback), {}, error); return jobHasFinished;
      }
      const auto responseJson = juce::JSON::parse(response);
      const auto* root = responseJson.getDynamicObject();
      CloudProjectDescriptor descriptor;
      if (!root || !CloudSyncJson::fromVar(root->getProperty("descriptor"), descriptor)) {
        deliverDownload(std::move(callback), {}, makeError("INVALID_RESPONSE", "Invalid cloud descriptor")); return jobHasFinished;
      }
      const auto* objects = root->getProperty("objects").getArray();
      if (!objects) { deliverDownload(std::move(callback), {}, makeError("INVALID_RESPONSE", "Missing download objects")); return jobHasFinished; }
      CloudDownloadResult result; result.descriptor = descriptor;
      const int total = objects->size(); int completed = 0;
      for (const auto& value : *objects) {
        const auto* object = value.getDynamicObject(); CloudObjectRef reference;
        if (!object || !CloudSyncJson::fromVar(value, reference)) { deliverDownload(std::move(callback), {}, makeError("INVALID_RESPONSE", "Invalid download object")); return jobHasFinished; }
        const auto url = object->getProperty("url").toString().trim();
        juce::String extension = ".blob";
        if (reference.contentType == "audio/flac") extension = ".flac";
        else if (reference.contentType == "application/json") extension = ".json";
        else if (reference.contentType == "image/png") extension = ".png";
        else if (reference.contentType == "image/jpeg") extension = ".jpg";
        else if (reference.contentType == "image/webp") extension = ".webp";
        else if (reference.contentType == "application/octet-stream") extension = ".bin";
        const auto file = destination.getChildFile("objects").getChildFile(
            reference.sha256 + extension);
        if (url.isEmpty() || !downloadFile(url, reference, file, error)) {
          SessionAuthLogger::write("[CloudDownload] object failed hash=" +
                                   reference.sha256.substring(0, 12) + " status=" +
                                   juce::String(error.httpCode) + " code=" +
                                   error.code + " message=" + error.message);
          deliverDownload(std::move(callback), {}, error); return jobHasFinished;
        }
        result.objects.push_back({reference, file}); ++completed;
        deliverProgress(progress, {"downloading", reference.sha256, completed, total, total > 0 ? static_cast<double>(completed) / total : 1.0});
      }
      SessionAuthLogger::write("[CloudDownload] complete songId=" + songId +
                               " objects=" + juce::String(result.objects.size()));
      deliverDownload(std::move(callback), std::move(result), {}); return jobHasFinished;
    }
  };
  pool.addJob(new Job(std::move(credentials), std::move(songId), std::move(destinationDirectory), std::move(progress), std::move(callback)), true);
}


void CloudTransferClient::deleteSong(CloudTransferCredentials credentials,
                                     juce::String songId,
                                     DeleteCallback callback) {
  struct Job final : juce::ThreadPoolJob {
    CloudTransferCredentials credentials;
    juce::String songId;
    DeleteCallback callback;
    Job(CloudTransferCredentials c, juce::String id, DeleteCallback done)
        : ThreadPoolJob("CloudDelete"), credentials(std::move(c)),
          songId(std::move(id)), callback(std::move(done)) {}
    JobStatus runJob() override {
      CloudTransferError error;
      juce::String idToken;
      if (songId.trim().isEmpty()) {
        deliverDelete(std::move(callback),
                      makeError("DELETE_INVALID", "Missing cloud song id"));
        return jobHasFinished;
      }
      if (!refreshToken(credentials, idToken, error)) {
        SessionAuthLogger::write("[CloudDelete] token refresh failed code=" +
                                 error.code + " message=" + error.message);
        deliverDelete(std::move(callback), std::move(error));
        return jobHasFinished;
      }
      auto* request = new JsonObject();
      request->setProperty("songId", songId.trim());
      juce::String response;
      SessionAuthLogger::write("[CloudDelete] request songId=" + songId.trim());
      if (!postJson(normalizedBase(credentials.apiBaseUrl) +
                        "/api/song-sync/delete",
                    idToken, juce::JSON::toString(juce::var(request)),
                    response, error)) {
        SessionAuthLogger::write("[CloudDelete] failed status=" +
                                 juce::String(error.httpCode) + " code=" +
                                 error.code + " message=" + error.message);
        deliverDelete(std::move(callback), std::move(error));
        return jobHasFinished;
      }
      SessionAuthLogger::write("[CloudDelete] complete songId=" + songId.trim());
      deliverDelete(std::move(callback), {});
      return jobHasFinished;
    }
  };
  pool.addJob(new Job(std::move(credentials), std::move(songId),
                      std::move(callback)), true);
}
