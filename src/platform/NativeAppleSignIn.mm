#include "NativeAppleSignIn.h"
#import <AuthenticationServices/AuthenticationServices.h>
#if JUCE_IOS
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

namespace {
void completeAttempt(uint64_t, NativeAppleSignIn::Result);
ASPresentationAnchor anchorForController(ASAuthorizationController*);

juce::String stringFromNative(NSString* value) {
  return value != nil ? juce::String::fromUTF8(value.UTF8String) : juce::String{};
}

juce::String proofFromNative(NSData* value) {
  if (value == nil || value.length == 0 || value.length > 16384)
    return {};
  return juce::String::fromUTF8(static_cast<const char*>(value.bytes),
                                static_cast<int>(value.length));
}
} // namespace

@interface PlayWorshipAppleSignInDelegate
    : NSObject <ASAuthorizationControllerDelegate,
                ASAuthorizationControllerPresentationContextProviding>
@property(nonatomic, assign) uint64_t attemptId;
@end

@implementation PlayWorshipAppleSignInDelegate
- (ASPresentationAnchor)presentationAnchorForAuthorizationController:
    (ASAuthorizationController*)controller {
  return anchorForController(controller);
}

- (void)authorizationController:(ASAuthorizationController*)controller
    didCompleteWithAuthorization:(ASAuthorization*)authorization {
  juce::ignoreUnused(controller);
  NativeAppleSignIn::Result result;
  if ([authorization.credential isKindOfClass:ASAuthorizationAppleIDCredential.class]) {
    auto* credential = (ASAuthorizationAppleIDCredential*)authorization.credential;
    result.outcome = NativeAppleSignIn::Outcome::succeeded;
    result.error = NativeAppleSignIn::Error::none;
    result.credential.state = stringFromNative(credential.state);
    result.credential.identityToken = proofFromNative(credential.identityToken);
    result.credential.authorizationCode = proofFromNative(credential.authorizationCode);
    result.credential.appleUser = stringFromNative(credential.user);
    result.credential.email = stringFromNative(credential.email);
    if (credential.fullName != nil)
      result.credential.fullName = stringFromNative(
          [NSPersonNameComponentsFormatter localizedStringFromPersonNameComponents:credential.fullName
              style:NSPersonNameComponentsFormatterStyleDefault options:0]).substring(0, 200);
  } else {
    result.error = NativeAppleSignIn::Error::invalidCredential;
  }
  completeAttempt(self.attemptId, std::move(result));
}

- (void)authorizationController:(ASAuthorizationController*)controller
          didCompleteWithError:(NSError*)error {
  juce::ignoreUnused(controller);
  NativeAppleSignIn::Result result;
  if ([error.domain isEqualToString:ASAuthorizationErrorDomain] &&
      error.code == ASAuthorizationErrorCanceled) {
    result.outcome = NativeAppleSignIn::Outcome::cancelled;
    result.error = NativeAppleSignIn::Error::none;
  }
  // NSError descriptions and response data never enter logs or UI text.
  completeAttempt(self.attemptId, std::move(result));
}
@end

@interface PlayWorshipAppleButtonTarget : NSObject {
@public
  std::function<void()> onClick;
}
- (void)clicked:(id)sender;
@end

@implementation PlayWorshipAppleButtonTarget
- (void)clicked:(id)sender {
  juce::ignoreUnused(sender);
  const auto callback = onClick;
  if (callback)
    callback();
}
@end

namespace {
struct Attempt {
  uint64_t id = 0;
  NativeAppleSignIn::Request request;
  NativeAppleSignIn::Completion completion;
  ASAuthorizationController* controller = nil;
  PlayWorshipAppleSignInDelegate* delegate = nil;
  ASPresentationAnchor anchor = nil;

