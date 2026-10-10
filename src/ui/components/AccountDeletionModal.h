#pragma once
#include "AuthenticationStyle.h"
#include "../../data/AccountDeletionApi.h"
/** Reviewable native flow. Production currently supplies preview only; destructive
 * callbacks remain unbound until the server executor/status flow is approved. */
class AccountDeletionModal final : public juce::Component {
public:
  enum class State { loading, review, reauthenticating, confirmation, submitting, pending, complete, error, closed };
  explicit AccountDeletionModal(juce::String uid);
  std::function<void(int)> onPreviewRequested, onReauthenticationRequested, onStatusRequested;
  std::function<void(int, const juce::String &)> onConfirmationRequested;
  std::function<void()> onCancelled;
  std::function<bool()> onAccepted; // Persist UID block and clear session after verified acceptance.
  void requestPreview();
  void invalidate();
  void receivePreview(int generation, const juce::String &uid, AccountDeletionPreview);
  void receiveReauthentication(int generation, const juce::String &uid, const juce::String &proof, juce::int64 expiresAtMs);
  void receiveStatus(int generation, const juce::String &uid, State verifiedState);
  void fail(int generation, const juce::String &uid);
  State getState() const { return state; }
  int getGeneration() const { return generation; }
  juce::String policyText() const;
  void paint(juce::Graphics &) override;
  void resized() override;
private:
  bool accepts(int, const juce::String &) const;
  void update();
  juce::String accountUid, proof;
  juce::int64 proofExpiresAtMs = 0;
  int generation = 0;
  bool acceptanceHandled = false;
  State state = State::loading;
  AccountDeletionPreview preview;
  juce::Label title;
  AuthUi::Paragraph message, status;
  juce::Component content;
  juce::Viewport viewport;
  AuthUi::Button next{AuthUi::Role::primary}, cancel{AuthUi::Role::secondary};
};
