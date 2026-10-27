#include "ui/panels/SettingsPanel.h"
#include "data/ApplicationDataPaths.h"
#include "data/Localization.h"
#include "data/SongData.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>

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

class RoutingTestDevice final : public juce::AudioIODevice {
public:
  RoutingTestDevice() : AudioIODevice("Routing output", "Routing test") {}
  juce::StringArray getOutputChannelNames() override {
    return {"Out 1", "Out 2", "Out 3", "Out 4", "Out 5", "Out 6", "Out 7", "Out 8"};
  }
  juce::StringArray getInputChannelNames() override { return {}; }
  juce::Array<double> getAvailableSampleRates() override { return {48000.0}; }
  juce::Array<int> getAvailableBufferSizes() override { return {64}; }
  int getDefaultBufferSize() override { return 64; }
  juce::String open(const juce::BigInteger &, const juce::BigInteger &out,
                    double, int) override {
    outputs = out;
    opened = true;
    return {};
  }
  void close() override { stop(); opened = false; }
  bool isOpen() override { return opened; }
  void start(juce::AudioIODeviceCallback *next) override {
    stop();
    callback = next;
    if (callback != nullptr) callback->audioDeviceAboutToStart(this);
  }
  void stop() override {
    if (auto *old = std::exchange(callback, nullptr)) old->audioDeviceStopped();
  }
  bool isPlaying() override { return callback != nullptr; }
  juce::String getLastError() override { return {}; }
  int getCurrentBufferSizeSamples() override { return 64; }
  double getCurrentSampleRate() override { return 48000.0; }
  int getCurrentBitDepth() override { return 32; }
  juce::BigInteger getActiveOutputChannels() const override { return outputs; }
  juce::BigInteger getActiveInputChannels() const override { return {}; }
  int getOutputLatencyInSamples() override { return 0; }
  int getInputLatencyInSamples() override { return 0; }
private:
  bool opened = false;
  juce::BigInteger outputs;
  juce::AudioIODeviceCallback *callback = nullptr;
};

class RoutingTestDeviceType final : public juce::AudioIODeviceType {
public:
  RoutingTestDeviceType() : AudioIODeviceType("Routing test") {}
  void scanForDevices() override {}
  juce::StringArray getDeviceNames(bool) const override { return {"Routing output"}; }
  int getDefaultDeviceIndex(bool) const override { return 0; }
  int getIndexOfDevice(juce::AudioIODevice *, bool) const override { return 0; }
  bool hasSeparateInputsAndOutputs() const override { return false; }
  juce::AudioIODevice *createDevice(const juce::String &, const juce::String &) override {
    return new RoutingTestDevice();
  }
};
}

