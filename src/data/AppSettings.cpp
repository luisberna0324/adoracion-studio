#include "AppSettings.h"
#include "SongData.h"

namespace {
juce::File getMtControlsDirectory() {
  auto dir =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("MTControls");
  dir.createDirectory();
  return dir;
}

constexpr const char *kRoutingByCategoryKey = "routingByCategory";
constexpr const char *kRoutingByDeviceKey = "routingByDevice";
constexpr const char *kRoutingChannelKey = "channel";
constexpr const char *kRoutingWidthKey = "width";

int getCategoryIndexForName(const juce::String &category) {
  const auto needle = category.trim();
  if (needle.isEmpty())
    return -1;

  int otherIndex = -1;
  for (size_t i = 0; i < kTrackCategories.size(); ++i) {
    const juce::String name = kTrackCategories[i];
    if (name.equalsIgnoreCase("Other"))
      otherIndex = static_cast<int>(i);
    if (name.equalsIgnoreCase(needle))
      return static_cast<int>(i);
  }
  return otherIndex;
}

std::vector<CategoryRoutingDefaults> buildDefaultRouting() {
  std::vector<CategoryRoutingDefaults> out;
  out.resize(kTrackCategories.size());
  for (auto &entry : out) {
    entry.baseChannel = 1;
    entry.width = 1;
  }
  return out;
}

juce::var routingDefaultsToVar(
    const std::vector<CategoryRoutingDefaults> &defaults) {
  auto *routingObj = new juce::DynamicObject();
  for (size_t i = 0; i < kTrackCategories.size(); ++i) {
    auto *entry = new juce::DynamicObject();
    const CategoryRoutingDefaults def =
        i < defaults.size() ? defaults[i] : CategoryRoutingDefaults{};
    entry->setProperty(kRoutingChannelKey, juce::jmax(1, def.baseChannel));
    entry->setProperty(kRoutingWidthKey, juce::jlimit(1, 2, def.width));
    routingObj->setProperty(kTrackCategories[i], juce::var(entry));
  }
  return juce::var(routingObj);
}

void applyRoutingVarToDefaults(const juce::var &routingVar,
                               std::vector<CategoryRoutingDefaults> &defaults) {
  const auto *routingObj = routingVar.getDynamicObject();
  if (routingObj == nullptr)
    return;

  for (size_t i = 0; i < kTrackCategories.size(); ++i) {
    const juce::String categoryName = kTrackCategories[i];
    if (!routingObj->hasProperty(categoryName))
      continue;
    const auto entryVar = routingObj->getProperty(categoryName);
    const auto *entryObj = entryVar.getDynamicObject();
    if (entryObj == nullptr)
      continue;
    int channel = 1;
    if (entryObj->hasProperty(kRoutingChannelKey))
      channel = static_cast<int>(entryObj->getProperty(kRoutingChannelKey));
    int width = 1;
    if (entryObj->hasProperty(kRoutingWidthKey))
      width = static_cast<int>(entryObj->getProperty(kRoutingWidthKey));
    channel = juce::jmax(1, channel);
    width = juce::jlimit(1, 2, width);
    if (i < defaults.size()) {
      defaults[i].baseChannel = channel;
      defaults[i].width = width;
    }
  }
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

bool AppSettings::getPrimeImportEnabled() {
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return kDefaultPrimeImportEnabled;

  const auto parsed = juce::JSON::parse(f);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty("primeImportEnabled"))
      return static_cast<bool>(o->getProperty("primeImportEnabled"));
  }
  return kDefaultPrimeImportEnabled;
}

void AppSettings::setPrimeImportEnabled(const bool value) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 4);
    o->setProperty("primeImportEnabled", value);
  }

  getSettingsFile().replaceWithText(juce::JSON::toString(root, true));
}

AppLanguage AppSettings::getLanguage() {
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return kDefaultLanguage;

  const auto parsed = juce::JSON::parse(f);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty("language")) {
      const auto raw = o->getProperty("language").toString().trim().toLowerCase();
      if (raw == "en")
        return AppLanguage::English;
      if (raw == "es")
        return AppLanguage::Spanish;
    }
  }
  return kDefaultLanguage;
}

