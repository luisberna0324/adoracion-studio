#include "SettingsPanel.h"

#include "../../audio/OutputChannelCatalog.h"
#include "../../data/Localization.h"
#include "../../data/SongData.h"

#include <algorithm>
#include <map>

class SettingsPanel::SettingsCard final : public juce::Component {
public:
  void paint(juce::Graphics &g) override {
    const auto b = getLocalBounds().toFloat();
    g.setColour(UITheme::cardBg.withAlpha(0.22f));
    g.fillRoundedRectangle(b, 12.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.9f));
    g.drawRoundedRectangle(b.reduced(0.5f), 12.0f, 1.0f);
  }
};

class SettingsPanel::RoutingRow final : public juce::Component {
public:
  RoutingRow(const juce::String &categoryName, const int outputChannels,
             const CategoryRoutingDefaults &defaults,
             std::function<void(const CategoryRoutingDefaults &)> changedCb)
      : onChanged(std::move(changedCb)),
        availableOutputChannels(juce::jmax(1, outputChannels)),
        current(defaults) {
    categoryLabel.setText(categoryName, juce::dontSendNotification);
    categoryLabel.setFont(juce::FontOptions(13.0f));
    categoryLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(categoryLabel);

    modeBox.addItem("Mono", 1);
    modeBox.addItem("Stereo", 2);
    modeBox.onChange = [this] {
      const int selected = modeBox.getSelectedId();
      current.width = (selected == 2 ? 2 : 1);
      refreshChannelOptions();
      if (onChanged)
        onChanged(current);
    };
    addAndMakeVisible(modeBox);

    channelBox.onChange = [this] {
      const int selected = channelBox.getSelectedId();
      if (selected > 0) {
        current.baseChannel = selected;
        if (onChanged)
          onChanged(current);
      }
    };
    addAndMakeVisible(channelBox);

    refreshChannelOptions();
    refreshThemeColours();
  }

  void resized() override {
    auto r = getLocalBounds().reduced(0, 4);
    
    // El componente de AudioDeviceSelectorComponent de JUCE usa una proporción
    // para sus etiquetas (usualmente un tercio del ancho o un máximo).
    // Usamos un ancho fijo mayor para que los ComboBoxes queden alineados.
    auto nameCol = r.removeFromLeft(220); 
    auto modeCol = r.removeFromLeft(110);
    r.removeFromLeft(16);
    
    categoryLabel.setBounds(nameCol);
    modeBox.setBounds(modeCol);
    
    auto channelCol = r.removeFromLeft(juce::jmin(260, r.getWidth()));
    channelBox.setBounds(channelCol);
  }

  void refreshThemeColours() {
    categoryLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
    applyComboBoxTheme(modeBox, UITheme::cardBgHover,
                       UITheme::cardBorder.withAlpha(0.65f));
    applyComboBoxTheme(channelBox, UITheme::cardBgHover,
                       UITheme::cardBorder.withAlpha(0.65f));
  }

  void setDefaults(const CategoryRoutingDefaults &defaults,
                   const int outputChannels) {
    availableOutputChannels = juce::jmax(1, outputChannels);
    current = defaults;
    refreshChannelOptions();
  }

  CategoryRoutingDefaults getDefaults() const { return current; }

private:
  void refreshChannelOptions() {
    const bool stereoAllowed = availableOutputChannels >= 2;
    modeBox.setItemEnabled(2, stereoAllowed);
    if (!stereoAllowed && current.width >= 2)
      current.width = 1;

    const int modeId = (current.width >= 2 ? 2 : 1);
    modeBox.setSelectedId(modeId, juce::dontSendNotification);

    channelBox.clear(juce::dontSendNotification);
    if (current.width >= 2) {
      const auto names =
          OutputChannelCatalog::buildStereoPairNames(availableOutputChannels);
      for (int i = 0; i < names.size(); ++i)
        channelBox.addItem(names[i], i + 1);
      const int maxBase = juce::jmax(1, availableOutputChannels - 1);
      current.baseChannel = juce::jlimit(1, maxBase, current.baseChannel);
      channelBox.setSelectedId(current.baseChannel, juce::dontSendNotification);
    } else {
      const auto names =
          OutputChannelCatalog::buildOutputChannelNames(availableOutputChannels);
      for (int i = 0; i < names.size(); ++i)
        channelBox.addItem(names[i], i + 1);
      const int maxBase = juce::jmax(1, availableOutputChannels);
      current.baseChannel = juce::jlimit(1, maxBase, current.baseChannel);
      channelBox.setSelectedId(current.baseChannel, juce::dontSendNotification);
    }
  }

  juce::String name;
  juce::Label categoryLabel;
  juce::ComboBox modeBox;
  juce::ComboBox channelBox;
  std::function<void(const CategoryRoutingDefaults &)> onChanged;
  int availableOutputChannels = 2;
  CategoryRoutingDefaults current;
};

