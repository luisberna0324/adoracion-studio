#pragma once

#include <JuceHeader.h>
#include <functional>

/**
 * StemCache — Decode-once cache for stem audio files.
 *
 * The playback pipeline (ResamplingPositionableSource → AudioFormatReader)
 * pays the AAC/WAV/FLAC decode cost on every getNextAudioBlock call. The
 * benchmark shows the AAC path takes 1.2-2.9ms per stem per block, which
 * exceeds the 11.6ms block budget when many stems are loaded.
 *
 * This class precomputes a lossless FLAC copy of every stem the first
 * time it is opened. Subsequent loads read the FLAC (which decodes
 * 5-10x faster than AAC on every platform JUCE supports). A sidecar
 * metadata file next to each cached .flac records the source file's
 * mtime, size and a short content hash so stale caches are detected
 * and rebuilt automatically.
 *
 * The cache directory lives under
 *   SongLibraryStore::getRootDirectory().getChildFile("decoded")
 * which JUCE maps to a per-platform application-data directory:
 *   macOS:   ~/Library/Application Support/MTControls/decoded
 *   Windows: %APPDATA%\MTControls\decoded
 *   Linux:   ~/.config/MTControls/decoded
 *
 * The format is FLAC because it is lossless (no quality loss from the
 * user's perspective), has a fast native decoder on every platform
 * (libFLAC is bundled in juce_audio_formats), and compresses to
 * roughly 50-60% of the original PCM size — a reasonable trade for the
 * decode-speed win.
 */
class StemCache {
public:
  /**
   * Returns the .flac file that holds the cached decode of the given
   * source. The file may not yet exist on disk — callers should check
   * isValid() before using it. The returned path is stable for a given
   * (sourceFile, songId) pair, so callers can hold on to it.
   *
   * Note: the songId parameter is treated as an opaque filesystem-safe
   * key (a UUID in production). It is NOT sanitized; callers must pass
   * a stable identifier, never a user-editable title. See
   * perf/analysis_20260912/INFORME.md §4.1.
   */
  static juce::File getCacheFile(const juce::File &sourceFile,
                                 const juce::String &songId);

  /**
   * Returns true if a valid cache exists for sourceFile. Validity is
   * determined by:
   *   1. the .flac file exists
   *   2. the sidecar metadata file exists and parses cleanly
   *   3. source mtime matches
   *   4. source size matches
   *   5. (optional, currently disabled) content hash matches
   *
   * If any check fails, returns false and the caller should fall back
   * to reading the source file directly.
   */
  static bool isValid(const juce::File &sourceFile,
                      const juce::String &songId);

  /**
   * Synchronously decode the source file to FLAC and write the cache.
   * Blocks until done. Throws on error. Returns the cache file on
   * success. Use precomputeAsync() from the message thread so the UI
   * does not freeze.
   */
  static juce::File precompute(const juce::File &sourceFile,
                               const juce::String &songId);

  /**
   * Precompute the cache in a background thread. Fires `onComplete`
   * on the message thread when done (or immediately if called from the
   * message thread with no worker available). The bool argument to
   * onComplete is true on success.
   */
  static void precomputeAsync(
      const juce::File &sourceFile, const juce::String &songId,
      std::function<void(juce::File /*cacheFile*/,
                         bool /*success*/)> onComplete);

  /**
   * Returns the cache directory for a given song id, creating it if
   * necessary. Exposed for diagnostics and tests. The id is used
   * verbatim — no sanitization — so it must already be filesystem-safe
   * (e.g. a UUID hex string).
   */
  static juce::File getCacheDirectory(const juce::String &songId);

  /**
   * Sanitizes a raw song identifier (typically the song title) into a
   * stable, filesystem-safe cache key.
   *
   * Background: before this helper existed, getCacheDirectory() called
   * getChildFile() with the raw title. Titles containing characters that
   * are either path separators (`/`, `\` on Windows) or invalid in
   * Windows filenames (`:`, `*`, `?`, `"`, `<`, `>`, `|`) caused the
   * cache to be written to a wrong or split directory. The first
   * concrete case was a song titled "Al Que Es Digno / Vencio", whose
   * cache ended up in `decoded/Al Que Es Digno / Vencio/` (with the
   * `/` interpreted as a directory separator on POSIX), and the cache
   * lookup could not find it consistently.
   *
   * The sanitizer replaces every problematic character with `_`, trims
   * leading/trailing whitespace and dots, and falls back to "untitled"
   * if the result would otherwise be empty. The output is intended to
   * be human-readable, not a hash, so logs and on-disk inspection stay
   * useful. The sidecar files inside the directory still key on the
   * sourcePath hash, so collisions between similarly-titled songs are
   * caught by the existing isValid() mtime/size check.
   */
  static juce::String sanitizeCacheKey(const juce::String &rawSongId);

  /**
   * One-shot migration of any pre-sanitizer cache directories whose
   * names contain characters that sanitizeCacheKey() would now replace.
   * Called once at startup from the message thread; safe to call again
   * (idempotent). The migration renames directories in place so any
   * existing .flac / .meta.txt files keep working — no re-precompute
   * needed.
   */
  static void migrateLegacyCacheDirectories();

  /**
   * Removes the cache directory associated with a song. Safe to call
   * when the directory does not exist (no-op). The cache is content-
   * addressable by source path hash, not by song identity, so deleting
   * it is always safe — any subsequent loadSongStems() will re-precompute
   * as needed.
   *
   * Background: prior to this helper, deleting a song from the library
   * only removed audio/, waveforms/ and project-cache/ — the FLAC cache
   * at decoded/<songId>/ accumulated forever and could reach several
   * hundred MB per song that was once loaded as legacy (non-FLAC) and
   * later converted to a .pwproj.
   */
  static void removeSongCache(const juce::String &songId);

  /**
   * Removes a specific cached FLAC file (and its sidecar) for a source.
   * Used when the sidecar is stale (mtime/size mismatch) so the next
   * precompute doesn't have to wait for an overwrite.
   */
  static void removeCachedFile(const juce::File &sourceFile,
                                const juce::String &songId);

  /**
   * One-shot migration that renames any decoded/<dir>/ directory whose
   * name does NOT look like a UUID back to decoded/<songId>/, where
   * songId is the id of a song whose sanitizeCacheKey(title) matches
   * the directory name. This is for the historical cache layout that
   * keyed on song title (a human-readable, mutable string) — the new
   * layout keys on the immutable songId. Idempotent: directories that
   * already have a UUID-shaped name are left alone.
   *
   * See perf/analysis_20260912/INFORME.md §4.1.
   */
  static int migrateTitleKeyedDirectoriesToIds(
      const std::vector<juce::String> &validSongIds,
      const std::vector<std::pair<juce::String, juce::String>>
          &titleToIdMapping);

private:
  // The sidecar metadata file (sits next to the .flac with .meta.txt
  // suffix). Format is plain text key=value, one per line. The
  // content hash line is present but currently not enforced — mtime
  // + size is fast and accurate enough for our use case.
  static juce::File getMetadataFile(const juce::File &cacheFile);

  struct SourceSnapshot {
    juce::int64 size = 0;
    juce::Time mtime{};
  };

  static SourceSnapshot snapshotSource(const juce::File &sourceFile);
};
