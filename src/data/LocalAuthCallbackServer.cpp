#include "LocalAuthCallbackServer.h"

LocalAuthCallbackServer::LocalAuthCallbackServer(Callback cb)
    : juce::Thread("AuthCallbackServer"), onCallback(std::move(cb)) {}

LocalAuthCallbackServer::~LocalAuthCallbackServer() { stop(); }

bool LocalAuthCallbackServer::start(int &outPort) {
  stop();
  outPort = 0;

  if (listenerSocket.createListener(0, "127.0.0.1")) {
    outPort = listenerSocket.getBoundPort();
    if (outPort > 0) {
      startThread();
      return true;
    }
    listenerSocket.close();
  }

  for (int i = 0; i < 32; ++i) {
    const int candidate = 43123 + i;
    if (!listenerSocket.createListener(candidate, "127.0.0.1"))
      continue;
    outPort = listenerSocket.getBoundPort();
    if (outPort <= 0)
      outPort = candidate;
    startThread();
    return true;
  }
  return false;
}

void LocalAuthCallbackServer::stop() {
  signalThreadShouldExit();
  listenerSocket.close();
  stopThread(1500);
}

void LocalAuthCallbackServer::run() {
  while (!threadShouldExit()) {
    auto *client = listenerSocket.waitForNextConnection();
    if (client == nullptr)
      continue;

    juce::String rawRequest;
    char buffer[4096] = {};
    if (client->waitUntilReady(true, 1200) == 1) {
      for (int attempt = 0; attempt < 32; ++attempt) {
        const int bytesRead =
            client->read(buffer, static_cast<int>(sizeof(buffer) - 1), false);
        if (bytesRead <= 0)
          break;
        buffer[bytesRead] = '\0';
        rawRequest += juce::String::fromUTF8(buffer);
        // Callback URLs can be long due to encoded session payload.
        if (rawRequest.contains("\r\n\r\n"))
          break;
        if (client->waitUntilReady(true, 40) != 1)
          break;
      }
    }

    const auto firstLine =
        rawRequest.upToFirstOccurrenceOf("\r\n", false, false);
    const auto parts = juce::StringArray::fromTokens(firstLine, " ", "");
    const auto requestPath = parts.size() >= 2 ? parts[1].trim() : juce::String{};
    if (requestPath.startsWith("/auth/callback")) {
      if (onCallback)
        onCallback("http://127.0.0.1" + requestPath);
    }

    const juce::String response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n\r\n"
        "<html><body><h3>Procesando autenticacion...</h3>"
        "<p>Vuelve a la aplicacion para confirmar el resultado.</p>"
        "<script>setTimeout(function(){ window.close(); }, 900);</script>"
        "</body></html>";
    client->write(response.toRawUTF8(), response.getNumBytesAsUTF8() - 1);
    client->close();
    delete client;
  }
}
