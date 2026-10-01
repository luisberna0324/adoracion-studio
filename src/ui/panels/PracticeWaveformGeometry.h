#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

// The waveform belongs to the song timeline. Scrolling translates its envelope;
// it must never regroup the source bins around the current viewport position.
class PracticeWaveformGeometry {
public:
  struct Bar {
    // Kept for callers inspecting the stable samples. These are envelope
    // points, connected into a filled shape rather than separate bars.
    double timeSeconds = 0.0;
    float level = 0.0f;
    float halfHeight = 0.0f;
  };

  struct VisibleRange {
    size_t begin = 0;
    size_t end = 0;
  };

  // Call only when the selected instrument's bins change, including a new
  // asynchronous delivery. configure() and paint() do not inspect source bins
  // while the geometry and source remain unchanged.
  void setBins(const std::vector<float> &bins) {
    sourceBins = bins;
    sourceChanged = true;
  }

  void clear() {
    sourceBins.clear();
    bars.clear();
    envelopeTiles.clear();
    invalidateVisibleEnvelope();
    sourceChanged = true;
  }

  // Returns true when the cache was rebuilt. Width and height are in JUCE
  // logical points. Sample intervals are anchored at song time zero and
  // represent three horizontal points at this scale. A fixed multiple of that
  // grid avoids exceeding the source resolution or 65536 envelope points.
  bool configure(double lengthSeconds, double visibleSpanSeconds, float width,
                 float height) {
    lengthSeconds = finiteNonNegative(lengthSeconds);
    visibleSpanSeconds = finiteNonNegative(visibleSpanSeconds);
    width = static_cast<float>(finiteNonNegative(width));
    height = static_cast<float>(finiteNonNegative(height));
    visibleSpanSeconds = juce::jmin(lengthSeconds, visibleSpanSeconds);

    if (!sourceChanged && lengthSeconds == length &&
        visibleSpanSeconds == span && width == configuredWidth &&
        height == configuredHeight)
      return false;

    length = lengthSeconds;
    span = visibleSpanSeconds;
    configuredWidth = width;
    configuredHeight = height;
    sourceChanged = false;
    bars.clear();
    envelopeTiles.clear();
    invalidateVisibleEnvelope();
    secondsPerPoint = 0.0;
    if (sourceBins.empty() || length <= 0.0 || span <= 0.0 || width <= 0.0 ||
        height <= 0.0)
      return true;

    secondsPerPoint = span / width;
    const double gridInterval = barSpacingPoints * secondsPerPoint;
    const double requestedCount = std::ceil(length / gridInterval);
    if (!std::isfinite(requestedCount))
      return true;
    const auto maximumPoints = juce::jmin(sourceBins.size(), maximumEnvelopePoints);
    const double stride = juce::jmax(
        1.0, std::ceil(requestedCount / static_cast<double>(maximumPoints)));
    const double interval = gridInterval * stride;
    if (!std::isfinite(interval) || interval <= 0.0)
      return true;
    const auto count = juce::jmin(
        maximumPoints, static_cast<size_t>(std::ceil(length / interval)));
    bars.reserve(count);
    const double binsPerSecond = sourceBins.size() / length;
    const float maximumHalfHeight = height * 0.46f;
    for (size_t index = 0; index < count; ++index) {
      const double from = static_cast<double>(index) * interval;
      const double to = juce::jmin(length, from + interval);
      const size_t first = juce::jmin(
          sourceBins.size() - 1,
          static_cast<size_t>(std::floor(from * binsPerSecond)));
      const size_t last = juce::jlimit(
          first + 1, sourceBins.size(),
          static_cast<size_t>(std::ceil(to * binsPerSecond)));
      float level = 0.0f;
      for (size_t bin = first; bin < last; ++bin) {
        const float sample = sourceBins[bin];
        if (std::isfinite(sample))
          level = juce::jmax(level, std::abs(sample));
      }
      level = juce::jlimit(0.0f, 1.0f, level);
      bars.push_back(
          {from, level, juce::jmax(1.0f, level * maximumHalfHeight)});
    }
    buildEnvelopeTiles();
    // clear() retains JUCE Path storage. Reserve for every visible tile plus
    // its neighbours so paint never grows our path buffer while scrolling.
    const double maximumVisibleSpan = juce::jmin(length, juce::jmax(span, 48.0));
    const int maximumVisibleTiles = static_cast<int>(
        std::ceil(maximumVisibleSpan / (interval * tileIntervals))) + 3;
    visibleEnvelope.preallocateSpace(maximumVisibleTiles * coordinatesPerTile);
    return true;
  }