SettingsPanel::SettingsPanel(ThemePreset current, ThemeChanged cb,
                             const AppLanguage language,
                             LanguageChanged languageCb,
                             const bool expandWindowOnLaunch,
                             ExpandLaunchChanged expandCb,
                             const bool primeImportEnabled,
                             PrimeImportChanged primeImportCb,
                             const bool stemOptimizationEnabled,
                             StemOptimizationChanged stemOptimizationCb,
                             const PadsCrossfadePreset padsCrossfadePreset,
                             PadsCrossfadeChanged padsCrossfadeCb,
                             const MetronomeClickStyle metronomeClickStyle,
                             MetronomeClickStyleChanged metronomeClickStyleCb,
                             CloseRequested closeCb,
                             SessionSummary sessionSummary,
                             LogoutRequested logoutCb,
                             std::vector<CategoryRoutingDefaults> defaults,
                             int outputChannels,
                             RoutingDefaultsChanged routingCb,
                             juce::AudioDeviceManager *deviceManagerToUse,
                             int maxInputChannels,
                             int maxOutputChannels)
    : onThemeChanged(std::move(cb)),
      onLanguageChanged(std::move(languageCb)),
      onExpandLaunchChanged(std::move(expandCb)),
      onPrimeImportChanged(std::move(primeImportCb)),
      onStemOptimizationChanged(std::move(stemOptimizationCb)),
      onPadsCrossfadeChanged(std::move(padsCrossfadeCb)),
      onMetronomeClickStyleChanged(std::move(metronomeClickStyleCb)),
      onCloseRequested(std::move(closeCb)),
      onLogoutRequested(std::move(logoutCb)),
      session(std::move(sessionSummary)),
      onRoutingDefaultsChanged(std::move(routingCb)),
      routingDefaults(std::move(defaults)),
      availableOutputChannels(juce::jmax(1, outputChannels)),
      deviceManager(deviceManagerToUse) {
  title.setJustificationType(juce::Justification::centredLeft);
  title.setFont(makeAppFont(18.0f, AppFontWeight::SemiBold));
  addAndMakeVisible(title);
  closeButton.onClick = [this] {
    if (onCloseRequested)
      onCloseRequested();
  };
  addAndMakeVisible(closeButton);

  if (deviceManager != nullptr) {
    audioSetupComp = std::make_unique<juce::AudioDeviceSelectorComponent>(
        *deviceManager, 0, maxInputChannels, 0, maxOutputChannels, true, false, true, false);
    audioSetupComp->setItemHeight(useTouchOptimisedControls() ? 34 : 24);
    audioPage.addAndMakeVisible(audioSetupComp.get());
    deviceManager->addChangeListener(this);
  }

  tabs.addTab(L10n::tr("General", "General"), UITheme::cardBg, &generalPage,
              false);
  tabs.addTab(L10n::tr(u8"Audio", "Audio"), UITheme::cardBg, &audioPage,
              false);
  tabs.addTab(L10n::tr("Ruteo", "Routing"), UITheme::cardBg, &routingPage,
              false);
  tabs.setTabBarDepth(useTouchOptimisedControls() ? 196 : 172);
  tabs.onTabChanged = [this](int newIndex, const juce::String &) {
    if (newIndex == 2) {
      refreshRoutingForDevice();
    }
  };
  addAndMakeVisible(tabs);

  appearanceCard = std::make_unique<SettingsCard>();
  playbackCard = std::make_unique<SettingsCard>();
  sessionCard = std::make_unique<SettingsCard>();
  actionsCard = std::make_unique<SettingsCard>();
  generalPage.addAndMakeVisible(*appearanceCard);
  generalPage.addAndMakeVisible(*playbackCard);
  generalPage.addAndMakeVisible(*sessionCard);
  generalPage.addAndMakeVisible(*actionsCard);

  themeLabel.setJustificationType(juce::Justification::centredLeft);
  themeLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(themeLabel);

  themeBox.addItem(L10n::tr("Verde Noche", "Night Green"),
                   static_cast<int>(ThemePreset::Spotify));
  themeBox.addItem(L10n::tr("Ambar Estudio", "Studio Amber"),
                   static_cast<int>(ThemePreset::Ableton));
  const int selectedThemeId =
      current == ThemePreset::Ableton
          ? static_cast<int>(ThemePreset::Ableton)
          : static_cast<int>(ThemePreset::Spotify);
  themeBox.setSelectedId(selectedThemeId, juce::dontSendNotification);
  generalPage.addAndMakeVisible(themeBox);

  languageLabel.setJustificationType(juce::Justification::centredLeft);
  languageLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(languageLabel);

  languageBox.addItem("Espanol", static_cast<int>(AppLanguage::Spanish) + 1);
  languageBox.addItem("English", static_cast<int>(AppLanguage::English) + 1);
  languageBox.setSelectedId(static_cast<int>(language) + 1,
                            juce::dontSendNotification);
  languageBox.onChange = [this] {
    const int id = languageBox.getSelectedId();
    if (id <= 0)
      return;
    const auto selectedLanguage =
        static_cast<AppLanguage>(juce::jlimit(0, 1, id - 1));
    if (onLanguageChanged)
      onLanguageChanged(selectedLanguage);
    applyLanguageTexts();
    resized();
    repaint();
  };
  generalPage.addAndMakeVisible(languageBox);

  expandLaunchLabel.setJustificationType(juce::Justification::centredLeft);
  expandLaunchLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(expandLaunchLabel);

  expandLaunchToggle.setClickingTogglesState(true);
  expandLaunchToggle.setToggleState(expandWindowOnLaunch,
                                    juce::dontSendNotification);
  expandLaunchToggle.onClick = [this] {
    if (onExpandLaunchChanged)
      onExpandLaunchChanged(expandLaunchToggle.getToggleState());
  };
  generalPage.addAndMakeVisible(expandLaunchToggle);

  primeImportLabel.setJustificationType(juce::Justification::centredLeft);
  primeImportLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(primeImportLabel);

  primeImportToggle.setClickingTogglesState(true);
  primeImportToggle.setToggleState(primeImportEnabled,
                                   juce::dontSendNotification);
  primeImportToggle.onClick = [this] {
    if (onPrimeImportChanged)
      onPrimeImportChanged(primeImportToggle.getToggleState());
  };
  generalPage.addAndMakeVisible(primeImportToggle);

  stemOptimizationLabel.setJustificationType(juce::Justification::centredLeft);
  stemOptimizationLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(stemOptimizationLabel);

  stemOptimizationToggle.setClickingTogglesState(true);
  stemOptimizationToggle.setToggleState(stemOptimizationEnabled,
                                        juce::dontSendNotification);
  stemOptimizationToggle.onClick = [this] {
    if (onStemOptimizationChanged)
      onStemOptimizationChanged(stemOptimizationToggle.getToggleState());
  };
  generalPage.addAndMakeVisible(stemOptimizationToggle);

  padsCrossfadeLabel.setJustificationType(juce::Justification::centredLeft);
  padsCrossfadeLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(padsCrossfadeLabel);

  padsCrossfadeBox.addItem("Rapido (1s)", 1);
  padsCrossfadeBox.addItem("Medio (3s)", 2);
  padsCrossfadeBox.addItem("Largo (5s)", 3);
  int padsCrossfadeId = 2;
  if (padsCrossfadePreset == PadsCrossfadePreset::Fast)
    padsCrossfadeId = 1;
  else if (padsCrossfadePreset == PadsCrossfadePreset::Long)
    padsCrossfadeId = 3;
  padsCrossfadeBox.setSelectedId(padsCrossfadeId, juce::dontSendNotification);
  padsCrossfadeBox.setJustificationType(juce::Justification::centredLeft);
  padsCrossfadeBox.onChange = [this] {
    const int id = padsCrossfadeBox.getSelectedId();
    if (id <= 0 || !onPadsCrossfadeChanged)
      return;
    auto preset = PadsCrossfadePreset::Medium;
    if (id == 1)
      preset = PadsCrossfadePreset::Fast;
    else if (id == 3)
      preset = PadsCrossfadePreset::Long;
    onPadsCrossfadeChanged(preset);
  };
  generalPage.addAndMakeVisible(padsCrossfadeBox);

  metronomeClickStyleLabel.setJustificationType(juce::Justification::centredLeft);
  metronomeClickStyleLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(metronomeClickStyleLabel);

  metronomeClickStyleBox.addItem("Classic", 1);
  metronomeClickStyleBox.addItem("Woodblock", 2);
  metronomeClickStyleBox.addItem("Cowbell", 3);
  metronomeClickStyleBox.addItem("Digital", 4);
  metronomeClickStyleBox.addItem("Gentle", 5);
  metronomeClickStyleBox.addItem("Percussive", 6);
  metronomeClickStyleBox.addItem("Saw", 7);
  metronomeClickStyleBox.addItem("Blip", 8);
  metronomeClickStyleBox.setSelectedId(
      static_cast<int>(metronomeClickStyle) + 1, juce::dontSendNotification);
  metronomeClickStyleBox.setJustificationType(juce::Justification::centredLeft);
  metronomeClickStyleBox.onChange = [this] {
    const int id = metronomeClickStyleBox.getSelectedId();
    if (id <= 0 || !onMetronomeClickStyleChanged)
      return;
    onMetronomeClickStyleChanged(
        static_cast<MetronomeClickStyle>(juce::jlimit(0, 7, id - 1)));
  };
  generalPage.addAndMakeVisible(metronomeClickStyleBox);

  sessionSectionLabel.setJustificationType(juce::Justification::centredLeft);
  sessionSectionLabel.setFont(makeAppFont(14.0f, AppFontWeight::SemiBold));
  generalPage.addAndMakeVisible(sessionSectionLabel);

  sessionUidLabel.setJustificationType(juce::Justification::centredLeft);
  sessionUidLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(sessionUidLabel);
  sessionUidValue.setJustificationType(juce::Justification::centredLeft);
  sessionUidValue.setFont(makeAppFont(12.0f));
  generalPage.addAndMakeVisible(sessionUidValue);

  sessionEmailLabel.setJustificationType(juce::Justification::centredLeft);
  sessionEmailLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(sessionEmailLabel);
  sessionEmailValue.setJustificationType(juce::Justification::centredLeft);
  sessionEmailValue.setFont(makeAppFont(12.0f));
  generalPage.addAndMakeVisible(sessionEmailValue);

  sessionPlanLabel.setJustificationType(juce::Justification::centredLeft);
  sessionPlanLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(sessionPlanLabel);
  sessionPlanValue.setJustificationType(juce::Justification::centredLeft);
  sessionPlanValue.setFont(makeAppFont(12.0f));
  generalPage.addAndMakeVisible(sessionPlanValue);

  sessionStatusLabel.setJustificationType(juce::Justification::centredLeft);
  sessionStatusLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(sessionStatusLabel);
  sessionStatusValue.setJustificationType(juce::Justification::centredLeft);
  sessionStatusValue.setFont(makeAppFont(12.0f));
  generalPage.addAndMakeVisible(sessionStatusValue);

  sessionExpiresLabel.setJustificationType(juce::Justification::centredLeft);
  sessionExpiresLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(sessionExpiresLabel);
  sessionExpiresValue.setJustificationType(juce::Justification::centredLeft);
  sessionExpiresValue.setFont(makeAppFont(12.0f));
  generalPage.addAndMakeVisible(sessionExpiresValue);

  sessionLicenseLabel.setJustificationType(juce::Justification::centredLeft);
  sessionLicenseLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(sessionLicenseLabel);
  sessionLicenseValue.setJustificationType(juce::Justification::centredLeft);
  sessionLicenseValue.setFont(makeAppFont(12.0f));
  generalPage.addAndMakeVisible(sessionLicenseValue);

  actionsSectionLabel.setJustificationType(juce::Justification::centredLeft);
  actionsSectionLabel.setFont(makeAppFont(14.0f, AppFontWeight::SemiBold));
  generalPage.addAndMakeVisible(actionsSectionLabel);

  actionLogoutLabel.setJustificationType(juce::Justification::centredLeft);
  actionLogoutLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(actionLogoutLabel);

  actionInfoLabel.setJustificationType(juce::Justification::centredLeft);
  actionInfoLabel.setFont(makeAppFont(12.0f, AppFontWeight::Medium));
  generalPage.addAndMakeVisible(actionInfoLabel);

  generalPage.addAndMakeVisible(infoLinkButton);

  logoutButton.onClick = [this] {
    if (onLogoutRequested)
      onLogoutRequested();
  };
  generalPage.addAndMakeVisible(logoutButton);

  routingSectionLabel.setJustificationType(juce::Justification::centredLeft);
  routingSectionLabel.setFont(juce::FontOptions(14.0f));
  routingPage.addAndMakeVisible(routingSectionLabel);
  routingHintLabel.setJustificationType(juce::Justification::centredLeft);
  routingHintLabel.setFont(juce::FontOptions(12.0f));
  routingPage.addAndMakeVisible(routingHintLabel);

  routingViewport.setViewedComponent(&routingContent, false);
  routingViewport.setScrollBarsShown(true, false, false, false);
  routingViewport.setScrollBarThickness(2);
  routingPage.addAndMakeVisible(routingViewport);

  themeBox.onChange = [this] {
    const auto id = themeBox.getSelectedId();
    if (id <= 0 || !onThemeChanged)
      return;
    onThemeChanged(static_cast<ThemePreset>(id));
  };

  routingRows.clear();
  routingContent.removeAllChildren();
  if (routingDefaults.size() < kTrackCategories.size())
    routingDefaults.resize(kTrackCategories.size());

  std::vector<size_t> orderedIndices;
  orderedIndices.reserve(kTrackCategories.size());
  auto findIndex = [](const juce::String &name) -> int {
    for (size_t i = 0; i < kTrackCategories.size(); ++i) {
      if (juce::String(kTrackCategories[i]).equalsIgnoreCase(name))
        return static_cast<int>(i);
    }
    return -1;
  };
  const int clickIdx = findIndex("Click");
  const int cuesIdx = findIndex("Cues");
  if (clickIdx >= 0)
    orderedIndices.push_back(static_cast<size_t>(clickIdx));
  if (cuesIdx >= 0 && cuesIdx != clickIdx)
    orderedIndices.push_back(static_cast<size_t>(cuesIdx));
  for (size_t i = 0; i < kTrackCategories.size(); ++i) {
    if (static_cast<int>(i) == clickIdx || static_cast<int>(i) == cuesIdx)
      continue;
    orderedIndices.push_back(i);
  }

  for (size_t orderIdx = 0; orderIdx < orderedIndices.size(); ++orderIdx) {
    const size_t i = orderedIndices[orderIdx];
    const juce::String categoryName = kTrackCategories[i];
    auto row = std::make_unique<RoutingRow>(
        categoryName, availableOutputChannels, routingDefaults[i],
        [this, i](const CategoryRoutingDefaults &next) {
          if (i < routingDefaults.size())
            routingDefaults[i] = next;
          setRoutingDefaultsLocal(routingDefaults);
        });
    routingContent.addAndMakeVisible(*row);
    routingRowCategoryIndices.push_back(i);
    routingRows.push_back(std::move(row));
  }

  refreshRoutingForDevice();
  tabs.setCurrentTabIndex(0);
  applyLanguageTexts();
  refreshThemeColours();
}

