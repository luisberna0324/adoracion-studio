#pragma once

#include <JuceHeader.h>

class LocalAuthCallbackServer final : private juce::Thread {
public:
  using Callback = std::function<void(const juce::String &)>;

  explicit LocalAuthCallbackServer(Callback cb);
  ~LocalAuthCallbackServer() override;

  bool start(int &outPort);
  void stop();

private:
  void run() override;

  juce::StreamingSocket listenerSocket;
  Callback onCallback;
};
