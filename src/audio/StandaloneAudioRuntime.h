#pragma once

#include "StandaloneAudioDeviceBridge.h"

// Owns the app's audio/MIDI attachment, without a plug-in host or wrapper.
// Processor and bridge outlive this object; detach before destroying the editor
// or processor. The old standalone settings keys are intentionally retained.
class StandaloneAudioRuntime final {
public:
  StandaloneAudioRuntime(juce::AudioProcessor &processorToUse,
                         StandaloneAudioDeviceBridge &bridgeToUse,
                         juce::PropertySet *settingsToUse = nullptr)
      : processor(processorToUse), bridge(bridgeToUse), settings(settingsToUse) {}

  ~StandaloneAudioRuntime() { stop(); }

  juce::String start(int maxOutputChannels) {
    if (started)
      return {};
    const auto savedSetup = settings != nullptr
                                ? settings->getXmlValue("audioSetup") : nullptr;
    const auto error = bridge.initialise(maxOutputChannels, savedSetup.get());
    auto &manager = bridge.getAudioDeviceManager();
    // State restoration normalizes saved routes. Publish the real device width
    // first so output 3+ is not clamped to the bridge's initial stereo default.
    // With no device, retain legal routes until a device is available.
    const auto *device = manager.getCurrentAudioDevice();
    bridge.setAvailableOutputChannelCount(device != nullptr
        ? device->getActiveOutputChannels().countNumberOfSetBits()
        : maxOutputChannels);
    if (settings != nullptr) {
      juce::MemoryBlock state;
      if (state.fromBase64Encoding(settings->getValue("filterState")) &&
          state.getSize() != 0)
        processor.setStateInformation(state.getData(),
                                      static_cast<int>(state.getSize()));
    }
    player.setProcessor(&processor);
    manager.addAudioCallback(&player);
    manager.addMidiInputDeviceCallback({}, &player);
    started = true;
    return error;
  }

  void saveState() {
    if (settings == nullptr || !started)
      return;
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    settings->setValue("filterState", state.toBase64Encoding());
    if (const auto audioSetup = bridge.getAudioDeviceManager().createStateXml())
      settings->setValue("audioSetup", audioSetup.get());
  }

  void stop() {
    if (!started)
      return;
    auto &manager = bridge.getAudioDeviceManager();
    manager.removeMidiInputDeviceCallback({}, &player);
    manager.removeAudioCallback(&player);
    player.setProcessor(nullptr);
    manager.closeAudioDevice();
    started = false;
  }

private:
  juce::AudioProcessor &processor;
  StandaloneAudioDeviceBridge &bridge;
  juce::PropertySet *settings;
  juce::AudioProcessorPlayer player;
  bool started = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneAudioRuntime)
};
