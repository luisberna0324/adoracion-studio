#include "PluginProcessor.h"
#include "audio/SectionAutomation.h"
#include "data/ApplicationDataPaths.h"
#include "data/AssetBootstrapManager.h"
#include "ui/panels/PadsComponent.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

// Readiness is observed only on the test/control thread; realtime processing
// keeps its existing nonblocking seek and file-read contracts.
struct SectionAutomationTestAccess {
  static bool loadGuides(MTControlsAudioProcessor &processor, const juce::File &directory) {
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    const bool loaded = processor.dynamicGuideEngine.loadGuideDirectories({directory}, formats);
    if (loaded) {
      processor.rebuildSectionScheduler();
      processor.dynamicGuideResetGeneration.fetch_add(1, std::memory_order_release);
    }
    return loaded;
  }
  static float guideSample(const MTControlsAudioProcessor &processor, int frame) {
    return processor.dynamicGuideRenderStorage.getSample(0, frame);
  }
  static int currentPart(const MTControlsAudioProcessor &processor) {
    return processor.arrangementCurrentPartIndex.load();
  }
  static auto scheduler(const MTControlsAudioProcessor &processor) {
    return processor.sectionScheduler.acquireSnapshot();
  }
  static double tempoRatio(const MTControlsAudioProcessor &processor) {
    return processor.getPlaybackTempoRatio();
  }
  static bool seekApplied(const MTControlsAudioProcessor &processor) {
    return processor.seekRequestGeneration.load(std::memory_order_acquire) ==
           processor.appliedSeekRequestGeneration;
  }
  static bool waitForSource(MTControlsAudioProcessor &processor, int frames) {
    for (const auto &track : processor.stemTracks)
      if (!track->waitForNextAudioBlockReady(frames, 1000)) return false;
    return true;
  }
  static void setReadThreadsRunning(MTControlsAudioProcessor &processor, bool running) {
    for (auto &thread : processor.stemReadThreads) {
      if (running) thread->startThread();
      else thread->stopThread(2000);
    }
  }
  static bool waitForCacheBudget(MTControlsAudioProcessor &processor) {
    for (int attempt = 0; attempt < 500; ++attempt) {
      if (processor.getSectionEntryCacheBytes() <= processor.getSectionEntryCacheBudget()) return true;
      juce::Thread::sleep(2);
    }
    return false;
  }
  static int announcedPendingTarget(const MTControlsAudioProcessor &processor) { return processor.lastAnnouncedPendingJumpTarget; }
  static bool publishedTargetReady(MTControlsAudioProcessor &processor, double seconds) {
    const auto frame = static_cast<juce::int64>(std::llround(seconds * 44100.0));
    for (const auto &track : processor.stemTracks)
      if (!track->isPrefetchReady(frame, 256)) return false;
    return true;
  }
  static bool waitForTransitionTarget(MTControlsAudioProcessor &processor,
                                      double seconds) {
    const auto frame = static_cast<juce::int64>(std::llround(seconds * 44100.0));
    // Match the runtime's anticipatory request so its next callback observes
    // the same resident target rather than issuing a new generation.
    constexpr int frames = 32768;
    for (auto &track : processor.stemTracks)
      track->requestPrefetchPosition(frame, frames);
    for (int attempt = 0; attempt < 500; ++attempt) {
      for (auto &track : processor.stemTracks)
        track->requestPrefetchPosition(frame, frames);
      bool ready = true;
      for (const auto &track : processor.stemTracks)
        ready = track->isPrefetchReady(frame, frames) && ready;
      if (ready) return true;
      juce::Thread::sleep(2);
    }
    return false;
  }
};

namespace {
void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, const char *message) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > 0.0001)
    throw std::runtime_error(std::string(message) + " actual=" + std::to_string(actual));
}
void writeWav(const juce::File &file, const int frames, const float level = 0.1f) {
  file.getParentDirectory().createDirectory();
  // FileOutputStream appends to an existing file; replacing a guide fixture
  // must remove the previous RIFF rather than append another WAV after it.
  expect(file.deleteFile(), "fixture replaces any previous WAV");
  auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
  juce::WavAudioFormat format;
  auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
      .withSampleRate(44100.0).withNumChannels(1).withBitsPerSample(16));
  expect(writer != nullptr, "fixture WAV opens");
  juce::AudioBuffer<float> audio(1, 4096);
  for (int frame = 0; frame < audio.getNumSamples(); ++frame) audio.setSample(0, frame, level);
  for (int remaining = frames; remaining > 0; remaining -= audio.getNumSamples())
    expect(writer->writeFromAudioSampleBuffer(audio, 0, std::min(remaining, audio.getNumSamples())), "fixture WAV writes");
}

void testTimelineSelectionAndPublication() {
  using Automation = playworship::audio::SectionAutomation;
  auto timeline = std::make_unique<Automation>();
  Automation::Part first, second;
  first.token = 1; first.start = 1.0; first.end = 2.0;
  first.stems[0].mask = Automation::volume;
  first.stems[0].volume = 0.25f;
  first.pads.mask = Automation::note; first.pads.note = 4;
  second.token = 2; second.start = 3.0; second.end = 4.0;
  second.stems[1].mask = Automation::mute; second.stems[1].mute = true;
  timeline->publish({first, second});
  const auto inside = timeline->select(1.0);
  expect(inside.valid && inside.token == 1 && inside.nextBoundary == 2.0, "entry is inclusive and exposes next boundary");
  near(inside.stems[0].volume, 0.25, "sparse stem volume is recovered");
  expect(inside.stems[1].mask == 0 && inside.pads.note == 4, "untouched controls remain sparse");
  expect(timeline->select(2.0).token == 0 && timeline->select(2.0).nextBoundary == 3.0,
      "end is exclusive and a gap restores base mix");
  expect(timeline->select(3.0).token == 2 && timeline->select(4.0).token == 0, "direct seeking uses destination state");
  std::array<double, 512> boundaries{};
  const int boundaryCount = timeline->collectBoundaries(inside, 0.5, 4.5, boundaries.data(), static_cast<int>(boundaries.size()));
  expect(boundaryCount == 4 && boundaries[0] == 1.0 && boundaries[1] == 2.0 && boundaries[2] == 3.0 && boundaries[3] == 4.0,
      "long audio blocks enumerate every section and gap boundary in one coherent generation");
  timeline->publish({first, second}, true);
  const auto silentSection = timeline->select(3.25);
  const auto silentGap = timeline->select(2.25);
  expect(silentSection.pads.note == -1 && !silentSection.pads.enabled &&
         silentGap.pads.note == -1 && !silentGap.pads.enabled,
         "unassigned section and gap are silent when pads are scoped to sections");
  timeline->publish({first, second});
  expect(timeline->select(3.25).pads.mask == 0,
         "songs without scoped pads retain sparse global pad semantics");
  std::atomic<bool> done{false};
  std::thread publisher([&] {
    for (int index = 0; index < 2500; ++index) {
      auto part = first;
      part.token = (index & 1) ? 10 : 20;
      part.stems[0].volume = (index & 1) ? 0.1f : 0.2f;
      part.pads.note = (index & 1) ? 1 : 2;
      timeline->publish({part});
    }
    done.store(true, std::memory_order_release);
  });
  bool coherent = true;
  while (!done.load(std::memory_order_acquire)) {
    const auto snapshot = timeline->select(1.25);
    if (!snapshot.valid || snapshot.token == 1) continue;
    if (snapshot.token == 10)
      coherent = coherent && snapshot.stems[0].volume == 0.1f && snapshot.pads.note == 1;
    else
      coherent = coherent && snapshot.token == 20 && snapshot.stems[0].volume == 0.2f && snapshot.pads.note == 2;
  }
  publisher.join();
  expect(coherent, "realtime reader never mixes atomic publication generations");
}

