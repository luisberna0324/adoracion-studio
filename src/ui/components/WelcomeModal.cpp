#include "WelcomeModal.h"
#include "../../data/Localization.h"
#include "../../platform/NativeAppleSignIn.h"

EmailAuthenticationForm::EmailAuthenticationForm() {
  setFocusContainerType(FocusContainerType::keyboardFocusContainer);
  viewport.setViewedComponent(&content, false);
  viewport.setScrollBarsShown(true, false);
  viewport.setScrollBarThickness(5);
  addAndMakeVisible(viewport);
  juce::Label* labels[] = {&explanation, &emailLabel, &passwordLabel};
  for (auto* control : labels) {
    AuthUi::label(*control, 14);
    control->setJustificationType(juce::Justification::centredLeft);
    content.addAndMakeVisible(*control);
  }
  AuthUi::label(explanation, 15);
  emailLabel.setText(L10n::tr("Correo", "Email"), juce::dontSendNotification);
  passwordLabel.setText(L10n::tr(u8"Contraseña de PlayWorship", "PlayWorship password"), juce::dontSendNotification);
  for (auto* editor : {&emailEditor, &passwordEditor}) {
    editor->setLookAndFeel(&fieldStyle);
    editor->setFont(juce::FontOptions(16));
    editor->setMultiLine(false);
    editor->setReturnKeyStartsNewLine(false);
    editor->setIndents(14, 15);
    editor->setColour(juce::TextEditor::textColourId, AuthUi::text);
    editor->setColour(juce::TextEditor::highlightColourId, AuthUi::accent.withAlpha(0.3f));
    editor->setColour(juce::CaretComponent::caretColourId, AuthUi::accent);
    editor->onEscapeKey = [this] { goBack(); };
    content.addAndMakeVisible(*editor);
  }
  emailEditor.setTitle(L10n::tr("Correo", "Email"));
  emailEditor.setDescription(L10n::tr("El correo de tu cuenta de PlayWorship.", "Your PlayWorship account email."));
  emailEditor.setKeyboardType(juce::TextInputTarget::emailAddressKeyboard);
  emailEditor.setExplicitFocusOrder(1);
  emailEditor.setInputRestrictions(254);
  emailEditor.setTextToShowWhenEmpty(L10n::tr("nombre@correo.com", "name@example.com"), AuthUi::muted.withAlpha(0.7f));
  emailEditor.onReturnKey = [this] {
    if (isResetMode()) submit();
    else {
      viewport.setViewPosition(0, juce::jmax(0, passwordEditor.getBottom() - viewport.getHeight()));
      passwordEditor.grabKeyboardFocus();
    }
  };
  passwordEditor.setTitle(L10n::tr(u8"Contraseña de PlayWorship", "PlayWorship password"));
  passwordEditor.setKeyboardType(juce::TextInputTarget::passwordKeyboard);
  passwordEditor.setPasswordCharacter(0x2022);
  passwordEditor.setInputRestrictions(1024);
  passwordEditor.setExplicitFocusOrder(2);
  passwordEditor.setBorder(juce::BorderSize<int>(0, 0, 0, 48));
  passwordEditor.onReturnKey = [this] { submit(); };
  content.addAndMakeVisible(showPasswordButton);
  content.addAndMakeVisible(submitButton);
  content.addAndMakeVisible(resetButton);
  content.addChildComponent(mailIcon);
  showPasswordButton.setExplicitFocusOrder(3);
  showPasswordButton.setMouseClickGrabsKeyboardFocus(false);
  submitButton.setExplicitFocusOrder(4);
  resetButton.setExplicitFocusOrder(5);
  showPasswordButton.onClick = [this] {
    const bool reveal = passwordEditor.getPasswordCharacter() != 0;
    passwordEditor.setPasswordCharacter(reveal ? 0 : 0x2022);
    showPasswordButton.setToggleState(reveal, juce::dontSendNotification);
    showPasswordButton.setButtonText(reveal ? L10n::tr("Ocultar", "Hide") : L10n::tr("Mostrar", "Show"));
    showPasswordButton.setTitle(reveal ? L10n::tr(u8"Ocultar contraseña", "Hide password") : L10n::tr(u8"Mostrar contraseña", "Show password"));
    showPasswordButton.setTooltip(showPasswordButton.getTitle());
  };
  submitButton.onClick = [this] { submit(); };
  resetButton.onClick = [this] {
    if (busy) return;
    clearCredentials();
    mode = Mode::reset;
    updateMode();
    if (onModeChanged) onModeChanged();
    focusEmail();
  };
  updateMode();
}

