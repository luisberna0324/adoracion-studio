#include "SongImportWizardComponent.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

class SongImportWizardComponent::MappingRow final : public juce::Component {
public:
  explicit MappingRow(const StemDraft &stem) {
    fileLabel.setText(stem.file.getFileName(), juce::dontSendNotification);
    fileLabel.setColour(juce::Label::textColourId,
                        juce::Colours::white.withAlpha(0.72f));
    fileLabel.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(fileLabel);

    displayNameEditor.setText(stem.displayName, juce::dontSendNotification);
    displayNameEditor.setColour(juce::TextEditor::backgroundColourId,
                                juce::Colour::fromRGB(48, 52, 61));
    displayNameEditor.setColour(juce::TextEditor::textColourId,
                                juce::Colours::white);
    displayNameEditor.setColour(juce::TextEditor::outlineColourId,
                                juce::Colour::fromRGB(66, 72, 84));
    addAndMakeVisible(displayNameEditor);

    for (size_t i = 0; i < kTrackCategories.size(); ++i)
      categoryBox.addItem(kTrackCategories[i], static_cast<int>(i) + 1);
    categoryBox.setText(stem.category, juce::dontSendNotification);
    categoryBox.setColour(juce::ComboBox::backgroundColourId,
                          juce::Colour::fromRGB(48, 52, 61));
    categoryBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    categoryBox.setColour(juce::ComboBox::outlineColourId,
                          juce::Colour::fromRGB(66, 72, 84));
    categoryBox.setColour(juce::ComboBox::arrowColourId,
                          juce::Colours::white.withAlpha(0.65f));
    addAndMakeVisible(categoryBox);

    includeToggle.setButtonText("Load");
    includeToggle.setToggleState(stem.include, juce::dontSendNotification);
    includeToggle.setColour(juce::ToggleButton::textColourId,
                            juce::Colours::white.withAlpha(0.86f));
    addAndMakeVisible(includeToggle);
  }

  void applyTo(StemDraft &stem) const {
    stem.displayName = displayNameEditor.getText().trim();
    stem.category = categoryBox.getText().trim();
    stem.include = includeToggle.getToggleState();
  }

  void resized() override {
    auto r = getLocalBounds().reduced(4, 3);
    auto fileCol = r.removeFromLeft(230);
    auto nameCol = r.removeFromLeft(170);
    auto catCol = r.removeFromLeft(160);

    fileLabel.setBounds(fileCol);
    displayNameEditor.setBounds(nameCol.reduced(2, 0));
    categoryBox.setBounds(catCol.reduced(2, 0));
    includeToggle.setBounds(r.reduced(8, 0));
  }

  void paint(juce::Graphics &g) override {
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 9));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 22));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f,
                           1.0f);
  }

private:
  juce::Label fileLabel;
  juce::TextEditor displayNameEditor;
  juce::ComboBox categoryBox;
  juce::ToggleButton includeToggle;
};

