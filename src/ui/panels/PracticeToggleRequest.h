#pragma once

#include <cstdint>

// UI-only acknowledgement guard. Never advertises the requested value as the
// audible value, and bounds an unacknowledged remote command to two seconds.
struct PracticeToggleRequest {
  bool pending = false;
  bool requested = false;
  std::uint32_t requestedAt = 0;

  void begin(bool value, std::uint32_t now) noexcept {
    pending = true;
    requested = value;
    requestedAt = now;
  }

  // Returns true only for an unconfirmed timeout, not context cancellation.
  bool reconcile(bool actual, bool validContext, std::uint32_t now) noexcept {
    const bool timedOut = pending && validContext && actual != requested &&
                         now - requestedAt >= 2000;
    if (!validContext || actual == requested || timedOut)
      pending = false;
    return timedOut;
  }
};
