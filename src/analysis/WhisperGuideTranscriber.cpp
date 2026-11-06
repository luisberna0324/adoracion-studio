#include "GuideTranscription.h"
#include "GuideRecognitionSelection.h"
#include "GuideSpeechSegmentation.h"
#include "GuideAcousticTemplateMatcher.h"
#include "GuideRepetitionEvidence.h"
#include "GuideRepetitionMatcher.h"
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
#include "GuideCatalogueGrammar.h"

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
// Candidate selection uses the shared catalog as a lexical prior. Expanding
// that vocabulary invalidates earlier recognition even with the same prompt.
constexpr const char *algorithmVersion = "guide-energy-sinc16k-beam5-prompt128-bilingual-13-guide-repetitions";

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

// The second pass only rereads short source windows. Even at the largest
// supported input rate and channel count, resampling scratch space is bounded.
bool readRepetitionWindow(SourceResampler &source, juce::int64 start, int count,
                          std::vector<float> &pcm, const CancellationCheck &cancelled,
                          juce::String &error) {
  pcm.resize(size_t(count));
  for (int i = 0; i < count; i += 2048)
    if (!source.read(start + i, std::min(2048, count - i), pcm.data() + i, cancelled, error))
      return false;
  return true;
}

// Recover at most one remaining musical section. Silence at either edge of
// the matched sample is common; only actual voice can justify a second mark.
struct RepetitionVoiceRange {
  int begin = 0, end = 0, activeSamples = 0;
};

RepetitionVoiceRange remainingVoice(const std::vector<float> &pcm, int begin, int end) {
  RepetitionVoiceRange voice;
  for (int from = begin; from < end; from += frameSamples) {
    const int to = std::min(from + frameSamples, end);
    double energy = 0.0;
    float peak = 0.0f;
    for (int i = from; i < to; ++i) {
      const float sample = pcm[size_t(i)];
      peak = std::max(peak, std::abs(sample));
      energy += double(sample) * sample;
    }
    if (peak < .006f || energy < .0025 * .0025 * (to - from)) continue;
    if (voice.activeSamples == 0) voice.begin = from;
    voice.end = to;
    voice.activeSamples += to - from;
  }
  return voice;
}

