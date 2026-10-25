#include "PluginEditor.h"
#include "data/SongLyrics.h"
#include "ui/panels/SongLyricsPanel.h"
#include "ui/panels/TopBar.h"
#include "ui/panels/PracticePanel.h"

bool MTControlsAudioProcessorEditor::canShowSongLyrics() const {
  return mainUiInitialized && hasCurrentLoadedSong && !songLoadBusy &&
         !playbackSession.isRemote() && currentLoadedSong.id.isNotEmpty() &&
         hasSongLyrics(currentLoadedSong.lyrics);
}

void MTControlsAudioProcessorEditor::showSongLyrics() {
  if (songLyricsOverlay != nullptr || !canShowSongLyrics())
    return;
  songLyricsReturnFocus = juce::Component::getCurrentlyFocusedComponent();
  if (songLyricsReturnFocus == nullptr || !songLyricsReturnFocus->isShowing()) {
    if (practiceOverlay != nullptr)
      songLyricsReturnFocus = practiceOverlay->findChildWithID("practice-lyrics");
    else if (topBar != nullptr) {
      auto *direct = topBar->findChildWithID("song-lyrics");
      songLyricsReturnFocus = direct != nullptr && direct->isVisible()
          ? direct : topBar->findChildWithID("topbar-more");
    }
  }
  songLyricsContextId = currentLoadedSong.id;
  const auto generation = ++songLyricsOverlayGeneration;
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  songLyricsOverlay = std::make_unique<SongLyricsPanel>(
      currentLoadedSong.title, currentLoadedSong.lyrics, true,
      std::function<void(std::optional<SongLyricsDocument>)>{}, [safe, generation] {
        // The panel owns the button invoking this callback. Release it only
        // after JUCE has finished delivering that input event.
        juce::MessageManager::callAsync([safe, generation] {
          if (safe != nullptr && safe->songLyricsOverlayGeneration == generation)
            safe->closeSongLyrics();
        });
      });
  addAndMakeVisible(*songLyricsOverlay);
  songLyricsOverlay->setBounds(getLocalBounds());
  songLyricsOverlay->toFront(true);
  updatePhoneOrientation();
}

void MTControlsAudioProcessorEditor::closeSongLyrics(const bool restoreFocus) {
  if (songLyricsOverlay == nullptr)
    return;
  ++songLyricsOverlayGeneration;
  const auto focus = songLyricsReturnFocus;
  songLyricsReturnFocus = nullptr;
  songLyricsContextId.clear();
  songLyricsOverlay->setVisible(false);
  songLyricsOverlay.reset();
  updatePhoneOrientation();
  resized();
  if (restoreFocus && mainUiInitialized && focus != nullptr &&
      focus->isShowing() && focus->isEnabled() && focus->getWantsKeyboardFocus())
    focus->grabKeyboardFocus();
}

void MTControlsAudioProcessorEditor::refreshSongLyricsContext() {
  if (songLyricsOverlay != nullptr &&
      (!canShowSongLyrics() || currentLoadedSong.id != songLyricsContextId))
    closeSongLyrics(false);
}
