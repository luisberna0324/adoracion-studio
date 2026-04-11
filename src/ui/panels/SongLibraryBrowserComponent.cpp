#include "SongLibraryBrowserComponent.h"

#include "SongImportWizardComponent.h"
#include "../../cloud/CloudLibrarySyncService.h"
#include "../../data/Localization.h"

#include <algorithm>

namespace {
constexpr int kColTitle = 1;
constexpr int kColCloud = 2;
constexpr int kColStatus = 3;
constexpr int kColActions = 4;

bool songAlreadyInSetlist(const LibraryState &lib, const juce::String &setlistId,
                          const juce::String &songId) {
  if (setlistId.isEmpty() || songId.isEmpty())
    return false;
  for (const auto &sl : lib.setlists) {
    if (sl.id != setlistId)
      continue;
    return std::any_of(sl.items.begin(), sl.items.end(),
                       [&songId](const SetlistItem &it) {
                         return it.songId == songId;
                       });
  }
  return false;
}
} // namespace

void SongLibraryBrowserComponent::LibraryTable::mouseDoubleClick(
    const juce::MouseEvent &e) {
  TableListBox::mouseDoubleClick(e);
  if (browser != nullptr) {
    const auto pos = e.getPosition();
    const int row = getRowContainingPosition(pos.x, pos.y);
    if (row >= 0)
      browser->handleLoadRow(row);
  }
}

class SongLibraryBrowserComponent::UnifiedTableModel final
    : public juce::TableListBoxModel {
public:
  explicit UnifiedTableModel(SongLibraryBrowserComponent &o) : owner(o) {}

  int getNumRows() override {
    return static_cast<int>(owner.unifiedRows.size());
  }

  void paintRowBackground(juce::Graphics &g, int rowNumber, int width, int height,
                          bool rowIsSelected) override {
    if (!juce::isPositiveAndBelow(rowNumber,
                                  static_cast<int>(owner.unifiedRows.size())))
      return;
    const auto id = owner.unifiedRows[static_cast<size_t>(rowNumber)].songId;
    const bool loaded =
        id.isNotEmpty() && id == owner.highlightLoadedSongId;
    juce::Rectangle<int> r(0, 0, width, height);
    if (loaded)
      g.setColour(UITheme::accentSoft.withAlpha(0.35f));
    else if (rowIsSelected)
      g.setColour(UITheme::cardBgHover);
    else
      g.setColour(UITheme::cardBg);
    g.fillRect(r);
    g.setColour(UITheme::cardBorder.withAlpha(0.45f));
    g.drawRect(r, 1);
  }

  void paintCell(juce::Graphics &g, int rowNumber, int columnId, int width,
                 int height, bool rowIsSelected) override {
    juce::ignoreUnused(rowIsSelected);
    if (!juce::isPositiveAndBelow(rowNumber,
                                 static_cast<int>(owner.unifiedRows.size())))
      return;
    const auto &row = owner.unifiedRows[static_cast<size_t>(rowNumber)];
    const auto &songId = row.songId;
    g.setColour(UITheme::textPrimary);
    g.setFont(juce::FontOptions(13.0f));

    if (columnId == kColTitle) {
      juce::String t = owner.titleForSongId(songId);
      if (songId == owner.highlightLoadedSongId)
        t += L10n::tr("  (reproduciendo)", "  (playing)");
      g.drawFittedText(t, 8, 0, width - 12, height, juce::Justification::centredLeft,
                       1);
      return;
    }

    if (columnId == kColCloud) {
      juce::String label;
      if (owner.drivePreviewLoading)
        label = L10n::tr("...", "...");
      else if (!owner.driveFetchOk)
        label = L10n::tr("?", "?");
      else if (owner.isInCloudColumn(songId, row.cloudOnly))
        label = L10n::tr("Si", "Yes");
      else
        label = L10n::tr("No", "No");
      g.drawFittedText(label, 4, 0, width - 8, height, juce::Justification::centred,
                       1);
      return;
    }

    if (columnId == kColStatus) {
      juce::String status = owner.busyTextFor(songId);
      if (status.isEmpty() && row.cloudOnly)
        status = L10n::tr("Solo en la nube", "Cloud only");
      if (status.isEmpty() && owner.rowHasConflict(songId))
        status = L10n::tr("Revisar sync", "Review sync");
      if (status.isEmpty())
        status = "—";
      g.setFont(juce::FontOptions(12.0f));
      g.setColour(UITheme::textSecondary);
      g.drawFittedText(status, 6, 0, width - 10, height,
                        juce::Justification::centredLeft, 1);
      return;
    }

    if (columnId == kColActions) {
      g.setColour(UITheme::textPrimary);
      g.setFont(juce::FontOptions(16.0f));
      g.drawFittedText("···", 0, 0, width, height, juce::Justification::centred, 1);
    }
  }

  juce::Component *refreshComponentForCell(int, int, bool,
                                           juce::Component *) override {
    return nullptr;
  }

  int getColumnAutoSizeWidth(int columnId) override {
    switch (columnId) {
    case kColTitle:
      return 320;
    case kColCloud:
      return 72;
    case kColStatus:
      return 150;
    case kColActions:
      return 48;
    default:
      return 40;
    }
  }

  void cellClicked(int rowNumber, int columnId,
                   const juce::MouseEvent &e) override {
    if (columnId == kColActions &&
        juce::isPositiveAndBelow(rowNumber,
                                 static_cast<int>(owner.unifiedRows.size())))
      owner.showRowActionsMenu(rowNumber, e.getPosition());
  }

private:
  SongLibraryBrowserComponent &owner;
};

