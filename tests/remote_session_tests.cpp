#include "network/RemoteSession.h"
#include "network/PlaybackSession.h"
#include <JuceHeader.h>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>

using playworship::remote::DiscoveredServer;
using playworship::remote::RemoteSession;

template <typename Predicate>
bool waitUntil(Predicate predicate, int timeoutMs = 5000) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return predicate();
}

// A disconnected or mirrored UI must never mutate its local audio processor.
struct TestProcessor {
  float volume = 0.75f;
  int writes = 0;
  float getStemVolume(int) const { return volume; }
  float getStemGain(int) const { return 1.0f; }
  float getMasterVolume() const { return volume; }
  float getMasterGain() const { return 1.0f; }
  float getMetronomeVolume() const { return volume; }
  float getMetronomeGain() const { return 1.0f; }
  float getDynamicGuideVolume() const { return volume; }
  float getDynamicGuideGain() const { return 1.0f; }
  float getPadsVolume() const { return volume; }
  float getPadsGain() const { return 1.0f; }
  void setStemVolume(int, float v) { volume = v; ++writes; }
  int getLoadedStemCount() const { return 1; }
  float getStemLevel(int) const { return 0.1f; }
  bool hasLoadedSong() const { return true; }
  double getSongCurrentPositionSeconds() const { return 4.0; }
  double getPlaybackTempoBpm() const { return 120.0; }
  double getSongLengthSeconds() const { return 100.0; }
  bool isSongPlaying() const { return false; }
};
bool testPlaybackAdapter() {
  TestProcessor local;
  BasicPlaybackSession<TestProcessor> session(local);
  session.setStemVolume(0, 0.5f);
  if (local.writes != 1 || session.getStemVolume(0) != 0.5f) return false;
  session.setRemote(true);
  int sent = 0;
  session.onCommand = [&](const juce::String &, const juce::String &action, const juce::var &value, const juce::String &target) {
    if (action == "mix.volume" && target == "included" && static_cast<double>(value) == 0.25) ++sent;
  };
  session.setStemVolume(0, 0.25f);
  if (sent || local.writes != 1) return false;
  session.applyState(juce::JSON::parse(R"({"song":{"id":"song","bpm":120,"stems":[{"id":"excluded","include":false},{"id":"included","include":true}]},"length":100,"position":18,"playing":false,"tempo":120,"mixer":[{"target":"included","volume":0.6}]})"));
  if (session.getLoadedStemCount() != 1 || std::abs(session.getStemVolume(0)-0.6f) > 0.001f) return false;
  session.setStemVolume(0, 0.25f);
  if (sent != 1 || local.writes != 1) return false;
  session.applyMeters(juce::JSON::parse(R"({"position":19,"playing":false,"levels":{"included":{"l":0.8,"r":0.8}}})"));
  if (std::abs(session.getStemLevel(0)-0.8f)>0.001f || session.getSongCurrentPositionSeconds()!=19) return false;
  session.setConnected(false);
  session.setStemVolume(0, 0.25f);
  if (session.getStemLevel(0)!=0 || session.getSongCurrentPositionSeconds()!=19 || sent!=1) return false;
  session.setRemote(false);
  return session.getStemVolume(0)==0.5f && local.writes==1;
}

