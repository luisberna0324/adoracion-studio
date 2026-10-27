#include "SectionDetectionService.h"
#include "../platform/AppleMusicStructureBridge.h"
#include <juce_cryptography/juce_cryptography.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cmath>

namespace playworship::analysis {
namespace {
class AppleAnalyzer final : public IMusicStructureAnalyzer {
public:
  bool available() const override { return pwMusicStructureAvailable() != 0; }
  juce::String unavailableReason() const override { return juce::String::fromUTF8(pwMusicStructureUnavailableReason()); }
  juce::String providerId() const override { return "apple-music-understanding"; }
  juce::String version() const override { return "rhythm-structure-1"; }
  bool analyze(const juce::File &file, double duration, const CancellationCheck &cancelled,
               MusicStructureAnalysis &out, juce::String &error) override {
    struct WaitState { std::mutex mutex; std::condition_variable cv; bool done = false; juce::String json, error; };
    WaitState state;
    const auto token = pwMusicStructureStart(file.getFullPathName().toRawUTF8(),
      [](const char *json, const char *failure, void *context) {
        auto &s = *static_cast<WaitState *>(context);
        std::lock_guard<std::mutex> lock(s.mutex);
        s.json = juce::String::fromUTF8(json ? json : "");
        s.error = juce::String::fromUTF8(failure ? failure : "");
        s.done = true; s.cv.notify_one();
      }, &state);
    if (token == 0) { error = unavailableReason(); return false; }
    std::unique_lock<std::mutex> lock(state.mutex);
    bool sentCancel = false;
    while (!state.done) {
      if (cancelled && cancelled() && !sentCancel) {
        sentCancel = true;
        lock.unlock(); pwMusicStructureCancel(token); lock.lock();
      }
      state.cv.wait_for(lock, std::chrono::milliseconds(40));
    }
    // Bridge guarantees exactly one callback even when cancelled. Context and
    // temporary audio remain alive until it has finished reading the asset.
    if (sentCancel || (cancelled && cancelled())) { error = "Cancelado"; return false; }
    if (state.error.isNotEmpty()) { error = state.error; return false; }
    const auto json = juce::JSON::parse(state.json);
    const auto *object = json.getDynamicObject();
    if (!object) { error = juce::String::fromUTF8("Respuesta de análisis inválida"); return false; }
    out.provider = providerId(); out.version = version(); out.tempo.source = "MusicUnderstanding.rhythm";
    const auto ranges = [&](const char *key, std::vector<TimeRange> &dest) {
      if (auto *items = object->getProperty(key).getArray()) for (const auto &item : *items) {
        if (const auto *range = item.getDynamicObject())
          dest.push_back({double(range->getProperty("startSeconds")), double(range->getProperty("endSeconds"))});
      }
    };
    ranges("sections", out.sections); ranges("segments", out.segments); ranges("phrases", out.phrases);
    const auto times = [&](const char *key, std::vector<double> &dest) {
      if (auto *items = object->getProperty(key).getArray()) for (const auto &item : *items) dest.push_back(double(item));
    };
    times("beats", out.tempo.beats); times("bars", out.tempo.bars);
    if (object->hasProperty("bpm")) out.tempo.beatsPerMinute = double(object->getProperty("bpm"));
    return validateMusicStructure(out, duration, error);
  }
};

struct AudioSource {
  AnalysisStem stem;
  std::unique_ptr<juce::AudioFormatReader> reader;
};

bool prepareSources(const SectionDetectionRequest &request, juce::AudioFormatManager &formats,
                    std::vector<AudioSource> &sources, double &duration, double &rate,
                    juce::String &fingerprint, juce::String &error, const CancellationCheck &cancelled) {
  juce::MemoryOutputStream identity;
  identity.writeString("section-audio-mix-1:mono:peak-0.95:linear-absolute-time");
  int musicCount = 0;
  for (const auto &stem : request.stems) {
    if (cancelled()) return false;
    if (stem.role == AnalysisStemRole::excluded) continue;
    if (!std::isfinite(stem.offsetSeconds) || stem.offsetSeconds < 0) { error = juce::String::fromUTF8("Offset de stem inválido"); return false; }
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(stem.file));
    if (!reader || reader->sampleRate <= 0 || reader->lengthInSamples <= 0 || reader->numChannels == 0) {
      error = "No se puede leer el audio: " + stem.file.getFileName(); return false;
    }
    duration = std::max(duration, stem.offsetSeconds + double(reader->lengthInSamples) / reader->sampleRate);
    if (stem.role == AnalysisStemRole::music) {
      ++musicCount; rate = std::max(rate, reader->sampleRate);
      auto stream = stem.file.createInputStream();
      if (!stream) { error = "No se puede abrir el audio"; return false; }
      // Hash the full content in bounded blocks, with cancellation between blocks.
      juce::MemoryOutputStream chunks;
      juce::HeapBlock<char> buffer(1024 * 1024);
      while (!stream->isExhausted()) {
        if (cancelled()) return false;
        const auto n = stream->read(buffer.getData(), 1024 * 1024);
        if (n <= 0) { error = "Error leyendo la huella del audio"; return false; }
        chunks.writeString(juce::SHA256(buffer.getData(), size_t(n)).toHexString());
      }
      identity.writeString(juce::SHA256(chunks.getData(), chunks.getDataSize()).toHexString());
      identity.writeDouble(stem.offsetSeconds);
      sources.push_back({stem, std::move(reader)});
    }
  }
  if (musicCount == 0) { error = juce::String::fromUTF8("Selecciona al menos una pista musical; guía y click se analizan por separado."); return false; }
  if (!std::isfinite(duration) || duration <= 0 || duration > 12 * 3600 || rate > 384000) {
    error = juce::String::fromUTF8("Duración o frecuencia de audio fuera del límite de análisis local (12 h / 384 kHz)."); return false;
  }
  identity.writeDouble(duration); identity.writeDouble(rate);
  fingerprint = juce::SHA256(identity.getData(), identity.getDataSize()).toHexString();
  return true;
}

bool renderMix(std::vector<AudioSource> &sources, double duration, double rate,
               const juce::File &scratch, const juce::File &output,
               juce::String &error, const CancellationCheck &cancelled) {
  constexpr int block = 8192;
  const auto frameCount = static_cast<juce::int64>(std::ceil(duration * rate));
  juce::WavAudioFormat wav;
  std::unique_ptr<juce::OutputStream> stream = scratch.createOutputStream();
  auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
      .withSampleRate(rate).withNumChannels(1).withBitsPerSample(32)
      .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
  if (!writer) { error = "No se pudo crear la mezcla temporal"; return false; }
  juce::AudioBuffer<float> mix(1, block);
  float peak = 0;
  for (juce::int64 frame = 0; frame < frameCount; frame += block) {
    if (cancelled()) return false;
    const int count = int(std::min<juce::int64>(block, frameCount - frame));
    mix.clear();
    for (auto &source : sources) {
      const auto ratio = source.reader->sampleRate / rate;
      const double firstPosition = (double(frame) / rate - source.stem.offsetSeconds) * source.reader->sampleRate;
      const auto first = static_cast<juce::int64>(std::floor(firstPosition));
      const auto last = static_cast<juce::int64>(std::ceil(firstPosition + count * ratio)) + 2;
      const int toRead = int(last - first + 1);
      juce::AudioBuffer<float> input(int(source.reader->numChannels), toRead);
      input.clear();
      if (!source.reader->read(&input, 0, toRead, first, true, true)) { error = "Error decodificando la mezcla"; return false; }
      for (int i = 0; i < count; ++i) {
        const double absolute = (double(frame + i) / rate - source.stem.offsetSeconds) * source.reader->sampleRate;
        if (absolute < 0 || absolute >= double(source.reader->lengthInSamples)) continue;
        const double pos = absolute - double(first);
        const int a = int(std::floor(pos)); const float f = float(pos - a);
        double sample = 0;
        for (int c = 0; c < input.getNumChannels(); ++c)
          sample += input.getSample(c, a) * (1.0f - f) + input.getSample(c, a + 1) * f;
        sample /= double(input.getNumChannels()) * double(sources.size());
        if (!std::isfinite(sample)) { error = "El audio contiene muestras no finitas"; return false; }
        mix.addSample(0, i, float(sample));
      }
    }
    peak = std::max(peak, mix.getMagnitude(0, count));
    if (!writer->writeFromAudioSampleBuffer(mix, 0, count)) { error = "Error escribiendo la mezcla"; return false; }
  }
  writer.reset();
  auto reader = std::unique_ptr<juce::AudioFormatReader>(wav.createReaderFor(scratch.createInputStream().release(), true));
  auto finalStream = output.createOutputStream();
  writer.reset(finalStream ? wav.createWriterFor(finalStream.get(), rate, 1, 24, {}, 0) : nullptr);
  if (!reader || !writer) { error = "No se pudo normalizar la mezcla"; return false; }
  finalStream.release();
  const float gain = 0.95f / std::max(1.0f, peak);
  for (juce::int64 frame = 0; frame < frameCount; frame += block) {
    if (cancelled()) return false;
    const int count = int(std::min<juce::int64>(block, frameCount - frame));
    if (!reader->read(&mix, 0, count, frame, true, false)) { error = "Error leyendo la mezcla"; return false; }
    mix.applyGain(0, count, gain);
    if (!writer->writeFromAudioSampleBuffer(mix, 0, count)) { error = "Error guardando la mezcla"; return false; }
  }
  return true;
}
} // namespace

