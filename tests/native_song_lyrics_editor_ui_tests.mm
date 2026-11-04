// Standalone UIKit test application. Link MTControls, never include the bridge
// implementation here, and do not compile this main into the product app.
#include "platform/NativeSongLyricsEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <iostream>
#include <cmath>
#include <stdexcept>

@interface UIViewController (PlayWorshipLyricsTestSelectors)
- (void)closePressed;
- (void)savePressed;
- (void)updateAppearance;
- (void)applySection:(juce::String)kind range:(NSRange)range revision:(uint64_t)revision;
- (void)applyChord:(juce::String)symbol range:(NSRange)range revision:(uint64_t)revision;
- (void)removeChordAtRange:(NSRange)range revision:(uint64_t)revision;
- (UIMenu *)menuForRange:(NSRange)range suggested:(NSArray<UIMenuElement *> *)actions;
- (void)selectionMenuGesture:(UILongPressGestureRecognizer *)gesture;
- (void)moveLyricsCard:(juce::String)source before:(juce::String)target revision:(uint64_t)revision;
- (void)textViewDidChangeSelection:(UITextView *)textView;
- (void)scrollViewWillBeginDragging:(UIScrollView *)scrollView;
- (void)scrollViewDidEndDragging:(UIScrollView *)scrollView willDecelerate:(BOOL)decelerate;
@end
@interface NSLayoutManager (PlayWorshipLyricsCardTestSelectors)
- (CGRect)cardRectForSection:(id)section;
@end

// Drive only the production observer's state handler; this does not synthesize
// UIKit selection gestures or claim physical loupe/handle interaction coverage.
@interface PlayWorshipLyricsObservedLongPress : UILongPressGestureRecognizer
@property(nonatomic) UIGestureRecognizerState fixtureState;
@property(nonatomic) CGPoint fixtureLocation;
@end
@implementation PlayWorshipLyricsObservedLongPress
- (UIGestureRecognizerState)state { return self.fixtureState; }
- (CGPoint)locationInView:(UIView *)view {
  return view == self.view || self.view == nil ? self.fixtureLocation : [self.view convertPoint:self.fixtureLocation toView:view];
}
@end

@interface PlayWorshipLyricsDragSessionFixture : NSObject <UIDragSession, UIDropSession>
@property(nonatomic, copy) NSArray<UIDragItem *> *items;
@property(nonatomic) BOOL allowsMoveOperation;
@property(nonatomic, getter=isRestrictedToDraggingApplication) BOOL restrictedToDraggingApplication;
@property(nonatomic, strong) id localContext;
@property(nonatomic, strong) NSProgress *progress;
@property(nonatomic) UIDropSessionProgressIndicatorStyle progressIndicatorStyle;
@property(nonatomic, weak) UIView *coordinateView;
@property(nonatomic) CGPoint fixtureLocation;
@end
@implementation PlayWorshipLyricsDragSessionFixture
- (instancetype)init {
  if ((self = [super init])) { self.items = @[]; self.allowsMoveOperation = YES; self.restrictedToDraggingApplication = YES; self.progress = [NSProgress progressWithTotalUnitCount:1]; }
  return self;
}
- (id<UIDragSession>)localDragSession { return self; }
- (CGPoint)locationInView:(UIView *)view { return self.coordinateView == nil ? self.fixtureLocation : [self.coordinateView convertPoint:self.fixtureLocation toView:view]; }
- (BOOL)hasItemsConformingToTypeIdentifiers:(NSArray<NSString *> *)types {
  for (UIDragItem *item in self.items) for (NSString *type in types)
    if ([item.itemProvider.registeredTypeIdentifiers containsObject:type]) return YES;
  return NO;
}
- (BOOL)canLoadObjectsOfClass:(Class<NSItemProviderReading>)aClass { (void)aClass; return NO; }
- (NSProgress *)loadObjectsOfClass:(Class<NSItemProviderReading>)aClass completion:(void (^)(NSArray<__kindof id<NSItemProviderReading>> *))completion {
  (void)aClass; completion(@[]); return self.progress;
}
@end

namespace {
UIDragInteraction *dragInteractionOnView(UIView *view) {
  for (id<UIInteraction> interaction in view.interactions)
    if ([interaction isKindOfClass:UIDragInteraction.class]) return (UIDragInteraction *)interaction;
  return nil;
}
bool containsNativeButton(UIView *root) {
  if ([root isKindOfClass:UIButton.class]) return true;
  for (UIView *child in root.subviews) if (containsNativeButton(child)) return true;
  return false;
}
bool writeLyricsWindowPreview(UIWindow *window, NSString *filename) {
  UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithBounds:window.bounds];
  UIImage *preview = [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) {
    (void)context; [window drawViewHierarchyInRect:window.bounds afterScreenUpdates:YES];
  }];
  NSString *directory = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
  [NSFileManager.defaultManager createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
  NSData *png = UIImagePNGRepresentation(preview);
  return png.length > 0 && [png writeToFile:[directory stringByAppendingPathComponent:filename] atomically:YES];
}
void sendMenuGesture(UIViewController *controller, UITextView *editor,
                     PlayWorshipLyricsObservedLongPress *gesture,
                     UIGestureRecognizerState state) {
  UITextPosition *position = editor.selectedTextRange.start ?: editor.beginningOfDocument;
  const CGRect caret = [editor caretRectForPosition:position];
  gesture.fixtureLocation = CGPointMake(CGRectGetMidX(caret), CGRectGetMidY(caret));
  gesture.fixtureState = state;
  [controller selectionMenuGesture:gesture];
}
UIView *identifiedView(UIView *root, NSString *identifier) {
  if ([root.accessibilityIdentifier isEqualToString:identifier]) return root;
  for (UIView *child in root.subviews)
    if (UIView *result = identifiedView(child, identifier)) return result;
  return nil;
}
void collectIdentifiedViews(UIView *root, NSString *prefix, NSMutableArray<UIView *> *output) {
  if ([root.accessibilityIdentifier hasPrefix:prefix]) [output addObject:root];
  for (UIView *child in root.subviews) collectIdentifiedViews(child, prefix, output);
}
NSArray<UIView *> *identifiedViews(UIView *root, NSString *prefix) {
  NSMutableArray<UIView *> *result = [NSMutableArray array];
  collectIdentifiedViews(root, prefix, result);
  return result;
}
UIMenu *menuWithIdentifier(UIMenu *menu, NSString *identifier) API_AVAILABLE(ios(13.0)) {
  if ([menu.identifier isEqualToString:identifier]) return menu;
  for (UIMenuElement *entry in menu.children)
    if ([entry isKindOfClass:UIMenu.class])
      if (UIMenu *found = menuWithIdentifier((UIMenu *)entry, identifier)) return found;
  return nil;
}
UIAction *actionWithIdentifier(UIMenu *menu, NSString *identifier) API_AVAILABLE(ios(13.0)) {
  for (UIMenuElement *entry in menu.children) {
    if ([entry isKindOfClass:UIAction.class] && [((UIAction *)entry).identifier isEqualToString:identifier])
      return (UIAction *)entry;
    if ([entry isKindOfClass:UIMenu.class])
      if (UIAction *found = actionWithIdentifier((UIMenu *)entry, identifier)) return found;
  }
  return nil;
}
UIMenu *sectionSubmenu(UIMenu *menu) API_AVAILABLE(ios(13.0)) {
  for (UIMenuElement *entry in menu.children)
    if ([entry isKindOfClass:UIMenu.class] &&
        ![((UIMenu *)entry).identifier isEqualToString:@"nativeLyricsChordMenu"] &&
        ((UIMenu *)entry).children.count == 9) return (UIMenu *)entry;
  return nil;
}
NSArray<UILabel *> *visibleChordLabels(UIView *root) {
  NSMutableArray<UILabel *> *result = [NSMutableArray array];
  for (UIView *entry in identifiedViews(root, @"nativeLyricsChord:"))
    if ([entry isKindOfClass:UILabel.class]) [result addObject:(UILabel *)entry];
  return result;
}
UILabel *chordLabel(UIView *root, NSString *symbol) {
  for (UILabel *entry in visibleChordLabels(root))
    if ([entry.text isEqualToString:symbol]) return entry;
  return nil;
}
NSArray<NSString *> *cardHeaderIdentities(UIView *root) {
  NSMutableArray<NSString *> *result = [NSMutableArray array];
  for (UIView *header in identifiedViews(root, @"nativeLyricsCardHeader:"))
    [result addObject:header.accessibilityIdentifier];
  return [result sortedArrayUsingSelector:@selector(compare:)];
}
void layoutCards(UIViewController *controller, UITextView *editor) {
  [editor.layoutManager ensureLayoutForTextContainer:editor.textContainer];
  [controller.view setNeedsLayout]; [controller.view layoutIfNeeded];
  [editor setNeedsLayout]; [editor layoutIfNeeded];
}
void layoutTopCards(UIViewController *controller, UITextView *editor) {
  [editor setContentOffset:CGPointZero animated:NO];
  layoutCards(controller, editor);
}
CGRect headerDocumentFrame(UIView *header, UITextView *editor) {
  return [header.superview convertRect:header.frame toView:editor];
}
NSArray<UIView *> *orderedCardHeaders(UIView *root, UITextView *editor) {
  return [identifiedViews(root, @"nativeLyricsCardHeader:") sortedArrayUsingComparator:^NSComparisonResult(UIView *a, UIView *b) {
    const CGFloat firstY = CGRectGetMinY(headerDocumentFrame(a, editor));
    const CGFloat secondY = CGRectGetMinY(headerDocumentFrame(b, editor));
    return firstY < secondY ? NSOrderedAscending : (firstY > secondY ? NSOrderedDescending : NSOrderedSame);
  }];
}
NSRange completeParagraphs(NSString *text, NSRange selection) {
  NSRange result = [text paragraphRangeForRange:selection];
  NSUInteger end = NSMaxRange(result);
  if (end > result.location && [text characterAtIndex:end - 1] == '\n') {
    --end;
    if (end > result.location && [text characterAtIndex:end - 1] == '\r') --end;
  } else if (end > result.location && [text characterAtIndex:end - 1] == '\r') --end;
  return NSMakeRange(result.location, end - result.location);
}
NSRange presentationRange(UIViewController *controller, NSString *identifier) {
  for (id entry in [controller valueForKey:@"sectionPresentations"])
    if ([[entry valueForKey:@"identifier"] isEqualToString:identifier])
      return [[entry valueForKey:@"range"] rangeValue];
  return NSMakeRange(NSNotFound, 0);
}
CGRect cardDocumentRect(UIViewController *controller, UITextView *editor, NSString *identifier) {
  for (id entry in [controller valueForKey:@"sectionPresentations"]) {
    if (![[entry valueForKey:@"identifier"] isEqualToString:identifier]) continue;
    const CGRect card = [editor.layoutManager cardRectForSection:entry];
    return CGRectIsNull(card) ? card : CGRectOffset(card, editor.textContainerInset.left, editor.textContainerInset.top);
  }
  return CGRectNull;
}
// Compare selection geometry with the actual glyph baseline, independently of
// the decorative paragraph spacing used to make room for a card heading.
CGRect glyphLineBox(UITextView *editor, NSUInteger character) {
  if (editor.textStorage.length == 0) return CGRectNull;
  character = std::min(character, editor.textStorage.length - 1);
  const NSUInteger glyph = [editor.layoutManager glyphIndexForCharacterAtIndex:character];
  const CGRect line = [editor.layoutManager lineFragmentRectForGlyphAtIndex:glyph effectiveRange:nullptr];
  const CGPoint location = [editor.layoutManager locationForGlyphAtIndex:glyph];
  UIFont *font = [editor.textStorage attribute:NSFontAttributeName atIndex:character effectiveRange:nullptr] ?: editor.font;
  return CGRectMake(0, line.origin.y + location.y - font.ascender + editor.textContainerInset.top,
      editor.bounds.size.width, std::ceil(font.lineHeight));
}
NSArray<UITextSelectionRect *> *nativeSelectionRects(UITextView *editor, UITextRange *range) {
  // Invoke the public UITextView implementation, bypassing only our subclass's
  // geometry adapter so its flags and horizontal geometry can be compared.
  using Function = NSArray<UITextSelectionRect *> *(*)(UITextView *, SEL, UITextRange *);
  const SEL selector = @selector(selectionRectsForRange:);
  auto native = reinterpret_cast<Function>(method_getImplementation(class_getInstanceMethod(UITextView.class, selector)));
  return native(editor, selector, range);
}
bool selectionAnnotationsMatch(NSArray<UITextSelectionRect *> *actual, NSArray<UITextSelectionRect *> *native) {
  if (actual.count != native.count) return false;
  for (NSUInteger index = 0; index < actual.count; ++index) {
    UITextSelectionRect *a = actual[index], *b = native[index];
    if (a.containsStart != b.containsStart || a.containsEnd != b.containsEnd ||
        a.writingDirection != b.writingDirection || a.isVertical != b.isVertical ||
        std::abs(a.rect.origin.x - b.rect.origin.x) > 0.1 || std::abs(a.rect.size.width - b.rect.size.width) > 0.1) return false;
    if (@available(iOS 17.4, *))
      if (!CGAffineTransformEqualToTransform(a.transform, b.transform)) return false;
  }
  return true;
}
NSDictionary *editingGeometry(UITextView *editor) {
  const NSRange selection = editor.selectedRange;
  const CGPoint offset = editor.contentOffset;
  const CGRect frame = editor.frame;
  const CGRect bounds = editor.bounds;
  const CGSize size = editor.contentSize;
  const UIEdgeInsets textInset = editor.textContainerInset;
  const UIEdgeInsets contentInset = editor.contentInset;
  const UIEdgeInsets adjustedInset = editor.adjustedContentInset;
  return @{@"selection":@[@(selection.location), @(selection.length)],
      @"offset":@[@(offset.x), @(offset.y)],
      @"frame":@[@(frame.origin.x), @(frame.origin.y), @(frame.size.width), @(frame.size.height)],
      @"bounds":@[@(bounds.origin.x), @(bounds.origin.y), @(bounds.size.width), @(bounds.size.height)],
      @"contentSize":@[@(size.width), @(size.height)],
      @"textInset":@[@(textInset.top), @(textInset.left), @(textInset.bottom), @(textInset.right)],
      @"contentInset":@[@(contentInset.top), @(contentInset.left), @(contentInset.bottom), @(contentInset.right)],
      @"adjustedInset":@[@(adjustedInset.top), @(adjustedInset.left), @(adjustedInset.bottom), @(adjustedInset.right)],
      @"firstResponder":@(editor.isFirstResponder)};
}
void insertComposedText(UITextView *editor, NSString *text) {
  [text enumerateSubstringsInRange:NSMakeRange(0, text.length)
      options:NSStringEnumerationByComposedCharacterSequences
      usingBlock:^(NSString *substring, NSRange substringRange, NSRange enclosingRange, BOOL *stop) {
        (void)substringRange; (void)enclosingRange; (void)stop;
        [editor insertText:substring];
      }];
}
void later(double seconds, std::function<void()> callback) {
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(seconds * NSEC_PER_SEC)),
      dispatch_get_main_queue(), ^{ callback(); });
}
void insertOverRunLoop(UITextView *editor, NSArray<NSString *> *characters, NSUInteger index, std::function<void()> completed) {
  if (index == characters.count) { completed(); return; }
  [editor insertText:characters[index]];
  later(0.008, [editor, characters, index, completed] { insertOverRunLoop(editor, characters, index + 1, completed); });
}
void waitUntil(std::function<bool()> condition, std::function<void()> completed, double deadline) {
  if (condition() || CFAbsoluteTimeGetCurrent() >= deadline) { completed(); return; }
  later(0.05, [condition, completed, deadline] { waitUntil(condition, completed, deadline); });
}
void writeReport(bool success, int checks, const juce::String &error) {
  NSDictionary *report = @{ @"success":@(success), @"checks":@(checks),
      @"error":[NSString stringWithUTF8String:error.toRawUTF8()] ?: @"",
      @"device":UIDevice.currentDevice.model, @"os":UIDevice.currentDevice.systemVersion };
  NSData *json = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil];
  NSString *directory = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
  [NSFileManager.defaultManager createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
  [json writeToFile:[directory stringByAppendingPathComponent:@"native-lyrics-ui-report.json"] atomically:YES];
  std::cout << "NATIVE_LYRICS_UI_RESULT " << std::string(static_cast<const char *>(json.bytes), json.length) << std::endl;
}
}

