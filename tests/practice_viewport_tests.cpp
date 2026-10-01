#include "ui/panels/PracticeTimelineRuler.h"
#include "ui/panels/PracticeViewport.h"
#include "ui/panels/WaveformBar.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
void expectViewport(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void expectViewportNear(const double actual, const double expected,
                        const char *message) {
  expectViewport(std::abs(actual - expected) < 0.000001, message);
}

const PracticeTimelineMark *strongMarkAt(
    const std::vector<PracticeTimelineMark> &marks, const double time) {
  for (const auto &mark : marks)
    if (mark.strong && std::abs(mark.timeSeconds - time) < 0.000001)
      return &mark;
  return nullptr;
}

SongTempoRegionSnapshot tempoRegion(const double from, const double to,
                                    const double bpm, const juce::String &meter) {
  SongTempoRegionSnapshot region;
  region.startSec = from;
  region.endSec = to;
  region.bpm = bpm;
  region.timeSignature = meter;
  return region;
}

void testZoomLimitsAndSongClamping() {
  PracticeViewport viewport(120.0);
  expectViewportNear(viewport.getSpan(), 12.0, "default viewport shows twelve seconds");
  expectViewport(viewport.zoomBy(2.0), "spreading fingers enlarges the waveform");
  expectViewportNear(viewport.getSpan(), 6.0, "zoom factor halves visible duration");
  viewport.zoomBy(0.5);
  expectViewportNear(viewport.getSpan(), 12.0, "closing fingers restores the wider view");
  viewport.zoomBy(10000.0);
  expectViewportNear(viewport.getSpan(), 2.0, "zoom in stops at two seconds");
  viewport.zoomBy(0.00001);
  expectViewportNear(viewport.getSpan(), 48.0, "zoom out stops at forty-eight seconds");
  expectViewport(!viewport.zoomBy(1.0), "unit magnification does not change zoom");
  expectViewport(!viewport.zoomBy(0.0) && !viewport.zoomBy(-1.0) &&
                     !viewport.zoomBy(std::numeric_limits<double>::quiet_NaN()),
                 "invalid factors preserve the viewport");
  viewport.setLength(5.0);
  expectViewportNear(viewport.getSpan(), 5.0, "visible duration cannot exceed a short song");
  expectViewport(!viewport.zoomBy(1.0), "unit factor on a short song keeps requested zoom");
  viewport.setLength(120.0);
  expectViewportNear(viewport.getSpan(), 48.0, "length clamp preserves the user's requested zoom");
  viewport.reset();
  expectViewportNear(viewport.getSpan(), 12.0, "song reset restores the default duration");
  viewport.setLength(1.25);
  expectViewportNear(viewport.getSpan(), 1.25, "songs shorter than the zoom minimum remain valid");
  viewport.setLength(-1.0);
  expectViewportNear(viewport.getSpan(), 0.0, "no song has no visible source duration");
}

void testFractionalMeterAndAdaptiveDensity() {
  const auto odd = PracticeTimelineRuler::build({0.0, 3.5}, 420.0f, 120.0, "7/8");
  expectViewport(strongMarkAt(odd, 0.0) != nullptr &&
                     strongMarkAt(odd, 1.75) != nullptr &&
                     strongMarkAt(odd, 3.5) != nullptr,
                 "seven eighths retains three-and-a-half quarter notes per bar");
  expectViewport(strongMarkAt(odd, 1.75)->label == "2",
                 "odd meter bar labels advance correctly");
  const auto dense = PracticeTimelineRuler::build({0.0, 48.0}, 240.0f, 120.0, "4/4");
  for (const auto &mark : dense)
    expectViewport(mark.strong, "wide overview hides pulse marks");
  double lastLabel = -1000.0;
  for (const auto &mark : dense)
    if (mark.label.isNotEmpty()) {
      expectViewport((mark.timeSeconds - lastLabel) * 5.0 >= 72.0,
                     "overview bar labels maintain readable spacing");
      lastLabel = mark.timeSeconds;
    }
  const auto detail = PracticeTimelineRuler::build({0.0, 2.0}, 300.0f, 120.0, "4/4");
  bool hasPulse = false;
  for (const auto &mark : detail)
    hasPulse = hasPulse || !mark.strong;
  expectViewport(hasPulse, "zooming in reveals musical pulses without snap subdivisions");
}

void testLiveTempoMapAndNumbering() {
  SongTempoMapSnapshot map;
  map.regions = {tempoRegion(0.0, 8.0, 120.0, "4/4"),
                 tempoRegion(8.0, 16.0, 60.0, "4/4")};
  const auto marks = PracticeTimelineRuler::build({0.0, 16.0}, 1600.0f, 120.0, "4/4", map);
  expectViewport(strongMarkAt(marks, 8.0) != nullptr &&
                     strongMarkAt(marks, 12.0) != nullptr &&
                     strongMarkAt(marks, 10.0) == nullptr,
                 "ruler spacing follows the live tempo region rather than fixed song BPM");
  expectViewport(strongMarkAt(marks, 12.0)->label == "6",
                 "bar numbering continues across a tempo boundary");

  map.regions = {tempoRegion(0.0, 1.0, 120.0, "4/4"),
                 tempoRegion(1.0, 5.0, 60.0, "4/4")};
  const auto partial = PracticeTimelineRuler::build({0.0, 5.0}, 1000.0f, 120.0, "4/4", map);
  expectViewport(strongMarkAt(partial, 3.0) != nullptr &&
                     strongMarkAt(partial, 1.0) == nullptr,
                 "a tempo change inside a bar preserves its musical phase");

  map.regions = {tempoRegion(0.0, 8.0, 120.0, "4/4"),
                 tempoRegion(8.0, 11.5, 120.0, "7/8")};
  const auto changedMeter = PracticeTimelineRuler::build({0.0, 11.5}, 1150.0f, 120.0, "4/4", map);
  expectViewport(strongMarkAt(changedMeter, 9.75) != nullptr &&
                     strongMarkAt(changedMeter, 9.75)->label == "6",
                 "new meter uses exact denominator and keeps global bar labels");

  WaveformBar waveform;
  waveform.setTransportProviders([] { return 0.0; }, [] { return 16.0; });
  waveform.setSongRhythm(120.0, "4/4");
  map.regions = {tempoRegion(0.0, 16.0, 120.0, "4/4")};
  waveform.setTempoMap(map);
  expectViewport(strongMarkAt(waveform.getPracticeTimelineMarks({8.0, 16.0}, 800.0f), 10.0) != nullptr,
                 "WaveformBar provider reflects its initial live tempo map");
  map.regions = {tempoRegion(0.0, 8.0, 120.0, "4/4"),
                 tempoRegion(8.0, 16.0, 60.0, "4/4")};
  waveform.setTempoMap(map);
  expectViewport(strongMarkAt(waveform.getPracticeTimelineMarks({8.0, 16.0}, 800.0f), 10.0) == nullptr,
                 "provider immediately reflects tempo edits without saved song metadata");
}

void testNearestPulseAndAllowedWindow() {
  const auto snap = [](const double position, const juce::Range<double> allowed) {
    return PracticeTimelineRuler::snapToPulse(position, 120.0, "4/4", {}, allowed);
  };
  expectViewportNear(snap(0.24, {0.0, 12.0}), 0.0,
                     "first half of a pulse rounds down");
  expectViewportNear(snap(0.26, {0.0, 12.0}), 0.5,
                     "second half of a pulse rounds up");
  expectViewportNear(snap(0.25, {0.0, 12.0}), 0.5,
                     "a midpoint tie chooses the following pulse");
  expectViewportNear(PracticeTimelineRuler::snapToPulse(0.13, 120.0, "7/8"),
                     0.25, "seven eighths quantizes to an eighth-note pulse");
  expectViewportNear(PracticeTimelineRuler::snapToPulse(100.2, 120.0, "4/4"),
                     100.0, "an unbounded window safely handles an infinite final pulse index");
  expectViewportNear(snap(0.11, {0.1, 0.9}), 0.5,
                     "unaligned window start cannot become an A mark");
  expectViewportNear(snap(0.89, {0.1, 0.9}), 0.5,
                     "unaligned window end cannot become a B mark");
  expectViewportNear(snap(100.0, {0.1, 0.9}), 0.5,
                     "out-of-window positions choose a valid pulse inside the window");
  expectViewportNear(snap(0.5, {0.5, 0.5}), 0.5,
                     "an inclusive boundary can contain exactly one pulse");
  expectViewport(std::isnan(snap(0.2, {0.1, 0.4})),
                 "a window without a pulse does not invent an off-grid point");
  expectViewport(std::isnan(snap(std::numeric_limits<double>::quiet_NaN(), {0.0, 1.0})),
                 "invalid positions cannot create loop markers");
  expectViewportNear(snap(-1.0, {-2.0, 1.0}), 0.0,
                     "the musical grid starts at the beginning of the song");
  expectViewportNear(PracticeTimelineRuler::snapToPulse(0.26, 0.0, "invalid"),
                     0.5, "invalid rhythm metadata uses the ruler's fallback rhythm");
}

void testPulseSnapFollowsLiveTempoPhase() {
  SongTempoMapSnapshot map;
  map.regions = {tempoRegion(0.0, 0.75, 120.0, "4/4"),
                 tempoRegion(0.75, 4.0, 60.0, "4/4")};
  expectViewportNear(PracticeTimelineRuler::snapToPulse(0.76, 120.0, "4/4", map,
                                                       {0.0, 4.0}),
                     0.5, "a BPM boundary inside a pulse does not become a new pulse");
  expectViewportNear(PracticeTimelineRuler::snapToPulse(0.9, 120.0, "4/4", map,
                                                       {0.0, 4.0}),
                     1.25, "fractional pulse phase carries across a BPM change");
  map.regions[1].timeSignature = "7/8";
  expectViewportNear(PracticeTimelineRuler::snapToPulse(0.74, 120.0, "4/4", map,
                                                       {0.0, 4.0}),
                     0.75, "a meter change starts a new bar on its exact boundary");
  expectViewportNear(PracticeTimelineRuler::snapToPulse(0.74, 120.0, "4/4", map,
                                                       {0.7, 0.75}),
                     0.75, "a meter-boundary pulse remains valid at the allowed endpoint");
  const auto marks = PracticeTimelineRuler::build({0.0, 4.0}, 1600.0f,
                                                  120.0, "4/4", map);
  for (const auto &mark : marks)
    expectViewportNear(PracticeTimelineRuler::snapToPulse(mark.timeSeconds, 120.0,
                                                        "4/4", map, {0.0, 4.0}),
                       mark.timeSeconds, "snapping agrees with every visible ruler pulse");

  WaveformBar waveform;
  waveform.setTransportProviders([] { return 0.0; }, [] { return 16.0; });
  waveform.setSongRhythm(120.0, "4/4");
  map.regions = {tempoRegion(0.0, 16.0, 120.0, "4/4")};
  waveform.setTempoMap(map);
  expectViewportNear(waveform.getPracticeSnappedPosition(9.49, {0.0, 16.0}),
                     9.5, "WaveformBar snaps using the initial live tempo map");
  map.regions = {tempoRegion(0.0, 8.0, 120.0, "4/4"),
                 tempoRegion(8.0, 16.0, 60.0, "4/4")};
  waveform.setTempoMap(map);
  expectViewportNear(waveform.getPracticeSnappedPosition(9.49, {0.0, 16.0}),
                     9.0, "snapping reflects an unsaved tempo edit immediately");
  expectViewportNear(waveform.getPracticeSnappedPosition(16.4, {0.0, 20.0}),
                     16.0, "the provider intersects the permitted window with song length");
  expectViewport(std::isnan(waveform.getPracticeSnappedPosition(17.0, {16.1, 20.0})),
                 "a window beyond the song cannot produce a loop marker");
}
} // namespace

void runPracticeViewportTests() {
  testZoomLimitsAndSongClamping();
  testFractionalMeterAndAdaptiveDensity();
  testLiveTempoMapAndNumbering();
  testNearestPulseAndAllowedWindow();
  testPulseSnapFollowsLiveTempoPhase();
}
