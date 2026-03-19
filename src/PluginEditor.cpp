#include "PluginEditor.h"
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

juce::String formatTimeSeconds(double seconds) {
  const int total = juce::jmax(0, static_cast<int>(std::floor(seconds + 0.5)));
  const int mm = total / 60;
  const int ss = total % 60;
  return juce::String(mm).paddedLeft('0', 2) + ":" +
         juce::String(ss).paddedLeft('0', 2);
}

constexpr int kTopBarH = 60;
constexpr int kWaveformH = 100;
constexpr int kWaveformExpandedH = 160;

struct UITheme {
  static juce::Colour appBg;
  static juce::Colour appBgTop;
  static juce::Colour appBgBottom;
  static juce::Colour panelDark;
  static juce::Colour cardBg;
  static juce::Colour cardBgHover;
  static juce::Colour cardBgPressed;
  static juce::Colour cardBorder;
  static juce::Colour textPrimary;
  static juce::Colour textSecondary;
  static juce::Colour accent;
  static juce::Colour accentSoft;
  static juce::Colour accentMuted;
  static juce::Colour accentMaster;
  static juce::Colour cardBgMaster;
  static juce::Colour cardBgClickGuide;
  static juce::Colour onAccent;
  static juce::Colour dividerSoft;
};

juce::Colour UITheme::appBg =
    juce::Colour::fromRGB(50, 46, 43); // #322E2B
juce::Colour UITheme::appBgTop =
    juce::Colour::fromRGB(56, 52, 49); // #383431
juce::Colour UITheme::appBgBottom =
    juce::Colour::fromRGB(70, 65, 61); // #46413D
juce::Colour UITheme::panelDark =
    juce::Colour::fromRGB(38, 34, 32); // #262220
juce::Colour UITheme::cardBg = juce::Colour::fromRGB(212, 212, 212);
juce::Colour UITheme::cardBgHover = juce::Colour::fromRGB(218, 218, 218);
juce::Colour UITheme::cardBgPressed =
    juce::Colour::fromRGB(201, 201, 201);
juce::Colour UITheme::cardBorder = juce::Colour::fromRGB(189, 189, 189);
juce::Colour UITheme::textPrimary = juce::Colour::fromRGB(78, 78, 78);
juce::Colour UITheme::textSecondary =
    juce::Colour::fromRGB(118, 118, 118);
juce::Colour UITheme::accent = juce::Colour::fromRGB(193, 122, 90);
juce::Colour UITheme::accentSoft = juce::Colour::fromRGB(196, 154, 108);
juce::Colour UITheme::accentMuted = juce::Colour::fromRGB(181, 124, 124);
juce::Colour UITheme::accentMaster = juce::Colour::fromRGB(163, 108, 74);
juce::Colour UITheme::cardBgMaster =
    juce::Colour::fromRGB(232, 216, 200); // #E8D8C8
juce::Colour UITheme::cardBgClickGuide =
    juce::Colour::fromRGB(200, 205, 214); // #C8CDD6
juce::Colour UITheme::onAccent = juce::Colours::white;
juce::Colour UITheme::dividerSoft =
    juce::Colour::fromRGBA(255, 255, 255, 20);

enum class ThemePreset { Default = 1, Spotify, Ableton };

void applyTheme(ThemePreset preset) {
  switch (preset) {
  case ThemePreset::Spotify:
    UITheme::appBg = juce::Colour::fromRGB(18, 18, 18);      // #121212
    UITheme::appBgTop = juce::Colour::fromRGB(25, 20, 20);   // #191414
    UITheme::appBgBottom = juce::Colour::fromRGB(29, 29, 29);
    UITheme::panelDark = juce::Colour::fromRGB(16, 16, 16);
    UITheme::cardBg = juce::Colour::fromRGB(28, 28, 28);
    UITheme::cardBgHover = juce::Colour::fromRGB(36, 36, 36);
    UITheme::cardBgPressed = juce::Colour::fromRGB(44, 44, 44);
    UITheme::cardBorder = juce::Colour::fromRGB(48, 48, 48);
    UITheme::textPrimary = juce::Colour::fromRGB(245, 245, 245);
    UITheme::textSecondary = juce::Colour::fromRGB(179, 179, 179);
    UITheme::accent = juce::Colour::fromRGB(29, 185, 84);    // #1DB954
    UITheme::accentSoft = juce::Colour::fromRGB(30, 215, 96); // #1ED760
    UITheme::accentMuted = juce::Colour::fromRGB(88, 140, 88);
    UITheme::accentMaster = juce::Colour::fromRGB(29, 185, 84);
    UITheme::cardBgMaster = juce::Colour::fromRGB(24, 24, 24);
    UITheme::cardBgClickGuide = juce::Colour::fromRGB(32, 32, 32);
    UITheme::onAccent = juce::Colours::black;
    UITheme::dividerSoft = juce::Colour::fromRGBA(255, 255, 255, 16);
    break;
  case ThemePreset::Ableton:
    UITheme::appBg = juce::Colour::fromRGB(30, 30, 30);
    UITheme::appBgTop = juce::Colour::fromRGB(36, 36, 36);
    UITheme::appBgBottom = juce::Colour::fromRGB(44, 44, 44);
    UITheme::panelDark = juce::Colour::fromRGB(24, 24, 24);
    UITheme::cardBg = juce::Colour::fromRGB(40, 40, 40);
    UITheme::cardBgHover = juce::Colour::fromRGB(48, 48, 48);
    UITheme::cardBgPressed = juce::Colour::fromRGB(58, 58, 58);
    UITheme::cardBorder = juce::Colour::fromRGB(68, 68, 68);
    UITheme::textPrimary = juce::Colour::fromRGB(230, 230, 230);
    UITheme::textSecondary = juce::Colour::fromRGB(160, 160, 160);
    UITheme::accent = juce::Colour::fromRGB(255, 179, 71);   // warm
    UITheme::accentSoft = juce::Colour::fromRGB(76, 182, 255); // cool
    UITheme::accentMuted = juce::Colour::fromRGB(180, 140, 95);
    UITheme::accentMaster = juce::Colour::fromRGB(255, 179, 71);
    UITheme::cardBgMaster = juce::Colour::fromRGB(46, 46, 46);
    UITheme::cardBgClickGuide = juce::Colour::fromRGB(54, 54, 54);
    UITheme::onAccent = juce::Colours::black;
    UITheme::dividerSoft = juce::Colour::fromRGBA(255, 255, 255, 16);
    break;
  case ThemePreset::Default:
  default:
    UITheme::appBg = juce::Colour::fromRGB(50, 46, 43);
    UITheme::appBgTop = juce::Colour::fromRGB(56, 52, 49);
    UITheme::appBgBottom = juce::Colour::fromRGB(70, 65, 61);
    UITheme::panelDark = juce::Colour::fromRGB(38, 34, 32);
    UITheme::cardBg = juce::Colour::fromRGB(212, 212, 212);
    UITheme::cardBgHover = juce::Colour::fromRGB(218, 218, 218);
    UITheme::cardBgPressed = juce::Colour::fromRGB(201, 201, 201);
    UITheme::cardBorder = juce::Colour::fromRGB(189, 189, 189);
    UITheme::textPrimary = juce::Colour::fromRGB(78, 78, 78);
    UITheme::textSecondary = juce::Colour::fromRGB(118, 118, 118);
    UITheme::accent = juce::Colour::fromRGB(193, 122, 90);
    UITheme::accentSoft = juce::Colour::fromRGB(196, 154, 108);
    UITheme::accentMuted = juce::Colour::fromRGB(181, 124, 124);
    UITheme::accentMaster = juce::Colour::fromRGB(163, 108, 74);
    UITheme::cardBgMaster = juce::Colour::fromRGB(232, 216, 200);
    UITheme::cardBgClickGuide = juce::Colour::fromRGB(200, 205, 214);
    UITheme::onAccent = juce::Colours::white;
    UITheme::dividerSoft = juce::Colour::fromRGBA(255, 255, 255, 20);
    break;
  }
}

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

struct ImportedStemData {
  juce::String sourcePath;
  juce::String storedPath;
  juce::String fileName;
  juce::String displayName;
  juce::String category;
  bool include = true;
  double durationSec = 0.0;
  int sampleRate = 0;
  int channels = 0;
};

struct ImportedSongData {
  juce::String id;
  juce::String title;
  juce::String artist;
  int bpm = 0;
  juce::String key;
  juce::String timeSignature;
  juce::String fileType;
  int64_t createdMs = 0;
  std::vector<ImportedStemData> stems;
};

constexpr std::array<const char *, 11> kTrackCategories = {{
    "Bass",
    "Click",
    "Accordion",
    "Cues",
    "Drums",
    "Acoustic",
    "Keys",
    "Percussion",
    "Piano",
    "Vocals",
    "Other",
}};

juce::String makeDisplayNameFromFile(const juce::File &file) {
  auto name = file.getFileNameWithoutExtension().trim();
  if (name.isEmpty())
    return "STEM";

  name = name.replaceCharacter('_', ' ')
             .replaceCharacter('-', ' ')
             .replaceCharacter('.', ' ')
             .toUpperCase();
  return name;
}

