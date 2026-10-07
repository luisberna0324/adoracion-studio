#include "PluginEditor.h"
#include "data/Localization.h"
#include "data/AppSettings.h"
#include "ui/learning/LearningGuideOverlay.h"
#include "ui/panels/ChannelGridPanel.h"
#include "ui/panels/PadsComponent.h"
#include "ui/components/PadsHandlePanel.h"
#include "ui/panels/PracticePanel.h"
#include "ui/panels/SongListPanel.h"
#include "ui/panels/TopBar.h"
#include "ui/panels/SettingsPanel.h"
#include "ui/components/AppModal.h"
#include <algorithm>

using LearningAnchor = playworship::learning::Anchor;

playworship::learning::StepFacts MTControlsAudioProcessorEditor::learningStepFacts() const {
  using Mode = playworship::remote::RemoteSession::Mode;
  playworship::learning::StepFacts facts;
  if (learningLessonId.startsWith("teams-cloud-")) {
    facts.sharedCloudAccessible = activeSession.allowed && activeSession.uid.isNotEmpty() &&
        teamsMembershipsVerified && !playbackSession.isRemote() &&
        std::any_of(teamsLearningSources.begin(), teamsLearningSources.end(),
          [this](const auto &source) { return source.lessonId == learningLessonId; });
    facts.cloudOpen = songListPanelRaw && songListPanelRaw->isCloudLibraryOpen();
    facts.sharedSongDownloaded = teamsLearningDownloaded;
    facts.sharedSongAdded = teamsLearningAdded;
  }
  facts.settingsOpen = settingsPanelRaw && settingsPanelRaw->isShowing();
  facts.remotePageOpen = facts.settingsOpen && settingsPanelRaw->isLearningRemotePageOpen();
  facts.serverModeActive = remoteSession.getMode() == Mode::Server;
  facts.serverListening = facts.serverModeActive && remoteSession.getPort() > 0 &&
      remoteSession.getDiscoveryStatus() == playworship::remote::LanDiscovery::Status::Ready;
  facts.serverHasClient = remoteSession.getClientCount() > 0;
  facts.clientModeActive = remoteSession.getMode() == Mode::Client;
  facts.remoteConnected = playbackSession.isRemote() && playbackSession.isConnected();
  facts.songLoaded = playbackSession.hasLoadedSong() &&
      (learningLessonId != "remote-client" || (facts.clientModeActive && facts.remoteConnected)) &&
      (learningLessonId != "remote-server" || facts.serverListening);
  facts.remotePlayApplied = learningRemoteAction.applied() && playbackSession.isSongPlaying();
  facts.remoteStopApplied = learningRemoteAction.applied() && !playbackSession.isSongPlaying();
  if (learningLessonId == "demo-song")
    facts.demoExerciseComplete = demoLearningSnapshot().isDemo && demoLearningProgress.satisfied();
  return facts;
}

bool MTControlsAudioProcessorEditor::canAdvanceLearningStep() const {
  const auto *lesson = currentLearningLesson();
  if (!lesson || learningStepIndex < 0 || learningStepIndex >= static_cast<int>(lesson->steps.size()))
    return false;
  const auto &step = lesson->steps[static_cast<size_t>(learningStepIndex)];
  if (lesson->id == "demo-song" && !demoLearningSnapshot().isDemo) return false;
  const auto facts = learningStepFacts();
  return playworship::learning::remoteStepPrerequisite(lesson->id, step, facts) ==
      playworship::learning::RemotePrerequisite::None &&
      playworship::learning::evaluateStepCondition(step.condition, facts);
}

const playworship::learning::Lesson *MTControlsAudioProcessorEditor::currentLearningLesson() const {
  const auto found = std::find_if(learningLessons.begin(), learningLessons.end(),
      [this](const auto &lesson) { return lesson.id == learningLessonId; });
  return found == learningLessons.end() ? nullptr : &*found;
}

