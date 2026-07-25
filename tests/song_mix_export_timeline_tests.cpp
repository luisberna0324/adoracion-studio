#include "../src/audio/SongMixExporter.h"
#include "../src/audio/DynamicGuideTiming.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void testSlowTempoTimeline() {
  constexpr std::int64_t sourceFrames = 480000;
  song_mix_export::Timeline timeline(48000.0, 10.0, sourceFrames, 0.75);

  expect(timeline.getOutputFrames() == 640000,
         "0.75 tempo expands output duration");
  expect(timeline.sourceFrameAtOutputBoundary(0) == 0,
         "source mapping starts at zero");
  expect(timeline.sourceFrameAtOutputBoundary(timeline.getOutputFrames()) ==
             sourceFrames,
         "source mapping consumes the complete range");
  expect(std::abs(timeline.sourceSecondsAtOutputBoundary(320000) - 15.0) <
             1.0e-9,
         "source timeline maps through slow output");
  expect(std::abs(timeline.playbackSecondsAtOutputBoundary(320000) - 20.0) <
             1.0e-9,
         "playback timeline accounts for tempo");
}

void testFastTempoTimeline() {
  constexpr std::int64_t sourceFrames = 480003;
  song_mix_export::Timeline timeline(48000.0, 0.0, sourceFrames, 2.0);

  expect(timeline.getOutputFrames() == 240002,
         "2.0 tempo contracts output duration");

  std::int64_t consumed = 0;
  for (std::int64_t outputStart = 0;
       outputStart < timeline.getOutputFrames(); outputStart += 512) {
    const auto outputEnd =
        std::min(outputStart + 512, timeline.getOutputFrames());
    const auto sourceStart =
        timeline.sourceFrameAtOutputBoundary(outputStart);
    const auto sourceEnd = timeline.sourceFrameAtOutputBoundary(outputEnd);
    expect(sourceEnd >= sourceStart, "source boundaries remain monotonic");
    consumed += sourceEnd - sourceStart;
  }
  expect(consumed == sourceFrames,
         "cumulative block mapping neither truncates nor overreads");
}

void testTempoNormalization() {
  song_mix_export::Timeline tooSlow(44100.0, 0.0, 44100, 0.0);
  song_mix_export::Timeline tooFast(44100.0, 0.0, 44100, 4.0);
  song_mix_export::Timeline invalid(44100.0, 0.0, 44100, NAN);

  expect(tooSlow.getTempoRatio() == 0.75, "tempo clamps to engine minimum");
  expect(tooFast.getTempoRatio() == 2.0, "tempo clamps to engine maximum");
  expect(invalid.getTempoRatio() == 1.0, "non-finite tempo falls back safely");

  song_mix_export::Timeline overflow(
      44100.0, 0.0, std::numeric_limits<std::int64_t>::max(), 0.75);
  expect(overflow.getOutputFrames() == 0,
         "an overflowing output timeline is rejected");
}

void testGuideAnnouncementTiming() {
  using playworship::audio::DynamicGuideAnnouncementTiming;
  using playworship::audio::dynamicGuideAnnouncementTiming;
  expect(dynamicGuideAnnouncementTiming("Chorus", "Chorus") ==
             DynamicGuideAnnouncementTiming::oneBarBeforeSection,
         "musical sections are announced one bar early");
  expect(dynamicGuideAnnouncementTiming("Breakdown", "Breakdown") ==
             DynamicGuideAnnouncementTiming::oneBarBeforeSection,
         "breakdown follows the musical-section timing contract");
  expect(dynamicGuideAnnouncementTiming("4", "4") ==
             DynamicGuideAnnouncementTiming::atSectionEntry,
         "counts are announced at section entry");
  expect(dynamicGuideAnnouncementTiming("Hold", "Hold") ==
             DynamicGuideAnnouncementTiming::atSectionEntry,
         "utility cues are announced at section entry");
}

void testGuideTriggersSortByActualAnnouncementTime() {
  const std::vector<SongMixExportGuideSection> sections{
      {"4", 9.0},
      {"Chorus", 10.0},
      {"Invalid", std::numeric_limits<double>::quiet_NaN()},
  };
  const auto triggers =
      song_mix_export::prepareGuideTriggers(sections, 2.0);

  expect(triggers.size() == 2,
         "non-finite guide times are rejected before sorting");
  expect(triggers[0].name == "Chorus" &&
             std::abs(triggers[0].triggerSeconds - 8.0) < 1.0e-9,
         "musical lead can sort before an earlier entry cue");
  expect(triggers[1].name == "4" &&
             std::abs(triggers[1].triggerSeconds - 9.0) < 1.0e-9,
         "entry cue retains its actual trigger time");
}

} // namespace

int main() {
  testSlowTempoTimeline();
  testFastTempoTimeline();
  testTempoNormalization();
  testGuideAnnouncementTiming();
  testGuideTriggersSortByActualAnnouncementTime();
  std::cout << "song mix export timeline tests passed\n";
  return 0;
}
