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
  const auto *routingObj = routingVar.getDynamicObject();
  if (routingObj == nullptr)
    return defaults;

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
    defaults[i].baseChannel = channel;
    defaults[i].width = width;
  }

  return defaults;
}

void AppSettings::setRoutingDefaults(
    const std::vector<CategoryRoutingDefaults> &defaults) {
  juce::var root;
  if (getSettingsFile().existsAsFile())
    root = juce::JSON::parse(getSettingsFile());
  if (!root.isObject())
    root = juce::var(new juce::DynamicObject());

  auto *routingObj = new juce::DynamicObject();
  for (size_t i = 0; i < kTrackCategories.size(); ++i) {
    auto *entry = new juce::DynamicObject();
    const CategoryRoutingDefaults def =
        i < defaults.size() ? defaults[i] : CategoryRoutingDefaults{};
    entry->setProperty(kRoutingChannelKey, juce::jmax(1, def.baseChannel));
    entry->setProperty(kRoutingWidthKey, juce::jlimit(1, 2, def.width));
    routingObj->setProperty(kTrackCategories[i], juce::var(entry));
  }

  if (auto *o = root.getDynamicObject()) {
    o->setProperty("version", 2);
    o->setProperty(kRoutingByCategoryKey, juce::var(routingObj));
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