SongLibraryBrowserComponent::SongLibraryBrowserComponent(
    SubmitImportedSongFn submitImportedSongIn,
    juce::String setlistIdForPickIn, juce::String highlightLoadedSongIdIn,
    std::function<void(const juce::String &songId)> onLocalSongChosenIn,
    const bool primeImportEnabledIn,
    std::function<void()> onLibraryStorageChangedIn)
    : submitImportedSong(std::move(submitImportedSongIn)),
      setlistIdForPick(std::move(setlistIdForPickIn)),
      highlightLoadedSongId(std::move(highlightLoadedSongIdIn)),
      onLocalSongChosen(std::move(onLocalSongChosenIn)),
      primeImportEnabled(primeImportEnabledIn),
      onLibraryStorageChanged(std::move(onLibraryStorageChangedIn)) {
  setSize(760, 540);

  filterEditor.setMultiLine(false);
  filterEditor.setReturnKeyStartsNewLine(false);
  filterEditor.setTextToShowWhenEmpty(
      L10n::tr("Filtrar por nombre...", "Filter by name..."),
      UITheme::textSecondary.withAlpha(0.65f));
  filterEditor.setColour(juce::TextEditor::backgroundColourId, UITheme::cardBgHover);
  filterEditor.setColour(juce::TextEditor::textColourId, UITheme::textPrimary);
  filterEditor.setColour(juce::TextEditor::outlineColourId,
                         UITheme::cardBorder.withAlpha(0.7f));
  filterEditor.onTextChange = [this] { handleFilterChanged(); };
  addAndMakeVisible(filterEditor);

  addSongButton.setButtonText(L10n::tr("Agregar cancion", "Add Song"));
  addSongButton.setColour(juce::TextButton::buttonColourId, UITheme::accent);
  addSongButton.setColour(juce::TextButton::buttonOnColourId, UITheme::accentSoft);
  addSongButton.setColour(juce::TextButton::textColourOffId, UITheme::onAccent);
  addSongButton.setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  addSongButton.onClick = [this] { launchImportWizard(); };
  addAndMakeVisible(addSongButton);

  tableModel = std::make_unique<UnifiedTableModel>(*this);
  table.setModel(tableModel.get());
  table.setColour(juce::TableListBox::backgroundColourId, UITheme::cardBg);
  table.setOutlineThickness(0);
  table.setMultipleSelectionEnabled(false);
  table.getHeader().addColumn(L10n::tr("Titulo", "Title"), kColTitle, 320, 120, 900,
                              juce::TableHeaderComponent::defaultFlags);
  table.getHeader().addColumn(L10n::tr("Nube", "Cloud"), kColCloud, 72, 48, 120,
                              juce::TableHeaderComponent::defaultFlags);
  table.getHeader().addColumn(L10n::tr("Estado", "Status"), kColStatus, 160, 80,
                              280, juce::TableHeaderComponent::defaultFlags);
  table.getHeader().addColumn(L10n::tr("Acciones", "Actions"), kColActions, 48, 40,
                              80, juce::TableHeaderComponent::defaultFlags);
  table.browser = this;
  addAndMakeVisible(table);

  refreshLocalList();
  startRemotePreviewFetch();
}

