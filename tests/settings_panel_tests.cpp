#include "ui/panels/SettingsPanel.h"
#include "data/ApplicationDataPaths.h"
#include "data/SongData.h"

#include <cstdlib>
#include <iostream>

namespace {
void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

juce::MouseEvent mouseEvent(juce::Component &component, bool dragged = false) {
  const auto position = component.getLocalBounds().toFloat().getCentre();
  const auto now = juce::Time::getCurrentTime();
  return {juce::Desktop::getInstance().getMainMouseSource(), position, {},
          1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component,
          now, position, now, 1, dragged};
}
}

struct SettingsPanelTestAccess {
  static void run() {
    int routingChanges = 0;
    SettingsPanel panel(ThemePreset::Spotify, {}, AppLanguage::Spanish, {},
        false, {}, false, {}, false, {}, PadsCrossfadePreset{}, {},
        MetronomeClickStyle{}, {}, {}, {}, {}, {}, {}, 8,
        [&](const auto &, const auto &) { ++routingChanges; }, nullptr, 0, 8);
    panel.setVisible(true);

    // Portrait phones, landscape phones/tablets, narrow desktop windows, and
    // tall desktops exercise both row layouts and the scrollbar threshold.
    for (const auto size : {juce::Point<int>(320, 640), {390, 844}, {740, 360},
                            {800, 600}, {1180, 820}, {1440, 1000}, {1440, 1500},
                            {560, 1200}, {560, 700}}) {
      panel.setSize(size.x, size.y);
      panel.routingTabButton.onClick();
      auto &viewport = panel.settingsViewport;
      expect(panel.routingContent.getNumChildComponents() ==
                 static_cast<int>(kTrackCategories.size()), "all routing categories exist");
      expect(panel.routingPage.getLocalBounds().contains(panel.routingContent.getBounds()),
             "the scrolling tab contains the entire routing list, including its final rows");
      expect(panel.routingPage.getLocalBounds().contains(panel.routingHintLabel.getBounds()) &&
                 panel.routingHintLabel.getY() >= panel.routingContent.getBottom(),
             "routing footer follows the rows without clipping or overlap");
      expect(!viewport.getHorizontalScrollBar().isVisible() &&
                 panel.tabs.getWidth() <= viewport.getMaximumVisibleWidth(),
             "routing needs no horizontal scrolling and reserves scrollbar width");

      AdaptiveComboBox *lastSelector = nullptr;
      for (auto *row : panel.routingContent.getChildren()) {
        juce::Rectangle<int> previousControl;
        for (auto *child : row->getChildren()) {
          expect(row->getLocalBounds().contains(child->getBounds()) &&
                     !child->getBounds().isEmpty(), "every label and selector fits its row");
          if (auto *selector = dynamic_cast<AdaptiveComboBox *>(child)) {
            expect(selector->getWidth() >= 100 && selector->getHeight() >=
                       (row->getWidth() < 520 || useTouchOptimisedControls() ? 44 : 36),
                   "selectors retain usable dimensions on narrow and touch layouts");
            expect(!selector->getBounds().intersects(previousControl),
                   "mode and channel selectors do not overlap");
            previousControl = selector->getBounds();
            lastSelector = selector;
          }
        }
      }
      expect(lastSelector != nullptr, "routing exposes a channel selector");

      viewport.setViewPosition(0, 0);
      const auto headerBounds = panel.title.getBounds();
      const auto navigationBounds = panel.routingTabButton.getBounds();
      const int selectedId = lastSelector->getSelectedId();
      juce::MouseWheelDetails wheel;
      wheel.deltaY = -1.0f;
      // Feed the production event path through a child control, not directly
      // into the viewport: scrolling must not reroute audio under the pointer.
      for (int i = 0; i < 100; ++i)
        lastSelector->mouseWheelMove(mouseEvent(*lastSelector), wheel);
      expect(lastSelector->getSelectedId() == selectedId && routingChanges == 0,
             "wheel over routing controls scrolls without changing output");
      const auto lastBounds = viewport.getLocalArea(lastSelector,
                                                    lastSelector->getLocalBounds());
      expect(lastBounds.getY() >= 0 && lastBounds.getBottom() <= viewport.getHeight(),
             "scrolling reaches the complete final routing selector");
      const auto footerBounds = viewport.getLocalArea(&panel.routingHintLabel,
                                                      panel.routingHintLabel.getLocalBounds());
      expect(footerBounds.getY() >= 0 && footerBounds.getBottom() <= viewport.getHeight(),
             "the routing footer is fully reachable at the end of the list");
      expect(panel.title.getBounds() == headerBounds &&
                 panel.routingTabButton.getBounds() == navigationBounds,
             "header and navigation stay fixed while scrolling");

      const int scrollY = viewport.getViewPositionY();
      panel.resized();
      expect(viewport.getViewPositionY() == scrollY,
             "relayout preserves the current scroll position");
      lastSelector->mouseDown(mouseEvent(*lastSelector));
      lastSelector->mouseUp(mouseEvent(*lastSelector, true));
      expect(!lastSelector->isPopupActive() && routingChanges == 0,
             "a drag across a routing selector does not open or change it");

      panel.generalTabButton.onClick();
      panel.routingTabButton.onClick();
      expect(viewport.getViewPositionY() == 0,
             "switching tabs starts routing at the top of its own content");
    }
  }
};

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr)
    return 77;
#endif
  if (juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_CREATION_TEST_ROOT", {}).isEmpty())
    return 77;
  (void) ApplicationDataPaths::isolatedTestRoot();
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  SettingsPanelTestAccess::run();
  std::cout << "Settings routing: layout, scrolling and gesture checks passed\n";
}
