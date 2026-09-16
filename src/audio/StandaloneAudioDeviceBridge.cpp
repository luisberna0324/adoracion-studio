#include "StandaloneAudioDeviceBridge.h"

StandaloneAudioDeviceBridge::StandaloneAudioDeviceBridge()
    : audioDeviceMonitor(audioDeviceManager) {
  // The standalone app must request an output bus. Initialising with zero
  // outputs leaves Android with a valid UI transport but no audio callback.
  juce::String err = audioDeviceManager.initialise(0, 2, nullptr, true);
  juce::ignoreUnused(err);

#if JUCE_ANDROID
  // The device's low-latency default can be only 2 ms. On the Galaxy A50,
  // decoding and time-processing a 19-stem song takes around 20-30 ms on a
  // busy block, so a 20 ms callback still underruns. Keep enough headroom for
  // those bursts before the host registers its audio callback.
  if (auto* device = audioDeviceManager.getCurrentAudioDevice()) {
    auto setup = audioDeviceManager.getAudioDeviceSetup();
    constexpr double androidMultitrackCallbackSeconds = 0.040;
    const int minimumFrames = static_cast<int>(std::ceil(
        setup.sampleRate * androidMultitrackCallbackSeconds));
    int selected = 0;
    for (const int size : device->getAvailableBufferSizes())
      if (size >= minimumFrames && (selected == 0 || size < selected))
        selected = size;
    if (selected > 0 && selected != setup.bufferSize) {
      setup.bufferSize = selected;
      const auto setupError = audioDeviceManager.setAudioDeviceSetup(setup, true);
      juce::Logger::writeToLog("Android playback buffer: " +
          juce::String(setup.bufferSize) + " frames; " +
          (setupError.isEmpty() ? juce::String("configured") : setupError));
    }
  }
#endif

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
