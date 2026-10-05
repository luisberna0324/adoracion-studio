#include "PluginEditor.h"
#include "PluginEditorSupport.h"
#include "ui/panels/TeamsPanel.h"
#include "ui/panels/SongListPanel.h"
#include "ui/panels/WaveformBar.h"
#include "ui/panels/SettingsPanel.h"
#include "data/TeamsLocalStore.h"
#include "data/Localization.h"
#include "data/AppSettings.h"
#include "ui/components/AppModal.h"
#include "ui/learning/LearningGuideOverlay.h"

using namespace PluginEditorSupport;

namespace {
juce::var payload(std::initializer_list<std::pair<juce::Identifier, juce::var>> properties) {
  auto *object = new juce::DynamicObject();
  for (const auto &property : properties)
    object->setProperty(property.first, property.second);
  return object;
}

juce::String failureText(const CloudTransferError &error) {
  if (error.code == "USER_NOT_FOUND")
    return L10n::tr("Ese correo aún no tiene una cuenta de PlayWorship.", "That email does not have a PlayWorship account yet.");
  if (error.code == "EMAIL_VERIFICATION_REQUIRED")
    return L10n::tr("Verifica tu correo para administrar Teams.", "Verify your email to manage Teams.");
  if (error.code == "TEAMS_SUBSCRIPTION_REQUIRED" || error.code == "TEAM_SUBSCRIPTION_REQUIRED")
    return L10n::tr("La suscripción del propietario debe tener Teams habilitado.", "The owner's subscription must have Teams enabled.");
  if (error.code == "TEAM_ROLE_REQUIRED" || error.code == "TEAM_ACCESS_DENIED")
    return L10n::tr("No tienes permiso para realizar esta acción en el equipo.", "You do not have permission for this team action.");
  if (error.code == "TEAM_CLOUD_UNAVAILABLE" || error.code == "NO_SYNC_PLAN" || error.code == "TRIAL_EXPIRED")
    return L10n::tr("La nube del propietario no está disponible en este momento.", "The owner's cloud is currently unavailable.");
  if (error.code == "OTHER_TEAM_MEMBERSHIP")
    return L10n::tr("Este usuario ya pertenece a un Team. Debe salir del actual antes de unirse a otro.", "This user already belongs to a Team. They must leave it before joining another.");
  if (error.code == "ALREADY_MEMBER")
    return L10n::tr("Ese usuario ya pertenece al equipo.", "That user already belongs to the team.");
  if (error.code == "SONG_NOT_FOUND")
    return L10n::tr("La canción ya no está en la nube del propietario. Actualiza Nube.", "The song is no longer in the owner's cloud. Refresh Cloud.");
  if (error.code == "REVISION_CONFLICT")
    return L10n::tr("La canción cambió en la nube. Actualiza Nube y vuelve a descargarla.", "The cloud song changed. Refresh Cloud and download it again.");
  if (error.httpCode == 404)
    return L10n::tr("Teams todavía no está disponible en este servidor.", "Teams is not available on this server yet.");
  return error.message.isNotEmpty() ? error.message : error.code;
}
} // namespace

void MTControlsAudioProcessorEditor::invalidateCloudAccountView() {
  ++teamsOnboardingGeneration;
  if (teamsOnboardingClient) teamsOnboardingClient->cancelAll();
  teamsOnboardingPending = teamsMembershipsVerified = false;
  hasTeamsMembership = false;
  teamsLearningSources.clear();
  teamsPendingRevocations = juce::var();
  teamsWelcomesShown.clear();
  teamsNextPollMs = teamsNextGuideCheckMs = 0;
  teamsLearningDownloaded = teamsLearningAdded = false;
  if (teamsWelcomeVisible) appModal.reset();
  teamsWelcomeVisible = false;
  if (learningLessonId.startsWith("teams-cloud-") && learningGuide) pauseLearningGuide();
  canManageSharedCloud = false;
  ++cloudLibrarySearchGeneration;
  cloudCatalogSongs.clear();
  closeTeams();
  closeSettings();
  if (teamsClient) teamsClient->cancelAll();
  // This service invalidates queued materialization as well as network callbacks.
  teamsPreparation.reset();
  if (cloudLibraryClient) cloudLibraryClient->cancelAll();
  if (cloudSettingsClient) cloudSettingsClient->cancelAll();
  if (songSyncService) songSyncService->cancelAll();
  if (songListPanelRaw) {
    for (const auto &id : cloudDownloadsInProgress)
      songListPanelRaw->clearCloudUploadProgress(juce::String(id));
    for (const auto &id : cloudUploadsInProgress)
      songListPanelRaw->clearCloudUploadProgress(juce::String(id));
    songListPanelRaw->setCloudSongs({});
    songListPanelRaw->setCloudSearchLoading(false);
  }
  cloudDownloadsInProgress.clear();
  cloudUploadsInProgress.clear();
  cloudDeletesInProgress.clear();
  cloudSettingsInProgress.clear();
  cloudSettingsStatusInProgress.clear();
}

