#include "PluginProcessor.h"
#include "audio/RehearsalMix.h"
#include "network/PlaybackSession.h"
#include "network/RemoteStateCodec.h"
#include "ui/panels/RehearsalPanel.h"
#include "ui/panels/TopBar.h"
#include "ui/panels/WaveformBar.h"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void expect(bool ok, const char *what) {
  if (!ok) { std::cerr << "FAILED: " << what << '\n'; std::exit(1); }
}
ImportedSongData song() {
  ImportedSongData s;
  s.id = "test-song"; s.title = "Ensayo de instrumentos"; s.artist = "PlayWorship";
  s.bpm = 120; s.key = "C"; s.timeSignature = "4/4";
  for (auto category : {"Click", "Cues", "Bass", "Drums", "Drums", "Other"}) {
    ImportedStemData stem;
    stem.category = category; stem.displayName = category;
    stem.stemId = "stem-" + juce::String(static_cast<int>(s.stems.size()));
    s.stems.push_back(stem);
  }
  auto excluded = s.stems.back(); excluded.include = false; excluded.stemId = "excluded";
  s.stems.push_back(excluded);
  s.arrangement.parts.push_back({"intro", "Intro", "", 0.0, 12.0});
  s.arrangement.parts.push_back({"verse", "Verse", "", 12.0, 30.0});
  s.arrangement.parts.push_back({"chorus", "Chorus", "", 30.0, 48.0});
  return s;
}
void groupsAndValidation() {
  RehearsalMix mix;
  mix.configure(song());
  expect(mix.snapshot().getProperty("groups", {}).size() == 3, "categories group stems, Other stays individual, click/cues excluded");
  expect(!mix.apply("rehearsal.select", "old-song", "category:bass"), "stale song command rejected");
  expect(!mix.apply("rehearsal.select", "test-song", "category:click"), "click cannot be focused");
  expect(!mix.apply("rehearsal.select", "test-song", "unknown"), "unknown group rejected");
  expect(mix.apply("rehearsal.select", "test-song", "category:bass"), "select bass");
  auto g = mix.gains();
  expect(g.forStem(0) == 1 && g.forStem(1) == 1, "click and cues unchanged");
  expect(g.forStem(2) == 1 && g.forStem(3) == .25f && g.forStem(4) == .25f, "group and accompaniment factors");
  for (const double invalid : {-1.0, 1.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    expect(!mix.apply("rehearsal.restLevel", "test-song", invalid), "invalid levels rejected");
  expect(!mix.apply("rehearsal.groupLevel", "test-song", "0.5"), "string level rejected");
  expect(mix.apply("rehearsal.groupLevel", "test-song", .5), "group attenuation accepted");
  expect(mix.apply("rehearsal.select", "test-song", "category:drums"), "select drums");
  g = mix.gains();
  expect(g.forStem(2) == .25f && g.forStem(3) == .5f && g.forStem(4) == .5f, "switch groups without cumulative attenuation");
  mix.reset();
  for (int i = 0; i < 64; ++i) expect(mix.gains().forStem(i) == 1, "normal mix restores unity");
  mix.apply("rehearsal.select", "test-song", "category:bass");
  auto next = song(); next.id = "next"; mix.configure(next);
  expect(mix.gains().forStem(2) == 1 && !mix.apply("rehearsal.clear", "test-song", {}), "new song resets and rejects old IDs");
  mix.clear(); expect(mix.snapshot().getProperty("songId", {}).toString().isEmpty(), "unload clears identity");
}

class ConstantReader : public juce::AudioFormatReader {
public:
  ConstantReader() : AudioFormatReader(nullptr, "constant") {
    sampleRate = 48000; bitsPerSample = 32; lengthInSamples = 480000;
    numChannels = 2; usesFloatingPointData = true;
  }
  bool readSamples(int *const *dest, int channels, int offset, juce::int64, int count) override {
    for (int c = 0; c < channels; ++c) if (dest[c])
      std::fill_n(reinterpret_cast<float *>(dest[c]) + offset, count, .8f);
    return true;
  }
};
void audioRamp() {
  juce::TimeSliceThread thread("rehearsal-test");
  StemTrack track(std::make_unique<ConstantReader>(), thread, 48000, 32768, false);
  track.setVolume(.5f); track.prepareToPlay(128, 48000);
  juce::AudioBuffer<float> buffer(2, 150);
  juce::AudioSourceChannelInfo info(&buffer, 11, 128);
  track.getNextAudioBlock(info);
  expect(std::abs(buffer.getSample(0, 130) - .4f) < 1e-5f, "base mix renders");
  float last = .4f;
  track.setRehearsalGain(.25f);
  for (int block = 0; block < 25; ++block) {
    buffer.clear(); track.getNextAudioBlock(info);
    for (int i = 11; i < 139; ++i) {
      auto value = buffer.getSample(0, i);
      expect(value <= last + 1e-6f && last - value < .001f, "attenuation is continuous across blocks");
      expect(value == buffer.getSample(1, i), "stereo ramp identical");
      last = value;
    }
    expect(buffer.getSample(0, 0) == 0, "ramp respects buffer offset");
  }
  expect(std::abs(last - .1f) < 1e-5f && track.getVolume() == .5f, "temporary gain does not alter base volume");
  expect(std::abs(track.getMagnitude() - .1f) < 1e-5f, "meter reflects practice audio");
  track.setRehearsalGain(1.0f);
  for (int block = 0; block < 25; ++block) {
    track.getNextAudioBlock(info);
    for (int i = 11; i < 139; ++i) {
      const auto value = buffer.getSample(0, i);
      expect(value >= last - 1e-6f && value - last < .001f, "normal mix restores smoothly"); last = value;
    }
  }
  expect(std::abs(last - .4f) < 1e-5f, "original level restored");
  track.setMuted(true); track.getNextAudioBlock(info);
  expect(buffer.getMagnitude(0, 11, 128) == 0, "rehearsal respects mute");
  track.setMuted(false); track.setGlobalMixState(1, true); track.getNextAudioBlock(info);
  expect(buffer.getMagnitude(0, 11, 128) == 0, "rehearsal respects other solo");
}

struct FakeProcessor {
  RehearsalMix mix;
  double getSongCurrentPositionSeconds() const { return 0; }
  double getSongLengthSeconds() const { return 48; }
  double getPlaybackTempoBpm() const { return 120; }
  bool isSongPlaying() const { return false; }
  juce::var getRehearsalState() const { return mix.snapshot(); }
  bool applyRehearsalAction(const juce::String &a, const juce::String &id, const juce::var &v) { return mix.apply(a, id, v); }
};
void remoteAdapter() {
  FakeProcessor p; p.mix.configure(song());
  BasicPlaybackSession<FakeProcessor> session(p);
  session.setRemote(true);
  expect(!session.supportsRehearsal(), "old server lacks capability");
  auto *root = new juce::DynamicObject(); juce::var state(root);
  root->setProperty("song", playworship::remote::encodeSong(song(), false));
  root->setProperty("rehearsal", p.mix.snapshot()); root->setProperty("seq", 1);
  session.applyState(state);
  juce::String commandId, commandAction, commandSong; juce::var commandValue;
  session.onCommand = [&](const auto &id, const auto &action, const auto &value, const auto &target) {
    commandId = id; commandAction = action; commandSong = target; commandValue = value;
  };
  session.setRehearsalGroup("category:bass");
  expect(commandAction == "rehearsal.select" && commandSong == "test-song", "remote uses explicit command with song identity");
  expect(p.mix.apply(commandAction, commandSong, commandValue), "server validates group command");
  root->setProperty("rehearsal", p.mix.snapshot()); session.applyState(state);
  session.setRehearsalLevel(false, .4f);
  expect(std::abs(session.getRehearsalLevel(false) - .4f) < 1e-6f, "remote drag stays at intended value");
  session.applyAcknowledgement(commandId, false);
  expect(session.getRehearsalLevel(false) == .25f, "rejected level returns to confirmed state");
  session.setConnected(false); commandAction.clear(); session.setRehearsalGroup("category:drums");
  expect(commandAction.isEmpty(), "disconnected client sends no command");
  session.clearRehearsal(); session.applyState(state);
  expect(commandAction == "rehearsal.clear" && p.mix.apply(commandAction, commandSong, commandValue), "reconnect clears unchanged rehearsal state");
  root->setProperty("rehearsal", p.mix.snapshot()); session.applyState(state);
  session.setConnected(false); session.clearRehearsal();
  p.mix.apply("rehearsal.select", "test-song", "category:drums");
  root->setProperty("rehearsal", p.mix.snapshot()); commandAction.clear(); session.applyState(state);
  expect(commandAction.isEmpty(), "deferred clear preserves another client's newer selection");
}

void snapshots() {
  const auto path = juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_REHEARSAL_SNAPSHOTS", {});
  if (path.isEmpty()) return;
  MTControlsAudioProcessor processor;
  auto fixture = song(); processor.configureRehearsalSong(fixture);
  processor.applyRehearsalAction("rehearsal.select", fixture.id, "category:bass");
  PlaybackSession session(processor); session.setRemote(true);
  auto *root = new juce::DynamicObject(); juce::var state(root);
  root->setProperty("song", playworship::remote::encodeSong(fixture, false));
  root->setProperty("rehearsal", processor.getRehearsalState());
  root->setProperty("length", 48.0); root->setProperty("sourceLength", 48.0);
  root->setProperty("windowEnd", 48.0); root->setProperty("tempo", 120.0);
  session.applyState(state);
  ChannelStripLookAndFeel lnf;
  RehearsalPanel panel(session, lnf);
  TopBar bar(processor, &session);
  bar.setTransportProviders([&] { return false; }, [] { return 4.0; }, [] { return 48.0; },
                           [] { return juce::String("C"); }, [&] { return fixture.id; }, [&] { return &fixture; });
  bar.setRehearsalMode(true);
  WaveformBar wave;
  wave.setArrangement(fixture.arrangement, false);
  wave.setTransportProviders([] { return 4.0; }, [] { return 48.0; });
  std::vector<float> bins(512);
  for (size_t i = 0; i < bins.size(); ++i) bins[i] = .2f + .5f * std::abs(std::sin(static_cast<float>(i) * .37f));
  wave.setWaveformBins(std::move(bins));
  juce::Component canvas;
  for (juce::Component *c : std::initializer_list<juce::Component *>{&panel.header, &bar, &wave, &panel}) canvas.addAndMakeVisible(c);
  juce::File folder(path); folder.createDirectory();
  for (auto preset : {ThemePreset::Default, ThemePreset::Spotify, ThemePreset::Ableton}) {
    applyTheme(preset); panel.refreshThemeColours(); bar.refreshThemeColours(); wave.refreshThemeColours();
    for (auto size : {juce::Point<int>(320, 568), {390, 844}, {1180, 820}}) {
      canvas.setSize(size.x, size.y);
      auto area = canvas.getLocalBounds();
      panel.header.setBounds(area.removeFromTop(56));
      bar.setMinimalPhoneMode(size.x < 700); bar.setBounds(area.removeFromTop(size.x < 700 ? 160 : 64));
      wave.setMinimalPhoneMode(size.x < 700); wave.setBounds(area.removeFromTop(108));
      panel.setBounds(area); panel.refresh();
      expect(panel.header.getLocalBounds().contains(panel.header.close.getBounds()), "close remains inside header");
      auto file = folder.getChildFile(juce::String(static_cast<int>(preset)) + "-" + juce::String(size.x) + ".png");
      auto out = file.createOutputStream(); expect(out != nullptr, "preview file opens");
      juce::PNGImageFormat png; expect(png.writeImageToStream(canvas.createComponentSnapshot(canvas.getLocalBounds()), *out), "preview renders");
    }
  }
}
}
int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  groupsAndValidation(); audioRamp(); remoteAdapter(); snapshots();
  std::cout << "Rehearsal tests passed\n";
}
