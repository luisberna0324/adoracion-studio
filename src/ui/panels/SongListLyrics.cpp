#include "SongListPanel.h"
#include "../../data/Localization.h"
#include <algorithm>
#include <exception>
#include <thread>

void SongListPanel::cancelSongLyricsEditor() {
  ++nativeLyricsGeneration;
  if (nativeLyricsCancelled) nativeLyricsCancelled->store(true);
  if (nativeLyricsEditor) nativeLyricsEditor->cancel();
  nativeLyricsEditor.reset();
  nativeLyricsCancelled.reset();
  nativeLyricsSaving.reset();
}

void SongListPanel::openSongLyricsEditor(const juce::String &songId) {
  if (remoteLibrary || !NativeSongLyricsEditor::isAvailable() ||
      isSongEditorOpen() || libraryCreationWizard != nullptr || isProjectImportInProgress()) return;
  const auto wantedId = songId.trim();
  if (wantedId.isEmpty()) return;
  // Re-read local metadata. Rows may have been reordered or updated while their
  // action menu was open; a view index or an old row snapshot is not authoritative.
  const auto songs = SongLibraryStore::loadSongs();
  const auto found = std::find_if(songs.begin(), songs.end(), [&wantedId](const auto &song) {
    return song.id == wantedId;
  });
  if (found == songs.end()) return;
  const auto generation = ++nativeLyricsGeneration;
  const auto cancelled = std::make_shared<std::atomic<bool>>(false);
  const auto saving = std::make_shared<std::atomic<bool>>(false);
  nativeLyricsCancelled = cancelled;
  nativeLyricsSaving = saving;
  const auto safe = juce::Component::SafePointer<SongListPanel>(this);
  NativeSongLyricsEditor::Options options{found->title, found->lyrics,
                                          found->artist, static_cast<double>(found->bpm)};
  nativeLyricsEditor = NativeSongLyricsEditor::present(*this, std::move(options),
      [safe, generation, cancelled, saving, wantedId](std::optional<SongLyricsDocument> document,
                                                    NativeSongLyricsEditor::SaveCompletion completion) {
        if (safe == nullptr || cancelled->load() || safe->nativeLyricsGeneration != generation || safe->remoteLibrary)
          return;
        if (saving->exchange(true)) {
          completion(false, L10n::tr("Espera a que termine el guardado actual.", "Wait for the current save to finish."));
          return;
        }
        const auto response = std::make_shared<NativeSongLyricsEditor::SaveCompletion>(std::move(completion));
        const auto finish = [safe, generation, cancelled, saving, response](bool success, juce::String error) {
          juce::MessageManager::callAsync([safe, generation, cancelled, saving, response, success, error = std::move(error)]() mutable {
            saving->store(false);
            if (safe == nullptr || cancelled->load() || safe->nativeLyricsGeneration != generation || safe->remoteLibrary)
              return;
            if (success) safe->refreshFromStorage();
            // No onSongEdited/activation callback: lyric changes must not reload
            // audio, restart transport, or change the user's selected song.
            if (*response) (*response)(success, std::move(error));
          });
        };
        try {
          std::thread([cancelled, wantedId, document = std::move(document), finish]() mutable {
            if (cancelled->load()) return;
            bool success = false;
            juce::String error;
            try {
              success = SongLibraryStore::updateSongLyrics(wantedId, std::move(document), &error);
            } catch (const std::exception &) {
              error = L10n::tr("No se pudo guardar la letra. Inténtalo de nuevo.", "Couldn't save the lyrics. Please try again.");
            } catch (...) {
              error = L10n::tr("No se pudo guardar la letra. Inténtalo de nuevo.", "Couldn't save the lyrics. Please try again.");
            }
            if (!success && error.isEmpty())
              error = L10n::tr("No se pudo guardar la letra. Inténtalo de nuevo.", "Couldn't save the lyrics. Please try again.");
            finish(success, std::move(error));
          }).detach();
        } catch (...) {
          finish(false, L10n::tr("No se pudo iniciar el guardado. Inténtalo de nuevo.", "Couldn't start saving. Please try again."));
        }
      }, [safe, generation] {
        // A UIKit completion may close after the parent has changed workspace
        // or opened another editor. Never release that newer presentation.
        juce::MessageManager::callAsync([safe, generation] {
          if (safe != nullptr && safe->nativeLyricsGeneration == generation)
            safe->cancelSongLyricsEditor();
        });
      });
  if (nativeLyricsEditor == nullptr) cancelSongLyricsEditor();
}