void MTControlsAudioProcessorEditor::showTeams() {
  if ((!canManageSharedCloud && !hasTeamsMembership) || !activeSession.allowed) return;
  if (teamsOverlay) { teamsOverlay->toFront(true); return; }
  closeSettings();
  teamsOverlay = std::make_unique<TeamsPanel>();
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  teamsOverlay->onClose = [safe] {
    juce::MessageManager::callAsync([safe] { if (safe) safe->closeTeams(); });
  };
  teamsOverlay->onAction = [safe](const juce::String &operation, juce::var data) {
    if (safe) safe->handleTeamsAction(operation, std::move(data));
  };
  addAndMakeVisible(*teamsOverlay);
  teamsOverlay->setBounds(getLocalBounds());
  teamsOverlay->toFront(true);
  updatePhoneOrientation();
  const auto cache = TeamsLocalStore::loadOverview(activeSession.uid);
  if (cache.isObject()) teamsOverlay->setOverview(cache);
  handleTeamsAction("list", {});
}

void MTControlsAudioProcessorEditor::closeTeams() {
  teamsOverlay.reset();
  teamsDetails = juce::var();
  ++teamsDetailsGeneration;
  updatePhoneOrientation();
}

void MTControlsAudioProcessorEditor::displayTeamDetails(juce::var details) {
  teamsDetails = details.clone();
  if (teamsOverlay) teamsOverlay->setDetails(details);
}

