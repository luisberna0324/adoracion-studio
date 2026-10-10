#include "ui/components/WelcomeModal.h"
#include "ui/components/ReauthPromptModal.h"
#include "ui/UiCommon.h"
#include "data/Localization.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
int assertions = 0;

void expect(bool condition, const char *message) {
  ++assertions;
  if (!condition) throw std::runtime_error(message);
}

template <typename Control, typename Predicate>
Control *find(juce::Component &root, const Predicate &matches) {
  if (auto *control = dynamic_cast<Control *>(&root); control && matches(*control))
    return control;
  for (auto *child : root.getChildren())
    if (auto *control = find<Control>(*child, matches)) return control;
  return nullptr;
}

template <typename Control>
Control &typed(juce::Component &root) {
  auto *control = find<Control>(root, [](const Control &) { return true; });
  expect(control != nullptr, "expected typed auth component");
  return *control;
}

juce::Button &button(juce::Component &root, const char *es, const char *en) {
  const auto text = L10n::tr(es, en);
  auto *control = find<juce::Button>(root, [&text](const auto &candidate) {
    return candidate.getButtonText() == text;
  });
  expect(control != nullptr, "expected auth action button");
  return *control;
}

juce::TextEditor &editor(juce::Component &root, const char *es, const char *en) {
  const auto title = L10n::tr(es, en);
  auto *control = find<juce::TextEditor>(root, [&title](const auto &candidate) {
    return candidate.getTitle() == title;
  });
  expect(control != nullptr, "expected titled auth field");
  return *control;
}

bool effectivelyVisible(const juce::Component &component) {
  for (auto *current = &component; current; current = current->getParentComponent())
    if (!current->isVisible()) return false;
  return true;
}

bool hasLabel(juce::Component &root, const juce::String &text) {
  return find<juce::Label>(root, [&text](const auto &label) {
    return label.getText() == text && effectivelyVisible(label);
  }) != nullptr;
}

void click(juce::Button &control) {
  expect(effectivelyVisible(control) && control.isEnabled(),
         "requested auth action is visible and enabled");
  expect(static_cast<bool>(control.onClick), "auth action has a callback");
  control.onClick();
}

void show(juce::Component &component, int width, int height) {
  component.setVisible(true);
  component.setSize(width, height);
}

juce::TextEditor &email(juce::Component &root) {
  return editor(root, "Correo", "Email");
}

juce::TextEditor &password(juce::Component &root) {
  return editor(root, u8"Contraseña de PlayWorship", "PlayWorship password");
}

void openEmail(juce::Component &root) {
  click(button(root, "Ingresar con correo", "Sign in with email"));
}

void openReset(juce::Component &root) {
  click(button(root, u8"Crear o recuperar contraseña", "Set or reset password"));
}

void testWelcomeNavigationAndCredentials() {
  WelcomeModal welcome;
  show(welcome, 390, 700);
  int cancelled = 0;
  welcome.onAuthenticationCancelled = [&] { ++cancelled; };

  expect(effectivelyVisible(button(welcome, "Continuar con Google", "Continue with Google")),
         "Google provider starts available");
  expect(!effectivelyVisible(typed<EmailAuthenticationForm>(welcome)),
         "email form starts hidden");
  openEmail(welcome);
  auto &form = typed<EmailAuthenticationForm>(welcome);
  expect(effectivelyVisible(form) && !form.isResetMode(), "email opens password login");
  expect(!effectivelyVisible(button(welcome, "Continuar con Google", "Continue with Google")),
         "provider actions are hidden in email form");
  expect(password(welcome).getPasswordCharacter() != 0, "password is masked by default");
  password(welcome).setText("not-a-real-password", false);
  expect(!button(welcome, "Mostrar", "Show").getMouseClickGrabsKeyboardFocus(),
         "revealing a password does not steal focus and dismiss the mobile keyboard");
  click(button(welcome, "Mostrar", "Show"));
  expect(password(welcome).getPasswordCharacter() == 0, "explicit reveal toggles masking");
  openReset(welcome);
  expect(form.isResetMode() && !form.isLinkSent() && form.preferredHeight() < 330,
         "reset is a compact email-only flow");
  expect(!effectivelyVisible(password(welcome)) && password(welcome).getText().isEmpty(),
         "reset hides and clears the password");
  expect(password(welcome).getPasswordCharacter() != 0, "reset restores password masking");
  click(button(welcome, "Volver", "Back"));
  expect(!form.isResetMode() && effectivelyVisible(password(welcome)),
         "back from reset restores login form");
  password(welcome).setText("another-fictional-password", false);
  click(button(welcome, "Volver", "Back"));
  expect(!effectivelyVisible(form), "back from email restores providers");
  expect(password(welcome).getText().isEmpty(), "return to providers clears password");
  expect(cancelled == 2, "opening and leaving email invalidate pending authentication");
  openEmail(welcome);
  password(welcome).setText("temporary-ui-fixture", false);
  click(button(welcome, "Mostrar", "Show"));
  welcome.setVisible(false);
  expect(password(welcome).getText().isEmpty() && password(welcome).getPasswordCharacter() != 0,
         "hiding welcome clears and remasks the password");
}

