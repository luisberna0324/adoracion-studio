#pragma once

#include <JuceHeader.h>
#include "DynamicGuideTiming.h"
#include "../data/SongData.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace song_mix_export {

/**
 * Maps a finite source-song range onto the rendered/output timeline.
 *
 * Tempo ratio follows the engine convention: source frames consumed per
 * rendered frame. Integer source boundaries are distributed cumulatively so
 * the last output block consumes the source range exactly once.
 */
class Timeline {
public:
  Timeline(const double sampleRateToUse, const double sourceStartSecondsToUse,
           const std::int64_t sourceFramesToUse,
           const double tempoRatioToUse) noexcept
      : sampleRate(std::isfinite(sampleRateToUse) && sampleRateToUse > 0.0
                       ? sampleRateToUse
                       : 44100.0),
        sourceStartSeconds(std::isfinite(sourceStartSecondsToUse)
                               ? std::max(0.0, sourceStartSecondsToUse)
                               : 0.0),
        sourceFrames(std::max<std::int64_t>(0, sourceFramesToUse)),
        tempoRatio(normalizeTempoRatio(tempoRatioToUse)),
        outputFrames(calculateOutputFrames(sourceFrames, tempoRatio)) {}

  static double normalizeTempoRatio(const double ratio) noexcept {
    return std::clamp(std::isfinite(ratio) ? ratio : 1.0, 0.75, 2.0);
  }

  std::int64_t getSourceFrames() const noexcept { return sourceFrames; }
  std::int64_t getOutputFrames() const noexcept { return outputFrames; }
  double getTempoRatio() const noexcept { return tempoRatio; }

  std::int64_t sourceFrameAtOutputBoundary(
      const std::int64_t outputFrame) const noexcept {
    if (sourceFrames <= 0 || outputFrames <= 0)
      return 0;
    const auto clamped =
        std::clamp<std::int64_t>(outputFrame, 0, outputFrames);
    if (clamped == outputFrames)
      return sourceFrames;
    return static_cast<std::int64_t>(std::llround(
        (static_cast<long double>(clamped) *
         static_cast<long double>(sourceFrames)) /
        static_cast<long double>(outputFrames)));
  }

  double sourceSecondsAtOutputBoundary(
      const std::int64_t outputFrame) const noexcept {
    if (sourceFrames <= 0 || outputFrames <= 0)
      return sourceStartSeconds;
    const auto clamped =
        std::clamp<std::int64_t>(outputFrame, 0, outputFrames);
    const long double sourceOffsetFrames =
        (static_cast<long double>(clamped) *
         static_cast<long double>(sourceFrames)) /
        static_cast<long double>(outputFrames);
    return sourceStartSeconds +
           static_cast<double>(sourceOffsetFrames / sampleRate);
  }

  double playbackSecondsAtOutputBoundary(
      const std::int64_t outputFrame) const noexcept {
    return sourceSecondsAtOutputBoundary(outputFrame) / tempoRatio;
  }

private:
  static std::int64_t
  calculateOutputFrames(const std::int64_t sourceFrameCount,
                        const double ratio) noexcept {
    if (sourceFrameCount <= 0)
      return 0;
    const long double exact =
        static_cast<long double>(sourceFrameCount) /
        static_cast<long double>(ratio);
    if (!std::isfinite(exact) ||
        exact >
            static_cast<long double>(
                std::numeric_limits<std::int64_t>::max())) {
      return 0;
    }
    return std::max<std::int64_t>(
        1, static_cast<std::int64_t>(std::llround(exact)));
  }

  double sampleRate = 44100.0;
  double sourceStartSeconds = 0.0;
  std::int64_t sourceFrames = 0;
  double tempoRatio = 1.0;
  std::int64_t outputFrames = 0;
};

} // namespace song_mix_export

struct StemMixExportStemState {
  juce::String filePath;
  float volume = 1.0f;
  float gain = 1.0f;
  bool muted = false;
  bool soloed = false;
  bool clickGuide = false;
  bool pitchShiftable = false;
  // Stable song stem identity; automation never relies on file/array order.
  juce::String stemId;
};

struct SongMixExportVirtualSourceState {
  bool enabled = false;
  float volume = 1.0f;
  float gain = 1.0f;
  bool muted = false;
  bool soloed = false;
};

struct SongMixExportGuideSection {
  juce::String name;
  double startSeconds = 0.0;
  bool utilityCue = false;
};

namespace song_mix_export {

struct PreparedGuideTrigger {
  juce::String name;
  double triggerSeconds = 0.0;
};

inline std::vector<PreparedGuideTrigger> prepareGuideTriggers(
    const std::vector<SongMixExportGuideSection> &sections,
    const double oneBarLeadSourceSeconds) {
  std::vector<PreparedGuideTrigger> prepared;
  prepared.reserve(sections.size());
  const double finiteLead =
      std::isfinite(oneBarLeadSourceSeconds)
          ? std::max(0.0, oneBarLeadSourceSeconds)
          : 0.0;
  for (const auto &section : sections) {
    if (!std::isfinite(section.startSeconds))
      continue;
    const bool triggerAtEntry =
        playworship::audio::dynamicGuideAnnouncementTiming(
            section.name, section.name, section.utilityCue) ==
        playworship::audio::DynamicGuideAnnouncementTiming::atSectionEntry;
    prepared.push_back(
        {section.name,
         std::max(0.0, section.startSeconds -
                           (triggerAtEntry ? 0.0 : finiteLead))});
  }
  std::stable_sort(
      prepared.begin(), prepared.end(),
      [](const PreparedGuideTrigger &lhs,
         const PreparedGuideTrigger &rhs) {
        return lhs.triggerSeconds < rhs.triggerSeconds;
      });
  return prepared;
}

} // namespace song_mix_export

struct SongMixExportRequest {
  enum class Format { Wav, Mp3, M4a };
  juce::File outputFile;
  Format format = Format::Wav;
  double sampleRate = 44100.0;
  double startSeconds = 0.0;
  double endSeconds = 0.0;
  double fadeOutSeconds = 0.0;
  int pitchSemiTones = 0;
  double tempoRatio = 1.0;
  double playbackBpm = 120.0;
  float masterVolume = 1.0f;
  float masterGain = 1.0f;
  bool masterMuted = false;
  SongMixExportVirtualSourceState dynamicClick;
  int clickDivisionMask = 1;
  int beatsPerBar = 4;
  juce::String clickStyle = "Classic";
  SongMixExportVirtualSourceState dynamicGuide;
  juce::String guideLanguage = "es";
  std::vector<SongMixExportGuideSection> guideSections;
  std::vector<StemMixExportStemState> stems;
  // Committed arrangement only. Stem values above are the unmodified base mix.
  SongArrangementSnapshot arrangement;
};

class SongMixExporter {
public:
  struct Result {
    bool ok = false;
    juce::String error;
    juce::File outputFile;
    double durationSeconds = 0.0;
  };

  // Runs off the message thread. The callback receives rendering progress.
  static Result exportToWav(const SongMixExportRequest &request,
                            const std::function<void(double)> &onProgress);
};
