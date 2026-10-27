#pragma once
#include "../../analysis/SectionDetectionService.h"
#include "../UiCommon.h"

// Review-only state. Persistence remains owned by WaveformBar's existing edit
// transaction. This component never edits ImportedSongData or the processor.
class SectionDetectionPanel final : public juce::Component {
public:
  struct Actions {
    std::function<void(std::vector<SongArrangementPartSnapshot>)> preview;
    std::function<void(double)> listen;
    std::function<bool(const std::vector<playworship::analysis::SectionSuggestion> &, double, juce::String &)> accept;
    std::function<void()> close;
  };
  SectionDetectionPanel(ImportedSongData, Actions,
      std::shared_ptr<playworship::analysis::IMusicStructureAnalyzer> =
          playworship::analysis::createPlatformMusicStructureAnalyzer());
  ~SectionDetectionPanel() override;
  void paint(juce::Graphics &) override;
  void resized() override;
private:
  friend struct SectionDetectionPanelTestAccess;
  struct RoleRow;
  struct SuggestionRow;
  void start();
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
  playworship::analysis::SectionDetectionResult result;
  juce::File analysisCacheDirectory;
  std::vector<bool> selected;
  std::vector<std::unique_ptr<RoleRow>> roles;
  std::vector<std::unique_ptr<SuggestionRow>> rows;
  juce::Viewport viewport;
  juce::Component content;
  juce::Label title, status, help;
  juce::ToggleButton gridConfirmed;
  juce::TextButton analyze, cancel, add, acceptSelected, selectAll;
  double progress = -1;
  juce::ProgressBar spinner{progress};
  bool busy = false, reviewing = false, reviewModified = false;
};
