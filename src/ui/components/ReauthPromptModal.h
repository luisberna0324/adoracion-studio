#pragma once

#include <JuceHeader.h>

class ReauthPromptModal final : public juce::Component {
public:
  std::function<void()> onLoginNow;
  std::function<void()> onLater;

  ReauthPromptModal();

  void showWaitingForCallback();
  void showReadyState();
  void showError(const juce::String &text);

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  void setStatusText(const juce::String &text, bool isError);

  juce::Label title;
  juce::Label message;
  juce::Label graceInfo;
  juce::Label status;
  juce::TextButton loginNowButton;
  juce::TextButton laterButton;
};
