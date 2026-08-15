#include "StemCache.h"

#include "../SongLoadProbe.h"
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
  // P0 (perf): sanear el songId antes de construir el path. Sin esto,
  // títulos con `/`, `:`, etc. rompen getChildFile() (en POSIX la `/`
  // se interpreta como separador de directorio, en Windows es un
  // caracter inválido en general) y la cache termina en una ruta
  // distinta de la que isValid() busca después.
  auto dir = getDecodeRoot().getChildFile(sanitizeCacheKey(songId));
  dir.createDirectory();
  return dir;
}

juce::String StemCache::sanitizeCacheKey(const juce::String &rawSongId) {
  if (rawSongId.isEmpty())
    return "untitled";

  // Caracteres problemáticos en Windows (`\ : * ? " < > |`) más el
  // separador POSIX (`/`). Reemplazo por `_`. Mantengo espacios y el
  // resto del texto legible para que las rutas de cache sean fáciles
  // de inspeccionar.
  static const juce::juce_wchar kBadChars[] = {
      '/', '\\', ':', '*', '?', '"', '<', '>', '|'};
  juce::String result = rawSongId;
  for (juce::juce_wchar c : kBadChars)
    result = result.replaceCharacter(c, juce::juce_wchar('_'));

  // Windows se queja de nombres con espacio al final o al inicio, y
  // los hidden files de Unix empiezan con punto. Trim de ambos.
  result = result.trim();
  while (result.startsWithChar('.'))
    result = result.substring(1);
  result = result.trim();

  if (result.isEmpty())
    return "untitled";

  return result;
}

