#include "PluginEditor.h"
#include "data/AppSettings.h"
#include "data/Localization.h"
#include "data/SessionAuthLogger.h"
#include "ui/components/WelcomeModal.h"

void MTControlsAudioProcessorEditor::launchLocalDemoLogin(
    const juce::String& username, const juce::String& password) {
  // Public test credentials select a local workspace. They never represent a
  // backend identity and are never passed to a network authentication method.
  if (username.trim() != "usuarioplay0324" || password != "usuarioplay0324") {
    setEmailAuthenticationStatus(L10n::tr(
        "Para la prueba local, usa usuarioplay0324 como usuario y contraseña.",
        "For the local trial, use usuarioplay0324 as both username and password."), true, false);
    return;
  }
  enterLocalDemoSession();
}

void MTControlsAudioProcessorEditor::enterLocalDemoSession() {
  cancelAuthenticationAttempt();
  sessionResumeCancelled = true;
  ++appleConfigurationGeneration;
  appleSignInAvailable = false;
  invalidateCloudAccountView();
  if (remoteSession.getMode() != playworship::remote::RemoteSession::Mode::Local)
    setRemoteMode(playworship::remote::RemoteSession::Mode::Local);
  localDemoSession = true;
  AppSettings::setLocalDemoSessionActive(true);
  activeSession = {};
  activeSession.allowed = true;
  lastOnlineValidationMs = lastSuccessfulLoginMs = reauthGraceDeadlineMs = 0;
  reauthPromptDismissed = false;
  hideReauthPromptModal();
  demoWelcomeChecked = true;
  if (auto* modal = dynamic_cast<WelcomeModal*>(welcomeModal.get())) {
    modal->setChecking(false);
    modal->setAuthenticationBusy(false);
    modal->clearCredentials();
    modal->setAppleSignInAvailable(false);
  }
  SessionAuthLogger::write("Local demo: entering offline workspace (no account credentials)");
  initializeMainUi();
  prepareDemoSong(false);
}
