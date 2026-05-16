#include "SecuenciasClient.h"

namespace {
constexpr int kTimeoutMs = 15000;
constexpr juce::int64 kTtlMs = 10 * 60 * 1000;

juce::String readAll(juce::InputStream& s) { return s.readEntireStreamAsString(); }

juce::String cacheKey(const juce::String& a, int b) { return (a + "#" + juce::String(b)).toStdString(); }

bool isFresh(juce::int64 atMs) {
  return (juce::Time::currentTimeMillis() - atMs) < kTtlMs;
}
} // namespace

SecuenciasClient::SecuenciasClient() : pool(2) {}
SecuenciasClient::~SecuenciasClient() { cancelAll(); pool.removeAllJobs(true, 2000); }

juce::String SecuenciasClient::normalizeUrl(const juce::String& url) {
  if (url.startsWithIgnoreCase("http://") || url.startsWithIgnoreCase("https://"))
    return url;
  if (url.startsWithChar('/'))
    return "https://www.secuencias.com" + url;
  return url;
}

double SecuenciasClient::readDouble(const juce::var& v) { return v.toString().getDoubleValue(); }

void SecuenciasClient::cancelAll() {
  const juce::ScopedLock sl(lock);
  cancelled = true;
}

void SecuenciasClient::searchSongs(const juce::String& query, int page, SearchCallback cb) {
  const auto q = query.trim();
  if (q.isEmpty()) {
    if (cb) cb({}, {SecuenciasError::Type::InvalidInput, 0, "Query vacia", "search/songs"});
    return;
  }

  const auto key = cacheKey(q, page);
  {
    const juce::ScopedLock sl(lock);
    auto it = searchCache.find(key.toStdString());
    if (it != searchCache.end() && isFresh(it->second.atMs)) {
      if (cb) cb(it->second.items, {});
      return;
    }
  }

  struct Job : juce::ThreadPoolJob {
    SecuenciasClient& c; juce::String q; int page; SearchCallback cb; std::string key;
    Job(SecuenciasClient& cc, juce::String qq, int p, SearchCallback f, std::string k)
      : juce::ThreadPoolJob("SecuenciasSearch"), c(cc), q(std::move(qq)), page(p), cb(std::move(f)), key(std::move(k)) {}
    JobStatus runJob() override {
      juce::DynamicObject::Ptr body = new juce::DynamicObject();
      body->setProperty("searchText", q);
      body->setProperty("order", 1);
      body->setProperty("chart", juce::var());
      body->setProperty("sounds", juce::var());
      body->setProperty("proPresenter", juce::var());
      body->setProperty("productionCues", juce::var());
      body->setProperty("pad", juce::var());
      body->setProperty("pageNumber", page);
      body->setProperty("siteID", 3);
      body->setProperty("languageCode", "es");
      body->setProperty("restrictedCountry", false);
      body->setProperty("countryID", 48);
      body->setProperty("syncLicensing", juce::var());
      body->setProperty("reverseOrder", juce::var());

      juce::URL url("https://api.multitracks.com/search/songs");
      url = url.withPOSTData(juce::JSON::toString(juce::var(body)));
      int status = 0;
      auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withExtraHeaders("Content-Type: application/json\r\nUser-Agent: Mozilla/5.0\r\n")
        .withConnectionTimeoutMs(kTimeoutMs).withHttpRequestCmd("POST").withStatusCode(&status);
      auto stream = url.createInputStream(opts);
      if (!stream) { if (cb) juce::MessageManager::callAsync([f=cb]{ f({}, {SecuenciasError::Type::Network,0,"No response","search/songs"});}); return jobHasFinished; }
      auto text = readAll(*stream);
      if (status < 200 || status >= 300) { if (cb) juce::MessageManager::callAsync([f=cb,status]{ f({}, {SecuenciasError::Type::Http,status,"HTTP error","search/songs"});}); return jobHasFinished; }
      auto parsed = juce::JSON::parse(text);
      auto* o = parsed.getDynamicObject();
      if (!o) { if (cb) juce::MessageManager::callAsync([f=cb]{ f({}, {SecuenciasError::Type::Json,0,"JSON invalido","search/songs"});}); return jobHasFinished; }

      const auto resultFlag = static_cast<int>(o->getProperty("result"));
      if (resultFlag == 0) {
        const auto message = o->getProperty("message").toString();
        if (cb) juce::MessageManager::callAsync([f=cb,message]{
          f({}, {SecuenciasError::Type::Http,0, message.isNotEmpty() ? message : "Search failed", "search/songs"});
        });
        return jobHasFinished;
      }

      std::vector<SongSearchResult> out;
      auto items = o->getProperty("items");
      if (auto* itemsArray = items.getArray()) {
        out.reserve(static_cast<size_t>(itemsArray->size()));
        for (const auto& v : *itemsArray) {
          auto* so = v.getDynamicObject(); if (!so) continue;
          SongSearchResult s;
          s.source = "remote-search";
          s.songId = so->getProperty("songID").toString();
          s.trackId = so->getProperty("trackID").toString();
          s.stemId = so->getProperty("stemID").toString();
          s.title = so->getProperty("title").toString();
          s.artist = so->getProperty("artist").toString();
          s.album = so->getProperty("album").toString();
          s.originalKey = so->getProperty("originalKey").toString();
          s.durationSeconds = readDouble(so->getProperty("duration"));
          s.artworkUrl = normalizeUrl(so->getProperty("imgURL").toString());
          if (s.artworkUrl.trim().isEmpty())
            s.artworkUrl = normalizeUrl(so->getProperty("imgURL2x").toString());
          s.waveformUrl = normalizeUrl(so->getProperty("waveformURL").toString());
          s.previewUrl = normalizeUrl(so->getProperty("previewURL").toString());
          s.songUrl = normalizeUrl(so->getProperty("songURL").toString());

          if (s.artist.trim().isEmpty()) {
            if (auto* artists = so->getProperty("artists").getArray()) {
              for (const auto& av : *artists) {
                if (auto* ao = av.getDynamicObject()) {
                  const auto candidate = ao->getProperty("artist").toString().trim();
                  if (candidate.isNotEmpty()) {
                    s.artist = candidate;
                    break;
                  }
                }
              }
            }
          }
          out.push_back(std::move(s));
        }
      } else {
        // Fallback de compatibilidad para payload antiguo.
        auto songs = o->getProperty("songs");
        if (auto* songsArray = songs.getArray()) {
          out.reserve(static_cast<size_t>(songsArray->size()));
          for (const auto& v : *songsArray) {
            auto* so = v.getDynamicObject(); if (!so) continue;
            SongSearchResult s;
            s.source = "remote-search";
            s.songId = so->getProperty("songID").toString();
            s.trackId = so->getProperty("trackID").toString();
            s.stemId = so->getProperty("stemID").toString();
            s.title = so->getProperty("title").toString();
            s.artist = so->getProperty("artist").toString();
            s.album = so->getProperty("album").toString();
            s.originalKey = so->getProperty("originalKey").toString();
            s.durationSeconds = readDouble(so->getProperty("duration"));
            s.artworkUrl = normalizeUrl(so->getProperty("imgURL").toString());
            s.waveformUrl = normalizeUrl(so->getProperty("waveformURL").toString());
            s.previewUrl = normalizeUrl(so->getProperty("previewURL").toString());
            s.songUrl = normalizeUrl(so->getProperty("songURL").toString());
            out.push_back(std::move(s));
          }
        }
      }
      {
        const juce::ScopedLock sl(c.lock);
        c.searchCache[key] = {juce::Time::currentTimeMillis(), out};
      }
      if (cb) {
        juce::MessageManager::callAsync([f=cb, payload=std::move(out)]() mutable {
          f(std::move(payload), {});
        });
      }
      return jobHasFinished;
    }
  };

  pool.addJob(new Job(*this, q, page, std::move(cb), key.toStdString()), true);
}