void MTControlsAudioProcessorEditor::showLearningGuide() {
  if (!mainUiInitialized) return;
  if (!learningProgress) learningProgress = std::make_unique<playworship::learning::ProgressStore>();
  if (!learningGuide) {
    learningGuide = std::make_unique<LearningGuideOverlay>();
    const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
    learningGuide->onLessonSelected = [safe](juce::String id) {
      if (!safe) return;
      if (id == "demo-song") safe->prepareDemoLesson();
      else safe->startLearningLesson(id);
    };
    learningGuide->onNext = [safe] { if (safe) safe->advanceLearningStep(true); };
    learningGuide->onPrevious = [safe] { if (safe) safe->advanceLearningStep(false); };
    learningGuide->onPause = [safe] { if (safe) safe->pauseLearningGuide(); };
    learningGuide->onRestart = [safe] { if (safe) safe->restartLearningLesson(); };
    learningGuide->onShowCourses = [safe] { if (safe) safe->showLearningCatalog(); };
    addAndMakeVisible(*learningGuide);
  }
  showLearningCatalog();
  learningGuide->grabKeyboardFocus();
}

void MTControlsAudioProcessorEditor::showLearningCatalog() {
  if (!learningGuide || !learningProgress) return;
  learningPhoneContent = isPhoneSizedTouchLayout(*this);
  learningEnglishContent = AppSettings::getLanguage() == AppLanguage::English;
  learningLessons = playworship::learning::makeLessons(learningPhoneContent, learningEnglishContent);
  appendTeamsLearningLessons();
  learningLessonId.clear();
  learningObservedStepKey.clear();
  learningRemoteAction.deactivate();
  demoLearningProgress.reset();
  learningGuide->showCatalog(learningLessons, *learningProgress);
  learningGuide->toFront(false);
  updateLearningGuideTarget();
}

void MTControlsAudioProcessorEditor::startLearningLesson(const juce::String &id) {
  learningLessonId = id;
  learningStepIndex = 0;
  if (id.startsWith("teams-cloud-")) {
    teamsLearningDownloaded = teamsLearningAdded = false;
    teamsNextGuideCheckMs = 0;
    openTeamsLearningCloud();
  }
  const auto *lesson = currentLearningLesson();
  if (!lesson || lesson->steps.empty() || !learningProgress) return;
  learningStepIndex = learningProgress->get(id).completed ? 0 : learningProgress->resumeIndex(*lesson);
  const auto result = learningProgress->record(*lesson, learningStepIndex, false);
  learningSaveError = result.failed() ? L10n::tr("No se pudo guardar el progreso en este dispositivo.",
      "Progress could not be saved on this device.") : juce::String{};
  refreshLearningStep();
}

void MTControlsAudioProcessorEditor::advanceLearningStep(const bool forward) {
  const auto *lesson = currentLearningLesson();
  if (!lesson || !learningProgress || !learningGuide) return;
  if (forward && !canAdvanceLearningStep()) { updateLearningGuideTarget(); return; }
  if (forward && learningStepIndex == static_cast<int>(lesson->steps.size()) - 1) {
    const auto result = learningProgress->record(*lesson, learningStepIndex, true);
    if (result.failed()) {
      learningSaveError = L10n::tr("No se pudo guardar. Puedes volver a pulsar Terminar.",
          "Progress could not be saved. You can press Finish again.");
      learningGuide->setStatusMessage(learningSaveError);
      return;
    }
    learningSaveError.clear();
    showLearningCatalog();
    return;
  }
  learningStepIndex = juce::jlimit(0, static_cast<int>(lesson->steps.size()) - 1,
                                  learningStepIndex + (forward ? 1 : -1));
  const auto result = learningProgress->record(*lesson, learningStepIndex, false);
  learningSaveError = result.failed() ? L10n::tr("No se pudo guardar el progreso en este dispositivo.",
      "Progress could not be saved on this device.") : juce::String{};
  refreshLearningStep();
}

void MTControlsAudioProcessorEditor::restartLearningLesson() {
  const auto *lesson = currentLearningLesson();
  if (!lesson || !learningProgress) return;
  const auto result = learningProgress->restart(*lesson);
  learningStepIndex = 0;
  learningSaveError = result.failed() ? L10n::tr("No se pudo guardar el reinicio del progreso.",
      "Restarted progress could not be saved.") : juce::String{};
  refreshLearningStep();
}

