#pragma once

#include "LanDiscovery.h"
#include <JuceHeader.h>
#include <deque>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ix {
class WebSocket;
class WebSocketServer;
}

namespace playworship::remote {
class RemoteSendQueue;

class RemoteSession final {
public:
  enum class Mode { Local, Server, Client };
  struct Event {
    enum class Kind { Snapshot, Meters, Waveform, Command, Connected, Disconnected };
    Kind kind = Kind::Snapshot;
    std::string text;
    std::vector<uint8_t> bytes;
    std::string coalescingKey{};
    std::string sourceId{};
    juce::var value{};
  };

  RemoteSession();
  ~RemoteSession();
  bool becomeServer(const juce::String &displayName);
  void becomeClient();
  void connectTo(const DiscoveredServer &server);
  void stop();
  Mode getMode() const noexcept { return mode; }
  bool isConnected() const noexcept { return connected; }
  int getClientCount() const;
  int getPort() const noexcept { return serverPort; }
  juce::String getSelectedServerName() const { return selectedServerName; }
  std::vector<DiscoveredServer> getDiscoveredServers() const;
  LanDiscovery::Status getDiscoveryStatus() const noexcept { return discovery.getStatus(); }
  std::vector<Event> drainEvents();
  void sendCommand(const juce::String &id, const juce::String &action,
                   const juce::var &value = {}, const juce::String &target = {});
  void sendAck(const juce::String &id, bool accepted,
               const juce::String &reason = {});
  void broadcast(const std::string &json);
  void publish(const std::string &snapshotJson,
               const std::vector<float> &waveform,
               const juce::String &waveformHash);
  void publishState(const juce::var &snapshot,
                    const std::vector<float> &waveform,
                    const juce::String &waveformHash);

private:
  bool enqueue(Event event);
  void sendWaveformTo(ix::WebSocket &socket,
                      const std::vector<std::string> &frames);
  std::vector<std::string> encodeWaveform(const std::vector<float> &bins,
                                         uint32_t serial) const;
  void onClientBinary(const std::string &data);
  static void putU32(std::string &data, uint32_t value);
  static uint32_t getU32(const char *data);

  Mode mode = Mode::Local;
  std::atomic<bool> connected{false};
  int serverPort = 0;
  juce::String selectedServerName;
  LanDiscovery discovery;
  std::unique_ptr<ix::WebSocketServer> serverSocket;
  std::shared_ptr<ix::WebSocket> clientSocket;
  std::shared_ptr<RemoteSendQueue> clientSender;
  mutable std::mutex sendersMutex;
  std::map<std::string, std::shared_ptr<RemoteSendQueue>> peerSenders;
  mutable std::vector<std::shared_ptr<RemoteSendQueue>> retiredSenders;
  mutable std::mutex eventsMutex;
  std::deque<Event> events;
  mutable std::mutex publishedMutex;
  juce::var latestSnapshot, latestLibrary;
  juce::String latestLibraryHash;
  juce::String latestWaveformHash;
  std::vector<std::string> latestWaveformFrames;
  uint32_t waveformSerial = 0;
  uint32_t receivingSerial = 0;
  juce::var receivedSnapshot;
  uint16_t receivingCount = 0;
  std::vector<std::string> receivingChunks;
};

} // namespace playworship::remote