void SettingsPanel::paint(juce::Graphics &g) {
  auto b = getLocalBounds().toFloat();
  g.setColour(UITheme::panelDark.brighter(0.06f));
  g.fillRoundedRectangle(b, 14.0f);
  g.setColour(UITheme::cardBorder.withAlpha(0.92f));
  g.drawRoundedRectangle(b.reduced(0.5f), 14.0f, 1.0f);

  const int headerH = useTouchOptimisedControls() ? 78 : 64;
  const int railW = useTouchOptimisedControls() ? 196 : 172;
  g.setColour(UITheme::cardBorder.withAlpha(0.88f));
  g.drawHorizontalLine(headerH, 0.0f, static_cast<float>(getWidth()));
  g.drawVerticalLine(railW, static_cast<float>(headerH),
                     static_cast<float>(getHeight()));
}

void SettingsPanel::resized() {
  auto area = getLocalBounds();
  const int headerH = useTouchOptimisedControls() ? 78 : 64;
  const int closeSize = useTouchOptimisedControls() ? 42 : 34;

  auto header = area.removeFromTop(headerH);
  title.setBounds(header.reduced(40, 0));
  auto closeArea = header.removeFromRight(closeSize + 18);
  closeButton.setBounds(closeArea.withSizeKeepingCentre(closeSize, closeSize));
  tabs.setBounds(area);

  layoutGeneralPage();
  layoutAudioPage();
  layoutRoutingPage();
}