void testInputValidationAndDispatch() {
  WelcomeModal welcome;
  show(welcome, 390, 700);
  openEmail(welcome);
  int logins = 0, resets = 0;
  juce::String receivedEmail, receivedPassword;
  welcome.onPasswordLoginRequested = [&](const auto &address, const auto &secret) {
    ++logins;
    receivedEmail = address;
    receivedPassword = secret;
  };
  welcome.onPasswordResetRequested = [&](const auto &address) {
    ++resets;
    receivedEmail = address;
  };
  const juce::StringArray invalidEmails{"", "plain", "@example.test", "a@@example.test",
                                       "a@example", "a@.test", "a@example.", "a b@example.test"};
  for (const auto &invalid : invalidEmails) {
    email(welcome).setText(invalid, false);
    password(welcome).setText("ui-test-secret", false);
    click(button(welcome, "Ingresar", "Sign in"));
    expect(logins == 0, "invalid email never dispatches credentials");
    expect(hasLabel(welcome, L10n::tr(u8"Escribe un correo válido.", "Enter a valid email.")),
           "invalid email explains the correction");
  }
  email(welcome).setText("practice@example.test", false);
  password(welcome).clear();
  click(button(welcome, "Ingresar", "Sign in"));
  expect(logins == 0, "empty password does not dispatch");
  expect(hasLabel(welcome, L10n::tr(u8"Escribe tu contraseña de PlayWorship.",
                                    "Enter your PlayWorship password.")),
         "empty password displays an actionable message");
  email(welcome).setText("  practice@example.test  ", false);
  password(welcome).setText(" space is significant ", false);
  click(button(welcome, "Ingresar", "Sign in"));
  expect(logins == 1 && receivedEmail == "practice@example.test" &&
             receivedPassword == " space is significant ",
         "valid login trims email and preserves password whitespace");
  expect(password(welcome).getText().isEmpty(), "submitted password is removed from the form");
  openReset(welcome);
  email(welcome).setText("bad-email", false);
  click(button(welcome, "Enviar enlace", "Send link"));
  expect(resets == 0, "invalid reset email never dispatches a link request");
  email(welcome).setText("  practice@example.test  ", false);
  click(button(welcome, "Enviar enlace", "Send link"));
  expect(resets == 1 && receivedEmail == "practice@example.test",
         "reset dispatches only the trimmed email");
}