void AppSettings::setLanguage(const AppLanguage value) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 5);
    o->setProperty("language",
                   value == AppLanguage::English ? juce::var("en")
                                                 : juce::var("es"));
  }

  getSettingsFile().replaceWithText(juce::JSON::toString(root, true));
}

int AppSettings::getThemePresetId() {
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return 1;

  const auto parsed = juce::JSON::parse(f);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty("themePresetId")) {
      const int id = static_cast<int>(o->getProperty("themePresetId"));
      return juce::jlimit(1, 3, id);
    }
  }
  return 1;
}

void AppSettings::setThemePresetId(const int presetId) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 6);
    o->setProperty("themePresetId", juce::jlimit(1, 3, presetId));
  }

  getSettingsFile().replaceWithText(juce::JSON::toString(root, true));
}

juce::String AppSettings::getLastSelectedSongId() {
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return {};

  const auto parsed = juce::JSON::parse(f);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty("lastSelectedSongId"))
      return o->getProperty("lastSelectedSongId").toString().trim();
  }
  return {};
}

void AppSettings::setLastSelectedSongId(const juce::String &songId) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 7);
    o->setProperty("lastSelectedSongId", songId.trim());
  }

  getSettingsFile().replaceWithText(juce::JSON::toString(root, true));
}

std::vector<CategoryRoutingDefaults> AppSettings::getRoutingDefaults() {
  auto defaults = buildDefaultRouting();
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return defaults;

  const auto parsed = juce::JSON::parse(f);
  const auto *root = parsed.getDynamicObject();
  if (root == nullptr || !root->hasProperty(kRoutingByCategoryKey))
    return defaults;

  const auto routingVar = root->getProperty(kRoutingByCategoryKey);
  applyRoutingVarToDefaults(routingVar, defaults);

  return defaults;
}

void AppSettings::setRoutingDefaults(
    const std::vector<CategoryRoutingDefaults> &defaults) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 2);
    o->setProperty(kRoutingByCategoryKey, routingDefaultsToVar(defaults));
  }

  getSettingsFile().replaceWithText(juce::JSON::toString(root, true));
}

std::vector<CategoryRoutingDefaults>
AppSettings::getRoutingDefaultsForDevice(const juce::String &deviceKey) {
  if (deviceKey.trim().isEmpty())
    return getRoutingDefaults();

  auto defaults = getRoutingDefaults();
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return defaults;

  const auto parsed = juce::JSON::parse(f);
  const auto *root = parsed.getDynamicObject();
  if (root == nullptr || !root->hasProperty(kRoutingByDeviceKey))
    return defaults;

  const auto byDeviceVar = root->getProperty(kRoutingByDeviceKey);
  const auto *byDeviceObj = byDeviceVar.getDynamicObject();
  if (byDeviceObj == nullptr)
    return defaults;

  if (!byDeviceObj->hasProperty(deviceKey))
    return defaults;

  const auto routingVar = byDeviceObj->getProperty(deviceKey);
  applyRoutingVarToDefaults(routingVar, defaults);
  return defaults;
}

void AppSettings::setRoutingDefaultsForDevice(
    const juce::String &deviceKey,
    const std::vector<CategoryRoutingDefaults> &defaults) {
  if (deviceKey.trim().isEmpty()) {
    setRoutingDefaults(defaults);
    return;
  }

  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  juce::DynamicObject *byDeviceObj = nullptr;
  if (auto *o = root.getDynamicObject()) {
    if (o->hasProperty(kRoutingByDeviceKey)) {
      byDeviceObj = o->getProperty(kRoutingByDeviceKey).getDynamicObject();
    }
    if (byDeviceObj == nullptr) {
      byDeviceObj = new juce::DynamicObject();
      o->setProperty(kRoutingByDeviceKey, juce::var(byDeviceObj));
    }
    byDeviceObj->setProperty(deviceKey, routingDefaultsToVar(defaults));
    o->setProperty("version", 3);
  }

  getSettingsFile().replaceWithText(juce::JSON::toString(root, true));
}

CategoryRoutingDefaults
AppSettings::getRoutingDefaultsForCategory(const juce::String &category) {
  const auto defaults = getRoutingDefaults();
  const int idx = getCategoryIndexForName(category);
  if (idx < 0 || idx >= static_cast<int>(defaults.size()))
    return CategoryRoutingDefaults{};
  return defaults[static_cast<size_t>(idx)];
}
