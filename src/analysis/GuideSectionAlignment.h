#pragma once

#include "GuideTranscription.h"

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
  // Target bar relative to the bar containing the spoken announcement.
  // One proposes the following bar; this remains visually reviewable.
  int anticipationBars = 1;
};

struct GuideAlignmentDraft {
  GuideAlignmentOptions options;
  double durationSeconds = 0.0;
  std::vector<GuideGridMark> grid;
  std::vector<SectionSuggestion> suggestions;
  juce::String error;
};

// Source time only; output is transient and never changes a song or its tempo.
GuideAlignmentDraft alignGuideSections(const GuideTranscript &,
                                      const GuideAlignmentOptions &);

// A local boundary edit keeps neighbouring sections contiguous. Moving the
// grid origin instead rebuilds the entire draft through alignGuideSections.
bool moveGuideSectionBoundary(GuideAlignmentDraft &, const juce::String &id,
                              double seconds, bool snapToBars,
                              juce::String &error);

} // namespace playworship::analysis
