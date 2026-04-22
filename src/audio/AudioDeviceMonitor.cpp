#include "AudioDeviceMonitor.h"

namespace {
juce::String makeKey(const juce::String &typeName, const juce::String &deviceName) {
  return typeName.trim().toLowerCase() + "|" + deviceName.trim().toLowerCase();
}
} // namespace

AudioDeviceMonitor::AudioDeviceMonitor(juce::AudioDeviceManager &managerRef)
    : deviceManager(managerRef) {
  deviceManager.addChangeListener(this);
  triggerAsyncUpdate();
}

AudioDeviceMonitor::~AudioDeviceMonitor() {
  cancelPendingUpdate();
  deviceManager.removeChangeListener(this);
}

void AudioDeviceMonitor::changeListenerCallback(juce::ChangeBroadcaster *) {
  triggerAsyncUpdate();
}

void AudioDeviceMonitor::handleAsyncUpdate() {
  scanAndDiff();
}

void AudioDeviceMonitor::scanAndDiff() {
  const auto &deviceTypes = deviceManager.getAvailableDeviceTypes();
  auto *activeDevice = deviceManager.getCurrentAudioDevice();
  const juce::String activeType = deviceManager.getCurrentAudioDeviceType();
  const juce::String activeName =
      activeDevice != nullptr ? activeDevice->getName().trim() : juce::String{};
  const int activeOutputs =
      activeDevice != nullptr ? activeDevice->getOutputChannelNames().size() : 0;

  std::map<juce::String, DeviceInfo> current;

  for (auto *ioType : deviceTypes) {
    if (ioType == nullptr)
      continue;
    const juce::String typeName = ioType->getTypeName();
    const juce::StringArray outputNames = ioType->getDeviceNames(false);

    for (int i = 0; i < outputNames.size(); ++i) {
      const auto devName = outputNames[i].trim();
      if (devName.isEmpty())
        continue;

      int numOut = 0;
      if (typeName.equalsIgnoreCase(activeType) &&
          devName.equalsIgnoreCase(activeName))
        numOut = juce::jmax(0, activeOutputs);

      DeviceInfo info;
      info.name = devName;
      info.typeName = typeName;
      info.numOutputChannels = juce::jmax(0, numOut);
      info.isMultichannel = info.numOutputChannels > 2;
      current[makeKey(typeName, devName)] = std::move(info);
    }
  }

  if (!firstScanDone) {
    lastSnapshotByKey = std::move(current);
    firstScanDone = true;
    return;
  }

  for (const auto &kv : current) {
    if (lastSnapshotByKey.find(kv.first) == lastSnapshotByKey.end()) {
      const DeviceInfo info = kv.second;
      if (onDeviceConnected != nullptr) {
        juce::MessageManager::callAsync(
            [cb = onDeviceConnected, info] { cb(info); });
      }
    }
  }

  for (const auto &kv : lastSnapshotByKey) {
    if (current.find(kv.first) == current.end()) {
      const juce::String name = kv.second.name;
      if (onDeviceDisconnected != nullptr) {
        juce::MessageManager::callAsync(
            [cb = onDeviceDisconnected, name] { cb(name); });
      }
    }
  }

  lastSnapshotByKey = std::move(current);
}
