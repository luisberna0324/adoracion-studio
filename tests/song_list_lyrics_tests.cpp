#include "ui/panels/SongListPanel.h"
#include "data/Localization.h"
#include "IsolatedTestRoot.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

// This desktop-only gate replaces only the platform bridge. Native UIKit
// presentation, keyboard and selection remain separate simulator/device checks.
namespace {
bool nativeAvailable = true;
int presentations = 0;
struct FakePresentation {
  NativeSongLyricsEditor::Options options;
  NativeSongLyricsEditor::Save save;
  std::function<void()> close;
  bool cancelled = false;
};
std::shared_ptr<FakePresentation> latest;
class FakeHandle final : public NativeSongLyricsEditor::Handle {
public:
  explicit FakeHandle(std::shared_ptr<FakePresentation> stateIn) : state(std::move(stateIn)) {}
  void cancel() override { state->cancelled = true; }
  std::shared_ptr<FakePresentation> state;
};
int checks = 0;
void expect(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
void pump(const std::function<bool()> &done, const char *message) {
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
  while (!done() && juce::Time::getMillisecondCounterHiRes() < deadline)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  expect(done(), message);
}
void drain() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }
juce::Button *lyricsAction(juce::Component &component) {
  if (auto *button = dynamic_cast<juce::Button *>(&component); button != nullptr &&
      button->getName() == L10n::tr("Letra", "Lyrics")) return button;
  for (auto *child : component.getChildren())
    if (auto *button = lyricsAction(*child)) return button;
  return nullptr;
}
SongLyricsDocument document(const char *text) {
  SongLyricsDocument doc; doc.text = juce::String::fromUTF8(text); return doc;
}
ImportedSongData loadSong(const juce::String &id) {
  const auto songs = SongLibraryStore::loadSongs();
  const auto found = std::find_if(songs.begin(), songs.end(), [&id](const auto &song) { return song.id == id; });
  expect(found != songs.end(), "fixture song remains registered");
  return *found;
}
}
namespace NativeSongLyricsEditor {
bool isAvailable() { return nativeAvailable; }
std::unique_ptr<Handle> present(juce::Component &, Options options, Save save, std::function<void()> close) {
  if (!nativeAvailable) return {};
  ++presentations;
  latest = std::make_shared<FakePresentation>();
  latest->options = std::move(options); latest->save = std::move(save); latest->close = std::move(close);
  return std::make_unique<FakeHandle>(latest);
}
}

