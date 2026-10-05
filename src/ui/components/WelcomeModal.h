#pragma once

#include <JuceHeader.h>

class WelcomeModal final : public juce::Component {
public:
  std::function<void()> onLoginRequested;
  std::function<void()> onAppleLoginRequested;

  WelcomeModal();
  ~WelcomeModal() override;

  void setStatusText(const juce::String &text, bool isError);
  void resetStatusToDefault();
  void setChecking(bool isChecking);
  void setAppleSignInAvailable(bool available);
  void setAuthenticationBusy(bool busy);

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  std::unique_ptr<juce::LookAndFeel_V4> primaryLaf;
  juce::ImageComponent logo;
  juce::Image logoImage;
  juce::Label title;
  juce::Label subtitle;
  juce::Label status;
  juce::TextButton loginButton;
  std::unique_ptr<juce::Component> appleButton;
  bool checking = false;
  juce::Rectangle<int> contentBounds;
};