struct ProcessorFixture {
  juce::File source;
  std::unique_ptr<MTControlsAudioProcessor> processor;
  ProcessorFixture() {
    const auto root = ApplicationDataPaths::isolatedTestRoot();
    expect(root.isDirectory(), "isolated test root exists");
    const auto pads = AssetBootstrapManager::getPadsPackDirectory();
    expect(pads.isAChildOf(root), "pad asset bootstrap is isolated");
    const auto pad = pads.getChildFile("C.wav");
    if (!pad.existsAsFile()) {
      writeWav(pad, 88200);
      for (int note = 1; note < PadsEngine::kNoteCount; ++note)
        expect(pad.copyFileTo(pads.getChildFile(juce::String(PadsEngine::kNoteNames[note]) + ".wav")), "resident pad fixtures copy");
      for (int extra = 0; extra < 8; ++extra)
        expect(pad.copyFileTo(pads.getChildFile("fixture-extra-" + juce::String(extra) + ".wav")), "local bootstrap asset count is complete");
    }
    source = root.getChildFile("section-automation-" + juce::Uuid().toString() + ".wav");
    writeWav(source, 44100 * 6);
    processor = std::make_unique<MTControlsAudioProcessor>();
    processor->prepareToPlay(44100.0, 256);
    juce::StringArray files; files.add(source.getFullPathName()); files.add(source.getFullPathName());
    expect(processor->loadSongStems(files, juce::Uuid().toString(), "Automation", "C", 120.0), "real processor loads two stems");
    processor->setSectionAutomationStemIds({"guitar", "click"});
    processor->setStemClickGuideMask({false, true});
    processor->setStemVolume(0, 0.8f); processor->setStemGain(0, 1.1f);
    processor->setPadsVolume(0.6f); processor->setPadsGain(0.7f);
    processor->setMetronomeEnabled(false);
    processor->setSongFadeOutSeconds(0.0);
    SongArrangementPartSnapshot a, b;
    a.id = "verse"; a.name = "Verse"; a.startSec = 0.0; a.endSec = 2.0;
    b.id = "chorus"; b.name = "Chorus"; b.startSec = 2.0; b.endSec = 4.0;
    processor->setSongArrangement({{a, b}}, false);
  }
  void block(int count = 1) {
    juce::AudioBuffer<float> buffer(processor->getTotalNumOutputChannels(), 256); juce::MidiBuffer midi;
    for (int index = 0; index < count; ++index) processor->processBlock(buffer, midi);
  }
  void seekAndWait(double seconds) {
    processor->seekSongTo(seconds);
    waitForPendingSeek();
  }
  void waitForPendingSeek() {
    for (int attempt = 0; attempt < 500; ++attempt) {
      block();
      if (SectionAutomationTestAccess::seekApplied(*processor)) return;
      juce::Thread::sleep(2);
    }
    expect(false, "real audio seek accepts its resident prefetch generation");
  }
};

void testDraftSaveCancelAndBaseMix() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  p.playSong();
  expect(p.beginSectionAutomationEditing("chorus") && !p.isSongPlaying(), "editing pauses and pins the selected section");
  p.setStemVolume(0, 0.3f);
  expect(!p.beginSectionAutomationEditing("verse"), "repeated begin cannot discard an existing draft");
  near(p.getStemVolume(0), 0.3, "draft previews selected fader");
  near(p.getStemBaseVolume(0), 0.8, "draft leaves persisted base unchanged");
  p.setStemGain(0, 1.4f); p.setStemMute(0, true);
  p.setPadsVolume(0.25f); p.setPadsEnabled(false);
  expect(!p.getSongArrangementSnapshot().parts[1].automation.hasOverrides(), "draft never escapes through committed snapshot");
  expect(p.finishSectionAutomationEditing(false), "cancel succeeds");
  near(p.getStemVolume(0), 0.8, "cancel restores the previous effective mix");
  near(p.getPadsVolume(), 0.6, "cancel restores pads base mix");
  expect(p.isPadsEnabled(), "cancel restores pads power");
  expect(p.beginSectionAutomationEditing("chorus"), "draft can reopen");
  p.setStemVolume(0, 0.3f); p.setStemMute(0, true);
  p.setStemVolume(1, 0.45f); // Click is excluded and remains a normal control.
  p.setPadsVolume(0.25f);
  p.triggerPadNote(4, -1);
  expect(p.finishSectionAutomationEditing(true), "save commits");
  const auto committed = p.getSongArrangementSnapshot();
  expect(committed.parts.size() == 2 && committed.parts[1].id == "chorus", "committed snapshot retains original order and IDs");
  expect(committed.parts[1].automation.stems.size() == 1 &&
         committed.parts[1].automation.stems[0].stemId == "guitar" &&
         !committed.parts[1].automation.stems[0].gain, "save includes touched instrument fields only");
  expect(committed.parts[1].automation.pads.noteIndex == 4 && committed.parts[1].automation.pads.octave == -1,
      "pad note and relative octave commit with sparse values");
  near(p.getStemBaseVolume(1), 0.45, "click remains outside automation");
  expect(!p.isCustomArrangementActive() && !p.isSongPlaying(), "automation save preserves normal playback mode and paused state");
  p.seekSongTo(2.25); fixture.block();
  near(p.getStemVolume(0), 0.3, "seeking directly into section applies fader");
  expect(p.isStemMuted(0) && p.getActivePadNoteIndex() == 4, "section mute and pad note apply on seek");
  p.triggerPadNoteForSongTransition(2, 0);
  expect(p.getBasePadNoteIndex() == 2 && p.getActivePadNoteIndex() == 4 && p.getActivePadOctave() == -1,
      "automatic song-key follow changes only the base pad and respects the section preset");
  p.stopPadNote(2); // An unrelated note must not silence the effective preset.
  expect(p.getActivePadNoteIndex() == 4, "stopping an unrelated base note preserves automated note");
  p.setStemVolume(0, 0.55f);
  near(p.getStemVolume(0), 0.55, "manual changes temporarily take automated fader");
  near(p.getStemBaseVolume(0), 0.8, "temporary override leaves base mix unchanged");
  expect(p.beginSectionAutomationEditing("verse"), "another section edit can preview while manual mix exists");
  p.setStemVolume(0, 0.4f);
  expect(p.finishSectionAutomationEditing(false), "preview cancels without discarding live override");
  near(p.getStemVolume(0), 0.55, "cancel restores pre-edit manual fader override");
  p.seekSongTo(4.5); fixture.block();
  near(p.getStemVolume(0), 0.8, "gap after section restores base fader");
  expect(!p.isStemMuted(0) && p.getActivePadNoteIndex() == -1 && !p.isPadsEnabled(),
      "gap restores base stem mute but silences unassigned pads in an automated song");
  p.seekSongTo(2.25); fixture.block();
  near(p.getStemVolume(0), 0.3, "re-entry restores saved value after manual override expires");
  p.setStemVolume(0, 0.45f);
  p.seekSongTo(0.5); fixture.block();
  p.seekSongTo(2.25); fixture.block();
  near(p.getStemVolume(0), 0.3, "A-to-B-to-A cannot resurrect a previous manual override");
  auto withMissing = p.getSongArrangementSnapshot();
  SectionStemAutomation absent; absent.stemId = "temporarily-excluded"; absent.gain = 1.6f;
  withMissing.parts[1].automation.stems.push_back(absent);
  p.setSongArrangement(withMissing, false);
  expect(p.beginSectionAutomationEditing("chorus"), "existing sparse automation reopens");
  p.setPadsGain(0.9f);
  expect(!p.clearSectionAutomation("verse"), "clear cannot discard another pinned draft");
  expect(p.finishSectionAutomationEditing(true), "unrelated pad change saves");
  const auto afterMissing = p.getSongArrangementSnapshot();
  expect(std::any_of(afterMissing.parts[1].automation.stems.begin(), afterMissing.parts[1].automation.stems.end(), [](const auto &value) {
    return value.stemId == "temporarily-excluded" && value.gain == 1.6f;
  }), "saving preserves automation for temporarily missing instruments");
  expect(p.beginSectionAutomationEditing("chorus") && p.clearSectionAutomation("chorus"), "clear can remove the active draft and saved section");
  expect(!p.isSectionAutomationEditing() && !p.getSongArrangementSnapshot().parts[1].automation.hasOverrides(), "clear removes edit mode and committed overrides");
  near(p.getStemVolume(0), 0.8, "clear immediately restores base mix");
}

