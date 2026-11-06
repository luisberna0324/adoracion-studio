#include "../src/analysis/SectionDetectionService.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <limits>
#include <thread>

using namespace playworship::analysis;

namespace {
void expect(bool condition, const char *message) {
  if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
void near(double actual, double expected, double tolerance, const char *message) {
  if (std::abs(actual - expected) > tolerance) {
    std::cerr << "actual " << actual << ", expected " << expected << '\n';
    expect(false, message);
  }
}

struct TemporaryFixtures {
  juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getNonexistentChildFile("playworship-section-service-tests", "", false);
  TemporaryFixtures() { expect(directory.createDirectory().wasOk(), "create isolated fixture directory"); }
  ~TemporaryFixtures() { directory.deleteRecursively(); }

  juce::File wave(const juce::String &name, double rate, int frames,
                  const std::function<float(int)> &sample, bool floatingPoint = false) {
    const auto file = directory.getChildFile(name + ".wav");
    expect(file.deleteFile(), "remove old synthetic fixture");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(rate).withNumChannels(1).withBitsPerSample(floatingPoint ? 32 : 24)
        .withSampleFormat(floatingPoint ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                       : juce::AudioFormatWriterOptions::SampleFormat::integral));
    expect(writer != nullptr, "create synthetic wave writer");
    juce::AudioBuffer<float> buffer(1, frames);
    for (int i = 0; i < frames; ++i) buffer.setSample(0, i, sample(i));
    expect(writer->writeFromAudioSampleBuffer(buffer, 0, frames), "write synthetic audio");
    writer.reset();
    return file;
  }
};

class FakeAnalyzer final : public IMusicStructureAnalyzer {
public:
  std::atomic<int> calls{0};
  bool enabled = true;
  juce::String algorithmVersion = "synthetic-1";
  std::function<bool(const juce::File &, double, const CancellationCheck &,
                     MusicStructureAnalysis &, juce::String &)> inspect;
  bool available() const override { return enabled; }
  juce::String unavailableReason() const override { return "synthetic provider unavailable"; }
  juce::String providerId() const override { return "synthetic-test-only"; }
  juce::String version() const override { return algorithmVersion; }
  bool analyze(const juce::File &file, double duration, const CancellationCheck &cancelled,
               MusicStructureAnalysis &out, juce::String &error) override {
    ++calls;
    if (inspect && !inspect(file, duration, cancelled, out, error)) return false;
    out.provider = providerId(); out.version = version();
    out.sections = {{0, duration / 2}, {duration / 2, duration}};
    out.tempo.bars = {0, duration / 2};
    out.tempo.source = "synthetic-test-only";
    return true;
  }
};

class FakeGuideTranscriber final : public IGuideTranscriber {
public:
  std::atomic<int> calls{0};
  bool enabled = true;
  juce::String algorithmVersion = "guide-synthetic-1";
  std::function<juce::String()> resolveVersion;
  std::function<bool(const GuideTranscriptionRequest &, const CancellationCheck &,
                     const GuideTranscriptionProgress &, GuideTranscript &, juce::String &)> inspect;
  bool available() const override { return enabled; }
  juce::String unavailableReason() const override { return "synthetic guide transcriber unavailable"; }
  juce::String providerId() const override { return "synthetic-guide-test-only"; }
  juce::String version() const override { return resolveVersion ? resolveVersion() : algorithmVersion; }
  bool transcribe(const GuideTranscriptionRequest &request, const CancellationCheck &cancelled,
                  const GuideTranscriptionProgress &progress, GuideTranscript &out,
                  juce::String &error) override {
    ++calls;
    juce::WavAudioFormat wav;
    auto reader = std::unique_ptr<juce::AudioFormatReader>(wav.createReaderFor(request.audioFile.createInputStream().release(), true));
    expect(reader != nullptr, "synthetic guide provider receives selected source audio");
    out.durationSeconds = request.offsetSeconds + double(reader->lengthInSamples) / reader->sampleRate;
    out.segments = {{request.offsetSeconds + 0.1, request.offsetSeconds + 0.3, "Coro"},
                    {request.offsetSeconds + 0.5, request.offsetSeconds + 0.7, "uno dos tres"},
                    {request.offsetSeconds + 0.8, request.offsetSeconds + 1.0, "Verso dos"}};
    if (progress) { progress(0.2); progress(0.6); }
    return !inspect || inspect(request, cancelled, progress, out, error);
  }
};

struct RunState {
  std::mutex mutex;
  std::condition_variable ready;
  bool completed = false;
  int completionCount = 0;
  DetectionOutcome outcome;
  std::vector<DetectionStage> stages;
  std::vector<double> fractions;
  std::thread::id completionThread;
};
struct Run {
  std::shared_ptr<RunState> state = std::make_shared<RunState>();
  std::shared_ptr<SectionDetectionTask> task;
  Run(const SectionDetectionService &service, SectionDetectionRequest request) {
    const auto captured = state;
    task = service.start(std::move(request), [captured](DetectionStage stage) {
      std::lock_guard<std::mutex> lock(captured->mutex);
      captured->stages.push_back(stage);
    }, [captured](DetectionOutcome outcome) {
      std::lock_guard<std::mutex> lock(captured->mutex);
      captured->completionThread = std::this_thread::get_id();
      captured->outcome = std::move(outcome);
      ++captured->completionCount;
      captured->completed = true;
      captured->ready.notify_all();
    }, [captured](double fraction) {
      std::lock_guard<std::mutex> lock(captured->mutex);
      captured->fractions.push_back(fraction);
    });
  }
  DetectionOutcome wait() const {
    std::unique_lock<std::mutex> lock(state->mutex);
    expect(state->ready.wait_for(lock, std::chrono::seconds(15), [&] { return state->completed; }),
           "asynchronous analysis completes within timeout");
    expect(state->completionCount == 1, "completion is delivered exactly once");
    expect(state->completionThread != std::this_thread::get_id(), "completion runs off caller thread");
    return state->outcome;
  }
};

SectionDetectionRequest requestFor(const juce::File &file) {
  SectionDetectionRequest request;
  request.songId = "synthetic-song";
  request.stems = {{file, AnalysisStemRole::music, 0}};
  return request;
}

std::unique_ptr<juce::AudioFormatReader> readMix(const juce::File &file) {
  juce::WavAudioFormat wav;
  auto reader = std::unique_ptr<juce::AudioFormatReader>(
      wav.createReaderFor(file.createInputStream().release(), true));
  expect(reader != nullptr, "provider receives readable local wave file");
  expect(reader->numChannels == 1, "analysis mix is mono");
  return reader;
}

double sampleAt(juce::AudioFormatReader &reader, double seconds) {
  juce::AudioBuffer<float> buffer(1, 1);
  expect(reader.read(&buffer, 0, 1, juce::int64(std::llround(seconds * reader.sampleRate)), true, false),
         "read a precise analysis sample");
  return buffer.getSample(0, 0);
}

void testTimelineMix(TemporaryFixtures &fixtures) {
  const auto a = fixtures.wave("music-44100", 44100, 44100,
      [](int i) { return i < 4410 ? 0.0f : 0.4f; });
  const auto b = fixtures.wave("music-48000", 48000, 24000, [](int) { return 0.2f; });
  const auto guide = fixtures.wave("guide-32000", 32000, 64000, [](int) { return 0.9f; });
  const auto click = fixtures.wave("click-22050", 22050, 55125, [](int) { return -0.9f; });
  const auto excluded = fixtures.directory.getChildFile("excluded-missing.wav");
  auto provider = std::make_shared<FakeAnalyzer>();
  juce::File rendered;
  provider->inspect = [&](const auto &file, double duration, const auto &, auto &, auto &) {
    rendered = file;
    near(duration, 2.5, 1.0e-10, "guide and click retain the full timeline tail");
    auto reader = readMix(file);
    near(reader->sampleRate, 48000, 0, "highest musical sample rate is preserved explicitly");
    expect(reader->lengthInSamples == 120000, "source-time duration has exact output frame count");
    near(sampleAt(*reader, 0.1), 0, 0.00002, "silence before delayed stems is retained");
    near(sampleAt(*reader, 0.3), 0, 0.00002, "initial silence inside a stem is retained");
    near(sampleAt(*reader, 0.4), 0.19, 0.00002, "44.1 kHz stem respects its source offset");
    near(sampleAt(*reader, 0.6), 0.285, 0.00002, "48 kHz and 44.1 kHz stems align by absolute source time");
    near(sampleAt(*reader, 1.1), 0.19, 0.00002, "short stem does not shift the other stem");
    near(sampleAt(*reader, 1.3), 0, 0.00002, "guide and click audio are excluded from the mix");
    near(sampleAt(*reader, 2.4), 0, 0.00002, "silent guide/click tail is retained");
    return true;
  };
  SectionDetectionRequest request;
  request.songId = "offset-song";
  request.stems = {{a, AnalysisStemRole::music, 0.25}, {b, AnalysisStemRole::music, 0.5},
                   {guide, AnalysisStemRole::guide, 0.25}, {click, AnalysisStemRole::click, 0},
                   {excluded, AnalysisStemRole::excluded, 999}};
  const SectionDetectionService service(provider);
  Run run(service, request);
  const auto outcome = run.wait();
  expect(outcome.error.isEmpty() && !outcome.cancelled && !outcome.cacheHit, "full synthetic analysis succeeds");
  expect(outcome.result.songId == request.songId && outcome.result.suggestions.size() == 2,
         "analysis returns reviewable suggestions for the requested song");
  expect(!rendered.exists() && !rendered.getParentDirectory().exists(), "temporary decoded audio is removed before completion");
  expect(run.state->stages == std::vector<DetectionStage>{DetectionStage::preparingAudio,
      DetectionStage::analyzingStructure, DetectionStage::mergingSuggestions, DetectionStage::review},
      "worker reports real stages and never claims guide transcription");
}

void testCacheAndContentChanges(TemporaryFixtures &fixtures) {
  auto file = fixtures.wave("cache-source", 44100, 22050, [](int) { return 0.25f; });
  const auto originalSize = file.getSize();
  const auto originalTime = file.getLastModificationTime();
  auto provider = std::make_shared<FakeAnalyzer>();
  juce::File rendered;
  provider->inspect = [&](const auto &mix, double, const auto &, auto &, auto &) {
    rendered = mix;
    auto reader = readMix(mix);
    near(reader->sampleRate, 44100, 0, "a single sample rate is preserved without resampling");
    expect(reader->lengthInSamples == 22050, "a song without guide or click retains exact duration");
    return true;
  };
  SectionDetectionService service(provider);
  auto request = requestFor(file);
  request.cacheDirectory = fixtures.directory.getChildFile("metadata-cache");
  const auto first = Run(service, request).wait();
  expect(first.error.isEmpty() && !first.cacheHit && provider->calls == 1, "first run analyzes and writes metadata cache");
  expect(!rendered.exists(), "first run removes temporary mix");
  const auto second = Run(service, request).wait();
  expect(second.error.isEmpty() && second.cacheHit && provider->calls == 1, "second run reuses cached provider evidence");
  expect(first.result.analyzedAudioFingerprint == second.result.analyzedAudioFingerprint, "cache identity is stable");
  const auto cacheFiles = request.cacheDirectory.findChildFiles(juce::File::findFiles, true);
  expect(cacheFiles.size() == 1 && cacheFiles[0].hasFileExtension("json"), "only result metadata is retained in cache");
  file = fixtures.wave("cache-source", 44100, 22050, [](int) { return 0.5f; });
  expect(file.getSize() == originalSize && file.setLastModificationTime(originalTime), "mutate bytes with unchanged size and timestamp");
  const auto third = Run(service, request).wait();
  expect(third.error.isEmpty() && !third.cacheHit && provider->calls == 2, "changed audio bytes invalidate cache");
  expect(third.result.analyzedAudioFingerprint != first.result.analyzedAudioFingerprint, "fingerprint hashes actual content");
  provider->algorithmVersion = "synthetic-2";
  const auto fourth = Run(service, request).wait();
  expect(fourth.error.isEmpty() && !fourth.cacheHit && provider->calls == 3, "provider version invalidates cache");
}

void testFailuresAndCleanup(TemporaryFixtures &fixtures) {
  const auto music = fixtures.wave("failure-music", 48000, 4800, [](int) { return 0.2f; });
  auto provider = std::make_shared<FakeAnalyzer>();
  SectionDetectionService service(provider);
  auto noMusic = requestFor(music);
  noMusic.stems.front().role = AnalysisStemRole::guide;
  const auto guideOnly = Run(service, noMusic).wait();
  expect(guideOnly.error.isNotEmpty() && provider->calls == 0, "guide-only input cannot masquerade as music");
  const auto corrupted = fixtures.directory.getChildFile("corrupt.wav");
  expect(corrupted.replaceWithText("This is not audio"), "create corrupt fixture");
  const auto badAudio = Run(service, requestFor(corrupted)).wait();
  expect(badAudio.error.isNotEmpty() && provider->calls == 0, "corrupt audio fails before provider");
  auto invalidOffset = requestFor(music);
  invalidOffset.stems.front().offsetSeconds = -0.1;
  expect(Run(service, invalidOffset).wait().error.isNotEmpty(), "unsupported negative offset fails explicitly");
  juce::File rendered;
  provider->inspect = [&](const auto &file, double, const auto &, auto &, auto &error) {
    rendered = file; error = "synthetic analysis failure"; return false;
  };
  Run failed(service, requestFor(music));
  expect(failed.wait().error == "synthetic analysis failure", "provider failure reaches caller");
  expect(!rendered.getParentDirectory().exists(), "provider failure cleans temporary audio");
  expect(failed.state->stages.back() == DetectionStage::error, "provider failure reports error state");
  provider->enabled = false;
  expect(!service.available(), "unsupported provider is visibly unavailable");
  expect(Run(service, requestFor(music)).wait().error == provider->unavailableReason(),
         "unsupported platform returns an explanatory failure");
}

void testOverrangeAudio(TemporaryFixtures &fixtures) {
  const auto file = fixtures.wave("floating-overrange", 48000, 4800,
      [](int i) { return i < 2400 ? 4.0f : 1.0f; }, true);
  auto provider = std::make_shared<FakeAnalyzer>();
  provider->inspect = [&](const auto &mix, double, const auto &, auto &, auto &) {
    auto reader = readMix(mix);
    near(sampleAt(*reader, 0.025), 0.95, 0.00002, "overrange float audio is limited to a safe peak");
    near(sampleAt(*reader, 0.075), 0.2375, 0.00002, "peak normalization preserves dynamics without intermediate clipping");
    return true;
  };
  const auto outcome = Run(SectionDetectionService(provider), requestFor(file)).wait();
  expect(outcome.error.isEmpty(), "overrange float audio analyzes safely");
}

void testCancellation(TemporaryFixtures &fixtures) {
  const auto music = fixtures.wave("cancel-music", 48000, 4800, [](int) { return 0.3f; });
  auto provider = std::make_shared<FakeAnalyzer>();
  std::mutex gate;
  std::condition_variable entered;
  bool analyzing = false;
  juce::File rendered;
  provider->inspect = [&](const auto &file, double, const CancellationCheck &cancelled, auto &, auto &error) {
    {
      std::lock_guard<std::mutex> lock(gate);
      rendered = file; analyzing = true; entered.notify_one();
    }
    std::unique_lock<std::mutex> lock(gate);
    while (!cancelled()) entered.wait_for(lock, std::chrono::milliseconds(10));
    error = "cancelled";
    return false;
  };
  SectionDetectionService service(provider);
  Run run(service, requestFor(music));
  {
    std::unique_lock<std::mutex> lock(gate);
    expect(entered.wait_for(lock, std::chrono::seconds(5), [&] { return analyzing; }), "provider starts before cancellation");
  }
  const auto before = std::chrono::steady_clock::now();
  run.task->cancel();
  run.task->cancel();
  expect(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(50), "cancel does not join or block caller");
  const auto outcome = run.wait();
  expect(outcome.cancelled && run.task->isCancelled(), "cancelled result is distinct from an analysis error");
  expect(outcome.result.suggestions.empty(), "cancelled analysis returns no accepted suggestions");
  expect(run.state->stages.back() == DetectionStage::cancelled, "cancellation is visible");
  expect(!rendered.getParentDirectory().exists(), "cancellation cleans all temporary audio");
}

SectionDetectionRequest guideRequest(TemporaryFixtures &fixtures, const juce::String &prefix) {
  auto request = requestFor(fixtures.wave(prefix + "-guide", 16000, 32000, [](int) { return 0.2f; }));
  request.transcribeGuideOnly = true;
  request.stems.front().role = AnalysisStemRole::guide;
  request.guideModelFile = fixtures.directory.getChildFile(prefix + "-model.bin");
  expect(request.guideModelFile.replaceWithText("synthetic-test-model-a"), "write synthetic model identity");
  return request;
}

void testGuideIndependentFromStructure(TemporaryFixtures &fixtures) {
  auto structure = std::make_shared<FakeAnalyzer>(); structure->enabled = false;
  auto guide = std::make_shared<FakeGuideTranscriber>();
  const SectionDetectionService service(structure, guide);
  expect(!service.available() && service.guideAvailable(), "guide transcription is available without Apple structure");
  auto request = guideRequest(fixtures, "guide-independent");
  request.stems.front().offsetSeconds = 0.25;
  request.stems.push_back({fixtures.directory.getChildFile("missing-music.wav"), AnalysisStemRole::music, 0});
  request.stems.push_back({fixtures.directory.getChildFile("missing-click.wav"), AnalysisStemRole::click, 0});
  guide->inspect = [&](const auto &received, const auto &, const auto &progress, auto &, auto &) {
    expect(received.audioFile == request.stems.front().file && received.modelFile == request.guideModelFile,
           "guide source and selected model are passed unchanged");
    expect(received.language == "es-en", "default bilingual mode reaches the transcriber");
    near(received.offsetSeconds, 0.25, 0, "source offset reaches the transcriber");
    progress(0.5); progress(std::numeric_limits<double>::quiet_NaN()); progress(1.5);
    return true;
  };
  Run run(service, request); const auto outcome = run.wait();
  expect(outcome.error.isEmpty() && outcome.guideTranscript && structure->calls == 0 && guide->calls == 1,
         "guide-only run neither uses nor requires structure or unselected music");
  near(outcome.result.durationSeconds, 2.25, 0.000001, "guide timeline duration includes explicit offset");
  expect(outcome.result.songId == request.songId && outcome.result.suggestions.empty(),
         "transcription never invents musical boundaries or section suggestions");
  expect(outcome.guideTranscript->segments.size() == 3 && outcome.result.guideCues.size() == 2,
         "literal transcript retains counting while musical normalizer excludes it");
  near(outcome.guideTranscript->segments[0].startSeconds, 0.35, 0.000001, "spoken timestamp remains source-based");
  near(outcome.result.guideCues[0].cueTimeSeconds, 0.35, 0.000001, "normalization does not quantize spoken timestamps");
  expect(outcome.result.guideCues[1].normalizedType == SectionType::verse &&
             outcome.result.guideCues[1].spokenOrdinal == 2,
         "timed spoken labels normalize Spanish types and ordinals");
  expect(run.state->stages == std::vector<DetectionStage>{DetectionStage::preparingAudio,
      DetectionStage::transcribingGuide, DetectionStage::review}, "guide run reports actual transcription, never structure/fusion");
  double previous = -1;
  for (double value : run.state->fractions) {
    expect(std::isfinite(value) && value >= previous && value <= 1, "numeric transcription progress is finite bounded and monotonic");
    previous = value;
  }
  expect(previous == 1, "successful transcription completes numeric progress");
  expect(outcome.guideTranscript->provider == guide->providerId() &&
             outcome.guideTranscript->audioFingerprint.isNotEmpty() &&
             outcome.guideTranscript->modelFingerprint.isNotEmpty(), "verified request identities accompany transcript");
}

void testGuideCacheIdentities(TemporaryFixtures &fixtures) {
  auto guide = std::make_shared<FakeGuideTranscriber>();
  const SectionDetectionService service(nullptr, guide);
  auto request = guideRequest(fixtures, "guide-cache");
  request.cacheDirectory = fixtures.directory.getChildFile("guide-evidence-cache");
  auto outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && !outcome.cacheHit && guide->calls == 1, "first guide run stores validated evidence");
  const auto audioFingerprint = outcome.guideTranscript->audioFingerprint;
  const auto modelFingerprint = outcome.guideTranscript->modelFingerprint;
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.cacheHit && guide->calls == 1 && outcome.result.guideCues.size() == 2,
         "cached transcript regenerates normalized spoken cues without inference");
  const auto originalSize = request.stems.front().file.getSize();
  const auto originalTime = request.stems.front().file.getLastModificationTime();
  fixtures.wave("guide-cache-guide", 16000, 32000, [](int) { return 0.4f; });
  expect(request.stems.front().file.getSize() == originalSize &&
             request.stems.front().file.setLastModificationTime(originalTime), "guide mutation preserves size and timestamp");
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && !outcome.cacheHit && guide->calls == 2 &&
             outcome.guideTranscript->audioFingerprint != audioFingerprint, "actual guide bytes invalidate ASR cache");
  expect(request.guideModelFile.replaceWithText("synthetic-test-model-b"), "change model bytes without changing path");
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && !outcome.cacheHit && guide->calls == 3 &&
             outcome.guideTranscript->modelFingerprint != modelFingerprint, "model content invalidates ASR cache");
  request.guideLanguage = "en"; outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && !outcome.cacheHit && guide->calls == 4 && outcome.guideTranscript->language == "en",
         "selected language invalidates ASR cache");
  request.stems.front().offsetSeconds = 0.5; outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && !outcome.cacheHit && guide->calls == 5, "explicit offset invalidates ASR cache");
  near(outcome.guideTranscript->segments.front().startSeconds, 0.6, 0.000001, "offset cache uses updated spoken times");
  guide->algorithmVersion = "guide-synthetic-2"; outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && !outcome.cacheHit && guide->calls == 6, "provider version invalidates ASR cache");
  const auto files = request.cacheDirectory.findChildFiles(juce::File::findFiles, true);
  expect(files.size() == 6, "independent ASR requests retain only six JSON evidence files");
  for (const auto &file : files) expect(file.hasFileExtension("json"), "ASR cache never stores decoded audio or models");
  // Corrupt every cache entry so that the exact current identity must re-run.
  for (const auto &file : files) expect(file.replaceWithText("{\"schemaVersion\":999}"), "replace cache with unsupported schema");
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && !outcome.cacheHit && guide->calls == 7, "unsupported cache schema is recomputed safely");
}

