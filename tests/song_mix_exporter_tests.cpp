#include "audio/SongMixExporter.h"
#include "data/AssetBootstrapManager.h"

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

void writeSource(const juce::File &path, const int frames = 4800,
                 const std::function<float(int)> &value = {}) {
  auto stream = std::unique_ptr<juce::OutputStream>(path.createOutputStream());
  juce::WavAudioFormat wav;
  auto writer = wav.createWriterFor(
      stream, juce::AudioFormatWriterOptions{}
                  .withSampleRate(48000.0).withNumChannels(1).withBitsPerSample(24));
  expect(writer != nullptr, "source WAV writer opens");
  juce::AudioBuffer<float> samples(1, frames);
  samples.clear();
  for (int frame = 0; frame < samples.getNumSamples(); ++frame)
    samples.setSample(0, frame, value ? value(frame) : 0.25f);
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

juce::AudioBuffer<float> renderAndRead(const SongMixExportRequest &request,
                                     const int frames) {
  const auto result = SongMixExporter::exportToWav(request, {});
  if (!result.ok)
    std::cerr << result.error << '\n';
  expect(result.ok, "section automation bounce succeeds");
  expect(std::abs(result.durationSeconds - frames / request.sampleRate) < 1.0e-8,
         "arranged bounce retains the exact output duration");
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  auto reader = std::unique_ptr<juce::AudioFormatReader>(
      formats.createReaderFor(request.outputFile));
  expect(reader && reader->lengthInSamples == frames,
         "arranged WAV has the expected frame count");
  juce::AudioBuffer<float> samples(2, frames);
  expect(reader->read(&samples, 0, frames, 0, true, true),
         "arranged WAV decodes");
  return samples;
}

void checkSavedSectionAutomation(const juce::File &root) {
  const auto first = root.getChildFile("automation-first.wav");
  const auto second = root.getChildFile("automation-second.wav");
  writeSource(first, 57600, [](int) { return 0.2f; });
  writeSource(second, 57600, [](int) { return 0.1f; });
  SongMixExportRequest request;
  request.outputFile = root.getChildFile("automation-mix.wav");
  request.sampleRate = 48000.0;
  request.endSeconds = 1.2;
  // Deliberately different from the automation's order. A click/guide stem
  // remains manual even if an old or hand-edited snapshot targets its ID.
  request.stems = {
      {second.getFullPathName(), 0.8f, 0.75f, false, false, false, false, "keys"},
      {first.getFullPathName(), 0.5f, 1.0f, false, false, false, false, "bass"},
      {second.getFullPathName(), 0.2f, 1.0f, false, false, true, false, "click"}};
  SongArrangementPartSnapshot a, b, c;
  a.id = "a"; a.startSec = 0.1; a.endSec = 0.35;
  a.automation.stems = {{"bass", 0.25f, {}, {}},
                         {"missing-stem", 0.0f, {}, true},
                         {"click", 0.0f, {}, true}};
  b.id = "b"; b.startSec = 0.35; b.endSec = 0.65;
  b.automation.stems = {{"bass", {}, 2.0f, {}}, {"keys", {}, {}, true}};
  c.id = "c"; c.startSec = 0.9; c.endSec = 1.2;
  c.automation.stems = {{"bass", {}, {}, true}, {"keys", {}, 1.5f, {}}};
  // Sorting belongs to the renderer, not to the caller.
  request.arrangement.parts = {c, a, b};
  const auto savedArrangement = request.arrangement;
  const auto rendered = renderAndRead(request, 57600);
  const auto near = [&](int frame, float expected, const char *message) {
    expect(std::abs(meanMagnitude(rendered, frame, 128) - expected) < 0.00002f,
           message);
  };
  near(2400, 0.18f, "gap before the first section uses the base mix");
  near(9600, 0.13f, "saved volume targets its stable stem ID and leaves click manual");
  near(21600, 0.22f, "next section restores absent fields to base and applies gain/mute");
  near(38400, 0.18f, "gap after automation restores every stem's base mix");
  near(50400, 0.14f, "later sparse mute and gain affect only their own stems");
  expect(std::abs(rendered.getSample(0, 4799) - 0.18f) < 0.000002f,
         "automation cannot begin before its exact source-frame boundary");
  expect(rendered.getSample(0, 4800) < 0.18f &&
             rendered.getSample(0, 4800) > 0.1799f &&
             std::abs(rendered.getSample(0, 5519) - 0.155f) < 0.00001f &&
             std::abs(rendered.getSample(0, 6239) - 0.13f) < 0.00001f,
         "boundary changes use the live StemTrack 30 ms gain ramp");
  expect(request.stems[0].volume == 0.8f && request.stems[0].gain == 0.75f &&
             !request.stems[0].muted && request.stems[1].volume == 0.5f &&
             request.arrangement.parts[0].automation == savedArrangement.parts[0].automation &&
             request.arrangement.parts[1].automation == savedArrangement.parts[1].automation,
         "rendering leaves the base mix and committed automation unchanged");

  request.startSeconds = 0.4;
  request.endSeconds = 0.6;
  request.outputFile = root.getChildFile("automation-partial.wav");
  const auto partial = renderAndRead(request, 9600);
  expect(std::abs(partial.getSample(0, 0) - 0.22f) < 0.000002f,
         "export starting inside a section begins with that section's saved mix");
}

void checkCopiedSectionAutomation(const juce::File &root) {
  const auto source = root.getChildFile("copy-source.wav");
  writeSource(source, 48000, [](int frame) { return frame < 24000 ? 0.2f : 0.4f; });
  SongMixExportRequest request;
  request.outputFile = root.getChildFile("copy-mix.wav");
  request.sampleRate = 48000.0;
  // Zero means the complete arrangement, including the inserted copy.
  request.stems = {{source.getFullPathName(), 0.5f, 1.0f, false, false, false, false, "bass"}};
  SongArrangementPartSnapshot original, copy;
  original.id = "original"; original.startSec = 1.0; original.endSec = 1.5;
  copy.id = "copy"; copy.startSec = 0.25; copy.endSec = 0.75;
  copy.sourcePartId = "original"; copy.sourceStartSec = 0.5; copy.sourceEndSec = 1.0;
  copy.automation.stems = {{"bass", 0.25f, {}, {}}};
  request.arrangement.parts = {original, copy};
  const auto rendered = renderAndRead(request, 72000);
  expect(std::abs(meanMagnitude(rendered, 4800, 128) - 0.1f) < 0.00001f,
         "original audio before the insertion uses base volume");
  expect(std::abs(meanMagnitude(rendered, 21600, 128) - 0.1f) < 0.00001f,
         "copy moved before its original reads its explicit source and its own automation");
  expect(std::abs(meanMagnitude(rendered, 40800, 128) - 0.1f) < 0.00001f &&
             std::abs(meanMagnitude(rendered, 52800, 128) - 0.2f) < 0.00001f &&
             std::abs(meanMagnitude(rendered, 64800, 128) - 0.2f) < 0.00001f,
         "after a copy, original source resumes and copied automation does not leak");

  for (const double ratio : {0.75, 1.5}) {
    request.tempoRatio = ratio;
    request.outputFile = root.getChildFile("copy-tempo-" + juce::String(ratio) + ".wav");
    const auto stretched = renderAndRead(
        request, static_cast<int>(std::llround(72000.0 / ratio)));
    auto referenceRequest = request;
    referenceRequest.outputFile = root.getChildFile(
        "copy-tempo-reference-" + juce::String(ratio) + ".wav");
    referenceRequest.arrangement.parts[1].automation.stems.clear();
    const auto reference = renderAndRead(
        referenceRequest, static_cast<int>(std::llround(72000.0 / ratio)));
    const auto firstWindow = static_cast<int>(std::llround(21600.0 / ratio));
    const auto lastWindow = static_cast<int>(std::llround(64800.0 / ratio));
    const auto copiedLevel = meanMagnitude(stretched, firstWindow, 256);
    const auto restoredLevel = meanMagnitude(stretched, lastWindow, 256);
    const auto referenceCopy = meanMagnitude(reference, firstWindow, 256);
    const auto referenceRestored = meanMagnitude(reference, lastWindow, 256);
    // A phase vocoder need not preserve DC's absolute amplitude. Compare with
    // the same arranged render/DSP without automation, rather than treating a
    // constant fixture as an audible tone with unity frequency response.
    const bool correctLevels = referenceCopy > 0.01f && referenceRestored > 0.01f &&
        std::abs(copiedLevel / referenceCopy - 0.5f) < 0.03f &&
        std::abs(restoredLevel / referenceRestored - 1.0f) < 0.03f;
    if (!correctLevels)
      std::cerr << "tempo=" << ratio << " copied=" << copiedLevel
                << " reference-copy=" << referenceCopy << " restored=" << restoredLevel
                << " reference-restored=" << referenceRestored << '\n';
    expect(correctLevels,
           "tempo processing preserves copied audio, its automation and restored base mix");
  }
}
void checkIndependentGuideCueExport(const juce::File &root) {
  const auto source = root.getChildFile("silent-guide-source.wav");
  writeSource(source, 4800, [](int) { return 0.0f; });
  const auto cuesDirectory = AssetBootstrapManager::getGuidesLanguageCuesDirectory("en");
  writeSource(cuesDirectory.getChildFile("fixture exact a.wav"), 12, [](int) { return 0.1f; });
  writeSource(cuesDirectory.getChildFile("fixture exact b.wav"), 12, [](int) { return 0.2f; });
  SongMixExportRequest request;
  request.outputFile = root.getChildFile("independent-cue-mix.wav");
  request.sampleRate = 48000.0; request.endSeconds = 0.1;
  request.stems = {{source.getFullPathName()}};
  request.dynamicGuide.enabled = true; request.guideLanguage = "en";
  SongArrangementCueSnapshot a, b;
  a.id = "a"; a.name = "fixture exact a"; a.positionSec = 128.0 / 48000.0;
  b.id = "b"; b.name = "fixture exact b"; b.positionSec = 132.0 / 48000.0;
  request.arrangement.cues = {a, b};
  const auto rendered = renderAndRead(request, 4800);
  expect(rendered.getSample(0, 127) == 0.0f &&
      std::abs(rendered.getSample(0, 128) - 0.1f) < 1e-6 &&
      std::abs(rendered.getSample(0, 132) - 0.3f) < 1e-6 &&
      std::abs(rendered.getSample(0, 140) - 0.2f) < 1e-6 &&
      rendered.getSample(0, 144) == 0.0f,
      "real WAV bounce preserves independent cue sample offsets and overlapping voice tails");
  request.startSeconds = 256.0 / 48000.0;
  request.outputFile = root.getChildFile("independent-cue-seek.wav");
  const auto partial = renderAndRead(request, 4544);
  expect(partial.getMagnitude(0, 4544) == 0.0f,
      "export beginning after cues does not resurrect earlier instructions");
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
  checkSavedSectionAutomation(root);
  checkCopiedSectionAutomation(root);
  checkIndependentGuideCueExport(root);
  std::cout << "Song mix export regression checks passed\n";
}
