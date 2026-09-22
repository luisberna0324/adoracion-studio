#include "../src/data/PortableFlacConversion.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void createAudio(const juce::File &file, juce::AudioFormat &format,
                 int bits, int channels, int frames = 150123) {
  std::unique_ptr<juce::OutputStream> output = file.createOutputStream();
  const auto options = juce::AudioFormatWriterOptions{}
                           .withSampleRate(48000.0)
                           .withNumChannels(channels)
                           .withBitsPerSample(bits)
                           .withSampleFormat(
                               bits == 32
                                   ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                   : juce::AudioFormatWriterOptions::SampleFormat::integral);
  auto writer = format.createWriterFor(output, options);
  expect(writer != nullptr, "test creates an input writer");
  juce::AudioBuffer<float> buffer(channels, frames);
  for (int channel = 0; channel < channels; ++channel)
    for (int frame = 0; frame < frames; ++frame)
      buffer.setSample(channel, frame,
                       0.6f * std::sin(static_cast<float>(frame) *
                                       (0.013f + 0.01f * channel)));
  expect(writer->writeFromAudioSampleBuffer(buffer, 0, frames),
         "test writes input samples");
}

std::unique_ptr<juce::AudioFormatReader> openAudio(const juce::File &file) {
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  return std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
}

void checkConversion(const juce::File &source, const juce::File &destination,
                     int expectedBits) {
  std::vector<double> progress;
  juce::String error = "old error";
  expect(portable_audio::convertToFlac(
             source, destination, error,
             [&progress](double value) { progress.push_back(value); }),
         "valid input converts without an external encoder");
  expect(error.isEmpty(), "successful conversion clears old errors");
  expect(progress.size() >= 3 && progress.front() == 0.0 &&
             progress.back() == 1.0,
         "streaming progress starts at zero and completes at one");
  for (size_t i = 1; i < progress.size(); ++i)
    expect(progress[i] >= progress[i - 1] && progress[i] <= 1.0,
           "conversion progress is monotonic and bounded");

  auto original = openAudio(source);
  auto encoded = openAudio(destination);
  expect(original != nullptr && encoded != nullptr,
         "source and converted FLAC are decodable");
  expect(encoded->sampleRate == original->sampleRate &&
             encoded->numChannels == original->numChannels &&
             encoded->lengthInSamples == original->lengthInSamples &&
             encoded->bitsPerSample == static_cast<unsigned int>(expectedBits),
         "FLAC preserves sample rate, channels, frames and supported precision");

  // The raw read overload handles every channel, including channels 3–8.
  constexpr int block = 257;
  const auto channels = static_cast<int>(original->numChannels);
  juce::AudioBuffer<float> a(channels, block), b(channels, block);
  std::vector<int *> ap(static_cast<size_t>(channels));
  std::vector<int *> bp(static_cast<size_t>(channels));
  for (int channel = 0; channel < channels; ++channel) {
    ap[static_cast<size_t>(channel)] =
        reinterpret_cast<int *>(a.getWritePointer(channel));
    bp[static_cast<size_t>(channel)] =
        reinterpret_cast<int *>(b.getWritePointer(channel));
  }
  for (juce::int64 position = 0; position < original->lengthInSamples;
       position += block) {
    const auto count = static_cast<int>(juce::jmin<juce::int64>(
        block, original->lengthInSamples - position));
    expect(original->read(ap.data(), channels, position, count, false) &&
               encoded->read(bp.data(), channels, position, count, false),
           "every converted audio block can be decoded");
    for (int channel = 0; channel < channels; ++channel)
      for (int sample = 0; sample < count; ++sample) {
        if (!original->usesFloatingPointData) {
          expect(ap[static_cast<size_t>(channel)][sample] ==
                     bp[static_cast<size_t>(channel)][sample],
                 "integer PCM samples survive losslessly in every channel");
        } else {
          const auto decoded = static_cast<double>(
                                   bp[static_cast<size_t>(channel)][sample]) /
                               2147483648.0;
          expect(std::abs(decoded - a.getSample(channel, sample)) < 0.000001,
                 "floating-point input is quantized within 24-bit precision");
        }
      }
  }
}