  const std::vector<Bar> &getBars() const { return bars; }

  VisibleRange visibleRange(const double currentSeconds,
                            const double visibleSpanSeconds = 0.0) const {
    if (bars.empty() || !std::isfinite(currentSeconds))
      return {};
    const double shownSpan = displaySpan(visibleSpanSeconds);
    const double start = currentSeconds - shownSpan * 0.5;
    const double end = currentSeconds + shownSpan * 0.5;
    if (end <= 0.0 || start >= length)
      return {};
    const auto byTime = [](const Bar &bar, const double time) {
      return bar.timeSeconds < time;
    };
    const auto first = std::lower_bound(bars.begin(), bars.end(), start, byTime);
    const auto last = std::lower_bound(first, bars.end(), end, byTime);
    const size_t begin = static_cast<size_t>(first - bars.begin());
    const size_t finish = static_cast<size_t>(last - bars.begin());
    // The connecting curve starts before the viewport and ends after it.
    // Include those neighbours so a partial interval cannot leave an edge gap.
    return {begin > 0 ? begin - 1 : 0,
            juce::jmin(bars.size(), finish + 1)};
  }

  float positionForBar(const Bar &bar, const juce::Rectangle<float> rectangle,
                       const double currentSeconds,
                       const double visibleSpanSeconds = 0.0) const {
    return rectangle.getCentreX() +
           static_cast<float>((bar.timeSeconds - currentSeconds) *
                              rectangle.getWidth() / displaySpan(visibleSpanSeconds));
  }

  // Uses the caller's current colour. Only visible cached tiles are visited;
  // their reusable union path changes when a tile enters or leaves the view.
  // Baseline and fixed playhead belong to the panel.
  void paint(juce::Graphics &g, const juce::Rectangle<float> rectangle,
             const double currentSeconds,
             const double visibleSpanSeconds = 0.0) const {
    if (rectangle.isEmpty() || span <= 0.0)
      return;
    const double shownSpan = displaySpan(visibleSpanSeconds);
    const auto visible = visibleRange(currentSeconds, shownSpan);
    if (visible.begin == visible.end || envelopeTiles.empty())
      return;
    const size_t firstTile = visible.begin / tileIntervals;
    const size_t endTile = juce::jmin(
        envelopeTiles.size(), (visible.end + tileIntervals - 1) / tileIntervals);
    if (firstTile != cachedFirstTile || endTile != cachedEndTile) {
      visibleEnvelope.clear();
      visibleEnvelope.setUsingNonZeroWinding(true);
      const double origin = envelopeTiles[firstTile].originSeconds;
      for (size_t index = firstTile; index < endTile; ++index)
        visibleEnvelope.addPath(
            envelopeTiles[index].path,
            juce::AffineTransform::translation(
                static_cast<float>((envelopeTiles[index].originSeconds - origin) /
                                   secondsPerPoint),
                0.0f));
      cachedFirstTile = firstTile;
      cachedEndTile = endTile;
    }
    const juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(rectangle.toNearestInt());
    const float scaleX = static_cast<float>(secondsPerPoint *
                                            rectangle.getWidth() / shownSpan);
    const float scaleY = rectangle.getHeight() / configuredHeight;
    const float offsetX = rectangle.getCentreX() + static_cast<float>(
        (envelopeTiles[firstTile].originSeconds - currentSeconds) *
        rectangle.getWidth() / shownSpan);
    // Tiles overlap by one interval. Filling their union once prevents either
    // anti-aliased seams or darker overlap bands when the caller uses alpha.
    g.fillPath(visibleEnvelope,
               juce::AffineTransform::scale(scaleX, scaleY)
                   .translated(offsetX, rectangle.getCentreY()));
  }

private:
  static double finiteNonNegative(const double value) {
    return std::isfinite(value) ? juce::jmax(0.0, value) : 0.0;
  }