void SettingsPanel::refreshThemeColours() {
  title.setColour(juce::Label::textColourId, UITheme::textPrimary);
  title.setJustificationType(juce::Justification::centred);
  title.setFont(makeAppFont(useTouchOptimisedControls() ? 20.0f : 18.0f,
                            AppFontWeight::Bold));
  closeButton.setColour(juce::TextButton::buttonColourId,
                        UITheme::cardBgHover.withAlpha(0.9f));
  closeButton.setColour(juce::TextButton::buttonOnColourId,
                        UITheme::cardBgPressed);
  closeButton.setColour(juce::TextButton::textColourOffId,
                        UITheme::textPrimary);
  closeButton.setColour(juce::TextButton::textColourOnId,
                        UITheme::textPrimary);
  closeButton.setButtonText("x");
  tabs.setColour(juce::TabbedButtonBar::tabTextColourId,
                 UITheme::textSecondary);
  tabs.setColour(juce::TabbedButtonBar::frontTextColourId,
                 UITheme::textPrimary);
  tabs.setColour(juce::TabbedButtonBar::tabOutlineColourId,
                 UITheme::cardBorder.withAlpha(0.88f));
  tabs.setColour(juce::TabbedButtonBar::frontOutlineColourId,
                 UITheme::cardBorder);
  themeLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  applyComboBoxTheme(themeBox, UITheme::cardBgHover.withAlpha(0.78f),
                     UITheme::cardBorder.withAlpha(0.84f));
  languageLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  applyComboBoxTheme(languageBox, UITheme::cardBgHover.withAlpha(0.78f),
                     UITheme::cardBorder.withAlpha(0.84f));
  expandLaunchLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  expandLaunchToggle.setColour(juce::ToggleButton::textColourId,
                               UITheme::textPrimary);
  expandLaunchToggle.setColour(juce::ToggleButton::tickColourId,
                               UITheme::accent);
  expandLaunchToggle.setColour(juce::ToggleButton::tickDisabledColourId,
                               UITheme::textSecondary);
  primeImportLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  primeImportToggle.setColour(juce::ToggleButton::textColourId,
                              UITheme::textPrimary);
  primeImportToggle.setColour(juce::ToggleButton::tickColourId,
                              UITheme::accent);
  primeImportToggle.setColour(juce::ToggleButton::tickDisabledColourId,
                              UITheme::textSecondary);
  stemOptimizationLabel.setColour(juce::Label::textColourId,
                                  UITheme::textSecondary);
  stemOptimizationToggle.setColour(juce::ToggleButton::textColourId,
                                   UITheme::textPrimary);
  stemOptimizationToggle.setColour(juce::ToggleButton::tickColourId,
                                   UITheme::accent);
  stemOptimizationToggle.setColour(juce::ToggleButton::tickDisabledColourId,
                                   UITheme::textSecondary);
  padsCrossfadeLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  applyComboBoxTheme(padsCrossfadeBox, UITheme::cardBgHover.withAlpha(0.78f),
                     UITheme::cardBorder.withAlpha(0.84f));
  metronomeClickStyleLabel.setColour(juce::Label::textColourId,
                                     UITheme::textSecondary);
  applyComboBoxTheme(metronomeClickStyleBox,
                     UITheme::cardBgHover.withAlpha(0.78f),
                     UITheme::cardBorder.withAlpha(0.84f));
  sessionSectionLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  sessionUidLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  sessionEmailLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  sessionPlanLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  sessionStatusLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  sessionExpiresLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  sessionLicenseLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  sessionUidValue.setColour(juce::Label::textColourId, UITheme::textPrimary);
  sessionEmailValue.setColour(juce::Label::textColourId, UITheme::textPrimary);
  sessionPlanValue.setColour(juce::Label::textColourId, UITheme::textPrimary);
  sessionStatusValue.setColour(juce::Label::textColourId, UITheme::textPrimary);
  sessionExpiresValue.setColour(juce::Label::textColourId, UITheme::textPrimary);
  sessionLicenseValue.setColour(juce::Label::textColourId, UITheme::textPrimary);
  actionsSectionLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  actionLogoutLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  actionInfoLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  infoLinkButton.setColour(juce::HyperlinkButton::textColourId,
                           UITheme::accent);
  logoutButton.setColour(juce::TextButton::buttonColourId,
                         juce::Colour::fromRGB(116, 42, 42));
  logoutButton.setColour(juce::TextButton::buttonOnColourId,
                         juce::Colour::fromRGB(148, 54, 54));
  logoutButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  routingSectionLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  routingHintLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  for (auto &row : routingRows)
    row->refreshThemeColours();
  repaint();
}

