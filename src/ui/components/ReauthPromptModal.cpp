#include "ReauthPromptModal.h"
#include "../../data/Localization.h"
#include "../../platform/NativeAppleSignIn.h"

namespace {
#ifndef MTCONTROLS_REAUTH_GRACE_MINUTES
#define MTCONTROLS_REAUTH_GRACE_MINUTES 300
#endif
} // namespace

ReauthPromptModal::ReauthPromptModal() {
  AuthUi::label(title, 22.0f, true);
  title.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(title);

  AuthUi::label(message, 14.0f);
  message.setJustificationType(juce::Justification::centred);
  message.setColour(juce::Label::textColourId, AuthUi::muted);
  addAndMakeVisible(message);

  const int graceMinutes =
      juce::jmax(1, static_cast<int>(MTCONTROLS_REAUTH_GRACE_MINUTES));
  const auto graceText = graceMinutes % 60 == 0
      ? L10n::tr("Hasta ", "Up to ") + juce::String(graceMinutes / 60) +
            L10n::tr(" h para renovar tu sesión.", " h to renew your session.")
      : L10n::tr("Hasta ", "Up to ") + juce::String(graceMinutes) +
            L10n::tr(" min para renovar tu sesión.", " min to renew your session.");
  graceInfo.setText(graceText, juce::dontSendNotification);
  AuthUi::label(graceInfo, 12.0f);
  graceInfo.setColour(juce::Label::textColourId, AuthUi::muted);
  graceInfo.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(graceInfo);

  AuthUi::label(status, 13.0f);
  status.setJustificationType(juce::Justification::centred);
  addChildComponent(status);

  separator.setText(L10n::tr("o", "or"), juce::dontSendNotification);
  AuthUi::label(separator, 13.0f);
  separator.setColour(juce::Label::textColourId, AuthUi::muted);
  separator.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(separator);

  backButton.setButtonText(L10n::tr("Volver", "Back"));
  backButton.setTooltip(L10n::tr("Volver", "Back"));
  backButton.onClick = [this] {
    if (inlineAppleLink) showEmailForm(false);
    else emailForm.goBack();
  };
  addChildComponent(backButton);

  loginNowButton.setButtonText(L10n::tr("Continuar con Google", "Continue with Google"));
  loginNowButton.onClick = [this] {
    if (onLoginNow) onLoginNow();
  };
  addAndMakeVisible(loginNowButton);

  laterButton.setButtonText(L10n::tr(u8"Más tarde", "Later"));
  laterButton.onClick = [this] {
    if (onAuthenticationCancelled) onAuthenticationCancelled();
    clearCredentials();
    if (onLater) onLater();
  };
  addAndMakeVisible(laterButton);

  emailButton.setButtonText(L10n::tr("Ingresar con correo", "Sign in with email"));
  emailButton.onClick = [this] { showEmailForm(true); };
  addAndMakeVisible(emailButton);

  emailForm.onPasswordLoginRequested = [this](const auto &email, const auto &password) {
    if (onPasswordLoginRequested) onPasswordLoginRequested(email, password);
  };
  emailForm.onPasswordResetRequested = [this](const auto &email) {
    if (onPasswordResetRequested) onPasswordResetRequested(email);
  };
  emailForm.onStatusChanged = [this](const auto &text, bool error) {
    setStatusText(text, error);
  };
  emailForm.onBack = [this] { showEmailForm(false); };
  emailForm.onModeChanged = [this] {
    setStatusText({}, false);
    updateHeader();
    resized();
    if (onPreferredHeightChanged) onPreferredHeightChanged();
  };
  addChildComponent(emailForm);
  updateHeader();
}

int ReauthPromptModal::preferredHeight() const {
  const int sidePadding = getWidth() < 360 ? 16 : 20;
  const int formWidth = getWidth() > 0 ? juce::jmin(380, getWidth() - sidePadding * 2) : 320;
  const int base = emailMode
      ? 48 + 44 + 20 + emailForm.preferredHeight(formWidth)
      : (appleButton ? 458 : 400);
  return base + (status.getText().isNotEmpty() ? 64 : 0);
}

void ReauthPromptModal::paint(juce::Graphics &g) {
  AuthUi::drawSurface(g, getLocalBounds().toFloat());
  if (separator.isVisible()) {
    const auto row = separator.getBounds().toFloat();
    g.setColour(AuthUi::border);
    const float y = row.getCentreY();
    g.drawLine(row.getX(), y, row.getCentreX() - 22.0f, y);
    g.drawLine(row.getCentreX() + 22.0f, y, row.getRight(), y);
  }
}

void ReauthPromptModal::resized() {
  repaint();
  auto area = getLocalBounds().reduced(getWidth() < 360 ? 16 : 20, emailMode ? 24 : 20);
  const int controlsWidth = juce::jmin(380, area.getWidth());
  area = area.withSizeKeepingCentre(controlsWidth, area.getHeight());
  auto header = area.removeFromTop(44);
  backButton.setBounds(header.withWidth(44));
  title.setBounds(emailMode || inlineAppleLink ? header.reduced(44, 0) : header);

  message.setVisible(!emailMode);
  if (!emailMode) {
    area.removeFromTop(8);
    message.setBounds(area.removeFromTop(38));
  }
  area.removeFromTop(emailMode ? 20 : 14);

  status.setVisible(status.getText().isNotEmpty());
  if (status.isVisible()) {
    status.setBounds(area.removeFromTop(54));
    area.removeFromTop(10);
  }

  if (emailMode) {
    emailForm.setBounds(area);
    return;
  }

  loginNowButton.setBounds(area.removeFromTop(48));
  if (appleButton) {
    area.removeFromTop(10);
    appleButton->setBounds(area.removeFromTop(48));
  }
  separator.setBounds(area.removeFromTop(28));
  emailButton.setBounds(area.removeFromTop(48));
  area.removeFromTop(8);
  laterButton.setBounds(area.removeFromTop(44));
  graceInfo.setBounds(area.removeFromTop(30));
}

