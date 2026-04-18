#include "AssetBootstrapManager.h"

#include "AppSettings.h"

namespace {
#if defined(MTCONTROLS_ASSETS_BASE_URL)
constexpr const char *kAssetsBaseUrl = MTCONTROLS_ASSETS_BASE_URL;
#else
constexpr const char *kAssetsBaseUrl = "https://storage.googleapis.com/playworship-assets";
#endif

constexpr const char *kEncoderVersion = "2026-04-18-1";
constexpr const char *kPadsVersion = "roman-v1";

juce::String buildEncoderUrl() {
#if JUCE_WINDOWS
  return juce::String(kAssetsBaseUrl) + "/encoder/windows/" + kEncoderVersion +
         "/ffmpeg.exe";
#elif JUCE_MAC
  return juce::String(kAssetsBaseUrl) + "/encoder/macos/" + kEncoderVersion +
         "/ffmpeg";
#else
  return juce::String(kAssetsBaseUrl) + "/encoder/linux/" + kEncoderVersion +
         "/ffmpeg";
#endif
}

juce::String buildPadsZipUrl() {
  return juce::String(kAssetsBaseUrl) + "/pads/" + kPadsVersion +
         "/pads-pack.zip";
}
} // namespace

juce::File AssetBootstrapManager::getAssetsRootDirectory() {
  auto root =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("MTControls")
          .getChildFile("Assets");
  root.createDirectory();
  return root;
}

juce::File AssetBootstrapManager::getEncoderBinaryPath() {
  auto dir = getAssetsRootDirectory().getChildFile("encoder");
  dir.createDirectory();
#if JUCE_WINDOWS
  return dir.getChildFile("ffmpeg.exe");
#else
  return dir.getChildFile("ffmpeg");
#endif
}

juce::File AssetBootstrapManager::getPadsPackDirectory() {
  auto dir = getAssetsRootDirectory().getChildFile("pads").getChildFile("RomanVizcaino");
  dir.createDirectory();
  return dir;
}

bool AssetBootstrapManager::downloadFile(const juce::String &url,
                                         const juce::File &destination,
                                         juce::String &errorMessage) {
  const juce::URL remote(url);
  const auto options =
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withConnectionTimeoutMs(45000)
          .withNumRedirectsToFollow(5);

  std::unique_ptr<juce::InputStream> input(remote.createInputStream(options));
  if (input == nullptr) {
    errorMessage = "Couldn't download asset from " + url;
    return false;
  }

  destination.getParentDirectory().createDirectory();
  if (destination.existsAsFile())
    destination.deleteFile();

  juce::FileOutputStream out(destination);
  if (!out.openedOk()) {
    errorMessage = "Couldn't open destination file: " +
                   destination.getFullPathName();
    return false;
  }

  constexpr int kChunkSize = 64 * 1024;
  juce::HeapBlock<char> buffer(static_cast<size_t>(kChunkSize));
  while (!input->isExhausted()) {
    const auto bytesRead = input->read(buffer.getData(), kChunkSize);
    if (bytesRead < 0) {
      errorMessage = "Error while reading remote asset stream.";
      return false;
    }
    if (bytesRead == 0)
      break;
    if (!out.write(buffer.getData(), bytesRead)) {
      errorMessage = "Error while writing downloaded file: " +
                     destination.getFullPathName();
      return false;
    }
  }

  out.flush();
  if (!destination.existsAsFile() || destination.getSize() <= 0) {
    errorMessage = "Downloaded file is empty: " + destination.getFileName();
    return false;
  }

  return true;
}

bool AssetBootstrapManager::unpackZipToDirectory(const juce::File &zipFile,
                                                 const juce::File &destinationDir,
                                                 juce::String &errorMessage) {
  if (!zipFile.existsAsFile()) {
    errorMessage = "Zip file not found: " + zipFile.getFullPathName();
    return false;
  }

  juce::ZipFile zip(zipFile);
  const int totalEntries = zip.getNumEntries();
  if (totalEntries <= 0) {
    errorMessage = "Zip file has no entries.";
    return false;
  }

  if (destinationDir.exists())
    destinationDir.deleteRecursively();
  destinationDir.createDirectory();

  for (int i = 0; i < totalEntries; ++i) {
    const auto *entry = zip.getEntry(i);
    if (entry == nullptr)
      continue;

    auto target = destinationDir.getChildFile(entry->filename);
    if (entry->filename.endsWithChar('/')) {
      target.createDirectory();
      continue;
    }

    target.getParentDirectory().createDirectory();
    std::unique_ptr<juce::InputStream> entryStream(zip.createStreamForEntry(i));
    if (entryStream == nullptr) {
      errorMessage = "Couldn't read zip entry: " + entry->filename;
      return false;
    }

    juce::FileOutputStream out(target);
    if (!out.openedOk()) {
      errorMessage = "Couldn't create extracted file: " +
                     target.getFullPathName();
      return false;
    }

    out.writeFromInputStream(*entryStream, -1);
  }

  return true;
}

