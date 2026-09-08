#include "CloudLibraryClient.h"

#include "AuthSessionApi.h"
#include "SessionAuthLogger.h"
#include "SongLibraryStore.h"

namespace {
constexpr int kTimeoutMs = 20000;

void deliver(CloudLibraryClient::SearchCallback callback, CloudSongPage page,
             CloudRequestError error) {
  if (!callback) return;
  juce::MessageManager::callAsync(
      [callback = std::move(callback), page = std::move(page), error = std::move(error)]() mutable {
        callback(std::move(page), std::move(error));
      });
}

CloudRequestError serverError(const juce::String& body, int status) {
  CloudRequestError error;
  error.httpCode = status;
  const auto parsed = juce::JSON::parse(body);
  if (const auto* object = parsed.getDynamicObject()) {
    error.code = object->getProperty("error").toString();
    error.message = error.code;
  } else {
    error.code = "INVALID_RESPONSE";
    error.message = "Invalid cloud response";
  }
  return error;
}

juce::String artworkExtensionForContentType(const juce::String& contentType) {
  const auto normalized = contentType.toLowerCase().trim();
  if (normalized.contains("png")) return ".png";
  if (normalized.contains("webp")) return ".webp";
  return ".jpg";
}

juce::String cacheCloudArtwork(const juce::String& url, const juce::String& sha256,
                               const juce::String& contentType) {
  if (url.isEmpty() || sha256.isEmpty()) return {};

  auto directory = SongLibraryStore::getRootDirectory().getChildFile("cloud-artwork");
  if (!directory.createDirectory()) return {};
  const auto target = directory.getChildFile(sha256.toLowerCase() +
                                              artworkExtensionForContentType(contentType));
  if (target.existsAsFile() && target.getSize() > 0)
    return target.getFullPathName();

  auto stream = juce::URL(url).createInputStream(
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withConnectionTimeoutMs(kTimeoutMs));
  if (stream == nullptr || stream->getTotalLength() > 16 * 1024 * 1024)
    return {};

  juce::MemoryBlock bytes;
  stream->readIntoMemoryBlock(bytes);
  if (bytes.getSize() == 0 || bytes.getSize() > 16 * 1024 * 1024)
    return {};
  juce::MemoryInputStream imageInput(bytes.getData(), bytes.getSize(), false);
  if (!juce::ImageFileFormat::loadFrom(imageInput).isValid())
    return {};
  return target.replaceWithData(bytes.getData(), bytes.getSize())
             ? target.getFullPathName()
             : juce::String{};
}
} // namespace

CloudLibraryClient::CloudLibraryClient() = default;
CloudLibraryClient::~CloudLibraryClient() { cancelAll(); }

void CloudLibraryClient::cancelAll() { pool.removeAllJobs(true, 3000); }

void CloudLibraryClient::search(
    const juce::String& apiBaseUrl, const juce::String& firebaseApiKey,
    const juce::String& refreshToken, const juce::String& query,
    const juce::String& cursor, int limit, SearchCallback callback) {
  struct Job final : juce::ThreadPoolJob {
    juce::String base, apiKey, refresh, text, cursor;
    int limit;
    SearchCallback callback;
    Job(juce::String b, juce::String k, juce::String r, juce::String q,
        juce::String c, int l, SearchCallback cb)
        : ThreadPoolJob("CloudLibrarySearch"), base(std::move(b)), apiKey(std::move(k)),
          refresh(std::move(r)), text(std::move(q)), cursor(std::move(c)),
          limit(l), callback(std::move(cb)) {}

    JobStatus runJob() override {
      const auto token = AuthSessionApi::refreshFirebaseIdToken(apiKey, refresh, kTimeoutMs);
      if (!token.ok) {
        SessionAuthLogger::write("Cloud library token refresh failed");
        deliver(std::move(callback), {}, {0, "AUTH_REFRESH_FAILED", token.errorMessage});
        return jobHasFinished;
      }
      auto normalizedBase = base.trim().trimCharactersAtEnd("/");
      auto* request = new juce::DynamicObject();
      request->setProperty("query", text.trim());
      request->setProperty("cursor", cursor);
      request->setProperty("limit", juce::jlimit(1, 50, limit));
      auto url = juce::URL(normalizedBase + "/api/song-sync/songs")
                     .withPOSTData(juce::JSON::toString(juce::var(request)));
      int status = 0;
      auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withExtraHeaders("Authorization: Bearer " + token.idToken +
                            "\r\nContent-Type: application/json\r\n")
          .withConnectionTimeoutMs(kTimeoutMs).withHttpRequestCmd("POST").withStatusCode(&status);
      auto stream = url.createInputStream(options);
      if (!stream) {
        SessionAuthLogger::write("Cloud library POST unavailable");
        deliver(std::move(callback), {}, {0, "NETWORK", "Cloud library unavailable"});
        return jobHasFinished;
      }
      const auto body = stream->readEntireStreamAsString();
      if (status < 200 || status >= 300) {
        const auto requestError = serverError(body, status);
        SessionAuthLogger::write("Cloud library POST status=" +
                                 juce::String(status) + " error=" +
                                 requestError.code);
        deliver(std::move(callback), {}, requestError);
        return jobHasFinished;
      }
      const auto parsed = juce::JSON::parse(body);
      const auto* root = parsed.getDynamicObject();
      if (!root) {
        SessionAuthLogger::write("Cloud library response was not JSON");
        deliver(std::move(callback), {}, {status, "INVALID_RESPONSE", "Invalid cloud response"});
        return jobHasFinished;
      }
      CloudSongPage page;
      page.nextCursor = root->getProperty("nextCursor").toString();
      if (const auto* songs = root->getProperty("songs").getArray()) {
        for (const auto& value : *songs) {
          const auto* object = value.getDynamicObject();
          if (!object) continue;
          CloudSongSummary song;
          song.songId = object->getProperty("songId").toString();
          song.title = object->getProperty("title").toString();
          song.artist = object->getProperty("artist").toString();
          song.key = object->getProperty("key").toString();
          song.bpm = static_cast<int>(object->getProperty("bpm"));
          song.stemCount = juce::jmax(0, static_cast<int>(object->getProperty("stemCount")));
          song.revision = static_cast<int>(object->getProperty("revision"));
          song.durationSeconds = object->getProperty("durationSeconds").toString().getDoubleValue();
          song.storageBytes = object->getProperty("storageBytes").toString().getLargeIntValue();
          song.updatedAtMs = juce::Time::fromISO8601(object->getProperty("updatedAt").toString()).toMilliseconds();
          song.artworkPath = cacheCloudArtwork(
              object->getProperty("artworkUrl").toString(),
              object->getProperty("artworkSha256").toString(),
              object->getProperty("artworkContentType").toString());
          if (song.songId.isNotEmpty()) page.songs.push_back(std::move(song));
        }
      }
      SessionAuthLogger::write("Cloud library results=" +
                               juce::String(static_cast<int>(page.songs.size())));
      deliver(std::move(callback), std::move(page), {});
      return jobHasFinished;
    }
  };
  pool.addJob(new Job(apiBaseUrl, firebaseApiKey, refreshToken, query, cursor,
                      limit, std::move(callback)), true);
}
