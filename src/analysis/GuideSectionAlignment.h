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

struct GuideAlignmentOptions {
  double originSeconds = 0.0;
  double bpm = 120.0;
  juce::String timeSignature = "4/4";
  SongTempoMapSnapshot tempoMap;
  // Semantic corrections are local to review. Raw speech and stable source
  // identities are retained regardless of the chosen musical interpretation.
  std::vector<GuideLearnedMapping> learnedMappings;
};

// A spoken instruction/count is a point, independent of musical boundaries.
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

struct GuideAlignmentDraft {
  GuideAlignmentOptions options;
  double durationSeconds = 0.0;
  std::vector<GuideGridMark> grid;
  std::vector<SectionSuggestion> suggestions;
  std::vector<GuideSecondaryCueSuggestion> secondaryCues;
  juce::String error;
};

// Source time only; output is transient and never changes a song or its tempo.
// Main sections always start at the bar following the announcement's bar,
// after a bounded onset-boundary tolerance. Only explicit visual editing can
// override that proposed entry; secondary counts/instructions keep their onset.
GuideAlignmentDraft alignGuideSections(const GuideTranscript &,
                                      const GuideAlignmentOptions &);

// A local boundary edit keeps neighbouring sections contiguous. Moving the
// grid origin instead rebuilds the entire draft through alignGuideSections.
bool moveGuideSectionBoundary(GuideAlignmentDraft &, const juce::String &id,
                              double seconds, bool snapToBars,
                              juce::String &error);

// Secondary points never alter section boundaries or inherit anticipation.
// Optional snapping uses the draft's actual pulses; Alt/free keeps exact time.
bool moveGuideSecondaryCue(GuideAlignmentDraft &, const juce::String &id,
                            double seconds, bool snapToBeats,
                            juce::String &error);

// Pure atomic plan for both families. Existing sections/cues remain immutable;
// new cues use [0, duration), existing cue capacity and the final section anchors.
SectionAcceptanceResult appendConfirmedGuideSuggestions(
    const SongArrangementSnapshot &existing, const GuideAlignmentDraft &);

} // namespace playworship::analysis
