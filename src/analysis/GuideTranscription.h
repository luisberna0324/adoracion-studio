#pragma once
#include "SectionDetection.h"
#include <memory>

namespace playworship::analysis {

// Source-audio seconds, including explicit stem offset. These are spoken
// announcements, not inferred musical section boundaries.
struct GuideTranscriptAlternative {
  juce::String text, language;
  double avgLogProbability = -100.0;
  double noSpeechProbability = 1.0;
};

// Optional ASR word intervals in source seconds. They express timing evidence,
// not exact vocal onsets or an inferred musical pulse.
struct GuideTranscriptWord {
  juce::String text;
  double startSeconds = 0.0, endSeconds = 0.0;
  std::optional<double> confidence;
};

// Optional independently measured onsets for a complete known-voice compound.
// Text identifies the measured announcement; an omitted ASR count can only be
// recovered by matching its actual recording. The original parent stays raw.
// Correlation measures similarity to a recording, not a token probability.
struct GuideAcousticAnnouncement {
  juce::String text, language, templateIdentity;
  double startSeconds = 0.0, endSeconds = 0.0, correlation = 0.0;
};

// A repeated instruction cites its original first-pass segment in this guide.
// A section recovered by decoding the remaining speech has no such reference.
struct GuideRepetitionAnnouncement {
  juce::String text, language;
  double startSeconds = 0.0, endSeconds = 0.0;
  std::optional<size_t> referenceSegment;
  std::optional<double> correlation;
};

struct GuideTranscriptSegment {
  double startSeconds = 0.0;
  double endSeconds = 0.0;
  juce::String text;
  juce::String language;
  // Geometric mean of token probabilities, not calibrated musical confidence.
  std::optional<double> confidence;
  // Evaluated hypotheses, including the selected one; original text is retained.
  std::vector<GuideTranscriptAlternative> alternatives;
  std::vector<GuideTranscriptWord> words;
  // The raw parent is retained even when these independent points are usable.
  std::vector<GuideAcousticAnnouncement> acousticAnnouncements;
  std::optional<double> acousticResidualRatio;
  // A bounded catalogue-constrained retry is separate evidence. Its scores
  // are conditioned by the grammar and never replace literal ASR alternatives.
  std::vector<GuideTranscriptAlternative> catalogueAlternatives;
  std::vector<GuideRepetitionAnnouncement> repetitionAnnouncements;
};

struct GuideTranscript {
  juce::String provider, providerVersion, modelFingerprint, audioFingerprint;
  juce::String language;
  double durationSeconds = 0.0;
  std::vector<GuideTranscriptSegment> segments;
};

struct GuideTranscriptionRequest {
  juce::File audioFile, modelFile;
  juce::String language = "es-en";
  double offsetSeconds = 0.0;
  // The service captures mutable provider resources before looking up cache.
  // A provider with such resources must reject a different captured version.
  juce::String expectedProviderVersion;
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
