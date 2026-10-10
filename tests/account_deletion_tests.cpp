#include "ui/components/AccountDeletionModal.h"
#include "data/AppSettings.h"
#include "data/SessionPersistence.h"
#include <iostream>
#include <stdexcept>
int checks = 0;
void expect(bool value, const char *text) { ++checks; if (!value) throw std::runtime_error(text); }
juce::Button &action(AccountDeletionModal &modal, const char *id) {
  for (auto *component : modal.getChildren()) if (component->getComponentID() == id) return *dynamic_cast<juce::Button *>(component);
  throw std::runtime_error("missing deletion action");
}
void click(juce::Button &button) { if (button.isEnabled() && button.onClick) button.onClick(); }
int main() {
  if (juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_CREATION_TEST_ROOT", {}).isEmpty()) return 77;
  try {
    juce::ScopedJuceInitialiser_GUI gui;
    using State = AccountDeletionModal::State;
    for (auto language : {AppLanguage::Spanish, AppLanguage::English}) {
      AppSettings::setLanguage(language);
      for (const auto size : {juce::Point<int>{320, 620}, {390, 700}, {568, 244}, {844, 620}}) {
        AccountDeletionModal modal("owner"); modal.setVisible(true); modal.setSize(size.x, size.y);
        int request = -1; modal.onPreviewRequested = [&](int generation) { request = generation; };
        modal.requestPreview();
        modal.receivePreview(request, "foreign", {true, true, true, 12, 4});
        expect(modal.getState() == State::loading, "foreign UID preview must not change state");
        modal.receivePreview(request, "owner", {true, false, true, 12, 4});
        expect(modal.getState() == State::review, "valid impact preview shows review");
        expect(!action(modal, "deletion-next").isEnabled(), "disabled production execution must not offer deletion");
        expect(modal.getLocalBounds().contains(action(modal, "deletion-next").getBounds()), "primary action contained at responsive sizes");
        expect(modal.getLocalBounds().contains(action(modal, "deletion-cancel").getBounds()), "cancel contained at responsive sizes");
        expect(action(modal, "deletion-next").getHeight() >= 44, "primary touch target at least 44");
        const auto copy = modal.policyText();
        expect(copy.contains(language == AppLanguage::Spanish ? "miembros" : "members"), "copy explains member access loss");
        expect(copy.contains(language == AppLanguage::Spanish ? "sin reembolso" : "no automatic refund"), "copy explains billing/refund");
        expect(copy.contains(language == AppLanguage::Spanish ? "permanecerán" : "remain"), "copy preserves local data");
        click(action(modal, "deletion-cancel"));
        modal.receivePreview(request, "owner", {true, true, true, 12, 4});
        expect(modal.getState() == State::closed, "cancel invalidates late callback");
      }
    }
    AccountDeletionModal modal("owner"); modal.setVisible(true); modal.setSize(390, 700);
    int request = 0, reauths = 0, confirmations = 0, statuses = 0, acceptances = 0;
    modal.onAccepted = [&] { ++acceptances; return true; };
    modal.onPreviewRequested = [&](int value) { request = value; };
    modal.onReauthenticationRequested = [&](int value) { ++reauths; expect(value == request, "reauth keeps attempt generation"); };
    modal.onConfirmationRequested = [&](int, const juce::String &proof) { ++confirmations; expect(proof == "simulated-proof", "confirmation reuses exact proof"); };
    modal.onStatusRequested = [&](int) { ++statuses; };
    modal.requestPreview(); modal.receivePreview(request, "owner", {true, true, true, 3, 2});
    click(action(modal, "deletion-next")); click(action(modal, "deletion-next"));
    expect(reauths == 1 && modal.getState() == State::reauthenticating, "double click reauth dispatched once");
    modal.receiveReauthentication(request, "foreign", "proof", juce::Time::currentTimeMillis() + 300000);
    expect(modal.getState() == State::reauthenticating, "foreign reauth never confirms or replaces account");
    modal.receiveReauthentication(request, "owner", "simulated-proof", juce::Time::currentTimeMillis() + 300000);
    click(action(modal, "deletion-next")); click(action(modal, "deletion-next"));
    expect(confirmations == 1 && modal.getState() == State::submitting, "double click cannot submit twice");
    modal.fail(request, "owner"); expect(modal.getState() == State::pending, "uncertain response stays pending, never success");
    expect(acceptances == 0, "uncertain response does not clear original session before verified acceptance");
    click(action(modal, "deletion-next")); click(action(modal, "deletion-next"));
    expect(statuses == 1 && confirmations == 1, "retry queries status instead of starting another job");
    modal.receiveStatus(request, "owner", State::pending);
    expect(action(modal, "deletion-next").isEnabled(), "pending status allows another lookup");
    expect(acceptances == 1, "verified acceptance triggers credential cleanup");
    modal.receiveStatus(request, "foreign", State::complete);
    expect(modal.getState() == State::pending, "wrong account cannot claim completion");
    modal.receiveStatus(request, "owner", State::complete);
    expect(modal.getState() == State::complete && !action(modal, "deletion-next").isEnabled(), "only verified completion displays success");
    expect(acceptances == 1, "repeated accepted/completed responses do not duplicate credential cleanup");
    AccountDeletionModal stale("owner"); stale.setVisible(true); stale.setSize(390, 700);
    stale.onPreviewRequested = [&](int value) { request = value; }; stale.onReauthenticationRequested = [](int) {};
    stale.requestPreview(); auto old = request; stale.fail(request, "owner"); click(action(stale, "deletion-next"));
    stale.receivePreview(old, "owner", {true, true, false, 0, 0}); expect(stale.getState() == State::loading, "old response ignored after retry");
    stale.receivePreview(request, "owner", {true, true, false, 0, 0}); click(action(stale, "deletion-next"));
    stale.receiveReauthentication(request, "owner", "expired", 1); expect(stale.getState() == State::error, "expired proof cannot delete");
    auto response = juce::JSON::parse(R"({"ok":true,"uid":"owner","executionAvailable":false,"impact":{"cloudSongs":3,"sharedMembers":2,"ownsSharedCloud":true,"localFilesPreserved":true}})");
    expect(AccountDeletionApi::parsePreview(response, "owner").ok, "parse scoped backend preview");
    expect(!AccountDeletionApi::parsePreview(response, "foreign").ok, "parser rejects account mismatch");
    response.getDynamicObject()->setProperty("executionAvailable", "true");
    expect(!AccountDeletionApi::parsePreview(response, "owner").ok, "parser rejects malformed execution availability");
    PersistedSessionSnapshot session; session.uid = "deletion-fixture"; session.refreshToken = "fake-refresh"; session.firebaseApiKey = "fake-api-key";
    session.allowed = true; expect(SessionPersistence::save(session), "fixture session saves before deletion acceptance");
    const auto localProject = SessionPersistence::getSnapshotFile().getSiblingFile("keep-local.pwproj"); localProject.replaceWithText("local fixture");
    expect(SessionPersistence::markAccountDeletionPending(session.uid), "acceptance marker persists before clearing credentials");
    PersistedSessionSnapshot restored; expect(!SessionPersistence::load(restored), "restart cannot restore deletion-pending credentials");
    expect(!SessionPersistence::save(session), "late login/refresh callback cannot persist blocked credentials");
    expect(localProject.loadFileAsString() == "local fixture", "local files are preserved");
    session.uid = "another-account"; expect(SessionPersistence::save(session), "marker is scoped to UID, other accounts unaffected");
    expect(SessionPersistence::load(restored) && restored.uid == session.uid, "other account restores normally");
    SessionPersistence::clear();
    std::cout << "Account deletion: " << checks << " checks passed (simulated, no network/destructive adapters)\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
