#pragma once

#include <JuceHeader.h>
#include <ixwebsocket/IXWebSocket.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <algorithm>

namespace playworship::remote {
// One bounded sender per connection. A blocked TCP peer cannot stall the UI or
// the sender belonging to another client. Payload objects are immutable here.
class RemoteSendQueue final {
public:
  enum class Kind { Reliable, Snapshot, Meters, Binary };
  explicit RemoteSendQueue(std::weak_ptr<ix::WebSocket> peer)
      : socket(std::move(peer)), worker([this] { run(); }) {}
  ~RemoteSendQueue() { stop(); }
  bool isFinished() const { return finished.load(); }
  void enableIncrementalSnapshots() { incrementalSnapshots = true; }
  void discardPending() {
    std::lock_guard<std::mutex> lock(mutex);
    queue.clear();
  }
  void requestStop() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopping = true;
      queue.clear();
    }
    changed.notify_one();
  }
  void stop() {
    requestStop();
    if (worker.joinable()) worker.join();
  }
  void push(juce::var value, Kind kind = Kind::Reliable) {
    enqueue({std::move(value), {}, kind});
  }
  void pushBinary(const std::string &bytes) { enqueue({{}, bytes, Kind::Binary}); }
private:
  struct Item { juce::var value; std::string bytes; Kind kind; };
  void enqueue(Item item) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (stopping) return;
      // Meter frames have no acknowledgement semantics. Snapshots can only
      // replace snapshots after the most recent ack/catalogue/binary barrier.
      if (item.kind == Kind::Meters) {
        queue.erase(std::remove_if(queue.begin(), queue.end(), [](const Item &v) { return v.kind == Kind::Meters; }), queue.end());
      } else if (item.kind == Kind::Snapshot) {
        for (auto it = queue.end(); it != queue.begin();) {
          --it;
          if (it->kind == Kind::Reliable || it->kind == Kind::Binary) break;
          if (it->kind == Kind::Snapshot) { queue.erase(it); break; }
        }
      }
      if (queue.size() >= 128) {
        // A reliable message must never be silently discarded. Reconnecting
        // gives this peer a fresh complete snapshot and waveform.
        congested = stopping = true;
        queue.clear();
      } else queue.push_back(std::move(item));
    }
    changed.notify_one();
  }
  void run() {
    juce::Thread::setCurrentThreadName("Remote send");
    for (;;) {
      Item item;
      {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return stopping || !queue.empty(); });
        if (stopping) break;
        item = std::move(queue.front()); queue.pop_front();
      }
      auto peer = socket.lock();
      if (!peer) break;
      if (item.kind == Kind::Binary) peer->sendBinary(item.bytes);
      else {
        auto payload = item.value;
        if (payload.getProperty("type", {}).toString() == "snapshot") {
          const auto song = payload.getProperty("song", {});
          const auto settings = payload.getProperty("settings", {});
          const auto settingsSignature = juce::JSON::toString(settings, true);
          if (incrementalSnapshots.load()) {
            auto delta = std::make_unique<juce::DynamicObject>();
            for (const auto &property : payload.getDynamicObject()->getProperties())
              delta->setProperty(property.name, property.value);
            if (song.isObject() && song == lastSong) delta->removeProperty("song");
            if (settings.isObject() && settingsSignature == lastSettingsSignature) delta->removeProperty("settings");
            delta->setProperty("incremental", true);
            payload = juce::var(delta.release());
          }
          lastSong = song;
          lastSettingsSignature = settingsSignature;
        }
        peer->send(juce::JSON::toString(payload, true).toStdString());
      }
    }
    if (congested) if (auto peer = socket.lock()) peer->close(1013, "Remote queue full; reconnect");
    finished = true;
  }
  std::weak_ptr<ix::WebSocket> socket;
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<Item> queue;
  bool stopping = false, congested = false;
  std::atomic<bool> finished{false};
  std::atomic<bool> incrementalSnapshots{false};
  juce::var lastSong;
  juce::String lastSettingsSignature;
  std::thread worker;
};
} // namespace playworship::remote
