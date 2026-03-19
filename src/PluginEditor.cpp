#include "PluginEditor.h"
#include "data/SongData.h"
#include "ui/UiCommon.h"
#include "ui/panels/ChannelGridPanel.h"
#include "ui/panels/MixerFaderPanel.h"
#include "ui/panels/TopBar.h"
#include "ui/panels/WaveformBar.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

namespace {
float dbToNorm(float db) {
  return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

float linearToNorm(float linear) {
  const float db =
      juce::Decibels::gainToDecibels(juce::jmax(0.00001f, linear), -100.0f);
  return dbToNorm(db);
}

constexpr int kTopBarH = 60;
constexpr int kWaveformH = 100;
constexpr int kWaveformExpandedH = 160;

class SettingsPanel final : public juce::Component {
public:
  using ThemeChanged = std::function<void(ThemePreset)>;
  using CloseRequested = std::function<void()>;

  SettingsPanel(ThemePreset current, ThemeChanged cb, CloseRequested closeCb)
      : onThemeChanged(std::move(cb)), onClose(std::move(closeCb)) {
    title.setText("Settings", juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centredLeft);
    title.setFont(juce::FontOptions(16.0f).withKerningFactor(0.04f));
    addAndMakeVisible(title);

    themeLabel.setText("Skin", juce::dontSendNotification);
    themeLabel.setJustificationType(juce::Justification::centredLeft);
    themeLabel.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(themeLabel);

    themeBox.addItem("Default", static_cast<int>(ThemePreset::Default));
    themeBox.addItem("Spotify", static_cast<int>(ThemePreset::Spotify));
    themeBox.addItem("Ableton", static_cast<int>(ThemePreset::Ableton));
    themeBox.setSelectedId(static_cast<int>(current));
    themeBox.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(themeBox);

    closeButton.setButtonText("CLOSE");
    closeButton.setClickingTogglesState(false);
    closeButton.setViewportIgnoreDragFlag(true);
    closeButton.onClick = [this] { closeSelf(); };
    addAndMakeVisible(closeButton);

    themeBox.onChange = [this] {
      const auto id = themeBox.getSelectedId();
      if (id <= 0 || !onThemeChanged)
        return;
      onThemeChanged(static_cast<ThemePreset>(id));
    };
  }

  void paint(juce::Graphics &g) override {
    auto b = getLocalBounds().toFloat();
    g.setColour(UITheme::cardBg);
    g.fillRoundedRectangle(b, 6.0f);
    g.setColour(UITheme::cardBorder);
    g.drawRoundedRectangle(b, 6.0f, 1.0f);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(16);
    title.setBounds(area.removeFromTop(28));
    area.removeFromTop(10);
    auto row = area.removeFromTop(26);
    themeLabel.setBounds(row.removeFromLeft(60));
    themeBox.setBounds(row);
    area.removeFromTop(14);
    closeButton.setBounds(area.removeFromTop(28).removeFromRight(90));
  }

  void refreshThemeColours() {
    title.setColour(juce::Label::textColourId, UITheme::textPrimary);
    themeLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
    themeBox.setColour(juce::ComboBox::backgroundColourId,
                       UITheme::cardBgHover);
    themeBox.setColour(juce::ComboBox::outlineColourId,
                       UITheme::cardBorder.withAlpha(0.65f));
    themeBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
    themeBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
    closeButton.setColour(juce::TextButton::buttonColourId,
                          UITheme::cardBgHover);
    closeButton.setColour(juce::TextButton::buttonOnColourId,
                          UITheme::cardBgPressed);
    closeButton.setColour(juce::TextButton::textColourOffId,
                          UITheme::textPrimary);
    closeButton.setColour(juce::TextButton::textColourOnId,
                          UITheme::textPrimary);
    repaint();
  }

private:
  juce::Label title;
  juce::Label themeLabel;
  juce::ComboBox themeBox;
  juce::TextButton closeButton;
  ThemeChanged onThemeChanged;
  CloseRequested onClose;

  void closeSelf() {
    if (onClose)
      onClose();
    if (auto *dw = findParentComponentOfClass<juce::DialogWindow>()) {
      dw->exitModalState(0);
      dw->setVisible(false);
    }
  }
  };

class SongLibraryStore {
public:
  static std::vector<ImportedSongData> loadSongs() {
    std::vector<ImportedSongData> songs;

    const auto dbFile = getDbFile();
    if (!dbFile.existsAsFile())
      return songs;

    const auto parsed = juce::JSON::parse(dbFile.loadFileAsString());
    auto *rootObj = parsed.getDynamicObject();
    if (rootObj == nullptr)
      return songs;

    auto *songsArray = rootObj->getProperty("songs").getArray();
    if (songsArray == nullptr)
      return songs;

    songs.reserve(static_cast<size_t>(songsArray->size()));
    for (const auto &songVar : *songsArray)
      songs.push_back(songFromVar(songVar));

    return songs;
  }

  static bool appendSong(ImportedSongData song) {
    copyStemFiles(song);

    auto songs = loadSongs();
    songs.push_back(std::move(song));
    return saveSongs(songs);
  }

  static bool saveWaveformCache(const juce::String &songId,
                                const std::vector<float> &bins) {
    if (songId.isEmpty() || bins.empty())
      return false;
    auto file = getWaveformCacheDir().getChildFile(songId + ".bin");
    juce::FileOutputStream out(file);
    if (out.failedToOpen())
      return false;
    out.write(bins.data(), bins.size() * sizeof(float));
    return true;
  }

  static std::vector<float> loadWaveformCache(const juce::String &songId) {
    if (songId.isEmpty())
      return {};
    auto file = getWaveformCacheDir().getChildFile(songId + ".bin");
    if (!file.existsAsFile())
      return {};
    const auto size = file.getSize();
    if (size < static_cast<juce::int64>(sizeof(float)) ||
        (size % sizeof(float)) != 0)
      return {};
    std::vector<float> bins(static_cast<size_t>(size / sizeof(float)));
    juce::FileInputStream in(file);
    if (in.failedToOpen())
      return {};
    in.read(bins.data(), static_cast<int>(size));
    return bins;
  }

private:
  static juce::File getRootDirectory() {
    auto root =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("MTControls");
    root.createDirectory();
    root.getChildFile("audio").createDirectory();
    return root;
  }

  static juce::File getDbFile() {
    return getRootDirectory().getChildFile("songs.json");
  }

  static juce::File getAudioRoot() {
    auto audioRoot = getRootDirectory().getChildFile("audio");
    audioRoot.createDirectory();
    return audioRoot;
  }

  static juce::File getWaveformCacheDir() {
    auto dir = getRootDirectory().getChildFile("waveforms");
    dir.createDirectory();
    return dir;
  }

  static bool saveSongs(const std::vector<ImportedSongData> &songs) {
    juce::Array<juce::var> songVars;
    songVars.ensureStorageAllocated(static_cast<int>(songs.size()));
    for (const auto &song : songs)
      songVars.add(songToVar(song));

    juce::var rootVar(new juce::DynamicObject());
    if (auto *root = rootVar.getDynamicObject()) {
      root->setProperty("version", 1);
      root->setProperty("songs", juce::var(songVars));
    }

    const auto json = juce::JSON::toString(rootVar, true);
    return getDbFile().replaceWithText(json);
  }

  static void copyStemFiles(ImportedSongData &song) {
    if (song.id.isEmpty())
      song.id = juce::Uuid().toString();

    auto songDir = getAudioRoot().getChildFile(song.id);
    songDir.createDirectory();

    for (size_t i = 0; i < song.stems.size(); ++i) {
      auto &stem = song.stems[i];
      if (!stem.include || stem.sourcePath.isEmpty())
        continue;

      const juce::File src(stem.sourcePath);
      if (!src.existsAsFile())
        continue;

      const auto baseName =
          juce::File::createLegalFileName(src.getFileNameWithoutExtension());
      auto dst = songDir.getNonexistentChildFile(
          baseName.isEmpty()
              ? juce::String("stem_") + juce::String(static_cast<int>(i) + 1)
              : baseName,
          src.getFileExtension(), false);

      if (src.copyFileTo(dst))
        stem.storedPath = dst.getFullPathName();
    }
  }

  static juce::var stemToVar(const ImportedStemData &stem) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("sourcePath", stem.sourcePath);
    obj->setProperty("storedPath", stem.storedPath);
    obj->setProperty("fileName", stem.fileName);
    obj->setProperty("displayName", stem.displayName);
    obj->setProperty("category", stem.category);
    obj->setProperty("include", stem.include);
    obj->setProperty("durationSec", stem.durationSec);
    obj->setProperty("sampleRate", stem.sampleRate);
    obj->setProperty("channels", stem.channels);
    return juce::var(obj);
  }

  static ImportedStemData stemFromVar(const juce::var &v) {
    ImportedStemData stem;
    auto *obj = v.getDynamicObject();
    if (obj == nullptr)
      return stem;

    stem.sourcePath = obj->getProperty("sourcePath").toString();
    stem.storedPath = obj->getProperty("storedPath").toString();
    stem.fileName = obj->getProperty("fileName").toString();
    stem.displayName = obj->getProperty("displayName").toString();
    stem.category = obj->getProperty("category").toString();
    stem.include = static_cast<bool>(obj->getProperty("include"));
    stem.durationSec = static_cast<double>(obj->getProperty("durationSec"));
    stem.sampleRate = static_cast<int>(obj->getProperty("sampleRate"));
    stem.channels = static_cast<int>(obj->getProperty("channels"));
    return stem;
  }

  static juce::var songToVar(const ImportedSongData &song) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("id", song.id);
    obj->setProperty("title", song.title);
    obj->setProperty("artist", song.artist);
    obj->setProperty("bpm", song.bpm);
    obj->setProperty("key", song.key);
    obj->setProperty("timeSignature", song.timeSignature);
    obj->setProperty("fileType", song.fileType);
    obj->setProperty("createdMs", static_cast<juce::int64>(song.createdMs));

    juce::Array<juce::var> stemVars;
    stemVars.ensureStorageAllocated(static_cast<int>(song.stems.size()));
    for (const auto &stem : song.stems)
      stemVars.add(stemToVar(stem));
    obj->setProperty("stems", juce::var(stemVars));
    return juce::var(obj);
  }

