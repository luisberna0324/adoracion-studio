#include "StandaloneAudioDeviceBridge.h"

StandaloneAudioDeviceBridge::StandaloneAudioDeviceBridge()
    : audioDeviceMonitor(audioDeviceManager) {
  syncMonitorCallbacks();
}

juce::String StandaloneAudioDeviceBridge::initialise(
    int maxOutputChannels, const juce::XmlElement *savedSetup) {
  // Sanitize before opening the device, so an old settings file cannot briefly
  // reopen a microphone while the app is starting.
  auto outputOnlySetup = savedSetup != nullptr
                             ? std::make_unique<juce::XmlElement>(*savedSetup)
                             : nullptr;
  if (outputOnlySetup != nullptr) {
    const auto legacyDeviceName = outputOnlySetup->getStringAttribute("audioDeviceName");
    if (legacyDeviceName.isNotEmpty()) {
      outputOnlySetup->setAttribute("audioOutputDeviceName", legacyDeviceName);
      outputOnlySetup->removeAttribute("audioDeviceName");
    }
    outputOnlySetup->setAttribute("audioInputDeviceName", "");
    outputOnlySetup->setAttribute("audioDeviceInChans", "0");
  }
  auto error = audioDeviceManager.initialise(
      0, juce::jmax(1, maxOutputChannels), outputOnlySetup.get(), true);
  juce::AudioDeviceManager::AudioDeviceSetup setup;
  audioDeviceManager.getAudioDeviceSetup(setup);
  setup.inputDeviceName = {};
  setup.inputChannels.clear();
  setup.useDefaultInputChannels = false;
  if (error.isEmpty())
    error = audioDeviceManager.setAudioDeviceSetup(setup, false);

  // Fallback UX: if there are MIDI input devices detected but none enabled yet,
  // enable them so MIDI Learn works out of the box on first launch.
  const auto midiInputs = juce::MidiInput::getAvailableDevices();
  bool hasEnabledInput = false;
  for (const auto &dev : midiInputs) {
    if (audioDeviceManager.isMidiInputDeviceEnabled(dev.identifier)) {
      hasEnabledInput = true;
      break;
    }
  }
  if (!hasEnabledInput) {
    for (const auto &dev : midiInputs)
      audioDeviceManager.setMidiInputDeviceEnabled(dev.identifier, true);
  }

  return error;
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
