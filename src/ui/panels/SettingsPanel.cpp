#include "SettingsPanel.h"

#include "../../audio/OutputChannelCatalog.h"
#include "../../data/Localization.h"
#include "../../data/SongData.h"

#include <algorithm>
#include <map>

class SettingsPanel::RoutingRow final : public juce::Component {
public:
  RoutingRow(const juce::String &categoryName, const int outputChannels,
             const CategoryRoutingDefaults &defaults,
             std::function<void(const CategoryRoutingDefaults &)> changedCb)
      : onChanged(std::move(changedCb)),
        availableOutputChannels(juce::jmax(1, outputChannels)),
        current(defaults) {
    categoryLabel.setText(categoryName, juce::dontSendNotification);
    categoryLabel.setFont(juce::FontOptions(11.5f));
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
    auto r = getLocalBounds().reduced(4, 2);
    auto nameCol = r.removeFromLeft(120);
    auto modeCol = r.removeFromLeft(90);
    categoryLabel.setBounds(nameCol);
    modeBox.setBounds(modeCol.reduced(6, 0));
    channelBox.setBounds(r.reduced(4, 0));
  }

  void refreshThemeColours() {
    categoryLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
    modeBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBg);
    modeBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
    modeBox.setColour(juce::ComboBox::outlineColourId,
                      UITheme::cardBorder.withAlpha(0.7f));
    modeBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
    channelBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBg);
    channelBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
    channelBox.setColour(juce::ComboBox::outlineColourId,
                         UITheme::cardBorder.withAlpha(0.7f));
    channelBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
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
      onLogoutRequested(std::move(logoutCb)),
      session(std::move(sessionSummary)),
      onRoutingDefaultsChanged(std::move(routingCb)),
      routingDefaults(std::move(defaults)),
      availableOutputChannels(juce::jmax(1, outputChannels)),
      deviceManager(deviceManagerToUse) {
  juce::ignoreUnused(closeCb, maxInputChannels, maxOutputChannels);
  title.setJustificationType(juce::Justification::centredLeft);
  title.setFont(juce::FontOptions(16.0f).withKerningFactor(0.04f));
  addAndMakeVisible(title);

  tabs.addTab(L10n::tr("General", "General"), UITheme::cardBg, &generalPage,
              false);
  tabs.addTab(L10n::tr("Ruteo", "Routing"), UITheme::cardBg, &routingPage,
              false);
  tabs.setTabBarDepth(30);
  tabs.onTabChanged = [this](int newIndex, const juce::String &) {
    if (newIndex == 1) {
      refreshDeviceOptions();
      refreshRoutingForDevice();
    }
  };
  addAndMakeVisible(tabs);

  deviceLabel.setJustificationType(juce::Justification::centredLeft);
  deviceLabel.setFont(juce::FontOptions(11.0f));
  routingPage.addAndMakeVisible(deviceLabel);

  deviceBox.onChange = [this] {
    const int id = deviceBox.getSelectedId();
    if (id > 0)
      applyDeviceSelection(id - 1);
  };
  routingPage.addAndMakeVisible(deviceBox);

  themeLabel.setJustificationType(juce::Justification::centredLeft);
  themeLabel.setFont(juce::FontOptions(12.0f));
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
  themeBox.setJustificationType(juce::Justification::centredLeft);
  generalPage.addAndMakeVisible(themeBox);

  languageLabel.setJustificationType(juce::Justification::centredLeft);
  languageLabel.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(languageLabel);

  languageBox.addItem("Espanol", static_cast<int>(AppLanguage::Spanish) + 1);
  languageBox.addItem("English", static_cast<int>(AppLanguage::English) + 1);
  languageBox.setSelectedId(static_cast<int>(language) + 1,
                            juce::dontSendNotification);
  languageBox.setJustificationType(juce::Justification::centredLeft);
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
  expandLaunchLabel.setFont(juce::FontOptions(12.0f));
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
  primeImportLabel.setFont(juce::FontOptions(12.0f));
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
  stemOptimizationLabel.setFont(juce::FontOptions(12.0f));
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
  padsCrossfadeLabel.setFont(juce::FontOptions(12.0f));
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

  sessionSectionLabel.setJustificationType(juce::Justification::centredLeft);
  sessionSectionLabel.setFont(juce::FontOptions(14.0f));
  generalPage.addAndMakeVisible(sessionSectionLabel);

  sessionUidLabel.setJustificationType(juce::Justification::centredLeft);
  sessionUidLabel.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(sessionUidLabel);
  sessionUidValue.setJustificationType(juce::Justification::centredLeft);
  sessionUidValue.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(sessionUidValue);

  sessionEmailLabel.setJustificationType(juce::Justification::centredLeft);
  sessionEmailLabel.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(sessionEmailLabel);
  sessionEmailValue.setJustificationType(juce::Justification::centredLeft);
  sessionEmailValue.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(sessionEmailValue);

  sessionPlanLabel.setJustificationType(juce::Justification::centredLeft);
  sessionPlanLabel.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(sessionPlanLabel);
  sessionPlanValue.setJustificationType(juce::Justification::centredLeft);
  sessionPlanValue.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(sessionPlanValue);

  sessionStatusLabel.setJustificationType(juce::Justification::centredLeft);
  sessionStatusLabel.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(sessionStatusLabel);
  sessionStatusValue.setJustificationType(juce::Justification::centredLeft);
  sessionStatusValue.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(sessionStatusValue);

  sessionExpiresLabel.setJustificationType(juce::Justification::centredLeft);
  sessionExpiresLabel.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(sessionExpiresLabel);
  sessionExpiresValue.setJustificationType(juce::Justification::centredLeft);
  sessionExpiresValue.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(sessionExpiresValue);

  sessionLicenseLabel.setJustificationType(juce::Justification::centredLeft);
  sessionLicenseLabel.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(sessionLicenseLabel);
  sessionLicenseValue.setJustificationType(juce::Justification::centredLeft);
  sessionLicenseValue.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(sessionLicenseValue);

  actionsSectionLabel.setJustificationType(juce::Justification::centredLeft);
  actionsSectionLabel.setFont(juce::FontOptions(14.0f));
  generalPage.addAndMakeVisible(actionsSectionLabel);

  actionLogoutLabel.setJustificationType(juce::Justification::centredLeft);
  actionLogoutLabel.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(actionLogoutLabel);

  actionInfoLabel.setJustificationType(juce::Justification::centredLeft);
  actionInfoLabel.setFont(juce::FontOptions(12.0f));
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

  refreshDeviceOptions();
  refreshRoutingForDevice();
  tabs.setCurrentTabIndex(0);
  applyLanguageTexts();
  refreshThemeColours();
}

