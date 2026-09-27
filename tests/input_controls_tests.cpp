#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#endif
#endif

#include "ui/UiCommon.h"
#include "ui/components/SearchField.h"

#include <cstdlib>
#include <iostream>


namespace {
void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void pump() {
  juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
}

void testEditorThemePreservesBehaviour() {
  juce::TextEditor editor;
  editor.setMultiLine(false);
  editor.setReturnKeyStartsNewLine(false);
  editor.setJustification(juce::Justification::centredLeft);
  editor.setInputRestrictions(3, "0123456789");
  editor.setKeyboardType(juce::TextInputTarget::numericKeyboard);
  editor.setSize(160, 48);
  auto *filter = editor.getInputFilter();
  int returns = 0, escapes = 0, focusLosses = 0;
  editor.onReturnKey = [&] { ++returns; };
  editor.onEscapeKey = [&] { ++escapes; };
  editor.onFocusLost = [&] { ++focusLosses; };

  applyTextEditorTheme(editor, UITheme::cardBg, UITheme::cardBorder);
  expect(!editor.isMultiLine() && !editor.getReturnKeyStartsNewLine(),
         "theme preserves single-line and Return behaviour");
  expect(editor.getKeyboardType() == juce::TextInputTarget::numericKeyboard,
         "theme preserves the form's keyboard type");
  expect(editor.getInputFilter() == filter,
         "theme preserves the form's input filter");
  expect(editor.getTopIndent() == 0,
         "single-line fields have no asymmetric top indent");
  editor.insertTextAtCaret("1x2y34");
  expect(editor.getText() == "123", "numeric restrictions remain effective");
  expect(static_cast<bool>(editor.onReturnKey) &&
             static_cast<bool>(editor.onEscapeKey) &&
             static_cast<bool>(editor.onFocusLost),
         "theme preserves form callbacks");
  editor.onReturnKey();
  editor.onEscapeKey();
  editor.onFocusLost();
  expect(returns == 1 && escapes == 1 && focusLosses == 1,
         "the original callbacks still execute exactly once");

  juce::TextEditor notes;
  notes.setMultiLine(true, false);
  notes.setReturnKeyStartsNewLine(true);
  notes.setJustification(juce::Justification::topLeft);
  notes.setReadOnly(true);
  applyTextEditorTheme(notes, UITheme::cardBg, UITheme::cardBorder);
  expect(notes.isMultiLine() && notes.getReturnKeyStartsNewLine() &&
             notes.isReadOnly(),
         "theme must not convert multiline/read-only editors into form fields");
  expect(notes.getJustificationType() == juce::Justification::topLeft,
         "theme preserves multiline alignment");
}

void testSearchPadding() {
  SearchField search;
  search.setBounds(0, 0, 280, 44);
  const int topIndent = search.editor().getTopIndent();
  const int leftIndent = search.editor().getLeftIndent();
  const int emptyTrailing = search.editor().getBorder().getRight();
  const int accessoryWidth = useTouchOptimisedControls() ? 44 : 36;

  search.setText("Glorioso dia");
  expect(search.editor().getTopIndent() == topIndent &&
             search.editor().getLeftIndent() == leftIndent,
         "showing clear must not move search text vertically or horizontally");
  expect(search.editor().getBorder().getRight() >= accessoryWidth &&
             search.editor().getBorder().getRight() > emptyTrailing,
         "clear reserves trailing space using the right border, not top indent");

  search.refreshThemeColours();
  expect(search.editor().getTopIndent() == topIndent &&
             search.editor().getBorder().getRight() >= accessoryWidth,
         "theme refresh preserves search accessory spacing");

  search.setLoading(true);
  expect(search.editor().getTopIndent() == topIndent &&
             search.editor().getBorder().getRight() >= accessoryWidth,
         "spinner does not move the text and retains its trailing space");
  search.setText({});
  expect(search.editor().getBorder().getRight() >= accessoryWidth,
         "an empty loading search still reserves the spinner area");
  search.setLoading(false);
  expect(search.editor().getTopIndent() == topIndent &&
             search.editor().getBorder().getRight() == emptyTrailing,
         "clearing search restores trailing space without a vertical jump");
}

juce::MouseEvent clickEvent(juce::Component &component, bool dragged = false) {
  const auto position = component.getLocalBounds().toFloat().getCentre();
  const auto now = juce::Time::getCurrentTime();
  return {juce::Desktop::getInstance().getMainMouseSource(), position,
          juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
          1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component,
          now, position, now, 1, dragged};
}

void testSingleComboPopup(bool mobileForm = false) {
  juce::Component host;
  const auto *display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
  if (display == nullptr) {
    std::cerr << "SKIP: popup interaction needs an interactive display\n";
    std::exit(77);
  }
  // A JUCE menu intentionally dismisses itself when its process loses focus.
  // An offscreen, non-focusable test peer makes that 20 Hz timer race the
  // synthetic mouseDown/mouseUp sequence, unrelated to duplicate menus.
  host.setName("Input controls regression test");
  host.setBounds(display->userArea.withSizeKeepingCentre(320, 180));
  host.addToDesktop(juce::ComponentPeer::windowHasTitleBar);
  host.setVisible(true);
  AdaptiveComboBox combo;
  combo.addItem("C", 1);
  combo.addItem("D", 2);
  combo.addItem("E", 3);
  combo.setSelectedId(1, juce::dontSendNotification);
  combo.setBounds(24, 24, 200, 44);
  host.addAndMakeVisible(combo);
  applyComboBoxTheme(combo, UITheme::cardBg, UITheme::cardBorder);
  combo.setMobileFormMode(mobileForm);
  int changes = 0;
  combo.onChange = [&] { ++changes; };

  juce::Process::makeForegroundProcess();
  host.toFront(true);
  combo.grabKeyboardFocus();
  const auto focusDeadline = juce::Time::getMillisecondCounter() + 1000u;
  while ((!juce::Process::isForegroundProcess() ||
          !host.hasKeyboardFocus(true)) &&
         juce::Time::getMillisecondCounter() < focusDeadline) {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    combo.grabKeyboardFocus();
  }
  if (!juce::Process::isForegroundProcess() || !host.hasKeyboardFocus(true)) {
    std::cerr << "SKIP: OS did not grant foreground focus to the popup test\n";
    host.removeFromDesktop();
    std::exit(77);
  }

  juce::Label label;
  const auto options = combo.getLookAndFeel().getOptionsForComboBoxPopupMenu(
      combo, label);
  expect(options.getMaximumNumColumns() == 1,
         "combo options remain a single list even on a short display");
  expect(options.getMinimumWidth() >= combo.getWidth() &&
             options.getStandardItemHeight() == touchOptimisedPopupItemHeight(),
         "combo popup uses the shared width and platform touch metrics");

  const auto event = clickEvent(combo);
  combo.mouseDown(event);
  pump();
  if (mobileForm) {
    expect(!combo.isPopupActive(), "a form selector waits for a completed tap");
    combo.mouseUp(clickEvent(combo, true));
    pump();
    expect(!combo.isPopupActive(), "scrolling across a form selector does not open it");
    combo.mouseDown(event);
    combo.mouseUp(event);
    pump();
    pump();
  }
  expect(combo.isPopupActive() &&
             juce::Component::getNumCurrentlyModalComponents() == 1,
         "mouseDown opens one JUCE popup and keeps its active guard set");
  auto *popup = juce::Component::getCurrentlyModalComponent();
  if (!mobileForm)
    combo.mouseUp(event);
  pump();
  expect(combo.isPopupActive() &&
             juce::Component::getNumCurrentlyModalComponents() == 1 &&
             juce::Component::getCurrentlyModalComponent() == popup,
         "mouseUp from the same gesture must not open a second popup");

  popup->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
  popup->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
  pump();
  expect(!combo.isPopupActive() && combo.getSelectedId() == 2 && changes == 1,
         "choosing updates once and closes the list");

  combo.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
  pump();
  pump();
  pump();
  popup = juce::Component::getCurrentlyModalComponent();
  expect(popup != nullptr, "a dismissed combo can be reopened");
  popup->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
  pump();
  expect(!combo.isPopupActive() && combo.getSelectedId() == 2 && changes == 1,
         "Escape closes without changing the current value or notifying");

  if (mobileForm) {
    combo.showPopup();
    combo.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    pump();
    pump();
    expect(!combo.isPopupActive() &&
               juce::Component::getNumCurrentlyModalComponents() == 0,
           "Escape cancels a pending menu before it appears");
  }

  combo.setEnabled(false);
  combo.showPopup();
  pump();
  expect(!combo.isPopupActive() &&
             juce::Component::getNumCurrentlyModalComponents() == 0,
         "disabled combo never opens a popup");
  combo.setLookAndFeel(nullptr);
  host.removeFromDesktop();
}
} // namespace

