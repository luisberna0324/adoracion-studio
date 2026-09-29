#pragma once

#include <JuceHeader.h>
#include <functional>

/** Shared in-app dialog for short input, confirmation and information. */
class AppModal final : public juce::Component, private juce::ComponentListener,
                       private juce::Timer {
public:
  enum class Kind { Input, Confirm, Destructive, Information };
  using Result = std::function<void(bool, const juce::String &)>;

  AppModal(juce::Component &source, Kind kind, juce::String title,
           juce::String message, juce::String primaryText,
           juce::String secondaryText, Result result,
           juce::String fieldLabel = {}, juce::String initialValue = {});
  ~AppModal() override;

  std::function<void()> onClosed;
  void show();
  void paint(juce::Graphics &) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &) override;
  void inputAttemptWhenModal() override;

private:
  class ActionButton;
  void componentMovedOrResized(juce::Component &, bool, bool) override;
  void timerCallback() override;
  void updateInputState();
  void finish(bool accepted);

  juce::Component::SafePointer<juce::Component> host;
  Kind kind;
  Result result;
  juce::String heading;
  juce::Rectangle<int> cardBounds;
  juce::Label titleLabel, messageLabel, fieldLabelComponent, helperLabel;
  juce::TextEditor valueEditor;
  std::unique_ptr<ActionButton> primaryButton, secondaryButton;
  bool finished = false;
  int lastKeyboardInset = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppModal)
};