void MTControlsAudioProcessorEditor::pauseLearningGuide() {
  if (const auto *lesson = currentLearningLesson(); lesson && learningProgress)
    learningProgress->record(*lesson, learningStepIndex, false);
  learningGuide.reset();
  learningObservedStepKey.clear();
  learningRemoteAction.deactivate();
  demoLearningProgress.reset();
  grabKeyboardFocus();
}

void MTControlsAudioProcessorEditor::refreshLearningStep() {
  const auto *lesson = currentLearningLesson();
  if (!lesson || !learningGuide) return;
  const auto &step = lesson->steps[static_cast<size_t>(learningStepIndex)];
  const auto key = lesson->id + "/" + step.id;
  if (key != learningObservedStepKey) {
    learningObservedStepKey = key;
    if (lesson->id == "demo-song") demoLearningProgress.begin(step.id, demoLearningSnapshot());
    else demoLearningProgress.reset();
    if (step.condition == playworship::learning::StepCondition::RemotePlayApplied)
      learningRemoteAction.begin(true);
    else if (step.condition == playworship::learning::StepCondition::RemoteStopApplied)
      learningRemoteAction.begin(false);
    else learningRemoteAction.deactivate();
  }
  learningRevealTargetPending = true;
  learningGuide->showStep(*lesson, learningStepIndex,
      getLearningTargetBounds(lesson->steps[static_cast<size_t>(learningStepIndex)].anchor),
      playbackSession.hasLoadedSong());
  updateLearningGuideTarget();
}

juce::Rectangle<int> MTControlsAudioProcessorEditor::getLearningTargetBounds(LearningAnchor anchor) const {
  using Prerequisite = playworship::learning::RemotePrerequisite;
  if (const auto *lesson = currentLearningLesson()) {
    const auto prerequisite = playworship::learning::remoteStepPrerequisite(
        lesson->id, lesson->steps[static_cast<size_t>(learningStepIndex)], learningStepFacts());
    switch (prerequisite) {
    case Prerequisite::OpenSettings: anchor = LearningAnchor::Settings; break;
    case Prerequisite::OpenRemotePage: anchor = LearningAnchor::RemoteSettings; break;
    case Prerequisite::ChooseClient:
    case Prerequisite::ChooseServer: anchor = LearningAnchor::RemoteMode; break;
    case Prerequisite::WaitServer: anchor = LearningAnchor::RemoteStatus; break;
    case Prerequisite::WaitClient: anchor = LearningAnchor::RemoteServers; break;
    case Prerequisite::CloseSettings: anchor = LearningAnchor::SettingsClose; break;
    case Prerequisite::None: break;
    }
  }
  const auto *host = learningGuide && learningGuide->getParentComponent()
      ? learningGuide->getParentComponent() : this;
  const auto local = [host](const juce::Component *component, juce::Rectangle<int> bounds) {
    return component && component->isShowing()
        ? host->getLocalArea(component, bounds).getIntersection(host->getLocalBounds()) : juce::Rectangle<int>{};
  };
  if (anchor == LearningAnchor::None) return {};
  if (anchor == LearningAnchor::SharedCloud) {
    if (songListPanel && songListPanel->isShowing())
      return local(songListPanel.get(), songListPanel->getLocalBounds());
    anchor = LearningAnchor::SongSelection;
  }
  if (settingsPanelRaw && host == settingsPanelRaw.getComponent())
    return settingsPanelRaw->getLearningTargetBounds(anchor);
  if (anchor == LearningAnchor::SettingsClose || anchor == LearningAnchor::RemoteSettings ||
      anchor == LearningAnchor::RemoteMode || anchor == LearningAnchor::RemoteAlias ||
      anchor == LearningAnchor::RemoteStatus || anchor == LearningAnchor::RemoteServers)
    anchor = LearningAnchor::Settings;
  if (learningLessonId == "demo-song" && !demoLearningProgress.satisfied()) {
    const auto *lesson = currentLearningLesson();
    if (lesson) {
      const auto &id = lesson->steps[static_cast<size_t>(learningStepIndex)].id;
      if (id == "demo-open-practice" && practiceOverlay) anchor = LearningAnchor::PracticeExit;
      if (id == "demo-close-practice" && !practiceOverlay) anchor = LearningAnchor::More;
    }
  }
  if (practiceOverlay && practiceOverlay->isShowing()) {
    const auto *practice = dynamic_cast<const PracticePanel *>(practiceOverlay.get());
    return practice ? local(practice, practice->getLearningTargetBounds(
        isLearningPracticeStep() ? anchor : LearningAnchor::PracticeExit))
        : juce::Rectangle<int>{};
  }
  if (anchor == LearningAnchor::SongSelection && songListPanel && songListPanel->isShowing())
    return local(songListPanel.get(), songListPanel->getLocalBounds());
  if (anchor == LearningAnchor::Mixer) {
    const auto *grid = dynamic_cast<const ChannelGridPanel *>(channelGridPanel.get());
    return grid ? local(grid, grid->getLearningStripBounds()) : juce::Rectangle<int>{};
  }
  if ((anchor == LearningAnchor::Waveform || anchor == LearningAnchor::PracticeLoop) &&
      isLearningPracticeStep())
    anchor = LearningAnchor::More;
  if (anchor == LearningAnchor::Waveform && waveformBar)
    return local(waveformBar.get(), waveformBar->getLocalBounds());
  if (anchor == LearningAnchor::RemoteIndicator && waveformBar)
    return local(waveformBar.get(), waveformBar->getLocalBounds().removeFromTop(36).removeFromRight(168));
  if (anchor == LearningAnchor::Pads && !learningPhoneContent) {
    if (padsComponent && padsComponent->isShowing()) return local(padsComponent.get(), padsComponent->getLocalBounds());
    if (padsHandlePanel) return local(padsHandlePanel.get(), padsHandlePanel->getLocalBounds());
  }
  return topBar ? local(topBar.get(), topBar->getLearningTargetBounds(anchor)) : juce::Rectangle<int>{};
}

