#pragma once
#include "../data/SongLyrics.h"

// The native mobile editors use the same UTF-16 document and block-edge rules.
// These helpers do not insert decorative text or change audio sections.
namespace NativeSongLyricsEditing {
struct Range {
  int start = -1;
  int length = 0;
  bool valid() const { return start >= 0 && length > 0; }
};
Range enclosingParagraphs(const juce::String &text, int start, int length,
                          bool trimFinalSeparator = true);
bool isCompleteParagraphs(const juce::String &text, int start, int length);
// One UTF-16 length scan per document, then constant-time neighboring-boundary
// checks per section. Returned flags correspond to document.sections order.
std::vector<bool> completeParagraphsForSections(const SongLyricsDocument &document);
bool replaceText(SongLyricsDocument &document, int start, int length,
                 const juce::String &replacement);
bool setText(SongLyricsDocument &document, const juce::String &text);
} // namespace NativeSongLyricsEditing
