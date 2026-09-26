#include "../src/audio/SongMixExporter.h"
#include "../src/audio/DynamicGuideTiming.h"
#include "../src/data/ArrangementSourceMapping.h"
#include "IsolatedTestRoot.h"

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
  expect(dynamicGuideAnnouncementTiming("Breakdown", "Breakdown", true) ==
             DynamicGuideAnnouncementTiming::atSectionEntry,
         "a Breakdown inserted from Utilities is announced at its entry");
  expect(dynamicGuideAnnouncementTiming("4", "4") ==
             DynamicGuideAnnouncementTiming::atSectionEntry,
         "counts are announced at section entry");
  expect(dynamicGuideAnnouncementTiming("Hold", "Hold") ==
             DynamicGuideAnnouncementTiming::atSectionEntry,
         "utility cues are announced at section entry");
}

void testSlowlyBuildGuideFallback() {
  auto directory = isolatedTestRoot().getChildFile("guide");
  expect(directory.createDirectory().wasOk(),
         "guide fallback test creates its temporary directory");

  const auto buildSample = directory.getChildFile("build.wav");
  juce::WavAudioFormat wav;
  std::unique_ptr<juce::OutputStream> output = buildSample.createOutputStream();
  expect(output != nullptr, "guide fallback test creates a WAV output");
  const auto writerOptions = juce::AudioFormatWriterOptions{}
                                 .withSampleRate(44100.0)
                                 .withNumChannels(1)
                                 .withBitsPerSample(16);
  auto writer = wav.createWriterFor(output, writerOptions);
  expect(writer != nullptr, "guide fallback test creates a WAV writer");
  juce::AudioBuffer<float> sample(1, 64);
  sample.clear();
  sample.setSample(0, 0, 0.5f);
  expect(writer->writeFromAudioSampleBuffer(sample, 0, sample.getNumSamples()),
         "guide fallback test writes the Build sample");
  writer.reset();

  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  DynamicGuideEngine guide;
  guide.prepare(44100.0);
  juce::String error;
  expect(guide.loadGuideDirectories({directory}, formats, &error),
         "guide fallback test loads its sample");
  const int buildTrigger = guide.resolveTriggerId("Build");
  expect(buildTrigger >= 0, "Build resolves to a guide trigger");
  expect(guide.resolveTriggerId("Slowly Build") == buildTrigger,
         "Slowly Build falls back to Build when its recording is absent");
  expect(guide.resolveTriggerId("Sube lento") == buildTrigger,
         "the localized Sube lento label uses the same fallback");

  expect(directory.deleteRecursively(),
         "guide fallback test removes its temporary directory");
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

SongArrangementPartSnapshot makeArrangementPart(const char *id,
                                                 const char *name,
                                                 const double start,
                                                 const double end) {
  SongArrangementPartSnapshot part;
  part.id = id;
  part.name = name;
  part.startSec = start;
  part.endSec = end;
  part.sourceStartSec = start;
  part.sourceEndSec = end;
  return part;
}

void testCopySourceRangesRespectInsertedClipBoundaries() {
  using arrangement_source_mapping::resolveCopySourceRange;

  constexpr double sourceLength = 300.0;
  const auto postChorus =
      makeArrangementPart("post", "Post-Chorus", 225.671641791, 240.0);
  const auto verse = makeArrangementPart("verse", "Verse", 20.0, 30.0);

  const auto [postStartWithoutCopy, postEndWithoutCopy] =
      resolveCopySourceRange({postChorus}, postChorus, sourceLength);
  expect(std::abs(postStartWithoutCopy - postChorus.startSec) < 1.0e-9 &&
             std::abs(postEndWithoutCopy - postChorus.endSec) < 1.0e-9,
         "a Post-Chorus without an adjacent copy retains its short source range");

  auto corruptPostCopy = makeArrangementPart("bad-post", "Post-Chorus", 240.0,
                                              415.522388060);
  corruptPostCopy.sourcePartId = postChorus.id;
  corruptPostCopy.sourceStartSec = 50.149253731;
  corruptPostCopy.sourceEndSec = 225.671641791;
  const std::vector<SongArrangementPartSnapshot> postWithAdjacentCopy{
      postChorus, corruptPostCopy};
  const auto [postStartWithCopy, postEndWithCopy] =
      resolveCopySourceRange(postWithAdjacentCopy, postChorus, sourceLength);
  expect(std::abs(postStartWithCopy - postChorus.startSec) < 1.0e-9 &&
             std::abs(postEndWithCopy - postChorus.endSec) < 1.0e-9,
         "a Post-Chorus end on an inserted-clip boundary uses the left source side");

  auto corruptVerseCopy = makeArrangementPart("bad-verse", "Verse", 30.0, 50.0);
  corruptVerseCopy.sourcePartId = verse.id;
  corruptVerseCopy.sourceStartSec = 0.0;
  corruptVerseCopy.sourceEndSec = 20.0;
  const std::vector<SongArrangementPartSnapshot> verseWithAdjacentCopy{
      verse, corruptVerseCopy};
  const auto [verseStartWithCopy, verseEndWithCopy] =
      resolveCopySourceRange(verseWithAdjacentCopy, verse, sourceLength);
  expect(std::abs(verseStartWithCopy - verse.startSec) < 1.0e-9 &&
             std::abs(verseEndWithCopy - verse.endSec) < 1.0e-9,
         "the boundary rule applies to non-Post-Chorus sections too");

  auto trimmedPostCopy = makeArrangementPart("trimmed-post", "Post-Chorus",
                                              240.0, 244.0);
  trimmedPostCopy.sourcePartId = postChorus.id;
  trimmedPostCopy.sourceStartSec = 230.0;
  trimmedPostCopy.sourceEndSec = 234.0;
  const std::vector<SongArrangementPartSnapshot> trimmedPostParts{
      postChorus, trimmedPostCopy};
  const auto [trimmedStart, trimmedEnd] =
      resolveCopySourceRange(trimmedPostParts, trimmedPostCopy, sourceLength);
  expect(std::abs(trimmedStart - 230.0) < 1.0e-9 &&
             std::abs(trimmedEnd - 234.0) < 1.0e-9,
         "a valid trimmed duplicate retains its explicit source range");

  const auto [recoveredStart, recoveredEnd] =
      resolveCopySourceRange(postWithAdjacentCopy, corruptPostCopy, sourceLength);
  expect(std::abs(recoveredStart - postChorus.startSec) < 1.0e-9 &&
             std::abs(recoveredEnd - postChorus.endSec) < 1.0e-9,
         "an invalid duplicate range is not propagated by Copy");
}

} // namespace

int main() {
  testSlowTempoTimeline();
  testFastTempoTimeline();
  testTempoNormalization();
  testGuideAnnouncementTiming();
  testSlowlyBuildGuideFallback();
  testGuideTriggersSortByActualAnnouncementTime();
  testCopySourceRangesRespectInsertedClipBoundaries();
  std::cout << "song mix export timeline tests passed\n";
  return 0;
}