SongImportWizardComponent::SongImportWizardComponent(
    std::function<void(ImportedSongData)> onSubmitSong)
    : onSubmit(std::move(onSubmitSong)) {
  setSize(760, 560);

  titleLabel.setText("Add Song", juce::dontSendNotification);
  titleLabel.setFont(juce::FontOptions(30.0f));
  titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
  addAndMakeVisible(titleLabel);

  subtitleLabel.setColour(juce::Label::textColourId,
                          juce::Colours::white.withAlpha(0.65f));
  subtitleLabel.setFont(juce::FontOptions(14.0f));
  addAndMakeVisible(subtitleLabel);

  configureTextEditor(songTitleEditor, "Song title");
  configureTextEditor(artistEditor, "Artist");
  configureTextEditor(bpmEditor, "BPM");
  bpmEditor.setInputRestrictions(3, "0123456789");
  addAndMakeVisible(songTitleEditor);
  addAndMakeVisible(artistEditor);
  addAndMakeVisible(bpmEditor);

  fileTypeBox.addItem("Multitrack WAV", 1);
  fileTypeBox.addItem("Stereo", 2);
  fileTypeBox.addItem("Split", 3);
  fileTypeBox.setSelectedId(1);
  configureCombo(fileTypeBox);

  keyBox.addItemList(
      {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"},
      1);
  keyBox.setSelectedId(1);
  configureCombo(keyBox);

  timeSignatureBox.addItemList({"4/4", "3/4", "6/8"}, 1);
  timeSignatureBox.setSelectedId(1);
  configureCombo(timeSignatureBox);

  addAndMakeVisible(fileTypeBox);
  addAndMakeVisible(keyBox);
  addAndMakeVisible(timeSignatureBox);

  chooseFilesButton.setButtonText("Load stems...");
  chooseFilesButton.onClick = [this] { openStemChooser(); };
  addAndMakeVisible(chooseFilesButton);

  selectedFilesLabel.setColour(juce::Label::textColourId,
                               juce::Colours::white.withAlpha(0.70f));
  selectedFilesLabel.setText("No stems selected", juce::dontSendNotification);
  addAndMakeVisible(selectedFilesLabel);

  selectedFilesEditor.setReadOnly(true);
  selectedFilesEditor.setMultiLine(true);
  selectedFilesEditor.setColour(juce::TextEditor::backgroundColourId,
                                juce::Colour::fromRGB(42, 46, 54));
  selectedFilesEditor.setColour(juce::TextEditor::textColourId,
                                juce::Colours::white.withAlpha(0.9f));
  selectedFilesEditor.setColour(juce::TextEditor::outlineColourId,
                                juce::Colour::fromRGB(64, 69, 79));
  addAndMakeVisible(selectedFilesEditor);

  mapViewport.setViewedComponent(&mapContent, false);
  mapViewport.setScrollBarsShown(true, false, false, false);
  mapViewport.setScrollBarThickness(1);
  mapViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(mapViewport);

  summaryEditor.setReadOnly(true);
  summaryEditor.setMultiLine(true);
  summaryEditor.setColour(juce::TextEditor::backgroundColourId,
                          juce::Colour::fromRGB(42, 46, 54));
  summaryEditor.setColour(juce::TextEditor::textColourId,
                          juce::Colours::white.withAlpha(0.9f));
  summaryEditor.setColour(juce::TextEditor::outlineColourId,
                          juce::Colour::fromRGB(64, 69, 79));
  addAndMakeVisible(summaryEditor);

  cancelButton.setButtonText("Cancel");
  cancelButton.onClick = [this] { closeDialog(); };
  backButton.setButtonText("Back");
  backButton.onClick = [this] { changeStep(currentStep - 1); };
  nextButton.setButtonText("Next");
  nextButton.onClick = [this] { goToNextStep(); };
  submitButton.setButtonText("Submit track");
  submitButton.onClick = [this] { submitSong(); };

  for (auto *btn : {&cancelButton, &backButton, &nextButton, &submitButton})
    addAndMakeVisible(*btn);

  changeStep(0);
}

SongImportWizardComponent::~SongImportWizardComponent() = default;

void SongImportWizardComponent::paint(juce::Graphics &g) {
  auto b = getLocalBounds().toFloat();
  g.setColour(juce::Colour::fromRGB(24, 26, 30));
  g.fillRoundedRectangle(b, 8.0f);
  g.setColour(juce::Colour::fromRGBA(255, 255, 255, 24));
  g.drawRoundedRectangle(b.reduced(0.5f), 8.0f, 1.0f);
}

void SongImportWizardComponent::resized() {
  auto area = getLocalBounds().reduced(16);
  auto header = area.removeFromTop(74);
  titleLabel.setBounds(header.removeFromTop(42));
  subtitleLabel.setBounds(header);

  auto footer = area.removeFromBottom(46);
  auto content = area;

  cancelButton.setBounds(footer.removeFromLeft(96));
  backButton.setBounds(footer.removeFromRight(96).withTrimmedLeft(8));
  submitButton.setBounds(footer.removeFromRight(140).withTrimmedLeft(8));
  nextButton.setBounds(footer.removeFromRight(96).withTrimmedLeft(8));

  layoutStepOne(content);
  layoutStepTwo(content);
  layoutStepThree(content);
}

void SongImportWizardComponent::configureTextEditor(
    juce::TextEditor &editor, const juce::String &placeholder) {
  editor.setTextToShowWhenEmpty(placeholder,
                                juce::Colours::white.withAlpha(0.45f));
  editor.setColour(juce::TextEditor::backgroundColourId,
                   juce::Colour::fromRGB(42, 46, 54));
  editor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
  editor.setColour(juce::TextEditor::outlineColourId,
                   juce::Colour::fromRGB(64, 69, 79));
}

void SongImportWizardComponent::configureCombo(juce::ComboBox &combo) {
  combo.setColour(juce::ComboBox::backgroundColourId,
                  juce::Colour::fromRGB(42, 46, 54));
  combo.setColour(juce::ComboBox::textColourId, juce::Colours::white);
  combo.setColour(juce::ComboBox::outlineColourId,
                  juce::Colour::fromRGB(64, 69, 79));
  combo.setColour(juce::ComboBox::arrowColourId,
                  juce::Colours::white.withAlpha(0.65f));
}

void SongImportWizardComponent::layoutStepOne(juce::Rectangle<int> bounds) {
  const bool visible = currentStep == 0;
  for (auto *c : {static_cast<juce::Component *>(&chooseFilesButton),
                  static_cast<juce::Component *>(&selectedFilesLabel),
                  static_cast<juce::Component *>(&selectedFilesEditor),
                  static_cast<juce::Component *>(&songTitleEditor),
                  static_cast<juce::Component *>(&artistEditor),
                  static_cast<juce::Component *>(&bpmEditor),
                  static_cast<juce::Component *>(&fileTypeBox),
                  static_cast<juce::Component *>(&keyBox),
                  static_cast<juce::Component *>(&timeSignatureBox)}) {
    c->setVisible(visible);
  }
  if (!visible)
    return;

  auto topRow = bounds.removeFromTop(30);
  chooseFilesButton.setBounds(topRow.removeFromLeft(140));
  selectedFilesLabel.setBounds(topRow.reduced(12, 0));

  bounds.removeFromTop(8);
  selectedFilesEditor.setBounds(bounds.removeFromTop(180));
  bounds.removeFromTop(12);
  songTitleEditor.setBounds(bounds.removeFromTop(34));
  bounds.removeFromTop(8);
  artistEditor.setBounds(bounds.removeFromTop(34));
  bounds.removeFromTop(8);

  auto row = bounds.removeFromTop(34);
  fileTypeBox.setBounds(row.removeFromLeft(220));
  row.removeFromLeft(8);
  bpmEditor.setBounds(row.removeFromLeft(80));
  row.removeFromLeft(8);
  keyBox.setBounds(row.removeFromLeft(80));
  row.removeFromLeft(8);
  timeSignatureBox.setBounds(row.removeFromLeft(100));
}

void SongImportWizardComponent::layoutStepTwo(juce::Rectangle<int> bounds) {
  const bool visible = currentStep == 1;
  mapViewport.setVisible(visible);
  if (!visible)
    return;

  mapViewport.setBounds(bounds);
  layoutMappingRows();
}

void SongImportWizardComponent::layoutStepThree(juce::Rectangle<int> bounds) {
  const bool visible = currentStep == 2;
  summaryEditor.setVisible(visible);
  if (!visible)
    return;
  summaryEditor.setBounds(bounds);
}

void SongImportWizardComponent::openStemChooser() {
  auto safeThis = juce::Component::SafePointer<SongImportWizardComponent>(this);
  stemChooser = std::make_unique<juce::FileChooser>(
      "Select stems",
      juce::File::getSpecialLocation(juce::File::userHomeDirectory),
      "*.wav;*.aif;*.aiff;*.flac");

  const auto flags = juce::FileBrowserComponent::openMode |
                     juce::FileBrowserComponent::canSelectFiles |
                     juce::FileBrowserComponent::canSelectMultipleItems;

  stemChooser->launchAsync(flags, [safeThis](const juce::FileChooser &chooser) {
    if (safeThis == nullptr)
      return;
    safeThis->handleSelectedStemFiles(chooser.getResults());
    safeThis->stemChooser.reset();
  });
}

void SongImportWizardComponent::handleSelectedStemFiles(
    const juce::Array<juce::File> &files) {
  stems.clear();
  if (files.isEmpty()) {
    updateSelectedFilesText();
    return;
  }

  juce::AudioFormatManager fm;
  fm.registerBasicFormats();

  stems.reserve(static_cast<size_t>(files.size()));
  for (const auto &file : files) {
    if (!file.existsAsFile())
      continue;

    StemDraft draft;
    draft.file = file;
    draft.displayName = makeDisplayNameFromFile(file);
    draft.category =
        suggestCategoryFromFilename(file.getFileNameWithoutExtension());

    if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
            fm.createReaderFor(file))) {
      draft.sampleRate = static_cast<int>(reader->sampleRate);
      draft.channels = static_cast<int>(reader->numChannels);
      if (reader->sampleRate > 0.0)
        draft.durationSec =
            static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    }

    stems.push_back(std::move(draft));
  }

  if (songTitleEditor.getText().trim().isEmpty() && !files.isEmpty()) {
    songTitleEditor.setText(files[0].getParentDirectory().getFileName(),
                            juce::dontSendNotification);
  }

  updateSelectedFilesText();
  rebuildMappingRows();
}