void MTControlsAudioProcessorEditor::handleTeamsAction(const juce::String &operation, juce::var data) {
  const auto uid = activeSession.uid;
  if (uid.isEmpty() || !activeSession.allowed || activeSession.refreshToken.isEmpty()) {
    if (teamsOverlay)
      teamsOverlay->setStatus(L10n::tr("Inicia sesión para usar Teams.", "Sign in to use Teams."), true);
    return;
  }
  const auto teamId = data.getProperty("teamId", {}).toString();
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  if (operation != "list" && operation != "details" &&
      operation != "add-member" && operation != "remove-member" && operation != "leave")
    return;
  if ((operation == "add-member" || operation == "remove-member") && !canManageSharedCloud) return;
  if (!teamsClient) teamsClient = std::make_unique<TeamsClient>();
  if (teamsOverlay) teamsOverlay->setBusy(true);
  const auto generation = operation == "details" ? ++teamsDetailsGeneration : teamsDetailsGeneration;
  if (operation == "details") {
    if (auto *request = data.getDynamicObject()) request->setProperty("includeSongs", false);
    const auto cached = TeamsLocalStore::loadDetails(uid, teamId);
    if (cached.isObject()) displayTeamDetails(cached);
  }
  teamsClient->request({getApiBaseUrl(), activeSession.firebaseApiKey, activeSession.refreshToken}, operation, data,
      [safe, uid, operation, teamId, generation](juce::var result, CloudTransferError error) {
        if (!safe || safe->activeSession.uid != uid || !safe->teamsOverlay) return;
        if (generation != safe->teamsDetailsGeneration) return;
        safe->teamsOverlay->setBusy(false);
        if (error.code.isNotEmpty()) {
          if (error.httpCode == 401 || error.httpCode == 403 ||
              error.code == "TEAM_SUBSCRIPTION_REQUIRED" || error.code == "TEAMS_SUBSCRIPTION_REQUIRED" ||
              error.code == "TEAM_ROLE_REQUIRED" || error.code == "TEAM_ACCESS_DENIED") {
            safe->canManageSharedCloud = false;
            safe->teamsDetails = juce::var();
            safe->teamsOverlay->setOverview(payload({{"teams", juce::Array<juce::var>()},
                {"capabilities", payload({{"canManageSharedCloud", false}})}}));
            if (safe->settingsPanelRaw) safe->settingsPanelRaw->setTeamsManagementEnabled(false);
          }
          safe->teamsOverlay->setStatus(failureText(error), true);
          return;
        }
        if (operation == "list") {
          TeamsLocalStore::saveOverview(uid, result);
          const auto capability = result.getProperty("capabilities", {})
                                      .getProperty("canManageSharedCloud", false);
          safe->canManageSharedCloud = capability.isBool() && static_cast<bool>(capability);
          const auto previous = safe->teamsOverlay->selectedTeamId();
          safe->teamsOverlay->setOverview(result);
          if (previous.isNotEmpty() && previous == safe->teamsOverlay->selectedTeamId()) {
            safe->handleTeamsAction("details", payload({{"teamId", previous}}));
          }
        } else if (operation == "details") {
          TeamsLocalStore::saveDetails(uid, teamId, result);
          safe->displayTeamDetails(result);
        } else if (operation == "leave") {
          safe->teamsNextPollMs = 0;
          safe->closeTeams();
        } else {
          safe->teamsOverlay->setStatus(L10n::tr("Cambios guardados.", "Changes saved."));
          safe->handleTeamsAction("details", payload({{"teamId", teamId}}));
        }
      });
}

void MTControlsAudioProcessorEditor::downloadSharedCloudSong(
    const CloudSongSummary &source, const juce::String &targetSetlistId) {
  if (!teamsMembershipsVerified || !source.isSharedCloud() || source.ownerUid.isEmpty() || source.songId.isEmpty() ||
      source.revision <= 0 || activeSession.uid.isEmpty() || !activeSession.allowed ||
      activeSession.firebaseApiKey.isEmpty() || activeSession.refreshToken.isEmpty())
    return;
  // A confirmed removal must finish cleanup before any rejoined source downloads.
  if (const auto *pending = teamsPendingRevocations.getArray())
    for (const auto &entry : *pending)
      if (entry.getProperty("teamId", {}).toString() == source.teamId) return;
  const auto catalogId = source.catalogId();
  if (!cloudDownloadsInProgress.insert(catalogId.toStdString()).second) return;
  if (!teamsPreparation) teamsPreparation = std::make_unique<TeamsPreparationService>();
  const auto uid = activeSession.uid;
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  if (songListPanelRaw) songListPanelRaw->setCloudUploadProgress(catalogId, 0.0, "downloading");
  teamsPreparation->prepareSong(
      {getApiBaseUrl(), activeSession.firebaseApiKey, activeSession.refreshToken},
      uid, source.teamId, source.songId, source.revision,
      [safe, uid, catalogId](CloudTransferProgress progress) {
        if (safe && safe->activeSession.uid == uid && safe->songListPanelRaw)
          safe->songListPanelRaw->setCloudUploadProgress(catalogId, progress.fraction, progress.phase);
      },
      [safe, uid, catalogId, targetSetlistId](ImportedSongData song, CloudTransferError failure) {
        if (!safe || safe->activeSession.uid != uid) return;
        safe->cloudDownloadsInProgress.erase(catalogId.toStdString());
        if (safe->songListPanelRaw) safe->songListPanelRaw->clearCloudUploadProgress(catalogId);
        if (failure.code.isNotEmpty()) {
          juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
              L10n::tr("No se pudo descargar la canción", "Couldn't download song"), failureText(failure));
          safe->refreshCloudLibrary({});
          return;
        }
        const bool added = targetSetlistId.isNotEmpty() &&
            SongLibraryStore::addSongToSetlist(targetSetlistId, song.id);
        if (safe->songListPanelRaw) safe->songListPanelRaw->refreshFromStorageView();
        safe->refreshCloudLibrary({});
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            added ? L10n::tr("Canción agregada", "Song added")
                  : L10n::tr("Canción descargada", "Song downloaded"),
            added ? L10n::tr("La canción se descargó y agregó al setlist.", "The song was downloaded and added to the setlist.")
                  : L10n::tr("La canción ya está disponible en la Biblioteca local.", "The song is now available in the local Library."));
      });
}

