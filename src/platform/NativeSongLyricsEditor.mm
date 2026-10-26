#include "NativeSongLyricsEditor.h"
#include "../data/SongLyrics.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <climits>
#include <atomic>
#import <UIKit/UIKit.h>
#if !__has_feature(objc_arc)
#error NativeSongLyricsEditor.mm requires Objective-C ARC
#endif
#include <memory>
#include <array>
#include <algorithm>
#include <unordered_map>
#include <cmath>

namespace {
struct LyricsPresentation;
using SectionSnapshot = std::shared_ptr<const std::vector<SongLyricsSection>>;
struct NativeLyricsTextEdit {
  NSRange range;
  juce::String removed, inserted;
  SectionSnapshot before, after;
};
bool sameSections(const std::vector<SongLyricsSection> &a, const std::vector<SongLyricsSection> &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].id != b[i].id || a[i].kind != b[i].kind || a[i].start != b[i].start || a[i].length != b[i].length) return false;
  return true;
}
NSString *nativeString(const juce::String &text) {
  return [NSString stringWithUTF8String:text.toRawUTF8()] ?: @"";
}
juce::String sharedString(NSString *text) {
  return text != nil ? juce::String::fromUTF8(text.UTF8String) : juce::String{};
}
UIColor *nativeColor(const juce::var &value, UIColor *fallback) {
  const auto token = value.toString().trim().trimCharactersAtStart("#");
  if (token.length() != 6 && token.length() != 8) return fallback;
  const auto number = static_cast<uint32_t>(token.getHexValue64());
  return [UIColor colorWithRed:((number >> 16) & 255) / 255.0
                         green:((number >> 8) & 255) / 255.0
                          blue:(number & 255) / 255.0
                         alpha:token.length() == 8 ? ((number >> 24) & 255) / 255.0 : 1.0];
}
UIViewController *presenterForOwner(juce::Component &owner) {
  auto *top = owner.getTopLevelComponent();
  auto *peer = top != nullptr ? top->getPeer() : owner.getPeer();
  UIView *view = peer != nullptr ? (__bridge UIView *)peer->getNativeHandle() : nil;
  UIViewController *controller = view.window.rootViewController;
  while (controller.presentedViewController != nil && !controller.presentedViewController.isBeingDismissed)
    controller = controller.presentedViewController;
  return controller;
}
}

// These are presentation records only. The document remains plain text plus
// UTF-16 ranges; decorative headings never enter the text or its undo stack.
@interface PlayWorshipLyricsSectionPresentation : NSObject
@property(nonatomic, copy) NSString *identifier;
@property(nonatomic, copy) NSString *label;
@property(nonatomic, copy) NSString *code;
@property(nonatomic, strong) UIColor *color;
@property(nonatomic) NSRange range;
@property(nonatomic) NSRange paragraphRange;
@property(nonatomic) BOOL completeBlock;
@end
@implementation PlayWorshipLyricsSectionPresentation
@end

namespace {
NSRange enclosingLyricsParagraphs(NSString *text, NSRange selection, bool trimFinalSeparator) {
  if (selection.length == 0 || selection.location > text.length ||
      selection.length > text.length - selection.location) return NSMakeRange(NSNotFound, 0);
  NSUInteger firstStart = 0, lastEnd = 0, lastContentEnd = 0;
  [text getParagraphStart:&firstStart end:nullptr contentsEnd:nullptr
                forRange:NSMakeRange(selection.location, 1)];
  // The end is exclusive. A selection ending at the next paragraph's first
  // character therefore does not accidentally label that next paragraph.
  [text getParagraphStart:nullptr end:&lastEnd contentsEnd:&lastContentEnd
                forRange:NSMakeRange(NSMaxRange(selection) - 1, 1)];
  const NSUInteger end = trimFinalSeparator ? lastContentEnd : lastEnd;
  return end > firstStart ? NSMakeRange(firstStart, end - firstStart) : NSMakeRange(NSNotFound, 0);
}
bool isCompleteLyricsParagraphs(NSString *text, NSRange range) {
  const auto content = enclosingLyricsParagraphs(text, range, true);
  const auto includingSeparator = enclosingLyricsParagraphs(text, range, false);
  return content.location != NSNotFound && range.location == content.location &&
      (NSMaxRange(range) == NSMaxRange(content) || NSMaxRange(range) == NSMaxRange(includingSeparator));
}
// The portable model deliberately keeps insertion at exact fragment borders
// outside that fragment. Whole-paragraph cards instead behave like native
// editable blocks: continuing to type at either edge belongs to that card.
// Callers pass a transient candidate, committing it only after this succeeds.
bool replaceNativeLyricsText(SongLyricsDocument &candidate, int start, int length,
                             const juce::String &replacement) {
  juce::String ownerID;
  int ownerStart = 0;
  bool ownsStart = false;
  const int inserted = songLyricsUtf16Length(replacement);
  if (length == 0 && inserted > 0) {
    NSString *before = nativeString(candidate.text);
    for (const auto &section : candidate.sections) {
      if (section.start != start && section.start + section.length != start) continue;
      const NSRange range = NSMakeRange(static_cast<NSUInteger>(section.start), static_cast<NSUInteger>(section.length));
      if (!isCompleteLyricsParagraphs(before, range)) continue;
      // At a shared border the following card owns the insertion. Extending
      // both neighbours would overlap their UTF-16 ranges.
      if (section.start == start) {
        ownerID = section.id; ownerStart = section.start; ownsStart = true; break;
      }
      ownerID = section.id; ownerStart = section.start;
    }
  }
  if (!replaceSongLyricsText(candidate, start, length, replacement)) return false;
  if (ownerID.isNotEmpty()) {
    for (auto &section : candidate.sections) {
      if (section.id != ownerID) continue;
      if (ownsStart) section.start = ownerStart;
      section.length += inserted;
      break;
    }
  }
  return isValidSongLyricsDocument(candidate);
}
bool isNativeLyricsUtf16Boundary(NSString *text, NSUInteger offset) {
  if (offset == 0 || offset >= text.length) return true;
  const unichar previous = [text characterAtIndex:offset - 1];
  const unichar next = [text characterAtIndex:offset];
  return !(previous >= 0xd800 && previous <= 0xdbff && next >= 0xdc00 && next <= 0xdfff);
}
bool setNativeLyricsText(SongLyricsDocument &candidate, const juce::String &text) {
  if (candidate.text == text) return true;
  // Some native IME/autocorrection callbacks provide final text without a
  // usable exact replacement. Derive that delta, retaining the same block-edge
  // ownership rule without splitting an emoji's UTF-16 surrogate pair.
  NSString *before = nativeString(candidate.text), *after = nativeString(text);
  if (before.length > INT_MAX || after.length > INT_MAX) return false;
  NSUInteger prefix = 0;
  while (prefix < before.length && prefix < after.length &&
      [before characterAtIndex:prefix] == [after characterAtIndex:prefix]) ++prefix;
  while (prefix > 0 && (!isNativeLyricsUtf16Boundary(before, prefix) || !isNativeLyricsUtf16Boundary(after, prefix))) --prefix;
  NSUInteger suffix = 0;
  while (suffix < before.length - prefix && suffix < after.length - prefix &&
      [before characterAtIndex:before.length - suffix - 1] == [after characterAtIndex:after.length - suffix - 1]) ++suffix;
  while (suffix > 0 && (!isNativeLyricsUtf16Boundary(before, before.length - suffix) ||
      !isNativeLyricsUtf16Boundary(after, after.length - suffix))) --suffix;
  NSString *replacement = [after substringWithRange:NSMakeRange(prefix, after.length - prefix - suffix)];
  return replaceNativeLyricsText(candidate, static_cast<int>(prefix), static_cast<int>(before.length - prefix - suffix), sharedString(replacement));
}
UIFont *lyricsScaledFont(CGFloat size, UIFontWeight weight, UIFontTextStyle style) {
  return [[UIFontMetrics metricsForTextStyle:style] scaledFontForFont:[UIFont systemFontOfSize:size weight:weight]];
}
CGFloat lyricsHeaderHeight() {
  return std::max<CGFloat>(32, std::ceil(lyricsScaledFont(14, UIFontWeightSemibold, UIFontTextStyleSubheadline).lineHeight + 14));
}
CGFloat lyricsNavigationCircleDiameter() {
  return std::max<CGFloat>(36, std::ceil(lyricsScaledFont(13, UIFontWeightSemibold, UIFontTextStyleSubheadline).lineHeight + 16));
}
bool hasBlankLyricsParagraphGap(NSString *text, NSRange previous, NSUInteger currentStart) {
  const NSUInteger previousEnd = NSMaxRange(previous);
  if (previousEnd >= currentStart || currentStart > text.length) return false;
  NSString *between = [text substringWithRange:NSMakeRange(previousEnd, currentStart - previousEnd)];
  return [between rangeOfCharacterFromSet:NSCharacterSet.whitespaceAndNewlineCharacterSet.invertedSet].location == NSNotFound &&
      [between rangeOfCharacterFromSet:NSCharacterSet.newlineCharacterSet].location != NSNotFound;
}
UIColor *lyricsLiftedColor(UIColor *base, CGFloat amount) {
  CGFloat red = 0, green = 0, blue = 0, alpha = 1;
  if (![base getRed:&red green:&green blue:&blue alpha:&alpha]) return UIColor.secondarySystemBackgroundColor;
  return [UIColor colorWithRed:red + (1 - red) * amount green:green + (1 - green) * amount
      blue:blue + (1 - blue) * amount alpha:alpha];
}
}

