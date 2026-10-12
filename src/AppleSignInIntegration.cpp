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
      return L10n::tr("Este acceso Apple no está disponible para esta cuenta. Ingresa con tu método habitual.",
                      "This Apple sign-in is unavailable for this account. Use your usual sign-in method.");
    case Error::identityConflict:
    case Error::identityMismatch:
      return L10n::tr("Esta identidad Apple pertenece a otra cuenta. No se cambió tu cuenta actual.",
                      "This Apple identity belongs to another account. Your current account was not changed.");
    case Error::reauthenticationRequired:
    case Error::unauthorized:
      return L10n::tr("Renueva tu sesión con tu método habitual.",
                      "Renew your session using your usual sign-in method.");
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
  if (localDemoSession) return;
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
      safe->resized();
    });
  }).detach();
#endif
}

void MTControlsAudioProcessorEditor::setAppleAuthenticationStatus(
    const juce::String& text, bool error, bool busy) {
  if (deletionReauthStatus) deletionReauthStatus(text, error, busy);
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
  appleLoginInProgress = busy;
  if (!appleLinkInProgress) reauthLoginInProgress = busy;
}

void MTControlsAudioProcessorEditor::launchAppleLogin(bool linkExisting) {
#if MTCONTROLS_APPLE_SIGN_IN_ENABLED && (JUCE_IOS || JUCE_MAC)
  if (!appleSignInAvailable || appleLoginInProgress) return;
  const bool inlineLink = linkExisting && appleLinkFlow.stage == AppleLinkFlow::Stage::linking;
  if (!linkExisting) resetAppleLinkFlow();
  auto snapshot = activeSession;
  if (inlineLink) {
    snapshot.uid = appleLinkCandidate.uid;
    snapshot.refreshToken = appleLinkCandidate.refreshToken;
    snapshot.firebaseApiKey = appleLinkCandidate.firebaseApiKey;
  }
  if (inlineLink && !appleLinkFlow.valid(juce::Time::currentTimeMillis())) {
    cancelAuthenticationAttempt();
    setAppleAuthenticationStatus(appleErrorMessage(AppleSignInApi::Error::expiredChallenge), true, false);
    return;
  }
  if (linkExisting && ((!inlineLink && !mainUiInitialized) || snapshot.uid.isEmpty() ||
      snapshot.refreshToken.isEmpty() || snapshot.firebaseApiKey.isEmpty())) return;
  const auto generation = ++authAttemptGeneration;
  emailAuthenticationBusy = false;
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
  context.expectedUid = linkExisting ? snapshot.uid : juce::String{};
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  std::thread([safe, generation, context, snapshot, linkExisting]() mutable {
    if (linkExisting) {
      const auto refresh = AuthSessionApi::refreshFirebaseIdToken(snapshot.firebaseApiKey,
                                                                 snapshot.refreshToken);
      if (!refresh.ok) {
        juce::MessageManager::callAsync([safe, generation] {
          if (safe && generation == safe->authAttemptGeneration) {
            if (safe->appleLinkFlow.stage == AppleLinkFlow::Stage::linking) safe->cancelAuthenticationAttempt();
            safe->setAppleAuthenticationStatus(
                appleErrorMessage(AppleSignInApi::Error::reauthenticationRequired), true, false);
          }
        });
        return;
      }
      context.firebaseIdToken = refresh.idToken;
    }
    const auto challenge = AppleSignInApi::requestChallenge(context);
    juce::MessageManager::callAsync([safe, generation, context, challenge, linkExisting] {
      if (!safe || generation != safe->authAttemptGeneration ||
          (linkExisting && !safe->acceptAppleLinkCallback(context.expectedUid))) return;
      if (!challenge.ok()) {
        if (safe->appleLinkFlow.stage == AppleLinkFlow::Stage::linking) safe->cancelAuthenticationAttempt();
        safe->setAppleAuthenticationStatus(appleErrorMessage(challenge.error), true, false);
        if (challenge.error == AppleSignInApi::Error::reauthenticationRequired ||
            challenge.error == AppleSignInApi::Error::unauthorized) {
          safe->showReauthPromptModal();
          if (auto* modal = dynamic_cast<ReauthPromptModal*>(safe->reauthPromptModal.get()))
            modal->showError(appleErrorMessage(challenge.error));
        }
        return;
      }
      auto& anchor = linkExisting && safe->appleLinkFlow.stage != AppleLinkFlow::Stage::linking && safe->settingsPanelRaw
          ? static_cast<juce::Component&>(*safe->settingsPanelRaw) : static_cast<juce::Component&>(*safe);
      const auto started = NativeAppleSignIn::start(challenge.challenge.nativeRequest(!linkExisting), anchor,
          [safe, generation, context, challenge, linkExisting](NativeAppleSignIn::Result result) {
            if (!safe || generation != safe->authAttemptGeneration ||
                (linkExisting && !safe->acceptAppleLinkCallback(context.expectedUid))) return;
            if (result.outcome == NativeAppleSignIn::Outcome::cancelled) {
              if (safe->appleLinkFlow.stage == AppleLinkFlow::Stage::linking) safe->cancelAuthenticationAttempt();
              safe->setAppleAuthenticationStatus(L10n::tr("Inicio de sesión cancelado.",
                                                         "Sign-in cancelled."), false, false);
              return;
            }
            if (result.outcome != NativeAppleSignIn::Outcome::succeeded) {
              if (safe->appleLinkFlow.stage == AppleLinkFlow::Stage::linking) safe->cancelAuthenticationAttempt();
              safe->setAppleAuthenticationStatus(appleErrorMessage(AppleSignInApi::Error::invalidCredential),
                                                true, false);
              return;
            }
            std::thread([safe, generation, context, challenge, linkExisting, credential = std::move(result.credential)] {
              const auto exchanged = AppleSignInApi::exchangeCredential(context, challenge.challenge, credential);
              juce::MessageManager::callAsync([safe, generation, context, exchanged, linkExisting] {
                if (!safe || generation != safe->authAttemptGeneration ||
                    (linkExisting && !safe->acceptAppleLinkCallback(context.expectedUid))) return;
                if (!exchanged.ok()) {
                  safe->setAppleAuthenticationStatus(appleErrorMessage(exchanged.error), true, false);
                  if (safe->appleLinkFlow.stage == AppleLinkFlow::Stage::linking)
                    safe->cancelAuthenticationAttempt();
                  return;
                }
                safe->setAppleAuthenticationStatus(
                    linkExisting ? L10n::tr("Apple vinculado. Puedes ingresar a esta cuenta con Apple.",
                                           "Apple linked. You can sign in to this account with Apple.")
                                 : L10n::tr("Sesión iniciada.", "Signed in."), false, false);
                safe->appleLinkInProgress = false;
                safe->resetAppleLinkFlow();
                safe->completeAuthenticatedSession(exchanged.session);
              });
            }).detach();
          });
      if (!started) {
        if (safe->appleLinkFlow.stage == AppleLinkFlow::Stage::linking) safe->cancelAuthenticationAttempt();
        safe->setAppleAuthenticationStatus(appleErrorMessage(AppleSignInApi::Error::unavailable), true, false);
      }
    });
  }).detach();
#else
  juce::ignoreUnused(linkExisting);
#endif
}