void SongImportWizardComponent::updateSelectedFilesText() {
  if (stems.empty()) {
    selectedFilesLabel.setText("No stems selected", juce::dontSendNotification);
    selectedFilesEditor.setText({}, juce::dontSendNotification);
    return;
  }

  selectedFilesLabel.setText(
      juce::String(stems.size()) + " stems ready to map",
      juce::dontSendNotification);

  juce::StringArray lines;
  for (const auto &stem : stems) {
    juce::String info = stem.file.getFileName() + "  |  " + stem.category;
    if (stem.sampleRate > 0)
      info << "  |  " << stem.sampleRate << " Hz";
    if (stem.channels > 0)
      info << "  |  ch " << stem.channels;
    lines.add(info);
  }
  selectedFilesEditor.setText(lines.joinIntoString("\n"),
                              juce::dontSendNotification);
}

void SongImportWizardComponent::rebuildMappingRows() {
  mappingRows.clear();
  mapContent.removeAllChildren();

  for (const auto &stem : stems) {
    auto row = std::make_unique<MappingRow>(stem);
    mapContent.addAndMakeVisible(*row);
    mappingRows.push_back(std::move(row));
  }

  layoutMappingRows();
}

void SongImportWizardComponent::layoutMappingRows() {
  const int rowH = 42;
  const int gap = 5;
  const int width = juce::jmax(1, mapViewport.getWidth() - 10);
  int y = 6;
  for (auto &row : mappingRows) {
    row->setBounds(4, y, width, rowH);
    y += rowH + gap;
  }
  mapContent.setSize(width + 8, juce::jmax(mapViewport.getHeight(), y + 4));
}