GuideTranscriptSegment bilingualSegmentFixture() {
  GuideTranscriptSegment segment{0.1, 0.3, "  ChOrUs, TWO!  "};
  segment.language = "en";
  segment.confidence = 0.8125;
  segment.alternatives = {{"  ChOrUs, TWO!  ", "en", -0.25, 0.03125},
                          {juce::String::fromUTF8("¡Coro, dos!"), "es", -0.625, 0.0625}};
  // Word ranges are ASR uncertainty evidence and may overlap; their starts
  // remain ordered, and the parent's original text/times are unchanged.
  segment.words = {{"ChOrUs,", 0.11, 0.22, 0.75}, {"TWO!", 0.21, 0.29, {}}};
  return segment;
}

GuideTranscriptSegment acousticSegmentFixture() {
  auto segment = bilingualSegmentFixture();
  segment.startSeconds = 0.5; segment.endSeconds = 1.2;
  segment.words = {{"ChOrUs,", 0.51, 0.8, 0.75}, {"TWO!", 0.79, 1.19, {}}};
  // Template activity can exceed the approximate ASR parent and voices can
  // overlap. Both ranges remain inside its 200/300 ms source context.
  segment.acousticAnnouncements = {{"ChOrUs,", "en", "pack:en:chorus", 0.3, 0.95, 0.0},
                                   {"TWO!", "en", "pack:en:two", 0.8, 1.5, 1.0}};
  segment.acousticResidualRatio = 0.03125;
  return segment;
}

