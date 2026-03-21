#include "AppSettings.h"

namespace {
juce::File getMtControlsDirectory() {
  auto dir =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("MTControls");
  dir.createDirectory();
  return dir;
}
} // namespace

juce::File AppSettings::getSettingsFile() {
  return getMtControlsDirectory().getChildFile("app_settings.json");
}

bool AppSettings::getExpandWindowOnLaunch() {
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return kDefaultExpandWindowOnLaunch;

  const auto parsed = juce::JSON::parse(f);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty("expandWindowOnLaunch"))
      return static_cast<bool>(o->getProperty("expandWindowOnLaunch"));
  }
  return kDefaultExpandWindowOnLaunch;
}

void AppSettings::setExpandWindowOnLaunch(const bool value) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 1);
    o->setProperty("expandWindowOnLaunch", value);
  }

  getSettingsFile().replaceWithText(juce::JSON::toString(root, true));
}