@interface PlayWorshipLyricsLayoutManager : NSLayoutManager
@property(nonatomic, strong) NSArray<PlayWorshipLyricsSectionPresentation *> *sections;
@property(nonatomic, strong) UIColor *cardBackground;
@property(nonatomic, strong) UIColor *cardBorder;
@property(nonatomic, strong) NSMutableDictionary<NSString *, NSValue *> *cardFrames;
@property(nonatomic) CGFloat lastContainerWidth;
- (void)invalidateCardFrames;
- (CGRect)cardRectForSection:(PlayWorshipLyricsSectionPresentation *)section;
@end
@implementation PlayWorshipLyricsLayoutManager
- (instancetype)init {
  if ((self = [super init])) { self.sections = @[]; self.cardFrames = [NSMutableDictionary new]; }
  return self;
}
- (void)invalidateCardFrames { [self.cardFrames removeAllObjects]; }
- (CGRect)cardRectForSection:(PlayWorshipLyricsSectionPresentation *)section {
  NSTextContainer *container = self.textContainers.firstObject;
  if (container == nil || !section.completeBlock || section.range.length == 0) return CGRectNull;
  if (self.lastContainerWidth != container.size.width) {
    self.lastContainerWidth = container.size.width; [self invalidateCardFrames];
  }
  NSValue *cached = self.cardFrames[section.identifier];
  if (cached != nil) return cached.CGRectValue;
  NSRange characters = section.range;
  // Do not include a final paragraph separator's empty visual line in a card.
  NSString *text = self.textStorage.string;
  while (characters.length > 0) {
    const unichar last = [text characterAtIndex:NSMaxRange(characters) - 1];
    if (last != '\n' && last != '\r' && last != 0x2029) break;
    --characters.length;
  }
  if (characters.length == 0) return CGRectNull;
  const NSRange glyphs = [self glyphRangeForCharacterRange:characters actualCharacterRange:nullptr];
  [self ensureLayoutForGlyphRange:glyphs];
  __block CGRect used = CGRectNull;
  [self enumerateLineFragmentsForGlyphRange:glyphs usingBlock:^(CGRect line, CGRect ink, NSTextContainer *target, NSRange range, BOOL *stop) {
    (void)line; (void)target; (void)range; (void)stop;
    used = CGRectIsNull(used) ? ink : CGRectUnion(used, ink);
  }];
  if (CGRectIsNull(used)) return CGRectNull;
  const CGFloat top = lyricsHeaderHeight() * 0.5 + 8;
  const CGRect card = CGRectMake(0, CGRectGetMinY(used) - top, container.size.width,
                                CGRectGetHeight(used) + top + 18);
  self.cardFrames[section.identifier] = [NSValue valueWithCGRect:card];
  return card;
}
- (void)drawBackgroundForGlyphRange:(NSRange)glyphs atPoint:(CGPoint)origin {
  const NSRange visible = [self characterRangeForGlyphRange:glyphs actualGlyphRange:nullptr];
  for (PlayWorshipLyricsSectionPresentation *section in self.sections) {
    if (!section.completeBlock || NSIntersectionRange(section.range, visible).length == 0) continue;
    const CGRect bounds = [self cardRectForSection:section];
    if (CGRectIsNull(bounds)) continue;
    UIBezierPath *shape = [UIBezierPath bezierPathWithRoundedRect:CGRectOffset(bounds, origin.x, origin.y) cornerRadius:16];
    [self.cardBackground setFill]; [shape fill];
    [self.cardBorder setStroke]; shape.lineWidth = 1; [shape stroke];
  }
  // Selection, marked-text backgrounds and inline legacy labels remain native.
  [super drawBackgroundForGlyphRange:glyphs atPoint:origin];
}
@end

@interface PlayWorshipLyricsCardHeader : UIView
@property(nonatomic, strong) UILabel *circle;
@property(nonatomic, strong) UILabel *name;
- (void)configure:(PlayWorshipLyricsSectionPresentation *)section textColor:(UIColor *)textColor;
@end
@implementation PlayWorshipLyricsCardHeader
- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.userInteractionEnabled = NO;
    self.isAccessibilityElement = YES;
    self.accessibilityTraits = UIAccessibilityTraitHeader;
    self.circle = [UILabel new]; self.circle.textAlignment = NSTextAlignmentCenter;
    self.circle.clipsToBounds = YES;
    self.name = [UILabel new];
    self.circle.isAccessibilityElement = NO; self.name.isAccessibilityElement = NO;
    [self addSubview:self.circle]; [self addSubview:self.name];
  }
  return self;
}
- (void)configure:(PlayWorshipLyricsSectionPresentation *)section textColor:(UIColor *)textColor {
  self.accessibilityIdentifier = [@"nativeLyricsCardHeader:" stringByAppendingString:section.identifier];
  self.accessibilityLabel = section.label;
  self.circle.text = section.code; self.name.text = section.label;
  self.circle.font = lyricsScaledFont(12, UIFontWeightSemibold, UIFontTextStyleCaption1);
  self.name.font = lyricsScaledFont(14, UIFontWeightSemibold, UIFontTextStyleSubheadline);
  self.circle.adjustsFontSizeToFitWidth = YES;
  self.circle.minimumScaleFactor = 0.65;
  self.circle.textColor = UIColor.whiteColor; self.name.textColor = textColor;
  self.circle.backgroundColor = [UIColor colorWithWhite:0.12 alpha:1];
  self.backgroundColor = section.color;
  self.circle.layer.borderColor = [textColor colorWithAlphaComponent:0.25].CGColor;
  self.circle.layer.borderWidth = 1;
  self.layer.cornerRadius = lyricsHeaderHeight() * 0.5;
}
- (CGSize)sizeThatFits:(CGSize)size {
  const CGFloat height = lyricsHeaderHeight();
  return CGSizeMake(std::min<CGFloat>(size.width, height + [self.name sizeThatFits:size].width + 23), height);
}
- (void)layoutSubviews {
  [super layoutSubviews];
  const CGFloat diameter = CGRectGetHeight(self.bounds) - 8;
  self.circle.frame = CGRectMake(4, 4, diameter, diameter);
  self.circle.layer.cornerRadius = diameter * 0.5;
  self.name.frame = CGRectMake(CGRectGetHeight(self.bounds) + 4, 0,
      std::max<CGFloat>(0, CGRectGetWidth(self.bounds) - CGRectGetHeight(self.bounds) - 17), CGRectGetHeight(self.bounds));
}
@end

// Keep the visible navigation circle compact without shrinking its native
// touch/accessibility target. Selection decoration belongs to the circle only.
@interface PlayWorshipLyricsNavigationButton : UIButton
@property(nonatomic, strong) UIView *circleView;
@property(nonatomic, strong) UIColor *sectionColor;
- (void)refreshCircle;
@end
@implementation PlayWorshipLyricsNavigationButton
@synthesize sectionColor = _sectionColor;
- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.backgroundColor = UIColor.clearColor;
    self.circleView = [UIView new];
    self.circleView.userInteractionEnabled = NO;
    self.circleView.isAccessibilityElement = NO;
    self.circleView.clipsToBounds = YES;
    [self insertSubview:self.circleView atIndex:0];
    self.titleLabel.textAlignment = NSTextAlignmentCenter;
    self.titleLabel.adjustsFontSizeToFitWidth = YES;
    self.titleLabel.minimumScaleFactor = 0.65;
  }
  return self;
}
- (void)setSectionColor:(UIColor *)color {
  _sectionColor = color;
  [self refreshCircle];
}
- (void)setSelected:(BOOL)selected {
  [super setSelected:selected];
  [self refreshCircle];
}
- (void)refreshCircle {
  UIColor *color = self.sectionColor ?: UIColor.clearColor;
  self.circleView.layer.borderWidth = self.selected ? 2.5 : 1.5;
  self.circleView.layer.borderColor = color.CGColor;
  self.circleView.backgroundColor = self.selected ? [color colorWithAlphaComponent:0.14] : UIColor.clearColor;
}
- (void)layoutSubviews {
  [super layoutSubviews];
  const CGFloat diameter = lyricsNavigationCircleDiameter();
  self.circleView.frame = CGRectMake((CGRectGetWidth(self.bounds) - diameter) * 0.5,
      (CGRectGetHeight(self.bounds) - diameter) * 0.5, diameter, diameter);
  self.circleView.layer.cornerRadius = diameter * 0.5;
}
- (CGRect)titleRectForContentRect:(CGRect)contentRect {
  const CGFloat diameter = lyricsNavigationCircleDiameter();
  return CGRectMake(CGRectGetMidX(contentRect) - diameter * 0.5 + 2,
      CGRectGetMidY(contentRect) - diameter * 0.5, diameter - 4, diameter);
}
@end

