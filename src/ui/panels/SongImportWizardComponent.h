#pragma once

#include "../../data/PrimeLibrarySource.h"
#include "../../data/SongData.h"
#include "../../data/SongLibraryStore.h"
#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>

class SongImportWizardComponent final : public juce::Component {
public:
  enum class Mode { Import = 0, Edit };

  explicit SongImportWizardComponent(
      std::function<bool(ImportedSongData, juce::String &,
                         const SongImportProgressFn &)>
          onSubmitSong,
      bool primeImportEnabled, Mode mode = Mode::Import,
      ImportedSongData initialSong = {});
  ~SongImportWizardComponent() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void refreshThemeColours();

private:
  enum class Step { Import = 0, Metadata, Mapping, Review };

  struct StemDraft {
    juce::File file;
    juce::String sourcePath;
    juce::String storedPath;
    juce::String stemId;
    juce::String displayName;
    juce::String category;
    bool fromExistingSong = false;
    bool include = true;
    double durationSec = 0.0;
    int sampleRate = 0;
    int channels = 0;
  };

  class MappingRow;
  class PrimeSongListModel;
  class StemImportListModel;
  class StemListRowComponent;

  void configureTextEditor(juce::TextEditor &editor,
                           const juce::String &placeholder);
  void configureCombo(juce::ComboBox &combo);
  void layoutStepImport(juce::Rectangle<int> bounds);
  void layoutStepMetadata(juce::Rectangle<int> bounds);
  void layoutStepMapping(juce::Rectangle<int> bounds);
  void layoutStepReview(juce::Rectangle<int> bounds);
  void openStemChooser(bool replaceCurrent);
  void handleSelectedStemFiles(const juce::Array<juce::File> &files,
                               bool replaceCurrent);
  void preloadFromExistingSong();
  void updateSelectedFilesText();
  void removeStemAtIndex(int rowIndex);
  void rebuildMappingRows();
  void layoutMappingRows();
  void reloadPrimeSongs();
  void useSelectedPrimeSong();
  void applyPrimeSong(size_t index);
  void syncMappingsToDrafts();
  void updateSummaryText();
  void goToNextStep();
  bool validateStepImport();
  bool validateStepMetadata();
  bool validateStepMapping();
  ImportedSongData buildSongData();
  void submitSong();
  void changeStep(Step step);
  void closeDialog();
  juce::String stepSubtitle(Step step) const;
  void applyEditorTheme(juce::TextEditor &editor,
                        const juce::String &placeholder);
  void stylePrimaryButton(juce::TextButton &button);
  void styleSecondaryButton(juce::TextButton &button);

  std::function<bool(ImportedSongData, juce::String &,
                     const SongImportProgressFn &)>
      onSubmit;
  Mode mode = Mode::Import;
  ImportedSongData initialSong;
  bool primeImportEnabled = false;
  Step currentStep = Step::Import;
  std::unique_ptr<juce::FileChooser> stemChooser;
  std::vector<StemDraft> stems;
  std::vector<std::unique_ptr<MappingRow>> mappingRows;
  std::vector<PrimeSongData> primeSongs;
  std::unique_ptr<PrimeSongListModel> primeSongListModel;
  std::unique_ptr<StemImportListModel> stemImportListModel;

  juce::Label titleLabel;
  juce::Label subtitleLabel;
  juce::Label stemsSectionLabel;
  juce::Label primeSectionLabel;
  juce::Label primeStatusLabel;
  juce::Label infoSectionLabel;
  juce::Label songTitleLabel;
  juce::Label artistLabel;
  juce::Label bpmLabel;
  juce::Label keyLabel;
  juce::Label timeSignatureLabel;
  juce::Label mapFileHeader;
  juce::Label mapNameHeader;
  juce::Label mapCategoryHeader;
  juce::Label mapLoadHeader;

  juce::TextButton addFilesButton;
  juce::TextButton replaceFilesButton;
  juce::TextButton refreshPrimeButton;
  juce::TextButton usePrimeSongButton;
  juce::ListBox primeSongsList;
  juce::Label selectedFilesLabel;
  juce::ListBox stemsList;
  juce::TextEditor songTitleEditor;
  juce::TextEditor artistEditor;
  juce::TextEditor bpmEditor;
  juce::ComboBox keyBox;
  juce::ComboBox timeSignatureBox;

  juce::Viewport mapViewport;
  juce::Component mapContent;

  juce::TextEditor summaryEditor;

  juce::TextButton cancelButton;
  juce::TextButton backButton;
  juce::TextButton nextButton;
  juce::TextButton submitButton;

};
