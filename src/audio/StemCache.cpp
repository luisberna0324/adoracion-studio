#include "StemCache.h"

#include "../data/SongLibraryStore.h"

namespace {

// Sidecar metadata format. Plain text, key=value, one per line.
constexpr const char *kMetaMtimeKey = "source_mtime_ms";
constexpr const char *kMetaSizeKey = "source_size";

juce::AudioFormatManager &getFormatManager() {
  static juce::AudioFormatManager mgr;
  static const bool initialized = []() {
    mgr.registerBasicFormats();
    return true;
  }();
  juce::ignoreUnused(initialized);
  return mgr;
}

juce::File getDecodeRoot() {
  return SongLibraryStore::getRootDirectory().getChildFile("decoded");
}

} // namespace

juce::File StemCache::getCacheDirectory(const juce::String &songId) {
  auto dir = getDecodeRoot().getChildFile(songId);
  dir.createDirectory();
  return dir;
}

juce::File StemCache::getCacheFile(const juce::File &sourceFile,
                                   const juce::String &songId) {
  // Hash the absolute source path into a stable, filesystem-safe
  // filename suffix. The songId is already unique per song; the
  // hash disambiguates stems within the song directory and avoids
  // any path-length or character-set problems from the source name.
  const auto sourcePath = sourceFile.getFullPathName();
  juce::int64 hash = 0xcbf29ce484222325LL; // FNV-1a 64-bit
  for (auto c : sourcePath) {
    hash ^= static_cast<juce::int64>(c);
    hash *= 0x100000001b3LL;
  }
  const auto hex = juce::String::toHexString(hash).paddedLeft('0', 16);
  return getCacheDirectory(songId).getChildFile("stem_" + hex + ".flac");
}

juce::File StemCache::getMetadataFile(const juce::File &cacheFile) {
  return cacheFile.withFileExtension(".flac.meta.txt");
}

StemCache::SourceSnapshot StemCache::snapshotSource(const juce::File &sourceFile) {
  SourceSnapshot s;
  if (sourceFile.existsAsFile()) {
    s.size = sourceFile.getSize();
    s.mtime = sourceFile.getLastModificationTime();
  }
  return s;
}

bool StemCache::isValid(const juce::File &sourceFile,
                        const juce::String &songId) {
  const auto cache = getCacheFile(sourceFile, songId);
  if (!cache.existsAsFile())
    return false;

  const auto meta = getMetadataFile(cache);
  if (!meta.existsAsFile())
    return false;

  // Parse the sidecar. We are deliberately lenient: a malformed file
  // is treated as invalid so the next load rebuilds the cache.
  const auto text = meta.loadFileAsString();
  juce::int64 recordedSize = -1;
  juce::int64 recordedMtime = -1;
  for (auto line : juce::StringArray::fromLines(text)) {
    if (line.startsWith(kMetaSizeKey)) {
      recordedSize = line.substring(juce::String(kMetaSizeKey).length() + 1)
                         .getLargeIntValue();
    } else if (line.startsWith(kMetaMtimeKey)) {
      recordedMtime = line.substring(juce::String(kMetaMtimeKey).length() + 1)
                          .getLargeIntValue();
    }
  }
  if (recordedSize < 0 || recordedMtime < 0)
    return false;

  const auto current = snapshotSource(sourceFile);
  if (current.size != recordedSize)
    return false;
  if (current.mtime.toMilliseconds() != recordedMtime)
    return false;

  return true;
}

juce::File StemCache::precompute(const juce::File &sourceFile,
                                 const juce::String &songId) {
  const auto cache = getCacheFile(sourceFile, songId);
  cache.getParentDirectory().createDirectory();

  auto &mgr = getFormatManager();
  std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(sourceFile));
  if (reader == nullptr)
    throw std::runtime_error("StemCache: cannot create reader for source");

  // Create a temporary output file so a partial write cannot leave
  // the cache in a bad state. JUCE's FLAC writer requires the output
  // stream up front.
  const auto tmp = cache.withFileExtension(".flac.tmp");
  if (tmp.exists())
    tmp.deleteFile();

  std::unique_ptr<juce::FileOutputStream> out(tmp.createOutputStream());
  if (out == nullptr)
    throw std::runtime_error("StemCache: cannot create output stream");

  juce::FlacAudioFormat flac;
  std::unique_ptr<juce::AudioFormatWriter> writer(
      flac.createWriterFor(out.get(), reader->sampleRate,
                          static_cast<unsigned int>(reader->numChannels),
                          16, {}, 0));
  if (writer == nullptr)
    throw std::runtime_error("StemCache: cannot create FLAC writer");
  out.release(); // writer takes ownership

  // Stream the entire file in chunk-sized blocks. A 65536-sample
  // chunk keeps memory pressure low even for the longest stems.
  constexpr int kChunkSize = 65536;
  const auto totalSamples = reader->lengthInSamples;
  juce::AudioBuffer<float> buffer(
      static_cast<int>(reader->numChannels), kChunkSize);
  juce::int64 samplesWritten = 0;
  while (samplesWritten < totalSamples) {
    const int toRead =
        static_cast<int>(juce::jmin<juce::int64>(kChunkSize,
                                                  totalSamples - samplesWritten));
    if (!reader->read(&buffer, 0, toRead, samplesWritten, true, true))
      throw std::runtime_error("StemCache: read failed mid-stream");
    if (!writer->writeFromAudioSampleBuffer(buffer, 0, toRead))
      throw std::runtime_error("StemCache: FLAC write failed");
    samplesWritten += toRead;
  }
  writer->flush();
  writer.reset(); // closes the file
  tmp.moveFileTo(cache);

  // Write the sidecar so future loads can validate the cache.
  const auto snap = snapshotSource(sourceFile);
  const auto meta = getMetadataFile(cache);
  meta.replaceWithText(juce::String(kMetaMtimeKey) + "=" +
                        juce::String(snap.mtime.toMilliseconds()) + "\n" +
                        juce::String(kMetaSizeKey) + "=" +
                        juce::String(snap.size) + "\n");
  return cache;
}

void StemCache::precomputeAsync(
    const juce::File &sourceFile, const juce::String &songId,
    std::function<void(juce::File, bool)> onComplete) {
  juce::Thread::launch(
      [sourceFile, songId, cb = std::move(onComplete)]() {
        juce::File result;
        bool ok = false;
        try {
          result = precompute(sourceFile, songId);
          ok = true;
        } catch (...) {
          ok = false;
        }
        if (cb) {
          // Marshal back to the message thread so the UI can react
          // without any thread-safety concerns.
          juce::MessageManager::callAsync(
              [cb, result, ok]() { cb(result, ok); });
        }
      });
}
