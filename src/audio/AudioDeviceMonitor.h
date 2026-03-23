#pragma once

#include <JuceHeader.h>

#include <functional>
#include <map>
#include <vector>

/** Observa enumeración de dispositivos de audio; no modifica el dispositivo activo. */
class AudioDeviceMonitor : public juce::ChangeListener, private juce::Timer {
public:
  struct DeviceInfo {
    juce::String name;
    juce::String typeName;
    int numOutputChannels = 0;
    bool isMultichannel = false;
  };

  explicit AudioDeviceMonitor(juce::AudioDeviceManager &managerRef);
  ~AudioDeviceMonitor() override;

  std::function<void(const DeviceInfo &)> onDeviceConnected;
  std::function<void(const juce::String &)> onDeviceDisconnected;

  void changeListenerCallback(juce::ChangeBroadcaster *) override;

private:
  void timerCallback() override;
  void scanAndDiff();

  juce::AudioDeviceManager &deviceManager;
  std::map<juce::String, DeviceInfo> lastSnapshotByKey;
  bool firstScanDone = false;
};
