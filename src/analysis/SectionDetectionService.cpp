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

constexpr int guideCacheSchema = 2;
constexpr juce::int64 maximumGuideCacheBytes = 8 * 1024 * 1024;

// A bounded-memory content fingerprint, with cancellation between reads. The
// domain marker fixes both the block size and the chunk-digest construction.
bool contentFingerprint(const juce::File &file, const CancellationCheck &cancelled,
                        juce::String &fingerprint, juce::String &error) {
  auto stream = file.createInputStream();
  if (!stream || stream->getTotalLength() <= 0) {
    error = "No se puede leer el archivo: " + file.getFileName(); return false;
  }
  juce::MemoryOutputStream chunks;
  chunks.writeString("content-chunk-sha256-1:1048576");
  juce::HeapBlock<char> block(1024 * 1024);
  while (!stream->isExhausted()) {
    if (cancelled()) return false;
    const auto count = stream->read(block.getData(), 1024 * 1024);
    if (count <= 0) { error = "Error leyendo el archivo: " + file.getFileName(); return false; }
    chunks.writeString(juce::SHA256(block.getData(), size_t(count)).toHexString());
  }
  chunks.writeInt64(stream->getPosition());
  fingerprint = juce::SHA256(chunks.getData(), chunks.getDataSize()).toHexString();
  return true;
}

bool validGuideTranscript(const GuideTranscript &transcript, double offset,
                          double duration, juce::String &error) {
  if (!std::isfinite(transcript.durationSeconds) ||
      std::abs(transcript.durationSeconds - duration) > 0.05 ||
      transcript.segments.size() > 65536) {
    error = juce::String::fromUTF8("La transcripción devolvió una duración o cantidad de frases inválida."); return false;
  }
  double previousEnd = offset;
  size_t textBytes = 0;
  const auto validLanguage = [](const juce::String &language) {
    return language.length() <= 32 && language.containsOnly("abcdefghijklmnopqrstuvwxyz-");
  };
  const auto validTemplateIdentity = [](const juce::String &identity) {
    if (identity.trim().isEmpty() || identity.length() > 128) return false;
    for (auto character : identity)
      if (character < 0x20 || (character >= 0x7f && character <= 0x9f)) return false;
    return true;
  };
  for (const auto &segment : transcript.segments) {
    textBytes += size_t(segment.text.getNumBytesAsUTF8());
    if (!std::isfinite(segment.startSeconds) || !std::isfinite(segment.endSeconds) ||
        segment.startSeconds < offset - 0.000001 || segment.startSeconds < previousEnd - 0.000001 ||
        segment.endSeconds <= segment.startSeconds || segment.endSeconds > duration + 0.000001 ||
        segment.text.trim().isEmpty() || segment.text.length() > 16384 || !validLanguage(segment.language) ||
        (segment.confidence && (!std::isfinite(*segment.confidence) || *segment.confidence < 0 || *segment.confidence > 1)) ||
        segment.alternatives.size() > 2 || segment.words.size() > 128 ||
        segment.acousticAnnouncements.size() > 8) {
      error = juce::String::fromUTF8("La transcripción contiene frases o tiempos inválidos."); return false;
    }
    for (const auto &alternative : segment.alternatives) {
      textBytes += size_t(alternative.text.getNumBytesAsUTF8());
      if (alternative.text.trim().isEmpty() || alternative.text.length() > 16384 ||
          alternative.language.isEmpty() || !validLanguage(alternative.language) ||
          !std::isfinite(alternative.avgLogProbability) || alternative.avgLogProbability > 0 ||
          !std::isfinite(alternative.noSpeechProbability) || alternative.noSpeechProbability < 0 ||
          alternative.noSpeechProbability > 1) {
        error = juce::String::fromUTF8("La transcripción contiene alternativas inválidas."); return false;
      }
    }
    double previousWordStart = -1.0;
    for (const auto &word : segment.words) {
      textBytes += size_t(word.text.getNumBytesAsUTF8());
      if (word.text.trim().isEmpty() || word.text.length() > 256 ||
          !std::isfinite(word.startSeconds) || !std::isfinite(word.endSeconds) ||
          word.startSeconds < segment.startSeconds - 0.000001 ||
          word.startSeconds <= previousWordStart || word.endSeconds <= word.startSeconds ||
          word.endSeconds > segment.endSeconds + 0.000001 ||
          word.startSeconds < offset - 0.000001 || word.endSeconds > duration + 0.000001 ||
          (word.confidence && (!std::isfinite(*word.confidence) ||
              *word.confidence < 0.0 || *word.confidence > 1.0))) {
        error = juce::String::fromUTF8("La transcripción contiene intervalos de palabras inválidos."); return false;
      }
      previousWordStart = word.startSeconds;
    }
    // The measured recording may start before the ASR parent's approximate
    // onset or end after its last timestamp. Keep that bounded context inside
    // real source audio; overlapping voices retain their independent ends.
    const double acousticBegin = std::max(offset, segment.startSeconds - 0.2);
    const double acousticEnd = std::min(duration, segment.endSeconds + 0.3);
    if ((segment.acousticAnnouncements.empty() && segment.acousticResidualRatio) ||
        (!segment.acousticAnnouncements.empty() &&
         (!segment.acousticResidualRatio || !std::isfinite(*segment.acousticResidualRatio) ||
          *segment.acousticResidualRatio < 0.0 || *segment.acousticResidualRatio > 1.0))) {
      error = juce::String::fromUTF8("La transcripción contiene una reconstrucción acústica inválida."); return false;
    }
    double previousAcousticStart = -1.0;
    for (const auto &announcement : segment.acousticAnnouncements) {
      textBytes += size_t(announcement.text.getNumBytesAsUTF8()) +
          size_t(announcement.language.getNumBytesAsUTF8()) +
          size_t(announcement.templateIdentity.getNumBytesAsUTF8());
      if (announcement.text.trim().isEmpty() || announcement.text.length() > 512 ||
          (announcement.language != "es" && announcement.language != "en") ||
          !validTemplateIdentity(announcement.templateIdentity) ||
          !std::isfinite(announcement.startSeconds) || !std::isfinite(announcement.endSeconds) ||
          announcement.startSeconds < acousticBegin - 0.000001 ||
          announcement.startSeconds <= previousAcousticStart ||
          announcement.endSeconds <= announcement.startSeconds ||
          announcement.endSeconds > acousticEnd + 0.000001 ||
          !std::isfinite(announcement.correlation) ||
          announcement.correlation < 0.0 || announcement.correlation > 1.0) {
        error = juce::String::fromUTF8("La transcripción contiene evidencia acústica inválida."); return false;
      }
      previousAcousticStart = announcement.startSeconds;
    }
    if (textBytes > 4 * 1024 * 1024) {
      error = juce::String::fromUTF8("La transcripción excede el límite de texto."); return false;
    }
    previousEnd = segment.endSeconds;
  }
  return true;
}

