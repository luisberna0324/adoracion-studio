#pragma once

#include "GuideTranscription.h"
#include "GuideSectionInterpretation.h"

namespace playworship::analysis {

struct GuideGridMark {
  double timeSeconds = 0.0;
  int barNumber = 1;
  int pulseNumber = 1;
  bool strong = true;
};

struct GuideCatalogSelection {
  juce::String announcementId;
  GuideCatalogChoice choice;
};

struct GuideAlignmentOptions {
  double originSeconds = 0.0;
  double bpm = 120.0;
  juce::String timeSignature = "4/4";
  SongTempoMapSnapshot tempoMap;
  // Semantic corrections are local to review. Raw speech and stable source
  // identities are retained regardless of the chosen musical interpretation.
  std::vector<GuideLearnedMapping> learnedMappings;
  std::vector<GuideCatalogSelection> catalogueSelections;
};

// A spoken instruction/count is a point, independent of musical boundaries.
// Instructions and counts retain their measured source position; an explicit
// visual correction is stored separately from the original spoken time.
// Individual counts use numeric names 1..7. A compound count without word
// timestamps remains one literal point; no missing pulses are synthesized.
// Its source onset remains available after a visual correction. For a compound
// with word intervals, rawTimeSeconds is the ASR interval's lower bound and
// timeSeconds is its sole supported grid pulse, not a claimed voice onset.
struct GuideSecondaryCueSuggestion {
  juce::String id, name, rawText;
  double rawTimeSeconds = 0.0, timeSeconds = 0.0;
  std::optional<double> asrConfidence;
  bool userEdited = false;
};

// A missing automatic musical entry is review state tied to its source ID.
// Secondary issues can be resolved by explicitly moving/discarding that cue;
// unrelated primary or structural errors remain blocking.
struct GuideEntryTimingIssue {
  juce::String id, rawText;
  double rawTimeSeconds = 0.0;
  bool secondary = false;
};

// Unresolved speech remains a point in the review, never an unknown musical
// section. Every selectable candidate belongs to the shared app catalogue.
struct GuidePendingAnnouncement {
  juce::String id, rawText;
  double startSeconds = 0.0, endSeconds = 0.0;
  std::vector<GuideCatalogChoice> candidates;
};

struct GuideSourceAnnouncement {
  juce::String id, rawText;
  double startSeconds = 0.0, endSeconds = 0.0;
  std::optional<double> confidence;
};

struct GuideAlignmentDraft {
  GuideAlignmentOptions options;
  double durationSeconds = 0.0;
  std::vector<GuideGridMark> grid;
  std::vector<SectionSuggestion> suggestions;
  std::vector<GuideSecondaryCueSuggestion> secondaryCues;
  std::vector<GuideEntryTimingIssue> timingIssues;
  std::vector<GuidePendingAnnouncement> pendingAnnouncements;
  std::vector<GuideSourceAnnouncement> sourceAnnouncements;
  juce::String error;
};

// Source time only; output is transient and never changes a song or its tempo.
// Main sections always start at the bar following the announcement's bar,
// after a bounded onset-boundary tolerance. Only explicit visual editing can
// override that proposed entry. Independent instructions/counts keep their
// measured timing instead of inheriting a musical section's anticipation.
GuideAlignmentDraft alignGuideSections(const GuideTranscript &,
                                      const GuideAlignmentOptions &);

// Initial review only: start at the first spoken occurrence and conservatively
// refine its pulse phase with independent measured counts. Never call this
// when rebuilding an explicitly dragged origin. No transcript time is changed.
GuideAlignmentOptions makeInitialGuideAlignmentOptions(
    const GuideTranscript &, GuideAlignmentOptions musicalContext = {});

// A local boundary edit keeps neighbouring sections contiguous. Moving the
// grid origin instead rebuilds the entire draft through alignGuideSections.
bool moveGuideSectionBoundary(GuideAlignmentDraft &, const juce::String &id,
                              double seconds, bool snapToBars,
                              juce::String &error);

// Secondary points never alter section boundaries. An instruction's proposed
// musical entry may be adjusted independently of its original spoken time.
// Optional snapping uses the draft's actual pulses; Alt/free keeps exact time.
bool moveGuideSecondaryCue(GuideAlignmentDraft &, const juce::String &id,
                            double seconds, bool snapToBeats,
                            juce::String &error);

// Structured timing issues are reviewable; invalid source/grid/annotation errors
// are not. Acceptance remains blocked while any timing issue survives.
bool canReviewGuideAlignment(const GuideAlignmentDraft &);

// Discard only this secondary point and its own timing issue, preserving all
// other unresolved entries. Does not alter section boundaries or source audio.
bool discardGuideSecondaryCue(GuideAlignmentDraft &, const juce::String &id,
                              juce::String &error);

// Explicit role-aware catalogue selection, retaining the source identity and
// existing manual edits. The chosen role determines its musical timing.
bool chooseGuideCatalogMark(GuideAlignmentDraft &, const juce::String &id,
                            const GuideCatalogChoice &, juce::String &error);

bool discardGuidePendingAnnouncement(GuideAlignmentDraft &, const juce::String &id,
                                     juce::String &error);

// Pure atomic plan for both families. Existing sections/cues remain immutable;
// new cues use [0, duration), existing cue capacity and the final section anchors.
SectionAcceptanceResult appendConfirmedGuideSuggestions(
    const SongArrangementSnapshot &existing, const GuideAlignmentDraft &);

} // namespace playworship::analysis
