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
  static bool waitForTransitionTarget(MTControlsAudioProcessor &processor,
                                      double seconds) {
    const auto frame = static_cast<juce::int64>(std::llround(seconds * 44100.0));
    // Match the runtime's anticipatory request so its next callback observes
    // the same resident target rather than issuing a new generation.
    constexpr int frames = 32768;
    for (auto &track : processor.stemTracks)
      track->requestPrefetchPosition(frame, frames);
    for (int attempt = 0; attempt < 500; ++attempt) {
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
void writeWav(const juce::File &file, const int frames) {
  file.getParentDirectory().createDirectory();
  auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
  juce::WavAudioFormat format;
  auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
      .withSampleRate(44100.0).withNumChannels(1).withBitsPerSample(16));
  expect(writer != nullptr, "fixture WAV opens");
  juce::AudioBuffer<float> audio(1, 4096);
  for (int frame = 0; frame < audio.getNumSamples(); ++frame) audio.setSample(0, frame, 0.1f);
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
  expect(p.getPendingArrangementPartJumpIndex() == 1 &&
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
  run("pads remain idempotent across section re-entry", testPadsAreIdempotentAndRestoreBase);
  run("remote ownership excludes physical MIDI draft writes", testRemoteDraftRejectsPhysicalMidi);
  run("startup song-key follow preserves automation and remote ownership", testSongKeyFollowUsesBaseAndNeverEchoesRemote);
  return failures == 0 ? 0 : 1;
}
