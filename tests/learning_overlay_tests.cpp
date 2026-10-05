#include "ui/learning/LearningGuideOverlay.h"
#include "ui/components/AppModal.h"
#include "data/Localization.h"
#include "ui/panels/PracticePanel.h"
#include "ui/panels/SettingsPanel.h"

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

juce::Button &buttonNamed(LearningGuideOverlay &overlay, const juce::String &name) {
  for (auto *child : overlay.getChildren())
    if (child->getName() == name)
      if (auto *button = dynamic_cast<juce::Button *>(child))
        return *button;
  throw std::runtime_error("missing guide action: " + name.toStdString());
}

void drainMessages() {
  juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
}

juce::MouseEvent pointerEvent(juce::Component &component, bool dragged = false) {
  const auto position = component.getLocalBounds().toFloat().getCentre();
  const auto now = juce::Time::getCurrentTime();
  return {juce::Desktop::getInstance().getMainMouseSource(), position, {},
          1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component,
          now, position, now, 1, dragged};
}

void capture(juce::Component &component, const juce::String &name) {
  const auto path = juce::SystemStats::getEnvironmentVariable("PW_LEARNING_CAPTURE_DIR", {});
  if (path.isEmpty()) return;
  expect(juce::File::isAbsolutePath(path), "capture folder uses an absolute path");
  const juce::File directory(path);
  expect(directory.createDirectory().wasOk(), "capture folder is available");
  auto stream = directory.getChildFile(name + ".png").createOutputStream();
  expect(stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk(),
         "capture replaces the previous PNG");
  expect(juce::PNGImageFormat().writeImageToStream(
             component.createComponentSnapshot(component.getLocalBounds(), true, 2.0f), *stream),
         "production guide component renders to PNG");
}
} // namespace

struct LearningOverlayTestAccess {
  static juce::Rectangle<int> card(const LearningGuideOverlay &guide) {
    return guide.cardBounds;
  }
  static juce::Rectangle<int> target(const LearningGuideOverlay &guide) {
    return guide.target;
  }
  static juce::Rectangle<int> safe(const LearningGuideOverlay &guide) {
    return guide.safeBounds();
  }
  static int index(const LearningGuideOverlay &guide) { return guide.stepIndex; }
  static juce::Viewport &stepViewport(LearningGuideOverlay &guide) {
    return guide.textViewport;
  }
  static juce::Viewport &catalogViewport(LearningGuideOverlay &guide) {
    return guide.catalogViewport;
  }
  static void showCourses(LearningGuideOverlay &guide) {
    guide.dispatch(LearningGuideOverlay::Action::Courses);
  }
  static void restart(LearningGuideOverlay &guide) {
    guide.dispatch(LearningGuideOverlay::Action::Restart);
  }
};

struct SettingsPanelTestAccess {
  static void openRemote(SettingsPanel &panel) {
    panel.showSettingsPage(SettingsPanel::Destination::Remote);
  }
  static juce::Button &clientButton(SettingsPanel &panel) {
    return panel.remoteClientButton;
  }
};

namespace {
struct Fixture final : juce::Component {
  Fixture() : play("learningTestPlay", SvgIconName::Play, true) {
    setVisible(true);
    addAndMakeVisible(play);
    addAndMakeVisible(guide);
  }
  void resized() override { guide.setBounds(getLocalBounds()); }
  void paint(juce::Graphics &g) override {
    g.fillAll(UITheme::appBg);
    g.setColour(UITheme::panelDark);
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(12), 18);
  }
  TransportIconButton play;
  LearningGuideOverlay guide;
};

playworship::learning::Lesson longLesson() {
  playworship::learning::Lesson lesson;
  lesson.id = "test-course";
  lesson.title = L10n::tr("Reproducción y mezcla", "Playback and mix");
  lesson.description = L10n::tr("Practica con los controles reales.", "Practice with the real controls.");
  lesson.steps = {
      {"first", L10n::tr("Reproduce tu canción", "Play your song"),
       L10n::tr("Usa el botón de reproducción. Puedes escuchar la mezcla y pausar cuando quieras. Esta explicación sigue disponible mientras pruebas el control.",
                  "Use the play button. Listen to the mix and pause whenever you want. This explanation remains available while you try the control."),
       L10n::tr("Pulsa Play y luego pausa.", "Press Play, then pause."),
       playworship::learning::Anchor::PlayPause, true},
      {"last", L10n::tr("Listo para continuar", "Ready to continue"),
       L10n::tr("Terminaste este curso.", "You finished this course."), {},
       playworship::learning::Anchor::None, false}};
  return lesson;
}