void SecuenciasClient::getSongDetails(const juce::String& songId, int key, DetailCallback cb) {
  const auto id = songId.trim();
  if (id.isEmpty()) { if (cb) cb({}, {SecuenciasError::Type::InvalidInput,0,"songId vacio","player.aspx"}); return; }
  const auto ck = cacheKey(id, key);
  {
    const juce::ScopedLock sl(lock);
    auto it = detailCache.find(ck.toStdString());
    if (it != detailCache.end() && isFresh(it->second.atMs)) { if (cb) cb(it->second.detail, {}); return; }
  }
  struct Job : juce::ThreadPoolJob {
    SecuenciasClient& c; juce::String id; int key; DetailCallback cb; std::string ck;
    Job(SecuenciasClient& cc, juce::String i, int k, DetailCallback f, std::string ckk): juce::ThreadPoolJob("SecuenciasDetail"), c(cc), id(std::move(i)), key(k), cb(std::move(f)), ck(std::move(ckk)) {}
    JobStatus runJob() override {
      juce::URL url("https://www.secuencias.com/ws/player.aspx");
      url = url.withParameter("songID", id).withParameter("key", juce::String(key)).withParameter("language", "es");
      int status = 0;
      auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(kTimeoutMs).withHttpRequestCmd("GET").withStatusCode(&status);
      auto stream = url.createInputStream(opts);
      if (!stream) { if (cb) juce::MessageManager::callAsync([f=cb]{ f({}, {SecuenciasError::Type::Network,0,"No response","player.aspx"});}); return jobHasFinished; }
      auto text = readAll(*stream);
      if (status < 200 || status >= 300) { if (cb) juce::MessageManager::callAsync([f=cb,status]{ f({}, {SecuenciasError::Type::Http,status,"HTTP error","player.aspx"});}); return jobHasFinished; }
      auto parsed = juce::JSON::parse(text); auto* o = parsed.getDynamicObject();
      if (!o) { if (cb) juce::MessageManager::callAsync([f=cb]{ f({}, {SecuenciasError::Type::Json,0,"JSON invalido","player.aspx"});}); return jobHasFinished; }
      SongDetail d; d.songId=o->getProperty("songID").toString(); d.bpm=readDouble(o->getProperty("bpm")); d.durationSeconds=readDouble(o->getProperty("duration")); d.previewStart=readDouble(o->getProperty("previewStart")); d.previewEnd=readDouble(o->getProperty("previewEnd"));
      if (auto* arr=o->getProperty("keys").getArray()) for (auto& kv:*arr){ if(auto* ko=kv.getDynamicObject()){ d.keys.push_back({ko->getProperty("key").toString(),ko->getProperty("range").toString()}); }}
      if (auto* arr=o->getProperty("tracks").getArray()) for (auto& tv:*arr){ if(auto* to=tv.getDynamicObject()){ SongTrack t; t.trackId=to->getProperty("trackID").toString(); t.stemId=to->getProperty("stemID").toString(); if(auto* po=to->getProperty("part").getDynamicObject()){ t.name=po->getProperty("name").toString(); t.isClickOrGuide=(bool)po->getProperty("isClickOrGuide"); } t.color=to->getProperty("color").toString(); t.waveformUrl=normalizeUrl(to->getProperty("waveform").toString()); t.audioPreviewUrl=normalizeUrl(to->getProperty("audio").toString()); t.panPreviewUrl=normalizeUrl(to->getProperty("pan").toString()); t.enabled=to->getProperty("state").toString().equalsIgnoreCase("enabled"); t.volume=readDouble(to->getProperty("volume")); t.categoryId=to->getProperty("categoryID").toString(); d.tracks.push_back(std::move(t)); }}
      { const juce::ScopedLock sl(c.lock); c.detailCache[ck]={juce::Time::currentTimeMillis(), d}; }
      if (cb) {
        juce::MessageManager::callAsync([f=cb, payload=std::move(d)]() mutable {
          f(std::move(payload), {});
        });
      }
      return jobHasFinished;
    }
  };
  pool.addJob(new Job(*this, id, key, std::move(cb), ck.toStdString()), true);
}