void SettingsPanel::applyLanguageTexts() {
  title.setText(L10n::tr(u8"Configuración", "Settings"),
                juce::dontSendNotification);
  closeButton.setButtonText("x");
  tabs.setTabName(0, L10n::tr("General", "General"));
  tabs.setTabName(1, L10n::tr(u8"Audio", "Audio"));
  tabs.setTabName(2, L10n::tr("Ruteo", "Routing"));
  themeLabel.setText(L10n::tr("Tema", "Theme"), juce::dontSendNotification);
  languageLabel.setText(L10n::tr("Idioma", "Language"),
                        juce::dontSendNotification);
  expandLaunchLabel.setText(L10n::tr("Ventana al iniciar", "Window on launch"),
                            juce::dontSendNotification);
  expandLaunchToggle.setButtonText(
      L10n::tr("Expandir al area de trabajo", "Expand to work area"));
  primeImportLabel.setText(
      L10n::tr("Importar desde disco local", "Import from local disk"),
                           juce::dontSendNotification);
  primeImportToggle.setButtonText(
      L10n::tr(u8"Habilitar importación desde disco local",
               "Enable import from local disk"));
  stemOptimizationLabel.setText(
      L10n::tr("Optimizacion de stems",
               "Stem optimization"),
      juce::dontSendNotification);
  stemOptimizationToggle.setButtonText(
      L10n::tr("Convertir stems para ahorrar almacenamiento",
               "Convert stems to save storage"));
  padsCrossfadeLabel.setText(
      L10n::tr("Crossfade de pads", "Pads crossfade"),
      juce::dontSendNotification);
  metronomeClickStyleLabel.setText(
      L10n::tr("Estilo del click", "Click style"),
      juce::dontSendNotification);
  padsCrossfadeBox.changeItemText(1, L10n::tr(u8"Rápido (1s)", "Fast (1s)"));
  padsCrossfadeBox.changeItemText(2, L10n::tr("Medio (3s)", "Medium (3s)"));
  padsCrossfadeBox.changeItemText(3, L10n::tr("Largo (5s)", "Long (5s)"));
  metronomeClickStyleBox.changeItemText(1, "Classic");
  metronomeClickStyleBox.changeItemText(2, "Woodblock");
  metronomeClickStyleBox.changeItemText(3, "Cowbell");
  metronomeClickStyleBox.changeItemText(4, "Digital");
  metronomeClickStyleBox.changeItemText(5, "Gentle");
  metronomeClickStyleBox.changeItemText(6, "Percussive");
  metronomeClickStyleBox.changeItemText(7, "Saw");
  metronomeClickStyleBox.changeItemText(8, "Blip");
  sessionSectionLabel.setText(L10n::tr(u8"Sesión", "Session"),
                              juce::dontSendNotification);
  sessionUidLabel.setText("UID", juce::dontSendNotification);
  sessionEmailLabel.setText("Email", juce::dontSendNotification);
  sessionPlanLabel.setText(L10n::tr("Plan", "Plan"), juce::dontSendNotification);
  sessionStatusLabel.setText(L10n::tr("Estado", "Status"),
                             juce::dontSendNotification);
  sessionExpiresLabel.setText(L10n::tr("Vence", "Expires"),
                              juce::dontSendNotification);
  sessionLicenseLabel.setText(L10n::tr("Licencia", "License key"),
                              juce::dontSendNotification);
  sessionUidValue.setText(session.uid.isNotEmpty() ? session.uid : "-",
                          juce::dontSendNotification);
  sessionEmailValue.setText(session.email.isNotEmpty() ? session.email : "-",
                            juce::dontSendNotification);
  sessionPlanValue.setText(session.plan.isNotEmpty() ? session.plan : "-",
                           juce::dontSendNotification);
  sessionStatusValue.setText(session.subscriptionStatus.isNotEmpty()
                                 ? session.subscriptionStatus
                                 : "-",
                             juce::dontSendNotification);
  sessionExpiresValue.setText(
      session.expiresAt.isNotEmpty() ? formatIsoDateTime(session.expiresAt) : "-",
      juce::dontSendNotification);
  sessionLicenseValue.setText(
      session.licenseKey.isNotEmpty() ? session.licenseKey : "-",
      juce::dontSendNotification);
  actionsSectionLabel.setText(L10n::tr("Acciones", "Actions"),
                              juce::dontSendNotification);
  actionLogoutLabel.setText(L10n::tr(u8"Cerrar sesión en esta aplicacion",
                                     "Sign out on this app"),
                            juce::dontSendNotification);
  actionInfoLabel.setText(L10n::tr(u8"Saber más", "More information"),
                          juce::dontSendNotification);
  logoutButton.setButtonText(L10n::tr(u8"Cerrar sesión", "Sign out"));
  infoLinkButton.setButtonText(L10n::tr(u8"Saber más", "More information"));
  routingSectionLabel.setText(L10n::tr("Ruteo por tipo", "Routing by category"),
                              juce::dontSendNotification);
  routingHintLabel.setText(
      L10n::tr("Define salidas por categoría para el dispositivo actual.",
               "Set per-category outputs for the current device."),
      juce::dontSendNotification);
}

