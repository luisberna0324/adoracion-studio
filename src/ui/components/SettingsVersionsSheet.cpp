#include "SettingsVersionsSheet.h"
#include "../../data/Localization.h"
#include "../UiCommon.h"

class SettingsVersionsSheet::ActionButton final : public juce::Button {
public:
  explicit ActionButton(juce::String label) : juce::Button(label) {
    setTitle(label);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }

  void paintButton(juce::Graphics &g, bool over, bool down) override {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    auto fill = UITheme::cardBgHover;
    if (over || hasKeyboardFocus(true)) fill = fill.brighter(0.10f);
    if (down) fill = fill.darker(0.12f);
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 11.0f);
    if (hasKeyboardFocus(true)) {
      g.setColour(UITheme::accent);
      g.drawRoundedRectangle(bounds.reduced(1.0f), 10.0f, 2.0f);
    }
    g.setColour(UITheme::textPrimary);
    g.setFont(makeAppFont(13.5f, AppFontWeight::SemiBold));
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(10, 4),
                     juce::Justification::centred, 1);
  }
};

SettingsVersionsSheet::SettingsVersionsSheet() {
  setTitle(L10n::tr("Versiones disponibles", "Available versions"));
  setWantsKeyboardFocus(true);
  setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);

  title.setText(L10n::tr("Versiones disponibles", "Available versions"),
                juce::dontSendNotification);
  title.setFont(makeAppFont(19.0f, AppFontWeight::SemiBold));
  title.setColour(juce::Label::textColourId, UITheme::textPrimary);
  addAndMakeVisible(title);

  description.setText(
      L10n::tr("Selecciona una versión para ver los cambios.",
               "Select a version to view its changes."),
      juce::dontSendNotification);
  description.setFont(makeAppFont(12.0f));
  description.setColour(juce::Label::textColourId, UITheme::textSecondary);
  addAndMakeVisible(description);

  status.setMultiLine(true, true);
  status.setReadOnly(true);
  status.setCaretVisible(false);
  status.setScrollbarsShown(false);
  status.setFont(makeAppFont(13.0f));
  status.setColour(juce::TextEditor::backgroundColourId,
                   juce::Colours::transparentBlack);
  status.setColour(juce::TextEditor::textColourId, UITheme::textSecondary);
  status.setColour(juce::TextEditor::outlineColourId,
                   juce::Colours::transparentBlack);
  status.setColour(juce::TextEditor::focusedOutlineColourId,
                   juce::Colours::transparentBlack);
  addAndMakeVisible(status);

  actionsViewport.setViewedComponent(&actionsContent, false);
  actionsViewport.setScrollBarsShown(true, false);
  actionsViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  actionsViewport.setWantsKeyboardFocus(false);
  addAndMakeVisible(actionsViewport);

  setSize(300, 138);
  rebuild();
}

SettingsVersionsSheet::~SettingsVersionsSheet() {
  actionsViewport.setViewedComponent(nullptr, false);
}

void SettingsVersionsSheet::addAction(juce::String label,
                                      std::function<void()> callback) {
  auto button = std::make_unique<ActionButton>(std::move(label));
  const auto safe = juce::Component::SafePointer<SettingsVersionsSheet>(this);
  button->onClick = [safe, action = std::move(callback)] {
    juce::MessageManager::callAsync([safe, action] {
      if (safe != nullptr && safe->isShowing()) action();
    });
  };
  actionsContent.addAndMakeVisible(*button);
  actions.push_back(std::move(button));
}

void SettingsVersionsSheet::showLoading() {
  view = View::Loading;
  rebuild();
}

void SettingsVersionsSheet::showError(const juce::String &message) {
  view = View::Error;
  errorMessage = message;
  rebuild();
}

void SettingsVersionsSheet::setVersions(
    std::vector<CloudSettingsVersion> items) {
  versions = std::move(items);
  view = View::List;
  rebuild();
}

void SettingsVersionsSheet::dismiss() {
  if (onDismiss) onDismiss();
}

bool SettingsVersionsSheet::keyPressed(const juce::KeyPress &key) {
  if (key == juce::KeyPress::escapeKey) {
    dismiss();
    return true;
  }
  return false;
}

void SettingsVersionsSheet::updatePreferredSize() {
  constexpr int headerHeight = 72;
  constexpr int rowHeight = 40;
  constexpr int rowGap = 5;
  const int visibleRows = view == View::List
                              ? juce::jlimit(1, 3, static_cast<int>(actions.size()))
                              : 1;
  const int bodyHeight = visibleRows * rowHeight +
                         juce::jmax(0, visibleRows - 1) * rowGap;
  setSize(300, headerHeight + bodyHeight + 14);
}

void SettingsVersionsSheet::focusFirstAction() {
  const auto safe = juce::Component::SafePointer<SettingsVersionsSheet>(this);
  juce::MessageManager::callAsync([safe] {
    if (safe == nullptr || !safe->isShowing()) return;
    if (!safe->actions.empty()) safe->actions.front()->grabKeyboardFocus();
    else safe->grabKeyboardFocus();
  });
}

