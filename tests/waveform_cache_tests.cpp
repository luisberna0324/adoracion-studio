#include "ui/WaveformCacheWindow.h"
#include <cstdlib>
#include <iostream>

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

int main() {
  using namespace playworship::ui;
  for (double width : {705.0, 735.5, 1033.0, 2340.0}) {
    for (double zoom : {1.0, 0.25, 1.0 / 18.0}) {
      WaveformCacheWindow previous;
      int rebuilds = 0;
      for (int frame = 0; frame <= 10000; ++frame) {
        const double start = (1.0 - zoom) * frame / 10000.0;
        if (previous.width != 0 && start >= previous.start &&
            start + zoom <= previous.start + previous.span)
          continue;
        const auto cache = makeWaveformCacheWindow(start, zoom, width);
        expect(cache.start <= start + 1e-12 &&
                   cache.start + cache.span >= start + zoom - 1e-12,
               "cache must cover the viewport, including both song ends");
        const double pixelsPerRatio = width / zoom;
        const double originColumn = cache.start * pixelsPerRatio / 2.0;
        expect(std::abs(originColumn - std::round(originColumn)) < 1e-8,
               "rebuild must preserve the globally anchored sampling grid");
        if (previous.width != 0) {
          // A source transient must keep the same screen X across cache swaps.
          const double transient = start + zoom * 0.43;
          const auto screenX = [&](const WaveformCacheWindow &c) {
            const double pixel = (transient - c.start) / c.span * c.width;
            const double sourceX = (start - c.start) / c.span * c.width;
            return (pixel - sourceX) * width / (zoom / c.span * c.width);
          };
          expect(std::abs(screenX(cache) - screenX(previous)) < 1e-8,
                 "rebuilding a cache cannot move a waveform transient");
          expect(std::abs(cache.span / cache.width - previous.span / previous.width) < 1e-12,
                 "overlapping caches must sample identical source intervals");
        }
        previous = cache;
        ++rebuilds;
      }
      if (zoom == 0.25)
        expect(rebuilds <= 6, "forward follow must reuse its overscan across frames");
    }
  }
  expect(makeWaveformCacheWindow(0, 0, 700).width == 0,
         "zero span must not generate an invalid image");
  std::cout << "waveform cache tests passed\n";
}
