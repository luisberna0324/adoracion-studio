#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace playworship::lyrics
{
// Times are positions in the song's musical/source timeline, independent of sections.
struct Target
{
    bool hide = false;
    juce::String showId, layoutId, groupId, slideId;
    int occurrence = 1;
    juce::String layoutSignature, title, text;
    // New show-bound marks resolve this slide in the current active layout.
    // Legacy marks keep their exact saved layout/occurrence/signature.
    bool useActiveLayout = false;
};
struct Mark
{
    juce::String id;
    double seconds = 0.0;
    bool enabled = true;
    Target target;
};
struct SongLyrics
{
    bool enabled = false;
    juce::String showId, showName, layoutId, layoutName;
    std::vector<Mark> marks;
};
struct SlideDestination
{
    Target target;
    int absoluteIndex = 0; // FreeShow REST is one based; get_output.slide.index is zero based.
    int screen = 1;
    juce::String displayName;
};
struct Layout
{
    juce::String id, name, signature;
    std::vector<SlideDestination> slides;
};
struct SlideGroup
{
    juce::String id, name;
    std::vector<SlideDestination> slides;
};
struct ShowSummary { juce::String id, name; };
struct ShowCatalog
{
    juce::String id, name, activeLayoutId, error;
    std::vector<Layout> layouts;
    std::vector<SlideGroup> groups; // All groups in the show, including unused groups.
    bool ok() const { return error.isEmpty() && id.isNotEmpty() && !layouts.empty(); }
};
struct Resolution
{
    bool valid = false;
    int absoluteIndex = 0;
    juce::String error;
};

juce::var toVar(const SongLyrics&);
bool fromVar(const juce::var&, SongLyrics&, juce::String* error = nullptr);
std::vector<ShowSummary> parseShowList(const juce::var& response);
// Responses may be REST envelopes {action:...,data:...} or their data member.
ShowCatalog parseShow(const juce::String& showId, const juce::var& response,
                      const juce::var& activeLayoutReference = {});
Resolution resolve(const Target&, const ShowCatalog&);
juce::var unwrapResponse(const juce::var&);
juce::String targetLabel(const Target&);
juce::var makeSelectRequest(const Target&, int absoluteIndex);
bool outputMatches(const Target&, int absoluteIndex, const juce::var& response);
}
