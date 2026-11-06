#pragma once

#include "../data/SongData.h"
#include "../data/GuideVocabulary.h"
#include <atomic>
#include <cstdint>

namespace playworship::analysis {

inline constexpr int sectionDetectionSchemaVersion = 1;
inline constexpr const char *sectionDetectionAlgorithmVersion = "sections-mvp-11-count-word-boundaries";
inline constexpr std::size_t maximumSectionSuggestions = 256;
inline constexpr double minimumAcceptedSectionDurationSeconds = 0.25;

// The shared vocabulary preserves every existing serialized value (0..15).
using SectionType = playworship::guide::SectionFamily;
enum class ConfidenceLevel { low, medium, high };
enum class SuggestionStatus { suggested, confirmed, rejected };

struct TimeRange {
  double startSeconds = 0.0;
  double endSeconds = 0.0;
};

struct TempoEvidence {
  std::optional<double> beatsPerMinute;
  std::vector<double> beats;
  std::vector<double> bars;
  juce::String source;
};

struct MusicStructureAnalysis {
  std::vector<TimeRange> sections, segments, phrases;
  TempoEvidence tempo;
  juce::String provider, version;
};

struct GuideCue {
  double cueTimeSeconds = 0.0;
  juce::String rawText;
  SectionType normalizedType = SectionType::unknown;
  std::optional<int> spokenOrdinal;
  std::optional<double> asrConfidence;
};

// An independent spoken instruction, retained separately from musical ranges.
// name uses an existing guide key when its meaning is equivalent. Instructions
// without an exact recorded counterpart keep their literal name for review.
struct GuideUtilityCue {
  double cueTimeSeconds = 0.0;
  juce::String rawText, name;
  std::optional<double> asrConfidence;
};

struct SectionSuggestion {
  juce::String id;
  SectionType type = SectionType::unknown;
  juce::String displayName;
  int occurrence = 1;
  double rawStartSeconds = 0.0;
  double startSeconds = 0.0;
  double endSeconds = 0.0;
  // Internal bar indices start at zero; a UI displays index + 1.
  std::optional<int> startBarIndex, endBarIndex;
  double quantizationDistanceSeconds = 0.0;
  std::optional<double> matchedCueTimeSeconds;
  std::optional<double> guideAnticipationSeconds;
  juce::StringArray sources;
  ConfidenceLevel confidenceLevel = ConfidenceLevel::low;
  juce::String reason;
  SuggestionStatus status = SuggestionStatus::suggested;
  bool userEdited = false;
};

struct SectionDetectionResult {
  int schemaVersion = sectionDetectionSchemaVersion;
  juce::String songId;
  double durationSeconds = 0.0;
  juce::String analysisProvider;
  juce::String analysisVersion = sectionDetectionAlgorithmVersion;
  juce::String analyzedAudioFingerprint;
  MusicStructureAnalysis structure;
  std::vector<GuideCue> guideCues;
  std::vector<SectionSuggestion> suggestions;
};

struct FusionOptions {
  // Only supply a grid explicitly confirmed as synchronized by the user.
  std::vector<double> confirmedBars;
  juce::String confirmedGridSource = juce::String::fromUTF8("cuadrícula confirmada");
  double maximumSnapSeconds = 0.35;
  double maximumSnapFractionOfBar = 0.125;
};

using CancellationCheck = std::function<bool()>;

juce::String sectionTypeName(SectionType type);
juce::String confidenceName(ConfidenceLevel level);
std::optional<GuideCue> normalizeGuideCue(double cueTimeSeconds,
                                        const juce::String &rawText,
                                        std::optional<double> asrConfidence = {});
// Known count/instrument/intensity instructions are independent cues, not
// musical boundaries. Unrecognized speech deliberately returns false.
bool isGuideUtilityCue(const juce::String &rawText);
std::optional<GuideUtilityCue> normalizeGuideUtilityCue(
    double cueTimeSeconds, const juce::String &rawText,
    std::optional<double> asrConfidence = {});
// Counts remain distinct from secondary instrument/intensity instructions.
bool isGuideCountCue(const juce::String &rawText);
// Individual counts 1..7 use their existing numeric guide key. A compound,
// generic count-in or unsupported number stays one literal reviewable point;
// this API cannot infer word timestamps or generate a sequence of clips.
std::optional<GuideUtilityCue> normalizeGuideCountCue(
    double cueTimeSeconds, const juce::String &rawText,
    std::optional<double> asrConfidence = {});
// Never assumes a beat at zero, meter, constant tempo, or a filename role.
bool validateMusicStructure(const MusicStructureAnalysis &analysis,
                            double durationSeconds, juce::String &error);
// Reviewed/edited suggestions are protected: explicitly clear them to re-fuse.
bool fuseSectionSuggestions(SectionDetectionResult &result,
                            const FusionOptions &options, juce::String &error,
                            const CancellationCheck &cancelled = {});

struct SectionAcceptanceResult {
  SongArrangementSnapshot arrangement;
  juce::StringArray addedSuggestionIds;
  juce::StringArray conflictingSuggestionIds;
  juce::String error;
};
// Existing parts (including their source maps/automation) and cues are immutable.
// Suggestions only fill unoccupied source-linear ranges. This is a plan for the
// editor's existing undo/persistence transaction, never a persistence operation.
SectionAcceptanceResult appendConfirmedSuggestions(
    const SongArrangementSnapshot &existing,
    const std::vector<SectionSuggestion> &selected,
    double durationSeconds);

// Conversion uses the original source sample rate, never the playback tempo.
std::optional<std::int64_t> secondsToSourceSamples(double seconds, double sampleRate);
std::optional<double> sourceSamplesToSeconds(std::int64_t sample, double sampleRate);
// The offset is explicit: source sample zero can be later than song time zero.
std::optional<double> sourceSampleToTimelineSeconds(std::int64_t sample,
                                                   double sampleRate,
                                                   double offsetSeconds);

juce::var sectionDetectionResultToVar(const SectionDetectionResult &result);
bool sectionDetectionResultFromVar(const juce::var &value,
                                   SectionDetectionResult &result,
                                   juce::String &error);
// Cache key includes content fingerprint, provider/algorithm versions, duration,
// and fusion parameters. The caller owns file I/O and cache eviction.
juce::String sectionDetectionCacheKey(const SectionDetectionResult &result,
                                      const FusionOptions &options);

} // namespace playworship::analysis
