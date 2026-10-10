#include "AccountDeletionModal.h"
#include "../../data/Localization.h"
AccountDeletionModal::AccountDeletionModal(juce::String uid) : accountUid(std::move(uid)) {
  AuthUi::label(title, 21, true); AuthUi::label(message, 14); AuthUi::label(status, 13);
  title.setText(L10n::tr("Eliminar cuenta", "Delete account"), juce::dontSendNotification);
  addAndMakeVisible(title); content.addAndMakeVisible(message); content.addAndMakeVisible(status);
  viewport.setViewedComponent(&content, false); viewport.setScrollBarsShown(true, false); addAndMakeVisible(viewport);
  message.setText(policyText(), juce::dontSendNotification);
  next.setComponentID("deletion-next"); cancel.setComponentID("deletion-cancel");
  cancel.setButtonText(L10n::tr("Cancelar", "Cancel"));
  addAndMakeVisible(next); addAndMakeVisible(cancel);
  cancel.onClick = [this] { if (state == State::submitting || state == State::pending || state == State::complete) return;
    invalidate(); };
  next.onClick = [this] {
    if (!next.isEnabled()) return;
    if (state == State::error) { requestPreview(); return; }
    if (state == State::pending) { next.setEnabled(false); if (onStatusRequested) onStatusRequested(generation); else fail(generation, accountUid); return; }
    if (state == State::review) { state = State::reauthenticating; update(); if (onReauthenticationRequested) onReauthenticationRequested(generation); else fail(generation, accountUid); return; }
    if (state == State::confirmation) {
      if (proof.isEmpty() || proofExpiresAtMs <= juce::Time::currentTimeMillis()) { proof.clear(); state = State::review; update(); return; }
      state = State::submitting; update();
      if (onConfirmationRequested) onConfirmationRequested(generation, proof); else fail(generation, accountUid);
    }
  };
  update();
}
AccountDeletionModal::~AccountDeletionModal() {
  ++generation;
  if (!dismissalHandled && onDismissed) onDismissed(state == State::submitting || state == State::pending || state == State::complete);
}
juce::String AccountDeletionModal::policyText() const {
  return L10n::tr("Se borrarán tu cuenta y todo tu contenido de Cloud. Si eres propietario de una nube compartida, sus miembros perderán el acceso. Se cancelará automáticamente la renovación de Cloud, sin reembolso automático. Tus proyectos, audio y copias descargadas permanecerán en este dispositivo. Los datos de otros propietarios no se borrarán. Los comprobantes obligatorios se tratarán según la política legal aplicable.",
    "Your account and all your Cloud content will be deleted. If you own a shared Cloud, its members will lose access. Cloud renewal will be cancelled automatically, with no automatic refund. Your projects, audio and downloaded copies will remain on this device. Other owners' data will be preserved. Required accounting records will be handled under the applicable legal policy.");
}
bool AccountDeletionModal::accepts(int request, const juce::String &uid) const { return request == generation && uid == accountUid && state != State::closed; }
void AccountDeletionModal::invalidate() {
  if (!dismissalHandled && onDismissed) onDismissed(state == State::submitting || state == State::pending || state == State::complete);
  dismissalHandled = true; ++generation; state = State::closed; proof.clear(); proofExpiresAtMs = 0; update(); if (onCancelled) onCancelled(); }
