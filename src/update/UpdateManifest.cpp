#include "UpdateManifest.h"

#include <array>

namespace PlayWorshipUpdate {
namespace {

juce::String propString(const juce::DynamicObject &object, const char *name) {
  if (!object.hasProperty(name))
    return {};
  return object.getProperty(name).toString().trim();
}

juce::int64 propInt64(const juce::DynamicObject &object, const char *name) {
  if (!object.hasProperty(name))
    return 0;
  return static_cast<juce::int64>(object.getProperty(name));
}

UpdatePackage parsePackage(const juce::var &value) {
  UpdatePackage out;
  if (const auto *object = value.getDynamicObject()) {
    out.type = propString(*object, "type");
    out.url = propString(*object, "url");
    out.sha256 = propString(*object, "sha256").toLowerCase();
    out.sizeBytes = propInt64(*object, "sizeBytes");

    if (const auto *sig = object->getProperty("signature").getDynamicObject()) {
      out.signatureType = propString(*sig, "type");
      out.publisher = propString(*sig, "publisher");
    }
  }
  return out;
}

std::array<int, 3> parseSemverParts(const juce::String &version) {
  std::array<int, 3> out = {0, 0, 0};
  auto parts = juce::StringArray::fromTokens(version.upToFirstOccurrenceOf("-", false, false),
                                            ".", "");
  for (int i = 0; i < juce::jmin(3, parts.size()); ++i)
    out[static_cast<size_t>(i)] = parts[i].getIntValue();
  return out;
}

} // namespace

ManifestParseResult parseUpdateManifest(const juce::String &json) {
  ManifestParseResult result;
  result.manifest.rawJson = json;
  const auto parsed = juce::JSON::parse(json);
  const auto *root = parsed.getDynamicObject();
  if (root == nullptr) {
    result.error = "El servidor no devolvio JSON valido.";
    return result;
  }

  auto &manifest = result.manifest;
  manifest.updateAvailable =
      static_cast<bool>(root->getProperty("updateAvailable"));
  manifest.mandatory = static_cast<bool>(root->getProperty("mandatory"));
  manifest.currentVersion = propString(*root, "currentVersion");
  manifest.latestVersion = propString(*root, "latestVersion");
  manifest.channel = propString(*root, "channel");
  manifest.package = parsePackage(root->getProperty("package"));
  manifest.delta = parsePackage(root->getProperty("delta"));
  manifest.hasDelta = manifest.delta.url.isNotEmpty() &&
                      manifest.delta.sha256.isNotEmpty() &&
                      static_cast<bool>(root->getProperty("delta")
                                            .getProperty("available", false));

  if (const auto *notes = root->getProperty("releaseNotes").getDynamicObject()) {
    manifest.releaseTitle = propString(*notes, "title");
    manifest.releaseNotesUrl = propString(*notes, "url");
    if (auto *items = notes->getProperty("items").getArray()) {
      for (const auto &item : *items) {
        const auto text = item.toString().trim();
        if (text.isNotEmpty())
          manifest.releaseItems.add(text);
      }
    }
  }

  if (manifest.updateAvailable) {
    if (manifest.latestVersion.isEmpty()) {
      result.error = "El manifest no incluye latestVersion.";
      return result;
    }
    if (manifest.package.url.isEmpty() || manifest.package.sha256.isEmpty()) {
      result.error = "El manifest no incluye package.url/package.sha256.";
      return result;
    }
  }

  result.ok = true;
  return result;
}

juce::String getPlatformId() {
#if JUCE_WINDOWS
  return "windows";
#elif JUCE_MAC
  return "macos";
#elif JUCE_LINUX
  return "linux";
#else
  return "unknown";
#endif
}

juce::String getArchitectureId() {
#if JUCE_64BIT
#if defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#else
  return "x64";
#endif
#else
  return "x86";
#endif
}

int compareSemver(const juce::String &a, const juce::String &b) {
  const auto lhs = parseSemverParts(a);
  const auto rhs = parseSemverParts(b);
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i] < rhs[i])
      return -1;
    if (lhs[i] > rhs[i])
      return 1;
  }
  return 0;
}

} // namespace PlayWorshipUpdate
