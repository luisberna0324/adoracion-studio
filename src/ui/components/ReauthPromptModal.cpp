#include "ReauthPromptModal.h"
#include "../../data/Localization.h"

namespace {
#ifndef MTCONTROLS_REAUTH_GRACE_MINUTES
#define MTCONTROLS_REAUTH_GRACE_MINUTES 300
#endif
} // namespace

ReauthPromptModal::ReauthPromptModal() {
  title.setText(L10n::tr(u8"Renovar sesión", "Renew session"),
                juce::dontSendNotification);
  title.setJustificationType(juce::Justification::centred);
  title.setFont(juce::FontOptions(20.0f).withStyle("Bold"));
  addAndMakeVisible(title);

  message.setText(
      L10n::tr(u8"Para continuar con seguridad, necesitamos que vuelvas a iniciar sesión.",
               "To continue securely, please sign in again."),
      juce::dontSendNotification);
  message.setJustificationType(juce::Justification::centred);
  message.setFont(juce::FontOptions(13.0f));
  addAndMakeVisible(message);

  const int graceMinutes =
      juce::jmax(1, static_cast<int>(MTCONTROLS_REAUTH_GRACE_MINUTES));
  juce::String graceText;
  if (graceMinutes % 60 == 0) {
    const int hours = graceMinutes / 60;
    graceText = L10n::tr("Tienes hasta ", "You have up to ") +
                juce::String(hours) +
                (hours == 1 ? L10n::tr(" hora", " hour")
                            : L10n::tr(" horas", " hours")) +
                L10n::tr(" de gracia para completar el login.",
                         " grace period to complete sign in.");
  } else {
    graceText = L10n::tr("Tienes ", "You have ") + juce::String(graceMinutes) +
                L10n::tr(" minutos de gracia para completar el login.",
                         " minutes of grace period to complete sign in.");
  }
  graceInfo.setText(graceText, juce::dontSendNotification);
  graceInfo.setJustificationType(juce::Justification::centred);
  graceInfo.setFont(juce::FontOptions(12.0f));
  addAndMakeVisible(graceInfo);

  status.setText(L10n::tr("Pulsa Ingresar ahora para abrir el navegador.",
                          "Press Sign in now to open the browser."),
                 juce::dontSendNotification);
  status.setJustificationType(juce::Justification::centred);
  status.setFont(juce::FontOptions(12.0f));
  addAndMakeVisible(status);

  loginNowButton.setButtonText(L10n::tr("Ingresar ahora", "Sign in now"));
  loginNowButton.onClick = [this] {
    if (onLoginNow)
      onLoginNow();
  };
  addAndMakeVisible(loginNowButton);

  laterButton.setButtonText(L10n::tr(u8"Más tarde", "Later"));
  laterButton.onClick = [this] {
    if (onLater)
      onLater();
  };
  addAndMakeVisible(laterButton);

}

void ReauthPromptModal::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds().toFloat();
  g.setColour(juce::Colour::fromRGBA(0, 0, 0, 70));
  g.fillRoundedRectangle(bounds, 10.0f);
  g.setColour(juce::Colour::fromRGB(65, 76, 98));
  g.drawRoundedRectangle(bounds, 10.0f, 1.0f);
}

void ReauthPromptModal::resized() {
  auto area = getLocalBounds().reduced(14);
  title.setBounds(area.removeFromTop(34));
  area.removeFromTop(4);
  message.setBounds(area.removeFromTop(52));
  graceInfo.setBounds(area.removeFromTop(28));
  status.setBounds(area.removeFromTop(26));
  area.removeFromTop(12);

  auto controlsRow = area.removeFromTop(42).withSizeKeepingCentre(340, 36);
  auto loginArea = controlsRow.removeFromLeft(185);
  controlsRow.removeFromLeft(10);
  auto laterArea = controlsRow;
  loginNowButton.setBounds(loginArea);
  laterButton.setBounds(laterArea);
}

void ReauthPromptModal::setStatusText(const juce::String &text, bool isError) {
  status.setText(text, juce::dontSendNotification);
  status.setColour(juce::Label::textColourId,
                   isError ? juce::Colour::fromRGB(255, 164, 164)
                           : juce::Colour::fromRGB(205, 214, 227));
}

void ReauthPromptModal::showWaitingForCallback() {
  setStatusText(L10n::tr(u8"Esperando autenticación en el navegador...",
                         "Waiting for browser authentication..."),
                false);
  loginNowButton.setEnabled(false);
  laterButton.setEnabled(false);
}

void ReauthPromptModal::showReadyState() {
  setStatusText(L10n::tr("Pulsa Ingresar ahora para abrir el navegador.",
                         "Press Sign in now to open the browser."),
                false);
  loginNowButton.setEnabled(true);
  laterButton.setEnabled(true);
}

void ReauthPromptModal::showError(const juce::String &text) {
  setStatusText(text, true);
  loginNowButton.setEnabled(true);
  laterButton.setEnabled(true);
}