struct SettingsPanelTestAccess {
  static void testActiveRoutingChannels() {
    juce::AudioDeviceManager manager;
    manager.addAudioDeviceType(std::make_unique<RoutingTestDeviceType>());
    const auto setup = juce::parseXML(
        "<DEVICESETUP deviceType=\"Routing test\" audioOutputDeviceName=\"Routing output\" "
        "audioDeviceRate=\"48000\" audioDeviceBufferSize=\"64\" audioDeviceOutChans=\"11\"/>");
    expect(manager.initialise(0, 8, setup.get(), false).isEmpty(),
           "routing mock device opens without hardware");
    expect(manager.getCurrentAudioDevice()->getOutputChannelNames().size() == 8,
           "routing device exposes more physical outputs than active outputs");
    SettingsPanel panel(ThemePreset::Spotify, {}, AppLanguage::Spanish, {},
        PadsCrossfadePreset{}, {}, MetronomeClickStyle{}, {}, {}, {}, {}, {}, {},
        8, {}, &manager, 0, 8);

    // A device can expose eight outputs while JUCE only supplies two callback
    // channels. Routing must never offer the six disabled outputs.
    for (const int activeCount : {2, 1, 4}) {
      juce::AudioDeviceManager::AudioDeviceSetup next;
      manager.getAudioDeviceSetup(next);
      next.useDefaultOutputChannels = false;
      next.outputChannels.clear();
      next.outputChannels.setRange(0, activeCount, true);
      expect(manager.setAudioDeviceSetup(next, true).isEmpty(),
             "active output mask changes on the routing device");
      panel.refreshRoutingForDevice();
      expect(panel.availableOutputChannels == activeCount,
             "routing follows active callback channels rather than device capacity");
      for (auto *row : panel.routingContent.getChildren()) {
        AdaptiveComboBox *mode = nullptr, *channels = nullptr;
        for (auto *child : row->getChildren()) {
          if (auto *selector = dynamic_cast<AdaptiveComboBox *>(child)) {
            if (selector->getItemText(0) == "Mono") mode = selector;
            else channels = selector;
          }
        }
        expect(mode != nullptr && channels != nullptr, "routing row exposes mode and channel selectors");
        const bool stereo = mode->getSelectedId() == 2;
        expect(channels->getNumItems() == activeCount - (stereo ? 1 : 0),
               "channel choices exclude disabled outputs in both routing modes");
        expect(activeCount > 1 || (!mode->isItemEnabled(2) && !stereo),
               "a single active output disables stereo routing");
      }
    }
  }

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
    testActiveRoutingChannels();
    int routingChanges = 0;
    SettingsPanel panel(ThemePreset::Spotify, {}, AppLanguage::Spanish, {},
        PadsCrossfadePreset{}, {},
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
    expect(home->getNumChildComponents() == (useTouchOptimisedControls() ? 7 : 8),
           "ordinary accounts expose only available settings destinations");
    for (auto *child : home->getChildren())
      if (auto *button = dynamic_cast<juce::Button *>(child))
        expect(button->getButtonText() != "Biblioteca",
               "removed import preferences leave no empty Library settings page");
    int learningRequests = 0;
    panel.onLearningRequested = [&] { ++learningRequests; };
    int teamsRequests = 0;
    panel.onTeamsRequested = [&] { ++teamsRequests; };
    panel.showSettingsPage(Destination::Teams);
    panel.teamsButton.onClick();
    expect(teamsRequests == 0 && !panel.teamsButton.isVisible() &&
               !panel.teamsLabel.isVisible() && !panel.teamsHelp.isVisible(),
           "default denied Teams cannot be opened through navigation or a direct callback");
    panel.setTeamsManagementEnabled(true);
    expect(home->getNumChildComponents() == (useTouchOptimisedControls() ? 8 : 9),
           "enabled management profile gains exactly one Teams settings entry");
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
      if (button->getButtonText() == "Teams") {
        expect(teamsRequests == 1 && panel.destination == Destination::Home &&
                   panel.settingsViewport.getViewedComponent() == home,
               "Teams opens user administration without an empty settings page");
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
      for (const auto page : {Destination::Appearance, Destination::Playback}) {
        panel.showSettingsPage(page);
        for (auto *control : panel.generalPage.getChildren())
          if (control->isVisible())
            expect(!control->getBounds().isEmpty() && panel.generalPage.getLocalBounds().contains(control->getBounds()),
                   "every visible preference fits in narrow pages");
        const juce::Label *const helps[] = {
            &panel.themeHelp, &panel.languageHelp,
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
    for (auto *child : panel.generalPage.getChildren()) {
      expect(dynamic_cast<juce::ToggleButton *>(child) == nullptr,
             "removed startup, Prime and optimization options leave no hidden toggles");
      if (auto *label = dynamic_cast<juce::Label *>(child))
        expect(!label->getText().containsIgnoreCase("Prime") &&
                   !label->getText().containsIgnoreCase("Ventana al iniciar") &&
                   !label->getText().containsIgnoreCase("Optimización de stems"),
               "removed options have no leftover labels or help text");
    }
    expect(panel.learningButton.isVisible() && panel.learningButton.getHeight() >= 36 &&
               panel.generalPage.getLocalBounds().contains(panel.learningButton.getBounds()),
           "desktop General retains an accessible guide entry after compact navigation");
    panel.learningButton.onClick();
    expect(learningRequests == 2,
           "the desktop guide button shares the same learning request as the grouped row");
    expect(panel.teamsButton.isVisible() &&
               panel.teamsHelp.getText() == L10n::tr(
                   "Consulta tu Team o administra el acceso a tu nube compartida.",
                   "View your Team or manage access to your shared cloud.") &&
               panel.generalPage.getLocalBounds().contains(panel.teamsButton.getBounds()),
           "authorized desktop profile exposes Teams with shared Cloud access help");
    panel.teamsButton.onClick();
    expect(teamsRequests == 2, "desktop Teams action uses the same enabled navigation guard");
    snapshot(panel, "settings-desktop");

    // A capability refresh must update both navigation styles without leaving
    // blank rows or allowing previously stored callbacks to bypass revocation.
    for (const auto size : {juce::Point<int>(320, 640), {390, 2000}, {1080, 780}}) {
      panel.setSize(size.x, size.y);
      for (const bool enabled : {false, true, false}) {
        panel.setTeamsManagementEnabled(enabled);
        panel.showSettingsPage(Destination::Home);
        if (panel.usesGroupedNavigation()) {
          auto *index = panel.settingsViewport.getViewedComponent();
          int teamsRows = 0;
          juce::Rectangle<int> previousRow;
          for (auto *entry : index->getChildren()) {
            expect(index->getLocalBounds().contains(entry->getBounds()) &&
                       entry->getHeight() >= 48 && !entry->getBounds().intersects(previousRow),
                   "dynamic settings entries retain their layout, including tall pages with unchanged size");
            previousRow = entry->getBounds();
            if (auto *button = dynamic_cast<juce::Button *>(entry);
                button != nullptr && button->getButtonText() == "Teams") ++teamsRows;
          }
          expect(teamsRows == (enabled ? 1 : 0), "grouped index follows the live Teams capability");
        } else {
          expect(panel.teamsButton.isVisible() == enabled && panel.teamsLabel.isVisible() == enabled &&
                     panel.teamsHelp.isVisible() == enabled,
                 "desktop Teams row disappears completely when permission is revoked");
          if (!enabled)
            expect(panel.teamsButton.getBounds().isEmpty(), "disabled desktop Teams reserves no row space");
        }
        const int previousRequests = teamsRequests;
        panel.showSettingsPage(Destination::Teams);
        panel.teamsButton.onClick();
        expect(teamsRequests == previousRequests + (enabled ? 2 : 0),
               "navigation and direct callbacks both respect enabled, denied and revoked capability");
      }
    }

    // Tutorial destinations must refer to the real controls and reveal rows
    // hidden by scrolling, both in grouped settings and desktop General.
    using Anchor = playworship::learning::Anchor;
    for (const auto size : {juce::Point<int>(320, 640), {390, 844}, {1080, 780}}) {
      panel.setSize(size.x, size.y);
      panel.showSettingsPage(Destination::Home);
      panel.revealLearningTarget(Anchor::RemoteSettings);
      expect(!panel.getLearningTargetBounds(Anchor::RemoteSettings).isEmpty(),
             "remote entry is visible in a guided Settings visit");
      panel.showSettingsPage(Destination::Remote);
      expect(panel.isLearningRemotePageOpen(), "actual remote page satisfies navigation step");
      panel.updateRemoteState(playworship::remote::RemoteSession::Mode::Server, {}, "Listening");
      for (const auto anchor : {Anchor::RemoteMode, Anchor::RemoteAlias, Anchor::RemoteStatus}) {
        panel.revealLearningTarget(anchor);
        expect(!panel.getLearningTargetBounds(anchor).isEmpty(),
               "guided server controls can be revealed in the viewport");
      }
      panel.updateRemoteState(playworship::remote::RemoteSession::Mode::Client,
                              {{"Mac", "192.168.0.10", 49172, 0}}, "Connected");
      panel.revealLearningTarget(Anchor::RemoteServers);
      expect(!panel.getLearningTargetBounds(Anchor::RemoteServers).isEmpty(),
             "guided client selector is reachable");
      expect(panel.getLearningTargetBounds(Anchor::SettingsClose) == panel.closeButton.getBounds(),
             "tutorial points to the existing close button");
      snapshot(panel, "settings-learning-remote-" + juce::String(size.x));
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
  std::cout << "Settings: grouped navigation, remote state, layout, scrolling and gestures passed\n";
}
