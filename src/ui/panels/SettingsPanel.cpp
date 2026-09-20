#include "SettingsPanel.h"

#include "../../audio/OutputChannelCatalog.h"
#include "../../data/Localization.h"
#include "../../data/SongData.h"

#include <algorithm>
#include <map>

void SettingsPanel::SettingsTabButton::paintButton(
    juce::Graphics &g, const bool shouldDrawButtonAsHighlighted,
    const bool shouldDrawButtonAsDown) {
  auto bounds = getLocalBounds().toFloat().reduced(0.5f);
  auto background = findColour(juce::TextButton::buttonColourId);
  if (shouldDrawButtonAsDown && !active)
    background = findColour(juce::TextButton::buttonOnColourId);
  else if (shouldDrawButtonAsHighlighted && !active)
    background = background.brighter(0.08f);

  g.setColour(background);
  juce::Path buttonShape;
  if (position == Position::Middle) {
    g.fillRect(bounds);
    buttonShape.addRectangle(bounds);
  } else {
    const bool roundTop = position == Position::First;
    const bool roundBottom = position == Position::Last;
    buttonShape.addRoundedRectangle(
        bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
        8.0f, 8.0f, roundTop, roundTop, roundBottom, roundBottom);
    g.fillPath(buttonShape);
  }
  g.setColour(UITheme::textPrimary.withAlpha(0.14f));
  g.strokePath(buttonShape, juce::PathStrokeType(1.0f));
  g.setColour(findColour(juce::TextButton::textColourOffId));
  g.setFont(makeAppFont(15.0f,
                        active ? AppFontWeight::SemiBold
                               : AppFontWeight::Medium));
  g.drawText(getButtonText(), getLocalBounds().reduced(16, 0),
             juce::Justification::centredLeft, true);

}

void SettingsPanel::SettingsCloseButton::paintButton(
    juce::Graphics &g, const bool shouldDrawButtonAsHighlighted,
    const bool shouldDrawButtonAsDown) {
  auto circle = getLocalBounds().toFloat().reduced(2.0f);
  auto fill = UITheme::cardBgHover.withAlpha(0.7f);
  if (shouldDrawButtonAsDown)
    fill = UITheme::cardBgPressed;
  else if (shouldDrawButtonAsHighlighted)
    fill = fill.brighter(0.08f);
  g.setColour(fill);
  g.fillEllipse(circle);

  const auto centre = circle.getCentre();
  const float arm = circle.getWidth() * 0.19f;
  g.setColour(UITheme::textPrimary.withAlpha(0.9f));
  g.drawLine(centre.x - arm, centre.y - arm, centre.x + arm, centre.y + arm,
             1.8f);
  g.drawLine(centre.x + arm, centre.y - arm, centre.x - arm, centre.y + arm,
             1.8f);
}

void SettingsPanel::SettingsLogoutButton::paintButton(
    juce::Graphics &g, const bool shouldDrawButtonAsHighlighted,
    const bool shouldDrawButtonAsDown) {
  auto background = UITheme::accentMuted;
  if (shouldDrawButtonAsDown)
    background = background.darker(0.12f);
  else if (shouldDrawButtonAsHighlighted)
    background = background.brighter(0.06f);
  g.setColour(background);
  g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
  g.setColour(UITheme::textPrimary);
  g.setFont(makeAppFont(13.0f, AppFontWeight::Medium));
  g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred,
             true);
}

SettingsPanel::SettingsCopyButton::SettingsCopyButton() {
  setWantsKeyboardFocus(false);
  onClick = [this] {
    if (showingConfirmation || valueToCopy.isEmpty())
      return;
    juce::SystemClipboard::copyTextToClipboard(valueToCopy);
    showingConfirmation = true;
    setInterceptsMouseClicks(false, false);
    repaint();
    startTimer(1500);
  };
}

void SettingsPanel::SettingsCopyButton::setValueToCopy(
    const juce::String &value) {
  valueToCopy = value;
}

