#include "PluginEditor.h"
#include "PluginEditorSupport.h"
#include "data/EmailPasswordAuthApi.h"
#include "data/Localization.h"
#include "data/LocalAuthCallbackServer.h"
#include "platform/NativeAuthSession.h"
#include "platform/NativeAppleSignIn.h"
#include "ui/components/WelcomeModal.h"
#include "ui/components/ReauthPromptModal.h"
#include <thread>

using namespace PluginEditorSupport;

namespace {
juce::String emailErrorMessage(EmailPasswordAuthApi::Error error) {
  using Error = EmailPasswordAuthApi::Error;
  switch (error) {
    case Error::invalidCredentials:
      return L10n::tr(u8"El correo o la contraseña no son correctos. Puedes establecer o recuperar tu contraseña.",
                      "The email or password is incorrect. You can set or reset your password.");
    case Error::invalidRequest:
      return L10n::tr(u8"Revisa el correo y los campos e inténtalo nuevamente.",
                      "Check your email and the fields, then try again.");
    case Error::rateLimited:
      return L10n::tr(u8"Espera un momento antes de volver a intentarlo.",
                      "Wait a moment before trying again.");
    case Error::network:
      return L10n::tr(u8"No pudimos conectar. Comprueba tu conexión e inténtalo nuevamente.",
                      "Couldn't connect. Check your connection and try again.");
    case Error::none:
    case Error::unavailable:
    case Error::invalidResponse:
      return L10n::tr(u8"No pudimos completar la solicitud. Inténtalo nuevamente más tarde.",
                      "Couldn't complete the request. Please try again later.");
  }
}
}

void MTControlsAudioProcessorEditor::setEmailAuthenticationStatus(
    const juce::String& text, bool error, bool busy) {
  emailAuthenticationBusy = busy;
  reauthLoginInProgress = busy;
  if (auto* modal = dynamic_cast<WelcomeModal*>(welcomeModal.get())) {
    modal->setChecking(false);
    modal->setAuthenticationBusy(busy);
    modal->setStatusText(busy ? juce::String() : text, error);
  }
  if (auto* modal = dynamic_cast<ReauthPromptModal*>(reauthPromptModal.get())) {
    modal->setAuthenticationBusy(busy);
    modal->setStatusText(busy ? juce::String() : text, error);
  }
}

void MTControlsAudioProcessorEditor::cancelAuthenticationAttempt(bool preserveAppleLinkFlow) {
  if (!preserveAppleLinkFlow) resetAppleLinkFlow();
  ++authAttemptGeneration;
  authExpectedState.clear();
  NativeAuthSession::cancel();
  NativeAppleSignIn::cancel();
  if (localAuthServer) localAuthServer->stop();
  {
    const juce::ScopedLock lock(authCallbackLock);
    pendingAuthCallbackUrl.clear();
  }
  emailAuthenticationBusy = false;
  appleLoginInProgress = false;
  appleLinkInProgress = false;
  reauthLoginInProgress = false;
  if (auto* modal = dynamic_cast<WelcomeModal*>(welcomeModal.get()))
    modal->setAuthenticationBusy(false);
  if (auto* modal = dynamic_cast<ReauthPromptModal*>(reauthPromptModal.get()))
    modal->setAuthenticationBusy(false);
}

void MTControlsAudioProcessorEditor::launchPasswordLogin(
    const juce::String& email, const juce::String& password) {
  if (emailAuthenticationBusy) return;
  cancelAuthenticationAttempt(appleLinkFlow.stage == AppleLinkFlow::Stage::authenticate);
  sessionResumeCancelled = false;
  const auto generation = authAttemptGeneration;
  const auto baseUrl = getAuthBaseUrl();
  const auto deviceId = buildStableDeviceId();
  setEmailAuthenticationStatus(L10n::tr(u8"Iniciando sesión…", "Signing in…"), false, true);
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  std::thread([safe, generation, baseUrl, deviceId, email, password] {
    const auto result = EmailPasswordAuthApi::signIn(baseUrl, email, password, deviceId);
    juce::MessageManager::callAsync([safe, generation, result] {
      if (!safe || generation != safe->authAttemptGeneration) return;
      if (!result.ok()) {
        safe->setEmailAuthenticationStatus(emailErrorMessage(result.error), true, false);
        return;
      }
      safe->setEmailAuthenticationStatus(L10n::tr(u8"Sesión iniciada.", "Signed in."), false, false);
      safe->completeAuthenticatedSession(result.session);
    });
  }).detach();
}

void MTControlsAudioProcessorEditor::requestPasswordLink(const juce::String& email) {
  if (emailAuthenticationBusy) return;
  cancelAuthenticationAttempt();
  const auto generation = authAttemptGeneration;
  const auto baseUrl = getAuthBaseUrl();
  setEmailAuthenticationStatus(L10n::tr(u8"Solicitando enlace…", "Requesting link…"), false, true);
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  std::thread([safe, generation, baseUrl, email] {
    const auto result = EmailPasswordAuthApi::requestPasswordLink(baseUrl, email);
    juce::MessageManager::callAsync([safe, generation, result] {
      if (!safe || generation != safe->authAttemptGeneration) return;
      if (!result.ok()) {
        safe->setEmailAuthenticationStatus(emailErrorMessage(result.error), true, false);
        return;
      }
      safe->setEmailAuthenticationStatus({}, false, false);
      if (auto* modal = dynamic_cast<WelcomeModal*>(safe->welcomeModal.get()))
        modal->showPasswordLinkSent();
      if (auto* modal = dynamic_cast<ReauthPromptModal*>(safe->reauthPromptModal.get()))
        modal->showPasswordLinkSent();
    });
  }).detach();
}
