#pragma once

#include <JuceHeader.h>

#include <functional>

class AssetBootstrapManager {
public:
  using ProgressFn = std::function<void(double, const juce::String &)>;

  static juce::File getAssetsRootDirectory();
  static juce::File getEncoderBinaryPath();
  static juce::File getAvailableEncoderBinary();
  static juce::File getPadsPackDirectory();
  static juce::File getClicksPackDirectory();
  static juce::File getGuidesRootDirectory();
  static juce::File getGuidesLanguageSectionsDirectory(const juce::String &languageCode);
  static juce::File getGuidesLanguageCuesDirectory(const juce::String &languageCode);

  static bool ensureEncoderReady(juce::String *errorMessage = nullptr,
                                 const ProgressFn &progress = {});
  static bool ensurePadsPackReady(juce::String *errorMessage = nullptr,
                                 bool allowNetworkDownloads = true);
  static bool ensureClicksPackReady(juce::String *errorMessage = nullptr);
  static bool ensureGuidesPackReady(juce::String *errorMessage = nullptr);

private:
  static bool downloadFile(const juce::String &url, const juce::File &destination,
                           juce::String &errorMessage,
                           const ProgressFn &progress = {},
                           const juce::String &phaseText = {});
  static bool unpackZipToDirectory(const juce::File &zipFile,
                                   const juce::File &destinationDir,
                                   juce::String &errorMessage);
  static bool hasEnoughPadAudioFiles(const juce::File &dir) noexcept;
  static bool hasEnoughClickAudioFiles(const juce::File &dir) noexcept;
  static void persistState(const juce::String &encoderStatus,
                           const juce::String &padsStatus,
                           const juce::String &errorMessage);
};