void expectActionsInside(LearningGuideOverlay &guide) {
  const auto card = LearningOverlayTestAccess::card(guide);
  std::vector<juce::Rectangle<int>> previous;
  for (const auto *child : guide.getChildren()) {
    if (!child->isVisible()) continue;
    const auto bounds = child->getBounds();
    expect(!bounds.isEmpty() && card.contains(bounds),
           "visible guide controls remain within the card");
    for (const auto &other : previous)
      expect(!bounds.intersects(other), "guide controls do not overlap one another");
    previous.push_back(bounds);
  }
}

void testSharedCloudCourse() {
  Fixture fixture;
  const auto lesson = playworship::learning::makeSharedCloudLesson("teams-cloud-preview", "equipo@example.com", false);
  for (const auto size : {juce::Point<int>(390, 844), juce::Point<int>(1200, 800)}) {
    fixture.setSize(size.x, size.y);
    fixture.guide.setSafeAreaInsets({24, 12, 20, 12});
    fixture.guide.setVisible(false);
    {
      AppModal welcome(fixture, AppModal::Kind::Confirm,
          "Tienes acceso a una nube compartida",
          "equipo@example.com ha compartido sus canciones contigo. Puedes descargarlas y agregarlas a tus setlists.",
          "Ver canciones", juce::String::fromUTF8("Más tarde"), nullptr);
      fixture.addAndMakeVisible(welcome);
      welcome.setBounds(fixture.getLocalBounds());
      capture(fixture, "teams-welcome-" + juce::String(size.x));
    }
    fixture.guide.setVisible(true);
    for (int i = 0; i < 3; ++i) {
      fixture.guide.showStep(lesson, i, {20, 120, size.x - 40, size.y - 150}, false);
      fixture.guide.setStepReadiness(false, lesson.steps[static_cast<size_t>(i)].task);
      expectActionsInside(fixture.guide);
      capture(fixture, "teams-" + juce::String(size.x) + "-" + juce::String(i));
    }
  }
}

void testResponsiveSpotlightAndInteraction() {
  Fixture fixture;
  const auto lesson = longLesson();
  bool scrolledText = false;
  for (const auto size : {juce::Point<int>(320, 667), juce::Point<int>(390, 844),
                          juce::Point<int>(667, 375), juce::Point<int>(844, 390),
                          juce::Point<int>(768, 1024), juce::Point<int>(1200, 800)}) {
    fixture.setSize(size.x, size.y);
    fixture.guide.setSafeAreaInsets(size.x <= 844
                                       ? juce::BorderSize<int>(24, 16, 20, 16)
                                       : juce::BorderSize<int>{});
    // Use the real shared transport control behind the overlay. In landscape
    // it is on the right, leaving room for the guide on the opposite side.
    fixture.play.setBounds(size.x - 76, size.y / 2 - 22, 44, 44);
    fixture.guide.showStep(lesson, 0, fixture.play.getBounds(), false);
    const auto card = LearningOverlayTestAccess::card(fixture.guide);
    const auto target = LearningOverlayTestAccess::target(fixture.guide);
    expect(LearningOverlayTestAccess::safe(fixture.guide).contains(card),
           "the guide card respects safe area in portrait, landscape and desktop");
    expect(card.getWidth() <= 360 && card.getHeight() <= 300 &&
               (size.y >= 500 || card.getHeight() <= 210),
           "the step card gains reading space in tall layouts and stays compact in landscape phones");
    expect(!card.intersects(target), "the card avoids an actionable target when space exists");
    expect(!fixture.guide.hitTest(target.getCentreX(), target.getCentreY()),
           "the spotlight hole lets the real control receive input");
    expect(fixture.getComponentAt(target.getCentre()) == &fixture.play,
           "JUCE hit testing reaches the real Play control through the guide");
    expect(fixture.guide.hitTest(card.getCentreX(), card.getCentreY()),
           "the card keeps its own input available");
    expect(fixture.guide.hitTest(2, 2), "the shaded backdrop blocks unintended actions");
    expectActionsInside(fixture.guide);
    expect(buttonNamed(fixture.guide, "learningPrevious").getWidth() == 44 &&
               buttonNamed(fixture.guide, "learningNext").getWidth() <= 124,
           "navigation uses a shared back icon and a compact text-width forward action");
    for (const auto *name : {"learningPrevious", "learningNext", "learningMore", "learningPause"})
      expect(buttonNamed(fixture.guide, name).getHeight() == 44,
             "compact visuals preserve comfortable 44-point touch targets");
    auto &viewport = LearningOverlayTestAccess::stepViewport(fixture.guide);
    expect(viewport.getViewedComponent()->getHeight() > 0 && viewport.getHeight() > 0,
           "step instructions have a readable viewport");
    auto *text = fixture.getComponentAt(fixture.getLocalPoint(&viewport,
        juce::Point<int>{viewport.getWidth() / 2, 8}));
    expect(text == viewport.getViewedComponent(),
           "text receives pointer events so touch scrolling reaches the viewport listener");
    juce::MouseWheelDetails wheel;
    wheel.deltaY = -1.0f;
    text->mouseWheelMove(pointerEvent(*text), wheel);
    if (viewport.getViewedComponent()->getHeight() > viewport.getHeight()) {
      expect(viewport.getViewPositionY() > 0,
             "a real wheel event over guide text scrolls to instructions below the fold");
      scrolledText = true;
    }
    viewport.setViewPosition(0, 0);
    capture(fixture, "learning-step-" + juce::String(size.x) + "x" + juce::String(size.y));
  }
  expect(scrolledText, "compact profiles exercise pointer scrolling through longer instructions");
}

