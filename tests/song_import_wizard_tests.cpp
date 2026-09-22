#include "ui/panels/SongImportWizardComponent.h"

#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>

namespace {
void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void pumpUntil(const std::function<bool()> &done, const char *message) {
  const auto started = juce::Time::getMillisecondCounter();
  while (!done() && juce::Time::getMillisecondCounter() - started < 5000)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  expect(done(), message);
}

template <typename Control>
Control &childOf(juce::Component &parent) {
  for (auto *child : parent.getChildren())
    if (auto *control = dynamic_cast<Control *>(child))
      return *control;
  expect(false, "expected a typed child control");
  std::abort();
}

void expectInside(const juce::Component &component,
                  const juce::Rectangle<int> &available, const char *message) {
  expect(!component.getBounds().isEmpty() &&
             available.contains(component.getBounds()), message);
}

struct AudioFixtures {
  juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getNonexistentChildFile(
                                 "playworship-wizard-test", {}, false);
  juce::File first = directory.getChildFile("01-piano.wav");
  juce::File second = directory.getChildFile("02-bass.wav");
  juce::File third = directory.getChildFile("03-drums.wav");

  AudioFixtures() {
    expect(directory.createDirectory().wasOk(), "create isolated WAV fixtures");
    for (const auto &file : {first, second, third}) {
      juce::WavAudioFormat wav;
      std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
      expect(stream != nullptr, "open WAV fixture output");
      auto writer = wav.createWriterFor(
          stream, juce::AudioFormatWriterOptions{}
                      .withSampleRate(44100.0)
                      .withNumChannels(1)
                      .withBitsPerSample(16));
      expect(writer != nullptr, "create WAV fixture writer");
      juce::AudioBuffer<float> audio(1, 256);
      audio.clear();
      audio.setSample(0, 16, 0.25f);
      expect(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()),
             "write a real, readable audio fixture");
    }
  }

  ~AudioFixtures() { directory.deleteRecursively(); }
};
} // namespace

// Friend-only access keeps test knobs out of the production public API.
struct SongImportWizardTestAccess {
  using Wizard = SongImportWizardComponent;
  using Step = Wizard::Step;

  static void fillMetadata(Wizard &wizard) {
    // No notifications: these tests must never trigger online autocomplete.
    wizard.songTitleEditor.setText("Wizard fixture", juce::dontSendNotification);
    wizard.artistEditor.setText("Local test", juce::dontSendNotification);
    wizard.bpmEditor.setText("120", juce::dontSendNotification);
  }

  static juce::Component &mappingRow(Wizard &wizard, int index = 0) {
    auto *row = wizard.mapContent.getChildComponent(index);
    expect(row != nullptr, "mapping row exists");
    return *row;
  }

  static void layoutStep(Wizard &wizard, Step step, bool phone,
                         juce::Rectangle<int> content) {
    wizard.currentStep = step;
    wizard.phoneCreationLayout = phone;
    wizard.layoutStepImport(content);
    wizard.layoutStepMetadata(content);
    wizard.layoutStepMapping(content);
    wizard.layoutStepReview(content);
  }

