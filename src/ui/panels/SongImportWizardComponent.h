#pragma once

#include "../../data/SongData.h"
#include "../../data/SongLibraryStore.h"
#include "../../data/SecuenciasClient.h"
#include "../UiCommon.h"
#include <JuceHeader.h>

#include <functional>
#include <atomic>
#include <memory>
#include <vector>
#include <optional>
#include <unordered_map>
#include <unordered_set>

class SongLyricsPanel;
class SongLoadProgressOverlay;
class MobileImportStaging;

class SongImportWizardComponent final : public juce::Component,
                                        private juce::Timer {
public:
  enum class Mode { Import = 0, Edit };

  explicit SongImportWizardComponent(
      std::function<bool(ImportedSongData, juce::String &,
                         const SongImportProgressFn &)>
          onSubmitSong,
      Mode mode = Mode::Import,
      ImportedSongData initialSong = {},
      std::function<bool(const ImportedSongData &, juce::File &,
                         juce::String &)> projectDestinationResolver = {},
      std::function<bool(ImportedSongData, juce::String &)>
          onSaveMetadataOnly = {});
  ~SongImportWizardComponent() override;

  void paint(juce::Graphics &g) override;
  void paintOverChildren(juce::Graphics &g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &event) override;
  void mouseMove(const juce::MouseEvent &event) override;
  void mouseExit(const juce::MouseEvent &event) override;
  void mouseUp(const juce::MouseEvent &event) override;
  void refreshThemeColours();
  void setOnDismissRequested(std::function<void()> callback);
  void setEmbeddedLibraryPage(bool enabled);
  bool isSubmissionInProgress() const { return isSubmitting; }

private:
  friend struct SongImportWizardTestAccess;
  enum class Step { Metadata = 0, Stems, Mapping, Review };

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
  class StemImportListModel;
  class StemListRowComponent;
  class SecuenciasResultListModel;

  void configureTextEditor(juce::TextEditor &editor,
                           const juce::String &placeholder);
  void configureCombo(AdaptiveComboBox &combo);
  void layoutStepImport(juce::Rectangle<int> bounds);
  void layoutStepMetadata(juce::Rectangle<int> bounds);
  void layoutStepMapping(juce::Rectangle<int> bounds);
  void layoutStepReview(juce::Rectangle<int> bounds);
  void openStemChooser(bool replaceCurrent);
  void handleSelectedStemFiles(const juce::Array<juce::File> &files,
                               bool replaceCurrent);
#if JUCE_IOS || JUCE_ANDROID || MTCONTROLS_MAC_APP_STORE
  void stageSelectedMobileFiles(juce::Array<juce::URL> urls,
                                bool replaceCurrent, bool artwork);
#endif
  void preloadFromExistingSong();
  void updateSelectedFilesText();
  void removeStemAtIndex(int rowIndex);
  void rebuildMappingRows();
  void layoutMappingRows();
  void syncMappingsToDrafts();
  void updateSummaryText();
  void goToNextStep();
  void saveMetadataOnly();
  bool validateStepImport();
  bool validateStepMetadata();
  bool validateStepMapping();
  ImportedSongData buildSongData();
  void submitSong();
  void createBlankSong();
  bool submitSongInBackground(ImportedSongData song);
  void setSubmitting(bool submitting);
  void changeStep(Step step);
  void closeDialog();
  juce::String stepSubtitle(Step step) const;
  void applyEditorTheme(juce::TextEditor &editor,
                        const juce::String &placeholder);
  void styleSecondaryButton(juce::TextButton &button);
  bool usesPhoneCreationLayout() const;
  bool usesCreationLayout() const {
    return phoneCreationLayout ||
           (mode == Mode::Import &&
            (embeddedLibraryPage || !useTouchOptimisedControls()));
  }
  void timerCallback() override;
  void revealFocusedMappingEditor();
  void revealFocusedMetadataEditor();
  std::vector<juce::TextEditor *> currentFormEditors();
  bool focusAdjacentEditor(int direction);
  void advanceTextEditor();
  void finishTextEditing();
  void updateKeyboardNavigation();
  juce::BorderSize<int> keyboardInsetsForLayout();
  void paintPhoneMetadata(juce::Graphics &g);
  juce::Point<int> metadataMousePosition(const juce::MouseEvent &event) const;

  class MetadataViewport final : public juce::Viewport {
  public:
    std::function<void()> onScroll;
    void visibleAreaChanged(const juce::Rectangle<int> &) override {
      if (onScroll) onScroll();
    }
  };

  void hideSecuenciasSuggestions(bool resetStatus);
  void handleSecuenciasResult(const std::vector<SongSearchResult>& results, const SecuenciasError& err);
  void fetchSecuenciasDetailForSelected();
  void applySecuenciasMetadata(const SongSearchResult& result, const SongDetail* detail);
  void openArtworkChooser();
  void openLyricsEditor();
  void updateLyricsAction();

  juce::Image getCachedArtwork(const juce::String &url) const;
  void requestArtworkAsync(const juce::String &url);


  std::function<bool(ImportedSongData, juce::String &,
                     const SongImportProgressFn &)>
      onSubmit;
  std::function<bool(const ImportedSongData &, juce::File &, juce::String &)>
      resolveProjectDestination;
  std::function<bool(ImportedSongData, juce::String &)> onMetadataOnlySubmit;
  std::function<void()> onDismissRequested;
  Mode mode = Mode::Import;
  ImportedSongData initialSong;
  bool isSubmitting = false;
  std::unique_ptr<SongLoadProgressOverlay> submissionOverlay;
  std::unique_ptr<SongLyricsPanel> lyricsPanel;
  std::optional<SongLyricsDocument> lyricsDraft;
  Step currentStep = Step::Metadata;
  std::unique_ptr<juce::FileChooser> stemChooser;
  std::unique_ptr<juce::FileChooser> artworkChooser;
  std::shared_ptr<MobileImportStaging> mobileImportStaging;
  std::unordered_map<std::string, juce::File> stagedMobileFiles;
  std::vector<StemDraft> stems;
  std::vector<std::unique_ptr<MappingRow>> mappingRows;
  std::unique_ptr<StemImportListModel> stemImportListModel;

  juce::Label titleLabel;
  juce::Label subtitleLabel;
  juce::Label stemsSectionLabel;
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
  juce::Label selectedFilesLabel;
  juce::ListBox stemsList;
  juce::TextEditor songTitleEditor;
  juce::TextEditor artistEditor;
  juce::TextEditor bpmEditor;
  AdaptiveComboBox keyBox;
  AdaptiveComboBox timeSignatureBox;

  MetadataViewport metadataViewport;
  juce::Component metadataContent;
  juce::TextButton keyboardDoneButton;
  juce::TextButton keyboardPreviousButton;
  juce::TextButton keyboardNextButton;
  juce::Component::SafePointer<juce::Component> lastMetadataFocus;
  int metadataScrollTarget = 0;
  bool metadataScrollAnimating = false;

  juce::Viewport mapViewport;
  juce::Component mapContent;
  juce::Component::SafePointer<juce::Component> lastMappingFocus;

  juce::TextEditor summaryEditor;

  juce::TextButton cancelButton;
  juce::TextButton backButton;
  juce::TextButton nextButton;
  juce::TextButton submitButton;
  juce::TextButton saveMetadataButton;
  juce::TextButton lyricsButton;
  std::unique_ptr<juce::Button> blankSongMetadataAction;
  bool creatingBlankSong = false;
  juce::TextButton clearSongSearchButton;

  SecuenciasClient secuenciasClient;
  std::unique_ptr<SecuenciasResultListModel> secuenciasListModel;
  int secuenciasSearchToken = 0;
  int latestSearchRequestId = 0;
  int latestDetailRequestId = 0;
  bool suppressTitleAutocomplete = false;
  mutable juce::CriticalSection artworkLock;
  std::unordered_map<std::string, juce::Image> artworkCache;
  std::unordered_set<std::string> artworkInFlight;
  std::vector<SongSearchResult> secuenciasResults;
  std::optional<SongDetail> selectedSongDetail;
  int secuenciasSelectedRow = -1;
  juce::String selectedArtworkUrl;
  bool artworkOverlayHovered = false;
  juce::Rectangle<int> artworkOverlayButtonBounds;

  juce::Label secuenciasStatusLabel;
  juce::ImageComponent secuenciasArtworkPreview;
  juce::Label secuenciasArtworkCaption;
  juce::Label changeArtworkLabel;
  juce::Label secuenciasDetailLabel;
  juce::ListBox secuenciasList;

  HeaderIconButton mobileCloseButton{"songWizardClose", IconGlyph::Close};
  std::unique_ptr<juce::LookAndFeel_V4> mobilePrimaryButtonLookAndFeel;
  bool phoneCreationLayout = false;
  bool embeddedLibraryPage = false;
  bool phoneKeyboardVisible = false;
  juce::BorderSize<int> phoneKeyboardInsets;
  juce::uint32 keyboardLastVisibleTime = 0;
  juce::Rectangle<int> stepperBounds;
  juce::Rectangle<int> metadataCardBounds;

};
