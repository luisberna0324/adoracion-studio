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
};
