#pragma once

#include "UpdateManifest.h"

#include <JuceHeader.h>
#include <functional>

namespace PlayWorshipUpdate {

struct UpdateServiceConfig {
  juce::String updateBaseUrl;
  juce::String currentVersion;
  juce::String channel = "stable";
};

struct UpdateProgress {
  juce::String status;
  double progress = -1.0;
  juce::int64 bytesDone = 0;
  juce::int64 bytesTotal = 0;
};

class UpdateService final : private juce::Thread {
public:
  using AvailableCallback = std::function<void(UpdateManifest)>;
  using ProgressCallback = std::function<void(UpdateProgress)>;
  using ErrorCallback = std::function<void(juce::String)>;

  explicit UpdateService(UpdateServiceConfig config);
  ~UpdateService() override;

  void setCallbacks(AvailableCallback available, ProgressCallback progress,
                    ErrorCallback error);
  void checkNow();
  void downloadAndInstall(UpdateManifest manifest);
  bool isBusy() const noexcept;

private:
  enum class Job { None, Check, Download };

  void run() override;
  void runCheck();
  void runDownload();
  void postError(juce::String message);
  void postProgress(UpdateProgress progress);
  juce::URL buildCheckUrl() const;
  juce::File getUpdatesRoot() const;
  juce::File resolveUpdaterExecutable() const;
  juce::File resolveInstallRoot() const;
  bool launchUpdater(const juce::File &packageFile,
                     const UpdatePackage &package,
                     const juce::String &targetVersion);

  UpdateServiceConfig config;
  mutable juce::CriticalSection lock;
  Job pendingJob = Job::None;
  UpdateManifest pendingManifest;
  AvailableCallback onAvailable;
  ProgressCallback onProgress;
  ErrorCallback onError;
};

} // namespace PlayWorshipUpdate
