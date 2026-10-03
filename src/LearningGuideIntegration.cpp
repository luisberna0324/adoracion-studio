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
#include "ui/components/AppModal.h"
#include <algorithm>

using LearningAnchor = playworship::learning::Anchor;

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
    learningGuide->onLessonSelected = [safe](juce::String id) { if (safe) safe->startLearningLesson(id); };
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
  learningLessonId.clear();
  learningGuide->setBounds(getLocalBounds());
  learningGuide->showCatalog(learningLessons, *learningProgress);
  learningGuide->toFront(false);
  updateLearningGuideTarget();
}

void MTControlsAudioProcessorEditor::startLearningLesson(const juce::String &id) {
  learningLessonId = id;
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
  grabKeyboardFocus();
}

void MTControlsAudioProcessorEditor::refreshLearningStep() {
  const auto *lesson = currentLearningLesson();
  if (!lesson || !learningGuide) return;
  learningGuide->showStep(*lesson, learningStepIndex,
      getLearningTargetBounds(lesson->steps[static_cast<size_t>(learningStepIndex)].anchor),
      playbackSession.hasLoadedSong());
  updateLearningGuideTarget();
}

juce::Rectangle<int> MTControlsAudioProcessorEditor::getLearningTargetBounds(LearningAnchor anchor) const {
  const auto local = [this](const juce::Component *component, juce::Rectangle<int> bounds) {
    return component && component->isShowing()
        ? getLocalArea(component, bounds).getIntersection(getLocalBounds()) : juce::Rectangle<int>{};
  };
  if (practiceOverlay && practiceOverlay->isShowing()) {
    const auto *practice = dynamic_cast<const PracticePanel *>(practiceOverlay.get());
    return practice ? local(practice, practice->getLearningTargetBounds(
        learningLessonId == "practice-sections" ? anchor : LearningAnchor::PracticeExit))
        : juce::Rectangle<int>{};
  }
  if (anchor == LearningAnchor::SongSelection && songListPanel && songListPanel->isShowing())
    return local(songListPanel.get(), songListPanel->getLocalBounds());
  if (anchor == LearningAnchor::Mixer) {
    const auto *grid = dynamic_cast<const ChannelGridPanel *>(channelGridPanel.get());
    return grid ? local(grid, grid->getLearningStripBounds()) : juce::Rectangle<int>{};
  }
  if ((anchor == LearningAnchor::Waveform || anchor == LearningAnchor::PracticeLoop) &&
      learningLessonId == "practice-sections" && learningPhoneContent)
    anchor = LearningAnchor::More;
  if (anchor == LearningAnchor::Waveform && waveformBar)
    return local(waveformBar.get(), waveformBar->getLocalBounds());
  if (anchor == LearningAnchor::Pads && !learningPhoneContent) {
    if (padsComponent && padsComponent->isShowing()) return local(padsComponent.get(), padsComponent->getLocalBounds());
    if (padsHandlePanel) return local(padsHandlePanel.get(), padsHandlePanel->getLocalBounds());
  }
  return topBar ? local(topBar.get(), topBar->getLearningTargetBounds(anchor)) : juce::Rectangle<int>{};
}

void MTControlsAudioProcessorEditor::updateLearningGuideTarget() {
  if (!learningGuide) return;
  const bool blocked = settingsOverlay != nullptr || settingsWindow != nullptr ||
      (appModal != nullptr && appModal->isVisible()) ||
      (reauthPromptModal != nullptr && reauthPromptModal->isVisible()) ||
      juce::Component::getNumCurrentlyModalComponents() > 0;
  learningGuide->setVisible(!blocked);
  if (blocked) return;
  const bool english = AppSettings::getLanguage() == AppLanguage::English;
  if (english != learningEnglishContent) {
    learningEnglishContent = english;
    learningLessons = playworship::learning::makeLessons(learningPhoneContent, english);
    if (learningGuide->isShowingCatalog())
      learningGuide->showCatalog(learningLessons, *learningProgress);
    else
      refreshLearningStep();
    return;
  }
  juce::BorderSize<int> insets;
#if JUCE_IOS || JUCE_ANDROID
  if (const auto *display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()))
    insets = display->safeAreaInsets;
#endif
  learningGuide->setSafeAreaInsets(insets);
  if (const auto *lesson = currentLearningLesson()) {
    learningGuide->updateTarget(getLearningTargetBounds(lesson->steps[static_cast<size_t>(learningStepIndex)].anchor),
                               playbackSession.hasLoadedSong());
    auto message = learningSaveError;
    if (message.isEmpty() && learningPhoneContent && learningLessonId == "practice-sections" && !practiceOverlay)
      message = L10n::tr("Abre Ensayo desde Más para probar estos gestos.",
          "Open Practice from More to try these gestures.");
    if (message.isEmpty() && practiceOverlay && learningLessonId != "practice-sections")
      message = L10n::tr("Cierra Ensayo con su botón X para volver a los controles de este curso.",
          "Close Practice using its X button to return to this course's controls.");
    learningGuide->setStatusMessage(message);
  }
}

juce::Rectangle<int> PracticePanel::getLearningTargetBounds(LearningAnchor anchor) const {
  switch (anchor) {
  case LearningAnchor::Waveform: return getWaveformLayout().plot;
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