EmailAuthenticationForm::~EmailAuthenticationForm() {
  emailEditor.setLookAndFeel(nullptr);
  passwordEditor.setLookAndFeel(nullptr);
}

void EmailAuthenticationForm::showLogin() {
  clearCredentials();
  mode = Mode::login;
  updateMode();
  focusEmail();
}

void EmailAuthenticationForm::showPasswordLinkSent() {
  clearCredentials();
  mode = Mode::linkSent;
  setBusy(false);
  updateMode();
  if (onModeChanged) onModeChanged();
  if (isShowing()) submitButton.grabKeyboardFocus();
}

void EmailAuthenticationForm::goBack() {
  if (isResetMode() && !busy) {
    showLogin();
    if (onModeChanged) onModeChanged();
  } else if (onBack) onBack();
}

void EmailAuthenticationForm::setBusy(bool isBusy) {
  busy = isBusy;
  juce::Component* controls[] = {&emailEditor, &passwordEditor, &showPasswordButton, &submitButton, &resetButton};
  for (auto* control : controls)
    control->setEnabled(!busy);
  submitButton.setButtonText(busy ? (isResetMode() ? L10n::tr("Enviando…", "Sending…")
                                                   : L10n::tr("Ingresando…", "Signing in…"))
                               : isLinkSent() ? L10n::tr("Volver al acceso", "Back to sign in")
                               : isResetMode() ? L10n::tr("Enviar enlace", "Send link")
                                               : L10n::tr("Ingresar", "Sign in"));
}

void EmailAuthenticationForm::clearCredentials() {
  passwordEditor.clear();
  passwordEditor.setPasswordCharacter(0x2022);
  showPasswordButton.setToggleState(false, juce::dontSendNotification);
  showPasswordButton.setButtonText(L10n::tr("Mostrar", "Show"));
  showPasswordButton.setTitle(L10n::tr(u8"Mostrar contraseña", "Show password"));
  showPasswordButton.setTooltip(showPasswordButton.getTitle());
}

void EmailAuthenticationForm::focusEmail() {
  if (isShowing() && !isLinkSent() && emailEditor.isEnabled()) emailEditor.grabKeyboardFocus();
}

void EmailAuthenticationForm::updateMode() {
  emailEditor.getProperties().set("playworshipReturnKeyNext", !isResetMode());
  explanation.setText(isLinkSent()
      ? L10n::tr(u8"Si el correo está asociado a una cuenta, recibirás un enlace para crear o recuperar tu contraseña. Revisa también la carpeta de spam.",
                  "If the email belongs to an account, you'll receive a link to set or reset your password. Check your spam folder too.")
      : isResetMode()
      ? L10n::tr(u8"Te enviaremos un enlace para crear o recuperar la contraseña de tu cuenta de PlayWorship.",
                  "We'll email you a link to set or reset your PlayWorship password.")
      : L10n::tr(u8"Si usabas Google, puedes crear una contraseña para esta misma cuenta.",
                  "If you used Google, you can set a password for the same account."), juce::dontSendNotification);
  explanation.setJustificationType(isLinkSent() ? juce::Justification::centredTop : juce::Justification::topLeft);
  resetButton.setButtonText(isLinkSent() ? L10n::tr("Solicitar otro enlace", "Request another link")
      : L10n::tr(u8"Crear o recuperar contraseña", "Set or reset password"));
  emailLabel.setVisible(!isLinkSent());
  emailEditor.setVisible(!isLinkSent());
  passwordLabel.setVisible(!isResetMode());
  passwordEditor.setVisible(!isResetMode());
  showPasswordButton.setVisible(!isResetMode());
  resetButton.setVisible(!isResetMode() || isLinkSent());
  mailIcon.setVisible(isLinkSent());
  setBusy(busy);
  viewport.setViewPosition(0, 0);
  resized();
}

