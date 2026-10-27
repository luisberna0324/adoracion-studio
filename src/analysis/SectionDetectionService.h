#pragma once
#include "SectionDetection.h"
#include <memory>

namespace playworship::analysis {

enum class DetectionStage { preparingAudio, analyzingStructure, transcribingGuide,
                            mergingSuggestions, review, error, cancelled };
juce::String detectionStageName(DetectionStage);

// Synchronous provider operation called exclusively on the analysis worker.
// Providers return source-file seconds; no platform time types cross this API.
class IMusicStructureAnalyzer {
public:
  virtual ~IMusicStructureAnalyzer() = default;
  virtual bool available() const = 0;
  virtual juce::String unavailableReason() const = 0;
  virtual juce::String providerId() const = 0;
  virtual juce::String version() const = 0;
  virtual bool analyze(const juce::File &, double durationSeconds,
                       const CancellationCheck &, MusicStructureAnalysis &,
                       juce::String &error) = 0;
};
std::shared_ptr<IMusicStructureAnalyzer> createPlatformMusicStructureAnalyzer();

enum class AnalysisStemRole { music, guide, click, excluded };
struct AnalysisStem {
  juce::File file;
  AnalysisStemRole role = AnalysisStemRole::music;
  // Current ImportedStemData has no offset: adapter explicitly supplies zero.
  double offsetSeconds = 0.0;
};
struct SectionDetectionRequest {
  juce::String songId;
  std::vector<AnalysisStem> stems;
  FusionOptions fusion;
  juce::File cacheDirectory;
};
struct DetectionOutcome {
  SectionDetectionResult result;
  juce::String error;
  bool cancelled = false, cacheHit = false;
};

class SectionDetectionTask {
public:
  void cancel() noexcept { cancelled->store(true); }
  bool isCancelled() const noexcept { return cancelled->load(); }
private:
  friend class SectionDetectionService;
  std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
};

class SectionDetectionService {
public:
  explicit SectionDetectionService(std::shared_ptr<IMusicStructureAnalyzer> provider =
                                      createPlatformMusicStructureAnalyzer());
  bool available() const;
  juce::String unavailableReason() const;
  // Progress/completion run on worker, never audio/UI thread. Capture values and
  // marshal UI changes with callAsync/SafePointer. Cancelling never joins UI.
  std::shared_ptr<SectionDetectionTask> start(
      SectionDetectionRequest,
      std::function<void(DetectionStage)> progress,
      std::function<void(DetectionOutcome)> completion) const;
private:
  std::shared_ptr<IMusicStructureAnalyzer> provider;
};
} // namespace playworship::analysis
