#include "ui/panels/SongImportWizardComponent.h"
#include "ui/panels/SongListPanel.h"
#include "data/SongProjectCreation.h"
#include "data/ProjectStore.h"
#include "data/ApplicationDataPaths.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <utility>

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
  juce::File directory = ApplicationDataPaths::isolatedTestRoot()
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

struct SongListPanelTestAccess {
  static SongImportWizardComponent &clickLibraryPlus(SongListPanel &panel) {
    // Follow the real Biblioteca header button, including its production callback.
    panel.viewMode = SongListPanel::ViewMode::Songs;
    panel.showPage(1, false);
    panel.openLibraryBrowser();
    for (auto *child : panel.libraryCatalogPanel.getChildren()) {
      if (child->getName() != "createSong")
        continue;
      auto *button = dynamic_cast<juce::Button *>(child);
      expect(button != nullptr, "Biblioteca has its + button");
      button->onClick();
      expect(panel.libraryCreationWizard != nullptr && panel.songImportOverlay == nullptr,
             "Biblioteca + opens an internal page instead of an editor overlay");
      return *panel.libraryCreationWizard;
    }
    expect(false, "find Biblioteca + entry point");
    std::abort();
  }

  static bool returnedToLibrary(SongListPanel &panel) {
    return panel.libraryCreationWizard == nullptr &&
           panel.libraryCatalogPanel.getParentComponent()->isVisible();
  }

  static bool animatingBackToLibrary(SongListPanel &panel) {
    return panel.libraryCreationWizard != nullptr &&
           panel.libraryCreationWizard->getParentComponent()->isVisible() &&
           panel.libraryCatalogPanel.getParentComponent()->isVisible();
  }

  static SongImportWizardComponent &openEditor(SongListPanel &panel,
                                                const juce::String &songId) {
    panel.selectedSongId = songId;
    panel.nowPlayingSongId = songId;
    panel.openSongEditWizard(songId);
    std::function<SongImportWizardComponent *(juce::Component &)> findWizard;
    findWizard = [&findWizard](juce::Component &component)
        -> SongImportWizardComponent * {
      if (auto *wizard = dynamic_cast<SongImportWizardComponent *>(&component))
        return wizard;
      for (auto *child : component.getChildren())
        if (auto *wizard = findWizard(*child))
          return wizard;
      return nullptr;
    };
    auto *wizard = findWizard(panel);
    expect(wizard != nullptr, "song row Edit opens the actual wizard");
    return *wizard;
  }

