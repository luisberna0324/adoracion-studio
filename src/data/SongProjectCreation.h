#pragma once

#include "SongLibraryStore.h"

// Shared creation transaction for the mobile and desktop wizard. Call save on
// a worker; the final library registration is serialized on the message thread.
namespace SongProjectCreation {
bool resolveDestination(const ImportedSongData &, juce::File &, juce::String &);
bool save(ImportedSongData, juce::String &, const SongImportProgressFn &);
}
