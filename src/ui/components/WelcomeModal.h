#pragma once

#include <JuceHeader.h>
#include "AuthenticationStyle.h"
#include "../UiCommon.h"

// Shared by welcome and session renewal; credentials live only in the form.
class EmailAuthenticationForm final : public juce::Component {
public:
  std::function<void(const juce::String &, const juce::String &)>
      onPasswordLoginRequested;
  std::function<void(const juce::String &, const juce::String &)>
      onLocalDemoLoginRequested;
  std::function<void(const juce::String &)> onPasswordResetRequested;
  std::function<void(const juce::String &, bool)> onStatusChanged;
  std::function<void()> onBack;
  std::function<void()> onModeChanged;

  EmailAuthenticationForm();
  ~EmailAuthenticationForm() override;
  void showLogin();
  void showLocalDemoLogin();
  void showPasswordLinkSent();
  void goBack();
  void setBusy(bool busy);
  void clearCredentials();
  bool isResetMode() const { return mode == Mode::reset || mode == Mode::linkSent; }
  bool isLinkSent() const { return mode == Mode::linkSent; }
  bool isLocalDemoMode() const { return mode == Mode::localDemoLogin; }
  int preferredHeight(int width = 0) const;
  void focusEmail();
  void resized() override;
  void visibilityChanged() override;

private:
  void updateMode();
  void submit();
  enum class Mode { login, reset, linkSent, localDemoLogin };
  AuthUi::FieldLookAndFeel fieldStyle;
  juce::Viewport viewport;
  juce::Component content;
  AuthUi::Paragraph explanation;
  juce::Label emailLabel, passwordLabel;
  juce::TextEditor emailEditor, passwordEditor;
  AuthUi::Button showPasswordButton{AuthUi::Role::eye};
  AuthUi::Button submitButton{AuthUi::Role::primary};
  AuthUi::Button resetButton{AuthUi::Role::link};
  AuthUi::MailIcon mailIcon;
  Mode mode = Mode::login;
  bool busy = false;
};

class WelcomeModal final : public juce::Component {
public:
  std::function<void()> onLoginRequested;
  std::function<void()> onAppleLoginRequested;
  std::function<void()> onAuthenticationCancelled;
  std::function<void(const juce::String &, const juce::String &)>
      onPasswordLoginRequested;
  std::function<void(const juce::String &, const juce::String &)>
      onLocalDemoLoginRequested;
  std::function<void(const juce::String &)> onPasswordResetRequested;

  WelcomeModal();
  ~WelcomeModal() override;

  void setStatusText(const juce::String &text, bool isError);
  void resetStatusToDefault();
  void setChecking(bool isChecking);
  void setAppleSignInAvailable(bool available);
  void setAuthenticationBusy(bool busy);
  void clearCredentials();
  void showPasswordLinkSent();
  void showLocalDemoLogin();

  void paint(juce::Graphics &) override;
  void resized() override;
  void visibilityChanged() override;

private:
  void showEmailForm(bool show);
  void updateActionVisibility();
  void updateHeading();
  juce::ImageComponent logo;
  juce::Image logoImage;
  juce::Label title;
  juce::Label subtitle;
  juce::Label status;
  juce::Label separator;
  AuthUi::Button loginButton{AuthUi::Role::google};
  AuthUi::Button emailButton{AuthUi::Role::secondary};
  AuthUi::Button localDemoButton{AuthUi::Role::secondary};
  HeaderIconButton backButton{"authBack", IconGlyph::ArrowLeft};
  EmailAuthenticationForm emailForm;
  std::unique_ptr<juce::Component> appleButton;
  bool checking = false;
  bool emailMode = false;
  bool authenticationBusy = false;
  juce::Rectangle<int> contentBounds;
};
