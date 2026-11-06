#pragma once

#include "GuideRepetitionMatcher.h"
#include "GuideSectionInterpretation.h"
#include "GuideSpeechSegmentation.h"

namespace playworship::analysis {

inline bool parentSupportsSection(const GuideTranscriptSegment &parent, const juce::String &section) {
  const auto namesSection = [&](const juce::String &text) {
    if (sameGuideDirectiveMeaning(section, text)) return true;
    for (const auto &part : juce::StringArray::fromTokens(text, ",;.!?", ""))
      if (sameGuideDirectiveMeaning(section, part.trim())) return true;
    return false;
  };
  if (namesSection(parent.text)) return true;
  for (const auto &candidate : parent.alternatives)
    if (namesSection(candidate.text)) return true;
  for (const auto &candidate : parent.catalogueAlternatives)
    if (namesSection(candidate.text)) return true;
  return false;
}

inline bool guideRepetitionEligible(const GuideTranscriptSegment &parent,
                                    const std::vector<GuideLearnedMapping> &mappings = {}) {
  if (!parent.acousticAnnouncements.empty() || parent.acousticResidualRatio ||
      parent.text.trim().isEmpty() || parent.text.length() > 512) return false;
  const auto hasCounts = [](const juce::String &text) {
    return guideCompoundCount(text).has_value() || normalizeGuideCountCue(0.0, text).has_value();
  };
  if (hasCounts(parent.text) || std::any_of(parent.alternatives.begin(), parent.alternatives.end(),
      [&](const auto &alternative) { return hasCounts(alternative.text); })) return false;
  const auto interpretation = interpretGuideAnnouncement(parent, mappings);
  const auto state = interpretation.recognitionState();
  return state == GuideRecognitionState::unresolved || state == GuideRecognitionState::tentative ||
      interpretation.provenance == "compound-announcements";
}

// Evidence must describe a complete pending utterance and cite an independently
// recognized instruction in the same transcript. Neither pack matches nor
// previously recovered children can become references for a later match.
inline bool validGuideRepetition(const GuideTranscript &transcript, const GuideTranscriptSegment &parent) {
  const auto &parts = parent.repetitionAnnouncements;
  constexpr double rounding = 0.000001;
  constexpr double maximumUnexplainedEdge = 0.16;
  const auto duration = parent.endSeconds - parent.startSeconds;
  if (parts.empty() || parts.size() > 2 || !std::isfinite(duration) ||
      duration < .24 - rounding || duration > 5.0 + rounding || !guideRepetitionEligible(parent)) return false;
  if (const auto compound = guideCompoundDirectives(parent.text)) {
    if (int(parts.size()) != compound->phrases.size()) return false;
    for (int i = 0; i < compound->phrases.size(); ++i)
      if (!sameGuideDirectiveMeaning(compound->phrases[i], parts[size_t(i)].text)) return false;
  }
  double previousEnd = parent.startSeconds;
  unsigned references = 0;
  for (const auto &part : parts) {
    if (part.text.isEmpty() || part.text.length() > 128 || (part.language != "es" && part.language != "en") ||
        !std::isfinite(part.startSeconds) || !std::isfinite(part.endSeconds) ||
        part.startSeconds < previousEnd - rounding || part.startSeconds > previousEnd + maximumUnexplainedEdge ||
        part.endSeconds <= part.startSeconds || part.endSeconds > parent.endSeconds + rounding) return false;
    previousEnd = part.endSeconds;
    if (!part.referenceSegment) {
      if (part.correlation || !isExactGuideSectionDirective(part.text) || !parentSupportsSection(parent, part.text)) return false;
      continue;
    }
    if (*part.referenceSegment >= transcript.segments.size() || !part.correlation ||
        !std::isfinite(*part.correlation) || *part.correlation < guideRepetitionMinimumCorrelation ||
        *part.correlation > 1.0) return false;
    const auto &reference = transcript.segments[*part.referenceSegment];
    const auto referenceDuration = reference.endSeconds - reference.startSeconds;
    // An identical excerpt has the seed's duration, apart from the search
    // context that may be clipped from the two edges of the ASR window.
    if ((reference.startSeconds < parent.endSeconds && reference.endSeconds > parent.startSeconds) ||
        referenceDuration < .24 - rounding || referenceDuration > 3.0 + rounding ||
        part.endSeconds - part.startSeconds > referenceDuration + 2.0 / 16000.0 ||
        referenceDuration - (part.endSeconds - part.startSeconds) > maximumUnexplainedEdge + 2.0 / 16000.0 ||
        reference.text != part.text || reference.language != part.language || !isGuideRepetitionSeed(reference)) return false;
    ++references;
  }
  return references == 1 && previousEnd >= parent.endSeconds - maximumUnexplainedEdge;
}

inline juce::var guideRepetitionAnnouncementsToVar(const std::vector<GuideRepetitionAnnouncement> &parts) {
  juce::Array<juce::var> list;
  for (const auto &part : parts) {
    auto entry = juce::var(new juce::DynamicObject());
    auto &object = *entry.getDynamicObject();
    object.setProperty("text", part.text);
    object.setProperty("language", part.language);
    object.setProperty("startSeconds", part.startSeconds);
    object.setProperty("endSeconds", part.endSeconds);
    if (part.referenceSegment) object.setProperty("referenceSegment", int(*part.referenceSegment));
    if (part.correlation) object.setProperty("correlation", *part.correlation);
    list.add(std::move(entry));
  }
  return list;
}

} // namespace playworship::analysis
