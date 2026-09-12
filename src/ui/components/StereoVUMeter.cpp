#include "StereoVUMeter.h"

#include <cstdint>
#include <cmath>
#include <utility>

namespace {
float dbToNorm(float db) {
  return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

float linearToNorm(float linear) {
  const float db =
      juce::Decibels::gainToDecibels(juce::jmax(0.00001f, linear), -100.0f);
  return dbToNorm(db);
}

#if defined(MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS) &&                  \
    MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS
struct VuMeterRepaintDiagnostics {
  uint64_t timerTicks = 0;
  uint64_t repaintRequests = 0;
  uint64_t skippedTicks = 0;
  uint64_t paintCalls = 0;
  double nextLogMs = 0.0;
};

VuMeterRepaintDiagnostics &vuMeterRepaintDiagnostics() {
  static VuMeterRepaintDiagnostics diagnostics;
  return diagnostics;
}

void maybeLogVuMeterRepaintDiagnostics(const double nowMs) {
  auto &diagnostics = vuMeterRepaintDiagnostics();
  if (diagnostics.nextLogMs <= 0.0) {
    diagnostics.nextLogMs = nowMs + 2000.0;
    return;
  }

  if (nowMs < diagnostics.nextLogMs)
    return;

  juce::Logger::writeToLog(
      "VU repaint diagnostics: timerTicks=" +
      juce::String(static_cast<double>(diagnostics.timerTicks), 0) +
      " repaintRequests=" +
      juce::String(static_cast<double>(diagnostics.repaintRequests), 0) +
      " skippedTicks=" +
      juce::String(static_cast<double>(diagnostics.skippedTicks), 0) +
      " paintCalls=" +
      juce::String(static_cast<double>(diagnostics.paintCalls), 0));

  diagnostics.timerTicks = 0;
  diagnostics.repaintRequests = 0;
  diagnostics.skippedTicks = 0;
  diagnostics.paintCalls = 0;
  diagnostics.nextLogMs = nowMs + 2000.0;
}

void recordVuMeterPaint() {
  auto &diagnostics = vuMeterRepaintDiagnostics();
  ++diagnostics.paintCalls;
  maybeLogVuMeterRepaintDiagnostics(
      juce::Time::getMillisecondCounterHiRes());
}

void recordVuMeterTimerResult(const bool requestedRepaint,
                              const double nowMs) {
  auto &diagnostics = vuMeterRepaintDiagnostics();
  ++diagnostics.timerTicks;
  if (requestedRepaint)
    ++diagnostics.repaintRequests;
  else
    ++diagnostics.skippedTicks;
  maybeLogVuMeterRepaintDiagnostics(nowMs);
}
#endif
} // namespace

StereoVUMeter::StereoVUMeter(LevelProvider levelProvider,
                             MuteProvider muteProvider, bool isCompact)
    : layout(isCompact ? Layout::createCompact() : Layout{}),
      levels(std::move(levelProvider)), isMuted(std::move(muteProvider)) {
  // Android software-paints each meter and its transparent parent chain. With
  // one meter per stem, 30 Hz can starve the transport/waveform message timer.
#if JUCE_ANDROID
  startTimerHz(8);
#else
  // 30 Hz is plenty for VU visual smoothing; 60 Hz caused idle CPU spikes
  // when many VUs are present (one per stem).
  startTimerHz(30);
#endif
}

int StereoVUMeter::getVisibleSegmentCount() const noexcept {
  const float trackH = juce::jmax(
      0.0f, static_cast<float>(getHeight() - layout.peakLedSize - layout.gap));
  const float segmentAreaH =
      juce::jmax(0.0f, trackH - static_cast<float>(layout.trackPadding * 2));
  const float segmentGap = static_cast<float>(layout.segmentGap);
  const int fittedSegments = static_cast<int>(
      std::floor((segmentAreaH + segmentGap) / (1.0f + segmentGap)));

  return juce::jmax(6, juce::jmin(layout.segments, fittedSegments));
}

void StereoVUMeter::paint(juce::Graphics &g) {
#if defined(MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS) &&                  \
    MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS
  recordVuMeterPaint();
#endif

  auto bounds = getLocalBounds().toFloat();
  const float trackH =
      juce::jmax(0.0f, bounds.getHeight() - layout.peakLedSize - layout.gap);
  const auto scaleRect = juce::Rectangle<float>(
      bounds.getX(), bounds.getY() + layout.peakLedSize + layout.gap,
      static_cast<float>(layout.scaleWidth), trackH);
  const auto meterColRect =
      juce::Rectangle<float>(scaleRect.getRight() + layout.gap, bounds.getY(),
                             static_cast<float>(layout.columnWidth),
                             bounds.getHeight());

  struct Mark {
    float db;
    const char *label;
  };
  static constexpr Mark marks[] = {
      {0.0f, "0"}, {-12.0f, "-12"}, {-24.0f, "-24"}, {-48.0f, "-48"}};

  g.setFont(juce::FontOptions(6.0f));
  for (const auto &mark : marks) {
    const float y =
        scaleRect.getBottom() - (scaleRect.getHeight() * dbToNorm(mark.db));
    g.setColour(juce::String(mark.label) == "0"
                    ? UITheme::accent
                    : UITheme::textSecondary.withAlpha(0.45f));
    g.drawFittedText(mark.label, static_cast<int>(scaleRect.getX()),
                     static_cast<int>(y - 3.0f),
                     static_cast<int>(scaleRect.getWidth() - 1.0f), 6,
                     juce::Justification::centredRight, 1);
  }

  const auto drawColumn = [&g, this, trackH](
                              const juce::Rectangle<float> &column, float level,
                              bool peakOn) {
    const auto ledRect = juce::Rectangle<float>(
        column.getCentreX() - (layout.peakLedSize * 0.5f), column.getY(),
        static_cast<float>(layout.peakLedSize),
        static_cast<float>(layout.peakLedSize));
    if (peakOn) {
      g.setColour(UITheme::accent.withAlpha(0.33f));
      g.fillEllipse(ledRect.expanded(2.0f));
      g.setColour(UITheme::accent);
    } else {
      g.setColour(UITheme::textSecondary.withAlpha(0.22f));
    }
    g.fillEllipse(ledRect);

    auto track = juce::Rectangle<float>(
        column.getX(), column.getY() + layout.peakLedSize + layout.gap,
        static_cast<float>(layout.columnWidth), trackH);
    g.setColour(UITheme::textSecondary.withAlpha(0.12f));
    g.fillRoundedRectangle(track, 3.0f);

    auto segmentArea = track.reduced(static_cast<float>(layout.trackPadding));
    const float segmentGap = static_cast<float>(layout.segmentGap);
    const int segments = getVisibleSegmentCount();
    const float availableSegH =
        segmentArea.getHeight() - (segmentGap * (segments - 1));
    const float segmentH =
        juce::jmax(1.0f, availableSegH / static_cast<float>(segments));
    const int activeSegments =
        juce::jlimit(0, segments,
                     static_cast<int>(
                         std::round(level * static_cast<float>(segments))));

    for (int i = 0; i < segments; ++i) {
      const float y = segmentArea.getBottom() -
                      ((static_cast<float>(i + 1) * segmentH) +
                       (static_cast<float>(i) * segmentGap));
      const auto seg = juce::Rectangle<float>(
          segmentArea.getCentreX() - (layout.segmentWidth * 0.5f), y,
          static_cast<float>(layout.segmentWidth), segmentH);

      const bool isActive = i < activeSegments;
      const float zone =
          static_cast<float>(i + 1) / static_cast<float>(segments);

      juce::Colour active = UITheme::panelDark;
      juce::Colour inactive = UITheme::panelDark.withAlpha(0.06f);
      if (zone > 0.85f) {
        active = UITheme::accent;
        inactive = UITheme::accent.withAlpha(0.13f);
      } else if (zone > 0.65f) {
        active = UITheme::accentSoft;
        inactive = UITheme::accentSoft.withAlpha(0.10f);
      }

      g.setColour(isActive ? active : inactive);
      g.fillRoundedRectangle(seg, 1.0f);
    }
  };

  const double now = juce::Time::getMillisecondCounterHiRes();
  drawColumn(meterColRect, levelL, now < holdUntilL);
}

void StereoVUMeter::resized() {
  lastPaintedActiveSegments = -1;
  lastPaintedPeakOn = false;
}

void StereoVUMeter::timerCallback() {
  float inputL = 0.0f;
  float inputR = 0.0f;

  if (levels) {
    const auto lr = levels();
    inputL = lr.first;
    inputR = lr.second;
  }

  if (isMuted && isMuted()) {
    inputL = 0.0f;
    inputR = 0.0f;
  }

  const float targetL = linearToNorm(juce::jmax(inputL, inputR));

  const auto smooth = [](float current, float target) {
    const float coeff = target > current ? 0.2f : 0.06f;
    const float next = current + (target - current) * coeff;
    return next < 0.0005f ? 0.0f : next;
  };

  levelL = smooth(levelL, targetL);
  const double now = juce::Time::getMillisecondCounterHiRes();
  if (targetL >= 0.985f)
    holdUntilL = now + 1500.0;

  const int segmentCount = getVisibleSegmentCount();
  const int activeSegments =
      juce::jlimit(0, segmentCount,
                   static_cast<int>(
                       std::round(levelL * static_cast<float>(segmentCount))));
  const bool peakOn = now < holdUntilL;
  const bool shouldRepaint = activeSegments != lastPaintedActiveSegments ||
                             peakOn != lastPaintedPeakOn;

#if defined(MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS) &&                  \
    MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS
  recordVuMeterTimerResult(shouldRepaint, now);
#endif

  // The LED ladder is discrete, so repaint only when the visible segment count
  // or peak LED state changes.
  if (!shouldRepaint)
    return;

  lastPaintedActiveSegments = activeSegments;
  lastPaintedPeakOn = peakOn;
  repaint();
}
