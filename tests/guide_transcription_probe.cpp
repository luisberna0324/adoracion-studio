#include "analysis/GuideTranscription.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>

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
                 "[--model ggml-base-q5_1.bin] [--language es] [--offset seconds] "
                 "[--cancel-after-ms milliseconds] [--cancel-at-progress fraction]\n"
                 "Exit codes: 0 recognized, 1 failed, 2 cancelled, 3 backend unavailable. "
                 "Transcript text is written only to the requested JSON file.\n";
  };
  if (arguments.isEmpty() || arguments.contains("--help")) { usage(); return arguments.isEmpty() ? 1 : 0; }
  const auto allowed = juce::StringArray{"--audio", "--output", "--model", "--language", "--offset", "--cancel-after-ms", "--cancel-at-progress"};
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
  double offset = 0, cancelAfterMs = 0, cancelAtProgress = 2;
  if (!numeric("--offset", 0, offset) || !numeric("--cancel-after-ms", 0, cancelAfterMs) ||
      !numeric("--cancel-at-progress", 2, cancelAtProgress) ||
      (!value("--cancel-at-progress").isEmpty() && cancelAtProgress > 1.0)) {
    std::cerr << "Invalid numeric option.\n"; return 1;
  }
  GuideTranscriptionRequest request;
  request.audioFile = juce::File(value("--audio"));
  request.modelFile = value("--model").isEmpty() ? defaultWhisperGuideModelFile() : juce::File(value("--model"));
  request.language = value("--language", "es"); request.offsetSeconds = offset;
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
  const auto progress = [&](double fraction) {
    maximumProgress = std::max(maximumProgress, fraction);
    if (fraction >= cancelAtProgress && cancelAtProgress < 1.0) cancelRequested.store(true);
  };
  GuideTranscript transcript;
  juce::String error;
  const auto succeeded = provider->transcribe(request, cancelled, progress, transcript, error);
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
  if (!output.getParentDirectory().createDirectory()) { std::cerr << "Cannot create output directory.\n"; return 1; }
  juce::TemporaryFile temporary(output);
  if (!temporary.getFile().replaceWithText(juce::JSON::toString(result)) || !temporary.overwriteTargetFileWithTemporary()) {
    std::cerr << "Cannot write result JSON.\n"; return 1;
  }
  std::cout << "success=" << (succeeded ? "true" : "false") << " cancelled=" << (wasCancelled ? "true" : "false")
            << " segments=" << transcript.segments.size() << " progress=" << maximumProgress << "\n";
  return succeeded ? 0 : wasCancelled ? 2 : provider->available() ? 1 : 3;
}