  static void testStepLayouts(const AudioFixtures &files) {
    Wizard wizard({}, false);
    fillMetadata(wizard);
    wizard.handleSelectedStemFiles({files.first, files.second}, false);

    // Exercise intrinsic step layouts without pretending a desktop test host
    // supplies native iOS/Android keyboard or safe-area insets.
    for (const int width : {320, 390, 430}) {
      wizard.setSize(width, 760);
      const juce::Rectangle<int> content(17, 150, width - 34, 430);
      layoutStep(wizard, Step::Metadata, true, content);
      expect(wizard.metadataViewport.getBounds() == content,
             "mobile Detalles fits the available content viewport");
      const auto form = wizard.metadataContent.getLocalBounds();
      expect(form.getHeight() == 550, "metadata content remains stable-height");
      for (auto *control : {static_cast<juce::Component *>(&wizard.songTitleEditor),
                            static_cast<juce::Component *>(&wizard.artistEditor),
                            static_cast<juce::Component *>(&wizard.bpmEditor),
                            static_cast<juce::Component *>(&wizard.keyBox),
                            static_cast<juce::Component *>(&wizard.timeSignatureBox)}) {
        expectInside(*control, form, "metadata field fits scrollable content");
        expect(control->getHeight() >= 44, "metadata fields meet touch height");
      }
      expect(!wizard.songTitleEditor.getBounds().intersects(
                 wizard.artistEditor.getBounds()), "title and artist do not overlap");
      expect(!wizard.bpmEditor.getBounds().intersects(wizard.keyBox.getBounds()) &&
                 !wizard.keyBox.getBounds().intersects(wizard.timeSignatureBox.getBounds()),
             "BPM/key/time share a non-overlapping mobile row");

      layoutStep(wizard, Step::Stems, true, content);
      expectInside(wizard.addFilesButton, content, "Archivos action fits content");
      expect(wizard.addFilesButton.getHeight() >= 44,
             "Archivos action meets touch height");
      expectInside(wizard.stemsList, content, "Archivos list fits content");
      expect(wizard.stemsList.getRowHeight() == 62,
             "Archivos keeps compact touch rows");

      layoutStep(wizard, Step::Mapping, true, content);
      expect(wizard.mapViewport.getBounds() == content,
             "mobile Mapping fits available viewport");
      for (auto *row : wizard.mapContent.getChildren()) {
        expect(row->getHeight() == 104, "mobile mapping row reserves touch targets");
        auto &name = childOf<juce::TextEditor>(*row);
        auto &category = childOf<juce::ComboBox>(*row);
        auto &include = childOf<juce::ToggleButton>(*row);
        for (auto *control : {static_cast<juce::Component *>(&name),
                              static_cast<juce::Component *>(&category),
                              static_cast<juce::Component *>(&include)}) {
          expectInside(*control, row->getLocalBounds(),
                       "mobile mapping control fits its row");
          expect(control->getHeight() >= 44 && control->getWidth() >= 44,
                 "mapping controls meet both touch dimensions");
        }
        expect(name.getY() == category.getY() &&
                   include.getBottom() <= name.getY(),
               "mapping heading and fields are stacked without overlap");
      }

      layoutStep(wizard, Step::Review, true, content);
      expect(wizard.summaryEditor.getBounds() == content &&
                 wizard.summaryEditor.isMultiLine() &&
                 wizard.summaryEditor.isReadOnly(),
             "Revisar remains a bounded read-only scrollable summary");

      // A docked keyboard changes only the viewport's extent, not row height.
      layoutStep(wizard, Step::Mapping, true, content.withHeight(150));
      wizard.mapViewport.setViewPosition(0, 80);
      expect(wizard.mapViewport.getViewPositionY() > 0,
             "mapping rows remain vertically reachable above a keyboard");
      expect(mappingRow(wizard).getHeight() == 104,
             "short viewport does not compress mapping controls");
    }

    wizard.setSize(860, 640);
    layoutStep(wizard, Step::Metadata, false, {12, 98, 836, 530});
    expect(!wizard.metadataViewport.isVisible() &&
               wizard.songTitleEditor.getParentComponent() == &wizard,
           "desktop metadata remains outside the phone viewport");
    expect(wizard.secuenciasArtworkPreview.getX() > wizard.songTitleEditor.getRight(),
           "desktop retains its two-column metadata layout");

    for (const int contentWidth : {836, 500}) {
      layoutStep(wizard, Step::Mapping, false, {12, 98, contentWidth, 530});
      auto &row = mappingRow(wizard);
      auto &name = childOf<juce::TextEditor>(row);
      auto &category = childOf<juce::ComboBox>(row);
      expect(row.getHeight() == 42 && name.getHeight() == 36 &&
                 category.getHeight() == 36,
             "desktop mapping retains 42-point rows even when narrow");
      expect(name.getY() == 3 && category.getY() == 3,
             "narrow desktop rows never silently use the mobile stack");
    }
  }

  static void testDraftNavigation(const AudioFixtures &files) {
    Wizard wizard({}, false, Wizard::Mode::Import, {},
                  [](const ImportedSongData &, juce::File &, juce::String &) {
                    return false;
                  });
    fillMetadata(wizard);
    wizard.handleSelectedStemFiles({files.first, files.second}, false);
    wizard.changeStep(Step::Mapping);
    auto &first = mappingRow(wizard);
    childOf<juce::TextEditor>(first).setText("My edited piano",
                                            juce::dontSendNotification);
    auto &category = childOf<juce::ComboBox>(first);
    category.setSelectedId(3, juce::dontSendNotification);
    const auto editedCategory = category.getText();
    childOf<juce::ToggleButton>(first).setToggleState(false,
                                                     juce::dontSendNotification);

    wizard.backButton.onClick();
    expect(wizard.currentStep == Step::Stems &&
               wizard.stems[0].displayName == "My edited piano" &&
               wizard.stems[0].category == editedCategory && !wizard.stems[0].include,
           "Back persists name/category/include in the draft before leaving Mapping");
    wizard.handleSelectedStemFiles({files.third}, false);
    wizard.removeStemAtIndex(1);
    expect(wizard.stems.size() == 2 &&
               wizard.stems[0].displayName == "My edited piano" &&
               wizard.stems[0].category == editedCategory && !wizard.stems[0].include &&
               wizard.stems[1].file == files.third,
           "adding and removing audio preserve the remaining mapped values");
    expect(childOf<juce::TextEditor>(mappingRow(wizard)).getText() == "My edited piano",
           "rebuilt controls display the preserved draft");

    wizard.handleSelectedStemFiles({}, true);
    expect(wizard.stems.size() == 2 && !wizard.stems[0].include,
           "cancelling Replace preserves files and inclusion choices");
    const auto song = wizard.buildSongData();
    expect(song.stems.size() == 1 && song.stems[0].fileName == files.third.getFileName(),
           "submission data includes only the retained enabled stem");

    wizard.changeStep(Step::Mapping);
    for (auto *row : wizard.mapContent.getChildren())
      childOf<juce::ToggleButton>(*row).setToggleState(false,
                                                       juce::dontSendNotification);
    wizard.backButton.onClick();
    expect(wizard.currentStep == Step::Stems,
           "all-disabled mapping can return to Archivos");
    wizard.goToNextStep();
    expect(wizard.currentStep == Step::Mapping,
           "Archivos lets all-disabled drafts return to Mapping to re-enable Cargar");
  }

