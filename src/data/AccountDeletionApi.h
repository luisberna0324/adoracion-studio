#pragma once
#include <JuceHeader.h>
struct AccountDeletionPreview {
  bool ok = false, executionAvailable = false, ownsSharedCloud = false;
  int cloudSongs = 0, sharedMembers = 0;
};
struct AccountDeletionReauthentication {
  bool ok = false, executionAvailable = false;
  juce::String proofId, receipt;
  juce::int64 expiresAtMs = 0;
};
struct AccountDeletionStatus {
  bool ok = false;
  juce::String state;
  int retryAfterSeconds = 0;
};
class AccountDeletionApi {
public:
  static AccountDeletionReauthentication parseReauthentication(const juce::var &, juce::int64 nowMs);
  static AccountDeletionStatus parseStatus(const juce::var &);
  static AccountDeletionReauthentication reauthenticate(const juce::String &base, const juce::String &currentIdToken,
      const juce::String &freshIdToken);
  static AccountDeletionStatus request(const juce::String &base, const juce::String &freshIdToken, const juce::String &proofId, const juce::String &receipt);
  static AccountDeletionStatus continuePending(const juce::String &base, const juce::String &receipt);
  static AccountDeletionStatus status(const juce::String &base, const juce::String &receipt);
  static AccountDeletionPreview parsePreview(const juce::var &, const juce::String &expectedUid);
  // Blocking, background thread only; existing Firebase refresh/auth architecture.
  static AccountDeletionPreview preview(const juce::String &base, const juce::String &apiKey,
      const juce::String &refreshToken, const juce::String &expectedUid);
};
