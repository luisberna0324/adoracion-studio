#pragma once

#include "../../data/SongData.h"
#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>

class SongImportWizardComponent final : public juce::Component {
public:
  explicit SongImportWizardComponent(
      std::function<void(ImportedSongData)> onSubmitSong);
  ~SongImportWizardComponent() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void refreshThemeColours();

private:
  struct StemDraft {
    juce::File file;
    juce::String displayName;
    juce::String category;
    bool include = true;
    double durationSec = 0.0;
    int sampleRate = 0;
    int channels = 0;
  };

  class MappingRow;

  void configureTextEditor(juce::TextEditor &editor,
                           const juce::String &placeholder);
  void configureCombo(juce::ComboBox &combo);
  void layoutStepOne(juce::Rectangle<int> bounds);
  void layoutStepTwo(juce::Rectangle<int> bounds);
  void layoutStepThree(juce::Rectangle<int> bounds);
  void openStemChooser();
  void handleSelectedStemFiles(const juce::Array<juce::File> &files);
  void updateSelectedFilesText();
  void rebuildMappingRows();
  void layoutMappingRows();
  void syncMappingsToDrafts();
  void updateSummaryText();
  void goToNextStep();
  bool validateStepOne();
  bool validateStepTwo();
  ImportedSongData buildSongData();
  void submitSong();
  void changeStep(int step);
  void closeDialog();
  void applyEditorTheme(juce::TextEditor &editor,
                        const juce::String &placeholder);
  void stylePrimaryButton(juce::TextButton &button);
  void styleSecondaryButton(juce::TextButton &button);

  std::function<void(ImportedSongData)> onSubmit;
  int currentStep = 0;
  std::unique_ptr<juce::FileChooser> stemChooser;
  std::vector<StemDraft> stems;
  std::vector<std::unique_ptr<MappingRow>> mappingRows;

  juce::Label titleLabel;
  juce::Label subtitleLabel;
  juce::Label stemsSectionLabel;
  juce::Label infoSectionLabel;
  juce::Label songTitleLabel;
  juce::Label artistLabel;
  juce::Label fileTypeLabel;
  juce::Label bpmLabel;
  juce::Label keyLabel;
  juce::Label timeSignatureLabel;
  juce::Label mapFileHeader;
  juce::Label mapNameHeader;
  juce::Label mapCategoryHeader;
  juce::Label mapLoadHeader;

  juce::TextButton chooseFilesButton;
  juce::Label selectedFilesLabel;
  juce::TextEditor selectedFilesEditor;
  juce::TextEditor songTitleEditor;
  juce::TextEditor artistEditor;
  juce::TextEditor bpmEditor;
  juce::ComboBox fileTypeBox;
  juce::ComboBox keyBox;
  juce::ComboBox timeSignatureBox;

  juce::Viewport mapViewport;
  juce::Component mapContent;

  juce::TextEditor summaryEditor;

  juce::TextButton cancelButton;
  juce::TextButton backButton;
  juce::TextButton nextButton;
  juce::TextButton submitButton;

  const juce::String songTitlePlaceholder = "Song title";
  const juce::String artistPlaceholder = "Artist";
  const juce::String bpmPlaceholder = "BPM";
};