SongLibraryBrowserComponent::~SongLibraryBrowserComponent() {
  table.setModel(nullptr);
}

void SongLibraryBrowserComponent::paint(juce::Graphics &g) {
  g.fillAll(UITheme::appBg);
}

void SongLibraryBrowserComponent::resized() {
  auto area = getLocalBounds().reduced(14);
  auto top = area.removeFromTop(34);
  filterEditor.setBounds(top.removeFromLeft(juce::jmax(200, top.getWidth() - 150)));
  top.removeFromLeft(10);
  addSongButton.setBounds(top.removeFromRight(140));
  area.removeFromTop(10);
  table.setBounds(area);
}

void SongLibraryBrowserComponent::refreshLocalList() {
  localLibrary = SongLibraryStore::loadLibrarySnapshot();
  rebuildFilteredLists();
}

void SongLibraryBrowserComponent::handleFilterChanged() {
  rebuildFilteredLists();
}

void SongLibraryBrowserComponent::rebuildFilteredLists() {
  const juce::String q = filterEditor.getText().trim().toLowerCase();
  unifiedRows.clear();

  for (const auto &song : localLibrary.songs) {
    if (song.id.isEmpty())
      continue;
    if (setlistIdForPick.isNotEmpty() &&
        songAlreadyInSetlist(localLibrary, setlistIdForPick, song.id))
      continue;
    const auto title = song.title.trim().toLowerCase();
    if (!q.isEmpty() && !title.contains(q))
      continue;
    unifiedRows.push_back({song.id, false});
  }

  if (driveFetchOk) {
    std::vector<juce::String> localIds;
    localIds.reserve(localLibrary.songs.size());
    for (const auto &s : localLibrary.songs)
      if (s.id.isNotEmpty())
        localIds.push_back(s.id);
    for (const auto &song : remoteLibrary.songs) {
      if (song.id.isEmpty())
        continue;
      if (std::find(localIds.begin(), localIds.end(), song.id) != localIds.end())
        continue;
      const auto title = song.title.trim().toLowerCase();
      if (!q.isEmpty() && !title.contains(q))
        continue;
      unifiedRows.push_back({song.id, true});
    }
  }

  table.updateContent();
  repaint();
}

void SongLibraryBrowserComponent::startRemotePreviewFetch() {
  drivePreviewLoading = true;
  driveFetchOk = false;
  juce::Component::SafePointer<SongLibraryBrowserComponent> safe(this);
  juce::Thread::launch([safe]() {
    LibraryState remote;
    juce::String err;
    const bool ok =
        CloudLibrarySyncService::fetchRemoteLibraryPreview(remote, err);
    (void)err;
    juce::MessageManager::callAsync([safe, ok, remote]() mutable {
      if (safe == nullptr)
        return;
      safe->remoteLibrary = std::move(remote);
      safe->driveFetchOk = ok;
      if (!ok)
        safe->remoteLibrary = {};
      safe->drivePreviewLoading = false;
      safe->rebuildFilteredLists();
    });
  });
}

void SongLibraryBrowserComponent::refetchRemotePreview() {
  startRemotePreviewFetch();
}

void SongLibraryBrowserComponent::closeParentDialog() {
  if (auto *dw = findParentComponentOfClass<juce::DialogWindow>()) {
    dw->exitModalState(1);
    dw->setVisible(false);
  }
}

void SongLibraryBrowserComponent::notifyStorageChanged() {
  if (onLibraryStorageChanged)
    onLibraryStorageChanged();
}

const ImportedSongData *SongLibraryBrowserComponent::findLocalSong(
    const juce::String &songId) const {
  if (songId.isEmpty())
    return nullptr;
  for (const auto &s : localLibrary.songs) {
    if (s.id == songId)
      return &s;
  }
  return nullptr;
}

const ImportedSongData *SongLibraryBrowserComponent::findRemoteSong(
    const juce::String &songId) const {
  if (songId.isEmpty())
    return nullptr;
  for (const auto &s : remoteLibrary.songs) {
    if (s.id == songId)
      return &s;
  }
  return nullptr;
}

