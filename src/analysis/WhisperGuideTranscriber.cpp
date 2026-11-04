#include "GuideTranscription.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <thread>

#if defined(PLAYWORSHIP_WHISPER_GUIDE) && PLAYWORSHIP_WHISPER_GUIDE
#include <whisper.h>
#endif

namespace playworship::analysis {
namespace {
constexpr double analysisRate = 16000.0;
constexpr double maximumDuration = 12.0 * 3600.0;
constexpr juce::int64 maximumModelBytes = 256 * 1024 * 1024;
constexpr int frameSamples = 320;             // 20 ms at 16 kHz.
constexpr int preRollFrames = 10;             // 200 ms of original context.
constexpr int endingSilenceFrames = 15;       // Separate independent announcements.
constexpr int minimumActiveFrames = 4;        // Reject short isolated clicks.
constexpr int maximumUtteranceFrames = 1000;  // Bounded PCM: at most 20 seconds.
constexpr const char *algorithmVersion = "guide-energy-sinc16k-beam5-prompt128-2";

bool isCancelled(const CancellationCheck &cancelled) { return cancelled && cancelled(); }

// JUCE's streaming SHA-256 keeps constant memory. Cancellation is checked on
// every read; a partial hash is never returned to the caller as a valid hash.
class HashStream final : public juce::InputStream {
public:
  HashStream(juce::FileInputStream &s, const CancellationCheck &c) : source(s), cancelled(c) {}
  juce::int64 getTotalLength() override { return source.getTotalLength(); }
  juce::int64 getPosition() override { return source.getPosition(); }
  bool setPosition(juce::int64 p) override { return source.setPosition(p); }
  bool isExhausted() override { return isCancelled(cancelled) || source.isExhausted(); }
  int read(void *destination, int bytes) override {
    return isCancelled(cancelled) ? 0 : source.read(destination, std::min(bytes, 64 * 1024));
  }
private:
  juce::FileInputStream &source;
  const CancellationCheck &cancelled;
};

bool hashFile(const juce::File &file, const CancellationCheck &cancelled,
              juce::String &hash, juce::String &error) {
  auto input = file.createInputStream();
  if (!input) { error = juce::String::fromUTF8("No se puede abrir el archivo local de reconocimiento."); return false; }
  const auto originalLength = input->getTotalLength();
  HashStream stream(*input, cancelled);
  const auto value = juce::SHA256(stream).toHexString();
  if (isCancelled(cancelled)) { error = "Cancelado"; return false; }
  if (input->getPosition() != originalLength || input->getStatus().failed()) {
    error = juce::String::fromUTF8("No se pudo leer la huella completa del archivo."); return false;
  }
  hash = value;
  return true;
}

#if defined(PLAYWORSHIP_WHISPER_GUIDE) && PLAYWORSHIP_WHISPER_GUIDE
std::timed_mutex transcriptionMutex;

struct InferenceCallbacks {
  const CancellationCheck &cancelled;
  const GuideTranscriptionProgress &progress;
  double baseProgress = 0.0, progressSpan = 0.0;
};

// Read small blocks by absolute source position. A windowed-sinc low-pass
// resampler avoids aliasing when downsampling (including 44.1/48/96 kHz).
// The output origin is still sample zero; no trimming or group delay is added.
class SourceResampler {
public:
  explicit SourceResampler(juce::AudioFormatReader &r) : reader(r) {}
  bool read(juce::int64 outputFrame, int count, float *output,
            const CancellationCheck &cancelled, juce::String &error) {
    constexpr int radius = 24;
    const double ratio = reader.sampleRate / analysisRate;
    const auto first = static_cast<juce::int64>(std::floor(double(outputFrame) * ratio)) - radius;
    const auto last = static_cast<juce::int64>(std::ceil(double(outputFrame + count) * ratio)) + radius;
    const auto length = int(last - first + 1);
    input.setSize(int(reader.numChannels), length, false, false, true);
    input.clear();
    const auto from = std::max<juce::int64>(0, first);
    const auto to = std::min(reader.lengthInSamples, last + 1);
    if (to > from && !reader.read(&input, int(from - first), int(to - from), from, true, true)) {
      error = juce::String::fromUTF8("No se puede decodificar la pista de guía."); return false;
    }
    mono.resize(size_t(length));
    for (int i = 0; i < length; ++i) {
      if ((i & 4095) == 0 && isCancelled(cancelled)) return false;
      double sum = 0;
      for (int channel = 0; channel < input.getNumChannels(); ++channel) {
        const double sample = input.getSample(channel, i);
        if (!std::isfinite(sample)) { error = "El audio contiene muestras no finitas."; return false; }
        sum += sample;
      }
      mono[size_t(i)] = float(sum / input.getNumChannels());
    }
    const double cutoff = 0.475 * std::min(1.0, 1.0 / ratio);
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < count; ++i) {
      if ((i & 255) == 0 && isCancelled(cancelled)) return false;
      const double absolute = double(outputFrame + i) * ratio;
      const auto centre = static_cast<juce::int64>(std::floor(absolute));
      double value = 0, weights = 0;
      for (int tap = -radius + 1; tap <= radius; ++tap) {
        const auto sourceIndex = centre + tap;
        const double distance = absolute - double(sourceIndex);
        const double argument = 2.0 * cutoff * distance;
        const double sinc = std::abs(argument) < 1.0e-12 ? 1.0 : std::sin(pi * argument) / (pi * argument);
        const double window = 0.5 + 0.5 * std::cos(pi * distance / radius);
        const double weight = 2.0 * cutoff * sinc * window;
        value += mono[size_t(sourceIndex - first)] * weight;
        weights += weight;
      }
      const double sample = weights != 0.0 ? value / weights : 0.0;
      if (!std::isfinite(sample) || std::abs(sample) > std::numeric_limits<float>::max()) {
        error = "El audio contiene amplitudes no finitas."; return false;
      }
      output[i] = float(sample);
    }
    return true;
  }
private:
  juce::AudioFormatReader &reader;
  juce::AudioBuffer<float> input;
  std::vector<float> mono;
};

class WhisperGuideTranscriber final : public IGuideTranscriber {
public:
  bool available() const override { return true; }
  juce::String unavailableReason() const override { return {}; }
  juce::String providerId() const override { return "whisper.cpp"; }
  juce::String version() const override { return juce::String(whisper_version()) + ":" + algorithmVersion; }

