#include "../src/audio/SongMixExporter.h"
#include "../src/audio/DynamicGuideTiming.h"
#include "../src/audio/DynamicGuideEvents.h"
#include "../src/data/ArrangementSourceMapping.h"
#include "../src/data/AppSettings.h"
#include "IsolatedTestRoot.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

// This isolated timing/codec target has no application-settings backend.
AppLanguage AppSettings::getLanguage() { return AppLanguage::Spanish; }

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
             DynamicGuideAnnouncementTiming::atSectionEntry,
         "breakdown is an immediate secondary instruction");
  expect(dynamicGuideAnnouncementTiming("A Capella", "A Capella") ==
             DynamicGuideAnnouncementTiming::atSectionEntry &&
             dynamicGuideAnnouncementTiming("acapella", "acapella") ==
             DynamicGuideAnnouncementTiming::atSectionEntry,
         "both a capella spellings are immediate secondary instructions");
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

void testIndependentCuesAndSampleOffsets() {
  SongArrangementSnapshot arrangement;
  SongArrangementPartSnapshot verse;
  verse.id = "verse"; verse.startSec = 1.0; verse.endSec = 3.0;
  arrangement.parts.push_back(verse);
  SongArrangementCueSnapshot count;
  count.id = "counts"; count.name = "1"; count.partId = "verse";
  count.offsetSec = 0.25; count.positionSec = 99.0;
  count.countEnd = 4; count.countIntervalSec = 0.5;
  arrangement.cues.push_back(count);
  SongArrangementCueSnapshot utility;
  utility.id = "build"; utility.name = "Build"; utility.positionSec = 1.5;
  arrangement.cues.push_back(utility);
  const auto triggers = song_mix_export::prepareGuideTriggers({{"Verse", 1.0}}, 0.5, arrangement);
  expect(triggers.size() == 6 && triggers[0].triggerSeconds == 0.5 &&
      triggers[1].name == "1" && triggers[1].triggerSeconds == 1.25 &&
      triggers[2].name == "Build" && triggers[2].triggerSeconds == 1.5 &&
      triggers.back().name == "4" && triggers.back().triggerSeconds == 2.75,
      "independent anchored cues expand counts on the pulse without changing section boundaries");
  arrangement.cues[0].countOffsetsSec = {0.0, 0.5, 0.75, 1.25};
  arrangement.cues[0].countIntervalSec = 0.0;
  const auto variable = song_mix_export::prepareGuideTriggers({}, 0.5, arrangement);
  expect(variable.size() == 5 && variable[0].triggerSeconds == 1.25 &&
      variable[2].name == "2" && variable[2].triggerSeconds == 1.75 &&
      variable[3].name == "3" && variable[3].triggerSeconds == 2.0 &&
      variable[4].name == "4" && variable[4].triggerSeconds == 2.5,
      "grouped count follows explicit pulse offsets across tempo regions instead of repeating one fixed interval");

  const auto directory = isolatedTestRoot().getChildFile("guide-event-offsets");
  expect(directory.createDirectory().wasOk(), "guide event fixture exists");
  const auto write = [&](const char *name, float level) {
    std::unique_ptr<juce::OutputStream> output = directory.getChildFile(name).createOutputStream();
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(output, juce::AudioFormatWriterOptions{}
        .withSampleRate(1000.0).withNumChannels(1).withBitsPerSample(24));
    juce::AudioBuffer<float> sample(1, 12);
    for (int i = 0; i < 12; ++i) sample.setSample(0, i, level);
    expect(writer && writer->writeFromAudioSampleBuffer(sample, 0, 12), "guide sample writes");
  };
  write("1.wav", 0.1f); write("build.wav", 0.2f);
  juce::AudioFormatManager formats; formats.registerBasicFormats();
  DynamicGuideEngine engine; engine.prepare(1000.0);
  expect(engine.loadGuideDirectories({directory}, formats), "guide fixtures load");
  {
    DynamicGuideEngine retained;
    retained.prepareRetainedCopyFrom(engine);
    expect(engine.triggerForSectionKey("1"), "start the phrase before retaining its live voice");
    juce::AudioBuffer<float> prefix(1, 4); prefix.clear(); engine.render(prefix);
    retained.captureRealtimeStateFrom(engine);
    juce::AudioBuffer<float> expected(1, 20), actual(1, 20); expected.clear(); actual.clear();
    engine.render(expected);
    expect(engine.loadGuideDirectories({directory}, formats), "incoming guide reloads its independent catalog");
    retained.render(actual);
    expect(actual.getMagnitude(0, 0, 8) > 0.09f && actual.getMagnitude(0, 8, 12) == 0.0f,
        "retained guide finishes its in-flight phrase instead of restarting or truncating it");
    for (int sample = 0; sample < 20; ++sample)
      expect(expected.getSample(0, sample) == actual.getSample(0, sample),
          "retained guide voice remains exact after incoming catalog replacement");
    expect(retained.triggerForSectionKey("Build"), "outgoing guide keeps its catalog for future timeline cues");
  }
  engine.reset();
  using Scheduler = playworship::audio::SectionScheduler;
  Scheduler::Snapshot snapshot; snapshot.valid = true; snapshot.cueCount = 3;
  snapshot.cues[0] = {110, engine.resolveTriggerId("1")};
  snapshot.cues[1] = {114, engine.resolveTriggerId("Build")};
  snapshot.cues[2] = {140, engine.resolveTriggerId("1")};
  playworship::audio::DynamicGuideEvents events;
  events.addCues(snapshot, 100, 40, 0, 40);
  juce::AudioBuffer<float> audio(1, 40); audio.clear();
  events.render(engine, audio, 0, 40);
  expect(std::abs(audio.getSample(0, 9)) < 1e-6 &&
      std::abs(audio.getSample(0, 10) - 0.1f) < 1e-6 &&
      std::abs(audio.getSample(0, 14) - 0.3f) < 1e-6 &&
      std::abs(audio.getSample(0, 22) - 0.2f) < 1e-6 && audio.getSample(0, 39) == 0.0f,
      "multiple cues in one callback start on exact frames, overlap without truncation, and exclude the right boundary");
  engine.reset(); audio.clear();
  playworship::audio::DynamicGuideEvents sought;
  sought.addCues(snapshot, 130, 20, 0, 10);
  sought.render(engine, audio, 0, 10);
  expect(audio.getSample(0, 4) == 0.0f && std::abs(audio.getSample(0, 5) - 0.1f) < 1e-6,
      "seek excludes past cues and tempo maps the destination cue into the output block");
  engine.reset();
  for (int i = 0; i < 8; ++i)
    expect(engine.triggerForSectionKey("1"), "bounded guide voices accept eight simultaneous instructions");
  expect(!engine.triggerForSectionKey("Build"), "a ninth voice is rejected without cutting a voice or queuing stale speech");
  engine.reset(); audio.clear(); engine.render(audio);
  expect(audio.getMagnitude(0, 40) == 0.0f, "reset removes every active cue voice and no pending voice resurfaces");
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

void testCopySourceRangesSurviveMovingBeforeTheirOriginal() {
  using arrangement_source_mapping::resolveCopySourceRange;

  constexpr double sourceLength = 100.0;
  // Moving a ten-second copy before its original ripples the original from
  // 20..30 to 30..40. Its arrangement coordinates are no longer source times.
  const auto shiftedOriginal =
      makeArrangementPart("verse", "Verse", 30.0, 40.0);
  auto movedCopy = makeArrangementPart("copy", "Verse", 10.0, 20.0);
  movedCopy.sourcePartId = shiftedOriginal.id;
  movedCopy.sourceStartSec = 20.0;
  movedCopy.sourceEndSec = 30.0;
  const std::vector<SongArrangementPartSnapshot> movedParts{
      movedCopy, shiftedOriginal};

  const auto [copyStart, copyEnd] =
      resolveCopySourceRange(movedParts, movedCopy, sourceLength);
  expect(std::abs(copyStart - 20.0) < 1.0e-9 &&
             std::abs(copyEnd - 30.0) < 1.0e-9,
         "copying a duplicate moved before its original keeps its source audio");

  const auto [originalStart, originalEnd] =
      resolveCopySourceRange(movedParts, shiftedOriginal, sourceLength);
  expect(std::abs(originalStart - 20.0) < 1.0e-9 &&
             std::abs(originalEnd - 30.0) < 1.0e-9,
         "copying the rippled original resolves its original source coordinates");

  // A trimmed four-second copy shifts the original by four seconds. Copying
  // it again must retain that exact trim instead of expanding to the original.
  const auto originalAfterTrim =
      makeArrangementPart("verse", "Verse", 24.0, 34.0);
  auto trimmedCopy = makeArrangementPart("trimmed-copy", "Verse", 10.0, 14.0);
  trimmedCopy.sourcePartId = originalAfterTrim.id;
  trimmedCopy.sourceStartSec = 22.0;
  trimmedCopy.sourceEndSec = 26.0;
  const auto [trimmedStart, trimmedEnd] = resolveCopySourceRange(
      {trimmedCopy, originalAfterTrim}, trimmedCopy, sourceLength);
  expect(std::abs(trimmedStart - 22.0) < 1.0e-9 &&
             std::abs(trimmedEnd - 26.0) < 1.0e-9,
         "copying a trimmed duplicate before its original keeps the exact trim");

  const auto [orphanStart, orphanEnd] =
      resolveCopySourceRange({trimmedCopy}, trimmedCopy, sourceLength);
  expect(std::abs(orphanStart - 22.0) < 1.0e-9 &&
             std::abs(orphanEnd - 26.0) < 1.0e-9,
         "deleting the original does not prevent copying a valid explicit range");

  auto invalidOrphan = trimmedCopy;
  invalidOrphan.sourceStartSec = std::numeric_limits<double>::quiet_NaN();
  const auto [nanStart, nanEnd] =
      resolveCopySourceRange({invalidOrphan}, invalidOrphan, sourceLength);
  expect(nanStart == 0.0 && nanEnd == 0.0,
         "an orphan with a non-finite source start cannot be copied");
  invalidOrphan.sourceStartSec = 22.0;
  invalidOrphan.sourceEndSec = std::numeric_limits<double>::infinity();
  const auto [infiniteStart, infiniteEnd] =
      resolveCopySourceRange({invalidOrphan}, invalidOrphan, sourceLength);
  expect(infiniteStart == 0.0 && infiniteEnd == 0.0,
         "an orphan with a non-finite source end cannot be copied");
}

} // namespace

int main() {
  testSlowTempoTimeline();
  testFastTempoTimeline();
  testTempoNormalization();
  testGuideAnnouncementTiming();
  testSlowlyBuildGuideFallback();
  testGuideTriggersSortByActualAnnouncementTime();
  testIndependentCuesAndSampleOffsets();
  testCopySourceRangesRespectInsertedClipBoundaries();
  testCopySourceRangesSurviveMovingBeforeTheirOriginal();
  std::cout << "song mix export timeline tests passed\n";
  return 0;
}
