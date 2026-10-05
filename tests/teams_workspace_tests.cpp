#include "ui/panels/SongListPanel.h"
#include "data/AppSettings.h"
#include "data/CloudSyncIndexStore.h"
#include "data/Localization.h"
#include "data/SessionPersistence.h"
#include "data/TeamsLocalStore.h"
#include "data/TeamsPreparationService.h"
#include "IsolatedTestRoot.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

SetlistData legacySetlist(const juce::String &id, const juce::String &songId) {
  SetlistData result;
  result.id = result.name = id;
  SetlistItem item;
  item.itemId = item.songId = songId;
  item.pitchSemitones = 3;
  item.tempoBpm = 110;
  result.items.push_back(item);
  return result;
}

void checkNoWorkspaceSelectors(const juce::Component &component) {
  const auto name = component.getName();
  expect(name != "libraryWorkspace" && name != "setlistsWorkspace" &&
             name != "songsWorkspace",
         "ordinary library and setlists do not expose Team/Personal selectors");
  for (int i = 0; i < component.getNumChildComponents(); ++i)
    checkNoWorkspaceSelectors(*component.getChildComponent(i));
}
} // namespace

struct TeamsWorkspaceTestAccess {
  static LibraryCatalogPanel &library(SongListPanel &panel) {
    return panel.libraryCatalogPanel;
  }

  static int localRow(const LibraryCatalogPanel &panel, const juce::String &id) {
    for (size_t i = 0; i < panel.visibleRows.size(); ++i) {
      const auto &row = panel.visibleRows[i];
      if (row.songId == id && !row.isCloudOnly && row.songIndex >= 0)
        return static_cast<int>(i);
    }
    return -1;
  }

  static juce::String playingSong(const SongListPanel &panel) {
    return panel.nowPlayingSongId;
  }

  static int cloudRow(const LibraryCatalogPanel &panel, const CloudSongSummary &song) {
    for (size_t i = 0; i < panel.visibleRows.size(); ++i)
      if (panel.visibleRows[i].isCloudOnly && panel.visibleRows[i].songId == song.catalogId())
        return static_cast<int>(i);
    return -1;
  }

  static void showCloud(LibraryCatalogPanel &panel) { panel.cloudFilterButton.onClick(); }
  static void showLocal(LibraryCatalogPanel &panel) { panel.localFilterButton.onClick(); }

  static void checkCloudRow(const LibraryCatalogPanel &panel, const CloudSongSummary &song,
                            const juce::String &localSongId) {
    const auto index = cloudRow(panel, song);
    expect(index >= 0, "personal and shared songs have distinct rows in the normal Cloud catalog");
    const auto &row = panel.visibleRows[static_cast<size_t>(index)];
    expect(row.remoteSongId == song.songId && row.isSharedCloud == song.isSharedCloud() &&
               row.localSongId == localSongId && row.isCloudDownloaded == localSongId.isNotEmpty(),
           "download state resolves only the exact account, owner, song and revision");
  }

  static std::unique_ptr<juce::Component> rowMenu(LibraryCatalogPanel &panel, int row) {
    auto menu = panel.createRowMenu(row, {});
    expect(menu != nullptr, "catalog row exposes its ordinary action menu");
    return menu;
  }

  static juce::Button *action(juce::Component &menu, const juce::String &label) {
    for (int i = 0; i < menu.getNumChildComponents(); ++i)
      if (auto *button = dynamic_cast<juce::Button *>(menu.getChildComponent(i));
          button != nullptr && button->getName() == label)
        return button;
    return nullptr;
  }

  static void trigger(juce::Component &menu, const juce::String &label) {
    auto *button = action(menu, label);
    expect(button != nullptr && button->isEnabled() && button->onClick != nullptr,
           "ordinary catalog action is available");
    button->onClick();
  }

  static void checkReadOnlyCloud(juce::Component &menu) {
    expect(action(menu, L10n::tr("Eliminar de la nube", "Delete from cloud")) == nullptr &&
               action(menu, L10n::tr("Sincronizar ajustes", "Sync settings")) == nullptr &&
               action(menu, L10n::tr("Versiones de ajustes", "Settings versions")) == nullptr &&
               action(menu, L10n::tr("Subir a nube", "Upload to cloud")) == nullptr,
           "shared Cloud menus never expose owner deletion, upload, settings sync or versions");
  }