void testValidatedStepReadiness() {
  LearningGuideOverlay guide;
  guide.setSize(390, 844);
  const auto lesson = longLesson();
  guide.showStep(lesson, 1, {300, 100, 44, 44}, true);
  int next = 0, previous = 0, pause = 0, courses = 0, restart = 0;
  guide.onNext = [&] { ++next; };
  guide.onPrevious = [&] { ++previous; };
  guide.onPause = [&] { ++pause; };
  guide.onShowCourses = [&] { ++courses; };
  guide.onRestart = [&] { ++restart; };
  const auto pending = L10n::tr("Abre el panel para continuar.", "Open the panel to continue.");
  guide.setStepReadiness(false, pending);
  auto &forward = buttonNamed(guide, "learningNext");
  expect(!forward.isEnabled() && forward.getDescription() == pending,
         "a pending real action disables Finish and exposes its instruction to accessibility");
  expect(buttonNamed(guide, "learningPrevious").isEnabled() &&
             buttonNamed(guide, "learningMore").isEnabled() &&
             buttonNamed(guide, "learningPause").isEnabled(),
         "pending tasks retain all escape and course navigation actions");
  expect(!guide.hitTest(322, 122),
         "a pending action leaves the actual highlighted control interactive");
  forward.onClick();
  guide.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  guide.setStepReadiness(true, L10n::tr("Panel abierto", "Panel opened"));
  drainMessages();
  expect(next == 0,
         "a blocked tap or shortcut cannot wait in the queue and advance when the task becomes ready");
  forward.onClick();
  guide.setStepReadiness(false, pending);
  guide.setStepReadiness(true, {});
  drainMessages();
  expect(next == 0, "changing validation cancels a forward action queued for the previous state");
  forward.onClick();
  drainMessages();
  expect(next == 1, "a validated task enables the current forward action");
  guide.setStepReadiness(false, pending);
  guide.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
  guide.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
  LearningOverlayTestAccess::showCourses(guide);
  LearningOverlayTestAccess::restart(guide);
  drainMessages();
  expect(previous == 1 && pause == 1 && courses == 1 && restart == 1,
         "validation never blocks previous, pause, courses or restart");
  guide.showStep(lesson, 0, {}, true);
  expect(forward.isEnabled() && forward.getDescription().isEmpty(),
         "a new step clears the previous task state until its host validates it");
  const int initialTextHeight = LearningOverlayTestAccess::stepViewport(guide)
                                    .getViewedComponent()->getHeight();
  guide.setStepReadiness(false, lesson.steps.front().task);
  expect(LearningOverlayTestAccess::stepViewport(guide)
             .getViewedComponent()->getHeight() == initialTextHeight,
         "a pending instruction equal to the task is displayed once");
  guide.setStepReadiness(false, pending);
  expect(!forward.isEnabled(), "Continue also waits for action completion outside the final step");
  capture(guide, "learning-pending-action");
}

