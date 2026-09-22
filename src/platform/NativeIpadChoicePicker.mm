#import <UIKit/UIKit.h>

#include "NativeIpadChoicePicker.h"

#include <memory>

namespace {

UIViewController* activeViewController() {
  for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
    if (scene.activationState != UISceneActivationStateForegroundActive ||
        ![scene isKindOfClass:UIWindowScene.class])
      continue;
    for (UIWindow* window in ((UIWindowScene*)scene).windows) {
      if (window.isKeyWindow) {
        UIViewController* controller = window.rootViewController;
        while (controller.presentedViewController != nil &&
               !controller.presentedViewController.isBeingDismissed)
          controller = controller.presentedViewController;
        return controller;
      }
    }
  }
  return nil;
}

} // namespace

namespace NativeIpadChoicePicker {

bool isAvailable() {
  // UIKit action sheets adapt automatically: anchored popover on iPad and
  // bottom sheet on iPhone. Keeping this native on both devices gives the
  // expected touch, accessibility, keyboard and dismissal behaviour.
  return true;
}

bool show(juce::Component& anchor, const std::vector<juce::String>& items,
          const int selectedIndex, std::function<void(int)> completion) {
  if (!isAvailable() || items.empty())
    return false;

  auto* controller = activeViewController();
  if (controller == nil)
    return false;

  auto* menu = [UIAlertController alertControllerWithTitle:nil
                                             message:nil
                                      preferredStyle:UIAlertControllerStyleActionSheet];
  // The action sheet retains its handlers until dismissal. Keep one callback
  // alive for every action instead of moving it while constructing the menu.
  auto callback = std::make_shared<std::function<void(int)>>(std::move(completion));
  for (size_t i = 0; i < items.size(); ++i) {
    NSString* title = [NSString stringWithUTF8String:items[i].toRawUTF8()];
    NSString* displayedTitle = static_cast<int>(i) == selectedIndex
                                   ? [NSString stringWithFormat:@"✓ %@", title]
                                   : title;
    UIAlertAction* action = [UIAlertAction actionWithTitle:displayedTitle
                                                      style:UIAlertActionStyleDefault
                                                    handler:^(UIAlertAction*) {
      juce::MessageManager::callAsync([callback, i]() {
        if (*callback)
          (*callback)(static_cast<int>(i));
      });
    }];
    if (static_cast<int>(i) == selectedIndex)
      action.enabled = YES;
    [menu addAction:action];
  }
  const auto cancelText = juce::translate("Cancel");
  NSString* cancelTitle =
      [NSString stringWithUTF8String:cancelText.toRawUTF8()];
  // UIAlertActionStyleCancel is rendered by UIKit as a second, detached card.
  // This picker represents one flat list of mutually exclusive values, so keep
  // dismissal in that same list while preserving its no-selection behaviour.
  [menu addAction:[UIAlertAction actionWithTitle:cancelTitle
                                             style:UIAlertActionStyleDefault
                                           handler:^(UIAlertAction*) {
    juce::MessageManager::callAsync([callback]() {
      if (*callback)
        (*callback)(-1);
    });
  }]];

  if (UIPopoverPresentationController* popover = menu.popoverPresentationController) {
    auto* topLevel = anchor.getTopLevelComponent();
    auto* peer = topLevel != nullptr ? topLevel->getPeer() : anchor.getPeer();
    if (peer == nullptr)
      return false;

    auto* nativeView = (__bridge UIView*)peer->getNativeHandle();
    const auto anchorBounds = topLevel != nullptr
                                  ? topLevel->getLocalArea(&anchor, anchor.getLocalBounds())
                                  : anchor.getLocalBounds();
    popover.sourceView = nativeView;
    popover.sourceRect = CGRectMake(anchorBounds.getX(), anchorBounds.getY(),
                                    anchorBounds.getWidth(), anchorBounds.getHeight());
    popover.permittedArrowDirections = UIPopoverArrowDirectionDown |
                                       UIPopoverArrowDirectionUp;
  }
  [controller presentViewController:menu animated:YES completion:nil];
  return true;
}

} // namespace NativeIpadChoicePicker