  bool transcribe(const GuideTranscriptionRequest &request, const CancellationCheck &cancelled,
                  const GuideTranscriptionProgress &progress, GuideTranscript &out,
                  juce::String &error) override {
    error.clear();
    const auto cancelledNow = [&] {
      if (!isCancelled(cancelled)) return false;
      error = "Cancelado"; return true;
    };
    if (cancelledNow()) return false;
    if (!std::isfinite(request.offsetSeconds) || request.offsetSeconds < 0 || request.offsetSeconds > maximumDuration) {
      error = juce::String::fromUTF8("Offset de guía inválido."); return false;
    }
    const auto language = request.language.trim().toLowerCase();
    if (language.isEmpty() || language == "auto" || whisper_lang_id(language.toRawUTF8()) < 0) {
      error = juce::String::fromUTF8("Selecciona un idioma de guía compatible; no se usa detección automática."); return false;
    }
    const auto model = request.modelFile == juce::File{} ? defaultWhisperGuideModelFile() : request.modelFile;
    if (!model.existsAsFile() || model.getSize() <= 0 || model.getSize() > maximumModelBytes) {
      error = juce::String::fromUTF8("Falta un modelo Whisper local válido (máximo 256 MiB). Instala el modelo multilingüe de guía."); return false;
    }
    if (!request.audioFile.existsAsFile()) { error = juce::String::fromUTF8("La pista de guía local no existe."); return false; }
    // v1.9.5 can opt into an external ANE encoder via process environment even
    // with GPU disabled. Do not load that optional dynamic inference backend.
    if (juce::SystemStats::getEnvironmentVariable("ANEFORGE_ENCODER", {}).isNotEmpty()) {
      error = juce::String::fromUTF8("El reconocimiento de guía requiere el backend CPU integrado; desactiva el encoder ANE externo."); return false;
    }

    // One model/context per process at a time bounds memory and avoids shared
    // whisper contexts. Waiting workers remain cancelable without blocking UI.
    std::unique_lock<std::timed_mutex> exclusive(transcriptionMutex, std::defer_lock);
    while (!exclusive.try_lock_for(std::chrono::milliseconds(40))) if (cancelledNow()) return false;
    if (cancelledNow()) return false;
    const auto notify = [&](double value) { if (progress) progress(std::clamp(value, 0.0, 1.0)); };
    notify(0.0);
    GuideTranscript result;
    result.provider = providerId(); result.providerVersion = version(); result.language = language;
    const auto audioSize = request.audioFile.getSize();
    const auto audioModified = request.audioFile.getLastModificationTime();
    const auto modelSize = model.getSize();
    const auto modelModified = model.getLastModificationTime();
    if (!hashFile(request.audioFile, cancelled, result.audioFingerprint, error) ||
        !hashFile(model, cancelled, result.modelFingerprint, error)) return false;
    notify(0.05);
    if (cancelledNow()) return false;
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(request.audioFile));
    if (!reader || !std::isfinite(reader->sampleRate) || reader->sampleRate < 8000 ||
        reader->sampleRate > 384000 || reader->lengthInSamples <= 0 || reader->numChannels == 0 || reader->numChannels > 32) {
      error = juce::String::fromUTF8("Guía ilegible o fuera del límite local (8–384 kHz, hasta 32 canales)."); return false;
    }
    const double duration = double(reader->lengthInSamples) / reader->sampleRate;
    if (!std::isfinite(duration) || duration <= 0 || duration > maximumDuration ||
        request.offsetSeconds + duration > maximumDuration) {
      error = juce::String::fromUTF8("La guía excede el límite de análisis de 12 horas."); return false;
    }
    result.durationSeconds = duration + request.offsetSeconds;
    // No text, paths or model diagnostics from the embedded engine go to logs.
    whisper_log_set([](ggml_log_level, const char *, void *) {}, nullptr);
    auto contextParams = whisper_context_default_params();
    contextParams.use_gpu = false; contextParams.flash_attn = false;
    std::unique_ptr<whisper_context, decltype(&whisper_free)> context(
        whisper_init_from_file_with_params(model.getFullPathName().toRawUTF8(), contextParams), &whisper_free);
    if (cancelledNow()) return false;
    if (!context) { error = juce::String::fromUTF8("No se puede cargar el modelo Whisper local."); return false; }
    if (language != "en" && !whisper_is_multilingual(context.get())) {
      error = juce::String::fromUTF8("Este modelo solo reconoce inglés. Selecciona el modelo multilingüe."); return false;
    }
    notify(0.1);
    SourceResampler resampler(*reader);
    const auto totalSamples = static_cast<juce::int64>(std::ceil(duration * analysisRate));
    std::array<float, frameSamples> frame{};
    std::vector<float> recent, utterance;
    recent.reserve(preRollFrames * frameSamples); utterance.reserve(maximumUtteranceFrames * frameSamples);
    juce::int64 utteranceStart = 0, speechStart = 0, speechEnd = 0;
    int silenceFrames = 0, activeFrames = 0;