void SongImportWizardComponent::syncMappingsToDrafts() {
  const auto count = juce::jmin(mappingRows.size(), stems.size());
  for (size_t i = 0; i < count; ++i)
    mappingRows[i]->applyTo(stems[i]);
}

void SongImportWizardComponent::updateSummaryText() {
  syncMappingsToDrafts();

  const auto included =
      std::count_if(stems.begin(), stems.end(),
                    [](const StemDraft &stem) { return stem.include; });

  juce::StringArray lines;
  lines.add("Song: " + songTitleEditor.getText().trim());
  lines.add("Artist: " + artistEditor.getText().trim());
  lines.add("BPM: " + bpmEditor.getText().trim() + "   Key: " +
            keyBox.getText() + "   Time: " + timeSignatureBox.getText());
  lines.add("File type: " + fileTypeBox.getText());
  lines.add("Stems to load: " + juce::String(static_cast<int>(included)) +
            " / " + juce::String(static_cast<int>(stems.size())));
  lines.add("");
  lines.add("Mapped stems:");

  for (const auto &stem : stems) {
    lines.add((stem.include ? "[x] " : "[ ] ") + stem.displayName + " -> " +
              stem.category + " (" + stem.file.getFileName() + ")");
  }

  summaryEditor.setText(lines.joinIntoString("\n"),
                        juce::dontSendNotification);
}

