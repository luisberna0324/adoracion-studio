#include "analysis/GuideTranscription.h"
#include "analysis/SectionDetectionService.h"
#include "analysis/GuideSectionAlignment.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <condition_variable>
#include <mutex>

using namespace playworship::analysis;

namespace {
juce::var transcriptJson(const GuideTranscript &transcript) {
  auto root = juce::var(new juce::DynamicObject());
  auto *object = root.getDynamicObject();
  object->setProperty("provider", transcript.provider);
  object->setProperty("providerVersion", transcript.providerVersion);
  object->setProperty("modelFingerprint", transcript.modelFingerprint);
  object->setProperty("audioFingerprint", transcript.audioFingerprint);
  object->setProperty("language", transcript.language);
  object->setProperty("durationSeconds", transcript.durationSeconds);
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
      alternatives.add(candidate);
    }
    item.getDynamicObject()->setProperty("alternatives", alternatives);
    if (!segment.words.empty()) {
      juce::Array<juce::var> words;
      for (const auto &word : segment.words) {
        auto entry = juce::var(new juce::DynamicObject());
        entry.getDynamicObject()->setProperty("text", word.text);
        entry.getDynamicObject()->setProperty("startSeconds", word.startSeconds);
        entry.getDynamicObject()->setProperty("endSeconds", word.endSeconds);
        if (word.confidence) entry.getDynamicObject()->setProperty("confidence", *word.confidence);
        words.add(entry);
      }
      item.getDynamicObject()->setProperty("words", words);
    }
    if (!segment.acousticAnnouncements.empty()) {
      juce::Array<juce::var> announcements;
      for (const auto &announcement : segment.acousticAnnouncements) {
        auto entry = juce::var(new juce::DynamicObject());
        auto *properties = entry.getDynamicObject();
        properties->setProperty("text", announcement.text);
        properties->setProperty("language", announcement.language);
        properties->setProperty("templateIdentity", announcement.templateIdentity);
        properties->setProperty("startSeconds", announcement.startSeconds);
        properties->setProperty("endSeconds", announcement.endSeconds);
        properties->setProperty("correlation", announcement.correlation);
        announcements.add(std::move(entry));
      }
      item.getDynamicObject()->setProperty("acousticAnnouncements", std::move(announcements));
    }
    if (segment.acousticResidualRatio)
      item.getDynamicObject()->setProperty("acousticResidualRatio", *segment.acousticResidualRatio);
    segments.add(item);
  }
  object->setProperty("segments", segments);
  return root;
}
}