void SettingsPanel::layoutGeneralPage() {
  auto area = generalPage.getLocalBounds().reduced(
      useTouchOptimisedControls() ? 28 : 22,
      useTouchOptimisedControls() ? 24 : 18);
  const int rowH = useTouchOptimisedControls() ? 62 : 52;
  const int controlH = touchOptimisedControlHeight();
  const int rightW = juce::jlimit(220, 360, area.getWidth() / 3);
  const int rowGap = 6;

  if (appearanceCard != nullptr)
    appearanceCard->setBounds({});
  if (playbackCard != nullptr)
    playbackCard->setBounds({});
  if (sessionCard != nullptr)
    sessionCard->setBounds({});
  sessionSectionLabel.setBounds({});
  sessionUidLabel.setBounds({});
  sessionUidValue.setBounds({});
  sessionEmailLabel.setBounds({});
  sessionEmailValue.setBounds({});
  sessionPlanLabel.setBounds({});
  sessionPlanValue.setBounds({});
  sessionStatusLabel.setBounds({});
  sessionStatusValue.setBounds({});
  sessionExpiresLabel.setBounds({});
  sessionExpiresValue.setBounds({});
  sessionLicenseLabel.setBounds({});
  sessionLicenseValue.setBounds({});
  actionsSectionLabel.setBounds({});
  actionLogoutLabel.setBounds({});
  actionInfoLabel.setBounds({});
  infoLinkButton.setBounds({});

  auto layoutChoiceRow = [&](juce::Rectangle<int> row, juce::Label &label,
                             juce::Component &control) {
    auto right = row.removeFromRight(rightW);
    label.setBounds(row.reduced(0, 8));
    control.setBounds(right.withSizeKeepingCentre(right.getWidth(), controlH));
  };

  auto layoutToggleRow = [&](juce::Rectangle<int> row, juce::Label &label,
                             juce::ToggleButton &toggle) {
    auto right = row.removeFromRight(rightW);
    label.setBounds(row.reduced(0, 8));
    toggle.setBounds(right.withSizeKeepingCentre(right.getWidth(), controlH));
  };

  layoutChoiceRow(area.removeFromTop(rowH), themeLabel, themeBox);
  area.removeFromTop(rowGap);
  layoutChoiceRow(area.removeFromTop(rowH), languageLabel, languageBox);
  area.removeFromTop(rowGap);
  layoutToggleRow(area.removeFromTop(rowH), expandLaunchLabel, expandLaunchToggle);
  area.removeFromTop(rowGap);
  layoutToggleRow(area.removeFromTop(rowH), primeImportLabel, primeImportToggle);
  area.removeFromTop(rowGap);
  layoutToggleRow(area.removeFromTop(rowH), stemOptimizationLabel,
                  stemOptimizationToggle);
  area.removeFromTop(rowGap);
  layoutChoiceRow(area.removeFromTop(rowH), padsCrossfadeLabel, padsCrossfadeBox);
  area.removeFromTop(rowGap);
  layoutChoiceRow(area.removeFromTop(rowH), metronomeClickStyleLabel,
                  metronomeClickStyleBox);

  if (actionsCard != nullptr) {
    const int railW = useTouchOptimisedControls() ? 196 : 172;
    actionsCard->setBounds(16, 240, railW - 32,
                           useTouchOptimisedControls() ? 58 : 50);
    logoutButton.setBounds(actionsCard->getBounds().reduced(12, 10));
  }
}