juce::String detectionStageName(DetectionStage s) {
  switch (s) {
    case DetectionStage::preparingAudio: return juce::String::fromUTF8("Preparando audio…");
    case DetectionStage::analyzingStructure: return juce::String::fromUTF8("Analizando estructura y compases…");
    case DetectionStage::transcribingGuide: return juce::String::fromUTF8("Transcribiendo guía…");
    case DetectionStage::mergingSuggestions: return juce::String::fromUTF8("Fusionando sugerencias…");
    case DetectionStage::review: return juce::String::fromUTF8("Revisión de propuestas");
    case DetectionStage::cancelled: return "Cancelado";
    case DetectionStage::error: return juce::String::fromUTF8("Error de análisis");
  }
  return {};
}
std::shared_ptr<IMusicStructureAnalyzer> createPlatformMusicStructureAnalyzer() { return std::make_shared<AppleAnalyzer>(); }
SectionDetectionService::SectionDetectionService(std::shared_ptr<IMusicStructureAnalyzer> p) : provider(std::move(p)) {}
bool SectionDetectionService::available() const { return provider && provider->available(); }
juce::String SectionDetectionService::unavailableReason() const { return provider ? provider->unavailableReason() : juce::String::fromUTF8("Sin proveedor de análisis"); }

std::shared_ptr<SectionDetectionTask> SectionDetectionService::start(SectionDetectionRequest request,
    std::function<void(DetectionStage)> progress, std::function<void(DetectionOutcome)> completion) const {
  auto task = std::make_shared<SectionDetectionTask>();
  auto cancelledState = task->cancelled;
  std::thread([p = provider, request = std::move(request), progress = std::move(progress),
               completion = std::move(completion), cancelledState] {
    DetectionOutcome outcome;
    const auto cancelled = [cancelledState] { return cancelledState->load(); };
    const auto report = [&](DetectionStage stage) { if (progress) progress(stage); };
    const auto run = [&] {
      if (cancelled()) return;
      if (!p || !p->available()) { outcome.error = p ? p->unavailableReason() : "Sin proveedor"; return; }
      report(DetectionStage::preparingAudio);
      juce::AudioFormatManager formats; formats.registerBasicFormats();
      std::vector<AudioSource> sources; double duration = 0, rate = 0; juce::String fingerprint;
      if (!prepareSources(request, formats, sources, duration, rate, fingerprint, outcome.error, cancelled)) return;
      auto &result = outcome.result;
      result.songId = request.songId; result.durationSeconds = duration;
      result.analysisProvider = p->providerId(); result.analysisVersion = sectionDetectionAlgorithmVersion;
      result.analyzedAudioFingerprint = fingerprint; result.structure.provider = p->providerId(); result.structure.version = p->version();
      const auto cacheKey = sectionDetectionCacheKey(result, request.fusion);
      const auto cacheFile = request.cacheDirectory.getChildFile(cacheKey + ".json");
      if (request.cacheDirectory != juce::File{} && cacheFile.existsAsFile() && cacheFile.getSize() < 8 * 1024 * 1024) {
        SectionDetectionResult cached; juce::String ignored;
        if (sectionDetectionResultFromVar(juce::JSON::parse(cacheFile), cached, ignored) &&
            cached.analyzedAudioFingerprint == fingerprint && cached.analysisProvider == p->providerId() &&
            cached.structure.version == p->version() && cached.durationSeconds == duration &&
            validateMusicStructure(cached.structure, duration, ignored)) {
          result.structure = std::move(cached.structure); outcome.cacheHit = true;
        }
      }
      if (!outcome.cacheHit) {
        const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getNonexistentChildFile("playworship-sections", "", false);
        if (!directory.createDirectory()) { outcome.error = juce::String::fromUTF8("No se pudo crear el temporal de análisis"); return; }
        struct Cleanup { juce::File directory; ~Cleanup() { directory.deleteRecursively(); } } cleanup{directory};
        const auto mix = directory.getChildFile("analysis.wav");
        if (!renderMix(sources, duration, rate, directory.getChildFile("scratch.wav"), mix, outcome.error, cancelled)) return;
        if (cancelled()) return;
        report(DetectionStage::analyzingStructure);
        if (!p->analyze(mix, duration, cancelled, result.structure, outcome.error)) return;
        if (!validateMusicStructure(result.structure, duration, outcome.error)) return;
        if (cancelled()) return;
        if (request.cacheDirectory != juce::File{} && request.cacheDirectory.createDirectory()) {
          juce::TemporaryFile temporary(cacheFile);
          if (temporary.getFile().replaceWithText(juce::JSON::toString(sectionDetectionResultToVar(result))))
            temporary.overwriteTargetFileWithTemporary();
          // Bound metadata cache; no decoded audio is retained.
          auto files = request.cacheDirectory.findChildFiles(juce::File::findFiles, false, "*.json");
          std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.getLastModificationTime() < b.getLastModificationTime(); });
          while (files.size() > 64) { files.getReference(0).deleteFile(); files.remove(0); }
        }
      }
      if (cancelled()) return;
      report(DetectionStage::mergingSuggestions);
      fuseSectionSuggestions(result, request.fusion, outcome.error, cancelled);
    };
    try { run(); } catch (const std::exception &e) { outcome.error = juce::String::fromUTF8("Error de análisis local: ") + juce::String(e.what()); }
    outcome.cancelled = cancelled();
    report(outcome.cancelled ? DetectionStage::cancelled : outcome.error.isNotEmpty() ? DetectionStage::error : DetectionStage::review);
    if (completion) completion(std::move(outcome));
  }).detach();
  return task;
}
} // namespace playworship::analysis
