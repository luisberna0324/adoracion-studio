#include "ui/components/SettingsVersionsSheet.h"
#include "ui/UiCommon.h"
#include "data/AppSettings.h"
#include <cstdlib>
#include <iostream>

// Isolate the chooser from app preferences, font assets, audio and network.
AppLanguage AppSettings::getLanguage() { return AppLanguage::Spanish; }
juce::Font makeAppFont(float size, AppFontWeight) {
  return juce::Font(juce::FontOptions(size));
}
juce::Colour UITheme::accent{0xffffb545};
juce::Colour UITheme::textPrimary{0xffffffff};
juce::Colour UITheme::textSecondary{0xffbbbbbb};
juce::Colour UITheme::cardBg{0xff252525};
juce::Colour UITheme::cardBgHover{0xff333333};
juce::Colour UITheme::cardBorder{0xff555555};

static void expect(bool ok, const char *message) {
  if (!ok) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

static void pump() {
  juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
}

static void collectButtons(juce::Component &parent,
                           std::vector<juce::Button *> &buttons) {
  for (auto *child : parent.getChildren()) {
    if (auto *button = dynamic_cast<juce::Button *>(child))
      buttons.push_back(button);
    collectButtons(*child, buttons);
  }
}

static juce::Button *button(SettingsVersionsSheet &sheet,
                            const juce::String &prefix) {
  std::vector<juce::Button *> buttons;
  collectButtons(sheet, buttons);
  for (auto *item : buttons)
    if (item->isVisible() && item->getButtonText().startsWith(prefix))
      return item;
  return nullptr;
}

static void click(SettingsVersionsSheet &sheet, const char *utf8Prefix) {
  auto *item = button(sheet, juce::String::fromUTF8(utf8Prefix));
  expect(item != nullptr && item->isEnabled(), "expected enabled action");
  item->triggerClick();
  pump();
}

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr)
    return 77;
#endif
  juce::ScopedJuceInitialiser_GUI gui;
  SettingsVersionsSheet sheet;
  sheet.setTopLeftPosition(-2000, -2000);
  sheet.addToDesktop(juce::ComponentPeer::windowIsTemporary |
                     juce::ComponentPeer::windowIgnoresMouseClicks |
                     juce::ComponentPeer::windowIgnoresKeyPresses);
  sheet.setVisible(true);

  int selectedRevision = 0, dismissed = 0, retried = 0;
  sheet.onDismiss = [&] { ++dismissed; };
  sheet.onRetry = [&] {
    ++retried;
    sheet.showLoading();
  };
  sheet.onVersionSelected = [&](const CloudSettingsVersion &version) {
    selectedRevision = version.revision;
  };

  sheet.showLoading();
  expect(button(sheet, "Cancelar") == nullptr,
         "chooser never shows a Cancel button");
  expect(sheet.getHeight() <= 260, "loading chooser remains compact");

  sheet.setVersions({});
  expect(button(sheet, "v") == nullptr,
         "empty history has no version actions");
  sheet.showError("Error de conexión");
  click(sheet, "Reintentar");
  expect(retried == 1 && selectedRevision == 0,
         "retry never selects a version");

  std::vector<CloudSettingsVersion> versions;
  for (int revision = 7; revision > 0; --revision) {
    CloudSettingsVersion version;
    version.revision = revision;
    version.createdAtMs = 1700000000000;
    version.summary.mixChanges = 12;
    versions.push_back(std::move(version));
  }
  sheet.setVersions(versions);
  expect(sheet.getWidth() == 300 && sheet.getHeight() <= 220,
         "chooser uses compact width and at most three visible rows");
  expect(button(sheet, "Cancelar") == nullptr,
         "version list has no closing action");
  const auto firstVersionPrefix = juce::String::fromUTF8(u8"v7 ·");
  expect(button(sheet, firstVersionPrefix) != nullptr,
         "version option contains revision and date");
  expect(button(sheet, firstVersionPrefix)->getDescription().isEmpty(),
         "version option has no song, summary, or state subtitle");
  expect(button(sheet, "Siguientes") == nullptr &&
             button(sheet, "Anteriores") == nullptr,
         "only version options appear below the description");

  click(sheet, u8"v7 ·");
  expect(selectedRevision == 7 && dismissed == 0,
         "choosing emits one version and does not apply it");
  sheet.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
  expect(dismissed == 1, "Escape dismisses the chooser");
  std::cout << "Settings version chooser: all checks passed\n";
}