void expectGuideMetadata(const GuideTranscriptSegment &actual,
                         const GuideTranscriptSegment &expected,
                         double sourceTimeTolerance = 0.0) {
  near(actual.startSeconds, expected.startSeconds, sourceTimeTolerance, "metadata retains the spoken source start");
  near(actual.endSeconds, expected.endSeconds, sourceTimeTolerance, "metadata retains the spoken source end");
  expect(actual.text == expected.text && actual.language == expected.language,
         "metadata retains the raw selected text, whitespace and per-announcement language");
  expect(actual.confidence.has_value() == expected.confidence.has_value(),
         "metadata retains the presence of heuristic token confidence");
  if (expected.confidence)
    near(*actual.confidence, *expected.confidence, 0, "metadata retains exact confidence");
  expect(actual.alternatives.size() == expected.alternatives.size(),
         "metadata retains both evaluated hypotheses including the selected one");
  for (size_t i = 0; i < expected.alternatives.size(); ++i) {
    const auto &candidate = actual.alternatives[i];
    const auto &original = expected.alternatives[i];
    expect(candidate.text == original.text && candidate.language == original.language,
           "cache roundtrip retains each literal alternative and its language in original order");
    near(candidate.avgLogProbability, original.avgLogProbability, 0,
         "cache roundtrip retains exact average token log probability");
    near(candidate.noSpeechProbability, original.noSpeechProbability, 0,
         "cache roundtrip retains exact no-speech probability");
  }
  expect(actual.catalogueAlternatives.size() == expected.catalogueAlternatives.size(),
         "conditioned catalogue hypotheses remain separate from the original ASR alternatives");
  for (size_t i = 0; i < expected.catalogueAlternatives.size(); ++i) {
    const auto &candidate = actual.catalogueAlternatives[i];
    const auto &original = expected.catalogueAlternatives[i];
    expect(candidate.text == original.text && candidate.language == original.language,
           "catalogue hypotheses retain literal text, language and their own order");
    near(candidate.avgLogProbability, original.avgLogProbability, 0,
         "conditioned token score is retained without replacing parent confidence");
    near(candidate.noSpeechProbability, original.noSpeechProbability, 0,
         "conditioned no-speech probability survives an exact cache roundtrip");
  }
  expect(actual.words.size() == expected.words.size(),
         "metadata retains optional ASR word intervals separately from parent evidence");
  for (size_t i = 0; i < expected.words.size(); ++i) {
    const auto &word = actual.words[i];
    const auto &original = expected.words[i];
    expect(word.text == original.text && word.confidence == original.confidence,
           "word text and the presence of its heuristic confidence survive cache roundtrip");
    near(word.startSeconds, original.startSeconds, sourceTimeTolerance, "word interval start survives cache roundtrip");
    near(word.endSeconds, original.endSeconds, sourceTimeTolerance, "word interval end survives cache roundtrip");
  }
  expect(actual.acousticResidualRatio == expected.acousticResidualRatio &&
             actual.acousticAnnouncements.size() == expected.acousticAnnouncements.size(),
         "acoustic reconstruction metadata remains distinct from ASR confidence and optional word evidence");
  for (size_t i = 0; i < expected.acousticAnnouncements.size(); ++i) {
    const auto &entry = actual.acousticAnnouncements[i];
    const auto &original = expected.acousticAnnouncements[i];
    expect(entry.text == original.text && entry.language == original.language &&
               entry.templateIdentity == original.templateIdentity,
           "acoustic text, language and template identity survive cache roundtrip without normalization");
    near(entry.startSeconds, original.startSeconds, sourceTimeTolerance, "measured acoustic onset survives cache roundtrip");
    near(entry.endSeconds, original.endSeconds, sourceTimeTolerance, "overlapping acoustic end is not clipped to its neighbour or ASR parent");
    near(entry.correlation, original.correlation, 0, "template correlation is retained without replacing ASR confidence");
  }
}

