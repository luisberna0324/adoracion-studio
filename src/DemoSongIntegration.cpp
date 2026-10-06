#include "PluginEditor.h"
#include "data/AppSettings.h"
#include "data/DemoSongService.h"
#include "data/Localization.h"
#include "ui/components/AppModal.h"
#include "ui/components/SongLoadProgressOverlay.h"
#include "ui/learning/LearningGuideOverlay.h"
#include "ui/panels/ChannelGridPanel.h"
#include "ui/panels/PracticePanel.h"
#include "ui/panels/SongListPanel.h"
#include "ui/panels/SettingsPanel.h"
#include <thread>

void MTControlsAudioProcessorEditor::offerDemoWelcome() {
  if (demoWelcomeChecked || !mainUiInitialized || !activeSession.allowed ||
      demoLoadingOverlay || appModal || settingsPanelRaw || practiceOverlay ||
      learningGuide || songLoadBusy || deferredStartupSongLoadPending ||
      juce::Component::getCurrentlyModalComponent()) return;
  demoWelcomeChecked = true;
  // Existing libraries keep their normal launch. This invitation never imports
  // a project until the user explicitly chooses it.
  if (AppSettings::getDemoWelcomeSeen() || hasCurrentLoadedSong ||
      !SongLibraryStore::loadSongs().empty() ||
      remoteSession.getMode() != playworship::remote::RemoteSession::Mode::Local)
    return;
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  appModal = std::make_unique<AppModal>(*this, AppModal::Kind::Confirm,
      L10n::tr("Bienvenido a PlayWorship", "Welcome to PlayWorship"),
      L10n::tr("Aprende con una demo incluida: 90 segundos, ocho instrumentos y secciones listas. Prueba los controles paso a paso, sin conexión. Puedes borrarla y recuperarla desde Aprender PlayWorship.",
               "Learn with an included demo: 90 seconds, eight instruments and prepared sections. Try the controls step by step, offline. Delete it or recover it from Learn PlayWorship."),
      L10n::tr("Probar demo", "Try demo"), L10n::tr("Más tarde", "Later"),
      [safe](bool accepted, const juce::String &) {
        AppSettings::setDemoWelcomeSeen(true);
        if (accepted) juce::MessageManager::callAsync([safe] {
          if (!safe || !safe->mainUiInitialized || !safe->activeSession.allowed) return;
          safe->showLearningGuide();
          safe->prepareDemoLesson();
        });
      });
  appModal->onClosed = [safe] { if (safe) safe->appModal.reset(); };
  appModal->show();
}

