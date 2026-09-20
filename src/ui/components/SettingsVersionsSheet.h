#pragma once

#include "../../data/CloudSettingsClient.h"
#include <JuceHeader.h>

// Compact chooser only. Confirmation and details belong to the next modal.
class SettingsVersionsSheet final : public juce::Component {
public:
  SettingsVersionsSheet();
  ~SettingsVersionsSheet() override;

  std::function<void()> onDismiss;
  std::function<void()> onRetry;
  std::function<void(const CloudSettingsVersion &)> onVersionSelected;

  void showLoading();
  void showError(const juce::String &message);
  void setVersions(std::vector<CloudSettingsVersion> versions);
  void dismiss();
  void paint(juce::Graphics &) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &) override;

private:
  class ActionButton;
  enum class View { Loading, Error, List };
  View view = View::Loading;
  juce::String errorMessage;
  std::vector<CloudSettingsVersion> versions;
  juce::Label title, description;
  juce::TextEditor status;
  juce::Viewport actionsViewport;
  juce::Component actionsContent;
  std::vector<std::unique_ptr<ActionButton>> actions;

  void rebuild();
  void addAction(juce::String title, std::function<void()> callback);
  void updatePreferredSize();
  void focusFirstAction();
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsVersionsSheet)
};

// Transparent owner-sized layer that positions the flat chooser by its source.
class SettingsVersionsSheetOverlay final : public juce::Component,
                                           private juce::ComponentListener {
public:
  SettingsVersionsSheetOverlay(juce::Component &owner,
                               std::unique_ptr<SettingsVersionsSheet> content,
                               juce::Rectangle<int> sourceAnchor);
  ~SettingsVersionsSheetOverlay() override;
  void show();
  void dismiss();
  void paint(juce::Graphics &) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &) override;
  void inputAttemptWhenModal() override;
private:
  juce::Component &owner;
  std::unique_ptr<SettingsVersionsSheet> content;
  juce::Rectangle<int> sourceAnchor;
  juce::Component::SafePointer<juce::Component> previousFocus;
  void componentMovedOrResized(juce::Component &, bool, bool) override;
};
