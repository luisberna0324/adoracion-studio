#import <AuthenticationServices/AuthenticationServices.h>
#import <UIKit/UIKit.h>

#include "NativeAuthSession.h"

@interface PlayWorshipAuthPresentationContext
    : NSObject <ASWebAuthenticationPresentationContextProviding>
@end

@implementation PlayWorshipAuthPresentationContext
- (ASPresentationAnchor)presentationAnchorForWebAuthenticationSession:
    (ASWebAuthenticationSession *)session {
  for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
    if (scene.activationState != UISceneActivationStateForegroundActive ||
        ![scene isKindOfClass:UIWindowScene.class])
      continue;

    for (UIWindow *window in ((UIWindowScene *)scene).windows)
      if (window.isKeyWindow)
        return window;
  }

  for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
    if (![scene isKindOfClass:UIWindowScene.class])
      continue;
    UIWindow *window = ((UIWindowScene *)scene).windows.firstObject;
    if (window != nil)
      return window;
  }

  return nil;
}
@end

namespace {

ASWebAuthenticationSession *activeSession = nil;

PlayWorshipAuthPresentationContext *presentationContext = nil;

void releaseSession() {
  activeSession = nil;
  presentationContext = nil;
}

} // namespace

namespace NativeAuthSession {

bool start(const juce::String &loginUrl, const juce::String &callbackScheme,
           Completion completion) {
  cancel();

  NSURL *url = [NSURL URLWithString:
      [NSString stringWithUTF8String:loginUrl.toRawUTF8()]];
  NSString *scheme = [NSString stringWithUTF8String:callbackScheme.toRawUTF8()];
  if (url == nil || scheme.length == 0)
    return false;

  presentationContext = [PlayWorshipAuthPresentationContext new];
  activeSession = [[ASWebAuthenticationSession alloc]
      initWithURL:url
      callbackURLScheme:scheme
      completionHandler:^(NSURL *callbackURL, NSError *error) {
        const auto callback = callbackURL != nil
                                  ? juce::String(callbackURL.absoluteString.UTF8String)
                                  : juce::String();
        const auto message = error != nil
                                 ? juce::String(error.localizedDescription.UTF8String)
                                 : juce::String();
        releaseSession();
        juce::MessageManager::callAsync(
            [completion = std::move(completion), callback, message]() mutable {
              if (completion)
                completion(callback, message);
            });
      }];
  activeSession.presentationContextProvider = presentationContext;
  activeSession.prefersEphemeralWebBrowserSession = NO;
  if (![activeSession start]) {
    releaseSession();
    return false;
  }

  return true;
}

void cancel() {
  if (activeSession != nil)
    [activeSession cancel];
  releaseSession();
}

} // namespace NativeAuthSession
