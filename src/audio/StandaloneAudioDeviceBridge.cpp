#include "StandaloneAudioDeviceBridge.h"

StandaloneAudioDeviceBridge::StandaloneAudioDeviceBridge()
    : audioDeviceMonitor(audioDeviceManager) {
  juce::String err = audioDeviceManager.initialise(0, 0, nullptr, true);
  juce::ignoreUnused(err);
  syncMonitorCallbacks();
}

void StandaloneAudioDeviceBridge::syncMonitorCallbacks() {
  audioDeviceMonitor.onDeviceConnected =
      [this](const AudioDeviceMonitor::DeviceInfo &info) {
        if (onDeviceConnected)
          onDeviceConnected(info);
      };
  audioDeviceMonitor.onDeviceDisconnected =
      [this](const juce::String &name) {
        if (onDeviceDisconnected)
          onDeviceDisconnected(name);
      };
}

void StandaloneAudioDeviceBridge::setAvailableOutputChannelCount(
    const int outputCount) {
  availableOutputChannelCount.store(juce::jmax(1, outputCount),
                                    std::memory_order_relaxed);
}

int StandaloneAudioDeviceBridge::getAvailableOutputChannelCount() const noexcept {
  return availableOutputChannelCount.load(std::memory_order_relaxed);
}

juce::StringArray StandaloneAudioDeviceBridge::getOutputChannelNames() const {
  return OutputChannelCatalog::buildOutputChannelNames(
      getAvailableOutputChannelCount());
}

AppAudioMode StandaloneAudioDeviceBridge::getCurrentAudioMode() const noexcept {
  return getAvailableOutputChannelCount() > 2 ? AppAudioMode::Stage
                                               : AppAudioMode::Rehearsal;
}

void StandaloneAudioDeviceBridge::setOnDeviceConnected(
    std::function<void(const AudioDeviceMonitor::DeviceInfo &)> cb) {
  onDeviceConnected = std::move(cb);
  syncMonitorCallbacks();
}

void StandaloneAudioDeviceBridge::setOnDeviceDisconnected(
    std::function<void(const juce::String &)> cb) {
  onDeviceDisconnected = std::move(cb);
  syncMonitorCallbacks();
}