void testBusyCancellation() {
  WelcomeModal welcome;
  show(welcome, 390, 700);
  int cancelled = 0, logins = 0, resets = 0;
  welcome.onAuthenticationCancelled = [&] { ++cancelled; };
  welcome.onPasswordLoginRequested = [&](const auto &, const auto &) { ++logins; };
  welcome.onPasswordResetRequested = [&](const auto &) { ++resets; };
  welcome.setAuthenticationBusy(true);
  expect(!button(welcome, "Continuar con Google", "Continue with Google").isEnabled(),
         "busy provider cannot start another browser attempt");
  expect(button(welcome, "Ingresar con correo", "Sign in with email").isEnabled(),
         "email remains an escape from a blocked browser return");
  openEmail(welcome);
  email(welcome).setText("practice@example.test", false);
  password(welcome).setText("ui-test-secret", false);
  auto &submit = button(welcome, "Ingresar", "Sign in");
  welcome.setAuthenticationBusy(true);
  expect(!email(welcome).isEnabled() && !password(welcome).isEnabled(),
         "credentials cannot change during a request");
  expect(!submit.isEnabled(), "busy login disables submit");
  expect(submit.getButtonText() == L10n::tr(u8"Ingresando…", "Signing in…"),
         "busy login tells the user that the request is running");
  // Also exercise the form guard against a queued/reentrant activation.
  submit.onClick();
  expect(logins == 0, "busy guard rejects duplicate login dispatch");
  click(button(welcome, "Volver", "Back"));
  expect(cancelled == 2 && !effectivelyVisible(typed<EmailAuthenticationForm>(welcome)),
         "back cancels a slow login and restores providers");
  expect(password(welcome).getText().isEmpty(), "cancelling a request clears credentials");
  openEmail(welcome);
  openReset(welcome);
  auto &reset = button(welcome, "Enviar enlace", "Send link");
  welcome.setAuthenticationBusy(true);
  expect(!reset.isEnabled(), "busy link request disables submit");
  expect(reset.getButtonText() == L10n::tr(u8"Enviando…", "Sending…"),
         "busy recovery shows progress without allowing another request");
  reset.onClick();
  expect(resets == 0, "busy guard rejects duplicate reset dispatch");
  click(button(welcome, "Volver", "Back"));
  expect(!effectivelyVisible(typed<EmailAuthenticationForm>(welcome)),
         "back cancels a slow reset without waiting for HTTP");
}

void testLocalDemoFlowIsSeparateAndCancellable() {
  WelcomeModal welcome;
  show(welcome, 390, 700);
  int cancelled = 0, remoteLogins = 0, resetRequests = 0, localLogins = 0;
  juce::String receivedUsername, receivedPassword;
  welcome.onAuthenticationCancelled = [&] { ++cancelled; };
  welcome.onPasswordLoginRequested = [&](const auto &, const auto &) { ++remoteLogins; };
  welcome.onPasswordResetRequested = [&](const auto &) { ++resetRequests; };
  welcome.onLocalDemoLoginRequested = [&](const auto &username, const auto &secret) {
    ++localLogins;
    receivedUsername = username;
    receivedPassword = secret;
  };
  welcome.setStatusText("Checking an existing session", false);
  welcome.setChecking(true);
  welcome.setAuthenticationBusy(true);
  auto &localAction = button(welcome, u8"Prueba sin conexión", "Offline trial");
  expect(effectivelyVisible(localAction) && localAction.isEnabled() && localAction.getHeight() >= 48,
         "offline trial remains reachable while automatic session validation is pending");
  click(localAction);
  auto &form = typed<EmailAuthenticationForm>(welcome);
  expect(cancelled == 1 && form.isLocalDemoMode() && !form.isResetMode(),
         "opening offline trial cancels an old request and opens a distinct form mode");
  expect(hasLabel(welcome, L10n::tr(u8"Prueba sin conexión", "Offline trial")),
         "offline trial is explicitly identified in the heading");
  auto &username = editor(welcome, "Usuario", "Username");
  auto &secret = editor(welcome, u8"Contraseña", "Password");
  expect(username.getKeyboardType() == juce::TextInputTarget::textKeyboard &&
             secret.getPasswordCharacter() != 0,
         "offline trial has an ordinary username keyboard and a masked password");
  expect(!effectivelyVisible(button(welcome, u8"Crear o recuperar contraseña", "Set or reset password")),
         "offline trial does not offer a backend password recovery request");
  button(welcome, u8"Crear o recuperar contraseña", "Set or reset password").onClick();
  expect(form.isLocalDemoMode() && resetRequests == 0,
         "a stale recovery action cannot leave or send requests from the local form");
  secret.setText("usuarioplay0324", false);
  click(button(welcome, "Abrir demo", "Open demo"));
  expect(localLogins == 0 && hasLabel(welcome, L10n::tr("Escribe el usuario de prueba.", "Enter the trial username.")),
         "offline trial requires a username without applying email validation");
  username.setText("usuarioplay0324", false);
  secret.clear();
  click(button(welcome, "Abrir demo", "Open demo"));
  expect(localLogins == 0 && hasLabel(welcome, L10n::tr(u8"Escribe la contraseña de prueba.", "Enter the trial password.")),
         "offline trial requires a password before local dispatch");
  username.setText("  usuarioplay0324  ", false);
  secret.setText(" space stays significant ", false);
  click(button(welcome, "Abrir demo", "Open demo"));
  expect(localLogins == 1 && receivedUsername == "usuarioplay0324" &&
             receivedPassword == " space stays significant " &&
             remoteLogins == 0 && resetRequests == 0 && secret.getText().isEmpty(),
         "offline credentials use only the distinct local callback and clear the submitted password");
  welcome.showPasswordLinkSent();
  expect(form.isLocalDemoMode() && !form.isLinkSent(),
         "a late backend confirmation cannot replace the offline form");
  secret.setText("temporary-local-secret", false);
  welcome.setAuthenticationBusy(true);
  auto &submit = button(welcome, "Abriendo…", "Opening…");
  submit.onClick();
  expect(localLogins == 1, "busy offline trial rejects a duplicate queued submit");
  click(button(welcome, "Volver", "Back"));
  expect(cancelled == 2 && !effectivelyVisible(form) && secret.getText().isEmpty(),
         "back from offline trial clears credentials and returns to providers");
  openEmail(welcome);
  expect(!form.isLocalDemoMode() && email(welcome).getText().isEmpty() &&
             email(welcome).getKeyboardType() == juce::TextInputTarget::emailAddressKeyboard &&
             effectivelyVisible(button(welcome, u8"Crear o recuperar contraseña", "Set or reset password")),
         "normal email sign in restores its keyboard and recovery without retaining a local username");
  click(button(welcome, "Volver", "Back"));
  click(button(welcome, u8"Prueba sin conexión", "Offline trial"));
  auto &hiddenSecret = editor(welcome, u8"Contraseña", "Password");
  hiddenSecret.setText("secret-only-in-ui", false);
  click(button(welcome, "Mostrar", "Show"));
  welcome.setVisible(false);
  expect(hiddenSecret.getText().isEmpty() && hiddenSecret.getPasswordCharacter() != 0,
         "hiding offline trial clears and remasks its password");
}

