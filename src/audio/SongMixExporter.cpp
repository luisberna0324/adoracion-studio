#include "SongMixExporter.h"

#include "../StemMixEngine.h"
#include "../StemTrack.h"
#include "../MetronomeEngine.h"
#include "DynamicGuideEngine.h"
#include "DynamicGuideTiming.h"
#include "SimpleDelayLine.h"
#include "../data/AssetBootstrapManager.h"
#include "../data/SongData.h"

#include <cmath>

namespace {
constexpr int kExportBlockSize = 512;
constexpr int kMaxExportLatencySamples = 65536;
constexpr int kLatencyStableBlocks = 4;
constexpr int kMaxWarmupOutputFrames =
    kMaxExportLatencySamples + (16 * kExportBlockSize);

float windowFadeAt(const double timeSeconds, const double fadeStartSeconds,
                   const double endSeconds, const double fadeOutSeconds) {
  if (fadeOutSeconds <= 0.0 || timeSeconds <= fadeStartSeconds)
    return 1.0f;
  if (timeSeconds >= endSeconds)
    return 0.0f;
  return static_cast<float>(juce::jlimit(
      0.0, 1.0, (endSeconds - timeSeconds) / fadeOutSeconds));
}

float finiteClamped(const float value, const float minimum,
                    const float maximum, const float fallback) {
  return juce::jlimit(minimum, maximum,
                      std::isfinite(value) ? value : fallback);
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

  const double sampleRate =
      std::isfinite(request.sampleRate) && request.sampleRate > 0.0
          ? request.sampleRate
          : 44100.0;
  const double fadeOutSeconds =
      std::isfinite(request.fadeOutSeconds)
          ? juce::jmax(0.0, request.fadeOutSeconds)
          : 0.0;
  const double playbackBpm = juce::jlimit(
      20.0, 300.0,
      std::isfinite(request.playbackBpm) ? request.playbackBpm : 120.0);
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
    const double sourceSampleRate =
        std::isfinite(reader->sampleRate) && reader->sampleRate > 0.0
            ? reader->sampleRate
            : sampleRate;
    maxLengthSeconds = juce::jmax(
        maxLengthSeconds,
        static_cast<double>(reader->lengthInSamples) / sourceSampleRate);
    LoadedStem entry;
    entry.state = stem;
    entry.track = std::make_unique<StemTrack>(std::move(reader), readThread,
                                               sourceSampleRate, 262144, false);
    entry.track->prepareToPlay(kExportBlockSize, sampleRate);
    loaded.push_back(std::move(entry));
  }
  if (loaded.empty()) {
    result.error = "No se pudo abrir ningún stem de audio.";
    return result;
  }

  const double startSeconds =
      std::isfinite(request.startSeconds)
          ? juce::jmax(0.0, request.startSeconds)
          : 0.0;
  double endSeconds =
      std::isfinite(request.endSeconds) ? request.endSeconds
                                        : maxLengthSeconds;
  if (endSeconds <= startSeconds)
    endSeconds = maxLengthSeconds;
  endSeconds = juce::jmin(endSeconds, maxLengthSeconds);
  if (endSeconds <= startSeconds) {
    result.error = "El rango de exportación no es válido.";
    return result;
  }

  const long double startFrameExact =
      static_cast<long double>(startSeconds) * sampleRate;
  const long double sourceFramesExact =
      static_cast<long double>(endSeconds - startSeconds) * sampleRate;
  const long double maxFrame =
      static_cast<long double>(std::numeric_limits<juce::int64>::max());
  if (!std::isfinite(startFrameExact) ||
      !std::isfinite(sourceFramesExact) || startFrameExact > maxFrame ||
      sourceFramesExact > maxFrame ||
      startFrameExact + sourceFramesExact > maxFrame) {
    result.error = "El rango de exportación excede el tamaño soportado.";
    return result;
  }
  const auto startFrame =
      static_cast<juce::int64>(std::llround(startFrameExact));
  const auto sourceFrames =
      static_cast<std::int64_t>(std::llround(sourceFramesExact));
  const song_mix_export::Timeline timeline(
      sampleRate, startSeconds, sourceFrames, request.tempoRatio);
  const auto totalFrames =
      static_cast<juce::int64>(timeline.getOutputFrames());
  if (totalFrames <= 0) {
    result.error = "El rango de exportación no contiene muestras de audio.";
    return result;
  }
  const double tempoRatio = timeline.getTempoRatio();
  const double renderEndSeconds =
      startSeconds + static_cast<double>(sourceFrames) / sampleRate;
  const auto fadeStartSeconds =
      fadeOutSeconds > 0.0
          ? juce::jmax(startSeconds,
                       renderEndSeconds - fadeOutSeconds)
          : renderEndSeconds;

