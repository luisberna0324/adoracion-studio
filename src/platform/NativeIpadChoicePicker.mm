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
      if (window.isKeyWindow)
        return window.rootViewController;
    }
  }
  return nil;
}

} // namespace

namespace NativeIpadChoicePicker {

bool isAvailable() {
  return UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad;
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
    UIAlertAction* action = [UIAlertAction actionWithTitle:title
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
  [menu addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                             style:UIAlertActionStyleCancel
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