void ReauthPromptModal::setAppleSignInAvailable(bool available) {
  const auto previousHeight = preferredHeight();
  if (available && !appleButton) {
    appleButton = NativeAppleSignIn::createButton([this] {
      if (onAppleLoginNow) onAppleLoginNow();
    });
    if (appleButton) addAndMakeVisible(*appleButton);
  } else if (!available) {
    appleButton.reset();
  }
  updateActionVisibility();
  resized();
  if (previousHeight != preferredHeight() && onPreferredHeightChanged)
    onPreferredHeightChanged();
}

void ReauthPromptModal::setStatusText(const juce::String &text, bool isError) {
  const auto previousHeight = preferredHeight();
  status.setText(text, juce::dontSendNotification);
  status.setColour(juce::Label::textColourId,
                   isError ? juce::Colour::fromRGB(255, 164, 164) : AuthUi::muted);
  resized();
  if (previousHeight != preferredHeight() && onPreferredHeightChanged)
    onPreferredHeightChanged();
}

void ReauthPromptModal::showWaitingForCallback() {
  setAuthenticationBusy(true);
  setStatusText(L10n::tr(u8"Completa el acceso en la ventana que se abrió.",
                         "Complete sign in in the window that opened."), false);
}

void ReauthPromptModal::showReadyState() {
  setAuthenticationBusy(false);
  setStatusText({}, false);
}

void ReauthPromptModal::showError(const juce::String &text) {
  setAuthenticationBusy(false);
  setStatusText(text, true);
}

void ReauthPromptModal::showPasswordLinkSent() {
  setAuthenticationBusy(false);
  setStatusText({}, false);
  emailForm.showPasswordLinkSent();
  updateHeader();
  resized();
  if (onPreferredHeightChanged) onPreferredHeightChanged();
}

void ReauthPromptModal::setAuthenticationBusy(bool busy) {
  waiting = busy;
  loginNowButton.setEnabled(!busy);
  // Email remains available to abandon a blocked browser callback.
  emailButton.setEnabled(true);
  if (appleButton) appleButton->setEnabled(!busy);
  emailForm.setBusy(busy);
}

void ReauthPromptModal::clearCredentials() { emailForm.clearCredentials(); }

void ReauthPromptModal::visibilityChanged() {
  if (!isVisible()) { if (inlineAppleLink && onAuthenticationCancelled) onAuthenticationCancelled(); clearCredentials(); }
}

void ReauthPromptModal::showEmailForm(bool show) {
  if (show && inlineAppleLink) {
    if (onInlineAuthenticationNavigation) onInlineAuthenticationNavigation();
  } else if (onAuthenticationCancelled) onAuthenticationCancelled();
  setAuthenticationBusy(false);
  emailMode = show;
  if (show) emailForm.showLogin();
  else clearCredentials();
  showReadyState();
  updateHeader();
  updateActionVisibility();
  resized();
  if (onPreferredHeightChanged) onPreferredHeightChanged();
  if (show) emailForm.focusEmail();
}

void ReauthPromptModal::updateHeader() {
  if (emailMode && emailForm.isLinkSent()) {
    title.setText(L10n::tr("Revisa tu correo", "Check your email"), juce::dontSendNotification);
    message.setText(L10n::tr("Abre el enlace y luego vuelve para iniciar sesión.",
                            "Open the link, then return to sign in."), juce::dontSendNotification);
  } else if (emailMode && emailForm.isResetMode()) {
    title.setText(L10n::tr(u8"Tu contraseña", "Your password"), juce::dontSendNotification);
    message.setText(L10n::tr("Crea o recupera la contraseña de tu cuenta PlayWorship.",
                            "Set or reset your PlayWorship account password."), juce::dontSendNotification);
  } else {
    title.setText(L10n::tr(u8"Renovar sesión", "Renew session"), juce::dontSendNotification);
    message.setText(emailMode
        ? L10n::tr("Usa el correo y la contraseña de tu cuenta PlayWorship.",
                    "Use your PlayWorship account email and password.")
        : L10n::tr("Vuelve a iniciar sesión para mantener el acceso a tu cuenta.",
                    "Sign in again to keep access to your account."), juce::dontSendNotification);
  }
}

void ReauthPromptModal::updateActionVisibility() {
  loginNowButton.setVisible(!emailMode);
  laterButton.setVisible(!emailMode);
  emailButton.setVisible(!emailMode);
  separator.setVisible(!emailMode);
  graceInfo.setVisible(!emailMode);
  backButton.setVisible(emailMode || inlineAppleLink);
  emailForm.setVisible(emailMode);
  if (appleButton) {
    appleButton->setVisible(!emailMode && !inlineAppleLink);
    appleButton->setEnabled(!waiting);
  }
}

void ReauthPromptModal::setInlineAppleLink(bool active) {
  inlineAppleLink = active;
  clearCredentials();
  updateHeader();
  updateActionVisibility();
  resized();
}