@class PlayWorshipNativeLyricsController;
@interface PlayWorshipNativeLyricsTextView : UITextView
@property(nonatomic, weak) PlayWorshipNativeLyricsController *lyricsController;
- (void)markLyricsSection:(id)sender;
@end
@interface PlayWorshipNativeLyricsNavigation : UINavigationController
@end
@interface PlayWorshipNativeLyricsController : UIViewController <UITextViewDelegate> {
@public
  std::weak_ptr<LyricsPresentation> presentation;
  SongLyricsDocument document;
  juce::var configuration;
@private
  juce::String initialDocument;
  bool updatingAppearance;
  bool appearanceDirty;
  bool busy;
  bool hasPendingReplacement;
  NSRange pendingRange;
  juce::String pendingReplacement;
  juce::String chipIdentity;
  uint64_t documentRevision;
  std::vector<NativeLyricsTextEdit> textEdits;
  size_t textEditCursor;
  SectionSnapshot sectionSnapshot;
  int historyAction; // -1 native undo, +1 native redo. Never registers text actions.
  juce::String activeSectionID;
  juce::String scrollTargetID;
  bool refreshingCards;

}
@property(nonatomic, strong) PlayWorshipNativeLyricsTextView *editor;
@property(nonatomic, strong) UILabel *placeholder;
@property(nonatomic, strong) UILabel *songLabel;
@property(nonatomic, strong) UILabel *metadataLabel;
@property(nonatomic, strong) UIView *headerOverlay;
@property(nonatomic, strong) NSMutableDictionary<NSString *, PlayWorshipLyricsCardHeader *> *cardHeaders;
@property(nonatomic, strong) NSArray<PlayWorshipLyricsSectionPresentation *> *sectionPresentations;
@property(nonatomic, strong) PlayWorshipLyricsLayoutManager *lyricsLayout;
@property(nonatomic, strong) UIScrollView *sectionScroll;
@property(nonatomic, strong) UIStackView *sectionStack;
@property(nonatomic, strong) NSLayoutConstraint *sectionHeight;
@property(nonatomic, strong) NSLayoutConstraint *editorBottom;
@property(nonatomic, strong) UIBarButtonItem *closeItem;
@property(nonatomic, strong) UIBarButtonItem *saveItem;
@property(nonatomic, strong) NSArray<UIMenuItem *> *previousLegacyMenu;
- (void)configure:(NativeSongLyricsEditor::Options)options;
- (void)closePressed;
- (void)savePressed;
- (void)showSectionChooser;
- (void)applySection:(juce::String)kind range:(NSRange)range revision:(uint64_t)revision;
- (void)saveFinished:(bool)success error:(juce::String)message;
- (void)updateAppearance;
- (void)refreshCardDecorations;
- (void)updateActiveSectionForScroll;
- (void)setActiveSection:(const juce::String &)identifier animated:(BOOL)animated;
- (void)rebuildSectionPresentations;
- (UIMenu *)menuForRange:(NSRange)range suggested:(NSArray<UIMenuElement *> *)actions API_AVAILABLE(ios(16.0));
@end

namespace {
struct LyricsPresentation {
  explicit LyricsPresentation(juce::Component &component) : owner(&component) {}
  juce::Component::SafePointer<juce::Component> owner;
  NativeSongLyricsEditor::Save save;
  std::function<void()> close;
  __weak PlayWorshipNativeLyricsController *controller = nil;
  __weak PlayWorshipNativeLyricsNavigation *navigation = nil;
  bool finished = false;
  bool cancelled = false;
  int previousOrientations = 0;
  bool changedOrientation = false;
};
void restoreOrientation(const std::shared_ptr<LyricsPresentation> &state) {
  if (state->changedOrientation) {
    state->changedOrientation = false;
    juce::Desktop::getInstance().setOrientationsEnabled(state->previousOrientations);
  }
}
void finishPresentation(std::shared_ptr<LyricsPresentation> state, bool notify) {
  if (state == nullptr) return;
  if (!notify) state->cancelled = true;
  if (state->finished) return;
  state->finished = true;
  auto callback = notify ? std::move(state->close) : std::function<void()>{};
  state->save = {}; state->close = {};
  auto *controller = state->controller;
  controller.editor.delegate = nil;
  [controller.editor resignFirstResponder];
  if (controller.previousLegacyMenu != nil)
    UIMenuController.sharedMenuController.menuItems = controller.previousLegacyMenu;
  auto complete = ^{
    restoreOrientation(state);
    if (callback)
      juce::MessageManager::callAsync([state, callback] {
        if (!state->cancelled && state->owner != nullptr) callback();
      });
  };
  auto *navigation = state->navigation;
  auto dismiss = ^{
    if (navigation.presentingViewController != nil)
      [navigation.presentingViewController dismissViewControllerAnimated:notify completion:complete];
    else complete();
  };
  // Owner cancellation may arrive while UIKit is still presenting the editor
  // or its discard alert. Wait for that transition instead of dropping the
  // dismissal request during an active presentation.
  UIViewController *transitioning = controller.presentedViewController;
  if (transitioning == nil || !transitioning.isBeingPresented) transitioning = navigation;
  id<UIViewControllerTransitionCoordinator> coordinator = transitioning.transitionCoordinator;
  if (transitioning.isBeingPresented && coordinator != nil) {
    const BOOL scheduled = [coordinator animateAlongsideTransition:nil
        completion:^(id<UIViewControllerTransitionCoordinatorContext>) { dismiss(); }];
    if (!scheduled) dismiss();
  } else dismiss();
}
class UIKitLyricsHandle final : public NativeSongLyricsEditor::Handle {
public:
  UIKitLyricsHandle(std::shared_ptr<LyricsPresentation> next,
                    PlayWorshipNativeLyricsNavigation *controller)
      : state(std::move(next)), navigation(controller) {}
  ~UIKitLyricsHandle() override { cancel(); }
  void cancel() override {
    auto closing = std::move(state);
    if (closing == nullptr) return;
    if (NSThread.isMainThread) finishPresentation(closing, false);
    else dispatch_async(dispatch_get_main_queue(), ^{ finishPresentation(closing, false); });
    navigation = nil;
  }
private:
  std::shared_ptr<LyricsPresentation> state;
  __strong PlayWorshipNativeLyricsNavigation *navigation;
};
}

@implementation PlayWorshipNativeLyricsTextView
- (BOOL)canPerformAction:(SEL)action withSender:(id)sender {
  if (action == @selector(markLyricsSection:)) return self.selectedRange.length > 0 && self.editable;
  return [super canPerformAction:action withSender:sender];
}
- (void)markLyricsSection:(id)sender { (void)sender; [self.lyricsController showSectionChooser]; }
- (void)layoutSubviews {
  [super layoutSubviews];
  [self.lyricsController refreshCardDecorations];
}
@end
@implementation PlayWorshipNativeLyricsNavigation
- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
  return UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad
      ? UIInterfaceOrientationMaskAll : UIInterfaceOrientationMaskPortrait;
}
- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation { return UIInterfaceOrientationPortrait; }
- (BOOL)shouldAutorotate { return YES; }
- (UIViewController *)childViewControllerForStatusBarStyle { return self.topViewController; }
@end

