#pragma once

#include "AudioDeviceMonitor.h"
#include "OutputChannelCatalog.h"
#include <JuceHeader.h>
#include <atomic>
#include <functional>

enum class AppAudioMode { Rehearsal, Stage };

class StandaloneAudioDeviceBridge {
public:
  StandaloneAudioDeviceBridge();
  juce::String initialise(int maxOutputChannels,
                          const juce::XmlElement *savedSetup = nullptr);

  void setAvailableOutputChannelCount(int outputCount);
  int getAvailableOutputChannelCount() const noexcept;
  juce::StringArray getOutputChannelNames() const;
  AppAudioMode getCurrentAudioMode() const noexcept;

  juce::AudioDeviceManager &getAudioDeviceManager() noexcept {
    return audioDeviceManager;
  }
  const juce::AudioDeviceManager &getAudioDeviceManager() const noexcept {
    return audioDeviceManager;
  }
  AudioDeviceMonitor &getDeviceMonitor() noexcept { return audioDeviceMonitor; }
  const AudioDeviceMonitor &getDeviceMonitor() const noexcept {
    return audioDeviceMonitor;
  }

  void setOnDeviceConnected(
      std::function<void(const AudioDeviceMonitor::DeviceInfo &)> cb);
  void setOnDeviceDisconnected(std::function<void(const juce::String &)> cb);

private:
  void syncMonitorCallbacks();

  juce::AudioDeviceManager audioDeviceManager;
  AudioDeviceMonitor audioDeviceMonitor;
  std::atomic<int> availableOutputChannelCount{2};
  std::function<void(const AudioDeviceMonitor::DeviceInfo &)> onDeviceConnected;
  std::function<void(const juce::String &)> onDeviceDisconnected;
};