bool SongLibraryBrowserComponent::isInCloudColumn(const juce::String &songId,
                                                  const bool cloudOnlyRow) const {
  if (cloudOnlyRow)
    return true;
  if (driveFetchOk && findRemoteSong(songId) != nullptr)
    return true;
  const auto *loc = findLocalSong(songId);
  if (loc == nullptr)
    return false;
  for (const auto &st : loc->stems) {
    if (st.remoteFileId.trim().isNotEmpty())
      return true;
  }
  return false;
}

bool SongLibraryBrowserComponent::rowHasConflict(const juce::String &songId) const {
  if (!driveFetchOk)
    return false;
  const auto *loc = findLocalSong(songId);
  const auto *rem = findRemoteSong(songId);
  if (loc == nullptr || rem == nullptr)
    return false;
  if (loc->snapshotVersion != rem->snapshotVersion)
    return true;
  const juce::int64 da = loc->updatedMs;
  const juce::int64 db = rem->updatedMs;
  const juce::int64 diff = da > db ? da - db : db - da;
  return diff > 1;
}

juce::String SongLibraryBrowserComponent::busyTextFor(const juce::String &songId) const {
  const auto it = busyBySongId.find(songId);
  return it != busyBySongId.end() ? it->second : juce::String();
}

void SongLibraryBrowserComponent::setBusy(const juce::String &songId,
                                           juce::String text) {
  if (songId.isEmpty())
    return;
  busyBySongId[songId] = std::move(text);
}

void SongLibraryBrowserComponent::clearBusy(const juce::String &songId) {
  busyBySongId.erase(songId);
}

void SongLibraryBrowserComponent::handleLoadRow(const int row) {
  if (!juce::isPositiveAndBelow(row, static_cast<int>(unifiedRows.size())))
    return;
  const auto &ur = unifiedRows[static_cast<size_t>(row)];
  if (ur.cloudOnly) {
    // In setlist-pick mode: add to setlist + download in background, close browser.
    if (setlistIdForPick.isNotEmpty()) {
      addCloudSongToSetlistAndDownload(ur.songId);
      return;
    }
    // In main library mode: show the normal confirm + download dialog.
    confirmPullThenRun(ur.songId);
    return;
  }
  if (ur.songId.isEmpty())
    return;
  if (onLocalSongChosen)
    onLocalSongChosen(ur.songId);
  closeParentDialog();
}

void SongLibraryBrowserComponent::showRowActionsMenu(const int row,
                                                       juce::Point<int> anchor) {
  if (!juce::isPositiveAndBelow(row, static_cast<int>(unifiedRows.size())))
    return;
  const auto &ur = unifiedRows[static_cast<size_t>(row)];
  const auto &songId = ur.songId;
  if (busyTextFor(songId).isNotEmpty())
    return;

  juce::PopupMenu m;
  const int kLoad = 1;
  const int kPull = 2;
  const int kPush = 3;
  const int kDelete = 4;

  if (!ur.cloudOnly)
    m.addItem(kLoad, L10n::tr("Cargar", "Load"), true, false);

  const bool canCloud =
      driveFetchOk && CloudLibrarySyncService::hasValidDriveSession();
  m.addItem(kPull,
            L10n::tr("Descargar desde Drive", "Download from Drive"),
            canCloud, false);

  m.addItem(kPush, L10n::tr("Subir a Drive", "Upload to Drive"),
            canCloud && !ur.cloudOnly, false);

  if (setlistIdForPick.isEmpty())
    m.addItem(kDelete, L10n::tr("Eliminar de la biblioteca", "Remove from library"),
              true, false);
  else if (songAlreadyInSetlist(localLibrary, setlistIdForPick, songId))
    m.addItem(kDelete, L10n::tr("Quitar del setlist", "Remove from setlist"), true,
              false);

  m.showMenuAsync(
      juce::PopupMenu::Options()
          .withParentComponent(&table)
          .withTargetScreenArea(table.localAreaToGlobal(
              juce::Rectangle<int>(anchor.x, anchor.y, 1, 1))),
      juce::ModalCallbackFunction::create(
          [this, songId, ur](const int result) {
            if (result == 0)
              return;
            if (result == kLoad && !ur.cloudOnly) {
              if (onLocalSongChosen)
                onLocalSongChosen(songId);
              closeParentDialog();
            } else if (result == kPull)
              confirmPullThenRun(songId);
            else if (result == kPush)
              runPushAsync(songId);
            else if (result == kDelete)
              confirmDelete(songId);
          }));
}

