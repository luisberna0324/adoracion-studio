#pragma once

#include <JuceHeader.h>
#include "WelcomeModal.h"

class ReauthPromptModal final : public juce::Component {
public:
  std::function<void()> onLoginNow;
  std::function<void()> onAppleLoginNow;
  std::function<void()> onLater;
  std::function<void()> onAuthenticationCancelled;
  std::function<void()> onPreferredHeightChanged;
  std::function<void(const juce::String &, const juce::String &)>
      onPasswordLoginRequested;
  std::function<void(const juce::String &)> onPasswordResetRequested;

  ReauthPromptModal();

  void showWaitingForCallback();
  void showReadyState();
  void showError(const juce::String &text);
  void showPasswordLinkSent();
  void setAppleSignInAvailable(bool available);
  void setAuthenticationBusy(bool busy);
  void setStatusText(const juce::String &text, bool isError);
  void clearCredentials();
  int preferredHeight() const;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void visibilityChanged() override;

private:
  void showEmailForm(bool show);
  void updateActionVisibility();
  void updateHeader();

  juce::Label title;
  juce::Label message;
  juce::Label graceInfo;
  juce::Label status;
  juce::Label separator;
  HeaderIconButton backButton{"authBack", IconGlyph::ArrowLeft};
  AuthUi::Button loginNowButton{AuthUi::Role::google};
  AuthUi::Button laterButton{AuthUi::Role::link};
  AuthUi::Button emailButton{AuthUi::Role::secondary};
  EmailAuthenticationForm emailForm;
  std::unique_ptr<juce::Component> appleButton;
  bool waiting = false;
  bool emailMode = false;
};