void testLocalDemoAndProviderLayouts() {
  for (const auto size : {juce::Point<int>{390, 700}, {390, 360}, {820, 1000}, {1200, 760}}) {
    WelcomeModal welcome;
    show(welcome, size.x, size.y);
    auto &google = button(welcome, "Continuar con Google", "Continue with Google");
    auto &emailAction = button(welcome, "Ingresar con correo", "Sign in with email");
    auto &localAction = button(welcome, u8"Prueba sin conexión", "Offline trial");
    for (auto *action : {&google, &emailAction, &localAction})
      expect(action->getHeight() >= 48 && welcome.getLocalBounds().contains(action->getBounds()),
             "all provider actions fit the phone, compact keyboard, tablet and desktop at full height");
    expect(google.getBottom() <= emailAction.getY() && emailAction.getBottom() < localAction.getY(),
           "offline trial follows the provider actions with distinct spacing");
    click(localAction);
    auto &form = typed<EmailAuthenticationForm>(welcome);
    expect(!form.getBounds().isEmpty() && welcome.getLocalBounds().contains(form.getBounds()),
           "offline form fits each supported surface");
    auto &viewport = typed<juce::Viewport>(form);
    auto *content = viewport.getViewedComponent();
    expect(content != nullptr && content->getLocalBounds().contains(
               editor(welcome, "Usuario", "Username").getBounds()) &&
               content->getLocalBounds().contains(editor(welcome, u8"Contraseña", "Password").getBounds()),
           "offline credentials fit their scrollable content");
    if (size.y == 360) {
      viewport.setViewPosition(0, content->getHeight());
      expect(viewport.getViewArea().contains(button(welcome, "Abrir demo", "Open demo").getBounds()) &&
                 effectivelyVisible(button(welcome, "Volver", "Back")),
             "offline submit is reachable by scrolling and back remains fixed above the keyboard");
    }
  }
}