  std::vector<std::unique_ptr<StemTrack>> tracks;
  auto clickGuide = std::make_unique<std::atomic<bool>[]>(loaded.size());
  auto pitchShiftable = std::make_unique<std::atomic<bool>[]>(loaded.size());
  bool anySolo =
      (request.dynamicClick.enabled && request.dynamicClick.soloed) ||
      (request.dynamicGuide.enabled && request.dynamicGuide.soloed);
  for (const auto &stem : loaded)
    anySolo = anySolo || stem.state.soloed;
  for (size_t i = 0; i < loaded.size(); ++i) {
    auto &stem = loaded[i];
    clickGuide[i].store(stem.state.clickGuide, std::memory_order_relaxed);
    pitchShiftable[i].store(stem.state.pitchShiftable, std::memory_order_relaxed);
    stem.track->setVolume(
        finiteClamped(stem.state.volume, 0.0f, 1.0f, 1.0f));
    stem.track->setTrim(
        finiteClamped(stem.state.gain, 0.0f, 2.0f, 1.0f));
    stem.track->setMuted(stem.state.muted);
    stem.track->setSoloed(stem.state.soloed);
    stem.track->setGlobalMixState(1.0f, anySolo);
    stem.track->setNextReadPosition(startFrame);
    tracks.push_back(std::move(stem.track));
  }

  StemMixEngine mix;
  mix.prepare(sampleRate, kExportBlockSize, 2);
  mix.setPitchSemiTones(request.pitchSemiTones);
  mix.setTempoRatio(tempoRatio);
  mix.reset();
  const bool timeProcessing = request.pitchSemiTones != 0 ||
                              !juce::approximatelyEqual(tempoRatio, 1.0);
  const float masterGain = request.masterMuted
                               ? 0.0f
                               : finiteClamped(request.masterVolume, 0.0f, 1.0f,
                                               1.0f) *
                                     finiteClamped(request.masterGain, 0.0f,
                                                   2.0f, 1.0f);

  juce::AudioFormatManager virtualFormats;
  virtualFormats.registerBasicFormats();
  MetronomeEngine click;
  click.prepare(sampleRate);
  click.setEnabled(request.dynamicClick.enabled);
  click.setBpm(playbackBpm);
  click.setBeatsPerBar(request.beatsPerBar);
  click.setDivisionMask(request.clickDivisionMask);
  click.setSubdivision(subdivisionForMask(request.clickDivisionMask));
  click.setVolume(1.0f);
  click.setMuted(false);
  juce::String clickLoadError;
  click.loadClickSamplesFromFolder(AssetBootstrapManager::getClicksPackDirectory(),
                                   request.clickStyle, virtualFormats,
                                   &clickLoadError);
  const double playbackStartSeconds =
      timeline.playbackSecondsAtOutputBoundary(0);
  click.syncPhaseForTimeSeconds(playbackStartSeconds);
  const double startBeatPosition = playbackStartSeconds * playbackBpm / 60.0;
  const double beatBoundaryTolerance =
      (0.5 * playbackBpm / (60.0 * sampleRate)) + 1.0e-9;
  if (std::abs(startBeatPosition - std::round(startBeatPosition)) <=
      beatBoundaryTolerance) {
    click.triggerImmediateClick();
  }

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
  clickDelay.prepare(2, kMaxExportLatencySamples);
  guideDelay.prepare(2, kMaxExportLatencySamples);
  const double guideLeadSourceSeconds =
      (60.0 / playbackBpm) *
      juce::jmax(1, request.beatsPerBar) * tempoRatio;
  const auto guideTriggers = song_mix_export::prepareGuideTriggers(
      request.guideSections, guideLeadSourceSeconds);
  size_t nextGuideTrigger = 0;