void testAudioBoundaryAndRamp() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  auto arrangement = p.getSongArrangementSnapshot();
  SectionStemAutomation stem; stem.stemId = "guitar"; stem.volume = 0.2f;
  arrangement.parts[1].automation.stems.push_back(stem);
  p.setSongArrangement(arrangement, false);
  p.seekSongTo(1.999); fixture.block();
  p.playSong(); fixture.block();
  expect(p.getSongCurrentPositionSeconds() > 2.0, "audio crosses section boundary without a UI timer");
  near(p.getStemVolume(0), 0.2, "audio publishes destination section immediately after crossing");
  fixture.seekAndWait(0.5);
  near(p.getStemVolume(0), 0.8, "audio seek restores original section base");

  juce::AudioFormatManager formats; formats.registerBasicFormats();
  auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(fixture.source));
  juce::TimeSliceThread readerThread("Automation ramp test");
  StemTrack track(std::move(reader), readerThread, 44100.0, 0, false);
  track.prepareToPlay(256, 44100.0);
  juce::AudioBuffer<float> audio(2, 256);
  juce::AudioSourceChannelInfo info(&audio, 0, 256);
  track.scheduleMixStateTransition(64, 0.0f, 1.0f, false);
  track.scheduleMixStateTransition(128, 1.0f, 1.0f, false);
  track.scheduleMixStateTransition(192, 0.0f, 1.0f, false);
  track.getNextAudioBlock(info);
  near(audio.getSample(0, 0), audio.getSample(0, 63), "scheduled target preserves prefix audio");
  expect(audio.getSample(0, 65) < audio.getSample(0, 63) && audio.getSample(0, 65) > 0.09f,
      "section boundary begins a gradual gain ramp instead of an abrupt mute");
  expect(audio.getSample(0, 130) > audio.getSample(0, 129) && audio.getSample(0, 194) < audio.getSample(0, 193),
      "a single read applies every queued mix event in order");
  for (int frame = 65; frame < 256; ++frame)
    expect(std::abs(audio.getSample(0, frame) - audio.getSample(0, frame - 1)) < 0.0002f, "gain transition has bounded sample slope");
  track.setVolume(0.0f);
  for (int block = 0; block < 6; ++block) track.getNextAudioBlock(info);
  expect(audio.getMagnitude(0, 0, 256) < 0.00001f, "30 ms ramp reaches the target mute");
}
void testLongBlockMultipleSectionEdges() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  p.setStemMute(1, true);
  p.prepareToPlay(44100.0, 8192);
  auto arrangement = p.getSongArrangementSnapshot();
  arrangement.parts[0].startSec = 1.8; arrangement.parts[0].endSec = 2.1;
  arrangement.parts[1].startSec = 2.15; arrangement.parts[1].endSec = 2.5;
  SectionStemAutomation first, second;
  first.stemId = second.stemId = "guitar";
  first.volume = 0.2f; second.volume = 0.7f;
  arrangement.parts[0].automation.stems = {first};
  arrangement.parts[1].automation.stems = {second};
  p.setSongArrangement(arrangement, false);
  p.seekSongTo(2.05);
  fixture.block();
  expect(SectionAutomationTestAccess::waitForSource(p, 8192),
      "large callback source bank is resident before playback");
  p.playSong();
  juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 8192); juce::MidiBuffer midi;
  p.processBlock(buffer, midi);
  near(p.getStemVolume(0), 0.7, "long callback reaches the second section after an annotation gap");
  near(buffer.getSample(0, 7000), 0.077, "audio applies gap restoration then second-section gain inside the same callback");
}

void testJumpIntentSurvivesLoopBlocks() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  auto arrangement = p.getSongArrangementSnapshot();
  arrangement.parts[0].muteInstruments = true;
  p.setSongArrangement(arrangement, true);
  p.setStemVolume(1, 0.4f);
  fixture.seekAndWait(2.5); p.playSong(); fixture.waitForPendingSeek();
  p.toggleArrangementPartLoop(1);
  p.requestArrangementPartJump(0);
  expect(p.getPreparingArrangementPartJumpIndex() == 0 && p.getPendingArrangementPartJumpIndex() == -1 && p.getArrangementPartLoopIndex() == 1, "preparation preserves Loop and has no confirmed queue");
  expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 0.0), "backward target prepares");
  fixture.block();
  expect(p.getPendingArrangementPartJumpIndex() == 0 && p.getSongCurrentPositionSeconds() > 2.5,
         "backward Ir stays queued through an interior block instead of disappearing or jumping early");
  expect(p.getArrangementPartLoopIndex() == -1, "Ir immediately cancels the Loop intent");
  expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 0.0), "backward destination is resident");
  fixture.block(280);
  expect(p.getPendingArrangementPartJumpIndex() == -1 && p.getSongCurrentPositionSeconds() < 0.3,
         "real audio transport consumes backward Ir at the current section end");
  expect(p.getSongArrangementSnapshot().parts[0].muteInstruments, "backward Ir preserves destination Mute");
  expect(SectionAutomationTestAccess::waitForSource(p, 256), "muted destination source is resident");
  juce::AudioBuffer<float> audio(p.getTotalNumOutputChannels(), 256); juce::MidiBuffer midi;
  p.processBlock(audio, midi);
  near(audio.getSample(0, 200), 0.04, "destination instruments stay muted while its real click remains audible");
  p.requestArrangementPartJump(1);
  expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 2.0), "revisited destination is resident");
  fixture.block(340);
  expect(p.getPendingArrangementPartJumpIndex() == -1 && p.getSongCurrentPositionSeconds() >= 2.0 && p.getSongCurrentPositionSeconds() < 2.4,
         "real transport can revisit the previous section after a backward jump");
  expect(p.getArrangementPartLoopIndex() == -1, "neither destination inherits the cancelled Loop");
}

void testIrPublishesBackwardPrefetchBeforeBoundary() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  fixture.seekAndWait(2.5); p.playSong(); fixture.waitForPendingSeek();
  p.requestArrangementPartJump(0);
  for (int callback = 0; callback < 100 && !SectionAutomationTestAccess::publishedTargetReady(p, 0.0); ++callback) {
    fixture.block();
    juce::Thread::sleep(6); // Approximate the 256-frame callback cadence; no target priming helper.
  }
  expect(SectionAutomationTestAccess::publishedTargetReady(p, 0.0),
         "Ir callbacks alone publish the backward target for the I/O worker");
  fixture.block(); // Confirm after the worker publishes readiness.
  expect(p.getPendingArrangementPartJumpIndex() == 0 && p.getSongCurrentPositionSeconds() < 4.0,
         "background preparation leaves Ir queued before the outgoing boundary");
  juce::AudioBuffer<float> audio(p.getTotalNumOutputChannels(), 256); juce::MidiBuffer midi;
  for (int callback = 0; callback < 300 && p.getPendingArrangementPartJumpIndex() == 0; ++callback) {
    expect(SectionAutomationTestAccess::waitForSource(p, 256), "accelerated harness keeps the ordinary source bank resident");
    p.processBlock(audio, midi);
    for (int offset : {0, 128})
      expect(audio.getMagnitude(0, offset, 128) > 0.000001f,
             "resident backward jump introduces no silent half-callback");
  }
  expect(p.getPendingArrangementPartJumpIndex() == -1 && p.getSongCurrentPositionSeconds() < 0.1,
         "prepared target executes at the section boundary without a held callback");
}

