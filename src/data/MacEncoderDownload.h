#pragma once

#include "MacEncoderCompatibility.h"
#include <juce_cryptography/juce_cryptography.h>

namespace mac_encoder {

struct DownloadAsset {
  Architecture architecture;
  const char *version;
  const char *relativePath;
  juce::int64 sizeBytes;
  const char *sha256;
};

inline DownloadAsset downloadAsset(Architecture architecture) {
  if (architecture == Architecture::intel64) {
    return {architecture, "2026-11-06-1",
            "/encoder/macos/2026-11-06-1/x86_64/ffmpeg", 3364064,
            "c65eae3b76aa6138693269c06f566f6441daf5059ae23a4715708f0f2094129d"};
  }
  return {architecture, "2026-04-18-1",
          "/encoder/macos/2026-04-18-1/ffmpeg", 51576880,
          "22a02449174a956e2b72f54a652913794af6eb8c5400b98760263fd2caf24f77"};
}

inline bool validateDownloadAsset(const DownloadAsset &asset,
                                  juce::String &error) {
  error.clear();
  const juce::String expectedHash(asset.sha256);
  if (asset.sizeBytes <= 0 || expectedHash.length() != 64 ||
      !expectedHash.containsOnly("0123456789abcdefABCDEF")) {
    error = "The audio encoder download is not configured for this Mac's architecture.";
    return false;
  }
  return true;
}

// Integrity checks apply to freshly downloaded managed assets. Existing
// compatible bundled or user-provided encoders remain reusable without
// hashing a large executable whenever the UI checks availability.
inline bool verifyDownload(const juce::File &temporary,
                           const DownloadAsset &asset, juce::String &error) {
  if (!validateDownloadAsset(asset, error))
    return false;
  if (!temporary.existsAsFile() || temporary.getSize() != asset.sizeBytes) {
    error = "The downloaded audio encoder has an unexpected size. Please try again.";
    return false;
  }
  auto stream = temporary.createInputStream();
  if (stream == nullptr || !stream->getStatus().wasOk()) {
    error = "Couldn't read the downloaded audio encoder.";
    return false;
  }
  const auto actualHash = juce::SHA256(*stream).toHexString();
  if (!stream->getStatus().wasOk() || stream->getPosition() != asset.sizeBytes ||
      !actualHash.equalsIgnoreCase(asset.sha256)) {
    error = "The downloaded audio encoder failed its integrity check. Please try again.";
    return false;
  }
  if (!supportsArchitecture(temporary, asset.architecture)) {
    error = "The downloaded audio encoder is incompatible with this Mac's architecture.";
    return false;
  }
  return true;
}

inline bool installVerifiedDownload(const juce::File &temporary,
                                    const juce::File &destination,
                                    const DownloadAsset &asset,
                                    juce::String &error) {
  error.clear();
  // Check this before any cleanup so misuse cannot remove the installed file.
  if (temporary == destination) {
    error = "The encoder download must use a separate temporary file.";
    return false;
  }
  if (!verifyDownload(temporary, asset, error)) {
    temporary.deleteFile();
    return false;
  }
  return installCompatibleDownload(temporary, destination, asset.architecture, error);
}

} // namespace mac_encoder