juce::var guideTranscriptToVar(const GuideTranscript &transcript, double offset) {
  auto value = juce::var(new juce::DynamicObject());
  auto &object = *value.getDynamicObject();
  object.setProperty("schemaVersion", guideCacheSchema);
  object.setProperty("provider", transcript.provider);
  object.setProperty("providerVersion", transcript.providerVersion);
  object.setProperty("modelFingerprint", transcript.modelFingerprint);
  object.setProperty("audioFingerprint", transcript.audioFingerprint);
  object.setProperty("language", transcript.language);
  object.setProperty("offsetSeconds", offset);
  object.setProperty("durationSeconds", transcript.durationSeconds);
  juce::Array<juce::var> segments;
  for (const auto &segment : transcript.segments) {
    auto item = juce::var(new juce::DynamicObject());
    item.getDynamicObject()->setProperty("startSeconds", segment.startSeconds);
    item.getDynamicObject()->setProperty("endSeconds", segment.endSeconds);
    item.getDynamicObject()->setProperty("text", segment.text);
    item.getDynamicObject()->setProperty("language", segment.language);
    if (segment.confidence) item.getDynamicObject()->setProperty("confidence", *segment.confidence);
    juce::Array<juce::var> alternatives;
    for (const auto &alternative : segment.alternatives) {
      auto candidate = juce::var(new juce::DynamicObject());
      candidate.getDynamicObject()->setProperty("text", alternative.text);
      candidate.getDynamicObject()->setProperty("language", alternative.language);
      candidate.getDynamicObject()->setProperty("avgLogProbability", alternative.avgLogProbability);
      candidate.getDynamicObject()->setProperty("noSpeechProbability", alternative.noSpeechProbability);
      alternatives.add(std::move(candidate));
    }
    item.getDynamicObject()->setProperty("alternatives", std::move(alternatives));
    if (!segment.words.empty()) {
      juce::Array<juce::var> words;
      for (const auto &word : segment.words) {
        auto entry = juce::var(new juce::DynamicObject());
        entry.getDynamicObject()->setProperty("text", word.text);
        entry.getDynamicObject()->setProperty("startSeconds", word.startSeconds);
        entry.getDynamicObject()->setProperty("endSeconds", word.endSeconds);
        if (word.confidence) entry.getDynamicObject()->setProperty("confidence", *word.confidence);
        words.add(std::move(entry));
      }
      item.getDynamicObject()->setProperty("words", std::move(words));
    }
    if (!segment.acousticAnnouncements.empty()) {
      juce::Array<juce::var> announcements;
      for (const auto &announcement : segment.acousticAnnouncements) {
        auto entry = juce::var(new juce::DynamicObject());
        entry.getDynamicObject()->setProperty("text", announcement.text);
        entry.getDynamicObject()->setProperty("language", announcement.language);
        entry.getDynamicObject()->setProperty("templateIdentity", announcement.templateIdentity);
        entry.getDynamicObject()->setProperty("startSeconds", announcement.startSeconds);
        entry.getDynamicObject()->setProperty("endSeconds", announcement.endSeconds);
        entry.getDynamicObject()->setProperty("correlation", announcement.correlation);
        announcements.add(std::move(entry));
      }
      item.getDynamicObject()->setProperty("acousticAnnouncements", std::move(announcements));
      if (segment.acousticResidualRatio)
        item.getDynamicObject()->setProperty("acousticResidualRatio", *segment.acousticResidualRatio);
    }
    segments.add(std::move(item));
  }
  object.setProperty("segments", std::move(segments));
  return value;
}