void testGuideBilingualMetadataRoundTrip(TemporaryFixtures &fixtures) {
  auto guide = std::make_shared<FakeGuideTranscriber>();
  const SectionDetectionService service(nullptr, guide);
  auto request = guideRequest(fixtures, "guide-bilingual-metadata");
  request.cacheDirectory = fixtures.directory.getChildFile("bilingual-metadata-cache");
  const auto expected = bilingualSegmentFixture();
  guide->inspect = [expected](const auto &received, const auto &, const auto &,
                              auto &transcript, auto &) {
    expect(received.language == "es-en", "bilingual metadata uses per-announcement dual-language mode");
    transcript.segments.front() = expected;
    return true;
  };
  auto outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.guideTranscript && !outcome.cacheHit && guide->calls == 1,
         "valid bilingual evidence is published and cached once");
  expect(outcome.guideTranscript->language == "es-en",
         "transcript language records the requested mode rather than the selected announcement language");
  expectGuideMetadata(outcome.guideTranscript->segments.front(), expected);
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.cacheHit && guide->calls == 1,
         "bilingual metadata is recovered from disk without another inference");
  expectGuideMetadata(outcome.guideTranscript->segments.front(), expected);
  expect(!outcome.guideTranscript->segments[1].confidence &&
             outcome.guideTranscript->segments[1].alternatives.empty(),
         "optional confidence and absent alternatives remain absent across cache roundtrip");
  const auto cache = request.cacheDirectory.getChildFile("guide-transcripts-v2");
  const auto files = cache.findChildFiles(juce::File::findFiles, false, "*.json");
  expect(files.size() == 1 && !request.cacheDirectory.getChildFile("guide-transcripts-v1").exists(),
         "new bilingual metadata uses its own v2 evidence directory");
  const auto originalJson = files[0].loadFileAsString();
  auto corrupted = juce::JSON::parse(originalJson);
  expect(int(corrupted.getProperty("schemaVersion", 0)) == 2,
         "bilingual metadata cache declares schema two");
  auto *segments = corrupted.getProperty("segments", {}).getArray();
  expect(segments && !segments->isEmpty() && segments->getReference(0).getDynamicObject(),
         "valid bilingual cache exposes a complete segment object");
  segments->getReference(0).getDynamicObject()->setProperty("confidence", 1.25);
  expect(files[0].replaceWithText(juce::JSON::toString(corrupted)),
         "create a cache with valid request identity but invalid segment confidence");
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && !outcome.cacheHit && guide->calls == 2,
         "invalid cached metadata is discarded and recomputed instead of reaching review");
  expectGuideMetadata(outcome.guideTranscript->segments.front(), expected);
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.cacheHit && guide->calls == 2,
         "recomputed bilingual evidence replaces the corrupt cache with a reusable result");

  auto oldSchemaTwo = juce::JSON::parse(originalJson);
  oldSchemaTwo["segments"].getArray()->getReference(0).getDynamicObject()->removeProperty("words");
  expect(files[0].replaceWithText(juce::JSON::toString(oldSchemaTwo)),
         "create an older compatible schema-two cache without optional word evidence");
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.cacheHit && guide->calls == 2 &&
             outcome.guideTranscript->segments.front().words.empty(),
         "old caches without word intervals remain readable without inventing word evidence");

  for (bool malformedContainer : {true, false}) {
    auto badWords = juce::JSON::parse(originalJson);
    auto *entry = badWords["segments"].getArray()->getReference(0).getDynamicObject();
    if (malformedContainer) entry->setProperty("words", "not-an-array");
    else entry->getProperty("words").getArray()->getReference(0).getDynamicObject()
        ->setProperty("endSeconds", 0.31);
    expect(files[0].replaceWithText(juce::JSON::toString(badWords)),
           "create cache metadata with malformed or out-of-parent word evidence");
    outcome = Run(service, request).wait();
    expect(outcome.error.isEmpty() && !outcome.cacheHit,
           "invalid word metadata is recomputed instead of reaching review");
    expectGuideMetadata(outcome.guideTranscript->segments.front(), expected);
  }
  expect(guide->calls == 4, "each invalid word cache triggers exactly one fresh provider call");
}

void testGuideRejectsInvalidBilingualMetadata(TemporaryFixtures &fixtures) {
  auto guide = std::make_shared<FakeGuideTranscriber>();
  const SectionDetectionService service(nullptr, guide);
  auto request = guideRequest(fixtures, "guide-invalid-bilingual-metadata");
  request.cacheDirectory = fixtures.directory.getChildFile("invalid-bilingual-metadata-cache");
  using Mutator = std::function<void(GuideTranscriptSegment &)>;
  const std::vector<Mutator> invalid = {
      [](auto &s) { s.confidence = std::numeric_limits<double>::quiet_NaN(); },
      [](auto &s) { s.confidence = -0.01; },
      [](auto &s) { s.confidence = 1.01; },
      [](auto &s) { s.alternatives.front().avgLogProbability = std::numeric_limits<double>::quiet_NaN(); },
      [](auto &s) { s.alternatives.front().avgLogProbability = 0.01; },
      [](auto &s) { s.alternatives.front().noSpeechProbability = std::numeric_limits<double>::quiet_NaN(); },
      [](auto &s) { s.alternatives.front().noSpeechProbability = -0.01; },
      [](auto &s) { s.alternatives.front().noSpeechProbability = 1.01; },
      [](auto &s) { s.alternatives.push_back(s.alternatives.front()); },
      [](auto &s) { s.words.front().startSeconds = std::numeric_limits<double>::quiet_NaN(); },
      [](auto &s) { s.words.front().startSeconds = 0.09; },
      [](auto &s) { s.words.front().endSeconds = 0.31; },
      [](auto &s) { s.words.front().endSeconds = s.words.front().startSeconds; },
      [](auto &s) { s.words.back().startSeconds = s.words.front().startSeconds; },
      [](auto &s) { s.words.front().text = " "; },
      [](auto &s) { s.words.front().text = juce::String::repeatedString("x", 257); },
      [](auto &s) { s.words.front().confidence = std::numeric_limits<double>::quiet_NaN(); },
      [](auto &s) { s.words.front().confidence = 1.01; },
      [](auto &s) { s.words.resize(129, s.words.front()); }};
  for (const auto &mutate : invalid) {
    guide->inspect = [mutate](const auto &, const auto &, const auto &, auto &transcript, auto &) {
      transcript.segments.front() = bilingualSegmentFixture();
      mutate(transcript.segments.front());
      return true;
    };
    const auto outcome = Run(service, request).wait();
    expect(outcome.error.isNotEmpty() && !outcome.guideTranscript &&
               outcome.result.guideCues.empty() && outcome.result.suggestions.empty(),
           "nonfinite or out-of-range bilingual evidence never publishes a partial transcript or cues");
    expect(!request.cacheDirectory.exists(), "invalid bilingual evidence never creates a cache entry");
  }
  expect(guide->calls == int(invalid.size()), "each invalid metadata case reaches the provider exactly once");
}