    const auto recognize = [&]() -> bool {
      if (utterance.empty() || activeFrames < minimumActiveFrames) return true;
      if (cancelledNow()) return false;
      if (result.segments.size() >= 65536) { error = juce::String::fromUTF8("La guía contiene demasiados anuncios."); return false; }
      const double windowStart = double(utteranceStart) / analysisRate;
      InferenceCallbacks callbacks{cancelled, progress, 0.1 + 0.89 * windowStart / duration,
          0.89 * double(utterance.size()) / analysisRate / duration};
      auto params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
      params.n_threads = int(std::max(1u, std::min(4u, std::thread::hardware_concurrency())));
      params.language = language.toRawUTF8(); params.translate = false;
      // Generic vocabulary only: never condition on a fixture's labels, order
      // or expected timestamps. Short isolated guide words need domain context.
      params.initial_prompt = language == "es"
          ? "Guía musical hablada: intro, verso, estrofa, pre-coro, coro, puente, instrumental, solo, interludio, salida, final; uno, dos, tres, cuatro."
          : language == "en"
              ? "Spoken music guide: intro, verse, pre-chorus, chorus, bridge, instrumental, solo, interlude, outro, ending; one, two, three, four."
              : nullptr;
      // no_context clears prior utterances; a nonzero text budget is still
      // necessary for whisper.cpp to actually consume the generic prompt.
      params.no_context = true; params.n_max_text_ctx = 128;
      params.carry_initial_prompt = true;
      params.single_segment = true; params.no_timestamps = false;
      params.print_special = params.print_progress = params.print_realtime = params.print_timestamps = false;
      params.token_timestamps = false;
      params.temperature = 0.0f; params.temperature_inc = 0.0f;
      params.greedy.best_of = 1; params.beam_search.beam_size = 5; params.max_tokens = 64;
      params.suppress_blank = true; params.suppress_nst = true;
      params.no_speech_thold = 0.6f;
      params.encoder_begin_callback = [](whisper_context *, whisper_state *, void *data) {
        return !isCancelled(static_cast<InferenceCallbacks *>(data)->cancelled);
      };
      params.encoder_begin_callback_user_data = &callbacks;
      params.abort_callback = [](void *data) { return isCancelled(static_cast<InferenceCallbacks *>(data)->cancelled); };
      params.abort_callback_user_data = &callbacks;
      params.progress_callback = [](whisper_context *, whisper_state *, int percent, void *data) {
        const auto &c = *static_cast<InferenceCallbacks *>(data);
        if (c.progress) c.progress(std::clamp(c.baseProgress + c.progressSpan * percent / 100.0, 0.0, 0.99));
      };
      params.progress_callback_user_data = &callbacks;
      float peak = 0.0f;
      for (auto sample : utterance) peak = std::max(peak, std::abs(sample));
      // Normalize only a detected utterance, never its surrounding silent
      // timeline. The gain cap avoids turning low-level residuals into speech.
      const float normalizationGain = peak > 0 ? std::min(20.0f, 0.95f / peak) : 1.0f;
      auto inferenceAudio = utterance;
      for (auto &sample : inferenceAudio) sample *= normalizationGain;
      const auto status = whisper_full(context.get(), params, inferenceAudio.data(), int(inferenceAudio.size()));
      if (cancelledNow()) return false;
      if (status != 0) { error = juce::String::fromUTF8("Whisper no pudo reconocer la guía local."); return false; }
      for (int i = 0; i < whisper_full_n_segments(context.get()); ++i) {
        if (whisper_full_get_segment_no_speech_prob(context.get(), i) >= 0.6f) continue;
        auto text = juce::String::fromUTF8(whisper_full_get_segment_text(context.get(), i)).trim();
        if (text.isEmpty() || !text.containsAnyOf(juce::String::fromUTF8("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZáéíóúÁÉÍÓÚñÑ0123456789"))) continue;
        // ASR timestamps are local 10 ms ticks. Restrict them to the observed
        // speech extent, retaining absolute file silence and the explicit offset.
        const double observedStart = double(speechStart) / analysisRate;
        const double observedEnd = double(speechEnd) / analysisRate;
        double start = std::max(observedStart, windowStart + double(whisper_full_get_segment_t0(context.get(), i)) * 0.01);
        double end = std::min(observedEnd, windowStart + double(whisper_full_get_segment_t1(context.get(), i)) * 0.01);
        if (!std::isfinite(start) || !std::isfinite(end) || end <= start) { start = observedStart; end = observedEnd; }
        if (end <= start) continue;
        result.segments.push_back({request.offsetSeconds + std::clamp(start, 0.0, duration),
            request.offsetSeconds + std::clamp(end, 0.0, duration), std::move(text)});
      }
      return true;
    };

