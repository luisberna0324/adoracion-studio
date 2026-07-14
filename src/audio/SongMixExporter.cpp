#include "SongMixExporter.h"

#include "../StemMixEngine.h"
#include "../StemTrack.h"

#include <cmath>

namespace {

constexpr int kExportBlockSize = 512;

float windowFadeAt(double t, double fadeStartSec, double windowEndSec,
                   double fadeOutSec) {
  if (fadeOutSec <= 0.0 || windowEndSec <= fadeStartSec)
    return 1.0f;
  if (t <= fadeStartSec)
    return 1.0f;
  if (t >= windowEndSec)
    return 0.0f;
  return static_cast<float>(juce::jlimit(0.0, 1.0, (windowEndSec - t) / fadeOutSec));
}

} // namespace

SongMixExporter::Result
SongMixExporter::exportToWav(const SongMixExportRequest &request,
                             const std::function<void(double)> &onProgress) {
  Result result;
  if (request.stems.empty()) {
    result.error = "No stems to export.";
    return result;
  }
  if (!request.outputFile.getParentDirectory().exists()) {
    result.error = "Output folder does not exist.";
    return result;
  }

  const double sampleRate =
      request.sampleRate > 0.0 ? request.sampleRate : 44100.0;
  const int numChannels = juce::jmax(1, request.numChannels);

  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();

  struct LoadedStem {
    std::unique_ptr<StemTrack> track;
    StemMixExportStemState state;
  };
  std::vector<LoadedStem> loaded;
  loaded.reserve(request.stems.size());

  double maxLengthSec = 0.0;
  for (const auto &stem : request.stems) {
    const juce::File file(stem.filePath);
    if (!file.existsAsFile())
      continue;

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager.createReaderFor(file));
    if (reader == nullptr)
      continue;

    const double readerSampleRate = reader->sampleRate;
    const auto lengthInSamples = reader->lengthInSamples;
    maxLengthSec = juce::jmax(
        maxLengthSec,
        readerSampleRate > 0.0
            ? static_cast<double>(lengthInSamples) / readerSampleRate
            : 0.0);

    LoadedStem entry;
    entry.state = stem;
    entry.track = std::make_unique<StemTrack>(std::move(reader), readerSampleRate,
                                              262144);
    entry.track->prepareToPlay(kExportBlockSize, sampleRate);
    loaded.push_back(std::move(entry));
  }

  if (loaded.empty()) {
    result.error = "Could not open any stem files.";
    return result;
  }

  const double startSec = juce::jmax(0.0, request.startSeconds);
  double endSec = request.endSeconds;
  if (endSec <= startSec || endSec <= 0.0)
    endSec = maxLengthSec;
  endSec = juce::jmin(endSec, maxLengthSec);
  if (endSec <= startSec) {
    result.error = "Invalid export range.";
    return result;
  }

  const double fadeOutSec = juce::jmax(0.0, request.fadeOutSeconds);
  const double fadeStartSec =
      fadeOutSec > 0.0 ? juce::jmax(startSec, endSec - fadeOutSec) : endSec;

  const juce::int64 startSample =
      static_cast<juce::int64>(std::llround(startSec * sampleRate));
  const juce::int64 endSample =
      static_cast<juce::int64>(std::llround(endSec * sampleRate));
  const juce::int64 totalSamples =
      juce::jmax<juce::int64>(0, endSample - startSample);

  std::vector<std::unique_ptr<StemTrack>> tracks;
  const size_t stemCountLoaded = loaded.size();
  auto clickGuideStates = std::make_unique<std::atomic<bool>[]>(stemCountLoaded);
  auto pitchShiftStates = std::make_unique<std::atomic<bool>[]>(stemCountLoaded);
  tracks.reserve(stemCountLoaded);

  bool anySolo = false;
  for (const auto &entry : loaded) {
    if (entry.state.soloed)
      anySolo = true;
  }

  for (size_t i = 0; i < loaded.size(); ++i) {
    auto &entry = loaded[i];
    clickGuideStates[i].store(entry.state.clickGuide, std::memory_order_relaxed);
    pitchShiftStates[i].store(entry.state.pitchShiftable,
                                std::memory_order_relaxed);
    entry.track->setVolume(juce::jlimit(0.0f, 1.0f, entry.state.volume));
    entry.track->setTrim(juce::jlimit(0.0f, 2.0f, entry.state.gain));
    entry.track->setMuted(entry.state.muted);
    entry.track->setSoloed(entry.state.soloed);
    entry.track->setGlobalMixState(1.0f, anySolo);
    entry.track->setNextReadPosition(startSample);
    tracks.push_back(std::move(entry.track));
  }

  StemMixEngine mixEngine;
  mixEngine.prepare(sampleRate, kExportBlockSize, numChannels);
  mixEngine.setPitchSemiTones(request.pitchSemiTones);
  mixEngine.setTempoRatio(request.tempoRatio);
  mixEngine.reset();

  const bool timeProcessActive =
      request.pitchSemiTones != 0 ||
      !juce::approximatelyEqual(request.tempoRatio, 1.0);

  const float masterOutGain =
      request.masterMuted
          ? 0.0f
          : juce::jlimit(0.0f, 1.0f, request.masterVolume) *
                juce::jlimit(0.0f, 2.0f, request.masterGain);

  juce::File outputFile = request.outputFile;
  outputFile.deleteFile();

  std::unique_ptr<juce::FileOutputStream> stream(
      outputFile.createOutputStream());
  if (stream == nullptr) {
    result.error = "Could not create output file.";
    return result;
  }

  juce::WavAudioFormat wavFormat;
  std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
      stream.get(), sampleRate, static_cast<unsigned int>(numChannels), 24, {},
      0));
  if (writer == nullptr) {
    result.error = "Could not create WAV writer.";
    return result;
  }
  stream.release();

  juce::AudioBuffer<float> buffer(numChannels, kExportBlockSize);
  const int stemCount = static_cast<int>(tracks.size());
  juce::int64 globalPos = startSample;
  juce::int64 writtenSamples = 0;

  while (globalPos < endSample) {
    const int numSamples = static_cast<int>(
        juce::jmin<juce::int64>(kExportBlockSize, endSample - globalPos));
    buffer.setSize(numChannels, numSamples, false, false, true);
    buffer.clear();

    const double blockStartSec = static_cast<double>(globalPos) / sampleRate;
    const double blockEndSec =
        static_cast<double>(globalPos + numSamples) / sampleRate;
    const float windowFadeStart =
        windowFadeAt(blockStartSec, fadeStartSec, endSec, fadeOutSec);
    const float windowFadeEnd =
        windowFadeAt(blockEndSec, fadeStartSec, endSec, fadeOutSec);

    mixEngine.mixStems(tracks, clickGuideStates.get(), pitchShiftStates.get(),
                       stemCount, numSamples, numChannels);
    mixEngine.renderToOutput(buffer, timeProcessActive, 1.0f, 1.0f,
                             windowFadeStart, windowFadeEnd);

    if (!juce::approximatelyEqual(masterOutGain, 1.0f))
      buffer.applyGain(masterOutGain);

    if (!writer->writeFromAudioSampleBuffer(buffer, 0, numSamples)) {
      result.error = "Failed while writing audio.";
      outputFile.deleteFile();
      return result;
    }

    globalPos += numSamples;
    writtenSamples += numSamples;
    if (onProgress != nullptr && totalSamples > 0)
      onProgress(static_cast<double>(writtenSamples) /
                 static_cast<double>(totalSamples));
  }

  writer.reset();
  result.ok = true;
  result.outputFile = outputFile;
  result.durationSeconds =
      sampleRate > 0.0 ? static_cast<double>(writtenSamples) / sampleRate : 0.0;
  return result;
}