GuideTranscriptSegment catalogueSegmentFixture() {
  auto segment = bilingualSegmentFixture();
  segment.catalogueAlternatives = {{" Subir intensidad. ", "es", -.75, 0.0},
                                   {"Build!", "en", -100.0, 1.0}};
  return segment;
}

void testGuideCatalogueMetadataRoundTripAndTamper(TemporaryFixtures &fixtures) {
  auto guide = std::make_shared<FakeGuideTranscriber>();
  guide->algorithmVersion = "guide-test-11-closed-catalogue";
  const SectionDetectionService service(nullptr, guide);
  auto request = guideRequest(fixtures, "guide-catalogue-metadata");
  request.cacheDirectory = fixtures.directory.getChildFile("catalogue-metadata-cache");
  const auto expected = catalogueSegmentFixture();
  guide->inspect = [expected](const auto &, const auto &, const auto &, auto &transcript, auto &) {
    transcript.segments.front() = expected; return true;
  };
  auto outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.guideTranscript && !outcome.cacheHit && guide->calls == 1 &&
             outcome.guideTranscript->providerVersion == guide->algorithmVersion,
         "provider-eleven identity and optional conditioned metadata are validated before publication");
  expectGuideMetadata(outcome.guideTranscript->segments.front(), expected);
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.cacheHit && guide->calls == 1,
         "catalogue hypotheses are read from cache without a fresh ASR call");
  expectGuideMetadata(outcome.guideTranscript->segments.front(), expected);
  const auto files = request.cacheDirectory.getChildFile("guide-transcripts-v2")
      .findChildFiles(juce::File::findFiles, false, "*.json");
  expect(files.size() == 1, "optional catalogue metadata retains cache schema two");
  const auto originalJson = files[0].loadFileAsString();
  const auto original = juce::JSON::parse(originalJson);
  const auto &stored = original["segments"].getArray()->getReference(0);
  expect(stored["catalogueAlternatives"].isArray() && stored["catalogueAlternatives"].getArray()->size() == 2 &&
             stored["alternatives"].getArray()->size() == 2 && double(stored["confidence"]) == *expected.confidence,
         "the two original hypotheses and confidence are never overwritten by two conditioned hypotheses");
  expect(!original["segments"].getArray()->getReference(1).hasProperty("catalogueAlternatives"),
         "segments with no conditioned hypotheses omit the optional property");
  for (bool explicitEmpty : {false, true}) {
    auto older = juce::JSON::parse(originalJson);
    auto *segment = older["segments"].getArray()->getReference(0).getDynamicObject();
    if (explicitEmpty) segment->setProperty("catalogueAlternatives", juce::Array<juce::var>{});
    else segment->removeProperty("catalogueAlternatives");
    expect(files[0].replaceWithText(juce::JSON::toString(older)), "write an older compatible cache without catalogue hypotheses");
    outcome = Run(service, request).wait();
    auto expectedOlder = expected; expectedOlder.catalogueAlternatives.clear();
    expect(outcome.error.isEmpty() && outcome.cacheHit && guide->calls == 1,
           "absent or explicitly empty optional catalogue metadata remains backward compatible");
    expectGuideMetadata(outcome.guideTranscript->segments.front(), expectedOlder);
  }
  using CacheMutator = std::function<void(juce::DynamicObject &)>;
  std::vector<CacheMutator> invalid;
  const auto field = [&](const char *key, juce::var value) {
    invalid.push_back([key = juce::Identifier(key), value](auto &segment) {
      segment.getProperty("catalogueAlternatives").getArray()->getReference(0)
          .getDynamicObject()->setProperty(key, value);
    });
  };
  for (const auto *key : {"text", "language", "avgLogProbability", "noSpeechProbability"})
    invalid.push_back([key = juce::Identifier(key)](auto &segment) {
      segment.getProperty("catalogueAlternatives").getArray()->getReference(0)
          .getDynamicObject()->removeProperty(key);
    });
  for (const auto *key : {"avgLogProbability", "noSpeechProbability"}) {
    field(key, "0.1"); field(key, true); field(key, juce::var{});
  }
  for (const auto *key : {"text", "language"}) field(key, 4);
  field("text", " "); field("text", juce::String::repeatedString("x", 16385));
  field("language", "fr"); field("language", "ES"); field("language", "es-en"); field("language", "");
  field("avgLogProbability", .01); field("avgLogProbability", -100.01);
  field("noSpeechProbability", -.01); field("noSpeechProbability", 1.01);
  for (const auto &container : {juce::var("not-an-array"), juce::var{}, juce::var(true)})
    invalid.push_back([container](auto &segment) { segment.setProperty("catalogueAlternatives", container); });
  invalid.push_back([](auto &segment) {
    segment.getProperty("catalogueAlternatives").getArray()->getReference(0) = "not-an-object";
  });
  invalid.push_back([](auto &segment) {
    auto *items = segment.getProperty("catalogueAlternatives").getArray(); items->add(items->getReference(0));
  });
  for (const auto &mutate : invalid) {
    auto corrupted = juce::JSON::parse(originalJson);
    mutate(*corrupted["segments"].getArray()->getReference(0).getDynamicObject());
    expect(files[0].replaceWithText(juce::JSON::toString(corrupted)), "write malformed conditioned-hypothesis cache metadata");
    outcome = Run(service, request).wait();
    expect(outcome.error.isEmpty() && outcome.guideTranscript && !outcome.cacheHit,
           "malformed or weakly typed conditioned hypotheses are recomputed before reaching review");
    expectGuideMetadata(outcome.guideTranscript->segments.front(), expected);
  }
  const int expectedCalls = 1 + int(invalid.size());
  expect(guide->calls == expectedCalls, "each malformed catalogue cache triggers one replacement provider call");
  guide->algorithmVersion = "guide-test-12-next-catalogue";
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && !outcome.cacheHit && guide->calls == expectedCalls + 1,
         "a different provider version cannot reuse conditioned metadata under the prior catalogue identity");
}

void testGuideRejectsInvalidCatalogueMetadata(TemporaryFixtures &fixtures) {
  auto guide = std::make_shared<FakeGuideTranscriber>();
  const SectionDetectionService service(nullptr, guide);
  auto request = guideRequest(fixtures, "guide-invalid-catalogue-metadata");
  request.cacheDirectory = fixtures.directory.getChildFile("invalid-catalogue-metadata-cache");
  using Mutator = std::function<void(GuideTranscriptSegment &)>;
  const std::vector<Mutator> invalid = {
      [](auto &s) { s.catalogueAlternatives.front().text = " "; },
      [](auto &s) { s.catalogueAlternatives.front().text = juce::String::repeatedString("x", 16385); },
      [](auto &s) { s.catalogueAlternatives.front().language = "fr"; },
      [](auto &s) { s.catalogueAlternatives.front().language = "ES"; },
      [](auto &s) { s.catalogueAlternatives.front().language = "es-en"; },
      [](auto &s) { s.catalogueAlternatives.front().avgLogProbability = std::numeric_limits<double>::quiet_NaN(); },
      [](auto &s) { s.catalogueAlternatives.front().avgLogProbability = std::numeric_limits<double>::infinity(); },
      [](auto &s) { s.catalogueAlternatives.front().avgLogProbability = -100.01; },
      [](auto &s) { s.catalogueAlternatives.front().avgLogProbability = .01; },
      [](auto &s) { s.catalogueAlternatives.front().noSpeechProbability = std::numeric_limits<double>::quiet_NaN(); },
      [](auto &s) { s.catalogueAlternatives.front().noSpeechProbability = -.01; },
      [](auto &s) { s.catalogueAlternatives.front().noSpeechProbability = 1.01; },
      [](auto &s) { s.catalogueAlternatives.resize(3, s.catalogueAlternatives.front()); }};
  for (const auto &mutate : invalid) {
    guide->inspect = [mutate](const auto &, const auto &, const auto &, auto &transcript, auto &) {
      transcript.segments.front() = catalogueSegmentFixture(); mutate(transcript.segments.front()); return true;
    };
    const auto outcome = Run(service, request).wait();
    expect(outcome.error.isNotEmpty() && !outcome.guideTranscript && outcome.result.guideCues.empty() &&
               outcome.result.suggestions.empty() && !request.cacheDirectory.exists(),
           "invalid conditioned metadata cannot publish or cache any partial ASR, catalogue choices or sections");
  }
  guide->inspect = [](const auto &, const auto &, const auto &, auto &transcript, auto &) {
    transcript.segments.clear();
    for (int i = 0; i < 129; ++i) {
      auto segment = catalogueSegmentFixture();
      segment.startSeconds = .001 * i; segment.endSeconds = segment.startSeconds + .0005;
      segment.words.clear();
      for (auto &candidate : segment.catalogueAlternatives)
        candidate.text = juce::String::repeatedString("x", 16384);
      transcript.segments.push_back(std::move(segment));
    }
    return true;
  };
  const auto oversized = Run(service, request).wait();
  expect(oversized.error.isNotEmpty() && !oversized.guideTranscript && !request.cacheDirectory.exists(),
         "conditioned hypotheses count toward the existing four-megabyte aggregate text budget");
}

