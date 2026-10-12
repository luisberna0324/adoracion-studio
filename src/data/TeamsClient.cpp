#include "TeamsAvailability.h"
#include "TeamsClient.h"

#include "AuthSessionApi.h"

namespace {
constexpr int timeoutMs = 20000;
constexpr int maximumResponseBytes = 4 * 1024 * 1024;

CloudTransferError parseServerError(const juce::var &body, int status) {
  CloudTransferError error{status, "TEAMS_REQUEST_FAILED",
                           "No se pudo completar la solicitud de Teams."};
  if (const auto *object = body.getDynamicObject()) {
    // The backend's public error code/message are structured, never its raw
    // response or request URL (which can contain invitation credentials).
    const auto code = object->getProperty("error").toString();
    if (code.isNotEmpty() && code.length() <= 80 &&
        code.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"))
      error.code = code;
    const auto message = object->getProperty("message").toString().trim();
    if (message.isNotEmpty() && message.length() <= 500)
      error.message = message;
  }
  return error;
}
} // namespace

TeamsClient::TeamsClient() : state(std::make_shared<RequestState>()) {}

TeamsClient::~TeamsClient() {
  state->alive.store(false);
  cancelAll();
  // A token refresh already inside the HTTP library must unwind before the
  // ThreadPool destroys its worker. Normal cancellation stays non-blocking.
  pool.removeAllJobs(true, timeoutMs + 1000);
}

void TeamsClient::cancelAll() {
  state->generation.fetch_add(1);
  pool.removeAllJobs(true, 0);
}

void TeamsClient::request(CloudTransferCredentials credentials,
                          juce::String operation, juce::var payload,
                          Callback callback) {
  if (!teamsFileSharingAvailable) {
    if (callback) callback({}, {403, "TEAM_SHARING_PAUSED", "Teams file sharing is temporarily unavailable. Personal Cloud remains available."});
    return;
  }
  struct Job final : juce::ThreadPoolJob {
    CloudTransferCredentials credentials;
    juce::String operation;
    juce::var payload;
    Callback callback;
    std::shared_ptr<RequestState> state;
    const juce::uint64 generation;

    Job(CloudTransferCredentials credentialsIn, juce::String operationIn,
        juce::var payloadIn, Callback callbackIn,
        std::shared_ptr<RequestState> stateIn)
        : ThreadPoolJob("TeamsRequest"),
          credentials(std::move(credentialsIn)),
          operation(std::move(operationIn)), payload(std::move(payloadIn)),
          callback(std::move(callbackIn)), state(std::move(stateIn)),
          generation(state->generation.load()) {}

    bool cancelled() const {
      return shouldExit() || !state->alive.load() ||
             state->generation.load() != generation;
    }

    void deliver(juce::var result, CloudTransferError error) {
      if (!callback || cancelled())
        return;
      juce::MessageManager::callAsync(
          [cb = std::move(callback), response = std::move(result),
           requestError = std::move(error), requestState = state,
           requestGeneration = generation]() mutable {
            if (requestState->alive.load() &&
                requestState->generation.load() == requestGeneration)
              cb(std::move(response), std::move(requestError));
          });
    }

    JobStatus runJob() override {
      if (cancelled())
        return jobHasFinished;
      const auto base = credentials.apiBaseUrl.trim().trimCharactersAtEnd("/");
      if ((!base.startsWithIgnoreCase("https://") &&
           !base.startsWithIgnoreCase("http://")) || base.containsChar('\r') ||
          base.containsChar('\n') || operation.isEmpty() ||
          operation.length() > 64 ||
          credentials.firebaseApiKey.isEmpty() ||
          credentials.refreshToken.isEmpty() ||
          (!payload.isVoid() && payload.getDynamicObject() == nullptr)) {
        deliver({}, {0, "INVALID_REQUEST", "Solicitud de Teams incompleta."});
        return jobHasFinished;
      }

      const auto token = AuthSessionApi::refreshFirebaseIdToken(
          credentials.firebaseApiKey, credentials.refreshToken, timeoutMs);
      if (cancelled())
        return jobHasFinished;
      if (!token.ok) {
        deliver({}, {0, "AUTH_REFRESH_FAILED",
                     "Inicia sesión de nuevo para acceder a Teams."});
        return jobHasFinished;
      }

      auto url = juce::URL(base + "/api/teams");
      const bool listing = operation == "list";
      if (!listing) {
        auto body = payload.isVoid() ? juce::var(new juce::DynamicObject())
                                     : payload.clone();
        body.getDynamicObject()->setProperty("operation", operation);
        url = url.withPOSTData(juce::JSON::toString(body));
      }
      int status = 0;
      auto stream = url.createInputStream(
          juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
              .withExtraHeaders("Authorization: Bearer " + token.idToken +
                                "\r\nContent-Type: application/json\r\n")
              .withHttpRequestCmd(listing ? "GET" : "POST")
              .withConnectionTimeoutMs(timeoutMs)
              .withStatusCode(&status));
      if (cancelled())
        return jobHasFinished;
      if (stream == nullptr) {
        deliver({}, {status, "NETWORK", "Teams no está disponible. Reintenta."});
        return jobHasFinished;
      }

      juce::MemoryBlock bytes;
      stream->readIntoMemoryBlock(bytes, maximumResponseBytes + 1);
      if (cancelled())
        return jobHasFinished;
      if (bytes.getSize() > maximumResponseBytes) {
        deliver({}, {status, "INVALID_RESPONSE", "La respuesta de Teams es demasiado grande."});
        return jobHasFinished;
      }
      const auto parsed = juce::JSON::parse(bytes.toString());
      if (status < 200 || status >= 300) {
        deliver({}, parseServerError(parsed, status));
        return jobHasFinished;
      }
      if (parsed.getDynamicObject() == nullptr) {
        deliver({}, {status, "INVALID_RESPONSE", "Respuesta de Teams inválida."});
        return jobHasFinished;
      }
      deliver(parsed, {});
      return jobHasFinished;
    }
  };
  pool.addJob(new Job(std::move(credentials), operation.trim(), payload.clone(),
                      std::move(callback), state), true);
}
