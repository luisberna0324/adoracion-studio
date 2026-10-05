#include "data/ApplicationDataPaths.h"
#include "data/AssetBootstrapManager.h"
#include "data/MobileImportStaging.h"
#include "data/PortableFlacConversion.h"
#include "data/PrimeLibrarySource.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void expect(bool result, const char *message) {
  if (!result)
    throw std::runtime_error(message);
}

void testNoExecutableBootstrap(const juce::File &root) {
  // A cached helper from an older edition must not silently re-enable child
  // processes or downloads. All test writes stay in the explicit temp root.
  const auto encoder = AssetBootstrapManager::getEncoderBinaryPath();
  expect(encoder.replaceWithText("not-an-executable"), "create stale encoder fixture");
  const auto before = root.findChildFiles(juce::File::findFiles, true).size();
  juce::String error;
  bool progressCalled = false;
  expect(!AssetBootstrapManager::ensureEncoderReady(
             &error, [&progressCalled](double, const juce::String &) { progressCalled = true; }),
         "Store edition must not accept or bootstrap an executable encoder");
  expect(error.isNotEmpty() && !progressCalled, "reject before any download progress");
  expect(AssetBootstrapManager::getAvailableEncoderBinary() == juce::File{},
         "cached encoder must not become available");
  expect(root.findChildFiles(juce::File::findFiles, true).size() == before &&
             encoder.loadFileAsString() == "not-an-executable",
         "rejection must not install, replace or persist asset state");
  expect(!PrimeLibrarySource::isAvailable(), "Store edition must not scan another app container");
  expect(PrimeLibrarySource::loadSongs(error).empty() && error.isNotEmpty(),
         "direct Prime access rejects with an explanation");
}

void createWav(const juce::File &file) {
  std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
  juce::WavAudioFormat format;
  auto writer = format.createWriterFor(stream,
      juce::AudioFormatWriterOptions{}.withSampleRate(44100).withNumChannels(2).withBitsPerSample(24));
  expect(writer != nullptr, "create native decode fixture");
  juce::AudioBuffer<float> samples(2, 12000);
  for (int i = 0; i < samples.getNumSamples(); ++i) {
    samples.setSample(0, i, 0.2f * static_cast<float>(std::sin(i * 0.04)));
    samples.setSample(1, i, 0.1f * static_cast<float>(std::sin(i * 0.07)));
  }
  expect(writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples()),
         "write fixture PCM");
}

void testNativeM4aAndPrivateCopy(const juce::File &root) {
  const auto source = root.getChildFile("test-source.wav");
  const auto m4a = root.getChildFile("test-source.m4a");
  createWav(source);
  // afconvert generates a test fixture only. The shipping app never invokes
  // it or FFmpeg; the conversion below must use CoreAudio in process.
  juce::ChildProcess fixtureEncoder;
  expect(fixtureEncoder.start(juce::StringArray{
             "/usr/bin/afconvert", "-f", "m4af", "-d", "aac", "-b", "256000",
             source.getFullPathName(), m4a.getFullPathName()}), "start fixture generator");
  expect(fixtureEncoder.waitForProcessToFinish(30000) && fixtureEncoder.getExitCode() == 0,
         "native system fixture generator succeeds");

  juce::String error;
  MobileImportStaging staging;
  const auto copy = staging.stage(juce::URL(m4a), error);
  expect(copy.existsAsFile() && error.isEmpty() && copy != m4a, "picker import creates a private copy");
  expect(m4a.deleteFile(), "original may disappear after the import");
  const auto converted = root.getChildFile("native-decoded.flac");
  expect(portable_audio::convertToFlac(copy, converted, error),
         "Mac Store M4A import converts via native CoreAudio without an encoder");
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  auto original = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(copy));
  auto decoded = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(converted));
  expect(original && decoded && decoded->numChannels == 2 &&
             decoded->sampleRate == original->sampleRate &&
             decoded->lengthInSamples == original->lengthInSamples,
         "native import preserves decoded format and frame count");
  juce::AudioBuffer<float> audio(2, 2048);
  expect(decoded->read(&audio, 0, audio.getNumSamples(), 0, true, true) &&
             audio.getMagnitude(0, audio.getNumSamples()) > 0.02f,
         "converted FLAC contains the source audio");
  const auto invalid = root.getChildFile("invalid.m4a");
  expect(invalid.replaceWithText("invalid audio") && converted.replaceWithText("keep-destination"),
         "prepare failure fixture");
  expect(!portable_audio::convertToFlac(invalid, converted, error) && error.isNotEmpty() &&
             converted.loadFileAsString() == "keep-destination",
         "failed native decode preserves existing destination");
}
} // namespace

int main() {
  try {
    const auto root = ApplicationDataPaths::isolatedTestRoot();
    testNoExecutableBootstrap(root);
    testNativeM4aAndPrivateCopy(root);
    std::cout << "Mac Store tests passed: no encoder bootstrap, private copies, native M4A and atomic failure\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