void SettingsPanel::paint(juce::Graphics &g) {
  auto b = getLocalBounds().toFloat();
  g.setColour(UITheme::cardBg);
  g.fillRoundedRectangle(b, 6.0f);
  g.setColour(UITheme::cardBorder);
  g.drawRoundedRectangle(b, 6.0f, 1.0f);
}

void SettingsPanel::resized() {
  auto area = getLocalBounds().reduced(16);
  auto header = area.removeFromTop(28);
  title.setBounds(header);
  area.removeFromTop(10);
  tabs.setBounds(area);

  layoutGeneralPage();
  layoutRoutingPage();
}

void SettingsPanel::refreshThemeColours() {
  title.setColour(juce::Label::textColourId, UITheme::textPrimary);
  tabs.setColour(juce::TabbedButtonBar::tabTextColourId,
                 UITheme::textSecondary);
  tabs.setColour(juce::TabbedButtonBar::frontTextColourId,
                 UITheme::textPrimary);
  tabs.setColour(juce::TabbedButtonBar::tabOutlineColourId,
                 UITheme::cardBorder.withAlpha(0.7f));
  tabs.setColour(juce::TabbedButtonBar::frontOutlineColourId,
                 UITheme::cardBorder);
  deviceLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  deviceBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBg);
  deviceBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
  deviceBox.setColour(juce::ComboBox::outlineColourId,
                      UITheme::cardBorder.withAlpha(0.7f));
  deviceBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
  themeLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  themeBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBgHover);
  themeBox.setColour(juce::ComboBox::outlineColourId,
                     UITheme::cardBorder.withAlpha(0.65f));
  themeBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
  themeBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
  languageLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  languageBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBgHover);
  languageBox.setColour(juce::ComboBox::outlineColourId,
                        UITheme::cardBorder.withAlpha(0.65f));
  languageBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
  languageBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
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
  padsCrossfadeBox.setColour(juce::ComboBox::backgroundColourId,
                             UITheme::cardBgHover);
  padsCrossfadeBox.setColour(juce::ComboBox::outlineColourId,
                             UITheme::cardBorder.withAlpha(0.65f));
  padsCrossfadeBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
  padsCrossfadeBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
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
                         juce::Colour::fromRGB(125, 45, 45));
  logoutButton.setColour(juce::TextButton::buttonOnColourId,
                         juce::Colour::fromRGB(148, 54, 54));
  logoutButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  routingSectionLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  for (auto &row : routingRows)
    row->refreshThemeColours();
  repaint();
}

