#include "data/AppleSignInApi.h"
#include <iostream>
#include <stdexcept>

namespace {
using Api = AppleSignInApi;
constexpr juce::int64 nowMs = 1800000000000LL;
const juce::String state = "opaque-state-0123456789-abcdef";
const juce::String nonce = "01234567890123456789012345678901";

void expect(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

Api::Context context(bool link = false) {
  Api::Context value;
  value.apiBaseUrl = "https://api.playworship.app/";
  value.deviceId = "contract-test-device";
  if (link) {
    value.purpose = Api::Purpose::link;
    value.firebaseIdToken = "test-firebase-bearer";
    value.expectedUid = "existing-account";
  }
  return value;
}

juce::var challengeBody() {
  auto* object = new juce::DynamicObject();
  object->setProperty("ok", true);
  object->setProperty("challengeId", "challenge-0123456789abcdef");
  object->setProperty("state", state);
  object->setProperty("nonce", nonce);
  object->setProperty("expiresAtMs", nowMs + 120000);
  return juce::var(object);
}

Api::Challenge challenge(const Api::Context& ctx) {
  const auto parsed = Api::parseChallengeResponse(challengeBody(), ctx, nowMs);
  expect(parsed.ok(), "a valid backend challenge parses");
  return parsed.challenge;
}

NativeAppleSignIn::Credential credential() {
  NativeAppleSignIn::Credential value;
  value.state = state;
  value.identityToken = "test.apple.identity";
  value.authorizationCode = "test-apple-code";
  value.appleUser = "apple-subject-not-firebase-uid";
  return value;
}

juce::var sessionBody(const juce::String& uid, bool allowed = true) {
  auto* session = new juce::DynamicObject();
  session->setProperty("uid", uid);
  session->setProperty("authProvider", "apple.com");
  session->setProperty("authProviderUser", "verified-apple-subject");
  session->setProperty("email", "canonical@example.test");
  session->setProperty("plan", "trial");
  session->setProperty("subscriptionStatus", "trial");
  session->setProperty("allowed", allowed);
  session->setProperty("refreshToken", "test-firebase-refresh");
  session->setProperty("firebaseApiKey", "test-firebase-public-api-key");
  session->setProperty("idTokenExpiresAtMs", nowMs + 3600000);
  auto* response = new juce::DynamicObject();
  response->setProperty("ok", true);
  response->setProperty("session", juce::var(session));
  return juce::var(response);
}

void challengeRequiresFreshOpaqueProof() {
  const auto prepared = challenge(context()).nativeRequest();
  expect(prepared.nonceHash == "861009ec4d599fab1f40abc76e6f89880cff5833c79c548c99f9045f191cd90b",
         "native nonce is the standard lowercase SHA256 of the raw challenge nonce");
  for (const auto malformed : {juce::var("1800000120000"), juce::var(true),
                               juce::var(static_cast<double>(nowMs + 120000))}) {
    auto body = challengeBody();
    body.getDynamicObject()->setProperty("expiresAtMs", malformed);
    expect(!Api::parseChallengeResponse(body, context(), nowMs).ok(),
           "challenge expiration must be integer epoch milliseconds");
  }
  auto body = challengeBody();
  body.getDynamicObject()->setProperty("expiresAtMs", nowMs);
  expect(Api::parseChallengeResponse(body, context(), nowMs).error == Api::Error::expiredChallenge,
         "expired challenges do not open the native sheet");
  body = challengeBody();
  body.getDynamicObject()->setProperty("expiresAtMs", nowMs + 600001);
  expect(!Api::parseChallengeResponse(body, context(), nowMs).ok(),
         "an unbounded challenge lifetime is rejected");
  body = challengeBody();
  body.getDynamicObject()->setProperty("nonce", "weak-nonce");
  expect(!Api::parseChallengeResponse(body, context(), nowMs).ok(),
         "short or malformed nonce cannot become a native authorization request");
}

void proofsAndLinkBearerStayOutOfUrls() {
  const auto ctx = context(true);
  auto proof = credential();
  proof.authorizationCode = "opaque-code&query=never-in-url";
  proof.email = "relay@private.icloud.com";
  proof.fullName = "Nombre de prueba";
  const auto request = Api::makeNativeRequest(ctx, challenge(ctx), proof, nowMs);
  expect(request.ok() && request.method == "POST" &&
             request.url == "https://api.playworship.app/api/auth/apple/native" &&
             !request.url.contains(proof.identityToken) && !request.url.contains(proof.authorizationCode),
         "Apple proofs travel in POST JSON to an exact HTTPS endpoint");
  const auto body = juce::JSON::parse(request.jsonBody);
  expect(body.getProperty("authorizationCode", {}).toString() == proof.authorizationCode &&
             body.getProperty("profile", {}).getProperty("email", {}).toString() == proof.email &&
             body.getProperty("profile", {}).getProperty("fullName", {}).toString() == proof.fullName,
         "opaque proof and optional profile survive JSON encoding");
  expect(!request.jsonBody.contains(ctx.firebaseIdToken) && !request.jsonBody.contains(ctx.expectedUid) &&
             !request.jsonBody.contains(nonce) && !request.jsonBody.contains(proof.appleUser) &&
             !body.hasProperty("allowed") && !body.hasProperty("plan") && !body.hasProperty("purpose"),
         "client cannot nominate a Firebase UID, purpose or access grants in the exchange");
  expect(request.headers.contains("Authorization: Bearer " + ctx.firebaseIdToken) &&
             Api::makeChallengeRequest(ctx).headers.contains("Authorization: Bearer " + ctx.firebaseIdToken),
         "explicit linking authenticates both requests using the existing Firebase session");
  proof = credential();
  expect(!juce::JSON::parse(Api::makeNativeRequest(ctx, challenge(ctx), proof, nowMs).jsonBody)
              .hasProperty("profile"),
         "repeat Apple authorizations do not invent missing profile fields");
}

void exchangeRejectsWrongAttempt() {
  const auto ctx = context();
  auto proof = credential();
  proof.state = "another-state-0123456789abcdef";
  expect(Api::makeNativeRequest(ctx, challenge(ctx), proof, nowMs).error == Api::Error::stateMismatch,
         "a proof for a different authorization is never posted");
  proof = credential();
  expect(Api::makeNativeRequest(ctx, challenge(ctx), proof, nowMs + 120000).error ==
             Api::Error::expiredChallenge,
         "expiration is checked again after the user closes the native sheet");
  auto otherDevice = ctx;
  otherDevice.deviceId = "different-device";
  expect(!Api::makeNativeRequest(otherDevice, challenge(ctx), proof, nowMs).ok(),
         "challenge is not reused across a different device context");
  expect(!Api::makeNativeRequest(context(true), challenge(ctx), proof, nowMs).ok(),
         "a sign-in challenge cannot be upgraded to a linking request");
  proof.identityToken.clear();
  expect(!Api::makeNativeRequest(ctx, challenge(ctx), proof, nowMs).ok(),
         "missing Apple token cannot produce a partial login request");
}

void originsAndHeadersFailClosed() {
  for (const auto base : {"http://api.playworship.app", "https://user@api.playworship.app",
                          "https://api.playworship.app/path", "https://api.playworship.app?token=x",
                          "https://api.playworship.app#fragment", "https://api.playworship.app\r\nInjected:x"}) {
    auto ctx = context();
    ctx.apiBaseUrl = base;
    expect(!Api::makeChallengeRequest(ctx).ok(),
           "non-HTTPS, userinfo, query, fragment and header-injection origins are rejected");
  }
  auto ctx = context(true);
  ctx.firebaseIdToken += "\r\nInjected: value";
  expect(!Api::makeChallengeRequest(ctx).ok(), "Bearer cannot inject another HTTP header");
  ctx = context();
  ctx.firebaseIdToken = "unexpected-bearer";
  expect(!Api::makeChallengeRequest(ctx).ok(),
         "an authenticated identity must use explicit linking, not implicit email association");
}

void serverSessionPreservesUidAndDenials() {
  const auto ctx = context(true);
  const auto linked = Api::parseSessionResponse(sessionBody(ctx.expectedUid), ctx, nowMs);
  expect(linked.ok() && linked.session.uid == ctx.expectedUid &&
             linked.session.email == "canonical@example.test" && linked.session.hasCredentials(),
         "the backend's canonical Firebase account and session are returned unchanged");
  const auto wrongUid = Api::parseSessionResponse(sessionBody("another-account"), ctx, nowMs);
  expect(wrongUid.error == Api::Error::identityMismatch && !wrongUid.session.hasCredentials(),
         "linking never hands a different UID's credentials to the editor");
  const auto denied = Api::parseSessionResponse(sessionBody(ctx.expectedUid, false), ctx, nowMs);
  expect(denied.ok() && !denied.session.allowed,
         "a valid authentication response cannot turn backend access denial into a grant");
  auto body = sessionBody(ctx.expectedUid);
  body.getDynamicObject()->getProperty("session").getDynamicObject()->setProperty("allowed", "false");
  expect(!Api::parseSessionResponse(body, ctx, nowMs).ok(),
         "string false is not coerced to a true access flag");
  body = sessionBody(ctx.expectedUid);
  body.getDynamicObject()->getProperty("session").getDynamicObject()->setProperty("refreshToken", "");
  expect(!Api::parseSessionResponse(body, ctx, nowMs).ok(), "partial Firebase sessions are rejected");
}

void errorsAndConfigurationRemainBounded() {
  expect(!Api::parseConfiguration(juce::JSON::parse(R"({"enabled":"true","nativeClientId":"com.multitracks.playworship"})")).enabled,
         "configuration does not enable Apple using a truthy string");
  const auto disabled = Api::parseConfiguration(juce::JSON::parse(R"({"enabled":false,"nativeClientId":"com.multitracks.playworship"})"));
  expect(disabled.ok() && !disabled.enabled, "a disabled backend remains disabled");
  expect(!Api::parseConfiguration(juce::JSON::parse(R"({"enabled":true})")).enabled,
         "enabled configuration without an audience cannot activate native UI");
  const auto body = juce::JSON::parse(R"({"ok":false,"error":"AppleAccountLinkRequired","message":"untrusted provider details"})");
  const auto failure = Api::parseSessionResponse(body, context(), nowMs);
  expect(failure.error == Api::Error::linkRequired && !failure.session.hasCredentials(),
         "known backend errors expose only a typed outcome and no credentials");
  expect(Api::parseSessionResponse(juce::JSON::parse(R"({"ok":false,"error":"provider-secret-details"})"),
                                  context(), nowMs).error == Api::Error::backendRejected,
         "unknown backend strings do not become UI messages or logs");
}
} // namespace

int main() {
  try {
    challengeRequiresFreshOpaqueProof();
    proofsAndLinkBearerStayOutOfUrls();
    exchangeRejectsWrongAttempt();
    originsAndHeadersFailClosed();
    serverSessionPreservesUidAndDenials();
    errorsAndConfigurationRemainBounded();
    expect(!NativeAppleSignIn::requiresFreshLogin(NativeAppleSignIn::CredentialState::authorized), "authorized credential keeps session");
    expect(!NativeAppleSignIn::requiresFreshLogin(NativeAppleSignIn::CredentialState::unavailable), "temporary OS errors do not sign out");
    expect(NativeAppleSignIn::requiresFreshLogin(NativeAppleSignIn::CredentialState::revoked), "revoked credential requires fresh login");
    expect(NativeAppleSignIn::requiresFreshLogin(NativeAppleSignIn::CredentialState::notFound), "missing credential requires fresh login");
    expect(NativeAppleSignIn::requiresFreshLogin(NativeAppleSignIn::CredentialState::transferred), "transferred credential requires fresh login");
    std::cout << "Apple sign-in contract tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Apple sign-in contract test failed: " << error.what() << '\n';
    return 1;
  }
}
