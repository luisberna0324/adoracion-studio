#pragma once

#include "../audio/AudioDeviceMonitor.h"
#include "UiCommon.h"
#include <JuceHeader.h>

#include <functional>

/** Franja sutil a ancho completo sobre el waveform: nueva interfaz, botón Configurar. */
class DeviceNotificationBanner : public juce::Component, private juce::Timer {
public:
  DeviceNotificationBanner();

  void showForDevice(const AudioDeviceMonitor::DeviceInfo &info);
  /** Cierre por timeout; invoca onDismiss. */
  void dismiss();

  std::function<void()> onConfirm;
  std::function<void()> onDismiss;

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  void timerCallback() override;

  juce::Label messageLabel;
  juce::TextButton configureButton{"Configurar"};
  static constexpr int kAutoDismissMs = 10000;
};
