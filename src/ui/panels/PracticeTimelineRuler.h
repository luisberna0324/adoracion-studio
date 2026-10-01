#pragma once

#include "../../data/SongData.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

struct PracticeTimelineMark {
  double timeSeconds = 0.0;
  juce::String label;
  bool strong = false;
};

// Read-only musical reference. It uses source BPM, never the altered playback
// BPM. WaveformBar supplies the ordered live tempo map, including unsaved edits.
class PracticeTimelineRuler {
public:
  // Quantization uses every musical pulse, independently of which ruler marks
  // fit at the current zoom. Allowed endpoints are inclusive. An empty result
  // is represented by NaN, so a non-grid window edge is never used as a mark.
  static double snapToPulse(
      const double position, const double fallbackBpm,
      const juce::String &fallbackMeter,
      const SongTempoMapSnapshot &tempoMap = {},
      const juce::Range<double> allowed = {
          0.0, std::numeric_limits<double>::max()}) {
    const double from = std::max(0.0, allowed.getStart());
    const double to = allowed.getEnd();
    double closest = std::numeric_limits<double>::quiet_NaN();
    if (!std::isfinite(position) || !std::isfinite(allowed.getStart()) ||
        !std::isfinite(to) || to < from)
      return closest;
    if (to == 0.0)
      return 0.0;

    const auto base = rhythm(fallbackBpm, fallbackMeter);
    Rhythm previous = base;
    double phaseQuarters = 0.0;
    double cursor = 0.0;
    const auto appendSegment = [&](const double start, const double end,
                                   const Rhythm current) {
      if (end < start)
        return;
      if (previous.numerator != current.numerator ||
          previous.denominator != current.denominator)
        phaseQuarters = 0.0;
      const double quarterSeconds = 60.0 / current.bpm;
      const double quartersPerBar = current.numerator * 4.0 / current.denominator;
      const double pulseSeconds = quarterSeconds * 4.0 / current.denominator;
      const double origin = start - phaseQuarters * quarterSeconds;
      const double segmentFrom = std::max(from, start);
      const double segmentTo = std::min(to, end);
      if (segmentFrom <= segmentTo && std::isfinite(pulseSeconds) &&
          pulseSeconds > 0.0 && std::isfinite(origin)) {
        const double first = std::max(
            0.0, std::ceil((segmentFrom - origin) / pulseSeconds - epsilon));
        double last = std::floor((segmentTo - origin) / pulseSeconds + epsilon);
        // At a tempo/meter boundary, the following region owns the boundary.
        if (end < to - epsilon && origin + last * pulseSeconds >= end - epsilon)
          last -= 1.0;
        // The default unbounded window can overflow its final pulse index to
        // +infinity. It is still a valid upper clamp for the finite indices
        // adjacent to the requested position; no iteration visits that end.
        if (std::isfinite(first) && first <= last) {
          const double target = (std::clamp(position, segmentFrom, segmentTo) - origin) /
                                pulseSeconds;
          for (const double index : {std::clamp(std::floor(target), first, last),
                                     std::clamp(std::ceil(target), first, last)}) {
            const double candidate = origin + index * pulseSeconds;
            if (!std::isfinite(candidate) || candidate < from || candidate > to ||
                candidate < start - epsilon || candidate > end + epsilon)
              continue;
            const double distance = std::abs(candidate - position);
            const double bestDistance = std::abs(closest - position);
            if (!std::isfinite(closest) || distance < bestDistance - epsilon ||
                (std::abs(distance - bestDistance) <= epsilon && candidate > closest))
              closest = candidate;
          }
        }
      }

      // Preserve fractional pulse/bar phase through BPM changes; a meter
      // change starts a new bar, matching build() and the visible ruler.
      const double total = phaseQuarters + (end - start) / quarterSeconds;
      const double completed = std::floor(total / quartersPerBar + epsilon);
      if (std::isfinite(completed) && completed < maximumTickIndex)
        phaseQuarters = std::max(0.0, total - completed * quartersPerBar);
      previous = current;
    };

    for (const auto &region : tempoMap.regions) {
      if (!std::isfinite(region.startSec) || !std::isfinite(region.endSec) ||
          region.endSec <= cursor || region.startSec > to)
        continue;
      const double start = std::clamp(region.startSec, cursor, to);
      if (start > cursor)
        appendSegment(cursor, start, base);
      const double end = std::clamp(region.endSec, start, to);
      appendSegment(start, end,
                    rhythm(std::isfinite(region.bpm) && region.bpm > 0.0
                               ? region.bpm : base.bpm,
                           region.timeSignature.trim().isNotEmpty()
                               ? region.timeSignature : fallbackMeter));
      cursor = end;
      // An explicit new meter beginning exactly at the allowed endpoint can
      // contribute that boundary pulse, even though no later time is allowed.
      if (cursor >= to && region.endSec > to)
        break;
    }
    if (cursor < to)
      appendSegment(cursor, to, base);
    return closest;
  }