void testCompactLayoutsAndReachability() {
  for (const auto size : {juce::Point<int>{390, 700}, {390, 360}, {820, 1000}, {1200, 760}}) {
    WelcomeModal welcome;
    show(welcome, size.x, size.y);
    openEmail(welcome);
    auto &form = typed<EmailAuthenticationForm>(welcome);
    expect(!form.getBounds().isEmpty() && welcome.getLocalBounds().contains(form.getBounds()),
           "email form fits phone, compact keyboard, tablet and desktop surfaces");
    auto &viewport = typed<juce::Viewport>(form);
    expect(form.getLocalBounds().contains(viewport.getBounds()), "form viewport stays inside card");
    auto *content = viewport.getViewedComponent();
    expect(content != nullptr, "email form has scrollable content");
    for (auto *field : {&email(welcome), &password(welcome)}) {
      expect(field->getHeight() >= 50 && content->getLocalBounds().contains(field->getBounds()),
             "text fields retain touch targets and fit scrollable content");
    }
    auto &back = button(welcome, "Volver", "Back");
    expect(dynamic_cast<HeaderIconButton *>(&back) != nullptr &&
               back.getParentComponent() == &welcome && back.getHeight() >= 44 &&
               welcome.getLocalBounds().contains(back.getBounds()),
           "cancel uses the shared header icon outside the scrollable form");
    auto &eye = button(welcome, "Mostrar", "Show");
    expect(eye.getWidth() >= 44 && eye.getHeight() >= 44 &&
               password(welcome).getBounds().contains(eye.getBounds()),
           "password reveal sits inside the field with a full touch target");
    expect(button(welcome, "Ingresar", "Sign in").getHeight() >= 48 &&
               button(welcome, u8"Crear o recuperar contraseña", "Set or reset password").getHeight() >= 44,
           "login and recovery retain distinct, reachable touch targets");
    expect(!email(welcome).getBounds().intersects(password(welcome).getBounds()),
           "credential fields never overlap");
    if (size.y == 360) {
      const auto heading = L10n::tr(u8"Iniciar sesión", "Sign in");
      auto *title = find<juce::Label>(welcome, [&heading](const auto &label) {
        return label.getText() == heading && effectivelyVisible(label);
      });
      expect(title != nullptr && form.getY() >= title->getBottom() &&
                 form.getY() - title->getBottom() <= 28,
             "compact form follows the heading with standard spacing and no empty status area");
      expect(content->getHeight() > viewport.getHeight(), "keyboard-height layout can scroll");
      const auto headerBackBounds = back.getBounds();
      viewport.setViewPosition(0, content->getHeight());
      expect(effectivelyVisible(back) && back.getBounds() == headerBackBounds,
             "cancel remains fixed in the header while scrolling with a keyboard");
      expect(viewport.getViewArea().contains(
                 button(welcome, u8"Crear o recuperar contraseña", "Set or reset password").getBounds()),
             "recovery is reachable at the bottom of the compact form");
      viewport.setViewPosition(0, 0);
      expect(viewport.getViewArea().contains(email(welcome).getBounds()),
             "email stays reachable at the start of the compact form");
    }
    openReset(welcome);
    expect(!form.getBounds().isEmpty() && welcome.getLocalBounds().contains(form.getBounds()),
           "reset form fits each supported surface");
  }
}

void testReauthenticationStatusAndHeight() {
  ReauthPromptModal reauth;
  show(reauth, 530, 530);
  std::vector<int> requestedHeights;
  int cancelled = 0;
  reauth.onPreferredHeightChanged = [&] { requestedHeights.push_back(reauth.preferredHeight()); };
  reauth.onAuthenticationCancelled = [&] { ++cancelled; };
  const int providerHeight = reauth.preferredHeight();
  expect(providerHeight > 0, "reauth provider card has a bounded preferred height");
  openEmail(reauth);
  const int loginHeight = reauth.preferredHeight();
  expect(loginHeight > providerHeight && !requestedHeights.empty() &&
             requestedHeights.back() == loginHeight,
         "email navigation notifies parent to expand reauth card");
  reauth.showError("UI fixture: request failed");
  expect(reauth.preferredHeight() > loginHeight && requestedHeights.back() == reauth.preferredHeight(),
         "reauth error reserves room and notifies the parent of its new height");
  reauth.resized();
  reauth.setAppleSignInAvailable(false);
  expect(hasLabel(reauth, "UI fixture: request failed"),
         "resizing and provider configuration preserve reauth error status");
  reauth.setAuthenticationBusy(true);
  reauth.resized();
  expect(hasLabel(reauth, "UI fixture: request failed"),
         "busy updates do not overwrite a custom reauth status");
  reauth.setAuthenticationBusy(false);
  openReset(reauth);
  const int resetHeight = reauth.preferredHeight();
  expect(resetHeight < loginHeight && requestedHeights.back() == resetHeight,
         "reset navigation requests the smaller reauth card");
  reauth.setStatusText("UI fixture: link requested", false);
  reauth.resized();
  expect(hasLabel(reauth, "UI fixture: link requested"),
         "link success remains visible after parent layout");
  click(button(reauth, "Volver", "Back"));
  expect(reauth.preferredHeight() == loginHeight && requestedHeights.back() == loginHeight,
         "back from reset requests login card height");
  password(reauth).setText("reauth-fictional-secret", false);
  reauth.setVisible(false);
  expect(password(reauth).getText().isEmpty(), "hiding reauth clears password");
  reauth.setVisible(true);
  click(button(reauth, "Volver", "Back"));
  expect(reauth.preferredHeight() == providerHeight && requestedHeights.back() == providerHeight && cancelled >= 2,
         "back to providers cancels attempt and restores compact reauth height");
  int later = 0;
  reauth.onLater = [&] { ++later; };
  const int beforeLater = cancelled;
  click(button(reauth, u8"Más tarde", "Later"));
  expect(later == 1 && cancelled > beforeLater,
         "Later cancels authentication before dismissing reauth");
}