void MTControlsAudioProcessorEditor::refreshAppleCredentialMonitoring() {
  ++appleCredentialGeneration;
  appleCredentialMonitor.reset();
#if MTCONTROLS_APPLE_SIGN_IN_ENABLED && (JUCE_IOS || JUCE_MAC)
  if (localDemoSession || activeSession.authProvider != "apple.com" ||
      activeSession.uid.isEmpty() || activeSession.authProviderUser.isEmpty()) return;
  const auto generation = appleCredentialGeneration;
  const auto uid = activeSession.uid, user = activeSession.authProviderUser;
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  appleCredentialMonitor = NativeAppleSignIn::monitorCredential(user,
      [safe, generation, uid, user](NativeAppleSignIn::CredentialState state) {
        if (safe) safe->applyAppleCredentialState(generation, uid, user, state);
      });
#endif
}

void MTControlsAudioProcessorEditor::applyAppleCredentialState(
    uint64_t generation, const juce::String& uid, const juce::String& appleUser,
    NativeAppleSignIn::CredentialState state) {
  if (generation != appleCredentialGeneration || localDemoSession ||
      activeSession.uid != uid || activeSession.authProvider != "apple.com" ||
      activeSession.authProviderUser != appleUser ||
      !NativeAppleSignIn::requiresFreshLogin(state)) return;
  // Reuse logout to invalidate every pending auth/network callback and clear
  // only this device's session. Do not delete or unlink the Firebase account.
  handleLogout();
  if (auto* modal = dynamic_cast<WelcomeModal*>(welcomeModal.get()))
    modal->setStatusText(L10n::tr("El acceso con Apple cambió. Vuelve a ingresar con Apple, Google o correo.",
                                "Apple access changed. Sign in again with Apple, Google or email."), true);
}

