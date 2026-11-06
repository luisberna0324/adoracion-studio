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

// Only resolved announcements can become automatic marks. Unknown and
// tentative meanings can still receive independent recognition evidence.
// reviewOnly protects conflicts and out-of-catalogue identities.
enum class GuideRecognitionState { unresolved, tentative, resolved, reviewOnly };

// A choice is generated from the shared catalogue, never from a song-specific
// correction. key is a canonical selectable name, including an ordinal when
// present (for example Verse 2, Build or 1); label is its Spanish display name.
// score only orders review choices. supported means the observed hypothesis
// satisfies the existing decoder gates; it is not a calibrated probability.
struct GuideCatalogChoice {
  GuideAnnouncementKind kind = GuideAnnouncementKind::unknown;
  SectionType type = SectionType::unknown;
  std::optional<int> ordinal;
  juce::String key, label;
  double score = 0.0;
  juce::String evidence;
  bool supported = false;
};

std::vector<GuideCatalogChoice> guideCatalogEntries();

// Returns every canonical section/allowed ordinal, utility, declared literal
// utility and individual count exactly once, with evidence-supported matches
// first. Unsupported choices remain available for explicit visual selection.
std::vector<GuideCatalogChoice> guideCatalogChoices(
    const GuideTranscriptSegment &,
    const std::vector<GuideLearnedMapping> &learnedMappings = {});

// Post-ASR interpretation used by both waveform alignment and corpus evaluation.
// Corrections to a count or an instruction must never become a musical boundary.
// The original transcript remains the only source of text, timing and identity.
struct GuideAnnouncementInterpretation {
  GuideAnnouncementKind kind = GuideAnnouncementKind::unknown;
  GuideSectionInterpretation section;
  juce::String secondaryName, provenance, reason;
  std::vector<GuideCatalogChoice> candidates;
  bool recognized() const noexcept;
  GuideRecognitionState recognitionState() const;
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
