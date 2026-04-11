#include "SongDownloadManager.h"
#include "CloudLibrarySyncService.h"

SongDownloadManager &SongDownloadManager::getInstance() {
  static SongDownloadManager instance;
  return instance;
}

bool SongDownloadManager::isDownloading(const juce::String &songId) const {
  const juce::ScopedLock sl(lock);
  const auto it = states.find(songId);
  if (it == states.end())
    return false;
  // Still downloading = progress in [0,1) and not failed
  return !it->second.failed && it->second.progress < 1.0f;
}

float SongDownloadManager::getProgress(const juce::String &songId) const {
  const juce::ScopedLock sl(lock);
  const auto it = states.find(songId);
  if (it == states.end())
    return -1.0f;
  if (it->second.failed)
    return -1.0f;
  return it->second.progress;
}

SongDownloadManager::DownloadState
SongDownloadManager::getState(const juce::String &songId) const {
  const juce::ScopedLock sl(lock);
  const auto it = states.find(songId);
  if (it == states.end())
    return {};
  return it->second;
}

bool SongDownloadManager::hasActiveDownloads() const {
  const juce::ScopedLock sl(lock);
  for (const auto &kv : states)
    if (!kv.second.failed && kv.second.progress < 1.0f)
      return true;
  return false;
}

void SongDownloadManager::clearState(const juce::String &songId) {
  {
    const juce::ScopedLock sl(lock);
    states.erase(songId);
  }
  sendChangeMessage();
}

void SongDownloadManager::startDownload(const juce::String &songId,
                                        OnComplete onComplete) {
  {
    const juce::ScopedLock sl(lock);
    const auto it = states.find(songId);
    if (it != states.end() && !it->second.failed && it->second.progress < 1.0f)
      return; // already running
    states[songId] = {0.0f, false, {}};
  }
  sendChangeMessage();

  juce::Thread::launch([this, songId, onComplete]() {
    auto progressCb = [this, songId](float p) {
      reportProgress(songId, p);
    };

    const auto result =
        CloudLibrarySyncService::pullSongFromDrive(songId, progressCb);

    reportFinished(songId, result.ok, result.message, onComplete);
  });
}

void SongDownloadManager::reportProgress(const juce::String &songId,
                                         float progress) {
  {
    const juce::ScopedLock sl(lock);
    auto it = states.find(songId);
    if (it == states.end())
      return;
    it->second.progress = juce::jlimit(0.0f, 0.99f, progress);
  }
  juce::MessageManager::callAsync([this] { sendChangeMessage(); });
}

void SongDownloadManager::reportFinished(const juce::String &songId, bool ok,
                                         const juce::String &errorMessage,
                                         OnComplete onComplete) {
  {
    const juce::ScopedLock sl(lock);
    auto &s = states[songId];
    if (ok) {
      s.progress = 1.0f;
      s.failed = false;
      s.errorMessage = {};
    } else {
      s.progress = 0.0f;
      s.failed = true;
      s.errorMessage = errorMessage;
    }
  }
  juce::MessageManager::callAsync([this, songId, ok, errorMessage, onComplete] {
    sendChangeMessage();
    if (onComplete)
      onComplete(ok, errorMessage);
    // Auto-clear success after notification so the row stops showing overlay
    if (ok) {
      const juce::ScopedLock sl(lock);
      const auto it = states.find(songId);
      if (it != states.end() && it->second.progress >= 1.0f)
        states.erase(it);
    }
  });
}