juce::String SettingsPanel::formatIsoDateTime(const juce::String &isoDateTime) {
  const auto trimmed = isoDateTime.trim();
  if (trimmed.isEmpty())
    return "-";

  const auto parsed = juce::Time::fromISO8601(trimmed);
  if (parsed.toMilliseconds() <= 0)
    return trimmed;

  return parsed.formatted("%Y-%m-%d %H:%M");
}

void SettingsPanel::layoutAudioPage() {
  if (audioSetupComp != nullptr) {
    audioSetupComp->setBounds(audioPage.getLocalBounds().reduced(18, 16));
  }
}

void SettingsPanel::layoutRoutingPage() {
  auto area = routingPage.getLocalBounds().reduced(18, 16);

  auto labelArea = area.removeFromTop(22);
  routingSectionLabel.setBounds(labelArea);
  
  area.removeFromTop(8);
  auto hintArea = area.removeFromBottom(24);
  routingHintLabel.setBounds(hintArea);
  area.removeFromBottom(6);
  
  auto listArea = area;
  routingViewport.setBounds(listArea);
  
  const int rowH = 36;
  int y = 0;
  const int contentW = juce::jmax(1, routingViewport.getWidth() - 16);
  for (auto &rowComp : routingRows) {
    rowComp->setBounds(0, y, contentW, rowH);
    y += rowH;
  }
  routingContent.setSize(contentW, juce::jmax(y + 2, routingViewport.getHeight()));
}

