#include "AppSettings.h"
#include "ApplicationDataPaths.h"
#include "SongData.h"

#include <atomic>

namespace {
juce::File getMtControlsDirectory() {
  auto dir =
      ApplicationDataPaths::applicationDataDirectory()
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
constexpr const char *kMidiBindingsKey = "midiBindings";
constexpr const char *kMetronomeClickStyleKey = "metronomeClickStyle";
constexpr int kDeprecatedClassicThemeId = 1;
constexpr int kNightGreenThemeId = 2;
constexpr int kStudioAmberThemeId = 3;

// Localization helpers are called from high-frequency paint paths (notably
// the scrolling waveform). Keep the persisted language in memory after the
// first lookup so repainting never reopens and reparses app_settings.json.
std::atomic<int> cachedLanguage{-1};

int encodeLanguage(const AppLanguage language) noexcept {
  return language == AppLanguage::English ? 1 : 0;
}

AppLanguage decodeLanguage(const int language) noexcept {
  return language == 1 ? AppLanguage::English : AppLanguage::Spanish;
}

int normalizeThemePresetId(const int rawId) {
  if (rawId == kDeprecatedClassicThemeId)
    return kNightGreenThemeId;
  return juce::jlimit(kNightGreenThemeId, kStudioAmberThemeId, rawId);
}

juce::String metronomeClickStyleToString(const MetronomeClickStyle style) {
  switch (style) {
  case MetronomeClickStyle::Woodblock:
    return "woodblock";
  case MetronomeClickStyle::Cowbell:
    return "cowbell";
  case MetronomeClickStyle::Digital:
    return "digital";
  case MetronomeClickStyle::Gentle:
    return "gentle";
  case MetronomeClickStyle::Percussive:
    return "percussive";
  case MetronomeClickStyle::Saw:
    return "saw";
  case MetronomeClickStyle::Blip:
    return "blip";
  case MetronomeClickStyle::Classic:
  default:
    return "classic";
  }
}

MetronomeClickStyle metronomeClickStyleFromString(const juce::String &raw) {
  const auto text = raw.trim().toLowerCase();
  if (text == "woodblock")
    return MetronomeClickStyle::Woodblock;
  if (text == "cowbell")
    return MetronomeClickStyle::Cowbell;
  if (text == "digital")
    return MetronomeClickStyle::Digital;
  if (text == "gentle")
    return MetronomeClickStyle::Gentle;
  if (text == "percussive")
    return MetronomeClickStyle::Percussive;
  if (text == "saw")
    return MetronomeClickStyle::Saw;
  if (text == "blip")
    return MetronomeClickStyle::Blip;
  return MetronomeClickStyle::Classic;
}

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
  // Defaults mirror the user-visible "expected" routing the first time
  // someone installs the plugin, before any per-device override has been
  // written to app_settings.json:
  //   Click   -> mono, channel 1
  //   Cues    -> mono, channel 2 (the "Guía" stem)
  //   others  -> stereo, channel 1
  // Per-device overrides in app_settings.json (`routingByDevice`) still
  // take precedence once the user has saved a config from the Settings
  // -> Routing tab.
  std::vector<CategoryRoutingDefaults> out;
  out.resize(kTrackCategories.size());
  for (size_t i = 0; i < kTrackCategories.size(); ++i) {
    const juce::String name = kTrackCategories[i];
    auto &entry = out[static_cast<size_t>(i)];
    if (name.equalsIgnoreCase("Click")) {
      entry.baseChannel = 1;
      entry.width = 1;
    } else if (name.equalsIgnoreCase("Cues")) {
      entry.baseChannel = 2;
      entry.width = 1;
    } else {
      entry.baseChannel = 1;
      entry.width = 2;
    }
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

PadsCrossfadePreset AppSettings::getPadsCrossfadePreset() {
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return kDefaultPadsCrossfadePreset;

  const auto parsed = juce::JSON::parse(f);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty("padsCrossfadePreset")) {
      const auto raw =
          o->getProperty("padsCrossfadePreset").toString().trim().toLowerCase();
      if (raw == "fast")
        return PadsCrossfadePreset::Fast;
      if (raw == "long")
        return PadsCrossfadePreset::Long;
      if (raw == "medium")
        return PadsCrossfadePreset::Medium;
    }
  }
  return kDefaultPadsCrossfadePreset;
}

void AppSettings::setPadsCrossfadePreset(const PadsCrossfadePreset preset) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  juce::String presetText = "medium";
  if (preset == PadsCrossfadePreset::Fast)
    presetText = "fast";
  else if (preset == PadsCrossfadePreset::Long)
    presetText = "long";

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 12);
    o->setProperty("padsCrossfadePreset", presetText);
  }

  getSettingsFile().replaceWithText(juce::JSON::toString(root, true));
}

