#include "AppModal.h"
#include "../UiCommon.h"
#include "../../data/Localization.h"

class AppModal::ActionButton final : public juce::Button {
public:
  ActionButton(juce::String text, bool isPrimary, bool isDestructive)
      : juce::Button(text), primary(isPrimary), destructive(isDestructive) {
    setButtonText(text);
    setTitle(text);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }

  void paintButton(juce::Graphics &g, bool over, bool down) override {
    auto area = getLocalBounds().toFloat().reduced(0.5f);
    auto fill = primary ? (destructive ? AppColours::accentDestructive
                                         : UITheme::accent)
                        : UITheme::cardBgHover;
    if (over || hasKeyboardFocus(true)) fill = fill.brighter(0.12f);
    if (down) fill = fill.darker(0.10f);
    if (!isEnabled()) fill = UITheme::cardBgHover;
    g.setColour(fill);
    g.fillRoundedRectangle(area, 12.0f);
    if (!primary || !isEnabled() || hasKeyboardFocus(true)) {
      g.setColour(hasKeyboardFocus(true) ? UITheme::accent
                                         : UITheme::cardBorder);
      g.drawRoundedRectangle(area.reduced(0.7f), 11.5f,
                             hasKeyboardFocus(true) ? 2.0f : 1.0f);
    }
    g.setColour(!isEnabled() ? UITheme::textSecondary
                             : destructive ? juce::Colours::white
                             : primary ? UITheme::onAccent
                                       : UITheme::textPrimary);
    g.setFont(makeAppFont(14.0f, AppFontWeight::SemiBold));
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(8, 2),
                     juce::Justification::centred, 1);
  }

private:
  bool primary, destructive;
};

AppModal::AppModal(juce::Component &source, Kind requestedKind,
                   juce::String title, juce::String message,
                   juce::String primaryText, juce::String secondaryText,
                   Result callback, juce::String fieldLabel,
                   juce::String initialValue)
    : host(source.getTopLevelComponent() != nullptr
               ? source.getTopLevelComponent() : &source), kind(requestedKind),
      result(std::move(callback)), heading(std::move(title)) {
  setTitle(heading);
  setWantsKeyboardFocus(true);
  setFocusContainerType(FocusContainerType::keyboardFocusContainer);
  setInterceptsMouseClicks(true, true);

  titleLabel.setText(heading, juce::dontSendNotification);
  titleLabel.setFont(makeAppFont(20.0f, AppFontWeight::SemiBold));
  titleLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  titleLabel.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(titleLabel);

  messageLabel.setText(message, juce::dontSendNotification);
  messageLabel.setFont(makeAppFont(13.5f));
  messageLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  messageLabel.setJustificationType(juce::Justification::topLeft);
  messageLabel.setMinimumHorizontalScale(1.0f);
  messageLabel.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(messageLabel);

  if (kind == Kind::Input) {
    fieldLabelComponent.setText(fieldLabel, juce::dontSendNotification);
    fieldLabelComponent.setFont(makeAppFont(12.5f, AppFontWeight::Medium));
    fieldLabelComponent.setColour(juce::Label::textColourId, UITheme::textPrimary);
    fieldLabelComponent.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(fieldLabelComponent);

    valueEditor.setText(initialValue, false);
    valueEditor.setFont(makeAppFont(16.0f));
    valueEditor.setMultiLine(false);
    valueEditor.setReturnKeyStartsNewLine(false);
    valueEditor.setTextToShowWhenEmpty(fieldLabel, UITheme::textSecondary);
    applyTextEditorTheme(valueEditor);
    valueEditor.onTextChange = [this] { updateInputState(); };
    valueEditor.onReturnKey = [this] { finish(true); };
    valueEditor.onEscapeKey = [this] { finish(false); };
    addAndMakeVisible(valueEditor);

    helperLabel.setText(L10n::tr("Escribe un nombre para continuar.",
                                    "Enter a name to continue."),
                        juce::dontSendNotification);
    helperLabel.setFont(makeAppFont(11.5f));
    helperLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
    helperLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(helperLabel);
  }

  primaryButton = std::make_unique<ActionButton>(
      std::move(primaryText), true, kind == Kind::Destructive);
  primaryButton->onClick = [this] { finish(true); };
  addAndMakeVisible(*primaryButton);
  if (kind != Kind::Information) {
    secondaryButton = std::make_unique<ActionButton>(
        std::move(secondaryText), false, false);
    secondaryButton->onClick = [this] { finish(false); };
    addAndMakeVisible(*secondaryButton);
  }
  updateInputState();
}