template <typename Modal>
void exercisePasswordLinkSuccess(Modal &modal) {
  openEmail(modal);
  auto &form = typed<EmailAuthenticationForm>(modal);
  email(modal).setText("practice@example.test", false);
  password(modal).setText("fictional-password-before-recovery", false);
  openReset(modal);
  int requests = 0;
  modal.onPasswordResetRequested = [&](const auto &address) {
    expect(address == "practice@example.test", "recovery keeps the same account email");
    ++requests;
    modal.setAuthenticationBusy(true);
    // Simulate the successful network completion without sending any email.
    modal.showPasswordLinkSent();
  };
  click(button(modal, "Enviar enlace", "Send link"));
  expect(requests == 1 && form.isLinkSent(), "successful recovery opens its confirmation state");
  expect(hasLabel(modal, L10n::tr("Revisa tu correo", "Check your email")),
         "confirmation explains the next action in the header");
  expect(!effectivelyVisible(email(modal)) && !effectivelyVisible(password(modal)) &&
             password(modal).getText().isEmpty() && password(modal).getPasswordCharacter() != 0,
         "confirmation hides all credential fields and keeps the password cleared and masked");
  expect(effectivelyVisible(button(modal, "Volver al acceso", "Back to sign in")) &&
             effectivelyVisible(button(modal, "Solicitar otro enlace", "Request another link")),
         "confirmation offers an explicit return and another-link action");
  click(button(modal, "Solicitar otro enlace", "Request another link"));
  expect(!form.isLinkSent() && form.isResetMode() && effectivelyVisible(email(modal)) &&
             email(modal).getText() == "practice@example.test" && requests == 1,
         "another-link action restores the retained email without sending automatically");
  click(button(modal, "Enviar enlace", "Send link"));
  expect(requests == 2 && form.isLinkSent(), "another email is requested only after explicit submit");
  click(button(modal, "Volver al acceso", "Back to sign in"));
  expect(!form.isResetMode() && effectivelyVisible(email(modal)) &&
             effectivelyVisible(password(modal)) && password(modal).getText().isEmpty(),
         "return to sign in restores the login fields without restoring any password");
}

void testPasswordLinkSuccess() {
  WelcomeModal welcome;
  show(welcome, 390, 700);
  exercisePasswordLinkSuccess(welcome);
  ReauthPromptModal reauth;
  show(reauth, 530, 530);
  std::vector<int> preferredHeights;
  reauth.onPreferredHeightChanged = [&] { preferredHeights.push_back(reauth.preferredHeight()); };
  exercisePasswordLinkSuccess(reauth);
  expect(!preferredHeights.empty() && preferredHeights.back() == reauth.preferredHeight(),
         "reauth success and return propagate their final preferred height");
}

