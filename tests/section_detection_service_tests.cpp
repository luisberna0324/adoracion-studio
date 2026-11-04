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
  std::function<bool(const GuideTranscriptionRequest &, const CancellationCheck &,
                     const GuideTranscriptionProgress &, GuideTranscript &, juce::String &)> inspect;
  bool available() const override { return enabled; }
  juce::String unavailableReason() const override { return "synthetic guide transcriber unavailable"; }
  juce::String providerId() const override { return "synthetic-guide-test-only"; }
  juce::String version() const override { return algorithmVersion; }
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
    expect(received.language == "es", "guide language reaches the transcriber");
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
  testGuideErrorsAndCancellation(fixtures);
  std::cout << "Section detection service tests passed (synthetic provider; no Apple accuracy claims).\n";
  return 0;
}
