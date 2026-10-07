#include "data/EmailPasswordAuthApi.h"
#include <iostream>
#include <stdexcept>

namespace {
using Api = EmailPasswordAuthApi;
constexpr juce::int64 now = 1800000000000LL;
void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
juce::var response(bool allowed = true) {
  auto* session = new juce::DynamicObject();
  session->setProperty("uid", "same-existing-google-uid");
  session->setProperty("email", "member@example.test");
  session->setProperty("plan", "trial");
  session->setProperty("subscriptionStatus", "trial");
  session->setProperty("allowed", allowed);
  session->setProperty("refreshToken", "fixture-refresh-token");
  session->setProperty("firebaseApiKey", "fixture-public-key");
  session->setProperty("idTokenExpiresAtMs", now + 3600000);
  auto* body = new juce::DynamicObject();
  body->setProperty("ok", true);
  body->setProperty("session", juce::var(session));
  return juce::var(body);
}

void requestKeepsCredentialsInHttpsJson() {
  const juce::String password = "  fixture&\"secret\\value  ";
  auto request = Api::makeLoginRequest("https://api.playworship.app/",
                                     " MEMBER@EXAMPLE.TEST ", password, "fixture-device");
  const auto body = juce::JSON::parse(request.jsonBody);
  expect(request.ok() && request.url == "https://api.playworship.app/api/auth/login",
         "credential POST uses exact HTTPS endpoint without query or callback");
  expect(body.getProperty("password", {}).toString() == password &&
         body.getProperty("email", {}).toString() == "member@example.test",
         "email normalizes, password whitespace and escaping remain significant");
  expect(!request.url.contains(password) && !body.hasProperty("uid") && !body.hasProperty("allowed"),
         "client cannot submit identity or access grants");
  for (const auto origin : {"http://api.playworship.app", "https://user@api.playworship.app",
                           "https://api.playworship.app/path", "https://api.playworship.app?x=1",
                           "https://api.playworship.app#fragment", "https://api.playworship.app\r\nInjected:x"}) {
    expect(!Api::makeLoginRequest(origin, "member@example.test", password, "device").ok() &&
           !Api::makeResetRequest(origin, "member@example.test").ok(),
           "insecure and injected origins fail closed for both auth operations");
  }
  for (const auto email : {"", "not-an-email", "@example.test", "member@", "a@b@c", "a@b\r\nX:y"})
    expect(!Api::makeResetRequest("https://api.playworship.app", email).ok(),
           "malformed addresses are not submitted");
  expect(!Api::makeLoginRequest("https://api.playworship.app", "a@b.test", "", "device").ok(),
         "blank passwords cannot launch a network request");
}

void sessionPreservesIdentityAndAccessRules() {
  const auto parsed = Api::parseLoginResponse(response(), now);
  expect(parsed.ok() && parsed.session.uid == "same-existing-google-uid" && parsed.session.allowed,
         "server's canonical UID is kept without fabricating another account");
  auto nullable = response();
  const auto nullValue = juce::JSON::parse("{\"expiresAt\":null}").getProperty("expiresAt", {});
  nullable.getDynamicObject()->getProperty("session").getDynamicObject()->setProperty("expiresAt", nullValue);
  nullable.getDynamicObject()->getProperty("session").getDynamicObject()->setProperty("licenseKey", nullValue);
  expect(Api::parseLoginResponse(nullable, now).ok(), "Firebase free/trial JSON null fields remain optional");
  expect(Api::parseLoginResponse(response(false), now).ok() &&
         !Api::parseLoginResponse(response(false), now).session.allowed,
         "authentication never changes a license denial into an access grant");
  for (const auto bad : {juce::var("true"), juce::var(1), juce::var{}}) {
    auto body = response();
    body.getDynamicObject()->getProperty("session").getDynamicObject()->setProperty("allowed", bad);
    expect(!Api::parseLoginResponse(body, now).ok(), "access grant must be a JSON boolean");
  }
  for (const auto bad : {juce::var("1800003600000"), juce::var(now), juce::var(now + 86400001)}) {
    auto body = response();
    body.getDynamicObject()->getProperty("session").getDynamicObject()->setProperty("idTokenExpiresAtMs", bad);
    expect(!Api::parseLoginResponse(body, now).ok(), "token expiry is bounded future epoch milliseconds");
  }
  auto body = response();
  body.getDynamicObject()->getProperty("session").getDynamicObject()->removeProperty("refreshToken");
  expect(!Api::parseLoginResponse(body, now).ok(), "partial sessions cannot enter the app");
  body = response();
  body.getDynamicObject()->getProperty("session").getDynamicObject()->setProperty("plan", "");
  expect(!Api::parseLoginResponse(body, now).ok(), "allowed sessions require plan and subscription status");
}

void passwordLinksAndErrorsDoNotDiscloseAccountState() {
  const auto request = Api::makeResetRequest("https://api.playworship.app", " MEMBER@example.test ");
  const auto body = juce::JSON::parse(request.jsonBody);
  expect(request.ok() && body.getDynamicObject()->getProperties().size() == 1 &&
         body.getProperty("email", {}).toString() == "member@example.test",
         "password request sends only an address, no UID or client-assigned credential");
  expect(Api::parseResetResponse(juce::JSON::parse("{\"ok\":true,\"message\":\"untrusted text\"}")).ok(),
         "reset result only consumes success flag; UI supplies fixed generic text");
  expect(!Api::parseResetResponse(juce::JSON::parse("{\"ok\":\"true\"}")).ok(),
         "truthy strings are not accepted as successful delivery");
  expect(Api::responseError(juce::JSON::parse("{\"error\":\"EMAIL_NOT_FOUND private debug details\"}"), 401)
             == Api::Error::invalidCredentials,
         "unknown and wrong-password errors remain indistinguishable");
  expect(Api::responseError({}, 429) == Api::Error::rateLimited &&
         Api::responseError({}, 503) == Api::Error::unavailable,
         "rate limits and unavailable backend remain recoverable without raw errors");
}
}

int main() {
  try {
    requestKeepsCredentialsInHttpsJson();
    sessionPreservesIdentityAndAccessRules();
    passwordLinksAndErrorsDoNotDiscloseAccountState();
    std::cout << "Email/password contract tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