void testGuideReparentedInsideSettings() {
  juce::Component editor;
  editor.setVisible(true);
  editor.setSize(390, 844);
  SettingsPanel settings(ThemePreset::Spotify, {}, AppLanguage::Spanish, {},
      false, {}, false, {}, false, {}, PadsCrossfadePreset{}, {},
      MetronomeClickStyle{}, {}, {}, {}, {}, {}, {}, 8, {}, nullptr, 0, 8);
  editor.addAndMakeVisible(settings);
  settings.setBounds(editor.getLocalBounds());
  SettingsPanelTestAccess::openRemote(settings);
  LearningGuideOverlay guide;
  editor.addAndMakeVisible(guide);
  guide.setBounds(editor.getLocalBounds());
  guide.showStep(longLesson(), 0, {}, true);
  settings.addAndMakeVisible(guide);
  guide.setBounds(settings.getLocalBounds());
  guide.toFront(false);
  auto &clientButton = SettingsPanelTestAccess::clientButton(settings);
  const auto target = guide.getLocalArea(&clientButton, clientButton.getLocalBounds());
  guide.updateTarget(target, true);
  guide.setStepReadiness(false, L10n::tr("Selecciona Cliente.", "Select Client."));
  expect(guide.getParentComponent() == &settings &&
             guide.getLocalBounds().contains(target) && !target.isEmpty(),
         "reparenting resolves a real settings action into the overlay coordinate space");
  expect(!guide.hitTest(target.getCentreX(), target.getCentreY()),
         "the guide inside Settings leaves its actual segmented control interactive");
  auto *actual = dynamic_cast<juce::Button *>(
      settings.getComponentAt(settings.getLocalPoint(&guide, target.getCentre())));
  expect(actual == &clientButton,
         "JUCE traverses the reparented guide to the production Client control");
  using Mode = playworship::remote::RemoteSession::Mode;
  int modeChanges = 0;
  settings.onRemoteModeChanged = [&](Mode mode) {
    if (mode == Mode::Client) {
      ++modeChanges;
      guide.setStepReadiness(true, L10n::tr("Cliente seleccionado", "Client selected"));
    }
  };
  actual->onClick();
  expect(modeChanges == 1 && buttonNamed(guide, "learningNext").isEnabled(),
         "the real Settings callback validates the action without a duplicate guide control");
  capture(settings, "learning-settings-client");
  buttonNamed(guide, "learningMore").onClick();
  const auto *menu = juce::Component::getCurrentlyModalComponent();
  const bool embeddedGuideMenu = menu != nullptr && guide.isParentOf(menu) &&
                                !menu->isOnDesktop();
  capture(settings, "learning-settings-options");
  juce::PopupMenu::dismissAllActiveMenus();
  drainMessages();
  expect(embeddedGuideMenu,
         "guide options render as an owned child modal inside Settings instead of a separate desktop window");
  expect(guide.isVisible() && settings.isVisible() &&
             guide.getParentComponent() == &settings && !guide.isShowingCatalog(),
         "dismissing its own menu leaves the guide at the same visible step");
  editor.addAndMakeVisible(guide);
  guide.setBounds(editor.getLocalBounds());
  guide.updateTarget({320, 60, 44, 44}, true);
  expect(guide.getParentComponent() == &editor &&
             LearningOverlayTestAccess::target(guide) == juce::Rectangle<int>(320, 60, 44, 44),
         "returning the guide to the editor removes stale panel coordinates");
}

