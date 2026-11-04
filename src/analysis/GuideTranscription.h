#pragma once
#include "SectionDetection.h"
#include <memory>

namespace playworship::analysis {

// Source-audio seconds, including explicit stem offset. These are spoken
// announcements, not inferred musical section boundaries.
struct GuideTranscriptSegment {
  double startSeconds = 0.0;
  double endSeconds = 0.0;
  juce::String text;
};

struct GuideTranscript {
  juce::String provider, providerVersion, modelFingerprint, audioFingerprint;
  juce::String language;
  double durationSeconds = 0.0;
  std::vector<GuideTranscriptSegment> segments;
};

struct GuideTranscriptionRequest {
  juce::File audioFile, modelFile;
  juce::String language = "es";
  double offsetSeconds = 0.0;
};

using GuideTranscriptionProgress = std::function<void(double)>;

class IGuideTranscriber {
public:
  virtual ~IGuideTranscriber() = default;
  virtual bool available() const = 0;
  virtual juce::String unavailableReason() const = 0;
  virtual juce::String providerId() const = 0;
  virtual juce::String version() const = 0;
  virtual bool transcribe(const GuideTranscriptionRequest &,
                          const CancellationCheck &,
                          const GuideTranscriptionProgress &,
                          GuideTranscript &, juce::String &error) = 0;
};

std::shared_ptr<IGuideTranscriber> createWhisperGuideTranscriber();
juce::File defaultWhisperGuideModelFile();

} // namespace playworship::analysis