void SongImportWizardComponent::goToNextStep() {
  if (currentStep == 0 && !validateStepOne())
    return;
  if (currentStep == 1 && !validateStepTwo())
    return;
  changeStep(currentStep + 1);
}

bool SongImportWizardComponent::validateStepOne() {
  if (stems.empty()) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, "Missing stems",
        "Select at least one stem file before continuing.");
    return false;
  }

  if (songTitleEditor.getText().trim().isEmpty()) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, "Missing song title",
        "Add a song title before continuing.");
    return false;
  }

  return true;
}

bool SongImportWizardComponent::validateStepTwo() {
  syncMappingsToDrafts();
  const bool hasAnyLoaded =
      std::any_of(stems.begin(), stems.end(),
                  [](const StemDraft &stem) { return stem.include; });

  if (!hasAnyLoaded) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, "No stems selected",
        "Enable at least one stem to load.");
    return false;
  }

  return true;
}

ImportedSongData SongImportWizardComponent::buildSongData() {
  syncMappingsToDrafts();

  ImportedSongData song;
  song.id = juce::Uuid().toString();
  song.title = songTitleEditor.getText().trim();
  song.artist = artistEditor.getText().trim();
  song.bpm = bpmEditor.getText().getIntValue();
  song.key = keyBox.getText().trim();
  song.timeSignature = timeSignatureBox.getText().trim();
  song.fileType = fileTypeBox.getText().trim();
  song.createdMs = juce::Time::getCurrentTime().toMilliseconds();

  song.stems.reserve(stems.size());
  for (const auto &stem : stems) {
    ImportedStemData data;
    data.sourcePath = stem.file.getFullPathName();
    data.fileName = stem.file.getFileName();
    data.displayName = stem.displayName;
    data.category = stem.category;
    data.include = stem.include;
    data.durationSec = stem.durationSec;
    data.sampleRate = stem.sampleRate;
    data.channels = stem.channels;
    song.stems.push_back(std::move(data));
  }

  return song;
}

void SongImportWizardComponent::submitSong() {
  if (currentStep != 2)
    return;

  if (onSubmit)
    onSubmit(buildSongData());

  closeDialog();
}

void SongImportWizardComponent::changeStep(int step) {
  currentStep = juce::jlimit(0, 2, step);

  switch (currentStep) {
  case 0:
    subtitleLabel.setText("Step 1/3 - Load stems and basic track data",
                          juce::dontSendNotification);
    break;
  case 1:
    subtitleLabel.setText("Step 2/3 - Map instruments and categories",
                          juce::dontSendNotification);
    break;
  case 2:
    subtitleLabel.setText("Step 3/3 - Confirm the stems to import",
                          juce::dontSendNotification);
    updateSummaryText();
    break;
  }

  backButton.setVisible(currentStep > 0);
  nextButton.setVisible(currentStep < 2);
  submitButton.setVisible(currentStep == 2);
  resized();
  repaint();
}

void SongImportWizardComponent::closeDialog() {
  if (auto *dw = findParentComponentOfClass<juce::DialogWindow>()) {
    dw->exitModalState(0);
    dw->setVisible(false);
  }
}
