#pragma once

#include "../data/SongLyrics.h"
#include <functional>
#include <memory>

namespace juce { class Component; }

// The editor is currently offered only by the UIKit implementation. The song
// document remains portable; other platforms can preserve it without an editor.
namespace NativeSongLyricsEditor {
struct Options {
  juce::String title;
  std::optional<SongLyricsDocument> document;
  juce::String artist;
  double tempoBpm = 0.0;
};
using SaveCompletion = std::function<void(bool success, juce::String error)>;
using Save = std::function<void(std::optional<SongLyricsDocument>, SaveCompletion)>;

class Handle {
public:
  virtual ~Handle() = default;
  // Discards and dismisses silently. Safe when a parent closes or logs out.
  virtual void cancel() = 0;
};

bool isAvailable();
std::unique_ptr<Handle> present(juce::Component &owner, Options options,
                               Save onSave, std::function<void()> onClose);
// Localised titles and #RRGGBB colours shared by the bridge and contract tests.
juce::var presentationConfig(const Options &options);
// Recomputed from the current text order after native section edits. These
// localised codes/ordinals are presentation only and never alter the document.
juce::var sectionLabels(const SongLyricsDocument &document);
} // namespace NativeSongLyricsEditor