bool testRemoteFaderReconciliation() {
  TestProcessor local;
  BasicPlaybackSession<TestProcessor> session(local);
  session.setRemote(true);
  juce::String latestId;
  session.onCommand = [&](const juce::String &id, const juce::String &, const juce::var &, const juce::String &) { latestId = id; };
  auto state = juce::JSON::parse(R"({"seq":1,"song":{"id":"song","stems":[{"id":"stem"}]},"mixer":[{"target":"stem","volume":0.2,"gain":1},{"target":"master","volume":0.2,"gain":1},{"target":"click","volume":0.2,"gain":1},{"target":"guide","volume":0.2,"gain":1}],"ui":{"getPadsVolume":0.2,"getPadsGain":1}})");
  session.applyState(state);
  auto expect = [](bool ok, const char *reason) {
    if (!ok) std::cerr << reason << '\n';
    return ok;
  };
  auto equal = [](float a, float b) { return std::abs(a-b) < 0.0001f; };
  struct Control { juce::String action, target; std::function<float()> read; };
  std::vector<Control> controls{
    {"mix.volume", "stem", [&] { return session.getStemVolume(0); }},
    {"mix.gain", "stem", [&] { return session.getStemGain(0); }},
    {"mix.volume", "master", [&] { return session.getMasterVolume(); }},
    {"mix.gain", "master", [&] { return session.getMasterGain(); }},
    {"mix.volume", "click", [&] { return session.getMetronomeVolume(); }},
    {"mix.gain", "click", [&] { return session.getMetronomeGain(); }},
    {"mix.volume", "guide", [&] { return session.getDynamicGuideVolume(); }},
    {"mix.gain", "guide", [&] { return session.getDynamicGuideGain(); }},
    {"pads.volume", {}, [&] { return session.getPadsVolume(); }},
    {"pads.gain", {}, [&] { return session.getPadsGain(); }}
  };
  for (auto &control : controls) {
    session.send(control.action, 0.4, control.target);
    const auto earlierId = latestId;
    if (!expect(equal(control.read(), 0.4f), "Fader must follow input before network reply")) return false;
    session.send(control.action, 0.8, control.target);
    session.applyAcknowledgement(earlierId, true);
    session.applyState(state);
    if (!expect(equal(control.read(), 0.8f), "An earlier ack/snapshot must not undo a later drag")) return false;
    session.applyAcknowledgement("another-client", false);
    if (!expect(equal(control.read(), 0.8f), "Another client's ack must not clear this preview")) return false;
    session.applyAcknowledgement(latestId, true);
    session.applyState(state);
    if (!expect(equal(control.read(), 0.8f), "Reapplying the same UI snapshot must not clear an acknowledged preview")) return false;
  }
  // The first newer snapshot after ack is authoritative, including clamped values.
  auto confirmed = state.clone();
  confirmed.getDynamicObject()->setProperty("seq", 2);
  auto confirmedMixer = confirmed.getProperty("mixer", {});
  for (auto &row : *confirmedMixer.getArray()) {
    row.getDynamicObject()->setProperty("volume", 0.75);
    row.getDynamicObject()->setProperty("gain", 0.75);
  }
  auto ui = confirmed.getProperty("ui", {});
  ui.getDynamicObject()->setProperty("getPadsVolume", 0.75);
  ui.getDynamicObject()->setProperty("getPadsGain", 0.75);
  session.applyState(confirmed);
  for (const auto &control : controls)
    if (!expect(equal(control.read(), 0.75f), "Confirmed server values must reach all controls")) return false;
  session.send("mix.volume", 0.9, "stem");
  session.applyAcknowledgement(latestId, false);
  if (!expect(equal(session.getStemVolume(0), 0.75f), "Rejected commands must restore confirmed state")) return false;
  session.send("mix.volume", 0.9, "stem");
  session.setConnected(false);
  if (!expect(equal(session.getStemVolume(0), 0.75f), "Disconnect must discard unconfirmed preview")) return false;
  session.applyState(confirmed);
  session.send("mix.volume", 0.9, "stem");
  auto otherSong = state.clone();
  otherSong.getProperty("song", {}).getDynamicObject()->setProperty("id", "other-song");
  session.applyState(otherSong);
  if (!expect(equal(session.getStemVolume(0), 0.2f), "Changing songs must discard the previous preview")) return false;
  return expect(local.writes == 0, "Client preview must never write to local audio");
}