void SettingsPanel::applyLanguageTexts() {
  title.setText(L10n::tr("Configuracion", "Settings"),
                juce::dontSendNotification);
  tabs.setTabName(0, L10n::tr("General", "General"));
  tabs.setTabName(1, L10n::tr("Ruteo", "Routing"));
  deviceLabel.setText(L10n::tr("Interfaz", "Device"), juce::dontSendNotification);
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
      L10n::tr("Habilitar importacion desde disco local",
               "Enable import from local disk"));
  stemOptimizationLabel.setText(
      L10n::tr("Optimizacion de stems", "Stem optimization"),
      juce::dontSendNotification);
  stemOptimizationToggle.setButtonText(
      L10n::tr("Optimizar almacenamiento en disco",
               "Optimize disk storage"));
  padsCrossfadeLabel.setText(
      L10n::tr("Crossfade de pads", "Pads crossfade"),
      juce::dontSendNotification);
  padsCrossfadeBox.changeItemText(1, L10n::tr("Rapido (1s)", "Fast (1s)"));
  padsCrossfadeBox.changeItemText(2, L10n::tr("Medio (3s)", "Medium (3s)"));
  padsCrossfadeBox.changeItemText(3, L10n::tr("Largo (5s)", "Long (5s)"));
  sessionSectionLabel.setText(L10n::tr("Sesion", "Session"),
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
  actionLogoutLabel.setText(L10n::tr("Salir de la aplicacion", "Logout from application"),
                            juce::dontSendNotification);
  actionInfoLabel.setText(L10n::tr("Saber mas", "More information"),
                          juce::dontSendNotification);
  logoutButton.setButtonText(
      L10n::tr("Salir de la aplicacion", "Logout from application"));
  infoLinkButton.setButtonText(L10n::tr("Saber mas", "More information"));
  routingSectionLabel.setText(L10n::tr("Ruteo por tipo", "Routing by category"),
                              juce::dontSendNotification);
}