void capture(juce::Component &component, const juce::File &directory, const juce::String &name) {
  const auto image = component.createComponentSnapshot(component.getLocalBounds());
  auto stream = directory.getChildFile(name + ".png").createOutputStream();
  // FileOutputStream opens existing files at their end. Each rerun must replace
  // the previous PNG; appending leaves viewers displaying the stale first image.
  expect(stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk(),
         "replace an existing auth screenshot instead of appending another PNG");
  expect(juce::PNGImageFormat().writeImageToStream(image, *stream),
         "write auth UI screenshot");
}

void optionalScreenshots() {
  const auto path = juce::SystemStats::getEnvironmentVariable("PW_AUTH_UI_SCREENSHOTS", {});
  if (path.isEmpty()) return;
  expect(juce::File::isAbsolutePath(path), "capture directory must be absolute");
  juce::File directory(path);
  expect(directory.createDirectory().wasOk(), "create auth capture directory");
  struct Layout { const char *name; int width, height; };
  for (const auto layout : {Layout{"phone", 390, 700}, {"phone-keyboard", 390, 360},
                            {"tablet", 820, 1000}, {"desktop", 1200, 760}}) {
    WelcomeModal welcome;
    show(welcome, layout.width, layout.height);
    capture(welcome, directory, juce::String(layout.name) + "-providers");
    openEmail(welcome);
    email(welcome).setText("practice@example.test", false);
    capture(welcome, directory, juce::String(layout.name) + "-email");
    if (layout.height == 360) {
      auto &viewport = typed<juce::Viewport>(typed<EmailAuthenticationForm>(welcome));
      viewport.setViewPosition(0, viewport.getViewedComponent()->getHeight());
      capture(welcome, directory, "phone-keyboard-email-bottom");
    }
    openReset(welcome);
    capture(welcome, directory, juce::String(layout.name) + "-reset");
    welcome.showPasswordLinkSent();
    capture(welcome, directory, juce::String(layout.name) + "-link-sent");
#if JUCE_DEBUG
    welcome.showLocalDemoLogin();
    editor(welcome, "Usuario", "Username").setText("usuarioplay0324", false);
    capture(welcome, directory, juce::String(layout.name) + "-offline-trial");
#endif
  }
  ReauthPromptModal reauth;
  show(reauth, 530, 530);
  reauth.onPreferredHeightChanged = [&] {
    reauth.setSize(530, reauth.preferredHeight());
  };
  reauth.setSize(530, reauth.preferredHeight());
  capture(reauth, directory, "reauth-providers");
  openEmail(reauth);
  email(reauth).setText("practice@example.test", false);
  reauth.setStatusText(L10n::tr(u8"Revisa el correo o establece tu contraseña.",
                               "Check your email or set your password."), true);
  capture(reauth, directory, "reauth-email");
  reauth.showPasswordLinkSent();
  capture(reauth, directory, "reauth-link-sent");
  std::cout << "Auth UI captures: " << directory.getFullPathName() << '\n';
}

