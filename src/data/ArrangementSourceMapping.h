#pragma once

#include "SongData.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace arrangement_source_mapping {

enum class BoundaryBias {
  right,
  left,
};

inline const SongArrangementPartSnapshot *findPartById(
    const std::vector<SongArrangementPartSnapshot> &parts,
    const juce::String &id) {
  const auto it = std::find_if(parts.begin(), parts.end(),
                               [&id](const SongArrangementPartSnapshot &part) {
                                 return part.id == id;
                               });
  return it == parts.end() ? nullptr : &*it;
}

inline const SongArrangementPartSnapshot *findOriginalSourcePart(
    const std::vector<SongArrangementPartSnapshot> &parts,
    const SongArrangementPartSnapshot &part) {
  const auto *source = &part;
  for (size_t depth = 0; depth < parts.size(); ++depth) {
    if (source->sourcePartId.isEmpty())
      break;
    const auto *next = findPartById(parts, source->sourcePartId);
    if (next == nullptr || next == source)
      break;
    source = next;
  }
  return source;
}

inline std::pair<double, double> clampRange(double start, double end,
                                             const double sourceLength) {
  start = juce::jlimit(0.0, sourceLength, start);
  end = juce::jlimit(0.0, sourceLength, end);
  if (end < start)
    std::swap(start, end);
  return {start, end};
}

inline std::pair<double, double> sourceRangeForOriginalPart(
    const std::vector<SongArrangementPartSnapshot> &parts,
    const SongArrangementPartSnapshot &part, const double sourceLength) {
  const auto *source = findOriginalSourcePart(parts, part);
  if (source == nullptr)
    return {0.0, 0.0};
  if (source->sourcePartId.isNotEmpty() &&
      source->sourceEndSec > source->sourceStartSec)
    return clampRange(source->sourceStartSec, source->sourceEndSec, sourceLength);
  return clampRange(source->startSec, source->endSec, sourceLength);
}

inline bool hasSourceRangeContainedInOriginal(
    const std::vector<SongArrangementPartSnapshot> &parts,
    const SongArrangementPartSnapshot &part, const double sourceLength) {
  if (part.sourcePartId.isEmpty() ||
      part.sourceEndSec <= part.sourceStartSec)
    return false;
  const auto *referencedPart = findPartById(parts, part.sourcePartId);
  if (referencedPart == nullptr || referencedPart == &part)
    return false;

  const auto [originalStart, originalEnd] =
      sourceRangeForOriginalPart(parts, part, sourceLength);
  constexpr double tolerance = 0.0001;
  return part.sourceStartSec >= originalStart - tolerance &&
         part.sourceEndSec <= originalEnd + tolerance;
}

inline bool mapArrangementSecondsToSourceSeconds(
    const std::vector<SongArrangementPartSnapshot> &parts,
    const double arrangementSeconds, const double sourceLength,
    double &sourceSecondsOut,
    const BoundaryBias boundaryBias = BoundaryBias::right) {
  if (sourceLength <= 0.0)
    return false;

  struct Segment {
    double start = 0.0;
    double end = 0.0;
    double sourceStart = 0.0;
    double sourceEnd = 0.0;
  };

  std::vector<Segment> segments;
  segments.reserve(parts.size());
  for (const auto &part : parts) {
    if (part.sourcePartId.isEmpty() || part.endSec <= part.startSec ||
        part.sourceEndSec <= part.sourceStartSec)
      continue;
    const auto [sourceStart, sourceEnd] =
        clampRange(part.sourceStartSec, part.sourceEndSec, sourceLength);
    if (sourceEnd > sourceStart)
      segments.push_back({part.startSec, part.endSec, sourceStart, sourceEnd});
  }

  std::stable_sort(segments.begin(), segments.end(),
                   [](const Segment &a, const Segment &b) {
                     return std::abs(a.start - b.start) <= 0.0001
                                ? a.end < b.end
                                : a.start < b.start;
                   });

  double insertedOffset = 0.0;
  for (const auto &segment : segments) {
    const bool atLeftBoundary =
        std::abs(arrangementSeconds - segment.start) <= 0.0001;
    if (arrangementSeconds < segment.start ||
        (boundaryBias == BoundaryBias::left && atLeftBoundary)) {
      sourceSecondsOut = juce::jlimit(0.0, sourceLength,
                                      arrangementSeconds - insertedOffset);
      return true;
    }
    if (arrangementSeconds < segment.end) {
      const double ratio = juce::jlimit(
          0.0, 1.0, (arrangementSeconds - segment.start) /
                         juce::jmax(0.0001, segment.end - segment.start));
      sourceSecondsOut = juce::jlimit(
          0.0, sourceLength,
          segment.sourceStart +
              (ratio * juce::jmax(0.0001, segment.sourceEnd - segment.sourceStart)));
      return true;
    }
    insertedOffset += segment.end - segment.start;
  }

  sourceSecondsOut =
      juce::jlimit(0.0, sourceLength, arrangementSeconds - insertedOffset);
  return true;
}

inline std::pair<double, double> resolveCopySourceRange(
    const std::vector<SongArrangementPartSnapshot> &parts,
    const SongArrangementPartSnapshot &part, const double sourceLength) {
  if (part.sourcePartId.isNotEmpty()) {
    if (hasSourceRangeContainedInOriginal(parts, part, sourceLength))
      return clampRange(part.sourceStartSec, part.sourceEndSec, sourceLength);
    // Do not propagate an explicit range that escapes the referenced source.
    if (findPartById(parts, part.sourcePartId) == nullptr)
      return {0.0, 0.0};
    return sourceRangeForOriginalPart(parts, part, sourceLength);
  }

  double sourceStart = 0.0;
  double sourceEnd = 0.0;
  if (mapArrangementSecondsToSourceSeconds(parts, part.startSec, sourceLength,
                                           sourceStart, BoundaryBias::right) &&
      mapArrangementSecondsToSourceSeconds(parts, part.endSec, sourceLength,
                                           sourceEnd, BoundaryBias::left))
    return clampRange(sourceStart, sourceEnd, sourceLength);
  return clampRange(part.startSec, part.endSec, sourceLength);
}

} // namespace arrangement_source_mapping
