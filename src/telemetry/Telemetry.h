#pragma once

#include <cstdint>

namespace playworship::telemetry {

// These names are deliberately an allow-list. Never pass exception text,
// paths, URLs, titles, account data, or tokens to this API.
enum class Incident : std::uint8_t {
  authRefreshFailed,
  accessValidationFailed,
  cloudSyncFailed,
  songImportFailed,
  songLoadFailed,
  audioDeviceChanged,
};

// Called only from setup/teardown or JUCE's message/background threads.
void initialize();
void shutdown();
void capture(Incident incident) noexcept;

// The audio callback may only increment this counter. A non-realtime caller
// must periodically call flushAudioIncidents() to send one aggregate event.
void recordAudioCallbackGap() noexcept;
void flushAudioIncidents();

} // namespace playworship::telemetry
