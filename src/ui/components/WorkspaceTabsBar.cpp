#include "WorkspaceTabsBar.h"

#include "../../data/Localization.h"

WorkspaceTabsBar::WorkspaceTabsBar()
    : mixerButton(L10n::tr("Mixer", "Mixer")),
      libraryButton(L10n::tr("Biblioteca", "Library")) {
  for (auto *button : {&mixerButton, &libraryButton}) {
    button->setClickingTogglesState(true);
    button->setRadioGroupId(0x574B54);
    addAndMakeVisible(*button);
  }

  mixerButton.onClick = [this] { setActiveTab(WorkspaceTab::Mixer, true); };
  libraryButton.onClick = [this] { setActiveTab(WorkspaceTab::Library, true); };

  applyButtonStyles();
  updateButtonToggles();
}

void WorkspaceTabsBar::setOnTabChanged(std::function<void(WorkspaceTab)> cb) {
  onTabChanged = std::move(cb);
}

void WorkspaceTabsBar::setActiveTab(const WorkspaceTab tab,
                                    const bool sendCallback) {
  if (activeTab == tab && !sendCallback) {
    updateButtonToggles();
    return;
  }

  const bool changed = activeTab != tab;
  activeTab = tab;
  updateButtonToggles();

  if ((changed || sendCallback) && onTabChanged)
    onTabChanged(activeTab);
}

WorkspaceTab WorkspaceTabsBar::getActiveTab() const { return activeTab; }

void WorkspaceTabsBar::refreshThemeColours() {
  applyButtonStyles();
  repaint();
}

void WorkspaceTabsBar::paint(juce::Graphics &g) {
  juce::ignoreUnused(g);
}

void WorkspaceTabsBar::resized() {
  auto area = getLocalBounds().reduced(1, 0);
  const int gap = 3;
  const int buttonW = (area.getWidth() - gap) / 2;
  auto mixerBounds = area.removeFromLeft(buttonW);
  area.removeFromLeft(gap);
  auto libraryBounds = area;

  const bool mixerActive = activeTab == WorkspaceTab::Mixer;
  const int inactiveInsetTop = 3;
  if (mixerActive)
    libraryBounds.removeFromTop(inactiveInsetTop);
  else
    mixerBounds.removeFromTop(inactiveInsetTop);

  mixerButton.setBounds(mixerBounds);
  libraryButton.setBounds(libraryBounds);
}

void WorkspaceTabsBar::applyButtonStyles() {
  for (auto *button : {&mixerButton, &libraryButton}) {
    button->setColour(
        juce::TextButton::buttonColourId,
        UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.24f));
    button->setColour(juce::TextButton::buttonOnColourId,
                      UITheme::panelDark.interpolatedWith(UITheme::cardBg, 0.44f));
    button->setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
    button->setColour(juce::TextButton::textColourOnId, UITheme::accent);
  }
}

void WorkspaceTabsBar::updateButtonToggles() {
  mixerButton.setToggleState(activeTab == WorkspaceTab::Mixer,
                             juce::dontSendNotification);
  libraryButton.setToggleState(activeTab == WorkspaceTab::Library,
                               juce::dontSendNotification);
  resized();
  repaint();
}