  static std::vector<PracticeTimelineMark> build(
      const juce::Range<double> visible, const float width,
      const double fallbackBpm, const juce::String &fallbackMeter,
      const SongTempoMapSnapshot &tempoMap = {}) {
    std::vector<PracticeTimelineMark> marks;
    if (!std::isfinite(visible.getStart()) || !std::isfinite(visible.getEnd()) ||
        !std::isfinite(width) || width <= 0.0f || visible.getEnd() <= 0.0 ||
        visible.getLength() <= 0.0)
      return marks;
    marks.reserve(static_cast<size_t>(std::min(4096.0, width / 4.0 + 8.0)));
    const double from = std::max(0.0, visible.getStart());
    const double to = visible.getEnd();
    const double pointsPerSecond = width / visible.getLength();
    const auto base = rhythm(fallbackBpm, fallbackMeter);
    Rhythm previous = base;
    std::int64_t barNumber = 1;
    double phaseQuarters = 0.0;
    double cursor = 0.0;
    const auto appendSegment = [&](const double start, const double end,
                                   const Rhythm current) {
      if (end <= start)
        return;
      const bool meterChanged = previous.numerator != current.numerator ||
                                previous.denominator != current.denominator;
      if (meterChanged) {
        if (phaseQuarters > epsilon)
          ++barNumber;
        phaseQuarters = 0.0;
      }
      const double quarterSeconds = 60.0 / current.bpm;
      const double quartersPerBar = current.numerator * 4.0 / current.denominator;
      const double pulseSeconds = quarterSeconds * 4.0 / current.denominator;
      const double barSeconds = quarterSeconds * quartersPerBar;
      const double origin = start - phaseQuarters * quarterSeconds;
      const double segmentFrom = std::max(from, start);
      const double segmentTo = std::min(to, end);
      if (segmentFrom <= segmentTo && std::isfinite(barSeconds) &&
          std::isfinite(pulseSeconds) && barSeconds > 0.0 && pulseSeconds > 0.0) {
        const double pixelsPerBar = barSeconds * pointsPerSecond;
        const bool showPulses = pulseSeconds * pointsPerSecond >= minimumPulseSpacing;
        const double requestedSkip = std::ceil(minimumMarkSpacing / pixelsPerBar);
        if (!std::isfinite(requestedSkip) || requestedSkip >= maximumTickIndex)
          return;
        const auto barSkip = static_cast<std::int64_t>(
            std::max(1.0, requestedSkip));
        const auto labelSkip = barSkip * static_cast<std::int64_t>(
            std::max(1.0, std::ceil(minimumLabelSpacing / pixelsPerBar / barSkip)));
        const double tickSeconds = showPulses ? pulseSeconds : barSeconds * barSkip;
        const double firstIndex = std::max(
            0.0, std::ceil((segmentFrom - origin) / tickSeconds - epsilon));
        if (std::isfinite(firstIndex) && firstIndex < maximumTickIndex) {
          auto tick = static_cast<std::int64_t>(firstIndex);
          for (; marks.size() < 4096; ++tick) {
            const double time = origin + static_cast<double>(tick) * tickSeconds;
            if (time > segmentTo + epsilon ||
                (time >= end - epsilon && end < to - epsilon))
              break;
            const bool strong = !showPulses || tick % current.numerator == 0;
            const double offsetBars = showPulses
                ? static_cast<double>(tick / current.numerator)
                : static_cast<double>(tick) * barSkip;
            if (offsetBars >= maximumTickIndex)
              break;
            const auto number = barNumber + static_cast<std::int64_t>(offsetBars);
            const auto label = strong && (number - 1) % labelSkip == 0
                ? juce::String(number) : juce::String{};
            if (!marks.empty() && std::abs(marks.back().timeSeconds - time) <= epsilon) {
              marks.back().strong = marks.back().strong || strong;
              if (label.isNotEmpty())
                marks.back().label = label;
            } else {
              marks.push_back({std::max(0.0, time), label, strong});
            }
          }
        }
      }
      const double total = phaseQuarters + (end - start) / quarterSeconds;
      const double completed = std::floor(total / quartersPerBar + epsilon);
      if (std::isfinite(completed) && completed < maximumTickIndex) {
        barNumber += static_cast<std::int64_t>(completed);
        phaseQuarters = std::max(0.0, total - completed * quartersPerBar);
      }
      previous = current;
    };

    // Normalized WaveformBar maps are sorted. First region wins any malformed
    // overlap; gaps use the song's base rhythm rather than inventing tempo data.
    for (const auto &region : tempoMap.regions) {
      if (!std::isfinite(region.startSec) || !std::isfinite(region.endSec) ||
          region.endSec <= cursor || region.startSec >= to)
        continue;
      const double start = std::clamp(region.startSec, cursor, to);
      if (start > cursor)
        appendSegment(cursor, start, base);
      const double end = std::clamp(region.endSec, start, to);
      appendSegment(start, end,
                    rhythm(std::isfinite(region.bpm) && region.bpm > 0.0
                               ? region.bpm : base.bpm,
                           region.timeSignature.trim().isNotEmpty()
                               ? region.timeSignature : fallbackMeter));
      cursor = end;
      if (cursor >= to)
        break;
    }
    if (cursor < to)
      appendSegment(cursor, to, base);

    // Two regions can have different tick spacing. Preserve the strongest
    // marks but suppress labels that would collide across a tempo boundary.
    double lastLabelTime = -std::numeric_limits<double>::infinity();
    for (auto &mark : marks) {
      if (mark.label.isEmpty())
        continue;
      if ((mark.timeSeconds - lastLabelTime) * pointsPerSecond < minimumLabelSpacing - epsilon)
        mark.label.clear();
      else
        lastLabelTime = mark.timeSeconds;
    }
    return marks;
  }

private:
  struct Rhythm {
    double bpm;
    int numerator;
    int denominator;
  };

  static Rhythm rhythm(const double bpm, const juce::String &meter) {
    const auto trimmed = meter.trim();
    const int slash = trimmed.indexOfChar('/');
    const int numerator = (slash > 0 ? trimmed.substring(0, slash) : trimmed).getIntValue();
    const int denominator = slash > 0 ? trimmed.substring(slash + 1).getIntValue() : 4;
    return {std::isfinite(bpm) && bpm > 0.0 ? bpm : 120.0,
            numerator > 0 ? numerator : 4, denominator > 0 ? denominator : 4};
  }

  static constexpr double epsilon = 0.0000001;
  static constexpr double minimumPulseSpacing = 10.0;
  static constexpr double minimumMarkSpacing = 8.0;
  static constexpr double minimumLabelSpacing = 72.0;
  static constexpr double maximumTickIndex = 1.0e15;
};