AppLanguage AppSettings::getLanguage() {
  const int cached = cachedLanguage.load(std::memory_order_acquire);
  if (cached >= 0)
    return decodeLanguage(cached);

  auto language = kDefaultLanguage;
  const auto f = getSettingsFile();
  if (f.existsAsFile()) {
    const auto parsed = juce::JSON::parse(f);
    if (const auto *o = parsed.getDynamicObject()) {
      if (o->hasProperty("language")) {
        const auto raw =
            o->getProperty("language").toString().trim().toLowerCase();
        if (raw == "en")
          language = AppLanguage::English;
        else if (raw == "es")
          language = AppLanguage::Spanish;
      }
    }
  }

  cachedLanguage.store(encodeLanguage(language), std::memory_order_release);
  return language;
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

  if (getSettingsFile().replaceWithText(juce::JSON::toString(root, true)))
    cachedLanguage.store(encodeLanguage(value), std::memory_order_release);
}

MetronomeClickStyle AppSettings::getMetronomeClickStyle() {
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return kDefaultMetronomeClickStyle;

  const auto parsed = juce::JSON::parse(f);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty(kMetronomeClickStyleKey))
      return metronomeClickStyleFromString(
          o->getProperty(kMetronomeClickStyleKey).toString());
  }
  return kDefaultMetronomeClickStyle;
}

void AppSettings::setMetronomeClickStyle(const MetronomeClickStyle value) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 13);
    o->setProperty(kMetronomeClickStyleKey, metronomeClickStyleToString(value));
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
    return kNightGreenThemeId;

  const auto parsed = juce::JSON::parse(f);
  if (const auto *o = parsed.getDynamicObject()) {
    if (o->hasProperty("themePresetId")) {
      const int rawId = static_cast<int>(o->getProperty("themePresetId"));
      const int normalizedId = normalizeThemePresetId(rawId);
      if (normalizedId != rawId)
        setThemePresetId(normalizedId);
      return normalizedId;
    }
  }
  return kNightGreenThemeId;
}

void AppSettings::setThemePresetId(const int presetId) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 6);
    o->setProperty("themePresetId", normalizeThemePresetId(presetId));
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

juce::String AppSettings::getRemoteServerAlias() {
  const auto file = getSettingsFile();
  if (!file.existsAsFile())
    return {};
  return juce::JSON::parse(file).getProperty("remoteServerAlias", {})
      .toString().trim();
}

void AppSettings::setRemoteServerAlias(const juce::String &alias) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());
  if (auto *object = root.getDynamicObject()) {
    object->setProperty("version", 16);
    object->setProperty("remoteServerAlias", alias.trim());
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

std::vector<MidiBindingSetting> AppSettings::getMidiBindings() {
  std::vector<MidiBindingSetting> out;
  const auto f = getSettingsFile();
  if (!f.existsAsFile())
    return out;

  const auto parsed = juce::JSON::parse(f);
  const auto *root = parsed.getDynamicObject();
  if (root == nullptr || !root->hasProperty(kMidiBindingsKey))
    return out;

  const auto bindingsVar = root->getProperty(kMidiBindingsKey);
  const auto *arr = bindingsVar.getArray();
  if (arr == nullptr)
    return out;

  for (const auto &entryVar : *arr) {
    const auto *entryObj = entryVar.getDynamicObject();
    if (entryObj == nullptr)
      continue;

    MidiBindingSetting binding;
    binding.targetId = static_cast<int>(entryObj->getProperty("targetId"));
    binding.midiChannel =
        juce::jlimit(1, 16, static_cast<int>(entryObj->getProperty("midiChannel")));
    binding.ccNumber =
        juce::jlimit(0, 127, static_cast<int>(entryObj->getProperty("ccNumber")));
    const auto sourceTypeText =
        entryObj->getProperty("sourceType").toString().trim().toLowerCase();
    if (sourceTypeText == "note")
      binding.sourceType = MidiBindingSourceType::Note;
    else
      binding.sourceType = MidiBindingSourceType::CC;
    const auto modeText =
        entryObj->getProperty("mode").toString().trim().toLowerCase();
    if (modeText == "toggle")
      binding.mode = MidiBindingMode::Toggle;
    else if (modeText == "momentary")
      binding.mode = MidiBindingMode::Momentary;
    else
      binding.mode = MidiBindingMode::Absolute;
    if (binding.targetId >= 0)
      out.push_back(binding);
  }

  return out;
}

void AppSettings::setMidiBindings(
    const std::vector<MidiBindingSetting> &bindings) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  juce::Array<juce::var> bindingsArray;
  for (const auto &binding : bindings) {
    if (binding.targetId < 0)
      continue;
    auto *entryObj = new juce::DynamicObject();
    entryObj->setProperty("targetId", binding.targetId);
    entryObj->setProperty("midiChannel", juce::jlimit(1, 16, binding.midiChannel));
    entryObj->setProperty("ccNumber", juce::jlimit(0, 127, binding.ccNumber));
    entryObj->setProperty("sourceType",
                          binding.sourceType == MidiBindingSourceType::Note
                              ? juce::var("note")
                              : juce::var("cc"));
    juce::String modeText = "absolute";
    if (binding.mode == MidiBindingMode::Toggle)
      modeText = "toggle";
    else if (binding.mode == MidiBindingMode::Momentary)
      modeText = "momentary";
    entryObj->setProperty("mode", modeText);
    bindingsArray.add(juce::var(entryObj));
  }

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 15);
    o->setProperty(kMidiBindingsKey, juce::var(bindingsArray));
  }

  getSettingsFile().replaceWithText(juce::JSON::toString(root, true));
}