  int outputLatencySamples = 0;
  if (timeProcessing) {
    // Warm the stretcher with silence without consuming the selected source
    // range. The same prepared voices are then used for the real render.
    for (auto &track : tracks)
      track->setNextReadPosition(track->getTotalLength());

    juce::AudioBuffer<float> warmupBuffer(2, kExportBlockSize);
    int lastObservedLatency = -1;
    int stableLatencyBlocks = 0;
    int warmedOutputFrames = 0;
    while (warmedOutputFrames < kMaxWarmupOutputFrames &&
           stableLatencyBlocks < kLatencyStableBlocks) {
      const int inputFrames = mix.requiredInputFrames(kExportBlockSize);
      mix.mixStems(tracks, clickGuide.get(), pitchShiftable.get(),
                   static_cast<int>(tracks.size()), inputFrames, 2);
      warmupBuffer.clear();
      mix.renderToOutput(warmupBuffer, true, inputFrames, 1.0f, 1.0f, 1.0f,
                         1.0f);
      warmedOutputFrames += kExportBlockSize;

      const int observedLatency = mix.getOutputLatencySamples();
      if (observedLatency > 0) {
        stableLatencyBlocks =
            lastObservedLatency > 0 &&
                    std::abs(observedLatency - lastObservedLatency) <= 2
                ? stableLatencyBlocks + 1
                : 1;
        lastObservedLatency = observedLatency;
        if (warmedOutputFrames <
            observedLatency + (2 * kExportBlockSize)) {
          stableLatencyBlocks = 0;
        }
      }
    }

    if (stableLatencyBlocks < kLatencyStableBlocks ||
        lastObservedLatency <= 0) {
      result.error =
          "No se pudo estabilizar la latencia del procesamiento de tono/tempo.";
      return result;
    }
    if (lastObservedLatency > kMaxExportLatencySamples) {
      result.error =
          "La latencia del procesamiento excede el límite del exportador.";
      return result;
    }
    outputLatencySamples = lastObservedLatency;
  }
  if (totalFrames >
      std::numeric_limits<juce::int64>::max() -
          static_cast<juce::int64>(outputLatencySamples)) {
    result.error = "La duración y latencia del render exceden el límite soportado.";
    return result;
  }
  const auto generatedTarget =
      static_cast<juce::int64>(outputLatencySamples) + totalFrames;

  for (auto &track : tracks)
    track->setNextReadPosition(startFrame);

  clickDelay.setDelay(outputLatencySamples);
  guideDelay.setDelay(outputLatencySamples);

  auto outputFile = request.outputFile;
  outputFile.deleteFile();
  const bool needsTranscode = request.format != SongMixExportRequest::Format::Wav;
  const auto renderFile = needsTranscode
                              ? outputFile.getSiblingFile(
                                    "." + outputFile.getFileNameWithoutExtension() +
                                    "-" + juce::Uuid().toString() + ".wav")
                              : outputFile;
  renderFile.deleteFile();
  auto stream =
      std::unique_ptr<juce::OutputStream>(renderFile.createOutputStream());
  juce::WavAudioFormat wav;
  const auto writerOptions =
      juce::AudioFormatWriterOptions{}
          .withSampleRate(sampleRate)
          .withNumChannels(2)
          .withBitsPerSample(24);
  auto writer =
      stream != nullptr ? wav.createWriterFor(stream, writerOptions) : nullptr;
  if (writer == nullptr) {
    result.error = "No se pudo crear el archivo WAV.";
    return result;
  }

  juce::AudioBuffer<float> buffer(2, kExportBlockSize);
  juce::AudioBuffer<float> clickBuffer(2, kExportBlockSize);
  juce::AudioBuffer<float> guideBuffer(2, kExportBlockSize);
  juce::int64 written = 0;
  juce::int64 generated = 0;
  juce::int64 sourceOutputGenerated = 0;
  bool tailPrepared = false;