  double displaySpan(const double requested) const {
    return std::isfinite(requested) && requested > 0.0
        ? juce::jmin(length, requested) : span;
  }

  struct EnvelopeTile {
    double originSeconds = 0.0;
    juce::Path path;
  };

  void invalidateVisibleEnvelope() {
    visibleEnvelope.clear();
    cachedFirstTile = cachedEndTile = std::numeric_limits<size_t>::max();
  }

  void buildEnvelopeTiles() {
    envelopeTiles.reserve((bars.size() + tileIntervals - 1) / tileIntervals);
    std::vector<juce::Point<float>> points;
    points.reserve(tileIntervals + 4);
    for (size_t coreStart = 0; coreStart < bars.size(); coreStart += tileIntervals) {
      const size_t first = coreStart > 0 ? coreStart - 1 : 0;
      const size_t last = juce::jmin(bars.size() - 1, coreStart + tileIntervals + 1);
      const double origin = bars[coreStart].timeSeconds;
      points.clear();
      for (size_t index = first; index <= last; ++index)
        points.push_back(
            {static_cast<float>((bars[index].timeSeconds - origin) / secondsPerPoint),
             bars[index].halfHeight});
      if (last == bars.size() - 1)
        points.push_back(
            {static_cast<float>((length - origin) / secondsPerPoint),
             bars.back().halfHeight});

      juce::Path path;
      path.preallocateSpace(coordinatesPerTile);
      path.startNewSubPath(points.front().x, -points.front().y);
      for (size_t index = 1; index < points.size(); ++index)
        appendEnvelopeCurve(path, points[index - 1], points[index], -1.0f);
      path.lineTo(points.back().x, points.back().y);
      for (size_t index = points.size() - 1; index > 0; --index)
        appendEnvelopeCurve(path, points[index], points[index - 1], 1.0f);
      path.closeSubPath();
      envelopeTiles.push_back({origin, std::move(path)});
    }
  }

  static void appendEnvelopeCurve(juce::Path &path, const juce::Point<float> from,
                                  const juce::Point<float> to, const float sign) {
    const float third = (to.x - from.x) / 3.0f;
    // Horizontal tangents retain every actual peak and stay between adjacent
    // amplitudes, without spline overshoot or the gaps of discrete columns.
    path.cubicTo(from.x + third, from.y * sign, to.x - third, to.y * sign,
                 to.x, to.y * sign);
  }

  static constexpr double barSpacingPoints = 3.0;
  static constexpr size_t maximumEnvelopePoints = 65536;
  static constexpr size_t tileIntervals = 64;
  static constexpr int coordinatesPerTile = 14 * (tileIntervals + 4) + 8;
  std::vector<float> sourceBins;
  std::vector<Bar> bars;
  std::vector<EnvelopeTile> envelopeTiles;
  mutable juce::Path visibleEnvelope;
  mutable size_t cachedFirstTile = std::numeric_limits<size_t>::max();
  mutable size_t cachedEndTile = std::numeric_limits<size_t>::max();
  double length = 0.0;
  double span = 0.0;
  double secondsPerPoint = 0.0;
  float configuredWidth = 0.0f;
  float configuredHeight = 0.0f;
  bool sourceChanged = true;
  friend struct PracticeWaveformGeometryTestAccess;
};
