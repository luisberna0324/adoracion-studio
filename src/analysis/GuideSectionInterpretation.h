#pragma once

#include "GuideTranscription.h"

namespace playworship::analysis {

// Local corrections remember a section family, never an occurrence number.
// rawText is matched as a complete, bounded phrase after case/punctuation
// normalization; it is never substituted into the transcript or its identity.
struct GuideLearnedMapping {
  juce::String rawText;
  SectionType type = SectionType::unknown;
};

struct GuideSectionInterpretation {
  SectionType type = SectionType::unknown;
  std::optional<int> ordinal;
  juce::String provenance, reason;
  bool tentative = false;
  bool recognized() const noexcept;
};

inline constexpr const char *mergedGuideAnnouncementsSource = "anuncios de guía fusionados";
inline constexpr const char *tentativeGuideInterpretationSource = "interpretación léxica por revisar";

GuideSectionInterpretation interpretGuideSection(
    const GuideTranscriptSegment &,
    const std::vector<GuideLearnedMapping> &learnedMappings = {});

enum class GuideAnnouncementKind { unknown, section, instruction, count };

// Post-ASR interpretation used by both waveform alignment and corpus evaluation.
// Corrections to a count or an instruction must never become a musical boundary.
// The original transcript remains the only source of text, timing and identity.
struct GuideAnnouncementInterpretation {
  GuideAnnouncementKind kind = GuideAnnouncementKind::unknown;
  GuideSectionInterpretation section;
  juce::String secondaryName, provenance, reason;
  bool recognized() const noexcept;
};

GuideAnnouncementInterpretation interpretGuideAnnouncement(
    const GuideTranscriptSegment &,
    const std::vector<GuideLearnedMapping> &learnedMappings = {});

// Exactly the existing section-suggestion identity, based on original ASR
// evidence. Interpretation and local corrections cannot change this ID.
juce::String guideSectionSuggestionId(const GuideTranscript &,
                                     const GuideTranscriptSegment &);

struct GuideConfirmedSectionRename {
  juce::String suggestionId, displayName;
};

// Pure extraction for renames belonging to a successfully confirmed review.
// Fused/ambiguous suggestions, secondary cues, unmatched identities and custom
// names are deliberately not taught. This function performs no I/O.
std::vector<GuideLearnedMapping> guideSectionCorrectionsForConfirmedRenames(
    const GuideTranscript &, const std::vector<SectionSuggestion> &,
    const std::vector<GuideConfirmedSectionRename> &);

// Pass only suggestions actually added by a successful acceptance. In
// addition to explicit renames, this learns unedited tentative candidates the
// user accepted, including a candidate whose time was adjusted. Rejected,
// renamed-custom or merged candidates are not
// implicitly taught; merely opening/cancelling a review never calls this API.
std::vector<GuideLearnedMapping> guideSectionCorrectionsForConfirmedReview(
    const GuideTranscript &, const std::vector<SectionSuggestion> &,
    const std::vector<GuideConfirmedSectionRename> &);

// Missing files load as an empty vocabulary. Malformed/oversized files and
// unknown schemas fail closed, and merge/save will not overwrite them. Only
// the explicit merge/save operation writes; callers invoke it after acceptance.
bool loadGuideSectionLearnedMappings(const juce::File &,
                                    std::vector<GuideLearnedMapping> &,
                                    juce::String &error);
bool mergeAndSaveGuideSectionLearnedMappings(
    const juce::File &, const std::vector<GuideLearnedMapping> &corrections,
    juce::String &error);

} // namespace playworship::analysis
