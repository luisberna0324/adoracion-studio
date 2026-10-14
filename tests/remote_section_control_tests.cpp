#include "network/PlaybackSession.h"
#include "network/RemoteSession.h"
#include "network/SectionControl.h"
#include "audio/SectionScheduler.h"
#include <chrono>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace {
void expect(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
SongArrangementPartSnapshot part(const char *id, double start, double end) {
  SongArrangementPartSnapshot p; p.id = id; p.name = "Coro"; p.startSec = start; p.endSec = end; return p;
}
// Models control-side state only. Validation, identity lookup, session adapter,
// JSON, queue, WebSocket, ACK/snapshot reconciliation and scheduler are real.
// section_automation_tests separately covers the native processor/audio path.
struct Processor {
  SongArrangementSnapshot metadata{{part("verse", 0, 10), part("chorus-1", 10, 20), part("chorus-2", 20, 30)}};
  std::vector<SongArrangementPartSnapshot> runtime = metadata.parts;
  juce::String revision = "2";
  int preparing = -1, pending = -1, loop = -1, writes = 0;
  bool loaded = true, playing = true, muted = true;
  double position = 15;
  uint64_t transportRevision = 0;
  juce::String getArrangementControlRevision() const { return revision; }
  SongArrangementSnapshot getSongArrangementSnapshot() const { return metadata; }
  juce::String getArrangementPartId(int index) const {
    if (index < 0 || index >= static_cast<int>(runtime.size())) return {};
    const auto &id = runtime[static_cast<size_t>(index)].id;
    return playworship::arrangement::uniquePartIndex(runtime, id, 256) == index ? id : juce::String{};
  }
  std::pair<int, int> getArrangementJumpState() const { return {pending, preparing}; }
  int getPendingArrangementPartJumpIndex() const { return pending; }
  int getPreparingArrangementPartJumpIndex() const { return preparing; }
  int getArrangementPartLoopIndex() const { return loop; }
  bool requestArrangementPartJumpById(const juce::String &id, const juce::String &expected = {}) {
    const int index = playworship::arrangement::uniquePartIndex(runtime, id, 256);
    if (!loaded || index < 0 || (expected.isNotEmpty() && expected != revision)) return false;
    ++writes;
    if (!playing) { position = runtime[static_cast<size_t>(index)].startSec; pending = preparing = loop = -1; }
    else if (preparing == index || pending == index) pending = preparing = -1;
    else { preparing = index; pending = -1; } // preserve Loop during preparation
    return true;
  }
  bool toggleArrangementPartLoopById(const juce::String &id, const juce::String &expected = {}) {
    const int index = playworship::arrangement::uniquePartIndex(runtime, id, 256);
    if (!loaded || index < 0 || (expected.isNotEmpty() && expected != revision)) return false;
    loop = loop == index ? -1 : index; pending = preparing = -1; ++writes; return true;
  }
  void clearPendingArrangementPartJump() { pending = preparing = -1; ++writes; }
  void clearArrangementPartLoop() { loop = -1; ++writes; }
  bool hasLoadedSong() const { return loaded; }
  bool isSectionAutomationEditing() const { return false; }
  bool isPracticeLoopActive() const { return false; }
  bool hasPracticeLoopPreparation() const { return false; }
  uint64_t getPracticeLoopRevision() const { return 0; }
  void clearPracticeLoop() {}
  uint64_t getTransportControlRevision() const { return transportRevision; }
  void playSong() { playing = true; ++transportRevision; }
  void stopSong() { playing = false; position = 0; ++transportRevision; ++writes; }
  void seekSongTo(double seconds) { position = seconds; ++transportRevision; }
  bool isSongPlaying() const { return playing; }
  double getSongCurrentPositionSeconds() const { return position; }
  double getSongLengthSeconds() const { return 30; }
  double getPlaybackTempoBpm() const { return 120; }
};

struct Fixture {
  using Remote = playworship::remote::RemoteSession;
  Remote server, wire;
  Processor engine, clientEngine;
  BasicPlaybackSession<Processor> client{clientEngine};
  juce::String songId = "song-A";
  bool loading = false, advertiseIds = true;
  int sequence = 0;
  std::map<juce::String, bool> acks;
  std::map<juce::String, juce::String> ackReasons;
  juce::String lastSentId;
  juce::var lastCommand;
  Fixture() {
    expect(server.becomeServer("Section control regression"), "loopback server starts");
    client.setRemote(true);
    client.onCommand = [this](const juce::String &id, const juce::String &action,
                             const juce::var &value, const juce::String &target) {
      lastSentId = id;
      wire.sendCommand(id, action, value, target);
    };
    publish();
    wire.becomeClient();
    playworship::remote::DiscoveredServer address;
    address.address = "127.0.0.1"; address.port = server.getPort(); address.instance = "fixture";
    wire.connectTo(address);
    wait([this] { return client.isConnected(); });
  }
  ~Fixture() { wire.stop(); server.stop(); }
  void publish() {
    auto *o = new juce::DynamicObject();
    o->setProperty("v", 1); o->setProperty("type", "snapshot"); o->setProperty("seq", ++sequence);
    auto *song = new juce::DynamicObject(); song->setProperty("id", songId);
    o->setProperty("song", juce::var(song)); o->setProperty("playing", engine.playing);
    o->setProperty("position", engine.position); o->setProperty("length", 30.0);
    if (advertiseIds) o->setProperty("sectionControls", playworship::remote::encodeSectionControls(engine, songId));
    server.publishState(juce::var(o), {}, {});
  }
  void pump() {
    for (const auto &event : server.drainEvents()) {
      if (event.kind != Remote::Event::Kind::Command) continue;
      const auto command = event.value;
      const auto action = command.getProperty("action", {}).toString();
      const auto id = command.getProperty("id", {}).toString();
      juce::String error;
      bool accepted = false;
      if (playworship::remote::isSectionControlAction(action))
        accepted = playworship::remote::applySectionControl(engine, songId, loading,
            action, command.getProperty("value", {}), command.getProperty("target", {}), error);
      else if (action == "transport.stop") { engine.stopSong(); accepted = true; }
      server.sendAck(id, accepted, error);
      lastCommand = command; publish();
    }
    for (const auto &event : wire.drainEvents()) {
      if (event.kind == Remote::Event::Kind::Connected || event.kind == Remote::Event::Kind::Disconnected) {
        client.setConnected(false); continue;
      }
      const auto value = event.value;
      if (value.getProperty("type", {}).toString() == "snapshot") client.applyState(value);
      else if (value.getProperty("type", {}).toString() == "ack") {
        const auto id = value.getProperty("id", {}).toString();
        const bool accepted = static_cast<bool>(value.getProperty("accepted", false));
        acks[id] = accepted;
        ackReasons[id] = value.getProperty("reason", {}).toString();
        client.applyAcknowledgement(id, accepted, value.getProperty("reason", {}).toString());
      }
    }
  }
  template <typename Predicate> void wait(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do { pump(); if (predicate()) return; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("loopback operation timed out");
  }
  bool finish(const juce::String &partId) {
    const auto id = lastSentId;
    wait([&] { return acks.count(id) != 0 && !client.isArrangementControlPending(partId) &&
        static_cast<int>(client.snapshot().getProperty("seq", {})) >= sequence; });
    return acks[id];
  }
};

void normalAndLoopToJump() {
  Fixture f;
  expect(f.client.toggleArrangementPartLoopById("chorus-2"), "new client sends stable occurrence ID");
  expect(f.client.isArrangementControlPending("chorus-2") && f.client.getArrangementPartLoopId().isEmpty(),
         "pending Loop does not falsely show accepted state");
  expect(f.finish("chorus-2") && f.engine.loop == 2 && f.client.getArrangementPartLoopId() == "chorus-2",
         "second repeated chorus is accepted and confirmed by ID");
  expect(f.lastCommand.getProperty("target", {}).toString() == "chorus-2" &&
         f.lastCommand.getProperty("value", {}).getProperty("songId", {}).toString() == "song-A",
         "wire payload identifies song and occurrence");
  expect(f.client.requestArrangementPartJumpById("verse"), "Loop to Ir sends");
  expect(f.finish("verse") && f.engine.preparing == 0 && f.engine.loop == 2 && f.engine.muted,
         "preparing Ir preserves active Loop and instrument mute");
  f.engine.pending = f.engine.preparing; f.engine.preparing = -1; f.engine.loop = -1;
  playworship::audio::SectionScheduler scheduler;
  scheduler.publish({{0, 1000, 0, 1000}, {1000, 2000, 1000, 2000}, {2000, 3000, 2000, 3000}});
  expect(!scheduler.evaluate(2500, 200, false, f.engine.pending, f.engine.loop).transition,
         "prepared Ir remains queued through an interior block");
  const auto decision = scheduler.evaluate(2900, 200, false, f.engine.pending, f.engine.loop);
  expect(decision.transition && decision.targetFrame == 0 && decision.transitionOffsetFrames == 100,
         "prepared backward Ir consumes only at the valid musical boundary");
  expect(f.clientEngine.writes == 0, "remote client never mutates its own engine");
}
void staleContextAndInvalidTargets() {
  Fixture f;
  f.engine.loop = 1;
  const int writes = f.engine.writes;
  f.engine.revision = "4"; // deliberately retain the client's earlier snapshot
  expect(f.client.requestArrangementPartJumpById("verse"), "old snapshot request sends its old revision");
  expect(!f.finish("verse") && f.engine.writes == writes && f.engine.loop == 1,
         "stale arrangement is rejected without clearing the valid Loop");
  f.songId = "song-B";
  expect(f.client.toggleArrangementPartLoopById("chorus-2"), "old song request sends");
  expect(!f.finish("chorus-2") && f.engine.writes == writes && f.engine.loop == 1,
         "old-song request with reused section IDs is rejected");
  // New-song snapshot clears obsolete feedback. Runtime may omit a metadata
  // section; acceptance must come from the processor, not metadata alone.
  f.engine.runtime.pop_back();
  expect(f.client.toggleArrangementPartLoopById("chorus-2"), "metadata-only section request sends");
  expect(!f.finish("chorus-2") && f.engine.writes == writes && f.engine.loop == 1,
         "motor rejection produces a negative ACK without side effects");
  f.engine.metadata.parts.push_back(f.engine.metadata.parts[0]);
  expect(f.client.requestArrangementPartJumpById("verse"), "duplicate-ID request sends");
  expect(!f.finish("verse") && f.engine.writes == writes, "ambiguous metadata IDs are rejected");
  f.loading = true;
  expect(f.client.toggleArrangementPartLoopById("chorus-1"), "loading request sends");
  expect(!f.finish("chorus-1") && f.engine.writes == writes, "loading rejects without changing intents");
}
void compatibilityAndMalformedRequests() {
  Fixture f;
  for (const auto &action : {"arrange.jump", "arrange.loop", "arrange.clearJump", "arrange.clearLoop"}) {
    const juce::String id(action); f.wire.sendCommand(id, action, 0);
    f.wait([&] { return f.acks.count(id) != 0; });
    expect(!f.acks[id] && f.engine.writes == 0 && f.wire.isConnected(),
           "legacy section commands reject explicitly while keeping the connection");
    expect(f.ackReasons[id].contains("Update the client app"),
           "legacy client receives an explicit update instruction in the ACK reason");
  }
  for (const char *json : {R"({"songId":"song-A","revision":2})", R"({"songId":"song-A"})", R"({"songId":true,"revision":"2"})"}) {
    const auto id = juce::Uuid().toString();
    f.wire.sendCommand(id, "arrange.jumpById", juce::JSON::parse(json), "verse");
    f.wait([&] { return f.acks.count(id) != 0; });
    expect(!f.acks[id] && f.engine.writes == 0, "malformed identity is rejected");
  }
  juce::String error;
  const auto valid = juce::JSON::parse(R"({"songId":"song-A","revision":"2"})");
  expect(!playworship::remote::applySectionControl(f.engine, f.songId, false,
      "arrange.jumpById", valid, true, error) && f.engine.writes == 0,
      "wrong-type destination cannot be converted into a section ID");
  expect(!playworship::remote::applySectionControl(f.engine, f.songId, false,
      "arrange.unknownById", valid, "verse", error) && f.engine.writes == 0,
      "unknown action cannot become a Loop toggle");
  f.advertiseIds = false; const int seq = f.sequence + 1; f.publish();
  f.wait([&] { return static_cast<int>(f.client.snapshot().getProperty("seq", {})) >= seq; });
  expect(!f.client.toggleArrangementPartLoopById("chorus-1") &&
         f.client.getArrangementControlError("chorus-1").contains("Update the server"),
         "new client never falls back to indices with an old server");
  const auto before = f.acks.size(); f.client.stopSong();
  f.wait([&] { return f.acks.size() > before; });
  expect(!f.engine.playing && f.wire.isConnected(), "other v1 controls still work");
  f.client.setConnected(false);
  expect(!f.client.requestArrangementPartJumpById("verse"), "disconnected client sends nothing");
}
void identityMappingAndFeedback() {
  Fixture f;
  // UI retains same-start order; runtime orders by end. Resolve indicators and
  // controls using IDs, never treat either index as the other's index.
  f.engine.metadata.parts = {part("long", 10, 30), part("short", 10, 20)};
  f.engine.runtime = {f.engine.metadata.parts[1], f.engine.metadata.parts[0]};
  f.engine.revision = "6"; const int seq = f.sequence + 1; f.publish();
  f.wait([&] { return static_cast<int>(f.client.snapshot().getProperty("seq", {})) >= seq; });
  expect(f.client.toggleArrangementPartLoopById("long") && f.finish("long") && f.engine.loop == 1,
         "different UI/runtime orders resolve the intended occurrence");
  expect(playworship::arrangement::uniquePartIndex(f.engine.metadata.parts, f.client.getArrangementPartLoopId()) == 0,
         "confirmed Loop maps back to the correct UI card");
  expect(f.client.requestArrangementPartJumpById("absent"), "invalid target sends for authoritative validation");
  expect(!f.finish("absent") && !f.client.getArrangementControlError("absent").isEmpty(),
         "negative ACK is visible to the requesting card");
  f.client.applyAcknowledgement("someone-else", false, "other rejection");
  expect(!f.client.getArrangementControlError("absent").contains("other rejection"), "unrelated ACK cannot overwrite feedback");
  expect(playworship::arrangement::uniquePartIndex(f.engine.runtime, juce::String("long"), 1) == -1,
         "targets beyond playable scheduler limit reject");
}
void deferredVisualContext() {
  Fixture f;
  f.client.setArrangementControlView("song-A", "2");
  f.songId = "song-B"; f.engine.revision = "4"; f.engine.loop = 1;
  const int seq = f.sequence + 1; f.publish();
  f.wait([&] { return static_cast<int>(f.client.snapshot().getProperty("seq", {})) >= seq; });
  expect(f.client.getArrangementPartLoopId().isEmpty(),
         "new-song indicators cannot identify cards in a deferred old-song view");
  expect(f.client.toggleArrangementPartLoopById("chorus-1") && !f.finish("chorus-1") &&
         f.engine.loop == 1 && f.engine.writes == 0,
         "gesture in old UI cannot use a newer session song/revision with reused part IDs");
  expect(f.lastCommand.getProperty("value", {}).getProperty("songId", {}).toString() == "song-A",
         "wire carries the displayed view's identity rather than the latest received identity");
  f.client.setArrangementControlView("song-B", "4");
  expect(f.client.getArrangementPartLoopId() == "chorus-1", "installing new UI restores its matching indicators");
  expect(f.client.toggleArrangementPartLoopById("chorus-1") && f.finish("chorus-1") && f.engine.loop == -1,
         "new view can explicitly toggle the correct song's Loop");
  f.engine.revision = "another-engine:4"; const int restartSeq = f.sequence + 1; f.publish();
  f.wait([&] { return static_cast<int>(f.client.snapshot().getProperty("seq", {})) >= restartSeq; });
  expect(f.client.toggleArrangementPartLoopById("chorus-1") && !f.finish("chorus-1") && f.engine.loop == -1,
         "new server epoch invalidates an old view even with the same song and structural counter");
}
} // namespace

int main() {
  try {
    normalAndLoopToJump(); std::cout << "PASS normal, repeated sections, Loop to Ir and boundary\n";
    staleContextAndInvalidTargets(); std::cout << "PASS stale song/arrangement, filtered/ambiguous targets and loading\n";
    compatibilityAndMalformedRequests(); std::cout << "PASS explicit v1 compatibility and malformed requests\n";
    identityMappingAndFeedback(); std::cout << "PASS UI/runtime identity mapping and negative ACK feedback\n";
    deferredVisualContext(); std::cout << "PASS deferred UI context, reused song section IDs and server epoch\n";
  } catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
  return 0;
}
