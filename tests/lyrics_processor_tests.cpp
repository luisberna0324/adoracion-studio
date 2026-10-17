#include "PluginProcessor.h"
#include "data/ApplicationDataPaths.h"
#include "data/AssetBootstrapManager.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// The audio renderer, clock, seek generations, prefetch banks, section scheduler
// and lyrics runtime are real. Only network delivery is observed numerically;
// FreeShowClient has separate HTTP/protocol tests and no server is contacted here.
struct LyricsRuntimeTestAccess {
  static void play(MTControlsAudioProcessor& p) {
    p.setTransportState(MTControlsAudioProcessor::TransportState::playing);
  }
  static void resumeWithSourceValidation(MTControlsAudioProcessor& p) {
    // Same source revalidation used by playSong for custom arrangements,
    // without reopening a host audio device in this offline renderer.
    p.seekSongToInternal(p.songCurrentPositionSeconds.load(), false);
    play(p);
  }
  static bool seekApplied(const MTControlsAudioProcessor& p) {
    return p.seekRequestGeneration.load() == p.appliedSeekRequestGeneration;
  }
  static auto position(const MTControlsAudioProcessor& p) { return p.transportClock.getPositionFrames(); }
  static void connected(MTControlsAudioProcessor& p, bool yes, uint64_t epoch) {
    p.lyricsCueScheduler.setDeliveryState(epoch, yes);
  }
  static std::vector<int> drain(MTControlsAudioProcessor& p) {
    std::vector<int> indices;
    playworship::audio::LyricsCueScheduler::Event event;
    while (p.lyricsCueScheduler.popEvent(event)) indices.push_back(event.cueIndex);
    return indices;
  }
  static bool ready(MTControlsAudioProcessor& p, int frames) {
    for (const auto& track : p.stemTracks)
      if (!track->waitForNextAudioBlockReady(frames, 1000)) return false;
    return true;
  }
  static bool targetReady(MTControlsAudioProcessor& p, double seconds) {
    const auto frame = static_cast<juce::int64>(std::llround(seconds * 44100.0));
    for (auto& track : p.stemTracks) track->requestPrefetchPosition(frame, 32768);
    for (int attempt = 0; attempt < 500; ++attempt) {
      bool ready = true;
      for (const auto& track : p.stemTracks) ready = track->isPrefetchReady(frame, 32768) && ready;
      if (ready) return true;
      juce::Thread::sleep(2);
    }
    return false;
  }
};
namespace {
void expect(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void writeWav(const juce::File& file, int frames) {
  expect(file.getParentDirectory().createDirectory().wasOk(), "fixture parent exists");
  file.deleteFile();
  std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
  juce::WavAudioFormat format;
  auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
      .withSampleRate(44100.0).withNumChannels(1).withBitsPerSample(16));
  expect(writer != nullptr, "fixture WAV writer opens");
  juce::AudioBuffer<float> audio(1, 4096);
  for (int frame = 0; frame < audio.getNumSamples(); ++frame) audio.setSample(0, frame, 0.15f);
  for (int remaining = frames; remaining > 0; remaining -= 4096)
    expect(writer->writeFromAudioSampleBuffer(audio, 0, std::min(remaining, 4096)), "fixture WAV writes");
}
struct Fixture {
  std::unique_ptr<MTControlsAudioProcessor> p;
  static constexpr int blockSize = 8192;
  Fixture() {
    const auto root = ApplicationDataPaths::isolatedTestRoot();
    const auto pads = AssetBootstrapManager::getPadsPackDirectory();
    expect(pads.isAChildOf(root), "assets stay inside isolated temporary test root");
    const auto pad = pads.getChildFile("C.wav");
    if (!pad.existsAsFile()) {
      writeWav(pad, 88200);
      for (int note = 1; note < PadsEngine::kNoteCount; ++note)
        expect(pad.copyFileTo(pads.getChildFile(juce::String(PadsEngine::kNoteNames[note]) + ".wav")), "pad fixture copies");
      for (int extra = 0; extra < 8; ++extra)
        expect(pad.copyFileTo(pads.getChildFile("extra-" + juce::String(extra) + ".wav")), "bootstrap fixture count complete");
    }
    const auto source = root.getChildFile("lyrics-source.wav"); writeWav(source, 44100 * 6);
    p = std::make_unique<MTControlsAudioProcessor>();
    p->prepareToPlay(44100.0, blockSize);
    expect(p->loadSongStems({source.getFullPathName()}, "lyrics-song", "Lyrics fixture", "C", 120.0), "real processor loads fixture stem");
    p->setMetronomeEnabled(false); p->setSongFadeOutSeconds(0.0);
    SongArrangementPartSnapshot a, b, c;
    a.id = "verse"; a.name = "Verse"; a.startSec = 0; a.endSec = 1;
    b.id = "chorus"; b.name = "Chorus"; b.startSec = 1; b.endSec = 2;
    c.id = "outro"; c.name = "Outro"; c.startSec = 2; c.endSec = 6;
    SongArrangementSnapshot arrangement; arrangement.parts = {a, b, c};
    p->setSongArrangement(arrangement, false);
    // Complete the loader's initial seek while stopped. Otherwise a cold
    // prefetch can leave that pre-existing seek pending across the first
    // audible span, and Pause legitimately allows it to become effective.
    block();
    expect(LyricsRuntimeTestAccess::seekApplied(*p), "loader initial seek is settled before transport assertions");
    LyricsRuntimeTestAccess::connected(*p, true, 1);
  }
  void lyrics(std::initializer_list<double> positions) {
    playworship::lyrics::SongLyrics song;
    song.enabled = true; song.showId = "show"; song.layoutId = "layout";
    int index = 0;
    for (const double seconds : positions) {
      playworship::lyrics::Mark mark;
      mark.id = "mark-" + juce::String(index++); mark.seconds = seconds;
      mark.target.showId = song.showId; mark.target.layoutId = song.layoutId;
      mark.target.groupId = "chorus"; mark.target.slideId = mark.id;
      song.marks.push_back(mark);
    }
    p->setSongLyrics(song);
  }
  void block(int frames = blockSize) {
    expect(LyricsRuntimeTestAccess::ready(*p, frames), "actual source audio is resident");
    juce::AudioBuffer<float> audio(p->getTotalNumOutputChannels(), frames); juce::MidiBuffer midi;
    p->processBlock(audio, midi);
  }
  void seek(double seconds) {
    p->seekSongTo(seconds);
    for (int attempt = 0; attempt < 500; ++attempt) {
      block();
      if (LyricsRuntimeTestAccess::seekApplied(*p)) return;
      juce::Thread::sleep(2);
    }
    expect(false, "seek becomes effective in real audio callback");
  }
  void indices(std::initializer_list<int> wanted, const char* message) {
    const auto got = LyricsRuntimeTestAccess::drain(*p);
    if (got != std::vector<int>(wanted)) {
      std::cerr << "indices received:"; for (auto i : got) std::cerr << ' ' << i;
      std::cerr << '\n';
      expect(false, message);
    }
  }
};
void testActualDenseSeekPauseAndTempo() {
  Fixture f; f.lyrics({0, .002, .003, .004, .5, .75, 1.5});
  LyricsRuntimeTestAccess::play(*f.p); f.block();
  f.indices({0, 1, 2, 3}, "actual callback enumerates dense cues once");
  f.p->pauseSong(); f.block(); f.block();
  f.indices({}, "paused callbacks generate no lyrics");
  expect(LyricsRuntimeTestAccess::targetReady(*f.p, f.p->getSongCurrentPositionSeconds()),
      "resume source revalidation bank is resident");
  LyricsRuntimeTestAccess::resumeWithSourceValidation(*f.p); f.block();
  expect(LyricsRuntimeTestAccess::seekApplied(*f.p), "custom resume source revalidation actually commits");
  f.indices({}, "custom-arrangement source revalidation on resume does not reproject current cue");
  f.p->pauseSong();
  f.p->seekSongTo(1.25);
  f.indices({}, "seek request alone must not project destination");
  f.block();
  f.indices({5}, "effective forward seek selects current cue only");
  f.seek(.6); f.indices({4}, "effective backward seek restores current cue");
  f.seek(.6); f.indices({4}, "explicit same-position seek synchronises the current destination");
  LyricsRuntimeTestAccess::play(*f.p);
  f.p->setPlaybackTempoBpm(240.0);
  bool crossed = false;
  for (int i = 0; i < 10 && !crossed; ++i) {
    const auto before = LyricsRuntimeTestAccess::position(*f.p);
    f.block(256);
    const auto after = LyricsRuntimeTestAccess::position(*f.p);
    const auto events = LyricsRuntimeTestAccess::drain(*f.p);
    for (const int cue : events) {
      expect(cue == 5 && before <= 33075 && after > 33075,
          "tempo cue fires only when actual original musical frame is crossed");
      crossed = true;
    }
  }
  // Larger blocks complete the same musical crossing if the tempo ramp has
  // not yet consumed enough original frames in the short blocks above.
  for (int i = 0; i < 10 && !crossed; ++i) {
    const auto before = LyricsRuntimeTestAccess::position(*f.p); f.block(1024);
    const auto after = LyricsRuntimeTestAccess::position(*f.p);
    for (const int cue : LyricsRuntimeTestAccess::drain(*f.p)) {
      expect(cue == 5 && before <= 33075 && after > 33075, "tempo retains source-frame anchoring"); crossed = true;
    }
  }
  expect(crossed, "tempo-changing real callback reaches the anchored cue");
  f.p->stopSong(); f.block(); LyricsRuntimeTestAccess::drain(*f.p);
  const auto stopped = LyricsRuntimeTestAccess::position(*f.p); f.block();
  expect(stopped == 0 && LyricsRuntimeTestAccess::position(*f.p) == 0, "stop returns actual transport to Inicio");
  f.indices({}, "stopped callbacks do not repeat cue");
}
void testActualJumpLoopAndDisconnection() {
  Fixture f; f.lyrics({0, .5, .998, 1.25, 2.0, 2.005, 3.0});
  f.p->pauseSong(); f.seek(.95); LyricsRuntimeTestAccess::drain(*f.p);
  expect(LyricsRuntimeTestAccess::targetReady(*f.p, 2), "jump destination bank is resident");
  LyricsRuntimeTestAccess::play(*f.p);
  expect(f.p->requestArrangementPartJumpById("outro"), "Ir accepts stable section destination");
  f.indices({}, "Ir request does not project before effective section boundary");
  f.block();
  f.indices({2, 4, 5}, "Ir uses real outgoing prefix and target suffix, omitting skipped lyric cues");
  expect(f.p->getSongCurrentPositionSeconds() > 2 && f.p->getSongCurrentPositionSeconds() < 2.2,
      "actual clock commits jump destination plus rendered suffix");
  f.p->pauseSong(); f.seek(1.95); LyricsRuntimeTestAccess::drain(*f.p);
  expect(LyricsRuntimeTestAccess::targetReady(*f.p, 1), "loop destination bank is resident");
  expect(f.p->toggleArrangementPartLoopById("chorus"), "Loop accepts stable section destination");
  LyricsRuntimeTestAccess::play(*f.p); f.block();
  f.indices({2}, "real loop reinstates cue at loop start even when cue belongs before section");
  for (int i = 0; i < 8; ++i) {
    expect(LyricsRuntimeTestAccess::targetReady(*f.p, 1), "repeated loop bank stays prepared");
    f.block();
  }
  const auto loopEvents = LyricsRuntimeTestAccess::drain(*f.p);
  expect(std::count(loopEvents.begin(), loopEvents.end(), 2) >= 1 &&
         std::count(loopEvents.begin(), loopEvents.end(), 3) >= 1,
      "subsequent real loop passes repeat independent marks");
  LyricsRuntimeTestAccess::connected(*f.p, false, 2); f.block();
  f.indices({}, "disconnection does not queue playback history");
  expect(f.p->isSongPlaying(), "disconnection leaves audio playing");
  LyricsRuntimeTestAccess::connected(*f.p, true, 3); f.block(256);
  f.indices({}, "reconnect only resumes upcoming crossings");
  auto changed = f.p->getSongLyrics(); changed.showId = "another-show";
  f.p->setSongLyrics(changed);
  f.p->pauseSong(); f.seek(.5); LyricsRuntimeTestAccess::drain(*f.p);
  LyricsRuntimeTestAccess::play(*f.p); f.block();
  f.indices({}, "changing linked show cannot project stale destinations");
}
void testActualBeforeFirstMarkIsBlank() {
  Fixture f; f.lyrics({.5, 1.25});
  f.p->pauseSong(); f.seek(1.5); f.indices({1}, "seek selects later current lyric");
  f.seek(.1); f.indices({-1}, "effective seek before first active mark clears old lyric");
  f.seek(.95); f.indices({0}, "loop baseline establishes current first mark");
  expect(LyricsRuntimeTestAccess::targetReady(*f.p, 0), "blank lead-in loop bank is resident");
  expect(f.p->toggleArrangementPartLoopById("verse"), "loop with blank lyrical lead-in accepts");
  LyricsRuntimeTestAccess::play(*f.p); f.block();
  f.indices({-1}, "actual loop reinstates blank before first independent mark");
}
void pumpMessages(int milliseconds) {
  juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
}
template <typename Predicate> void awaitReal(Predicate predicate, const char* message) {
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 6000.0;
  do {
    pumpMessages(10);
    if (predicate()) return;
  } while (juce::Time::getMillisecondCounterHiRes() < deadline);
  expect(false, message);
}
juce::var readRealOutput(int port) {
  // A direct HTTP/1.0 loopback read avoids ambient proxies and requests a
  // close-delimited response. This independent probe never controls a slide.
  juce::StreamingSocket socket;
  expect(socket.connect("127.0.0.1", port, 1200), "own loopback FreeShow output is reachable");
  const juce::String body = "{\"action\":\"get_output\"}";
  const juce::String request = "POST / HTTP/1.0\r\nHost: 127.0.0.1:" + juce::String(port) +
      "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
      juce::String(static_cast<int>(body.getNumBytesAsUTF8())) + "\r\n\r\n" + body;
  const int size = static_cast<int>(request.getNumBytesAsUTF8());
  expect(socket.write(request.toRawUTF8(), size) == size, "output probe writes exact loopback request");
  std::string bytes;
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
  while (juce::Time::getMillisecondCounterHiRes() < deadline) {
    const int ready = socket.waitUntilReady(true, 100);
    if (ready < 0) break;
    if (ready == 0) continue;
    char buffer[4096]; const int count = socket.read(buffer, static_cast<int>(sizeof(buffer)), false);
    if (count <= 0) break;
    bytes.append(buffer, static_cast<size_t>(count));
    expect(bytes.size() <= 65536, "fixture output response is bounded");
  }
  const auto headerEnd = bytes.find("\r\n\r\n");
  expect(headerEnd != std::string::npos && bytes.substr(0, bytes.find("\r\n")).find(" 200 ") != std::string::npos,
      "official FreeShow output probe returns HTTP 200");
  const auto json = juce::String::fromUTF8(bytes.data() + headerEnd + 4,
      static_cast<int>(bytes.size() - headerEnd - 4));
  const auto result = juce::JSON::parse(json);
  expect(result.isObject(), "official FreeShow output probe returns JSON object");
  return result;
}
void testRealFreeShow(const juce::String& showId, int port) {
  using namespace playworship::lyrics;
  expect(showId == "pw-synthetic-validation" && port >= 1 && port <= 65535,
      "real mode only controls the explicitly prepared PlayWorship synthetic fixture on loopback");
  Fixture f;
  auto& client = f.p->getFreeShowClient();
  ConnectionSettings settings; settings.host = "127.0.0.1"; settings.port = port;
  client.configure(settings); client.testConnection();
  awaitReal([&] { return client.getStatus().state == ConnectionStatus::State::connected; },
      "official FreeShow REST connection succeeds");
  ShowCatalog catalog; std::atomic<bool> loaded{false};
  client.requestShow(showId, [&](ShowCatalog result) { catalog = std::move(result); loaded.store(true, std::memory_order_release); });
  awaitReal([&] { return loaded.load(std::memory_order_acquire); }, "official FreeShow catalogue responds");
  expect(catalog.ok(), "official fixture catalogue and active-layout ordering parse successfully");
  const Layout* principal = nullptr; const Layout* alternate = nullptr;
  for (const auto& layout : catalog.layouts) {
    if (layout.id == "principal") principal = &layout;
    if (layout.id == "alternativo") alternate = &layout;
  }
  expect(principal != nullptr && principal->slides.size() == 7 && alternate != nullptr && !alternate->slides.empty(),
      "own fixture has exact principal/alternate layouts and expanded repeated two-screen chorus");
  SongLyrics song; song.enabled = true; song.showId = catalog.id; song.showName = catalog.name;
  song.layoutId = principal->id; song.layoutName = principal->name;
  const double positions[] = {0, .5, .998, 1.25, 2.0, 2.005, 3.0};
  for (int i = 0; i < 7; ++i) {
    Mark mark; mark.id = "real-" + juce::String(i); mark.seconds = positions[i];
    mark.target = principal->slides[static_cast<size_t>(i)].target; song.marks.push_back(mark);
  }
  Mark hide; hide.id = "real-hide"; hide.seconds = 3.5; hide.target.hide = true; song.marks.push_back(hide);
  f.p->setSongLyrics(song);
  const auto confirm = [&](const Target& target, int index, const char* step) {
    pumpMessages(40); // Allow the processor's real 100 Hz event dispatcher to submit the audio cue.
    awaitReal([&] {
      const auto status = client.getStatus();
      return status.outputConfirmed && status.lastProjected == targetLabel(target);
    }, step);
    expect(outputMatches(target, index, readRealOutput(port)), "actual FreeShow get_output agrees with processor cue");
    std::cout << "PASS REAL: " << step << " -> " << targetLabel(target) << '\n';
  };
  const auto confirmMark = [&](int mark, const char* step) {
    confirm(song.marks[static_cast<size_t>(mark)].target,
            principal->slides[static_cast<size_t>(mark)].absoluteIndex, step);
  };
  LyricsRuntimeTestAccess::play(*f.p); f.block(); confirmMark(0, "native first audio span projects intro");
  f.p->pauseSong(); f.seek(1.3); confirmMark(3, "effective forward seek projects literal Coro 2 without omitted screens");
  f.seek(.6); confirmMark(1, "effective backward seek restores first chorus screen");
  f.seek(.95); pumpMessages(40);
  expect(LyricsRuntimeTestAccess::targetReady(*f.p, 2), "real run jump bank is resident");
  LyricsRuntimeTestAccess::play(*f.p);
  expect(f.p->requestArrangementPartJumpById("outro"), "real Ir request accepted");
  f.block(); confirmMark(5, "native Ir split reaches second appearance, screen 2");
  f.p->pauseSong(); f.seek(1.95); confirmMark(3, "paused seek restores current independent cue before loop");
  expect(LyricsRuntimeTestAccess::targetReady(*f.p, 1), "real run loop bank is resident");
  expect(f.p->toggleArrangementPartLoopById("chorus"), "real Loop request accepted");
  LyricsRuntimeTestAccess::play(*f.p); f.block(); confirmMark(2, "native Loop reinstates current <= loop start");
  // A different cue is observed before wrapping again, proving repeated
  // projection rather than accepting an unchanged pre-existing output.
  f.block(); confirmMark(3, "native Loop crosses independent in-section cue");
  double before = f.p->getSongCurrentPositionSeconds(); bool wrapped = false;
  for (int i = 0; i < 8 && !wrapped; ++i) {
    expect(LyricsRuntimeTestAccess::targetReady(*f.p, 1), "real repeated loop entry stays resident");
    f.block();
    const auto after = f.p->getSongCurrentPositionSeconds(); wrapped = after < before; before = after;
    pumpMessages(30);
  }
  expect(wrapped, "native loop actually wraps again");
  confirmMark(2, "second native Loop pass repeats start cue");
  f.p->pauseSong(); f.p->clearArrangementPartLoop(); f.seek(.6); confirmMark(1, "disconnect baseline is first chorus screen");
  client.disconnect(); pumpMessages(50);
  LyricsRuntimeTestAccess::play(*f.p);
  for (int i = 0; i < 5; ++i) { f.block(); pumpMessages(20); }
  expect(f.p->isSongPlaying(), "disconnect leaves native audio running");
  expect(outputMatches(song.marks[1].target, principal->slides[1].absoluteIndex, readRealOutput(port)),
      "FreeShow output remains unchanged while disconnected cues are discarded");
  client.testConnection();
  awaitReal([&] { return client.getStatus().state == ConnectionStatus::State::connected; }, "real reconnect succeeds");
  pumpMessages(250);
  expect(outputMatches(song.marks[1].target, principal->slides[1].absoluteIndex, readRealOutput(port)),
      "reconnect does not replay omitted chorus history");
  std::cout << "PASS REAL: disconnect preserves audio and reconnect replays no history\n";
  while (f.p->getSongCurrentPositionSeconds() < 2.01) { f.block(); pumpMessages(20); }
  confirmMark(5, "next future cue after reconnect reaches second chorus screen 2");
  f.p->pauseSong(); f.seek(3.75); confirm(hide.target, 0, "explicit hide destination clears actual output");
  SongLyrics alternateSong; alternateSong.enabled = true; alternateSong.showId = catalog.id;
  alternateSong.layoutId = alternate->id;
  Mark alternateMark; alternateMark.id = "real-alternate"; alternateMark.target = alternate->slides.front().target;
  alternateSong.marks.push_back(alternateMark); f.p->setSongLyrics(alternateSong);
  f.seek(.1); confirm(alternateMark.target, alternate->slides.front().absoluteIndex,
      "confirmed seek resolves exact alternate-layout destination");
  alternateSong.marks[0].seconds = .5; f.p->setSongLyrics(alternateSong);
  f.seek(.1); confirm(hide.target, 0, "same-position explicit seek before first mark clears future lyric");
  client.disconnect();
}

}
int main(int argc, char** argv) {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr) return 77;
#endif
  juce::ScopedJuceInitialiser_GUI gui;
  try {
    if (argc > 1 && juce::String(argv[1]) == "--real") {
      expect(argc >= 3, "--real requires own synthetic fixture show ID");
      testRealFreeShow(argv[2], argc >= 4 ? juce::String(argv[3]).getIntValue() : 5506);
      return 0;
    }
    testActualDenseSeekPauseAndTempo(); std::cout << "PASS: real dense cues, seeks, pause, stop and tempo\n";
    testActualJumpLoopAndDisconnection(); std::cout << "PASS: real Ir, Loop, disconnect and binding changes\n";
    testActualBeforeFirstMarkIsBlank(); std::cout << "PASS: real seek and loop restore blank before first mark\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n'; return 1;
  }
  return 0;
}
