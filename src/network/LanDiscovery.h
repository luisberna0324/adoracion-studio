#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace playworship::remote {

struct DiscoveredServer {
  juce::String instance;
  juce::String address;
  int port = 0;
  juce::int64 lastSeenMs = 0;
};

/** DNS-SD discovery for the foreground standalone application. */
class LanDiscovery final {
public:
  enum class Status { Stopped, Searching, Ready, PermissionDenied, Failed };
  LanDiscovery() = default;
  ~LanDiscovery();

  bool startAdvertising(const juce::String &instanceName, int websocketPort);
  bool startBrowsing();
  void stop();
  std::vector<DiscoveredServer> getServers() const;
  Status getStatus() const noexcept { return status.load(); }

private:
  void runAdvertiser();
  void runBrowser();
  std::atomic<bool> running{false};
  std::atomic<Status> status{Status::Stopped};
  std::thread worker;
  bool advertising = false;
  int port = 0;
  juce::String instanceName;
  mutable std::mutex serversMutex;
  std::map<juce::String, DiscoveredServer> servers;
};

} // namespace playworship::remote