void testRepeatedLoopsStayInsideSection() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  p.prepareToPlay(44100.0, 8192); p.setStemMute(1, true);
  auto arrangement = p.getSongArrangementSnapshot();
  arrangement.parts[0].endSec = 0.25;
  arrangement.parts[1].startSec = 0.25; arrangement.parts[1].muteInstruments = true;
  p.setSongArrangement(arrangement, true);
  fixture.seekAndWait(0.23); p.playSong(); fixture.waitForPendingSeek();
  p.toggleArrangementPartLoop(0);
  juce::AudioBuffer<float> audio(p.getTotalNumOutputChannels(), 8192); juce::MidiBuffer midi;
  for (int iteration = 0; iteration < 12; ++iteration) {
    expect(SectionAutomationTestAccess::waitForSource(p, 8192) &&
           SectionAutomationTestAccess::waitForTransitionTarget(p, 0.0), "repeated loop audio banks are resident");
    p.processBlock(audio, midi);
    expect(p.getSongCurrentPositionSeconds() < 0.25 && p.getArrangementPartLoopIndex() == 0 &&
           SectionAutomationTestAccess::currentPart(p) == 0 && audio.getMagnitude(0, 0, 8192) > 0.05f,
           (juce::String("loop iteration=") + juce::String(iteration) + " position=" +
              juce::String(p.getSongCurrentPositionSeconds(), 6) + " loop=" + juce::String(p.getArrangementPartLoopIndex()) +
              " current=" + juce::String(SectionAutomationTestAccess::currentPart(p)) + " amplitude=" + juce::String(audio.getMagnitude(0, 0, 8192), 6)).toRawUTF8());
  }
}

void testColdJumpKeepsCurrentLoopAudible() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  p.setSectionEntryCacheBudget(2 * 32768 * 2 * sizeof(float)); // One entry per stem.
  fixture.seekAndWait(3.95); p.playSong(); fixture.waitForPendingSeek();
  expect(SectionAutomationTestAccess::waitForSource(p, 256) &&
         SectionAutomationTestAccess::waitForTransitionTarget(p, 2.0), "outgoing loop entry is resident");
  p.toggleArrangementPartLoop(1); fixture.block();
  SectionAutomationTestAccess::setReadThreadsRunning(p, false);
  p.requestArrangementPartJump(0);
  juce::AudioBuffer<float> audio(p.getTotalNumOutputChannels(), 256); juce::MidiBuffer midi;
  for (int callback = 0; callback < 24; ++callback) {
    p.processBlock(audio, midi);
    expect(p.getPendingArrangementPartJumpIndex() == -1 && p.getPreparingArrangementPartJumpIndex() == 0 &&
           p.getArrangementPartLoopIndex() == 1 && p.getSongCurrentPositionSeconds() >= 2.0 && p.getSongCurrentPositionSeconds() < 4.0,
           "cold target remains Preparing while the current Loop continues across its boundary");
    expect(audio.getMagnitude(0, 0, 256) > 0.02f, "preparing never introduces a silent song callback");
  }
  p.requestArrangementPartJump(1); // Current section cancels without stale completion.
  expect(p.getPreparingArrangementPartJumpIndex() == -1, "replaced preparation clears truthfully");
  p.toggleArrangementPartLoop(1); // Restore Loop after cancelling it via current-section Ir.
  p.requestArrangementPartJump(0); p.clearPendingArrangementPartJump();
  expect(p.getPreparingArrangementPartJumpIndex() == -1 && p.getPendingArrangementPartJumpIndex() == -1,
         "cancelled cold request has no confirmed command");
  p.requestArrangementPartJump(0);
  SectionAutomationTestAccess::setReadThreadsRunning(p, true);
  expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 0.0), "worker prepares cold destination");
  p.processBlock(audio, midi);
  expect(p.getPreparingArrangementPartJumpIndex() == -1 && p.getPendingArrangementPartJumpIndex() == 0 &&
         p.getArrangementPartLoopIndex() == -1 && p.getSongCurrentPositionSeconds() >= 2.0,
         "readiness confirms the next valid boundary and only then releases Loop");
  p.setSectionEntryCacheBudget(2 * 32768 * 2 * sizeof(float));
  for (int callback = 0; callback < 400 && p.getPendingArrangementPartJumpIndex() == 0; ++callback) {
    expect(SectionAutomationTestAccess::waitForSource(p, 256), "primary source stays resident in accelerated test");
    p.processBlock(audio, midi);
    expect(audio.getMagnitude(0, 0, 256) > 0.02f, "confirmed target remains audible until its future boundary");
  }
  expect(p.getPendingArrangementPartJumpIndex() == -1 && p.getSongCurrentPositionSeconds() < 0.1,
         "backward jump consumes at its future boundary");
  // Continue beyond the cached entry, at real callback cadence. This checks
  // the actual secondary-to-primary streaming handoff rather than UI flags.
  for (int callback = 0; callback < 160; ++callback) {
    p.processBlock(audio, midi); juce::Thread::sleep(6);
    expect(audio.getMagnitude(0, 0, 256) > 0.02f, "cached prefix hands off to the streaming reader without a silent callback");
  }
  expect(p.getSongCurrentPositionSeconds() > 32768.0 / 44100.0 && p.getSectionEntryCacheBytes() <= p.getSectionEntryCacheBudget(),
         "streaming passed the entry window within the reduced cache budget");
}

void testLateColdReadinessWaitsForFutureBoundary() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  auto arrangement = p.getSongArrangementSnapshot();
  SongArrangementPartSnapshot bridge; bridge.id = "bridge"; bridge.name = "Bridge"; bridge.startSec = 4.0; bridge.endSec = 6.0;
  arrangement.parts.push_back(bridge); p.setSongArrangement(arrangement, false);
  p.setSectionEntryCacheBudget(2 * 32768 * 2 * sizeof(float));
  fixture.seekAndWait(3.95); p.playSong(); fixture.waitForPendingSeek();
  expect(SectionAutomationTestAccess::waitForSource(p, 256) && SectionAutomationTestAccess::waitForTransitionTarget(p, 4.0),
         "natural continuation is resident before cold late Ir");
  SectionAutomationTestAccess::setReadThreadsRunning(p, false);
  p.requestArrangementPartJump(0);
  juce::AudioBuffer<float> audio(p.getTotalNumOutputChannels(), 256); juce::MidiBuffer midi;
  for (int callback = 0; callback < 48; ++callback) {
    p.processBlock(audio, midi);
    expect(p.getPendingArrangementPartJumpIndex() == -1 && p.getPreparingArrangementPartJumpIndex() == 0,
           "late cold intent never appears confirmed before audio is ready");
    expect(audio.getMagnitude(0, 0, 256) > 0.02f, "late preparation preserves current audio across the old boundary");
  }
  expect(p.getSongCurrentPositionSeconds() > 4.0, "Preparing follows current playback rather than freezing the missed boundary");
  SectionAutomationTestAccess::setReadThreadsRunning(p, true);
  expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 0.0), "late cold entry recovers");
  p.processBlock(audio, midi);
  expect(p.getPendingArrangementPartJumpIndex() == 0 && p.getSongCurrentPositionSeconds() > 4.0,
         "confirmation cannot jump back at the already passed boundary");
  for (int callback = 0; callback < 350 && p.getPendingArrangementPartJumpIndex() >= 0; ++callback) {
    expect(SectionAutomationTestAccess::waitForSource(p, 256), "future boundary source is resident");
    p.processBlock(audio, midi);
  }
  expect(p.getPendingArrangementPartJumpIndex() == -1 && p.getSongCurrentPositionSeconds() < 0.1,
         "late preparation consumes at the next actual section end");
}