void MTControlsAudioProcessorEditor::updateLearningGuideTarget() {
  if (!learningGuide) return;
  if (demoLoadingOverlay) { learningGuide->setVisible(false); return; }
  if (learningLessonId == "demo-song") demoLearningProgress.observe(demoLearningSnapshot());
  const bool settingsOpen = settingsPanelRaw && settingsPanelRaw->isShowing();
  const bool guideInSettings = settingsOpen && (learningGuide->isShowingCatalog() ||
      learningLessonId == "remote-server" || learningLessonId == "remote-client");
  auto *host = guideInSettings ? static_cast<juce::Component *>(settingsPanelRaw.getComponent()) : this;
  if (learningGuide->getParentComponent() != host) {
    host->addAndMakeVisible(*learningGuide);
    learningRevealTargetPending = true;
  }
  learningGuide->setBounds(host->getLocalBounds());
  learningGuide->toFront(false);
  const auto *modal = juce::Component::getCurrentlyModalComponent();
  const bool guideMenuOpen = modal != nullptr && learningGuide->isParentOf(modal);
  const bool blocked = (settingsOpen && !guideInSettings) ||
      (appModal != nullptr && appModal->isVisible()) ||
      (reauthPromptModal != nullptr && reauthPromptModal->isVisible()) ||
      (modal != nullptr && !guideMenuOpen &&
       (!guideInSettings || modal != settingsWindow.getComponent()));
  learningGuide->setVisible(!blocked);
  if (blocked) return;
  const bool english = AppSettings::getLanguage() == AppLanguage::English;
  if (english != learningEnglishContent) {
    learningEnglishContent = english;
    learningLessons = playworship::learning::makeLessons(learningPhoneContent, english);
    appendTeamsLearningLessons();
    if (learningGuide->isShowingCatalog())
      learningGuide->showCatalog(learningLessons, *learningProgress);
    else
      refreshLearningStep();
    return;
  }
  juce::BorderSize<int> insets;
#if JUCE_IOS || JUCE_ANDROID
  if (host == this)
    if (const auto *display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()))
    insets = display->safeAreaInsets;
