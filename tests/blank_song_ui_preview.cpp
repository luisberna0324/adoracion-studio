#include "ui/panels/SongImportWizardComponent.h"
#include "ui/panels/WaveformBar.h"
#include "data/ApplicationDataPaths.h"
#include <iostream>

struct SongImportWizardTestAccess {
  static void run(const juce::File &directory) {
    SongImportWizardComponent wizard({});
    wizard.setEmbeddedLibraryPage(true);
    wizard.songKindBox.setSelectedId(2, juce::sendNotificationSync);
    wizard.songTitleEditor.setText("Momento instrumental", false);
    wizard.artistEditor.setText("PlayWorship", false);
    wizard.bpmEditor.setText("120", false);
    wizard.keyBox.setSelectedId(3, juce::dontSendNotification);
    wizard.timeSignatureBox.setSelectedId(1, juce::dontSendNotification);
    wizard.blankClickButton.setToggleState(true, juce::dontSendNotification);
    wizard.blankPadButton.setToggleState(true, juce::dontSendNotification);
    auto capture = [&](const juce::String &name, int width, int height) {
      wizard.setSize(width, height);
      auto image = wizard.createComponentSnapshot(wizard.getLocalBounds());
      const auto file = directory.getChildFile(name + ".png");
      file.deleteFile();
      juce::FileOutputStream stream(file);
      if (!stream.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, stream)) throw std::runtime_error("capture failed");
    };
    wizard.changeStep(SongImportWizardComponent::Step::Metadata);
    capture("blank-details-mobile", 390, 860);
    capture("blank-details-desktop", 1000, 860);
    wizard.changeStep(SongImportWizardComponent::Step::Stems);
    capture("blank-free-mobile", 390, 760);
    wizard.blankFormBox.setSelectedId(2, juce::sendNotificationSync);
    wizard.addBlankSection(); wizard.addBlankSection();
    const char *names[] = {"Intro", "Verso", "Coro"};
    const char *counts[] = {"4", "8", "8"};
    for (int index = 0; index < 3; ++index) {
      auto *row = wizard.blankSectionsContent.getChildComponent(index);
      dynamic_cast<juce::TextEditor *>(row->getChildComponent(0))->setText(names[index], false);
      dynamic_cast<juce::TextEditor *>(row->getChildComponent(1))->setText(counts[index], false);
    }
    capture("blank-sections-mobile", 390, 760);
    capture("blank-sections-desktop", 1000, 760);
    auto song = wizard.buildSongData();
    wizard.changeStep(SongImportWizardComponent::Step::Review);
    capture("blank-review-mobile", 390, 760);
    WaveformBar timeline;
    double position = 12.0;
    timeline.setBlankSong(true); timeline.setWaveformBins({});
    timeline.setSongRhythm(song.bpm, song.timeSignature);
    timeline.setTransportProviders([&] { return position; }, [&] { return estimateSongLengthSeconds(song); }, [] { return true; });
    timeline.setArrangement(song.arrangement, true);
    timeline.setLiveMode(true);
    for (int width : {390, 1000}) {
      timeline.setMinimalPhoneMode(width < 600);
      timeline.setSize(width, 190);
      auto image = timeline.createComponentSnapshot(timeline.getLocalBounds());
      const auto file = directory.getChildFile("blank-live-" + juce::String(width) + ".png");
      file.deleteFile();
      juce::FileOutputStream stream(file);
      if (!stream.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, stream)) throw std::runtime_error("timeline capture failed");
    }
  }
};

int main(int argc, char **argv) {
  // Refuse to inspect or write ordinary user application data.
  const auto root = ApplicationDataPaths::isolatedTestRoot();
  if (!root.isDirectory()) return 77;
  juce::ScopedJuceInitialiser_GUI gui;
  applyTheme(ThemePreset::Spotify);
  if (argc < 2) { std::cerr << "Usage: blank-song-preview <capture-directory>\n"; return 2; }
  juce::File directory(argv[1]);
  directory.createDirectory();
  SongImportWizardTestAccess::run(directory);
  std::cout << "Captured real JUCE components at " << directory.getFullPathName() << '\n';
}