void SettingsVersionsSheet::rebuild() {
  actions.clear();
  actionsViewport.setViewPosition(0, 0);
  status.setVisible(view != View::List || versions.empty());
  actionsViewport.setVisible(view == View::List && !versions.empty());

  if (view == View::Loading) {
    status.setText(L10n::tr("Consultando versiones…", "Loading versions…"),
                   false);
  } else if (view == View::Error) {
    status.setText(errorMessage, false);
    addAction(L10n::tr("Reintentar", "Retry"),
              [this] { if (onRetry) onRetry(); });
  } else if (versions.empty()) {
    status.setText(
        L10n::tr("Todavía no hay versiones sincronizadas.",
                 "There are no synced versions yet."),
        false);
  } else {
    for (size_t index = 0; index < versions.size(); ++index) {
      const auto &version = versions[index];
      const auto label =
          "v" + juce::String(version.revision) + " · " +
          juce::Time(version.createdAtMs).formatted("%d/%m/%Y %H:%M");
      addAction(label, [this, index] {
        if (onVersionSelected) onVersionSelected(versions[index]);
      });
    }
  }

  updatePreferredSize();
  resized();
  repaint();
  focusFirstAction();
}

void SettingsVersionsSheet::paint(juce::Graphics &g) {
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(getLocalBounds().toFloat(), 16.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.80f));
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 16.0f,
                         1.0f);
}

void SettingsVersionsSheet::resized() {
  auto area = getLocalBounds().reduced(10);
  title.setBounds(area.removeFromTop(25).reduced(3, 0));
  description.setBounds(area.removeFromTop(23).reduced(3, 0));
  area.removeFromTop(8);
  status.setBounds(area.reduced(4, 0));
  actionsViewport.setBounds(area);

  constexpr int rowHeight = 40;
  constexpr int rowGap = 5;
  const int contentHeight = static_cast<int>(actions.size()) * rowHeight +
                            juce::jmax(0, static_cast<int>(actions.size()) - 1) * rowGap;
  actionsContent.setSize(juce::jmax(1, actionsViewport.getMaximumVisibleWidth()),
                         juce::jmax(1, contentHeight));
  auto rows = actionsContent.getLocalBounds();
  for (auto &action : actions) {
    action->setBounds(rows.removeFromTop(rowHeight));
    rows.removeFromTop(rowGap);
  }
}

SettingsVersionsSheetOverlay::SettingsVersionsSheetOverlay(
    juce::Component &parent, std::unique_ptr<SettingsVersionsSheet> sheet,
    juce::Rectangle<int> anchor)
    : owner(parent), content(std::move(sheet)),
      sourceAnchor(anchor),
      previousFocus(juce::Component::getCurrentlyFocusedComponent()) {
  owner.addComponentListener(this);
  content->addComponentListener(this);
  addAndMakeVisible(*content);
  const auto safe =
      juce::Component::SafePointer<SettingsVersionsSheetOverlay>(this);
  content->onDismiss = [safe] {
    if (safe != nullptr) safe->dismiss();
  };
  setWantsKeyboardFocus(true);
}

SettingsVersionsSheetOverlay::~SettingsVersionsSheetOverlay() {
  juce::Desktop::getInstance().getAnimator().cancelAnimation(content.get(), false);
  content->removeComponentListener(this);
  owner.removeComponentListener(this);
  if (isCurrentlyModal()) exitModalState(0);
}

void SettingsVersionsSheetOverlay::show() {
  owner.addAndMakeVisible(*this);
  setBounds(owner.getLocalBounds());
  toFront(false);
  enterModalState(false);
  content->grabKeyboardFocus();
}

void SettingsVersionsSheetOverlay::dismiss() {
  juce::Desktop::getInstance().getAnimator().cancelAnimation(content.get(), false);
  exitModalState(0);
  setVisible(false);
  if (previousFocus != nullptr && previousFocus->isShowing())
    previousFocus->grabKeyboardFocus();
}

void SettingsVersionsSheetOverlay::paint(juce::Graphics &g) {
  juce::ignoreUnused(g);
}

void SettingsVersionsSheetOverlay::resized() {
  auto available = getLocalBounds();
  if (const auto *display = juce::Desktop::getInstance()
                                .getDisplays()
                                .getDisplayForRect(getScreenBounds())) {
    const auto insets = display->safeAreaInsets;
    const int horizontal = juce::jmax(insets.getLeft(), insets.getRight());
    available = juce::BorderSize<int>(insets.getTop(), horizontal,
                                      insets.getBottom(), horizontal)
                    .subtractedFrom(available);
  }
  available = available.reduced(8);
  const int width = juce::jmin(content->getWidth(), available.getWidth());
  const int height = juce::jmin(content->getHeight(), available.getHeight());
  constexpr int gap = 8;
  int x = sourceAnchor.getRight() + gap;
  if (x + width > available.getRight())
    x = sourceAnchor.getX() - gap - width;
  x = juce::jlimit(available.getX(), available.getRight() - width, x);
  const int y = juce::jlimit(available.getY(), available.getBottom() - height,
                             sourceAnchor.getY() - 10);
  content->setBounds(x, y, width, height);
}

void SettingsVersionsSheetOverlay::mouseDown(const juce::MouseEvent &event) {
  if (!content->getBounds().contains(event.getPosition())) dismiss();
}

void SettingsVersionsSheetOverlay::inputAttemptWhenModal() { dismiss(); }

void SettingsVersionsSheetOverlay::componentMovedOrResized(
    juce::Component &component, bool, bool) {
  if (&component == &owner) setBounds(owner.getLocalBounds());
  else resized();
}