#endif
  learningGuide->setSafeAreaInsets(insets);
  if (const auto *lesson = currentLearningLesson()) {
    const auto &step = lesson->steps[static_cast<size_t>(learningStepIndex)];
    using Prerequisite = playworship::learning::RemotePrerequisite;
    const auto prerequisite = playworship::learning::remoteStepPrerequisite(lesson->id, step, learningStepFacts());
    if (prerequisite != learningLastPrerequisite) {
      learningLastPrerequisite = prerequisite;
      learningRevealTargetPending = true;
    }
    auto anchorToReveal = step.anchor;
    switch (prerequisite) {
    case Prerequisite::OpenRemotePage: anchorToReveal = LearningAnchor::RemoteSettings; break;
    case Prerequisite::ChooseClient:
    case Prerequisite::ChooseServer: anchorToReveal = LearningAnchor::RemoteMode; break;
    case Prerequisite::WaitServer: anchorToReveal = LearningAnchor::RemoteStatus; break;
    case Prerequisite::WaitClient: anchorToReveal = LearningAnchor::RemoteServers; break;
    case Prerequisite::CloseSettings: anchorToReveal = LearningAnchor::SettingsClose; break;
    default: break;
    }
    if (guideInSettings && learningRevealTargetPending)
      settingsPanelRaw->revealLearningTarget(anchorToReveal);
    learningRevealTargetPending = false;
    learningGuide->updateTarget(getLearningTargetBounds(step.anchor),
                               playbackSession.hasLoadedSong());
    const bool replayDemo = lesson->id == "demo-song" && (!practiceOverlay || isLearningPracticeStep()) &&
        (step.anchor == LearningAnchor::Mixer || step.anchor == LearningAnchor::Waveform);
    learningGuide->setSupplementaryTarget(replayDemo
        ? getLearningTargetBounds(LearningAnchor::PlayPause) : juce::Rectangle<int>{});
    auto message = learningSaveError;
    if (message.isEmpty() && replayDemo && !playbackSession.isSongPlaying())
      message = L10n::tr("Puedes pulsar Play para escuchar mientras practicas este paso.",
          "You can press Play to listen while practicing this step.");
    const bool ready = canAdvanceLearningStep();
    auto pendingMessage = ready ? juce::String{} : step.task;
    switch (prerequisite) {
    case Prerequisite::OpenSettings:
      pendingMessage = L10n::tr("Para retomar este paso, abre Configuración.", "To resume this step, open Settings."); break;
    case Prerequisite::OpenRemotePage:
      pendingMessage = L10n::tr("Entra en Control remoto; en escritorio está dentro de General.", "Open Remote control; on desktop it is in General."); break;
    case Prerequisite::ChooseServer:
      pendingMessage = L10n::tr("Selecciona Servidor para retomar el ejercicio.", "Select Server to resume the exercise."); break;
    case Prerequisite::ChooseClient:
      pendingMessage = L10n::tr("Selecciona Cliente para retomar el ejercicio.", "Select Client to resume the exercise."); break;
    case Prerequisite::WaitServer:
      pendingMessage = L10n::tr("Espera a que este equipo esté disponible como Servidor.", "Wait until this device is available as Server."); break;
    case Prerequisite::WaitClient:
      pendingMessage = L10n::tr("Prepara el otro equipo como Servidor y espera la conexión; si aparecen varios, elige uno.", "Prepare the other device as Server and wait for connection; choose one if several appear."); break;
    case Prerequisite::CloseSettings:
      pendingMessage = L10n::tr("La conexión está lista. Cierra Configuración para continuar el ejercicio.", "The connection is ready. Close Settings to continue the exercise."); break;
    case Prerequisite::None: break;
    }
    if (!ready && (learningLessonId == "remote-client" || learningLessonId == "remote-server")) {
      using Discovery = playworship::remote::LanDiscovery::Status;
      if (remoteSession.getDiscoveryStatus() == Discovery::PermissionDenied)
        pendingMessage = L10n::tr("Permite Red local para PlayWorship en los ajustes del sistema y vuelve a esta guía.",
            "Allow Local Network for PlayWorship in system settings, then return to this guide.");
      else if (remoteSession.getDiscoveryStatus() == Discovery::Failed || remoteConnectionStatus.isNotEmpty())
        pendingMessage = remoteConnectionStatus.isNotEmpty() ? remoteConnectionStatus :
            L10n::tr("No se pudo iniciar el descubrimiento. Revisa la red local y vuelve a elegir el modo.",
                     "Discovery could not start. Check the local network and choose the mode again.");
      else if (learningRemoteAction.reason().isNotEmpty())
        pendingMessage = learningRemoteAction.reason();
    }
    if (learningLessonId.startsWith("teams-cloud-") && !ready) {
      if (!teamsMembershipsVerified)
        pendingMessage = L10n::tr("Conéctate para comprobar el acceso a la nube compartida.", "Connect to verify shared cloud access.");
      else if (playbackSession.isRemote())
        pendingMessage = L10n::tr("Retoma esta guía en modo Local.", "Resume this guide in Local mode.");
      else if (step.condition == playworship::learning::StepCondition::SharedSongDownloaded) {
        const auto source = std::find_if(teamsLearningSources.begin(), teamsLearningSources.end(),
            [this](const auto &s) { return s.lessonId == learningLessonId; });
        const auto song = std::find_if(cloudCatalogSongs.begin(), cloudCatalogSongs.end(),
            [&](const auto &s) { return source != teamsLearningSources.end() && s.teamId == source->teamId; });
        pendingMessage = song != cloudCatalogSongs.end()
            ? L10n::tr("Puedes descargar «", "You can download “") + song->title +
                L10n::tr("» desde su menú de tres puntos.", "” from its three-dot menu.")
            : L10n::tr("Busca una canción compartida en Cloud. Si la nube está vacía, puedes retomar la guía cuando el propietario suba canciones.",
                "Find a shared song in Cloud. If the cloud is empty, resume when the owner uploads songs.");
      }
    }
    learningGuide->setStepReadiness(ready, pendingMessage);
    if (learningLessonId == "demo-song" && !demoLearningSnapshot().isDemo)
      message = L10n::tr("Vuelve a Cursos y elige la demo para retomar esta práctica.",
          "Return to Courses and choose the demo to resume this practice.");
    if (message.isEmpty() && !ready && isLearningPracticeStep() && !practiceOverlay)
      message = L10n::tr("Abre Ensayo desde Más para probar estos gestos.",
          "Open Practice from More to try these gestures.");
    if (message.isEmpty() && practiceOverlay && !isLearningPracticeStep())
      message = L10n::tr("Cierra Ensayo con su botón X para volver a los controles de este curso.",
          "Close Practice using its X button to return to this course's controls.");
    if (message.isEmpty() && !ready && practiceOverlay && step.id == "demo-open-practice")
      message = L10n::tr("Cierra Ensayo con X y ábrelo de nuevo desde Más para probar este paso.",
          "Close Practice with X and open it again from More to try this step.");
    learningGuide->setStatusMessage(message);
  }
}

juce::Rectangle<int> PracticePanel::getLearningTargetBounds(LearningAnchor anchor) const {
  switch (anchor) {
  case LearningAnchor::Waveform: return getWaveformLayout().plot.getUnion(instrumentHeaderBounds);
  case LearningAnchor::PracticeLoop:
    return getWaveformLayout().plot.getUnion(playPauseButton.getBounds().getUnion(loopClearButton.getBounds()));
  case LearningAnchor::More: return instrumentHeaderBounds;
  case LearningAnchor::PracticeExit: return closeButton.getBounds();
  case LearningAnchor::PlayPause: return playPauseButton.getBounds();
  case LearningAnchor::Stop: return stopButton.getBounds();
  case LearningAnchor::Tempo: return tempoSelector.getBounds();
  case LearningAnchor::Key: return pitchSelector.getBounds();
  default: return {};
  }
}