void EmailAuthenticationForm::submit() {
  if (busy) return;
  if (isLinkSent()) {
    showLogin();
    if (onModeChanged) onModeChanged();
    return;
  }
  const auto email = emailEditor.getText().trim();
  const int at = email.indexOfChar('@');
  if (at <= 0 || at != email.lastIndexOfChar('@') ||
      email.substring(at + 1).indexOfChar('.') <= 0 || email.endsWithChar('.') || email.containsAnyOf(" \r\n\t")) {
    if (onStatusChanged) onStatusChanged(L10n::tr(u8"Escribe un correo válido.", "Enter a valid email."), true);
    focusEmail();
    return;
  }
  emailEditor.setText(email, false);
  if (isResetMode()) {
    if (onPasswordResetRequested) onPasswordResetRequested(email);
    return;
  }
  const auto password = passwordEditor.getText();
  if (password.isEmpty()) {
    if (onStatusChanged) onStatusChanged(L10n::tr(u8"Escribe tu contraseña de PlayWorship.", "Enter your PlayWorship password."), true);
    viewport.setViewPosition(0, juce::jmax(0, passwordEditor.getBottom() - viewport.getHeight()));
    passwordEditor.grabKeyboardFocus();
    return;
  }
  clearCredentials();
  if (onPasswordLoginRequested) onPasswordLoginRequested(email, password);
}

int EmailAuthenticationForm::preferredHeight(int width) const {
  const int contentWidth = juce::jmax(1, width > 0 ? width : (getWidth() > 0 ? getWidth() : 320));
  const int copyHeight = explanation.heightForWidth(contentWidth);
  if (isLinkSent())
    return 56 + 16 + copyHeight + 24 + 48 + 8 + 44;
  if (isResetMode())
    return copyHeight + 20 + 20 + 8 + 50 + 24 + 48;
  return 20 + 8 + 50 + 20 + 20 + 8 + 50 + 24 + 48 + 12 + copyHeight + 8 + 44;
}

void EmailAuthenticationForm::resized() {
  viewport.setBounds(getLocalBounds());
  const int contentWidth = juce::jmax(1, getWidth() - (getHeight() < preferredHeight(getWidth()) ? 8 : 0));
  content.setSize(contentWidth, preferredHeight(contentWidth));
  auto area = content.getLocalBounds();
  const int copyHeight = explanation.heightForWidth(contentWidth);
  if (isLinkSent()) {
    mailIcon.setBounds(area.removeFromTop(56));
    area.removeFromTop(16);
    explanation.setBounds(area.removeFromTop(copyHeight));
    area.removeFromTop(24);
    submitButton.setBounds(area.removeFromTop(48));
    area.removeFromTop(8);
    resetButton.setBounds(area.removeFromTop(44));
    return;
  }
  if (isResetMode()) {
    explanation.setBounds(area.removeFromTop(copyHeight));
    area.removeFromTop(20);
  }
  emailLabel.setBounds(area.removeFromTop(20));
  area.removeFromTop(8);
  emailEditor.setBounds(area.removeFromTop(50));
  if (!isResetMode()) {
    area.removeFromTop(20);
    passwordLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(8);
    passwordEditor.setBounds(area.removeFromTop(50));
    showPasswordButton.setBounds(passwordEditor.getBounds().removeFromRight(48).reduced(2));
  }
  area.removeFromTop(24);
  submitButton.setBounds(area.removeFromTop(48));
  if (!isResetMode()) {
    area.removeFromTop(12);
    explanation.setBounds(area.removeFromTop(copyHeight));
    area.removeFromTop(8);
    resetButton.setBounds(area.removeFromTop(44));
  }
}

