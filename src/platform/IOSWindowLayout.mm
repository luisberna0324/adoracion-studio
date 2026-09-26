#import <UIKit/UIKit.h>

#include "IOSWindowLayout.h"

namespace {
const juce::Identifier orientationRequestKey("playworshipIOSOrientationRequest");

void synchroniseWindow(juce::ResizableWindow &window, bool waitForTransition) {
  auto *peer = window.getPeer();
  if (peer == nullptr)
    return;
  auto *view = static_cast<UIView *>(peer->getNativeHandle());
  auto *nativeWindow = view.window;
  if (nativeWindow == nil)
    return;

  const juce::Component::SafePointer<juce::ResizableWindow> safeWindow(&window);
  auto coordinator = nativeWindow.rootViewController.transitionCoordinator;
  if (waitForTransition && coordinator != nil && coordinator.isAnimated) {
    [coordinator animateAlongsideTransition:nil
        completion:^(id<UIViewControllerTransitionCoordinatorContext>) {
          juce::MessageManager::callAsync([safeWindow] {
            if (safeWindow != nullptr)
              synchroniseWindow(*safeWindow, false);
          });
        }];
    return;
  }

  if (@available(iOS 16.0, *)) {
    auto *scene = nativeWindow.windowScene;
    auto *controller = nativeWindow.rootViewController;
    if (scene != nil && controller != nil) {
      const auto allowed = controller.supportedInterfaceOrientations;
      const auto current = static_cast<UIInterfaceOrientationMask>(
          1u << scene.interfaceOrientation);
      if ((allowed & current) == 0) {
        // At launch JUCE can set its orientation mask before a UIWindowScene
        // exists. An unchanged mask will then never request native rotation.
        // Reconcile the scene itself once it is attached, even if JUCE already
        // reports the desired allowed orientations.
        auto &properties = window.getProperties();
        const int requestedMask = static_cast<int>(allowed);
        if (static_cast<int>(properties.getWithDefault(orientationRequestKey, 0))
            != requestedMask) {
          properties.set(orientationRequestKey, requestedMask);
          [controller setNeedsUpdateOfSupportedInterfaceOrientations];
          auto *preferences = [[UIWindowSceneGeometryPreferencesIOS alloc]
              initWithInterfaceOrientations:allowed];
          [scene requestGeometryUpdateWithPreferences:preferences
              errorHandler:^(NSError *error) {
                const auto message = juce::String(error.localizedDescription.UTF8String);
                juce::MessageManager::callAsync([safeWindow, message] {
                  if (safeWindow != nullptr)
                    safeWindow->getProperties().remove(orientationRequestKey);
                  juce::Logger::writeToLog("iOS orientation request: " + message);
                });
              }];
#if !__has_feature(objc_arc)
          [preferences release];
#endif
        }
        return; // UIKit owns the geometry until its transition completes.
      }
      window.getProperties().remove(orientationRequestKey);
    }
  }

  // The active scene is the source of truth. JUCE's display cache may still
  // contain the portrait launch size during the first landscape transition.
  CGRect bounds = nativeWindow.bounds;
  if (@available(iOS 13.0, *)) {
    if (auto *scene = nativeWindow.windowScene)
      bounds = scene.coordinateSpace.bounds;
  }
  const juce::Rectangle<int> target(juce::roundToInt(bounds.origin.x),
                                     juce::roundToInt(bounds.origin.y),
                                     juce::roundToInt(bounds.size.width),
                                     juce::roundToInt(bounds.size.height));
  if (target.isEmpty())
    return;
  // The view can already have the landscape size while its UIWindow still
  // clips it to the previous portrait frame. Check both native levels.
  if (!peer->isFullScreen() || peer->getBounds() != target ||
      !CGRectEqualToRect(nativeWindow.frame, bounds) ||
      window.getLocalBounds() != target.withPosition(0, 0))
    peer->setBounds(target, true);

  // A child editor can have changed size while the window kept the same
  // bounds. Explicitly lay out both levels, even in that case.
  static_cast<juce::Component &>(window).resized();
  if (auto *content = window.getContentComponent())
    content->resized();
}
} // namespace

void synchroniseIOSFullscreenWindow(juce::ResizableWindow &window) {
  synchroniseWindow(window, true);
}
