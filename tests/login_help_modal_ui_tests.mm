#import <Cocoa/Cocoa.h>
#include "ui/components/WelcomeModal.h"
#include "ui/components/ServiceTermsPrompt.h"
#include "ui/learning/LearningGuideOverlay.h"
#include "ui/learning/DemoLearningProgress.h"
#include "ui/panels/WaveformBar.h"
#include "data/Localization.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void expect(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

void drain() { juce::MessageManager::getInstance()->runDispatchLoopUntil(90); }

void drainUntil(const std::function<bool()>& completed) {
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 750.0;
  while (!completed() && juce::Time::getMillisecondCounterHiRes() < deadline)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

juce::Button& button(juce::Component& host, const char* name) {
  for (auto* child : host.getChildren())
    if (child->getName() == name)
      if (auto* result = dynamic_cast<juce::Button*>(child)) return *result;
  throw std::runtime_error("missing guide button");
}

void capture(juce::Component& component, const juce::String& name) {
  const auto path = juce::SystemStats::getEnvironmentVariable("PW_MODAL_CAPTURE_DIR", {});
  if (path.isEmpty()) return;
  const juce::File directory(path);
  expect(directory.createDirectory().wasOk(), "capture directory available");
  auto stream = directory.getChildFile(name + ".png").createOutputStream();
  expect(stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk(), "capture file available");
  auto snapshot = component.createComponentSnapshot(component.getLocalBounds(), true, 2.0f);
 #if JUCE_MAC
  // JUCE snapshots omit NSView children. Render each real native control with
  // AppKit, then place its pixels at the component's measured position.
  std::function<void(juce::Component&)> includeNative = [&](juce::Component& child) {
    if (!child.isShowing()) return;
    if (auto* native = dynamic_cast<juce::NSViewComponent*>(&child)) {
      auto* view = (__bridge NSView*)native->getView();
      if (view != nil && !view.hidden) {
        auto* bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
        auto* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        auto image = juce::ImageFileFormat::loadFrom(png.bytes, png.length);
        expect(image.isValid(), "native Apple control renders from its actual NSView");
        const auto bounds = component.getLocalArea(&child, child.getLocalBounds());
        juce::Graphics graphics(snapshot);
        graphics.drawImage(image, bounds.getX() * 2, bounds.getY() * 2,
            bounds.getWidth() * 2, bounds.getHeight() * 2,
            0, 0, image.getWidth(), image.getHeight());
      }
    }
    for (auto* nested : child.getChildren()) includeNative(*nested);
  };
  includeNative(component);
 #endif
  expect(juce::PNGImageFormat().writeImageToStream(snapshot, *stream),
      "actual production components render to PNG");
}

struct Surface : juce::Component {
  void paint(juce::Graphics& g) override { g.fillAll(UITheme::appBg); }
};

void testNativeLoginAndAccountConsent() {
  Surface host;
  host.setSize(390, 844);
  host.addToDesktop(0);
  host.setVisible(true);
  WelcomeModal welcome;
  host.addAndMakeVisible(welcome);
  welcome.setBounds(host.getLocalBounds());
  welcome.setAppleSignInAvailable(true);
  juce::Component* native = nullptr;
  for (auto* child : welcome.getChildren()) {
   #if JUCE_MAC
    if (dynamic_cast<juce::NSViewComponent*>(child)) native = child;
   #elif JUCE_IOS
    if (dynamic_cast<juce::UIViewComponent*>(child)) native = child;
   #endif
  }
  expect(native != nullptr && native->isShowing(), "a real native Apple button is initially visible");
  drain();
  expect(juce::Component::getCurrentlyModalComponent() == nullptr,
         "login starts without an informational modal");
  capture(host, "login-clean-390x844");
  host.setSize(1200, 760);
  welcome.setBounds(host.getLocalBounds());
  drain();
  expect(native->isShowing() && welcome.getLocalBounds().contains(native->getBounds()),
      "native Apple action also fits the desktop welcome layout");
  capture(host, "login-clean-desktop-1200x760");
  host.setSize(390, 844);
  welcome.setBounds(host.getLocalBounds());
  drain();
  int loginRequests = 0;
  welcome.onAppleLoginRequested = [&] { ++loginRequests; };
  {
    ServiceTermsPanel introduction({}, false);
    introduction.attach(host);
    introduction.enterModalState(false);
    // The first JUCE paint hides the native view; the timer also handles
    // modal changes and restores the same button after cancellation.
    capture(host, "cloud-consent-390x844");
    drain();
    expect(welcome.isCurrentlyBlockedByAnotherModalComponent() && !native->isVisible(),
           "account consent obscures and hides the native sign-in control");
   #if JUCE_MAC
    expect(((__bridge NSView*)dynamic_cast<juce::NSViewComponent*>(native)->getView()).hidden,
           "the native AppKit button is actually hidden under the modal");
   #endif
    expect(introduction.primary.getButtonText() == L10n::tr("Aceptar y continuar", "Accept and continue"),
           "account consent still requires an explicit accept action");
    expect(introduction.secondary.getButtonText() == L10n::tr("Ahora no", "Not now"),
           "account consent retains its dismiss action");
    introduction.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    drain();
    expect(!introduction.isCurrentlyModal(), "Escape dismisses consent without acceptance");
    introduction.setVisible(false);
  }
  drain();
  expect(native->isShowing(), "cancelling the modal restores the native Apple button");
  welcome.setChecking(true);
  drain();
  expect(!native->isVisible(), "the modal timer does not reveal Apple during access checking");
  welcome.setChecking(false);
  welcome.setAuthenticationBusy(true);
  expect(native->isVisible() && !native->isEnabled(), "busy sign-in keeps the restored button disabled");
  {
    ServiceTermsPanel consent({}, false);
    consent.attach(host);
    consent.enterModalState(false);
    drain();
    expect(!native->isVisible(), "account consent also hides the native login control");
    expect(consent.primary.getButtonText() == L10n::tr("Aceptar y continuar", "Accept and continue"),
           "authenticated consent remains distinct from information");
    capture(host, "cloud-consent-390x844");
    consent.secondary.onClick();
    drain();
    expect(!consent.isCurrentlyModal(), "account consent remains cancellable");
  }
  expect(loginRequests == 0, "view and modal verification never requests Apple authentication");
  host.removeFromDesktop();
}

void testGuideFooter() {
  const auto lessons = playworship::learning::makeLessons(true, false);
  const auto found = std::find_if(lessons.begin(), lessons.end(), [](const auto& lesson) {
    return lesson.id == "demo-song";
  });
  expect(found != lessons.end(), "the real demo course is available");
  Surface host;
  host.setVisible(true);
  LearningGuideOverlay guide;
  host.addAndMakeVisible(guide);
  int pauses = 0, next = 0;
  guide.onPause = [&] { ++pauses; };
  guide.onNext = [&] { ++next; };
  for (const auto size : {juce::Point<int>(320, 667), juce::Point<int>(390, 844),
                          juce::Point<int>(844, 390), juce::Point<int>(1200, 800)}) {
    host.setSize(size.x, size.y);
    guide.setBounds(host.getLocalBounds());
    guide.setSafeAreaInsets({24, 12, 20, 12});
    guide.showStep(*found, 1, {}, true);
    guide.setStepReadiness(false, found->steps[1].task);
    auto& close = button(guide, "learningPause");
    auto& forward = button(guide, "learningNext");
    auto& previous = button(guide, "learningPrevious");
    expect(close.isVisible() && close.isEnabled() && !forward.isEnabled(),
           "Close remains available when Continue waits for the exercise");
    expect(close.getY() == forward.getY() && close.getRight() + 8 == forward.getX(),
           "Close sits beside Continue in the footer");
    expect(close.getHeight() == 44 && forward.getHeight() == 44 && close.getWidth() >= 44,
           "footer actions preserve touch targets");
    expect(!button(guide, "learningCatalogClose").isVisible(),
           "the step has no redundant upper close icon");
    expect(std::none_of(guide.getChildren().begin(), guide.getChildren().end(),
             [](const auto* child) { return child->getName() == "learningMore"; }),
           "the help step has no overflow control or reserved menu header");
    for (auto* child : guide.getChildren())
      if (auto* viewport = dynamic_cast<juce::Viewport*>(child); viewport && viewport->isVisible())
        expect(viewport->getHeight() >= 130, "removing the menu header gives the instructions more reading space");
    expect(host.getComponentAt(host.getLocalPoint(&close, close.getLocalBounds().getCentre())) == &close,
           "a real pointer or touch reaches the footer Close button");
    expect(previous.getExplicitFocusOrder() < close.getExplicitFocusOrder() &&
           close.getExplicitFocusOrder() < forward.getExplicitFocusOrder(),
           "keyboard focus follows the footer's visual order");
    std::vector<juce::Rectangle<int>> occupied;
    for (auto* child : guide.getChildren()) {
      if (!child->isVisible()) continue;
      expect(!child->getBounds().isEmpty() && guide.getLocalBounds().contains(child->getBounds()),
             "all visible controls remain in the surface");
      for (const auto& bounds : occupied)
        expect(!bounds.intersects(child->getBounds()), "footer and reading controls do not overlap");
      occupied.push_back(child->getBounds());
    }
    expect(!guide.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)),
           "help leaves the transport Space shortcut available");
    guide.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    drain();
    expect(next == 0, "keyboard Continue cannot skip an unvalidated exercise");
    close.onClick();
    drain();
    expect(pauses > 0 && next == 0, "Close requests pause without advancing the lesson");
    const auto previousPauses = pauses;
    guide.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    drain();
    expect(pauses == previousPauses + 1, "Escape still pauses from the footer layout");
    capture(host, "guide-footer-" + juce::String(size.x) + "x" + juce::String(size.y));
    guide.showStep(*found, static_cast<int>(found->steps.size()) - 1, {}, true);
    guide.setStepReadiness(true, {});
    expect(forward.getButtonText() == L10n::tr("Terminar", "Finish"),
           "only the final step presents Finish beside Close");
    capture(host, "guide-finish-" + juce::String(size.x) + "x" + juce::String(size.y));
    forward.onClick();
    drain();
    expect(next == 1, "Finish advances only after the final step is ready");
    next = 0;
  }
}

