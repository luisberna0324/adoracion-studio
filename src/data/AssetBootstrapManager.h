#pragma once

#include <JuceHeader.h>

class AssetBootstrapManager {
public:
  static juce::File getAssetsRootDirectory();
  static juce::File getEncoderBinaryPath();
  static juce::File getPadsPackDirectory();

  static bool ensureEncoderReady(juce::String *errorMessage = nullptr);
  static bool ensurePadsPackReady(juce::String *errorMessage = nullptr);

private:
  static bool downloadFile(const juce::String &url, const juce::File &destination,
                           juce::String &errorMessage);
  static bool unpackZipToDirectory(const juce::File &zipFile,
                                   const juce::File &destinationDir,
                                   juce::String &errorMessage);
  static bool hasEnoughPadAudioFiles(const juce::File &dir) noexcept;
  static void persistState(const juce::String &encoderStatus,
                           const juce::String &padsStatus,
                           const juce::String &errorMessage);
};