void testTargetUpdatesAndBoundarySteps() {
  LearningGuideOverlay guide;
  guide.setSize(390, 844);
  guide.setSafeAreaInsets({32, 12, 24, 12});
  const auto lesson = longLesson();
  guide.showStep(lesson, -9, {330, 80, 44, 44}, false);
  expect(LearningOverlayTestAccess::index(guide) == 0,
         "negative saved step indexes clamp to the first step");
  expect(!buttonNamed(guide, "learningPrevious").isEnabled(),
         "Previous is disabled on the first step");
  auto &textViewport = LearningOverlayTestAccess::stepViewport(guide);
  const int missingSongHeight = textViewport.getViewedComponent()->getHeight();
  guide.updateTarget({330, 80, 44, 44}, true);
  expect(textViewport.getViewedComponent()->getHeight() < missingSongHeight,
         "loading a song removes the reading-only explanation without resetting the course");
  const int readyHeight = textViewport.getViewedComponent()->getHeight();
  guide.setStatusMessage(L10n::tr("Puedes seguir usando el panel abierto.",
                                 "You can continue using the open panel."));
  expect(textViewport.getViewedComponent()->getHeight() > readyHeight,
         "controller status messages remain reachable inside the text viewport");
  guide.showStep(lesson, 100, {330, 80, 44, 44}, true);
  expect(LearningOverlayTestAccess::index(guide) == 1,
         "indexes beyond the lesson clamp to the final step");
  expect(buttonNamed(guide, "learningNext").getButtonText() == L10n::tr("Terminar", "Finish"),
         "the last step offers Finish rather than a nonexistent next step");
  expect(buttonNamed(guide, "learningPrevious").isEnabled(),
         "Previous becomes available after the first step");
  guide.updateTarget({-50, -50, 120, 120}, true);
  expect(guide.getLocalBounds().contains(LearningOverlayTestAccess::target(guide)),
         "target updates clip to the current screen and its safe area");
  guide.updateTarget({700, 900, 44, 44}, true);
  expect(LearningOverlayTestAccess::target(guide).isEmpty(),
         "offscreen targets do not expose unrelated controls");
  expect(guide.hitTest(30, 100), "an unavailable target leaves the backdrop blocking input");
  guide.showStep({}, 0, {}, false);
  expect(!buttonNamed(guide, "learningNext").isEnabled(),
         "empty or invalid courses cannot advance through nonexistent steps");
}

void testCatalogScrollAndSavedProgress() {
  const auto temp = juce::File(juce::String(std::filesystem::temp_directory_path().string()))
                        .getChildFile("playworship-guide-" + juce::Uuid().toString() + ".json");
  playworship::learning::ProgressStore store(temp);
  auto lessons = playworship::learning::makeLessons(true, L10n::isEnglish());
  expect(lessons.size() >= 4, "the guide exposes its available courses");
  expect(store.record(lessons.front(), 0, false).wasOk(),
         "the catalog can read real saved course progress");
  expect(store.record(lessons[1], static_cast<int>(lessons[1].steps.size()) - 1, true).wasOk(),
         "completed courses are available to the catalog");
  for (const auto size : {juce::Point<int>(320, 667), juce::Point<int>(667, 375),
                          juce::Point<int>(390, 844), juce::Point<int>(768, 1024)}) {
    LearningGuideOverlay guide;
    guide.setSize(size.x, size.y);
    guide.setSafeAreaInsets({24, 12, 20, 12});
    guide.showCatalog(lessons, store);
    expect(guide.isShowingCatalog(), "course presentation changes to catalog mode");
    expect(LearningOverlayTestAccess::safe(guide).contains(LearningOverlayTestAccess::card(guide)),
           "the course catalog stays in the device safe area");
    expectActionsInside(guide);
    auto &viewport = LearningOverlayTestAccess::catalogViewport(guide);
    expect(viewport.getViewedComponent()->getNumChildComponents() == static_cast<int>(lessons.size()),
           "every course is reachable from the catalog viewport");
    expect(viewport.getViewedComponent()->getChildComponent(0)->getDescription().contains(
               L10n::tr("Continuar", "Continue")),
           "an unfinished course displays a resume action");
    expect(viewport.getViewedComponent()->getChildComponent(1)->getDescription().contains(
               L10n::tr("Completado", "Completed")),
           "a finished course displays its completed state");
    viewport.setViewPosition(0, viewport.getViewedComponent()->getHeight());
    expect(viewport.getViewPositionY() > 0 ||
               viewport.getViewedComponent()->getHeight() <= viewport.getHeight(),
           "catalog scrolling reaches courses below the fold");
    expect(guide.hitTest(20, 20), "catalog mode does not leave a stale spotlight input hole");
    viewport.setViewPosition(0, 0);
    capture(guide, "learning-catalog-" + juce::String(size.x) + "x" + juce::String(size.y));
  }
  auto revisedLessons = lessons;
  revisedLessons[1].steps.back().id = "new-final-step";
  LearningGuideOverlay revisedGuide;
  revisedGuide.setSize(390, 844);
  revisedGuide.showCatalog(revisedLessons, store);
  auto *revisedRow = LearningOverlayTestAccess::catalogViewport(revisedGuide)
                        .getViewedComponent()->getChildComponent(1);
  expect(!revisedRow->getDescription().contains(L10n::tr("Completado", "Completed")),
         "a completed record with an unknown step cannot mark a revised course complete");
  temp.deleteFile();
}