int main() {
  if (!testRemoteFaderReconciliation()) return 1;
  if (!testPlaybackAdapter()) { std::cerr << "Playback adapter isolation failed\n"; return 1; }
  if (const char *name = std::getenv("PW_REMOTE_ADVERTISE_ONLY")) {
    RemoteSession advertiser;
    if (!advertiser.becomeServer(name) || !waitUntil([&] {
          return advertiser.getDiscoveryStatus() == playworship::remote::LanDiscovery::Status::Ready;
        }, 8000)) return 1;
    std::cout << "ready\n" << std::flush;
    std::string done;
    std::getline(std::cin, done);
    return 0;
  }
  if (const char *expected = std::getenv("PW_REMOTE_DISCOVERY_EXPECT")) {
    RemoteSession browser;
    browser.becomeClient();
    DiscoveredServer endpoint;
    const bool found = waitUntil([&] {
      for (const auto &server : browser.getDiscoveredServers())
        if (server.instance == expected) {
          endpoint = server;
          return true;
        }
      return false;
    }, 8000);
    if (!found) { std::cerr << "external server not discovered\n"; return 1; }
    browser.connectTo(endpoint);
    if (!waitUntil([&] { return browser.isConnected(); })) {
      std::cerr << "discovered address did not connect\n"; return 1;
    }
    std::cout << "external server discovered and connected\n";
    return 0;
  }
  RemoteSession server;
  RemoteSession first;
  RemoteSession second;
  if (!server.becomeServer("Remote test")) {
    std::cerr << "Could not start local server\n";
    return 1;
  }
  first.becomeClient();
  second.becomeClient();
  DiscoveredServer address;
  if (std::getenv("PW_REMOTE_DIRECT_CONNECT") != nullptr) {
    address = {"Remote test", "127.0.0.1", server.getPort(),
               juce::Time::currentTimeMillis()};
  } else if (!waitUntil([&] {
        for (const auto &found : first.getDiscoveredServers())
          if (found.instance == "Remote test" &&
              found.port == server.getPort()) {
            address = found;
            return true;
          }
        return false;
      }, 8000)) {
    std::cerr << "Client did not discover the advertised server\n";
    return 1;
  }
  first.connectTo(address);
  second.connectTo(address);
  if (!waitUntil([&] {
        return first.isConnected() && second.isConnected() &&
               server.getClientCount() == 2;
      })) {
    std::cerr << "Both clients did not connect\n";
    return 1;
  }

  std::vector<float> waveform(10000);
  for (size_t i = 0; i < waveform.size(); ++i)
    waveform[i] = static_cast<float>(static_cast<int>(i % 101) - 50) / 50.0f;
  std::vector<uint8_t> expectedBytes;
  expectedBytes.reserve(waveform.size() * sizeof(float));
  for (float value : waveform) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift : {24, 16, 8, 0})
      expectedBytes.push_back(static_cast<uint8_t>((bits >> shift) & 0xff));
  }
  const auto hash =
      juce::SHA256(waveform.data(), waveform.size() * sizeof(float))
          .toHexString();
  server.publish(R"({"v":1,"type":"snapshot","seq":1,"library":{"songs":[]}})", waveform,
                 hash);
  int firstSnapshots = 0, secondSnapshots = 0;
  int firstWaves = 0, secondWaves = 0;
  const bool bothReceived = waitUntil([&] {
    for (const auto &event : first.drainEvents()) {
      firstSnapshots += event.kind == RemoteSession::Event::Kind::Snapshot;
      if (event.kind == RemoteSession::Event::Kind::Waveform) {
        ++firstWaves;
        if (event.bytes != expectedBytes)
          return false;
      }
    }
    for (const auto &event : second.drainEvents()) {
      secondSnapshots += event.kind == RemoteSession::Event::Kind::Snapshot;
      secondWaves += event.kind == RemoteSession::Event::Kind::Waveform;
    }
    return firstSnapshots > 0 && secondSnapshots > 0 &&
           firstWaves == 1 && secondWaves == 1;
  });
  if (!bothReceived) {
    std::cerr << "Snapshot or waveform missing from a client\n";
    return 1;
  }

  server.publish(R"({"v":1,"type":"snapshot","seq":2,"library":{"songs":[]}})", waveform, hash);
  bool deltaWithoutLibrary = false;
  if (!waitUntil([&] {
    for (const auto &event : second.drainEvents()) {
      const auto value = juce::JSON::parse(event.text);
      if (value.getProperty("seq",0) == juce::var(2))
        deltaWithoutLibrary = !value.getDynamicObject()->hasProperty("library");
    }
    return deltaWithoutLibrary;
  })) { std::cerr << "Unchanged catalogue was retransmitted\n"; return 1; }
  second.stop(); second.becomeClient(); second.connectTo(address);
  bool rejoinedWithLibrary=false, rejoinedWithWave=false;
  if (!waitUntil([&] {
    for (const auto &event : second.drainEvents()) {
      if (event.kind == RemoteSession::Event::Kind::Waveform) rejoinedWithWave = event.bytes == expectedBytes;
      if (event.kind == RemoteSession::Event::Kind::Snapshot) {
        const auto value = juce::JSON::parse(event.text);
        rejoinedWithLibrary = value.getProperty("library",{}).isObject();
      }
    }
    return rejoinedWithLibrary && rejoinedWithWave;
  })) { std::cerr << "Reconnecting client did not receive full state\n"; return 1; }

  server.broadcast(R"({"v":1,"type":"meters","position":19,"levels":{"master":{"l":0.8,"r":0.6}}})");
  bool firstMeter=false, secondMeter=false;
  if (!waitUntil([&] {
    for (const auto &event : first.drainEvents()) if (event.kind == RemoteSession::Event::Kind::Meters) firstMeter=true;
    for (const auto &event : second.drainEvents()) if (event.kind == RemoteSession::Event::Kind::Meters) secondMeter=true;
    return firstMeter && secondMeter;
  })) { std::cerr << "Meter broadcast missing\n"; return 1; }

  first.sendCommand("command-1", "transport.seek", 18.5);
  bool receivedCommand = waitUntil([&] {
    for (const auto &event : server.drainEvents()) {
      if (event.kind != RemoteSession::Event::Kind::Command)
        continue;
      const auto json = juce::JSON::parse(event.text);
      if (json.getProperty("id", {}).toString() == "command-1" &&
          json.getProperty("action", {}).toString() == "transport.seek")
        return true;
    }
    return false;
  });
  if (!receivedCommand) {
    std::cerr << "Server did not receive command\n";
    return 1;
  }
  server.sendAck("command-1", true);
  if (!waitUntil([&] {
        for (const auto &event : first.drainEvents()) {
          if (event.kind != RemoteSession::Event::Kind::Snapshot)
            continue;
          if (juce::JSON::parse(event.text)
                  .getProperty("type", {})
                  .toString() == "ack")
            return true;
        }
        return false;
      })) {
    std::cerr << "Client did not receive acknowledgement\n";
    return 1;
  }
  // New clients negotiate metadata deltas; their delivered event must still
  // contain the complete song/settings without carrying artwork over the wire.
  auto metadataState = juce::JSON::parse(R"({"v":1,"type":"snapshot","seq":10,"song":{"id":"delta-song","artwork":"cached-art"},"settings":{"window":3}})");
  server.publishState(metadataState, waveform, hash);
  bool fullMetadata = false;
  if (!waitUntil([&] {
    for (const auto &event : second.drainEvents())
      if (static_cast<int>(event.value.getProperty("seq", 0)) == 10)
        fullMetadata = event.value.getProperty("song", {}).getProperty("artwork", {}).toString() == "cached-art";
    return fullMetadata;
  })) return 1;
  auto deltaState = juce::JSON::parse(R"({"v":1,"type":"snapshot","seq":11})");
  deltaState.getDynamicObject()->setProperty("song", metadataState.getProperty("song", {}));
  deltaState.getDynamicObject()->setProperty("settings", metadataState.getProperty("settings", {}));
  server.publishState(deltaState, waveform, hash);
  bool mergedDelta = false;
  if (!waitUntil([&] {
    for (const auto &event : second.drainEvents()) {
      if (static_cast<int>(event.value.getProperty("seq", 0)) != 11) continue;
      const auto wire = juce::JSON::parse(event.text);
      mergedDelta = !wire.hasProperty("song") && !wire.hasProperty("settings") &&
          event.value.getProperty("song", {}).getProperty("artwork", {}).toString() == "cached-art" &&
          static_cast<int>(event.value.getProperty("settings", {}).getProperty("window", 0)) == 3;
    }
    return mergedDelta;
  })) { std::cerr << "Incremental metadata was retransmitted or not merged\n"; return 1; }

  // Burst control values collapse per client/target, but transport commands
  // remain ordered barriers. This models a busy UI that drains once per frame.
  first.drainEvents(); second.drainEvents(); server.drainEvents();
  for (int i = 0; i < 60; ++i) first.sendCommand("fader-a-" + juce::String(i), "mix.volume", i / 100.0, "master");
  first.sendCommand("barrier", "transport.stop");
  for (int i = 0; i < 60; ++i) first.sendCommand("fader-b-" + juce::String(i), "mix.volume", i / 100.0, "master");
  first.sendCommand("end-burst", "test.barrier");
  std::vector<juce::String> ids;
  if (!waitUntil([&] {
    for (const auto &event : server.drainEvents()) {
      if (event.kind == RemoteSession::Event::Kind::Command)
        ids.push_back(juce::JSON::parse(event.text).getProperty("id", {}).toString());
    }
    return std::find(ids.begin(), ids.end(), "end-burst") != ids.end();
  })) { std::cerr << "Burst lost its final action\n"; return 1; }
  const auto a = std::find(ids.begin(), ids.end(), "fader-a-59");
  const auto barrier = std::find(ids.begin(), ids.end(), "barrier");
  const auto b = std::find(ids.begin(), ids.end(), "fader-b-59");
  if (a == ids.end() || b == ids.end() || barrier == ids.end() || !(a < barrier && barrier < b) || ids.size() >= 122) {
    std::cerr << "Fader coalescing lost ordering or did not reduce work\n"; return 1;
  }

  // A real TCP peer completes WebSocket upgrade, then stops reading. Large
  // frames fill its receive window; publishing must still return promptly and
  // the healthy second client must receive the final ack followed by its state.
  juce::StreamingSocket slowPeer;
  if (!slowPeer.connect("127.0.0.1", server.getPort(), 2000)) return 1;
  const std::string handshake = "GET /remote HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
  slowPeer.write(handshake.data(), static_cast<int>(handshake.size()));
  if (!waitUntil([&] { return server.getClientCount() == 3; })) return 1;
  double maxPublishMs = 0;
  const juce::String padding = juce::String::repeatedString("x", 512 * 1024);
  for (int i = 0; i < 40; ++i) {
    auto value = juce::JSON::parse(R"({"v":1,"type":"snapshot"})");
    value.getDynamicObject()->setProperty("seq", 100 + i);
    value.getDynamicObject()->setProperty("padding", padding);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    server.publishState(value, waveform, hash);
    maxPublishMs = std::max(maxPublishMs, juce::Time::getMillisecondCounterHiRes() - start);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  }
  if (maxPublishMs > 100.0) { std::cerr << "Slow reader blocked publication: " << maxPublishMs << " ms\n"; return 1; }
  second.drainEvents();
  server.sendAck("after-pressure", true);
  server.publishState(juce::JSON::parse(R"({"v":1,"type":"snapshot","seq":200})"), waveform, hash);
  bool ackAfterPressure = false, finalState = false;
  if (!waitUntil([&] {
    for (const auto &event : second.drainEvents()) {
      if (event.text.empty()) continue;
      const auto json = juce::JSON::parse(event.text);
      if (json.getProperty("id", {}).toString() == "after-pressure") ackAfterPressure = true;
      if (static_cast<int>(json.getProperty("seq", 0)) == 200) finalState = ackAfterPressure;
    }
    return finalState;
  })) { std::cerr << "Slow peer stalled healthy client or reordered ack/state\n"; return 1; }
  slowPeer.close();
  std::cout << "slow-reader max publish: " << maxPublishMs << " ms; burst events: " << ids.size() << "\n";
  std::cout << "remote session loopback passed\n";
  return 0;
}
