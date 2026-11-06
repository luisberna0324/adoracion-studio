#pragma once

#include "GuideTranscription.h"

namespace playworship::analysis {

inline constexpr double guideRepetitionMinimumCorrelation = .94;

struct GuideRepetitionReference {
  juce::String key;
  std::vector<float> samples;
};

struct GuideRepetitionMatch {
  size_t referenceIndex = 0;
  int startSample = 0, endSample = 0;
  double correlation = 0.0;
};

// Both input and references are mono 16 kHz from this guide. Sample offsets
// are relative to the supplied window, which may include brief context around
// the pending announcement. The caller owns source bounds and ASR metadata.
std::optional<GuideRepetitionMatch> matchGuideRepetition(
    const std::vector<float> &targetPcm16k,
    const std::vector<GuideRepetitionReference> &references,
    const CancellationCheck &cancelled = {});

} // namespace playworship::analysis