void testVerticalAccountConsent() {
  for (const auto size : {juce::Point<int>(320, 667), juce::Point<int>(390, 844),
                          juce::Point<int>(844, 390), juce::Point<int>(1200, 800)}) {
    Surface host;
    host.setVisible(true);
    host.setSize(size.x, size.y);
    ServiceTermsPanel consent({}, false);
    consent.attach(host);
    const auto card = consent.cardBounds();
    expect(card.getHeight() > card.getWidth() && host.getLocalBounds().contains(card),
           "account consent uses a vertical card inside portrait and landscape surfaces");
    expect(card.contains(consent.primary.getBounds()) && card.contains(consent.secondary.getBounds()) &&
           consent.primary.getHeight() >= 44 && consent.secondary.getHeight() >= 44,
           "accept and decline remain readable, tappable and inside the card");
    expect(consent.primary.getX() == consent.secondary.getX() &&
           consent.primary.getBottom() < consent.secondary.getY(),
           "consent actions stay vertically stacked without overlap");
    auto& viewport = consent.bodyViewport();
    expect(viewport.getViewHeight() >= 44, "vertical consent retains a reading viewport");
    capture(host, "terms-vertical-" + juce::String(size.x) + "x" + juce::String(size.y));
    viewport.setViewPosition(0, viewport.getViewedComponent()->getHeight());
    expect(viewport.getViewPositionY() + viewport.getViewHeight() >=
           viewport.getViewedComponent()->getHeight(), "all legal content and links are reachable by scrolling");
    if (size.x > size.y)
      capture(host, "terms-vertical-" + juce::String(size.x) + "x" + juce::String(size.y) + "-bottom");
  }
}

