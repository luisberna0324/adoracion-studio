#include "SongImportWizardComponent.h"

#include "../UiCommon.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

class SongImportWizardComponent::MappingRow final : public juce::Component {
public:
  explicit MappingRow(const StemDraft &stem) {
    fileLabel.setText(stem.file.getFileName(), juce::dontSendNotification);
    fileLabel.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(fileLabel);

    displayNameEditor.setText(stem.displayName, juce::dontSendNotification);
    addAndMakeVisible(displayNameEditor);

    for (size_t i = 0; i < kTrackCategories.size(); ++i)
      categoryBox.addItem(kTrackCategories[i], static_cast<int>(i) + 1);
    categoryBox.setText(stem.category, juce::dontSendNotification);
    addAndMakeVisible(categoryBox);

    includeToggle.setButtonText("Load");
    includeToggle.setToggleState(stem.include, juce::dontSendNotification);
    addAndMakeVisible(includeToggle);

    refreshThemeColours();
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
    g.setColour(UITheme::cardBgHover.withAlpha(0.9f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.8f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f,
                           1.0f);
  }

  void refreshThemeColours() {
    fileLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
    displayNameEditor.setColour(juce::TextEditor::backgroundColourId,
                                UITheme::cardBg);
    displayNameEditor.setColour(juce::TextEditor::textColourId,
                                UITheme::textPrimary);
    displayNameEditor.setColour(juce::TextEditor::outlineColourId,
                                UITheme::cardBorder.withAlpha(0.7f));
    categoryBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBg);
    categoryBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
    categoryBox.setColour(juce::ComboBox::outlineColourId,
                          UITheme::cardBorder.withAlpha(0.7f));
    categoryBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
    includeToggle.setColour(juce::ToggleButton::textColourId,
                            UITheme::textPrimary);
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
  addAndMakeVisible(titleLabel);

  subtitleLabel.setFont(juce::FontOptions(14.0f));
  addAndMakeVisible(subtitleLabel);

  stemsSectionLabel.setText("Stems", juce::dontSendNotification);
  stemsSectionLabel.setFont(juce::FontOptions(12.0f));
  stemsSectionLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(stemsSectionLabel);

  infoSectionLabel.setText("Song Info", juce::dontSendNotification);
  infoSectionLabel.setFont(juce::FontOptions(12.0f));
  infoSectionLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(infoSectionLabel);

  songTitleLabel.setText("Title", juce::dontSendNotification);
  songTitleLabel.setFont(juce::FontOptions(11.0f));
  songTitleLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(songTitleLabel);

  artistLabel.setText("Artist", juce::dontSendNotification);
  artistLabel.setFont(juce::FontOptions(11.0f));
  artistLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(artistLabel);

  fileTypeLabel.setText("File type", juce::dontSendNotification);
  fileTypeLabel.setFont(juce::FontOptions(11.0f));
  fileTypeLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(fileTypeLabel);

  bpmLabel.setText("BPM", juce::dontSendNotification);
  bpmLabel.setFont(juce::FontOptions(11.0f));
  bpmLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(bpmLabel);

  keyLabel.setText("Key", juce::dontSendNotification);
  keyLabel.setFont(juce::FontOptions(11.0f));
  keyLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(keyLabel);

  timeSignatureLabel.setText("Time", juce::dontSendNotification);
  timeSignatureLabel.setFont(juce::FontOptions(11.0f));
  timeSignatureLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(timeSignatureLabel);

  mapFileHeader.setText("File", juce::dontSendNotification);
  mapFileHeader.setFont(juce::FontOptions(11.0f));
  mapFileHeader.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(mapFileHeader);

  mapNameHeader.setText("Display name", juce::dontSendNotification);
  mapNameHeader.setFont(juce::FontOptions(11.0f));
  mapNameHeader.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(mapNameHeader);

  mapCategoryHeader.setText("Category", juce::dontSendNotification);
  mapCategoryHeader.setFont(juce::FontOptions(11.0f));
  mapCategoryHeader.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(mapCategoryHeader);

  mapLoadHeader.setText("Load", juce::dontSendNotification);
  mapLoadHeader.setFont(juce::FontOptions(11.0f));
  mapLoadHeader.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(mapLoadHeader);

  configureTextEditor(songTitleEditor, songTitlePlaceholder);
  configureTextEditor(artistEditor, artistPlaceholder);
  configureTextEditor(bpmEditor, bpmPlaceholder);
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

  selectedFilesLabel.setText("No stems selected", juce::dontSendNotification);
  addAndMakeVisible(selectedFilesLabel);

  selectedFilesEditor.setReadOnly(true);
  selectedFilesEditor.setMultiLine(true);
  addAndMakeVisible(selectedFilesEditor);

  mapViewport.setViewedComponent(&mapContent, false);
  mapViewport.setScrollBarsShown(true, false, false, false);
  mapViewport.setScrollBarThickness(1);
  mapViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(mapViewport);

  summaryEditor.setReadOnly(true);
  summaryEditor.setMultiLine(true);
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

  refreshThemeColours();
  changeStep(0);
}

SongImportWizardComponent::~SongImportWizardComponent() = default;

void SongImportWizardComponent::paint(juce::Graphics &g) {
  auto b = getLocalBounds().toFloat();
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(b, 8.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.8f));
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

