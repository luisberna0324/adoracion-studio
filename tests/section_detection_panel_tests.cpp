#include "ui/panels/SectionDetectionPanel.h"

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
             SectionDetectionPanelTestAccess::state(unsupported).contains("no compatible"),
         "an unavailable provider is explained while analysis stays disabled");
}
} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  try {
    TemporaryRoot temporary;
    const auto audio = temporary.file.getChildFile("music.wav");
    writeAudio(audio);
    const auto song = makeSong(audio);
    reviewFlow(temporary.file, song);
    std::cout << "PASS: real analysis control, review edits, preview, conflicts and selected confirmation\n";
    cancellationAndUnsupported(temporary.file, song);
    std::cout << "PASS: asynchronous cancellation and unavailable provider UI\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n'; return 1;
  }
}