  static ImportedSongData songFromVar(const juce::var &v) {
    ImportedSongData song;
    auto *obj = v.getDynamicObject();
    if (obj == nullptr)
      return song;

    song.id = obj->getProperty("id").toString();
    song.title = obj->getProperty("title").toString();
    song.artist = obj->getProperty("artist").toString();
    song.bpm = static_cast<int>(obj->getProperty("bpm"));
    song.key = obj->getProperty("key").toString();
    song.timeSignature = obj->getProperty("timeSignature").toString();
    song.fileType = obj->getProperty("fileType").toString();
    song.createdMs = static_cast<juce::int64>(obj->getProperty("createdMs"));

    auto *stemsArray = obj->getProperty("stems").getArray();
    if (stemsArray != nullptr) {
      song.stems.reserve(static_cast<size_t>(stemsArray->size()));
      for (const auto &stemVar : *stemsArray)
        song.stems.push_back(stemFromVar(stemVar));
    }

    return song;
  }
};

class SongImportWizardComponent final : public juce::Component {
public:
  explicit SongImportWizardComponent(
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
        {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"}, 1);
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

  void paint(juce::Graphics &g) override {
    auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour::fromRGB(24, 26, 30));
    g.fillRoundedRectangle(b, 8.0f);
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 24));
    g.drawRoundedRectangle(b.reduced(0.5f), 8.0f, 1.0f);
  }

  void resized() override {
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

  class MappingRow final : public juce::Component {
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

  void configureTextEditor(juce::TextEditor &editor,
                           const juce::String &placeholder) {
    editor.setTextToShowWhenEmpty(placeholder,
                                  juce::Colours::white.withAlpha(0.45f));
    editor.setColour(juce::TextEditor::backgroundColourId,
                     juce::Colour::fromRGB(42, 46, 54));
    editor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    editor.setColour(juce::TextEditor::outlineColourId,
                     juce::Colour::fromRGB(64, 69, 79));
  }

  void configureCombo(juce::ComboBox &combo) {
    combo.setColour(juce::ComboBox::backgroundColourId,
                    juce::Colour::fromRGB(42, 46, 54));
    combo.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    combo.setColour(juce::ComboBox::outlineColourId,
                    juce::Colour::fromRGB(64, 69, 79));
    combo.setColour(juce::ComboBox::arrowColourId,
                    juce::Colours::white.withAlpha(0.65f));
  }

  void layoutStepOne(juce::Rectangle<int> bounds) {
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

  void layoutStepTwo(juce::Rectangle<int> bounds) {
    const bool visible = currentStep == 1;
    mapViewport.setVisible(visible);
    if (!visible)
      return;

    mapViewport.setBounds(bounds);
    layoutMappingRows();
  }

  void layoutStepThree(juce::Rectangle<int> bounds) {
    const bool visible = currentStep == 2;
    summaryEditor.setVisible(visible);
    if (!visible)
      return;
    summaryEditor.setBounds(bounds);
  }

  void openStemChooser() {
    auto safeThis =
        juce::Component::SafePointer<SongImportWizardComponent>(this);
    stemChooser = std::make_unique<juce::FileChooser>(
        "Select stems",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav;*.aif;*.aiff;*.flac");

    const auto flags = juce::FileBrowserComponent::openMode |
                       juce::FileBrowserComponent::canSelectFiles |
                       juce::FileBrowserComponent::canSelectMultipleItems;

    stemChooser->launchAsync(
        flags, [safeThis](const juce::FileChooser &chooser) {
          if (safeThis == nullptr)
            return;
          safeThis->handleSelectedStemFiles(chooser.getResults());
          safeThis->stemChooser.reset();
        });
  }

  void handleSelectedStemFiles(const juce::Array<juce::File> &files) {
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

  void updateSelectedFilesText() {
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

  void rebuildMappingRows() {
    mappingRows.clear();
    mapContent.removeAllChildren();

    for (const auto &stem : stems) {
      auto row = std::make_unique<MappingRow>(stem);
      mapContent.addAndMakeVisible(*row);
      mappingRows.push_back(std::move(row));
    }

    layoutMappingRows();
  }

  void layoutMappingRows() {
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

  void syncMappingsToDrafts() {
    const auto count = juce::jmin(mappingRows.size(), stems.size());
    for (size_t i = 0; i < count; ++i)
      mappingRows[i]->applyTo(stems[i]);
  }

  void updateSummaryText() {
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

  void goToNextStep() {
    if (currentStep == 0 && !validateStepOne())
      return;
    if (currentStep == 1 && !validateStepTwo())
      return;
    changeStep(currentStep + 1);
  }

  bool validateStepOne() {
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

  bool validateStepTwo() {
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

  ImportedSongData buildSongData() {
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

  void submitSong() {
    if (currentStep != 2)
      return;

    if (onSubmit)
      onSubmit(buildSongData());

    closeDialog();
  }

  void changeStep(int step) {
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

  void closeDialog() {
    if (auto *dw = findParentComponentOfClass<juce::DialogWindow>()) {
      dw->exitModalState(0);
      dw->setVisible(false);
    }
  }

  std::function<void(ImportedSongData)> onSubmit;
  int currentStep = 0;
  std::unique_ptr<juce::FileChooser> stemChooser;
  std::vector<StemDraft> stems;
  std::vector<std::unique_ptr<MappingRow>> mappingRows;

  juce::Label titleLabel;
  juce::Label subtitleLabel;

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
};
} // namespace

class MTControlsAudioProcessorEditor::SetlistHandlePanel final
    : public juce::Component {
public:
  explicit SetlistHandlePanel(juce::String setlistName)
      : title(std::move(setlistName).toUpperCase()) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }

  std::function<void()> onToggle;

  void setCollapsed(bool collapsedState) {
    if (isCollapsed == collapsedState)
      return;
    isCollapsed = collapsedState;
    repaint();
  }

  void paint(juce::Graphics &g) override {
    auto b = getLocalBounds().toFloat();
    auto fill = UITheme::accent.withMultipliedAlpha(isHovered ? 0.95f : 0.88f);
    if (isMouseDown)
      fill = UITheme::accentSoft;

    g.setColour(fill);
    g.fillRoundedRectangle(b.reduced(0.5f), 4.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.72f));
    g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);

    const float cy = b.getCentreY();
    const float cx = b.getCentreX();
    juce::Path arrow;
    if (isCollapsed) {
      arrow.addTriangle(cx - 4.0f, cy - 26.0f, cx - 4.0f, cy - 18.0f, cx + 1.5f,
                        cy - 22.0f);
    } else {
      arrow.addTriangle(cx + 4.0f, cy - 26.0f, cx + 4.0f, cy - 18.0f, cx - 1.5f,
                        cy - 22.0f);
    }
    g.setColour(UITheme::onAccent.withAlpha(0.9f));
    g.fillPath(arrow);

    juce::Graphics::ScopedSaveState ss(g);
    const auto center = b.getCentre();
    g.addTransform(juce::AffineTransform::rotation(
        -juce::MathConstants<float>::halfPi, center.x, center.y));
    g.setColour(UITheme::onAccent.withAlpha(0.92f));
    g.setFont(juce::FontOptions(10.0f).withKerningFactor(0.12f));

    const int rw = static_cast<int>(b.getHeight()) - 8;
    const int rh = static_cast<int>(b.getWidth()) - 6;
    g.drawFittedText(title, static_cast<int>(center.x) - (rw / 2),
                     static_cast<int>(center.y) - (rh / 2), rw, rh,
                     juce::Justification::centred, 1);
  }

  void mouseEnter(const juce::MouseEvent &) override {
    isHovered = true;
    repaint();
  }

  void mouseExit(const juce::MouseEvent &) override {
    isHovered = false;
    isMouseDown = false;
    repaint();
  }

  void mouseDown(const juce::MouseEvent &) override {
    isMouseDown = true;
    repaint();
  }

  void mouseUp(const juce::MouseEvent &) override {
    const bool trigger = isMouseDown;
    isMouseDown = false;
    repaint();
    if (trigger && onToggle)
      onToggle();
  }

private:
  juce::String title;
  bool isCollapsed = false;
  bool isHovered = false;
  bool isMouseDown = false;
};

void ChannelStripLookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y,
                                               int width, int height,
                                               float sliderPosProportional,
                                               float rotaryStartAngle,
                                               float rotaryEndAngle,
                                               juce::Slider &) {
  const auto bounds = juce::Rectangle<float>(
      static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
      static_cast<float>(height));
  const auto centre = bounds.getCentre();
  const float knobRadius =
      juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.41f;
  const float outerRadius = knobRadius + 3.0f;
  const float angle =
      rotaryStartAngle +
      sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

  juce::Path outerArc;
  outerArc.addCentredArc(centre.x, centre.y, outerRadius, outerRadius, 0.0f,
                         rotaryStartAngle + 0.22f, rotaryEndAngle - 0.22f,
                         true);
  g.setColour(UITheme::cardBorder);
  g.strokePath(outerArc,
               juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                    juce::PathStrokeType::rounded));

  g.setColour(UITheme::cardBg);
  g.fillEllipse(centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f,
                knobRadius * 2.0f);
  g.setColour(UITheme::textSecondary);
  g.drawEllipse(centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f,
                knobRadius * 2.0f, 2.0f);

  g.setColour(juce::Colours::black.withAlpha(0.08f));
  g.drawEllipse(centre.x - knobRadius + 2.0f, centre.y - knobRadius + 2.0f,
                knobRadius * 2.0f - 4.0f, knobRadius * 2.0f - 4.0f, 1.0f);

  const float tickStart = knobRadius - 8.0f;
  const float tickEnd = knobRadius - 2.0f;
  const juce::Point<float> p1{centre.x + std::sin(angle) * tickStart,
                              centre.y - std::cos(angle) * tickStart};
  const juce::Point<float> p2{centre.x + std::sin(angle) * tickEnd,
                              centre.y - std::cos(angle) * tickEnd};
  g.setColour(UITheme::textPrimary);
  g.drawLine(p1.x, p1.y, p2.x, p2.y, 2.2f);
}

void ChannelStripLookAndFeel::drawLinearSlider(
    juce::Graphics &g, int x, int y, int width, int height, float sliderPos,
    float minSliderPos, float maxSliderPos, const juce::Slider::SliderStyle,
    juce::Slider &slider) {

  // Track (Recessed Groove)
  const float trackWidth = 3.0f;
  const float trackX = x + (width * 0.5f) - (trackWidth * 0.5f);
  const float thumbHeight = 24.0f;
  const float thumbInset = thumbHeight * 0.5f;
  const float trackY = y + thumbInset;
  const float trackHeight = height - (thumbInset * 2.0f);

  g.setColour(juce::Colours::black.withAlpha(0.2f));
  g.fillRoundedRectangle(trackX, trackY, trackWidth, trackHeight, 1.5f);

  // Inner shadow to emphasize depth
  g.setColour(juce::Colours::black.withAlpha(0.1f));
  g.drawRoundedRectangle(trackX, trackY, trackWidth, trackHeight, 1.5f, 1.0f);

  // Thumb
  const float thumbWidth = width - 4.0f;
  const float thumbX = x + 2.0f;
  const float thumbY = juce::jlimit(static_cast<float>(y),
                                    static_cast<float>(y + height) - thumbHeight,
                                    sliderPos - thumbInset);

  // Drop Shadow
  g.setColour(juce::Colours::black.withAlpha(0.15f));
  g.fillRoundedRectangle(thumbX, thumbY + 2.0f, thumbWidth, thumbHeight, 3.0f);

  // Thumb Body
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 3.0f);

  // Thumb Border (Sharper contrast)
  g.setColour(UITheme::textSecondary.withAlpha(0.7f));
  g.drawRoundedRectangle(thumbX, thumbY, thumbWidth, thumbHeight, 3.0f, 1.0f);

  // Indicator Line (Wider, more prominent)
  g.setColour(UITheme::textPrimary);
  const float lineY = thumbY + (thumbHeight * 0.5f);
  g.drawLine(thumbX + 1.0f, lineY, thumbX + thumbWidth - 1.0f, lineY, 2.0f);
}

void ChannelStripLookAndFeel::drawButtonBackground(juce::Graphics &g,
                                                   juce::Button &button,
                                                   const juce::Colour &,
                                                   bool isMouseOverButton,
                                                   bool isButtonDown) {
  auto bounds = button.getLocalBounds().toFloat();
  auto fill = UITheme::cardBg;
  const bool isMute = button.getComponentID() == "muteButton";

  if (isMouseOverButton)
    fill = UITheme::cardBgHover;
  if (isButtonDown || button.getToggleState())
    fill = UITheme::cardBgPressed;

  if (isMute && button.getToggleState()) {
    fill = UITheme::accentMuted;
    if (isMouseOverButton)
      fill = UITheme::accentMuted.brighter(0.08f);
  }

  g.setColour(fill);
  g.fillRoundedRectangle(bounds, 3.0f);

  g.setColour(UITheme::cardBorder.darker(0.16f));
  g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

juce::Font ChannelStripLookAndFeel::getTextButtonFont(juce::TextButton &, int) {
  return juce::Font(juce::FontOptions(9.0f).withKerningFactor(0.12f));
}

StereoVUMeter::StereoVUMeter(LevelProvider levelProvider,
                             MuteProvider muteProvider, bool isCompact)
    : layout(isCompact ? Layout::createCompact() : Layout{}),
      levels(std::move(levelProvider)), isMuted(std::move(muteProvider)) {
  startTimerHz(60);
}

void StereoVUMeter::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();
  const float trackH =
      juce::jmax(0.0f, bounds.getHeight() - layout.peakLedSize - layout.gap);
  const auto scaleRect = juce::Rectangle<float>(
      bounds.getX(), bounds.getY() + layout.peakLedSize + layout.gap,
      static_cast<float>(layout.scaleWidth), trackH);
  const auto meterColRect =
      juce::Rectangle<float>(scaleRect.getRight() + layout.gap, bounds.getY(),
                             static_cast<float>(layout.columnWidth),
                             bounds.getHeight());

  struct Mark {
    float db;
    const char *label;
  };
  static constexpr Mark marks[] = {
      {0.0f, "0"}, {-12.0f, "-12"}, {-24.0f, "-24"}, {-48.0f, "-48"}};

  g.setFont(juce::FontOptions(6.0f));
  for (const auto &mark : marks) {
    const float y =
        scaleRect.getBottom() - (scaleRect.getHeight() * dbToNorm(mark.db));
    g.setColour(juce::String(mark.label) == "0"
                    ? UITheme::accent
                    : UITheme::textSecondary.withAlpha(0.45f));
    g.drawFittedText(mark.label, static_cast<int>(scaleRect.getX()),
                     static_cast<int>(y - 3.0f),
                     static_cast<int>(scaleRect.getWidth() - 1.0f), 6,
                     juce::Justification::centredRight, 1);
  }

  const auto drawColumn = [&g, this, trackH](const juce::Rectangle<float> &column,
                                     float level, bool peakOn) {
    const auto ledRect = juce::Rectangle<float>(
        column.getCentreX() - (layout.peakLedSize * 0.5f), column.getY(),
        static_cast<float>(layout.peakLedSize),
        static_cast<float>(layout.peakLedSize));
    if (peakOn) {
      g.setColour(UITheme::accent.withAlpha(0.33f));
      g.fillEllipse(ledRect.expanded(2.0f));
      g.setColour(UITheme::accent);
    } else {
      g.setColour(UITheme::textSecondary.withAlpha(0.22f));
    }
    g.fillEllipse(ledRect);

    auto track = juce::Rectangle<float>(
        column.getX(), column.getY() + layout.peakLedSize + layout.gap,
        static_cast<float>(layout.columnWidth), trackH);
    g.setColour(UITheme::textSecondary.withAlpha(0.12f));
    g.fillRoundedRectangle(track, 3.0f);

    auto segmentArea = track.reduced(static_cast<float>(layout.trackPadding));
    const float segmentGap = static_cast<float>(layout.segmentGap);
    const float minSegH = 1.0f;
    const int segments =
        juce::jmax(6, juce::jmin(layout.segments,
                                static_cast<int>(std::floor(
                                    (segmentArea.getHeight() + segmentGap) /
                                    (minSegH + segmentGap)))));
    const float availableSegH =
        segmentArea.getHeight() - (segmentGap * (segments - 1));
    const float segmentH =
        juce::jmax(1.0f, availableSegH / static_cast<float>(segments));
    const int activeSegments =
        juce::jlimit(0, segments,
                     static_cast<int>(
                         std::round(level * static_cast<float>(segments))));

    for (int i = 0; i < segments; ++i) {
      const float y = segmentArea.getBottom() -
                      ((static_cast<float>(i + 1) * segmentH) +
                       (static_cast<float>(i) * segmentGap));
      const auto seg = juce::Rectangle<float>(
          segmentArea.getCentreX() - (layout.segmentWidth * 0.5f), y,
          static_cast<float>(layout.segmentWidth), segmentH);

      const bool isActive = i < activeSegments;
      const float zone =
          static_cast<float>(i + 1) / static_cast<float>(segments);

      juce::Colour active = UITheme::panelDark;
      juce::Colour inactive = UITheme::panelDark.withAlpha(0.06f);
      if (zone > 0.85f) {
        active = UITheme::accent;
        inactive = UITheme::accent.withAlpha(0.13f);
      } else if (zone > 0.65f) {
        active = UITheme::accentSoft;
        inactive = UITheme::accentSoft.withAlpha(0.10f);
      }

      g.setColour(isActive ? active : inactive);
      g.fillRoundedRectangle(seg, 1.0f);
    }
  };

  const double now = juce::Time::getMillisecondCounterHiRes();
  drawColumn(meterColRect, levelL, now < holdUntilL);
}

void StereoVUMeter::timerCallback() {
  float inputL = 0.0f;
  float inputR = 0.0f;

  if (levels) {
    const auto lr = levels();
    inputL = lr.first;
    inputR = lr.second;
  }

  if (isMuted && isMuted()) {
    inputL = 0.0f;
    inputR = 0.0f;
  }

  const float targetL = linearToNorm(juce::jmax(inputL, inputR));

  const auto smooth = [](float current, float target) {
    const float coeff = target > current ? 0.2f : 0.06f;
    const float next = current + (target - current) * coeff;
    return next < 0.0005f ? 0.0f : next;
  };

  levelL = smooth(levelL, targetL);
  const double now = juce::Time::getMillisecondCounterHiRes();
  if (targetL >= 0.985f)
    holdUntilL = now + 1500.0;

  repaint();
}

ChannelStripCard::ChannelStripCard(const juce::String &channelName,
                                   ChannelStripLookAndFeel &lookAndFeelRef,
                                   StereoVUMeter::LevelProvider levelProvider,
                                   bool isCompact)
    : layout(isCompact ? Layout::createCompact() : Layout{}),
      lookAndFeel(lookAndFeelRef), name(channelName),
      meter(
          std::move(levelProvider),
          [this] { return muteButton.getToggleState(); }, isCompact),
      cardBgColour(UITheme::cardBg), channelBandColour(UITheme::accent),
      channelBandTextColour(UITheme::onAccent) {
  volSlider.setSliderStyle(juce::Slider::LinearVertical);
  volSlider.setViewportIgnoreDragFlag(true);
  volSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  volSlider.setRange(0.0, 1.0);
  volSlider.setValue(1.0);
  volSlider.setLookAndFeel(&lookAndFeel);
  addAndMakeVisible(volSlider);
  addAndMakeVisible(meter);

  configureKnob(gainKnob, 1.0);
  configureLabel(gainLabel, "GAIN", 7.5f, 0.08f, true);

  soloButton.setButtonText("S");
  soloButton.setClickingTogglesState(true);
  soloButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  soloButton.setColour(juce::TextButton::textColourOnId,
                       UITheme::textPrimary.darker(0.18f));
  soloButton.setLookAndFeel(&lookAndFeel);
  addAndMakeVisible(soloButton);

  muteButton.setButtonText("M");
  muteButton.setClickingTogglesState(true);
  muteButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  muteButton.setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  muteButton.setLookAndFeel(&lookAndFeel);
  muteButton.setComponentID("muteButton");
  addAndMakeVisible(muteButton);

  volSlider.onValueChange = [this] {
    if (onVolumeChanged)
      onVolumeChanged(static_cast<float>(volSlider.getValue()));
  };
  gainKnob.onValueChange = [this] {
    if (onGainChanged)
      onGainChanged(static_cast<float>(gainKnob.getValue()));
  };
  muteButton.onClick = [this] {
    if (onMuteChanged)
      onMuteChanged(muteButton.getToggleState());
  };
  soloButton.onClick = [this] {
    if (onSoloChanged)
      onSoloChanged(soloButton.getToggleState());
  };
}

ChannelStripCard::~ChannelStripCard() { clearLookAndFeelFromControls(); }

void ChannelStripCard::paint(juce::Graphics &g) {
  const auto card = getLocalBounds().toFloat();
  g.setColour(cardBgColour);
  g.fillRoundedRectangle(card, 4.0f);
  g.setColour(UITheme::cardBorder);
  g.drawRoundedRectangle(card, 4.0f, 1.0f);

  const auto band = juce::Rectangle<float>(
      0.0f, static_cast<float>(getHeight() - layout.channelBandHeight),
      static_cast<float>(getWidth()),
      static_cast<float>(layout.channelBandHeight));
  g.setColour(channelBandColour);
  g.fillRect(band);

  g.setColour(channelBandTextColour);
  g.setFont(juce::FontOptions(8.0f).withKerningFactor(0.08f));
  g.drawFittedText(name, band.toNearestInt(), juce::Justification::centred, 1);
}

void ChannelStripCard::resized() {
  const int buttonsY = getHeight() - layout.channelBandHeight -
                       layout.buttonToBandGap - layout.soloHeight;

  // Calculate total width of Fader + Gap + Meter
  const int totalGroupWidth = layout.faderWidth + layout.faderMeterGap +
                              meter.getLayout().getBlockWidth();

  // Center whole group horizontally
  const int groupX = (getWidth() - totalGroupWidth) / 2;
  const int faderX = groupX;
  const int meterX = faderX + layout.faderWidth + layout.faderMeterGap;

  const int knobX = faderX + (layout.faderWidth - layout.knobSize) / 2;
  const int knobY = layout.knobTop;
  gainKnob.setBounds(knobX, knobY, layout.knobSize, layout.knobSize);
  const int labelW = layout.knobSize + 10;
  const int labelH = 12;
  const int labelX = faderX + (layout.faderWidth - labelW) / 2;
  const int labelY = knobY + layout.knobSize + 2;
  gainLabel.setBounds(labelX, labelY, labelW, labelH);

  // Vertically, position the fader just above the buttons
  const int mainVolBottomSpacing = 12;
  const int componentBottomY = buttonsY - mainVolBottomSpacing;

  const int minFaderY = labelY + labelH + 6;
  const int faderY =
      juce::jmax(minFaderY, componentBottomY - layout.faderHeight);
  const int faderH = juce::jmax(12, componentBottomY - faderY);
  volSlider.setBounds(faderX, faderY, layout.faderWidth, faderH);

  // Make the VU meter span from near the top of the strip down to just
  // above the mute/solo buttons.
  const int meterTop = layout.knobTop;
  const int meterBottom = buttonsY - 10;
  const int meterHeight = juce::jmax(12, meterBottom - meterTop);
  meter.setBounds(meterX, meterTop, meter.getLayout().getBlockWidth(),
                  meterHeight);

  const int buttonsX = (getWidth() - layout.soloWidth) / 2;
  if (soloVisible) {
    const int buttonW = (layout.soloWidth - layout.buttonGap) / 2;
    soloButton.setBounds(buttonsX, buttonsY, buttonW, layout.soloHeight);
    muteButton.setBounds(buttonsX + buttonW + layout.buttonGap, buttonsY,
                         layout.soloWidth - buttonW - layout.buttonGap,
                         layout.soloHeight);
  } else {
    soloButton.setBounds({});
    if (muteAlignedToFader) {
      const int muteX =
          groupX + (totalGroupWidth - layout.soloWidth) / 2;
      muteButton.setBounds(muteX, buttonsY, layout.soloWidth,
                           layout.soloHeight);
    } else {
      muteButton.setBounds(buttonsX, buttonsY, layout.soloWidth,
                           layout.soloHeight);
    }
  }
}

const juce::String &ChannelStripCard::getChannelName() const { return name; }

void ChannelStripCard::setVolume(float volume) {
  volSlider.setValue(juce::jlimit(0.0f, 1.0f, volume),
                     juce::dontSendNotification);
}

void ChannelStripCard::setGain(float gain) {
  gainKnob.setValue(juce::jlimit(0.0f, 2.0f, gain),
                    juce::dontSendNotification);
}

void ChannelStripCard::setMuteState(bool muted) {
  muteButton.setToggleState(muted, juce::dontSendNotification);
}

void ChannelStripCard::setSoloState(bool soloed) {
  soloButton.setToggleState(soloed, juce::dontSendNotification);
}

void ChannelStripCard::setSoloEnabled(bool enabled) {
  if (!enabled)
    soloButton.setToggleState(false, juce::dontSendNotification);
  soloButton.setEnabled(enabled);
}

void ChannelStripCard::setSoloVisible(bool visible) {
  soloVisible = visible;
  soloButton.setVisible(visible);
  resized();
}

void ChannelStripCard::setGainVisible(bool visible) {
  gainVisible = visible;
  gainKnob.setVisible(visible);
  gainLabel.setVisible(visible);
}

void ChannelStripCard::setMuteAlignedToFader(bool aligned) {
  muteAlignedToFader = aligned;
  resized();
}

void ChannelStripCard::setChannelBandColours(juce::Colour bandColour,
                                             juce::Colour textColour) {
  channelBandColour = bandColour;
  channelBandTextColour = textColour;
  repaint();
}

void ChannelStripCard::setCardBgColour(juce::Colour colour) {
  cardBgColour = colour;
  repaint();
}

void ChannelStripCard::refreshThemeColours() {
  gainLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  soloButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  soloButton.setColour(juce::TextButton::textColourOnId,
                       UITheme::textPrimary.darker(0.18f));
  muteButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  muteButton.setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  repaint();
}

void ChannelStripCard::setOnVolumeChanged(std::function<void(float)> cb) {
  onVolumeChanged = std::move(cb);
}

void ChannelStripCard::setOnGainChanged(std::function<void(float)> cb) {
  onGainChanged = std::move(cb);
}

void ChannelStripCard::setOnMuteChanged(std::function<void(bool)> cb) {
  onMuteChanged = std::move(cb);
}

void ChannelStripCard::setOnSoloChanged(std::function<void(bool)> cb) {
  onSoloChanged = std::move(cb);
}

void ChannelStripCard::configureLabel(juce::Label &label,
                                      const juce::String &text, float size,
                                      float kerning, bool uppercase) {
  label.setText(uppercase ? text.toUpperCase() : text,
                juce::dontSendNotification);
  label.setJustificationType(juce::Justification::centred);
  label.setFont(juce::FontOptions(size).withKerningFactor(kerning));
  label.setColour(juce::Label::textColourId, UITheme::textPrimary);
  addAndMakeVisible(label);
}

void ChannelStripCard::configureKnob(juce::Slider &slider, double value) {
  slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  slider.setViewportIgnoreDragFlag(true);
  slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  slider.setRange(0.0, 2.0, 0.0);
  slider.setValue(value, juce::dontSendNotification);
  slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                             juce::MathConstants<float>::pi * 2.8f, true);
  slider.setLookAndFeel(&lookAndFeel);
  slider.setMouseDragSensitivity(170);
  slider.setDoubleClickReturnValue(true, value);
  addAndMakeVisible(slider);
}

void ChannelStripCard::clearLookAndFeelFromControls() {
  volSlider.setLookAndFeel(nullptr);
  gainKnob.setLookAndFeel(nullptr);
  gainLabel.setLookAndFeel(nullptr);
  soloButton.setLookAndFeel(nullptr);
  muteButton.setLookAndFeel(nullptr);
}

MTControlsAudioProcessorEditor::MTControlsAudioProcessorEditor(
    MTControlsAudioProcessor &p)
    : AudioProcessorEditor(p) {
  class MetronomePanel final : public juce::Component, private juce::Timer {
  public:
    class BpmSegmentButton final : public juce::Button {
    public:
      BpmSegmentButton() : juce::Button("metroBpmSegment") {}

      void setBpmText(juce::String text) {
        bpmText = std::move(text);
        repaint();
      }

      void paintButton(juce::Graphics &g, bool isMouseOverButton,
                       bool isButtonDown) override {
        auto b = getLocalBounds().toFloat();
        juce::Colour fill = UITheme::accent;
        if (isMouseOverButton)
          fill = fill.brighter(0.06f);
        if (isButtonDown)
          fill = UITheme::accentSoft;

        juce::Path leftSegment;
        leftSegment.addRoundedRectangle(b.getX(), b.getY(), b.getWidth(),
                                        b.getHeight(), 8.0f, 8.0f, true, false,
                                        true, false);
        g.setColour(fill);
        g.fillPath(leftSegment);
        g.setColour(UITheme::cardBorder.withAlpha(0.7f));
        g.strokePath(leftSegment, juce::PathStrokeType(1.0f));

        auto textArea = getLocalBounds().reduced(8, 2);
        textArea.removeFromBottom(8);
        g.setColour(UITheme::onAccent);
        g.setFont(juce::FontOptions(17.0f).withKerningFactor(0.06f));
        g.drawFittedText(bpmText, textArea, juce::Justification::centred, 1);

        const float cx = b.getCentreX();
        const float cy = b.getBottom() - 7.0f;
        juce::Path caret;
        caret.startNewSubPath(cx - 4.0f, cy - 2.0f);
        caret.lineTo(cx, cy + 2.0f);
        caret.lineTo(cx + 4.0f, cy - 2.0f);
        g.strokePath(caret, juce::PathStrokeType(1.6f));
      }

    private:
      juce::String bpmText = "120";
    };

    class MetronomeIconButton final : public juce::Button {
    public:
      MetronomeIconButton() : juce::Button("metroIconSegment") {
        setClickingTogglesState(true);
      }

      void setBeatPulse(float intensity, bool downbeat) {
        pulse = juce::jlimit(0.0f, 1.0f, intensity);
        isDownbeat = downbeat;
        repaint();
      }

      void paintButton(juce::Graphics &g, bool isMouseOverButton,
                       bool isButtonDown) override {
        auto b = getLocalBounds().toFloat();
        const bool enabled = getToggleState();

        juce::Colour fill = enabled ? UITheme::accent : UITheme::cardBgHover;
        if (enabled && pulse > 0.001f) {
          const auto flashColour =
              isDownbeat ? UITheme::accentSoft : UITheme::onAccent;
          fill = fill.interpolatedWith(flashColour, 0.18f + (pulse * 0.42f));
        }
        if (isMouseOverButton)
          fill = fill.brighter(0.06f);
        if (isButtonDown)
          fill = fill.darker(0.06f);

        juce::Path rightSegment;
        rightSegment.addRoundedRectangle(b.getX(), b.getY(), b.getWidth(),
                                         b.getHeight(), 8.0f, 8.0f, false, true,
                                         false, true);
        g.setColour(fill);
        g.fillPath(rightSegment);
        g.setColour(UITheme::cardBorder.withAlpha(0.7f));
        g.strokePath(rightSegment, juce::PathStrokeType(1.0f));

        const auto iconArea = b.reduced(b.getWidth() * 0.26f, b.getHeight() * 0.2f);
        juce::Path body;
        body.startNewSubPath(iconArea.getX() + 1.0f, iconArea.getBottom());
        body.lineTo(iconArea.getCentreX(), iconArea.getY() + 2.0f);
        body.lineTo(iconArea.getRight() - 1.0f, iconArea.getBottom());
        body.closeSubPath();

        const auto iconColour = enabled ? UITheme::onAccent : UITheme::textPrimary;
        g.setColour(iconColour);
        g.strokePath(body, juce::PathStrokeType(1.8f));

        g.drawLine(iconArea.getCentreX(), iconArea.getY() + 7.0f,
                   iconArea.getCentreX(), iconArea.getBottom() - 3.0f, 1.5f);
        g.drawLine(iconArea.getCentreX(), iconArea.getCentreY() + 1.0f,
                   iconArea.getRight() - 3.0f, iconArea.getY() + 5.0f, 1.5f);
      }

    private:
      float pulse = 0.0f;
      bool isDownbeat = false;
    };

    class MetronomePopover final : public juce::Component {
    public:
      using TapCallback = std::function<void()>;
      using TimeSignatureCallback = std::function<void(const juce::String &)>;
      using BpmSetCallback = std::function<void(double)>;
      using BpmAdjustCallback = std::function<void(double)>;
      using BpmProvider = std::function<double()>;

      MetronomePopover(const juce::String &timeSig, TapCallback onTapCb,
                       TimeSignatureCallback onTimeSigCb,
                       BpmSetCallback onBpmSetCb,
                       BpmAdjustCallback onBpmAdjustCb,
                       BpmProvider bpmProviderCb)
          : onTap(std::move(onTapCb)),
            onTimeSignatureChanged(std::move(onTimeSigCb)),
            onBpmSet(std::move(onBpmSetCb)),
            onBpmAdjust(std::move(onBpmAdjustCb)),
            bpmProvider(std::move(bpmProviderCb)) {
        tapButton.setButtonText("TAP");
        tapButton.setViewportIgnoreDragFlag(true);
        tapButton.onClick = [this] {
          if (onTap)
            onTap();
          syncBpmEditor();
        };
        addAndMakeVisible(tapButton);

        timeSigBox.addItem("2/4", 1);
        timeSigBox.addItem("3/4", 2);
        timeSigBox.addItem("4/4", 3);
        timeSigBox.addItem("5/4", 4);
        timeSigBox.addItem("6/8", 5);
        timeSigBox.setText(timeSig, juce::dontSendNotification);
        timeSigBox.onChange = [this] {
          if (onTimeSignatureChanged)
            onTimeSignatureChanged(timeSigBox.getText().trim());
        };
        addAndMakeVisible(timeSigBox);

        minusButton.setButtonText("-");
        plusButton.setButtonText("+");
        minusButton.setViewportIgnoreDragFlag(true);
        plusButton.setViewportIgnoreDragFlag(true);
        minusButton.onClick = [this] {
          if (onBpmAdjust)
            onBpmAdjust(-1.0);
          syncBpmEditor();
        };
        plusButton.onClick = [this] {
          if (onBpmAdjust)
            onBpmAdjust(1.0);
          syncBpmEditor();
        };
        addAndMakeVisible(minusButton);
        addAndMakeVisible(plusButton);

        bpmEditor.setInputRestrictions(4, "0123456789.");
        bpmEditor.setJustification(juce::Justification::centred);
        bpmEditor.onReturnKey = [this] { submitBpmEditor(); };
        bpmEditor.onFocusLost = [this] { submitBpmEditor(); };
        addAndMakeVisible(bpmEditor);

        syncBpmEditor();
      }

      void refreshThemeColours() {
        tapButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
        tapButton.setColour(juce::TextButton::buttonOnColourId, UITheme::cardBgPressed);
        tapButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
        tapButton.setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);

        minusButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
        minusButton.setColour(juce::TextButton::buttonOnColourId, UITheme::cardBgPressed);
        minusButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
        plusButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
        plusButton.setColour(juce::TextButton::buttonOnColourId, UITheme::cardBgPressed);
        plusButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);

        timeSigBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBgHover);
        timeSigBox.setColour(juce::ComboBox::outlineColourId, UITheme::cardBorder.withAlpha(0.65f));
        timeSigBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
        timeSigBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);

        bpmEditor.setColour(juce::TextEditor::backgroundColourId, UITheme::cardBgHover);
        bpmEditor.setColour(juce::TextEditor::textColourId, UITheme::textPrimary);
        bpmEditor.setColour(juce::TextEditor::outlineColourId, UITheme::cardBorder.withAlpha(0.7f));
        repaint();
      }

      void paint(juce::Graphics &g) override {
        auto b = getLocalBounds().toFloat();
        g.setColour(UITheme::cardBg);
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(UITheme::cardBorder);
        g.drawRoundedRectangle(b, 6.0f, 1.0f);
      }

      void resized() override {
        auto area = getLocalBounds().reduced(10);
        const int tapH = 32;
        tapButton.setBounds(area.removeFromTop(tapH));
        area.removeFromTop(8);

        timeSigBox.setBounds(area.removeFromTop(26));
        area.removeFromTop(8);

        auto bpmRow = area.removeFromTop(30);
        minusButton.setBounds(bpmRow.removeFromLeft(28));
        plusButton.setBounds(bpmRow.removeFromRight(28));
        bpmEditor.setBounds(bpmRow.reduced(6, 0));
      }

    private:
      juce::TextButton tapButton;
      juce::ComboBox timeSigBox;
      juce::TextButton minusButton;
      juce::TextButton plusButton;
      juce::TextEditor bpmEditor;

      TapCallback onTap;
      TimeSignatureCallback onTimeSignatureChanged;
      BpmSetCallback onBpmSet;
      BpmAdjustCallback onBpmAdjust;
      BpmProvider bpmProvider;

      void syncBpmEditor() {
        if (!bpmProvider)
          return;
        bpmEditor.setText(juce::String(static_cast<int>(std::round(bpmProvider()))),
                          juce::dontSendNotification);
      }

      void submitBpmEditor() {
        const auto text = bpmEditor.getText().trim();
        if (text.isEmpty() || !onBpmSet)
          return;
        const double bpm = text.getDoubleValue();
        if (bpm > 0.0)
          onBpmSet(bpm);
        syncBpmEditor();
      }
    };

    explicit MetronomePanel(MTControlsAudioProcessor &processorRef)
        : processor(processorRef) {
      bpmButton.setViewportIgnoreDragFlag(true);
      iconButton.setViewportIgnoreDragFlag(true);

      bpmButton.onClick = [this] { showPopover(); };
      iconButton.onClick = [this] {
        processor.setMetronomeEnabled(iconButton.getToggleState());
      };

      addAndMakeVisible(bpmButton);
      addAndMakeVisible(iconButton);

      applyTimeSignature("4/4");
      refreshFromProcessor();
      refreshThemeColours();
      startTimerHz(30);
    }

    void setSongBpm(double bpm) {
      if (bpm <= 0.0)
        return;
      manualOverride = false;
      processor.setMetronomeBpm(bpm);
      updateBpmDisplay();
    }

    void refreshThemeColours() { repaint(); }

    void paint(juce::Graphics &g) override {
      auto b = getLocalBounds().toFloat().reduced(4.0f, 6.0f);
      const float separatorX = b.getX() + (b.getWidth() * 0.64f);
      g.setColour(UITheme::cardBorder.withAlpha(0.5f));
      g.drawLine(separatorX, b.getY() + 4.0f, separatorX, b.getBottom() - 4.0f,
                 1.0f);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(4, 6);
      const int segmentW = static_cast<int>(std::round(area.getWidth() * 0.64f));
      bpmButton.setBounds(area.removeFromLeft(segmentW));
      iconButton.setBounds(area);
    }

  private:
    MTControlsAudioProcessor &processor;
    BpmSegmentButton bpmButton;
    MetronomeIconButton iconButton;
    bool manualOverride = false;
    double lastTickTime = 0.0;
    double beatCounter = 0.0;
    int lastBeatIndex = -1;
    float beatFlash = 0.0f;
    bool lastBeatWasDownbeat = true;
    std::deque<double> tapTimes;
    int beatsPerBar = 4;
    juce::String timeSignature = "4/4";

    void showPopover() {
      auto panel = std::make_unique<MetronomePopover>(
          timeSignature, [this] { registerTap(); },
          [this](const juce::String &sig) { applyTimeSignature(sig); },
          [this](double bpm) { setBpmAbsolute(bpm); },
          [this](double delta) { adjustBpm(delta); },
          [this] { return processor.getMetronomeBpm(); });
      panel->setSize(180, 120);
      panel->refreshThemeColours();

      auto &callout = juce::CallOutBox::launchAsynchronously(
          std::move(panel), bpmButton.getBounds(), this);
      callout.setDismissalMouseClicksAreAlwaysConsumed(true);
    }

    void refreshFromProcessor() {
      updateBpmDisplay();
      iconButton.setToggleState(processor.isMetronomeEnabled(),
                                juce::dontSendNotification);
    }

    void updateBpmDisplay() {
      bpmButton.setBpmText(
          juce::String(static_cast<int>(std::round(processor.getMetronomeBpm()))));
    }

    void setBpmAbsolute(double bpm) {
      if (bpm <= 0.0)
        return;
      manualOverride = true;
      processor.setMetronomeBpm(bpm);
      updateBpmDisplay();
    }

    void adjustBpm(double delta) {
      const auto mods = juce::ModifierKeys::getCurrentModifiers();
      const double step = mods.isShiftDown() ? (delta < 0.0 ? -5.0 : 5.0)
                                             : delta;
      setBpmAbsolute(processor.getMetronomeBpm() + step);
    }

    void registerTap() {
      const double now = juce::Time::getMillisecondCounterHiRes();
      if (!tapTimes.empty() && (now - tapTimes.back()) > 2000.0)
        tapTimes.clear();
      tapTimes.push_back(now);
      while (tapTimes.size() > 6)
        tapTimes.pop_front();

      if (tapTimes.size() < 2)
        return;

      double sum = 0.0;
      for (size_t i = 1; i < tapTimes.size(); ++i)
        sum += (tapTimes[i] - tapTimes[i - 1]);
      const double avgMs = sum / static_cast<double>(tapTimes.size() - 1);
      if (avgMs <= 0.0)
        return;

      setBpmAbsolute(60000.0 / avgMs);
    }

    void applyTimeSignature(const juce::String &sigText) {
      timeSignature = sigText.trim();
      const int numerator = timeSignature.upToFirstOccurrenceOf("/", false, false)
                                .getIntValue();
      beatsPerBar = numerator > 0 ? numerator : 4;
      processor.setMetronomeBeatsPerBar(beatsPerBar);
      beatCounter = 0.0;
      lastBeatIndex = -1;
      lastBeatWasDownbeat = true;
    }

    void timerCallback() override {
      const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
      if (lastTickTime == 0.0)
        lastTickTime = now;
      const double dt = now - lastTickTime;
      lastTickTime = now;

      refreshFromProcessor();

      if (processor.isMetronomeEnabled()) {
        const double bpm = processor.getMetronomeBpm();
        if (bpm > 0.0) {
          beatCounter += dt * (bpm / 60.0);
          const int beatIndex = static_cast<int>(std::floor(beatCounter));
          if (beatIndex != lastBeatIndex) {
            lastBeatIndex = beatIndex;
            lastBeatWasDownbeat = (beatIndex % juce::jmax(1, beatsPerBar)) == 0;
            beatFlash = 1.0f;
          }
        }
      } else {
        beatCounter = 0.0;
        lastBeatIndex = -1;
        beatFlash = 0.0f;
      }

      beatFlash = juce::jmax(0.0f, beatFlash - 0.08f);
      const float pulse = processor.isMetronomeEnabled() ? beatFlash : 0.0f;
      iconButton.setBeatPulse(pulse, lastBeatWasDownbeat);
    }
  };

  class SongListPanel final : public juce::Component {
  public:
    class AddSongButton final : public juce::Button {
    public:
      AddSongButton() : juce::Button("addSong") {}

      void paintButton(juce::Graphics &g, bool isMouseOverButton,
                       bool isButtonDown) override {
        auto b = getLocalBounds().toFloat();

        juce::Colour fill = UITheme::cardBg;
        if (isMouseOverButton)
          fill = UITheme::cardBgHover;
        if (isButtonDown)
          fill = UITheme::cardBgPressed;

        g.setColour(fill);
        g.fillRoundedRectangle(b, 5.0f);
        g.setColour(UITheme::cardBorder.withAlpha(0.82f));
        g.drawRoundedRectangle(b, 5.0f, 1.0f);

        auto iconRect = b.removeFromLeft(24.0f).reduced(4.0f);
        drawIconGlyph(g, iconRect, IconGlyph::Add, UITheme::accent, 1.6f);

        g.setColour(UITheme::textPrimary);
        g.setFont(juce::FontOptions(10.5f).withKerningFactor(0.08f));
        g.drawText("Add Song", b.reduced(4.0f, 0.0f).toNearestInt(),
                   juce::Justification::centredLeft);
      }
    };

    SongListPanel() {
      listViewport.setViewedComponent(&listContent, false);
      listViewport.setScrollBarsShown(true, false, false, false);
      listViewport.setScrollBarThickness(1);
      listViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
      addAndMakeVisible(listViewport);
      addAndMakeVisible(addSongButton);

      addSongButton.onClick = [this] { openSongImportWizard(); };
      listContent.setOnSongActivated([this](int songIndex) {
        if (!juce::isPositiveAndBelow(songIndex,
                                      static_cast<int>(librarySongs.size())))
          return;
        lastActivatedIndex = songIndex;
        if (onSongActivated)
          onSongActivated(librarySongs[static_cast<size_t>(songIndex)],
                          songIndex);
      });
      refreshFromStorage();
    }

    void setOnSongActivated(
        std::function<void(const ImportedSongData &, int)> cb) {
      onSongActivated = std::move(cb);
    }

    int getSongCount() const { return static_cast<int>(librarySongs.size()); }

    const ImportedSongData *getSongPtr(int index) const {
      if (!juce::isPositiveAndBelow(index,
                                    static_cast<int>(librarySongs.size())))
        return nullptr;
      return &librarySongs[static_cast<size_t>(index)];
    }

    int getLastActivatedIndex() const { return lastActivatedIndex; }

    void paint(juce::Graphics &g) override {
      const auto bounds = getLocalBounds().toFloat();
      g.setColour(UITheme::cardBg);
      g.fillRoundedRectangle(bounds, 6.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.75f));
      g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

      g.setColour(UITheme::textPrimary);
      g.setFont(juce::FontOptions(18.0f));
      g.drawText("Setlist A", 14, 8, getWidth() - 190, 24,
                 juce::Justification::centredLeft);

      const float pencilX = static_cast<float>(addSongButton.getX() - 24);
      drawIconGlyph(g, juce::Rectangle<float>(pencilX, 8.0f, 20.0f, 22.0f),
                    IconGlyph::Pencil, UITheme::accent, 1.6f);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(10);
      auto header = area.removeFromTop(30);

      const int addW = 126;
      addSongButton.setBounds(header.getRight() - addW, header.getY() + 3, addW,
                              24);

      area.removeFromTop(8);
      listViewport.setBounds(area);
      listContent.setSize(juce::jmax(1, listViewport.getWidth()),
                          juce::jmax(listViewport.getHeight(),
                                     listContent.getPreferredHeight()));
    }

  private:
    struct SongRow {
      int order = 0;
      juce::String title;
      juce::String meta;
      int songIndex = -1;
    };

    class ListContent : public juce::Component {
    public:
      void setSongs(std::vector<SongRow> rows) {
        songs = std::move(rows);
        repaint();
      }

      void setOnSongActivated(std::function<void(int)> cb) {
        onSongActivated = std::move(cb);
      }

      int getPreferredHeight() const {
        if (songs.empty())
          return 88;
        const int rowH = 52;
        const int gap = 6;
        return 8 + static_cast<int>(songs.size()) * (rowH + gap);
      }

      void paint(juce::Graphics &g) override {
        if (songs.empty()) {
          g.setColour(UITheme::textSecondary.withAlpha(0.88f));
          g.setFont(juce::FontOptions(12.0f));
          g.drawFittedText("No songs yet. Use Add Song to import stems.",
                           getLocalBounds().reduced(10),
                           juce::Justification::centred, 2);
          return;
        }

        const int rowH = 52;
        const int gap = 6;

        for (size_t i = 0; i < songs.size(); ++i) {
          auto row = juce::Rectangle<int>(
              8, 8 + static_cast<int>(i) * (rowH + gap), getWidth() - 16, rowH);
          g.setColour(UITheme::cardBgHover);
          g.fillRoundedRectangle(row.toFloat(), 4.0f);

          g.setColour(UITheme::cardBorder.withAlpha(0.58f));
          g.drawRoundedRectangle(row.toFloat(), 4.0f, 1.0f);

          auto c = row.reduced(8, 6);
          auto orderRect = c.removeFromLeft(24);
          g.setColour(UITheme::textSecondary);
          g.setFont(juce::FontOptions(11.0f));
          g.drawText(juce::String(songs[i].order), orderRect,
                     juce::Justification::centred);

          auto artRect = c.removeFromLeft(40).reduced(2);
          g.setColour(UITheme::accent.withAlpha(0.68f));
          g.fillRoundedRectangle(artRect.toFloat(), 3.0f);
          g.setColour(UITheme::cardBg.withAlpha(0.75f));
          g.drawLine(static_cast<float>(artRect.getX() + 6),
                     static_cast<float>(artRect.getCentreY()),
                     static_cast<float>(artRect.getRight() - 6),
                     static_cast<float>(artRect.getCentreY()), 1.2f);

          c.removeFromLeft(8);
          auto optionsRect = c.removeFromRight(18);
          auto checkRect = c.removeFromRight(20).reduced(2);
          c.removeFromRight(8);

          g.setColour(UITheme::textPrimary.darker(0.16f));
          g.setFont(juce::FontOptions(12.0f));
          g.drawFittedText(songs[i].title, c.removeFromTop(16),
                           juce::Justification::centredLeft, 1);
          g.setColour(UITheme::textSecondary);
          g.setFont(juce::FontOptions(9.0f));
          g.drawFittedText(songs[i].meta, c, juce::Justification::centredLeft,
                           1);

          g.setColour(UITheme::cardBorder.darker(0.16f).withAlpha(0.85f));
          g.drawRect(checkRect, 1);
          drawIconGlyph(g, optionsRect.toFloat(), IconGlyph::More,
                        UITheme::textSecondary, 1.4f);
        }
      }

      void mouseDoubleClick(const juce::MouseEvent &event) override {
        const int idx = rowIndexAtY(juce::roundToInt(event.position.y));
        if (!juce::isPositiveAndBelow(idx, static_cast<int>(songs.size())))
          return;

        if (onSongActivated)
          onSongActivated(songs[static_cast<size_t>(idx)].songIndex);
      }

    private:
      int rowIndexAtY(int y) const {
        constexpr int rowStart = 8;
        constexpr int rowH = 52;
        constexpr int gap = 6;

        if (y < rowStart)
          return -1;

        const int relY = y - rowStart;
        const int step = rowH + gap;
        const int row = relY / step;
        if (!juce::isPositiveAndBelow(row, static_cast<int>(songs.size())))
          return -1;

        if ((relY % step) >= rowH)
          return -1;

        return row;
      }

      std::vector<SongRow> songs;
      std::function<void(int)> onSongActivated;
    };

    void openSongImportWizard() {
      auto safeThis = juce::Component::SafePointer<SongListPanel>(this);

      juce::DialogWindow::LaunchOptions options;
      options.dialogTitle = "Import Song";
      options.dialogBackgroundColour = juce::Colour::fromRGB(24, 26, 30);
      options.escapeKeyTriggersCloseButton = true;
      options.useNativeTitleBar = true;
      options.resizable = true;
      options.componentToCentreAround = this;
      options.content.setOwned(
          new SongImportWizardComponent([safeThis](ImportedSongData song) {
            if (safeThis == nullptr)
              return;

            if (!SongLibraryStore::appendSong(std::move(song))) {
              juce::AlertWindow::showMessageBoxAsync(
                  juce::AlertWindow::WarningIcon, "Save error",
                  "Couldn't persist the imported song.");
              return;
            }

            // Pre-generate waveform cache for the newly imported song
            const auto allSongs = SongLibraryStore::loadSongs();
            if (!allSongs.empty()) {
              const auto &saved = allSongs.back();
              auto bins = buildMixedWaveformBins(saved);
              SongLibraryStore::saveWaveformCache(saved.id, bins);
            }

            safeThis->refreshFromStorage();
          }));

      if (auto *window = options.launchAsync()) {
        window->setResizable(true, true);
        window->centreAroundComponent(this, 780, 580);
      }
    }

    void refreshFromStorage() {
      librarySongs = SongLibraryStore::loadSongs();
      if (!juce::isPositiveAndBelow(lastActivatedIndex,
                                    static_cast<int>(librarySongs.size())))
        lastActivatedIndex = librarySongs.empty() ? -1 : 0;

      std::vector<SongRow> rows;
      rows.reserve(librarySongs.size());

      int order = 1;
      for (size_t i = 0; i < librarySongs.size(); ++i) {
        const auto &song = librarySongs[i];
        const auto includedStems = static_cast<int>(std::count_if(
            song.stems.begin(), song.stems.end(),
            [](const ImportedStemData &stem) { return stem.include; }));

        juce::String meta;
        if (song.artist.isNotEmpty())
          meta << song.artist << " • ";
        if (song.key.isNotEmpty())
          meta << song.key << " • ";
        if (song.bpm > 0)
          meta << juce::String(song.bpm) << " BPM • ";
        meta << juce::String(includedStems) << " stems";

        rows.push_back({order++,
                        song.title.isNotEmpty() ? song.title : "Untitled Song",
                        meta, static_cast<int>(i)});
      }

      listContent.setSongs(std::move(rows));
      resized();
    }

    juce::Viewport listViewport;
    ListContent listContent;
    AddSongButton addSongButton;
    std::vector<ImportedSongData> librarySongs;
    std::function<void(const ImportedSongData &, int)> onSongActivated;
    int lastActivatedIndex = -1;
  };

  auto topBarImpl = std::make_unique<TopBar>(p);
  auto *topBarRaw = topBarImpl.get();
  topBar = std::move(topBarImpl);
  topBarRaw->setTransportProviders(
      [&p] { return p.isSongPlaying(); },
      [&p] { return p.getSongCurrentPositionSeconds(); },
      [&p] { return p.getSongEndSeconds(); },
      [&p] { return p.getLoadedSongKey(); });
  topBarRaw->setMonoClickProvider([&p] { return p.isMonoClickMode(); });

  auto waveformBarImpl = std::make_unique<WaveformBar>();
  auto *waveformBarRaw = waveformBarImpl.get();
  waveformBar = std::move(waveformBarImpl);
  waveformBarRaw->setTransportProviders(
      [&p] { return p.getSongCurrentPositionSeconds(); },
      [&p] { return p.getSongLengthSeconds(); });
  waveformBarRaw->setSeekCallback([&p](double targetSeconds) {
    p.seekSongTo(targetSeconds);
  });
  waveformBarRaw->setOnEditToggle([this](bool expanded) {
    isWaveformExpanded = expanded;
    resized();
    repaint();
  });

  auto channelGridImpl =
      std::make_unique<ChannelGridPanel>(stripLookAndFeel, p);
  auto *channelGridRaw = channelGridImpl.get();
  channelGridPanel = std::move(channelGridImpl);

  auto mixerFaderImpl =
      std::make_unique<MixerFaderPanel>(stripLookAndFeel, p);
  auto *mixerFaderRaw = mixerFaderImpl.get();
  mixerFaderPanel = std::move(mixerFaderImpl);
  mixerFaderRaw->setOnAdjust([channelGridRaw](float delta) {
    if (channelGridRaw)
      channelGridRaw->nudgeFaders(delta);
  });
  refreshTopBarTheme = [topBarRaw, waveformBarRaw, channelGridRaw,
                        mixerFaderRaw] {
    if (topBarRaw)
      topBarRaw->refreshThemeColours();
    if (waveformBarRaw)
      waveformBarRaw->refreshThemeColours();
    if (channelGridRaw)
      channelGridRaw->refreshThemeColours();
    if (mixerFaderRaw)
      mixerFaderRaw->refreshThemeColours();
  };

  auto songListImpl = std::make_unique<SongListPanel>();
  auto *songListRaw = songListImpl.get();
  songListPanel = std::move(songListImpl);

  auto loadSong = [channelGridRaw, waveformBarRaw, mixerFaderRaw, &p,
                   this](const ImportedSongData &song, int songIndex) {
    juce::StringArray stemPaths;
    std::vector<bool> clickGuideMask;
    ImportedSongData loadedSong = song;
    loadedSong.stems.clear();
    loadedSong.stems.reserve(song.stems.size());
    std::vector<ImportedStemData> resolvedStems;
    resolvedStems.reserve(song.stems.size());

    for (const auto &stem : song.stems) {
      if (!stem.include)
        continue;

      juce::File file(stem.storedPath);
      if (!file.existsAsFile())
        file = juce::File(stem.sourcePath);
      if (file.existsAsFile()) {
        ImportedStemData loadedStem = stem;
        loadedStem.storedPath = file.getFullPathName();
        loadedStem.include = true;
        resolvedStems.push_back(std::move(loadedStem));
      }
    }

    std::stable_sort(resolvedStems.begin(), resolvedStems.end(),
                     [](const ImportedStemData &a, const ImportedStemData &b) {
                       return stemPriorityForStripOrder(a) <
                              stemPriorityForStripOrder(b);
                     });

    for (const auto &loadedStem : resolvedStems) {
      stemPaths.add(loadedStem.storedPath);
      loadedSong.stems.push_back(loadedStem);
      const bool isClickGuide = stemPriorityForStripOrder(loadedStem) <= 1;
      clickGuideMask.push_back(isClickGuide);
    }

    const bool loaded = p.loadSongStems(stemPaths, song.title, song.key,
                                        static_cast<double>(song.bpm));

    if (!loaded) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, "Load error",
          "Couldn't load audio stems for this song.");
      waveformBarRaw->setWaveformBins({});
      p.setStemClickGuideMask({});
      channelGridRaw->setSong(ImportedSongData{});
      return;
    }

    p.setStemClickGuideMask(clickGuideMask);

    channelGridRaw->setSong(loadedSong);
    if (mixerFaderRaw)
      mixerFaderRaw->setSongBpm(song.bpm);
    currentSongIndex = songIndex;

    auto cachedBins = SongLibraryStore::loadWaveformCache(loadedSong.id);
    if (cachedBins.empty()) {
      cachedBins = buildMixedWaveformBins(loadedSong);
      SongLibraryStore::saveWaveformCache(loadedSong.id, cachedBins);
    }

    waveformBarRaw->setWaveformBins(std::move(cachedBins));
  };

  songListRaw->setOnSongActivated(
      [loadSong](const ImportedSongData &song, int songIndex) {
        loadSong(song, songIndex);
      });

  auto loadSongByIndex = [songListRaw, loadSong](int index) {
    if (!songListRaw)
      return;
    if (const auto *song = songListRaw->getSongPtr(index))
      loadSong(*song, index);
  };

  auto prevSong = [songListRaw, loadSongByIndex, this]() {
    if (!songListRaw)
      return;
    const int count = songListRaw->getSongCount();
    if (count <= 0)
      return;
    int index = currentSongIndex;
    if (index < 0)
      index = songListRaw->getLastActivatedIndex();
    if (index < 0)
      index = 0;
    const int target = juce::jlimit(0, count - 1, index - 1);
    if (target == index)
      return;
    loadSongByIndex(target);
  };

  auto nextSong = [songListRaw, loadSongByIndex, this]() {
    if (!songListRaw)
      return;
    const int count = songListRaw->getSongCount();
    if (count <= 0)
      return;
    int index = currentSongIndex;
    if (index < 0)
      index = songListRaw->getLastActivatedIndex();
    if (index < 0)
      index = 0;
    const int target = juce::jlimit(0, count - 1, index + 1);
    if (target == index)
      return;
    loadSongByIndex(target);
  };

  prevSongAction = prevSong;
  nextSongAction = nextSong;

  topBarRaw->setTransportCallbacks(
      [&p] { p.toggleSongPlayPause(); }, [&p] { p.stopSong(); }, prevSong,
      nextSong, [&p](bool fadeOut) {
        p.setFadeStopsPlayback(false);
        p.setFadeActive(fadeOut);
      },
      [&p](double deltaSec) { p.seekSongRelative(deltaSec); },
      [&p](bool enabled) { p.setMonoClickMode(enabled); });
  topBarRaw->setOnSettingsWithAnchor(
      [this](juce::Rectangle<int> anchor, juce::Component *parent) {
        showSettings(anchor, parent);
      });

  setlistHandlePanel = std::make_unique<SetlistHandlePanel>("Setlist A");
  setlistHandlePanel->onToggle = [this] {
    isSetlistCollapsed = !isSetlistCollapsed;
    resized();
    repaint();
  };

  addAndMakeVisible(*topBar);
  addAndMakeVisible(*waveformBar);
  addAndMakeVisible(*mixerFaderPanel);
  addAndMakeVisible(*channelGridPanel);
  addAndMakeVisible(*songListPanel);
  addAndMakeVisible(*setlistHandlePanel);

  setWantsKeyboardFocus(true);
  grabKeyboardFocus();
  setSize(1200, 760);
}