bool readGuideCache(const juce::File &file, const GuideTranscript &identity,
                    double offset, GuideTranscript &transcript) {
  if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() >= maximumGuideCacheBytes) return false;
  const auto value = juce::JSON::parse(file);
  const auto *object = value.getDynamicObject();
  const auto numeric = [](const juce::var &v) { return v.isInt() || v.isInt64() || v.isDouble(); };
  if (!object || !object->getProperty("schemaVersion").isInt() ||
      int(object->getProperty("schemaVersion")) != guideCacheSchema ||
      object->getProperty("provider").toString() != identity.provider ||
      object->getProperty("providerVersion").toString() != identity.providerVersion ||
      object->getProperty("modelFingerprint").toString() != identity.modelFingerprint ||
      object->getProperty("audioFingerprint").toString() != identity.audioFingerprint ||
      object->getProperty("language").toString() != identity.language ||
      !numeric(object->getProperty("offsetSeconds")) ||
      !std::isfinite(double(object->getProperty("offsetSeconds"))) ||
      std::abs(double(object->getProperty("offsetSeconds")) - offset) > 0.000001 ||
      !numeric(object->getProperty("durationSeconds")) ||
      !std::isfinite(double(object->getProperty("durationSeconds"))) ||
      std::abs(double(object->getProperty("durationSeconds")) - identity.durationSeconds) > 0.000001) return false;
  const auto *segments = object->getProperty("segments").getArray();
  if (!segments || segments->size() > 65536) return false;
  transcript = identity;
  for (const auto &item : *segments) {
    const auto *segment = item.getDynamicObject();
    if (!segment || !numeric(segment->getProperty("startSeconds")) ||
        !numeric(segment->getProperty("endSeconds")) || !segment->getProperty("text").isString()) return false;
    GuideTranscriptSegment decoded{double(segment->getProperty("startSeconds")),
                                   double(segment->getProperty("endSeconds")), segment->getProperty("text").toString()};
    if (!segment->getProperty("language").isString()) return false;
    decoded.language = segment->getProperty("language").toString();
    if (segment->hasProperty("confidence")) {
      if (!numeric(segment->getProperty("confidence"))) return false;
      decoded.confidence = double(segment->getProperty("confidence"));
    }
    const auto *alternatives = segment->getProperty("alternatives").getArray();
    if (!alternatives || alternatives->size() > 2) return false;
    for (const auto &candidate : *alternatives) {
      const auto *alternative = candidate.getDynamicObject();
      if (!alternative || !alternative->getProperty("text").isString() ||
          !alternative->getProperty("language").isString() ||
          !numeric(alternative->getProperty("avgLogProbability")) ||
          !numeric(alternative->getProperty("noSpeechProbability"))) return false;
      decoded.alternatives.push_back({alternative->getProperty("text").toString(),
          alternative->getProperty("language").toString(), double(alternative->getProperty("avgLogProbability")),
          double(alternative->getProperty("noSpeechProbability"))});
    }
    if (segment->hasProperty("words")) {
      const auto *words = segment->getProperty("words").getArray();
      if (!words || words->size() > 128) return false;
      for (const auto &entry : *words) {
        const auto *word = entry.getDynamicObject();
        if (!word || !word->getProperty("text").isString() ||
            !numeric(word->getProperty("startSeconds")) ||
            !numeric(word->getProperty("endSeconds"))) return false;
        GuideTranscriptWord decodedWord{word->getProperty("text").toString(),
            double(word->getProperty("startSeconds")), double(word->getProperty("endSeconds"))};
        if (word->hasProperty("confidence")) {
          if (!numeric(word->getProperty("confidence"))) return false;
          decodedWord.confidence = double(word->getProperty("confidence"));
        }
        decoded.words.push_back(std::move(decodedWord));
      }
    }
    if (segment->hasProperty("acousticAnnouncements")) {
      const auto *announcements = segment->getProperty("acousticAnnouncements").getArray();
      if (!announcements || announcements->size() > 8) return false;
      for (const auto &entry : *announcements) {
        const auto *announcement = entry.getDynamicObject();
        if (!announcement || !announcement->getProperty("text").isString() ||
            !announcement->getProperty("language").isString() ||
            !announcement->getProperty("templateIdentity").isString() ||
            !numeric(announcement->getProperty("startSeconds")) ||
            !numeric(announcement->getProperty("endSeconds")) ||
            !numeric(announcement->getProperty("correlation"))) return false;
        decoded.acousticAnnouncements.push_back({announcement->getProperty("text").toString(),
            announcement->getProperty("language").toString(),
            announcement->getProperty("templateIdentity").toString(),
            double(announcement->getProperty("startSeconds")),
            double(announcement->getProperty("endSeconds")),
            double(announcement->getProperty("correlation"))});
      }
    }
    if (segment->hasProperty("acousticResidualRatio")) {
      if (!numeric(segment->getProperty("acousticResidualRatio"))) return false;
      decoded.acousticResidualRatio = double(segment->getProperty("acousticResidualRatio"));
    }
    transcript.segments.push_back(std::move(decoded));
  }
  juce::String ignored;
  return validGuideTranscript(transcript, offset, identity.durationSeconds, ignored);
}

