#include "ui/panels/SectionDetectionPanel.h"
#include <juce_cryptography/juce_cryptography.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace playworship::analysis;

namespace {
void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, const char *message) {
  expect(std::isfinite(actual) && std::abs(actual - expected) < 0.00001, message);
}
void pumpUntil(const std::function<bool()> &done, const char *message) {
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 8000;
  while (!done() && juce::Time::getMillisecondCounterHiRes() < deadline)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
  expect(done(), message);
}
void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); }

struct TemporaryRoot {
  juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("pw-section-panel-tests", "", false);
  TemporaryRoot() { expect(file.createDirectory().wasOk(), "isolated test directory opens"); }
  ~TemporaryRoot() { file.deleteRecursively(); }
};

void writeAudio(const juce::File &file) {
  auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
  juce::WavAudioFormat wav;
  auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
      .withSampleRate(8000).withNumChannels(1).withBitsPerSample(16));
  expect(writer != nullptr, "synthetic local audio writer opens");
  juce::AudioBuffer<float> samples(1, 48000);
  for (int i = 0; i < samples.getNumSamples(); ++i)
    samples.setSample(0, i, 0.2f * std::sin(float(i) * 0.06f));
  expect(writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples()),
         "synthetic six-second source is written");
}

ImportedSongData makeSong(const juce::File &audio) {
  ImportedSongData song;
  song.id = "isolated-section-panel"; song.title = juce::String::fromUTF8("Canción de prueba local");
  song.bpm = 120; song.timeSignature = "3/4";
  ImportedStemData music;
  music.stemId = "music"; music.sourcePath = audio.getFullPathName();
  music.storedPath = music.sourcePath; music.displayName = "Guia nombre ambiguo";
  music.fileName = "Guia.mp3"; music.category = "Other";
  music.durationSec = 6; music.sampleRate = 8000; music.channels = 1;
  auto guide = music; guide.stemId = "guide"; guide.category = "Cues";
  guide.displayName = "Indicaciones de voz";
  auto click = music; click.stemId = "click"; click.category = "Click";
  click.displayName = "Pulso";
  song.stems = {music, guide, click};
  return song;
}

class FakeAnalyzer final : public IMusicStructureAnalyzer {
public:
  bool supported = true;
  std::atomic<bool> entered{false}, release{false}, finished{false};
  std::atomic<int> calls{0};
  bool available() const override { return supported; }
  juce::String unavailableReason() const override { return "Proveedor local no compatible en esta prueba"; }
  juce::String providerId() const override { return "injected-ui-test"; }
  juce::String version() const override { return "review-fixture-1"; }
  bool analyze(const juce::File &audio, double duration,
               const CancellationCheck &cancelled, MusicStructureAnalysis &out,
               juce::String &error) override {
    ++calls; entered = true;
    while (!release && !(cancelled && cancelled()))
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (cancelled && cancelled()) {
      error = "Cancelado"; finished = true; return false;
    }
    if (!audio.existsAsFile() || std::abs(duration - 6.0) > 0.00001) {
      error = juce::String::fromUTF8("La preparación de audio no produjo el fixture esperado");
      finished = true; return false;
    }
    out.provider = providerId(); out.version = version();
    out.sections = {{0, 2}, {2, 4}, {4, 6}};
    out.tempo.source = "compases de fixture inyectado";
    out.tempo.bars = {0, 1, 2, 3, 4, 5};
    out.tempo.beats = {0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5};
    out.tempo.beatsPerMinute = 120;
    finished = true; return true;
  }
};

class FakeGuideTranscriber final : public IGuideTranscriber {
public:
  std::atomic<bool> entered{false}, release{false};
  bool available() const override { return true; }
  juce::String unavailableReason() const override { return {}; }
  juce::String providerId() const override { return "injected-guide-ui"; }
  juce::String version() const override { return "1"; }
  bool transcribe(const GuideTranscriptionRequest &request, const CancellationCheck &cancelled,
                  const GuideTranscriptionProgress &progress, GuideTranscript &out,
                  juce::String &error) override {
    entered = true;
    while (!release && !cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (cancelled()) { error = "Cancelado"; return false; }
    out.provider = providerId(); out.providerVersion = version();
    out.language = request.language; out.durationSeconds = 6;
    out.segments = {{1.25, 1.75, "Coro"}, {3.0, 3.7, "Verso uno"}};
    if (progress) progress(1.0);
    return true;
  }
};

class FakeModelSource final : public IGuideModelDownloadSource {
public:
  juce::String payload = "synthetic local model payload";
  std::atomic<int> calls{0};
  std::atomic<bool> entered{false}, release{false};
  bool waitBeforeOpen = false;
  std::unique_ptr<juce::InputStream> open(const GuideModelAsset &,
      const CancellationCheck &cancelled, juce::String &) override {
    ++calls; entered = true;
    while (waitBeforeOpen && !release && !cancelled())
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (cancelled()) return {};
    return std::make_unique<juce::MemoryInputStream>(payload.toRawUTF8(), size_t(payload.getNumBytesAsUTF8()), true);
  }
  GuideModelAsset asset() const {
    return {"https://example.invalid/model.bin", juce::SHA256(payload.toRawUTF8(), size_t(payload.getNumBytesAsUTF8())).toHexString(),
            juce::int64(payload.getNumBytesAsUTF8())};
  }
};
} // namespace