void testPreparedJumpWithChangingTempo() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  fixture.seekAndWait(0.5); p.playSong(); fixture.waitForPendingSeek();
  p.toggleArrangementPartLoop(0); p.setPlaybackTempoBpm(144.0);
  expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 0.0), "tempo loop entry is resident");
  for (int callback = 0; callback < 80; ++callback) {
    expect(SectionAutomationTestAccess::waitForSource(p, 1024), "tempo warmup has resident source");
    fixture.block(); juce::Thread::sleep(6);
  }
  p.requestArrangementPartJump(1);
  expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 2.0), "tempo destination is resident");
  fixture.block();
  expect(p.getPendingArrangementPartJumpIndex() == 1 && p.getArrangementPartLoopIndex() == -1,
         "tempo readiness confirms Ir and releases Loop");
  p.setPlaybackTempoBpm(156.0);
  for (int callback = 0; callback < 400 && p.getPendingArrangementPartJumpIndex() >= 0; ++callback) {
    expect(SectionAutomationTestAccess::waitForSource(p, 1024), "tempo-changing transition keeps the source readable");
    fixture.block(); juce::Thread::sleep(6);
  }
  expect(p.getPendingArrangementPartJumpIndex() == -1 && SectionAutomationTestAccess::currentPart(p) == 1 &&
         p.getSongCurrentPositionSeconds() >= 2.0 && p.getSongCurrentPositionSeconds() < 2.05,
         "tempo changes preserve the exact source boundary and target section");
}

void testRealLoopAndJumpSplitPreserveDestinationMix() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  p.prepareToPlay(44100.0, 8192);
  p.setStemMute(1, true);
  auto arrangement = p.getSongArrangementSnapshot();
  SectionStemAutomation first, second;
  first.stemId = second.stemId = "guitar";
  first.volume = 0.2f; first.gain = 1.3f;
  second.volume = 0.7f; second.gain = 1.6f;
  arrangement.parts[0].automation.stems = {first};
  arrangement.parts[1].automation.stems = {second};
  p.setSongArrangement(arrangement, true);
  fixture.seekAndWait(1.95);
  p.setStemVolume(0, 0.55f); p.setStemGain(0, 1.4f);
  p.toggleArrangementPartLoop(0);
  expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 1.95),
      "custom arrangement Play seek is resident before its acceptance callback");
  p.playSong();
  // Custom arrange re-seeks its current position on Play. Accept that command
  // with a short callback before testing the independent loop handoff.
  fixture.waitForPendingSeek();
  expect(SectionAutomationTestAccess::waitForSource(p, 8192) &&
      SectionAutomationTestAccess::waitForTransitionTarget(p, 0.0),
      "loop source and wrap destination are resident before the split callback");
  juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 8192);
  juce::MidiBuffer midi;
  p.processBlock(buffer, midi);
  expect(p.getSongCurrentPositionSeconds() > 0.1 &&
      p.getSongCurrentPositionSeconds() < 0.2 &&
      p.getArrangementPartLoopIndex() == 0,
      (juce::String("real section loop renders a prefix before wrapping inside the callback; position=") +
          juce::String(p.getSongCurrentPositionSeconds(), 6) + " loop=" +
          juce::String(p.getArrangementPartLoopIndex()) + " playing=" +
          juce::String(p.isSongPlaying() ? 1 : 0) + " tempoRatio=" +
          juce::String(SectionAutomationTestAccess::tempoRatio(p), 6)).toRawUTF8());
  near(p.getStemVolume(0), 0.55, "same-ID loop preserves the manual volume override");
  near(p.getStemGain(0), 1.4, "same-ID loop preserves the manual gain override");
  near(buffer.getSample(0, 7000), 0.077,
      "loop suffix renders the effective manual mix rather than the saved preset");

  p.pauseSong();
  p.toggleArrangementPartLoop(0);
  arrangement.parts[0].muteInstruments = true;
  arrangement.parts[1].startSec = 3.0;
  arrangement.parts[1].endSec = 5.0;
  p.setSongArrangement(arrangement, true);
  fixture.seekAndWait(1.95);
  expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 1.95),
      "custom arrangement jump Play seek is resident before acceptance");
  p.playSong();
  fixture.waitForPendingSeek();
  expect(SectionAutomationTestAccess::waitForSource(p, 8192),
      "jump source is resident before playback");
  p.requestArrangementPartJump(1);
  expect(p.getPreparingArrangementPartJumpIndex() == 1 &&
      SectionAutomationTestAccess::waitForTransitionTarget(p, 3.0),
      "playing queues a real section jump with a resident destination");
  p.processBlock(buffer, midi);
  expect(p.getSongCurrentPositionSeconds() > 3.1 &&
      p.getSongCurrentPositionSeconds() < 3.2 &&
      p.getPendingArrangementPartJumpIndex() == -1,
      "queued jump renders the muted origin prefix and the destination suffix in one callback");
  near(p.getStemVolume(0), 0.7, "real jump selects destination section volume");
  near(p.getStemGain(0), 1.6, "real jump selects destination section gain");
  expect(buffer.getMagnitude(0, 1500, 300) < 0.00001f,
      "origin mute settles before the jump boundary");
  near(buffer.getSample(0, 7000), 0.112,
      "muted source gate never leaks into the unmuted destination suffix");
}

void testPadsAreIdempotentAndRestoreBase() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  for (int attempt = 0; attempt < 100 && !p.isPadsPackLoaded(); ++attempt) juce::Thread::sleep(10);
  expect(p.isPadsPackLoaded() && p.ensurePadsNoteReady(4), "local pad attack is ready before realtime rendering");
  p.setStemMute(0, true); p.setStemMute(1, true);
  auto arrangement = p.getSongArrangementSnapshot();
  arrangement.parts[0].automation.pads.noteIndex = 4;
  arrangement.parts[0].automation.pads.enabled = true;
  arrangement.parts[0].automation.pads.volume = 1.0f;
  arrangement.parts[0].automation.pads.gain = 1.0f;
  arrangement.parts[1].automation.pads = arrangement.parts[0].automation.pads;
  arrangement.parts[1].endSec = 5.0;
  p.setSongArrangement(arrangement, false);
  p.seekSongTo(0.5); fixture.block(4);
  expect(p.getPadsOutputLevel() == 0.0f && p.getActivePadNoteIndex() == 4,
      "load and paused seek select pad values without starting automated sound");
  p.playSong(); fixture.block(700); p.pauseSong();
  expect(p.getPadsOutputLevel() > 0.095f && p.getPadsOutputLevel() < 0.105f,
      "repeated callbacks sustain one pad voice rather than retriggering its attack");
  for (int entry = 0; entry < 16; ++entry) {
    p.seekSongTo((entry & 1) ? 0.5 : 2.5);
    fixture.block(4);
  }
  expect(p.getPadsOutputLevel() > 0.095f && p.getPadsOutputLevel() < 0.105f,
      "section re-entry with the same note preserves phase and envelope");
  arrangement.parts[0].automation.pads.volume = 0.1f;
  arrangement.parts[1].automation.pads = {};
  p.setSongArrangement(arrangement, false);
  p.seekSongTo(0.5); fixture.block(700);
  expect(p.getPadsOutputLevel() > 0.0095f && p.getPadsOutputLevel() < 0.0105f,
      "automated pad settles at its low section volume");
  p.seekSongTo(2.5); fixture.block(1);
  expect(p.getActivePadNoteIndex() == -1 && !p.isPadsEnabled() &&
         p.getPadsOutputLevel() <= 0.0105f,
      "moving into a section without pads cannot raise the fading tail to base level");
  fixture.block(600);
  expect(p.getPadsOutputLevel() < 0.00001f,
      "unassigned section reaches complete pad silence");
  p.seekSongTo(5.5); fixture.block(600);
  expect(p.getActivePadNoteIndex() == -1 && p.getPadsOutputLevel() < 0.00001f,
      "leaving automated sections restores base pad note-off with its normal fade");
  p.triggerPadNoteForSongTransition(2, 0);
  expect(p.unloadSong(), "automated song unloads for the song-transition check");
  p.setSongArrangement({}, false);
  p.triggerPadNoteForSongTransition(4, 0);
  fixture.block(600);
  expect(!p.isPadsEnabled() && p.getActivePadNoteIndex() == -1 &&
         p.getPadsOutputLevel() < 0.00001f,
      "new song without programmed pads cannot resurrect the global note through key following");
  p.setPadsEnabled(true);
  p.triggerPadNote(4, 0);
  fixture.block(700);
  expect(p.isPadsEnabled() && p.getActivePadNoteIndex() == 4 && p.getPadsOutputLevel() > 0.0f,
      "an explicit manual pad gesture remains available after an automated song ends");

}