WelcomeModal::WelcomeModal() {
  setOpaque(false);
  logoImage = juce::ImageFileFormat::loadFrom(BinaryData::pwLogoSinFondo_png, BinaryData::pwLogoSinFondo_pngSize);
  logo.setImage(logoImage, juce::RectanglePlacement::centred);
  addAndMakeVisible(logo);
  for (auto* control : {&title, &subtitle, &status, &separator}) {
    AuthUi::label(*control, control == &title ? 28.0f : 14.0f, control == &title);
    control->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*control);
  }
  separator.setText(L10n::tr("o", "or"), juce::dontSendNotification);
  loginButton.setButtonText(L10n::tr("Continuar con Google", "Continue with Google"));
  loginButton.onClick = [this] { if (onLoginRequested) onLoginRequested(); };
  addAndMakeVisible(loginButton);
  emailButton.setButtonText(L10n::tr("Ingresar con correo", "Sign in with email"));
  emailButton.onClick = [this] { showEmailForm(true); };
  addAndMakeVisible(emailButton);
  backButton.setButtonText(L10n::tr("Volver", "Back"));
  backButton.setTitle(L10n::tr("Volver", "Back"));
  backButton.setTooltip(L10n::tr("Volver", "Back"));
  backButton.setExplicitFocusOrder(1);
  backButton.onClick = [this] { emailForm.goBack(); };
  addChildComponent(backButton);
  emailForm.onPasswordLoginRequested = [this](const auto& email, const auto& password) {
    if (onPasswordLoginRequested) onPasswordLoginRequested(email, password);
  };
  emailForm.onPasswordResetRequested = [this](const auto& email) {
    if (onPasswordResetRequested) onPasswordResetRequested(email);
  };
  emailForm.onStatusChanged = [this](const auto& message, bool error) { setStatusText(message, error); };
  emailForm.onBack = [this] { showEmailForm(false); };
  emailForm.onModeChanged = [this] { updateHeading(); resetStatusToDefault(); resized(); };
  addChildComponent(emailForm);
  updateHeading();
}

WelcomeModal::~WelcomeModal() = default;

void WelcomeModal::updateHeading() {
  title.setText(!emailMode ? L10n::tr("Bienvenido", "Welcome")
      : emailForm.isLinkSent() ? L10n::tr("Revisa tu correo", "Check your email")
      : emailForm.isResetMode() ? L10n::tr(u8"Tu contraseña", "Your password")
                               : L10n::tr(u8"Iniciar sesión", "Sign in"), juce::dontSendNotification);
  subtitle.setText(emailMode ? juce::String()
      : L10n::tr(u8"Accede a tu biblioteca y continúa con tu música.", "Sign in to your library and get back to your music."), juce::dontSendNotification);
}

void WelcomeModal::setStatusText(const juce::String& message, bool isError) {
  status.setText(message, juce::dontSendNotification);
  status.setColour(juce::Label::textColourId, isError ? juce::Colour(0xffffb4ab) : AuthUi::muted);
  resized();
}

void WelcomeModal::resetStatusToDefault() {
  setStatusText({}, false);
  setChecking(false);
  setAuthenticationBusy(false);
}

void WelcomeModal::setChecking(bool isChecking) {
  checking = isChecking;
  updateActionVisibility();
  resized();
}

void WelcomeModal::setAppleSignInAvailable(bool available) {
  if (available && !appleButton) {
    appleButton = NativeAppleSignIn::createButton([this] { if (onAppleLoginRequested) onAppleLoginRequested(); });
    if (appleButton) addAndMakeVisible(*appleButton);
  } else if (!available) appleButton.reset();
  updateActionVisibility();
  resized();
}

void WelcomeModal::setAuthenticationBusy(bool busy) {
  authenticationBusy = busy;
  loginButton.setEnabled(!busy);
  if (appleButton) appleButton->setEnabled(!busy);
  emailForm.setBusy(busy);
}

void WelcomeModal::clearCredentials() { emailForm.clearCredentials(); }
void WelcomeModal::visibilityChanged() { if (!isVisible()) clearCredentials(); }

void WelcomeModal::showPasswordLinkSent() {
  setStatusText({}, false);
  setAuthenticationBusy(false);
  emailForm.showPasswordLinkSent();
}