  auto rightEdge = footer.getRight();
  const int buttonH = footer.getHeight();
  const int y = footer.getY();
  const int gap = 8;
  auto placeRight = [&](juce::Component &button, int w) {
    button.setBounds(rightEdge - w, y, w, buttonH);
    rightEdge -= (w + gap);
  };

  if (submitButton.isVisible())
    placeRight(submitButton, 140);
  else
    placeRight(nextButton, 96);

  if (backButton.isVisible())
    placeRight(backButton, 96);

  layoutStepOne(content);
  layoutStepTwo(content);
  layoutStepThree(content);
}

void SongImportWizardComponent::refreshThemeColours() {
  titleLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  subtitleLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  for (auto *label :
       {&stemsSectionLabel, &infoSectionLabel, &songTitleLabel, &artistLabel,
        &fileTypeLabel, &bpmLabel, &keyLabel, &timeSignatureLabel,
        &mapFileHeader, &mapNameHeader, &mapCategoryHeader, &mapLoadHeader}) {
    label->setColour(juce::Label::textColourId, UITheme::textSecondary);
  }

  applyEditorTheme(songTitleEditor, songTitlePlaceholder);
  applyEditorTheme(artistEditor, artistPlaceholder);
  applyEditorTheme(bpmEditor, bpmPlaceholder);

  selectedFilesLabel.setColour(juce::Label::textColourId,
                               UITheme::textSecondary);
  selectedFilesEditor.setColour(juce::TextEditor::backgroundColourId,
                                UITheme::cardBgHover);
  selectedFilesEditor.setColour(juce::TextEditor::textColourId,
                                UITheme::textPrimary);
  selectedFilesEditor.setColour(juce::TextEditor::outlineColourId,
                                UITheme::cardBorder.withAlpha(0.7f));

  summaryEditor.setColour(juce::TextEditor::backgroundColourId,
                          UITheme::cardBgHover);
  summaryEditor.setColour(juce::TextEditor::textColourId,
                          UITheme::textPrimary);
  summaryEditor.setColour(juce::TextEditor::outlineColourId,
                          UITheme::cardBorder.withAlpha(0.7f));

  configureCombo(fileTypeBox);
  configureCombo(keyBox);
  configureCombo(timeSignatureBox);

  styleSecondaryButton(chooseFilesButton);
  styleSecondaryButton(cancelButton);
  styleSecondaryButton(backButton);
  stylePrimaryButton(nextButton);
  stylePrimaryButton(submitButton);

  for (auto &row : mappingRows)
    row->refreshThemeColours();

  repaint();
}

void SongImportWizardComponent::configureTextEditor(
    juce::TextEditor &editor, const juce::String &placeholder) {
  applyEditorTheme(editor, placeholder);
}