void testRemoteDraftRejectsPhysicalMidi() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  const auto bind = [&p](int target, int cc) {
    MTControlsAudioProcessor::MidiBinding binding;
    binding.targetId = target; binding.ccNumber = cc; binding.midiChannel = 1;
    binding.mode = MidiBindingMode::Absolute;
    p.setMidiBinding(binding);
  };
  bind(p.midiTargetStemVolumeBase, 9);
  bind(p.midiTargetMasterVolume, 10);
  bind(p.midiTargetStemVolumeBase + 1, 11);
  expect(p.beginSectionAutomationEditing("chorus", true), "remote owner draft begins atomically with ownership");
  juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 256); juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::controllerEvent(1, 9, 32), 0);
  midi.addEvent(juce::MidiMessage::controllerEvent(1, 10, 64), 1);
  midi.addEvent(juce::MidiMessage::controllerEvent(1, 11, 16), 2);
  p.processBlock(buffer, midi);
  near(p.getStemVolume(0), 0.8, "physical MIDI cannot overwrite another client's instrument draft");
  near(p.getMasterVolume(), 64.0 / 127.0, "master MIDI remains independent during a remote draft");
  near(p.getStemBaseVolume(1), 16.0 / 127.0, "excluded click MIDI remains available during a remote draft");
  expect(p.finishSectionAutomationEditing(true), "remote owner finishes");
  expect(!p.getSongArrangementSnapshot().parts[1].automation.hasOverrides(), "rejected MIDI does not enter committed automation");
  expect(p.beginSectionAutomationEditing("chorus"), "local owner draft begins");
  midi.clear(); midi.addEvent(juce::MidiMessage::controllerEvent(1, 9, 48), 0);
  p.processBlock(buffer, midi);
  near(p.getStemVolume(0), 48.0 / 127.0, "local automation intentionally captures physical MIDI controls");
  near(p.getStemBaseVolume(0), 0.8, "local MIDI draft still preserves base mix");
  expect(p.finishSectionAutomationEditing(true) && p.getSongArrangementSnapshot().parts[1].automation.hasOverrides(), "local MIDI draft commits");
}

void testPendingJumpOwnsSectionGuide() {
  const auto setup = [](ProcessorFixture &fixture, double start) {
    auto &p = *fixture.processor;
    const auto directory = ApplicationDataPaths::isolatedTestRoot().getChildFile("pending-jump-guide");
    writeWav(directory.getChildFile("intro.wav"), 16, 0.1f);
    writeWav(directory.getChildFile("verse.wav"), 16, 0.2f);
    writeWav(directory.getChildFile("chorus.wav"), 16, 0.3f);
    writeWav(directory.getChildFile("bridge.wav"), 16, 0.4f);
    expect(SectionAutomationTestAccess::loadGuides(p, directory), "distinct pending-jump guide voices load");
    auto settings = p.getDynamicGuideSettings(); settings.enabled = true; p.setDynamicGuideSettings(settings);
    p.setMetronomeBeatsPerBar(1); // Half-second guide window at 120 BPM.
    auto arrangement = p.getSongArrangementSnapshot(); arrangement.parts.clear();
    const char *names[] = {"Intro", "Verse", "Chorus", "Bridge"};
    const double edges[] = {0.0, 1.0, 3.0, 4.0, 6.0};
    for (int i = 0; i < 4; ++i) {
      SongArrangementPartSnapshot part; part.id = names[i]; part.name = names[i];
      part.startSec = edges[i]; part.endSec = edges[i + 1]; arrangement.parts.push_back(part);
    }
    p.setSongArrangement(arrangement, false);
    fixture.seekAndWait(start);
    p.playSong(); fixture.waitForPendingSeek();
  };
  const auto peak = [](ProcessorFixture &fixture, int blocks) {
    float value = 0.0f;
    for (int b = 0; b < blocks; ++b) {
      fixture.block();
      for (int i = 0; i < 256; ++i)
        value = std::max(value, SectionAutomationTestAccess::guideSample(*fixture.processor, i));
    }
    return value;
  };
  {
    ProcessorFixture f; setup(f, 2.48); auto &p = *f.processor;
    p.setSectionEntryCacheBudget(2 * 32768 * 2 * sizeof(float));
    expect(SectionAutomationTestAccess::waitForCacheBudget(p), "unleased guide entries trim before a deterministic cold request");
    expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 1.0), "loop guide source prewarms before cold request");
    p.toggleArrangementPartLoop(1); f.block();
    SectionAutomationTestAccess::setReadThreadsRunning(p, false);
    p.requestArrangementPartJump(3);
    const auto value = peak(f, 6);
    expect(p.getPreparingArrangementPartJumpIndex() == 3 && p.getPendingArrangementPartJumpIndex() == -1 && SectionAutomationTestAccess::announcedPendingTarget(p) == -1 && value < 0.35f,
           "Preparing never announces the unconfirmed Bridge target");
    p.clearPendingArrangementPartJump();
    SectionAutomationTestAccess::setReadThreadsRunning(p, true);
  }
  {
    ProcessorFixture f; setup(f, 2.48);
    near(peak(f, 4), 0.3, "without pending jump the normal next-section voice remains");
  }
  {
    ProcessorFixture f; setup(f, 2.48); auto &p = *f.processor;
    p.toggleArrangementPartLoop(1);
    p.requestArrangementPartJump(0);
    expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 0.0), "Intro ready for confirmation");
    near(peak(f, 4), 0.1, "pending Intro suppresses both sequential Chorus and outgoing Loop announcements");
    near(peak(f, 2), 0.0, "stable pending destination announces only once");
    p.requestArrangementPartJump(3);
    expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 4.0), "Bridge ready for confirmation");
    near(peak(f, 1), 0.4, "changing a late pending target announces only the new Bridge");
    p.clearPendingArrangementPartJump();
    p.clearArrangementPartLoop(); // Restore natural next section.
    near(peak(f, 1), 0.3, "cancel in the remaining lead-in window restores the suppressed sequential voice");
    p.requestArrangementPartJump(0);
    expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 0.0), "Intro ready for confirmation");
    near(peak(f, 1), 0.1, "requeuing an already announced target rearms its guide");
  }
  {
    ProcessorFixture f; setup(f, 2.48); auto &p = *f.processor;
    p.requestArrangementPartJump(0); p.clearPendingArrangementPartJump();
    near(peak(f, 4), 0.3, "cancelling before the guide window preserves normal announcements");
  }
  {
    ProcessorFixture f; setup(f, 2.99); auto &p = *f.processor;
    const auto directory = ApplicationDataPaths::isolatedTestRoot().getChildFile("pending-jump-guide");
    writeWav(directory.getChildFile("intro.wav"), 1024, 0.1f);
    expect(SectionAutomationTestAccess::loadGuides(p, directory), "long late-destination voice loads");
    p.requestArrangementPartJump(0);
    expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 0.0), "late jump destination is resident");
    int voicedSamples = 0;
    for (int b = 0; b < 5; ++b) {
      f.block();
      for (int i = 0; i < 256; ++i)
        if (SectionAutomationTestAccess::guideSample(p, i) > 0.05f) ++voicedSamples;
    }
    expect(p.getPendingArrangementPartJumpIndex() == -1 && SectionAutomationTestAccess::currentPart(p) == 0,
           "late jump still executes at the existing section boundary");
    expect(voicedSamples == 1024, "late destination voice continues completely across the jump without duplication");
  }
}