bool AssetBootstrapManager::hasEnoughPadAudioFiles(const juce::File &dir) noexcept {
  if (!dir.isDirectory())
    return false;
  const auto files =
      dir.findChildFiles(juce::File::findFiles, true, "*.mp3;*.wav;*.aif;*.aiff");
  return files.size() >= 12;
}

void AssetBootstrapManager::persistState(const juce::String &encoderStatus,
                                         const juce::String &padsStatus,
                                         const juce::String &errorMessage) {
  auto state = AppSettings::getAssetsBootstrapState();
  state.encoderVersion = kEncoderVersion;
  state.encoderStatus = encoderStatus;
  state.padsVersion = kPadsVersion;
  state.padsStatus = padsStatus;
  state.lastError = errorMessage;
  state.updatedAtMs = juce::Time::currentTimeMillis();
  AppSettings::setAssetsBootstrapState(state);
}

bool AssetBootstrapManager::ensureEncoderReady(juce::String *errorMessage) {
  auto encoder = getEncoderBinaryPath();
  juce::String localError;

  if (encoder.existsAsFile() && encoder.getSize() > 0) {
    persistState("Ready", AppSettings::getAssetsBootstrapState().padsStatus, {});
    return true;
  }

  persistState("Downloading", AppSettings::getAssetsBootstrapState().padsStatus,
               {});
  auto temp = encoder.getSiblingFile(encoder.getFileName() + ".download");
  if (!downloadFile(buildEncoderUrl(), temp, localError)) {
    persistState("Error", AppSettings::getAssetsBootstrapState().padsStatus,
                 localError);
    if (errorMessage != nullptr)
      *errorMessage = localError;
    return false;
  }

  if (encoder.existsAsFile())
    encoder.deleteFile();
  if (!temp.moveFileTo(encoder)) {
    localError = "Couldn't move downloaded encoder into final location.";
    temp.deleteFile();
    persistState("Error", AppSettings::getAssetsBootstrapState().padsStatus,
                 localError);
    if (errorMessage != nullptr)
      *errorMessage = localError;
    return false;
  }

#if !JUCE_WINDOWS
  encoder.setExecutePermission(true);
#endif

  persistState("Ready", AppSettings::getAssetsBootstrapState().padsStatus, {});
  return true;
}

bool AssetBootstrapManager::ensurePadsPackReady(juce::String *errorMessage) {
  const auto padsDir = getPadsPackDirectory();
  juce::String localError;

  if (hasEnoughPadAudioFiles(padsDir)) {
    persistState(AppSettings::getAssetsBootstrapState().encoderStatus, "Ready",
                 {});
    return true;
  }

  persistState(AppSettings::getAssetsBootstrapState().encoderStatus,
               "Downloading", {});
  const auto tempZip =
      getAssetsRootDirectory().getChildFile("pads").getChildFile("pads-pack.zip.download");
  if (!downloadFile(buildPadsZipUrl(), tempZip, localError)) {
    persistState(AppSettings::getAssetsBootstrapState().encoderStatus, "Error",
                 localError);
    if (errorMessage != nullptr)
      *errorMessage = localError;
    return false;
  }

  if (!unpackZipToDirectory(tempZip, padsDir, localError)) {
    tempZip.deleteFile();
    persistState(AppSettings::getAssetsBootstrapState().encoderStatus, "Error",
                 localError);
    if (errorMessage != nullptr)
      *errorMessage = localError;
    return false;
  }

  tempZip.deleteFile();
  if (!hasEnoughPadAudioFiles(padsDir)) {
    localError = "Pads package doesn't contain enough note files.";
    persistState(AppSettings::getAssetsBootstrapState().encoderStatus, "Error",
                 localError);
    if (errorMessage != nullptr)
      *errorMessage = localError;
    return false;
  }

  persistState(AppSettings::getAssetsBootstrapState().encoderStatus, "Ready", {});
  return true;
}