  static void testCancelledDestination(const AudioFixtures &files) {
    int saved = 0, dismissed = 0, resolved = 0;
    Wizard *subject = nullptr;
    Wizard wizard(
        [&](ImportedSongData, juce::String &, const SongImportProgressFn &) {
          ++saved;
          return true;
        }, false, Wizard::Mode::Import, {},
        [&](const ImportedSongData &, juce::File &, juce::String &) {
          ++resolved;
          expect(subject->isSubmissionInProgress(),
                 "destination resolution is covered by the submission guard");
          return false; // Cancellation emits no native message box.
        });
    subject = &wizard;
    wizard.setOnDismissRequested([&] { ++dismissed; });
    fillMetadata(wizard);
    wizard.handleSelectedStemFiles({files.first}, false);
    wizard.changeStep(Step::Review);
    wizard.submitSong();
    expect(resolved == 1 && saved == 0 && dismissed == 0 &&
               !wizard.isSubmissionInProgress(),
           "cancelled destination unlocks without saving or closing");
    expect(wizard.stems.size() == 1 && wizard.submitButton.isEnabled() &&
               wizard.mobileCloseButton.isEnabled(),
           "cancelled destination preserves the draft and restores actions");
  }

  static void testAsyncGuardAndSuccess(const AudioFixtures &files) {
    std::atomic<int> calls{0};
    std::atomic<bool> savedOnUi{true};
    juce::WaitableEvent finishSaving;
    int dismissed = 0;
    Wizard wizard(
        [&](ImportedSongData song, juce::String &, const SongImportProgressFn &progress) {
          savedOnUi = juce::MessageManager::getInstance()->isThisTheMessageThread();
          ++calls;
          expect(song.title == "Wizard fixture" && song.stems.size() == 1,
                 "worker receives an owned snapshot of the song draft");
          progress(0.25, "Test preparation");
          expect(finishSaving.wait(5000), "test releases the mock save worker");
          progress(1.0, "Test complete");
          return true;
        }, false);
    wizard.setOnDismissRequested([&] { ++dismissed; });
    fillMetadata(wizard);
    wizard.handleSelectedStemFiles({files.first}, false);
    wizard.changeStep(Step::Review);
    auto song = wizard.buildSongData();
    wizard.setSubmitting(true);
    expect(wizard.submitSongInBackground(std::move(song)), "start mock async save");
    pumpUntil([&] { return calls.load() == 1; }, "mock async save starts");

    wizard.submitSong();
    wizard.goToNextStep();
    wizard.backButton.onClick();
    wizard.changeStep(Step::Metadata);
    wizard.saveMetadataOnly();
    wizard.closeDialog();
    expect(wizard.currentStep == Step::Review && calls.load() == 1 && dismissed == 0,
           "pending submission rejects duplicate save, navigation and dismissal");
    expect(wizard.isSubmissionInProgress() && !wizard.submitButton.isEnabled() &&
               !wizard.backButton.isEnabled() && !wizard.mobileCloseButton.isEnabled(),
           "all closing and navigation actions stay disabled while saving");
    expect(!savedOnUi.load(), "project work executes outside the UI thread");

    finishSaving.signal();
    pumpUntil([&] { return dismissed == 1 && !wizard.isSubmissionInProgress(); },
              "successful async save returns to UI and dismisses once");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    expect(calls.load() == 1 && dismissed == 1 && !wizard.submissionOverlay &&
               wizard.submitButton.isEnabled(),
           "async completion removes progress and clears its guard exactly once");
  }
};

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr)
    return 77;
#endif
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  AudioFixtures fixtures;
  SongImportWizardTestAccess::testStepLayouts(fixtures);
  SongImportWizardTestAccess::testDraftNavigation(fixtures);
  SongImportWizardTestAccess::testCancelledDestination(fixtures);
  SongImportWizardTestAccess::testAsyncGuardAndSuccess(fixtures);
  std::cout << "Song creation wizard: all checks passed\n";
}