void testIndependentGuideCuesFollowTransport() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  const auto directory = ApplicationDataPaths::isolatedTestRoot().getChildFile("guide-transport");
  directory.createDirectory();
  writeWav(directory.getChildFile("1.wav"), 16);
  writeWav(directory.getChildFile("2.wav"), 16);
  writeWav(directory.getChildFile("3.wav"), 16);
  expect(SectionAutomationTestAccess::loadGuides(p, directory), "transport guide fixture loads");
  auto settings = p.getDynamicGuideSettings(); settings.enabled = true;
  p.setDynamicGuideSettings(settings);
  auto arrangement = p.getSongArrangementSnapshot();
  SongArrangementCueSnapshot count;
  count.id = "count"; count.name = "1"; count.partId = "verse";
  count.offsetSec = 128.0 / 44100.0; count.positionSec = count.offsetSec;
  count.countEnd = 3; count.countIntervalSec = 0.0;
  count.countOffsetsSec = {0.0, 64.0 / 44100.0, 128.0 / 44100.0};
  arrangement.cues.push_back(count);
  p.setSongArrangement(arrangement, false);
  const auto snapshot = SectionAutomationTestAccess::scheduler(p);
  expect(snapshot.count == 2 && snapshot.cueCount == 3 && snapshot.cues[0].frame == 128 &&
      snapshot.cues[1].frame == 192 && snapshot.cues[2].frame == 256,
      "counts publish a coherent independent timeline without new sections");
  fixture.seekAndWait(0.0); fixture.block(3);
  expect(SectionAutomationTestAccess::guideSample(p, 128) == 0.0f,
      "paused callbacks do not repeatedly trigger cues at the playhead");
  p.playSong(); fixture.block();
  expect(SectionAutomationTestAccess::guideSample(p, 127) == 0.0f &&
      SectionAutomationTestAccess::guideSample(p, 128) > 0.099f &&
      SectionAutomationTestAccess::guideSample(p, 144) == 0.0f &&
      SectionAutomationTestAccess::guideSample(p, 192) > 0.099f &&
      SectionAutomationTestAccess::currentPart(p) == 0 && !p.isSectionAutomationEditing(),
      "real playback starts each count on the sample while primary section and automation remain unchanged");
  fixture.block();
  expect(SectionAutomationTestAccess::guideSample(p, 0) > 0.099f &&
      SectionAutomationTestAccess::guideSample(p, 16) == 0.0f,
      "a cue on the callback boundary fires once in the following callback");
  p.pauseSong(); fixture.seekAndWait(0.1); p.playSong(); fixture.block();
  expect(SectionAutomationTestAccess::guideSample(p, 0) == 0.0f,
      "seeking beyond a count sequence never replays old instructions");
  p.pauseSong(); fixture.seekAndWait(0.0); p.playSong(); fixture.block();
  expect(SectionAutomationTestAccess::guideSample(p, 128) > 0.099f,
      "seeking back repeats only cues actually crossed in the new traversal");

  // Force a split callback: 44 source frames remain before Verse ends.
  // The outgoing count is 28 frames into that prefix and the destination
  // count is 32 frames into the suffix. Neither changes musical boundaries.
  p.pauseSong();
  arrangement.cues.clear();
  SongArrangementCueSnapshot outgoing, incoming, chorusCue;
  outgoing.id = "outgoing"; outgoing.name = "1"; outgoing.positionSec = (88200.0 - 16.0) / 44100.0;
  incoming.id = "incoming"; incoming.name = "2"; incoming.positionSec = 32.0 / 44100.0;
  chorusCue.id = "chorus-cue"; chorusCue.name = "3"; chorusCue.positionSec = (88200.0 + 32.0) / 44100.0;
  arrangement.cues = {outgoing, incoming, chorusCue};
  p.setSongArrangement(arrangement, true);
  fixture.seekAndWait(87900.0 / 44100.0);
  p.toggleArrangementPartLoop(0);
  expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 87900.0 / 44100.0),
      "custom Play seek is resident before acceptance");
  p.playSong(); fixture.waitForPendingSeek();
  near(p.getSongCurrentPositionSeconds(), 88156.0 / 44100.0,
      "Play seek is accepted one short callback before the loop boundary");
  expect(SectionAutomationTestAccess::waitForSource(p, 256) &&
      SectionAutomationTestAccess::waitForTransitionTarget(p, 0.0), "loop cue source and destination are resident");
  fixture.block();
  expect(p.getSongCurrentPositionSeconds() > 0.0 && p.getSongCurrentPositionSeconds() < 0.005 &&
      SectionAutomationTestAccess::currentPart(p) == 0 &&
      SectionAutomationTestAccess::guideSample(p, 27) == 0.0f &&
      SectionAutomationTestAccess::guideSample(p, 28) > 0.099f &&
      SectionAutomationTestAccess::guideSample(p, 44) == 0.0f &&
      SectionAutomationTestAccess::guideSample(p, 75) == 0.0f &&
      SectionAutomationTestAccess::guideSample(p, 76) > 0.099f,
      "loop split renders outgoing cue and destination cue at exact offsets without ghost instructions");
  p.pauseSong(); p.toggleArrangementPartLoop(0);
  fixture.seekAndWait(87900.0 / 44100.0);
  expect(SectionAutomationTestAccess::waitForTransitionTarget(p, 87900.0 / 44100.0),
      "custom jump Play seek is resident before acceptance");
  p.playSong(); fixture.waitForPendingSeek();
  near(p.getSongCurrentPositionSeconds(), 88156.0 / 44100.0,
      "Play seek is accepted one short callback before the jump boundary");
  expect(SectionAutomationTestAccess::waitForSource(p, 256), "jump source is resident");
  p.requestArrangementPartJump(1); // Playing queues; paused would seek immediately.
  expect(p.getPreparingArrangementPartJumpIndex() == 1 &&
      SectionAutomationTestAccess::waitForTransitionTarget(p, 2.0), "jump cue destination prefetch is resident");
  fixture.block();
  expect(p.getSongCurrentPositionSeconds() > 2.0 && p.getSongCurrentPositionSeconds() < 2.005 &&
      p.getPendingArrangementPartJumpIndex() == -1 && SectionAutomationTestAccess::currentPart(p) == 1 &&
      SectionAutomationTestAccess::guideSample(p, 28) > 0.099f &&
      SectionAutomationTestAccess::guideSample(p, 75) == 0.0f &&
      SectionAutomationTestAccess::guideSample(p, 76) > 0.099f,
      "jump split follows destination instructions without changing the selected musical section");
  arrangement.cues = {count};
  p.pauseSong();
  arrangement.parts[0].skipPlayback = true;
  p.setSongArrangement(arrangement, true);
  expect(SectionAutomationTestAccess::scheduler(p).cues[0].skipPlayback,
      "normal playback retains the skipped parent of an instruction in its coherent snapshot");
  fixture.seekAndWait(0.0); p.playSong(); fixture.block();
  expect(SectionAutomationTestAccess::guideSample(p, 128) == 0.0f,
      "normal playback suppresses instructions from an omitted section");
  p.pauseSong();
  expect(p.setPracticeLoopRange(0.0, 0.25), "practice loop accepts the omitted section for rehearsal");
  fixture.seekAndWait(0.0); p.playSong(); fixture.block();
  expect(SectionAutomationTestAccess::guideSample(p, 128) > 0.099f,
      "practice playback restores instructions along with the omitted section's audio");
  p.clearPracticeLoop();

  p.pauseSong();
  arrangement.parts.clear(); p.setSongArrangement(arrangement, false);
  expect(SectionAutomationTestAccess::scheduler(p).count == 1 &&
      SectionAutomationTestAccess::scheduler(p).cueCount == 3,
      "cue-only songs retain their default primary audio range and independent instructions");
  fixture.seekAndWait(0.0); p.playSong(); fixture.block();
  expect(SectionAutomationTestAccess::guideSample(p, 128) > 0.099f,
      "cue-only songs play their instructions without user-created musical sections");

  p.pauseSong();
  writeWav(directory.getChildFile("build.wav"), 16);
  expect(SectionAutomationTestAccess::loadGuides(p, directory), "legacy cue fixture reloads");
  SongArrangementSnapshot legacy;
  SongArrangementPartSnapshot left, utility, right;
  left.id = "legacy-verse"; left.name = "Verse"; left.startSec = 0.0; left.endSec = 1.0;
  left.sourceStartSec = 0.0; left.sourceEndSec = 1.0;
  utility.id = "legacy-build"; utility.name = "Build"; utility.utilityCue = true;
  utility.startSec = 1.0; utility.endSec = 1.25;
  utility.sourceStartSec = 1.0; utility.sourceEndSec = 1.25;
  right.id = "legacy-verse-rest"; right.name = "Verse";
  right.startSec = 1.25; right.endSec = 2.0;
  right.sourceStartSec = 1.25; right.sourceEndSec = 2.0;
  legacy.parts = {left, utility, right};
  p.setSongArrangement(legacy, true);
  const auto migrated = p.getSongArrangementSnapshot();
  expect(migrated.parts.size() == 1 && migrated.cues.size() == 1 &&
      migrated.parts[0].id == left.id && migrated.parts[0].endSec == 2.0 &&
      SectionAutomationTestAccess::scheduler(p).count == 1 &&
      SectionAutomationTestAccess::scheduler(p).cueCount == 1,
      "direct processor callers migrate provably continuous legacy fragments into one musical section and one instruction");
  fixture.seekAndWait((44100.0 - 128.0) / 44100.0); p.playSong(); fixture.block();
  expect(SectionAutomationTestAccess::guideSample(p, 127) == 0.0f &&
      SectionAutomationTestAccess::guideSample(p, 128) > 0.099f &&
      SectionAutomationTestAccess::currentPart(p) == 0,
      "migrated utility plays at its old exact point without a musical-section transition");


  p.pauseSong();
  writeWav(directory.getChildFile("break.wav"), 16, 0.1f);
  writeWav(directory.getChildFile("breakdown.wav"), 16, 0.2f);
  expect(SectionAutomationTestAccess::loadGuides(p, directory), "distinct Break and Breakdown voices load");
  auto utilityArrangement = p.getSongArrangementSnapshot();
  utilityArrangement.cues.clear();
  SongArrangementCueSnapshot pause;
  pause.id = "pause"; pause.name = "Break"; pause.positionSec = 128.0 / 44100.0;
  utilityArrangement.cues.push_back(pause);
  p.setSongArrangement(utilityArrangement, false);
  fixture.seekAndWait(0.0); p.playSong(); fixture.block();
  expect(std::abs(SectionAutomationTestAccess::guideSample(p, 128) - 0.1f) < 0.0001f,
      "Break utility uses its exact Pausa voice before musical alias fallback");
  p.pauseSong();
  expect(SectionAutomationTestAccess::loadGuides(p, directory), "guide pack reload recompiles cue trigger IDs");
  fixture.seekAndWait(0.0); p.playSong(); fixture.block();
  expect(std::abs(SectionAutomationTestAccess::guideSample(p, 128) - 0.1f) < 0.0001f,
      "guide reload preserves the utility identity rather than mapping Break to Breakdown");


  p.pauseSong();
  writeWav(directory.getChildFile("break.wav"), 1024, 0.1f);
  expect(SectionAutomationTestAccess::loadGuides(p, directory), "long cue fixture loads");
  fixture.seekAndWait(0.0); p.playSong(); fixture.block();
  expect(SectionAutomationTestAccess::guideSample(p, 200) > 0.099f,
      "long cue begins its audible tail");
  p.setDynamicGuideMuted(true); fixture.block(10);
  p.setDynamicGuideMuted(false); fixture.block();
  expect(SectionAutomationTestAccess::guideSample(p, 0) == 0.0f &&
      SectionAutomationTestAccess::guideSample(p, 200) == 0.0f,
      "muting consumes the cue and its delay history instead of resurrecting old speech after unmute");

}