@implementation PlayWorshipNativeLyricsController
- (UIStatusBarStyle)preferredStatusBarStyle {
  const auto colors = configuration.getProperty("colors", {});
  CGFloat red = 0, green = 0, blue = 0, alpha = 0;
  [nativeColor(colors.getProperty("background", {}), UIColor.systemBackgroundColor) getRed:&red green:&green blue:&blue alpha:&alpha];
  return red * 0.299 + green * 0.587 + blue * 0.114 < 0.5 ? UIStatusBarStyleLightContent : UIStatusBarStyleDarkContent;
}
- (NSString *)label:(const char *)key {
  return nativeString(configuration.getProperty(key, {} ).toString());
}
- (void)configure:(NativeSongLyricsEditor::Options)options {
  document = options.document.value_or(SongLyricsDocument{});
  configuration = NativeSongLyricsEditor::presentationConfig(options);
  initialDocument = juce::JSON::toString(songLyricsToVar(document), true);
  appearanceDirty = true;
  sectionSnapshot = std::make_shared<const std::vector<SongLyricsSection>>(document.sections);
}
- (void)viewDidLoad {
  [super viewDidLoad];
  auto colors = configuration.getProperty("colors", {});
  self.view.backgroundColor = nativeColor(colors.getProperty("pageBackground", colors.getProperty("background", {})), UIColor.systemBackgroundColor);
  self.view.tintColor = nativeColor(colors.getProperty("accent", {}), UIColor.systemGreenColor);
  self.navigationController.navigationBar.tintColor = self.view.tintColor;
  UINavigationBarAppearance *bar = [UINavigationBarAppearance new];
  [bar configureWithOpaqueBackground];
  bar.backgroundColor = self.view.backgroundColor;
  bar.titleTextAttributes = @{NSForegroundColorAttributeName:nativeColor(colors.getProperty("text", {}), UIColor.labelColor)};
  self.navigationController.navigationBar.standardAppearance = bar;
  self.navigationController.navigationBar.scrollEdgeAppearance = bar;
  self.navigationController.navigationBar.compactAppearance = bar;
  self.title = [self label:"heading"];
  self.closeItem = [[UIBarButtonItem alloc] initWithTitle:[self label:"closeTitle"]
      style:UIBarButtonItemStylePlain target:self action:@selector(closePressed)];
  self.saveItem = [[UIBarButtonItem alloc] initWithTitle:[self label:"saveTitle"]
      style:UIBarButtonItemStyleDone target:self action:@selector(savePressed)];
  self.closeItem.tintColor = self.view.tintColor;
  self.saveItem.tintColor = self.view.tintColor;
  self.closeItem.accessibilityIdentifier = @"nativeLyricsClose";
  self.saveItem.accessibilityIdentifier = @"nativeLyricsSave";
  self.navigationItem.leftBarButtonItem = self.closeItem;
  self.navigationItem.rightBarButtonItem = self.saveItem;

  self.songLabel = [UILabel new];
  self.songLabel.text = [self label:"title"];
  UIFont *titleFont = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle1];
  self.songLabel.font = [UIFont fontWithDescriptor:[titleFont.fontDescriptor fontDescriptorWithSymbolicTraits:UIFontDescriptorTraitBold] size:0];
  self.songLabel.numberOfLines = 2;
  self.songLabel.adjustsFontForContentSizeCategory = YES;
  self.songLabel.textColor = nativeColor(colors.getProperty("text", {}), UIColor.labelColor);
  self.songLabel.translatesAutoresizingMaskIntoConstraints = NO;
  self.songLabel.accessibilityIdentifier = @"nativeLyricsSongTitle";
  [self.view addSubview:self.songLabel];
  self.metadataLabel = [UILabel new];
  self.metadataLabel.translatesAutoresizingMaskIntoConstraints = NO;
  self.metadataLabel.font = lyricsScaledFont(14, UIFontWeightRegular, UIFontTextStyleSubheadline);
  self.metadataLabel.adjustsFontForContentSizeCategory = YES;
  self.metadataLabel.textColor = nativeColor(colors.getProperty("secondary", {}), UIColor.secondaryLabelColor);
  self.metadataLabel.text = [self label:"subtitle"];
  if (self.metadataLabel.text.length == 0) {
    NSString *artist = [self label:"artist"], *tempo = [self label:"tempoText"];
    self.metadataLabel.text = artist.length > 0 && tempo.length > 0
        ? [NSString stringWithFormat:@"%@ · %@", artist, tempo] : artist.length > 0 ? artist : tempo;
  }
  self.metadataLabel.accessibilityIdentifier = @"nativeLyricsMetadata";
  [self.view addSubview:self.metadataLabel];

  self.sectionScroll = [UIScrollView new];
  self.sectionScroll.translatesAutoresizingMaskIntoConstraints = NO;
  self.sectionScroll.showsHorizontalScrollIndicator = NO;
  self.sectionScroll.accessibilityIdentifier = @"nativeLyricsSections";
  self.sectionStack = [UIStackView new];
  self.sectionStack.axis = UILayoutConstraintAxisHorizontal;
  self.sectionStack.spacing = 4;
  self.sectionStack.alignment = UIStackViewAlignmentCenter;
  self.sectionStack.translatesAutoresizingMaskIntoConstraints = NO;
  [self.sectionScroll addSubview:self.sectionStack];
  [self.view addSubview:self.sectionScroll];

  // An explicit TextKit 1 stack supports the same native editing services on
  // every supported iOS version, with cards drawn behind its own glyphs.
  NSTextStorage *storage = [NSTextStorage new];
  self.lyricsLayout = [PlayWorshipLyricsLayoutManager new];
  [storage addLayoutManager:self.lyricsLayout];
  NSTextContainer *container = [[NSTextContainer alloc] initWithSize:CGSizeMake(0, CGFLOAT_MAX)];
  container.widthTracksTextView = YES;
  container.lineFragmentPadding = 0;
  [self.lyricsLayout addTextContainer:container];
  self.editor = [[PlayWorshipNativeLyricsTextView alloc] initWithFrame:CGRectZero textContainer:container];
  self.editor.lyricsController = self;
  self.editor.translatesAutoresizingMaskIntoConstraints = NO;
  self.editor.backgroundColor = self.view.backgroundColor;
  self.editor.textColor = nativeColor(colors.getProperty("text", {}), UIColor.labelColor);
  self.editor.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
  self.editor.adjustsFontForContentSizeCategory = YES;
  self.editor.textContainerInset = UIEdgeInsetsMake(24, 12, 32, 12);
  self.editor.alwaysBounceVertical = YES;
  self.editor.keyboardDismissMode = UIScrollViewKeyboardDismissModeInteractive;
  self.editor.contentInsetAdjustmentBehavior = UIScrollViewContentInsetAdjustmentNever;
  self.editor.accessibilityIdentifier = @"nativeLyricsText";
  self.editor.accessibilityLabel = [self label:"heading"];
  self.editor.text = nativeString(document.text);
  self.editor.delegate = self;
  [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(nativeUndoWill:)
      name:NSUndoManagerWillUndoChangeNotification object:nil];
  [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(nativeRedoWill:)
      name:NSUndoManagerWillRedoChangeNotification object:nil];
  [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(nativeHistoryDid:)
      name:NSUndoManagerDidUndoChangeNotification object:nil];
  [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(nativeHistoryDid:)
      name:NSUndoManagerDidRedoChangeNotification object:nil];
  [self.view addSubview:self.editor];
  self.headerOverlay = [UIView new];
  self.headerOverlay.userInteractionEnabled = NO;
  self.headerOverlay.clipsToBounds = YES;
  self.headerOverlay.isAccessibilityElement = NO;
  self.headerOverlay.accessibilityIdentifier = @"nativeLyricsCardHeaders";
  self.cardHeaders = [NSMutableDictionary new];
  [self.view addSubview:self.headerOverlay];
  [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(contentSizeChanged:)
      name:UIContentSizeCategoryDidChangeNotification object:nil];

  self.placeholder = [UILabel new];
  self.placeholder.translatesAutoresizingMaskIntoConstraints = NO;
  self.placeholder.text = [self label:"placeholder"];
  self.placeholder.textColor = nativeColor(colors.getProperty("secondary", {}), UIColor.secondaryLabelColor);
  self.placeholder.font = self.editor.font;
  self.placeholder.adjustsFontForContentSizeCategory = YES;
  self.placeholder.numberOfLines = 0;
  self.placeholder.userInteractionEnabled = NO;
  self.placeholder.accessibilityIdentifier = @"nativeLyricsPlaceholder";
  [self.editor addSubview:self.placeholder];
  self.sectionHeight = [self.sectionScroll.heightAnchor constraintEqualToConstant:0];
  [NSLayoutConstraint activateConstraints:@[
      [self.songLabel.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:12],
      [self.songLabel.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:16],
      [self.songLabel.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-16],
      [self.metadataLabel.topAnchor constraintEqualToAnchor:self.songLabel.bottomAnchor constant:4],
      [self.metadataLabel.leadingAnchor constraintEqualToAnchor:self.songLabel.leadingAnchor],
      [self.metadataLabel.trailingAnchor constraintEqualToAnchor:self.songLabel.trailingAnchor],
      [self.sectionScroll.topAnchor constraintEqualToAnchor:self.metadataLabel.bottomAnchor constant:10],
      [self.sectionScroll.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor],
      [self.sectionScroll.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor], self.sectionHeight,
      [self.sectionStack.leadingAnchor constraintEqualToAnchor:self.sectionScroll.contentLayoutGuide.leadingAnchor constant:16],
      [self.sectionStack.trailingAnchor constraintEqualToAnchor:self.sectionScroll.contentLayoutGuide.trailingAnchor constant:-16],
      [self.sectionStack.topAnchor constraintEqualToAnchor:self.sectionScroll.contentLayoutGuide.topAnchor],
      [self.sectionStack.bottomAnchor constraintEqualToAnchor:self.sectionScroll.contentLayoutGuide.bottomAnchor],
      [self.sectionStack.heightAnchor constraintEqualToAnchor:self.sectionScroll.frameLayoutGuide.heightAnchor],
      [self.editor.topAnchor constraintEqualToAnchor:self.sectionScroll.bottomAnchor constant:4],
      [self.editor.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor],
      [self.editor.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor],
      [self.placeholder.leadingAnchor constraintEqualToAnchor:self.editor.frameLayoutGuide.leadingAnchor constant:21],
      [self.placeholder.trailingAnchor constraintEqualToAnchor:self.editor.frameLayoutGuide.trailingAnchor constant:-21],
      [self.placeholder.topAnchor constraintEqualToAnchor:self.editor.contentLayoutGuide.topAnchor constant:24]
  ]];
  if (@available(iOS 15.0, *))
    self.editorBottom = [self.editor.bottomAnchor constraintEqualToAnchor:self.view.keyboardLayoutGuide.topAnchor];
  else {
    self.editorBottom = [self.editor.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(keyboardChanged:)
        name:UIKeyboardWillChangeFrameNotification object:nil];
  }
  self.editorBottom.active = YES;
  if (@available(iOS 16.0, *)) {} else {
    self.previousLegacyMenu = UIMenuController.sharedMenuController.menuItems ?: @[];
    UIMenuController.sharedMenuController.menuItems = @[[[UIMenuItem alloc]
        initWithTitle:[self label:"sectionActionTitle"] action:@selector(markLyricsSection:)]];
  }
  [self updateAppearance];
}
- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];
  // initWithRootViewController may load our view before navigationController is
  // assigned. Apply the native bar's app theme once it is attached as well.
  self.navigationController.view.tintColor = self.view.tintColor;
  self.navigationController.navigationBar.tintColor = self.view.tintColor;
  UINavigationBarAppearance *bar = [UINavigationBarAppearance new];
  [bar configureWithOpaqueBackground];
  bar.backgroundColor = self.view.backgroundColor;
  bar.titleTextAttributes = @{NSForegroundColorAttributeName:self.editor.textColor};
  self.navigationController.navigationBar.standardAppearance = bar;
  self.navigationController.navigationBar.scrollEdgeAppearance = bar;
  self.navigationController.navigationBar.compactAppearance = bar;
}
- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  [self refreshCardDecorations];
}
- (void)contentSizeChanged:(NSNotification *)notification {
  (void)notification;
  appearanceDirty = true; [self updateAppearance];
}
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }
- (void)keyboardChanged:(NSNotification *)notification {
  const CGRect screen = [notification.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
  const CGRect frame = [self.view convertRect:screen fromView:nil];
  const CGFloat overlap = MAX(0, CGRectGetMaxY(self.view.bounds) - CGRectGetMinY(frame));
  self.editorBottom.constant = -MAX(0, overlap - self.view.safeAreaInsets.bottom);
  [UIView animateWithDuration:[notification.userInfo[UIKeyboardAnimationDurationUserInfoKey] doubleValue]
      delay:0 options:static_cast<UIViewAnimationOptions>([notification.userInfo[UIKeyboardAnimationCurveUserInfoKey] integerValue] << 16)
      animations:^{ [self.view layoutIfNeeded]; } completion:nil];
}
- (BOOL)textView:(UITextView *)textView shouldChangeTextInRange:(NSRange)range replacementText:(NSString *)text {
  if (busy) return NO;
  auto candidate = document;
  if (range.location > INT_MAX || range.length > INT_MAX ||
      !replaceNativeLyricsText(candidate, static_cast<int>(range.location), static_cast<int>(range.length), sharedString(text)))
    return NO;
  pendingRange = range; pendingReplacement = sharedString(text); hasPendingReplacement = true;
  return YES;
}
- (void)nativeUndoWill:(NSNotification *)notification {
  if (notification.object == self.editor.undoManager) historyAction = -1;
}
- (void)nativeRedoWill:(NSNotification *)notification {
  if (notification.object == self.editor.undoManager) historyAction = 1;
}
- (void)nativeHistoryDid:(NSNotification *)notification {
  if (notification.object == self.editor.undoManager) historyAction = 0;
}
- (SectionSnapshot)snapshotSections {
  if (!sectionSnapshot || !sameSections(*sectionSnapshot, document.sections))
    sectionSnapshot = std::make_shared<const std::vector<SongLyricsSection>>(document.sections);
  return sectionSnapshot;
}
- (BOOL)restoreHistoryForText:(const juce::String &)text direction:(int)direction {
  // Replay only the compact text deltas into a temporary NSString. UIKit owns
  // the actual text/undo stack. Matching its result restores the corresponding
  // metadata snapshot, including labels whose entire text had been deleted.
  NSString *candidate = nativeString(document.text);
  size_t cursor = textEditCursor;
  while ((direction < 0 && cursor > 0) || (direction > 0 && cursor < textEdits.size())) {
    const auto &entry = textEdits[direction < 0 ? cursor - 1 : cursor];
    NSString *removed = nativeString(direction < 0 ? entry.inserted : entry.removed);
    NSString *inserted = nativeString(direction < 0 ? entry.removed : entry.inserted);
    const NSRange range = NSMakeRange(entry.range.location, removed.length);
    if (NSMaxRange(range) > candidate.length || ![[candidate substringWithRange:range] isEqualToString:removed]) break;
    candidate = [candidate stringByReplacingCharactersInRange:range withString:inserted];
    if (direction < 0) --cursor; else ++cursor;
    if (sharedString(candidate) == text) {
      document.text = text;
      sectionSnapshot = direction < 0 ? entry.before : entry.after;
      document.sections = *sectionSnapshot;
      textEditCursor = cursor;
      return YES;
    }
  }
  return NO;
}
- (void)textViewDidChange:(UITextView *)textView {
  if (updatingAppearance) return;
  const auto current = sharedString(textView.text);
  if (current == document.text) { hasPendingReplacement = false; return; }
  const int direction = historyAction != 0 ? historyAction :
      textView.undoManager.isUndoing ? -1 : textView.undoManager.isRedoing ? 1 : 0;
  bool restored = direction != 0 && [self restoreHistoryForText:current direction:direction];
  if (!restored) {
    const auto previous = document.text;
    const auto before = [self snapshotSections];
    auto candidate = document;
    const bool exact = hasPendingReplacement && replaceNativeLyricsText(candidate, static_cast<int>(pendingRange.location),
        static_cast<int>(pendingRange.length), pendingReplacement) && candidate.text == current;
    if (exact) document = std::move(candidate);
    else setNativeLyricsText(document, current);
    const auto after = [self snapshotSections];
    if (direction < 0) {
      // A manual section change starts a new metadata history boundary while
      // leaving UIKit's text undo stack intact. Capture an older native Undo
      // in reverse so Redo can restore even an entirely deleted new section.
      textEdits.erase(textEdits.begin(), textEdits.begin() + static_cast<std::ptrdiff_t>(textEditCursor));
      textEdits.insert(textEdits.begin(), {NSMakeRange(0, nativeString(current).length), current, previous, after, before});
      textEditCursor = 0;
    } else {
      if (textEditCursor < textEdits.size()) textEdits.resize(textEditCursor);
      NSString *original = nativeString(previous);
      const NSRange range = exact ? pendingRange : NSMakeRange(0, original.length);
      textEdits.push_back({range, sharedString([original substringWithRange:range]),
                          exact ? pendingReplacement : current, before, after});
      textEditCursor = textEdits.size();
    }
  }
  hasPendingReplacement = false;
  ++documentRevision; appearanceDirty = true;
  [self updateAppearance];
}
- (void)textViewDidChangeSelection:(UITextView *)textView {
  if (appearanceDirty && textView.markedTextRange == nil && !updatingAppearance) [self updateAppearance];
}
- (juce::var)kindDefinition:(const juce::String &)kind {
  const auto kinds = configuration.getProperty("sectionKinds", {});
  if (const auto *array = kinds.getArray())
    for (const auto &entry : *array)
      if (entry.getProperty("id", {}).toString() == kind) return entry;
  return {};
}
- (void)rebuildSectionPresentations {
  NSMutableArray<PlayWorshipLyricsSectionPresentation *> *entries = [NSMutableArray new];
  NSString *text = nativeString(document.text);
  std::unordered_map<std::string, const SongLyricsSection *> byID;
  byID.reserve(document.sections.size());
  for (const auto &section : document.sections) byID.emplace(section.id.toStdString(), &section);
  // Recalculate occurrences from the current document, not the opening config.
  // Intro/Bridge/etc acquire numbering only when duplicated, just like the
  // shared configuration and portable tests specify.
  const auto labels = NativeSongLyricsEditor::sectionLabels(document);
  if (const auto *array = labels.getArray()) for (const auto &label : *array) {
    const auto identifier = label.getProperty("id", {}).toString();
    const auto found = byID.find(identifier.toStdString());
    if (found == byID.end()) continue;
    const auto &section = *found->second;
    auto *entry = [PlayWorshipLyricsSectionPresentation new];
    entry.identifier = nativeString(identifier);
    entry.label = nativeString(label.getProperty("label", {}).toString());
    entry.code = nativeString(label.getProperty("code", {}).toString());
    entry.color = nativeColor(label.getProperty("color", {}), self.view.tintColor);
    entry.range = NSMakeRange(static_cast<NSUInteger>(section.start), static_cast<NSUInteger>(section.length));
    entry.paragraphRange = enclosingLyricsParagraphs(text, entry.range, false);
    entry.completeBlock = isCompleteLyricsParagraphs(text, entry.range);
    [entries addObject:entry];
  }
  self.sectionPresentations = entries;
  self.lyricsLayout.sections = entries;
  [self.lyricsLayout invalidateCardFrames];
}
- (void)updateAppearance {
  self.placeholder.hidden = self.editor.text.length > 0;
  if (!appearanceDirty || self.editor.markedTextRange != nil) return;
  appearanceDirty = false; updatingAppearance = true;
  const NSRange selection = self.editor.selectedRange;
  const CGPoint offset = self.editor.contentOffset;
  auto colors = configuration.getProperty("colors", {});
  auto *textColor = nativeColor(colors.getProperty("text", {}), UIColor.labelColor);
  self.lyricsLayout.cardBackground = nativeColor(colors.getProperty("cardBackground", {}), lyricsLiftedColor(self.view.backgroundColor, 0.11));
  self.lyricsLayout.cardBorder = nativeColor(colors.getProperty("cardBorder", {}), [UIColor.whiteColor colorWithAlphaComponent:0.08]);
  auto *storage = self.editor.textStorage;
  const NSRange all = NSMakeRange(0, storage.length);
  [self rebuildSectionPresentations];
  // Only presentation attributes change. Never replace attributedText or add
  // heading characters: both would disturb native selection and text history.
  NSUndoManager *undo = self.editor.undoManager;
  const BOOL registration = undo.isUndoRegistrationEnabled;
  if (registration) [undo disableUndoRegistration];
  [storage beginEditing];
  [storage removeAttribute:NSBackgroundColorAttributeName range:all];
  [storage addAttribute:NSForegroundColorAttributeName value:textColor range:all];
  NSMutableParagraphStyle *normal = [NSMutableParagraphStyle new];
  normal.lineSpacing = 3;
  [storage addAttribute:NSParagraphStyleAttributeName value:normal range:all];
  const CGFloat headerHeight = lyricsHeaderHeight();
  const BOOL startsWithCard = self.sectionPresentations.count > 0 && self.sectionPresentations.firstObject.completeBlock && self.sectionPresentations.firstObject.range.location == 0;
  // TextKit can omit paragraphSpacingBefore at the document's first line.
  // Reserve its floating heading physically in the native text-container inset.
  UIEdgeInsets insets = self.editor.textContainerInset;
  insets.top = startsWithCard ? headerHeight + 16 : 24;
  self.editor.textContainerInset = insets;
  PlayWorshipLyricsSectionPresentation *previousCard = nil;
  for (PlayWorshipLyricsSectionPresentation *section in self.sectionPresentations) {
    if (!section.completeBlock) {
      // Old, truly partial ranges remain exact inline highlights. Do not turn
      // their surrounding unlabelled words into an implicitly labelled card.
      [storage addAttribute:NSBackgroundColorAttributeName value:[section.color colorWithAlphaComponent:0.19] range:section.range];
      continue;
    }
    NSMutableParagraphStyle *body = [normal mutableCopy];
    body.firstLineHeadIndent = 12; body.headIndent = 12; body.tailIndent = -12;
    body.paragraphSpacing = 6;
    [storage addAttribute:NSParagraphStyleAttributeName value:body range:section.paragraphRange];
    const NSRange first = [storage.string paragraphRangeForRange:NSMakeRange(section.range.location, 1)];
    const NSRange last = [storage.string paragraphRangeForRange:NSMakeRange(NSMaxRange(section.range) - 1, 1)];
    NSMutableParagraphStyle *firstStyle = [body mutableCopy];
    const bool blankParagraphGap = previousCard != nil &&
        hasBlankLyricsParagraphGap(storage.string, previousCard.paragraphRange, section.range.location);
    firstStyle.paragraphSpacingBefore = section.range.location == 0 ? 0 : headerHeight - (blankParagraphGap ? 12 : 0);
    [storage addAttribute:NSParagraphStyleAttributeName value:firstStyle range:first];
    NSMutableParagraphStyle *lastStyle = [[storage attribute:NSParagraphStyleAttributeName atIndex:last.location effectiveRange:nullptr] mutableCopy];
    lastStyle.paragraphSpacing = headerHeight;
    [storage addAttribute:NSParagraphStyleAttributeName value:lastStyle range:last];
    previousCard = section;
  }
  [storage endEditing];
  if (registration) [undo enableUndoRegistration];
  self.editor.typingAttributes = @{NSFontAttributeName:self.editor.font,
                                   NSForegroundColorAttributeName:textColor};
  juce::String identity = sharedString(self.view.traitCollection.preferredContentSizeCategory);
  for (PlayWorshipLyricsSectionPresentation *section in self.sectionPresentations)
    identity << sharedString(section.identifier) << ':' << sharedString(section.label) << ':' << sharedString(section.code) << ';';
  const CGFloat diameter = std::max<CGFloat>(44, lyricsNavigationCircleDiameter() + 8);
  self.sectionHeight.constant = document.sections.empty() ? 0 : diameter + 4;
  self.sectionScroll.hidden = document.sections.empty();
  if (identity != chipIdentity) {
    chipIdentity = identity;
    for (UIView *view in self.sectionStack.arrangedSubviews) {
      [self.sectionStack removeArrangedSubview:view]; [view removeFromSuperview];
    }
    for (PlayWorshipLyricsSectionPresentation *section in self.sectionPresentations) {
      PlayWorshipLyricsNavigationButton *button = [[PlayWorshipLyricsNavigationButton alloc] initWithFrame:CGRectZero];
      button.sectionColor = section.color;
      [button setTitle:section.code forState:UIControlStateNormal];
      button.titleLabel.font = lyricsScaledFont(13, UIFontWeightSemibold, UIFontTextStyleSubheadline);
      [button setTitleColor:section.color forState:UIControlStateNormal];
      [button setTitleColor:section.color forState:UIControlStateSelected];
      button.accessibilityLabel = section.label;
      button.accessibilityIdentifier = [@"nativeLyricsSection:" stringByAppendingString:section.identifier];
      [button addTarget:self action:@selector(sectionChipPressed:) forControlEvents:UIControlEventTouchUpInside];
      [NSLayoutConstraint activateConstraints:@[[button.heightAnchor constraintEqualToConstant:diameter], [button.widthAnchor constraintEqualToConstant:diameter]]];
      [self.sectionStack addArrangedSubview:button];
    }
  }
  // The first section also reveals the fixed navigation strip, changing the
  // editor's height. Settle that geometry and TextKit's paragraph layout before
  // restoring the native viewport; restoring earlier lets UIKit move it again
  // as the remaining line fragments and contentSize are calculated lazily.
  [UIView performWithoutAnimation:^{
    [self.view setNeedsLayout];
    [self.view layoutIfNeeded];
    if (!CGRectIsEmpty(self.editor.bounds)) {
      [self.lyricsLayout ensureLayoutForTextContainer:self.editor.textContainer];
      [self.editor setNeedsLayout];
      [self.editor layoutIfNeeded];
    }
    if (!NSEqualRanges(self.editor.selectedRange, selection)) self.editor.selectedRange = selection;
    if (!CGPointEqualToPoint(self.editor.contentOffset, offset)) [self.editor setContentOffset:offset animated:NO];
  }];
  updatingAppearance = false;
  [self.editor setNeedsDisplay];
  [self refreshCardDecorations];
}
- (void)refreshCardDecorations {
  if (refreshingCards || updatingAppearance || self.editor == nil || CGRectIsEmpty(self.editor.bounds)) return;
  refreshingCards = true;
  self.headerOverlay.frame = self.editor.frame;
  NSMutableSet<NSString *> *visible = [NSMutableSet new];
  const CGRect viewport = CGRectInset(self.editor.bounds, 0, -lyricsHeaderHeight() - 64);
  const CGRect textViewport = CGRectOffset(viewport, -self.editor.textContainerInset.left, -self.editor.textContainerInset.top);
  const NSRange glyphs = [self.lyricsLayout glyphRangeForBoundingRect:textViewport inTextContainer:self.editor.textContainer];
  const NSRange characters = [self.lyricsLayout characterRangeForGlyphRange:glyphs actualGlyphRange:nullptr];
  UIColor *headerText = nativeColor(configuration.getProperty("colors", {}).getProperty("sectionHeaderText", {}), [UIColor colorWithWhite:0.12 alpha:1]);
  // Headers are siblings of the native text field, so they are real VoiceOver
  // headings without replacing UITextView's accessible editing element.
  for (PlayWorshipLyricsSectionPresentation *section in self.sectionPresentations) {
    if (!section.completeBlock || NSIntersectionRange(section.range, characters).length == 0) continue;
    const CGRect card = [self.lyricsLayout cardRectForSection:section];
    if (CGRectIsNull(card)) continue;
    CGRect header = CGRectMake(CGRectGetMinX(card) + self.editor.textContainerInset.left + 12,
        CGRectGetMinY(card) + self.editor.textContainerInset.top - lyricsHeaderHeight() * 0.5, 0, lyricsHeaderHeight());
    if (CGRectGetMaxY(header) < CGRectGetMinY(viewport) || CGRectGetMinY(header) > CGRectGetMaxY(viewport)) continue;
    [visible addObject:section.identifier];
    PlayWorshipLyricsCardHeader *view = self.cardHeaders[section.identifier];
    if (view == nil) { view = [[PlayWorshipLyricsCardHeader alloc] initWithFrame:CGRectZero]; self.cardHeaders[section.identifier] = view; [self.headerOverlay addSubview:view]; }
    [view configure:section textColor:headerText];
    const CGSize size = [view sizeThatFits:CGSizeMake(std::max<CGFloat>(0, self.editor.bounds.size.width - 64), lyricsHeaderHeight())];
    header.size = size;
    view.frame = [self.editor convertRect:header toView:self.headerOverlay];
    view.accessibilityTraits = UIAccessibilityTraitHeader | (sharedString(section.identifier) == activeSectionID ? UIAccessibilityTraitSelected : 0);
  }
  for (NSString *identifier in self.cardHeaders.allKeys)
    if (![visible containsObject:identifier]) { [self.cardHeaders[identifier] removeFromSuperview]; [self.cardHeaders removeObjectForKey:identifier]; }
  refreshingCards = false;
  if (scrollTargetID.isEmpty()) [self updateActiveSectionForScroll];
}
- (void)updateActiveSectionForScroll {
  if (self.sectionPresentations.count == 0 || self.editor.textStorage.length == 0) { [self setActiveSection:{} animated:NO]; return; }
  const CGFloat maximum = std::max<CGFloat>(0, self.editor.contentSize.height - self.editor.bounds.size.height);
  if (maximum > 0 && self.editor.contentOffset.y >= maximum - 1) {
    [self setActiveSection:sharedString(self.sectionPresentations.lastObject.identifier) animated:YES]; return;
  }
  const CGPoint anchor = CGPointMake(20, std::max<CGFloat>(0,
      self.editor.contentOffset.y + lyricsHeaderHeight() + 16 - self.editor.textContainerInset.top));
  const NSUInteger character = [self.lyricsLayout characterIndexForPoint:anchor inTextContainer:self.editor.textContainer
      fractionOfDistanceBetweenInsertionPoints:nullptr];
  PlayWorshipLyricsSectionPresentation *active = self.sectionPresentations.firstObject;
  for (PlayWorshipLyricsSectionPresentation *section in self.sectionPresentations) {
    if (section.range.location <= character) active = section; else break;
  }
  [self setActiveSection:sharedString(active.identifier) animated:YES];
}
- (void)setActiveSection:(const juce::String &)identifier animated:(BOOL)animated {
  const bool changed = activeSectionID != identifier;
  activeSectionID = identifier;
  for (UIView *view in self.sectionStack.arrangedSubviews) {
    if (![view isKindOfClass:UIButton.class]) continue;
    UIButton *button = (UIButton *)view;
    const auto key = sharedString(button.accessibilityIdentifier).fromFirstOccurrenceOf("nativeLyricsSection:", false, false);
    button.selected = key == identifier;
    button.accessibilityTraits = UIAccessibilityTraitButton | (button.selected ? UIAccessibilityTraitSelected : 0);
    if (button.selected && changed) [self.sectionScroll scrollRectToVisible:CGRectInset([button convertRect:button.bounds toView:self.sectionScroll], -8, 0) animated:animated];
  }
  for (NSString *key in self.cardHeaders)
    self.cardHeaders[key].accessibilityTraits = UIAccessibilityTraitHeader | (sharedString(key) == identifier ? UIAccessibilityTraitSelected : 0);
}
- (void)scrollViewDidScroll:(UIScrollView *)scrollView {
  if (scrollView == self.editor) [self refreshCardDecorations];
}
- (void)scrollViewWillBeginDragging:(UIScrollView *)scrollView {
  if (scrollView == self.editor) scrollTargetID = {};
}
- (void)scrollViewDidEndScrollingAnimation:(UIScrollView *)scrollView {
  if (scrollView == self.editor) { scrollTargetID = {}; [self updateActiveSectionForScroll]; }
}
- (void)sectionChipPressed:(UIButton *)button {
  if (busy) return;
  const auto identifier = sharedString(button.accessibilityIdentifier).fromFirstOccurrenceOf("nativeLyricsSection:", false, false);
  for (PlayWorshipLyricsSectionPresentation *section in self.sectionPresentations) {
    if (sharedString(section.identifier) != identifier) continue;
    self.editor.selectedRange = section.range;
    CGRect bounds = [self.lyricsLayout cardRectForSection:section];
    if (CGRectIsNull(bounds)) {
      [self.editor scrollRangeToVisible:section.range];
    } else {
      const CGFloat y = CGRectGetMinY(bounds) + self.editor.textContainerInset.top - lyricsHeaderHeight() * 0.5 - 12;
      const CGFloat maximum = std::max<CGFloat>(0, self.editor.contentSize.height - self.editor.bounds.size.height);
      const CGFloat target = std::clamp<CGFloat>(y, 0, maximum);
      const BOOL moving = std::abs(target - self.editor.contentOffset.y) > 0.5;
      scrollTargetID = moving ? identifier : juce::String();
      [self.editor setContentOffset:CGPointMake(self.editor.contentOffset.x, target) animated:moving];
    }
    [self setActiveSection:identifier animated:YES];
    break;
  }
}
- (void)applySection:(juce::String)kind range:(NSRange)range revision:(uint64_t)revision {
  if (busy || revision != documentRevision || range.length == 0 ||
      range.location > INT_MAX || range.length > INT_MAX) return;
  if (kind.isNotEmpty()) {
    const NSRange paragraphs = enclosingLyricsParagraphs(nativeString(document.text), range, true);
    if (paragraphs.location == NSNotFound || paragraphs.length == 0 || paragraphs.location > INT_MAX || paragraphs.length > INT_MAX) return;
    range = paragraphs;
  }
  const bool valid = kind.isEmpty()
      ? clearSongLyricsSections(document, static_cast<int>(range.location), static_cast<int>(range.length))
      : applySongLyricsSection(document, static_cast<int>(range.location), static_cast<int>(range.length), kind);
  if (valid) {
    // Section edits are independent of the native text undo stack. Snapshots
    // from before this manual change must never overwrite these newer labels.
    textEdits.clear(); textEditCursor = 0;
    sectionSnapshot = std::make_shared<const std::vector<SongLyricsSection>>(document.sections);
    ++documentRevision; appearanceDirty = true; [self updateAppearance];
  }
}
- (UIMenu *)menuForRange:(NSRange)range suggested:(NSArray<UIMenuElement *> *)actions {
  if (range.length == 0 || busy) return [UIMenu menuWithChildren:actions];
  NSMutableArray<UIMenuElement *> *choices = [NSMutableArray new];
  const auto revision = documentRevision;
  __weak auto weakSelf = self;
  const auto kinds = configuration.getProperty("sectionKinds", {});
  if (const auto *array = kinds.getArray()) {
    for (const auto &kind : *array) {
      const auto identifier = kind.getProperty("id", {}).toString();
      auto *choice = [UIAction actionWithTitle:nativeString(kind.getProperty("title", {}).toString()) image:nil identifier:nil handler:^(UIAction *) {
        [weakSelf applySection:identifier range:range revision:revision];
      }];
      [choices addObject:choice];
    }
  }
  [choices addObject:[UIAction actionWithTitle:[self label:"clearSectionTitle"] image:nil identifier:nil handler:^(UIAction *) {
    [weakSelf applySection:juce::String() range:range revision:revision];
  }]];
  auto *sectionMenu = [UIMenu menuWithTitle:[self label:"sectionActionTitle"] children:choices];
  return [UIMenu menuWithChildren:[@[sectionMenu] arrayByAddingObjectsFromArray:actions]];
}
- (UIMenu *)textView:(UITextView *)textView editMenuForTextInRange:(NSRange)range suggestedActions:(NSArray<UIMenuElement *> *)actions {
  (void)textView;
  if (@available(iOS 16.0, *)) return [self menuForRange:range suggested:actions];
  return nil;
}
- (UIMenu *)textView:(UITextView *)textView editMenuForTextInRanges:(NSArray<NSValue *> *)ranges suggestedActions:(NSArray<UIMenuElement *> *)actions {
  (void)textView;
  // Discontinuous bidirectional selections cannot be flattened into one label.
  if (@available(iOS 16.0, *)) {
    if (ranges.count == 1) return [self menuForRange:ranges.firstObject.rangeValue suggested:actions];
    return [UIMenu menuWithChildren:actions];
  }
  return nil;
}
- (void)showSectionChooser {
  const NSRange range = self.editor.selectedRange;
  if (range.length == 0 || busy) return;
  const auto revision = documentRevision;
  UIAlertController *sheet = [UIAlertController alertControllerWithTitle:[self label:"sectionActionTitle"] message:nil preferredStyle:UIAlertControllerStyleActionSheet];
  __weak auto weakSelf = self;
  const auto kinds = configuration.getProperty("sectionKinds", {});
  if (const auto *array = kinds.getArray()) for (const auto &kind : *array) {
    const auto identifier = kind.getProperty("id", {}).toString();
    [sheet addAction:[UIAlertAction actionWithTitle:nativeString(kind.getProperty("title", {}).toString()) style:UIAlertActionStyleDefault handler:^(UIAlertAction *) {
      [weakSelf applySection:identifier range:range revision:revision];
    }]];
  }
  [sheet addAction:[UIAlertAction actionWithTitle:[self label:"clearSectionTitle"] style:UIAlertActionStyleDestructive handler:^(UIAlertAction *) {
    [weakSelf applySection:juce::String() range:range revision:revision];
  }]];
  [sheet addAction:[UIAlertAction actionWithTitle:[self label:"keepEditingTitle"] style:UIAlertActionStyleCancel handler:nil]];
  sheet.popoverPresentationController.sourceView = self.editor;
  CGRect caret = [self.editor caretRectForPosition:self.editor.selectedTextRange.start];
  sheet.popoverPresentationController.sourceRect = CGRectIsEmpty(caret) ? self.editor.bounds : caret;
  [self presentViewController:sheet animated:YES completion:nil];
}
- (void)closePressed {
  if (busy) return;
  const auto state = presentation.lock();
  if (state == nullptr || state->finished) return;
  if (initialDocument == juce::JSON::toString(songLyricsToVar(document), true)) {
    finishPresentation(state, true); return;
  }
  UIAlertController *alert = [UIAlertController alertControllerWithTitle:[self label:"discardTitle"] message:[self label:"discardMessage"] preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction actionWithTitle:[self label:"keepEditingTitle"] style:UIAlertActionStyleCancel handler:nil]];
  [alert addAction:[UIAlertAction actionWithTitle:[self label:"discardConfirmTitle"] style:UIAlertActionStyleDestructive handler:^(UIAlertAction *) {
    finishPresentation(state, true);
  }]];
  [self presentViewController:alert animated:YES completion:nil];
}
- (void)setSaving:(BOOL)saving {
  busy = saving; self.editor.editable = !saving;
  self.closeItem.enabled = !saving; self.saveItem.enabled = !saving;
  self.saveItem.title = saving ? [self label:"savingTitle"] : [self label:"saveTitle"];
  self.sectionScroll.userInteractionEnabled = !saving;
  if (saving) [self.editor resignFirstResponder];
}
- (void)savePressed {
  if (busy) return;
  const auto state = presentation.lock();
  if (state == nullptr || state->finished || state->owner == nullptr || !state->save) return;
  if (!setNativeLyricsText(document, sharedString(self.editor.text)) || !isValidSongLyricsDocument(document)) {
    [self saveFinished:false error:sharedString([self label:"errorTitle"])]; return;
  }
  [self setSaving:YES];
  std::optional<SongLyricsDocument> result = hasSongLyrics(document) ? std::optional<SongLyricsDocument>(document) : std::nullopt;
  juce::MessageManager::callAsync([state, result] {
    if (state->finished || state->owner == nullptr || !state->save) return;
    auto save = state->save;
    const auto completed = std::make_shared<std::atomic<bool>>(false);
    save(result, [weak = std::weak_ptr<LyricsPresentation>(state), completed](bool success, juce::String error) {
      if (completed->exchange(true)) return;
      dispatch_async(dispatch_get_main_queue(), ^{
        auto active = weak.lock();
        if (active != nullptr && !active->finished && active->owner != nullptr)
          [active->controller saveFinished:success error:error];
      });
    });
  });
}
- (void)saveFinished:(bool)success error:(juce::String)message {
  const auto state = presentation.lock();
  if (state == nullptr || state->finished) return;
  if (success) { finishPresentation(state, true); return; }
  [self setSaving:NO];
  auto *alert = [UIAlertController alertControllerWithTitle:[self label:"errorTitle"] message:nativeString(message) preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
  [self presentViewController:alert animated:YES completion:nil];
}
@end

namespace NativeSongLyricsEditor {
bool isAvailable() { return true; }
std::unique_ptr<Handle> present(juce::Component &owner, Options options,
                              Save save, std::function<void()> close) {
  if (!NSThread.isMainThread || !save) return {};
  if (options.document && !isValidSongLyricsDocument(*options.document)) return {};
  auto *presenter = presenterForOwner(owner);
  if (presenter == nil || presenter.isBeingDismissed || presenter.isBeingPresented) return {};
  auto state = std::make_shared<LyricsPresentation>(owner);
  state->save = std::move(save); state->close = std::move(close);
  auto *controller = [PlayWorshipNativeLyricsController new];
  [controller configure:options]; controller->presentation = state;
  auto *navigation = [[PlayWorshipNativeLyricsNavigation alloc] initWithRootViewController:controller];
  const auto colors = NativeSongLyricsEditor::presentationConfig(options).getProperty("colors", {});
  navigation.view.tintColor = nativeColor(colors.getProperty("accent", {}), UIColor.systemGreenColor);
  navigation.navigationBar.tintColor = navigation.view.tintColor;
  navigation.modalPresentationStyle = UIModalPresentationFullScreen;
  navigation.modalInPresentation = YES;
  navigation.view.accessibilityViewIsModal = YES;
  state->controller = controller; state->navigation = navigation;
  if (UIDevice.currentDevice.userInterfaceIdiom != UIUserInterfaceIdiomPad) {
    state->previousOrientations = juce::Desktop::getInstance().getOrientationsEnabled();
    state->changedOrientation = state->previousOrientations != juce::Desktop::upright;
    if (state->changedOrientation) juce::Desktop::getInstance().setOrientationsEnabled(juce::Desktop::upright);
  }
  [presenter presentViewController:navigation animated:YES completion:nil];
  return std::make_unique<UIKitLyricsHandle>(std::move(state), navigation);
}
} // namespace NativeSongLyricsEditor