void WelcomeModal::showEmailForm(bool show) {
  if (onAuthenticationCancelled) onAuthenticationCancelled();
  setAuthenticationBusy(false);
  emailMode = show;
  if (show) emailForm.showLogin();
  else emailForm.clearCredentials();
  updateHeading();
  resetStatusToDefault();
  updateActionVisibility();
  resized();
  if (show) emailForm.focusEmail();
}

void WelcomeModal::updateActionVisibility() {
  loginButton.setVisible(!checking && !emailMode);
  emailButton.setVisible(!checking && !emailMode);
  separator.setVisible(!checking && !emailMode);
  emailForm.setVisible(!checking && emailMode);
  backButton.setVisible(!checking && emailMode);
  if (appleButton) {
    appleButton->setVisible(!checking && !emailMode);
    appleButton->setEnabled(!authenticationBusy);
  }
}

void WelcomeModal::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xff101713));
  juce::ColourGradient glow(AuthUi::accent.withAlpha(0.07f), (float)getWidth() * 0.3f, 0,
      juce::Colours::transparentBlack, (float)getWidth(), (float)getHeight(), true);
  g.setGradientFill(glow);
  g.fillRect(getLocalBounds());
  AuthUi::drawSurface(g, contentBounds.toFloat());
  if (separator.isVisible()) {
    const auto r = separator.getBounds().toFloat();
    g.setColour(AuthUi::border.withAlpha(0.65f));
    g.drawHorizontalLine((int)r.getCentreY(), r.getX(), r.getCentreX() - 22);
    g.drawHorizontalLine((int)r.getCentreY(), r.getCentreX() + 22, r.getRight());
  }
}

void WelcomeModal::resized() {
  const int outer = getWidth() < 600 ? 16 : 32;
  auto available = getLocalBounds().reduced(outer, 12);
  const int cardWidth = juce::jmax(1, juce::jmin(480, available.getWidth()));
  const int sidePadding = getWidth() < 600 ? 22 : 32;
  const bool hasStatus = status.getText().isNotEmpty();
  const int preferred = emailMode ? 48 + 44 + 20 + emailForm.preferredHeight(cardWidth - sidePadding * 2) + (hasStatus ? 68 : 0)
                                  : 414 + (appleButton ? 58 : 0) + (hasStatus ? 64 : 0);
  contentBounds = juce::Rectangle<int>(cardWidth,
      juce::jmin(preferred, available.getHeight())).withCentre(available.getCentre());
  // Changing steps moves the card without resizing this component. Invalidate
  // the parent background too, so old borders don't survive between children.
  repaint();
  auto area = contentBounds.reduced(sidePadding,
                                    emailMode ? 24 : contentBounds.getHeight() < 380 ? 14 : 28);
  logo.setVisible(!emailMode && contentBounds.getHeight() >= 380);
  if (logo.isVisible()) {
    logo.setBounds(area.removeFromTop(58));
    area.removeFromTop(18);
  }
  auto heading = area.removeFromTop(44);
  if (emailMode) {
    backButton.setBounds(heading.removeFromLeft(44));
    heading.removeFromRight(44);
    AuthUi::label(title, 22, true);
    title.setBounds(heading);
    area.removeFromTop(20);
  } else {
    AuthUi::label(title, 28, true);
    title.setBounds(heading);
    area.removeFromTop(4);
  }
  subtitle.setVisible(!emailMode && contentBounds.getHeight() >= 380);
  if (subtitle.isVisible()) {
    subtitle.setBounds(area.removeFromTop(46));
    area.removeFromTop(22);
  }
  status.setVisible(hasStatus);
  if (hasStatus) {
    status.setBounds(area.removeFromTop(56));
    area.removeFromTop(12);
  }
  if (emailMode) {
    emailForm.setBounds(area);
    return;
  }
  loginButton.setBounds(area.removeFromTop(48));
  if (appleButton) {
    area.removeFromTop(10);
    appleButton->setBounds(area.removeFromTop(48));
  }
  separator.setBounds(area.removeFromTop(34));
  emailButton.setBounds(area.removeFromTop(48));
}
