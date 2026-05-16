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

  static bool ensureEncoderReady(juce::String *errorMessage = nullptr,
                                 const ProgressFn &progress = {});
  static bool ensurePadsPackReady(juce::String *errorMessage = nullptr);

private:
  static bool downloadFile(const juce::String &url, const juce::File &destination,
                           juce::String &errorMessage,
                           const ProgressFn &progress = {},
                           const juce::String &phaseText = {});
  static bool unpackZipToDirectory(const juce::File &zipFile,
                                   const juce::File &destinationDir,
                                   juce::String &errorMessage);
  static bool hasEnoughPadAudioFiles(const juce::File &dir) noexcept;
  static void persistState(const juce::String &encoderStatus,
                           const juce::String &padsStatus,
                           const juce::String &errorMessage);
};
