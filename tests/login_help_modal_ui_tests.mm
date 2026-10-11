#import <Cocoa/Cocoa.h>
#include "ui/components/WelcomeModal.h"
#include "ui/components/ServiceTermsPrompt.h"
#include "ui/learning/LearningGuideOverlay.h"
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
    expect(button(guide, "learningMore").isVisible() && button(guide, "learningMore").isEnabled(),
           "Courses and Restart remain available from the options menu");
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
}

int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  try {
    testNativeLoginAndAccountConsent();
    testGuideFooter();
    std::cout << "PASS: native login modal visibility, cancellation, footer layout and navigation\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