int main(int argc, char **argv) {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr)
    return 77;
#endif
  juce::ScopedJuceInitialiser_GUI gui;
  const bool interactivePopup = argc > 1 && juce::String(argv[1]) == "--popup";
#if JUCE_MAC
  // A console test defaults to a prohibited activation policy. PopupMenu
  // correctly dismisses itself without an active host, so opt into GUI focus.
  if (interactivePopup) {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
  }
#endif
  applyTheme(ThemePreset::Spotify);
  if (interactivePopup) {
    testSingleComboPopup();
    testSingleComboPopup(true);
  } else {
    testEditorThemePreservesBehaviour();
    testSearchPadding();
    AdaptiveComboBox combo;
    combo.setSize(200, 44);
    applyComboBoxTheme(combo, UITheme::cardBg, UITheme::cardBorder);
    juce::Label label;
    const auto options = combo.getLookAndFeel().getOptionsForComboBoxPopupMenu(combo, label);
    expect(options.getMaximumNumColumns() == 1 &&
               options.getMinimumWidth() >= combo.getWidth() &&
               options.getStandardItemHeight() == touchOptimisedPopupItemHeight(),
           "combo uses one column and the shared width/touch metrics");
    combo.setLookAndFeel(nullptr);
  }
  std::cout << "Input controls: all checks passed\n";
}
