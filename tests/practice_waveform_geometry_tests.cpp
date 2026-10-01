#include "ui/panels/PracticeWaveformGeometry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

struct PracticeWaveformGeometryTestAccess {
  static size_t tileCount(const PracticeWaveformGeometry &geometry) {
    return geometry.envelopeTiles.size();
  }

  static size_t visibleTileCount(const PracticeWaveformGeometry &geometry) {
    return geometry.cachedEndTile - geometry.cachedFirstTile;
  }

  static const void *tileStorage(const PracticeWaveformGeometry &geometry) {
    return geometry.envelopeTiles.data();
  }

  static const juce::Path &visiblePath(const PracticeWaveformGeometry &geometry) {
    return geometry.visibleEnvelope;
  }
};

namespace {
void expectGeometry(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void expectGeometryNear(const double actual, const double expected,
                        const char *message) {
  if (std::abs(actual - expected) > 0.0001) {
    std::cerr << "FAILED: " << message << " (actual " << actual << ", expected "
              << expected << ")\n";
    std::exit(1);
  }
}

void testStableShapeWhileScrolling() {
  PracticeWaveformGeometry geometry;
  std::vector<float> bins(1200, 0.05f);
  bins[440] = 0.8f;
  bins[445] = -0.4f;
  bins[450] = 0.3f;
  geometry.setBins(bins);
  expectGeometry(geometry.configure(120.0, 12.0, 300.0f, 200.0f),
                 "new waveform builds stable song geometry");
  const auto &bars = geometry.getBars();
  const auto snapshot = bars;
  const auto *storage = bars.data();
  const juce::Rectangle<float> rectangle{10.0f, 20.0f, 300.0f, 200.0f};
  const auto before = geometry.visibleRange(45.0);
  const auto after = geometry.visibleRange(45.01);
  expectGeometry(before.begin < before.end && after.begin < after.end,
                 "scrolling selects a nonempty visible range");
  expectGeometry(after.end - after.begin <= 104,
                 "painting visits only viewport points and connecting neighbours");
  for (size_t index = juce::jmax(before.begin, after.begin);
       index < juce::jmin(before.end, after.end); ++index) {
    const auto &bar = bars[index];
    const float oldX = geometry.positionForBar(bar, rectangle, 45.0);
    const float newX = geometry.positionForBar(bar, rectangle, 45.01);
    expectGeometryNear(newX - oldX, -0.25,
                       "each waveform bar translates by the same subpoint distance");
    expectGeometryNear(bar.level, snapshot[index].level,
                       "scrolling preserves each source amplitude");
    expectGeometryNear(bar.timeSeconds, snapshot[index].timeSeconds,
                       "scrolling preserves each bar's source time");
    if (index + 1 < bars.size())
      expectGeometryNear(
          geometry.positionForBar(bars[index + 1], rectangle, 45.01) - newX,
          3.0, "relative distances remain fixed while scrolling");
  }
  expectGeometry(!geometry.configure(120.0, 12.0, 300.0f, 200.0f) &&
                     geometry.getBars().data() == storage,
                 "unchanged layout reuses geometry rather than regrouping bins");
  expectGeometry(geometry.visibleRange(-100.0).begin ==
                     geometry.visibleRange(-100.0).end &&
                     geometry.visibleRange(500.0).begin ==
                         geometry.visibleRange(500.0).end,
                 "views outside the song contain no synthetic bars");
}

void testUsesSourceAmplitudesAndInvalidatesOnChange() {
  PracticeWaveformGeometry geometry;
  geometry.setBins({0.0f, -0.25f, 0.8f, 0.4f});
  geometry.configure(4.0, 4.0, 12.0f, 100.0f);
  const auto &bars = geometry.getBars();
  expectGeometry(bars.size() == 4,
                 "fixed scale groups bins into song-anchored intervals");
  expectGeometryNear(bars[0].level, 0.0, "silence keeps zero source amplitude");
  expectGeometryNear(bars[1].level, 0.25,
                     "negative source amplitude uses its magnitude");
  expectGeometryNear(bars[2].level, 0.8,
                     "source peak is preserved without viewport normalization");
  expectGeometryNear(bars[3].level, 0.4,
                     "different source regions retain their own amplitude");
  expectGeometryNear(bars[2].halfHeight, 36.8,
                     "bar height follows source amplitude and configured height");

  geometry.setBins({0.2f, 0.1f, 0.7f, 0.9f});
  expectGeometry(geometry.configure(4.0, 4.0, 12.0f, 100.0f),
                 "instrument waveform replacement invalidates cached geometry");
  expectGeometryNear(geometry.getBars()[0].level, 0.2,
                     "new instrument bins replace the prior amplitudes");

  expectGeometry(geometry.configure(4.0, 4.0, 6.0f, 100.0f),
                 "layout change rebuilds geometry at the new fixed scale");
  expectGeometry(geometry.getBars().size() == 2,
                 "wider source intervals aggregate peaks at the new scale");
  expectGeometryNear(geometry.getBars()[0].level, 0.2,
                     "first aggregate uses maximum source magnitude");
  expectGeometryNear(geometry.getBars()[1].level, 0.9,
                     "second aggregate uses maximum source magnitude");

  geometry.setBins({std::numeric_limits<float>::quiet_NaN(), -2.0f});
  geometry.configure(2.0, 2.0, 6.0f, 100.0f);
  expectGeometryNear(geometry.getBars()[0].level, 0.0,
                     "nonfinite bins do not corrupt geometry");
  expectGeometryNear(geometry.getBars()[1].level, 1.0,
                     "source amplitudes are clamped to normalized drawing range");

  geometry.clear();
  geometry.configure(2.0, 2.0, 6.0f, 100.0f);
  expectGeometry(geometry.getBars().empty(),
                 "clearing an instrument removes cached bars");
}

juce::Image renderEnvelope(const PracticeWaveformGeometry &geometry,
                           const double position, const double visibleSpan = 0.0) {
  juce::Image image(juce::Image::ARGB, 310, 200, true);
  juce::Graphics graphics(image);
  graphics.setColour(juce::Colours::white.withAlpha(0.6f));
  geometry.paint(graphics, {5.0f, 0.0f, 300.0f, 200.0f}, position, visibleSpan);
  return image;
}

void expectTranslatedEnvelope(const juce::Image &before, const juce::Image &after) {
  for (int y = 8; y < 192; ++y)
    for (int x = 10; x < 297; ++x) {
      const int oldAlpha = before.getPixelAt(x + 3, y).getAlpha();
      const int newAlpha = after.getPixelAt(x, y).getAlpha();
      expectGeometry(std::abs(oldAlpha - newAlpha) <= 2,
                     "scrolling translates the same envelope without changing its shape");
    }
}

void testContinuousEnvelopeAndTileCache() {
  PracticeWaveformGeometry geometry;
  std::vector<float> bins(600, 0.05f);
  for (size_t index = 0; index < bins.size(); ++index)
    if (index % 3 != 0)
      bins[index] = static_cast<float>(0.15 + (index % 11) * 0.075);
  geometry.setBins(bins);
  geometry.configure(60.0, 12.0, 300.0f, 200.0f);
  const auto *tileStorage = PracticeWaveformGeometryTestAccess::tileStorage(geometry);
  const auto before = renderEnvelope(geometry, 30.0);
  expectGeometry(PracticeWaveformGeometryTestAccess::visibleTileCount(geometry) <
                     PracticeWaveformGeometryTestAccess::tileCount(geometry),
                 "renderer traverses the visible tiles rather than the whole song");
  const auto cachedPath = PracticeWaveformGeometryTestAccess::visiblePath(geometry);
  renderEnvelope(geometry, 30.01);
  expectGeometry(cachedPath == PracticeWaveformGeometryTestAccess::visiblePath(geometry) &&
                     tileStorage == PracticeWaveformGeometryTestAccess::tileStorage(geometry),
                 "subpoint scrolling reuses cached source tiles and their union path");
  expectGeometry(!geometry.configure(60.0, 12.0, 300.0f, 200.0f),
                 "painting does not invalidate fixed source geometry");

  for (int x = 5; x < 305; ++x) {
    const int alpha = before.getPixelAt(x, 100).getAlpha();
    expectGeometry(alpha >= 151 && alpha <= 154,
                   "filled envelope has no bar gaps, tile seams or doubled alpha");
  }
  expectGeometry(before.getPixelAt(4, 100).getAlpha() == 0 &&
                     before.getPixelAt(305, 100).getAlpha() == 0,
                 "the envelope covers its viewport edges without drawing beyond the clip");
  const auto after = renderEnvelope(geometry, 30.12);
  expectTranslatedEnvelope(before, after);

  // This shift changes the first cached tile. Source shape must still be
  // identical, including curves crossing the boundary between two tiles.
  const auto crossingBefore = renderEnvelope(geometry, 36.70);
  const auto crossingAfter = renderEnvelope(geometry, 36.82);
  expectTranslatedEnvelope(crossingBefore, crossingAfter);
}

void testEnvelopeResolutionIsBounded() {
  PracticeWaveformGeometry geometry;
  geometry.setBins(std::vector<float>(70000, 0.5f));
  geometry.configure(10000.0, 1.0, 1024.0f, 200.0f);
  expectGeometry(geometry.getBars().size() <= 65536,
                 "long songs cannot allocate millions of envelope points");

  geometry.setBins(std::vector<float>(4, 0.5f));
  geometry.configure(60.0, 1.0, 1024.0f, 200.0f);
  expectGeometry(geometry.getBars().size() <= 4,
                 "the cache never invents resolution beyond the source bins");
}

void testZoomTransformsStableGeometry() {
  PracticeWaveformGeometry geometry;
  std::vector<float> bins(600, 0.05f);
  bins[300] = 0.9f;
  bins[305] = 0.4f;
  geometry.setBins(bins);
  geometry.configure(60.0, 2.0, 300.0f, 200.0f);
  const auto snapshot = geometry.getBars();
  const auto *storage = geometry.getBars().data();
  const auto *tiles = PracticeWaveformGeometryTestAccess::tileStorage(geometry);
  const juce::Rectangle<float> rectangle{5.0f, 0.0f, 300.0f, 200.0f};
  const auto &point = geometry.getBars()[305];
  const double nearX = geometry.positionForBar(point, rectangle, 30.0, 12.0);
  const double farX = geometry.positionForBar(point, rectangle, 30.0, 24.0);
  expectGeometryNear(farX - rectangle.getCentreX(),
                     (nearX - rectangle.getCentreX()) * 0.5,
                     "zoom scales envelope positions around the fixed playhead");
  const auto narrow = geometry.visibleRange(30.0, 2.0);
  const auto wide = geometry.visibleRange(30.0, 48.0);
  expectGeometry(wide.end - wide.begin > narrow.end - narrow.begin,
                 "zoom out selects more cached points without changing their sampling");
  renderEnvelope(geometry, 30.0, 2.0);
  renderEnvelope(geometry, 30.0, 48.0);
  renderEnvelope(geometry, 30.0, 12.0);
  expectGeometry(storage == geometry.getBars().data() &&
                     tiles == PracticeWaveformGeometryTestAccess::tileStorage(geometry) &&
                     snapshot.size() == geometry.getBars().size(),
                 "pinch zoom preserves all source geometry caches");
  for (size_t index = 0; index < snapshot.size(); ++index) {
    expectGeometryNear(geometry.getBars()[index].level, snapshot[index].level,
                       "zoom never regroups source amplitudes");
    expectGeometryNear(geometry.getBars()[index].timeSeconds,
                       snapshot[index].timeSeconds,
                       "zoom preserves the musical time of every peak");
  }
}
} // namespace

void runPracticeWaveformGeometryTests() {
  testStableShapeWhileScrolling();
  testUsesSourceAmplitudesAndInvalidatesOnChange();
  testContinuousEnvelopeAndTileCache();
  testEnvelopeResolutionIsBounded();
  testZoomTransformsStableGeometry();
}