void testGuideAcousticMetadataRoundTrip(TemporaryFixtures &fixtures) {
  auto guide = std::make_shared<FakeGuideTranscriber>();
  const SectionDetectionService service(nullptr, guide);
  auto request = guideRequest(fixtures, "guide-acoustic-metadata");
  request.cacheDirectory = fixtures.directory.getChildFile("acoustic-metadata-cache");
  const auto expected = acousticSegmentFixture();
  guide->inspect = [expected](const auto &, const auto &, const auto &, auto &transcript, auto &) {
    transcript.segments = {expected}; return true;
  };
  auto outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.guideTranscript && !outcome.cacheHit && guide->calls == 1,
         "valid overlapping acoustic evidence is published and cached");
  expectGuideMetadata(outcome.guideTranscript->segments.front(), expected);
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.cacheHit && guide->calls == 1,
         "acoustic metadata roundtrip does not require another inference");
  expectGuideMetadata(outcome.guideTranscript->segments.front(), expected);
  const auto files = request.cacheDirectory.getChildFile("guide-transcripts-v2")
      .findChildFiles(juce::File::findFiles, false, "*.json");
  expect(files.size() == 1, "one schema-two file retains the acoustic evidence");
  const auto originalJson = files[0].loadFileAsString();
  auto original = juce::JSON::parse(originalJson);
  expect(int(original["schemaVersion"]) == 2,
         "optional acoustic evidence does not change the cache schema");
  const auto &stored = original["segments"].getArray()->getReference(0);
  expect(stored["acousticAnnouncements"].isArray() &&
             stored["acousticAnnouncements"].getArray()->size() == 2 &&
             stored.hasProperty("acousticResidualRatio"),
         "the cache stores the complete bounded acoustic array and reconstruction ratio");

  auto withoutAcoustics = expected;
  withoutAcoustics.acousticAnnouncements.clear(); withoutAcoustics.acousticResidualRatio.reset();
  for (bool explicitEmpty : {false, true}) {
    auto older = juce::JSON::parse(originalJson);
    auto *segment = older["segments"].getArray()->getReference(0).getDynamicObject();
    segment->removeProperty("acousticResidualRatio");
    if (explicitEmpty) segment->setProperty("acousticAnnouncements", juce::Array<juce::var>{});
    else segment->removeProperty("acousticAnnouncements");
    expect(files[0].replaceWithText(juce::JSON::toString(older)), "write compatible cache without acoustic evidence");
    outcome = Run(service, request).wait();
    expect(outcome.error.isEmpty() && outcome.cacheHit && guide->calls == 1,
           "absent or empty optional acoustic evidence remains compatible without inventing metadata");
    expectGuideMetadata(outcome.guideTranscript->segments.front(), withoutAcoustics);
  }

  using CacheMutator = std::function<void(juce::DynamicObject &)>;
  std::vector<CacheMutator> invalid;
  const auto badField = [&](const char *key, juce::var value) {
    invalid.push_back([key = juce::Identifier(key), value](auto &segment) {
      segment.getProperty("acousticAnnouncements").getArray()->getReference(0)
          .getDynamicObject()->setProperty(key, value);
    });
  };
  for (const auto *key : {"text", "language", "templateIdentity", "startSeconds", "endSeconds", "correlation"})
    invalid.push_back([key = juce::Identifier(key)](auto &segment) {
      segment.getProperty("acousticAnnouncements").getArray()->getReference(0)
          .getDynamicObject()->removeProperty(key);
    });
  for (const auto *key : {"startSeconds", "endSeconds", "correlation"}) {
    badField(key, "0.5"); badField(key, true); badField(key, juce::var{});
  }
  for (const auto *key : {"text", "language", "templateIdentity"}) badField(key, 12);
  badField("text", " "); badField("text", juce::String::repeatedString("x", 513));
  badField("language", "fr"); badField("language", "ES");
  badField("templateIdentity", " ");
  badField("templateIdentity", juce::String::repeatedString("x", 129));
  badField("templateIdentity", "pack:\tvoice");
  badField("templateIdentity", "pack:" + juce::String::charToString(0x85) + "voice");
  badField("startSeconds", 0.299); badField("endSeconds", 1.501);
  badField("endSeconds", 0.3); badField("correlation", -0.01); badField("correlation", 1.01);
  for (const auto &container : {juce::var("not-an-array"), juce::var{}, juce::var(true)})
    invalid.push_back([container](auto &segment) { segment.setProperty("acousticAnnouncements", container); });
  invalid.push_back([](auto &segment) {
    segment.getProperty("acousticAnnouncements").getArray()->getReference(0) = "not-an-object";
  });
  invalid.push_back([](auto &segment) {
    auto *items = segment.getProperty("acousticAnnouncements").getArray();
    const auto entry = items->getReference(0);
    while (items->size() < 9) items->add(entry);
  });
  invalid.push_back([](auto &segment) {
    segment.getProperty("acousticAnnouncements").getArray()->getReference(1)
        .getDynamicObject()->setProperty("startSeconds", 0.3);
  });
  invalid.push_back([](auto &segment) { segment.removeProperty("acousticResidualRatio"); });
  for (const auto &ratio : {juce::var("0.1"), juce::var(true), juce::var{}, juce::var(-0.01), juce::var(1.01)})
    invalid.push_back([ratio](auto &segment) { segment.setProperty("acousticResidualRatio", ratio); });
  invalid.push_back([](auto &segment) { segment.removeProperty("acousticAnnouncements"); });
  invalid.push_back([](auto &segment) { segment.setProperty("acousticAnnouncements", juce::Array<juce::var>{}); });
  for (const auto &mutate : invalid) {
    auto corrupted = juce::JSON::parse(originalJson);
    mutate(*corrupted["segments"].getArray()->getReference(0).getDynamicObject());
    expect(files[0].replaceWithText(juce::JSON::toString(corrupted)), "write malformed acoustic cache metadata");
    outcome = Run(service, request).wait();
    expect(outcome.error.isEmpty() && outcome.guideTranscript && !outcome.cacheHit,
           "malformed or weakly typed acoustic cache metadata is recomputed before review");
    expectGuideMetadata(outcome.guideTranscript->segments.front(), expected);
  }
  expect(guide->calls == 1 + int(invalid.size()),
         "each corrupt acoustic cache causes exactly one fresh provider call");

  auto shifted = expected;
  constexpr double sourceOffset = 0.4;
  shifted.startSeconds += sourceOffset; shifted.endSeconds += sourceOffset;
  for (auto &word : shifted.words) { word.startSeconds += sourceOffset; word.endSeconds += sourceOffset; }
  for (auto &entry : shifted.acousticAnnouncements) {
    entry.startSeconds += sourceOffset; entry.endSeconds += sourceOffset;
  }
  shifted.acousticResidualRatio = 0.0;
  request.stems.front().offsetSeconds = sourceOffset;
  guide->inspect = [shifted](const auto &, const auto &, const auto &, auto &transcript, auto &) {
    transcript.segments = {shifted}; return true;
  };
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.guideTranscript && !outcome.cacheHit,
         "a nonzero stem offset and a zero reconstruction ratio remain valid acoustic evidence");
  expectGuideMetadata(outcome.guideTranscript->segments.front(), shifted);
  const auto shiftedCalls = int(guide->calls);
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.cacheHit && guide->calls == shiftedCalls,
         "cached acoustic times already include the stem offset and are not shifted again");
  // Decimal JSON may round an arithmetic result such as .8 + .4 from
  // 1.2000000000000002 to 1.2. This is a few ULPs in this fixture, far below
  // one 16 kHz source sample; it does not relax any recognition tolerance.
  expectGuideMetadata(outcome.guideTranscript->segments.front(), shifted, 1e-14);
}