AppModal::~AppModal() {
  if (host != nullptr) host->removeComponentListener(this);
  if (isCurrentlyModal()) exitModalState(0);
}

void AppModal::show() {
  if (host == nullptr) return;
  host->addComponentListener(this);
  host->addAndMakeVisible(*this);
  setBounds(host->getLocalBounds());
  toFront(false);
  enterModalState(true);
  if (kind == Kind::Input) startTimerHz(10);
  if (kind == Kind::Input) valueEditor.grabKeyboardFocus();
  else primaryButton->grabKeyboardFocus();
}

void AppModal::paint(juce::Graphics &g) {
  g.fillAll(juce::Colours::black.withAlpha(0.58f));
  auto card = cardBounds.toFloat();
  juce::DropShadow(juce::Colours::black.withAlpha(0.30f), 18, {0, 8})
      .drawForRectangle(g, cardBounds);
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(card, 18.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.8f));
  g.drawRoundedRectangle(card.reduced(0.5f), 17.5f, 1.0f);
}

void AppModal::resized() {
  auto available = getLocalBounds();
  if (auto *display = juce::Desktop::getInstance().getDisplays()
                          .getDisplayForRect(getScreenBounds())) {
    auto insets = display->safeAreaInsets;
    insets.setBottom(juce::jmax(insets.getBottom(),
                                display->keyboardInsets.getBottom()));
    available = insets.subtractedFrom(available);
  }
  available.reduce(16, 16);
  const int width = juce::jmin(440, available.getWidth());
  const int desiredHeight = kind == Kind::Input ? 290 : 240;
  const int height = juce::jmin(desiredHeight, available.getHeight());
  cardBounds = juce::Rectangle<int>(width, height);
  cardBounds.setCentre(available.getCentre());

  auto inner = cardBounds.reduced(22, 20);
  titleLabel.setBounds(inner.removeFromTop(29));
  inner.removeFromTop(9);
  messageLabel.setBounds(inner.removeFromTop(kind == Kind::Input ? 43 : 79));
  if (kind == Kind::Input) {
    inner.removeFromTop(8);
    fieldLabelComponent.setBounds(inner.removeFromTop(20));
    inner.removeFromTop(5);
    valueEditor.setBounds(inner.removeFromTop(48));
    helperLabel.setBounds(inner.removeFromTop(22));
  }
  auto actions = cardBounds.reduced(22, 20).removeFromBottom(48);
  if (secondaryButton != nullptr) {
    const int gap = 10;
    const int half = (actions.getWidth() - gap) / 2;
    secondaryButton->setBounds(actions.removeFromLeft(half));
    actions.removeFromLeft(gap);
  }
  primaryButton->setBounds(actions);
}

bool AppModal::keyPressed(const juce::KeyPress &key) {
  if (key == juce::KeyPress::escapeKey) { finish(false); return true; }
  if (key == juce::KeyPress::returnKey) { finish(true); return true; }
  return false;
}

void AppModal::inputAttemptWhenModal() {}

void AppModal::componentMovedOrResized(juce::Component &component, bool, bool) {
  if (host != nullptr && &component == host.getComponent())
    setBounds(host->getLocalBounds());
}

void AppModal::timerCallback() {
  if (auto *display = juce::Desktop::getInstance().getDisplays()
                          .getDisplayForRect(getScreenBounds())) {
    const int keyboardInset = display->keyboardInsets.getBottom();
    if (keyboardInset != lastKeyboardInset) {
      lastKeyboardInset = keyboardInset;
      resized();
      repaint();
    }
  }
}

void AppModal::updateInputState() {
  if (kind != Kind::Input) return;
  const bool valid = valueEditor.getText().trim().isNotEmpty();
  primaryButton->setEnabled(valid);
  helperLabel.setVisible(!valid);
}

void AppModal::finish(bool accepted) {
  if (finished) return;
  if (accepted && kind == Kind::Input &&
      valueEditor.getText().trim().isEmpty()) {
    updateInputState();
    return;
  }
  finished = true;
  const auto value = kind == Kind::Input ? valueEditor.getText().trim()
                                         : juce::String();
  auto resultCallback = std::move(result);
  auto closedCallback = std::move(onClosed);
  exitModalState(accepted ? 1 : 0);
  setVisible(false);
  juce::MessageManager::callAsync(
      [closed = std::move(closedCallback),
       completed = std::move(resultCallback),
       accepted, value]() mutable {
        if (closed) closed();
        if (completed) completed(accepted, value);
      });
}