  static void clearSelection(SongListPanel &panel) {
    panel.selectedSongId.clear();
    panel.nowPlayingSongId.clear();
  }
};

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

    for (const auto size : {juce::Point<int>(860, 640), juce::Point<int>(600, 500),
                            juce::Point<int>(340, 680)}) {
      wizard.setSize(size.x, size.y);
      wizard.changeStep(Step::Metadata);
      expect(wizard.metadataViewport.isVisible() &&
                 wizard.songTitleEditor.getParentComponent() == &wizard.metadataContent &&
                 !wizard.stepperBounds.isEmpty() && !wizard.subtitleLabel.isVisible(),
             "desktop uses the mobile form and four-step indicator");
      expect(wizard.songTitleEditor.getHeight() == 36,
             "desktop adapts only control size");
      expectInside(wizard.metadataViewport, wizard.getLocalBounds(),
                   "desktop metadata viewport fits the dialog");
      expectInside(wizard.nextButton, wizard.getLocalBounds(),
                   "desktop navigation remains reachable");
      wizard.changeStep(Step::Stems);
      expect(wizard.stemsList.getRowHeight() == 48,
             "desktop file rows use compact controls");
      wizard.changeStep(Step::Mapping);
      auto &row = mappingRow(wizard);
      auto &name = childOf<juce::TextEditor>(row);
      auto &category = childOf<juce::ComboBox>(row);
      expect(row.getHeight() == 80 && name.getHeight() == 36 &&
                 category.getHeight() == 36 && name.getY() > 32,
             "desktop uses the same stacked mapping at desktop control sizes");
      expectInside(category, row.getLocalBounds(), "desktop category fits its row");
      wizard.changeStep(Step::Review);
      expectInside(wizard.summaryEditor, wizard.getLocalBounds(),
                   "desktop review fits the dialog");
    }
  }

  static void testDesktopEditLayout() {
    Wizard wizard({}, false, Wizard::Mode::Edit);
    wizard.setSize(860, 640);
    expect(!wizard.metadataViewport.isVisible() &&
               wizard.songTitleEditor.getParentComponent() == &wizard &&
               wizard.secuenciasArtworkPreview.getX() > wizard.songTitleEditor.getRight(),
           "desktop editing retains its existing layout");
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
        }, false, Wizard::Mode::Import, {},
        [&](const ImportedSongData &, juce::File &destination, juce::String &) {
          destination = files.directory.getChildFile("mock.pwproj");
          return true;
        });
    wizard.setOnDismissRequested([&] { ++dismissed; });
    fillMetadata(wizard);
    wizard.handleSelectedStemFiles({files.first}, false);
    wizard.changeStep(Step::Review);
    wizard.submitSong();
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
  static void testDesktopCreationRoundtrip(const AudioFixtures &files) {
    const auto root = ApplicationDataPaths::isolatedTestRoot();
    expect(SongLibraryStore::getRootDirectory().isAChildOf(root),
           "wizard integration storage is isolated from the user library");
    const auto source = files.directory.getChildFile("portable.flac");
    {
      juce::FlacAudioFormat format;
      std::unique_ptr<juce::OutputStream> stream = source.createOutputStream();
      auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
          .withSampleRate(44100).withNumChannels(1).withBitsPerSample(16));
      expect(writer != nullptr, "create portable audio fixture");
      juce::AudioBuffer<float> audio(1, 4096);
      audio.clear();
      audio.setSample(0, 16, 0.25f);
      expect(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()),
             "write portable audio fixture");
    }
    int dismissed = 0;
    std::atomic<double> reported{0.0};
    juce::String saveError;
    Wizard wizard([&](ImportedSongData song, juce::String &error,
                      const SongImportProgressFn &progress) {
      const bool ok = SongProjectCreation::save(std::move(song), error,
          [&](double value, const juce::String &phase) {
            reported = value;
            progress(value, phase);
          });
      saveError = error;
      return ok;
    }, false, Wizard::Mode::Import, {}, SongProjectCreation::resolveDestination);
    wizard.setSize(860, 640);
    wizard.setOnDismissRequested([&] { ++dismissed; });
    fillMetadata(wizard);
    wizard.goToNextStep();
    expect(wizard.currentStep == Step::Stems, "Detalles advances to Archivos");
    wizard.handleSelectedStemFiles({source}, false);
    wizard.goToNextStep();
    expect(wizard.currentStep == Step::Mapping, "Archivos advances to Secciones");
    childOf<juce::TextEditor>(mappingRow(wizard)).setText("Preserved piano",
                                                         juce::dontSendNotification);
    wizard.goToNextStep();
    expect(wizard.currentStep == Step::Review, "Secciones advances to Revisar");
    wizard.backButton.onClick();
    wizard.backButton.onClick();
    wizard.backButton.onClick();
    expect(wizard.currentStep == Step::Metadata &&
               wizard.songTitleEditor.getText() == "Wizard fixture",
           "returning through all screens preserves metadata");
    wizard.goToNextStep();
    wizard.goToNextStep();
    expect(childOf<juce::TextEditor>(mappingRow(wizard)).getText() == "Preserved piano",
           "returning forward preserves track mapping");
    wizard.goToNextStep();
    expect(wizard.summaryEditor.getText().contains(".pwproj"),
           "desktop review explains local project creation");
    const auto screenshots = juce::SystemStats::getEnvironmentVariable(
        "PLAYWORSHIP_WIZARD_SCREENSHOTS", {});
    if (screenshots.isNotEmpty()) {
      const juce::File directory(screenshots);
      expect(directory.createDirectory().wasOk(), "create screenshot directory");
      int index = 0;
      for (const auto step : {Step::Metadata, Step::Stems, Step::Mapping, Step::Review}) {
        wizard.changeStep(step);
        const auto snapshot = wizard.createComponentSnapshot(wizard.getLocalBounds());
        auto output = directory.getChildFile(juce::String(++index) + ".png").createOutputStream();
        expect(output != nullptr && juce::PNGImageFormat{}.writeImageToStream(snapshot, *output),
               "render actual desktop wizard screen");
      }
    }
    wizard.submitSong();
    expect(wizard.isSubmissionInProgress() && wizard.submissionOverlay != nullptr,
           "actual desktop submission displays background progress");
    pumpUntil([&] { return !wizard.isSubmissionInProgress(); },
              "actual project save completes");
    expect(saveError.isEmpty() && dismissed == 1 && reported == 1.0,
           "wizard closes only after successful persistence and final progress");
    const auto library = SongLibraryStore::loadLibrarySnapshot();
    expect(library.songs.size() == 1, "created song is visible in reloaded Biblioteca");
    const auto &registered = library.songs.front();
    const juce::File project(registered.projectPath);
    expect(project.existsAsFile() && project.hasFileExtension("pwproj") &&
               project.isAChildOf(SongLibraryStore::getRootDirectory().getChildFile("projects")),
           "wizard creates the actual pwproj in local Biblioteca");
    expect(source.deleteFile(), "remove only test source to prove package independence");
    ProjectDocument reopened;
    juce::String error;
    expect(ProjectStore::open(project, reopened, &error, true),
           "project reopens from Biblioteca without the original source");
    expect(reopened.song.title == "Wizard fixture" && reopened.song.stems.size() == 1 &&
               reopened.song.stems.front().displayName == "Preserved piano",
           "reopened package retains draft metadata and mapping");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(
        juce::File(reopened.song.stems.front().storedPath)));
    expect(reader != nullptr && reader->lengthInSamples == 4096,
           "reopened project has playable embedded audio");
    juce::File nextDestination;
    expect(SongProjectCreation::resolveDestination(reopened.song, nextDestination, error) &&
               nextDestination != project, "same-title creation never overwrites existing project");
    std::cout << "Verified wizard -> " << project.getFullPathName()
              << " -> Biblioteca -> reopened audio\n";
  }

  static void testProjectEditingRoundtrip(const AudioFixtures &files) {
    auto songs = SongLibraryStore::loadSongs();
    expect(songs.size() == 1, "edit fixture uses the created portable song");
    const auto songId = songs.front().id;
    const juce::File project(songs.front().projectPath);
    auto makeCover = [&files](const char *name, juce::Colour colour) {
      const auto file = files.directory.getChildFile(name);
      juce::Image image(juce::Image::RGB, 16, 16, true);
      juce::Graphics graphics(image);
      graphics.fillAll(colour);
      auto output = file.createOutputStream();
      expect(output != nullptr &&
                 juce::PNGImageFormat{}.writeImageToStream(image, *output),
             "create a real cover image");
      return file;
    };
    const auto firstCover = makeCover("edited-first.png", juce::Colours::red);
    const auto secondCover = makeCover("edited-second.png", juce::Colours::blue);

    auto edit = [&](const juce::File &cover, const juce::String &title,
                    const juce::String &bpm, bool quickSave) {
      int dismissed = 0;
      juce::String saveError;
      Wizard wizard(
          [&](ImportedSongData draft, juce::String &error,
              const SongImportProgressFn &progress) {
            const bool saved = SongLibraryStore::updateSong(std::move(draft),
                                                             &error, progress);
            saveError = error;
            return saved;
          },
          false, Wizard::Mode::Edit, SongLibraryStore::loadSongs().front(),
          {}, [&](ImportedSongData draft, juce::String &error) {
            return SongLibraryStore::updateSongMetadata(std::move(draft), &error);
          });
      wizard.setOnDismissRequested([&] { ++dismissed; });
      wizard.songTitleEditor.setText(title, juce::dontSendNotification);
      wizard.bpmEditor.setText(bpm, juce::dontSendNotification);
      wizard.selectedArtworkUrl = "manual:" + cover.getFullPathName();
      if (quickSave) {
        wizard.saveMetadataOnly();
      } else {
        wizard.changeStep(Step::Mapping);
        childOf<juce::TextEditor>(mappingRow(wizard)).setText(
            "Edited piano", juce::dontSendNotification);
        wizard.changeStep(Step::Review);
        wizard.submitSong();
      }
      pumpUntil([&] { return dismissed == 1 && !wizard.isSubmissionInProgress(); },
                "portable edit completes and closes the wizard");
      expect(saveError.isEmpty(), "portable edit reports no persistence error");
      const auto library = SongLibraryStore::loadSongs();
      expect(library.size() == 1 && library.front().id == songId &&
                 library.front().title == title &&
                 library.front().bpm == bpm.getIntValue() &&
                 library.front().tempoMap.regions.size() == 1 &&
                 std::abs(library.front().tempoMap.regions.front().bpm -
                          bpm.getIntValue()) < 0.01,
             "edited metadata persists under the same library identity");
      const auto artwork = juce::ImageFileFormat::loadFrom(
          juce::File(library.front().artworkPath));
      expect(artwork.isValid() &&
                 artwork.getPixelAt(4, 4) ==
                     (cover == firstCover ? juce::Colours::red : juce::Colours::blue),
             "library artwork matches the newly chosen cover");
      ProjectDocument reopened;
      juce::String error;
      expect(ProjectStore::open(project, reopened, &error, true),
             "edited portable package reopens");
      const auto packagedArtwork = juce::ImageFileFormat::loadFrom(
          juce::File(reopened.song.artworkPath));
      expect(reopened.song.title == title &&
                 reopened.song.bpm == bpm.getIntValue() &&
                 packagedArtwork.isValid() &&
                 packagedArtwork.getPixelAt(4, 4) == artwork.getPixelAt(4, 4),
             "portable manifest and embedded artwork match the library");
      if (!quickSave)
        expect(reopened.song.stems.front().displayName == "Edited piano",
               "full edit persists the mapped stem name inside the project");
    };

    edit(firstCover, "Edited once", "132", true);
    ProjectSongSettings settings;
    expect(ProjectStore::getCachedSettings(project, settings) &&
               settings.tempoBpm == 132.0,
           "changing the original BPM updates playback tempo when it was unchanged");
    edit(secondCover, "Edited twice", "144", false);
    expect(ProjectStore::getCachedSettings(project, settings) &&
               settings.tempoBpm == 144.0,
           "full edit also preserves the new BPM in project settings");
  }

  static void testLibraryPanelFlow() {
    juce::Component host;
    host.setSize(1200, 900);
    SongListPanel panel;
    host.addAndMakeVisible(panel);
    panel.setBounds(40, 40, 380, 780);
    auto &cancelled = SongListPanelTestAccess::clickLibraryPlus(panel);
    expect(cancelled.getParentComponent()->getParentComponent()->getParentComponent() == &panel,
           "creation is owned by a page inside the same song panel");
    expect(cancelled.getLocalBounds() == panel.getLocalBounds() && host.getNumChildComponents() == 1,
           "wizard uses the library panel dimensions, without a second surface");
    cancelled.closeDialog();
    pumpUntil([&] { return SongListPanelTestAccess::animatingBackToLibrary(panel); },
              "closing keeps both pages visible during the return slide");
    pumpUntil([&] { return SongListPanelTestAccess::returnedToLibrary(panel); },
              "cancelling immediately during the transition returns to Biblioteca");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(350);
    expect(SongListPanelTestAccess::returnedToLibrary(panel),
           "an old animation cannot hide Biblioteca after cancellation");

    const auto existing = SongLibraryStore::loadSongs();
    expect(existing.size() == 1 && !existing.front().stems.empty(),
           "use the previous test's embedded audio for panel creation");
    auto &wizard = SongListPanelTestAccess::clickLibraryPlus(panel);
    juce::Component::SafePointer<Wizard> pending(&wizard);
    panel.setSize(420, 680);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(350);
    expect(wizard.getLocalBounds() == panel.getLocalBounds(),
           "embedded creation follows panel resizing after the transition ends");
    fillMetadata(wizard);
    const auto screenshots = juce::SystemStats::getEnvironmentVariable(
        "PLAYWORSHIP_WIZARD_SCREENSHOTS", {});
    if (screenshots.isNotEmpty()) {
      auto output = juce::File(screenshots).getChildFile("library-plus.png").createOutputStream();
      expect(output != nullptr && juce::PNGImageFormat{}.writeImageToStream(
                 panel.createComponentSnapshot(panel.getLocalBounds()), *output),
             "render the wizard inside the actual Biblioteca panel");
    }
    wizard.songTitleEditor.setText("Created from Biblioteca +", juce::dontSendNotification);
    wizard.goToNextStep();
    wizard.handleSelectedStemFiles({juce::File(existing.front().stems.front().storedPath)}, false);
    wizard.goToNextStep();
    wizard.goToNextStep();
    expect(wizard.currentStep == Step::Review, "panel + reaches all four wizard screens");
    wizard.submitSong();
    expect(wizard.isSubmissionInProgress(), "panel save starts with guarded progress");
    pumpUntil([&] { return pending == nullptr; }, "successful panel creation closes its page");
    expect(SongListPanelTestAccess::returnedToLibrary(panel),
           "successful save returns to the same Biblioteca page");
    const auto songs = SongLibraryStore::loadSongs();
    const auto saved = std::find_if(songs.begin(), songs.end(), [](const ImportedSongData &song) {
      return song.title == "Created from Biblioteca +";
    });
    expect(saved != songs.end(), "song created from + is in Biblioteca");
    ProjectDocument reopened;
    juce::String error;
    expect(ProjectStore::open(juce::File(saved->projectPath), reopened, &error),
           "project created by the real panel callback reopens");
  }

  static void testLegacyMetadataEditing(const AudioFixtures &files) {
    const auto originalCover = files.directory.getChildFile("legacy-original.png");
    const auto newCover = files.directory.getChildFile("legacy-replacement.png");
    for (const auto &entry : {std::pair{originalCover, juce::Colours::red},
                              std::pair{newCover, juce::Colours::blue}}) {
      juce::Image image(juce::Image::RGB, 12, 12, true);
      juce::Graphics graphics(image);
      graphics.fillAll(entry.second);
      auto output = entry.first.createOutputStream();
      expect(output != nullptr &&
                 juce::PNGImageFormat{}.writeImageToStream(image, *output),
             "create legacy artwork fixtures");
    }
    ImportedSongData draft;
    draft.id = juce::Uuid().toString();
    draft.title = "Legacy edit fixture";
    draft.bpm = 120;
    draft.artworkSource = "manual:" + originalCover.getFullPathName();
    ImportedStemData stem;
    stem.stemId = juce::Uuid().toString();
    stem.sourcePath = files.first.getFullPathName();
    stem.fileName = files.first.getFileName();
    stem.displayName = "Piano";
    stem.category = "Piano";
    stem.durationSec = 256.0 / 44100.0;
    stem.sampleRate = 44100;
    stem.channels = 1;
    draft.stems.push_back(stem);
    juce::String error;
    expect(SongLibraryStore::appendSong(draft, &error),
           "create a legacy song with an embedded local cover");
    const auto activeSetlistId =
        SongLibraryStore::loadLibrarySnapshot().activeSetlistId;
    expect(SongLibraryStore::addSongToSetlist(activeSetlistId, draft.id),
           "add legacy edit fixture to the active setlist");
    expect(originalCover.deleteFile(), "remove the original artwork selection");

    auto persisted = SongLibraryStore::loadSongs().back();
    persisted.title = "Edited without changing cover";
    expect(SongLibraryStore::updateSongMetadata(persisted, &error),
           "metadata edit keeps a cover when its original picker file vanished");
    persisted = SongLibraryStore::loadSongs().back();
    expect(juce::ImageFileFormat::loadFrom(juce::File(persisted.artworkPath)).isValid(),
           "self-copy of saved artwork does not delete the source");

    persisted.artworkSource = "manual:" + newCover.getFullPathName();
    persisted.bpm = 132;
    expect(SongLibraryStore::updateSongMetadata(persisted, &error),
           "replace legacy cover and original BPM");
    persisted = SongLibraryStore::loadSongs().back();
    const auto replaced = juce::ImageFileFormat::loadFrom(
        juce::File(persisted.artworkPath));
    double playbackBpm = 0.0;
    expect(replaced.isValid(), "replacement legacy cover is decodable");
    expect(replaced.getPixelAt(3, 3) == juce::Colours::blue,
           "replacement legacy cover has the newly selected pixels");
    expect(persisted.tempoMap.regions.size() == 1,
           "legacy default tempo map still has one region");
    expect(std::abs(persisted.tempoMap.regions.front().bpm - 132.0) < 0.01,
           "legacy default tempo map follows the edited original BPM");
    const bool hasPlaybackOverride =
        SongLibraryStore::getActiveSetlistTempoBpm(draft.id, playbackBpm);
    expect(!hasPlaybackOverride || std::abs(playbackBpm - 132.0) < 0.01,
           "unmodified playback tempo follows the edited original BPM");
    expect(SongLibraryStore::updateTempoBpmForActiveSetlist(draft.id, 145.0),
           "set a custom playback tempo");
    persisted.bpm = 160;
    expect(SongLibraryStore::updateSongMetadata(persisted, &error) &&
               SongLibraryStore::getActiveSetlistTempoBpm(draft.id, playbackBpm) &&
               playbackBpm == 145.0,
           "editing original BPM retains a user-customized playback tempo");
  }

  static void testLoadedSongEditNotification() {
    const auto songs = SongLibraryStore::loadSongs();
    const auto songId = songs.back().id;
    juce::Component host;
    host.setSize(1000, 760);
    SongListPanel panel;
    host.addAndMakeVisible(panel);
    panel.setBounds(host.getLocalBounds());
    int editStarted = 0, editReloaded = 0, ordinaryActivation = 0;
    panel.setOnSongEditStarted([&](const juce::String &id) {
      expect(id == songId, "editing begins for the requested song");
      ++editStarted;
    });
    panel.setOnSongEdited([&](const ImportedSongData &song, int) {
      expect(song.id == songId && song.title == "Edited through song row",
             "loaded-song edit callback receives fresh library metadata");
      ++editReloaded;
    });
    panel.setOnSongActivated([&](const ImportedSongData &, int) {
      ++ordinaryActivation;
    });
    auto &wizard = SongListPanelTestAccess::openEditor(panel, songId);
    SongListPanelTestAccess::clearSelection(panel);
    wizard.songTitleEditor.setText("Edited through song row",
                                   juce::dontSendNotification);
    wizard.saveMetadataOnly();
    expect(editStarted == 1 && editReloaded == 1 && ordinaryActivation == 0,
           "editing a loaded song uses the dedicated reload callback");
  }

};

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr)
    return 77;
#endif
  if (juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_CREATION_TEST_ROOT", {}).isEmpty())
    return 77;
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  AudioFixtures fixtures;
  SongImportWizardTestAccess::testStepLayouts(fixtures);
  SongImportWizardTestAccess::testDesktopEditLayout();
  SongImportWizardTestAccess::testDraftNavigation(fixtures);
  SongImportWizardTestAccess::testCancelledDestination(fixtures);
  SongImportWizardTestAccess::testAsyncGuardAndSuccess(fixtures);
  SongImportWizardTestAccess::testDesktopCreationRoundtrip(fixtures);
  SongImportWizardTestAccess::testProjectEditingRoundtrip(fixtures);
  SongImportWizardTestAccess::testLibraryPanelFlow();
  SongImportWizardTestAccess::testLegacyMetadataEditing(fixtures);
  SongImportWizardTestAccess::testLoadedSongEditNotification();
  std::cout << "Song creation wizard: all checks passed\n";
}