void AccountDeletionModal::resumePendingStatus() { ++generation; state = State::pending; update(); if (onStatusRequested) onStatusRequested(generation); }
void AccountDeletionModal::requestPreview() { if (state == State::pending || state == State::submitting || state == State::complete || state == State::closed) return; ++generation; proof.clear(); preview = {}; state = State::loading; update(); if (onPreviewRequested) onPreviewRequested(generation); else fail(generation, accountUid); }
void AccountDeletionModal::receivePreview(int request, const juce::String &uid, AccountDeletionPreview value) {
  if (!accepts(request, uid) || state != State::loading) return;
  if (!value.ok) { fail(request, uid); return; } preview = value; state = State::review; update();
}
void AccountDeletionModal::receiveReauthentication(int request, const juce::String &uid, const juce::String &value, juce::int64 expiry) {
  if (!accepts(request, uid) || state != State::reauthenticating) return;
  if (value.isEmpty() || expiry <= juce::Time::currentTimeMillis()) { fail(request, uid); return; }
  proof = value; proofExpiresAtMs = expiry; state = State::confirmation; update();
}
void AccountDeletionModal::receiveStatus(int request, const juce::String &uid, State verified) {
  if (!accepts(request, uid) || (state != State::submitting && state != State::pending)) return;
  if (verified != State::pending && verified != State::complete) return;
  if (!acceptanceHandled && onAccepted) {
    if (!onAccepted()) { state = State::pending; update(); return; }
    acceptanceHandled = true;
  }
  state = verified; if (state == State::complete) { proof.clear(); proofExpiresAtMs = 0; } update();
}
void AccountDeletionModal::fail(int request, const juce::String &uid) {
  if (!accepts(request, uid)) return;
  // A lost response after confirmation is uncertain, never a fresh deletion request.
  if (state == State::submitting || state == State::pending) state = State::pending;
  else { state = State::error; proof.clear(); proofExpiresAtMs = 0; }
  update();
}
ReauthPromptModal *AccountDeletionModal::providerForm() {
  if (state != State::reauthenticating) return nullptr;
  if (!authentication) { authentication = std::make_unique<ReauthPromptModal>(); content.addAndMakeVisible(*authentication);
    authentication->onPreferredHeightChanged = [this] { resized(); }; }
  update(); return authentication.get();
}
void AccountDeletionModal::update() {
  const bool showForm = state == State::reauthenticating && authentication != nullptr;
  message.setVisible(!showForm); status.setVisible(!showForm);
  if (authentication) { authentication->setVisible(showForm); if (!showForm) authentication->clearCredentials(); }
  juce::String text;
  switch (state) {
    case State::loading: text = L10n::tr("Consultando el impacto…", "Checking the impact…"); break;
    case State::review: text = L10n::tr("Canciones de Cloud: ", "Cloud songs: ") + juce::String(preview.cloudSongs);
      if (preview.ownsSharedCloud) text += L10n::tr(". Miembros que perderán acceso: ", ". Members losing access: ") + juce::String(preview.sharedMembers);
      if (!preview.executionAvailable) text += L10n::tr(". La eliminación aún no está habilitada; no se ha borrado nada.", ". Deletion is not enabled yet; nothing has been deleted."); break;
    case State::reauthenticating: text = L10n::tr("Confirma tu identidad con la misma cuenta.", "Confirm your identity with the same account."); break;
    case State::confirmation: text = L10n::tr("Al confirmar se perderá el acceso a Cloud de inmediato, aunque quede tiempo pagado. No hay reembolso automático.", "Confirming removes Cloud access immediately, even if paid time remains. There is no automatic refund."); break;
    case State::submitting: text = L10n::tr("Enviando solicitud…", "Sending request…"); break;
    case State::pending: text = L10n::tr("La eliminación sigue pendiente. No se ha confirmado que terminó. Consultaremos su estado antes de reintentar.", "Deletion is still pending. Completion has not been confirmed. We will check its status before retrying."); break;
    case State::complete: text = L10n::tr("La cuenta y su Cloud se eliminaron. Los archivos locales se conservaron.", "The account and its Cloud were deleted. Local files were preserved."); break;
    case State::error: text = L10n::tr("No se pudo completar la consulta o confirmar tu identidad. Intenta de nuevo.", "Could not finish the lookup or confirm your identity. Try again."); break;
    case State::closed: break;
  }
  status.setText(text, juce::dontSendNotification);
  next.setButtonText(state == State::confirmation ? L10n::tr("Eliminar cuenta y Cloud", "Delete account and Cloud") :
    state == State::pending ? L10n::tr("Consultar estado", "Check status") : state == State::error ? L10n::tr("Reintentar", "Retry") : L10n::tr("Confirmar identidad", "Confirm identity"));
  next.setEnabled((state == State::review && preview.executionAvailable) || state == State::confirmation || state == State::pending || state == State::error);
  cancel.setEnabled(state != State::submitting && state != State::pending && state != State::complete && state != State::closed);
  resized(); repaint();
}
void AccountDeletionModal::paint(juce::Graphics &g) { AuthUi::drawSurface(g, getLocalBounds().toFloat()); }
void AccountDeletionModal::resized() {
  auto area = getLocalBounds().reduced(22); title.setBounds(area.removeFromTop(34)); area.removeFromTop(12);
  cancel.setBounds(area.removeFromBottom(44)); area.removeFromBottom(8);
  next.setBounds(area.removeFromBottom(48)); area.removeFromBottom(12);
  viewport.setBounds(area);
  const auto width = juce::jmax(1, area.getWidth() - viewport.getScrollBarThickness());
  if (authentication && state == State::reauthenticating) {
    const auto height = authentication->preferredHeight(); content.setSize(width, height);
    authentication->setBounds(0, 0, width, height); return;
  }
  const auto messageHeight = message.heightForWidth(width), statusHeight = status.heightForWidth(width);
  content.setSize(width, messageHeight + statusHeight + 16);
  message.setBounds(0, 0, width, messageHeight); status.setBounds(0, messageHeight + 16, width, statusHeight);
}