bool writeGuideCache(const juce::File &file, const GuideTranscript &transcript,
                     double offset, const CancellationCheck &cancelled) {
  if (cancelled() || !file.getParentDirectory().createDirectory()) return false;
  const auto json = juce::JSON::toString(guideTranscriptToVar(transcript, offset));
  if (json.getNumBytesAsUTF8() >= maximumGuideCacheBytes) return false;
  juce::TemporaryFile temporary(file);
  if (!temporary.getFile().replaceWithText(json) || cancelled() ||
      !temporary.overwriteTargetFileWithTemporary()) return false;
  auto files = file.getParentDirectory().findChildFiles(juce::File::findFiles, false, "*.json");
  std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.getLastModificationTime() < b.getLastModificationTime(); });
  while (files.size() > 64) { files.getReference(0).deleteFile(); files.remove(0); }
  return true;
}

void transcribeGuide(const std::shared_ptr<IGuideTranscriber> &provider,
                     const SectionDetectionRequest &request, DetectionOutcome &outcome,
                     const CancellationCheck &cancelled,
                     const std::function<void(DetectionStage)> &report,
                     const GuideTranscriptionProgress &numericProgress) {
  if (!provider || !provider->available()) {
    outcome.error = provider ? provider->unavailableReason() : juce::String::fromUTF8("Sin transcriptor local de guía"); return;
  }
  const AnalysisStem *guide = nullptr;
  for (const auto &stem : request.stems) if (stem.role == AnalysisStemRole::guide) {
    if (guide) { outcome.error = juce::String::fromUTF8("Selecciona una sola pista como guía."); return; }
    guide = &stem;
  }
  if (!guide) { outcome.error = juce::String::fromUTF8("Selecciona una pista como guía para transcribir."); return; }
  if (!std::isfinite(guide->offsetSeconds) || guide->offsetSeconds < 0) {
    outcome.error = juce::String::fromUTF8("Offset de guía inválido."); return;
  }
  const auto language = request.guideLanguage.trim().toLowerCase();
  if (language.isEmpty() || language.length() > 32 || !language.containsOnly("abcdefghijklmnopqrstuvwxyz-")) {
    outcome.error = juce::String::fromUTF8("Selecciona un idioma de transcripción válido."); return;
  }
  report(DetectionStage::preparingAudio);
  if (numericProgress) numericProgress(0);
  juce::AudioFormatManager formats; formats.registerBasicFormats();
  auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(guide->file));
  if (!reader || reader->sampleRate <= 0 || reader->sampleRate > 384000 ||
      reader->lengthInSamples <= 0 || reader->numChannels == 0) {
    outcome.error = juce::String::fromUTF8("No se puede leer el audio de la guía."); return;
  }
  const double duration = guide->offsetSeconds + double(reader->lengthInSamples) / reader->sampleRate;
  if (!std::isfinite(duration) || duration <= 0 || duration > 12 * 3600) {
    outcome.error = juce::String::fromUTF8("Duración de guía fuera del límite de análisis local (12 h)."); return;
  }
  reader.reset();
  const auto model = request.guideModelFile == juce::File{} ? defaultWhisperGuideModelFile() : request.guideModelFile;
  GuideTranscript identity;
  identity.provider = provider->providerId(); identity.providerVersion = provider->version();
  identity.language = language; identity.durationSeconds = duration;
  if (!contentFingerprint(guide->file, cancelled, identity.audioFingerprint, outcome.error) ||
      !contentFingerprint(model, cancelled, identity.modelFingerprint, outcome.error)) return;
  juce::MemoryOutputStream key;
  key.writeString("guide-transcript-cache-2"); key.writeString(identity.provider); key.writeString(identity.providerVersion);
  key.writeString(identity.audioFingerprint); key.writeString(identity.modelFingerprint); key.writeString(language);
  key.writeDouble(guide->offsetSeconds); key.writeDouble(duration);
  const auto filename = juce::SHA256(key.getData(), key.getDataSize()).toHexString() + ".json";
  const auto cacheFile = request.cacheDirectory == juce::File{} ? juce::File{} :
      request.cacheDirectory.getChildFile("guide-transcripts-v2").getChildFile(filename);
  GuideTranscript transcript;
  if (cancelled()) return;
  outcome.cacheHit = cacheFile != juce::File{} && readGuideCache(cacheFile, identity, guide->offsetSeconds, transcript);
  if (!outcome.cacheHit) {
    report(DetectionStage::transcribingGuide);
    double lastProgress = 0;
    const auto progress = [&](double fraction) {
      if (numericProgress && !cancelled() && std::isfinite(fraction)) {
        lastProgress = std::max(lastProgress, juce::jlimit(0.0, 1.0, fraction));
        numericProgress(lastProgress);
      }
    };
    const GuideTranscriptionRequest transcriptionRequest{guide->file, model, language,
        guide->offsetSeconds, identity.providerVersion};
    if (!provider->transcribe(transcriptionRequest, cancelled, progress, transcript, outcome.error)) {
      if (outcome.error.isEmpty() && !cancelled()) outcome.error = juce::String::fromUTF8("No se pudo transcribir la guía.");
      return;
    }
    if (cancelled()) return;
    if (!validGuideTranscript(transcript, guide->offsetSeconds, duration, outcome.error)) return;
    juce::String currentAudioFingerprint, currentModelFingerprint;
    if (!contentFingerprint(guide->file, cancelled, currentAudioFingerprint, outcome.error) ||
        !contentFingerprint(model, cancelled, currentModelFingerprint, outcome.error)) return;
    if (currentAudioFingerprint != identity.audioFingerprint || currentModelFingerprint != identity.modelFingerprint) {
      outcome.error = juce::String::fromUTF8("La guía o el modelo cambió durante la transcripción; vuelve a analizar."); return;
    }
    // Cache identities belong to this request, never unverified provider fields.
    transcript.provider = identity.provider; transcript.providerVersion = identity.providerVersion;
    transcript.audioFingerprint = identity.audioFingerprint; transcript.modelFingerprint = identity.modelFingerprint;
    transcript.language = identity.language; transcript.durationSeconds = duration;
  }
  if (cancelled()) return;
  SectionDetectionResult result;
  result.songId = request.songId; result.durationSeconds = duration;
  result.analysisProvider = identity.provider; result.analyzedAudioFingerprint = identity.audioFingerprint;
  for (const auto &segment : transcript.segments)
    if (const auto cue = normalizeGuideCue(segment.startSeconds, segment.text, segment.confidence)) result.guideCues.push_back(*cue);
  const auto providerUnchanged = [&] {
    if (provider->version() == identity.providerVersion) return true;
    outcome.error = juce::String::fromUTF8("El reconocedor local o su catálogo de voces cambió durante el análisis; vuelve a analizar.");
    outcome.cacheHit = false;
    return false;
  };
  // Revalidate both freshly inferred evidence and cache hits. The expected
  // version sent into the provider also protects its own initial capture from
  // an old -> new -> old pack change that these final checks cannot observe.
  if (!providerUnchanged()) return;
  const bool wroteCache = !outcome.cacheHit && cacheFile != juce::File{} &&
      writeGuideCache(cacheFile, transcript, guide->offsetSeconds, cancelled);
  if (cancelled() || !providerUnchanged()) {
    if (wroteCache) cacheFile.deleteFile();
    return;
  }
  // Spoken cue times are not musical starts. Alignment is deliberately absent.
  outcome.result = std::move(result);
  outcome.guideTranscript = std::move(transcript);
  if (numericProgress) numericProgress(1);
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
SectionDetectionService::SectionDetectionService(std::shared_ptr<IMusicStructureAnalyzer> p,
    std::shared_ptr<IGuideTranscriber> g) : provider(std::move(p)), guideProvider(std::move(g)) {}