// Access production controls and read-only review state. Analysis, edits,
// selection and acceptance are driven through the actual component callbacks.
struct SectionDetectionPanelTestAccess {
  static void cache(SectionDetectionPanel &p, const juce::File &directory) {
    p.analysisCacheDirectory = directory;
  }
  static auto &analyze(SectionDetectionPanel &p) { return p.analyze; }
  static auto &cancel(SectionDetectionPanel &p) { return p.cancel; }
  static auto &add(SectionDetectionPanel &p) { return p.add; }
  static auto &accept(SectionDetectionPanel &p) { return p.acceptSelected; }
  static auto &all(SectionDetectionPanel &p) { return p.selectAll; }
  static auto &transcribe(SectionDetectionPanel &p) { return p.transcribe; }
  static auto &tracks(SectionDetectionPanel &p) { return p.showTracks; }
  static auto transcript(const SectionDetectionPanel &p) { return p.transcript.getText(); }
  static bool transcriptVisible(const SectionDetectionPanel &p) { return p.transcript.isVisible(); }
  static bool transcriptReview(const SectionDetectionPanel &p) { return p.transcriptReview; }
  static auto help(const SectionDetectionPanel &p) { return p.help.getText(); }
  static void downloadSource(SectionDetectionPanel &p, std::shared_ptr<IGuideModelDownloadSource> source,
                             GuideModelAsset asset) {
    p.modelDownloadSource = std::move(source); p.modelAsset = std::move(asset);
  }
  static void model(SectionDetectionPanel &p, const juce::File &file) { p.guideModelFile = file; p.setBusy(false); }
  static void complete(SectionDetectionPanel &p, DetectionOutcome outcome) {
    p.completeDetection(std::move(outcome));
  }
  static void queueCompletion(SectionDetectionPanel &p, DetectionOutcome outcome,
                              std::function<void()> delivered) {
    auto safe = juce::Component::SafePointer<SectionDetectionPanel>(&p);
    juce::MessageManager::callAsync([safe, outcome = std::move(outcome), delivered = std::move(delivered)]() mutable {
      if (safe) safe->completeDetection(std::move(outcome));
      if (delivered) delivered();
    });
  }
  static bool busy(const SectionDetectionPanel &p) { return p.busy; }
  static bool reviewing(const SectionDetectionPanel &p) { return p.reviewing; }
  static bool spinner(const SectionDetectionPanel &p) { return p.spinner.isVisible(); }
  static auto state(const SectionDetectionPanel &p) { return p.status.getText(); }
  static auto result(const SectionDetectionPanel &p) { return p.result; }
  static bool gridConfirmed(const SectionDetectionPanel &p) { return p.gridConfirmed.getToggleState(); }
  static int role(SectionDetectionPanel &p, int index) {
    auto *row = p.content.getChildComponent(index);
    for (int i = 0; i < row->getNumChildComponents(); ++i)
      if (auto *box = dynamic_cast<juce::ComboBox *>(row->getChildComponent(i)))
        return box->getSelectedId();
    throw std::runtime_error("missing role selector");
  }
  static juce::Component &row(SectionDetectionPanel &p, int index) {
    int visibleRow = 0;
    for (int i = 0; i < p.content.getNumChildComponents(); ++i) {
      auto *component = p.content.getChildComponent(i);
      if (!component->isVisible()) continue;
      bool editorFound = false;
      for (int j = 0; j < component->getNumChildComponents(); ++j)
        editorFound |= dynamic_cast<juce::TextEditor *>(component->getChildComponent(j)) != nullptr;
      if (editorFound && visibleRow++ == index) return *component;
    }
    throw std::runtime_error("missing visible proposal row");
  }
  static juce::TextEditor &editor(SectionDetectionPanel &p, int index, int field) {
    auto &component = row(p, index); int found = 0;
    for (int i = 0; i < component.getNumChildComponents(); ++i)
      if (auto *editor = dynamic_cast<juce::TextEditor *>(component.getChildComponent(i)))
        if (found++ == field) return *editor;
    throw std::runtime_error("missing proposal text editor");
  }
  static juce::Button &button(SectionDetectionPanel &p, int index, const juce::String &caption) {
    auto &component = row(p, index);
    for (int i = 0; i < component.getNumChildComponents(); ++i)
      if (auto *button = dynamic_cast<juce::Button *>(component.getChildComponent(i)))
        if (button->getButtonText() == caption) return *button;
    throw std::runtime_error("missing proposal action");
  }
  static juce::ToggleButton &chosen(SectionDetectionPanel &p, int index) {
    auto &component = row(p, index);
    for (int i = 0; i < component.getNumChildComponents(); ++i)
      if (auto *button = dynamic_cast<juce::ToggleButton *>(component.getChildComponent(i)))
        return *button;
    throw std::runtime_error("missing proposal checkbox");
  }
};