void testPracticeTargetsAndLargeWaveform() {
  using Anchor = playworship::learning::Anchor;
  int audioActions = 0;
  PracticePanel::Sources sources;
  sources.songId = [] { return juce::String("learning-test-song"); };
  sources.title = [] { return juce::String::fromUTF8("Canción de prueba"); };
  sources.songKey = [] { return juce::String("C"); };
  sources.songBpm = sources.tempoBpm = [] { return 120.0; };
  sources.timeSignature = [] { return juce::String("4/4"); };
  sources.positionSeconds = [] { return 30.0; };
  sources.lengthSeconds = [] { return 180.0; };
  sources.canControl = [] { return true; };
  sources.instruments = [] { return std::vector<PracticePanel::Instrument>{{"bass", "Bajo"}}; };
  sources.requestInstrumentWaveform = [](const auto &, auto, auto deliver) {
    std::vector<float> bins(256);
    for (size_t i = 0; i < bins.size(); ++i)
      bins[i] = 0.1f + 0.7f * std::abs(std::sin(static_cast<float>(i) * 0.13f));
    deliver(std::move(bins));
  };
  sources.playPause = sources.stop = [&] { ++audioActions; };
  sources.seekToSeconds = sources.setTempoBpm = [&](double) { ++audioActions; };
  sources.setPitchSemitones = [&](int) { ++audioActions; };
  juce::Component host;
  host.setVisible(true);
  PracticePanel practice(std::move(sources));
  LearningGuideOverlay guide;
  host.addAndMakeVisible(practice);
  host.addAndMakeVisible(guide);
  const auto lessons = playworship::learning::makeLessons(true, L10n::isEnglish());
  const auto course = std::find_if(lessons.begin(), lessons.end(), [](const auto &entry) {
    return entry.id == "practice-sections";
  });
  expect(course != lessons.end(), "Practice remains available by its stable course ID");
  const auto &lesson = *course;
  const auto loopStep = std::find_if(lesson.steps.begin(), lesson.steps.end(), [](const auto &step) {
    return step.anchor == Anchor::PracticeLoop;
  });
  expect(loopStep != lesson.steps.end(), "Practice exposes its real A-B step");
  const int loopIndex = static_cast<int>(std::distance(lesson.steps.begin(), loopStep));
  for (const auto size : {juce::Point<int>(320, 667), juce::Point<int>(390, 844)}) {
    host.setSize(size.x, size.y);
    practice.setBounds(host.getLocalBounds());
    guide.setBounds(host.getLocalBounds());
    guide.setSafeAreaInsets({24, 0, 20, 0});
    const auto loopTarget = practice.getLearningTargetBounds(Anchor::PracticeLoop);
    guide.showStep(lesson, loopIndex, loopTarget, true);
    expect(!guide.hitTest(loopTarget.getCentreX(), loopTarget.getCentreY()),
           "A-B lesson leaves the waveform usable for moving between its marks");
    const auto playTarget = practice.getLearningTargetBounds(Anchor::PlayPause);
    expect(!guide.hitTest(playTarget.getCentreX(), playTarget.getCentreY()) &&
               dynamic_cast<juce::Button *>(host.getComponentAt(playTarget.getCentre())) != nullptr,
           "A-B lesson keeps the actual playback and region controls reachable");
    expectActionsInside(guide);
    capture(host, "learning-practice-ab-" + juce::String(size.x));
    const auto closeTarget = practice.getLearningTargetBounds(Anchor::PracticeExit);
    guide.showStep(lessons.front(), 0, closeTarget, true);
    expect(!guide.hitTest(closeTarget.getCentreX(), closeTarget.getCentreY()),
           "switching courses can expose the real Practice close button");
  }
  expect(audioActions == 0, "opening and navigating help does not issue audio actions");
  host.setSize(844, 390);
  guide.setBounds(host.getLocalBounds());
  guide.showStep(lessons.front(), 3, {0, 65, 844, 150}, true);
  const auto target = LearningOverlayTestAccess::target(guide);
  const auto card = LearningOverlayTestAccess::card(guide);
  expect(target.getHeight() - target.getIntersection(card).getHeight() >= 44,
         "a wide phone waveform keeps an interactive lane outside the reading card");
  capture(guide, "learning-wide-waveform");
}