  static void reverseSort(LibraryCatalogPanel &panel, bool reverse) {
    panel.sortMode = reverse ? LibraryCatalogPanel::SortMode::TitleDesc
                             : LibraryCatalogPanel::SortMode::TitleAsc;
    panel.rebuildRows();
  }

  static void saveSharedCloudPreview(LibraryCatalogPanel &panel) {
    const auto directory = juce::SystemStats::getEnvironmentVariable("PW_SHARED_CLOUD_PREVIEW_DIR", {});
    if (directory.isEmpty())
      return;
    const juce::File output(directory);
    expect(output.createDirectory().wasOk(), "shared Cloud preview directory");
    const auto oldBounds = panel.getBounds();
    const auto oldHeaderMode = panel.headerMode;
    const auto oldTarget = panel.targetSetlistId;
    const auto oldPhoneMode = panel.minimalPhoneMode;
    auto *oldParent = panel.getParentComponent();
    const auto oldParentIndex = oldParent != nullptr ? oldParent->getIndexOfChildComponent(&panel) : -1;
    const bool oldVisible = panel.isVisible();
    class PreviewPage final : public juce::Component {
    public:
      explicit PreviewPage(LibraryCatalogPanel &catalog) : library(catalog) {
        setOpaque(true);
        setLookAndFeel(&juce::LookAndFeel::getDefaultLookAndFeel());
        addAndMakeVisible(library);
      }
      ~PreviewPage() override { setLookAndFeel(nullptr); }
      void paint(juce::Graphics &g) override { g.fillAll(UITheme::panelDark); }
      void resized() override { library.setBounds(getLocalBounds()); }
    private:
      LibraryCatalogPanel &library;
    } page(panel);
    panel.setTargetSetlistId({});
    panel.setHeaderMode(LibraryCatalogPanel::HeaderMode::Library);
    for (const auto &size : {juce::Point<int>(390, 844), juce::Point<int>(1000, 760)}) {
      panel.setMinimalPhoneMode(size.x == 390);
      page.setSize(size.x, size.y);
      panel.refreshThemeColours();
      panel.resized();
      const auto image = page.createComponentSnapshot(page.getLocalBounds(), true);
      expect(image.isValid() && image.getPixelAt(size.x - 1, size.y - 1).getAlpha() == 255,
             "shared Cloud preview is painted on an opaque real JUCE page");
      const auto file = output.getChildFile(size.x == 390 ? "shared-cloud-phone.png"
                                                         : "shared-cloud-desktop.png");
      juce::FileOutputStream stream(file);
      juce::PNGImageFormat format;
      expect(stream.openedOk() && stream.setPosition(0) && stream.truncate().wasOk() &&
                 format.writeImageToStream(image, stream),
             "real mixed personal/shared Cloud catalog PNG");
    }
    page.removeChildComponent(&panel);
    if (oldParent != nullptr)
      oldParent->addChildComponent(&panel, oldParentIndex);
    panel.setVisible(oldVisible);
    panel.setMinimalPhoneMode(oldPhoneMode);
    panel.setBounds(oldBounds);
    panel.setHeaderMode(oldHeaderMode);
    panel.setTargetSetlistId(oldTarget);
    panel.refreshThemeColours();
    panel.resized();
  }

  static void checkCreationSources(SongListPanel &panel) {
    auto &catalog = panel.libraryCatalogPanel;
    catalog.setSize(358, 780);
    catalog.setHeaderTrailingInset(56);
    expect(panel.createSetlistButton.isVisible() && panel.addSongsButton.isVisible() &&
               catalog.createSongButton.isVisible() && catalog.loadSongButton.isVisible(),
           "normal creation and import controls remain available with Team downloads");
    expect(catalog.searchField.getHeight() >= 44 &&
               catalog.createSongButton.getHeight() >= 44 &&
               catalog.loadSongButton.getHeight() >= 44 &&
               !catalog.searchField.getBounds().intersects(catalog.createSongButton.getBounds()) &&
               !catalog.createSongButton.getBounds().intersects(catalog.loadSongButton.getBounds()),
           "phone library retains touch targets without overlapping navigation actions");
    checkNoWorkspaceSelectors(panel);
  }