void StemCache::migrateLegacyCacheDirectories() {
  // Llamar una vez al startup. Recorre las subcarpetas de decoded/ y
  // renombra cualquier directorio cuyo nombre contenga un caracter que
  // sanitizeCacheKey() ahora reemplazaría. Las FLACs y sidecars
  // adentro siguen siendo válidos (el hash del nombre de archivo es
  // sobre el sourcePath, no sobre el directorio), así que el cache
  // queda utilizable sin re-precompute.
  //
  // P0 (perf): el walker original era non-recursive (findChildFiles
  // con `false`). Eso dejaba sin migrar los directorios legacy
  //产生的 del bug del `/` en titles como "Al Que Es Digno / Vencio",
  // donde el cache terminaba dividido en dos subniveles
  // (`decoded/Al Que Es Digno /` y `decoded/Al Que Es Digno / Vencio/`).
  // Esta versión hace una pasada recursiva en dos etapas.
  const auto root = getDecodeRoot();
  if (!root.isDirectory())
    return;

  // ---- Etapa 1: recuperación de espacios ----------------------------------
  // Para cada directorio top-level bajo `root`, si tiene un sub-directorio
  // cuyo nombre empieza con un espacio, restauramos el espacio al final
  // del nombre del padre. Esto revierte el daño de una migración previa
  // que ya había perdido el espacio, y deja la jerarquía en el estado
  // original antes de la etapa 2.
  {
    juce::Array<juce::File> topDirs;
    root.findChildFiles(topDirs, juce::File::findDirectories, false);
    for (const auto &dir : topDirs) {
      juce::Array<juce::File> subChildren;
      dir.findChildFiles(subChildren, juce::File::findDirectories, false);
      for (const auto &sub : subChildren) {
        const auto subName = sub.getFileName();
        if (subName.isNotEmpty() && subName.startsWithChar(' ')) {
          const auto restored = dir.getFileName() + " ";
          const auto restoredPath = root.getChildFile(restored);
          if (!restoredPath.exists() && dir.moveFileTo(restoredPath)) {
            juce::Logger::writeToLog(
                "[StemCache] recovered trailing space on parent '" +
                dir.getFileName() + "' -> '" + restored + "'");
          }
          break;
        }
      }
    }
  }

  // ---- Etapa 2: migración real --------------------------------------------
  // Walk recursivo post-order (leaves first). Para cada directorio
  // visitado, reconstruimos la cadena de nombres hasta `root` y, si
  // contiene un `/`, es el split-path artifact: lo renombramos a la
  // versión saneada al nivel de `root`.
  struct Entry {
    juce::File parent;
    juce::String name;
  };
  std::vector<Entry> queue;
  {
    juce::Array<juce::File> topDirs;
    root.findChildFiles(topDirs, juce::File::findDirectories, false);
    for (const auto &dir : topDirs)
      queue.push_back({root, dir.getFileName()});
  }
  while (!queue.empty()) {
    const auto entry = queue.back();
    queue.pop_back();
    const auto dir = entry.parent.getChildFile(entry.name);
    if (!dir.isDirectory())
      continue;
    juce::Array<juce::File> children;
    dir.findChildFiles(children, juce::File::findDirectories, false);
    for (const auto &child : children)
      queue.push_back({dir, child.getFileName()});

    // Reconstruct the logical key by joining this dir's name with the
    // chain of parent directory names up to (but excluding) `root`.
    juce::String chainName = dir.getFileName();
    auto chainParent = dir.getParentDirectory();
    while (chainParent != root && chainParent.isDirectory()) {
      const auto pname = chainParent.getFileName();
      if (pname.isNotEmpty())
        chainName = pname + "/" + chainName;
      chainParent = chainParent.getParentDirectory();
    }
    const auto logicalKey = chainName.replace("\\", "/");
    const auto sanitized = sanitizeCacheKey(logicalKey);
    if (sanitized == chainName)
      continue;  // clean, no migration needed

    const auto target = root.getChildFile(sanitized);
    if (target == dir)
      continue;
    if (target.exists()) {
      juce::Logger::writeToLog(
          "[StemCache] legacy dir chain '" + chainName +
          "' cannot migrate: target '" + sanitized + "' already exists");
      continue;
    }
    if (dir.moveFileTo(target)) {
      juce::Logger::writeToLog(
          "[StemCache] migrated legacy chain '" + chainName + "' -> '" +
          sanitized + "'");
    } else {
      juce::Logger::writeToLog(
          "[StemCache] failed to migrate legacy chain '" + chainName +
          "': moveFileTo returned false");
    }
  }

  // ---- Etapa 3: limpieza de subdirs vacíos -------------------------------
  // Cualquier hijo directo de `root` que haya quedado vacío (sus
  // contenidos fueron migrados hacia arriba) se elimina para no acumular
  // subdirectorios muertos.
  juce::Array<juce::File> rootChildren;
  root.findChildFiles(rootChildren, juce::File::findDirectories, false);
  for (const auto &child : rootChildren) {
    if (child.isDirectory() &&
        child.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0) {
      child.deleteRecursively();
    }
  }
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
  // SongLoadProbe: time the lookup of the sidecar FLAC + metadata parse.
  // The hot path is existsAsFile() — when the cache directory is missing
  // entirely (e.g. legacy split-path scenario for "Al Que Es Digno /
  // Vencio") the lookup is a single stat() per stem. We only time when
  // the call originates from the load path (not from migration / tests).
  const int64_t t0 = juce::Time::getHighResolutionTicks();
  auto recordMiss = [&](const juce::String &reason,
                        const juce::String &extra = {}) {
    const int64_t dt = static_cast<int64_t>(
        (juce::Time::getHighResolutionTicks() - t0) * 1000000.0 /
        juce::Time::getHighResolutionTicksPerSecond());
    SongLoadProbe::instance().recordEvent(
        "StemCache.isValid", reason,
        "songId=" + songId + " us=" + juce::String(dt) +
            (extra.isNotEmpty() ? " " + extra : juce::String{}));
  };
  const auto cache = getCacheFile(sourceFile, songId);
  if (!cache.existsAsFile()) {
    recordMiss("miss.noFlac", "path=" + cache.getFullPathName());
    return false;
  }

  const auto meta = getMetadataFile(cache);
  if (!meta.existsAsFile()) {
    recordMiss("miss.noMeta");
    return false;
  }

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
  if (recordedSize < 0 || recordedMtime < 0) {
    recordMiss("miss.badMeta");
    return false;
  }

  const auto current = snapshotSource(sourceFile);
  if (current.size != recordedSize) {
    recordMiss("miss.sizeMismatch",
               "got=" + juce::String(current.size) +
                   " want=" + juce::String(recordedSize));
    return false;
  }
  if (current.mtime.toMilliseconds() != recordedMtime) {
    recordMiss("miss.mtimeMismatch",
               "got=" + juce::String(current.mtime.toMilliseconds()) +
                   " want=" + juce::String(recordedMtime));
    return false;
  }

  // SongLoadProbe: hit (size + mtime matched). Time the full lookup.
  const int64_t dt = static_cast<int64_t>(
      (juce::Time::getHighResolutionTicks() - t0) * 1000000.0 /
      juce::Time::getHighResolutionTicksPerSecond());
  SongLoadProbe::instance().recordEvent(
      "StemCache.isValid", "hit", "songId=" + songId + " us=" + juce::String(dt));
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
