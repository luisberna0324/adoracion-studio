#include "PluginEditor.h"
#include "PluginEditorSupport.h"
#include "data/AppleSignInApi.h"
#include "data/AuthSessionApi.h"
#include "data/Localization.h"
#include "data/LocalAuthCallbackServer.h"
#include "platform/NativeAuthSession.h"
#include "ui/components/WelcomeModal.h"
#include "ui/components/ReauthPromptModal.h"
#include "ui/panels/SettingsPanel.h"
#include <thread>

using namespace PluginEditorSupport;

namespace {
juce::String appleErrorMessage(AppleSignInApi::Error error) {
  using Error = AppleSignInApi::Error;
  switch (error) {
    case Error::linkRequired:
      return L10n::tr("Ingresa con Google a tu cuenta y vincula Apple desde Configuración → Mi cuenta.",
                      "Sign in to your account with Google and link Apple in Settings → My account.");
    case Error::identityConflict:
    case Error::identityMismatch:
      return L10n::tr("Esta identidad Apple pertenece a otra cuenta. No se cambió tu cuenta actual.",
                      "This Apple identity belongs to another account. Your current account was not changed.");
    case Error::reauthenticationRequired:
    case Error::unauthorized:
      return L10n::tr("Renueva tu sesión con Google y luego vuelve a vincular Apple.",
                      "Renew your session with Google, then link Apple again.");
    case Error::network:
      return L10n::tr("No pudimos conectar. Comprueba tu conexión y reintenta.",
                      "Couldn't connect. Check your connection and try again.");
    case Error::unavailable:
      return L10n::tr("El acceso con Apple no está disponible en este momento.",
                      "Apple sign-in is currently unavailable.");
    case Error::rateLimited:
      return L10n::tr("Espera un momento antes de volver a intentarlo.",
                      "Wait a moment before trying again.");
    case Error::expiredChallenge:
    case Error::challengeUsed:
      return L10n::tr("El intento venció. Vuelve a iniciar sesión con Apple.",
                      "This attempt expired. Start Apple sign-in again.");
    default:
      return L10n::tr("No se completó el acceso con Apple. Reintenta.",
                      "Apple sign-in wasn't completed. Try again.");
  }
}
}

void MTControlsAudioProcessorEditor::refreshAppleSignInConfiguration() {
#if MTCONTROLS_APPLE_SIGN_IN_ENABLED && (JUCE_IOS || JUCE_MAC)
  if (!NativeAppleSignIn::isSupported()) return;
  const auto generation = ++appleConfigurationGeneration;
  const auto baseUrl = getAuthBaseUrl();
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  std::thread([safe, generation, baseUrl] {
    const auto config = AppleSignInApi::requestConfiguration(baseUrl, 10000);
    juce::MessageManager::callAsync([safe, generation, config] {
      if (!safe || generation != safe->appleConfigurationGeneration) return;
      safe->appleSignInAvailable = config.ok() && config.enabled &&
          config.nativeClientId == "com.multitracks.playworship";
      if (auto* modal = dynamic_cast<WelcomeModal*>(safe->welcomeModal.get()))
        modal->setAppleSignInAvailable(safe->appleSignInAvailable);
      if (auto* modal = dynamic_cast<ReauthPromptModal*>(safe->reauthPromptModal.get()))
        modal->setAppleSignInAvailable(safe->appleSignInAvailable);
      if (safe->settingsPanelRaw)
        safe->settingsPanelRaw->setAppleSignInAvailable(safe->appleSignInAvailable);
      safe->resized();
    });
  }).detach();
#endif
}

void MTControlsAudioProcessorEditor::setAppleAuthenticationStatus(
    const juce::String& text, bool error, bool busy) {
  if (auto* modal = dynamic_cast<WelcomeModal*>(welcomeModal.get())) {
    modal->setChecking(false);
    modal->setStatusText(text, error);
    modal->setAuthenticationBusy(busy);
  }
  if (auto* modal = dynamic_cast<ReauthPromptModal*>(reauthPromptModal.get())) {
    if (busy) modal->showWaitingForCallback();
    else if (error) modal->showError(text);
    else modal->showReadyState();
  }
  if (appleLinkInProgress && settingsPanelRaw)
    settingsPanelRaw->setAppleSignInStatus(text, busy);
  appleLoginInProgress = busy;
  if (!appleLinkInProgress) reauthLoginInProgress = busy;
}