  static void activateLibrarySong(SongListPanel &panel, const ImportedSongData &song) {
    panel.libraryCatalogPanel.onSongLoadRequested(song, -1);
  }

  static void activateSetlistSong(SongListPanel &panel, const juce::String &songId) {
    panel.activateSongAtIndex(panel.getSongIndexById(songId));
  }

  static void openAddFromLibrary(SongListPanel &panel) {
    panel.openAddFromLibraryMenu();
  }

  static void addLibrarySong(SongListPanel &panel, const juce::String &songId) {
    panel.openAddFromLibraryMenu();
    const int row = localRow(panel.libraryCatalogPanel, songId);
    expect(row >= 0, "downloaded song is available in the ordinary add-to-setlist picker");
    panel.libraryCatalogPanel.togglePendingSelection(row);
  }
};

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr) return 77;
#endif
  if (juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_CREATION_TEST_ROOT", {}).isEmpty())
    return 77;
  try {
    const auto isolatedRoot = isolatedTestRoot();
    expect(SongLibraryStore::getRootDirectory().isAChildOf(isolatedRoot) &&
               SessionPersistence::getSnapshotFile().isAChildOf(isolatedRoot),
           "library and session fixtures are isolated before storage access");
    juce::ScopedJuceInitialiser_GUI gui;
    AppSettings::setLanguage(AppLanguage::Spanish);
    // Same theme entry point as TeamsUIPreview and PluginEditor. It installs
    // the app's shared SelectionLookAndFeel as JUCE's default LookAndFeel.
    applyTheme(ThemePreset::Spotify);
    const juce::String uid = "alice";
    PersistedSessionSnapshot session;
    session.uid = uid;
    session.refreshToken = "isolated-test-refresh-token";
    session.firebaseApiKey = "isolated-test-api-key";
    expect(SessionPersistence::save(session), "isolated member account snapshot");
    PersistedSessionSnapshot restoredSession;
    expect(SessionPersistence::load(restoredSession) && restoredSession.uid == uid,
           "member account fixture passes ordinary session validation");
    const auto tag = TeamsPreparationService::accountTag(uid);
    const juce::String songA = "team-" + tag + "-owner-a-r1";
    const juce::String songB = "team-" + tag + "-owner-b-r1";
    const juce::String legacyId = "team-list-" + tag + "-legacy-r1";
    auto registerSong = [&](const juce::String &id, bool include = true) {
      ImportedSongData result;
      result.id = result.projectId = result.title = id;
      result.bpm = 120;
      result.key = "C";
      result.timeSignature = "4/4";
      const auto audio = isolatedRoot.getChildFile(id + ".flac");
      expect(audio.replaceWithText("isolated nonempty cached audio"), "cached audio fixture");
      ImportedStemData stem;
      stem.stemId = "instrument";
      stem.include = include;
      stem.storedPath = stem.sourcePath = audio.getFullPathName();
      stem.durationSec = 60;
      stem.sampleRate = 48000;
      stem.channels = 2;
      result.stems.push_back(stem);
      expect(SongLibraryStore::registerCloudCachedSong(result), "ordinary cached song registration");
      return result;
    };
    const auto first = registerSong("personal-first");
    const auto second = registerSong("personal-second");
    const auto localA = registerSong(songA);
    const auto localB = registerSong(songB, false);
    const auto personalList = SongLibraryStore::createSetlist("Personal fixture");
    expect(SongLibraryStore::addSongToSetlist(personalList, first.id) &&
               SongLibraryStore::addSongToSetlist(personalList, second.id) &&
               SongLibraryStore::setAutoPlayNextForSetlist(personalList, first.id, true) &&
               SongLibraryStore::setActiveSetlistId(personalList) &&
               SongLibraryStore::deleteSetlist("setlist_default"),
           "active personal queue fixture");
    const juce::String remoteId = "same-remote-id";
    expect(TeamsLocalStore::upsertSongLink({uid, "owner-a", remoteId, 1, songA}) &&
               TeamsLocalStore::upsertSongLink({uid, "owner-b", remoteId, 1, songB}) &&
               SongLibraryStore::importPreparedSetlist(legacySetlist(legacyId, songA)),
           "internal provenance and legacy prepared setlist fixtures");
    CloudProjectLink personalLink;
    personalLink.projectId = first.projectId;
    personalLink.remoteSongId = remoteId;
    personalLink.remoteRevision = 1;
    personalLink.downloadedAtMs = 1;
    CloudProjectLink staleSharedLink = personalLink;
    staleSharedLink.projectId = localA.projectId;
    staleSharedLink.remoteSongId = "legacy-personal-link";
    expect(CloudSyncIndexStore::upsert(personalLink) && CloudSyncIndexStore::upsert(staleSharedLink),
           "colliding personal Cloud identity and stale local link fixtures");

    SongListPanel panel;
    panel.setMinimalPhoneMode(true);
    panel.setSize(390, 844);
    panel.setNowPlayingSong(first.id, true);
    int activations = 0;
    juce::String activatedSongId;
    int activatedIndex = -2;
    panel.setOnSongActivated([&](const ImportedSongData &requested, int index) {
      ++activations;
      activatedSongId = requested.id;
      activatedIndex = index;
    });
    const auto database = SongLibraryStore::getRootDirectory().getChildFile("songs.json");
    const auto before = database.loadFileAsString();
    const auto provenance = TeamsLocalStore::getAccountDirectory(uid).getChildFile("song-links.json");
    const auto provenanceBefore = provenance.loadFileAsString();
    expect(TeamsWorkspaceTestAccess::localRow(TeamsWorkspaceTestAccess::library(panel), songA) >= 0 &&
               TeamsWorkspaceTestAccess::localRow(TeamsWorkspaceTestAccess::library(panel), songB) >= 0,
           "downloads from multiple owners appear together in the ordinary local catalog");
    TeamsWorkspaceTestAccess::checkCreationSources(panel);
    auto &catalog = TeamsWorkspaceTestAccess::library(panel);
    auto localMenu = TeamsWorkspaceTestAccess::rowMenu(
        catalog, TeamsWorkspaceTestAccess::localRow(catalog, songA));
    TeamsWorkspaceTestAccess::checkReadOnlyCloud(*localMenu);
    expect(TeamsWorkspaceTestAccess::action(*localMenu, L10n::tr(u8"Editar canción", "Edit song")) != nullptr &&
               TeamsWorkspaceTestAccess::action(*localMenu, L10n::tr(u8"Eliminar canción", "Delete song")) != nullptr,
           "shared downloads retain ordinary local edit and remove actions despite stale personal links");

    CloudSongSummary personalCloud;
    personalCloud.songId = remoteId;
    personalCloud.title = "Dios de pactos";
    personalCloud.artist = "Marcos Witt";
    personalCloud.bpm = 70;
    personalCloud.key = "D";
    personalCloud.durationSeconds = 306;
    personalCloud.revision = 1;
    personalCloud.stemCount = 8;
    auto sharedA = personalCloud;
    sharedA.title = "Reinas por la eternidad";
    sharedA.artist = "Miel San Marcos";
    sharedA.bpm = 147;
    sharedA.key = "E";
    sharedA.durationSeconds = 372;
    sharedA.stemCount = 10;
    sharedA.teamId = "owner-a";
    sharedA.ownerUid = "owner-a-uid";
    sharedA.ownerEmail = "owner-a@example.invalid";
    auto sharedB = sharedA;
    sharedB.title = "Gracia sublime";
    sharedB.artist = juce::String::fromUTF8("Christine D’Clario");
    sharedB.bpm = 100;
    sharedB.key = "D";
    sharedB.durationSeconds = 330;
    sharedB.stemCount = 12;
    sharedB.teamId = "owner-b";
    sharedB.ownerUid = "owner-b-uid";
    sharedB.ownerEmail = "owner-b@example.invalid";
    panel.setCloudSongs({personalCloud, sharedA, sharedB});
    TeamsWorkspaceTestAccess::showCloud(catalog);
    TeamsWorkspaceTestAccess::checkCloudRow(catalog, personalCloud, first.id);
    TeamsWorkspaceTestAccess::checkCloudRow(catalog, sharedA, songA);
    TeamsWorkspaceTestAccess::checkCloudRow(catalog, sharedB, songB);
    auto sharedMenu = TeamsWorkspaceTestAccess::rowMenu(catalog,
        TeamsWorkspaceTestAccess::cloudRow(catalog, sharedA));
    TeamsWorkspaceTestAccess::checkReadOnlyCloud(*sharedMenu);
    expect(TeamsWorkspaceTestAccess::action(*sharedMenu, L10n::tr("Eliminar descarga", "Remove download")) != nullptr,
           "a downloaded shared Cloud row can remove only its local copy");
    auto personalMenu = TeamsWorkspaceTestAccess::rowMenu(catalog,
        TeamsWorkspaceTestAccess::cloudRow(catalog, personalCloud));
    expect(TeamsWorkspaceTestAccess::action(*personalMenu, L10n::tr("Eliminar de la nube", "Delete from cloud")) != nullptr,
           "personal Cloud management remains available when remote IDs collide");

    // Even an excluded stem must exist before Cloud offers the local copy.
    const juce::File excludedAudio(localB.stems.front().storedPath);
    expect(excludedAudio.deleteFile(), "missing excluded shared stem fixture");
    catalog.refreshFromStorage();
    TeamsWorkspaceTestAccess::checkCloudRow(catalog, sharedB, {});
    TeamsWorkspaceTestAccess::saveSharedCloudPreview(catalog);
    TeamsWorkspaceTestAccess::openAddFromLibrary(panel);
    juce::String requestedCatalogId, requestedSetlistId;
    panel.setOnCloudDownloadRequested([&](const juce::String &id, const juce::String &target) {
      requestedCatalogId = id;
      requestedSetlistId = target;
    });
    auto downloadMenu = TeamsWorkspaceTestAccess::rowMenu(catalog,
        TeamsWorkspaceTestAccess::cloudRow(catalog, sharedB));
    TeamsWorkspaceTestAccess::checkReadOnlyCloud(*downloadMenu);
    TeamsWorkspaceTestAccess::reverseSort(catalog, true);
    TeamsWorkspaceTestAccess::trigger(*downloadMenu, L10n::tr("Descargar y agregar", "Download and add"));
    expect(requestedCatalogId == sharedB.catalogId() && requestedSetlistId == personalList,
           "download action preserves the selected owner and personal setlist across catalog reordering");
    TeamsWorkspaceTestAccess::reverseSort(catalog, false);
    expect(excludedAudio.replaceWithText("isolated nonempty cached audio"), "restore shared stem fixture");
    auto newerB = sharedB;
    newerB.revision = 2;
    panel.setCloudSongs({personalCloud, sharedA, newerB});
    TeamsWorkspaceTestAccess::checkCloudRow(catalog, newerB, {});
    TeamsWorkspaceTestAccess::checkCloudRow(catalog, personalCloud, first.id);
    panel.setCloudSongs({personalCloud, sharedA, sharedB});
    TeamsWorkspaceTestAccess::showLocal(catalog);
    panel.refreshFromStorageView();
    panel.openLocalLibrary();
    panel.showActiveSetlist();
    const auto next = panel.getAutoPlayNextSong(first.id);
    const auto previous = panel.getPlaybackAdjacentSong(second.id, -1, false);
    const auto wrapped = panel.getPlaybackAdjacentSong(second.id, 1, true);
    expect(next && previous && wrapped &&
               next->id == second.id && previous->id == first.id && wrapped->id == first.id &&
               activations == 0 && TeamsWorkspaceTestAccess::playingSong(panel) == first.id,
           "catalog refresh and navigation preserve active playback and its queue");
    expect(database.loadFileAsString() == before && provenance.loadFileAsString() == provenanceBefore,
           "catalog refresh does not rewrite songs, setlists, or Team provenance");

    TeamsWorkspaceTestAccess::activateLibrarySong(panel, localB);
    expect(activations == 1 && activatedSongId == songB && activatedIndex == -1 &&
               SongLibraryStore::loadLibrarySnapshot().activeSetlistId == personalList &&
               database.loadFileAsString() == before,
           "playing a downloaded song outside the queue uses normal loading without creating a Team setlist");
    TeamsWorkspaceTestAccess::openAddFromLibrary(panel);
    TeamsWorkspaceTestAccess::showCloud(catalog);
    auto addMenu = TeamsWorkspaceTestAccess::rowMenu(catalog,
        TeamsWorkspaceTestAccess::cloudRow(catalog, sharedA));
    TeamsWorkspaceTestAccess::trigger(*addMenu, L10n::tr("Agregar al setlist", "Add to setlist"));
    expect(panel.getSongCount() == 3 && panel.getSongIndexById(songA) == 2 && activations == 1,
           "downloaded songs can be appended to personal setlists without starting playback");
    expect(SongLibraryStore::loadLibrarySnapshot().setlists.size() == 2,
           "adding a Team download preserves the existing setlists without synthesizing another");
    const auto beforeEdits = SongLibraryStore::loadLibrarySnapshot();
    const auto previousLegacy = std::find_if(beforeEdits.setlists.begin(), beforeEdits.setlists.end(),
        [&](const SetlistData &candidate) { return candidate.id == legacyId; });
    expect(previousLegacy != beforeEdits.setlists.end() && previousLegacy->items.size() == 1 &&
               previousLegacy->items.front().pitchSemitones == 3 &&
               previousLegacy->items.front().tempoBpm == 110,
           "navigation and personal setlist insertion retain existing legacy settings before explicit edits");

    auto editedA = localA;
    editedA.title = "Mi parte local";
    expect(SongLibraryStore::updateSongMetadata(editedA) &&
               SongLibraryStore::updatePitchSemitonesForActiveSetlist(songA, -2) &&
               SongLibraryStore::updateTempoBpmForActiveSetlist(songA, 92) &&
               SongLibraryStore::updateStemVolumeForActiveSetlist(songA, "instrument", 0.57f),
           "downloaded songs accept ordinary metadata and personal setlist edits");
    panel.refreshFromStorageView();
    const auto snapshot = SongLibraryStore::loadLibrarySnapshot();
    const auto edited = std::find_if(snapshot.songs.begin(), snapshot.songs.end(),
        [&](const ImportedSongData &candidate) { return candidate.id == songA; });
    const auto legacy = std::find_if(snapshot.setlists.begin(), snapshot.setlists.end(),
        [&](const SetlistData &candidate) { return candidate.id == legacyId; });
    int pitch = 0;
    double tempo = 0;
    SongMixSnapshot mix;
    expect(edited != snapshot.songs.end() && edited->title == "Mi parte local",
           "downloaded song metadata edits persist in the ordinary local catalog");
    expect(SongLibraryStore::getActiveSetlistPitchSemitones(songA, pitch) && pitch == -2 &&
               SongLibraryStore::getActiveSetlistTempoBpm(songA, tempo) && tempo == 92 &&
               SongLibraryStore::getActiveSetlistMix(songA, mix) && !mix.stems.empty() &&
               std::abs(mix.stems.front().volume - 0.57f) < 0.0001f,
           "downloaded song playback adjustments persist locally");
    // Ordinary cached songs share playback adjustments across their local setlists.
    expect(legacy != snapshot.setlists.end() && legacy->items.size() == 1 &&
               legacy->items.front().songId == songA &&
               legacy->items.front().pitchSemitones == -2 &&
               legacy->items.front().tempoBpm == 92,
           "explicit song edits follow normal adjustment behavior in existing local setlists");
    expect(provenance.loadFileAsString() == provenanceBefore,
           "local metadata and playback edits preserve the immutable shared origin");
    TeamsWorkspaceTestAccess::activateSetlistSong(panel, songA);
    expect(activations == 2 && activatedSongId == songA && activatedIndex == 2 &&
               SongLibraryStore::loadLibrarySnapshot().activeSetlistId == personalList,
           "a downloaded song inside a personal setlist follows ordinary row activation");
    std::cout << "Teams downloads: normal local catalog, personal edits, preserved queue and provenance passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
