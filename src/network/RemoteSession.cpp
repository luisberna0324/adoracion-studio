#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include "RemoteSession.h"
#include "RemoteSendQueue.h"

#include <algorithm>
#include <cstring>

namespace playworship::remote {
namespace {
constexpr size_t kWaveChunkBytes = 16384;
constexpr size_t kMaxJsonBytes = 16 * 1024 * 1024;
constexpr size_t kMaxCommandBytes = 256 * 1024;
constexpr size_t kMaxWaveBytes = 1024 * 1024;

std::string toUtf8(const juce::String &value) { return value.toStdString(); }
} // namespace

RemoteSession::RemoteSession() { ix::initNetSystem(); }
RemoteSession::~RemoteSession() { stop(); ix::uninitNetSystem(); }

bool RemoteSession::becomeServer(const juce::String &displayName) {
  stop();
  for (int candidate = 49172; candidate <= 49182; ++candidate) {
    auto socket = std::make_unique<ix::WebSocketServer>(
        candidate, "0.0.0.0", 128, 32, 3, ix::SocketServer::kDefaultAddressFamily, 5, 2);
    socket->disablePerMessageDeflate();
    socket->setOnConnectionCallback(
        [this](std::weak_ptr<ix::WebSocket> weakPeer, std::shared_ptr<ix::ConnectionState> connection) {
          auto peer = weakPeer.lock();
          if (!peer) return;
          const auto peerId = connection->getId();
          peer->setOnMessageCallback([this, weakPeer, peerId](const ix::WebSocketMessagePtr &message) {
            if (message->type == ix::WebSocketMessageType::Open) {
              auto sender = std::make_shared<RemoteSendQueue>(weakPeer);
              // Keep join replay ahead of updates without holding a lock while
              // any bytes are encoded or sent to a socket.
              const std::lock_guard<std::mutex> publishedLock(publishedMutex);
              const std::lock_guard<std::mutex> sendersLock(sendersMutex);
              if (latestSnapshot.isObject()) sender->push(latestSnapshot);
              for (const auto &frame : latestWaveformFrames) sender->pushBinary(frame);
              peerSenders[peerId] = std::move(sender);
            } else if (message->type == ix::WebSocketMessageType::Close || message->type == ix::WebSocketMessageType::Error) {
              {
                const std::lock_guard<std::mutex> lock(eventsMutex);
                events.erase(std::remove_if(events.begin(), events.end(), [&](const Event &event) { return event.sourceId == peerId; }), events.end());
              }
              {
                const std::lock_guard<std::mutex> lock(sendersMutex);
                auto it = peerSenders.find(peerId);
                if (it != peerSenders.end()) {
                  // IX invokes close while holding its state mutex. Joining here
                  // would deadlock a sender completing a failed send/close.
                  it->second->requestStop();
                  retiredSenders.push_back(std::move(it->second));
                  peerSenders.erase(it);
                }
              }
              Event disconnected{Event::Kind::PeerDisconnected, {}, {}};
              disconnected.sourceId = peerId;
              enqueue(std::move(disconnected));
            } else if (message->type == ix::WebSocketMessageType::Message &&
                       !message->binary && message->str.size() <= kMaxCommandBytes) {
              Event event{Event::Kind::Command, message->str, {}};
              event.sourceId = peerId;
              const auto command = juce::JSON::parse(message->str);
              event.value = command;
              if (command.getProperty("type", {}).toString() == "hello") {
                const std::lock_guard<std::mutex> lock(sendersMutex);
                const auto sender = peerSenders.find(peerId);
                if (sender != peerSenders.end() && static_cast<bool>(command.getProperty("incrementalSnapshots", false)))
                  sender->second->enableIncrementalSnapshots();
                return;
              }
              const auto action = command.getProperty("action", {}).toString();
              if (action == "mix.volume" || action == "mix.gain" || action == "pads.volume" || action == "pads.gain")
                event.coalescingKey = peerId + ":" + action.toStdString() + ":" + command.getProperty("target", {}).toString().toStdString();
              if (!enqueue(std::move(event))) if (auto peer = weakPeer.lock()) peer->close(1013, "Remote command queue full; reconnect");
            }
          });
        });
    if (socket->listenAndStart()) {
      serverPort = candidate;
      serverSocket = std::move(socket);
      mode = Mode::Server;
      serverDisplayName = displayName;
      discovery.startAdvertising(displayName, serverPort);
      return true;
    }
  }
  return false;
}

void RemoteSession::becomeClient() {
  stop();
  mode = Mode::Client;
  discovery.startBrowsing();
}

void RemoteSession::renameServer(const juce::String &displayName) {
  if (mode == Mode::Server && serverPort > 0) {
    serverDisplayName = displayName;
    discovery.startAdvertising(displayName, serverPort);
  }
}

void RemoteSession::connectTo(const DiscoveredServer &server) {
  if (mode != Mode::Client || server.address.isEmpty() ||
      server.port < 1 || server.port > 65535)
    return;
  if (clientSocket != nullptr)
    clientSocket->stop();
  clientSender.reset();
  connected = false;
  selectedServerName = server.instance;
  receivingChunks.clear();
  receivedSnapshot = juce::var();
  clientSocket = std::make_shared<ix::WebSocket>();
  clientSender = std::make_shared<RemoteSendQueue>(clientSocket);
  clientSocket->setUrl("ws://" + toUtf8(server.address) + ":" +
                       std::to_string(server.port) + "/remote");
  clientSocket->disablePerMessageDeflate();
  clientSocket->setPingInterval(5);
  clientSocket->setMinWaitBetweenReconnectionRetries(500);
  clientSocket->setMaxWaitBetweenReconnectionRetries(5000);
  clientSocket->setOnMessageCallback(
      [this](const ix::WebSocketMessagePtr &message) {
        if (message->type == ix::WebSocketMessageType::Open) {
          clientSender->push(juce::JSON::parse(R"({"v":1,"type":"hello","incrementalSnapshots":true})"));
          connected = true;
          enqueue({Event::Kind::Connected, {}, {}});
        } else if (message->type == ix::WebSocketMessageType::Close ||
                   message->type == ix::WebSocketMessageType::Error) {
          connected = false;
          clientSender->discardPending();
          receivingChunks.clear();
          receivedSnapshot = juce::var();
          enqueue({Event::Kind::Disconnected, {}, {}});
        } else if (message->type == ix::WebSocketMessageType::Message) {
          if (message->binary) {
            onClientBinary(message->str);
          } else if (message->str.size() <= kMaxJsonBytes) {
            auto json = juce::JSON::parse(message->str);
            const auto type = json.getProperty("type", {}).toString();
            // Older servers reject the capability probe with an empty ID.
            // Actual control commands always have a nonempty UUID.
            if (type == "ack" && json.getProperty("id", {}).toString().isEmpty()) return;
            const auto kind = type == "meters" ? Event::Kind::Meters : Event::Kind::Snapshot;
            if (type == "snapshot") {
              if (static_cast<bool>(json.getProperty("incremental", false))) {
                for (const auto *key : {"song", "settings"})
                  if (!json.hasProperty(key) && receivedSnapshot.hasProperty(key))
                    json.getDynamicObject()->setProperty(key, receivedSnapshot.getProperty(key, {}));
              }
              // Keep only immutable metadata references. The UI is allowed to
              // update position/playing on its own snapshot root.
              auto cached = std::make_unique<juce::DynamicObject>();
              for (const auto *key : {"song", "settings"})
                if (json.hasProperty(key)) cached->setProperty(key, json.getProperty(key, {}));
              receivedSnapshot = juce::var(cached.release());
            }
            Event event{kind, message->str, {}};
            event.value = json;
            if (!enqueue(std::move(event))) clientSocket->close(1013, "Remote receive queue full; reconnect");
          }
        }
      });
  clientSocket->start();
}

void RemoteSession::stop() {
  discovery.stop();
  if (clientSocket != nullptr) {
    clientSocket->stop();
    clientSender.reset();
    clientSocket.reset();
  }
  if (serverSocket != nullptr) {
    serverSocket->stop();
    serverSocket.reset();
  }
  {
    const std::lock_guard<std::mutex> lock(sendersMutex);
    peerSenders.clear();
    retiredSenders.clear();
  }
  connected = false;
  mode = Mode::Local;
  serverPort = 0;
  serverDisplayName.clear();
  selectedServerName.clear();
  {
    const std::lock_guard<std::mutex> lock(eventsMutex);
    events.clear();
  }
  {
    const std::lock_guard<std::mutex> lock(publishedMutex);
    latestSnapshot = juce::var();
    latestLibrary = juce::var();
    latestLibraryHash.clear();
    latestWaveformHash.clear();
    latestWaveformFrames.clear();
  }
}

int RemoteSession::getClientCount() const {
  const std::lock_guard<std::mutex> lock(sendersMutex);
  retiredSenders.erase(std::remove_if(retiredSenders.begin(), retiredSenders.end(),
      [](const auto &sender) { return sender->isFinished(); }), retiredSenders.end());
  return static_cast<int>(peerSenders.size());
}

std::vector<DiscoveredServer> RemoteSession::getDiscoveredServers() const {
  return discovery.getServers();
}

bool RemoteSession::enqueue(Event event) {
  const std::lock_guard<std::mutex> lock(eventsMutex);
  if (event.kind == Event::Kind::Connected || event.kind == Event::Kind::Disconnected) events.clear();
  // A server-side close must reach the UI even when its reliable command queue
  // is full, without discarding commands belonging to other clients.
  if (event.kind == Event::Kind::PeerDisconnected &&
      std::any_of(events.begin(), events.end(), [&](const Event &queued) {
        return queued.kind == Event::Kind::PeerDisconnected && queued.sourceId == event.sourceId;
      })) return true;
  if (event.kind == Event::Kind::Meters) {
    events.erase(std::remove_if(events.begin(), events.end(), [](const Event &queued) { return queued.kind == Event::Kind::Meters; }), events.end());
  }
  if (!event.coalescingKey.empty()) {
    for (auto it = events.end(); it != events.begin();) {
      --it;
      // A transport/song/setlist action is a barrier. Never reorder a fader
      // across a discrete action or merge gestures from different clients.
      if (it->coalescingKey.empty()) break;
      if (it->coalescingKey == event.coalescingKey) { events.erase(it); break; }
    }
  }
  if (events.size() >= 128 && event.kind != Event::Kind::PeerDisconnected) {
    if (event.kind == Event::Kind::Meters) return true;
    const auto meter = std::find_if(events.begin(), events.end(), [](const Event &v) { return v.kind == Event::Kind::Meters; });
    if (meter != events.end()) events.erase(meter);
    else return false;
  }
  events.push_back(std::move(event));
  return true;
}

std::vector<RemoteSession::Event> RemoteSession::drainEvents() {
  const std::lock_guard<std::mutex> lock(eventsMutex);
  std::vector<Event> result;
  result.reserve(events.size());
  while (!events.empty() && result.size() < 32) {
    result.push_back(std::move(events.front()));
    events.pop_front();
  }
  return result;
}

void RemoteSession::sendCommand(const juce::String &id,
                                const juce::String &action,
                                const juce::var &value,
                                const juce::String &target) {
  if (mode != Mode::Client || !connected || clientSocket == nullptr)
    return;
  auto object = std::make_unique<juce::DynamicObject>();
  object->setProperty("v", 1);
  object->setProperty("type", "command");
  object->setProperty("id", id);
  object->setProperty("action", action);
  object->setProperty("value", value);
  object->setProperty("target", target);
  clientSender->push(juce::var(object.release()));
}

void RemoteSession::sendAck(const juce::String &id, bool accepted,
                            const juce::String &reason) {
  if (mode != Mode::Server || serverSocket == nullptr)
    return;
  auto object = std::make_unique<juce::DynamicObject>();
  object->setProperty("v", 1);
  object->setProperty("type", "ack");
  object->setProperty("id", id);
  object->setProperty("accepted", accepted);
  object->setProperty("reason", reason);
  const juce::var message(object.release());
  const std::lock_guard<std::mutex> lock(sendersMutex);
  for (const auto &entry : peerSenders) entry.second->push(message);
}

void RemoteSession::putU32(std::string &data, uint32_t value) {
  data.push_back(static_cast<char>((value >> 24) & 0xff));
  data.push_back(static_cast<char>((value >> 16) & 0xff));
  data.push_back(static_cast<char>((value >> 8) & 0xff));
  data.push_back(static_cast<char>(value & 0xff));
}

uint32_t RemoteSession::getU32(const char *data) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(data[0])) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 8) |
         static_cast<uint32_t>(static_cast<uint8_t>(data[3]));
}