juce::String suggestCategoryFromFilename(const juce::String &filename) {
  const auto lower = filename.toLowerCase();
  if (lower.contains("click"))
    return "Click";
  if (lower.contains("cue") || lower.contains("guide"))
    return "Cues";
  if (lower.contains("bass"))
    return "Bass";
  if (lower.contains("drum") || lower.contains("kick") ||
      lower.contains("snare"))
    return "Drums";
  if (lower.contains("perc"))
    return "Percussion";
  if (lower.contains("piano"))
    return "Piano";
  if (lower.contains("key") || lower.contains("synth"))
    return "Keys";
  if (lower.contains("vox") || lower.contains("vocal"))
    return "Vocals";
  if (lower.contains("acoustic") || lower.contains("gtr") ||
      lower.contains("guitar"))
    return "Acoustic";
  return "Other";
}

int stemPriorityForStripOrder(const ImportedStemData &stem) {
  const auto bucket =
      (stem.displayName + " " + stem.category + " " + stem.fileName)
          .toLowerCase();

  if (bucket.contains("click") || bucket.contains("clic"))
    return 0;
  if (bucket.contains("guia") || bucket.contains("guide") ||
      bucket.contains("cue"))
    return 1;
  return 2;
}

std::vector<float> buildMixedWaveformBins(const ImportedSongData &song,
                                          int binCount = 1400) {
  if (binCount <= 0)
    return {};

  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();

  std::vector<juce::File> files;
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;

    juce::File file(stem.storedPath);
    if (!file.existsAsFile())
      file = juce::File(stem.sourcePath);
    if (!file.existsAsFile())
      continue;

    files.push_back(std::move(file));
  }

  if (files.empty())
    return {};

  juce::int64 maxLengthSamples = 0;
  for (const auto &file : files) {
    if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager.createReaderFor(file))) {
      maxLengthSamples = juce::jmax(maxLengthSamples, reader->lengthInSamples);
    }
  }

  if (maxLengthSamples <= 0)
    return {};

  std::vector<float> mixedBins(static_cast<size_t>(binCount), 0.0f);
  std::vector<float> stemBins(static_cast<size_t>(binCount), 0.0f);

  constexpr int blockSize = 4096;
  for (const auto &file : files) {
    std::fill(stemBins.begin(), stemBins.end(), 0.0f);

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager.createReaderFor(file));
    if (reader == nullptr)
      continue;

    const int channels = juce::jmax(1, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> readBuffer(channels, blockSize);

    juce::int64 readPos = 0;
    while (readPos < reader->lengthInSamples) {
      const int toRead = static_cast<int>(juce::jmin<juce::int64>(
          blockSize, reader->lengthInSamples - readPos));
      if (!reader->read(&readBuffer, 0, toRead, readPos, true, true))
        break;

      for (int sample = 0; sample < toRead; ++sample) {
        float mono = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
          mono += readBuffer.getSample(ch, sample);
        mono /= static_cast<float>(channels);

        const auto globalPos = readPos + static_cast<juce::int64>(sample);
        const int bin = juce::jlimit(
            0, binCount - 1,
            static_cast<int>((globalPos * static_cast<juce::int64>(binCount)) /
                             maxLengthSamples));

        stemBins[static_cast<size_t>(bin)] =
            juce::jmax(stemBins[static_cast<size_t>(bin)], std::abs(mono));
      }

      readPos += toRead;
    }

    for (int i = 0; i < binCount; ++i)
      mixedBins[static_cast<size_t>(i)] += stemBins[static_cast<size_t>(i)];
  }

  const auto maxIt = std::max_element(mixedBins.begin(), mixedBins.end());
  const float maxValue = (maxIt != mixedBins.end()) ? *maxIt : 0.0f;
  if (maxValue <= 0.0f)
    return mixedBins;

  for (auto &v : mixedBins) {
    const float normal = juce::jlimit(0.0f, 1.0f, v / maxValue);
    v = std::sqrt(normal);
  }

  return mixedBins;
}

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

enum class IconGlyph {
  Prev,
  SeekBack,
  Play,
  Pause,
  Stop,
  SeekForward,
  Next,
  Loop,
  FadeOut,
  FadeIn,
  Pencil,
  Settings,
  More,
  Add,
  Metronome
};