void MTControlsAudioProcessorEditor::appendTeamsLearningLessons() {
  for (const auto &source : teamsLearningSources)
    learningLessons.push_back(playworship::learning::makeSharedCloudLesson(
        source.lessonId, source.owner, learningEnglishContent));
}

void MTControlsAudioProcessorEditor::openTeamsLearningCloud() {
  if (!songListPanelRaw || playbackSession.isRemote()) return;
  closeSettings();
  phonePadsOpen = false;
  isPadsCollapsed = true;
  isSetlistCollapsed = false;
  isSetlistDrawerOpen = true;
  songListPanelRaw->openCloudLibrary();
  resized();
  repaint();
}

void MTControlsAudioProcessorEditor::updateTeamsOnboarding() {
  if (!mainUiInitialized || activeSession.uid.isEmpty() || !activeSession.allowed ||
      activeSession.refreshToken.isEmpty() || activeSession.firebaseApiKey.isEmpty()) return;
  processTeamsRevocations();
  const auto now = juce::Time::currentTimeMillis();
  if (!teamsOnboardingPending && now >= teamsNextPollMs) {
    teamsNextPollMs = now + 60000;
    teamsOnboardingPending = true;
    if (!teamsOnboardingClient) teamsOnboardingClient = std::make_unique<TeamsClient>();
    const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
    const auto uid = activeSession.uid;
    const auto generation = teamsOnboardingGeneration;
    teamsOnboardingClient->request(
        {getApiBaseUrl(), activeSession.firebaseApiKey, activeSession.refreshToken}, "list", {},
        [safe, uid, generation](juce::var result, CloudTransferError error) {
          if (!safe || uid != safe->activeSession.uid || generation != safe->teamsOnboardingGeneration) return;
          safe->teamsOnboardingPending = false;
          const auto *sources = result.getProperty("teams", {}).getArray();
          safe->teamsMembershipsVerified = error.code.isEmpty() && sources != nullptr;
          if (!safe->teamsMembershipsVerified) return; // A network failure is not revocation.
          // Explicit server removal receipts only: outages/expired plans are not removals.
          if (result.getProperty("revocations", {}).isArray()) {
            const auto receipts = result.getProperty("revocations", {});
            TeamsLocalStore::saveDetails(uid, "revocation-pending", payload({{"items", receipts}}));
            safe->teamsPendingRevocations = receipts;
          }
          TeamsLocalStore::saveOverview(uid, result);
          const auto memberships = result.getProperty("memberships", {});
          safe->hasTeamsMembership = memberships.isArray() && !memberships.getArray()->isEmpty();
          std::vector<TeamsLearningSource> next;
          for (const auto &entry : *sources) {
            if (next.size() >= 100) break;
            if (entry.getProperty("role", {}).toString() != "member") continue;
            const auto teamId = entry.getProperty("id", {}).toString();
            const auto owner = entry.getProperty("ownerEmail", {}).toString();
            const auto ownerUid = entry.getProperty("ownerUid", {}).toString();
            const auto membership = entry.getProperty("membershipId", "legacy").toString();
            const auto lessonId = TeamsLocalStore::onboardingLessonId(uid, teamId, membership);
            if (lessonId.isEmpty() || owner.isEmpty() || ownerUid.isEmpty() || ownerUid == uid) continue;
            if (std::none_of(next.begin(), next.end(), [&](const auto &s) { return s.teamId == teamId; }))
              next.push_back({teamId, owner, lessonId});
          }
          safe->teamsLearningSources = std::move(next);
          if (safe->settingsPanelRaw)
            safe->settingsPanelRaw->setTeamsManagementEnabled(safe->canManageSharedCloud || safe->hasTeamsMembership);
          if (safe->learningGuide) {
            if (safe->learningGuide->isShowingCatalog()) safe->showLearningCatalog();
            else if (safe->learningLessonId.startsWith("teams-cloud-") &&
                std::none_of(safe->teamsLearningSources.begin(), safe->teamsLearningSources.end(),
                  [&](const auto &s) { return s.lessonId == safe->learningLessonId; })) {
              safe->pauseLearningGuide();
            }
          }
        });
  }
  // Do not read the library from the UI animation loop; only the active course
  // needs a low-frequency check, and never while audio is playing.
  if (learningLessonId.startsWith("teams-cloud-") && learningGuide &&
      now >= teamsNextGuideCheckMs && !playbackSession.isSongPlaying()) {
    teamsNextGuideCheckMs = now + 1500;
    teamsLearningDownloaded = teamsLearningAdded = false;
    const auto source = std::find_if(teamsLearningSources.begin(), teamsLearningSources.end(),
        [this](const auto &s) { return s.lessonId == learningLessonId; });
    if (source != teamsLearningSources.end() && teamsMembershipsVerified) {
      const auto library = SongLibraryStore::loadLibrarySnapshot();
      for (const auto &link : TeamsLocalStore::loadSongLinks(activeSession.uid, source->teamId)) {
        const auto song = std::find_if(library.songs.begin(), library.songs.end(),
            [&](const auto &s) { return s.id == link.localSongId; });
        if (song == library.songs.end() || !TeamsPreparationService::isPreparedSongUsable(*song)) continue;
        teamsLearningDownloaded = true;
        for (const auto &setlist : library.setlists)
          for (const auto &item : setlist.items)
            if (item.songId == song->id) teamsLearningAdded = true;
      }
    }
  }
  // A welcome never steals a live performance, an editor, another guide, or a modal.
  if (!teamsMembershipsVerified || playbackSession.isSongPlaying() || playbackSession.isRemote() ||
      songLoadBusy || welcomeModal || appModal || reauthPromptModal || learningGuide || teamsOverlay ||
      (songListPanelRaw && songListPanelRaw->isSongEditorOpen()) ||
      (waveformBarPtr && waveformBarPtr->getExtraHeightForHostLayout() > 0) ||
      settingsPanelRaw || practiceOverlay || juce::Component::getCurrentlyModalComponent() ||
      !isShowing() || !juce::Process::isForegroundProcess()) return;
  if (!learningProgress) learningProgress = std::make_unique<playworship::learning::ProgressStore>();
  for (const auto &source : teamsLearningSources) {
    const auto lesson = playworship::learning::makeSharedCloudLesson(source.lessonId, source.owner,
        AppSettings::getLanguage() == AppLanguage::English);
    if (learningProgress->hasProgress(lesson) || teamsWelcomesShown.contains(source.lessonId)) continue;
    teamsWelcomesShown.add(source.lessonId);
    teamsWelcomeVisible = true;
    const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
    const auto uid = activeSession.uid;
    const auto generation = teamsOnboardingGeneration;
    appModal = std::make_unique<AppModal>(*this, AppModal::Kind::Confirm,
        L10n::tr("Tienes acceso a una nube compartida", "You have access to a shared cloud"),
        source.owner + L10n::tr(" ha compartido sus canciones contigo. Puedes descargarlas y agregarlas a tus setlists.",
            " has shared their songs with you. You can download them and add them to your setlists."),
        L10n::tr("Ver canciones", "View songs"), L10n::tr("Más tarde", "Later"),
        [safe, uid, generation, lesson](bool accepted, const juce::String &) {
          if (!safe || safe->activeSession.uid != uid || generation != safe->teamsOnboardingGeneration) return;
          if (safe->learningProgress) safe->learningProgress->record(lesson, 0, false);
          if (accepted) juce::MessageManager::callAsync([safe, uid, generation, id = lesson.id] {
            if (!safe || safe->activeSession.uid != uid || generation != safe->teamsOnboardingGeneration ||
                !safe->teamsMembershipsVerified || safe->playbackSession.isSongPlaying()) return;
            if (std::none_of(safe->teamsLearningSources.begin(), safe->teamsLearningSources.end(),
                [&](const auto &s) { return s.lessonId == id; })) return;
            safe->showLearningGuide();
            safe->startLearningLesson(id);
          });
        });
    appModal->onClosed = [safe] {
      if (safe) { safe->teamsWelcomeVisible = false; safe->appModal.reset(); }
    };
    appModal->show();
    break;
  }
}


