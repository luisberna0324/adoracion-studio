#pragma once
#include "../../analysis/SectionDetectionService.h"
#include "../../analysis/GuideModelProvisioner.h"
#include "../UiCommon.h"

// Review-only state. Persistence remains owned by WaveformBar's existing edit
// transaction. This component never edits ImportedSongData or the processor.
class SectionDetectionPanel final : public juce::Component,
                                    private juce::ComponentListener,
                                    private juce::Timer {
public:
  struct Actions {
    std::function<void(std::vector<SongArrangementPartSnapshot>)> preview;
    std::function<void(double)> listen;
    std::function<bool(const std::vector<playworship::analysis::SectionSuggestion> &, double, juce::String &)> accept;
    std::function<void()> close;
    // The main editor owns grid alignment and review on the existing waveform.
    // Without this callback the literal transcript remains available for probes.
    std::function<void(const playworship::analysis::GuideTranscript &)> reviewGuide;
  };
  SectionDetectionPanel(ImportedSongData, Actions,
      std::shared_ptr<playworship::analysis::IMusicStructureAnalyzer> =
          playworship::analysis::createPlatformMusicStructureAnalyzer(),
      std::shared_ptr<playworship::analysis::IGuideTranscriber> =
          playworship::analysis::createWhisperGuideTranscriber());
  ~SectionDetectionPanel() override;
  // The editor retains ownership; presentation covers its top-level host.
  // Keeping this explicit leaves diagnostic component fixtures non-modal.
  void showModal(juce::Component &source);
  // Called explicitly by the editor's Sugerir secciones entry. Constructors do
  // not start work or download anything implicitly.
  void beginSuggestionFlow();
  void paint(juce::Graphics &) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &) override;
  void inputAttemptWhenModal() override;
private:
  friend struct SectionDetectionPanelTestAccess;
  struct RoleRow;
  struct SuggestionRow;
  void start(bool guideOnly = false, bool continuePreparation = false);
  void requestCancel();
  void releaseModalPresentation();
  juce::Rectangle<int> modalAvailableBounds() const;
  void componentMovedOrResized(juce::Component &, bool, bool) override;
  void componentBeingDeleted(juce::Component &) override;
  void timerCallback() override;
  void completeDetection(playworship::analysis::DetectionOutcome);
  void chooseGuideModel();
  void prepareGuideModel();
  void showTranscript(const playworship::analysis::GuideTranscript &);
  void rebuildRows();
  void updatePreview();
  void setBusy(bool);
  void addProposal();
  void accept();
  void changeBoundary(size_t index, bool start, double value);
  std::vector<double> confirmedBars() const;
  ImportedSongData song;
  Actions actions;
  playworship::analysis::SectionDetectionService service;
  std::shared_ptr<playworship::analysis::SectionDetectionTask> task;
  std::shared_ptr<playworship::analysis::GuideModelPreparationTask> modelTask;
  std::shared_ptr<playworship::analysis::IGuideModelDownloadSource> modelDownloadSource;
  playworship::analysis::GuideModelAsset modelAsset = playworship::analysis::defaultWhisperGuideModelAsset();
  playworship::analysis::SectionDetectionResult result;
  juce::File analysisCacheDirectory;
  std::vector<bool> selected;
  std::vector<std::unique_ptr<RoleRow>> roles;
  std::vector<std::unique_ptr<SuggestionRow>> rows;
  juce::Viewport viewport;
  juce::Component content;
  juce::Label title, status, help;
  juce::Label modelStatus;
  AdaptiveComboBox guideLanguage;
  juce::TextEditor transcript;
  juce::File guideModelFile;
  std::unique_ptr<juce::FileChooser> modelChooser;
  juce::TextButton transcribe, chooseModel, showTracks;
  juce::ToggleButton gridConfirmed;
  juce::TextButton analyze, cancel, add, acceptSelected, selectAll;
  double progress = -1;
  juce::ProgressBar spinner{progress};
  bool busy = false, reviewing = false, reviewModified = false, transcriptReview = false;
  bool modalPresented = false, cancelling = false;
  juce::Component::SafePointer<juce::Component> modalHost;
  juce::Rectangle<int> cardBounds, lastAvailableBounds;
  juce::uint64 operationGeneration = 0;
};