template <typename Decode>
bool recoverGuideRepetitions(GuideTranscript &transcript, SourceResampler &resampler,
                             double offset, juce::int64 totalSamples, const Decode &decode,
                             const GuideTranscriptionProgress &notify,
                             const CancellationCheck &cancelled, juce::String &error) {
  // Bound memory and retry work even for a multi-hour guide. Collect evidence
  // from the first pass only; recovered children can never seed another match.
  constexpr size_t maxTargets = 64, maxReferences = 64;
  constexpr int contextSamples = 3200, edgeTolerance = 1280; // 200 ms and 80 ms
  const auto sampleAt = [&](double absoluteTime) {
    return std::clamp<juce::int64>(std::llround((absoluteTime - offset) * analysisRate), 0, totalSamples);
  };
  std::vector<size_t> targets, referenceSegments;
  for (size_t i = 0; i < transcript.segments.size() && targets.size() < maxTargets; ++i) {
    if (isCancelled(cancelled)) return false;
    const auto &segment = transcript.segments[i];
    const double length = segment.endSeconds - segment.startSeconds;
    if (length >= .24 && length <= 5.0 && guideRepetitionEligible(segment)) targets.push_back(i);
  }
  if (targets.empty()) return true;

  std::vector<GuideRepetitionReference> references;
  for (size_t i = 0; i < transcript.segments.size(); ++i) {
    if (isCancelled(cancelled)) return false;
    const auto &segment = transcript.segments[i];
    const double length = segment.endSeconds - segment.startSeconds;
    if (length < .24 || length > 3.0 || !isGuideRepetitionSeed(segment)) continue;
    const auto cue = normalizeGuideUtilityCue(0, segment.text);
    if (!cue) continue;
    const auto copies = std::count_if(references.begin(), references.end(),
        [&](const auto &reference) { return reference.key == cue->name; });
    if (copies >= 3) continue;
    // If another eligible reference would exceed the budget, partial
    // coverage cannot establish uniqueness against all competing meanings.
    if (references.size() >= maxReferences) return true;
    GuideRepetitionReference reference;
    reference.key = cue->name;
    const auto begin = sampleAt(segment.startSeconds), end = sampleAt(segment.endSeconds);
    if (end <= begin) continue;
    if (!readRepetitionWindow(resampler, begin, int(end - begin), reference.samples, cancelled, error)) return false;
    references.push_back(std::move(reference));
    referenceSegments.push_back(i);
  }
  if (references.empty()) return true;

  const double progressSpan = .069 / double(targets.size());
  for (size_t i = 0; i < targets.size(); ++i) {
    if (isCancelled(cancelled)) return false;
    const double progressBase = .92 + double(i) * progressSpan;
    notify(progressBase);
    auto &parent = transcript.segments[targets[i]];
    const auto start = sampleAt(parent.startSeconds), end = sampleAt(parent.endSeconds);
    // Extra source context permits up to a frame of differing activity bounds.
    const auto from = std::max<juce::int64>(0, start - contextSamples);
    const auto to = std::min<juce::int64>(totalSamples, end + contextSamples);
    std::vector<float> pcm;
    if (!readRepetitionWindow(resampler, from, int(to - from), pcm, cancelled, error)) return false;
    const auto match = matchGuideRepetition(pcm, references, cancelled);
    if (!match) continue;
    const int first = int(start - from), last = int(end - from);
    if (match->startSample < first - edgeTolerance || match->endSample > last + edgeTolerance) continue;
    const int begin = std::max(first, match->startSample);
    const int finish = std::min(last, match->endSample);
    if (begin >= finish) continue;
    const auto seedIndex = referenceSegments[match->referenceIndex];
    const auto &seed = transcript.segments[seedIndex];
    const auto sourceTime = [&](int sample) { return double(from + sample) / analysisRate; };
    GuideRepetitionAnnouncement utility{seed.text, seed.language,
        std::max(parent.startSeconds, offset + sourceTime(begin)),
        std::min(parent.endSeconds, offset + sourceTime(finish)), seedIndex, match->correlation};

    const auto left = remainingVoice(pcm, first, begin), right = remainingVoice(pcm, finish, last);
    // A partial physical match must never consume an unexplained second voice.
    // Tiny frame-edge discrepancies are allowed, but ambiguous remnants fail closed.
    constexpr int edgeNoise = 640, minSpeech = minimumActiveFrames * frameSamples;
    if (left.activeSamples > edgeNoise && right.activeSamples > edgeNoise) continue;
    const bool prefix = left.activeSamples > right.activeSamples;
    const auto &remaining = prefix ? left : right;
    std::vector<GuideRepetitionAnnouncement> parts;
    if (remaining.activeSamples > edgeNoise) {
      if (remaining.activeSamples < minSpeech) continue;
      const int sliceStart = prefix ? std::max(0, remaining.begin - contextSamples) : finish;
      const int sliceEnd = prefix ? begin : std::min(int(pcm.size()), remaining.end + contextSamples);
      std::vector<float> slice(pcm.begin() + sliceStart, pcm.begin() + sliceEnd);
      std::optional<GuideTranscriptSegment> child;
      if (!decode(slice, sourceTime(sliceStart), sourceTime(remaining.begin), sourceTime(remaining.end),
                  progressBase, progressSpan, child)) return false;
      if (!child || !isSupportedGuideRetryChild(*child) || !isExactGuideSectionDirective(child->text) ||
          !parentSupportsSection(parent, child->text)) continue;
      GuideRepetitionAnnouncement section{child->text, child->language, child->startSeconds,
                                          child->endSeconds, {}, {}};
      if (prefix) parts = {section, utility};
      else parts = {utility, section};
    } else {
      parts.push_back(utility);
    }
    parent.repetitionAnnouncements = std::move(parts);
    if (!validGuideRepetition(transcript, parent)) parent.repetitionAnnouncements.clear();
  }
  return !isCancelled(cancelled);
}

