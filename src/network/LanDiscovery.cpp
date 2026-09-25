#include "LanDiscovery.h"

#if (JUCE_MAC || JUCE_IOS) && !defined(PLAYWORSHIP_FORCE_PORTABLE_MDNS)
#define PLAYWORSHIP_NATIVE_BONJOUR 1
#include <dns_sd.h>
#else
#define PLAYWORSHIP_NATIVE_BONJOUR 0
#include "vendor/mdns.h"
#endif
#include <array>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <memory>
#if !JUCE_WINDOWS
#include <arpa/inet.h>
#include <sys/select.h>
#endif
#if JUCE_ANDROID
#include <jni.h>
#include <juce_core/native/juce_JNIHelpers_android.h>
#endif

namespace playworship::remote {
namespace {
#if !PLAYWORSHIP_NATIVE_BONJOUR
constexpr char kService[] = "_playworship._tcp.local.";
constexpr int kMdnsBufferSize = 4096;
#endif

void setAndroidMulticastEnabled(bool enabled) {
#if JUCE_ANDROID
  auto activity = juce::getCurrentActivity();
  if (activity.get() == nullptr)
    return;
  auto *env = juce::getEnv();
  auto activityClass = env->GetObjectClass(activity.get());
  if (activityClass == nullptr)
    return;
  auto method = env->GetMethodID(activityClass, "setRemoteMulticastEnabled",
                                  "(Z)V");
  if (method != nullptr)
    env->CallVoidMethod(activity.get(), method,
                        enabled ? JNI_TRUE : JNI_FALSE);
  env->DeleteLocalRef(activityClass);
#else
  juce::ignoreUnused(enabled);
#endif
}

#if PLAYWORSHIP_NATIVE_BONJOUR
// Use the system Bonjour daemon. Direct multicast requires a restricted
// entitlement on physical iOS devices, even when local-network access is granted.
constexpr char kServiceType[] = "_playworship._tcp";
using DiscoveryStatus = LanDiscovery::Status;

DiscoveryStatus statusForError(DNSServiceErrorType error) {
  return error == kDNSServiceErr_PolicyDenied ? DiscoveryStatus::PermissionDenied
                                            : DiscoveryStatus::Failed;
}

DNSServiceErrorType processBonjour(DNSServiceRef ref) {
  const int socket = DNSServiceRefSockFD(ref);
  if (socket < 0 || socket >= FD_SETSIZE)
    return kDNSServiceErr_BadReference;
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(socket, &readSet);
  timeval timeout{0, 200000};
  const int result = select(socket + 1, &readSet, nullptr, nullptr, &timeout);
  if (result > 0)
    return DNSServiceProcessResult(ref);
  if (result < 0 && errno != EINTR)
    return kDNSServiceErr_Unknown;
  return kDNSServiceErr_NoError;
}

void waitBeforeRetry(const std::atomic<bool> &running) {
  for (int i = 0; i < 15 && running; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

struct NativeBrowser;
struct NativeService {
  NativeBrowser *browser = nullptr;
  juce::String key, name, host;
  int port = 0;
  DNSServiceRef resolver = nullptr;
  DNSServiceRef addressQuery = nullptr;
  ~NativeService() {
    if (addressQuery != nullptr) DNSServiceRefDeallocate(addressQuery);
    if (resolver != nullptr) DNSServiceRefDeallocate(resolver);
  }
};

struct NativeBrowser {
  DNSServiceRef connection = nullptr;
  DNSServiceRef browser = nullptr;
  DNSServiceErrorType error = kDNSServiceErr_NoError;
  std::map<juce::String, std::unique_ptr<NativeService>> services;
  std::map<juce::String, DiscoveredServer> results;
  ~NativeBrowser() {
    services.clear();
    if (browser != nullptr) DNSServiceRefDeallocate(browser);
    if (connection != nullptr) DNSServiceRefDeallocate(connection);
  }
  void removeAddresses(const juce::String &key) {
    for (auto it = results.begin(); it != results.end();) {
      if (it->first.startsWith(key + "|")) it = results.erase(it);
      else ++it;
    }
  }
};

void DNSSD_API nativeAddressReply(DNSServiceRef, DNSServiceFlags flags, uint32_t,
    DNSServiceErrorType error, const char *, const sockaddr *address, uint32_t,
    void *context) {
  auto &service = *static_cast<NativeService *>(context);
  if (error != kDNSServiceErr_NoError) {
    service.browser->error = error;
    return;
  }
  if (address == nullptr || address->sa_family != AF_INET) return;
  char text[INET_ADDRSTRLEN]{};
  const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(address);
  if (inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text)) == nullptr) return;
  const auto key = service.key + "|" + juce::String(text);
  if ((flags & kDNSServiceFlagsAdd) != 0)
    service.browser->results[key] = {service.name, juce::String(text), service.port,
                                    juce::Time::currentTimeMillis()};
  else
    service.browser->results.erase(key);
}

void DNSSD_API nativeResolveReply(DNSServiceRef, DNSServiceFlags, uint32_t interfaceIndex,
    DNSServiceErrorType error, const char *, const char *host, uint16_t port,
    uint16_t, const unsigned char *, void *context) {
  auto &service = *static_cast<NativeService *>(context);
  auto &browser = *service.browser;
  if (error != kDNSServiceErr_NoError) { browser.error = error; return; }
  if (host == nullptr || port == 0) return;
  if (service.addressQuery != nullptr && service.host == host && service.port == ntohs(port)) return;
  if (service.addressQuery != nullptr) DNSServiceRefDeallocate(service.addressQuery);
  service.addressQuery = nullptr;
  browser.removeAddresses(service.key);
  service.host = host;
  service.port = ntohs(port);
  auto query = browser.connection;
  error = DNSServiceGetAddrInfo(&query, kDNSServiceFlagsShareConnection, interfaceIndex,
                                kDNSServiceProtocol_IPv4, host, nativeAddressReply, &service);
  if (error == kDNSServiceErr_NoError) service.addressQuery = query;
  else browser.error = error;
}

void DNSSD_API nativeBrowseReply(DNSServiceRef, DNSServiceFlags flags, uint32_t interfaceIndex,
    DNSServiceErrorType error, const char *name, const char *type, const char *domain,
    void *context) {
  auto &browser = *static_cast<NativeBrowser *>(context);
  if (error != kDNSServiceErr_NoError) { browser.error = error; return; }
  if (name == nullptr || type == nullptr || domain == nullptr) return;
  const auto key = juce::String(name) + "." + type + domain + "@" + juce::String(interfaceIndex);
  if ((flags & kDNSServiceFlagsAdd) == 0) {
    browser.removeAddresses(key);
    browser.services.erase(key);
    return;
  }
  if (browser.services.count(key) != 0) return;
  auto service = std::make_unique<NativeService>();
  service->browser = &browser;
  service->key = key;
  service->name = name;
  auto resolver = browser.connection;
  error = DNSServiceResolve(&resolver, kDNSServiceFlagsShareConnection, interfaceIndex,
                            name, type, domain, nativeResolveReply, service.get());
  if (error != kDNSServiceErr_NoError) { browser.error = error; return; }
  service->resolver = resolver;
  browser.services.emplace(key, std::move(service));
}

struct NativeRegistration {
  DNSServiceErrorType error = kDNSServiceErr_NoError;
  bool registered = false;
};
void DNSSD_API nativeRegisterReply(DNSServiceRef, DNSServiceFlags, DNSServiceErrorType error,
    const char *, const char *, const char *, void *context) {
  auto &registration = *static_cast<NativeRegistration *>(context);
  registration.error = error;
  registration.registered = error == kDNSServiceErr_NoError;
}
#else
struct ServiceContext {
  std::string instance;
  std::string host;
  int port = 0;
  sockaddr_in address{};
};

mdns_string_t asMdns(const std::string &s) { return {s.c_str(), s.size()}; }

mdns_record_t makePtr(const ServiceContext &ctx) {
  mdns_record_t record{};
  record.name = {kService, sizeof(kService) - 1};
  record.type = MDNS_RECORDTYPE_PTR;
  record.data.ptr.name = asMdns(ctx.instance);
  return record;
}

mdns_record_t makeSrv(const ServiceContext &ctx) {
  mdns_record_t record{};
  record.name = asMdns(ctx.instance);
  record.type = MDNS_RECORDTYPE_SRV;
  record.data.srv.name = asMdns(ctx.host);
  record.data.srv.port = static_cast<uint16_t>(ctx.port);
  return record;
}

mdns_record_t makeTxt(const ServiceContext &ctx) {
  mdns_record_t record{};
  record.name = asMdns(ctx.instance);
  record.type = MDNS_RECORDTYPE_TXT;
  record.data.txt.key = {"v", 1};
  record.data.txt.value = {"1", 1};
  return record;
}

mdns_record_t makeAddress(const ServiceContext &ctx) {
  mdns_record_t record{};
  record.name = asMdns(ctx.host);
  record.type = MDNS_RECORDTYPE_A;
  record.data.a.addr = ctx.address;
  return record;
}

int serviceCallback(int socket, const sockaddr *from, size_t addressLength,
                    mdns_entry_type_t entry, uint16_t queryId, uint16_t type,
                    uint16_t recordClass, uint32_t, const void *data,
                    size_t size, size_t nameOffset, size_t, size_t, size_t,
                    void *userData) {
  if (entry != MDNS_ENTRYTYPE_QUESTION || userData == nullptr)
    return 0;
  auto &ctx = *static_cast<ServiceContext *>(userData);
  char nameBuffer[256]{};
  const auto name = mdns_string_extract(data, size, &nameOffset, nameBuffer,
                                        sizeof(nameBuffer));
  const std::string queryName(name.str != nullptr ? name.str : "", name.length);
  mdns_record_t answer{};
  std::array<mdns_record_t, 3> additional{};
  size_t additionalCount = 0;
  if (queryName == kService &&
      (type == MDNS_RECORDTYPE_PTR || type == MDNS_RECORDTYPE_ANY)) {
    answer = makePtr(ctx);
    additional = {makeSrv(ctx), makeTxt(ctx), makeAddress(ctx)};
    additionalCount = 3;
  } else if (queryName == ctx.instance &&
             (type == MDNS_RECORDTYPE_SRV || type == MDNS_RECORDTYPE_ANY)) {
    answer = makeSrv(ctx);
    additional[0] = makeTxt(ctx);
    additional[1] = makeAddress(ctx);
    additionalCount = 2;
  } else if (queryName == ctx.instance && type == MDNS_RECORDTYPE_TXT) {
    answer = makeTxt(ctx);
    additional[0] = makeSrv(ctx);
    additional[1] = makeAddress(ctx);
    additionalCount = 2;
  } else if (queryName == ctx.host &&
             (type == MDNS_RECORDTYPE_A || type == MDNS_RECORDTYPE_ANY)) {
    answer = makeAddress(ctx);
  } else {
    return 0;
  }
  alignas(4) std::array<uint8_t, kMdnsBufferSize> output{};
  if ((recordClass & MDNS_UNICAST_RESPONSE) != 0)
    mdns_query_answer_unicast(socket, from, addressLength, output.data(),
                              output.size(), queryId,
                              static_cast<mdns_record_type_t>(type),
                              queryName.c_str(), queryName.size(), answer,
                              nullptr, 0,
                              additionalCount != 0 ? additional.data() : nullptr,
                              additionalCount);
  else
    mdns_query_answer_multicast(socket, output.data(), output.size(), answer,
                                nullptr, 0,
                                additionalCount != 0 ? additional.data() : nullptr,
                                additionalCount);
  return 0;
}

struct BrowserContext {
  LanDiscovery *owner = nullptr;
  std::map<juce::String, DiscoveredServer> *results = nullptr;
};

int browserCallback(int, const sockaddr *from, size_t, mdns_entry_type_t,
                    uint16_t, uint16_t type, uint16_t, uint32_t ttl,
                    const void *data, size_t size, size_t nameOffset, size_t,
                    size_t recordOffset, size_t recordLength, void *userData) {
  if (type != MDNS_RECORDTYPE_SRV || from == nullptr ||
      from->sa_family != AF_INET || userData == nullptr)
    return 0;
  auto &ctx = *static_cast<BrowserContext *>(userData);
  char instanceBuffer[256]{};
  const auto instance = mdns_string_extract(data, size, &nameOffset,
                                             instanceBuffer,
                                             sizeof(instanceBuffer));
  juce::String fullName = juce::String::fromUTF8(instance.str,
                                                  static_cast<int>(instance.length));
  if (!fullName.endsWithIgnoreCase(kService))
    return 0;
  char hostBuffer[256]{};
  const auto srv = mdns_record_parse_srv(data, size, recordOffset,
                                          recordLength, hostBuffer,
                                          sizeof(hostBuffer));
  if (srv.port == 0)
    return 0;
  char ip[INET_ADDRSTRLEN]{};
  const auto *source = reinterpret_cast<const sockaddr_in *>(from);
  if (inet_ntop(AF_INET, &source->sin_addr, ip, sizeof(ip)) == nullptr)
    return 0;
  const auto display =
      fullName.dropLastCharacters(static_cast<int>(sizeof(kService) - 1))
          .trimCharactersAtEnd(".");
  if (ttl == 0) {
    ctx.results->erase(fullName);
  } else {
    (*ctx.results)[fullName] =
        {display, juce::String(ip), static_cast<int>(srv.port),
         juce::Time::currentTimeMillis()};
  }
  return 0;
}

#endif
} // namespace

LanDiscovery::~LanDiscovery() { stop(); }

bool LanDiscovery::startAdvertising(const juce::String &name,
                                    int websocketPort) {
  stop();
  if (websocketPort < 1 || websocketPort > 65535)
    return false;
  advertising = true;
  port = websocketPort;
  instanceName = name.trim().replaceCharacter('.', '-').substring(0, 40);
  if (instanceName.isEmpty())
    instanceName = "PlayWorship";
  running = true;
  status = Status::Searching;
  setAndroidMulticastEnabled(true);
  worker = std::thread([this] { runAdvertiser(); });
  return true;
}

bool LanDiscovery::startBrowsing() {
  stop();
  advertising = false;
  running = true;
  status = Status::Searching;
  setAndroidMulticastEnabled(true);
  worker = std::thread([this] { runBrowser(); });
  return true;
}

void LanDiscovery::stop() {
  running = false;
  if (worker.joinable())
    worker.join();
  setAndroidMulticastEnabled(false);
  const std::lock_guard<std::mutex> lock(serversMutex);
  servers.clear();
  status = Status::Stopped;
}

std::vector<DiscoveredServer> LanDiscovery::getServers() const {
  const std::lock_guard<std::mutex> lock(serversMutex);
  std::vector<DiscoveredServer> result;
  const auto now = juce::Time::currentTimeMillis();
  for (const auto &entry : servers) {
    const auto &server = entry.second;
    if (now - server.lastSeenMs < 20000) {
      const auto duplicate = std::find_if(result.begin(), result.end(), [&](const auto &existing) {
        return existing.instance == server.instance && existing.address == server.address && existing.port == server.port;
      });
      if (duplicate == result.end()) result.push_back(server);
    }
  }
  return result;
}

void LanDiscovery::runAdvertiser() {
#if PLAYWORSHIP_NATIVE_BONJOUR
  while (running) {
    DNSServiceRef registration = nullptr;
    NativeRegistration context;
    context.error = DNSServiceRegister(&registration, 0, kDNSServiceInterfaceIndexAny,
        instanceName.toRawUTF8(), kServiceType, "local.", nullptr,
        htons(static_cast<uint16_t>(port)), 0, nullptr, nativeRegisterReply, &context);
    while (running && context.error == kDNSServiceErr_NoError) {
      const auto error = processBonjour(registration);
      if (error != kDNSServiceErr_NoError) context.error = error;
      if (context.registered) status = Status::Ready;
    }
    if (registration != nullptr) DNSServiceRefDeallocate(registration);
    if (context.error != kDNSServiceErr_NoError) {
      status = statusForError(context.error);
      juce::Logger::writeToLog("Remote Bonjour registration error: " + juce::String(context.error));
      waitBeforeRetry(running);
    }
  }
#else
  struct Endpoint { int socket; ServiceContext context; };
  std::vector<Endpoint> endpoints;
  const auto hostId = juce::Uuid().toString().removeCharacters("-").substring(0, 8);
  for (const auto &ip : juce::IPAddress::getAllAddresses()) {
    if (ip.isNull() || ip.isIPv6 || ip.toString().startsWith("127.")) continue;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(MDNS_PORT);
#if JUCE_MAC || JUCE_IOS
    address.sin_len = sizeof(address);
#endif
    if (inet_pton(AF_INET, ip.toString().toRawUTF8(), &address.sin_addr) != 1) continue;
    const int socket = mdns_socket_open_ipv4(&address);
    if (socket < 0) continue;
    ServiceContext context;
    context.instance = instanceName.toStdString() + "." + kService;
    context.host = ("playworship-" + hostId + "-" + juce::String(static_cast<int>(endpoints.size())) + ".local.").toStdString();
    context.port = port;
    context.address = address;
    endpoints.push_back({socket, std::move(context)});
  }
  if (endpoints.empty()) { status = Status::Failed; return; }
  status = Status::Ready;
  alignas(4) std::array<uint8_t, kMdnsBufferSize> buffer{};
  auto announce = [&](bool goodbye) {
    for (const auto &endpoint : endpoints) {
      const auto ptr = makePtr(endpoint.context);
      const std::array<mdns_record_t, 3> additional{makeSrv(endpoint.context),
          makeTxt(endpoint.context), makeAddress(endpoint.context)};
      if (goodbye)
        mdns_goodbye_multicast(endpoint.socket, buffer.data(), buffer.size(), ptr,
                              nullptr, 0, additional.data(), additional.size());
      else
        mdns_announce_multicast(endpoint.socket, buffer.data(), buffer.size(), ptr,
                               nullptr, 0, additional.data(), additional.size());
    }
  };
  announce(false);
  auto lastAnnouncement = juce::Time::currentTimeMillis();
  while (running) {
    fd_set readSet;
    FD_ZERO(&readSet);
    int maxSocket = 0;
    for (const auto &endpoint : endpoints) {
      FD_SET(endpoint.socket, &readSet);
      maxSocket = juce::jmax(maxSocket, endpoint.socket);
    }
    timeval timeout{0, 200000};
    if (select(maxSocket + 1, &readSet, nullptr, nullptr, &timeout) > 0)
      for (auto &endpoint : endpoints)
        if (FD_ISSET(endpoint.socket, &readSet))
          mdns_socket_listen(endpoint.socket, buffer.data(), buffer.size(), serviceCallback, &endpoint.context);
    if (juce::Time::currentTimeMillis() - lastAnnouncement > 10000) {
      announce(false);
      lastAnnouncement = juce::Time::currentTimeMillis();
    }
  }
  announce(true);
  for (const auto &endpoint : endpoints) mdns_socket_close(endpoint.socket);
#endif
}

void LanDiscovery::runBrowser() {
#if PLAYWORSHIP_NATIVE_BONJOUR
  while (running) {
    NativeBrowser context;
    context.error = DNSServiceCreateConnection(&context.connection);
    if (context.error == kDNSServiceErr_NoError) {
      auto browser = context.connection;
      context.error = DNSServiceBrowse(&browser, kDNSServiceFlagsShareConnection,
          kDNSServiceInterfaceIndexAny, kServiceType, "local.", nativeBrowseReply, &context);
      if (context.error == kDNSServiceErr_NoError) context.browser = browser;
    }
    while (running && context.error == kDNSServiceErr_NoError) {
      const auto error = processBonjour(context.connection);
      if (error != kDNSServiceErr_NoError) context.error = error;
      // Bonjour owns expiration and emits remove callbacks. Refresh the UI
      // cache while a service remains present, even without periodic packets.
      for (auto &[key, server] : context.results) {
        juce::ignoreUnused(key);
        server.lastSeenMs = juce::Time::currentTimeMillis();
      }
      {
        const std::lock_guard<std::mutex> lock(serversMutex);
        servers = context.results;
      }
      if (context.error == kDNSServiceErr_NoError)
        status = context.results.empty() ? Status::Searching : Status::Ready;
    }
    if (context.error != kDNSServiceErr_NoError) {
      status = statusForError(context.error);
      juce::Logger::writeToLog("Remote Bonjour discovery error: " + juce::String(context.error));
      {
        const std::lock_guard<std::mutex> lock(serversMutex);
        servers.clear();
      }
      waitBeforeRetry(running);
    }
  }
#else
  std::vector<int> sockets;
  for (const auto &ip : juce::IPAddress::getAllAddresses()) {
    if (ip.isNull() || ip.isIPv6 || ip.toString().startsWith("127."))
      continue;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
#if JUCE_MAC || JUCE_IOS
    address.sin_len = sizeof(address);
#endif
    const auto text = ip.toString().toStdString();
    if (inet_pton(AF_INET, text.c_str(), &address.sin_addr) != 1)
      continue;
    const int socket = mdns_socket_open_ipv4(&address);
    if (socket >= 0)
      sockets.push_back(socket);
  }
  if (sockets.empty()) {
    const int socket = mdns_socket_open_ipv4(nullptr);
    if (socket >= 0)
      sockets.push_back(socket);
  }
  if (sockets.empty()) { status = Status::Failed; return; }
  std::map<juce::String, DiscoveredServer> results;
  BrowserContext ctx{this, &results};
  alignas(4) std::array<uint8_t, kMdnsBufferSize> buffer{};
  juce::int64 lastQuery = 0;
  while (running && !sockets.empty()) {
    const auto now = juce::Time::currentTimeMillis();
    if (now - lastQuery >= 3000) {
      for (int socket : sockets)
        mdns_query_send(socket, MDNS_RECORDTYPE_PTR, kService,
                        sizeof(kService) - 1, buffer.data(), buffer.size(), 0);
      lastQuery = now;
    }
    fd_set readSet;
    FD_ZERO(&readSet);
    int maxSocket = 0;
    for (int socket : sockets) {
      FD_SET(socket, &readSet);
      maxSocket = juce::jmax(maxSocket, socket);
    }
    timeval timeout{0, 200000};
    if (select(maxSocket + 1, &readSet, nullptr, nullptr, &timeout) > 0) {
      for (int socket : sockets)
        if (FD_ISSET(socket, &readSet))
          mdns_query_recv(socket, buffer.data(), buffer.size(),
                          browserCallback, &ctx, 0);
      const std::lock_guard<std::mutex> lock(serversMutex);
      servers = results;
      status = results.empty() ? Status::Searching : Status::Ready;
    }
  }
  for (int socket : sockets)
    mdns_socket_close(socket);
#endif
}

} // namespace playworship::remote