void testWithinGuideEvidenceCache(TemporaryFixtures &fixtures) {
  auto provider = std::make_shared<FakeGuideTranscriber>();
  const SectionDetectionService service(nullptr, provider);
  auto request = guideRequest(fixtures, "repetition");
  request.cacheDirectory = fixtures.directory.getChildFile("within-guide-cache");
  provider->inspect = [](const auto &, const auto &, const auto &, auto &transcript, auto &) {
    GuideTranscriptSegment seed{0.1, 0.6, "Baja la intensidad.", "es", .9};
    seed.alternatives = {{seed.text, "es", -.1, .01}};
    GuideTranscriptSegment pending{1.1, 1.6, "Paje la intensidad.", "es", .6};
    pending.repetitionAnnouncements = {{seed.text, "es", 1.1, 1.6, 0, .98}};
    transcript.segments = {seed, pending};
    return true;
  };
  auto outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && !outcome.cacheHit, "a pending mark can cite an ordinary first-pass instruction");
  outcome = Run(service, request).wait();
  expect(outcome.cacheHit && provider->calls == 1 && outcome.guideTranscript &&
         outcome.guideTranscript->segments[1].text == "Paje la intensidad." &&
         outcome.guideTranscript->segments[1].repetitionAnnouncements.size() == 1 &&
         outcome.guideTranscript->segments[1].repetitionAnnouncements.front().referenceSegment == 0 &&
         outcome.guideTranscript->segments[1].repetitionAnnouncements.front().correlation == .98,
         "cache replay retains the original speech and independent within-guide reference");
  const auto files = request.cacheDirectory.findChildFiles(juce::File::findFiles, true, "*.json");
  expect(files.size() == 1, "within-guide analysis persisted one transcript");
  auto corrupted = juce::JSON::parse(files[0].loadFileAsString());
  auto &parts = corrupted["segments"].getArray()->getReference(1).getDynamicObject()
      ->getProperty("repetitionAnnouncements");
  parts.getArray()->getReference(0).getDynamicObject()->setProperty("referenceSegment", 1);
  expect(files[0].replaceWithText(juce::JSON::toString(corrupted)), "write a cache citing its own pending utterance");
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && !outcome.cacheHit && provider->calls == 2,
         "a self-referential cache is recomputed from audio instead of becoming a mark");
}

void testGuideRejectsInvalidAcousticMetadata(TemporaryFixtures &fixtures) {
  auto guide = std::make_shared<FakeGuideTranscriber>();
  const SectionDetectionService service(nullptr, guide);
  auto request = guideRequest(fixtures, "guide-invalid-acoustic-metadata");
  request.cacheDirectory = fixtures.directory.getChildFile("invalid-acoustic-metadata-cache");
  using Mutator = std::function<void(GuideTranscriptSegment &)>;
  const std::vector<Mutator> invalid = {
      [](auto &s) { s.acousticAnnouncements.front().startSeconds = std::numeric_limits<double>::quiet_NaN(); },
      [](auto &s) { s.acousticAnnouncements.front().endSeconds = std::numeric_limits<double>::infinity(); },
      [](auto &s) { s.acousticAnnouncements.front().startSeconds = 0.299; },
      [](auto &s) { s.acousticAnnouncements.back().endSeconds = 1.501; },
      [](auto &s) { s.acousticAnnouncements.front().endSeconds = s.acousticAnnouncements.front().startSeconds; },
      [](auto &s) { s.acousticAnnouncements.back().startSeconds = s.acousticAnnouncements.front().startSeconds; },
      [](auto &s) { s.acousticAnnouncements.back().startSeconds = 0.2; },
      [](auto &s) { s.acousticAnnouncements.front().text = " "; },
      [](auto &s) { s.acousticAnnouncements.front().text = juce::String::repeatedString("x", 513); },
      [](auto &s) { s.acousticAnnouncements.front().language = "fr"; },
      [](auto &s) { s.acousticAnnouncements.front().language = ""; },
      [](auto &s) { s.acousticAnnouncements.front().templateIdentity = " "; },
      [](auto &s) { s.acousticAnnouncements.front().templateIdentity = juce::String::repeatedString("x", 129); },
      [](auto &s) { s.acousticAnnouncements.front().templateIdentity = "pack:\nvoice"; },
      [](auto &s) { s.acousticAnnouncements.front().correlation = std::numeric_limits<double>::quiet_NaN(); },
      [](auto &s) { s.acousticAnnouncements.front().correlation = -0.01; },
      [](auto &s) { s.acousticAnnouncements.front().correlation = 1.01; },
      [](auto &s) { s.acousticAnnouncements.resize(9, s.acousticAnnouncements.front()); },
      [](auto &s) { s.acousticResidualRatio.reset(); },
      [](auto &s) { s.acousticResidualRatio = std::numeric_limits<double>::quiet_NaN(); },
      [](auto &s) { s.acousticResidualRatio = std::numeric_limits<double>::infinity(); },
      [](auto &s) { s.acousticResidualRatio = -0.01; },
      [](auto &s) { s.acousticResidualRatio = 1.01; },
      [](auto &s) { s.acousticAnnouncements.clear(); },
      [](auto &s) {
        s.startSeconds = 0.1; s.endSeconds = 1.9; s.words.clear();
        s.acousticAnnouncements.front().startSeconds = -0.01;
      },
      [](auto &s) {
        s.startSeconds = 0.1; s.endSeconds = 1.9; s.words.clear();
        s.acousticAnnouncements.back().endSeconds = 2.01;
      }};
  for (const auto &mutate : invalid) {
    guide->inspect = [mutate](const auto &, const auto &, const auto &, auto &transcript, auto &) {
      transcript.segments = {acousticSegmentFixture()};
      mutate(transcript.segments.front()); return true;
    };
    const auto outcome = Run(service, request).wait();
    expect(outcome.error.isNotEmpty() && !outcome.guideTranscript &&
               outcome.result.guideCues.empty() && outcome.result.suggestions.empty(),
           "invalid acoustic evidence never publishes a partial transcript or derived cues");
    expect(!request.cacheDirectory.exists(), "invalid acoustic provider metadata is never cached");
  }
  expect(guide->calls == int(invalid.size()), "each invalid acoustic provider case runs exactly once");
  request.stems.front().offsetSeconds = 0.4;
  guide->inspect = [](const auto &, const auto &, const auto &, auto &transcript, auto &) {
    // This onset fits the parent's pre-roll but precedes the real stem start.
    transcript.segments = {acousticSegmentFixture()}; return true;
  };
  const auto beforeSource = Run(service, request).wait();
  expect(beforeSource.error.isNotEmpty() && !beforeSource.guideTranscript &&
             !request.cacheDirectory.exists(),
         "parent context cannot admit acoustic activity before the actual offset source begins");
}