void SongImportWizardComponent::configureCombo(juce::ComboBox &combo) {
  combo.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBgHover);
  combo.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
  combo.setColour(juce::ComboBox::outlineColourId,
                  UITheme::cardBorder.withAlpha(0.7f));
  combo.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
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
                  static_cast<juce::Component *>(&timeSignatureBox),
                  static_cast<juce::Component *>(&stemsSectionLabel),
                  static_cast<juce::Component *>(&infoSectionLabel),
                  static_cast<juce::Component *>(&songTitleLabel),
                  static_cast<juce::Component *>(&artistLabel),
                  static_cast<juce::Component *>(&fileTypeLabel),
                  static_cast<juce::Component *>(&bpmLabel),
                  static_cast<juce::Component *>(&keyLabel),
                  static_cast<juce::Component *>(&timeSignatureLabel)}) {
    c->setVisible(visible);
  }
  if (!visible)
    return;

  const int sectionLabelH = 18;
  const int fieldLabelH = 16;
  const int fieldH = 30;
  const int rowGap = 6;

  auto stemHeader = bounds.removeFromTop(sectionLabelH);
  stemsSectionLabel.setBounds(stemHeader.removeFromLeft(200));
  chooseFilesButton.setBounds(stemHeader.removeFromRight(140));

  bounds.removeFromTop(4);
  selectedFilesLabel.setBounds(bounds.removeFromTop(18));
  bounds.removeFromTop(6);
  selectedFilesEditor.setBounds(bounds.removeFromTop(150));

  bounds.removeFromTop(12);
  infoSectionLabel.setBounds(bounds.removeFromTop(sectionLabelH));
  bounds.removeFromTop(6);

  auto infoArea = bounds;
  const int leftW =
      juce::jlimit(240, 320, static_cast<int>(infoArea.getWidth() * 0.48f));
  auto left = infoArea.removeFromLeft(leftW);
  auto right = infoArea;

  auto labelRow = left.removeFromTop(fieldLabelH);
  songTitleLabel.setBounds(labelRow);
  songTitleEditor.setBounds(left.removeFromTop(fieldH));
  left.removeFromTop(rowGap);

  labelRow = left.removeFromTop(fieldLabelH);
  artistLabel.setBounds(labelRow);
  artistEditor.setBounds(left.removeFromTop(fieldH));

  const int rightRowH = fieldLabelH + fieldH;
  auto rightRow = right.removeFromTop(rightRowH);
  const int gap = 10;
  auto colW = (rightRow.getWidth() - gap) / 2;
  auto col = rightRow.removeFromLeft(colW);
  fileTypeLabel.setBounds(col.removeFromTop(fieldLabelH));
  fileTypeBox.setBounds(col.removeFromTop(fieldH));
  rightRow.removeFromLeft(gap);
  col = rightRow.removeFromLeft(colW);
  bpmLabel.setBounds(col.removeFromTop(fieldLabelH));
  bpmEditor.setBounds(col.removeFromTop(fieldH));

  right.removeFromTop(rowGap);
  rightRow = right.removeFromTop(rightRowH);
  col = rightRow.removeFromLeft(colW);
  keyLabel.setBounds(col.removeFromTop(fieldLabelH));
  keyBox.setBounds(col.removeFromTop(fieldH));
  rightRow.removeFromLeft(gap);
  col = rightRow.removeFromLeft(colW);
  timeSignatureLabel.setBounds(col.removeFromTop(fieldLabelH));
  timeSignatureBox.setBounds(col.removeFromTop(fieldH));
}

void SongImportWizardComponent::layoutStepTwo(juce::Rectangle<int> bounds) {
  const bool visible = currentStep == 1;
  mapViewport.setVisible(visible);
  for (auto *c : {static_cast<juce::Component *>(&mapFileHeader),
                  static_cast<juce::Component *>(&mapNameHeader),
                  static_cast<juce::Component *>(&mapCategoryHeader),
                  static_cast<juce::Component *>(&mapLoadHeader)}) {
    c->setVisible(visible);
  }
  if (!visible)
    return;

  auto header = bounds.removeFromTop(20);
  auto fileCol = header.removeFromLeft(230);
  auto nameCol = header.removeFromLeft(170);
  auto catCol = header.removeFromLeft(160);
  mapFileHeader.setBounds(fileCol);
  mapNameHeader.setBounds(nameCol);
  mapCategoryHeader.setBounds(catCol);
  mapLoadHeader.setBounds(header.reduced(8, 0));

  bounds.removeFromTop(6);
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
  auto safeThis =
      juce::Component::SafePointer<SongImportWizardComponent>(this);
  stemChooser = std::make_unique<juce::FileChooser>(
      "Select stems",
      juce::File::getSpecialLocation(juce::File::userHomeDirectory),
      "*.wav;*.aif;*.aiff;*.flac;*.mp3");

  const auto chooserFlags = juce::FileBrowserComponent::openMode |
                           juce::FileBrowserComponent::canSelectFiles |
                           juce::FileBrowserComponent::canSelectMultipleItems;

  stemChooser->launchAsync(
      chooserFlags, [safeThis](const juce::FileChooser &chooser) {
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
    selectedFilesLabel.setText("No stems selected",
                               juce::dontSendNotification);
    selectedFilesEditor.setText({}, juce::dontSendNotification);
    return;
  }

  selectedFilesLabel.setText(juce::String(stems.size()) +
                                 " stems ready to map",
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
    data.stemId = juce::Uuid().toString();
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

void SongImportWizardComponent::applyEditorTheme(
    juce::TextEditor &editor, const juce::String &placeholder) {
  editor.setTextToShowWhenEmpty(placeholder,
                                UITheme::textSecondary.withAlpha(0.6f));
  editor.setColour(juce::TextEditor::backgroundColourId, UITheme::cardBgHover);
  editor.setColour(juce::TextEditor::textColourId, UITheme::textPrimary);
  editor.setColour(juce::TextEditor::outlineColourId,
                   UITheme::cardBorder.withAlpha(0.7f));
}

void SongImportWizardComponent::stylePrimaryButton(juce::TextButton &button) {
  button.setColour(juce::TextButton::buttonColourId, UITheme::accent);
  button.setColour(juce::TextButton::buttonOnColourId, UITheme::accentSoft);
  button.setColour(juce::TextButton::textColourOffId, UITheme::onAccent);
  button.setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
}

void SongImportWizardComponent::styleSecondaryButton(juce::TextButton &button) {
  button.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  button.setColour(juce::TextButton::buttonOnColourId, UITheme::cardBgPressed);
  button.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  button.setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);
}