@class PlayWorshipLyricsHarness;
@interface PlayWorshipLyricsHarness : NSObject {
  std::unique_ptr<juce::DocumentWindow> host;
  std::unique_ptr<juce::Component> owner;
  std::unique_ptr<NativeSongLyricsEditor::Handle> handle;
  NativeSongLyricsEditor::SaveCompletion firstCompletion;
  NativeSongLyricsEditor::SaveCompletion secondCompletion;
  std::optional<SongLyricsDocument> saved;
  SongLyricsDocument source;
  __strong PlayWorshipLyricsObservedLongPress *menuGestureFixture;
  int menuGuardCloseBaseline;
  int checks;
  int saves;
  int closes;
  NSRange firstMarkedRange;
  NSRange secondMarkedRange;
}
@property(nonatomic) BOOL manualMode;
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) UIViewController *editorController;
@property(nonatomic, strong) UITextView *textView;
- (void)startHost;
- (void)startManualFixture;
- (void)startChecks;
- (void)check:(BOOL)condition message:(const char *)message;
- (void)protected:(std::function<void()>)work;
- (void)checkNativeEditing;
- (void)checkSavingFailure;
- (void)checkSavingSuccess;
- (void)eventually:(std::function<bool()>)condition message:(const char *)message then:(std::function<void()>)next;
- (void)eventually:(std::function<bool()>)condition timeout:(double)seconds message:(const char *)message then:(std::function<void()>)next;
- (void)checkCancellation;
- (void)checkUnchangedClose;
- (void)checkParagraphSelection;
- (void)checkBoundaryTyping;
- (void)checkEditingFluency;
- (void)checkEmptyLineContinuation:(BOOL)hasFollowingSection;
- (void)checkSelectionGeometry;
- (void)checkLegacyPartialRange;
- (void)checkNativeChords;
- (void)checkAutomaticNativeMenus;
- (void)checkAutomaticMenuGuard:(NSInteger)index;
- (void)checkLegacyInstrumentalReadOnly;
- (void)checkNativeCardReordering;
- (void)finish:(BOOL)success error:(juce::String)error;
@end