std::vector<std::string>
RemoteSession::encodeWaveform(const std::vector<float> &bins,
                              uint32_t serial) const {
  if (bins.empty() || bins.size() * sizeof(float) > kMaxWaveBytes)
    return {};
  std::string data;
  data.reserve(bins.size() * sizeof(float));
  for (float value : bins) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    putU32(data, bits);
  }
  const auto total = static_cast<uint16_t>(
      (data.size() + kWaveChunkBytes - 1) / kWaveChunkBytes);
  std::vector<std::string> frames;
  frames.reserve(total);
  for (uint16_t index = 0; index < total; ++index) {
    const size_t offset = static_cast<size_t>(index) * kWaveChunkBytes;
    const size_t length = (std::min)(kWaveChunkBytes, data.size() - offset);
    std::string frame("PWW1", 4);
    putU32(frame, serial);
    frame.push_back(static_cast<char>(index >> 8));
    frame.push_back(static_cast<char>(index & 0xff));
    frame.push_back(static_cast<char>(total >> 8));
    frame.push_back(static_cast<char>(total & 0xff));
    putU32(frame, static_cast<uint32_t>(length));
    frame.append(data, offset, length);
    frames.push_back(std::move(frame));
  }
  return frames;
}

void RemoteSession::sendWaveformTo(
    ix::WebSocket &socket, const std::vector<std::string> &frames) {
  for (const auto &frame : frames)
    socket.sendBinary(frame);
}

