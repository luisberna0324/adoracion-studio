#pragma once
#include <JuceHeader.h>
struct AccountDeletionPreview {
  bool ok = false, executionAvailable = false, ownsSharedCloud = false;
  int cloudSongs = 0, sharedMembers = 0;
};
class AccountDeletionApi {
public:
  static AccountDeletionPreview parsePreview(const juce::var &, const juce::String &expectedUid);
  // Blocking, background thread only; existing Firebase refresh/auth architecture.
  static AccountDeletionPreview preview(const juce::String &base, const juce::String &apiKey,
      const juce::String &refreshToken, const juce::String &expectedUid);
};
