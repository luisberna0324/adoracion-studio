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
constexpr const char *kPadsPackKey = "padsPack";
constexpr const char *kAssetsBootstrapKey = "assetsBootstrap";

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

bool AppSettings::getStemOptimizationEnabled() {
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return kDefaultStemOptimizationEnabled;

  const auto parsed = juce::JSON::parse(f);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty("stemOptimizationEnabled"))
      return static_cast<bool>(o->getProperty("stemOptimizationEnabled"));
  }
  return kDefaultStemOptimizationEnabled;
}

void AppSettings::setStemOptimizationEnabled(const bool value) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 11);
    o->setProperty("stemOptimizationEnabled", value);
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

AppLayoutMode AppSettings::getLayoutMode() {
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return AppLayoutMode::Auto;

  const auto parsed = juce::JSON::parse(f);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty("layoutMode")) {
      const auto raw = o->getProperty("layoutMode").toString().trim().toLowerCase();
      if (raw == "compact")
        return AppLayoutMode::Compact;
      if (raw == "standard")
        return AppLayoutMode::Standard;
      if (raw == "large")
        return AppLayoutMode::Large;
    }
  }
  return AppLayoutMode::Auto;
}

void AppSettings::setLayoutMode(const AppLayoutMode mode) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  juce::String modeText = "auto";
  if (mode == AppLayoutMode::Compact)
    modeText = "compact";
  else if (mode == AppLayoutMode::Standard)
    modeText = "standard";
  else if (mode == AppLayoutMode::Large)
    modeText = "large";

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 10);
    o->setProperty("layoutMode", modeText);
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

juce::int64 AppSettings::getLastSuccessfulLoginMs() {
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return 0;

  const auto parsed = juce::JSON::parse(f);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty("lastSuccessfulLoginMs"))
      return static_cast<juce::int64>(o->getProperty("lastSuccessfulLoginMs"));
  }
  return 0;
}

void AppSettings::setLastSuccessfulLoginMs(const juce::int64 value) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 8);
    o->setProperty("lastSuccessfulLoginMs", value);
  }

  getSettingsFile().replaceWithText(juce::JSON::toString(root, true));
}

juce::int64 AppSettings::getReauthGraceDeadlineMs() {
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return 0;

  const auto parsed = juce::JSON::parse(f);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty("reauthGraceDeadlineMs"))
      return static_cast<juce::int64>(o->getProperty("reauthGraceDeadlineMs"));
  }
  return 0;
}

void AppSettings::setReauthGraceDeadlineMs(const juce::int64 value) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 9);
    o->setProperty("reauthGraceDeadlineMs", value);
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

PadsPackSettings AppSettings::getPadsPackSettings() {
  PadsPackSettings out;
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return out;

  const auto parsed = juce::JSON::parse(f);
  const auto *root = parsed.getDynamicObject();
  if (root == nullptr || !root->hasProperty(kPadsPackKey))
    return out;

  const auto packVar = root->getProperty(kPadsPackKey);
  const auto *packObj = packVar.getDynamicObject();
  if (packObj == nullptr)
    return out;

  out.folderPath = packObj->getProperty("folderPath").toString().trim();
  out.sourcePath = packObj->getProperty("sourcePath").toString().trim();
  out.hash = packObj->getProperty("hash").toString().trim();
  out.importedAtMs = static_cast<juce::int64>(packObj->getProperty("importedAtMs"));
  out.valid = static_cast<bool>(packObj->getProperty("valid"));
  out.status = packObj->getProperty("status").toString().trim();
  return out;
}

void AppSettings::setPadsPackSettings(const PadsPackSettings &settings) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  auto *packObj = new juce::DynamicObject();
  packObj->setProperty("folderPath", settings.folderPath.trim());
  packObj->setProperty("sourcePath", settings.sourcePath.trim());
  packObj->setProperty("hash", settings.hash.trim());
  packObj->setProperty("importedAtMs", settings.importedAtMs);
  packObj->setProperty("valid", settings.valid);
  packObj->setProperty("status", settings.status.trim());

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 12);
    o->setProperty(kPadsPackKey, juce::var(packObj));
  }

  getSettingsFile().replaceWithText(juce::JSON::toString(root, true));
}

AssetsBootstrapState AppSettings::getAssetsBootstrapState() {
  AssetsBootstrapState out;
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return out;

  const auto parsed = juce::JSON::parse(f);
  const auto *root = parsed.getDynamicObject();
  if (root == nullptr || !root->hasProperty(kAssetsBootstrapKey))
    return out;

  const auto assetsVar = root->getProperty(kAssetsBootstrapKey);
  const auto *assetsObj = assetsVar.getDynamicObject();
  if (assetsObj == nullptr)
    return out;

  out.encoderVersion = assetsObj->getProperty("encoderVersion").toString().trim();
  out.encoderStatus = assetsObj->getProperty("encoderStatus").toString().trim();
  out.padsVersion = assetsObj->getProperty("padsVersion").toString().trim();
  out.padsStatus = assetsObj->getProperty("padsStatus").toString().trim();
  out.lastError = assetsObj->getProperty("lastError").toString().trim();
  out.updatedAtMs = static_cast<juce::int64>(assetsObj->getProperty("updatedAtMs"));
  return out;
}

void AppSettings::setAssetsBootstrapState(const AssetsBootstrapState &state) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  auto *assetsObj = new juce::DynamicObject();
  assetsObj->setProperty("encoderVersion", state.encoderVersion.trim());
  assetsObj->setProperty("encoderStatus", state.encoderStatus.trim());
  assetsObj->setProperty("padsVersion", state.padsVersion.trim());
  assetsObj->setProperty("padsStatus", state.padsStatus.trim());
  assetsObj->setProperty("lastError", state.lastError.trim());
  assetsObj->setProperty("updatedAtMs", state.updatedAtMs);

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 13);
    o->setProperty(kAssetsBootstrapKey, juce::var(assetsObj));
  }

  getSettingsFile().replaceWithText(juce::JSON::toString(root, true));
}
