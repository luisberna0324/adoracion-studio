#include "SongMixExporter.h"

#include "../StemMixEngine.h"
#include "../StemTrack.h"
#include "../MetronomeEngine.h"
#include "DynamicGuideEngine.h"
#include "SimpleDelayLine.h"
#include "../data/AssetBootstrapManager.h"
#include "../data/SongData.h"

#include <cmath>

namespace {
constexpr int kExportBlockSize = 512;

float windowFadeAt(const double timeSeconds, const double fadeStartSeconds,
                   const double endSeconds, const double fadeOutSeconds) {
  if (fadeOutSeconds <= 0.0 || timeSeconds <= fadeStartSeconds)
    return 1.0f;
  if (timeSeconds >= endSeconds)
    return 0.0f;
  return static_cast<float>(juce::jlimit(
      0.0, 1.0, (endSeconds - timeSeconds) / fadeOutSeconds));
}

int subdivisionForMask(const int mask) {
  return (mask & 0x04) != 0 ? 4 : ((mask & 0x02) != 0 ? 2 : 1);
}
} // namespace

namespace {
bool transcodeBounce(const juce::File &wavSource,
                     const SongMixExportRequest &request,
                     juce::String &error) {
  auto encoder = AssetBootstrapManager::getAvailableEncoderBinary();
  if (!encoder.existsAsFile()) {
    if (!AssetBootstrapManager::ensureEncoderReady(&error))
      return false;
    encoder = AssetBootstrapManager::getAvailableEncoderBinary();
  }
  if (!encoder.existsAsFile()) {
    error = "No se encontró el codificador para crear el formato elegido.";
    return false;
  }

  juce::StringArray args{encoder.getFullPathName(), "-y", "-v", "error",
                         "-i", wavSource.getFullPathName(), "-map_metadata",
                         "-1", "-vn"};
  if (request.format == SongMixExportRequest::Format::Mp3)
    args.addArray({"-c:a", "libmp3lame", "-b:a", "320k"});
  else
    args.addArray({"-c:a", "aac", "-b:a", "256k", "-movflags", "+faststart"});
  args.add(request.outputFile.getFullPathName());

  juce::ChildProcess process;
  if (!process.start(args)) {
    error = "No se pudo iniciar la conversión del bounce.";
    return false;
  }
  if (!process.waitForProcessToFinish(10 * 60 * 1000)) {
    process.kill();
    error = "La conversión del bounce excedió el tiempo permitido.";
    return false;
  }
  const auto output = process.readAllProcessOutput().trim();
  if (process.getExitCode() != 0 || !request.outputFile.existsAsFile()) {
    error = output.isNotEmpty() ? output : "No se pudo convertir el bounce.";
    request.outputFile.deleteFile();
    return false;
  }
  return true;
}
} // namespace