SettingsPanel::~SettingsPanel() {
  if (deviceManager != nullptr)
    deviceManager->removeChangeListener(this);
}

void SettingsPanel::changeListenerCallback(juce::ChangeBroadcaster* source) {
  if (source == deviceManager) {
    refreshRoutingForDevice();
  }
}

void SettingsPanel::refreshRoutingForDevice() {
  if (deviceManager != nullptr) {
    auto *currentDevice = deviceManager->getCurrentAudioDevice();
    const juce::String typeName = deviceManager->getCurrentAudioDeviceType();
    const juce::String deviceName =
        currentDevice != nullptr ? currentDevice->getName() : juce::String{};
    currentDeviceKey = typeName.isNotEmpty() && deviceName.isNotEmpty()
                           ? (typeName + "|" + deviceName)
                           : juce::String{};
    if (currentDevice != nullptr) {
      availableOutputChannels =
          juce::jmax(1, currentDevice->getOutputChannelNames().size());
    }
  }

  const auto deviceDefaults =
      AppSettings::getRoutingDefaultsForDevice(currentDeviceKey);
  if (routingDefaults.size() < kTrackCategories.size())
    routingDefaults.resize(kTrackCategories.size());
  for (size_t i = 0; i < kTrackCategories.size(); ++i) {
    if (i < deviceDefaults.size())
      routingDefaults[i] = deviceDefaults[i];
  }

  for (size_t rowIndex = 0; rowIndex < routingRows.size(); ++rowIndex) {
    const size_t categoryIndex =
        rowIndex < routingRowCategoryIndices.size()
            ? routingRowCategoryIndices[rowIndex]
            : rowIndex;
    const CategoryRoutingDefaults def =
        categoryIndex < routingDefaults.size() ? routingDefaults[categoryIndex]
                                               : CategoryRoutingDefaults{};
    routingRows[rowIndex]->setDefaults(def, availableOutputChannels);
    if (categoryIndex < routingDefaults.size())
      routingDefaults[categoryIndex] = routingRows[rowIndex]->getDefaults();
  }
}

void SettingsPanel::setRoutingDefaultsLocal(
    const std::vector<CategoryRoutingDefaults> &next) {
  routingDefaults = next;
  if (onRoutingDefaultsChanged)
    onRoutingDefaultsChanged(currentDeviceKey, routingDefaults);
}