void MTControlsAudioProcessorEditor::prepareDemoLesson() {
  if (!mainUiInitialized || !activeSession.allowed || demoLoadingOverlay || !learningGuide) return;
  if (remoteSession.getMode() != playworship::remote::RemoteSession::Mode::Local) {
    learningGuide->setStatusMessage(L10n::tr(
        "Selecciona Local en Configuración → Control remoto y vuelve a elegir la demo.",
        "Select Local in Settings → Remote control, then choose the demo again."));
    return;
  }
  if (songLoadBusy || projectSaveBusy || mixExportBusy ||
      static_cast<MTControlsAudioProcessor &>(processor).isSectionAutomationEditing() ||
      !cloudDownloadsInProgress.empty() ||
      !cloudUploadsInProgress.empty() || !cloudDeletesInProgress.empty()) {
    learningGuide->setStatusMessage(L10n::tr(
        "Espera a que termine la operación actual y vuelve a elegir la demo.",
        "Wait for the current operation to finish, then choose the demo again."));
    return;
  }
  if (practiceOverlay) {
    learningGuide->setStatusMessage(L10n::tr(
        "Cierra Ensayo y vuelve a elegir la demo.", "Close Practice, then choose the demo again."));
    return;
  }
  closeSettings();
  deferredStartupSongLoadPending = false;
  deferredStartupSongIndex = -1;
  deferredStartupSongLoadAtMs = 0;
  flushActiveSongPersistenceNow();
  ProjectStore::flushAllPendingSettings();
  static_cast<MTControlsAudioProcessor &>(processor).stopSong();
  demoLoadingOverlay = std::make_unique<SongLoadProgressOverlay>();
  demoLoadingOverlay->setTitleText(L10n::tr("Preparando la demo", "Preparing the demo"));
  demoLoadingOverlay->setPhaseText(L10n::tr("Luz de esperanza · Demo incluida", "Luz de esperanza · Included demo"));
  addAndMakeVisible(*demoLoadingOverlay);
  demoLoadingOverlay->setBounds(getLocalBounds());
  demoLoadingOverlay->enterModalState(true, nullptr, false);
  const auto safe = juce::Component::SafePointer<MTControlsAudioProcessorEditor>(this);
  const auto uid = activeSession.uid;
  std::thread([safe, uid] {
    ImportedSongData song;
    const auto result = DemoSongService::ensureInstalled(song, [safe](double progress, const juce::String &) {
      juce::MessageManager::callAsync([safe, progress] {
        if (safe && safe->demoLoadingOverlay) safe->demoLoadingOverlay->setProgress(progress);
      });
    });
    juce::MessageManager::callAsync([safe, uid, song = std::move(song), result] {
      if (!safe) return;
      if (safe->demoLoadingOverlay) safe->demoLoadingOverlay->exitModalState(0);
      safe->demoLoadingOverlay.reset();
      if (!safe->mainUiInitialized || !safe->activeSession.allowed || safe->activeSession.uid != uid)
        return;
      if (result.failed()) {
        if (safe->learningGuide) safe->learningGuide->setStatusMessage(result.getErrorMessage());
        return;
      }
      if (safe->songListPanelRaw) safe->songListPanelRaw->refreshFromStorageView();
      if (auto *grid = dynamic_cast<ChannelGridPanel *>(safe->channelGridPanel.get()))
        grid->refreshLibraryFromStorage();
      // The demo is an ordinary library song. Do not add it to the active
      // setlist, replace a user's project or autoplay during onboarding.
      auto &p = static_cast<MTControlsAudioProcessor &>(safe->processor);
      p.stopSong();
      if (!safe->loadSongAction || !safe->loadSongAction(song, -1)) {
        if (safe->learningGuide) safe->learningGuide->setStatusMessage(L10n::tr(
            "La demo está en Biblioteca, pero no se pudo cargar. Inténtalo nuevamente.",
            "The demo is in your Library but could not be loaded. Try again."));
        return;
      }
      AppSettings::setDemoWelcomeSeen(true);
      safe->startLearningLesson("demo-song");
    });
  }).detach();
}

playworship::learning::DemoLearningProgress::Snapshot
MTControlsAudioProcessorEditor::demoLearningSnapshot() const {
  playworship::learning::DemoLearningProgress::Snapshot snapshot;
  const auto &p = static_cast<const MTControlsAudioProcessor &>(processor);
  snapshot.songId = hasCurrentLoadedSong ? currentLoadedSong.id : juce::String{};
  snapshot.isDemo = hasCurrentLoadedSong && DemoSongService::isDemoSong(currentLoadedSong) &&
      remoteSession.getMode() == playworship::remote::RemoteSession::Mode::Local;
  snapshot.playing = p.isSongPlaying();
  snapshot.position = p.getSongCurrentPositionSeconds();
  snapshot.startPosition = p.getSongStartSeconds();
  snapshot.transportRevision = p.getTransportControlRevision();
  for (int i = 0; i < p.getLoadedStemCount(); ++i)
    snapshot.stems.push_back({p.getStemBaseVolume(i), p.isStemBaseMuted(i), p.isStemSoloed(i)});
  snapshot.sectionLoopIndex = p.getArrangementPartLoopIndex();
  snapshot.practiceOpen = practiceOverlay && practiceOverlay->isShowing();
  snapshot.practiceFocus = p.getPracticeListeningMode();
  snapshot.practiceLoopActive = p.isPracticeLoopActive();
  snapshot.practiceLoopHasMarks = practiceOverlay && practiceOverlay->hasLoopMarks();
  return snapshot;
}

bool MTControlsAudioProcessorEditor::isLearningPracticeStep() const {
  if (learningLessonId == "practice-sections") return learningPhoneContent;
  if (learningLessonId != "demo-song" || !learningPhoneContent) return false;
  const auto *lesson = currentLearningLesson();
  if (!lesson || !juce::isPositiveAndBelow(learningStepIndex, static_cast<int>(lesson->steps.size())))
    return false;
  const auto &id = lesson->steps[static_cast<size_t>(learningStepIndex)].id;
  return id == "demo-open-practice" || id == "demo-practice-focus" ||
      id == "demo-practice-ab" || id == "demo-close-practice";
}