int main(int argc, char **argv) {
  juce::StringArray arguments;
  for (int i = 1; i < argc; ++i) arguments.add(juce::String::fromUTF8(argv[i]));
  auto provider = createWhisperGuideTranscriber();
  if (arguments.size() == 1 && arguments[0] == "--availability") {
    std::cout << "backend=" << (provider->available() ? "available" : "unavailable")
              << " provider=" << provider->providerId() << " version=" << provider->version() << "\n";
    return provider->available() ? 0 : 3;
  }
  const auto usage = [] {
    std::cerr << "Usage: MTControlsGuideTranscriptionProbe --audio local-file --output result.json "
                 "[--model ggml-base-q5_1.bin] [--language es-en] [--offset seconds] "
                 "[--cache-directory local-dir] [--bpm source-bpm] [--meter 4/4] [--grid-origin seconds] "
                 "[--cancel-after-ms milliseconds] [--cancel-at-progress fraction]\n"
                 "Exit codes: 0 recognized, 1 failed, 2 cancelled, 3 backend unavailable. "
                 "Transcript text is written only to the requested JSON file.\n";
  };
  if (arguments.isEmpty() || arguments.contains("--help")) { usage(); return arguments.isEmpty() ? 1 : 0; }
  const auto allowed = juce::StringArray{"--audio", "--output", "--model", "--language", "--offset", "--cancel-after-ms", "--cancel-at-progress", "--cache-directory", "--bpm", "--meter", "--grid-origin"};
  for (int i = 0; i < arguments.size(); i += 2) {
    if (!allowed.contains(arguments[i]) || i + 1 >= arguments.size()) { usage(); return 1; }
  }
  const auto value = [&](const juce::String &key, const juce::String &fallback = juce::String{}) {
    const auto index = arguments.indexOf(key);
    return index >= 0 ? arguments[index + 1] : fallback;
  };
  if (value("--audio").isEmpty() || value("--output").isEmpty()) { usage(); return 1; }
  const auto numeric = [&](const char *key, double fallback, double &destination) {
    const auto input = value(key);
    if (input.isEmpty()) { destination = fallback; return true; }
    char *end = nullptr;
    const double number = std::strtod(input.toRawUTF8(), &end);
    if (!end || end == input.toRawUTF8() || *end != '\0' || !std::isfinite(number) || number < 0.0) return false;
    destination = number; return true;
  };
  double offset = 0, cancelAfterMs = 0, cancelAtProgress = 2, bpm = 0, gridOrigin = 0;
  if (!numeric("--offset", 0, offset) || !numeric("--cancel-after-ms", 0, cancelAfterMs) ||
      !numeric("--cancel-at-progress", 2, cancelAtProgress) || !numeric("--bpm", 0, bpm) ||
      !numeric("--grid-origin", 0, gridOrigin) ||
      (!value("--cancel-at-progress").isEmpty() && cancelAtProgress > 1.0)) {
    std::cerr << "Invalid numeric option.\n"; return 1;
  }
  GuideTranscriptionRequest request;
  request.audioFile = juce::File(value("--audio"));
  request.modelFile = value("--model").isEmpty() ? defaultWhisperGuideModelFile() : juce::File(value("--model"));
  request.language = value("--language", "es-en"); request.offsetSeconds = offset;
  const juce::File output(value("--output"));
  if (output == request.audioFile || output == request.modelFile) {
    std::cerr << "Output must differ from audio and model.\n"; return 1;
  }
  const auto started = std::chrono::steady_clock::now();
  std::atomic<bool> cancelRequested{false};
  const auto cancelled = [&] {
    if (cancelAfterMs > 0 && std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() >= cancelAfterMs)
      cancelRequested.store(true);
    return cancelRequested.load();
  };
  double maximumProgress = 0;
  int lastReportedPercent = -1;
  const auto progress = [&](double fraction) {
    maximumProgress = std::max(maximumProgress, fraction);
    const int percent = int(maximumProgress * 100);
    if (percent / 5 > lastReportedPercent / 5 || lastReportedPercent < 0) {
      std::cout << "progress=" << percent << "%\n" << std::flush;
      lastReportedPercent = percent;
    }
    if (fraction >= cancelAtProgress && cancelAtProgress < 1.0) cancelRequested.store(true);
  };
  GuideTranscript transcript;
  juce::String error;
  bool succeeded = false, cacheHit = false;
  if (value("--cache-directory").isNotEmpty()) {
    // Use the actual application service: identical fingerprints, validation,
    // cancellation and atomic cache writes; never manufacture cache evidence.
    SectionDetectionRequest serviceRequest;
    serviceRequest.songId = "guide-probe";
    serviceRequest.stems = {{request.audioFile, AnalysisStemRole::guide, request.offsetSeconds}};
    serviceRequest.transcribeGuideOnly = true;
    serviceRequest.guideModelFile = request.modelFile;
    serviceRequest.guideLanguage = request.language;
    serviceRequest.cacheDirectory = juce::File(value("--cache-directory"));
    SectionDetectionService service(nullptr, provider);
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false;
    auto task = service.start(serviceRequest, {}, [&](DetectionOutcome outcome) {
      std::lock_guard<std::mutex> lock(mutex);
      error = outcome.error; cacheHit = outcome.cacheHit;
      succeeded = outcome.guideTranscript.has_value() && error.isEmpty() && !outcome.cancelled;
      if (outcome.guideTranscript) transcript = std::move(*outcome.guideTranscript);
      done = true; ready.notify_one();
    }, progress);
    std::unique_lock<std::mutex> lock(mutex);
    while (!done) {
      if (cancelled()) task->cancel();
      ready.wait_for(lock, std::chrono::milliseconds(40));
    }
  } else succeeded = provider->transcribe(request, cancelled, progress, transcript, error);
  const bool wasCancelled = cancelled() && !succeeded;
  auto result = transcriptJson(transcript);
  auto *object = result.getDynamicObject();
  object->setProperty("backendProvider", provider->providerId());
  object->setProperty("backendVersion", provider->version());
  object->setProperty("success", succeeded);
  object->setProperty("cancelled", wasCancelled);
  object->setProperty("error", error);
  object->setProperty("elapsedSeconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
  object->setProperty("maximumProgress", maximumProgress);
  object->setProperty("offsetSeconds", offset);
  object->setProperty("cacheHit", cacheHit);
  if (succeeded && bpm > 0) {
    GuideAlignmentOptions options;
    options.originSeconds = value("--grid-origin").isNotEmpty() ? gridOrigin
        : transcript.segments.empty() ? 0.0 : transcript.segments.front().startSeconds;
    options.bpm = bpm; options.timeSignature = value("--meter", "4/4");
    const auto draft = alignGuideSections(transcript, options);
    object->setProperty("alignmentError", draft.error);
    object->setProperty("alignmentOriginSeconds", options.originSeconds);
    object->setProperty("alignmentBpm", options.bpm);
    object->setProperty("alignmentMeter", options.timeSignature);
    object->setProperty("alignmentAnticipationBars", 1);
    object->setProperty("alignmentVersion", sectionDetectionAlgorithmVersion);
    object->setProperty("alignmentScope", "guide duration; source tempo without project tempo map");
    juce::Array<juce::var> sections;
    for (const auto &suggestion : draft.suggestions) {
      auto section = juce::var(new juce::DynamicObject());
      section.getDynamicObject()->setProperty("name", suggestion.displayName);
      section.getDynamicObject()->setProperty("type", int(suggestion.type));
      section.getDynamicObject()->setProperty("announcementSeconds", suggestion.rawStartSeconds);
      section.getDynamicObject()->setProperty("startSeconds", suggestion.startSeconds);
      section.getDynamicObject()->setProperty("endSeconds", suggestion.endSeconds);
      if (suggestion.startBarIndex) section.getDynamicObject()->setProperty("startBar", *suggestion.startBarIndex + 1);
      section.getDynamicObject()->setProperty("reason", suggestion.reason);
      sections.add(section);
    }
    object->setProperty("suggestions", sections);
    juce::Array<juce::var> secondaryCues;
    for (const auto &cue : draft.secondaryCues) {
      auto item = juce::var(new juce::DynamicObject());
      item.getDynamicObject()->setProperty("id", cue.id);
      item.getDynamicObject()->setProperty("name", cue.name);
      item.getDynamicObject()->setProperty("kind", isGuideCountCue(cue.name) ? "count" : "instruction");
      item.getDynamicObject()->setProperty("rawText", cue.rawText);
      item.getDynamicObject()->setProperty("announcementSeconds", cue.rawTimeSeconds);
      item.getDynamicObject()->setProperty("timeSeconds", cue.timeSeconds);
      if (cue.asrConfidence) item.getDynamicObject()->setProperty("asrConfidence", *cue.asrConfidence);
      secondaryCues.add(item);
    }
    object->setProperty("secondaryCues", secondaryCues);
  }
  if (!output.getParentDirectory().createDirectory()) { std::cerr << "Cannot create output directory.\n"; return 1; }
  juce::TemporaryFile temporary(output);
  if (!temporary.getFile().replaceWithText(juce::JSON::toString(result)) || !temporary.overwriteTargetFileWithTemporary()) {
    std::cerr << "Cannot write result JSON.\n"; return 1;
  }
  std::cout << "success=" << (succeeded ? "true" : "false") << " cancelled=" << (wasCancelled ? "true" : "false")
            << " segments=" << transcript.segments.size() << " progress=" << maximumProgress << "\n";
  return succeeded ? 0 : wasCancelled ? 2 : provider->available() ? 1 : 3;
}
