#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>
#include <utility>

class StereoVUMeter : public juce::Component, private juce::Timer {
public:
  struct Layout {
    int scaleWidth = 14;
    int gap = 3;
    int columnWidth = 20;
    int peakLedSize = 7;
    int trackPadding = 4;
    int segmentWidth = 12;
    int segmentHeight = 4;
    int segmentGap = 1;
    int segments = 30;
    int getTrackHeight() const {
      return (segments * segmentHeight) + ((segments - 1) * segmentGap) +
             (trackPadding * 2);
    }
    int getBlockWidth() const { return scaleWidth + gap + columnWidth; }
    int getBlockHeight() const { return peakLedSize + gap + getTrackHeight(); }

    static Layout createCompact() {
      Layout l;
      l.segmentWidth = 10;
      l.segmentHeight = 3;
      // Make column width a bit smaller to fit the 10px segments:
      l.columnWidth = 16;
      return l;
    }
  };

  using LevelProvider = std::function<std::pair<float, float>()>;
  using MuteProvider = std::function<bool()>;

  StereoVUMeter(LevelProvider levelProvider, MuteProvider muteProvider,
                bool isCompact = false);
  void paint(juce::Graphics &g) override;

  const Layout &getLayout() const { return layout; }

private:
  Layout layout;
  LevelProvider levels;
  MuteProvider isMuted;

  float levelL = 0.0f;
  double holdUntilL = 0.0;

  void timerCallback() override;
};