  while (written < totalFrames) {
    const bool feedingSource = sourceOutputGenerated < totalFrames;
    const auto outputFramesRemaining =
        feedingSource ? totalFrames - sourceOutputGenerated
                      : generatedTarget - generated;
    if (outputFramesRemaining <= 0) {
      result.error = "No se pudo drenar la cola del procesamiento de audio.";
      writer.reset();
      renderFile.deleteFile();
      return result;
    }

    auto blockOutputFrames =
        juce::jmin<juce::int64>(kExportBlockSize, outputFramesRemaining);
    const auto latencyBoundary =
        static_cast<juce::int64>(outputLatencySamples);
    if (generated < latencyBoundary &&
        generated + blockOutputFrames > latencyBoundary) {
      blockOutputFrames = latencyBoundary - generated;
    }
    const int outputFrames = static_cast<int>(blockOutputFrames);
    // Keep the processor's fractional scheduler moving for the tail. While
    // source is active, Timeline overrides this nominal value so the finite
    // selected range is consumed exactly once.
    const int nominalInputFrames = mix.requiredInputFrames(outputFrames);
    int inputFrames = nominalInputFrames;
    if (feedingSource) {
      const auto sourceFrameStart =
          timeline.sourceFrameAtOutputBoundary(sourceOutputGenerated);
      const auto sourceFrameEnd = timeline.sourceFrameAtOutputBoundary(
          sourceOutputGenerated + outputFrames);
      inputFrames = static_cast<int>(sourceFrameEnd - sourceFrameStart);
      if (inputFrames <= 0) {
        result.error = "El mapeo temporal produjo un bloque fuente vacío.";
        writer.reset();
        renderFile.deleteFile();
        return result;
      }
    } else if (!tailPrepared) {
      // Never read material located after the selected source range while
      // flushing the algorithmic tail.
      for (auto &track : tracks)
        track->setNextReadPosition(track->getTotalLength());
      tailPrepared = true;
    }

    buffer.setSize(2, outputFrames, false, false, true);

    const auto alignedOutputStart = juce::jlimit<juce::int64>(
        0, totalFrames,
        generated - static_cast<juce::int64>(outputLatencySamples));
    const auto alignedOutputEnd = juce::jlimit<juce::int64>(
        0, totalFrames,
        generated + outputFrames -
            static_cast<juce::int64>(outputLatencySamples));
    const double fadeBlockStartSeconds =
        timeline.sourceSecondsAtOutputBoundary(alignedOutputStart);
    const double fadeBlockEndSeconds =
        timeline.sourceSecondsAtOutputBoundary(alignedOutputEnd);

    mix.mixStems(tracks, clickGuide.get(), pitchShiftable.get(),
                 static_cast<int>(tracks.size()), inputFrames, 2);
    mix.renderToOutput(buffer, timeProcessing, inputFrames, 1.0f, 1.0f,
                       windowFadeAt(fadeBlockStartSeconds, fadeStartSeconds,
                                    renderEndSeconds,
                                    fadeOutSeconds),
                       windowFadeAt(fadeBlockEndSeconds, fadeStartSeconds,
                                    renderEndSeconds,
                                    fadeOutSeconds));

    clickBuffer.setSize(2, outputFrames, false, false, true);
    clickBuffer.clear();
    const bool clickAudible = request.dynamicClick.enabled &&
                              !request.dynamicClick.muted &&
                              (!anySolo || request.dynamicClick.soloed);
    if (feedingSource && clickAudible) {
      click.syncPhaseForTimeSeconds(
          timeline.playbackSecondsAtOutputBoundary(sourceOutputGenerated));
      click.process(clickBuffer);
      clickBuffer.applyGain(
          finiteClamped(request.dynamicClick.volume, 0.0f, 1.0f, 1.0f) *
          finiteClamped(request.dynamicClick.gain, 0.0f, 4.0f, 1.0f));
    }
    clickDelay.process(clickBuffer);
    for (int ch = 0; ch < 2; ++ch)
      buffer.addFrom(ch, 0, clickBuffer, ch, 0, outputFrames);

    guideBuffer.setSize(2, outputFrames, false, false, true);
    guideBuffer.clear();
    const bool guideAudible = request.dynamicGuide.enabled &&
                              !request.dynamicGuide.muted &&
                              (!anySolo || request.dynamicGuide.soloed);
    if (feedingSource && guideAudible) {
      const double virtualBlockStartSeconds =
          timeline.sourceSecondsAtOutputBoundary(sourceOutputGenerated);
      const double virtualBlockEndSeconds =
          timeline.sourceSecondsAtOutputBoundary(sourceOutputGenerated +
                                                 outputFrames);
      while (nextGuideTrigger < guideTriggers.size()) {
        const auto &trigger = guideTriggers[nextGuideTrigger];
        if (trigger.triggerSeconds >= virtualBlockEndSeconds)
          break;
        if (trigger.triggerSeconds >= virtualBlockStartSeconds) {
          if (!guide.triggerForSectionKey(trigger.name)) {
            const auto canonical =
                canonicalArrangementSectionName(trigger.name);
            if (canonical.isNotEmpty())
              guide.triggerForSectionKey(canonical);
          }
        }
        ++nextGuideTrigger;
      }
      guide.render(guideBuffer);
      guideBuffer.applyGain(
          finiteClamped(request.dynamicGuide.volume, 0.0f, 1.0f, 1.0f) *
          finiteClamped(request.dynamicGuide.gain, 0.0f, 2.0f, 1.0f));
    }
    guideDelay.process(guideBuffer);
    for (int ch = 0; ch < 2; ++ch)
      buffer.addFrom(ch, 0, guideBuffer, ch, 0, outputFrames);

    if (!juce::approximatelyEqual(masterGain, 1.0f))
      buffer.applyGain(masterGain);

    const int skipFrames = static_cast<int>(juce::jlimit<juce::int64>(
        0, outputFrames,
        static_cast<juce::int64>(outputLatencySamples) - generated));
    const int framesToWrite = static_cast<int>(juce::jmin<juce::int64>(
        outputFrames - skipFrames, totalFrames - written));
    if (framesToWrite > 0) {
      if (!writer->writeFromAudioSampleBuffer(buffer, skipFrames,
                                              framesToWrite)) {
        result.error = "Falló la escritura del audio.";
        writer.reset();
        renderFile.deleteFile();
        return result;
      }
      written += framesToWrite;
      if (onProgress)
        onProgress(static_cast<double>(written) /
                   static_cast<double>(totalFrames));
    }

    generated += outputFrames;
    if (feedingSource)
      sourceOutputGenerated += outputFrames;
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
