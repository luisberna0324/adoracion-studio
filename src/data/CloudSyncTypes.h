#pragma once

#include <JuceHeader.h>

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

// Network-neutral contract shared by desktop and iOS transfer implementations.
struct CloudObjectRef {
  juce::String sha256;
  juce::int64 sizeBytes = 0;
  juce::String contentType;
};

struct CloudStemDescriptor : CloudObjectRef {
  juce::String stemId;
  juce::String displayName;
  juce::String category;
  double durationSec = 0.0;
  int sampleRate = 0;
  int channels = 0;
  juce::String format = "flac";
};

struct CloudProjectDescriptor {
  int schemaVersion = 1;
  juce::String songId;
  juce::String projectId;
  int revision = 0;
  juce::String title;
  juce::String artist;
  double bpm = 0.0;
  juce::String key;
  double durationSeconds = 0.0;
  CloudObjectRef settings;
  std::optional<CloudObjectRef> waveform;
  std::optional<CloudObjectRef> artwork;
  std::vector<CloudStemDescriptor> stems;
  juce::String createdAt;
};

namespace CloudSyncJson {
inline juce::var toVar(const CloudObjectRef& object) {
  auto* dynamicObject = new juce::DynamicObject();
  dynamicObject->setProperty("sha256", object.sha256);
  dynamicObject->setProperty("sizeBytes", object.sizeBytes);
  dynamicObject->setProperty("contentType", object.contentType);
  return juce::var(dynamicObject);
}

inline bool fromVar(const juce::var& value, CloudObjectRef& object) {
  const auto* dynamicObject = value.getDynamicObject();
  if (dynamicObject == nullptr) return false;
  object.sha256 = dynamicObject->getProperty("sha256").toString().trim().toLowerCase();
  object.sizeBytes = dynamicObject->getProperty("sizeBytes").toString().getLargeIntValue();
  object.contentType = dynamicObject->getProperty("contentType").toString().trim();
  return object.sha256.isNotEmpty() && object.sizeBytes > 0 &&
         object.contentType.isNotEmpty();
}

inline juce::var toVar(const CloudStemDescriptor& stem) {
  auto value = toVar(static_cast<const CloudObjectRef&>(stem));
  auto* dynamicObject = value.getDynamicObject();
  dynamicObject->setProperty("stemId", stem.stemId);
  dynamicObject->setProperty("displayName", stem.displayName);
  dynamicObject->setProperty("category", stem.category);
  dynamicObject->setProperty("durationSec", stem.durationSec);
  dynamicObject->setProperty("sampleRate", stem.sampleRate);
  dynamicObject->setProperty("channels", stem.channels);
  dynamicObject->setProperty("format", stem.format);
  return value;
}

inline bool fromVar(const juce::var& value, CloudStemDescriptor& stem) {
  if (!fromVar(value, static_cast<CloudObjectRef&>(stem))) return false;
  const auto* dynamicObject = value.getDynamicObject();
  if (dynamicObject == nullptr) return false;
  stem.stemId = dynamicObject->getProperty("stemId").toString().trim();
  stem.displayName = dynamicObject->getProperty("displayName").toString().trim();
  stem.category = dynamicObject->getProperty("category").toString().trim();
  stem.durationSec = dynamicObject->getProperty("durationSec").toString().getDoubleValue();
  stem.sampleRate = static_cast<int>(dynamicObject->getProperty("sampleRate"));
  stem.channels = static_cast<int>(dynamicObject->getProperty("channels"));
  stem.format = dynamicObject->getProperty("format").toString().trim().toLowerCase();
  return stem.stemId.isNotEmpty() && stem.displayName.isNotEmpty() &&
         stem.category.isNotEmpty() && stem.durationSec > 0.0 &&
         stem.sampleRate > 0 && stem.channels > 0 &&
         stem.format == "flac";
}

inline juce::var toVar(const CloudProjectDescriptor& descriptor) {
  auto* dynamicObject = new juce::DynamicObject();
  dynamicObject->setProperty("schemaVersion", descriptor.schemaVersion);
  dynamicObject->setProperty("songId", descriptor.songId);
  dynamicObject->setProperty("projectId", descriptor.projectId);
  dynamicObject->setProperty("revision", descriptor.revision);
  dynamicObject->setProperty("title", descriptor.title);
  dynamicObject->setProperty("artist", descriptor.artist);
  // Preserve the canonical JSON of legacy integer BPM descriptors so their
  // stored content fingerprints do not change just because the model is double.
  if (std::isfinite(descriptor.bpm) &&
      descriptor.bpm >= std::numeric_limits<int>::min() &&
      descriptor.bpm <= std::numeric_limits<int>::max() &&
      std::floor(descriptor.bpm) == descriptor.bpm)
    dynamicObject->setProperty("bpm", static_cast<int>(descriptor.bpm));
  else
    dynamicObject->setProperty("bpm", descriptor.bpm);
  dynamicObject->setProperty("key", descriptor.key);
  dynamicObject->setProperty("durationSeconds", descriptor.durationSeconds);
  dynamicObject->setProperty("settings", toVar(descriptor.settings));
  if (descriptor.waveform.has_value())
    dynamicObject->setProperty("waveform", toVar(*descriptor.waveform));
  if (descriptor.artwork.has_value())
    dynamicObject->setProperty("artwork", toVar(*descriptor.artwork));
  juce::Array<juce::var> stems;
  for (const auto& stem : descriptor.stems) stems.add(toVar(stem));
  dynamicObject->setProperty("stems", juce::var(stems));
  dynamicObject->setProperty("createdAt", descriptor.createdAt);
  return juce::var(dynamicObject);
}

inline bool fromVar(const juce::var& value, CloudProjectDescriptor& descriptor) {
  const auto* dynamicObject = value.getDynamicObject();
  if (dynamicObject == nullptr) return false;
  descriptor = {};
  descriptor.schemaVersion = static_cast<int>(dynamicObject->getProperty("schemaVersion"));
  descriptor.songId = dynamicObject->getProperty("songId").toString().trim();
  descriptor.projectId = dynamicObject->getProperty("projectId").toString().trim();
  descriptor.revision = static_cast<int>(dynamicObject->getProperty("revision"));
  descriptor.title = dynamicObject->getProperty("title").toString().trim();
  descriptor.artist = dynamicObject->getProperty("artist").toString().trim();
  descriptor.bpm = static_cast<double>(dynamicObject->getProperty("bpm"));
  if (!std::isfinite(descriptor.bpm)) return false;
  descriptor.key = dynamicObject->getProperty("key").toString().trim();
  descriptor.durationSeconds =
      dynamicObject->getProperty("durationSeconds").toString().getDoubleValue();
  descriptor.createdAt = dynamicObject->getProperty("createdAt").toString().trim();
  if (!fromVar(dynamicObject->getProperty("settings"), descriptor.settings))
    return false;
  CloudObjectRef waveformObject;
  if (!dynamicObject->getProperty("waveform").isVoid() &&
      !dynamicObject->getProperty("waveform").isUndefined()) {
    if (!fromVar(dynamicObject->getProperty("waveform"), waveformObject))
      return false;
    descriptor.waveform = waveformObject;
  }
  CloudObjectRef artworkObject;
  if (!dynamicObject->getProperty("artwork").isVoid() &&
      !dynamicObject->getProperty("artwork").isUndefined()) {
    if (!fromVar(dynamicObject->getProperty("artwork"), artworkObject))
      return false;
    descriptor.artwork = artworkObject;
  }
  const auto* stemsArray = dynamicObject->getProperty("stems").getArray();
  if (stemsArray == nullptr || stemsArray->isEmpty()) return false;
  descriptor.stems.clear();
  descriptor.stems.reserve(static_cast<size_t>(stemsArray->size()));
  for (const auto& stemValue : *stemsArray) {
    CloudStemDescriptor stem;
    if (!fromVar(stemValue, stem)) return false;
    descriptor.stems.push_back(std::move(stem));
  }
  return descriptor.schemaVersion == 1 && descriptor.songId.isNotEmpty() &&
         descriptor.projectId.isNotEmpty() && descriptor.revision > 0 &&
         descriptor.title.isNotEmpty() && descriptor.durationSeconds > 0.0 &&
         descriptor.createdAt.isNotEmpty();
}

inline juce::String toJsonString(const CloudProjectDescriptor& descriptor,
                                 bool pretty = false) {
  return juce::JSON::toString(toVar(descriptor), pretty);
}

// Remote identity and timestamps change on each publication; they are not
// content changes and must not force another upload from the same device.
inline juce::String fingerprint(const CloudProjectDescriptor& descriptor) {
  auto stable = descriptor;
  stable.songId.clear();
  stable.revision = 0;
  stable.createdAt.clear();
  const auto json = toJsonString(stable);
  return juce::SHA256(json.toRawUTF8(), json.getNumBytesAsUTF8())
      .toHexString().toLowerCase();
}

inline bool fromJsonString(const juce::String& json, CloudProjectDescriptor& descriptor) {
  return fromVar(juce::JSON::parse(json), descriptor);
}
}  // namespace CloudSyncJson