void MTControlsAudioProcessorEditor::processTeamsRevocations() {
  const auto uid = activeSession.uid;
  if (!teamsPendingRevocations.isArray())
    teamsPendingRevocations = TeamsLocalStore::loadDetails(uid, "revocation-pending").getProperty("items", {});
  if (!teamsPendingRevocations.isArray()) teamsPendingRevocations = juce::var(juce::Array<juce::var>());
  const auto *entries = teamsPendingRevocations.getArray();
  if (!entries || entries->isEmpty() || playbackSession.isSongPlaying() || playbackSession.isRemote() ||
      songLoadBusy || appModal || welcomeModal || reauthPromptModal || practiceOverlay ||
      (songListPanelRaw && songListPanelRaw->isSongEditorOpen()) ||
      (waveformBarPtr && waveformBarPtr->getExtraHeightForHostLayout() > 0) ||
      juce::Component::getCurrentlyModalComponent() || !isShowing()) return;
  auto completed = TeamsLocalStore::loadDetails(uid, "revocation-completed");
  if (!completed.isObject()) completed = payload({});
  for (const auto &entry : *entries) {
    const auto teamId = entry.getProperty("teamId", {}).toString();
    const auto removalId = entry.getProperty("removalId", {}).toString();
    if (teamId.isEmpty() || removalId.isEmpty() ||
        completed.getProperty(juce::Identifier(teamId), {}).toString() == removalId) continue;
    const auto links = TeamsLocalStore::loadSongLinks(uid, teamId);
    if (hasCurrentLoadedSong && std::any_of(links.begin(), links.end(),
        [this](const auto &link) { return link.localSongId == currentLoadedSong.id; }) &&
        !unloadActiveSongForLocalPurge()) return;
    // Cancel before deleting so a late signed download cannot recreate a removed row.
    teamsPreparation.reset();
    for (auto it = cloudDownloadsInProgress.begin(); it != cloudDownloadsInProgress.end();) {
      if (juce::String(*it).startsWith("shared-cloud:")) {
        if (songListPanelRaw) songListPanelRaw->clearCloudUploadProgress(juce::String(*it));
        it = cloudDownloadsInProgress.erase(it);
      } else ++it;
    }
    if (!TeamsPreparationService::removeTeamDownloads(uid, teamId)) return;
    completed.getDynamicObject()->setProperty(juce::Identifier(teamId), removalId);
    if (!TeamsLocalStore::saveDetails(uid, "revocation-completed", completed)) return;
    if (songListPanelRaw) songListPanelRaw->refreshFromStorageView();
    refreshCloudLibrary({});
    const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
    appModal = std::make_unique<AppModal>(*this, AppModal::Kind::Confirm,
        L10n::tr("Acceso al Team finalizado", "Team access ended"),
        L10n::tr("Se eliminaron las canciones y descargas de este Team de este dispositivo y de tus setlists. Tus canciones personales se conservaron. Los demás dispositivos se actualizarán cuando vuelvan a conectarse.",
                 "This Team's songs and downloads were removed from this device and your setlists. Your personal songs were kept. Other devices will update when they reconnect."),
        L10n::tr("Entendido", "OK"), juce::String(), [](bool, const juce::String &) {});
    appModal->onClosed = [safe] { if (safe) safe->appModal.reset(); };
    appModal->show();
    return;
  }
  teamsPendingRevocations = juce::var(juce::Array<juce::var>());
}