void testDemoSectionLoopRoute() {
  using namespace playworship::learning;
  for (const bool phone : {true, false}) {
    const auto lessons = makeLessons(phone, false);
    const auto course = std::find_if(lessons.begin(), lessons.end(), [](const auto& value) { return value.id == "demo-song"; });
    const auto step = std::find_if(course->steps.begin(), course->steps.end(), [](const auto& value) { return value.id == "demo-section-loop"; });
    const int index = static_cast<int>(std::distance(course->steps.begin(), step));
    expect(step->description.contains("Más") && step->description.contains("Vista de secciones") &&
           !step->description.contains("puntero"), "step 8 describes existing controls without hover instructions");
    for (const bool alreadySections : {false, true}) {
      Surface host;
      host.setSize(phone ? 844 : 1200, phone ? 390 : 800);
      host.addToDesktop(juce::ComponentPeer::windowIsTemporary);
      host.setVisible(true);
      WaveformBar waveform;
      waveform.setMinimalPhoneMode(phone);
      waveform.setTransportProviders([] { return 0.0; }, [] { return 90.0; }, [] { return false; });
      waveform.setWaveformBins(std::vector<float>(512, 0.4f));
      SongArrangementSnapshot arrangement;
      for (int i = 0; i < 3; ++i) {
        SongArrangementPartSnapshot part;
        part.id = "demo-preview-" + juce::String(i);
        part.name = i == 0 ? "Verso" : i == 1 ? "Coro" : "Puente";
        part.startSec = part.sourceStartSec = i * 30.0;
        part.endSec = part.sourceEndSec = (i + 1) * 30.0;
        arrangement.parts.push_back(part);
      }
      waveform.setArrangement(arrangement, false);
      host.addAndMakeVisible(waveform);
      waveform.setBounds(20, 64, host.getWidth() - 40, 160);
      waveform.setLiveMode(alreadySections);
      SvgToolbarButton more("fixtureGlobalMore", SvgIconName::MoreHorizontal);
      more.setTitle("Más");
      host.addAndMakeVisible(more);
      more.setBounds(host.getWidth() - 64, 10, 44, 44);
      SvgToolbarButton play("fixturePlay", SvgIconName::Play);
      host.addAndMakeVisible(play);
      play.setBounds(20, 10, 44, 44);
      LearningGuideOverlay guide;
      host.addAndMakeVisible(guide);
      guide.setBounds(host.getLocalBounds());
      guide.showStep(*course, index, {}, true);
      DemoLearningProgress progress;
      DemoLearningProgress::Snapshot snapshot;
      snapshot.songId = "local-demo-preview";
      snapshot.isDemo = true;
      progress.begin(step->id, snapshot);
      int next = 0, previous = 0, pauses = 0, loops = 0;
      const auto sync = [&] {
        const auto anchor = demoSectionLoopAnchor(Anchor::Waveform, waveform.isLiveMode(), waveform.canToggleLiveMode());
        guide.updateTarget(anchor == Anchor::More ? more.getBounds() : waveform.getBounds(), true);
        const auto playbackAnchor = demoSectionLoopAnchor(Anchor::PlayPause, waveform.isLiveMode(), waveform.canToggleLiveMode());
        expect(playbackAnchor == Anchor::PlayPause, "routing the view selector never replaces the supplementary Play destination");
        guide.setSupplementaryTarget(play.getBounds());
        guide.setStepReadiness(progress.satisfied(), progress.satisfied() ? juce::String{} : step->task);
      };
      guide.onNext = [&] { ++next; };
      guide.onPrevious = [&] { ++previous; };
      guide.onPause = [&] { ++pauses; guide.setVisible(false); };
      waveform.setActiveArrangementLoopPartProvider([&] { return snapshot.sectionLoopIndex; });
      waveform.setOnArrangementPartLoopToggleRequest([&](int part) {
        ++loops;
        snapshot.sectionLoopIndex = snapshot.sectionLoopIndex == part ? -1 : part;
        progress.observe(snapshot);
        sync();
      });
      more.onClick = [&] {
        // Production hides help while the ordinary, foreign More menu is open.
        guide.setVisible(false);
        juce::PopupMenu menu;
        menu.addItem(1, "Vista de secciones");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&more).withParentComponent(&host),
          [&](int choice) { if (choice == 1) waveform.setLiveMode(true); guide.setVisible(true); sync(); });
      };
      sync();
      expect(host.getComponentAt(play.getBounds().getCentre()) == &play,
             "Play remains available while the loop exercise is waiting");
      const auto suffix = phone ? "phone" : "desktop";
      if (!alreadySections) {
        expect(host.getComponentAt(more.getBounds().getCentre()) == &more,
               "step 8 leaves the existing global More control hittable from Waveform view");
        capture(host, "step8-waveform-" + juce::String(suffix));
        button(guide, "learningPause").onClick();
        drainUntil([&] { return pauses > 0; });
        expect(pauses == 1 && !guide.isVisible() && !progress.satisfied(),
               "Close works while still waiting for Sections view without completing the exercise");
        guide.setVisible(true);
        sync();
        more.onClick();
        auto* menu = juce::Component::getCurrentlyModalComponent();
        expect(menu != nullptr && !guide.isVisible(), "the ordinary menu remains usable while help is temporarily hidden");
        menu->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
        menu->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        drainUntil([&] { return waveform.isLiveMode() && guide.isVisible(); });
        expect(waveform.isLiveMode() && guide.isVisible(), "selecting Sections view returns to the same guide step");
      }
      expect(!button(guide, "learningNext").isEnabled() && loops == 0,
             "entering Sections view alone never completes the loop exercise");
      juce::Component* loopControl = nullptr;
      for (auto* child : waveform.getChildren())
        if (child->getComponentID() == "section-action:demo-preview-0:1") loopControl = child;
      expect(loopControl != nullptr && loopControl->isVisible(), "the real WaveformBar exposes its existing Loop control");
      const auto centre = host.getLocalPoint(loopControl, loopControl->getLocalBounds().getCentre());
      expect(host.getComponentAt(centre) == loopControl, "the guide does not block the real section Loop control");
      capture(host, "step8-sections-" + juce::String(suffix));
      loopControl->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
      expect(loops == 1 && !progress.satisfied() && !button(guide, "learningNext").isEnabled(),
             "turning Loop on alone does not allow Continue");
      loopControl->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
      expect(loops == 2 && progress.satisfied() && button(guide, "learningNext").isEnabled(),
             "the same existing on/off cycle enables Continue");
      guide.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
      drain();
      button(guide, "learningNext").onClick();
      drain();
      expect(previous == 1 && next == 1, "previous and validated forward navigation still work");
      button(guide, "learningPause").onClick();
      drain();
      expect(pauses == (alreadySections ? 1 : 2) && !guide.isVisible(), "Close still pauses the current guide");
      host.removeFromDesktop();
    }
  }
}
}

int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  try {
    testNativeLoginAndAccountConsent();
    testGuideFooter();
    testVerticalAccountConsent();
    testDemoSectionLoopRoute();
    std::cout << "PASS: login, vertical consent and step 8 Waveform-to-Sections loop route\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