SongMixExporter::Result SongMixExporter::exportToWav(
    const SongMixExportRequest &request,
    const std::function<void(double)> &onProgress) {
  Result result;
  if (request.stems.empty()) {
    result.error = "No hay stems para exportar.";
    return result;
  }
  if (!request.outputFile.getParentDirectory().exists()) {
    result.error = "La carpeta de destino no existe.";
    return result;
  }

  const double sampleRate = request.sampleRate > 0.0 ? request.sampleRate : 44100.0;
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  juce::TimeSliceThread readThread("SongMixExportReader");

  struct LoadedStem {
    std::unique_ptr<StemTrack> track;
    StemMixExportStemState state;
  };
  std::vector<LoadedStem> loaded;
  double maxLengthSeconds = 0.0;
  for (const auto &stem : request.stems) {
    const juce::File file(stem.filePath);
    auto reader = file.existsAsFile()
                      ? std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file))
                      : nullptr;
    if (reader == nullptr)
      continue;
    maxLengthSeconds = juce::jmax(
        maxLengthSeconds, static_cast<double>(reader->lengthInSamples) / reader->sampleRate);
    LoadedStem entry;
    entry.state = stem;
    entry.track = std::make_unique<StemTrack>(std::move(reader), readThread,
                                               sampleRate, 262144, false);
    entry.track->prepareToPlay(kExportBlockSize, sampleRate);
    loaded.push_back(std::move(entry));
  }
  if (loaded.empty()) {
    result.error = "No se pudo abrir ningún stem de audio.";
    return result;
  }

  const double startSeconds = juce::jmax(0.0, request.startSeconds);
  double endSeconds = request.endSeconds;
  if (endSeconds <= startSeconds)
    endSeconds = maxLengthSeconds;
  endSeconds = juce::jmin(endSeconds, maxLengthSeconds);
  if (endSeconds <= startSeconds) {
    result.error = "El rango de exportación no es válido.";
    return result;
  }

  const auto startFrame = static_cast<juce::int64>(std::llround(startSeconds * sampleRate));
  const auto totalFrames = static_cast<juce::int64>(std::llround((endSeconds - startSeconds) * sampleRate));
  const auto fadeStartSeconds = request.fadeOutSeconds > 0.0
                                    ? juce::jmax(startSeconds, endSeconds - request.fadeOutSeconds)
                                    : endSeconds;

  std::vector<std::unique_ptr<StemTrack>> tracks;
  auto clickGuide = std::make_unique<std::atomic<bool>[]>(loaded.size());
  auto pitchShiftable = std::make_unique<std::atomic<bool>[]>(loaded.size());
  bool anySolo = request.dynamicClick.soloed || request.dynamicGuide.soloed;
  for (const auto &stem : loaded)
    anySolo = anySolo || stem.state.soloed;
  for (size_t i = 0; i < loaded.size(); ++i) {
    auto &stem = loaded[i];
    clickGuide[i].store(stem.state.clickGuide, std::memory_order_relaxed);
    pitchShiftable[i].store(stem.state.pitchShiftable, std::memory_order_relaxed);
    stem.track->setVolume(juce::jlimit(0.0f, 1.0f, stem.state.volume));
    stem.track->setTrim(juce::jlimit(0.0f, 2.0f, stem.state.gain));
    stem.track->setMuted(stem.state.muted);
    stem.track->setSoloed(stem.state.soloed);
    stem.track->setGlobalMixState(1.0f, anySolo);
    stem.track->setNextReadPosition(startFrame);
    tracks.push_back(std::move(stem.track));
  }

  StemMixEngine mix;
  mix.prepare(sampleRate, kExportBlockSize, 2);
  mix.setPitchSemiTones(request.pitchSemiTones);
  mix.setTempoRatio(request.tempoRatio);
  mix.reset();
  const bool timeProcessing = request.pitchSemiTones != 0 ||
                              !juce::approximatelyEqual(request.tempoRatio, 1.0);
  const float masterGain = request.masterMuted
                               ? 0.0f
                               : juce::jlimit(0.0f, 1.0f, request.masterVolume) *
                                     juce::jlimit(0.0f, 2.0f, request.masterGain);

  juce::AudioFormatManager virtualFormats;
  virtualFormats.registerBasicFormats();
  MetronomeEngine click;
  click.prepare(sampleRate);
  click.setEnabled(request.dynamicClick.enabled);
  click.setBpm(juce::jlimit(20.0, 300.0, request.playbackBpm));
  click.setBeatsPerBar(request.beatsPerBar);
  click.setDivisionMask(request.clickDivisionMask);
  click.setSubdivision(subdivisionForMask(request.clickDivisionMask));
  click.setVolume(1.0f);
  click.setMuted(false);
  juce::String clickLoadError;
  click.loadClickSamplesFromFolder(AssetBootstrapManager::getClicksPackDirectory(),
                                   request.clickStyle, virtualFormats,
                                   &clickLoadError);
  click.syncPhaseForTimeSeconds(request.startSeconds);

  DynamicGuideEngine guide;
  guide.prepare(sampleRate);
  if (request.dynamicGuide.enabled) {
    juce::String guideLoadError;
    guide.loadGuideDirectories(
        {AssetBootstrapManager::getGuidesLanguageSectionsDirectory(request.guideLanguage),
         AssetBootstrapManager::getGuidesLanguageCuesDirectory(request.guideLanguage)},
        virtualFormats, &guideLoadError);
  }
  SimpleDelayLine clickDelay;
  SimpleDelayLine guideDelay;
  clickDelay.prepare(2, 65536);
  guideDelay.prepare(2, 65536);
  size_t nextGuideSection = 0;
  const double guideLeadSeconds =
      (60.0 / juce::jmax(1.0, request.playbackBpm)) *
      juce::jmax(1, request.beatsPerBar);

  auto outputFile = request.outputFile;
  outputFile.deleteFile();
  const bool needsTranscode = request.format != SongMixExportRequest::Format::Wav;
  const auto renderFile = needsTranscode
                              ? outputFile.getSiblingFile(
                                    "." + outputFile.getFileNameWithoutExtension() +
                                    "-" + juce::Uuid().toString() + ".wav")
                              : outputFile;
  renderFile.deleteFile();
  auto stream = std::unique_ptr<juce::FileOutputStream>(renderFile.createOutputStream());
  juce::WavAudioFormat wav;
  auto writer = stream != nullptr ? std::unique_ptr<juce::AudioFormatWriter>(
      wav.createWriterFor(stream.get(), sampleRate, 2, 24, {}, 0)) : nullptr;
  if (writer == nullptr) {
    result.error = "No se pudo crear el archivo WAV.";
    return result;
  }
  stream.release();

  juce::AudioBuffer<float> buffer(2, kExportBlockSize);
  juce::AudioBuffer<float> clickBuffer(2, kExportBlockSize);
  juce::AudioBuffer<float> guideBuffer(2, kExportBlockSize);
  juce::int64 written = 0;
  while (written < totalFrames) {
    const int outputFrames = static_cast<int>(juce::jmin<juce::int64>(kExportBlockSize, totalFrames - written));
    const int inputFrames = mix.requiredInputFrames(outputFrames);
    buffer.setSize(2, outputFrames, false, false, true);
    const double blockStartSeconds = startSeconds + static_cast<double>(written) / sampleRate;
    const double blockEndSeconds = blockStartSeconds + static_cast<double>(outputFrames) / sampleRate;
    mix.mixStems(tracks, clickGuide.get(), pitchShiftable.get(),
                 static_cast<int>(tracks.size()), inputFrames, 2);
    mix.renderToOutput(buffer, timeProcessing, inputFrames, 1.0f, 1.0f,
                       windowFadeAt(blockStartSeconds, fadeStartSeconds, endSeconds, request.fadeOutSeconds),
                       windowFadeAt(blockEndSeconds, fadeStartSeconds, endSeconds, request.fadeOutSeconds));

    const int virtualLatency = mix.getOutputLatencySamples();
    clickBuffer.setSize(2, outputFrames, false, false, true);
    clickBuffer.clear();
    const bool clickAudible = request.dynamicClick.enabled &&
                              !request.dynamicClick.muted &&
                              (!anySolo || request.dynamicClick.soloed);
    if (clickAudible) {
      click.syncPhaseForTimeSeconds(blockStartSeconds);
      click.process(clickBuffer);
      clickBuffer.applyGain(
          juce::jlimit(0.0f, 1.0f, request.dynamicClick.volume) *
          juce::jlimit(0.0f, 4.0f, request.dynamicClick.gain));
    }
    clickDelay.setDelay(virtualLatency);
    clickDelay.process(clickBuffer);
    for (int ch = 0; ch < 2; ++ch)
      buffer.addFrom(ch, 0, clickBuffer, ch, 0, outputFrames);

    guideBuffer.setSize(2, outputFrames, false, false, true);
    guideBuffer.clear();
    const bool guideAudible = request.dynamicGuide.enabled &&
                              !request.dynamicGuide.muted &&
                              (!anySolo || request.dynamicGuide.soloed);
    if (guideAudible) {
      while (nextGuideSection < request.guideSections.size()) {
        const auto &section = request.guideSections[nextGuideSection];
        const double triggerSeconds =
            juce::jmax(0.0, section.startSeconds - guideLeadSeconds);
        if (triggerSeconds >= blockEndSeconds)
          break;
        if (triggerSeconds >= blockStartSeconds) {
          if (!guide.triggerForSectionKey(section.name)) {
            const auto canonical = canonicalArrangementSectionName(section.name);
            if (canonical.isNotEmpty())
              guide.triggerForSectionKey(canonical);
          }
        }
        ++nextGuideSection;
      }
      guide.render(guideBuffer);
      guideBuffer.applyGain(
          juce::jlimit(0.0f, 1.0f, request.dynamicGuide.volume) *
          juce::jlimit(0.0f, 2.0f, request.dynamicGuide.gain));
    }
    guideDelay.setDelay(virtualLatency);
    guideDelay.process(guideBuffer);
    for (int ch = 0; ch < 2; ++ch)
      buffer.addFrom(ch, 0, guideBuffer, ch, 0, outputFrames);

    if (!juce::approximatelyEqual(masterGain, 1.0f))
      buffer.applyGain(masterGain);
    if (!writer->writeFromAudioSampleBuffer(buffer, 0, outputFrames)) {
      result.error = "Falló la escritura del audio.";
      renderFile.deleteFile();
      return result;
    }
    written += outputFrames;
    if (onProgress)
      onProgress(static_cast<double>(written) /
                 static_cast<double>(totalFrames));
  }

  writer.reset();
  if (needsTranscode) {
    if (onProgress)
      onProgress(1.0);
    const bool converted = transcodeBounce(renderFile, request, result.error);
    renderFile.deleteFile();
    if (!converted)
      return result;
  }
  result.ok = true;
  result.outputFile = outputFile;
  result.durationSeconds = static_cast<double>(written) / sampleRate;
  return result;
}
