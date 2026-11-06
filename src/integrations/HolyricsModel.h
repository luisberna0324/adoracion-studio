#pragma once
#include "FreeShowModel.h"
namespace playworship::lyrics {
std::vector<ShowSummary> parseHolyricsSongs(const juce::var&);
ShowCatalog parseHolyricsSong(const juce::String&, const juce::var&);
bool holyricsOutputMatches(const Target&, int absoluteIndex, const juce::var&);
}