void MTControlsAudioProcessorEditor::updateInlineAppleLinkUi(bool active) {
  if (auto* modal = dynamic_cast<WelcomeModal*>(welcomeModal.get())) modal->setInlineAppleLink(active);
  if (auto* modal = dynamic_cast<ReauthPromptModal*>(reauthPromptModal.get())) modal->setInlineAppleLink(active);
  resized();
}
void MTControlsAudioProcessorEditor::resetAppleLinkFlow() {
  appleLinkFlow.cancel();
  appleLinkCandidate = {};
  appleLinkDialog.close();
  updateInlineAppleLinkUi(false);
}
bool MTControlsAudioProcessorEditor::captureInlineAppleAccount(const PersistedSessionSnapshot& snapshot) {
  if (appleLinkFlow.stage != AppleLinkFlow::Stage::authenticate) return false;
  if (snapshot.uid.isEmpty() || !snapshot.hasCredentials() ||
      !appleLinkFlow.authenticated(snapshot.uid.toStdString(), juce::Time::currentTimeMillis())) {
    cancelAuthenticationAttempt();
    setAppleAuthenticationStatus(L10n::tr("El intento venció o la sesión no es válida. Vuelve a iniciar el acceso con Apple.",
                                        "The attempt expired or the session is invalid. Start Apple sign-in again."), true, false);
    return true;
  }
  ++authAttemptGeneration;
  appleLinkCandidate = snapshot;
  const auto token = appleLinkFlow.generation;
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  setAppleAuthenticationStatus({}, false, true);
  const auto identity = snapshot.email.isNotEmpty() ? snapshot.email : snapshot.uid;
  appleLinkDialog = juce::AlertWindow::showScopedAsync(
      juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::QuestionIcon,
        L10n::tr("Confirma tu cuenta", "Confirm your account"),
        L10n::tr("Cuenta autenticada: ", "Authenticated account: ") + identity +
        L10n::tr("\nProveedor a añadir: Apple.\nSe conservarán la cuenta, biblioteca y permisos. A continuación autorizarás Apple de nuevo; confirma también la cuenta Apple en esa pantalla.",
                  "\nProvider to add: Apple.\nYour account, library and access will be preserved. You will authorize Apple again next; check the Apple account on that screen too."),
        L10n::tr("Vincular Apple", "Link Apple"), L10n::tr("Cancelar", "Cancel"), this),
      [safe, token](int result) {
        if (!safe || token != safe->appleLinkFlow.generation) return;
        if (result != 1 || !safe->appleLinkFlow.confirm(safe->appleLinkCandidate.uid.toStdString(), juce::Time::currentTimeMillis())) {
          safe->cancelAuthenticationAttempt(); return;
        }
        safe->setAppleAuthenticationStatus({}, false, false);
        safe->launchAppleLogin(true);
      });
  return true;
}

bool MTControlsAudioProcessorEditor::acceptAppleLinkCallback(const juce::String& expectedUid) {
  if (appleLinkFlow.stage != AppleLinkFlow::Stage::linking) return expectedUid == activeSession.uid;
  if (!appleLinkFlow.valid(juce::Time::currentTimeMillis())) {
    cancelAuthenticationAttempt();
    setAppleAuthenticationStatus(appleErrorMessage(AppleSignInApi::Error::expiredChallenge), true, false);
    return false;
  }
  return expectedUid == appleLinkCandidate.uid;
}