void drawIconGlyph(juce::Graphics &g, juce::Rectangle<float> area,
                   IconGlyph glyph, juce::Colour colour, float stroke = 1.7f) {
  g.setColour(colour);
  area = area.reduced(area.getWidth() * 0.22f, area.getHeight() * 0.22f);

  switch (glyph) {
  case IconGlyph::Play: {
    juce::Path p;
    p.addTriangle(area.getX(), area.getY(), area.getX(), area.getBottom(),
                  area.getRight(), area.getCentreY());
    g.fillPath(p);
    break;
  }
  case IconGlyph::Pause: {
    const float w = area.getWidth();
    const float h = area.getHeight();
    g.fillRoundedRectangle(area.getX() + (w * 0.20f), area.getY(), w * 0.22f, h,
                           1.2f);
    g.fillRoundedRectangle(area.getRight() - (w * 0.42f), area.getY(),
                           w * 0.22f, h, 1.2f);
    break;
  }
  case IconGlyph::Stop:
    g.fillRoundedRectangle(area.withSizeKeepingCentre(area.getWidth() * 0.72f,
                                                      area.getHeight() * 0.72f),
                           1.5f);
    break;
  case IconGlyph::Prev: {
    const float lineX = area.getX() + 1.0f;
    g.drawLine(lineX, area.getY(), lineX, area.getBottom(), stroke);
    juce::Path p;
    p.addTriangle(area.getRight(), area.getY(), area.getRight(),
                  area.getBottom(), area.getX() + 2.5f, area.getCentreY());
    g.fillPath(p);
    break;
  }
  case IconGlyph::SeekBack: {
    const float lineX = area.getX() + 2.0f;
    g.drawLine(lineX, area.getY(), lineX, area.getBottom(), stroke);
    juce::Path p1;
    p1.addTriangle(area.getCentreX(), area.getY(), area.getCentreX(),
                   area.getBottom(), area.getX() + 4.5f, area.getCentreY());
    g.fillPath(p1);
    juce::Path p2;
    p2.addTriangle(area.getRight() - 1.0f, area.getY(), area.getRight() - 1.0f,
                   area.getBottom(), area.getCentreX() + 2.5f,
                   area.getCentreY());
    g.fillPath(p2);
    break;
  }
  case IconGlyph::Next: {
    const float lineX = area.getRight() - 1.0f;
    g.drawLine(lineX, area.getY(), lineX, area.getBottom(), stroke);
    juce::Path p;
    p.addTriangle(area.getX(), area.getY(), area.getX(), area.getBottom(),
                  area.getRight() - 2.5f, area.getCentreY());
    g.fillPath(p);
    break;
  }
  case IconGlyph::SeekForward: {
    const float lineX = area.getRight() - 2.0f;
    g.drawLine(lineX, area.getY(), lineX, area.getBottom(), stroke);
    juce::Path p1;
    p1.addTriangle(area.getX() + 1.0f, area.getY(), area.getX() + 1.0f,
                   area.getBottom(), area.getCentreX() - 2.5f,
                   area.getCentreY());
    g.fillPath(p1);
    juce::Path p2;
    p2.addTriangle(area.getCentreX(), area.getY(), area.getCentreX(),
                   area.getBottom(), area.getRight() - 4.5f,
                   area.getCentreY());
    g.fillPath(p2);
    break;
  }
  case IconGlyph::Loop: {
    juce::Path arc;
    arc.addCentredArc(area.getCentreX(), area.getCentreY(),
                      area.getWidth() * 0.45f, area.getHeight() * 0.45f, 0.0f,
                      0.35f, 5.85f, true);
    g.strokePath(arc, juce::PathStrokeType(stroke, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
    juce::Path arrow;
    const float tx = area.getRight() - 1.0f;
    const float ty = area.getCentreY() - area.getHeight() * 0.20f;
    arrow.addTriangle(tx - 4.5f, ty - 2.0f, tx + 0.5f, ty, tx - 4.5f,
                      ty + 2.0f);
    g.fillPath(arrow);
    break;
  }
  case IconGlyph::FadeOut: {
    g.drawLine(area.getX(), area.getY(), area.getRight(), area.getBottom(),
               stroke);
    break;
  }
  case IconGlyph::FadeIn: {
    g.drawLine(area.getX(), area.getBottom(), area.getRight(), area.getY(),
               stroke);
    break;
  }
  case IconGlyph::Pencil: {
    juce::Path body;
    body.startNewSubPath(area.getX() + 2.0f, area.getBottom() - 2.0f);
    body.lineTo(area.getX() + 4.5f, area.getBottom() - 4.5f);
    body.lineTo(area.getRight() - 2.5f, area.getY() + 2.5f);
    body.lineTo(area.getRight() - 5.0f, area.getY());
    body.lineTo(area.getX() + 2.0f, area.getBottom() - 7.0f);
    body.closeSubPath();
    g.fillPath(body);
    break;
  }
  case IconGlyph::Settings: {
    const auto ring = area.withSizeKeepingCentre(area.getWidth() * 0.64f,
                                                 area.getHeight() * 0.64f);
    g.drawEllipse(ring, stroke);
    for (int i = 0; i < 6; ++i) {
      const float a =
          juce::MathConstants<float>::twoPi * (static_cast<float>(i) / 6.0f);
      const float x1 =
          ring.getCentreX() + std::cos(a) * (ring.getWidth() * 0.50f);
      const float y1 =
          ring.getCentreY() + std::sin(a) * (ring.getHeight() * 0.50f);
      const float x2 =
          ring.getCentreX() + std::cos(a) * (ring.getWidth() * 0.72f);
      const float y2 =
          ring.getCentreY() + std::sin(a) * (ring.getHeight() * 0.72f);
      g.drawLine(x1, y1, x2, y2, stroke);
    }
    break;
  }
  case IconGlyph::More: {
    const float dot = 2.0f;
    const float y = area.getCentreY() - dot;
    g.fillEllipse(area.getX(), y, dot * 2.0f, dot * 2.0f);
    g.fillEllipse(area.getCentreX() - dot, y, dot * 2.0f, dot * 2.0f);
    g.fillEllipse(area.getRight() - dot * 2.0f, y, dot * 2.0f, dot * 2.0f);
    break;
  }
  case IconGlyph::Add: {
    const auto ring = area.withSizeKeepingCentre(area.getWidth() * 0.86f,
                                                 area.getHeight() * 0.86f);
    g.drawEllipse(ring, stroke);
    g.drawLine(ring.getCentreX(), ring.getY() + 2.0f, ring.getCentreX(),
               ring.getBottom() - 2.0f, stroke);
    g.drawLine(ring.getX() + 2.0f, ring.getCentreY(), ring.getRight() - 2.0f,
               ring.getCentreY(), stroke);
    break;
  }
  case IconGlyph::Metronome: {
    juce::Path body;
    body.startNewSubPath(area.getX() + 1.0f, area.getBottom());
    body.lineTo(area.getCentreX(), area.getY() + 1.5f);
    body.lineTo(area.getRight() - 1.0f, area.getBottom());
    body.closeSubPath();
    g.strokePath(body, juce::PathStrokeType(stroke));
    g.drawLine(area.getCentreX(), area.getY() + 6.0f, area.getCentreX(),
               area.getBottom() - 3.0f, stroke * 0.9f);
    g.drawLine(area.getCentreX(), area.getCentreY() + 1.0f,
               area.getRight() - 3.0f, area.getY() + 4.0f, stroke * 0.9f);
    break;
  }
  }
}

juce::Image createNoiseTile() {
  juce::Image img(juce::Image::ARGB, 64, 64, true);
  juce::Random rng(0x4D5458);

  for (int y = 0; y < img.getHeight(); ++y) {
    for (int x = 0; x < img.getWidth(); ++x) {
      const float roll = rng.nextFloat();
      if (roll < 0.10f) {
        const auto alpha = static_cast<juce::uint8>(4 + rng.nextInt(7));
        img.setPixelAt(x, y, juce::Colours::white.withAlpha(alpha / 255.0f));
      }
    }
  }

  return img;
}

const juce::Image &getNoiseTile() {
  static const juce::Image noise = createNoiseTile();
  return noise;
}

void drawStudioBackground(juce::Graphics &g, juce::Rectangle<float> area) {
  juce::ColourGradient base(UITheme::appBgTop, area.getCentreX(), area.getY(),
                            UITheme::appBgBottom, area.getCentreX(),
                            area.getBottom(), false);
  g.setGradientFill(base);
  g.fillRect(area);

  juce::ColourGradient warmHalo(
      UITheme::accent.withAlpha(0.06f), area.getX() + area.getWidth() * 0.16f,
      area.getY() + area.getHeight() * 0.18f, UITheme::accent.withAlpha(0.0f),
      area.getX() + area.getWidth() * 0.68f,
      area.getY() + area.getHeight() * 0.80f, true);
  g.setGradientFill(warmHalo);
  g.fillRect(area);

  const float edge = 84.0f;
  const auto vignette = juce::Colours::black.withAlpha(0.08f);
  g.setGradientFill(juce::ColourGradient(
      vignette, area.getX(), area.getCentreY(), juce::Colours::transparentBlack,
      area.getX() + edge, area.getCentreY(), false));
  g.fillRect(area.getX(), area.getY(), edge, area.getHeight());
  g.setGradientFill(
      juce::ColourGradient(vignette, area.getRight(), area.getCentreY(),
                           juce::Colours::transparentBlack,
                           area.getRight() - edge, area.getCentreY(), false));
  g.fillRect(area.getRight() - edge, area.getY(), edge, area.getHeight());

  g.setTiledImageFill(getNoiseTile(), static_cast<int>(area.getX()),
                      static_cast<int>(area.getY()), 0.14f);
  g.fillRect(area);
}
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
  class TopBar final : public juce::Component, private juce::Timer {
  public:
    class IconButton final : public juce::Button {
    public:
      IconButton(juce::String name, IconGlyph g, bool primary = false)
          : juce::Button(std::move(name)), glyph(g), isPrimary(primary) {}

      void setGlyph(IconGlyph g) {
        glyph = g;
        repaint();
      }

      void setPulseAmount(float amount, bool downbeat) {
        pulseAmount = juce::jlimit(0.0f, 1.0f, amount);
        pulseDownbeat = downbeat;
        repaint();
      }

      void paintButton(juce::Graphics &gr, bool isMouseOverButton,
                       bool isButtonDown) override {
        auto b = getLocalBounds().toFloat();
        juce::Colour fill = isPrimary ? UITheme::accent : UITheme::cardBg;
        if (isMouseOverButton)
          fill = fill.brighter(0.06f);
        if (isButtonDown || getToggleState())
          fill = isPrimary ? UITheme::accentSoft : UITheme::cardBgPressed;
        if (pulseAmount > 0.001f) {
          const auto pulseColour =
              pulseDownbeat ? UITheme::accentSoft : UITheme::accent;
          fill = fill.interpolatedWith(pulseColour, 0.25f + (pulseAmount * 0.35f));
        }

        gr.setColour(fill);
        gr.fillRoundedRectangle(b, 6.0f);
        gr.setColour(UITheme::cardBorder.withAlpha(0.72f));
        gr.drawRoundedRectangle(b, 6.0f, 1.0f);
        drawIconGlyph(gr, b, glyph,
                      isPrimary ? UITheme::onAccent : UITheme::textPrimary);
      }

    private:
      IconGlyph glyph;
      bool isPrimary = false;
      float pulseAmount = 0.0f;
      bool pulseDownbeat = false;
    };

    class ClickableLabel final : public juce::Label {
    public:
      std::function<void()> onClick;

      void mouseDown(const juce::MouseEvent &) override {
        if (onClick)
          onClick();
      }
    };

    class SongWindowPopover final : public juce::Component {
    public:
      SongWindowPopover(MTControlsAudioProcessor &processorRef)
          : processor(processorRef) {
        configureRow(startLabel, startMinus, startValue, startPlus, "Start");
        configureRow(endLabel, endMinus, endValue, endPlus, "End");
        configureRow(fadeLabel, fadeMinus, fadeValue, fadePlus, "Fade out");

        startMinus.onClick = [this] { adjustStart(-stepSeconds()); };
        startPlus.onClick = [this] { adjustStart(stepSeconds()); };
        endMinus.onClick = [this] { adjustEnd(-stepSeconds()); };
        endPlus.onClick = [this] { adjustEnd(stepSeconds()); };
        fadeMinus.onClick = [this] { adjustFade(-stepSeconds()); };
        fadePlus.onClick = [this] { adjustFade(stepSeconds()); };

        refreshValues();
        applyTheme();
      }

      void refreshValues() {
        startValue.setText(formatTimeSeconds(processor.getSongStartSeconds()),
                           juce::dontSendNotification);
        endValue.setText(formatTimeSeconds(processor.getSongEndSeconds()),
                         juce::dontSendNotification);
        const int fadeSecs =
            static_cast<int>(std::round(processor.getSongFadeOutSeconds()));
        fadeValue.setText(juce::String(fadeSecs) + " sec",
                          juce::dontSendNotification);
      }

      void applyTheme() {
        for (auto *label : {&startLabel, &endLabel, &fadeLabel}) {
          label->setColour(juce::Label::textColourId, UITheme::textSecondary);
          label->setFont(juce::FontOptions(11.0f));
        }
        for (auto *value : {&startValue, &endValue, &fadeValue}) {
          value->setColour(juce::Label::textColourId, UITheme::textPrimary);
          value->setColour(juce::Label::backgroundColourId,
                           UITheme::cardBgHover);
          value->setColour(juce::Label::outlineColourId,
                           UITheme::cardBorder.withAlpha(0.65f));
          value->setJustificationType(juce::Justification::centred);
          value->setFont(juce::FontOptions(12.0f));
        }
        for (auto *btn :
             {&startMinus, &startPlus, &endMinus, &endPlus, &fadeMinus,
              &fadePlus}) {
          btn->setColour(juce::TextButton::buttonColourId,
                         UITheme::cardBgHover);
          btn->setColour(juce::TextButton::buttonOnColourId,
                         UITheme::cardBgPressed);
          btn->setColour(juce::TextButton::textColourOffId,
                         UITheme::textPrimary);
          btn->setViewportIgnoreDragFlag(true);
        }
      }

      void paint(juce::Graphics &g) override {
        auto b = getLocalBounds().toFloat();
        g.setColour(UITheme::cardBg);
        g.fillRoundedRectangle(b, 8.0f);
        g.setColour(UITheme::cardBorder);
        g.drawRoundedRectangle(b, 8.0f, 1.0f);
      }

      void resized() override {
        auto area = getLocalBounds().reduced(12);
        const int rowH = 28;
        layoutRow(area.removeFromTop(rowH), startLabel, startMinus, startValue,
                  startPlus);
        area.removeFromTop(8);
        layoutRow(area.removeFromTop(rowH), endLabel, endMinus, endValue,
                  endPlus);
        area.removeFromTop(8);
        layoutRow(area.removeFromTop(rowH), fadeLabel, fadeMinus, fadeValue,
                  fadePlus);
      }

    private:
      MTControlsAudioProcessor &processor;
      juce::Label startLabel, endLabel, fadeLabel;
      juce::TextButton startMinus, startPlus, endMinus, endPlus, fadeMinus,
          fadePlus;
      juce::Label startValue, endValue, fadeValue;

      void configureRow(juce::Label &label, juce::TextButton &minus,
                        juce::Label &value, juce::TextButton &plus,
                        const juce::String &text) {
        label.setText(text, juce::dontSendNotification);
        addAndMakeVisible(label);

        minus.setButtonText("-");
        plus.setButtonText("+");
        addAndMakeVisible(minus);
        addAndMakeVisible(plus);

        addAndMakeVisible(value);
      }

      void layoutRow(juce::Rectangle<int> row, juce::Label &label,
                     juce::TextButton &minus, juce::Label &value,
                     juce::TextButton &plus) {
        const int labelW = 60;
        const int btnW = 28;
        label.setBounds(row.removeFromLeft(labelW));
        minus.setBounds(row.removeFromLeft(btnW));
        plus.setBounds(row.removeFromRight(btnW));
        value.setBounds(row);
      }

      double stepSeconds() const {
        const auto mods = juce::ModifierKeys::getCurrentModifiers();
        return mods.isShiftDown() ? 5.0 : 1.0;
      }

      void adjustStart(double delta) {
        processor.setSongStartSeconds(processor.getSongStartSeconds() + delta);
        refreshValues();
      }

      void adjustEnd(double delta) {
        processor.setSongEndSeconds(processor.getSongEndSeconds() + delta);
        refreshValues();
      }

      void adjustFade(double delta) {
        processor.setSongFadeOutSeconds(
            processor.getSongFadeOutSeconds() + delta);
        refreshValues();
      }
    };

    TopBar(MTControlsAudioProcessor &processorRef)
        : prevButton("prev", IconGlyph::Prev),
          seekBackButton("seekBack", IconGlyph::SeekBack),
          playButton("play", IconGlyph::Play, true),
          stopButton("stop", IconGlyph::Stop),
          seekForwardButton("seekForward", IconGlyph::SeekForward),
          nextButton("next", IconGlyph::Next),
          fadeButton("fade", IconGlyph::FadeOut),
          settingsButton("settings", IconGlyph::Settings),
          processor(processorRef) {
      monoClickButton.setButtonText("MONO/CLICK");
      monoClickButton.setClickingTogglesState(true);
      monoClickButton.setViewportIgnoreDragFlag(true);
      monoClickButton.setColour(juce::TextButton::buttonColourId,
                                UITheme::cardBgHover);
      monoClickButton.setColour(juce::TextButton::buttonOnColourId,
                                UITheme::cardBgPressed);
      monoClickButton.setColour(juce::TextButton::textColourOffId,
                                UITheme::textPrimary);
      monoClickButton.setColour(juce::TextButton::textColourOnId,
                                UITheme::textPrimary);

      for (auto *btn :
           {&prevButton, &seekBackButton, &playButton, &stopButton,
            &seekForwardButton, &nextButton, &fadeButton, &settingsButton}) {
        addAndMakeVisible(*btn);
      }
      addAndMakeVisible(monoClickButton);

      configureDisplay(timeDisplay, "00:00 / 00:00");
      configureDisplay(keyDisplay, "Key");
      timeDisplay.onClick = [this] { showSongWindowPopover(); };
      configureMetronomeControls();

      prevButton.onClick = [this] {
        clearFadeIfActive();
        if (onPrevSong)
          onPrevSong();
      };
      seekBackButton.onClick = [this] {
        clearFadeIfActive();
        if (onSeekRelative)
          onSeekRelative(-5.0);
      };
      seekForwardButton.onClick = [this] {
        clearFadeIfActive();
        if (onSeekRelative)
          onSeekRelative(5.0);
      };
      nextButton.onClick = [this] {
        clearFadeIfActive();
        if (onNextSong)
          onNextSong();
      };
      fadeButton.onClick = [this] {
        const bool shouldFadeOut = !processor.isFadeActive();
        if (onFadeToggle)
          onFadeToggle(shouldFadeOut);
      };
      playButton.onClick = [this] {
        clearFadeIfActive();
        if (onPlayPause)
          onPlayPause();
        refreshFromProviders();
      };
      stopButton.onClick = [this] {
        clearFadeIfActive();
        if (onStop)
          onStop();
        refreshFromProviders();
      };
      settingsButton.onClick = [this] {
        clearFadeIfActive();
        if (onSettingsWithAnchor)
          onSettingsWithAnchor(settingsButton.getBounds(), this);
        else if (onSettings)
          onSettings();
      };
      monoClickButton.onClick = [this] {
        if (onMonoClickToggle)
          onMonoClickToggle(monoClickButton.getToggleState());
      };
      bpmSegmentButton.onClick = [this] { showMetronomePopover(); };
      metroToggleButton.onClick = [this] {
        processor.setMetronomeEnabled(metroToggleButton.getToggleState());
      };
      startTimerHz(20);
    }

    ~TopBar() override { stopTimer(); }

    void
    setTransportCallbacks(std::function<void()> playPauseCallback,
                          std::function<void()> stopCallback,
                          std::function<void()> prevSongCallback,
                          std::function<void()> nextSongCallback,
                          std::function<void(bool)> fadeToggleCallback,
                          std::function<void(double)> seekRelativeCallback,
                          std::function<void(bool)> monoClickCallback) {
      onPlayPause = std::move(playPauseCallback);
      onStop = std::move(stopCallback);
      onPrevSong = std::move(prevSongCallback);
      onNextSong = std::move(nextSongCallback);
      onFadeToggle = std::move(fadeToggleCallback);
      onSeekRelative = std::move(seekRelativeCallback);
      onMonoClickToggle = std::move(monoClickCallback);
    }

    void setTransportProviders(std::function<bool()> playingProviderFn,
                               std::function<double()> positionProviderFn,
                               std::function<double()> lengthProviderFn,
                               std::function<juce::String()> keyProviderFn) {
      playingProvider = std::move(playingProviderFn);
      positionProvider = std::move(positionProviderFn);
      lengthProvider = std::move(lengthProviderFn);
      keyProvider = std::move(keyProviderFn);
      refreshFromProviders();
    }

    void setMonoClickProvider(std::function<bool()> monoClickProviderFn) {
      monoClickProvider = std::move(monoClickProviderFn);
      if (monoClickProvider)
        monoClickButton.setToggleState(monoClickProvider(),
                                       juce::dontSendNotification);
    }

    void setOnSettings(std::function<void()> cb) {
      onSettings = std::move(cb);
    }

    void setOnSettingsWithAnchor(
        std::function<void(juce::Rectangle<int>, juce::Component *)> cb) {
      onSettingsWithAnchor = std::move(cb);
    }

    void refreshThemeColours() {
      timeDisplay.setColour(juce::Label::textColourId, UITheme::textPrimary);
      timeDisplay.setColour(juce::Label::backgroundColourId, UITheme::cardBg);
      timeDisplay.setColour(juce::Label::outlineColourId,
                            UITheme::cardBorder.withAlpha(0.55f));
      keyDisplay.setColour(juce::Label::textColourId, UITheme::textPrimary);
      keyDisplay.setColour(juce::Label::backgroundColourId, UITheme::cardBg);
      keyDisplay.setColour(juce::Label::outlineColourId,
                           UITheme::cardBorder.withAlpha(0.55f));
      monoClickButton.setColour(juce::TextButton::buttonColourId,
                                UITheme::cardBgHover);
      monoClickButton.setColour(juce::TextButton::buttonOnColourId,
                                UITheme::cardBgPressed);
      monoClickButton.setColour(juce::TextButton::textColourOffId,
                                UITheme::textPrimary);
      monoClickButton.setColour(juce::TextButton::textColourOnId,
                                UITheme::textPrimary);
      bpmSegmentButton.setColour(juce::TextButton::buttonColourId, UITheme::accent);
      bpmSegmentButton.setColour(juce::TextButton::buttonOnColourId,
                                 UITheme::accentSoft);
      bpmSegmentButton.setColour(juce::TextButton::textColourOffId,
                                 UITheme::onAccent);
      bpmSegmentButton.setColour(juce::TextButton::textColourOnId,
                                 UITheme::onAccent);
      repaint();
    }

    void paint(juce::Graphics &g) override {
      const auto b = getLocalBounds().toFloat();
      drawStudioBackground(g, b);
      g.setColour(UITheme::panelDark.withAlpha(0.10f));
      g.fillRect(b);
      g.setColour(UITheme::dividerSoft);
      g.drawLine(0.0f, static_cast<float>(getHeight()) - 0.5f,
                 static_cast<float>(getWidth()),
                 static_cast<float>(getHeight()) - 0.5f, 1.0f);
    }

    void resized() override {
      const int y = (getHeight() - 40) / 2;
      const int square = 44;
      const int gap = 8;

      int x = 12;

      auto placeSquare = [&x, y](juce::Component &c) {
        c.setBounds(x, y - 2, square, square);
        x += square + gap;
      };

      placeSquare(prevButton);
      placeSquare(seekBackButton);
      placeSquare(playButton);
      placeSquare(stopButton);
      placeSquare(seekForwardButton);
      placeSquare(nextButton);
      placeSquare(fadeButton);

      int rightX = getWidth() - 12;
      settingsButton.setBounds(rightX - square, y - 2, square, square);
      rightX -= square + gap;
      const int monoW = 92;
      monoClickButton.setBounds(rightX - monoW, y + 2, monoW, 36);
      rightX -= monoW + gap;
      keyDisplay.setBounds(rightX - 50, y, 50, 40);
      rightX -= 50 + gap;
      const int metroTotalW = 110;
      const int metroIconW = 44;
      const int metroY = y - 2;
      bpmSegmentButton.setBounds(rightX - metroTotalW, metroY,
                                 metroTotalW - metroIconW, 44);
      metroToggleButton.setBounds(rightX - metroIconW, metroY, metroIconW, 44);
      rightX -= metroTotalW + gap;
      timeDisplay.setBounds(rightX - 120, y, 120, 40);

      if (timeDisplay.getX() < x + 8) {
        const int shift = (x + 8) - timeDisplay.getX();
        for (auto *c :
             {static_cast<juce::Component *>(&timeDisplay),
              static_cast<juce::Component *>(&bpmSegmentButton),
              static_cast<juce::Component *>(&metroToggleButton),
              static_cast<juce::Component *>(&keyDisplay),
              static_cast<juce::Component *>(&monoClickButton),
              static_cast<juce::Component *>(&settingsButton)}) {
          c->setTopLeftPosition(c->getX() + shift, c->getY());
        }
      }
    }

  private:
    IconButton prevButton;
    IconButton seekBackButton;
    IconButton playButton;
    IconButton stopButton;
    IconButton seekForwardButton;
    IconButton nextButton;
    IconButton fadeButton;
    IconButton settingsButton;

    ClickableLabel timeDisplay;
    juce::Label keyDisplay;
    juce::TextButton monoClickButton;
    juce::TextButton bpmSegmentButton;
    IconButton metroToggleButton{"metroToggle", IconGlyph::Metronome};

    std::function<void()> onPlayPause;
    std::function<void()> onStop;
    std::function<void()> onPrevSong;
    std::function<void()> onNextSong;
    std::function<void(bool)> onFadeToggle;
    std::function<void(double)> onSeekRelative;
    std::function<void(bool)> onMonoClickToggle;
    std::function<void()> onSettings;
    std::function<void(juce::Rectangle<int>, juce::Component *)>
        onSettingsWithAnchor;

    void clearFadeIfActive() {
      if (!processor.isFadeActive())
        return;
      if (onFadeToggle)
        onFadeToggle(false);
    }

    std::function<bool()> playingProvider;
    std::function<double()> positionProvider;
    std::function<double()> lengthProvider;
    std::function<juce::String()> keyProvider;
    std::function<bool()> monoClickProvider;
    MTControlsAudioProcessor &processor;
    std::deque<double> tapTimes;
    double lastTickTime = 0.0;
    double beatCounter = 0.0;
    int lastBeatIndex = -1;
    float beatFlash = 0.0f;
    bool lastBeatWasDownbeat = true;
    int beatsPerBar = 4;
    juce::String timeSignature = "4/4";

    void configureDisplay(juce::Label &label, const juce::String &text) {
      label.setText(text, juce::dontSendNotification);
      label.setJustificationType(juce::Justification::centred);
      label.setFont(juce::FontOptions(13.0f));
      label.setColour(juce::Label::textColourId, UITheme::textPrimary);
      label.setColour(juce::Label::backgroundColourId, UITheme::cardBg);
      label.setColour(juce::Label::outlineColourId,
                      UITheme::cardBorder.withAlpha(0.55f));
      addAndMakeVisible(label);
    }

    void configureMetronomeControls() {
      bpmSegmentButton.setButtonText("120 ▼");
      bpmSegmentButton.setViewportIgnoreDragFlag(true);
      bpmSegmentButton.setColour(juce::TextButton::buttonColourId, UITheme::accent);
      bpmSegmentButton.setColour(juce::TextButton::buttonOnColourId,
                                 UITheme::accentSoft);
      bpmSegmentButton.setColour(juce::TextButton::textColourOffId,
                                 UITheme::onAccent);
      bpmSegmentButton.setColour(juce::TextButton::textColourOnId,
                                 UITheme::onAccent);
      bpmSegmentButton.setConnectedEdges(juce::Button::ConnectedOnRight);
      addAndMakeVisible(bpmSegmentButton);

      metroToggleButton.setClickingTogglesState(true);
      metroToggleButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
      addAndMakeVisible(metroToggleButton);
    }

    void timerCallback() override {
      refreshFromProviders();
      const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
      if (lastTickTime == 0.0)
        lastTickTime = now;
      const double dt = now - lastTickTime;
      lastTickTime = now;

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
      metroToggleButton.setToggleState(processor.isMetronomeEnabled(),
                                       juce::dontSendNotification);
      metroToggleButton.setPulseAmount(
          processor.isMetronomeEnabled() ? beatFlash : 0.0f,
          lastBeatWasDownbeat);
    }

    void updateBpmDisplay() {
      const int bpm = static_cast<int>(std::round(processor.getMetronomeBpm()));
      bpmSegmentButton.setButtonText((bpm > 0 ? juce::String(bpm) : "120") + " ▼");
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

    void setBpmAbsolute(double bpm) {
      if (bpm <= 0.0)
        return;
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

    void showMetronomePopover() {
      class MetronomePopover final : public juce::Component {
      public:
        MetronomePopover(std::function<void()> onTapCb,
                         std::function<void(const juce::String &)> onTimeSigCb,
                         std::function<void(double)> onSetBpmCb,
                         std::function<void(double)> onAdjustBpmCb,
                         std::function<double()> bpmProviderCb,
                         const juce::String &timeSig)
            : onTap(std::move(onTapCb)),
              onTimeSig(std::move(onTimeSigCb)),
              onSetBpm(std::move(onSetBpmCb)),
              onAdjustBpm(std::move(onAdjustBpmCb)),
              bpmProvider(std::move(bpmProviderCb)) {
          tapButton.setButtonText("TAP");
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
            if (onTimeSig)
              onTimeSig(timeSigBox.getText().trim());
          };
          addAndMakeVisible(timeSigBox);

          minusButton.setButtonText("-");
          plusButton.setButtonText("+");
          minusButton.onClick = [this] {
            if (onAdjustBpm)
              onAdjustBpm(-1.0);
            syncBpmEditor();
          };
          plusButton.onClick = [this] {
            if (onAdjustBpm)
              onAdjustBpm(1.0);
            syncBpmEditor();
          };
          addAndMakeVisible(minusButton);
          addAndMakeVisible(plusButton);

          bpmEditor.setInputRestrictions(4, "0123456789.");
          bpmEditor.setJustification(juce::Justification::centred);
          bpmEditor.onReturnKey = [this] { submitBpmEditor(); };
          bpmEditor.onFocusLost = [this] { submitBpmEditor(); };
          addAndMakeVisible(bpmEditor);
          applyTheme();
          syncBpmEditor();
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
          tapButton.setBounds(area.removeFromTop(32));
          area.removeFromTop(8);
          timeSigBox.setBounds(area.removeFromTop(26));
          area.removeFromTop(8);
          auto row = area.removeFromTop(30);
          minusButton.setBounds(row.removeFromLeft(28));
          plusButton.setBounds(row.removeFromRight(28));
          bpmEditor.setBounds(row.reduced(6, 0));
        }

      private:
        juce::TextButton tapButton, minusButton, plusButton;
        juce::ComboBox timeSigBox;
        juce::TextEditor bpmEditor;
        std::function<void()> onTap;
        std::function<void(const juce::String &)> onTimeSig;
        std::function<void(double)> onSetBpm;
        std::function<void(double)> onAdjustBpm;
        std::function<double()> bpmProvider;

        void applyTheme() {
          for (auto *btn : {&tapButton, &minusButton, &plusButton}) {
            btn->setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
            btn->setColour(juce::TextButton::buttonOnColourId, UITheme::cardBgPressed);
            btn->setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
          }
          timeSigBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBgHover);
          timeSigBox.setColour(juce::ComboBox::outlineColourId, UITheme::cardBorder.withAlpha(0.65f));
          timeSigBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
          timeSigBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
          bpmEditor.setColour(juce::TextEditor::backgroundColourId, UITheme::cardBgHover);
          bpmEditor.setColour(juce::TextEditor::textColourId, UITheme::textPrimary);
          bpmEditor.setColour(juce::TextEditor::outlineColourId, UITheme::cardBorder.withAlpha(0.7f));
        }

        void syncBpmEditor() {
          if (!bpmProvider)
            return;
          bpmEditor.setText(juce::String(static_cast<int>(std::round(bpmProvider()))),
                            juce::dontSendNotification);
        }

        void submitBpmEditor() {
          if (!onSetBpm)
            return;
          const double bpm = bpmEditor.getText().trim().getDoubleValue();
          if (bpm > 0.0)
            onSetBpm(bpm);
          syncBpmEditor();
        }
      };

      auto panel = std::make_unique<MetronomePopover>(
          [this] { registerTap(); }, [this](const juce::String &sig) { applyTimeSignature(sig); },
          [this](double bpm) { setBpmAbsolute(bpm); },
          [this](double delta) { adjustBpm(delta); },
          [this] { return processor.getMetronomeBpm(); }, timeSignature);
      panel->setSize(180, 120);
      auto *editor = findParentComponentOfClass<MTControlsAudioProcessorEditor>();
      juce::Component *parentComp =
          editor != nullptr ? static_cast<juce::Component *>(editor)
                            : static_cast<juce::Component *>(this);
      const auto anchor = parentComp->getLocalArea(this, bpmSegmentButton.getBounds());
      auto &callout = juce::CallOutBox::launchAsynchronously(
          std::move(panel), anchor, parentComp);
      callout.setDismissalMouseClicksAreAlwaysConsumed(true);
    }

    void showSongWindowPopover() {
      if (!processor.hasLoadedSong())
        return;
      auto panel = std::make_unique<SongWindowPopover>(processor);
      panel->setSize(230, 128);
      auto *editor = findParentComponentOfClass<MTControlsAudioProcessorEditor>();
      juce::Component *parentComp =
          editor != nullptr ? static_cast<juce::Component *>(editor)
                            : static_cast<juce::Component *>(this);
      const auto anchor = parentComp->getLocalArea(this, timeDisplay.getBounds());
      auto &callout = juce::CallOutBox::launchAsynchronously(
          std::move(panel), anchor, parentComp);
      callout.setDismissalMouseClicksAreAlwaysConsumed(true);
    }

    void refreshFromProviders() {
      const double pos = positionProvider ? positionProvider() : 0.0;
      const double len = lengthProvider ? lengthProvider() : 0.0;
      timeDisplay.setText(formatTimeSeconds(pos) + " / " +
                              formatTimeSeconds(len),
                          juce::dontSendNotification);

      updateBpmDisplay();

      if (keyProvider) {
        const auto key = keyProvider().trim();
        keyDisplay.setText(key.isNotEmpty() ? key : "Key",
                           juce::dontSendNotification);
      } else {
        keyDisplay.setText("Key", juce::dontSendNotification);
      }

      const bool playing = playingProvider ? playingProvider() : false;
      playButton.setGlyph(playing ? IconGlyph::Pause : IconGlyph::Play);
      if (monoClickProvider) {
        monoClickButton.setToggleState(monoClickProvider(),
                                       juce::dontSendNotification);
      }
      const bool fadeOutActive = processor.isFadeActive();
      fadeButton.setGlyph(fadeOutActive ? IconGlyph::FadeIn
                                        : IconGlyph::FadeOut);
      fadeButton.setToggleState(fadeOutActive, juce::dontSendNotification);
      metroToggleButton.setToggleState(processor.isMetronomeEnabled(),
                                       juce::dontSendNotification);
    }
  };

  class WaveformBar final : public juce::Component, private juce::Timer {
  public:
    using PositionProvider = std::function<double()>;
    using LengthProvider = std::function<double()>;
    using SeekCallback = std::function<void(double)>;
    using EditToggleCallback = std::function<void(bool)>;

    WaveformBar() {
      editButton.setButtonText("EDITAR");
      editButton.setClickingTogglesState(true);
      editButton.setViewportIgnoreDragFlag(true);
      editButton.onClick = [this] {
        if (onEditToggle)
          onEditToggle(editButton.getToggleState());
      };
      addAndMakeVisible(editButton);
      refreshThemeColours();

      startTimerHz(30);
    }

    void setWaveformBins(std::vector<float> bins) {
      waveformBins = std::move(bins);
      repaint();
    }

    void setTransportProviders(PositionProvider positionCb,
                               LengthProvider lengthCb) {
      positionProvider = std::move(positionCb);
      lengthProvider = std::move(lengthCb);
    }

    void setSeekCallback(SeekCallback cb) { onSeekTo = std::move(cb); }
    void setOnEditToggle(EditToggleCallback cb) {
      onEditToggle = std::move(cb);
    }

    void refreshThemeColours() {
      editButton.setColour(juce::TextButton::buttonColourId,
                           UITheme::cardBgHover);
      editButton.setColour(juce::TextButton::buttonOnColourId,
                           UITheme::cardBgPressed);
      editButton.setColour(juce::TextButton::textColourOffId,
                           UITheme::textPrimary);
      editButton.setColour(juce::TextButton::textColourOnId,
                           UITheme::textPrimary);
      repaint();
    }

    void paint(juce::Graphics &g) override {
      const auto bounds = getLocalBounds().toFloat();
      if (bounds.getWidth() < 2.0f)
        return;

      drawStudioBackground(g, bounds);
      {
        juce::ColourGradient base(UITheme::panelDark.withAlpha(0.42f),
                                  bounds.getCentreX(), bounds.getY(),
                                  UITheme::panelDark.withAlpha(0.65f),
                                  bounds.getCentreX(), bounds.getBottom(),
                                  false);
        g.setGradientFill(base);
        g.fillRect(bounds);
      }

      const auto waveBounds = bounds.reduced(0.0f, 8.0f);

      if (waveformBins.empty()) {
        g.setColour(UITheme::cardBg.withAlpha(0.55f));
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText("Double-click a song to load stems waveform",
                         waveBounds.toNearestInt().reduced(20),
                         juce::Justification::centred, 2);
        g.setColour(UITheme::accent);
        g.fillRect(0.0f, 0.0f, 2.0f, bounds.getHeight());
        return;
      }

      g.setColour(UITheme::dividerSoft.withAlpha(0.22f));
      constexpr int gridCount = 8;
      const float gridStep = 1.0f / static_cast<float>(gridCount);
      for (int i = 1; i < gridCount; ++i) {
        const float x =
            waveBounds.getX() + waveBounds.getWidth() * (i * gridStep);
        g.drawLine(x, waveBounds.getY(), x, waveBounds.getBottom(), 0.6f);
      }

      const float midY = waveBounds.getCentreY();
      const float ampScale = waveBounds.getHeight() * 0.48f;

      juce::Path filledWave;
      const int width = juce::jmax(2, static_cast<int>(bounds.getWidth()));
      const int bins = static_cast<int>(waveformBins.size());
      const auto window = getVisibleWindow();
      const float windowStart = window.first;
      const float windowSpan = window.second;

      for (int x = 0; x < width; ++x) {
        const float t = static_cast<float>(x) / static_cast<float>(width - 1);
        const float index =
            (windowStart + (t * windowSpan)) * static_cast<float>(bins - 1);
        const int i0 = static_cast<int>(index);
        const int i1 = juce::jmin(i0 + 1, bins - 1);
        const float frac = index - static_cast<float>(i0);
        const float amp =
            juce::jmap(frac, waveformBins[static_cast<size_t>(i0)],
                       waveformBins[static_cast<size_t>(i1)]);
        const float yTop = midY - (amp * ampScale);

        if (x == 0)
          filledWave.startNewSubPath(0.0f, yTop);
        else
          filledWave.lineTo(static_cast<float>(x), yTop);
      }

      for (int x = width - 1; x >= 0; --x) {
        const float t = static_cast<float>(x) / static_cast<float>(width - 1);
        const float index =
            (windowStart + (t * windowSpan)) * static_cast<float>(bins - 1);
        const int i0 = static_cast<int>(index);
        const int i1 = juce::jmin(i0 + 1, bins - 1);
        const float frac = index - static_cast<float>(i0);
        const float amp =
            juce::jmap(frac, waveformBins[static_cast<size_t>(i0)],
                       waveformBins[static_cast<size_t>(i1)]);
        const float yBottom = midY + (amp * ampScale);
        filledWave.lineTo(static_cast<float>(x), yBottom);
      }

      filledWave.closeSubPath();

      // Midline
      g.setColour(UITheme::dividerSoft.withAlpha(0.6f));
      g.drawLine(waveBounds.getX(), midY, waveBounds.getRight(), midY, 1.0f);

      // Wave body
      juce::ColourGradient waveFill(UITheme::cardBgHover.withAlpha(0.90f),
                                    waveBounds.getCentreX(), waveBounds.getY(),
                                    UITheme::cardBg.withAlpha(0.74f),
                                    waveBounds.getCentreX(),
                                    waveBounds.getBottom(), false);
      g.setGradientFill(waveFill);
      g.fillPath(filledWave);

      // Soft depth shadow
      juce::Path shadow = filledWave;
      shadow.applyTransform(
          juce::AffineTransform::translation(0.0f, 1.2f));
      g.setColour(UITheme::panelDark.withAlpha(0.18f));
      g.fillPath(shadow);

      float progressRatio = 0.0f;
      if (positionProvider && lengthProvider) {
        const double songLength = juce::jmax(0.0, lengthProvider());
        if (songLength > 0.0) {
          const double songPosition = juce::jmax(0.0, positionProvider());
          progressRatio = static_cast<float>(
              juce::jlimit(0.0, 1.0, songPosition / songLength));
        }
      }

      float playheadRatio =
          (progressRatio - windowStart) / juce::jmax(0.0001f, windowSpan);
      playheadRatio = juce::jlimit(0.0f, 1.0f, playheadRatio);
      const float playheadX =
          waveBounds.getX() + (playheadRatio * waveBounds.getWidth());
      // Progress tint
      if (playheadX > waveBounds.getX()) {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(waveBounds.withRight(playheadX).toNearestInt());
        juce::ColourGradient progressFill(
            UITheme::accent.withAlpha(0.36f), waveBounds.getCentreX(),
            waveBounds.getY(), UITheme::accentSoft.withAlpha(0.18f),
            waveBounds.getCentreX(), waveBounds.getBottom(), false);
        g.setGradientFill(progressFill);
        g.fillPath(filledWave);
      }

      // Crisp outline + highlight ridge
      g.setColour(UITheme::cardBorder.withAlpha(0.7f));
      g.strokePath(filledWave, juce::PathStrokeType(1.0f));
      g.setColour(UITheme::cardBgHover.withAlpha(0.55f));
      g.strokePath(filledWave, juce::PathStrokeType(0.6f));

      g.setColour(UITheme::accent);
      g.drawLine(playheadX, waveBounds.getY(), playheadX,
                 waveBounds.getBottom(), 1.4f);
    }

    void resized() override {
      auto area = getLocalBounds();
      const int buttonW = 64;
      const int buttonH = 22;
      editButton.setBounds(area.removeFromTop(buttonH)
                               .removeFromRight(buttonW)
                               .reduced(2));
    }

    void mouseDown(const juce::MouseEvent &event) override {
      handleSeek(event.position.x);
    }

    void mouseDrag(const juce::MouseEvent &event) override {
      handleSeek(event.position.x);
    }

    void mouseWheelMove(const juce::MouseEvent &event,
                        const juce::MouseWheelDetails &wheel) override {
      const auto mods = event.mods;
      if (!mods.isCtrlDown() && !mods.isCommandDown())
        return;
      const float dir = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
      if (dir == 0.0f)
        return;
      applyZoom(dir > 0.0f ? 1.1f : 1.0f / 1.1f);
    }

    void mouseMagnify(const juce::MouseEvent &,
                      float magnifyAmount) override {
      if (magnifyAmount == 0.0f)
        return;
      applyZoom(1.0f + magnifyAmount);
    }

  private:
    void timerCallback() override { repaint(); }

    void handleSeek(float x) {
      if (!onSeekTo || !lengthProvider)
        return;
      const double songLength = juce::jmax(0.0, lengthProvider());
      if (songLength <= 0.0)
        return;

      const auto bounds = getLocalBounds().toFloat();
      const float clampedX =
          juce::jlimit(bounds.getX(), bounds.getRight(), x);
      const float ratio =
          bounds.getWidth() > 0.0f
              ? (clampedX - bounds.getX()) / bounds.getWidth()
              : 0.0f;
      const auto window = getVisibleWindow();
      const double target =
          songLength * juce::jlimit(0.0f, 1.0f,
                                    window.first + (ratio * window.second));
      onSeekTo(target);
    }

    std::vector<float> waveformBins;
    PositionProvider positionProvider;
    LengthProvider lengthProvider;
    SeekCallback onSeekTo;
    EditToggleCallback onEditToggle;
    juce::TextButton editButton;
    float zoom = 1.0f;

    std::pair<float, float> getVisibleWindow() const {
      const float span = 1.0f / juce::jlimit(1.0f, 8.0f, zoom);
      float center = 0.5f;
      if (positionProvider && lengthProvider) {
        const double songLength = juce::jmax(0.0, lengthProvider());
        if (songLength > 0.0) {
          const double songPosition = juce::jmax(0.0, positionProvider());
          center = static_cast<float>(
              juce::jlimit(0.0, 1.0, songPosition / songLength));
        }
      }
      float start = center - (span * 0.5f);
      if (start < 0.0f)
        start = 0.0f;
      if (start + span > 1.0f)
        start = 1.0f - span;
      return {start, span};
    }

    void applyZoom(float factor) {
      zoom = juce::jlimit(1.0f, 8.0f, zoom * factor);
      repaint();
    }
  };

  class ChannelGridPanel final : public juce::Component {
  public:
    ChannelGridPanel(ChannelStripLookAndFeel &lookAndFeelRef,
                     MTControlsAudioProcessor &processorRef)
        : lookAndFeel(lookAndFeelRef), processor(processorRef) {
      viewport.setViewedComponent(&content, false);
      viewport.setScrollBarsShown(true, false, false, false);
      viewport.setScrollBarThickness(1);
      viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
      addAndMakeVisible(viewport);
    }

    void setSong(const ImportedSongData &song) {
      slots.clear();
      adjustableStems.clear();
      clickGuideStems.clear();
      content.removeAllChildren();
      const int stemCount = processor.getLoadedStemCount();
      int mappedStemIndex = 0;

      std::vector<const ImportedStemData *> orderedStems;
      orderedStems.reserve(song.stems.size());
      for (const auto &stem : song.stems) {
        if (stem.include)
          orderedStems.push_back(&stem);
      }
      std::stable_sort(
          orderedStems.begin(), orderedStems.end(),
          [](const ImportedStemData *a, const ImportedStemData *b) {
            return stemPriorityForStripOrder(*a) <
                   stemPriorityForStripOrder(*b);
          });

      for (const auto *stemPtr : orderedStems) {
        if (stemPtr == nullptr)
          continue;
        if (!juce::isPositiveAndBelow(mappedStemIndex, stemCount))
          break;
        const auto &stem = *stemPtr;

        juce::String channelName = stem.displayName.trim();
        if (channelName.isEmpty())
          channelName = stem.category.trim();
        if (channelName.isEmpty())
          channelName = juce::File(stem.fileName).getFileNameWithoutExtension();
        if (channelName.isEmpty())
          channelName = "STEM";

        const int stemIndex = mappedStemIndex++;
        const bool isAdjustable = stemPriorityForStripOrder(stem) > 1;
        const bool isClickGuide = stemPriorityForStripOrder(stem) <= 1;
        auto slot = std::make_unique<StripSlot>(
            channelName.toUpperCase(), lookAndFeel, [this, stemIndex] {
              const float level = processor.getStemLevel(stemIndex);
              return std::pair<float, float>{level, level};
            });
        if (isClickGuide)
          slot->getStrip().setCardBgColour(UITheme::cardBgClickGuide);
        slot->getStrip().setChannelBandColours(UITheme::accent, UITheme::onAccent);
        slot->getStrip().refreshThemeColours();
        slot->getStrip().setVolume(processor.getStemVolume(stemIndex));
        slot->getStrip().setGain(processor.getStemGain(stemIndex));
        slot->getStrip().setMuteState(processor.isStemMuted(stemIndex));
        slot->getStrip().setSoloState(processor.isStemSoloed(stemIndex));
        slot->getStrip().setOnVolumeChanged([this, stemIndex](float value) {
          processor.setStemVolume(stemIndex, value);
        });
        slot->getStrip().setOnGainChanged([this, stemIndex](float value) {
          processor.setStemGain(stemIndex, value);
        });
        slot->getStrip().setOnMuteChanged([this, stemIndex](bool muted) {
          processor.setStemMute(stemIndex, muted);
        });
        slot->getStrip().setOnSoloChanged([this, stemIndex](bool soloed) {
          processor.setStemSolo(stemIndex, soloed);
        });
        content.addAndMakeVisible(*slot);
        adjustableStems.push_back(isAdjustable);
        clickGuideStems.push_back(isClickGuide);
        slots.push_back(std::move(slot));
      }

      resized();
      repaint();
    }

    void nudgeFaders(float delta) {
      if (slots.empty())
        return;

      const int stemCount = static_cast<int>(slots.size());
      for (int i = 0; i < stemCount; ++i) {
        if (i >= static_cast<int>(adjustableStems.size()) ||
            !adjustableStems[static_cast<size_t>(i)]) {
          continue;
        }
        const float current = processor.getStemVolume(i);
        const float next = juce::jlimit(0.0f, 1.0f, current + delta);
        if (juce::approximatelyEqual(current, next))
          continue;
        processor.setStemVolume(i, next);
        slots[static_cast<size_t>(i)]->getStrip().setVolume(next);
      }
    }

    void refreshThemeColours() {
      for (size_t i = 0; i < slots.size(); ++i) {
        auto &strip = slots[i]->getStrip();
        const bool isClickGuide =
            i < clickGuideStems.size() ? clickGuideStems[i] : false;
        strip.setCardBgColour(isClickGuide ? UITheme::cardBgClickGuide
                                           : UITheme::cardBg);
        strip.setChannelBandColours(UITheme::accent, UITheme::onAccent);
        strip.refreshThemeColours();
      }
      repaint();
    }

    void paint(juce::Graphics &g) override {
      g.fillAll(UITheme::cardBg);
      if (slots.empty()) {
        g.setColour(UITheme::textSecondary.withAlpha(0.85f));
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText("Double-click a song\nfrom setlist", getLocalBounds(),
                         juce::Justification::centred, 2);
      }
    }

    void resized() override {
      viewport.setBounds(getLocalBounds());
      const int contentWidth = juce::jmax(1, viewport.getWidth());

      const int pad = 8;
      const int gapX = 4;
      const int gapY = 10;
      const auto stripLayout = ChannelStripCard::Layout{};
      const int cellW = stripLayout.width + 2;
      const int rows =
          (static_cast<int>(slots.size()) +
           juce::jmax(1, (contentWidth - (pad * 2) + gapX) / (cellW + gapX)) -
           1) /
          juce::jmax(1, (contentWidth - (pad * 2) + gapX) / (cellW + gapX));
      const int columns =
          juce::jmax(1, (contentWidth - (pad * 2) + gapX) / (cellW + gapX));
      const int cellH = stripLayout.height + 4;

      for (size_t i = 0; i < slots.size(); ++i) {
        const int row = static_cast<int>(i) / columns;
        const int col = static_cast<int>(i) % columns;
        const int x = pad + (col * (cellW + gapX));
        const int y = pad + (row * (cellH + gapY));
        slots[i]->setBounds(x, y, cellW, cellH);
      }

      const int neededH =
          pad + (rows * cellH) + (juce::jmax(0, rows - 1) * gapY) + pad;
      content.setSize(contentWidth, juce::jmax(viewport.getHeight(), neededH));
    }

  private:
    class StripSlot : public juce::Component {
    public:
      StripSlot(juce::String groupName, ChannelStripLookAndFeel &lnf,
                StereoVUMeter::LevelProvider levelProvider)
          : strip(std::make_unique<ChannelStripCard>(
                std::move(groupName), lnf, std::move(levelProvider), false)) {
        addAndMakeVisible(*strip);
      }

      void resized() override {
        const int cardW = strip->getLayout().width;
        const int cardH = strip->getLayout().height;
        const int cardX = (getWidth() - cardW) / 2;
        const int cardY = 0;
        strip->setBounds(cardX, cardY, cardW, cardH);
      }

      ChannelStripCard &getStrip() { return *strip; }

    private:
      std::unique_ptr<ChannelStripCard> strip;
    };

    ChannelStripLookAndFeel &lookAndFeel;
    MTControlsAudioProcessor &processor;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<StripSlot>> slots;
    std::vector<bool> adjustableStems;
    std::vector<bool> clickGuideStems;
  };

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

  class MixerFaderPanel final : public juce::Component {
  public:
    using AdjustCallback = std::function<void(float)>;

    MixerFaderPanel(ChannelStripLookAndFeel &lookAndFeelRef,
                    MTControlsAudioProcessor &processorRef)
        : lookAndFeel(lookAndFeelRef), processor(processorRef) {
      masterStrip = std::make_unique<ChannelStripCard>(
          "MASTER", lookAndFeel,
          [this] {
            float l = 0.0f;
            float r = 0.0f;
            processor.getOutputMeterLevels(l, r);
            return std::pair<float, float>{l, r};
          },
          false);
      masterStrip->setCardBgColour(UITheme::cardBgMaster);
      masterStrip->setChannelBandColours(UITheme::accentMaster,
                                         UITheme::onAccent);
      masterStrip->setSoloEnabled(false);
      masterStrip->setSoloVisible(false);
      masterStrip->setGainVisible(true);
      masterStrip->setMuteAlignedToFader(true);
      masterStrip->setVolume(processor.getMasterVolume());
      masterStrip->setGain(processor.getMasterGain());
      masterStrip->setMuteState(processor.isMasterMuted());
      masterStrip->setOnVolumeChanged([this](float value) {
        processor.setMasterVolume(value);
      });
      masterStrip->setOnGainChanged([this](float value) {
        processor.setMasterGain(value);
      });
      masterStrip->setOnMuteChanged([this](bool muted) {
        processor.setMasterMute(muted);
      });
      addAndMakeVisible(*masterStrip);

      configureTitleLabel(titleLabel, "ALL FADER");
      configureTitleLabel(masterLabel, "MASTER LEVEL");
      configureButton(upButton, "VOL +");
      configureButton(downButton, "VOL -");

      upButton.onClick = [this] {
        if (onAdjust)
          onAdjust(step);
      };
      downButton.onClick = [this] {
        if (onAdjust)
          onAdjust(-step);
      };

      addAndMakeVisible(titleLabel);
      addAndMakeVisible(masterLabel);
      addAndMakeVisible(upButton);
      addAndMakeVisible(downButton);
      refreshThemeColours();
    }

    ~MixerFaderPanel() override { clearLookAndFeel(); }

    void setOnAdjust(AdjustCallback cb) { onAdjust = std::move(cb); }
    void setSongBpm(double) {}

    void refreshThemeColours() {
      configureTitleLabel(titleLabel, "ALL FADER");
      configureTitleLabel(masterLabel, "MASTER LEVEL");
      configureButton(upButton, "VOL +");
      configureButton(downButton, "VOL -");

      if (masterStrip) {
        masterStrip->setCardBgColour(UITheme::cardBgMaster);
        masterStrip->setChannelBandColours(UITheme::accentMaster,
                                           UITheme::onAccent);
        masterStrip->refreshThemeColours();
      }
      repaint();
    }

    void paint(juce::Graphics &g) override {
      const auto bounds = getLocalBounds().toFloat();
      g.setColour(UITheme::cardBgMaster.withAlpha(0.9f));
      g.fillRoundedRectangle(bounds, 6.0f);
      g.setColour(UITheme::cardBorder.withAlpha(0.7f));
      g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

      auto drawSection = [&g](const juce::Rectangle<int> &section) {
        if (section.isEmpty())
          return;
        auto b = section.toFloat().reduced(2.0f, 2.0f);
        juce::ColourGradient grad(UITheme::cardBg.withAlpha(0.28f),
                                  b.getCentreX(), b.getY(),
                                  UITheme::cardBgHover.withAlpha(0.38f),
                                  b.getCentreX(), b.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(b, 4.0f);
        g.setColour(UITheme::cardBorder.withAlpha(0.25f));
        g.drawLine(b.getX() + 6.0f, b.getY(), b.getRight() - 6.0f, b.getY(),
                   1.0f);
      };

      drawSection(allFadersSection);
      drawSection(masterSection);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(8, 8);
      const auto stripLayout = ChannelStripCard::Layout{};
      const int stripW = stripLayout.width;
      const int stripH = stripLayout.height;

      const int titleH = 18;
      const int buttonH = 22;
      const int gap = 4;
      const int sectionGap = 10;

      const int sectionW = stripW;
      const int sectionX = area.getX() + (area.getWidth() - sectionW) / 2;
      int y = area.getY() + 4;
      const int allTop = y;

      titleLabel.setBounds(sectionX, y, sectionW, titleH);
      y += titleH + gap;

      const int buttonW = sectionW;
      upButton.setBounds(sectionX, y, buttonW, buttonH);
      y += buttonH + gap;
      downButton.setBounds(sectionX, y, buttonW, buttonH);
      y += buttonH + sectionGap;
      const int allBottom = y - (sectionGap / 2);

      const int masterTitleH = titleH;
      const int masterTitleGap = gap;
      const int masterTop =
          area.getBottom() - stripH - masterTitleH - masterTitleGap;
      masterLabel.setBounds(sectionX, masterTop, sectionW, masterTitleH);

      const int stripX = area.getX() + (area.getWidth() - stripW) / 2;
      const int stripY = masterTop + masterTitleH + masterTitleGap;
      masterStrip->setBounds(stripX, stripY, stripW, stripH);

      allFadersSection =
          juce::Rectangle<int>(area.getX(), allTop, area.getWidth(),
                               juce::jmax(0, allBottom - allTop));
      masterSection =
          juce::Rectangle<int>(area.getX(), masterTop, area.getWidth(),
                               juce::jmax(0, area.getBottom() - masterTop));
    }

  private:
    ChannelStripLookAndFeel &lookAndFeel;
    MTControlsAudioProcessor &processor;
    std::unique_ptr<ChannelStripCard> masterStrip;
    juce::Label titleLabel;
    juce::Label masterLabel;
    juce::TextButton upButton;
    juce::TextButton downButton;
    juce::Rectangle<int> allFadersSection;
    juce::Rectangle<int> masterSection;
    AdjustCallback onAdjust;
    const float step = 0.05f;

    void configureLabel(juce::Label &label, const juce::String &text) {
      label.setText(text.toUpperCase(), juce::dontSendNotification);
      label.setJustificationType(juce::Justification::centred);
      label.setFont(juce::FontOptions(9.0f).withKerningFactor(0.10f));
      label.setColour(juce::Label::textColourId, UITheme::textSecondary);
    }

    void configureTitleLabel(juce::Label &label, const juce::String &text) {
      label.setText(text.toUpperCase(), juce::dontSendNotification);
      label.setJustificationType(juce::Justification::centred);
      label.setFont(juce::FontOptions(11.0f).withKerningFactor(0.12f));
      label.setColour(juce::Label::textColourId, UITheme::onAccent);
      label.setColour(juce::Label::backgroundColourId, UITheme::accentSoft);
      label.setColour(juce::Label::outlineColourId,
                      UITheme::accent.withAlpha(0.6f));
    }

    void configureButton(juce::TextButton &button, const juce::String &text) {
      button.setButtonText(text);
      button.setViewportIgnoreDragFlag(true);
      button.setLookAndFeel(&lookAndFeel);
      button.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
      button.setColour(juce::TextButton::buttonOnColourId,
                       UITheme::cardBgPressed);
      button.setColour(juce::TextButton::textColourOffId,
                       UITheme::textPrimary);
      button.setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);
    }

    void clearLookAndFeel() {
      upButton.setLookAndFeel(nullptr);
      downButton.setLookAndFeel(nullptr);
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
