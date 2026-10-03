#include "ui/panels/SettingsPanel.h"
#include "data/ApplicationDataPaths.h"
#include "data/SongData.h"

#include <cmath>
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
  static void snapshot(SettingsPanel &panel, const juce::String &name) {
    const auto output = juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_SETTINGS_SNAPSHOTS", {});
    if (output.isEmpty()) return;
    juce::File folder(output);
    folder.createDirectory();
    auto stream = folder.getChildFile(name + ".png").createOutputStream();
    expect(stream != nullptr, "snapshot output opens");
    stream->setPosition(0);
    stream->truncate();
    juce::PNGImageFormat png;
    expect(png.writeImageToStream(panel.createComponentSnapshot(panel.getLocalBounds()), *stream),
           "settings preview renders");
  }

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

    using Destination = SettingsPanel::Destination;
    using Mode = playworship::remote::RemoteSession::Mode;
    panel.setSize(390, 780);
    panel.showSettingsPage(Destination::Home);
    auto *home = panel.settingsViewport.getViewedComponent();
    expect(home != &panel.tabs && !panel.generalTabButton.isVisible() &&
               !panel.backButton.isVisible(), "compact settings opens with the grouped index");
    expect(home->getNumChildComponents() == (useTouchOptimisedControls() ? 8 : 9),
           "mobile groups audio with routing while desktop keeps its audio page");
    int learningRequests = 0;
    panel.onLearningRequested = [&] { ++learningRequests; };
    snapshot(panel, "settings-home");

    for (auto *child : home->getChildren()) {
      auto *button = dynamic_cast<juce::Button *>(child);
      expect(button != nullptr && button->getHeight() >= 48,
             "each destination is an accessible touch target");
      panel.settingsViewport.setViewPosition(0, 10000);
      const int homeY = panel.settingsViewport.getViewPositionY();
      button->onClick();
      if (button->getButtonText() == "Aprender PlayWorship") {
        expect(learningRequests == 1 && panel.destination == Destination::Home &&
                   panel.settingsViewport.getViewedComponent() == home,
               "the learning row requests the guide without opening an empty settings page");
        continue;
      }
      expect(panel.settingsViewport.getViewedComponent() == &panel.tabs &&
                 panel.backButton.isVisible(), "a grouped row opens its existing settings page");
      expect(panel.closeButton.isVisible() && panel.getLocalBounds().contains(panel.closeButton.getBounds()) &&
                 panel.closeButton.getWidth() >= 44,
             "every detail keeps its close action visible inside the header");
      expect(panel.getComponentAt(panel.closeButton.getBounds().getCentre()) == &panel.closeButton,
             "scrolling content cannot cover the close action");
      expect(panel.settingsViewport.getViewPositionY() == 0,
             "detail starts at its own top");
      expect(panel.settingsViewport.getY() == 64 &&
                 panel.backButton.getWidth() == panel.closeButton.getWidth() &&
                 panel.backButton.getHeight() == panel.closeButton.getHeight(),
             "detail controls begin below the shared header actions");
      snapshot(panel, "settings-detail-" + juce::String(static_cast<int>(panel.destination)));
      panel.backButton.onClick();
      expect(panel.settingsViewport.getViewedComponent() == home &&
                 panel.settingsViewport.getViewPositionY() == homeY,
             "back returns to the same position in the grouped index");
    }

    // Dragging over a row must scroll without accidentally opening a page.
    auto *row = home->getChildComponent(0);
    row->mouseDown(mouseEvent(*row));
    row->mouseUp(mouseEvent(*row, true));
    expect(panel.destination == Destination::Home, "drag does not navigate");

    panel.showSettingsPage(Destination::Remote);
    const int localRemoteHeight = panel.mobileGeneralCardHeight();
    int modeChanges = 0;
    panel.onRemoteModeChanged = [&](Mode mode) {
      ++modeChanges;
      panel.updateRemoteState(mode, {}, "Buscando servidores");
    };
    panel.remoteClientButton.onClick();
    expect(modeChanges == 1 && panel.remoteServersBox.isVisible() &&
               !panel.remoteServerNameEditor.isVisible() &&
               panel.mobileGeneralCardHeight() > localRemoteHeight,
           "client selector reveals discovery and expands the compact local card");
    panel.showSettingsPage(Destination::Appearance);
    panel.updateRemoteState(Mode::Server, {}, "Servidor activo");
    expect(!panel.remoteServerNameEditor.isVisible() && panel.themeBox.isVisible(),
           "background remote updates cannot reveal remote controls on another page");
    panel.showSettingsPage(Destination::Remote);
    expect(panel.remoteServerNameEditor.isVisible() && !panel.remoteServersBox.isVisible(),
           "remote state survives navigation");
    expect(panel.remoteTargetHelp.isVisible() &&
               panel.remoteTargetHelp.getText().containsIgnoreCase("nombre"),
           "server mode explains its visible name");
    expect(panel.generalPage.getLocalBounds().contains(panel.remoteServerNameEditor.getBounds()) &&
               panel.remoteModeControl.getHeight() >= 44,
           "remote discovery, alias and segmented controls fit in portrait");
    snapshot(panel, "settings-remote");

    for (const auto width : {320, 390, 560}) {
      panel.setSize(width, 640);
      for (const auto page : {Destination::Appearance, Destination::Playback, Destination::Library}) {
        panel.showSettingsPage(page);
        for (auto *control : panel.generalPage.getChildren())
          if (control->isVisible())
            expect(!control->getBounds().isEmpty() && panel.generalPage.getLocalBounds().contains(control->getBounds()),
                   "every visible preference fits in narrow pages");
        const juce::Label *const helps[] = {
            &panel.themeHelp, &panel.languageHelp, &panel.expandLaunchHelp,
            &panel.primeImportHelp, &panel.stemOptimizationHelp,
            &panel.padsCrossfadeHelp, &panel.metronomeClickStyleHelp};
        int visibleHelps = 0;
        for (const auto *help : helps)
          if (help->isVisible()) {
            ++visibleHelps;
            expect(help->getText().isNotEmpty() &&
                       panel.generalPage.getLocalBounds().contains(help->getBounds()),
                   "each visible preference has help within its card");
          }
        expect(visibleHelps >= 2, "both preferences on a detail have help");
        if (page == Destination::Appearance) {
          expect(panel.themeHelp.getY() - panel.themeBox.getBottom() == 6 &&
                     panel.languageHelp.getY() - panel.languageBox.getBottom() == 6 &&
                     panel.languageLabel.getY() - panel.themeHelp.getBottom() == 12,
                 "choice explanations stay directly below their controls");
        }
        if (page == Destination::Library) {
          expect(panel.primeImportHelp.getY() - panel.primeImportToggle.getBottom() == 4 &&
                     panel.stemOptimizationHelp.getY() - panel.stemOptimizationToggle.getBottom() == 4,
                 "toggle explanations stay directly below their controls");
        }
      }
      panel.showSettingsPage(Destination::Account);
      panel.settingsViewport.setViewPosition(0, 10000);
      const auto support = panel.settingsViewport.getLocalArea(&panel.infoLinkButton, panel.infoLinkButton.getLocalBounds());
      expect(support.getBottom() <= panel.settingsViewport.getHeight(),
             "account actions remain reachable at the end of the page");
      expect(panel.actionPurgeHelp.getText().containsIgnoreCase("biblioteca") &&
                 panel.accountPage.getLocalBounds().contains(
                     panel.actionPurgeHelp.getBounds()),
             "the purge action explains that it empties the local Library");
      const auto helpSize = panel.themeHelp.getFont().getHeight();
      const auto sameHelpSize = [helpSize](const juce::Label &help) {
        return std::abs(help.getFont().getHeight() - helpSize) < 0.01f;
      };
      expect(sameHelpSize(panel.actionPurgeHelp) &&
                 sameHelpSize(panel.audioHelp) &&
                 sameHelpSize(panel.routingControlsHelp) &&
                 sameHelpSize(panel.diagnosticsPrivacyHint),
             "all settings explanations share the same type scale");
    }

    panel.setSize(390, 780);
    panel.showSettingsPage(Destination::Account);
    panel.settingsViewport.setViewPosition(0, 10000);
    snapshot(panel, "settings-account-actions");
    panel.showSettingsPage(Destination::Audio);
    if (useTouchOptimisedControls())
      expect(panel.destination == Destination::Routing,
             "mobile audio navigation resolves to the consolidated routing page");
    else
      expect(panel.audioHelp.getText().isNotEmpty() &&
                 panel.audioPage.getLocalBounds().contains(panel.audioHelp.getBounds()),
             "desktop audio page explains how its outputs feed routing");
    panel.showSettingsPage(Destination::Routing);
    expect(panel.routingDeviceBox.isVisible() == useTouchOptimisedControls(),
           "the audio output selector belongs to routing only on mobile");
    if (useTouchOptimisedControls())
      expect(panel.routingDeviceBox.getHeight() >= 48 &&
                 panel.routingDeviceBox.getBottom() < panel.routingSectionLabel.getY(),
             "the mobile device selector is a full-size control before category routing");
    expect(panel.routingControlsHelp.getText().containsIgnoreCase("Mono") &&
               panel.routingControlsHelp.getBottom() <= panel.routingContent.getY(),
           "routing explains mono, stereo and channel selectors before the list");

    panel.setSize(1080, 780);
    panel.generalTabButton.onClick();
    expect(panel.generalTabButton.isVisible() && !panel.backButton.isVisible() &&
               panel.themeBox.isVisible() && panel.remoteModeControl.isVisible(),
           "wide desktop retains its full settings navigation and controls");
    expect(panel.learningButton.isVisible() && panel.learningButton.getHeight() >= 36 &&
               panel.generalPage.getLocalBounds().contains(panel.learningButton.getBounds()),
           "desktop General retains an accessible guide entry after compact navigation");
    panel.learningButton.onClick();
    expect(learningRequests == 2,
           "the desktop guide button shares the same learning request as the grouped row");
    snapshot(panel, "settings-desktop");
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
  std::cout << "Settings: grouped navigation, remote state, layout, scrolling and gestures passed\n";
}