  ~Attempt() {
    controller.delegate = nil;
    controller.presentationContextProvider = nil;
#if !__has_feature(objc_arc)
    [controller release];
    [delegate release];
    [anchor release];
#endif
  }
};

std::unique_ptr<Attempt> activeAttempt;
uint64_t nextAttemptId = 0;

ASPresentationAnchor anchorForController(ASAuthorizationController* controller) {
  return activeAttempt != nullptr && activeAttempt->controller == controller
             ? activeAttempt->anchor : nil;
}

void completeAttempt(uint64_t id, NativeAppleSignIn::Result result) {
  if (![NSThread isMainThread]) {
    juce::MessageManager::callAsync([id, result = std::move(result)]() mutable {
      completeAttempt(id, std::move(result));
    });
    return;
  }
  if (activeAttempt == nullptr || activeAttempt->id != id)
    return;
  auto completed = std::move(activeAttempt);
  if (result.outcome == NativeAppleSignIn::Outcome::succeeded) {
    const auto error = NativeAppleSignIn::validateCredential(
        completed->request, result.credential, juce::Time::currentTimeMillis());
    if (error != NativeAppleSignIn::Error::none) {
      result.outcome = NativeAppleSignIn::Outcome::failed;
      result.error = error;
      result.credential = {};
    }
  }
  auto callback = std::move(completed->completion);
  // Release native delegates after returning from the OS callback.
  juce::MessageManager::callAsync(
      [completed = std::shared_ptr<Attempt>(std::move(completed)),
       callback = std::move(callback), result = std::move(result)]() mutable {
        completed.reset();
        if (callback)
          callback(std::move(result));
      });
}

ASPresentationAnchor windowForComponent(juce::Component& component) {
  const auto* peer = component.getPeer();
  if (peer == nullptr)
    return nil;
#if JUCE_IOS
  auto* view = (__bridge UIView*)peer->getNativeHandle();
  auto* window = view.window;
  return window.windowScene.activationState == UISceneActivationStateForegroundActive
             ? window : nil;
#else
  auto* view = (__bridge NSView*)peer->getNativeHandle();
  return view.window.isVisible ? view.window : nil;
#endif
}

#if JUCE_IOS
using NativeButtonComponent = juce::UIViewComponent;
#else
using NativeButtonComponent = juce::NSViewComponent;
#endif

class AppleButton final : public NativeButtonComponent {
public:
  AppleButton(std::function<void()> callback, bool darkBackground) {
    target = [PlayWorshipAppleButtonTarget new];
    target->onClick = std::move(callback);
    button = [[ASAuthorizationAppleIDButton alloc]
        initWithAuthorizationButtonType:ASAuthorizationAppleIDButtonTypeContinue
        authorizationButtonStyle:darkBackground ? ASAuthorizationAppleIDButtonStyleWhite
                                                : ASAuthorizationAppleIDButtonStyleBlack];
    button.cornerRadius = 8.0;
#if JUCE_IOS
    [button addTarget:target action:@selector(clicked:) forControlEvents:UIControlEventTouchUpInside];
#else
    button.target = target;
    button.action = @selector(clicked:);
#endif
    setView((__bridge void*)button);
    setSize(240, 44);
  }

  ~AppleButton() override {
    target->onClick = {};
#if JUCE_IOS
    [button removeTarget:target action:@selector(clicked:) forControlEvents:UIControlEventTouchUpInside];
#else
    button.target = nil;
    button.action = nullptr;
#endif
    setView(nullptr);
#if !__has_feature(objc_arc)
    [button release];
    [target release];
#endif
  }

  void enablementChanged() override { button.enabled = isEnabled(); }

private:
  ASAuthorizationAppleIDButton* button = nil;
  PlayWorshipAppleButtonTarget* target = nil;
};
} // namespace

namespace NativeAppleSignIn {
bool isSupported() {
  if (@available(iOS 13.0, macOS 10.15, *))
    return true;
  return false;
}

std::unique_ptr<juce::Component> createButton(std::function<void()> onClick,
                                            bool darkBackground) {
  if (![NSThread isMainThread] || !isSupported() || !onClick)
    return nullptr;
  return std::make_unique<AppleButton>(std::move(onClick), darkBackground);
}

bool start(const Request& request, juce::Component& anchor, Completion completion) {
  if (![NSThread isMainThread] || !isSupported() || activeAttempt != nullptr ||
      !completion || validateRequest(request, juce::Time::currentTimeMillis()) != Error::none)
    return false;
  auto* window = windowForComponent(anchor);
  if (window == nil)
    return false;

  auto attempt = std::make_unique<Attempt>();
  attempt->id = ++nextAttemptId;
  attempt->request = request;
  attempt->completion = std::move(completion);
  attempt->anchor = window;
#if !__has_feature(objc_arc)
  [window retain];
#endif
  auto* provider = [[ASAuthorizationAppleIDProvider alloc] init];
  auto* nativeRequest = [provider createRequest];
  nativeRequest.state = [NSString stringWithUTF8String:request.state.toRawUTF8()];
  nativeRequest.nonce = [NSString stringWithUTF8String:request.nonceHash.toRawUTF8()];
  nativeRequest.requestedScopes = request.requestFullName
      ? @[ASAuthorizationScopeEmail, ASAuthorizationScopeFullName]
      : @[ASAuthorizationScopeEmail];
  attempt->delegate = [PlayWorshipAppleSignInDelegate new];
  attempt->delegate.attemptId = attempt->id;
  attempt->controller = [[ASAuthorizationController alloc]
      initWithAuthorizationRequests:@[nativeRequest]];
  attempt->controller.delegate = attempt->delegate;
  attempt->controller.presentationContextProvider = attempt->delegate;
#if !__has_feature(objc_arc)
  [provider release];
#endif
  const auto id = attempt->id;
  activeAttempt = std::move(attempt);
  @try {
    [activeAttempt->controller performRequests];
  } @catch (NSException*) {
    completeAttempt(id, {});
  }
  return true;
}

void cancel() {
  jassert([NSThread isMainThread]);
  if (![NSThread isMainThread] || activeAttempt == nullptr)
    return;
  const auto id = activeAttempt->id;
  activeAttempt->controller.delegate = nil;
  activeAttempt->controller.presentationContextProvider = nil;
  if (@available(iOS 16.0, macOS 13.0, *))
    [activeAttempt->controller cancel];
  Result result;
  result.outcome = Outcome::cancelled;
  result.error = Error::none;
  completeAttempt(id, std::move(result));
}
} // namespace NativeAppleSignIn
