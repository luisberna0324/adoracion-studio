#pragma once

#include "GuideTranscription.h"

namespace playworship::analysis {

struct GuideModelAsset {
  juce::String url, sha256;
  juce::int64 sizeBytes = 0;
};

GuideModelAsset defaultWhisperGuideModelAsset();

// Injectable stream source keeps regression tests local. Production always uses
// the pinned HTTPS model asset, never a song/audio path or an upload request.
class IGuideModelDownloadSource {
public:
  virtual ~IGuideModelDownloadSource() = default;
  virtual std::unique_ptr<juce::InputStream> open(const GuideModelAsset &,
      const CancellationCheck &, juce::String &error) = 0;
};

struct GuideModelPreparationResult {
  juce::File modelFile;
  juce::String error;
  bool cancelled = false, alreadyPresent = false;
};

class GuideModelPreparationTask {
public:
  void cancel() noexcept { cancelled->store(true); }
  bool isCancelled() const noexcept { return cancelled->load(); }
private:
  friend std::shared_ptr<GuideModelPreparationTask> prepareWhisperGuideModel(
      juce::File, GuideTranscriptionProgress,
      std::function<void(GuideModelPreparationResult)>,
      std::shared_ptr<IGuideModelDownloadSource>, GuideModelAsset);
  std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
};

// Progress/completion run on a worker. Cancel requests never join the UI thread;
// the worker removes its temporary after the current bounded network read ends.
std::shared_ptr<GuideModelPreparationTask> prepareWhisperGuideModel(
    juce::File destination, GuideTranscriptionProgress,
    std::function<void(GuideModelPreparationResult)>,
    std::shared_ptr<IGuideModelDownloadSource> = {},
    GuideModelAsset = defaultWhisperGuideModelAsset());

} // namespace playworship::analysis