void SettingsPanel::SettingsCopyButton::paintButton(
    juce::Graphics &g, const bool shouldDrawButtonAsHighlighted,
    const bool shouldDrawButtonAsDown) {
  auto bounds = getLocalBounds().toFloat().reduced(5.0f);
  if (showingConfirmation) {
    juce::Path check;
    check.startNewSubPath(bounds.getX(), bounds.getCentreY());
    check.lineTo(bounds.getX() + bounds.getWidth() * 0.38f,
                 bounds.getBottom());
    check.lineTo(bounds.getRight(), bounds.getY());
    g.setColour(UITheme::accentSoft);
    g.strokePath(check, juce::PathStrokeType(2.0f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    return;
  }

  auto colour = UITheme::textSecondary;
  if (shouldDrawButtonAsDown)
    colour = colour.brighter(0.2f);
  else if (shouldDrawButtonAsHighlighted)
    colour = colour.brighter(0.1f);
  g.setColour(colour);
  auto rear = bounds.withSizeKeepingCentre(bounds.getWidth() * 0.72f,
                                            bounds.getHeight() * 0.72f);
  rear.translate(-2.0f, -2.0f);
  auto front = rear.translated(4.0f, 4.0f);
  g.drawRoundedRectangle(rear, 2.0f, 1.4f);
  g.drawRoundedRectangle(front, 2.0f, 1.4f);
}

void SettingsPanel::SettingsCopyButton::timerCallback() {
  stopTimer();
  showingConfirmation = false;
  setInterceptsMouseClicks(true, false);
  repaint();
}

class SettingsPanel::SettingsCard final : public juce::Component {
public:
  void paint(juce::Graphics &g) override {
    const auto b = getLocalBounds().toFloat();
    g.setColour(UITheme::cardBg);
    g.fillRoundedRectangle(b, 10.0f);
  }
};

class SettingsPanel::SettingsRowSeparator final : public juce::Component {
public:
  SettingsRowSeparator() { setInterceptsMouseClicks(false, false); }

  void paint(juce::Graphics &g) override {
    g.setColour(UITheme::textPrimary.withAlpha(0.06f));
    g.fillRect(getLocalBounds());
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
    
    auto channelCol = r.removeFromLeft(juce::jmin(280, r.getWidth()));
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
  IpadNativeComboBox modeBox;
  IpadNativeComboBox channelBox;
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
                             PurgeLocalDataRequested purgeLocalDataCb,
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
      onPurgeLocalDataRequested(std::move(purgeLocalDataCb)),
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

  diagnosticsCard = std::make_unique<SettingsCard>();
  diagnosticsPage.addAndMakeVisible(*diagnosticsCard);
  diagnosticsSectionLabel.setFont(
      makeAppFont(useTouchOptimisedControls() ? 18.0f : 16.0f,
                  AppFontWeight::SemiBold));
  diagnosticsSectionLabel.setJustificationType(juce::Justification::centredLeft);
  diagnosticsPage.addAndMakeVisible(diagnosticsSectionLabel);
  diagnosticsHint.setFont(makeAppFont(useTouchOptimisedControls() ? 14.0f : 13.0f));
  diagnosticsHint.setJustificationType(juce::Justification::topLeft);
  diagnosticsHint.setMinimumHorizontalScale(1.0f);
  diagnosticsPage.addAndMakeVisible(diagnosticsHint);
  diagnosticsPrivacyHint.setFont(
      makeAppFont(useTouchOptimisedControls() ? 13.0f : 12.0f));
  diagnosticsPrivacyHint.setJustificationType(juce::Justification::topLeft);
  diagnosticsPrivacyHint.setMinimumHorizontalScale(1.0f);
  diagnosticsPage.addAndMakeVisible(diagnosticsPrivacyHint);
  diagnosticsButton.onClick = [this] {
    if (onShareAudioDiagnostics) onShareAudioDiagnostics();
  };
  diagnosticsPage.addAndMakeVisible(diagnosticsButton);
  if (deviceManager != nullptr) {
    audioSetupComp = std::make_unique<juce::AudioDeviceSelectorComponent>(
        *deviceManager, 0, maxInputChannels, 0, maxOutputChannels, true, false, true, false);
    audioSetupComp->setItemHeight(useTouchOptimisedControls() ? 34 : 24);
    audioPage.addAndMakeVisible(audioSetupComp.get());
    deviceManager->addChangeListener(this);
  }

  tabs.addTab(L10n::tr("General", "General"), juce::Colours::transparentBlack,
              &generalPage,
              false);
  tabs.addTab(L10n::tr(u8"Sesión", "Account"),
              juce::Colours::transparentBlack, &accountPage, false);
  tabs.addTab(L10n::tr(u8"Audio", "Audio"), juce::Colours::transparentBlack,
              &audioPage,
              false);
  tabs.addTab(L10n::tr("Ruteo", "Routing"), juce::Colours::transparentBlack,
              &routingPage,
              false);
  tabs.addTab(L10n::tr(u8"Diagnóstico", "Diagnostics"),
              juce::Colours::transparentBlack, &diagnosticsPage, false);
  tabs.setTabBarDepth(useTouchOptimisedControls() ? 196 : 172);
  tabs.getTabbedButtonBar().setVisible(false);
  tabs.setTabBarDepth(0);
  tabs.setOutline(0);
  tabs.onTabChanged = [this](int newIndex, const juce::String &) {
    updateTabButtonStates(newIndex);
    if (newIndex == 3) {
      refreshRoutingForDevice();
    }
  };
  settingsViewport.setViewedComponent(&tabs, false);
  settingsViewport.setScrollBarsShown(true, false, true, false);
  settingsViewport.setScrollBarThickness(8);
  settingsViewport.setScrollOnDragMode(
      juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(settingsViewport);

  auto configureTabButton = [this](juce::TextButton &button,
                                    const int tabIndex) {
    button.setClickingTogglesState(false);
    button.setWantsKeyboardFocus(false);
    button.onClick = [this, tabIndex] {
      tabs.setCurrentTabIndex(tabIndex);
    };
    addAndMakeVisible(button);
  };
  configureTabButton(generalTabButton, 0);
  configureTabButton(accountTabButton, 1);
  configureTabButton(audioTabButton, 2);
  configureTabButton(routingTabButton, 3);
  configureTabButton(diagnosticsTabButton, 4);
  generalTabButton.setPosition(SettingsTabButton::Position::First);
  accountTabButton.setPosition(SettingsTabButton::Position::Middle);
  audioTabButton.setPosition(SettingsTabButton::Position::Middle);
  routingTabButton.setPosition(SettingsTabButton::Position::Middle);
  diagnosticsTabButton.setPosition(SettingsTabButton::Position::Last);

  appearanceCard = std::make_unique<SettingsCard>();
  playbackCard = std::make_unique<SettingsCard>();
  sessionCard = std::make_unique<SettingsCard>();
  actionsCard = std::make_unique<SettingsCard>();
  generalPage.addAndMakeVisible(*appearanceCard);
  generalPage.addAndMakeVisible(*playbackCard);
  accountPage.addAndMakeVisible(*sessionCard);
  accountPage.addAndMakeVisible(*actionsCard);
  sessionIdentitySeparator = std::make_unique<SettingsRowSeparator>();
  actionsTitleSeparator = std::make_unique<SettingsRowSeparator>();
  actionsRowSeparator = std::make_unique<SettingsRowSeparator>();
  accountPage.addAndMakeVisible(*sessionIdentitySeparator);
  accountPage.addAndMakeVisible(*actionsTitleSeparator);
  accountPage.addAndMakeVisible(*actionsRowSeparator);

  generalRowSeparators.reserve(6);
  for (int i = 0; i < 6; ++i) {
    auto separator = std::make_unique<SettingsRowSeparator>();
    generalPage.addAndMakeVisible(*separator);
    generalRowSeparators.push_back(std::move(separator));
  }

  themeLabel.setJustificationType(juce::Justification::centredLeft);
  themeLabel.setFont(makeAppFont(useTouchOptimisedControls() ? 14.0f : 13.0f,
                                 AppFontWeight::Medium));
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
  languageLabel.setFont(makeAppFont(useTouchOptimisedControls() ? 14.0f : 13.0f,
                                    AppFontWeight::Medium));
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
  expandLaunchLabel.setFont(makeAppFont(useTouchOptimisedControls() ? 14.0f : 13.0f,
                                        AppFontWeight::Medium));
  generalPage.addAndMakeVisible(expandLaunchLabel);

  expandLaunchToggle.setClickingTogglesState(true);
  expandLaunchToggle.setToggleState(expandWindowOnLaunch,
                                    juce::dontSendNotification);
  applyToggleSwitchTheme(expandLaunchToggle);
  expandLaunchToggle.onClick = [this] {
    if (onExpandLaunchChanged)
      onExpandLaunchChanged(expandLaunchToggle.getToggleState());
  };
  generalPage.addAndMakeVisible(expandLaunchToggle);

  primeImportLabel.setJustificationType(juce::Justification::centredLeft);
  primeImportLabel.setFont(makeAppFont(useTouchOptimisedControls() ? 14.0f : 13.0f,
                                       AppFontWeight::Medium));
  generalPage.addAndMakeVisible(primeImportLabel);

  primeImportToggle.setClickingTogglesState(true);
  primeImportToggle.setToggleState(primeImportEnabled,
                                   juce::dontSendNotification);
  applyToggleSwitchTheme(primeImportToggle);
  primeImportToggle.onClick = [this] {
    if (onPrimeImportChanged)
      onPrimeImportChanged(primeImportToggle.getToggleState());
  };
  generalPage.addAndMakeVisible(primeImportToggle);

  stemOptimizationLabel.setJustificationType(juce::Justification::centredLeft);
  stemOptimizationLabel.setFont(makeAppFont(useTouchOptimisedControls() ? 14.0f : 13.0f,
                                            AppFontWeight::Medium));
  generalPage.addAndMakeVisible(stemOptimizationLabel);

  stemOptimizationToggle.setClickingTogglesState(true);
  stemOptimizationToggle.setToggleState(stemOptimizationEnabled,
                                        juce::dontSendNotification);
  applyToggleSwitchTheme(stemOptimizationToggle);
  stemOptimizationToggle.onClick = [this] {
    if (onStemOptimizationChanged)
      onStemOptimizationChanged(stemOptimizationToggle.getToggleState());
  };
  generalPage.addAndMakeVisible(stemOptimizationToggle);

  padsCrossfadeLabel.setJustificationType(juce::Justification::centredLeft);
  padsCrossfadeLabel.setFont(makeAppFont(useTouchOptimisedControls() ? 14.0f : 13.0f,
                                         AppFontWeight::Medium));
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
  metronomeClickStyleLabel.setFont(makeAppFont(useTouchOptimisedControls() ? 14.0f : 13.0f,
                                               AppFontWeight::Medium));
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
  sessionSectionLabel.setFont(makeAppFont(15.0f, AppFontWeight::Medium));
  accountPage.addAndMakeVisible(sessionSectionLabel);

  auto metadataFont = makeAppFont(11.0f, AppFontWeight::Regular);
  metadataFont.setExtraKerningFactor(0.036f);
  sessionUidLabel.setJustificationType(juce::Justification::centredLeft);
  sessionUidLabel.setFont(metadataFont);
  accountPage.addAndMakeVisible(sessionUidLabel);
  sessionUidValue.setJustificationType(juce::Justification::centredLeft);
  sessionUidValue.setFont(juce::Font(juce::FontOptions(
      juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain)));
  sessionUidValue.setMinimumHorizontalScale(1.0f);
  accountPage.addAndMakeVisible(sessionUidValue);
  accountPage.addAndMakeVisible(sessionUidCopyButton);

  sessionEmailLabel.setJustificationType(juce::Justification::centredLeft);
  sessionEmailLabel.setFont(metadataFont);
  accountPage.addAndMakeVisible(sessionEmailLabel);
  sessionEmailValue.setJustificationType(juce::Justification::centredLeft);
  sessionEmailValue.setFont(makeAppFont(14.0f));
  accountPage.addAndMakeVisible(sessionEmailValue);

  sessionPlanLabel.setJustificationType(juce::Justification::centredLeft);
  sessionPlanLabel.setFont(metadataFont);
  accountPage.addAndMakeVisible(sessionPlanLabel);
  sessionPlanValue.setJustificationType(juce::Justification::centredLeft);
  sessionPlanValue.setFont(makeAppFont(14.0f));
  accountPage.addAndMakeVisible(sessionPlanValue);

  sessionStatusLabel.setJustificationType(juce::Justification::centredLeft);
  sessionStatusLabel.setFont(metadataFont);
  accountPage.addAndMakeVisible(sessionStatusLabel);
  sessionStatusValue.setJustificationType(juce::Justification::centredLeft);
  sessionStatusValue.setFont(makeAppFont(14.0f));
  accountPage.addAndMakeVisible(sessionStatusValue);

  sessionExpiresLabel.setJustificationType(juce::Justification::centredLeft);
  sessionExpiresLabel.setFont(metadataFont);
  accountPage.addAndMakeVisible(sessionExpiresLabel);
  sessionExpiresValue.setJustificationType(juce::Justification::centredLeft);
  sessionExpiresValue.setFont(makeAppFont(14.0f));
  accountPage.addAndMakeVisible(sessionExpiresValue);

  sessionLicenseLabel.setJustificationType(juce::Justification::centredLeft);
  sessionLicenseLabel.setFont(metadataFont);
  accountPage.addAndMakeVisible(sessionLicenseLabel);
  sessionLicenseValue.setJustificationType(juce::Justification::centredLeft);
  sessionLicenseValue.setFont(juce::Font(juce::FontOptions(
      juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain)));
  sessionLicenseValue.setMinimumHorizontalScale(1.0f);
  accountPage.addAndMakeVisible(sessionLicenseValue);
  accountPage.addAndMakeVisible(sessionLicenseCopyButton);

  sessionVersionLabel.setJustificationType(juce::Justification::centredLeft);
  sessionVersionLabel.setFont(metadataFont);
  accountPage.addAndMakeVisible(sessionVersionLabel);
  sessionVersionValue.setJustificationType(juce::Justification::centredLeft);
  sessionVersionValue.setFont(makeAppFont(14.0f));
  accountPage.addAndMakeVisible(sessionVersionValue);
  sessionQuotaLabel.setJustificationType(juce::Justification::centredLeft);
  sessionQuotaLabel.setFont(metadataFont);
  accountPage.addAndMakeVisible(sessionQuotaLabel);
  sessionQuotaValue.setJustificationType(juce::Justification::centredLeft);
  sessionQuotaValue.setFont(makeAppFont(13.0f));
  accountPage.addAndMakeVisible(sessionQuotaValue);
  for (auto* label : {&sessionDevicesQuotaLabel, &sessionSongsQuotaLabel,
                      &sessionStorageQuotaLabel}) {
    label->setJustificationType(juce::Justification::centredLeft);
    label->setFont(metadataFont);
    accountPage.addAndMakeVisible(*label);
  }
  for (auto* value : {&sessionDevicesQuotaValue, &sessionSongsQuotaValue,
                      &sessionStorageQuotaValue}) {
    value->setJustificationType(juce::Justification::centredRight);
    value->setFont(makeAppFont(14.0f, AppFontWeight::Medium));
    accountPage.addAndMakeVisible(*value);
  }

  actionsSectionLabel.setJustificationType(juce::Justification::centredLeft);
  actionsSectionLabel.setFont(makeAppFont(15.0f, AppFontWeight::Medium));
  accountPage.addAndMakeVisible(actionsSectionLabel);

  actionLogoutLabel.setJustificationType(juce::Justification::centredLeft);
  actionLogoutLabel.setFont(makeAppFont(14.0f, AppFontWeight::Regular));
  accountPage.addAndMakeVisible(actionLogoutLabel);

  actionInfoLabel.setJustificationType(juce::Justification::centredLeft);
  actionInfoLabel.setFont(makeAppFont(14.0f, AppFontWeight::Regular));
  accountPage.addAndMakeVisible(actionInfoLabel);

  accountPage.addAndMakeVisible(infoLinkButton);
  infoLinkButton.setFont(makeAppFont(13.0f, AppFontWeight::Medium), false);

  logoutButton.onClick = [this] {
    if (onLogoutRequested)
      onLogoutRequested();
  };
  accountPage.addAndMakeVisible(logoutButton);

  actionPurgeLabel.setJustificationType(juce::Justification::centredLeft);
  actionPurgeLabel.setFont(makeAppFont(14.0f, AppFontWeight::Regular));
  accountPage.addAndMakeVisible(actionPurgeLabel);
  purgeButton.onClick = [this] {
    if (onPurgeLocalDataRequested)
      onPurgeLocalDataRequested();
  };
  accountPage.addAndMakeVisible(purgeButton);

  routingSectionLabel.setJustificationType(juce::Justification::centredLeft);
  routingSectionLabel.setFont(juce::FontOptions(14.0f));
  routingPage.addAndMakeVisible(routingSectionLabel);
  routingHintLabel.setJustificationType(juce::Justification::centredLeft);
  routingHintLabel.setFont(juce::FontOptions(12.0f));
  routingPage.addAndMakeVisible(routingHintLabel);

  routingPage.addAndMakeVisible(routingContent);

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
  g.setColour(UITheme::panelDark);
  g.fillAll();
}

void SettingsPanel::resized() {
  auto area = getLocalBounds();
  const int headerH = useTouchOptimisedControls() ? 78 : 64;
  const int closeSize = useTouchOptimisedControls() ? 42 : 34;

  auto header = area.removeFromTop(headerH);
  title.setBounds(header.reduced(40, 0));
  auto closeArea = header.removeFromRight(closeSize + 18);
  closeButton.setBounds(closeArea.withSizeKeepingCentre(closeSize, closeSize));

  const bool compactNavigation = getWidth() < 760;
  if (compactNavigation) {
    auto nav = area.removeFromTop(70).reduced(12, 8);
    const int tabGap = 6;
    const int tabWidth = (nav.getWidth() - tabGap * 4) / 5;
    generalTabButton.setBounds(nav.removeFromLeft(tabWidth));
    nav.removeFromLeft(tabGap);
    accountTabButton.setBounds(nav.removeFromLeft(tabWidth));
    nav.removeFromLeft(tabGap);
    audioTabButton.setBounds(nav.removeFromLeft(tabWidth));
    nav.removeFromLeft(tabGap);
    routingTabButton.setBounds(nav.removeFromLeft(tabWidth));
    nav.removeFromLeft(tabGap);
    diagnosticsTabButton.setBounds(nav);
  } else {
    auto rail = area.removeFromLeft(240);
    rail.reduce(16, 16);
    const int tabHeight = 48;
    const int tabGap = 0;
    generalTabButton.setBounds(rail.removeFromTop(tabHeight));
    rail.removeFromTop(tabGap);
    accountTabButton.setBounds(rail.removeFromTop(tabHeight));
    rail.removeFromTop(tabGap);
    audioTabButton.setBounds(rail.removeFromTop(tabHeight));
    rail.removeFromTop(tabGap);
    routingTabButton.setBounds(rail.removeFromTop(tabHeight));
    rail.removeFromTop(tabGap);
    diagnosticsTabButton.setBounds(rail.removeFromTop(tabHeight));
  }
  const bool needsVerticalScroll = useTouchOptimisedControls() || getHeight() < 820;
  settingsViewport.setBounds(area);
  const int contentHeight = needsVerticalScroll
                                ? juce::jmax(area.getHeight(), 900)
                                : area.getHeight();
  tabs.setBounds(0, 0, juce::jmax(1, area.getWidth()), contentHeight);

  layoutGeneralPage();
  layoutAccountPage();
  layoutAudioPage();
  layoutRoutingPage();
  layoutDiagnosticsPage();
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
  closeButton.setButtonText({});
  for (auto *button : {&generalTabButton, &accountTabButton, &audioTabButton,
                       &routingTabButton, &diagnosticsTabButton}) {
    button->setColour(juce::TextButton::textColourOffId,
                      UITheme::textSecondary);
    button->setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
    button->setColour(juce::TextButton::buttonColourId,
                      UITheme::cardBgHover.withAlpha(0.52f));
    button->setColour(juce::TextButton::buttonOnColourId, UITheme::accent);
    button->setClickingTogglesState(false);
  }
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
  const auto sessionPrimary = UITheme::textPrimary;
  const auto sessionSecondary = UITheme::textSecondary;
  const auto actionSecondary = UITheme::textSecondary;
  sessionSectionLabel.setColour(juce::Label::textColourId, sessionPrimary);
  sessionUidLabel.setColour(juce::Label::textColourId, sessionSecondary);
  sessionEmailLabel.setColour(juce::Label::textColourId, sessionSecondary);
  sessionPlanLabel.setColour(juce::Label::textColourId, sessionSecondary);
  sessionStatusLabel.setColour(juce::Label::textColourId, sessionSecondary);
  sessionExpiresLabel.setColour(juce::Label::textColourId, sessionSecondary);
  sessionLicenseLabel.setColour(juce::Label::textColourId, sessionSecondary);
  sessionVersionLabel.setColour(juce::Label::textColourId, sessionSecondary);
  sessionQuotaLabel.setColour(juce::Label::textColourId, sessionSecondary);
  sessionDevicesQuotaLabel.setColour(juce::Label::textColourId, sessionSecondary);
  sessionSongsQuotaLabel.setColour(juce::Label::textColourId, sessionSecondary);
  sessionStorageQuotaLabel.setColour(juce::Label::textColourId, sessionSecondary);
  sessionUidValue.setColour(juce::Label::textColourId, sessionPrimary);
  sessionEmailValue.setColour(juce::Label::textColourId, sessionPrimary);
  sessionPlanValue.setColour(juce::Label::textColourId, sessionPrimary);
  sessionStatusValue.setColour(
      juce::Label::textColourId,
      session.subscriptionStatus.equalsIgnoreCase("active")
          ? UITheme::accentSoft
          : sessionPrimary);
  sessionExpiresValue.setColour(juce::Label::textColourId, sessionPrimary);
  sessionQuotaValue.setColour(juce::Label::textColourId, sessionPrimary);
  sessionDevicesQuotaValue.setColour(juce::Label::textColourId, sessionPrimary);
  sessionSongsQuotaValue.setColour(juce::Label::textColourId, sessionPrimary);
  sessionStorageQuotaValue.setColour(juce::Label::textColourId, sessionPrimary);
  sessionLicenseValue.setColour(juce::Label::textColourId, sessionPrimary);
  sessionVersionValue.setColour(juce::Label::textColourId, sessionPrimary);
  actionsSectionLabel.setColour(juce::Label::textColourId, sessionPrimary);
  actionLogoutLabel.setColour(juce::Label::textColourId, actionSecondary);
  actionPurgeLabel.setColour(juce::Label::textColourId, actionSecondary);
  actionInfoLabel.setColour(juce::Label::textColourId, actionSecondary);
  infoLinkButton.setColour(juce::HyperlinkButton::textColourId,
                           UITheme::accent);
  routingSectionLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  routingHintLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  diagnosticsSectionLabel.setColour(juce::Label::textColourId,
                                    UITheme::textPrimary);
  diagnosticsHint.setColour(juce::Label::textColourId, UITheme::textSecondary);
  diagnosticsPrivacyHint.setColour(juce::Label::textColourId,
                                   UITheme::textSecondary);
  diagnosticsButton.setColour(juce::TextButton::buttonColourId, UITheme::accent);
  diagnosticsButton.setColour(juce::TextButton::textColourOffId,
                              UITheme::onAccent);
  for (auto &row : routingRows)
    row->refreshThemeColours();
  updateTabButtonStates(tabs.getCurrentTabIndex());
  repaint();
}

void SettingsPanel::updateTabButtonStates(const int activeIndex) {
  const auto setState = [activeIndex](juce::TextButton &button,
                                      const int index) {
    button.setColour(juce::TextButton::buttonColourId,
                     index == activeIndex
                         ? UITheme::accent
                         : UITheme::cardBgHover.withAlpha(0.52f));
    button.setColour(juce::TextButton::textColourOffId,
                     index == activeIndex ? UITheme::onAccent
                                          : UITheme::textSecondary);
    button.setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
    if (auto *tabButton = dynamic_cast<SettingsTabButton *>(&button))
      tabButton->setActive(index == activeIndex);
    button.repaint();
  };
  setState(generalTabButton, 0);
  setState(accountTabButton, 1);
  setState(audioTabButton, 2);
  setState(routingTabButton, 3);
  setState(diagnosticsTabButton, 4);
}

void SettingsPanel::applyLanguageTexts() {
  title.setText(L10n::tr(u8"Configuración", "Settings"),
                juce::dontSendNotification);
  closeButton.setButtonText({});
  generalTabButton.setButtonText(L10n::tr("General", "General"));
  accountTabButton.setButtonText(L10n::tr(u8"Sesión", "Account"));
  audioTabButton.setButtonText(L10n::tr(u8"Audio", "Audio"));
  routingTabButton.setButtonText(L10n::tr("Ruteo", "Routing"));
  diagnosticsTabButton.setButtonText(
      L10n::tr(u8"Diagnóstico", "Diagnostics"));
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
  sessionVersionLabel.setText(L10n::tr("Versión", "Version"),
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
  sessionUidCopyButton.setValueToCopy(session.uid);
  sessionLicenseCopyButton.setValueToCopy(session.licenseKey);
  sessionVersionValue.setText(juce::String(JucePlugin_VersionString),
                              juce::dontSendNotification);
  sessionQuotaLabel.setText(L10n::tr("Cuotas de nube", "Cloud quotas"), juce::dontSendNotification);
  const auto formatBytes = [](juce::int64 bytes) {
    return bytes >= 1024LL * 1024LL * 1024LL
      ? juce::String(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 1) + " GB"
      : juce::String(static_cast<double>(bytes) / (1024.0 * 1024.0), 0) + " MB";
  };
  if (session.maxStorageBytes > 0) {
    const auto account = session.isOverride ? L10n::tr("Cuenta de prueba", "Test account")
      : session.accountType.isNotEmpty() ? session.accountType : session.plan;
    sessionQuotaValue.setText(account, juce::dontSendNotification);
    sessionDevicesQuotaLabel.setText(L10n::tr("Dispositivos", "Devices"), juce::dontSendNotification);
    sessionDevicesQuotaValue.setText(juce::String(session.usedDevices) + " / " +
        juce::String(session.maxDevices), juce::dontSendNotification);
    sessionSongsQuotaLabel.setText(L10n::tr("Canciones sincronizadas", "Synced songs"), juce::dontSendNotification);
    sessionSongsQuotaValue.setText(juce::String(session.syncedSongCount) + " / " +
        juce::String(session.maxSyncedSongs), juce::dontSendNotification);
    sessionStorageQuotaLabel.setText(L10n::tr("Almacenamiento", "Storage"), juce::dontSendNotification);
    sessionStorageQuotaValue.setText(formatBytes(session.usedStorageBytes) + " / " +
        formatBytes(session.maxStorageBytes), juce::dontSendNotification);
  } else {
    sessionQuotaValue.setText(
        quotaFetchFailed
            ? L10n::tr("No se pudieron actualizar las cuotas.",
                       "Couldn't refresh quotas.")
            : L10n::tr("Consultando cuotas…", "Loading quotas…"),
        juce::dontSendNotification);
    sessionDevicesQuotaLabel.setText({}, juce::dontSendNotification);
    sessionDevicesQuotaValue.setText({}, juce::dontSendNotification);
    sessionSongsQuotaLabel.setText({}, juce::dontSendNotification);
    sessionSongsQuotaValue.setText({}, juce::dontSendNotification);
    sessionStorageQuotaLabel.setText({}, juce::dontSendNotification);
    sessionStorageQuotaValue.setText({}, juce::dontSendNotification);
  }
  actionsSectionLabel.setText(L10n::tr("Acciones", "Actions"),
                              juce::dontSendNotification);
  actionLogoutLabel.setText(L10n::tr(u8"Cerrar sesión en esta aplicacion",
                                     "Sign out on this app"),
                            juce::dontSendNotification);
  actionPurgeLabel.setText(
      L10n::tr(u8"Purgar descargas, caché y vínculos de Cloud",
               "Purge Cloud downloads, cache, and links"),
      juce::dontSendNotification);
  actionInfoLabel.setText(L10n::tr(u8"¿Necesitas ayuda con tu cuenta?",
                                  "Need help with your account?"),
                          juce::dontSendNotification);
  logoutButton.setButtonText(L10n::tr(u8"Cerrar sesión", "Sign out"));
  purgeButton.setButtonText(L10n::tr("Purgar", "Purge"));
  infoLinkButton.setButtonText(L10n::tr("Contactar soporte", "Contact support"));
  routingSectionLabel.setText(L10n::tr("Ruteo por tipo", "Routing by category"),
                              juce::dontSendNotification);
  routingHintLabel.setText(
      L10n::tr("Define salidas por categoría para el dispositivo actual.",
               "Set per-category outputs for the current device."),
      juce::dontSendNotification);
  diagnosticsSectionLabel.setText(
      L10n::tr(u8"Diagnóstico de reproducción", "Playback diagnostics"),
      juce::dontSendNotification);
  diagnosticsHint.setText(
      L10n::tr(
          u8"Genera un informe con los últimos 2 minutos de métricas técnicas para compartir con soporte.",
          "Generate a report with the last 2 minutes of technical metrics to share with support."),
      juce::dontSendNotification);
  diagnosticsPrivacyHint.setText(
      L10n::tr(
          u8"Incluye un identificador de cuenta y datos del dispositivo. No incluye audio, credenciales, correos, nombres de canciones ni rutas locales.",
          "Includes an account identifier and device data. It does not include audio, credentials, email addresses, song names, or local paths."),
      juce::dontSendNotification);
  diagnosticsButton.setButtonText(
      diagnosticsBusy
          ? L10n::tr(u8"Preparando…", "Preparing…")
          : L10n::tr(u8"Compartir diagnóstico", "Share diagnostics"));
}

void SettingsPanel::layoutGeneralPage() {
  auto area = generalPage.getLocalBounds().reduced(
      useTouchOptimisedControls() ? 28 : 22,
      useTouchOptimisedControls() ? 24 : 18);
  const int rowH = useTouchOptimisedControls() ? 68 : 58;
  // ComboBox popups use the field height as their default item height.
  const int controlH = useTouchOptimisedControls() ? 50 : 38;
  const int rightW = juce::jlimit(220, 360, area.getWidth() / 3);
  const int rowGap = 6;
  const auto rowsBounds = area;

  if (appearanceCard != nullptr)
    appearanceCard->setBounds({});
  if (playbackCard != nullptr)
    playbackCard->setBounds({});
  if (sessionCard != nullptr)
    sessionCard->setBounds({});

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

  int separatorIndex = 0;
  auto addSeparator = [&] {
    const int separatorY = area.getY() + rowGap / 2;
    if (separatorIndex < static_cast<int>(generalRowSeparators.size()))
      generalRowSeparators[static_cast<size_t>(separatorIndex++)]->setBounds(
          rowsBounds.getX(), separatorY, rowsBounds.getWidth(), 1);
    area.removeFromTop(rowGap);
  };

  layoutChoiceRow(area.removeFromTop(rowH), themeLabel, themeBox);
  addSeparator();
  layoutChoiceRow(area.removeFromTop(rowH), languageLabel, languageBox);
  addSeparator();
  layoutToggleRow(area.removeFromTop(rowH), expandLaunchLabel, expandLaunchToggle);
  addSeparator();
  layoutToggleRow(area.removeFromTop(rowH), primeImportLabel, primeImportToggle);
  addSeparator();
  layoutToggleRow(area.removeFromTop(rowH), stemOptimizationLabel,
                  stemOptimizationToggle);
  addSeparator();
  layoutChoiceRow(area.removeFromTop(rowH), padsCrossfadeLabel, padsCrossfadeBox);
  addSeparator();
  layoutChoiceRow(area.removeFromTop(rowH), metronomeClickStyleLabel,
                  metronomeClickStyleBox);
}

void SettingsPanel::layoutAccountPage() {
  auto area = accountPage.getLocalBounds().reduced(
      useTouchOptimisedControls() ? 24 : 20,
      useTouchOptimisedControls() ? 18 : 14);
  constexpr int cardPaddingX = 22;
  constexpr int cardPaddingY = 20;
  constexpr int titleH = 20;
  constexpr int labelH = 14;
  constexpr int valueH = 22;
  constexpr int pairGap = 12;
  constexpr int dividerMargin = 16;
  constexpr int sectionGap = 16;
  constexpr int copyButtonSize = 28;

  sessionSectionLabel.setBounds({});
  sessionUidLabel.setBounds({});
  sessionUidValue.setBounds({});
  sessionUidCopyButton.setBounds({});
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
  sessionLicenseCopyButton.setBounds({});
  sessionVersionLabel.setBounds({});
  sessionVersionValue.setBounds({});
  sessionQuotaLabel.setBounds({});
  sessionQuotaValue.setBounds({});
  sessionDevicesQuotaLabel.setBounds({});
  sessionDevicesQuotaValue.setBounds({});
  sessionSongsQuotaLabel.setBounds({});
  sessionSongsQuotaValue.setBounds({});
  sessionStorageQuotaLabel.setBounds({});
  sessionStorageQuotaValue.setBounds({});
  actionsSectionLabel.setBounds({});
  actionLogoutLabel.setBounds({});
  actionPurgeLabel.setBounds({});
  actionInfoLabel.setBounds({});
  infoLinkButton.setBounds({});
  logoutButton.setBounds({});
  purgeButton.setBounds({});
  if (sessionIdentitySeparator != nullptr)
    sessionIdentitySeparator->setBounds({});
  if (actionsTitleSeparator != nullptr)
    actionsTitleSeparator->setBounds({});
  if (actionsRowSeparator != nullptr)
    actionsRowSeparator->setBounds({});

  const int pairH = labelH + valueH;
  constexpr int quotaRowH = 30;
  const int quotaH = labelH + valueH + quotaRowH * 3;
  const int sessionH = cardPaddingY + titleH + 16 + pairH + pairGap + pairH +
                       dividerMargin + 1 + dividerMargin + pairH + pairGap +
                       pairH + pairGap + pairH + pairGap + pairH + pairGap +
                       quotaH + cardPaddingY;
  auto infoArea = area.removeFromTop(sessionH);
  if (sessionCard != nullptr)
    sessionCard->setBounds(infoArea);

  auto info = infoArea.reduced(cardPaddingX, cardPaddingY);
  sessionSectionLabel.setBounds(info.removeFromTop(titleH));
  info.removeFromTop(16);

  auto layoutInfoPair = [&](juce::Rectangle<int> bounds, juce::Label &label,
                            juce::Label &value) {
    auto row = bounds;
    label.setBounds(row.removeFromTop(labelH));
    value.setBounds(row.removeFromTop(valueH));
  };

  auto uidPair = info.removeFromTop(pairH);
  sessionUidLabel.setBounds(uidPair.removeFromTop(labelH));
  auto uidValueRow = uidPair.removeFromTop(valueH);
  sessionUidCopyButton.setBounds(uidValueRow.removeFromRight(copyButtonSize));
  sessionUidValue.setBounds(uidValueRow);
  info.removeFromTop(pairGap);

  layoutInfoPair(info.removeFromTop(pairH), sessionEmailLabel,
                 sessionEmailValue);

  info.removeFromTop(dividerMargin);
  if (sessionIdentitySeparator != nullptr)
    sessionIdentitySeparator->setBounds(info.removeFromTop(1));
  info.removeFromTop(dividerMargin);

  auto subscriptionRow = info.removeFromTop(pairH);
  auto planArea = subscriptionRow.removeFromLeft(subscriptionRow.getWidth() / 2);
  subscriptionRow.removeFromLeft(12);
  layoutInfoPair(planArea, sessionPlanLabel, sessionPlanValue);
  layoutInfoPair(subscriptionRow, sessionStatusLabel, sessionStatusValue);
  info.removeFromTop(pairGap);

  layoutInfoPair(info.removeFromTop(pairH), sessionExpiresLabel,
                 sessionExpiresValue);
  info.removeFromTop(pairGap);

  auto licensePair = info.removeFromTop(pairH);
  sessionLicenseLabel.setBounds(licensePair.removeFromTop(labelH));
  auto licenseValueRow = licensePair.removeFromTop(valueH);
  sessionLicenseCopyButton.setBounds(
      licenseValueRow.removeFromRight(copyButtonSize));
  sessionLicenseValue.setBounds(licenseValueRow);
  info.removeFromTop(pairGap);

  layoutInfoPair(info.removeFromTop(pairH), sessionVersionLabel,
                 sessionVersionValue);
  info.removeFromTop(pairGap);
  sessionQuotaLabel.setBounds(info.removeFromTop(labelH));
  sessionQuotaValue.setBounds(info.removeFromTop(valueH));
  const auto layoutQuotaRow = [&](juce::Label& label, juce::Label& value) {
    auto row = info.removeFromTop(quotaRowH);
    value.setBounds(row.removeFromRight(180));
    label.setBounds(row);
  };
  layoutQuotaRow(sessionDevicesQuotaLabel, sessionDevicesQuotaValue);
  layoutQuotaRow(sessionSongsQuotaLabel, sessionSongsQuotaValue);
  layoutQuotaRow(sessionStorageQuotaLabel, sessionStorageQuotaValue);

  const auto elideValue = [](juce::Label &label, const juce::String &fullText) {
    const auto availableWidth = static_cast<float>(label.getWidth());
    const auto font = label.getFont();
    const auto measure = [&font](const juce::String &text) {
      juce::GlyphArrangement glyphs;
      glyphs.addLineOfText(font, text, 0.0f, 0.0f);
      return glyphs.getBoundingBox(0, -1, true).getWidth();
    };
    if (availableWidth <= 0 || measure(fullText) <= availableWidth) {
      label.setText(fullText, juce::dontSendNotification);
      return;
    }
    auto shortened = fullText;
    while (shortened.isNotEmpty() && measure(shortened + "...") > availableWidth)
      shortened = shortened.dropLastCharacters(1);
    label.setText(shortened + "...", juce::dontSendNotification);
  };
  elideValue(sessionUidValue, session.uid.isNotEmpty() ? session.uid : "-");
  elideValue(sessionLicenseValue,
             session.licenseKey.isNotEmpty() ? session.licenseKey : "-");

  area.removeFromTop(sectionGap);

  if (actionsCard != nullptr) {
    constexpr int actionsH = 194;
    actionsCard->setBounds(area.removeFromTop(actionsH));
    auto actions = actionsCard->getBounds().reduced(22, 4);
    actions.removeFromTop(16);
    actionsSectionLabel.setBounds(actions.removeFromTop(20));
    actions.removeFromTop(8);
    if (actionsTitleSeparator != nullptr)
      actionsTitleSeparator->setBounds(actions.removeFromTop(1));

    auto logoutRow = actions.removeFromTop(48);
    auto logoutControl = logoutRow.removeFromRight(112).reduced(0, 8);
    actionLogoutLabel.setBounds(logoutRow);
    logoutButton.setBounds(logoutControl);
    if (actionsRowSeparator != nullptr)
      actionsRowSeparator->setBounds(actions.removeFromTop(1));

    auto purgeRow = actions.removeFromTop(48);
    auto purgeControl = purgeRow.removeFromRight(112).reduced(0, 8);
    actionPurgeLabel.setBounds(purgeRow);
    purgeButton.setBounds(purgeControl);

    auto helpRow = actions.removeFromTop(48);
    auto helpControl = helpRow.removeFromRight(112);
    actionInfoLabel.setBounds(helpRow);
    infoLinkButton.setBounds(helpControl);
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
  auto area = audioPage.getLocalBounds().reduced(18, 16);
  if (audioSetupComp != nullptr) {
    audioSetupComp->setBounds(area);
  }
}

void SettingsPanel::layoutDiagnosticsPage() {
  auto area = diagnosticsPage.getLocalBounds().reduced(
      useTouchOptimisedControls() ? 28 : 22,
      useTouchOptimisedControls() ? 24 : 18);
  const int cardHeight = useTouchOptimisedControls() ? 252 : 218;
  const int cardWidth = juce::jmin(area.getWidth(), 720);
  const auto cardBounds = area.removeFromTop(cardHeight).withWidth(cardWidth);
  if (diagnosticsCard != nullptr)
    diagnosticsCard->setBounds(cardBounds);

  auto content = cardBounds.reduced(useTouchOptimisedControls() ? 24 : 22,
                                    useTouchOptimisedControls() ? 20 : 18);
  diagnosticsSectionLabel.setBounds(content.removeFromTop(28));
  content.removeFromTop(10);
  diagnosticsHint.setBounds(content.removeFromTop(
      useTouchOptimisedControls() ? 52 : 42));
  content.removeFromTop(6);
  diagnosticsPrivacyHint.setBounds(content.removeFromTop(
      useTouchOptimisedControls() ? 64 : 52));
  content.removeFromTop(12);
  const int buttonWidth = juce::jmin(useTouchOptimisedControls() ? 280 : 240,
                                    content.getWidth());
  diagnosticsButton.setBounds(
      content.removeFromTop(useTouchOptimisedControls() ? 46 : 42)
          .withWidth(buttonWidth));
}

void SettingsPanel::layoutRoutingPage() {
  auto area = routingPage.getLocalBounds().reduced(18, 16);

  auto labelArea = area.removeFromTop(22);
  routingSectionLabel.setBounds(labelArea);
  
  area.removeFromTop(8);
  auto hintArea = area.removeFromBottom(24);
  routingHintLabel.setBounds(hintArea);
  area.removeFromBottom(6);
  
  const auto listArea = area;
  const int rowH = useTouchOptimisedControls() ? 50 : 42;
  int y = 0;
  const int contentW = juce::jmax(1, listArea.getWidth());
  for (auto &rowComp : routingRows) {
    rowComp->setBounds(0, y, contentW, rowH);
    y += rowH;
  }
  routingContent.setBounds(listArea.withHeight(juce::jmax(y + 2, listArea.getHeight())));
}

SettingsPanel::~SettingsPanel() {
  if (deviceManager != nullptr)
    deviceManager->removeChangeListener(this);
}

void SettingsPanel::setDiagnosticsBusy(const bool busy) {
  diagnosticsBusy = busy;
  diagnosticsButton.setEnabled(!busy);
  diagnosticsButton.setButtonText(
      diagnosticsBusy
          ? L10n::tr(u8"Preparando…", "Preparing…")
          : L10n::tr(u8"Compartir diagnóstico", "Share diagnostics"));
}

void SettingsPanel::updateSessionSummary(SessionSummary sessionSummary) {
  quotaFetchFailed = false;
  session = std::move(sessionSummary);
  applyLanguageTexts();
  resized();
}

void SettingsPanel::setQuotaUnavailable() {
  quotaFetchFailed = true;
  applyLanguageTexts();
  resized();
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