void testGuideProviderVersionCapture(TemporaryFixtures &fixtures) {
  auto guide = std::make_shared<FakeGuideTranscriber>();
  guide->algorithmVersion = "guide-pack-old";
  const SectionDetectionService service(nullptr, guide);
  auto request = guideRequest(fixtures, "guide-provider-version");
  request.cacheDirectory = fixtures.directory.getChildFile("provider-version-cache");
  guide->inspect = [&](const auto &received, const auto &, const auto &, auto &, auto &) {
    expect(received.expectedProviderVersion == "guide-pack-old",
           "the transcriber receives the provider version captured before cache identity is calculated");
    guide->algorithmVersion = "guide-pack-new";
    return true;
  };
  auto outcome = Run(service, request).wait();
  expect(outcome.error.isNotEmpty() && !outcome.guideTranscript &&
             outcome.result.guideCues.empty() && !outcome.cacheHit && !request.cacheDirectory.exists(),
         "a provider version changed during inference cannot publish or cache evidence under the previous pack");

  guide->algorithmVersion = "guide-pack-old";
  guide->inspect = [](const auto &received, const auto &, const auto &, auto &transcript, auto &) {
    expect(received.expectedProviderVersion == "guide-pack-old", "fresh inference retains its captured expected version");
    transcript.provider = "forged-provider";
    transcript.providerVersion = "forged-version";
    transcript.audioFingerprint = "forged-audio";
    transcript.modelFingerprint = "forged-model";
    return true;
  };
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.guideTranscript && !outcome.cacheHit &&
             outcome.guideTranscript->provider == guide->providerId() &&
             outcome.guideTranscript->providerVersion == "guide-pack-old" &&
             outcome.guideTranscript->audioFingerprint != "forged-audio" &&
             outcome.guideTranscript->modelFingerprint != "forged-model",
         "unchanged authoritative provider identity still replaces untrusted response headers");
  const auto callsAfterFresh = int(guide->calls);
  const auto files = request.cacheDirectory.findChildFiles(juce::File::findFiles, true, "*.json");
  expect(files.size() == 1, "stable provider evidence creates one reusable cache entry");
  const auto cachedJson = files[0].loadFileAsString();

  std::atomic<int> reads{0};
  guide->resolveVersion = [&reads] {
    return ++reads == 1 ? juce::String("guide-pack-old") : juce::String("guide-pack-new");
  };
  outcome = Run(service, request).wait();
  expect(outcome.error.isNotEmpty() && !outcome.guideTranscript && !outcome.cacheHit &&
             outcome.result.guideCues.empty() && guide->calls == callsAfterFresh,
         "a provider changed while reading a matching cache is rejected without publishing or rerunning stale inference");
  expect(files[0].loadFileAsString() == cachedJson,
         "rejecting a changed provider does not overwrite the previous correctly identified cache entry");
  guide->resolveVersion = {};
  outcome = Run(service, request).wait();
  expect(outcome.error.isEmpty() && outcome.cacheHit && guide->calls == callsAfterFresh,
         "the untouched cache is reusable when its original provider version is restored");

  auto duringWrite = guideRequest(fixtures, "guide-provider-during-write");
  duringWrite.cacheDirectory = fixtures.directory.getChildFile("provider-during-write-cache");
  reads = 0;
  guide->resolveVersion = [&reads] {
    return ++reads <= 2 ? juce::String("guide-pack-old") : juce::String("guide-pack-new");
  };
  outcome = Run(service, duringWrite).wait();
  expect(outcome.error.isNotEmpty() && !outcome.guideTranscript && !outcome.cacheHit &&
             outcome.result.guideCues.empty() &&
             duringWrite.cacheDirectory.findChildFiles(juce::File::findFiles, true, "*.json").isEmpty(),
         "a provider changed after cache writing rolls back the newly written JSON before publication");

  guide->resolveVersion = {};
  auto aba = guideRequest(fixtures, "guide-provider-aba");
  aba.cacheDirectory = fixtures.directory.getChildFile("provider-aba-cache");
  guide->inspect = [&](const auto &received, const auto &, const auto &, auto &, auto &error) {
    guide->algorithmVersion = "guide-pack-new";
    const auto capturedAtProviderStart = guide->algorithmVersion;
    guide->algorithmVersion = "guide-pack-old";
    expect(received.expectedProviderVersion == "guide-pack-old",
           "the expected-version handshake remains fixed across an old-new-old pack transition");
    if (capturedAtProviderStart != received.expectedProviderVersion) {
      error = "Synthetic provider rejected its changed initial pack capture";
      return false;
    }
    return true;
  };
  outcome = Run(service, aba).wait();
  expect(outcome.error.isNotEmpty() && !outcome.guideTranscript &&
             outcome.result.guideCues.empty() && !aba.cacheDirectory.exists(),
         "a provider can reject an old-new-old transition using the supplied expected version before any cache publication");
}

void testGuideErrorsAndCancellation(TemporaryFixtures &fixtures) {
  auto guide = std::make_shared<FakeGuideTranscriber>();
  const SectionDetectionService service(nullptr, guide);
  auto request = guideRequest(fixtures, "guide-failure");
  request.cacheDirectory = fixtures.directory.getChildFile("failed-guide-cache");
  auto invalid = request; invalid.stems.clear();
  expect(Run(service, invalid).wait().error.isNotEmpty() && guide->calls == 0, "no guide fails before transcription");
  invalid = request; invalid.stems.push_back(request.stems.front());
  expect(Run(service, invalid).wait().error.isNotEmpty() && guide->calls == 0, "multiple guides require explicit selection");
  invalid = request; invalid.stems.front().offsetSeconds = -1;
  expect(Run(service, invalid).wait().error.isNotEmpty() && guide->calls == 0, "negative guide offset is rejected");
  invalid = request; invalid.guideModelFile = fixtures.directory.getChildFile("missing-model.bin");
  expect(Run(service, invalid).wait().error.isNotEmpty() && guide->calls == 0, "missing model fails before transcription");
  guide->inspect = [](const auto &, const auto &, const auto &, auto &, auto &error) { error = "synthetic ASR failure"; return false; };
  auto outcome = Run(service, request).wait();
  expect(outcome.error == "synthetic ASR failure" && !outcome.guideTranscript && outcome.result.guideCues.empty(),
         "provider failure exposes no partial transcript or cues");
  guide->inspect = [](const auto &, const auto &, const auto &, auto &transcript, auto &) {
    transcript.segments.front().startSeconds = std::numeric_limits<double>::quiet_NaN(); return true;
  };
  outcome = Run(service, request).wait();
  expect(outcome.error.isNotEmpty() && !outcome.guideTranscript, "nonfinite provider timestamp fails validation");
  guide->inspect = [](const auto &, const auto &, const auto &, auto &transcript, auto &) {
    transcript.segments.back().endSeconds = 20; return true;
  };
  expect(Run(service, request).wait().error.isNotEmpty(), "timestamp beyond source duration fails validation");
  guide->inspect = [](const auto &received, const auto &, const auto &, auto &, auto &) {
    expect(received.modelFile.replaceWithText("synthetic-test-model-changed"), "mutate selected model during inference"); return true;
  };
  expect(Run(service, request).wait().error.isNotEmpty(), "a model changed during inference cannot retain the old cache identity");
  expect(!request.cacheDirectory.exists(), "failed or invalid transcripts never create cache evidence");
  std::mutex mutex; std::condition_variable gate; bool entered = false;
  guide->inspect = [&](const auto &, const CancellationCheck &cancelled, const auto &, auto &, auto &error) {
    std::unique_lock<std::mutex> lock(mutex); entered = true; gate.notify_one();
    while (!cancelled()) gate.wait_for(lock, std::chrono::milliseconds(5));
    error = "cancelled"; return false;
  };
  Run run(service, request);
  { std::unique_lock<std::mutex> lock(mutex); expect(gate.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }), "ASR starts before cancellation"); }
  const auto before = std::chrono::steady_clock::now(); run.task->cancel();
  expect(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(50), "guide cancellation does not block caller");
  outcome = run.wait();
  expect(outcome.cancelled && !outcome.guideTranscript && outcome.result.guideCues.empty() && outcome.result.suggestions.empty(),
         "cancelled guide run never publishes partial transcript, cues, or musical sections");
  expect(!request.cacheDirectory.exists(), "cancelled transcript never creates cache evidence");
  guide->enabled = false;
  expect(!service.guideAvailable() && service.guideUnavailableReason() == guide->unavailableReason(), "guide capability explains unavailable local transcriber");
  expect(Run(service, request).wait().error == guide->unavailableReason(), "guide unavailability is independent from Apple");
}
} // namespace

int main() {
  TemporaryFixtures fixtures;
  testTimelineMix(fixtures);
  testCacheAndContentChanges(fixtures);
  testFailuresAndCleanup(fixtures);
  testOverrangeAudio(fixtures);
  testCancellation(fixtures);
  testGuideIndependentFromStructure(fixtures);
  testGuideCacheIdentities(fixtures);
  testGuideBilingualMetadataRoundTrip(fixtures);
  testGuideRejectsInvalidBilingualMetadata(fixtures);
  testGuideCatalogueMetadataRoundTripAndTamper(fixtures);
  testGuideRejectsInvalidCatalogueMetadata(fixtures);
  testGuideAcousticMetadataRoundTrip(fixtures);
  testWithinGuideEvidenceCache(fixtures);
  testGuideRejectsInvalidAcousticMetadata(fixtures);
  testGuideProviderVersionCapture(fixtures);
  testGuideErrorsAndCancellation(fixtures);
  std::cout << "Section detection service tests passed (synthetic provider; no Apple accuracy claims).\n";
  return 0;
}