class WhisperGuideTranscriber final : public IGuideTranscriber {
public:
  bool available() const override { return true; }
  juce::String unavailableReason() const override { return {}; }
  juce::String providerId() const override { return "whisper.cpp"; }
  juce::String version() const override {
    const auto fingerprint = guideAcousticPackFingerprint(defaultGuideAcousticPackFile());
    return juce::String(whisper_version()) + ":" + algorithmVersion + ":pack-" +
        (fingerprint.isEmpty() ? juce::String("unavailable") : fingerprint);
  }

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
    const bool bilingual = language == "es-en";
    if (!bilingual && language != "es" && language != "en") {
      error = juce::String::fromUTF8("Selecciona Español + inglés, español o inglés para reconocer la guía local."); return false;
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
    double lastProgress = 0.0;
    const GuideTranscriptionProgress notify = [&](double value) {
      if (!std::isfinite(value) || isCancelled(cancelled)) return;
      lastProgress = std::max(lastProgress, std::clamp(value, 0.0, 1.0));
      if (progress) progress(lastProgress);
    };
    notify(0.0);
    const auto acousticPack = defaultGuideAcousticPackFile();
    const auto acousticPackIdentity = guideAcousticPackFingerprint(acousticPack, cancelled);
    if (cancelledNow()) return false;
    GuideTranscript result;
    result.provider = providerId();
    result.providerVersion = juce::String(whisper_version()) + ":" + algorithmVersion + ":pack-" +
        (acousticPackIdentity.isEmpty() ? juce::String("unavailable") : acousticPackIdentity);
    if (request.expectedProviderVersion.isNotEmpty() && request.expectedProviderVersion != result.providerVersion) {
      error = juce::String::fromUTF8("El pack de voces cambió antes de iniciar el reconocimiento; vuelve a analizar.");
      return false;
    }
    result.language = language;
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
    GuideAcousticTemplateBank acousticTemplates;
    bool acousticTemplatesAttempted = false;
    recent.reserve(preRollFrames * frameSamples); utterance.reserve(maximumUtteranceFrames * frameSamples);
    juce::int64 utteranceStart = 0, speechStart = 0, speechEnd = 0;
    int silenceFrames = 0, activeFrames = 0;
    GuideCatalogueGrammar catalogueGrammar;

    const auto decodeWindow = [&](const std::vector<float> &pcm,
        double windowStart, double observedStart, double observedEnd,
        double progressBase, double progressAmount,
        std::optional<GuideTranscriptSegment> &selected, bool wordTimings = false,
        const juce::String &timingLanguage = {}, bool catalogueConstrained = false) -> bool {
      selected.reset();
      if (cancelledNow()) return false;
      const std::vector<juce::String> languages = wordTimings && timingLanguage.isNotEmpty()
          ? std::vector<juce::String>{timingLanguage}
          : bilingual ? std::vector<juce::String>{"es", "en"} : std::vector<juce::String>{language};
      float peak = 0.0f;
      for (auto sample : pcm) peak = std::max(peak, std::abs(sample));
      // Normalize only a detected utterance, never its surrounding silent
      // timeline. Both language passes receive exactly the same PCM window.
      const float normalizationGain = peak > 0 ? std::min(20.0f, 0.95f / peak) : 1.0f;
      auto inferenceAudio = pcm;
      for (auto &sample : inferenceAudio) sample *= normalizationGain;
      std::vector<GuideRecognitionCandidate> candidates;
      for (size_t pass = 0; pass < languages.size(); ++pass) {
        if (cancelledNow()) return false;
        const auto &passLanguage = languages[pass];
        const double progressSpan = progressAmount / double(languages.size());
        InferenceCallbacks callbacks{cancelled, notify,
            progressBase + double(pass) * progressSpan, progressSpan};
        auto params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
        params.n_threads = int(std::max(1u, std::min(4u, std::thread::hardware_concurrency())));
        params.language = passLanguage.toRawUTF8(); params.translate = false;
        // Generic vocabulary only: no song labels, expected order, timestamps
        // or aliases for observed recognition mistakes condition the decoder.
        params.initial_prompt = passLanguage == "es"
            ? "Guía musical hablada: intro, verso, estrofa, pre-coro, coro, estribillo, post-coro, puente, instrumental, solo, interludio, tag, vamp, salida, final; uno, dos, tres, cuatro; baja intensidad, a capella, sostener, subir, parar."
            : "Spoken music guide: intro, verse, pre-chorus, chorus, refrain, post-chorus, bridge, instrumental, solo, interlude, tag, vamp, outro, ending; one, two, three, four; breakdown, a cappella, hold, build, stop.";
        // no_context clears prior utterances and the other language pass. A
        // nonzero text budget still lets Whisper consume this generic prompt.
        params.no_context = true; params.n_max_text_ctx = 128;
        params.carry_initial_prompt = true;
        params.single_segment = true; params.no_timestamps = false;
        params.print_special = params.print_progress = params.print_realtime = params.print_timestamps = false;
        params.token_timestamps = wordTimings;
        params.temperature = 0.0f; params.temperature_inc = 0.0f;
        params.greedy.best_of = 1; params.beam_search.beam_size = 5; params.max_tokens = 64;
        params.suppress_blank = true; params.suppress_nst = true;
        params.no_speech_thold = 0.6f;
        if (catalogueConstrained) {
          params.grammar_rules = catalogueGrammar.rulesData();
          params.n_grammar_rules = catalogueGrammar.size();
          params.i_start_rule = 0;
          params.grammar_penalty = 100.0f;
        }
        params.encoder_begin_callback = [](whisper_context *, whisper_state *, void *data) {
          return !isCancelled(static_cast<InferenceCallbacks *>(data)->cancelled);
        };
        params.encoder_begin_callback_user_data = &callbacks;
        params.abort_callback = [](void *data) { return isCancelled(static_cast<InferenceCallbacks *>(data)->cancelled); };
        params.abort_callback_user_data = &callbacks;
        params.progress_callback = [](whisper_context *, whisper_state *, int percent, void *data) {
          const auto &c = *static_cast<InferenceCallbacks *>(data);
          if (c.progress) c.progress(std::clamp(c.baseProgress + c.progressSpan * std::clamp(percent, 0, 100) / 100.0, 0.0, 0.99));
        };
        params.progress_callback_user_data = &callbacks;
        const auto status = whisper_full(context.get(), params, inferenceAudio.data(), int(inferenceAudio.size()));
        if (cancelledNow()) return false;
        if (status != 0) { error = juce::String::fromUTF8("Whisper no pudo reconocer la guía local."); return false; }

        GuideRecognitionCandidate candidate;
        candidate.segment.language = passLanguage;
        // Word bounds are uncertainty intervals from an additional ASR pass,
        // not measured attacks or instructions to split the original text.
        GuideWordTimingCollector wordEvidence(request.offsetSeconds + observedStart,
                                              request.offsetSeconds + observedEnd);
        const auto collectWordToken = [&](const juce::String &tokenText,
            const whisper_token_data &data, double logProbability) {
          if (!wordTimings) return;
          const double begin = data.t0 < 0 || data.t1 < data.t0
              ? std::numeric_limits<double>::quiet_NaN()
              : request.offsetSeconds + std::clamp(windowStart + double(data.t0) * 0.01,
                                                   observedStart, observedEnd);
          const double end = request.offsetSeconds + std::clamp(
              windowStart + double(data.t1) * 0.01, observedStart, observedEnd);
          wordEvidence.append(tokenText, begin, end, logProbability);
        };
        double tokenLogProbability = 0.0;
        double noSpeechProbability = 0.0;
        for (int i = 0; i < whisper_full_n_segments(context.get()); ++i) {
          const auto rawText = juce::String::fromUTF8(whisper_full_get_segment_text(context.get(), i));
          if (rawText.trim().isEmpty() || !rawText.containsAnyOf(juce::String::fromUTF8(
              "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZáéíóúÁÉÍÓÚñÑ0123456789"))) continue;
          // ASR uses local 10 ms ticks. Keep this pass's times within observed
          // source speech, including all preceding silence and explicit offset.
          double begin = std::max(observedStart, windowStart + double(whisper_full_get_segment_t0(context.get(), i)) * 0.01);
          double end = std::min(observedEnd, windowStart + double(whisper_full_get_segment_t1(context.get(), i)) * 0.01);
          if (!std::isfinite(begin) || !std::isfinite(end) || end <= begin) { begin = observedStart; end = observedEnd; }
          if (end <= begin) continue;
          begin = request.offsetSeconds + std::clamp(begin, 0.0, duration);
          end = request.offsetSeconds + std::clamp(end, 0.0, duration);
          if (candidate.segment.text.isEmpty()) {
            candidate.segment.startSeconds = begin;
            candidate.segment.endSeconds = end;
          } else {
            candidate.segment.startSeconds = std::min(candidate.segment.startSeconds, begin);
            candidate.segment.endSeconds = std::max(candidate.segment.endSeconds, end);
          }
          // single_segment normally produces one result per <=20 s window;
          // preserve literal decoder strings if a backend returns more than one.
          candidate.segment.text += rawText;
          const double segmentNoSpeech = whisper_full_get_segment_no_speech_prob(context.get(), i);
          noSpeechProbability = std::max(noSpeechProbability,
              std::isfinite(segmentNoSpeech) ? std::clamp(segmentNoSpeech, 0.0, 1.0) : 1.0);
          for (int token = 0; token < whisper_full_n_tokens(context.get(), i); ++token) {
            const auto data = whisper_full_get_token_data(context.get(), i, token);
            if (data.id < 0 || data.id >= whisper_token_eot(context.get())) continue;
            const auto tokenText = juce::String::fromUTF8(whisper_full_get_token_text(context.get(), i, token));
            if (tokenText.trim().isEmpty()) {
              if (wordTimings) collectWordToken(tokenText, data, 0.0);
              continue;
            }
            double logProbability = data.plog;
            if (!std::isfinite(logProbability) || logProbability > 0.0) {
              const double probability = data.p;
              if (!std::isfinite(probability) || probability < 0.0 || probability > 1.0) continue;
              logProbability = std::log(std::max(probability, std::exp(-100.0)));
            }
            tokenLogProbability += std::clamp(logProbability, -100.0, 0.0);
            ++candidate.textTokenCount;
            collectWordToken(tokenText, data, logProbability);
          }
        }
        candidate.segment.text = candidate.segment.text.trim();
        if (wordTimings) candidate.segment.words = wordEvidence.takeWords();
        if (!candidate.segment.text.isEmpty()) {
          candidate.avgLogProbability = candidate.textTokenCount > 0
              ? tokenLogProbability / candidate.textTokenCount : -100.0;
          candidate.noSpeechProbability = noSpeechProbability;
          candidates.push_back(std::move(candidate));
        }
        notify(std::min(0.99, callbacks.baseProgress + callbacks.progressSpan));
      }
      selected = selectGuideRecognitionCandidate(candidates);
      return true;
    };

    const auto locateKnownAnnouncement = [&](GuideTranscriptSegment &parent, double windowStart) -> bool {
      const bool compound = guideAcousticCompoundPhrases(parent.text).has_value();
      const bool atomic = !compound && guideAcousticAtomicCandidate(parent);
      if ((!compound && !atomic) || acousticPackIdentity.isEmpty()) return true;
      if (cancelledNow()) return false;
      if (!acousticTemplatesAttempted) {
        acousticTemplatesAttempted = true;
        juce::String ignored;
        acousticTemplates = loadGuideAcousticTemplates(acousticPack, formats, cancelled, ignored);
        if (cancelledNow()) return false;
      }
      if (acousticTemplates.fingerprint != acousticPackIdentity || acousticTemplates.templates.empty()) return true;
      const auto located = compound
          ? matchKnownGuideCompound(utterance, request.offsetSeconds + windowStart, parent, acousticTemplates, cancelled)
          : matchKnownGuideAtomic(utterance, request.offsetSeconds + windowStart, parent, acousticTemplates, cancelled);
      if (located.cancelled || cancelledNow()) { error = "Cancelado"; return false; }
      if (located.announcements.empty()) return true;
      auto candidate = parent;
      candidate.acousticAnnouncements = located.announcements;
      candidate.acousticResidualRatio = located.residualRatio;
      const bool inSource = std::all_of(candidate.acousticAnnouncements.begin(),
          candidate.acousticAnnouncements.end(), [&](const auto &announcement) {
            return announcement.startSeconds >= request.offsetSeconds &&
                announcement.endSeconds <= result.durationSeconds + 1e-7;
          });
      if (inSource && guideAcousticAnnouncementsMatchParent(candidate)) {
        parent.acousticAnnouncements = std::move(candidate.acousticAnnouncements);
        parent.acousticResidualRatio = candidate.acousticResidualRatio;
      }
      return true;
    };

    const auto recognize = [&]() -> bool {
      if (utterance.empty() || activeFrames < minimumActiveFrames) return true;
      if (cancelledNow()) return false;
      if (result.segments.size() >= 65536) { error = juce::String::fromUTF8("La guía contiene demasiados anuncios."); return false; }
      const double windowStart = double(utteranceStart) / analysisRate;
      const double progressBase = 0.1 + 0.81 * windowStart / duration;
      const double progressAmount = 0.81 * double(utterance.size()) / analysisRate / duration;
      std::optional<GuideTranscriptSegment> selected;
      // Reserve progress for bounded retries. A successful
      // ordinary directive never gets split merely because its words pause.
      if (!decodeWindow(utterance, windowStart, double(speechStart) / analysisRate,
          double(speechEnd) / analysisRate, progressBase, progressAmount * 0.4, selected)) return false;
      if (selected && !locateKnownAnnouncement(*selected, windowStart)) return false;
      if (selected && selected->acousticAnnouncements.empty() && guideAcousticAtomicCandidate(*selected)) {
        // A single bounded catalogue retry contributes separate hypotheses.
        // Its probabilities are grammar-conditioned, not independent physical
        // proof and never replace the original two unconstrained ASR passes.
        std::optional<GuideTranscriptSegment> constrained;
        if (!decodeWindow(utterance, windowStart, double(speechStart) / analysisRate,
            double(speechEnd) / analysisRate, progressBase + progressAmount * .4,
            progressAmount * .1, constrained, false, {}, true)) return false;
        if (constrained) for (const auto &candidate : constrained->alternatives) {
          if (selected->catalogueAlternatives.size() >= 2) break;
          if (isGuideCatalogueUtterance(candidate.text) &&
              std::isfinite(candidate.avgLogProbability) && candidate.avgLogProbability >= -1.2 &&
              candidate.avgLogProbability <= 0.0 && std::isfinite(candidate.noSpeechProbability) &&
              candidate.noSpeechProbability >= 0.0 && candidate.noSpeechProbability <= .4)
            selected->catalogueAlternatives.push_back(candidate);
        }
      }
      if (selected && !selected->acousticAnnouncements.empty()) {
        // Preserve ASR literally; known recordings supply independent source
        // onsets, including actual overlaps and physically verified omissions.
        result.segments.push_back(std::move(*selected));
      } else if (selected && !isSingleGuideDirective(*selected)) {
        const auto compound = guideCompoundDirectives(selected->text);
        const auto windows = guideSpeechRetryWindows(utterance);
        std::vector<GuideTranscriptSegment> children;
        bool supported = !windows.empty() &&
            (!compound || windows.size() == size_t(compound->phrases.size()));
        for (size_t i = 0; supported && i < windows.size(); ++i) {
          const auto &window = windows[i];
          const std::vector<float> pcm(utterance.begin() + std::ptrdiff_t(window.beginSample),
              utterance.begin() + std::ptrdiff_t(window.endSample));
          std::optional<GuideTranscriptSegment> child;
          const double childSpan = progressAmount * 0.3 / double(windows.size());
          if (!decodeWindow(pcm, windowStart + double(window.beginSample) / analysisRate,
              windowStart + double(window.speechBeginSample) / analysisRate,
              windowStart + double(window.speechEndSample) / analysisRate,
              progressBase + progressAmount * 0.4 + double(i) * childSpan, childSpan, child)) return false;
          supported = child && isSupportedGuideRetryChild(*child) &&
              (!compound || sameGuideDirectiveMeaning(compound->phrases[int(i)], child->text));
          if (supported) children.push_back(std::move(*child));
        }
        if (!supported && compound) {
          // Explicit independently meaningful parts may have short gaps, or
          // one part may contain a pause ("All In"). Only measured silence
          // supplies candidate boundaries. Re-decode every contiguous group;
          // the complete decomposition must be semantically unique.
          const auto regions = guideSpeechRetryWindowsAtGap(utterance, 6);
          const auto partitions = guideSpeechRetryPartitions(regions, size_t(compound->phrases.size()));
          constexpr size_t maximumDecodes = 16;
          const auto regrouped = guideUniqueSpeechRetry(partitions, compound->phrases,
              [&](const GuideSpeechWindow &window, size_t attempt,
                  std::optional<GuideTranscriptSegment> &child) {
                const std::vector<float> pcm(utterance.begin() + std::ptrdiff_t(window.beginSample),
                    utterance.begin() + std::ptrdiff_t(window.endSample));
                const double childSpan = progressAmount * 0.3 / double(maximumDecodes);
                return decodeWindow(pcm, windowStart + double(window.beginSample) / analysisRate,
                    windowStart + double(window.speechBeginSample) / analysisRate,
                    windowStart + double(window.speechEndSample) / analysisRate,
                    progressBase + progressAmount * 0.7 + double(attempt) * childSpan,
                    childSpan, child);
              }, maximumDecodes);
          if (!regrouped.completed) return false;
          if (!regrouped.children.empty()) {
            children = regrouped.children;
            supported = true;
          }
        }
        if (supported) {
          if (result.segments.size() + children.size() > 65536) {
            error = juce::String::fromUTF8("La guía contiene demasiados anuncios."); return false;
          }
          for (auto &child : children) result.segments.push_back(std::move(child));
        } else {
          // Touching words cannot be separated by the energy gate. Retain the
          // original parent and attach bounded word evidence only when the
          // additional pass recognises the same phrase in the same language.
          if (guideCompoundCount(selected->text) || compound) {
            std::optional<GuideTranscriptSegment> timed;
            if (!decodeWindow(utterance, windowStart, double(speechStart) / analysisRate,
                double(speechEnd) / analysisRate, progressBase + progressAmount * 0.7,
                progressAmount * 0.3, timed, true, selected->language)) return false;
            if (timed && timed->language == selected->language &&
                playworship::guide::normalizedWords(timed->text) ==
                    playworship::guide::normalizedWords(selected->text))
              selected->words = std::move(timed->words);
          }
          result.segments.push_back(std::move(*selected));
        }
      } else if (selected) result.segments.push_back(std::move(*selected));
      notify(std::min(0.99, progressBase + progressAmount));
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
      if ((position / frameSamples) % 25 == 0) {
        // Scanning an active window has not recognised it yet. Its two decode
        // passes own that part of the progress range, in language order.
        const auto processed = utterance.empty() ? position + count : utteranceStart;
        notify(0.1 + 0.81 * double(processed) / double(totalSamples));
      }
    }
    if (!recognize() || cancelledNow()) return false;
    notify(.92);
    if (!recoverGuideRepetitions(result, resampler, request.offsetSeconds, totalSamples,
                                 decodeWindow, notify, cancelled, error)) {
      cancelledNow();
      return false;
    }
    notify(.99);
    if (request.audioFile.getSize() != audioSize || request.audioFile.getLastModificationTime() != audioModified ||
        model.getSize() != modelSize || model.getLastModificationTime() != modelModified ||
        guideAcousticPackFingerprint(acousticPack, cancelled) != acousticPackIdentity) {
      if (cancelledNow()) return false;
      error = juce::String::fromUTF8("El audio, el modelo o el pack de voces cambió durante el reconocimiento; vuelve a analizar."); return false;
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
