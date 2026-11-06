#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* JSON and error strings are UTF-8 and borrowed for the duration of the callback.
   Exactly one callback is delivered on a background thread for every nonzero
   token, including cancellation. Keep context alive until that callback returns.
   Results are source-file seconds, without tempo scaling or timeline offsets. */
typedef void (*PWMusicStructureCompletion)(const char* json, const char* error, void* context);

int32_t pwMusicStructureAvailable(void);
/* Borrowed process-lifetime string. Empty when the provider is available. */
const char* pwMusicStructureUnavailableReason(void);
/* localPath must be a local filesystem path, never a remote URL.
   A null completion returns zero and starts no operation. */
uint64_t pwMusicStructureStart(const char* localPath,
                              PWMusicStructureCompletion completion,
                              void* context);
/* Nonblocking and idempotent. Cancelling an already completed token is safe. */
void pwMusicStructureCancel(uint64_t token);

#ifdef __cplusplus
}
#endif
