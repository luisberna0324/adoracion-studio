#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>

enum class WorkspaceTab { Mixer = 0, Library = 1 };

class WorkspaceTabsBar final : public juce::Component {
public:
  WorkspaceTabsBar();

  void setOnTabChanged(std::function<void(WorkspaceTab)> cb);
  void setActiveTab(WorkspaceTab tab, bool sendCallback = false);
  WorkspaceTab getActiveTab() const;

  void refreshThemeColours();

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  juce::TextButton mixerButton;
  juce::TextButton libraryButton;
  WorkspaceTab activeTab = WorkspaceTab::Mixer;
  std::function<void(WorkspaceTab)> onTabChanged;

  void applyButtonStyles();
  void updateButtonToggles();
};
