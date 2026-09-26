#include "audio/SongMixExporter.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void writeSource(const juce::File &path) {
  auto stream = std::unique_ptr<juce::OutputStream>(path.createOutputStream());
  juce::WavAudioFormat wav;
  auto writer = wav.createWriterFor(
      stream, juce::AudioFormatWriterOptions{}
                  .withSampleRate(48000.0).withNumChannels(1).withBitsPerSample(24));
  expect(writer != nullptr, "source WAV writer opens");
  juce::AudioBuffer<float> samples(1, 4800);
  samples.clear();
  for (int frame = 0; frame < samples.getNumSamples(); ++frame)
    samples.setSample(0, frame, 0.25f);
  expect(writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples()),
         "source WAV is written");
}

float meanMagnitude(const juce::AudioBuffer<float> &samples,
                    const int first, const int count) {
  double sum = 0.0;
  for (int frame = first; frame < first + count; ++frame)
    sum += std::abs(samples.getSample(0, frame));
  return static_cast<float>(sum / count);
}
} // namespace

int main() {
  const auto rootName = juce::SystemStats::getEnvironmentVariable(
      "PLAYWORSHIP_CREATION_TEST_ROOT", {});
  expect(rootName.isNotEmpty(), "export test has an isolated temporary root");
  const juce::File root(rootName);
  expect(root.isDirectory(), "isolated export root exists");
  const auto source = root.getChildFile("source.wav");
  const auto output = root.getChildFile("mix.wav");
  writeSource(source);

  SongMixExportRequest request;
  request.outputFile = output;
  request.sampleRate = 48000.0;
  request.endSeconds = 0.1;
  request.fadeOutSeconds = 0.04;
  StemMixExportStemState stem;
  stem.filePath = source.getFullPathName();
  request.stems.push_back(stem);

  double lastProgress = -1.0;
  const auto result = SongMixExporter::exportToWav(request,
      [&](const double progress) {
        expect(progress >= lastProgress && progress <= 1.0,
               "export progress is monotonic and bounded");
        lastProgress = progress;
      });
  expect(result.ok && output.existsAsFile(), "offline export writes a WAV");
  expect(std::abs(result.durationSeconds - 0.1) < 1.0e-6 &&
             lastProgress == 1.0,
         "duration and final progress follow written output frames");

  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  auto reader = std::unique_ptr<juce::AudioFormatReader>(
      formats.createReaderFor(output));
  expect(reader != nullptr && reader->numChannels == 2 &&
             reader->bitsPerSample == 24 && reader->lengthInSamples == 4800,
         "exported WAV has the requested frame count, channels and bit depth");
  juce::AudioBuffer<float> rendered(2, 4800);
  expect(reader->read(&rendered, 0, 4800, 0, true, true),
         "exported WAV can be reopened and decoded");
  const auto early = meanMagnitude(rendered, 512, 256);
  const auto late = meanMagnitude(rendered, 4672, 128);
  expect(early > 0.05f && late < early * 0.25f,
         "window fade preserves early audio and attenuates the end");
  std::cout << "Song mix export regression checks passed\n";
}