void SongLibraryBrowserComponent::confirmPullThenRun(const juce::String &songId) {
  if (songId.isEmpty() || !driveFetchOk)
    return;
  if (findLocalSong(songId) != nullptr) {
    juce::Component::SafePointer<SongLibraryBrowserComponent> safe(this);
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        L10n::tr("Sobrescribir version local", "Overwrite local version"),
        L10n::tr("La version local sera reemplazada por la copia de Drive.",
                 "The local copy will be replaced with the version from Drive."),
        L10n::tr("Reemplazar", "Replace"), L10n::tr("Cancelar", "Cancel"), this,
        juce::ModalCallbackFunction::create([safe, songId](const int result) {
          if (safe == nullptr || result != 1)
            return;
          safe->runPullAsync(songId);
        }));
    return;
  }
  runPullAsync(songId);
}

void SongLibraryBrowserComponent::addCloudSongToSetlistAndDownload(
    const juce::String &songId) {
  if (songId.isEmpty())
    return;

  // 1. Find the song in the remote library snapshot.
  const ImportedSongData *remoteSong = findRemoteSong(songId);
  if (remoteSong == nullptr) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        L10n::tr("Error", "Error"),
        L10n::tr("No se encontro la cancion en Drive.",
                 "Song not found in Drive."));
    return;
  }

  // 2. Upsert into local library (with empty stem paths — will be filled by download).
  ImportedSongData skeleton = *remoteSong;
  for (auto &stem : skeleton.stems)
    stem.storedPath = {}; // no local file yet

  juce::String upsertErr;
  if (!SongLibraryStore::upsertLibrarySong(skeleton, &upsertErr)) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        L10n::tr("Error", "Error"),
        upsertErr.isNotEmpty() ? upsertErr
            : L10n::tr("No se pudo agregar la cancion.", "Couldn't add the song."));
    return;
  }

  // 3. Add to the setlist that was passed in (setlistIdForPick).
  if (setlistIdForPick.isNotEmpty())
    SongLibraryStore::addSongToSetlist(setlistIdForPick, songId);

  // 4. Notify SongListPanel (and any other listeners) so it refreshes immediately.
  notifyStorageChanged();

  // 5. Enqueue background download — SongDownloadManager will call notifyStorageChanged
  //    again on completion via the ChangeListener in SongListPanel.
  SongDownloadManager::getInstance().startDownload(songId, [](bool ok, juce::String err) {
    // On failure, show an alert on the message thread.
    if (!ok && err.isNotEmpty()) {
      juce::MessageManager::callAsync([err] {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            L10n::tr("Error de descarga", "Download error"), err);
      });
    }
  });

  // 6. Close the browser — user can now see progress on the setlist row.
  closeParentDialog();
}

void SongLibraryBrowserComponent::confirmDelete(const juce::String &songId) {
  juce::Component::SafePointer<SongLibraryBrowserComponent> safe(this);
  if (setlistIdForPick.isEmpty()) {
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        L10n::tr("Eliminar cancion", "Remove song"),
        L10n::tr("Se eliminara de la biblioteca y de todos los setlists.",
                 "This removes the song from the library and every setlist."),
        L10n::tr("Eliminar", "Delete"), L10n::tr("Cancelar", "Cancel"), this,
        juce::ModalCallbackFunction::create([safe, songId](const int result) {
          if (safe == nullptr || result != 1)
            return;
          safe->runDeleteAsync(songId);
        }));
  } else {
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        L10n::tr("Quitar del setlist", "Remove from setlist"),
        L10n::tr("La cancion permanecera en la biblioteca.",
                 "The song will stay in your library."),
        L10n::tr("Quitar", "Remove"), L10n::tr("Cancelar", "Cancel"), this,
        juce::ModalCallbackFunction::create([safe, songId](const int result) {
          if (safe == nullptr || result != 1)
            return;
          safe->runDeleteAsync(songId);
        }));
  }
}