    // Streaming energy gate only decides which local audio to transcribe. It
    // neither names music sections nor certifies speech: Whisper still may err.
    for (juce::int64 position = 0; position < totalSamples; position += frameSamples) {
      if (cancelledNow()) return false;
      const int count = int(std::min<juce::int64>(frameSamples, totalSamples - position));
      if (!resampler.read(position, count, frame.data(), cancelled, error)) { cancelledNow(); return false; }
      double energy = 0, peak = 0;
      for (int i = 0; i < count; ++i) { energy += double(frame[size_t(i)]) * frame[size_t(i)]; peak = std::max(peak, std::abs(double(frame[size_t(i)]))); }
      const bool active = std::sqrt(energy / count) >= 0.0025 && peak >= 0.006;
      if (utterance.empty()) {
        if (active) {
          utteranceStart = position - juce::int64(recent.size()); speechStart = position;
          utterance.assign(recent.begin(), recent.end()); utterance.insert(utterance.end(), frame.begin(), frame.begin() + count);
          speechEnd = position + count; activeFrames = 1; silenceFrames = 0; recent.clear();
        } else {
          recent.insert(recent.end(), frame.begin(), frame.begin() + count);
          const auto maximum = size_t(preRollFrames * frameSamples);
          if (recent.size() > maximum) recent.erase(recent.begin(), recent.begin() + std::ptrdiff_t(recent.size() - maximum));
        }
      } else {
        utterance.insert(utterance.end(), frame.begin(), frame.begin() + count);
        if (active) { ++activeFrames; silenceFrames = 0; speechEnd = position + count; } else ++silenceFrames;
        if (silenceFrames >= endingSilenceFrames || utterance.size() >= size_t(maximumUtteranceFrames * frameSamples)) {
          if (!recognize()) return false;
          // Carry only existing source PCM as pre-roll for the next window.
          const auto tail = std::min<size_t>(utterance.size(), preRollFrames * frameSamples);
          recent.assign(utterance.end() - std::ptrdiff_t(tail), utterance.end());
          utterance.clear(); activeFrames = silenceFrames = 0;
        }
      }
      if ((position / frameSamples) % 25 == 0) notify(0.1 + 0.89 * double(position + count) / double(totalSamples));
    }
    if (!recognize() || cancelledNow()) return false;
    if (request.audioFile.getSize() != audioSize || request.audioFile.getLastModificationTime() != audioModified ||
        model.getSize() != modelSize || model.getLastModificationTime() != modelModified) {
      error = juce::String::fromUTF8("El audio o el modelo cambió durante el reconocimiento; vuelve a analizar."); return false;
    }
    out = std::move(result);
    notify(1.0);
    return true;
  }
};
#else
class WhisperGuideTranscriber final : public IGuideTranscriber {
public:
  bool available() const override { return false; }
  juce::String unavailableReason() const override {
    return juce::String::fromUTF8("Esta compilación no incluye el reconocimiento local Whisper. El editor manual sigue disponible.");
  }
  juce::String providerId() const override { return "whisper.cpp"; }
  juce::String version() const override { return juce::String("disabled:") + algorithmVersion; }
  bool transcribe(const GuideTranscriptionRequest &, const CancellationCheck &cancelled,
                  const GuideTranscriptionProgress &, GuideTranscript &, juce::String &error) override {
    error = isCancelled(cancelled) ? "Cancelado" : unavailableReason(); return false;
  }
};
#endif
} // namespace

std::shared_ptr<IGuideTranscriber> createWhisperGuideTranscriber() { return std::make_shared<WhisperGuideTranscriber>(); }
juce::File defaultWhisperGuideModelFile() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("MTControls/models/ggml-base-q5_1.bin");
}
} // namespace playworship::analysis