bool SectionDetectionService::available() const { return provider && provider->available(); }
juce::String SectionDetectionService::unavailableReason() const { return provider ? provider->unavailableReason() : juce::String::fromUTF8("Sin proveedor de análisis"); }
bool SectionDetectionService::guideAvailable() const { return guideProvider && guideProvider->available(); }
juce::String SectionDetectionService::guideUnavailableReason() const { return guideProvider ? guideProvider->unavailableReason() : juce::String::fromUTF8("Sin transcriptor local de guía"); }

std::shared_ptr<SectionDetectionTask> SectionDetectionService::start(SectionDetectionRequest request,
    std::function<void(DetectionStage)> progress, std::function<void(DetectionOutcome)> completion,
    GuideTranscriptionProgress numericProgress) const {
  auto task = std::make_shared<SectionDetectionTask>();
  auto cancelledState = task->cancelled;
  std::thread([p = provider, g = guideProvider, request = std::move(request), progress = std::move(progress),
               completion = std::move(completion), numericProgress = std::move(numericProgress), cancelledState] {
    DetectionOutcome outcome;
    const auto cancelled = [cancelledState] { return cancelledState->load(); };
    const auto report = [&](DetectionStage stage) { if (progress) progress(stage); };
    const auto run = [&] {
      if (cancelled()) return;
      if (request.transcribeGuideOnly) {
        transcribeGuide(g, request, outcome, cancelled, report, numericProgress); return;
      }
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
    catch (...) { outcome.error = juce::String::fromUTF8("Error inesperado de análisis local."); }
    outcome.cancelled = cancelled();
    if (outcome.cancelled || outcome.error.isNotEmpty()) {
      outcome.guideTranscript.reset(); outcome.result.guideCues.clear();
    }
    report(outcome.cancelled ? DetectionStage::cancelled : outcome.error.isNotEmpty() ? DetectionStage::error : DetectionStage::review);
    if (completion) completion(std::move(outcome));
  }).detach();
  return task;
}
} // namespace playworship::analysis
