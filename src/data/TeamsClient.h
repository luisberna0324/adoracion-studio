#pragma once

#include "CloudTransferClient.h"

#include <atomic>
#include <functional>
#include <memory>

/** Authenticated Teams metadata requests. Audio transfers use CloudTransferClient. */
class TeamsClient final {
public:
  using Callback = std::function<void(juce::var, CloudTransferError)>;

  TeamsClient();
  ~TeamsClient();

  /** "list" performs GET /api/teams; other operations perform POST. */
  void request(CloudTransferCredentials credentials, juce::String operation,
               juce::var payload, Callback callback);

  /** Invalidates queued completions as well as in-flight requests. */
  void cancelAll();

private:
  struct RequestState {
    std::atomic<juce::uint64> generation{0};
    std::atomic<bool> alive{true};
  };
  std::shared_ptr<RequestState> state;
  juce::ThreadPool pool{2};
};
