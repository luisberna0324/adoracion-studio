#include "AppleMusicStructureBridge.h"

#if !defined(PLAYWORSHIP_APPLE_MUSIC_STRUCTURE_BRIDGE) || !PLAYWORSHIP_APPLE_MUSIC_STRUCTURE_BRIDGE

int32_t pwMusicStructureAvailable(void) { return 0; }

const char* pwMusicStructureUnavailableReason(void)
{
    return "Music Understanding requiere macOS 27 y una compilacion con el SDK de Apple compatible. "
           "El editor manual de secciones sigue disponible.";
}

uint64_t pwMusicStructureStart(const char*, PWMusicStructureCompletion, void*)
{
    // No operation is registered on platforms without the native bridge.
    // The caller must check availability before starting, and handles zero.
    return 0;
}

void pwMusicStructureCancel(uint64_t) {}

#endif