@implementation PlayWorshipLyricsHarness
- (void)startHost {
  owner = std::make_unique<juce::Component>();
  host = std::make_unique<juce::DocumentWindow>("Native Lyrics UI Tests", juce::Colours::black,
      juce::DocumentWindow::allButtons);
  host->setContentNonOwned(owner.get(), true);
  host->setBounds(0, 0, juce::roundToInt(UIScreen.mainScreen.bounds.size.width),
                            juce::roundToInt(UIScreen.mainScreen.bounds.size.height));
  host->setVisible(true);
  later(0.6, [self] { [self protected:[self] {
    auto *peer = host->getPeer();
    UIView *native = peer != nullptr ? (__bridge UIView *)peer->getNativeHandle() : nil;
    self.window = native.window;
    if (self.manualMode) {
      if (self.window == nil) { std::cout << "NATIVE_LYRICS_MANUAL_ERROR no native peer window" << std::endl; return; }
      [self startManualFixture];
    } else {
      [self check:self.window != nil message:"JUCEApplication supplies its real native scene and peer window"];
      [self startChecks];
    }
  }]; });
  if (!self.manualMode) later(180.0, [self] { [self finish:NO error:"UIKit harness timed out"]; });
}
- (void)startManualFixture {
  const auto fixture = juce::SystemStats::getEnvironmentVariable("MTCONTROLS_NATIVE_LYRICS_MANUAL_FIXTURE", {});
  const bool mixed = fixture == "mixed", reorder = fixture == "reorder";
  SongLyricsDocument document;
  if (reorder) {
    document.schemaVersion = 2; document.text = juce::String::fromUTF8("Uno 🎹\nDos\nTres");
    document.sections = {{"manual-a", "verse", 0, 6}, {"manual-b", "chorus", 7, 3}, {"manual-c", "bridge", 11, 4}};
    document.chords = {{"manual-chord-a", 1, "C"}, {"manual-chord-b", 8, "Am7"}, {"manual-chord-c", 11, "G"}};
  } else if (mixed) {
    document.text = "Uno\nDos";
    document.sections.push_back({"manual-verse", "verse", 0, 7});
    juce::String blockId;
    if (!addSongLyricsInstrumentalBlock(document, 4, "instrumental", "Solo", 4, &blockId)) {
      std::cout << "NATIVE_LYRICS_MANUAL_ERROR invalid mixed fixture" << std::endl; return;
    }
    const auto row = document.instrumentalBlocks.front().rows.front();
    if (!setSongLyricsInstrumentalCell(document, blockId, row.id, row.cells[0].id, "C") ||
        !setSongLyricsInstrumentalCell(document, blockId, row.id, row.cells[1].id, "Am7")) {
      std::cout << "NATIVE_LYRICS_MANUAL_ERROR invalid fixture cells" << std::endl; return;
    }
  }
  NativeSongLyricsEditor::Options options;
  options.title = reorder ? "Reordenar tarjetas" : mixed ? "Letra y acordes heredados" : "Prueba manual de letra";
  options.document = document;
  handle = NativeSongLyricsEditor::present(*owner, options,
      [self](auto lyrics, auto completion) {
        saved = std::move(lyrics); completion(true, {});
        std::cout << "NATIVE_LYRICS_MANUAL_SAVE_BRIDGE_ONLY (no project I/O)" << std::endl;
      }, [] { std::cout << "NATIVE_LYRICS_MANUAL_CLOSED (host remains open)" << std::endl; });
  if (!handle) { std::cout << "NATIVE_LYRICS_MANUAL_ERROR native presentation failed" << std::endl; return; }
  later(0.6, [self, mixed, reorder] {
    self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
    self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
    std::cout << "NATIVE_LYRICS_MANUAL_READY fixture=" << (reorder ? "reorder" : mixed ? "mixed" : "empty")
              << " (no automated checks, timeout or success report)" << std::endl;
  });
}
- (void)protected:(std::function<void()>)work {
  try { work(); }
  catch (const std::exception &error) { [self finish:NO error:juce::String::fromUTF8(error.what())]; }
}
- (void)check:(BOOL)condition message:(const char *)message {
  ++checks;
  if (!condition) {
    if (self.textView != nil) {
      NSMutableArray *headers = [NSMutableArray new];
      for (UIView *header in orderedCardHeaders(self.editorController.view, self.textView))
        [headers addObject:@{@"id":header.accessibilityIdentifier ?: @"", @"rect":NSStringFromCGRect(headerDocumentFrame(header, self.textView))}];
      NSMutableArray *sections = [NSMutableArray new];
      for (id entry in [self.editorController valueForKey:@"sectionPresentations"])
        [sections addObject:@{@"id":[entry valueForKey:@"identifier"], @"range":[entry valueForKey:@"range"], @"complete":[entry valueForKey:@"completeBlock"]}];
      NSLog(@"LYRICS_FAILURE_GEOMETRY %@ headers=%@ sections=%@ frames=%@", editingGeometry(self.textView), headers, sections, [self.textView.layoutManager valueForKey:@"cardFrames"]);
    }
    throw std::runtime_error(message);
  }
}
- (void)eventually:(std::function<bool()>)condition message:(const char *)message then:(std::function<void()>)next {
  [self eventually:condition timeout:3.0 message:message then:next];
}
- (void)eventually:(std::function<bool()>)condition timeout:(double)seconds message:(const char *)message then:(std::function<void()>)next {
  waitUntil(condition, [self, condition, message, next] {
    [self protected:[self, condition, message, next] {
      [self check:condition() message:message];
      next();
    }];
  }, CFAbsoluteTimeGetCurrent() + seconds);
}
- (void)startChecks {
  [self check:NativeSongLyricsEditor::isAvailable() message:"UIKit editor is available in the actual iOS target"];
  juce::Component unattached;
  [self check:!NativeSongLyricsEditor::present(unattached, {}, [](auto, auto) {}, [] {})
       message:"an unattached JUCE owner does not create an unowned native presentation"];
  source.text = juce::String::fromUTF8(u8"Primera línea de ensayo 🎹\nSegunda línea\n");
  for (int i = 0; i < 90; ++i) source.text << "Renglon adicional de prueba " << i << '\n';
  NativeSongLyricsEditor::Options options;
  options.title = "Native UIKit fixture"; options.document = source;
  __weak auto weakSelf = self;
  handle = NativeSongLyricsEditor::present(*owner, options,
      [weakSelf](auto document, auto completion) {
        auto *strong = weakSelf;
        if (strong == nil) return;
        ++strong->saves; strong->saved = std::move(document);
        if (strong->saves == 1) strong->firstCompletion = std::move(completion);
        else strong->secondCompletion = std::move(completion);
      }, [weakSelf] { auto *strong = weakSelf; if (strong != nil) ++strong->closes; });
  [self check:handle != nullptr message:"public bridge creates a native editor from a real JUCE peer"];
  later(0.7, [self] { [self protected:[self] { [self checkNativeEditing]; }]; });
}
- (void)checkNativeEditing {
  auto *navigation = (UINavigationController *)self.window.rootViewController.presentedViewController;
  [self check:[navigation isKindOfClass:UINavigationController.class] message:"editor is hosted in a native navigation controller"];
  self.editorController = navigation.topViewController;
  self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
  [self check:[self.textView isKindOfClass:UITextView.class] message:"all lyric cards share one real UIKit UITextView"];
  [self check:self.textView.editable && self.textView.selectable message:"native text supports selection and editing"];
  [self check:[self.textView.text isEqualToString:[NSString stringWithUTF8String:source.text.toRawUTF8()]]
       message:"UTF-8 model roundtrip preserves Unicode in native text"];
  [self check:identifiedView(self.editorController.view, @"nativeLyricsSections").hidden
       message:"section navigation is absent until a section exists"];
  [self check:[self.editorController.navigationItem.leftBarButtonItem.accessibilityIdentifier isEqualToString:@"nativeLyricsClose"] &&
              [self.editorController.navigationItem.rightBarButtonItem.accessibilityIdentifier isEqualToString:@"nativeLyricsSave"]
       message:"native Close and Done remain independently accessible"];
  [self check:[self.editorController.navigationItem.rightBarButtonItem.tintColor isEqual:self.editorController.view.tintColor] &&
              [navigation.navigationBar.tintColor isEqual:self.editorController.view.tintColor]
       message:"native Done and navigation use the PlayWorship accent"];
  [self.textView becomeFirstResponder];
  self.textView.selectedRange = NSMakeRange(0, 7);
  [self.editorController.view layoutIfNeeded];
  if (@available(iOS 16.0, *)) {
    UIMenu *menu = [self.textView.delegate textView:self.textView editMenuForTextInRange:self.textView.selectedRange suggestedActions:@[]];
    [self check:menuWithIdentifier(menu, @"nativeLyricsChordMenu") != nil && sectionSubmenu(menu) != nil
         message:"native selection menu offers Chord and Section submenus"];
    UIMenu *sections = sectionSubmenu(menu);
    [self check:sections.children.count == 9 message:"Section offers eight kinds and explicit removal"];
    UIAction *suggested = [UIAction actionWithTitle:@"Copy" image:nil identifier:nil handler:^(UIAction *) {}];
    UIMenu *withSystemActions = [self.textView.delegate textView:self.textView editMenuForTextInRange:self.textView.selectedRange suggestedActions:@[suggested]];
    [self check:menuWithIdentifier(withSystemActions, @"nativeLyricsChordMenu") != nil &&
                sectionSubmenu(withSystemActions) != nil && [withSystemActions.children containsObject:suggested]
         message:"adding Chord preserves Section and the same native suggested editing actions"];
  }
  NSString *originalText = [self.textView.text copy];
  const NSRange selected = self.textView.selectedRange;
  firstMarkedRange = completeParagraphs(originalText, selected);
  secondMarkedRange = completeParagraphs(originalText, [originalText rangeOfString:@"Segunda"]);
  [self.textView setContentOffset:CGPointMake(0, 180) animated:NO];
  const CGPoint offset = self.textView.contentOffset;
  NSDictionary *beforeSectionGeometry = editingGeometry(self.textView);
  [self.editorController applySection:"verse" range:selected
      revision:[[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue]];
  NSDictionary *afterSectionGeometry = editingGeometry(self.textView);
  layoutCards(self.editorController, self.textView);
  const BOOL preservedSectionGeometry = NSEqualRanges(self.textView.selectedRange, selected) && CGPointEqualToPoint(self.textView.contentOffset, offset);
  juce::String geometryMessage = "section card formatting preserves native selection and scroll offset";
  if (!preservedSectionGeometry) {
    NSDictionary *diagnostic = @{@"beforeApply":beforeSectionGeometry,
        @"immediatelyAfterApply":afterSectionGeometry, @"afterLayout":editingGeometry(self.textView)};
    NSData *data = [NSJSONSerialization dataWithJSONObject:diagnostic options:NSJSONWritingSortedKeys error:nil];
    const juce::String json = juce::String::fromUTF8(static_cast<const char *>(data.bytes), static_cast<int>(data.length));
    std::cout << "NATIVE_LYRICS_GEOMETRY " << json << std::endl;
    geometryMessage << ": " << json;
  }
  [self check:preservedSectionGeometry message:geometryMessage.toRawUTF8()];
  layoutTopCards(self.editorController, self.textView);
  [self check:[self.textView.text isEqualToString:originalText] && cardHeaderIdentities(self.editorController.view).count == 1
       message:"a partial-line selection creates one full-line card without inserting a heading into lyric text"];
  [self.editorController applySection:"chorus" range:[originalText rangeOfString:@"Segunda"]
      revision:[[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue]];
  layoutTopCards(self.editorController, self.textView);
  [self check:cardHeaderIdentities(self.editorController.view).count == 2 &&
              [self.textView.text isEqualToString:originalText]
       message:"two distinct lyric sections render as two cards over the unchanged shared text"];
  [self check:!identifiedView(self.editorController.view, @"nativeLyricsSections").hidden
       message:"marking sections reveals native section navigation"];
  auto *strip = (UIScrollView *)identifiedView(self.editorController.view, @"nativeLyricsSections");
  NSArray<UIView *> *chips = identifiedViews(strip, @"nativeLyricsSection:");
  [self check:chips.count == 2 message:"two real native chips correspond to two section cards"];
  for (UIView *view in chips) {
    UIButton *button = (UIButton *)view;
    [self check:[button isKindOfClass:UIButton.class] && button.bounds.size.width >= 44 &&
                fabs(button.bounds.size.width - button.bounds.size.height) <= 1 &&
                button.accessibilityLabel.length > 0
         message:"circular navigation chips retain a full accessible section name and a touch-sized target"];
  }
  UIButton *firstChip = (UIButton *)chips.firstObject;
  UIButton *lastChip = (UIButton *)chips.lastObject;
  [firstChip sendActionsForControlEvents:UIControlEventTouchUpInside];
  [self check:NSEqualRanges(self.textView.selectedRange, NSMakeRange(firstMarkedRange.location, 0)) && firstChip.selected && !lastChip.selected
       message:"first chip moves the insertion point to its card without selecting lyric text"];
  [lastChip sendActionsForControlEvents:UIControlEventTouchUpInside];
  [self check:NSEqualRanges(self.textView.selectedRange, NSMakeRange(secondMarkedRange.location, 0)) && lastChip.selected && !firstChip.selected
       message:"second chip navigates to its own card and moves the active indication"];
  // Header views are virtualized; inspect the first two with their document
  // region visible rather than assuming offscreen accessibility subviews exist.
  layoutTopCards(self.editorController, self.textView);
  NSArray<NSString *> *stableHeaders = cardHeaderIdentities(self.editorController.view);
  NSArray<UIView *> *headers = orderedCardHeaders(self.editorController.view, self.textView);
  [self check:headers.count == 2 && headers.firstObject.frame.size.height > 0 && headers.lastObject.frame.size.height > 0 &&
              CGRectGetMaxY(headerDocumentFrame(headers.firstObject, self.textView)) <= CGRectGetMinY(headerDocumentFrame(headers.lastObject, self.textView))
       message:"card headings have visible non-overlapping native layout frames"];
  const CGRect firstHeaderFrame = headerDocumentFrame(headers.firstObject, self.textView);
  [self check:CGRectGetMinY(firstHeaderFrame) >= CGRectGetMinY(self.textView.bounds) &&
              CGRectGetMaxY(firstHeaderFrame) <= CGRectGetMaxY(self.textView.bounds)
       message:"the first section heading is fully visible above its lyric body without clipping at the document start"];
  for (UIView *header in headers) {
    const CGRect frame = [header.superview convertRect:header.frame toView:self.textView];
    [self check:CGRectGetMinX(frame) >= 0 && CGRectGetMaxX(frame) <= self.textView.bounds.size.width + 1
         message:"card headings fit inside the editor width without horizontal clipping"];
  }
  // A body that wraps must grow its card and move following headings; no fixed
  // per-card editor height can crop those additional lines.
  const CGFloat nextHeaderBefore = headerDocumentFrame(headers.lastObject, self.textView).origin.y;
  self.textView.selectedRange = NSMakeRange(8, 0);
  [self.textView.undoManager beginUndoGrouping];
  [self.textView insertText:@" muchas palabras para comprobar que la tarjeta crece con varias líneas de texto y mantiene visible toda la letra "];
  [self.textView.undoManager endUndoGrouping];
  layoutTopCards(self.editorController, self.textView);
  headers = orderedCardHeaders(self.editorController.view, self.textView);
  [self check:headers.count == 2 && headerDocumentFrame(headers.lastObject, self.textView).origin.y > nextHeaderBefore + self.textView.font.lineHeight &&
              [cardHeaderIdentities(self.editorController.view) isEqualToArray:stableHeaders]
       message:"wrapping lyric text grows its card and keeps stable section identities"];
  [self.textView.undoManager undo]; layoutTopCards(self.editorController, self.textView);
  [self check:[self.textView.text isEqualToString:originalText] &&
              [cardHeaderIdentities(self.editorController.view) isEqualToArray:stableHeaders]
       message:"native Undo returns the original card sizes and section identities after wrapping"];
  // UTF16 insertion before the next card must shift its native range by two,
  // then native Undo/Redo must restore both exact text and metadata identities.
  self.textView.selectedRange = NSMakeRange(3, 0);
  [self.textView.undoManager beginUndoGrouping]; [self.textView insertText:@"🎸"]; [self.textView.undoManager endUndoGrouping];
  layoutTopCards(self.editorController, self.textView);
  chips = identifiedViews(strip, @"nativeLyricsSection:");
  [(UIButton *)chips.lastObject sendActionsForControlEvents:UIControlEventTouchUpInside];
  [self check:self.textView.selectedRange.location == secondMarkedRange.location + 2 &&
              self.textView.selectedRange.length == 0
       message:"Unicode editing shifts following card navigation in native UTF16 units"];
  [self.textView.undoManager undo]; layoutTopCards(self.editorController, self.textView);
  [self check:[self.textView.text isEqualToString:originalText] &&
              [cardHeaderIdentities(self.editorController.view) isEqualToArray:stableHeaders]
       message:"Unicode Undo restores text and both card identities"];
  [self.textView.undoManager redo]; layoutTopCards(self.editorController, self.textView);
  [self check:[self.textView.text containsString:@"Pri🎸mera"] &&
              [cardHeaderIdentities(self.editorController.view) isEqualToArray:stableHeaders]
       message:"Unicode Redo retains both section metadata snapshots"];
  [self.textView.undoManager undo]; layoutTopCards(self.editorController, self.textView);

  self.textView.selectedRange = NSMakeRange(self.textView.text.length, 0);
  NSString *before = [self.textView.text copy];
  [self.textView.undoManager beginUndoGrouping]; [self.textView insertText:@"Native undo suffix"]; [self.textView.undoManager endUndoGrouping];
  [self check:[self.textView.text hasSuffix:@"Native undo suffix"] && self.textView.undoManager.canUndo
       message:"native editing registers its own undo operation after card formatting"];
  [self.textView.undoManager undo]; layoutTopCards(self.editorController, self.textView);
  [self check:[self.textView.text isEqualToString:before] message:"card layout preserves UIKit native text Undo"];
  [self check:[cardHeaderIdentities(self.editorController.view) isEqualToArray:stableHeaders]
       message:"native text Undo retains unaffected lyric cards"];
  NSString *labelledText = [self.textView.text copy];
  self.textView.selectedRange = NSMakeRange(0, self.textView.text.length);
  [self.textView.undoManager beginUndoGrouping]; [self.textView insertText:@""]; [self.textView.undoManager endUndoGrouping];
  layoutTopCards(self.editorController, self.textView);
  [self check:self.textView.text.length == 0 && identifiedView(self.editorController.view, @"nativeLyricsSections").hidden &&
              cardHeaderIdentities(self.editorController.view).count == 0
       message:"deleting all labelled text removes its cards and navigation"];
  [self.textView.undoManager undo]; layoutTopCards(self.editorController, self.textView);
  [self check:[self.textView.text isEqualToString:labelledText] &&
              [cardHeaderIdentities(self.editorController.view) isEqualToArray:stableHeaders] &&
              !identifiedView(self.editorController.view, @"nativeLyricsSections").hidden
       message:"native Undo restores deleted text and every original card identity"];
  [self.textView.undoManager redo]; layoutTopCards(self.editorController, self.textView);
  [self check:self.textView.text.length == 0 && identifiedView(self.editorController.view, @"nativeLyricsSections").hidden
       message:"native Redo removes the same text and cards again"];
  [self.textView.undoManager undo]; layoutTopCards(self.editorController, self.textView);
  [self check:[self.textView.text isEqualToString:labelledText] &&
              [cardHeaderIdentities(self.editorController.view) isEqualToArray:stableHeaders]
       message:"repeated native Undo retains original card identity and content"];
  [self.editorController applySection:juce::String() range:NSMakeRange(0, 7)
      revision:[[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue]];
  self.textView.selectedRange = NSMakeRange(self.textView.text.length, 0);
  [self.textView.undoManager beginUndoGrouping]; [self.textView insertText:@"!"]; [self.textView.undoManager endUndoGrouping];
  [self.editorController applySection:"verse" range:NSMakeRange(0, 7)
      revision:[[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue]];
  [self.textView.undoManager undo]; layoutTopCards(self.editorController, self.textView);
  [self check:[self.textView.text isEqualToString:labelledText] && cardHeaderIdentities(self.editorController.view).count == 2
       message:"Undo of earlier typing preserves a card marked after that typing"];
  [self.textView.undoManager redo]; layoutTopCards(self.editorController, self.textView);
  [self check:[self.textView.text hasSuffix:@"!"] && cardHeaderIdentities(self.editorController.view).count == 2
       message:"Redo of earlier typing also preserves the newer card"];
  [self.textView.undoManager undo]; layoutTopCards(self.editorController, self.textView);
  if (@available(iOS 15.0, *)) {
    const CGRect frame = [self.textView.superview convertRect:self.textView.frame toView:self.editorController.view];
    [self check:CGRectGetMaxY(frame) <= CGRectGetMinY(self.editorController.view.keyboardLayoutGuide.layoutFrame) + 1
         message:"the shared lyric editor ends above the native keyboard guide"];
  }
  [self.editorController savePressed];
  [self check:!self.textView.editable && !self.editorController.navigationItem.leftBarButtonItem.enabled &&
              !self.editorController.navigationItem.rightBarButtonItem.enabled
       message:"pending persistence keeps the editor visible and disables editing and duplicate actions"];
  later(0.3, [self] { [self protected:[self] { [self checkSavingFailure]; }]; });
}
- (void)checkSavingFailure {
  [self check:saves == 1 && firstCompletion && saved && saved->sections.size() == 2 && saved->text == source.text &&
              saved->sections[0].kind == "verse" && saved->sections[0].start == static_cast<int>(firstMarkedRange.location) &&
              saved->sections[0].length == static_cast<int>(firstMarkedRange.length) &&
              saved->sections[1].kind == "chorus" && saved->sections[1].start == static_cast<int>(secondMarkedRange.location) &&
              saved->sections[1].length == static_cast<int>(secondMarkedRange.length)
       message:"public Save receives unchanged text and two complete-paragraph UTF16 section ranges"];
  [self check:closes == 0 message:"an unfinished save does not close the editor"];
  firstCompletion(false, "Intentional persistence failure");
  later(0.4, [self] { [self protected:[self] {
    [self check:self.textView.editable && self.editorController.presentedViewController != nil && closes == 0
         message:"save failure retains the native draft and presents a native error"];
    firstCompletion(true, {});
    later(0.1, [self] { [self protected:[self] {
      [self check:closes == 0 message:"a duplicated old completion cannot close the retained draft"];
      [self.editorController.presentedViewController dismissViewControllerAnimated:NO completion:^{
        [self.editorController savePressed];
        later(0.2, [self] { [self protected:[self] { [self checkSavingSuccess]; }]; });
      }];
    }]; });
  }]; });
}
- (void)checkSavingSuccess {
  [self check:saves == 2 && secondCompletion message:"retained draft can be saved again after failure"];
  auto completion = secondCompletion;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ completion(true, {}); });
  [self eventually:[self] { return closes == 1 && self.window.rootViewController.presentedViewController == nil; }
      message:"successful background completion dismisses once and then reports Close" then:[self] {
    handle.reset(); self.editorController = nil; self.textView = nil;
    [self check:source.sections.empty() message:"native marking never mutates the caller's source document"];
    [self checkCancellation];
  }];
}
- (void)checkCancellation {
  NativeSongLyricsEditor::Options options;
  options.title = "Cancel fixture";
  handle = NativeSongLyricsEditor::present(*owner, options, [](auto, auto) {}, [self] { ++closes; });
  [self check:handle != nullptr message:"empty song opens a native blank editor"];
  later(0.6, [self] { [self protected:[self] {
    self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
    self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
    [self check:self.textView.text.length == 0 && !identifiedView(self.editorController.view, @"nativeLyricsPlaceholder").hidden
         message:"blank editor shows its placeholder without sample lyrics"];
    NSArray *clipboard = UIPasteboard.generalPasteboard.items;
    UIPasteboard.generalPasteboard.string = @"Letra pegada 🎹\nSegunda línea de canción";
    [self.textView becomeFirstResponder];
    [self.textView paste:nil];
    [self eventually:[self] { return [self.textView.text isEqualToString:@"Letra pegada 🎹\nSegunda línea de canción"]; } timeout:120.0
        message:"native UIPasteboard paste inserts multiline Unicode lyrics" then:[self, clipboard] {
      [self.editorController applySection:"verse" range:NSMakeRange(0, self.textView.text.length)
          revision:[[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue]];
      [self.textView.undoManager undo];
      [self check:self.textView.text.length == 0
           message:"pasted lyrics use native Undo without leaving text or section markup"];
      [self.textView.undoManager redo];
      layoutCards(self.editorController, self.textView);
      [self check:[self.textView.text isEqualToString:@"Letra pegada 🎹\nSegunda línea de canción"] &&
                  cardHeaderIdentities(self.editorController.view).count == 1
           message:"Redo of a paste restores its card marked after the original paste"];
      [self.textView.undoManager undo];
      UIPasteboard.generalPasteboard.items = clipboard;
      [self.textView insertText:@"Unsaved native lyrics"];
      [self.editorController closePressed];
      later(0.1, [self] { [self protected:[self] {
        [self check:[self.editorController.presentedViewController isKindOfClass:UIAlertController.class] && closes == 1
             message:"closing a modified draft asks before discarding"];
        handle->cancel(); handle.reset();
        [self eventually:[self] { return self.window.rootViewController.presentedViewController == nil; }
            message:"owner cancellation dismisses silently even while a confirmation is open" then:[self] {
          [self check:closes == 1 message:"owner cancellation never emits the user Close callback"];
          self.editorController = nil; self.textView = nil;
          [self checkUnchangedClose];
        }];
      }]; });
    }];
  }]; });
}
- (void)checkUnchangedClose {
  handle = NativeSongLyricsEditor::present(*owner, {}, [](auto, auto) {}, [self] { ++closes; });
  [self check:handle != nullptr message:"editor can be opened after cancellation"];
  later(0.5, [self] { [self protected:[self] {
    auto *controller = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
    [controller closePressed];
    [self eventually:[self] { return closes == 2 && self.window.rootViewController.presentedViewController == nil; }
        message:"unchanged Close dismisses once without a discard prompt or save" then:[self] {
      handle.reset(); self.editorController = nil; self.textView = nil;
      [self checkParagraphSelection];
    }];
  }]; });
}
- (void)checkParagraphSelection {
  NSMutableString *native = [NSMutableString stringWithString:@"Intro 🎹\r\nLínea uno del verso\nLínea dos del verso\n"];
  for (int i = 0; i < 45; ++i) [native appendFormat:@"Zona sin sección %d\n", i];
  [native appendString:@"Coro final 🎸\n"];
  NSString *fixture = [native copy];
  SongLyricsDocument next;
  next.text = juce::String::fromUTF8(fixture.UTF8String);
  NativeSongLyricsEditor::Options options;
  options.title = "Paragraph and scroll fixture"; options.document = next;
  saved.reset();
  const int closeBaseline = closes;
  handle = NativeSongLyricsEditor::present(*owner, options,
      [self](auto document, auto completion) { saved = std::move(document); completion(true, {}); },
      [self] { ++closes; });
  [self check:handle != nullptr message:"paragraph fixture opens the real native editor"];
  later(0.6, [self, fixture, closeBaseline] { [self protected:[self, fixture, closeBaseline] {
    self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
    self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
    const NSRange introSelection = [fixture rangeOfString:@"🎹"];
    const NSRange verseWord = [fixture rangeOfString:@"uno"];
    const NSUInteger followingLine = [fixture rangeOfString:@"Zona sin sección 0"].location;
    const NSRange verseSelection = NSMakeRange(verseWord.location, followingLine - verseWord.location);
    const NSRange chorusSelection = [fixture rangeOfString:@"final 🎸"];
    const NSRange introRange = completeParagraphs(fixture, introSelection);
    const NSRange verseRange = completeParagraphs(fixture, verseSelection);
    const NSRange chorusRange = completeParagraphs(fixture, chorusSelection);
    [self check:introRange.length == [@"Intro 🎹" length] && NSMaxRange(verseRange) < followingLine
         message:"test fixture has a CRLF terminator and a selection ending exactly at a later paragraph boundary"];
    [self.editorController applySection:"intro" range:introSelection
        revision:[[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue]];
    [self.editorController applySection:"verse" range:verseSelection
        revision:[[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue]];
    [self.editorController applySection:"chorus" range:chorusSelection
        revision:[[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue]];
    layoutCards(self.editorController, self.textView);
    [self check:[self.textView.text isEqualToString:fixture] && cardHeaderIdentities(self.editorController.view).count >= 2
         message:"partial Unicode and multiline selections create visible leading cards without editing the lyric text or its separators"];
    auto *strip = identifiedView(self.editorController.view, @"nativeLyricsSections");
    NSArray<UIView *> *chips = identifiedViews(strip, @"nativeLyricsSection:");
    [self check:chips.count == 3 && self.textView.contentSize.height > self.textView.bounds.size.height
         message:"long unlabelled passages remain in the shared scrollable document between section cards"];
    [self.textView becomeFirstResponder];
    // Let the native keyboard finish resizing its layout guide before the
    // explicit navigation computes a target in the final editor viewport.
    later(0.5, [self, fixture, introRange, verseRange, chorusRange, closeBaseline, chips] { [self protected:[self, fixture, introRange, verseRange, chorusRange, closeBaseline, chips] {
      NSString *lastSectionIdentifier = chips.lastObject.accessibilityIdentifier;
      NSString *lastSectionID = [lastSectionIdentifier stringByReplacingOccurrencesOfString:@"nativeLyricsSection:" withString:@""];
      self.textView.selectedRange = NSMakeRange(fixture.length, 0);
      NSParagraphStyle *outsideStyle = self.textView.typingAttributes[NSParagraphStyleAttributeName];
      [self check:outsideStyle != nil && outsideStyle.headIndent == 0 && outsideStyle.firstLineHeadIndent == 0 &&
                  outsideStyle.tailIndent == 0 && outsideStyle.paragraphSpacingBefore == 0
           message:"an empty final paragraph outside a card receives normal typing geometry"];
      [self.textView.undoManager beginUndoGrouping]; [self.textView insertText:@"x"]; [self.textView.undoManager endUndoGrouping];
      [self check:[self.textView.text hasSuffix:@"\nx"] && NSEqualRanges(presentationRange(self.editorController, lastSectionID), chorusRange)
           message:"typing after a card's excluded final newline remains outside that section"];
      [self.textView.undoManager undo];
      [self check:[self.textView.text isEqualToString:fixture] && NSEqualRanges(presentationRange(self.editorController, lastSectionID), chorusRange)
           message:"Undo outside a card restores its trailing newline without extending the section"];
      NSString *lastHeaderIdentifier = [lastSectionIdentifier stringByReplacingOccurrencesOfString:@"nativeLyricsSection:" withString:@"nativeLyricsCardHeader:"];
      [(UIButton *)chips.lastObject sendActionsForControlEvents:UIControlEventTouchUpInside];
      [self eventually:[self, chorusRange, lastHeaderIdentifier, lastSectionIdentifier] {
        layoutCards(self.editorController, self.textView);
        UIView *last = identifiedView(self.editorController.view, lastHeaderIdentifier);
        UIButton *destination = (UIButton *)identifiedView(self.editorController.view, lastSectionIdentifier);
        if (last == nil || !NSEqualRanges(self.textView.selectedRange, NSMakeRange(chorusRange.location, 0))) return false;
        return destination.selected && CGRectContainsRect(CGRectInset(self.textView.bounds, -1, -1), headerDocumentFrame(last, self.textView));
      } message:"last chip scrolls to its visible card heading with a collapsed Unicode insertion point" then:[self, fixture, introRange, verseRange, chorusRange, closeBaseline] {
        NSArray<UIView *> *activeChips = identifiedViews(identifiedView(self.editorController.view, @"nativeLyricsSections"), @"nativeLyricsSection:");
        int selectedChips = 0;
        for (UIView *view in activeChips) if (((UIButton *)view).selected) ++selectedChips;
        if (selectedChips != 1 || !((UIButton *)activeChips.lastObject).selected) {
          NSMutableArray *states = [NSMutableArray new];
          for (UIView *view in activeChips) [states addObject:@{@"id":view.accessibilityIdentifier ?: @"", @"selected":@(((UIButton *)view).selected)}];
          NSLog(@"LYRICS_NAV_GEOMETRY %@ chips=%@", editingGeometry(self.textView), states);
        }
        [self check:selectedChips == 1 && ((UIButton *)activeChips.lastObject).selected
             message:"scroll navigation exposes one active circular section chip"];
        [self.editorController savePressed];
        [self eventually:[self, closeBaseline] { return saved.has_value() && closes == closeBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
            message:"the paragraph fixture saves and closes through the public bridge" then:[self, fixture, introRange, verseRange, chorusRange] {
          [self check:saved && saved->text == juce::String::fromUTF8(fixture.UTF8String) && saved->sections.size() == 3
               message:"card layout serializes the original shared lyric text with exactly three ranges"];
          [self check:saved->sections[0].kind == "intro" && saved->sections[0].start == static_cast<int>(introRange.location) &&
                      saved->sections[0].length == static_cast<int>(introRange.length)
               message:"a Unicode selection expands to its full first paragraph without consuming CRLF"];
          [self check:saved->sections[1].kind == "verse" && saved->sections[1].start == static_cast<int>(verseRange.location) &&
                      saved->sections[1].length == static_cast<int>(verseRange.length)
               message:"a multiline selection ending at the next paragraph start excludes that following paragraph"];
          [self check:saved->sections[2].kind == "chorus" && saved->sections[2].start == static_cast<int>(chorusRange.location) &&
                      saved->sections[2].length == static_cast<int>(chorusRange.length) && isValidSongLyricsDocument(*saved)
               message:"the last card retains valid native UTF16 boundaries after scroll and selection"];
          handle.reset(); self.editorController = nil; self.textView = nil;
          [self checkBoundaryTyping];
        }];
      }];
    }]; });
  }]; });
}
- (void)checkBoundaryTyping {
  NSString *fixture = @"Texto anterior sin sección\nÚltima línea 🎹";
  const NSRange originalRange = [fixture rangeOfString:@"Última línea 🎹"];
  SongLyricsDocument boundary;
  boundary.text = juce::String::fromUTF8(fixture.UTF8String);
  boundary.sections.push_back({"boundary-card", "chorus", static_cast<int>(originalRange.location), static_cast<int>(originalRange.length)});
  NativeSongLyricsEditor::Options options;
  options.title = "Card boundary typing fixture"; options.document = boundary;
  saved.reset();
  const int closeBaseline = closes;
  handle = NativeSongLyricsEditor::present(*owner, options,
      [self](auto document, auto completion) { saved = std::move(document); completion(true, {}); },
      [self] { ++closes; });
  [self check:handle != nullptr message:"last-card boundary fixture opens the real native editor"];
  later(0.6, [self, fixture, originalRange, closeBaseline] { [self protected:[self, fixture, originalRange, closeBaseline] {
    self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
    self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
    [self.textView becomeFirstResponder];
    layoutTopCards(self.editorController, self.textView);
    NSArray<NSString *> *stableHeaders = cardHeaderIdentities(self.editorController.view);
    [self check:stableHeaders.count == 1 && [stableHeaders.firstObject isEqualToString:@"nativeLyricsCardHeader:boundary-card"]
         message:"the fixture begins with one complete last-paragraph card and its saved identity"];
    auto expectCard = [self, stableHeaders](NSRange range, const char *message) {
      layoutTopCards(self.editorController, self.textView);
      UIButton *chip = (UIButton *)identifiedView(self.editorController.view, @"nativeLyricsSection:boundary-card");
      [chip sendActionsForControlEvents:UIControlEventTouchUpInside];
      layoutTopCards(self.editorController, self.textView);
      [self check:chip != nil && NSEqualRanges(presentationRange(self.editorController, @"boundary-card"), range) &&
                  NSEqualRanges(self.textView.selectedRange, NSMakeRange(range.location, 0)) &&
                  [cardHeaderIdentities(self.editorController.view) isEqualToArray:stableHeaders] message:message];
    };
    NSString *prefix = @"Inicio ";
    self.textView.selectedRange = NSMakeRange(originalRange.location, 0);
    [self.textView.undoManager beginUndoGrouping]; insertComposedText(self.textView, prefix); [self.textView.undoManager endUndoGrouping];
    expectCard(NSMakeRange(originalRange.location, originalRange.length + prefix.length),
        "typing character-by-character at a complete card start extends its exact UTF16 range and keeps its header ID");
    [self.textView.undoManager undo];
    expectCard(originalRange, "Undo of start-boundary typing restores the original card range and identity");
    [self check:[self.textView.text isEqualToString:fixture] message:"Undo of start-boundary typing restores the original lyric text"];
    [self.textView.undoManager redo];
    expectCard(NSMakeRange(originalRange.location, originalRange.length + prefix.length),
        "Redo of start-boundary typing restores the enlarged card without changing its identity");
    [self.textView.undoManager undo];
    NSString *suffix = @" final";
    self.textView.selectedRange = NSMakeRange(NSMaxRange(originalRange), 0);
    [self.textView.undoManager beginUndoGrouping]; insertComposedText(self.textView, suffix); [self.textView.undoManager endUndoGrouping];
    expectCard(NSMakeRange(originalRange.location, originalRange.length + suffix.length),
        "typing character-by-character at the last card end remains inside the same complete card");
    [self.textView.undoManager undo];
    expectCard(originalRange, "Undo of end-boundary typing restores the original range and header ID");
    [self.textView.undoManager redo];
    expectCard(NSMakeRange(originalRange.location, originalRange.length + suffix.length),
        "Redo of end-boundary typing restores the enlarged range and stable card identity");
    [self.textView.undoManager undo];
    NSString *continuation = @"\nNueva línea 🎸";
    self.textView.selectedRange = NSMakeRange(NSMaxRange(originalRange), 0);
    [self.textView.undoManager beginUndoGrouping]; insertComposedText(self.textView, continuation); [self.textView.undoManager endUndoGrouping];
    const NSRange continuedRange = NSMakeRange(originalRange.location, originalRange.length + continuation.length);
    expectCard(continuedRange, "a newline and following Unicode text continue the last card across paragraphs while preserving its identity");
    [self.textView.undoManager undo];
    expectCard(originalRange, "Undo of a newline continuation restores the original single-paragraph card");
    [self check:[self.textView.text isEqualToString:fixture] message:"Undo of newline continuation restores original text including Unicode"];
    [self.textView.undoManager redo];
    expectCard(continuedRange, "Redo restores the multiline continuation with its exact UTF16 range and original card ID");
    NSString *continuedText = [fixture stringByAppendingString:continuation];
    [self check:[self.textView.text isEqualToString:continuedText] message:"boundary edits append only the characters typed by the user"];
    [self.editorController savePressed];
    [self eventually:[self, closeBaseline] { return saved.has_value() && closes == closeBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
        message:"boundary typing fixture saves through the public native bridge" then:[self, continuedText, continuedRange] {
      [self check:saved && saved->text == juce::String::fromUTF8(continuedText.UTF8String) && saved->sections.size() == 1 &&
                  saved->sections[0].id == "boundary-card" && saved->sections[0].kind == "chorus" &&
                  saved->sections[0].start == static_cast<int>(continuedRange.location) &&
                  saved->sections[0].length == static_cast<int>(continuedRange.length) && isValidSongLyricsDocument(*saved)
           message:"saved boundary typing preserves one valid multiline section with the original identity and exact UTF16 extent"];
      handle.reset(); self.editorController = nil; self.textView = nil;
      [self checkEditingFluency];
    }];
  }]; });
}
- (void)checkEditingFluency {
  NSMutableString *native = [NSMutableString stringWithString:@"Primera línea del verso\nSegunda línea del verso\n\n"];
  for (int i = 0; i < 45; ++i) [native appendFormat:@"Párrafo lejano sin sección %d\n", i];
  [native appendString:@"Coro final que continúa"];
  NSString *fixture = [native copy];
  const NSRange first = completeParagraphs(fixture, [fixture rangeOfString:@"Primera línea del verso\nSegunda línea del verso"]);
  const NSRange last = [fixture rangeOfString:@"Coro final que continúa"];
  SongLyricsDocument editable;
  editable.text = juce::String::fromUTF8(fixture.UTF8String);
  editable.sections.push_back({"fluency-verse", "verse", static_cast<int>(first.location), static_cast<int>(first.length)});
  editable.sections.push_back({"fluency-chorus", "chorus", static_cast<int>(last.location), static_cast<int>(last.length)});
  NativeSongLyricsEditor::Options options;
  options.title = "Native editing fluency fixture"; options.document = editable;
  saved.reset();
  const int closeBaseline = closes;
  handle = NativeSongLyricsEditor::present(*owner, options,
      [self](auto document, auto completion) { saved = std::move(document); completion(true, {}); },
      [self] { ++closes; });
  [self check:handle != nullptr message:"native fluency fixture opens the shared editor"];
  later(0.6, [self, fixture, first, last, closeBaseline] { [self protected:[self, fixture, first, last, closeBaseline] {
    self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
    self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
    UIButton *firstChip = (UIButton *)identifiedView(self.editorController.view, @"nativeLyricsSection:fluency-verse");
    [firstChip sendActionsForControlEvents:UIControlEventTouchUpInside];
    [self check:!self.textView.isFirstResponder && NSEqualRanges(self.textView.selectedRange, NSMakeRange(first.location, 0))
         message:"section navigation leaves the keyboard hidden and never selects a whole block"];
    [self.textView becomeFirstResponder];
    later(0.5, [self, fixture, first, last, closeBaseline] { [self protected:[self, fixture, first, last, closeBaseline] {
      self.textView.selectedRange = NSMakeRange(3, 0);
      __block BOOL touchedDistantAttributes = NO;
      id observer = [NSNotificationCenter.defaultCenter addObserverForName:NSTextStorageDidProcessEditingNotification
          object:self.textView.textStorage queue:nil usingBlock:^(NSNotification *notification) {
        NSTextStorage *storage = notification.object;
        if ((storage.editedMask & NSTextStorageEditedAttributes) != 0 &&
            storage.editedRange.location != NSNotFound && NSMaxRange(storage.editedRange) > last.location)
          touchedDistantAttributes = YES;
      }];
      [self.textView insertText:@"x"];
      [NSNotificationCenter.defaultCenter removeObserver:observer];
      NSParagraphStyle *typing = self.textView.typingAttributes[NSParagraphStyleAttributeName];
      [self check:typing != nil && typing.headIndent >= 11 && typing.tailIndent <= -11 && typing.lineSpacing >= 2.9
           message:"typing inside a card retains its paragraph geometry without a normal-style insertion flash"];
      [self check:!touchedDistantAttributes
           message:"editing one paragraph does not reapply presentation attributes to distant lyric paragraphs"];
      UITextPosition *position = [self.textView positionFromPosition:self.textView.beginningOfDocument offset:2];
      UITextRange *word = [self.textView.tokenizer rangeEnclosingPosition:position withGranularity:UITextGranularityWord
          inDirection:UITextStorageDirectionForward];
      self.textView.selectedTextRange = word;
      const NSRange wordSelection = self.textView.selectedRange;
      NSString *selectedWord = [self.textView textInRange:word];
      layoutCards(self.editorController, self.textView);
      [self check:word != nil && wordSelection.length > 0 && NSEqualRanges(self.textView.selectedRange, wordSelection) &&
                  [[self.textView textInRange:self.textView.selectedTextRange] isEqualToString:selectedWord]
           message:"UIKit word selection remains exact while decorative cards lay out"];
      self.textView.selectedRange = NSMakeRange(NSMaxRange(wordSelection), 0);
      [self.textView setMarkedText:@" canci" selectedRange:NSMakeRange(6, 0)];
      [self check:self.textView.markedTextRange != nil &&
                  [[self.textView textInRange:self.textView.markedTextRange] isEqualToString:@" canci"]
           message:"native composition retains its marked range while a card is being edited"];
      [self.textView setMarkedText:@" canción" selectedRange:NSMakeRange(8, 0)];
      [self check:self.textView.markedTextRange != nil &&
                  [[self.textView textInRange:self.textView.markedTextRange] isEqualToString:@" canción"] && self.textView.isFirstResponder
           message:"updating an accented composition preserves the native candidate and keyboard focus"];
      [self.textView setMarkedText:@"ción" selectedRange:NSMakeRange(4, 0)];
      [self check:self.textView.markedTextRange != nil &&
                  [[self.textView textInRange:self.textView.markedTextRange] isEqualToString:@"ción"]
           message:"shortening an active composition preserves its native marked text range"];
      [self.textView unmarkText];
      [self check:self.textView.markedTextRange == nil && [self.textView.text containsString:@"ción"]
           message:"committing native composition keeps its accented text"];
      const NSUInteger lastStart = [self.textView.text rangeOfString:@"Coro final que continúa"].location;
      self.textView.selectedRange = NSMakeRange(self.textView.text.length, 0);
      [self.textView scrollRangeToVisible:self.textView.selectedRange];
      NSString *beforeTyping = [self.textView.text copy];
      NSMutableString *continuation = [NSMutableString string];
      for (int i = 0; i < 8; ++i) [continuation appendFormat:@"\nLa canción continúa en esta línea %d", i];
      NSString *typed = [continuation copy];
      NSMutableArray<NSString *> *characters = [NSMutableArray new];
      [typed enumerateSubstringsInRange:NSMakeRange(0, typed.length) options:NSStringEnumerationByComposedCharacterSequences
          usingBlock:^(NSString *substring, NSRange substringRange, NSRange enclosingRange, BOOL *stop) {
        (void)substringRange; (void)enclosingRange; (void)stop; [characters addObject:substring];
      }];
      insertOverRunLoop(self.textView, characters, 0, [self, beforeTyping, typed, lastStart, last, closeBaseline] {
        later(0.3, [self, beforeTyping, typed, lastStart, last, closeBaseline] { [self protected:[self, beforeTyping, typed, lastStart, last, closeBaseline] {
          const CGRect caret = [self.textView caretRectForPosition:self.textView.selectedTextRange.end];
          [self check:self.textView.isFirstResponder && self.textView.selectedRange.length == 0 &&
                      CGRectContainsRect(CGRectInset(self.textView.bounds, -1, -1), caret)
               message:"continued native typing scrolls its caret into view above the keyboard"];
          NSString *expected = [beforeTyping stringByAppendingString:typed];
          [self check:[self.textView.text isEqualToString:expected]
               message:"typing over successive run-loop turns adds only the requested lyric characters"];
          [self.editorController savePressed];
          [self eventually:[self, closeBaseline] { return saved.has_value() && closes == closeBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
              message:"fluent typing saves and closes through the public bridge" then:[self, expected, lastStart, last, typed] {
            [self check:saved && saved->text == juce::String::fromUTF8(expected.UTF8String) && saved->sections.size() == 2 &&
                        saved->sections[1].id == "fluency-chorus" && saved->sections[1].start == static_cast<int>(lastStart) &&
                        saved->sections[1].length == static_cast<int>(last.length + typed.length) && isValidSongLyricsDocument(*saved)
                 message:"typing and composition preserve persisted section identities and exact UTF16 extents"];
            handle.reset(); self.editorController = nil; self.textView = nil;
            [self checkEmptyLineContinuation:NO];
          }];
        }]; });
      });
    }]; });
  }]; });
}
// Inspect blank lines before typing any following lyric character: the old
// boundary tests typed a whole continuation and then navigated, hiding the
// instant where the caret had already escaped its card.
- (void)checkEmptyLineContinuation:(BOOL)hasFollowingSection {
  NSString *body = @"Última línea 🎹";
  NSString *following = @"Siguiente verso intacto";
  NSString *fixture = [@"Texto anterior sin sección\n" stringByAppendingString:body];
  if (hasFollowingSection) fixture = [fixture stringByAppendingFormat:@"\n\n%@", following];
  const NSRange originalRange = [fixture rangeOfString:body];
  const NSRange followingRange = hasFollowingSection ? [fixture rangeOfString:following] : NSMakeRange(NSNotFound, 0);
  SongLyricsDocument emptyLineDocument;
  emptyLineDocument.text = juce::String::fromUTF8(fixture.UTF8String);
  emptyLineDocument.sections.push_back({"empty-line-chorus", "chorus", static_cast<int>(originalRange.location), static_cast<int>(originalRange.length)});
  if (hasFollowingSection)
    emptyLineDocument.sections.push_back({"following-verse", "verse", static_cast<int>(followingRange.location), static_cast<int>(followingRange.length)});
  NativeSongLyricsEditor::Options options;
  options.title = hasFollowingSection ? "Empty line before following section" : "Empty line at document end";
  options.document = emptyLineDocument;
  saved.reset();
  const int closeBaseline = closes;
  handle = NativeSongLyricsEditor::present(*owner, options,
      [self](auto document, auto completion) { saved = std::move(document); completion(true, {}); },
      [self] { ++closes; });
  [self check:handle != nullptr message:"empty-line continuation fixture opens the shared native editor"];
  later(0.6, [self, fixture, originalRange, followingRange, following, emptyLineDocument, hasFollowingSection, closeBaseline] {
    [self protected:[self, fixture, originalRange, followingRange, following, emptyLineDocument, hasFollowingSection, closeBaseline] {
      self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
      self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
      [self.textView becomeFirstResponder];
      later(0.5, [self, fixture, originalRange, followingRange, following, emptyLineDocument, hasFollowingSection, closeBaseline] {
        [self protected:[self, fixture, originalRange, followingRange, following, emptyLineDocument, hasFollowingSection, closeBaseline] {
          self.textView.selectedRange = NSMakeRange(NSMaxRange(originalRange), 0);
          layoutCards(self.editorController, self.textView);
          auto expectInside = [self, originalRange](NSUInteger added, const char *message, BOOL exactCaret = YES) {
            const NSRange caretRange = NSMakeRange(NSMaxRange(originalRange) + added, 0);
            layoutCards(self.editorController, self.textView);
            const CGRect card = cardDocumentRect(self.editorController, self.textView, @"empty-line-chorus");
            const CGRect caret = [self.textView caretRectForPosition:self.textView.selectedTextRange.end];
            const NSRange actualRange = presentationRange(self.editorController, @"empty-line-chorus");
            const BOOL matches = !CGRectIsNull(card) && CGRectContainsRect(CGRectInset(card, -1, -1), caret) &&
                (!exactCaret || NSEqualRanges(self.textView.selectedRange, caretRange)) &&
                NSEqualRanges(actualRange, NSMakeRange(originalRange.location, originalRange.length + added));
            if (!matches) {
              NSLog(@"EMPTY_LINE_CONTINUATION actualText=%@ utf16Length=%lu addedExpected=%lu originalRange=%@ presentationRange=%@ selection=%@ caretExpected=%@ card=%@ caret=%@",
                  self.textView.text, static_cast<unsigned long>(self.textView.text.length), static_cast<unsigned long>(added),
                  NSStringFromRange(originalRange), NSStringFromRange(actualRange), NSStringFromRange(self.textView.selectedRange),
                  NSStringFromRange(caretRange), NSStringFromCGRect(card), NSStringFromCGRect(caret));
            }
            [self check:matches message:message];
            return card;
          };
          const CGRect before = expectInside(0, "the last written line begins with its caret and exact range inside its card");
          [self.textView.undoManager beginUndoGrouping]; [self.textView insertText:@"\n"]; [self.textView.undoManager endUndoGrouping];
          const CGRect oneBlank = expectInside(1, "Enter immediately keeps the blank-line caret inside the same section without typing or navigating");
          [self check:CGRectGetMaxY(oneBlank) >= CGRectGetMaxY(before) + self.textView.font.lineHeight - 1
               message:"the card grows by a text line immediately after Enter"];
          [self.textView.undoManager beginUndoGrouping]; [self.textView insertText:@"\n"]; [self.textView.undoManager endUndoGrouping];
          const CGRect twoBlank = expectInside(2, "repeated Enter keeps the second blank-line caret and exact UTF16 extent in the section");
          [self check:CGRectGetMaxY(twoBlank) >= CGRectGetMaxY(oneBlank) + self.textView.font.lineHeight - 1
               message:"a second empty line expands the card again before any lyric text is written"];
          [self.textView.undoManager beginUndoGrouping]; [self.textView insertText:@"   "]; [self.textView.undoManager endUndoGrouping];
          expectInside(5, "spaces on a new empty line retain the caret inside its existing section");
          NSString *beforeHistory = [self.textView.text copy];
          NSString *blankContinuation = @"\n\n   ";
          [self.textView.undoManager undo];
          // UIKit may coalesce consecutive insertText calls into one native
          // action even when the fixture brackets its undo groups. Derive the
          // surviving insertion from real text, never from section metadata.
          NSString *afterUndo = [self.textView.text copy];
          const BOOL validUndoLength = afterUndo.length >= fixture.length &&
              afterUndo.length <= fixture.length + blankContinuation.length;
          const NSUInteger surviving = validUndoLength ? afterUndo.length - fixture.length : 0;
          NSString *expectedUndo = [fixture stringByReplacingCharactersInRange:NSMakeRange(NSMaxRange(originalRange), 0)
              withString:[blankContinuation substringToIndex:surviving]];
          if (!validUndoLength || ![afterUndo isEqualToString:expectedUndo])
            NSLog(@"EMPTY_LINE_NATIVE_UNDO actual=%@ expected=%@", afterUndo, expectedUndo);
          [self check:validUndoLength && [afterUndo isEqualToString:expectedUndo]
               message:"native Undo restores an exact prefix of the inserted blank-line continuation"];
          expectInside(surviving, "Undo keeps section metadata and card geometry aligned with the actual surviving text", NO);
          [self.textView.undoManager redo];
          [self check:[self.textView.text isEqualToString:beforeHistory]
               message:"native Redo restores the exact text from before Undo regardless of insertion grouping"];
          expectInside(5, "Redo restores the expanded section and its native selection", NO);
          NSString *typed = @"\n\n   Nueva línea 🎹";
          self.textView.selectedRange = NSMakeRange(NSMaxRange(originalRange) + 5, 0);
          [self.textView.undoManager beginUndoGrouping]; [self.textView insertText:@"Nueva línea 🎹"]; [self.textView.undoManager endUndoGrouping];
          expectInside(typed.length, "writing after blank lines continues the same card with Unicode text");
          NSString *expectedText = [fixture stringByReplacingCharactersInRange:NSMakeRange(NSMaxRange(originalRange), 0) withString:typed];
          [self check:[self.textView.text isEqualToString:expectedText]
               message:"blank-line continuation adds only the requested characters to the document"];
          SongLyricsDocument expected = emptyLineDocument;
          expected.text = juce::String::fromUTF8(expectedText.UTF8String);
          expected.sections[0].length += static_cast<int>(typed.length);
          if (hasFollowingSection) {
            expected.sections[1].start += static_cast<int>(typed.length);
            const NSRange actualFollowing = presentationRange(self.editorController, @"following-verse");
            const CGRect currentCard = cardDocumentRect(self.editorController, self.textView, @"empty-line-chorus");
            const CGRect nextCard = cardDocumentRect(self.editorController, self.textView, @"following-verse");
            [self check:NSEqualRanges(actualFollowing, NSMakeRange(followingRange.location + typed.length, followingRange.length)) &&
                        [[self.textView.text substringWithRange:actualFollowing] isEqualToString:following] &&
                        CGRectGetMaxY(currentCard) <= CGRectGetMinY(nextCard)
                 message:"continuing an earlier card shifts the following section without absorbing its text or overlapping its card"];
          }
          const auto expectedJSON = juce::JSON::toString(songLyricsToVar(expected), true);
          [self.editorController savePressed];
          [self eventually:[self, closeBaseline] { return saved.has_value() && closes == closeBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
              message:"empty-line continuation saves and closes through the public native bridge" then:[self, expectedJSON, hasFollowingSection] {
            [self check:saved && isValidSongLyricsDocument(*saved) && juce::JSON::toString(songLyricsToVar(*saved), true) == expectedJSON
                 message:"saving preserves the expanded section identity, exact UTF16 ranges and untouched following content"];
            handle.reset(); self.editorController = nil; self.textView = nil;
            if (hasFollowingSection) [self checkSelectionGeometry];
            else [self checkEmptyLineContinuation:YES];
          }];
        }];
      });
    }];
  });
}
- (void)checkSelectionGeometry {
  NSString *body = @"Con esperanza seguimos\nTu luz acompaña el camino\nUna voz nos une al cantar\nCaminamos con alegría\nCada paso trae un comienzo\nJuntos volvemos a cantar";
  NSString *fixture = [@"Antes de la sección\n" stringByAppendingFormat:@"%@\nDespués de la sección", body];
  const NSRange cardRange = [fixture rangeOfString:body];
  SongLyricsDocument selectionDocument;
  selectionDocument.text = juce::String::fromUTF8(fixture.UTF8String);
  selectionDocument.sections.push_back({"selection-chorus", "chorus", static_cast<int>(cardRange.location), static_cast<int>(cardRange.length)});
  const auto expectedDocument = juce::JSON::toString(songLyricsToVar(selectionDocument), true);
  NativeSongLyricsEditor::Options options;
  options.title = "Selection geometry fixture"; options.document = selectionDocument;
  saved.reset();
  const int closeBaseline = closes;
  handle = NativeSongLyricsEditor::present(*owner, options,
      [self](auto document, auto completion) { saved = std::move(document); completion(true, {}); },
      [self] { ++closes; });
  [self check:handle != nullptr message:"multiline selection fixture opens the shared native editor"];
  later(0.6, [self, fixture, cardRange, expectedDocument, closeBaseline] { [self protected:[self, fixture, cardRange, expectedDocument, closeBaseline] {
    self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
    self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
    [self.textView becomeFirstResponder];
    later(0.5, [self, fixture, cardRange, expectedDocument, closeBaseline] { [self protected:[self, fixture, cardRange, expectedDocument, closeBaseline] {
      self.textView.selectedRange = cardRange;
      layoutCards(self.editorController, self.textView);
      NSParagraphStyle *firstStyle = [self.textView.textStorage attribute:NSParagraphStyleAttributeName atIndex:cardRange.location effectiveRange:nullptr];
      NSParagraphStyle *lastStyle = [self.textView.textStorage attribute:NSParagraphStyleAttributeName atIndex:NSMaxRange(cardRange) - 1 effectiveRange:nullptr];
      [self check:firstStyle.paragraphSpacingBefore >= 31 && lastStyle.paragraphSpacing >= 31
           message:"selection fixture includes large decorative spacing above and below its six-line card"];
      const CGRect startLine = glyphLineBox(self.textView, cardRange.location);
      const CGRect endLine = glyphLineBox(self.textView, NSMaxRange(cardRange));
      const CGRect startCaret = [self.textView caretRectForPosition:self.textView.selectedTextRange.start];
      const CGRect endCaret = [self.textView caretRectForPosition:self.textView.selectedTextRange.end];
      const CGFloat maximumHeight = self.textView.font.lineHeight + 2;
      [self check:startCaret.size.height > 0 && endCaret.size.height > 0 &&
                  startCaret.size.height <= maximumHeight && endCaret.size.height <= maximumHeight
           message:"card selection carets use the text line height instead of heading and footer spacing"];
      [self check:std::abs(startCaret.origin.y - startLine.origin.y) <= 2 && std::abs(endCaret.origin.y - endLine.origin.y) <= 2
           message:"selection carets align with the actual first and last glyph baselines"];
      NSArray<UITextSelectionRect *> *rects = [self.textView selectionRectsForRange:self.textView.selectedTextRange];
      BOOL foundStart = NO, foundEnd = NO, endpointsFit = YES;
      for (UITextSelectionRect *rect in rects) {
        if (!rect.containsStart && !rect.containsEnd) continue;
        const CGRect line = rect.containsStart ? startLine : endLine;
        foundStart |= rect.containsStart; foundEnd |= rect.containsEnd;
        endpointsFit &= rect.rect.size.height > 0 && rect.rect.size.height <= maximumHeight && std::abs(rect.rect.origin.y - line.origin.y) <= 2;
      }
      [self check:foundStart && foundEnd && endpointsFit
           message:"both selection handles stay within their text lines rather than decorative card gaps"];
      [self check:selectionAnnotationsMatch(rects, nativeSelectionRects(self.textView, self.textView.selectedTextRange))
           message:"selection geometry preserves UIKit endpoint flags, writing direction, transforms and horizontal extents"];
      const NSRange wordRange = [fixture rangeOfString:@"acompaña"];
      self.textView.selectedRange = wordRange;
      layoutCards(self.editorController, self.textView);
      [self check:NSEqualRanges(self.textView.selectedRange, wordRange) &&
                  [[self.textView textInRange:self.textView.selectedTextRange] isEqualToString:@"acompaña"] && self.textView.isFirstResponder
           message:"word selection remains exact and focused inside a decorated card"];
      NSArray<UITextSelectionRect *> *wordRects = [self.textView selectionRectsForRange:self.textView.selectedTextRange];
      [self check:wordRects.count > 0 && selectionAnnotationsMatch(wordRects, nativeSelectionRects(self.textView, self.textView.selectedTextRange))
           message:"word selection retains its native annotations and horizontal bounds"];
      [self check:[self.textView.text isEqualToString:fixture] && NSEqualRanges(presentationRange(self.editorController, @"selection-chorus"), cardRange)
           message:"selection rendering leaves lyric characters and section UTF16 ranges unchanged"];
      [self.editorController savePressed];
      [self eventually:[self, closeBaseline] { return saved.has_value() && closes == closeBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
          message:"selection geometry fixture saves through the public bridge" then:[self, expectedDocument] {
        [self check:saved && juce::JSON::toString(songLyricsToVar(*saved), true) == expectedDocument
             message:"saving after multiline and word selection preserves the complete original document"];
        handle.reset(); self.editorController = nil; self.textView = nil;
        [self checkLegacyPartialRange];
      }];
    }]; });
  }]; });
}
- (void)checkLegacyPartialRange {
  NSString *fixture = @"Inicio parcial 🎹 y cierre\nSegunda línea";
  const NSRange partial = [fixture rangeOfString:@"parcial 🎹"];
  SongLyricsDocument legacy;
  legacy.text = juce::String::fromUTF8(fixture.UTF8String);
  legacy.sections.push_back({"legacy-partial", "verse", static_cast<int>(partial.location), static_cast<int>(partial.length)});
  const auto originalJSON = juce::JSON::toString(songLyricsToVar(legacy), true);
  NativeSongLyricsEditor::Options options;
  options.title = "Legacy partial range fixture"; options.document = legacy;
  saved.reset();
  const int closeBaseline = closes;
  handle = NativeSongLyricsEditor::present(*owner, options,
      [self](auto document, auto completion) { saved = std::move(document); completion(true, {}); },
      [self] { ++closes; });
  [self check:handle != nullptr message:"legacy partial lyric ranges open without a migration"];
  later(0.6, [self, fixture, partial, originalJSON, closeBaseline] { [self protected:[self, fixture, partial, originalJSON, closeBaseline] {
    self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
    self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
    layoutCards(self.editorController, self.textView);
    [self check:[self.textView.text isEqualToString:fixture] && cardHeaderIdentities(self.editorController.view).count == 0 &&
                [self.textView.textStorage attribute:NSBackgroundColorAttributeName atIndex:partial.location effectiveRange:nullptr] != nil
         message:"legacy partial sections remain exact inline highlights instead of silently becoming full-paragraph cards"];
    UIButton *chip = (UIButton *)identifiedView(self.editorController.view, @"nativeLyricsSection:legacy-partial");
    [self check:chip != nil message:"a legacy partial section retains native chip navigation"];
    [chip sendActionsForControlEvents:UIControlEventTouchUpInside];
    [self check:NSEqualRanges(self.textView.selectedRange, NSMakeRange(partial.location, 0)) && !self.textView.isFirstResponder
         message:"legacy chip navigation moves the caret without selecting text or opening the keyboard"];
    [self.editorController savePressed];
    [self eventually:[self, closeBaseline] { return saved.has_value() && closes == closeBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
        message:"legacy partial range saves through the same public native bridge" then:[self, originalJSON] {
      [self check:saved && juce::JSON::toString(songLyricsToVar(*saved), true) == originalJSON
           message:"saving a legacy partial section preserves its original ID, range, kind and text byte-for-byte in canonical JSON"];
      handle.reset(); self.editorController = nil; self.textView = nil;
      [self checkNativeChords];
    }];
  }]; });
}
- (void)checkNativeChords {
  NSString *fixture = @"🕊️ esperanza\nTu voz me acompaña";
  const NSRange word = [fixture rangeOfString:@"esperanza"];
  const NSRange chordCaret = NSMakeRange(word.location + 4, 0); // r, after emoji + VS16.
  const NSRange nearbyCaret = NSMakeRange(word.location + 2, 0); // p, a deliberately close chord.
  SongLyricsDocument chordDocument;
  chordDocument.text = juce::String::fromUTF8(fixture.UTF8String);
  const NSRange chorus = completeParagraphs(fixture, word);
  chordDocument.sections.push_back({"chord-native-chorus", "chorus", static_cast<int>(chorus.location), static_cast<int>(chorus.length)});
  NativeSongLyricsEditor::Options options;
  options.title = "Native chord caret fixture"; options.document = chordDocument;
  saved.reset();
  const int closeBaseline = closes;
  handle = NativeSongLyricsEditor::present(*owner, options,
      [self](auto document, auto completion) { saved = std::move(document); completion(true, {}); },
      [self] { ++closes; });
  [self check:handle != nullptr message:"native chord fixture opens the shared UIKit editor"];
  later(0.6, [self, fixture, word, chordCaret, nearbyCaret, closeBaseline] { [self protected:[self, fixture, word, chordCaret, nearbyCaret, closeBaseline] {
    self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
    self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
    [self.textView becomeFirstResponder]; self.textView.selectedRange = chordCaret;
    layoutTopCards(self.editorController, self.textView);
    if (@available(iOS 16.0, *)) {
      UIAction *copy = [UIAction actionWithTitle:@"Copy" image:nil identifier:@"nativeChordFixtureCopy" handler:^(UIAction *) {}];
      UIAction *paste = [UIAction actionWithTitle:@"Paste" image:nil identifier:@"nativeChordFixturePaste" handler:^(UIAction *) {}];
      UIMenu *caretMenu = [self.textView.delegate textView:self.textView editMenuForTextInRange:chordCaret suggestedActions:@[copy, paste]];
      [self check:menuWithIdentifier(caretMenu, @"nativeLyricsChordMenu") != nil && sectionSubmenu(caretMenu) == nil &&
                  [caretMenu.children containsObject:copy] && [caretMenu.children containsObject:paste]
           message:"a collapsed native caret offers Chord without Section while preserving system actions by identity"];
      [self check:actionWithIdentifier(caretMenu, @"nativeLyricsChord:Am7") != nil
           message:"the native Chord menu exposes an Am7 action in its hierarchy"];
      UIMenu *selectionMenu = [self.textView.delegate textView:self.textView editMenuForTextInRange:word suggestedActions:@[copy, paste]];
      [self check:menuWithIdentifier(selectionMenu, @"nativeLyricsChordMenu") != nil && sectionSubmenu(selectionMenu) != nil &&
                  [selectionMenu.children containsObject:copy] && [selectionMenu.children containsObject:paste]
           message:"a selected word retains Section, Chord, Copy and Paste together"];
    }
    const CGPoint originalOffset = self.textView.contentOffset;
    const uint64_t revision = [[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue];
    [self.editorController applyChord:"Am7" range:chordCaret revision:revision];
    layoutCards(self.editorController, self.textView);
    [self check:[self.textView.text isEqualToString:fixture] && NSEqualRanges(self.textView.selectedRange, chordCaret) &&
                self.textView.isFirstResponder && self.editorController.presentedViewController == nil
         message:"adding a chord changes metadata while preserving the real text, exact native caret and keyboard"];
    [self check:std::abs(self.textView.contentOffset.x - originalOffset.x) < 0.1 &&
                std::abs(self.textView.contentOffset.y - originalOffset.y) < 0.1
         message:"placing a chord on an already visible lyric keeps the native viewport stable"];
    UILabel *label = chordLabel(self.editorController.view, @"Am7");
    UITextPosition *position = [self.textView positionFromPosition:self.textView.beginningOfDocument offset:chordCaret.location];
    const CGRect caret = [self.textView caretRectForPosition:position];
    const CGRect chordFrame = label != nil ? headerDocumentFrame(label, self.textView) : CGRectNull;
    [self check:label != nil && !CGRectIsNull(chordFrame) &&
                std::abs(CGRectGetMinX(chordFrame) - CGRectGetMinX(caret)) < 2.0 &&
                CGRectGetMaxY(chordFrame) <= CGRectGetMinY(caret) + 1.0
         message:"the accessible chord label sits over the precise r glyph and never covers its lyric line"];
    later(0.05, [self, fixture, chordCaret, nearbyCaret, closeBaseline] { [self protected:[self, fixture, chordCaret, nearbyCaret, closeBaseline] {
      [self.textView.undoManager undo]; layoutTopCards(self.editorController, self.textView);
      [self check:visibleChordLabels(self.editorController.view).count == 0 && [self.textView.text isEqualToString:fixture]
           message:"native Undo removes the chord without editing the lyric text"];
      later(0.05, [self, fixture, chordCaret, nearbyCaret, closeBaseline] { [self protected:[self, fixture, chordCaret, nearbyCaret, closeBaseline] {
        [self.textView.undoManager redo]; layoutTopCards(self.editorController, self.textView);
        [self check:chordLabel(self.editorController.view, @"Am7") != nil && [self.textView.text isEqualToString:fixture]
             message:"native Redo restores the chord and its lyric anchor"];
        const uint64_t oldRevision = [[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue];
        [self.editorController applyChord:"D/F#" range:nearbyCaret revision:oldRevision];
        layoutTopCards(self.editorController, self.textView);
        [self check:visibleChordLabels(self.editorController.view).count == 2
             message:"two independent chords can be placed inside the same word"];
        UILabel *first = chordLabel(self.editorController.view, @"D/F#"), *second = chordLabel(self.editorController.view, @"Am7");
        const CGRect firstFrame = first != nil ? headerDocumentFrame(first, self.textView) : CGRectNull;
        const CGRect secondFrame = second != nil ? headerDocumentFrame(second, self.textView) : CGRectNull;
        [self check:first != nil && second != nil && !CGRectIntersectsRect(firstFrame, secondFrame)
             message:"nearby chord symbols use separate reserved lanes instead of colliding or inserting spaces in the lyric"];
        // Capture after normal UIKit runloops, without moving the selection,
        // to distinguish a transient insertion-caret frame from settled layout.
        later(0.3, [self] { [self protected:[self] {
          UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithBounds:self.window.bounds];
          UIImage *preview = [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) {
            (void)context;
            [self.window drawViewHierarchyInRect:self.window.bounds afterScreenUpdates:YES];
          }];
          NSString *previewDirectory = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
          NSData *previewPNG = UIImagePNGRepresentation(preview);
          [self check:previewPNG.length > 0 && [previewPNG writeToFile:[previewDirectory stringByAppendingPathComponent:@"native-lyrics-chords-preview.png"] atomically:YES]
               message:"the settled real UIKit window exports a PNG with both independently anchored chords visible"];
        }]; });
        [self.editorController applyChord:"G" range:chordCaret revision:oldRevision];
        [self check:chordLabel(self.editorController.view, @"G") == nil && visibleChordLabels(self.editorController.view).count == 2
             message:"a contextual action captured before another edit cannot modify the new revision"];
        later(0.45, [self, fixture, chordCaret, nearbyCaret, closeBaseline] { [self protected:[self, fixture, chordCaret, nearbyCaret, closeBaseline] {
          self.textView.selectedRange = NSMakeRange(0, 0);
          [self.textView.undoManager beginUndoGrouping]; [self.textView insertText:@"🎹"]; [self.textView.undoManager endUndoGrouping];
          [self check:[self.textView.text hasPrefix:@"🎹🕊️"] && visibleChordLabels(self.editorController.view).count == 2
               message:"native Unicode typing before chord anchors retains both rendered chords"];
          later(0.05, [self, fixture, chordCaret, nearbyCaret, closeBaseline] { [self protected:[self, fixture, chordCaret, nearbyCaret, closeBaseline] {
            [self.textView.undoManager undo]; layoutTopCards(self.editorController, self.textView);
            [self check:[self.textView.text isEqualToString:fixture] && visibleChordLabels(self.editorController.view).count == 2
                 message:"native text Undo restores the exact Unicode lyric and both chord anchors"];
            self.textView.selectedRange = NSMakeRange(self.textView.text.length, 0);
            [self.textView.undoManager beginUndoGrouping];
            [self.textView setMarkedText:@"a" selectedRange:NSMakeRange(1, 0)];
            [self check:self.textView.markedTextRange != nil message:"UIKit owns an active marked-text composition"];
            [self.editorController applyChord:"F" range:chordCaret revision:[[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue]];
            [self check:chordLabel(self.editorController.view, @"F") == nil && visibleChordLabels(self.editorController.view).count == 2
                 message:"chord actions leave an active IME composition untouched"];
            [self.textView unmarkText]; [self.textView.undoManager endUndoGrouping];
            later(0.05, [self, fixture, chordCaret, nearbyCaret, closeBaseline] { [self protected:[self, fixture, chordCaret, nearbyCaret, closeBaseline] {
              [self.textView.undoManager undo]; layoutTopCards(self.editorController, self.textView);
              [self check:[self.textView.text isEqualToString:fixture] && visibleChordLabels(self.editorController.view).count == 2
                   message:"undoing committed IME text preserves independently added chord metadata"];
              [self.editorController savePressed];
              [self eventually:[self, closeBaseline] { return saved.has_value() && closes == closeBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
                  message:"native chord Save completes once through the existing asynchronous bridge" then:[self, fixture, chordCaret, nearbyCaret] {
                [self check:saved && saved->schemaVersion == 2 && saved->text == juce::String::fromUTF8(fixture.UTF8String) && saved->chords.size() == 2
                     message:"native Save emits a version-two structured lyric with the unchanged text and both chords"];
                bool sawFirst = false, sawSecond = false;
                for (const auto &chord : saved->chords) {
                  if (chord.symbol == "Am7") sawFirst = chord.offset == static_cast<int>(chordCaret.location);
                  if (chord.symbol == "D/F#") sawSecond = chord.offset == static_cast<int>(nearbyCaret.location);
                }
                [self check:sawFirst && sawSecond message:"saved chord anchors use document UTF16 offsets after text Undo and IME"];
                const auto savedJSON = juce::JSON::toString(songLyricsToVar(*saved), true);
                NativeSongLyricsEditor::Options reopen; reopen.title = "Reopened native chord fixture"; reopen.document = saved;
                handle.reset(); self.editorController = nil; self.textView = nil;
                const int finalCloseBaseline = closes;
                handle = NativeSongLyricsEditor::present(*owner, reopen,
                    [self](auto document, auto completion) { saved = std::move(document); completion(true, {}); }, [self] { ++closes; });
                [self check:handle != nullptr message:"the native bridge reopens a saved version-two chord document"];
                later(0.6, [self, fixture, savedJSON, finalCloseBaseline] { [self protected:[self, fixture, savedJSON, finalCloseBaseline] {
                  self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
                  self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
                  layoutTopCards(self.editorController, self.textView);
                  [self check:[self.textView.text isEqualToString:fixture] && chordLabel(self.editorController.view, @"Am7") != nil &&
                              chordLabel(self.editorController.view, @"D/F#") != nil
                       message:"reopening restores both native chord decorations over the original lyric"];
                  [self.editorController savePressed];
                  [self eventually:[self, finalCloseBaseline] { return closes == finalCloseBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
                      message:"the reopened document can complete the normal Save/Close flow" then:[self, savedJSON] {
                    [self check:saved && juce::JSON::toString(songLyricsToVar(*saved), true) == savedJSON
                         message:"save and reopen retain chord IDs, symbols and precise canonical offsets"];
                    handle.reset(); self.editorController = nil; self.textView = nil;
                    [self checkAutomaticNativeMenus];
                  }];
                }]; });
              }];
            }]; });
          }]; });
        }]; });
      }]; });
    }]; });
  }]; });
}

- (void)checkAutomaticNativeMenus {
  NativeSongLyricsEditor::Options options;
  options.title = "Native automatic menu fixture"; options.document = SongLyricsDocument{};
  menuGuardCloseBaseline = closes;
  handle = NativeSongLyricsEditor::present(*owner, options,
      [self](auto document, auto completion) { saved = std::move(document); completion(true, {}); }, [self] { ++closes; });
  [self check:handle != nullptr message:"automatic-menu fixture opens the real UIKit bridge"];
  later(0.6, [self] { [self protected:[self] {
    self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
    self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
    [self.textView becomeFirstResponder]; self.textView.selectedRange = NSMakeRange(0, 0);
    UILongPressGestureRecognizer *observer = nil;
    for (UIGestureRecognizer *gesture in self.textView.gestureRecognizers)
      if ([gesture.name isEqualToString:@"nativeLyricsSelectionMenuObserver"] && [gesture isKindOfClass:UILongPressGestureRecognizer.class]) observer = (UILongPressGestureRecognizer *)gesture;
    [self check:observer != nil && !observer.cancelsTouchesInView && !observer.delaysTouchesBegan && !observer.delaysTouchesEnded
         message:"the production selection-menu observer is a passive native long press"];
    [self check:[observer.delegate respondsToSelector:@selector(gestureRecognizer:shouldRecognizeSimultaneouslyWithGestureRecognizer:)] &&
                [observer.delegate gestureRecognizer:observer shouldRecognizeSimultaneouslyWithGestureRecognizer:self.textView.panGestureRecognizer]
         message:"the observer allows UIKit's native selection and scrolling gestures to coexist"];
    if (@available(iOS 16.0, *)) {
      UIMenu *blankMenu = [self.editorController menuForRange:NSMakeRange(0, 0) suggested:@[]];
      [self check:menuWithIdentifier(blankMenu, @"nativeLyricsChordMenu") != nil &&
                  menuWithIdentifier(blankMenu, @"nativeLyricsCreateSectionMenu") == nil && sectionSubmenu(blankMenu) == nil
           message:"an empty native cursor offers Chord without creating an empty instrumental section"];
      [self check:actionWithIdentifier(blankMenu, @"nativeLyricsCreateSection:intro") == nil &&
                  actionWithIdentifier(blankMenu, @"nativeLyricsCreateSection:solo") == nil &&
                  actionWithIdentifier(blankMenu, @"nativeLyricsCreateSection:instrumental") == nil
           message:"the edit menu no longer offers empty Intro, Solo or Instrumental creation"];
      menuGestureFixture = [PlayWorshipLyricsObservedLongPress new];
      menuGestureFixture.name = @"nativeLyricsSelectionMenuObserver";
      [self.textView addGestureRecognizer:menuGestureFixture];
      const auto revision = [[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue];
      sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateBegan);
      sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateEnded);
      [self eventually:[self] { return [[self.editorController valueForKey:@"automaticEditMenuVisible"] boolValue]; }
          message:"ending a selection gesture on an empty line presents the native edit menu" then:[self, revision] {
        if (@available(iOS 16.0, *)) {
          UIEditMenuInteraction *interaction = [self.editorController valueForKey:@"automaticEditMenuInteraction"];
          [self check:[interaction isKindOfClass:UIEditMenuInteraction.class] && [self.textView.interactions containsObject:interaction]
               message:"the automatic menu uses a real UIEditMenuInteraction attached to the native text field"];
        }
        [self check:self.textView.text.length == 0 && NSEqualRanges(self.textView.selectedRange, NSMakeRange(0, 0)) &&
                    self.textView.isFirstResponder && [[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue] == revision
             message:"automatic menu presentation preserves lyric bytes, revision, native cursor and editing focus"];
        if (@available(iOS 16.0, *)) [[self.editorController valueForKey:@"automaticEditMenuInteraction"] dismissMenu];
        [self eventually:[self] { return ![[self.editorController valueForKey:@"automaticEditMenuVisible"] boolValue]; }
            message:"the automatic edit menu reports its actual native dismissal" then:[self] { [self checkAutomaticMenuGuard:0]; }];
      }];
    } else {
      [self.editorController closePressed];
      [self eventually:[self] { return closes == menuGuardCloseBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
          message:"older iOS keeps the existing native-menu fallback" then:[self] {
        handle.reset(); self.editorController = nil; self.textView = nil; [self checkLegacyInstrumentalReadOnly];
      }];
    }
  }]; });
}
- (void)checkAutomaticMenuGuard:(NSInteger)index {
  if (index == 7) {
    [self.textView removeGestureRecognizer:menuGestureFixture]; menuGestureFixture = nil;
    [self.editorController closePressed];
    [self eventually:[self] { return closes == menuGuardCloseBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
        message:"automatic-menu guard fixtures close without leaving changes or alerts" then:[self] {
      handle.reset(); self.editorController = nil; self.textView = nil; [self checkLegacyInstrumentalReadOnly];
    }];
    return;
  }
  [self check:![[self.editorController valueForKey:@"automaticEditMenuVisible"] boolValue] && self.textView.text.length == 0
       message:"each automatic-menu guard begins with the unchanged empty native document"];
  self.textView.selectedRange = NSMakeRange(0, 0);
  if (index == 0) {
    [self.editorController textViewDidChangeSelection:self.textView];
  } else if (index == 1) {
    sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateBegan);
    [self.textView.undoManager beginUndoGrouping]; [self.textView insertText:@"A"]; [self.textView.undoManager endUndoGrouping];
    sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateEnded);
  } else if (index == 2) {
    sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateBegan);
    sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateEnded);
    [self.textView.undoManager beginUndoGrouping]; [self.textView insertText:@"B"]; [self.textView.undoManager endUndoGrouping];
  } else if (index == 3) {
    [self.textView.undoManager beginUndoGrouping];
    [self.textView setMarkedText:@"a" selectedRange:NSMakeRange(1, 0)];
    [self check:self.textView.markedTextRange != nil message:"automatic-menu IME guard uses a real marked-text composition"];
    sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateBegan);
    sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateEnded);
  } else if (index == 4) {
    sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateBegan);
    sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateEnded);
    [self.editorController scrollViewWillBeginDragging:self.textView];
    if ([self.editorController respondsToSelector:@selector(scrollViewDidEndDragging:willDecelerate:)])
      [self.editorController scrollViewDidEndDragging:self.textView willDecelerate:NO];
  } else if (index == 5) {
    sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateBegan);
    [self.editorController scrollViewWillBeginDragging:self.textView];
    if ([self.editorController respondsToSelector:@selector(scrollViewDidEndDragging:willDecelerate:)])
      [self.editorController scrollViewDidEndDragging:self.textView willDecelerate:NO];
    sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateEnded);
  } else {
    sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateBegan);
    sendMenuGesture(self.editorController, self.textView, menuGestureFixture, UIGestureRecognizerStateCancelled);
  }
  later(0.3, [self, index] { [self protected:[self, index] {
    const char *messages[] = {
      "selection notifications alone do not open an automatic menu while navigating or typing",
      "a text revision changed during selection prevents the stale automatic menu",
      "typing before the scheduled presentation cancels the pending automatic menu",
      "an active native IME composition never opens the automatic menu",
      "manual scrolling cancels a pending automatic menu without changing the native selection",
      "manual scrolling during a held selection gesture prevents menu presentation when the finger ends",
      "a cancelled selection gesture does not present an automatic menu"};
    [self check:![[self.editorController valueForKey:@"automaticEditMenuVisible"] boolValue] message:messages[index]];
    if (index == 3) { [self.textView unmarkText]; [self.textView.undoManager endUndoGrouping]; }
    later(0.05, [self, index] { [self protected:[self, index] {
      if (index >= 1 && index <= 3) [self.textView.undoManager undo];
      [self check:self.textView.text.length == 0 message:"guard fixture cleanup restores the original lyric through native Undo"];
      [self checkAutomaticMenuGuard:index + 1];
    }]; });
  }]; });
}
- (void)checkLegacyInstrumentalReadOnly {
  SongLyricsDocument document; document.text = "Uno\nDos\nTres";
  document.sections.push_back({"nested-verse", "verse", 0, 7});
  document.sections.push_back({"nested-target", "bridge", 8, 4});
  juce::String blockId;
  [self check:addSongLyricsInstrumentalBlock(document, 4, "instrumental", "Solo", 4, &blockId)
       message:"the compatibility fixture loads a legacy instrumental inside a multiparagraph verse"];
  const auto row = document.instrumentalBlocks.front().rows.front();
  [self check:setSongLyricsInstrumentalCell(document, blockId, row.id, row.cells[0].id, "C") &&
              setSongLyricsInstrumentalCell(document, blockId, row.id, row.cells[1].id, "Am7")
       message:"the legacy document contains stable C and Am7 cell metadata"];
  const auto originalJSON = juce::JSON::toString(songLyricsToVar(document), true);
  NativeSongLyricsEditor::Options options; options.title = "Legacy read-only chords"; options.document = document;
  saved.reset(); const int closeBaseline = closes;
  handle = NativeSongLyricsEditor::present(*owner, options,
      [self](auto lyrics, auto completion) { saved = std::move(lyrics); completion(true, {}); }, [self] { ++closes; });
  [self check:handle != nullptr message:"a legacy version-three document still opens through the native bridge"];
  later(0.6, [self, blockId, row, originalJSON, closeBaseline] { [self protected:[self, blockId, row, originalJSON, closeBaseline] {
    self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
    self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
    layoutTopCards(self.editorController, self.textView);
    UIView *card = identifiedView(self.editorController.view, [@"nativeLyricsInstrumental:" stringByAppendingString:[NSString stringWithUTF8String:blockId.toRawUTF8()]]);
    UIView *nativeRow = identifiedView(self.editorController.view, [@"nativeLyricsInstrumentalRow:" stringByAppendingString:[NSString stringWithUTF8String:row.id.toRawUTF8()]]);
    NSString *projection = [nativeRow respondsToSelector:@selector(text)] ? [nativeRow valueForKey:@"text"] : nil;
    const BOOL readOnly = [nativeRow isKindOfClass:UILabel.class] || ([nativeRow isKindOfClass:UITextView.class] && !((UITextView *)nativeRow).editable);
    [self check:card != nil && readOnly && [projection containsString:@"C"] && [projection containsString:@"Am7"] && ![projection containsString:@"○"]
         message:"legacy instrumental symbols remain visible and read-only without empty-circle cells"];
    [self check:!containsNativeButton(card) message:"legacy instrumental presentation has no Add Row or Remove Section controls"];
    UIView *backgrounds = identifiedView(self.editorController.view, @"nativeLyricsCardBackgrounds");
    const CGRect cardFrame = [card convertRect:card.bounds toView:backgrounds];
    bool separated = backgrounds != nil && !CGRectIsEmpty(cardFrame);
    for (NSValue *value in (NSArray<NSValue *> *)[backgrounds valueForKey:@"rectangles"])
      separated = separated && !CGRectIntersectsRect(value.CGRectValue, cardFrame);
    [self check:separated message:"legacy instrumental content stays separate from the surrounding lyric background"];
    UIView *outerGrip = identifiedView(self.editorController.view, @"nativeLyricsCardDragHandle:nested-verse");
    UIDragInteraction *outerDrag = dragInteractionOnView(outerGrip);
    [self check:outerDrag == nil || !outerDrag.enabled message:"a verse containing a nested instrumental is not offered as a movable whole card"];
    const auto revision = [[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue];
    [self.editorController moveLyricsCard:"nested-verse" before:"nested-target" revision:revision];
    [self check:[self.textView.text isEqualToString:@"Uno\nDos\nTres"] && [[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue] == revision
         message:"an unsupported nested move leaves the native text and revision unchanged"];
    later(0.3, [self, originalJSON, closeBaseline] { [self protected:[self, originalJSON, closeBaseline] {
      [self check:writeLyricsWindowPreview(self.window, @"native-lyrics-readonly-preview.png")
           message:"the settled real UIKit window exports the read-only compatibility preview"];
      [self.editorController savePressed];
      [self eventually:[self, closeBaseline] { return saved.has_value() && closes == closeBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
          message:"read-only legacy compatibility completes the normal Save flow" then:[self, originalJSON] {
        [self check:saved && juce::JSON::toString(songLyricsToVar(*saved), true) == originalJSON
             message:"saving a rejected nested move preserves every legacy cell, ID, range and lyric byte"];
        handle.reset(); self.editorController = nil; self.textView = nil; [self checkNativeCardReordering];
      }];
    }]; });
  }]; });
}
- (void)checkNativeCardReordering {
  SongLyricsDocument document; document.schemaVersion = 2;
  document.text = juce::String::fromUTF8("Uno 🎹\nDos\nTres");
  document.sections = {{"move-a", "verse", 0, 6}, {"move-b", "chorus", 7, 3}, {"move-c", "bridge", 11, 4}};
  document.chords = {{"move-chord-a", 1, "C"}, {"move-chord-b", 8, "Am7"}, {"move-chord-c", 11, "G"}};
  NativeSongLyricsEditor::Options options; options.title = "Native card reordering"; options.document = document;
  saved.reset(); const int closeBaseline = closes;
  handle = NativeSongLyricsEditor::present(*owner, options,
      [self](auto lyrics, auto completion) { saved = std::move(lyrics); completion(true, {}); }, [self] { ++closes; });
  [self check:handle != nullptr message:"three complete Unicode lyric cards open for native reordering"];
  later(0.6, [self, closeBaseline] { [self protected:[self, closeBaseline] {
    self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
    self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
    layoutTopCards(self.editorController, self.textView);
    NSArray<UIView *> *grips = identifiedViews(self.editorController.view, @"nativeLyricsCardDragHandle:");
    [self check:grips.count == 3 message:"each complete movable card exposes its own native drag grip"];
    UIDragInteraction *drag = nil;
    for (UIView *grip in grips) {
      UIDragInteraction *interaction = dragInteractionOnView(grip);
      [self check:interaction != nil && interaction.enabled && interaction.view == grip
           message:"the reorder drag interaction is enabled on its dedicated grip"];
      if ([grip.accessibilityIdentifier isEqualToString:@"nativeLyricsCardDragHandle:move-a"]) drag = interaction;
    }
    [self check:drag != nil message:"the source card resolves its own drag interaction"];
    bool nativeTextIntact = self.textView.editable && self.textView.selectable;
    for (id<UIInteraction> interaction in self.textView.interactions)
      if ([interaction isKindOfClass:UIDragInteraction.class] && ((UIDragInteraction *)interaction).delegate == drag.delegate) nativeTextIntact = false;
    [self check:nativeTextIntact message:"our card drag does not replace the native text view's editing, selection or text-drag interactions"];
    UIDropInteraction *drop = nil;
    for (id<UIInteraction> interaction in self.editorController.view.interactions)
      if ([interaction isKindOfClass:UIDropInteraction.class]) drop = (UIDropInteraction *)interaction;
    [self check:drop != nil message:"the editor owns a native drop interaction on its root container"];
    const uint64_t revision = [[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue];
    PlayWorshipLyricsDragSessionFixture *session = [PlayWorshipLyricsDragSessionFixture new];
    session.coordinateView = self.editorController.view;
    session.fixtureLocation = [drag.view convertPoint:CGPointMake(CGRectGetMidX(drag.view.bounds), CGRectGetMidY(drag.view.bounds)) toView:self.editorController.view];
    session.items = [drag.delegate dragInteraction:drag itemsForBeginningSession:session];
    NSDictionary *payload = [session.items.firstObject.localObject isKindOfClass:NSDictionary.class] ? session.items.firstObject.localObject : nil;
    [self check:session.items.count == 1 && [payload[@"sourceId"] isEqualToString:@"move-a"] &&
                [payload[@"revision"] unsignedLongLongValue] == revision && [payload[@"editorToken"] isKindOfClass:NSString.class] &&
                [payload[@"editorToken"] length] > 0 && [session.localContext isKindOfClass:NSDictionary.class]
         message:"the drag carries only local card identity, revision and an editor token"];
    [self check:[session.items.firstObject.itemProvider.registeredTypeIdentifiers containsObject:@"com.playworship.lyrics-card"] &&
                [drag.delegate dragInteraction:drag sessionAllowsMoveOperation:session] &&
                [drag.delegate dragInteraction:drag sessionIsRestrictedToDraggingApplication:session]
         message:"native card drags declare their own type and restrict Move to this application"];
    [self check:[drop.delegate dropInteraction:drop canHandleSession:session] message:"the drop target accepts a current local card payload"];
    PlayWorshipLyricsDragSessionFixture *foreign = [PlayWorshipLyricsDragSessionFixture new];
    NSMutableDictionary *foreignPayload = [payload mutableCopy]; foreignPayload[@"editorToken"] = NSUUID.UUID.UUIDString;
    UIDragItem *foreignItem = [[UIDragItem alloc] initWithItemProvider:session.items.firstObject.itemProvider]; foreignItem.localObject = foreignPayload;
    foreign.items = @[foreignItem]; foreign.localContext = foreignPayload;
    foreign.coordinateView = session.coordinateView; foreign.fixtureLocation = session.fixtureLocation;
    BOOL foreignRejected = ![drop.delegate dropInteraction:drop canHandleSession:foreign];
    if (!foreignRejected) {
      UIDropOperation operation = [drop.delegate dropInteraction:drop sessionDidUpdate:foreign].operation;
      foreignRejected = operation != UIDropOperationMove && operation != UIDropOperationCopy;
    }
    [self check:foreignRejected message:"a payload from another editor instance is rejected even when its type, point and card ID match"];
    [self.textView.undoManager removeAllActions];
    [self.editorController moveLyricsCard:"move-a" before:"move-b" revision:revision];
    [self.editorController moveLyricsCard:"move-a" before:"move-a" revision:revision];
    [self check:!self.textView.undoManager.canUndo && [self.textView.text isEqualToString:@"Uno 🎹\nDos\nTres"]
         message:"dropping onto the current position or the same card is a no-op without an Undo entry"];
    [self.editorController moveLyricsCard:"move-a" before:"move-c" revision:revision];
    [self check:[self.textView.text isEqualToString:@"Dos\nUno 🎹\nTres"]
         message:"moving a card before another actually reorders its lyric text, including the Unicode paragraph"];
    BOOL staleRejected = ![drop.delegate dropInteraction:drop canHandleSession:session];
    if (!staleRejected) {
      UIDropOperation operation = [drop.delegate dropInteraction:drop sessionDidUpdate:session].operation;
      staleRejected = operation != UIDropOperationMove && operation != UIDropOperationCopy;
    }
    [self check:staleRejected message:"a drag payload captured before the move is now stale and cannot be dropped"];
    [self.editorController moveLyricsCard:"move-c" before:"move-b" revision:revision];
    [self check:[self.textView.text isEqualToString:@"Dos\nUno 🎹\nTres"]
         message:"a stale selector request cannot reorder the current document"];
    later(0.05, [self, closeBaseline] { [self protected:[self, closeBaseline] {
      [self.textView.undoManager undo]; layoutCards(self.editorController, self.textView);
      [self check:[self.textView.text isEqualToString:@"Uno 🎹\nDos\nTres"] message:"native Undo restores the original card order and text"];
      later(0.05, [self, closeBaseline] { [self protected:[self, closeBaseline] {
        [self.textView.undoManager redo]; layoutCards(self.editorController, self.textView);
        [self check:[self.textView.text isEqualToString:@"Dos\nUno 🎹\nTres"] message:"native Redo restores the card move"];
        [self.editorController moveLyricsCard:"move-b" before:juce::String()
            revision:[[self.editorController valueForKey:@"documentRevision"] unsignedLongLongValue]];
        [self check:[self.textView.text isEqualToString:@"Uno 🎹\nTres\nDos"] message:"a native card can move to the end without adding a blank lyric line"];
        later(0.05, [self, closeBaseline] { [self protected:[self, closeBaseline] {
          [self.textView.undoManager undo];
          [self check:[self.textView.text isEqualToString:@"Dos\nUno 🎹\nTres"] message:"Undo of the end move leaves the earlier move intact"];
          later(0.05, [self, closeBaseline] { [self protected:[self, closeBaseline] {
            [self.textView.undoManager redo]; layoutCards(self.editorController, self.textView);
            [self check:[self.textView.text isEqualToString:@"Uno 🎹\nTres\nDos"] message:"Redo restores the end placement"];
            later(0.3, [self, closeBaseline] { [self protected:[self, closeBaseline] {
              [self check:writeLyricsWindowPreview(self.window, @"native-lyrics-reordered-preview.png")
                   message:"the settled real UIKit window exports the reordered-card preview"];
              [self.editorController savePressed];
              [self eventually:[self, closeBaseline] { return saved.has_value() && closes == closeBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
                message:"reordered cards save through the existing asynchronous bridge" then:[self] {
              bool ranges = saved && saved->text == juce::String::fromUTF8("Uno 🎹\nTres\nDos") && saved->sections.size() == 3 && saved->chords.size() == 3;
              if (saved) {
                for (const auto &section : saved->sections) {
                  if (section.id == "move-a") ranges = ranges && section.start == 0 && section.length == 6 && section.kind == "verse";
                  else if (section.id == "move-b") ranges = ranges && section.start == 12 && section.length == 3 && section.kind == "chorus";
                  else if (section.id == "move-c") ranges = ranges && section.start == 7 && section.length == 4 && section.kind == "bridge";
                  else ranges = false;
                }
                for (const auto &chord : saved->chords) {
                  if (chord.id == "move-chord-a") ranges = ranges && chord.offset == 1 && chord.symbol == "C";
                  else if (chord.id == "move-chord-b") ranges = ranges && chord.offset == 13 && chord.symbol == "Am7";
                  else if (chord.id == "move-chord-c") ranges = ranges && chord.offset == 7 && chord.symbol == "G";
                  else ranges = false;
                }
              }
              [self check:ranges message:"Save preserves every section/chord ID and its correct UTF16 anchor after both moves"];
              const auto canonical = juce::JSON::toString(songLyricsToVar(*saved), true);
              NativeSongLyricsEditor::Options reopen; reopen.title = "Reopened reordered cards"; reopen.document = saved;
              handle.reset(); self.editorController = nil; self.textView = nil;
              const int finalCloseBaseline = closes;
              handle = NativeSongLyricsEditor::present(*owner, reopen,
                  [self](auto lyrics, auto completion) { saved = std::move(lyrics); completion(true, {}); }, [self] { ++closes; });
              [self check:handle != nullptr message:"the saved card order reopens in the real native editor"];
              later(0.6, [self, canonical, finalCloseBaseline] { [self protected:[self, canonical, finalCloseBaseline] {
                self.editorController = ((UINavigationController *)self.window.rootViewController.presentedViewController).topViewController;
                self.textView = (UITextView *)identifiedView(self.editorController.view, @"nativeLyricsText");
                [self check:[self.textView.text isEqualToString:@"Uno 🎹\nTres\nDos"] message:"reopening restores the reordered text"];
                [self.editorController savePressed];
                [self eventually:[self, finalCloseBaseline] { return closes == finalCloseBaseline + 1 && self.window.rootViewController.presentedViewController == nil; }
                    message:"reopened reordered cards finish Save and Close" then:[self, canonical] {
                  [self check:saved && juce::JSON::toString(songLyricsToVar(*saved), true) == canonical
                       message:"reopen and Save preserve the complete canonical reordered document"];
                  [self finish:YES error:{}];
                }];
              }]; });
            }];
            }]; });
          }]; });
        }]; });
      }]; });
    }]; });
  }]; });
}

- (void)finish:(BOOL)success error:(juce::String)error {
  static bool finished = false;
  if (finished) return;
  finished = true;
  writeReport(success, checks, error);
  handle.reset(); self.editorController = nil; self.textView = nil;
  firstCompletion = {}; secondCompletion = {}; saved.reset(); source = {};
  host.reset(); owner.reset(); self.window = nil;
  std::fflush(stdout); std::fflush(stderr);
  juce::JUCEApplication::getInstance()->setApplicationReturnValue(success ? 0 : 1);
  later(0.05, [] { juce::JUCEApplication::quit(); });
}
@end

class NativeLyricsUITestApplication final : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override { return "Native Lyrics UI Tests"; }
  const juce::String getApplicationVersion() override { return "1.0"; }
  void initialise(const juce::String &) override {
    harness = [PlayWorshipLyricsHarness new];
    harness.manualMode = juce::SystemStats::getEnvironmentVariable("MTCONTROLS_NATIVE_LYRICS_MANUAL", {}) == "1";
    [harness startHost];
  }
  void shutdown() override { harness = nil; }
private:
  __strong PlayWorshipLyricsHarness *harness;
};
START_JUCE_APPLICATION(NativeLyricsUITestApplication)