namespace {
void click(juce::Button &button) {
  expect(button.isEnabled(), "clicked production control is enabled");
  button.triggerClick(); pump();
}
void capture(SectionDetectionPanel &panel) {
  const auto requested = juce::SystemStats::getEnvironmentVariable(
      "PW_SECTION_DETECTION_CAPTURE_PATH", {});
  if (requested.isEmpty()) return;
  expect(juce::File::isAbsolutePath(requested), "capture target is absolute");
  juce::File file(requested);
  expect(file.getParentDirectory().createDirectory().wasOk(), "capture directory opens");
  auto output = file.createOutputStream();
  expect(output && output->setPosition(0) && output->truncate().wasOk(), "capture file opens");
  expect(juce::PNGImageFormat().writeImageToStream(
      panel.createComponentSnapshot(panel.getLocalBounds(), true, 1.5f), *output),
      "production review component renders to PNG");
}

void reviewFlow(const juce::File &root, const ImportedSongData &song) {
  auto provider = std::make_shared<FakeAnalyzer>();
  int closeCalls = 0, acceptCalls = 0;
  double listened = -1;
  std::vector<SongArrangementPartSnapshot> preview;
  SongArrangementSnapshot existing, committed;
  SongArrangementPartSnapshot manual;
  manual.id = "manual-section"; manual.name = "Verso corregido";
  manual.startSec = 2; manual.endSec = 4; manual.sourceStartSec = 2; manual.sourceEndSec = 4;
  manual.automation.pads.volume = 0.35f;
  existing.parts.push_back(manual);
  SectionDetectionPanel::Actions actions;
  actions.preview = [&](auto value) { preview = std::move(value); };
  actions.listen = [&](double value) { listened = value; };
  actions.close = [&] { ++closeCalls; };
  actions.accept = [&](const auto &selected, double duration, juce::String &error) {
    ++acceptCalls;
    const auto plan = appendConfirmedSuggestions(existing, selected, duration);
    if (plan.error.isNotEmpty() || !plan.conflictingSuggestionIds.isEmpty()) {
      error = juce::String::fromUTF8("La propuesta coincide con una sección manual protegida"); return false;
    }
    committed = plan.arrangement; return true;
  };
  SectionDetectionPanel panel(song, actions, provider);
  panel.setSize(1120, 540);
  SectionDetectionPanelTestAccess::cache(panel, root.getChildFile("review-cache"));
  expect(!SectionDetectionPanelTestAccess::gridConfirmed(panel),
         "BPM metadata never silently confirms a synchronized grid");
  expect(SectionDetectionPanelTestAccess::role(panel, 0) == 1 &&
             SectionDetectionPanelTestAccess::role(panel, 1) == 2 &&
             SectionDetectionPanelTestAccess::role(panel, 2) == 3,
         "roles use categories while an ambiguous guide filename stays manually selectable");
  click(SectionDetectionPanelTestAccess::analyze(panel));
  pumpUntil([&] { return provider->entered.load(); }, "real Analyze reaches the injected worker provider");
  expect(SectionDetectionPanelTestAccess::busy(panel) &&
             SectionDetectionPanelTestAccess::spinner(panel) &&
             !SectionDetectionPanelTestAccess::accept(panel).isEnabled(),
         "analysis exposes indeterminate progress and prevents confirmation");
  expect(SectionDetectionPanelTestAccess::state(panel).containsIgnoreCase("estructura"),
         "the actual analysis stage reaches the visible status label");
  provider->release = true;
  pumpUntil([&] { return !SectionDetectionPanelTestAccess::busy(panel); }, "analysis completes asynchronously");
  expect(SectionDetectionPanelTestAccess::reviewing(panel) && preview.size() == 3 &&
             SectionDetectionPanelTestAccess::result(panel).suggestions.size() == 3,
         "final results become three editable rows and provisional timeline blocks");
  expect(SectionDetectionPanelTestAccess::state(panel).contains(juce::String::fromUTF8("heurística")),
         "review identifies the confidence as PlayWorship heuristics");
  auto &name = SectionDetectionPanelTestAccess::editor(panel, 1, 0);
  name.setText("Verso revisado a mano", true); pump();
  auto &start = SectionDetectionPanelTestAccess::editor(panel, 1, 1);
  start.setText("2.250000", false); start.onReturnKey(); pump();
  auto result = SectionDetectionPanelTestAccess::result(panel);
  near(result.suggestions[0].endSeconds, 2.25, "editing a boundary preserves contiguous neighbouring proposals");
  near(result.suggestions[1].startSeconds, 2.25, "the edited start retains sub-second precision");
  expect(result.suggestions[1].userEdited && result.suggestions[0].userEdited,
         "an edited boundary marks both affected suggestions as manually corrected");
  click(SectionDetectionPanelTestAccess::button(panel, 1, "Compases"));
  result = SectionDetectionPanelTestAccess::result(panel);
  near(result.suggestions[1].startSeconds, 2.0, "bar action uses actual provider bar candidates");
  expect(result.suggestions[1].displayName == "Verso revisado a mano",
         "bar adjustment preserves the manually edited name");
  click(SectionDetectionPanelTestAccess::analyze(panel));
  expect(provider->calls == 1 &&
             SectionDetectionPanelTestAccess::state(panel).contains("correcciones"),
         "Analyze again preserves corrections and never starts a replacing worker");
  click(SectionDetectionPanelTestAccess::chosen(panel, 0));
  expect(preview.size() == 2, "checkbox selection updates the transient timeline preview");
  click(SectionDetectionPanelTestAccess::all(panel));
  expect(preview.size() == 3, "select all restores every provisional block");
  click(SectionDetectionPanelTestAccess::button(panel, 2, "Escuchar"));
  near(listened, 2.0, "listen requests existing transport two seconds before the boundary");
  click(SectionDetectionPanelTestAccess::add(panel));
  expect(SectionDetectionPanelTestAccess::result(panel).suggestions.size() == 4,
         "Add splits the final proposal into separately editable ranges");
  click(SectionDetectionPanelTestAccess::button(panel, 3, "Eliminar"));
  expect(SectionDetectionPanelTestAccess::result(panel).suggestions.size() == 3 && preview.size() == 3,
         "Delete removes the transient proposal and updates its timeline block");
  capture(panel);
  click(SectionDetectionPanelTestAccess::accept(panel));
  expect(acceptCalls == 1 && closeCalls == 0 && committed.parts.empty() &&
             SectionDetectionPanelTestAccess::state(panel).contains("manual protegida"),
         "acceptance conflicts remain reviewable and never close or persist partial edits");
  click(SectionDetectionPanelTestAccess::chosen(panel, 1));
  click(SectionDetectionPanelTestAccess::accept(panel));
  expect(acceptCalls == 2 && closeCalls == 1 && committed.parts.size() == 3,
         "the selected conflict-free subset reaches the existing confirmation adapter");
  expect(committed.parts[1].id == manual.id &&
             committed.parts[1].name == manual.name &&
             committed.parts[1].automation == manual.automation,
         "confirmation retains the existing manual section and its automation");
}

void cancellationAndUnsupported(const juce::File &root, const ImportedSongData &song) {
  auto provider = std::make_shared<FakeAnalyzer>();
  int closeCalls = 0, acceptCalls = 0;
  SectionDetectionPanel::Actions actions;
  actions.close = [&] { ++closeCalls; };
  actions.accept = [&](const auto &, double, juce::String &) { ++acceptCalls; return true; };
  SectionDetectionPanel panel(song, actions, provider);
  panel.setSize(1000, 500);
  SectionDetectionPanelTestAccess::cache(panel, root.getChildFile("cancel-cache"));
  click(SectionDetectionPanelTestAccess::analyze(panel));
  pumpUntil([&] { return provider->entered.load(); }, "cancel test reaches worker analysis");
  click(SectionDetectionPanelTestAccess::cancel(panel));
  pumpUntil([&] { return !SectionDetectionPanelTestAccess::busy(panel); }, "cancelled provider returns to responsive UI");
  expect(provider->finished && SectionDetectionPanelTestAccess::state(panel) == "Cancelado" &&
             !SectionDetectionPanelTestAccess::reviewing(panel) && acceptCalls == 0 && closeCalls == 0,
         "cancellation is visible and never confirms or silently closes the review");
  click(SectionDetectionPanelTestAccess::cancel(panel));
  expect(closeCalls == 1, "the cancelled review can be explicitly dismissed");

  auto unavailable = std::make_shared<FakeAnalyzer>(); unavailable->supported = false;
  SectionDetectionPanel unsupported(song, {}, unavailable);
  unsupported.setSize(1000, 500);
  expect(!SectionDetectionPanelTestAccess::analyze(unsupported).isEnabled() &&
             SectionDetectionPanelTestAccess::analyze(unsupported).getTooltip().contains("no compatible"),
         "an unavailable provider is explained while analysis stays disabled");
}

void guideOnlyFlow(const juce::File &root, const ImportedSongData &song) {
  auto unavailable = std::make_shared<FakeAnalyzer>(); unavailable->supported = false;
  auto guide = std::make_shared<FakeGuideTranscriber>();
  int accepted = 0;
  SectionDetectionPanel::Actions actions;
  actions.accept = [&](const auto &, double, juce::String &) { ++accepted; return true; };
  SectionDetectionPanel panel(song, actions, unavailable, guide);
  panel.setSize(1120, 540);
  SectionDetectionPanelTestAccess::cache(panel, root.getChildFile("guide-ui-cache"));
  const auto model = root.getChildFile("dummy-model.bin");
  expect(model.replaceWithText("synthetic model identity"), "UI guide fixture has a local model identity");
  SectionDetectionPanelTestAccess::model(panel, model);
  expect(!SectionDetectionPanelTestAccess::analyze(panel).isEnabled(), "structure can remain unavailable");
  click(SectionDetectionPanelTestAccess::transcribe(panel));
  pumpUntil([&] { return guide->entered.load(); }, "guide action starts without the Apple provider");
  expect(SectionDetectionPanelTestAccess::busy(panel), "guide transcription runs asynchronously");
  guide->release = true;
  pumpUntil([&] { return !SectionDetectionPanelTestAccess::busy(panel); }, "guide UI finishes asynchronously");
  expect(SectionDetectionPanelTestAccess::transcriptReview(panel) &&
      SectionDetectionPanelTestAccess::transcript(panel).contains("00:01.250") &&
      SectionDetectionPanelTestAccess::transcript(panel).contains("Verso uno"),
      "spoken timestamps and literal text appear in the production transcript view");
  expect(SectionDetectionPanelTestAccess::result(panel).suggestions.empty() &&
      !SectionDetectionPanelTestAccess::accept(panel).isEnabled() && accepted == 0,
      "a transcript cannot be mistaken for accepted musical boundaries");
  click(SectionDetectionPanelTestAccess::tracks(panel));
  expect(!SectionDetectionPanelTestAccess::transcriptReview(panel) &&
      SectionDetectionPanelTestAccess::role(panel, 1) == 2,
      "returning to tracks preserves guide role selection");
}

void guideWaveformHandoff(const juce::File &root, const ImportedSongData &song) {
  auto unavailable = std::make_shared<FakeAnalyzer>(); unavailable->supported = false;
  int reviewed = 0, closed = 0, accepted = 0, previews = 0;
  std::vector<juce::String> calls;
  GuideTranscript received;
  SectionDetectionPanel::Actions actions;
  actions.reviewGuide = [&](const GuideTranscript &value) {
    ++reviewed; received = value; calls.push_back("review");
    expect(closed == 0, "waveform review is installed before closing the configuration panel");
  };
  actions.close = [&] { ++closed; calls.push_back("close"); };
  actions.preview = [&](const auto &) { ++previews; };
  actions.accept = [&](const auto &, double, juce::String &) { ++accepted; return true; };
  SectionDetectionPanel panel(song, actions, unavailable, std::make_shared<FakeGuideTranscriber>());
  panel.setSize(1120, 540);
  const auto model = root.getChildFile("handoff-model.bin");
  expect(model.replaceWithText("synthetic model identity"), "handoff UI has an explicit local model");
  SectionDetectionPanelTestAccess::model(panel, model);
  expect(SectionDetectionPanelTestAccess::transcribe(panel).getButtonText() == "Sugerir secciones" &&
             SectionDetectionPanelTestAccess::help(panel).contains("arrastra") &&
             !SectionDetectionPanelTestAccess::analyze(panel).isVisible() &&
             !SectionDetectionPanelTestAccess::add(panel).isVisible() &&
             !SectionDetectionPanelTestAccess::all(panel).isVisible() &&
             !SectionDetectionPanelTestAccess::accept(panel).isVisible(),
         "main action explains that suggestions are reviewed and dragged on the waveform");

  DetectionOutcome success;
  success.result.songId = song.id; success.result.durationSeconds = 6;
  GuideTranscript original;
  original.provider = "injected-guide-ui"; original.providerVersion = "handoff-1";
  original.audioFingerprint = "raw-audio-identity"; original.modelFingerprint = "raw-model-identity";
  original.language = "es"; original.durationSeconds = 6;
  for (int i = 0; i < 15; ++i)
    original.segments.push_back({0.1 + i * 0.3, 0.2 + i * 0.3,
                                juce::String::fromUTF8("Anuncio literal á ") + juce::String(i + 1)});
  success.guideTranscript = original;
  SectionDetectionPanelTestAccess::complete(panel, std::move(success));
  expect(reviewed == 1 && closed == 0,
         "guide success hands off once and defers closing until a later message callback");
  expect(received.segments.size() == 15 && received.providerVersion == original.providerVersion &&
             received.audioFingerprint == original.audioFingerprint &&
             received.modelFingerprint == original.modelFingerprint && received.language == original.language,
         "waveform review receives all raw transcript segments and identity metadata");
  for (size_t i = 0; i < original.segments.size(); ++i) {
    near(received.segments[i].startSeconds, original.segments[i].startSeconds, "handoff preserves spoken start time");
    near(received.segments[i].endSeconds, original.segments[i].endSeconds, "handoff preserves spoken end time");
    expect(received.segments[i].text == original.segments[i].text, "handoff preserves literal ASR text");
  }
  expect(!SectionDetectionPanelTestAccess::transcriptReview(panel) &&
             !SectionDetectionPanelTestAccess::transcriptVisible(panel) &&
             SectionDetectionPanelTestAccess::transcript(panel).isEmpty() &&
             !SectionDetectionPanelTestAccess::accept(panel).isEnabled() && accepted == 0 && previews == 0,
         "main flow shows no transcript list and neither accepts sections nor clears the owner's waveform preview");
  pumpUntil([&] { return closed == 1; }, "configuration closes after the waveform review handoff");
  expect(calls == std::vector<juce::String>{"review", "close"}, "handoff and deferred close run in order exactly once");
}

void modelPreparationIntegrity(const juce::File &root) {
  auto source = std::make_shared<FakeModelSource>();
  const auto asset = source->asset();
  const auto prepare = [&](const juce::File &destination, const GuideModelAsset &requestedAsset) {
    std::atomic<bool> done{false}; GuideModelPreparationResult result;
    auto task = prepareWhisperGuideModel(destination, {}, [&](GuideModelPreparationResult outcome) {
      result = std::move(outcome); done = true;
    }, source, requestedAsset);
    pumpUntil([&] { return done.load(); }, "model worker completes without external network");
    return result;
  };
  const auto directory = root.getChildFile("model-provisioning");
  const auto destination = directory.getChildFile("voice.bin");
  auto result = prepare(destination, asset);
  expect(result.error.isEmpty() && !result.cancelled && result.modelFile == destination && source->calls == 1 &&
             destination.loadFileAsString() == source->payload,
         "download worker installs only the exact verified model bytes");
  result = prepare(destination, asset);
  expect(result.alreadyPresent && result.error.isEmpty() && source->calls == 1,
         "verified existing model is reused without opening the download source");
  auto invalidHash = asset; invalidHash.sha256 = juce::String::repeatedString("0", 64);
  const auto corrupt = directory.getChildFile("corrupt.bin");
  result = prepare(corrupt, invalidHash);
  expect(result.error.isNotEmpty() && !corrupt.exists(), "checksum mismatch never installs a model");
  auto invalidSize = asset; ++invalidSize.sizeBytes;
  const auto truncated = directory.getChildFile("truncated.bin");
  result = prepare(truncated, invalidSize);
  expect(result.error.isNotEmpty() && !truncated.exists(), "short download never installs an incomplete model");
  const auto existing = directory.getChildFile("keep.bin");
  expect(existing.replaceWithText("preserve existing file"), "existing invalid-model fixture is written");
  const int callsBeforeExisting = source->calls.load();
  result = prepare(existing, asset);
  expect(result.error.isNotEmpty() && existing.loadFileAsString() == "preserve existing file" &&
             source->calls == callsBeforeExisting,
         "preparation preserves a mismatched existing file without network or replacement");
  source->waitBeforeOpen = true; source->entered = false;
  std::atomic<bool> done{false}; GuideModelPreparationResult cancelled;
  const auto cancelledFile = directory.getChildFile("cancelled.bin");
  auto task = prepareWhisperGuideModel(cancelledFile, {}, [&](GuideModelPreparationResult outcome) {
    cancelled = std::move(outcome); done = true;
  }, source, asset);
  pumpUntil([&] { return source->entered.load(); }, "cancel test reaches the model source");
  const auto before = std::chrono::steady_clock::now(); task->cancel();
  expect(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(50), "download cancellation does not join the UI thread");
  pumpUntil([&] { return done.load(); }, "cancelled model worker returns");
  expect(cancelled.cancelled && cancelled.modelFile == juce::File{} && !cancelledFile.exists(),
         "cancelled download does not publish or install a model");
  expect(directory.findChildFiles(juce::File::findFiles, false).size() == 2,
         "failure and cancellation remove every temporary download file");
}

void modelDownloadContinuesSuggestionFlow(const juce::File &root, const ImportedSongData &song) {
  auto unavailable = std::make_shared<FakeAnalyzer>(); unavailable->supported = false;
  auto guide = std::make_shared<FakeGuideTranscriber>(); guide->release = true;
  auto source = std::make_shared<FakeModelSource>();
  int reviewed = 0, closed = 0;
  SectionDetectionPanel::Actions actions;
  actions.reviewGuide = [&](const GuideTranscript &value) { ++reviewed; expect(value.segments.size() == 2, "prepared model continues to the guide provider"); };
  actions.close = [&] { ++closed; };
  SectionDetectionPanel panel(song, actions, unavailable, guide);
  SectionDetectionPanelTestAccess::cache(panel, root.getChildFile("download-flow-cache"));
  SectionDetectionPanelTestAccess::model(panel, root.getChildFile("download-flow-model.bin"));
  SectionDetectionPanelTestAccess::downloadSource(panel, source, source->asset());
  panel.beginSuggestionFlow();
  expect(!SectionDetectionPanelTestAccess::busy(panel) && source->calls == 0 && !guide->entered,
         "opening the suggestion flow never silently downloads a missing model");
  expect(SectionDetectionPanelTestAccess::transcribe(panel).isEnabled() &&
             SectionDetectionPanelTestAccess::transcribe(panel).getButtonText() == "Descargar y continuar",
         "a missing model presents one explicit download-and-continue action");
  click(SectionDetectionPanelTestAccess::transcribe(panel));
  pumpUntil([&] { return reviewed == 1 && closed == 1; }, "native download continues automatically into waveform review");
  expect(source->calls == 1 && guide->entered && !SectionDetectionPanelTestAccess::transcriptVisible(panel),
         "one model request is followed by local guide inference without a transcript list");
}

void guideHandoffOwnerCanClose(const ImportedSongData &song) {
  auto unavailable = std::make_shared<FakeAnalyzer>(); unavailable->supported = false;
  std::unique_ptr<SectionDetectionPanel> panel;
  int reviewed = 0, closed = 0;
  SectionDetectionPanel::Actions actions;
  actions.reviewGuide = [&](const GuideTranscript &) { ++reviewed; panel.reset(); };
  actions.close = [&] { ++closed; };
  panel = std::make_unique<SectionDetectionPanel>(song, actions, unavailable,
                                               std::make_shared<FakeGuideTranscriber>());
  DetectionOutcome success;
  success.guideTranscript = GuideTranscript{};
  SectionDetectionPanelTestAccess::complete(*panel, std::move(success));
  pump();
  expect(reviewed == 1 && !panel && closed == 0,
         "an owner that destroys the panel during review is not accessed or closed a second time");
}

void guideCancellationAfterQueuedSuccess(const juce::File &root, const ImportedSongData &song) {
  auto unavailable = std::make_shared<FakeAnalyzer>(); unavailable->supported = false;
  auto guide = std::make_shared<FakeGuideTranscriber>();
  int previews = 0, accepted = 0, reviewed = 0, closed = 0;
  SectionDetectionPanel::Actions actions;
  actions.preview = [&](const auto &) { ++previews; };
  actions.accept = [&](const auto &, double, juce::String &) { ++accepted; return true; };
  actions.reviewGuide = [&](const GuideTranscript &) { ++reviewed; };
  actions.close = [&] { ++closed; };
  SectionDetectionPanel panel(song, actions, unavailable, guide);
  panel.setSize(1120, 540);
  SectionDetectionPanelTestAccess::cache(panel, root.getChildFile("queued-guide-cancel-cache"));
  const auto model = root.getChildFile("queued-guide-cancel-model.bin");
  expect(model.replaceWithText("synthetic model identity"), "queued-cancel fixture has a local model");
  SectionDetectionPanelTestAccess::model(panel, model);
  click(SectionDetectionPanelTestAccess::transcribe(panel));
  pumpUntil([&] { return guide->entered.load(); }, "queued-cancel test reaches the guide worker");

  // Model a success already posted by the worker while the native mouse event
  // delivers Cancel before the message callback is consumed. Do not pump the
  // queue between posting success and invoking the production button callback.
  DetectionOutcome success;
  success.result.songId = song.id; success.result.durationSeconds = 6;
  GuideTranscript transcript; transcript.durationSeconds = 6;
  transcript.segments = {{1.25, 1.75, "Coro"}};
  success.guideTranscript = std::move(transcript);
  bool delivered = false;
  SectionDetectionPanelTestAccess::queueCompletion(panel, std::move(success), [&] { delivered = true; });
  SectionDetectionPanelTestAccess::cancel(panel).onClick();
  pumpUntil([&] { return delivered; }, "queued success reaches the real panel completion handler");
  expect(!SectionDetectionPanelTestAccess::busy(panel) &&
             !SectionDetectionPanelTestAccess::transcriptReview(panel) &&
             SectionDetectionPanelTestAccess::transcript(panel).isEmpty() &&
             SectionDetectionPanelTestAccess::state(panel) == "Cancelado" && previews == 0 && accepted == 0 &&
             reviewed == 0 && closed == 0,
         "Cancel wins over an already queued success without review handoff, close or arrangement writes");
}

void realGuideFlow(const juce::File &root) {
  const auto audioPath = juce::SystemStats::getEnvironmentVariable("PW_REAL_GUIDE_AUDIO", {});
  if (audioPath.isEmpty()) return;
  const juce::File audio(audioPath);
  auto song = makeSong(audio);
  song.stems = {song.stems[1]};
  song.title = juce::String::fromUTF8("Guía local · reconocimiento real");
  auto unavailable = std::make_shared<FakeAnalyzer>(); unavailable->supported = false;
  SectionDetectionPanel panel(song, {}, unavailable);
  panel.setSize(1120, 540);
  SectionDetectionPanelTestAccess::cache(panel, root.getChildFile("real-guide-ui-cache"));
  const auto model = juce::SystemStats::getEnvironmentVariable("PW_REAL_GUIDE_MODEL", {});
  if (model.isNotEmpty()) SectionDetectionPanelTestAccess::model(panel, juce::File(model));
  click(SectionDetectionPanelTestAccess::transcribe(panel));
  // Explicit real-model QA can run on a shared build host. Normal regression
  // tests retain their short deadline and never require a model/private audio.
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 900000;
  while (SectionDetectionPanelTestAccess::busy(panel) && juce::Time::getMillisecondCounterHiRes() < deadline)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  expect(!SectionDetectionPanelTestAccess::busy(panel), "real guide finishes within the UI budget");
  if (!SectionDetectionPanelTestAccess::transcriptReview(panel))
    throw std::runtime_error(SectionDetectionPanelTestAccess::state(panel).toStdString());
  expect(SectionDetectionPanelTestAccess::result(panel).suggestions.empty(),
      "real transcription never creates musical boundaries");
  capture(panel);
  std::cout << "PASS: real Whisper guide reaches transcript UI without Apple or arrangement writes\n";
}
} // namespace