void SongLibraryBrowserComponent::runPullAsync(const juce::String &songId) {
  setBusy(songId, L10n::tr("Descargando...", "Downloading..."));
  table.repaint();
  juce::Component::SafePointer<SongLibraryBrowserComponent> safe(this);
  juce::Thread::launch([safe, songId]() {
    const auto result = CloudLibrarySyncService::pullSongFromDrive(songId);
    juce::MessageManager::callAsync([safe, songId, result]() {
      if (safe == nullptr)
        return;
      safe->clearBusy(songId);
      safe->refreshLocalList();
      if (result.ok) {
        safe->notifyStorageChanged();
        safe->refetchRemotePreview();
      } else if (result.message.isNotEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, L10n::tr("Drive", "Drive"),
            result.message);
      }
      safe->table.updateContent();
      safe->repaint();
    });
  });
}

void SongLibraryBrowserComponent::runPushAsync(const juce::String &songId) {
  setBusy(songId, L10n::tr("Subiendo...", "Uploading..."));
  table.repaint();
  juce::Component::SafePointer<SongLibraryBrowserComponent> safe(this);
  juce::Thread::launch([safe, songId]() {
    const auto result = CloudLibrarySyncService::pushSongToDrive(songId);
    juce::MessageManager::callAsync([safe, songId, result]() {
      if (safe == nullptr)
        return;
      safe->clearBusy(songId);
      safe->refreshLocalList();
      if (result.ok) {
        safe->notifyStorageChanged();
        safe->refetchRemotePreview();
      } else if (result.message.isNotEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, L10n::tr("Drive", "Drive"),
            result.message);
      }
      safe->table.updateContent();
      safe->repaint();
    });
  });
}

void SongLibraryBrowserComponent::runDeleteAsync(const juce::String &songId) {
  setBusy(songId, L10n::tr("Eliminando...", "Removing..."));
  table.repaint();
  juce::Component::SafePointer<SongLibraryBrowserComponent> safe(this);
  const juce::String setlistCopy = setlistIdForPick;
  juce::Thread::launch([safe, songId, setlistCopy]() {
    juce::String err;
    bool ok = false;
    if (setlistCopy.isEmpty())
      ok = SongLibraryStore::removeSongFromLibrary(songId, &err);
    else
      ok = SongLibraryStore::removeSongFromSetlist(setlistCopy, songId);

    juce::MessageManager::callAsync([safe, songId, ok, err]() {
      if (safe == nullptr)
        return;
      safe->clearBusy(songId);
      safe->refreshLocalList();
      if (ok)
        safe->notifyStorageChanged();
      else if (err.isNotEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            L10n::tr("Biblioteca", "Library"), err);
      }
      safe->table.updateContent();
      safe->repaint();
    });
  });
}

juce::String SongLibraryBrowserComponent::titleForSongId(
    const juce::String &songId) const {
  for (const auto &s : localLibrary.songs) {
    if (s.id == songId)
      return s.title.isNotEmpty() ? s.title
                                    : L10n::tr("Sin titulo", "Untitled");
  }
  for (const auto &s : remoteLibrary.songs) {
    if (s.id == songId)
      return s.title.isNotEmpty() ? s.title
                                    : L10n::tr("Sin titulo", "Untitled");
  }
  return songId;
}

void SongLibraryBrowserComponent::launchImportWizard() {
  auto *editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
  auto *centerTarget =
      editor != nullptr ? static_cast<juce::Component *>(editor)
                        : static_cast<juce::Component *>(this);

  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = L10n::tr("Importar cancion", "Import Song");
  options.dialogBackgroundColour = UITheme::appBg;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = true;
  options.componentToCentreAround = centerTarget;

  auto safeThis = juce::Component::SafePointer<SongLibraryBrowserComponent>(this);
  auto *panel = new SongImportWizardComponent(
      [safeThis](ImportedSongData song, bool syncWithDrive,
                 juce::String &errorMessage) {
        if (safeThis == nullptr || !safeThis->submitImportedSong) {
          errorMessage = L10n::tr("La vista ya no esta disponible.",
                                  "This view is no longer available.");
          return false;
        }
        if (!safeThis->submitImportedSong(std::move(song), syncWithDrive,
                                          errorMessage))
          return false;
        safeThis->refreshLocalList();
        safeThis->notifyStorageChanged();
        safeThis->refetchRemotePreview();
        return true;
      },
      primeImportEnabled);
  panel->refreshThemeColours();
  options.content.setOwned(panel);

  if (auto *window = options.launchAsync()) {
    window->setResizable(true, true);
    window->centreAroundComponent(centerTarget, 780, 580);
  }
}
