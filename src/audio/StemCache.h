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
   * necessary. Exposed for diagnostics and tests.
   */
  static juce::File getCacheDirectory(const juce::String &songId);

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
