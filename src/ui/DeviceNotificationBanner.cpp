#include "DeviceNotificationBanner.h"

DeviceNotificationBanner::DeviceNotificationBanner() {
  setInterceptsMouseClicks(true, true);
  setAlwaysOnTop(true);

  messageLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  messageLabel.setFont(juce::FontOptions(12.5f));
  messageLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(messageLabel);

  configureButton.onClick = [this] {
    stopTimer();
    if (onConfirm)
      onConfirm();
    setVisible(false);
  };
  configureButton.setColour(juce::TextButton::buttonColourId,
                            UITheme::accent.withAlpha(0.92f));
  configureButton.setColour(juce::TextButton::textColourOffId, UITheme::onAccent);
  addAndMakeVisible(configureButton);

  setVisible(false);
}

void DeviceNotificationBanner::showForDevice(
    const AudioDeviceMonitor::DeviceInfo &info) {
  stopTimer();
  const juce::String msg =
      "Interfaz: " + info.name + " (" + juce::String(info.numOutputChannels) +
      " salidas)";
  messageLabel.setText(msg, juce::dontSendNotification);
  setVisible(true);
  startTimer(kAutoDismissMs);
}

void DeviceNotificationBanner::dismiss() {
  stopTimer();
  if (isVisible() && onDismiss)
    onDismiss();
  setVisible(false);
}

void DeviceNotificationBanner::timerCallback() { dismiss(); }

void DeviceNotificationBanner::paint(juce::Graphics &g) {
  g.fillAll(juce::Colours::black.withAlpha(0.55f));
  g.setColour(UITheme::accent.withAlpha(0.35f));
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
}

void DeviceNotificationBanner::resized() {
  auto r = getLocalBounds().reduced(10, 6);
  configureButton.setBounds(r.removeFromRight(110).reduced(0, 2));
  r.removeFromRight(8);
  messageLabel.setBounds(r);
}