void SettingsPanel::layoutGeneralPage() {
  auto area = generalPage.getLocalBounds().reduced(4);
  auto row = area.removeFromTop(26);
  themeLabel.setBounds(row.removeFromLeft(90));
  themeBox.setBounds(row);
  area.removeFromTop(8);
  auto languageRow = area.removeFromTop(26);
  languageLabel.setBounds(languageRow.removeFromLeft(90));
  languageBox.setBounds(languageRow);
  area.removeFromTop(14);
  auto expandRow = area.removeFromTop(26);
  expandLaunchLabel.setBounds(expandRow.removeFromLeft(160));
  expandLaunchToggle.setBounds(expandRow);
  area.removeFromTop(10);
  auto primeRow = area.removeFromTop(26);
  primeImportLabel.setBounds(primeRow.removeFromLeft(160));
  primeImportToggle.setBounds(primeRow);
  area.removeFromTop(10);
  auto stemOptimizationRow = area.removeFromTop(26);
  stemOptimizationLabel.setBounds(stemOptimizationRow.removeFromLeft(160));
  stemOptimizationToggle.setBounds(stemOptimizationRow);
  area.removeFromTop(10);
  auto padsCrossfadeRow = area.removeFromTop(26);
  padsCrossfadeLabel.setBounds(padsCrossfadeRow.removeFromLeft(160));
  padsCrossfadeBox.setBounds(padsCrossfadeRow);
  area.removeFromTop(16);
  sessionSectionLabel.setBounds(area.removeFromTop(24));
  area.removeFromTop(4);
  auto sessionUidRow = area.removeFromTop(24);
  sessionUidLabel.setBounds(sessionUidRow.removeFromLeft(120));
  sessionUidValue.setBounds(sessionUidRow);
  auto sessionEmailRow = area.removeFromTop(24);
  sessionEmailLabel.setBounds(sessionEmailRow.removeFromLeft(120));
  sessionEmailValue.setBounds(sessionEmailRow);
  auto sessionPlanRow = area.removeFromTop(24);
  sessionPlanLabel.setBounds(sessionPlanRow.removeFromLeft(120));
  sessionPlanValue.setBounds(sessionPlanRow);
  auto sessionStatusRow = area.removeFromTop(24);
  sessionStatusLabel.setBounds(sessionStatusRow.removeFromLeft(120));
  sessionStatusValue.setBounds(sessionStatusRow);
  auto sessionExpiresRow = area.removeFromTop(24);
  sessionExpiresLabel.setBounds(sessionExpiresRow.removeFromLeft(120));
  sessionExpiresValue.setBounds(sessionExpiresRow);
  auto sessionLicenseRow = area.removeFromTop(24);
  sessionLicenseLabel.setBounds(sessionLicenseRow.removeFromLeft(120));
  sessionLicenseValue.setBounds(sessionLicenseRow);
  area.removeFromTop(12);
  actionsSectionLabel.setBounds(area.removeFromTop(24));
  area.removeFromTop(4);
  actionLogoutLabel.setBounds(area.removeFromTop(22));
  logoutButton.setBounds(area.removeFromTop(30));
  area.removeFromTop(8);
  actionInfoLabel.setBounds(area.removeFromTop(22));
  infoLinkButton.setBounds(area.removeFromTop(24));
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

void SettingsPanel::layoutRoutingPage() {
  auto area = routingPage.getLocalBounds().reduced(4);
  routingSectionLabel.setBounds(area.removeFromTop(22));
  area.removeFromTop(8);
  auto deviceRow = area.removeFromTop(26);
  deviceLabel.setBounds(deviceRow.removeFromLeft(70));
  deviceBox.setBounds(deviceRow);
  area.removeFromTop(8);
  routingViewport.setBounds(area);
  const int rowH = 28;
  int y = 0;
  const int contentW = juce::jmax(1, routingViewport.getWidth() - 6);
  for (auto &rowComp : routingRows) {
    rowComp->setBounds(2, y, contentW, rowH);
    y += rowH + 4;
  }
  routingContent.setSize(contentW + 4,
                         juce::jmax(y + 2, routingViewport.getHeight()));
}

void SettingsPanel::refreshDeviceOptions() {
  deviceOptions.clear();
  deviceBox.clear(juce::dontSendNotification);

  if (deviceManager == nullptr) {
    deviceBox.addItem(L10n::tr("Audio no disponible", "Audio unavailable"), 1);
    deviceBox.setSelectedId(1, juce::dontSendNotification);
    return;
  }

  auto *currentDevice = deviceManager->getCurrentAudioDevice();
  const juce::String currentType = deviceManager->getCurrentAudioDeviceType();
  const juce::String currentName =
      currentDevice != nullptr ? currentDevice->getName() : juce::String{};

  auto &deviceTypes = deviceManager->getAvailableDeviceTypes();
  struct RankedOption {
    DeviceOption option;
    int score = 0;
  };
  std::map<juce::String, RankedOption> bestByDeviceName;

  for (auto *ioType : deviceTypes) {
    if (ioType == nullptr)
      continue;

    const juce::String typeName = ioType->getTypeName();
    const auto outputs = ioType->getDeviceNames(false);
    const int defaultIndex = ioType->getDefaultDeviceIndex(false);
    for (int i = 0; i < outputs.size(); ++i) {
      const auto name = outputs[i].trim();
      if (name.isEmpty())
        continue;

      int score = 0;
      if (typeName.equalsIgnoreCase(currentType))
        score += 2;
      if (i == defaultIndex)
        score += 1;

      const auto key = name.toLowerCase();
      auto it = bestByDeviceName.find(key);
      if (it == bestByDeviceName.end() || score > it->second.score) {
        bestByDeviceName[key] = RankedOption{{typeName, name}, score};
      } else if (score == it->second.score &&
                 typeName.compareIgnoreCase(it->second.option.typeName) < 0) {
        it->second.option = {typeName, name};
      }
    }
  }

  for (const auto &entry : bestByDeviceName)
    deviceOptions.push_back(entry.second.option);

  std::sort(deviceOptions.begin(), deviceOptions.end(),
            [](const DeviceOption &a, const DeviceOption &b) {
              const auto nameCmp = a.deviceName.compareIgnoreCase(b.deviceName);
              if (nameCmp != 0)
                return nameCmp < 0;
              return a.typeName.compareIgnoreCase(b.typeName) < 0;
            });

  int selectedId = 0;
  for (size_t i = 0; i < deviceOptions.size(); ++i) {
    const auto &opt = deviceOptions[i];
    const bool includeType = opt.typeName.isNotEmpty();
    const juce::String label =
        includeType ? (opt.typeName + " - " + opt.deviceName) : opt.deviceName;
    const int itemId = static_cast<int>(i) + 1;
    deviceBox.addItem(label, itemId);
    if (opt.deviceName.equalsIgnoreCase(currentName) &&
        (selectedId == 0 || opt.typeName.equalsIgnoreCase(currentType)))
      selectedId = itemId;
  }

  if (selectedId > 0)
    deviceBox.setSelectedId(selectedId, juce::dontSendNotification);
  else if (deviceBox.getNumItems() > 0)
    deviceBox.setSelectedId(1, juce::dontSendNotification);
}

void SettingsPanel::applyDeviceSelection(const int optionIndex) {
  if (deviceManager == nullptr)
    return;
  if (!juce::isPositiveAndBelow(optionIndex,
                                static_cast<int>(deviceOptions.size())))
    return;

  const auto &opt = deviceOptions[static_cast<size_t>(optionIndex)];
  auto *currentDevice = deviceManager->getCurrentAudioDevice();
  const juce::String currentType = deviceManager->getCurrentAudioDeviceType();
  const juce::String currentName =
      currentDevice != nullptr ? currentDevice->getName() : juce::String{};
  if (currentType.equalsIgnoreCase(opt.typeName) &&
      currentName.equalsIgnoreCase(opt.deviceName)) {
    refreshDeviceOptions();
    refreshRoutingForDevice();
    return;
  }

  deviceManager->setCurrentAudioDeviceType(opt.typeName, false);
  if (deviceManager->getCurrentAudioDeviceType() != opt.typeName) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        L10n::tr("No se pudo cambiar el driver de audio",
                 "Couldn't switch audio driver"),
        L10n::tr("No se pudo aplicar el driver de audio seleccionado.",
                 "The selected audio driver couldn't be applied."));
    refreshDeviceOptions();
    refreshRoutingForDevice();
    return;
  }

  juce::AudioDeviceManager::AudioDeviceSetup setup;
  deviceManager->getAudioDeviceSetup(setup);
  setup.outputDeviceName = opt.deviceName;
  setup.inputDeviceName = {};
  setup.useDefaultInputChannels = false;
  setup.inputChannels.clear();
  setup.useDefaultOutputChannels = true;

  const auto setupError = deviceManager->setAudioDeviceSetup(setup, false);
  if (setupError.isNotEmpty()) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        L10n::tr("No se pudo cambiar la interfaz de audio",
                 "Couldn't switch audio interface"),
        setupError);
  }

  refreshDeviceOptions();
  refreshRoutingForDevice();
  setRoutingDefaultsLocal(routingDefaults);
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