void MTControlsAudioProcessorEditor::showSettings(juce::Rectangle<int>,
                                                  juce::Component *parent) {
  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = "Settings";
  options.dialogBackgroundColour = UITheme::appBg;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = false;
  auto *panel = new SettingsPanel(
      static_cast<ThemePreset>(currentThemeId),
      [this](ThemePreset preset) { applyThemePreset(static_cast<int>(preset)); },
      [] {});
  panel->setSize(272, 172);
  panel->refreshThemeColours();
  options.content.setOwned(panel);

  if (auto *window = options.launchAsync()) {
    window->centreAroundComponent(parent != nullptr ? parent : this, 272, 172);
  }
}

void MTControlsAudioProcessorEditor::applyThemePreset(int presetId) {
  currentThemeId = presetId;
  applyTheme(static_cast<ThemePreset>(presetId));
  if (refreshTopBarTheme)
    refreshTopBarTheme();
  repaint();
}

MTControlsAudioProcessorEditor::~MTControlsAudioProcessorEditor() = default;

void MTControlsAudioProcessorEditor::paint(juce::Graphics &g) {
  drawStudioBackground(g, getLocalBounds().toFloat());
}

void MTControlsAudioProcessorEditor::resized() {
  auto area = getLocalBounds();

  topBar->setBounds(area.removeFromTop(kTopBarH));
  waveformBar->setBounds(
      area.removeFromTop(isWaveformExpanded ? kWaveformExpandedH
                                            : kWaveformH));

  auto mainArea = area;
  constexpr int handleW = 28;
  int setlistWidth =
      juce::jlimit(280, 460, static_cast<int>(mainArea.getWidth() * 0.35f));
  if (isSetlistCollapsed)
    setlistWidth = 0;

  auto leftSetlistArea = mainArea.removeFromLeft(setlistWidth);
  auto handleArea = mainArea.removeFromLeft(handleW);
  auto rightChannelsArea = mainArea.reduced(8);
  constexpr int mixerPanelW = 120;
  auto mixerPanelArea = rightChannelsArea.removeFromLeft(mixerPanelW);
  mixerFaderPanel->setBounds(mixerPanelArea);
  channelGridPanel->setBounds(rightChannelsArea);

  if (isSetlistCollapsed) {
    songListPanel->setVisible(false);
    songListPanel->setBounds({});
  } else {
    songListPanel->setVisible(true);
    songListPanel->setBounds(leftSetlistArea.reduced(8));
  }

  setlistHandlePanel->setBounds(handleArea.reduced(2, 8));
  setlistHandlePanel->setCollapsed(isSetlistCollapsed);
}

bool MTControlsAudioProcessorEditor::keyPressed(const juce::KeyPress &key) {
  auto &processorRef = static_cast<MTControlsAudioProcessor &>(processor);

  if (key == juce::KeyPress::spaceKey) {
    processorRef.toggleSongPlayPause();
    return true;
  }

  if (key == juce::KeyPress::leftKey) {
    if (prevSongAction)
      prevSongAction();
    return true;
  }

  if (key == juce::KeyPress::rightKey) {
    if (nextSongAction)
      nextSongAction();
    return true;
  }

  if (key == juce::KeyPress::escapeKey) {
    processorRef.stopSong();
    return true;
  }

  return false;
}
