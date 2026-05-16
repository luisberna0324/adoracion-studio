#pragma once

#include <JuceHeader.h>
#include <functional>
#include <unordered_map>

struct SongKey {
  juce::String key;
  juce::String range;
};

struct SongTrack {
  juce::String trackId;
  juce::String stemId;
  juce::String name;
  bool isClickOrGuide = false;
  juce::String color;
  juce::String waveformUrl;
  juce::String audioPreviewUrl;
  juce::String panPreviewUrl;
  bool enabled = false;
  double volume = 100.0;
  juce::String categoryId;
};

struct SongSearchResult {
  juce::String source;
  juce::String songId;
  juce::String trackId;
  juce::String stemId;
  juce::String title;
  juce::String artist;
  juce::String album;
  juce::String originalKey;
  double durationSeconds = 0.0;
  juce::String artworkUrl;
  juce::String waveformUrl;
  juce::String previewUrl;
  juce::String songUrl;
};

struct SongDetail {
  juce::String songId;
  double bpm = 0.0;
  double durationSeconds = 0.0;
  double previewStart = 0.0;
  double previewEnd = 0.0;
  std::vector<SongKey> keys;
  std::vector<SongTrack> tracks;
};

struct SecuenciasError {
  enum class Type { None, Network, Http, Json, EmptyResponse, InvalidInput };
  Type type = Type::None;
  int httpCode = 0;
  juce::String message;
  juce::String endpoint;
};

class SecuenciasClient {
public:
  using SearchCallback = std::function<void(std::vector<SongSearchResult>, SecuenciasError)>;
  using DetailCallback = std::function<void(SongDetail, SecuenciasError)>;

  SecuenciasClient();
  ~SecuenciasClient();

  void searchSongs(const juce::String& query, int page, SearchCallback cb);
  void getSongDetails(const juce::String& songId, int key, DetailCallback cb);
  void cancelAll();

private:
  struct CachedSearch { juce::int64 atMs = 0; std::vector<SongSearchResult> items; };
  struct CachedDetail { juce::int64 atMs = 0; SongDetail detail; };

  juce::CriticalSection lock;
  juce::ThreadPool pool;
  bool cancelled = false;
  std::unordered_map<std::string, CachedSearch> searchCache;
  std::unordered_map<std::string, CachedDetail> detailCache;

  static juce::String normalizeUrl(const juce::String& url);
  static double readDouble(const juce::var& v);
};
