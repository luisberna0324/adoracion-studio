#pragma once

#include <JuceHeader.h>

struct FirebaseRefreshResult {
  bool ok = false;
  juce::String idToken;
  juce::String refreshToken;
  juce::String errorMessage;
};

struct ValidateAccessResult {
  bool httpOk = false;
  bool allowed = false;
  juce::String plan;
  juce::String subscriptionStatus;
  juce::String expiresAt;
  juce::String licenseKey;
  juce::String reasonCode;
  juce::String reason;
  juce::String rawError;
};

/** Llamadas de red para renovar Firebase y validar acceso (hilos de fondo). */
class AuthSessionApi {
public:
  static FirebaseRefreshResult refreshFirebaseIdToken(
      const juce::String &firebaseApiKey, const juce::String &refreshToken,
      int timeoutMs = 20000);

  static ValidateAccessResult fetchValidateAccess(const juce::String &apiBaseUrl,
                                                  const juce::String &idToken,
                                                  const juce::String &deviceId,
                                                  const juce::String &licenseKey,
                                                  int timeoutMs = 20000);
};