void optionalPreview() {
  // PW_AUTH_UI_PREVIEW=120 opens a real peer for two minutes. Unlike snapshots,
  // this preserves normal dirty-region painting across an interactive reset.
  const auto option = juce::SystemStats::getEnvironmentVariable("PW_AUTH_UI_PREVIEW", {});
  if (option.isEmpty() || option == "0") return;
  const int requestedSeconds = option.getIntValue();
  const int seconds = juce::jlimit(10, 300, requestedSeconds > 1 ? requestedSeconds : 120);

  class PreviewWindow final : public juce::DocumentWindow {
  public:
    PreviewWindow()
        : juce::DocumentWindow("PlayWorship Auth - isolated preview", AuthUi::surface,
                               juce::DocumentWindow::closeButton) {
      setUsingNativeTitleBar(true);
      setResizable(false, false);
      auto *welcome = new WelcomeModal();
      setContentOwned(welcome, false);
      setContentComponentSize(390, 700);
      centreWithSize(getWidth(), getHeight());

      const juce::Component::SafePointer<WelcomeModal> safeWelcome(welcome);
      welcome->onPasswordResetRequested = [safeWelcome](const juce::String &) {
        if (safeWelcome == nullptr) return;
        safeWelcome->setAuthenticationBusy(true);
        juce::Timer::callAfterDelay(300, [safeWelcome] {
          if (safeWelcome != nullptr && safeWelcome->isShowing())
            safeWelcome->showPasswordLinkSent();
        });
      };
      welcome->onPasswordLoginRequested = [safeWelcome](const auto &, const auto &) {
        if (safeWelcome != nullptr)
          safeWelcome->setStatusText("Vista previa local: acceso simulado.", false);
      };
      welcome->onLoginRequested = [safeWelcome] {
        if (safeWelcome != nullptr)
          safeWelcome->setStatusText("Vista previa local: usa el acceso por correo.", false);
      };

      setVisible(true);
      openEmail(*welcome);
      openReset(*welcome);
      email(*welcome).setText("practice@example.test", false);
      toFront(true);
    }

    void closeButtonPressed() override { setVisible(false); }
  };

  PreviewWindow window;
  std::cout << "Auth preview: click Send link for a local 300 ms response; closes after "
            << seconds << " seconds. No network or real credentials.\n" << std::flush;
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
  while (window.isVisible() && juce::Time::getMillisecondCounterHiRes() < deadline) {
    if (!juce::MessageManager::getInstance()->runDispatchLoopUntil(20)) break;
  }
}
} // namespace

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr) return 77;
#endif
  // The test target compiles AppSettings with isolated storage, like SongWizard.
  // No real account/session is read or mutated, and every callback is local.
  if (juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_CREATION_TEST_ROOT", {}).isEmpty())
    return 77;
  try {
    juce::ScopedJuceInitialiser_GUI gui;
    applyTheme(ThemePreset::Spotify);
    {
      WelcomeModal welcome; show(welcome, 390, 844);
      int cancelled = 0;
      welcome.onAuthenticationCancelled = [&] { ++cancelled; welcome.setInlineAppleLink(false); };
      welcome.setInlineAppleLink(true);
      expect(effectivelyVisible(button(welcome, "Continuar con Google", "Continue with Google")), "inline linking reuses Google authentication");
#if JUCE_DEBUG
      expect(!effectivelyVisible(button(welcome, "Prueba sin conexión", "Offline trial")), "inline linking excludes demo account");
#endif
      expect(button(welcome, "Volver", "Back").getWidth() >= 40, "inline back has reachable bounds on iPhone");
      click(button(welcome, "Volver", "Back"));
      expect(cancelled == 1, "inline provider screen back cancels intent");
      welcome.setInlineAppleLink(true);
      click(button(welcome, "Ingresar con correo", "Sign in with email"));
      expect(cancelled == 1, "email navigation preserves pending explicit linking flow");
      click(button(welcome, "Volver", "Back"));
      expect(cancelled == 2, "inline email back cancels intent");
      welcome.setInlineAppleLink(true); welcome.setVisible(false);
      expect(cancelled == 3, "closing inline authentication cancels intent");
      ReauthPromptModal reauth; show(reauth, 390, 800);
      int reauthCancelled = 0;
      reauth.onAuthenticationCancelled = [&] { ++reauthCancelled; reauth.setInlineAppleLink(false); };
      reauth.setInlineAppleLink(true); click(button(reauth, "Volver", "Back"));
      expect(reauthCancelled == 1, "reauth back cancels inline link too");
    }
    testWelcomeNavigationAndCredentials();
    testInputValidationAndDispatch();
    testBusyCancellation();
#if JUCE_DEBUG
    testLocalDemoFlowIsSeparateAndCancellable();
    testLocalDemoAndProviderLayouts();
#else
    {
      WelcomeModal welcome; show(welcome, 820, 700);
      int calls = 0;
      welcome.onLocalDemoLoginRequested = [&](const auto &, const auto &) { ++calls; };
      expect(find<juce::Button>(welcome, [](const auto &b) { return b.getButtonText() == L10n::tr("Prueba sin conexión", "Offline trial"); }) == nullptr,
             "production contains no test-login button in its component tree");
      welcome.showLocalDemoLogin();
      auto &form = typed<EmailAuthenticationForm>(welcome);
      form.showLocalDemoLogin();
      expect(!form.isLocalDemoMode() && !form.isVisible() && calls == 0,
             "production rejects direct test-login entry points");
    }
#endif
    testCompactLayoutsAndReachability();
    testReauthenticationStatusAndHeight();
    testPasswordLinkSuccess();
    optionalScreenshots();
    std::cout << "Email auth UI: " << assertions << " checks passed (local callbacks, no network)\n";
    optionalPreview();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