void testSongKeyFollowUsesBaseAndNeverEchoesRemote() {
  ProcessorFixture fixture; auto &p = *fixture.processor;
  auto arrangement = p.getSongArrangementSnapshot();
  arrangement.parts[0].automation.pads.noteIndex = 4;
  arrangement.parts[0].automation.pads.octave = 1;
  arrangement.parts[0].automation.pads.enabled = true;
  p.setSongArrangement(arrangement, false);
  fixture.seekAndWait(0.0);
  ChannelStripLookAndFeel lookAndFeel;
  PadsComponent pads(p, lookAndFeel);
  expect(!pads.followSongKey("D") && p.getBasePadNoteIndex() == -1 &&
      p.getActivePadNoteIndex() == 4,
      "startup follows a silent base without overwriting an automated first-section note");
  p.triggerPadNoteForSongTransition(2, -1);
  expect(pads.followSongKey("G") && p.getBasePadNoteIndex() == 7 &&
      p.getBasePadOctave() == -1 && p.getActivePadNoteIndex() == 4 &&
      p.getActivePadOctave() == 1,
      "song-key follow updates the global pad while preserving both base and automated octave contexts");

  PlaybackSession remote(p);
  remote.setRemote(true); remote.setConnected(true);
  int commands = 0;
  remote.onCommand = [&commands](const juce::String &, const juce::String &,
      const juce::var &, const juce::String &) { ++commands; };
  PadsComponent remotePads(p, lookAndFeel, &remote);
  commands = 0;
  expect(!remotePads.followSongKey("D") && commands == 0 &&
      p.getBasePadNoteIndex() == 7,
      "remote song-key following emits no intentional pad command to the server");
}

}

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr) return 77;
#endif
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  int failures = 0;
  const auto run = [&failures](const char *name, auto test) {
    try { test(); std::cout << "PASS: " << name << '\n'; }
    catch (const std::exception &error) { ++failures; std::cerr << "FAIL: " << name << ": " << error.what() << '\n'; }
  };
  run("bounded coherent timeline selection", testTimelineSelectionAndPublication);
  run("sparse draft save cancel base and remote-ready state", testDraftSaveCancelAndBaseMix);
  run("audio section boundary and smoothed mix targets", testAudioBoundaryAndRamp);
  run("long callbacks cross every section and annotation gap", testLongBlockMultipleSectionEdges);
  run("real loop and queued jump apply destination mix inside split callbacks", testRealLoopAndJumpSplitPreserveDestinationMix);
  run("Ir intent survives interior Loop blocks", testJumpIntentSurvivesLoopBlocks);
  run("Ir publishes backward prefetch before its boundary", testIrPublishesBackwardPrefetchBeforeBoundary);
  run("repeated loops stay inside their section", testRepeatedLoopsStayInsideSection);
  run("late cold readiness waits for a future boundary", testLateColdReadinessWaitsForFutureBoundary);
  run("prepared jump follows changing tempo", testPreparedJumpWithChangingTempo);
  run("cold jump keeps the current Loop audible", testColdJumpKeepsCurrentLoopAudible);
  run("pads remain idempotent across section re-entry", testPadsAreIdempotentAndRestoreBase);
  run("remote ownership excludes physical MIDI draft writes", testRemoteDraftRejectsPhysicalMidi);
  run("independent guide cues follow real playback and seeking", testIndependentGuideCuesFollowTransport);
  run("pending jump owns section announcements", testPendingJumpOwnsSectionGuide);
  run("startup song-key follow preserves automation and remote ownership", testSongKeyFollowUsesBaseAndNeverEchoesRemote);
  return failures == 0 ? 0 : 1;
}