void testAsynchronousCallbacksAndKeyboard() {
  auto guide = std::make_unique<LearningGuideOverlay>();
  guide->setSize(390, 844);
  const auto lesson = longLesson();
  guide->showStep(lesson, 0, {}, true);
  int next = 0, pause = 0, previous = 0;
  guide->onNext = [&] { ++next; };
  guide->onPrevious = [&] { ++previous; };
  guide->onPause = [&] { ++pause; };
  expect(!guide->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)),
         "the guide does not take the audio transport's Space shortcut");
  expect(guide->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)), "Right advances a focused guide");
  expect(next == 0, "guide callbacks are deferred outside the triggering event");
  drainMessages();
  expect(next == 1, "the deferred callback reaches the current guide controller");
  guide->keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
  drainMessages();
  expect(previous == 0, "keyboard Previous obeys the first-step boundary");
  guide->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  guide->showStep(lesson, 1, {}, true);
  drainMessages();
  expect(next == 1, "a deferred action from an old step cannot advance a replaced card");
  guide->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
  drainMessages();
  expect(pause == 1, "Escape requests a pause so the controller can preserve progress");
  guide->onNext = [&] { guide.reset(); };
  guide->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  drainMessages();
  expect(guide == nullptr, "a navigation callback may safely destroy the overlay");
  guide = std::make_unique<LearningGuideOverlay>();
  guide->showStep(lesson, 0, {}, true);
  guide->onNext = [&] { ++next; };
  guide->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
  guide.reset();
  drainMessages();
  expect(next == 1, "destroyed overlays do not deliver queued controller callbacks");

  const auto temp = juce::File(juce::String(std::filesystem::temp_directory_path().string()))
                        .getChildFile("playworship-guide-click-" + juce::Uuid().toString() + ".json");
  playworship::learning::ProgressStore progress(temp);
  guide = std::make_unique<LearningGuideOverlay>();
  guide->setSize(390, 844);
  guide->showCatalog({lesson}, progress);
  juce::String selected;
  guide->onLessonSelected = [&](const juce::String &id) { selected = id; };
  auto *course = dynamic_cast<juce::Button *>(
      LearningOverlayTestAccess::catalogViewport(*guide).getViewedComponent()->getChildComponent(0));
  expect(course != nullptr, "catalog courses are real selectable buttons");
  static_cast<juce::Component &>(*course).mouseDown(pointerEvent(*course));
  static_cast<juce::Component &>(*course).mouseUp(pointerEvent(*course, true));
  drainMessages();
  expect(selected.isEmpty(), "scrolling over a course does not select it on release");
  course->onClick();
  expect(selected.isEmpty(), "catalog selection is deferred outside the button callback");
  drainMessages();
  expect(selected == lesson.id, "catalog selection returns the stable course ID");
  selected.clear();
  course->onClick();
  guide->showStep(lesson, 0, {}, true);
  drainMessages();
  expect(selected.isEmpty(), "a stale catalog click cannot replace an already opened course");
  temp.deleteFile();
}
} // namespace

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr) return 77;
#endif
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  int failures = 0;
  const auto run = [&](const char *name, auto test) {
    try {
      test();
      std::cout << "PASS: " << name << '\n';
    } catch (const std::exception &error) {
      ++failures;
      std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
    }
  };
  run("shared cloud guide phone and desktop", testSharedCloudCourse);
  run("responsive spotlight and actual control hit testing", testResponsiveSpotlightAndInteraction);
  run("target updates and course step boundaries", testTargetUpdatesAndBoundarySteps);
  run("validated tasks retain navigation and real control access", testValidatedStepReadiness);
  run("real settings actions through a reparented guide", testGuideReparentedInsideSettings);
  run("scrollable course catalog with saved progress", testCatalogScrollAndSavedProgress);
  run("real Practice targets and wide phone waveform", testPracticeTargetsAndLargeWaveform);
  run("safe asynchronous callbacks and focused keyboard input", testAsynchronousCallbacksAndKeyboard);
  return failures == 0 ? 0 : 1;
}
