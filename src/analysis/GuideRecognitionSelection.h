#pragma once

#include "GuideTranscription.h"
#include <optional>
#include <vector>

namespace playworship::analysis {

// One literal hypothesis for the same local audio window. Scores come from
// Whisper's decoder, not a calibrated probability of a correct musical label.
struct GuideRecognitionCandidate {
  GuideTranscriptSegment segment;
  double avgLogProbability = -100.0;
  double noSpeechProbability = 1.0;
  int textTokenCount = 0;
};

// Keep the winning hypothesis's text, language and source times unchanged.
// The returned alternatives contain every valid evaluated hypothesis,
// including the winner, so both passes' original evidence remains reviewable.
std::optional<GuideTranscriptSegment> selectGuideRecognitionCandidate(
    const std::vector<GuideRecognitionCandidate> &candidates);

} // namespace playworship::analysis
