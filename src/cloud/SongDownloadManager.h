#pragma once

#include <JuceHeader.h>
#include <functional>
#include <map>
#include <memory>

/**
 * SongDownloadManager
 *
 * Singleton-like manager (accessed via getInstance()) that tracks background
 * downloads of songs from Google Drive. Consumers can register a change
 * listener and query per-song progress from any thread.
 *
 * All public methods are safe to call from the message thread.
 * Listener callbacks are always delivered on the message thread.
 */
class SongDownloadManager : public juce::ChangeBroadcaster {
public:
  struct DownloadState {
    float progress = 0.0f; // 0.0 → 1.0
    bool failed = false;
    juce::String errorMessage;
  };

  using OnComplete = std::function<void(bool /*ok*/, juce::String /*error*/)>;

  /** Access the global shared instance. */
  static SongDownloadManager &getInstance();

  /**
   * Starts a background download for songId.
   * If a download for songId is already running, this is a no-op.
   * onComplete is called on the message thread when the download finishes.
   */
  void startDownload(const juce::String &songId,
                     OnComplete onComplete = nullptr);

  /** Returns true if a download is currently running for songId. */
  bool isDownloading(const juce::String &songId) const;

  /**
   * Returns current progress in [0,1] for a running download,
   * or -1.0f if no download is active for that songId.
   */
  float getProgress(const juce::String &songId) const;

  /**
   * Returns a copy of the download state for songId.
   * Returns a default-constructed state if not found.
   */
  DownloadState getState(const juce::String &songId) const;

  /** True if there is at least one active download. */
  bool hasActiveDownloads() const;

  /** Clears the completed/failed record for a song (call after showing error). */
  void clearState(const juce::String &songId);

private:
  SongDownloadManager() = default;

  // Called from background thread to update progress, then post to message thread.
  void reportProgress(const juce::String &songId, float progress);
  void reportFinished(const juce::String &songId, bool ok,
                      const juce::String &errorMessage, OnComplete onComplete);

  mutable juce::CriticalSection lock;
  std::map<juce::String, DownloadState> states;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongDownloadManager)
};
