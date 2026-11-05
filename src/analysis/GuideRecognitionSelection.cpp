#include "GuideRecognitionSelection.h"
#include "SectionDetection.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace playworship::analysis {
namespace {
bool valid(const GuideRecognitionCandidate &candidate) {
  const auto &segment = candidate.segment;
  return !segment.text.trim().isEmpty() &&
      (segment.language == "es" || segment.language == "en") &&
      std::isfinite(segment.startSeconds) && std::isfinite(segment.endSeconds) &&
      segment.startSeconds >= 0.0 && segment.endSeconds > segment.startSeconds &&
      std::isfinite(candidate.avgLogProbability) && candidate.avgLogProbability <= 0.0 &&
      candidate.avgLogProbability >= -100.0 && std::isfinite(candidate.noSpeechProbability) &&
      candidate.noSpeechProbability >= 0.0 && candidate.noSpeechProbability <= 1.0 &&
      candidate.textTokenCount >= 0;
}

double decoderScore(const GuideRecognitionCandidate &candidate) {
  return candidate.avgLogProbability - 0.5 * candidate.noSpeechProbability;
}

bool plausibleDirective(const GuideRecognitionCandidate &candidate) {
  // Vocabulary is only a small prior for reasonably supported short speech.
  // It must never make an unlikely label beat a clearly stronger hypothesis.
  return candidate.textTokenCount > 0 && candidate.avgLogProbability >= -1.2 &&
      candidate.noSpeechProbability <= 0.4 &&
      (normalizeGuideCue(0.0, candidate.segment.text).has_value() ||
       isGuideUtilityCue(candidate.segment.text));
}
} // namespace

std::optional<GuideTranscriptSegment> selectGuideRecognitionCandidate(
    const std::vector<GuideRecognitionCandidate> &candidates) {
  double bestDecoderScore = -std::numeric_limits<double>::infinity();
  for (const auto &candidate : candidates)
    if (valid(candidate)) bestDecoderScore = std::max(bestDecoderScore, decoderScore(candidate));
  std::optional<std::size_t> selected;
  double bestScore = -std::numeric_limits<double>::infinity();
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    const auto &candidate = candidates[index];
    if (!valid(candidate)) continue;
    const double acoustic = decoderScore(candidate);
    const double vocabularyPrior = acoustic >= bestDecoderScore - 0.2 && plausibleDirective(candidate)
        ? 0.15 : 0.0;
    const double score = acoustic + vocabularyPrior;
    if (!selected || score > bestScore) {
      selected = index;
      bestScore = score;
    }
  }
  if (!selected) return {};
  const auto &winner = candidates[*selected];
  auto result = winner.segment;
  result.confidence = winner.textTokenCount > 0
      ? std::optional<double>(std::exp(winner.avgLogProbability)) : std::nullopt;
  result.alternatives.clear();
  for (const auto &candidate : candidates) {
    if (!valid(candidate)) continue;
    result.alternatives.push_back({candidate.segment.text, candidate.segment.language,
                                  candidate.avgLogProbability, candidate.noSpeechProbability});
  }
  return result;
}
} // namespace playworship::analysis