void testInputs(const juce::File &directory) {
  juce::WavAudioFormat wav;
  juce::AiffAudioFormat aiff;
  const auto stereo = directory.getChildFile("stereo.wav");
  createAudio(stereo, wav, 16, 2);
  const auto stereoFlac = directory.getChildFile("stereo.flac");
  checkConversion(stereo, stereoFlac, 16);
  checkConversion(stereoFlac, directory.getChildFile("roundtrip.flac"), 16);

  const auto mono = directory.getChildFile("mono.aiff");
  createAudio(mono, aiff, 24, 1, 12001);
  checkConversion(mono, directory.getChildFile("mono.flac"), 24);

  const auto surround = directory.getChildFile("multichannel.wav");
  createAudio(surround, wav, 24, 6, 17003);
  checkConversion(surround, directory.getChildFile("multichannel.flac"), 24);

  const auto floating = directory.getChildFile("floating.wav");
  createAudio(floating, wav, 32, 2, 32101);
  checkConversion(floating, directory.getChildFile("floating.flac"), 24);
}

void testFailures(const juce::File &directory) {
  juce::String error;
  const auto target = directory.getChildFile("existing.flac");
  expect(target.replaceWithText("Existing destination"), "test creates sentinel");
  const auto expectPreserved = [&] {
    expect(error.isNotEmpty(), "failed conversion explains the error");
    expect(target.loadFileAsString() == "Existing destination",
           "failed conversion preserves previous destination");
  };
  expect(!portable_audio::convertToFlac(directory.getChildFile("missing.wav"),
                                       target, error),
         "missing input is rejected");
  expectPreserved();
  const auto broken = directory.getChildFile("broken.wav");
  expect(broken.replaceWithText("not a wave"), "test creates corrupt audio");
  expect(!portable_audio::convertToFlac(broken, target, error),
         "undecodable input is rejected");
  expectPreserved();
  const auto unsupported = directory.getChildFile("unsupported.mp3");
  expect(unsupported.replaceWithText("unsupported"), "test creates unsupported file");
  expect(!portable_audio::convertToFlac(unsupported, target, error),
         "mobile import does not advertise unsupported codecs");
  expectPreserved();
  expect(!portable_audio::convertToFlac(target, target, error),
         "source cannot be overwritten in place");
  expectPreserved();

  const auto source = directory.getChildFile("stereo.wav");
  expect(!portable_audio::convertToFlac(source, target.getChildFile("out.flac"),
                                       error),
         "a file cannot be used as a destination directory");
  expectPreserved();

  // Caller unwind during a long conversion must close the writer and delete
  // its partial file without changing the previously saved target.
  const auto filesBefore = directory.getNumberOfChildFiles(juce::File::findFiles);
  struct Abort {};
  bool aborted = false;
  try {
    portable_audio::convertToFlac(source, target, error, [](double progress) {
      if (progress > 0.0)
        throw Abort{};
    });
  } catch (const Abort &) {
    aborted = true;
  }
  expect(aborted, "test interrupts an in-flight streaming conversion");
  expect(target.loadFileAsString() == "Existing destination",
         "interrupted conversion preserves destination");
  expect(directory.getNumberOfChildFiles(juce::File::findFiles) == filesBefore,
         "interrupted conversion removes the partial FLAC");
}

} // namespace

int main() {
  try {
    const juce::TemporaryFile directory(".portable-flac-tests");
    expect(directory.getFile().createDirectory().wasOk(),
           "test creates its isolated temporary directory");
    testInputs(directory.getFile());
    testFailures(directory.getFile());
    std::cout << "Portable FLAC conversion tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
