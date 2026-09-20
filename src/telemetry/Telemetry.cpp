#include "Telemetry.h"

#include <atomic>

#if MTCONTROLS_SENTRY_ENABLED
#include <sentry.h>
#endif

namespace playworship::telemetry {
namespace {
std::atomic<unsigned> instanceCount{0};
std::atomic<std::uint32_t> audioCallbackGaps{0};

const char* incidentName(const Incident incident) noexcept {
  switch (incident) {
  case Incident::authRefreshFailed: return "auth.refresh_failed";
  case Incident::accessValidationFailed: return "auth.access_validation_failed";
  case Incident::cloudSyncFailed: return "cloud_sync.failed";
  case Incident::songImportFailed: return "song_import.failed";
  case Incident::songLoadFailed: return "song_load.failed";
  case Incident::audioDeviceChanged: return "audio_device.changed";
  }
  return "unknown";
}

#if MTCONTROLS_SENTRY_ENABLED
void captureMessage(const char* message) noexcept {
  sentry_capture_event(sentry_value_new_message_event(SENTRY_LEVEL_ERROR,
                                                        "playworship", message));
}
#endif
} // namespace

void initialize() {
  if (instanceCount.fetch_add(1, std::memory_order_acq_rel) != 0)
    return;
#if MTCONTROLS_SENTRY_ENABLED
  auto* options = sentry_options_new();
  sentry_options_set_dsn(options, MTCONTROLS_SENTRY_DSN);
  sentry_options_set_release(options, MTCONTROLS_SENTRY_RELEASE);
  sentry_options_set_environment(options, MTCONTROLS_SENTRY_ENVIRONMENT);
  // This integration only emits allow-listed message names and an aggregate
  // count; it never attaches local paths, request bodies, PII, or files.
  sentry_init(options);
#endif
}

void shutdown() {
  if (instanceCount.fetch_sub(1, std::memory_order_acq_rel) != 1)
    return;
#if MTCONTROLS_SENTRY_ENABLED
  sentry_close();
#endif
}

void capture(const Incident incident) noexcept {
#if MTCONTROLS_SENTRY_ENABLED
  captureMessage(incidentName(incident));
#else
  (void) incident;
#endif
}

void recordAudioCallbackGap() noexcept {
  audioCallbackGaps.fetch_add(1, std::memory_order_relaxed);
}

void flushAudioIncidents() {
  const auto count = audioCallbackGaps.exchange(0, std::memory_order_acq_rel);
#if MTCONTROLS_SENTRY_ENABLED
  if (count != 0) {
    auto event = sentry_value_new_message_event(SENTRY_LEVEL_WARNING,
                                                "playworship", "audio.callback_gaps");
    sentry_value_set_by_key(event, "gap_count", sentry_value_new_int32(static_cast<int>(count)));
    sentry_capture_event(event);
  }
#else
  (void) count;
#endif
}
} // namespace playworship::telemetry