struct SongListLyricsTestAccess {
  static int localRow(const LibraryCatalogPanel &panel, const juce::String &id) {
    for (int i = 0; i < static_cast<int>(panel.visibleRows.size()); ++i)
      if (panel.visibleRows[static_cast<size_t>(i)].songId == id) return i;
    return -1;
  }
  static void testMenuIdentity(const juce::String &first, const juce::String &second) {
    SongListPanel panel; panel.setSize(420, 760);
    auto &catalog = panel.libraryCatalogPanel;
    juce::String requested;
    catalog.setOnSongLyricsRequested([&](const auto &id) { requested = id; });
    const auto row = localRow(catalog, first);
    expect(row >= 0, "local song is visible in the actual catalog");
    nativeAvailable = false;
    auto unavailable = catalog.createRowMenu(row, {});
    expect(unavailable && lyricsAction(*unavailable) == nullptr, "catalog has no lyrics action without an available native editor");
    nativeAvailable = true;
    auto menu = catalog.createRowMenu(row, {});
    auto *action = lyricsAction(*menu);
    expect(action != nullptr, "local catalog offers the native lyrics action");
    std::reverse(catalog.visibleRows.begin(), catalog.visibleRows.end());
    action->onClick();
    expect(requested.isEmpty(), "catalog defers native presentation until its callout callback ends");
    pump([&] { return requested.isNotEmpty(); }, "deferred catalog action arrives");
    expect(requested == first, "catalog menu keeps its captured song identity after resorting rows");
    catalog.remoteLibrary = std::make_shared<LibraryState>();
    auto remote = catalog.createRowMenu(localRow(catalog, first), {});
    expect(remote && lyricsAction(*remote) == nullptr, "remote library never exposes the local lyrics editor");
    catalog.remoteLibrary.reset();
    const auto cloudIndex = localRow(catalog, first);
    catalog.visibleRows[static_cast<size_t>(cloudIndex)].isCloudOnly = true;
    auto cloud = catalog.createRowMenu(cloudIndex, {});
    expect(cloud && lyricsAction(*cloud) == nullptr, "Cloud rows do not expose local lyric editing");

    auto &list = panel.listContent;
    SongListPanel::SongRow a; a.songId = first; a.songIndex = 0;
    SongListPanel::SongRow b; b.songId = second; b.songIndex = 1;
    list.setSongs({a, b}); requested.clear();
    bool local = true;
    list.setOnSongLyricsRequested([&](const auto &id) { requested = id; }, [&] { return local; });
    auto listMenu = list.createActionsMenuForRow(0);
    action = lyricsAction(*listMenu);
    expect(action != nullptr, "setlist row exposes the same native lyrics action");
    list.setSongs({b, a}); action->onClick();
    expect(requested.isEmpty(), "setlist native presentation is deferred until its callout closes");
    pump([&] { return requested.isNotEmpty(); }, "setlist deferred action arrives");
    expect(requested == first, "setlist lyrics use the captured song ID after row reordering");
    local = false;
    auto remoteList = list.createActionsMenuForRow(0);
    expect(lyricsAction(*remoteList) == nullptr, "remote setlist has no local lyrics action");
    local = true; nativeAvailable = false;
    auto unsupportedList = list.createActionsMenuForRow(0);
    expect(lyricsAction(*unsupportedList) == nullptr, "unsupported platforms expose no setlist lyrics action");
    nativeAvailable = true;
  }
  static void testCrossfadeSetlist(const juce::String &first, const juce::String &second) {
    const auto setlistId = SongLibraryStore::createSetlist("Crossfade fixture");
    expect(SongLibraryStore::addSongToSetlist(setlistId, first)
        && SongLibraryStore::addSongToSetlist(setlistId, second)
        && SongLibraryStore::setActiveSetlistId(setlistId), "prepare isolated crossfade setlist");
    const auto transition = [&](const juce::String &songId) {
      const auto snapshot = SongLibraryStore::loadLibrarySnapshot();
      for (const auto &setlist : snapshot.setlists)
        if (setlist.id == setlistId)
          for (const auto &item : setlist.items)
            if (item.songId == songId) return item.transitionToNext;
      throw std::runtime_error("missing crossfade fixture item");
    };
    expect(!transition(first).crossfade, "legacy and unmarked items do not enable crossfade");
    expect(SongLibraryStore::setCrossfadeNextForSetlist(setlistId, first, true)
        && transition(first).autoPlay && transition(first).crossfade,
        "crossfade enables the outgoing autoplay flag and survives reloading the library");
    SongListPanel panel;
    expect(panel.isCrossfadeNextEnabledForSong(first)
        && panel.getCrossfadeNextSong(first).has_value()
        && panel.getCrossfadeNextSong(first)->id == second,
        "crossfade resolves only the immediate neighbour in the active playback setlist");
    auto lastMenu = panel.listContent.createActionsMenuForRow(1);
    juce::Button *lastCrossfade = nullptr;
    for (auto *child : lastMenu->getChildren())
      if (auto *button = dynamic_cast<juce::Button *>(child); button && button->getName().containsIgnoreCase("crossfade"))
        lastCrossfade = button;
    expect(lastCrossfade && !lastCrossfade->isEnabled(), "last row cannot select a crossfade without a destination");
    expect(!SongLibraryStore::setCrossfadeNextForSetlist(setlistId, second, true)
        && !transition(second).autoPlay && !transition(second).crossfade,
        "model refuses a crossfade from the final row");
    expect(SongLibraryStore::setAutoPlayNextForSetlist(setlistId, first, false)
        && !transition(first).autoPlay && !transition(first).crossfade,
        "disabling autoplay also clears the persisted crossfade");
    panel.refreshFromStorage();
    expect(!panel.isCrossfadeNextEnabledForSong(first) && !panel.getCrossfadeNextSong(first),
        "UI transport queries stop offering crossfade after autoplay is disabled");
    auto firstMenu = panel.listContent.createActionsMenuForRow(0);
    juce::Button *firstCrossfade = nullptr;
    for (auto *child : firstMenu->getChildren())
      if (auto *button = dynamic_cast<juce::Button *>(child); button && button->getName().containsIgnoreCase("crossfade"))
        firstCrossfade = button;
    expect(firstCrossfade && firstCrossfade->isEnabled() && firstCrossfade->onClick,
        "first-row menu offers its crossfade action");
    const auto snapshots = juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_CROSSFADE_SNAPSHOTS", {});
    if (snapshots.isNotEmpty()) {
      const juce::File folder(snapshots); folder.createDirectory();
      const auto target = folder.getChildFile("crossfade-setlist-menu.png"); target.deleteFile();
      if (auto stream = target.createOutputStream())
        juce::PNGImageFormat().writeImageToStream(firstMenu->createComponentSnapshot(firstMenu->getLocalBounds(), true, 2.0f), *stream);
    }
    firstCrossfade->onClick();
    expect(transition(first).autoPlay && transition(first).crossfade && panel.isCrossfadeNextEnabledForSong(first),
        "setlist menu enables and refreshes the persisted crossfade without playing audio");
    expect(SongLibraryStore::moveSongInSetlist(setlistId, 0, 1)
        && !transition(first).autoPlay && !transition(first).crossfade,
        "moving a marked item to the end clears its transition instead of leaving an invalid target");
    expect(SongLibraryStore::deleteSetlist(setlistId), "remove the isolated crossfade fixture setlist");
  }
  static void testNativeSaveLifecycle(const juce::String &id) {
    SongListPanel panel;
    int activations = 0, edits = 0;
    panel.setOnSongActivated([&](const auto &, int) { ++activations; });
    panel.setOnSongEdited([&](const auto &, int) { ++edits; });
    panel.setNowPlayingSong(id, true);
    panel.selectedSongId = id;
    panel.openSongLyricsEditor(id);
    expect(panel.isSongEditorOpen() && latest && latest->options.title == loadSong(id).title,
           "the native editor is owned by the panel and uses the latest local song");
    const auto current = latest;
    bool complete = false, success = false;
    current->save(document("Saved through native bridge"), [&](bool ok, auto error) {
      success = ok; complete = true; expect(ok && error.isEmpty(), "native successful save returns no error");
    });
    expect(!complete, "native editor stays busy until background persistence completes");
    pump([&] { return complete; }, "asynchronous lyrics save completes on the message thread");
    expect(success && loadSong(id).lyrics && loadSong(id).lyrics->text == "Saved through native bridge" &&
               activations == 0 && edits == 0 && panel.nowPlayingSongId == id && panel.selectedSongId == id,
           "saving persists only lyrics without activating audio or changing playback selection");
    current->close();
    pump([&] { return !panel.isSongEditorOpen(); }, "native close releases its owned handle");
    expect(current->cancelled, "released presentation is cancelled safely");

    panel.openSongLyricsEditor(id);
    const auto stale = latest;
    panel.setRemoteLibrary(std::make_shared<LibraryState>());
    expect(stale->cancelled && !panel.isSongEditorOpen(), "entering remote mode cancels local lyric editing");
    bool staleCompletion = false;
    stale->save(document("Late ignored text"), [&](bool, auto) { staleCompletion = true; });
    stale->close(); drain();
    expect(!staleCompletion && loadSong(id).lyrics->text == "Saved through native bridge",
           "late native callbacks cannot save after switching workspaces");
    panel.setRemoteLibrary(nullptr);
    panel.openSongLyricsEditor(id);
    const auto reopened = latest;
    stale->close(); drain();
    expect(panel.isSongEditorOpen() && !reopened->cancelled, "a stale close cannot dismiss a newer native editor");
    panel.cancelSongLyricsEditor();

    auto dying = std::make_unique<SongListPanel>();
    dying->openSongLyricsEditor(id);
    const auto destroyed = latest;
    dying.reset();
    expect(destroyed->cancelled, "destroying the panel cancels UIKit ownership");
    destroyed->save(document("After destruction"), [](bool, auto) {});
    destroyed->close(); drain();
    expect(loadSong(id).lyrics->text == "Saved through native bridge", "destroyed owner rejects stale save callbacks");
  }
  static void testSaveFailure(const juce::String &id) {
    SongListPanel panel;
    panel.openSongLyricsEditor(id);
    const auto state = latest;
    expect(SongLibraryStore::deleteSong(id), "remove only an isolated song to force a save error");
    bool completed = false, saved = true;
    juce::String failure;
    state->save(document("Text must remain in the native draft"), [&](bool ok, auto error) {
      saved = ok; failure = error; completed = true;
    });
    pump([&] { return completed; }, "failed persistence completes instead of leaving the native UI busy");
    expect(!saved && failure.isNotEmpty() && panel.isSongEditorOpen() && !state->cancelled,
           "save error keeps the same native editor open for correction or retry");
    state->close(); drain();
  }
};
int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr) return 77;
#endif
  if (juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_CREATION_TEST_ROOT", {}).isEmpty()) return 77;
  try {
    juce::ScopedJuceInitialiser_GUI gui;
    applyTheme(ThemePreset::Spotify);
    expect(SongLibraryStore::getRootDirectory().isAChildOf(isolatedTestRoot()), "native workflow filesystem is isolated");
    // Actual catalog loading excludes legacy entries without usable audio.
    // Register readable private WAVs through the ordinary import path instead
    // of weakening that product filter or using nonexistent stem references.
    const auto audioFile = isolatedTestRoot().getChildFile("lyrics-ui-source.wav");
    {
      juce::WavAudioFormat format;
      std::unique_ptr<juce::OutputStream> output = audioFile.createOutputStream();
      expect(output != nullptr, "open isolated WAV fixture output");
      auto writer = format.createWriterFor(output, juce::AudioFormatWriterOptions{}
          .withSampleRate(8000.0).withNumChannels(1).withBitsPerSample(16));
      expect(writer != nullptr, "create readable WAV fixture");
      juce::AudioBuffer<float> samples(1, 256);
      samples.clear(); samples.setSample(0, 16, 0.25f);
      expect(writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples()),
             "write real isolated audio samples");
    }
    const auto fixtureSong = [&audioFile](const char *id, const char *title) {
      ImportedSongData song; song.id = id; song.title = title;
      ImportedStemData stem;
      stem.stemId = song.id + "-stem"; stem.fileName = audioFile.getFileName();
      stem.sourcePath = audioFile.getFullPathName(); stem.displayName = "Piano";
      stem.category = "Keys"; stem.include = true; stem.sampleRate = 8000;
      stem.channels = 1; stem.durationSec = 256.0 / 8000.0;
      song.stems.push_back(std::move(stem));
      return song;
    };
    const auto first = fixtureSong("lyrics-ui-first", "First fixture");
    const auto second = fixtureSong("lyrics-ui-second", "Second fixture");
    juce::String error;
    expect(SongLibraryStore::appendSong(first, &error) && SongLibraryStore::appendSong(second, &error), "register two isolated native-editor fixtures");
    SongListLyricsTestAccess::testCrossfadeSetlist(first.id, second.id);
    SongListLyricsTestAccess::testMenuIdentity(first.id, second.id);
    SongListLyricsTestAccess::testNativeSaveLifecycle(first.id);
    SongListLyricsTestAccess::testSaveFailure(second.id);
    std::cout << "Native lyric workflow: " << checks << " checks passed (simulated bridge, no UIKit claims)\n";
    return 0;
  } catch (const std::exception &error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