int main(int argc, char **argv) {
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  try {
    // Explicit native-network QA only. The default regression path below uses
    // injected streams and never downloads a model or requires private audio.
    if (argc == 3 && juce::String::fromUTF8(argv[1]) == "--prepare-model") {
      std::atomic<bool> done{false}; GuideModelPreparationResult result;
      auto task = prepareWhisperGuideModel(juce::File(juce::String::fromUTF8(argv[2])), {},
          [&](GuideModelPreparationResult outcome) { result = std::move(outcome); done = true; });
      const auto deadline = juce::Time::getMillisecondCounterHiRes() + 600000;
      while (!done && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
      if (!done) { task->cancel(); throw std::runtime_error("Native model preparation timed out"); }
      expect(result.error.isEmpty() && !result.cancelled && result.modelFile.existsAsFile(),
             result.error.isEmpty() ? "Native model preparation failed" : result.error.toRawUTF8());
      std::cout << "PASS: native pinned model preparation; reused=" << result.alreadyPresent << '\n';
      return 0;
    }
    expect(argc == 1, "Usage: section panel tests [--prepare-model /absolute/model.bin]");
    TemporaryRoot temporary;
    const auto audio = temporary.file.getChildFile("music.wav");
    writeAudio(audio);
    const auto song = makeSong(audio);
    reviewFlow(temporary.file, song);
    std::cout << "PASS: real analysis control, review edits, preview, conflicts and selected confirmation\n";
    cancellationAndUnsupported(temporary.file, song);
    std::cout << "PASS: asynchronous cancellation and unavailable provider UI\n";
    guideOnlyFlow(temporary.file, song);
    std::cout << "PASS: guide-only transcription UI, timestamps and protected arrangement\n";
    guideWaveformHandoff(temporary.file, song);
    guideHandoffOwnerCanClose(song);
    std::cout << "PASS: raw guide handoff to waveform review, deferred close and owner lifetime\n";
    modelPreparationIntegrity(temporary.file);
    modelDownloadContinuesSuggestionFlow(temporary.file, song);
    std::cout << "PASS: model preparation integrity, offline reuse, cancellation and automatic suggestion continuation\n";
    guideCancellationAfterQueuedSuccess(temporary.file, song);
    std::cout << "PASS: cancelling discards guide success already queued for the UI\n";
    realGuideFlow(temporary.file);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n'; return 1;
  }
}