void MTControlsAudioProcessorEditor::launchAppleLogin(bool linkExisting) {
#if MTCONTROLS_APPLE_SIGN_IN_ENABLED && (JUCE_IOS || JUCE_MAC)
  if (!appleSignInAvailable || appleLoginInProgress) return;
  if (linkExisting && (!mainUiInitialized || activeSession.uid.isEmpty() ||
      activeSession.refreshToken.isEmpty() || activeSession.firebaseApiKey.isEmpty())) return;
  const auto generation = ++appleAuthGeneration;
  NativeAuthSession::cancel();
  NativeAppleSignIn::cancel();
  authExpectedState.clear();
  if (localAuthServer) localAuthServer->stop();
  sessionResumeCancelled = false;
  appleLinkInProgress = linkExisting;
  setAppleAuthenticationStatus(L10n::tr("Preparando el acceso con Apple…",
                                       "Preparing Apple sign-in…"), false, true);
  AppleSignInApi::Context context;
  context.apiBaseUrl = getAuthBaseUrl();
  context.deviceId = buildStableDeviceId();
#if JUCE_MAC
  context.platform = AppleSignInApi::Platform::macos;
#else
  context.platform = AppleSignInApi::Platform::ios;
#endif
  context.purpose = linkExisting ? AppleSignInApi::Purpose::link : AppleSignInApi::Purpose::signIn;
  context.expectedUid = linkExisting ? activeSession.uid : juce::String{};
  const auto snapshot = activeSession;
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  std::thread([safe, generation, context, snapshot, linkExisting]() mutable {
    if (linkExisting) {
      const auto refresh = AuthSessionApi::refreshFirebaseIdToken(snapshot.firebaseApiKey,
                                                                 snapshot.refreshToken);
      if (!refresh.ok) {
        juce::MessageManager::callAsync([safe, generation] {
          if (safe && generation == safe->appleAuthGeneration)
            safe->setAppleAuthenticationStatus(
                appleErrorMessage(AppleSignInApi::Error::reauthenticationRequired), true, false);
        });
        return;
      }
      context.firebaseIdToken = refresh.idToken;
    }
    const auto challenge = AppleSignInApi::requestChallenge(context);
    juce::MessageManager::callAsync([safe, generation, context, challenge, linkExisting] {
      if (!safe || generation != safe->appleAuthGeneration ||
          (linkExisting && context.expectedUid != safe->activeSession.uid)) return;
      if (!challenge.ok()) {
        safe->setAppleAuthenticationStatus(appleErrorMessage(challenge.error), true, false);
        if (challenge.error == AppleSignInApi::Error::reauthenticationRequired ||
            challenge.error == AppleSignInApi::Error::unauthorized) {
          safe->showReauthPromptModal();
          if (auto* modal = dynamic_cast<ReauthPromptModal*>(safe->reauthPromptModal.get()))
            modal->showError(appleErrorMessage(challenge.error));
        }
        return;
      }
      auto& anchor = linkExisting && safe->settingsPanelRaw
          ? static_cast<juce::Component&>(*safe->settingsPanelRaw) : static_cast<juce::Component&>(*safe);
      const auto started = NativeAppleSignIn::start(challenge.challenge.nativeRequest(!linkExisting), anchor,
          [safe, generation, context, challenge, linkExisting](NativeAppleSignIn::Result result) {
            if (!safe || generation != safe->appleAuthGeneration ||
                (linkExisting && context.expectedUid != safe->activeSession.uid)) return;
            if (result.outcome == NativeAppleSignIn::Outcome::cancelled) {
              safe->setAppleAuthenticationStatus(L10n::tr("Inicio de sesión cancelado.",
                                                         "Sign-in cancelled."), false, false);
              return;
            }
            if (result.outcome != NativeAppleSignIn::Outcome::succeeded) {
              safe->setAppleAuthenticationStatus(appleErrorMessage(AppleSignInApi::Error::invalidCredential),
                                                true, false);
              return;
            }
            std::thread([safe, generation, context, challenge, linkExisting, credential = std::move(result.credential)] {
              const auto exchanged = AppleSignInApi::exchangeCredential(context, challenge.challenge, credential);
              juce::MessageManager::callAsync([safe, generation, context, exchanged, linkExisting] {
                if (!safe || generation != safe->appleAuthGeneration ||
                    (linkExisting && context.expectedUid != safe->activeSession.uid)) return;
                if (!exchanged.ok()) {
                  safe->setAppleAuthenticationStatus(appleErrorMessage(exchanged.error), true, false);
                  return;
                }
                safe->setAppleAuthenticationStatus(
                    linkExisting ? L10n::tr("Apple vinculado. Puedes ingresar a esta cuenta con Apple.",
                                           "Apple linked. You can sign in to this account with Apple.")
                                 : L10n::tr("Sesión iniciada.", "Signed in."), false, false);
                safe->appleLinkInProgress = false;
                safe->completeAuthenticatedSession(exchanged.session);
              });
            }).detach();
          });
      if (!started)
        safe->setAppleAuthenticationStatus(appleErrorMessage(AppleSignInApi::Error::unavailable), true, false);
    });
  }).detach();
#else
  juce::ignoreUnused(linkExisting);
#endif
}
