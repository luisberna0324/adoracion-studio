#pragma once

#include <JuceHeader.h>

/** Snapshot de sesion en disco (MTControls/session_snapshot.json). */
struct PersistedSessionSnapshot {
  juce::String uid;
  juce::String email;
  // Verified provider metadata only; never Apple access/refresh tokens.
  juce::String authProvider;
  juce::String authProviderUser;
  juce::String plan;
  juce::String subscriptionStatus;
  juce::String expiresAt;
  juce::String licenseKey;
  bool allowed = false;
  juce::String reasonCode;
  juce::String reason;
  juce::String refreshToken;
  juce::String firebaseApiKey;
  juce::int64 idTokenExpiresAtMs = 0;
  /** Ultima vez que hubo login web o validate-access exitoso con red. */
  juce::int64 lastOnlineValidationMs = 0;

  bool hasCredentials() const {
    return refreshToken.isNotEmpty() && firebaseApiKey.isNotEmpty();
  }
};

class SessionPersistence {
public:
  static juce::File getSnapshotFile();

  static bool load(PersistedSessionSnapshot &out);
  static bool save(const PersistedSessionSnapshot &snapshot);
  static void clear();
  /** After server acceptance only. This marker preserves projects/audio, but
   * rejects stale/offline credentials for this UID across app restarts. */
  static bool markAccountDeletionPending(const juce::String &uid);
  static bool isAccountDeletionPending(const juce::String &uid);
  // Persist before submitting; independent of login snapshot and logout.
  // A different receipt cannot replace an uncertain request for the same UID.
  static bool saveAccountDeletionReceipt(const juce::String &uid, const juce::String &receipt);
  static juce::String loadAccountDeletionReceipt(const juce::String &uid);
  static juce::StringArray listAccountDeletionReceipts();
  static bool markAccountDeletionReceiptSubmitted(const juce::String &uid, const juce::String &receipt);
  static bool clearUnsubmittedDeletionReceipt(const juce::String &receipt);
  static bool acknowledgeCompletedDeletionReceipt(const juce::String &receipt);
  static bool blockAccountForDeletionReceipt(const juce::String &receipt);
  static bool clearAccountDeletionReceipt(const juce::String &uid, const juce::String &expectedReceipt);
};