void RemoteSession::publish(const std::string &snapshotJson,
                            const std::vector<float> &waveform,
                            const juce::String &waveformHash) {
  auto snapshot = juce::JSON::parse(snapshotJson);
  const auto library = snapshot.getProperty("library", {});
  if (library.isObject()) {
    const auto json = juce::JSON::toString(library, true);
    const auto hash = juce::SHA256(json.toRawUTF8(), static_cast<size_t>(json.getNumBytesAsUTF8())).toHexString();
    if (hash == latestLibraryHash) snapshot.getDynamicObject()->removeProperty("library");
    latestLibraryHash = hash;
  }
  publishState(snapshot, waveform, waveformHash);
}

void RemoteSession::publishState(const juce::var &snapshot,
                                 const std::vector<float> &waveform,
                                 const juce::String &waveformHash) {
  if (mode != Mode::Server || serverSocket == nullptr || !snapshot.isObject()) return;
  const std::lock_guard<std::mutex> publishedLock(publishedMutex);
  const bool waveChanged = waveformHash != latestWaveformHash;
  if (waveChanged) {
    latestWaveformHash = waveformHash;
    latestWaveformFrames = encodeWaveform(waveform, ++waveformSerial);
  }
  const auto library = snapshot.getProperty("library", {});
  const bool libraryChanged = library.isObject();
  if (libraryChanged) latestLibrary = library;
  // Shallow copy the root; cached subtrees remain immutable and are shared.
  auto full = std::make_unique<juce::DynamicObject>();
  for (const auto &property : snapshot.getDynamicObject()->getProperties())
    full->setProperty(property.name, property.value);
  if (latestLibrary.isObject()) full->setProperty("library", latestLibrary);
  latestSnapshot = juce::var(full.release());
  const std::lock_guard<std::mutex> sendersLock(sendersMutex);
  for (const auto &entry : peerSenders) {
    entry.second->push(snapshot, libraryChanged ? RemoteSendQueue::Kind::Reliable : RemoteSendQueue::Kind::Snapshot);
    if (waveChanged) for (const auto &frame : latestWaveformFrames) entry.second->pushBinary(frame);
  }
}

void RemoteSession::onClientBinary(const std::string &data) {
  if (data.size() < 16 || data.compare(0, 4, "PWW1") != 0 ||
      data.size() > kWaveChunkBytes + 16)
    return;
  const uint32_t serial = getU32(data.data() + 4);
  const auto index = static_cast<uint16_t>(
      (static_cast<uint8_t>(data[8]) << 8) | static_cast<uint8_t>(data[9]));
  const auto count = static_cast<uint16_t>(
      (static_cast<uint8_t>(data[10]) << 8) | static_cast<uint8_t>(data[11]));
  const uint32_t length = getU32(data.data() + 12);
  if (count == 0 || count > 64 || index >= count ||
      length != data.size() - 16)
    return;
  // Reconnects and a late-join replay may reuse the same waveform serial.
  // A completed/aborted transfer has released its slots even if the IDs match.
  if (serial != receivingSerial || count != receivingCount || receivingChunks.size() != count) {
    receivingSerial = serial;
    receivingCount = count;
    receivingChunks.assign(count, {});
  }
  receivingChunks[index] = data.substr(16);
  size_t totalSize = 0;
  for (const auto &chunk : receivingChunks) {
    if (chunk.empty())
      return;
    totalSize += chunk.size();
  }
  if (totalSize > kMaxWaveBytes || totalSize % sizeof(float) != 0) {
    receivingChunks.clear();
    return;
  }
  Event result;
  result.kind = Event::Kind::Waveform;
  result.bytes.reserve(totalSize);
  for (const auto &chunk : receivingChunks)
    result.bytes.insert(result.bytes.end(), chunk.begin(), chunk.end());
  receivingChunks.clear();
  enqueue(std::move(result));
}

} // namespace playworship::remote

void playworship::remote::RemoteSession::broadcast(const std::string &json) {
  if (mode != Mode::Server || serverSocket == nullptr) return;
  const auto message = juce::JSON::parse(json);
  const std::lock_guard<std::mutex> lock(sendersMutex);
  for (const auto &entry : peerSenders) entry.second->push(message, RemoteSendQueue::Kind::Meters);
}
