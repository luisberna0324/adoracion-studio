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
    modeBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBgHover);
    modeBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
    modeBox.setColour(juce::ComboBox::outlineColourId,
                      UITheme::cardBorder.withAlpha(0.65f));
    modeBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
    channelBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBgHover);
    channelBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
    channelBox.setColour(juce::ComboBox::outlineColourId,
                         UITheme::cardBorder.withAlpha(0.65f));
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
                             std::vector<MidiTargetOption> midiTargets,
                             MidiBindingsProvider midiBindingsProvider,
                             MidiLearnRequested midiLearnCb,
                             MidiClearRequested midiClearCb,
                             MidiBindingUpdated midiBindingUpdatedCb,
                             MidiInputDevicesProvider midiInputDevicesProvider,
                             MidiInputToggleRequested midiInputToggleCb,
                             MidiInputProbeTextProvider midiProbeTextProvider,
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
      midiTargetOptions(std::move(midiTargets)),
      getMidiBindings(std::move(midiBindingsProvider)),
      onMidiLearnRequested(std::move(midiLearnCb)),
      onMidiClearRequested(std::move(midiClearCb)),
      onMidiBindingUpdated(std::move(midiBindingUpdatedCb)),
      getMidiInputDevices(std::move(midiInputDevicesProvider)),
      onMidiInputToggleRequested(std::move(midiInputToggleCb)),
      getMidiProbeText(std::move(midiProbeTextProvider)),
      routingDefaults(std::move(defaults)),
      availableOutputChannels(juce::jmax(1, outputChannels)),
      deviceManager(deviceManagerToUse) {
  juce::ignoreUnused(closeCb, stemOptimizationEnabled);
  title.setJustificationType(juce::Justification::centredLeft);
  title.setFont(juce::FontOptions(16.0f).withKerningFactor(0.04f));
  addAndMakeVisible(title);

  if (deviceManager != nullptr) {
    audioSetupComp = std::make_unique<juce::AudioDeviceSelectorComponent>(
        *deviceManager, 0, maxInputChannels, 0, maxOutputChannels, true, false, true, false);
    audioSetupComp->setItemHeight(24);
    routingPage.addAndMakeVisible(audioSetupComp.get());
    deviceManager->addChangeListener(this);
  }

  tabs.addTab(L10n::tr("General", "General"), UITheme::cardBg, &generalPage,
              false);
  tabs.addTab(L10n::tr("Ruteo", "Routing"), UITheme::cardBg, &routingPage,
              false);
  tabs.addTab("MIDI", UITheme::cardBg, &midiPage, false);
  tabs.setTabBarDepth(30);
  tabs.onTabChanged = [this](int newIndex, const juce::String &) {
    if (newIndex == 1) {
      refreshRoutingForDevice();
    }
  };
  addAndMakeVisible(tabs);

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
  stemOptimizationToggle.setToggleState(false,
                                        juce::dontSendNotification);
  stemOptimizationToggle.onClick = [this] {
    if (onStemOptimizationChanged)
      onStemOptimizationChanged(stemOptimizationToggle.getToggleState());
  };
  stemOptimizationToggle.setEnabled(false);
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
  routingMidiHintLabel.setJustificationType(juce::Justification::centredLeft);
  routingMidiHintLabel.setFont(juce::FontOptions(12.0f));
  routingPage.addAndMakeVisible(routingMidiHintLabel);

  routingViewport.setViewedComponent(&routingContent, false);
  routingViewport.setScrollBarsShown(true, false, false, false);
  routingViewport.setScrollBarThickness(2);
  routingPage.addAndMakeVisible(routingViewport);

  midiConnectionsLabel.setJustificationType(juce::Justification::centredLeft);
  midiConnectionsLabel.setFont(juce::FontOptions(12.0f));
  midiPage.addAndMakeVisible(midiConnectionsLabel);
  midiInputDeviceBox.onChange = [this] { refreshMidiInputToggleForSelection(); };
  midiPage.addAndMakeVisible(midiInputDeviceBox);
  midiInputEnabledToggle.setClickingTogglesState(true);
  midiInputEnabledToggle.onClick = [this] {
    const int selected = midiInputDeviceBox.getSelectedId();
    if (!juce::isPositiveAndBelow(selected - 1,
                                  static_cast<int>(midiInputDevices.size())))
      return;
    const auto &device = midiInputDevices[static_cast<size_t>(selected - 1)];
    if (onMidiInputToggleRequested != nullptr)
      onMidiInputToggleRequested(device.identifier,
                                 midiInputEnabledToggle.getToggleState());
    refreshMidiInputDevices();
    refreshMidiInputStatus();
  };
  midiPage.addAndMakeVisible(midiInputEnabledToggle);

  midiTargetLabel.setJustificationType(juce::Justification::centredLeft);
  midiTargetLabel.setFont(juce::FontOptions(12.0f));
  midiPage.addAndMakeVisible(midiTargetLabel);
  midiInputStateLabel.setJustificationType(juce::Justification::centredLeft);
  midiInputStateLabel.setFont(juce::FontOptions(12.0f));
  midiPage.addAndMakeVisible(midiInputStateLabel);
  midiProbeStateLabel.setJustificationType(juce::Justification::centredLeft);
  midiProbeStateLabel.setFont(juce::FontOptions(12.0f));
  midiPage.addAndMakeVisible(midiProbeStateLabel);
  for (size_t i = 0; i < midiTargetOptions.size(); ++i)
    midiTargetBox.addItem(midiTargetOptions[i].name, static_cast<int>(i) + 1);
  midiTargetBox.onChange = [this] { refreshMidiBindings(); };
  midiPage.addAndMakeVisible(midiTargetBox);

  midiBindingLabel.setJustificationType(juce::Justification::centredLeft);
  midiBindingLabel.setFont(juce::FontOptions(12.0f));
  midiPage.addAndMakeVisible(midiBindingLabel);
  midiStatusLabel.setJustificationType(juce::Justification::centredLeft);
  midiStatusLabel.setFont(juce::FontOptions(12.0f));
  midiPage.addAndMakeVisible(midiStatusLabel);
  midiModeLabel.setJustificationType(juce::Justification::centredLeft);
  midiModeLabel.setFont(juce::FontOptions(12.0f));
  midiPage.addAndMakeVisible(midiModeLabel);
  midiModeBox.addItem("Absolute", 1);
  midiModeBox.addItem("Toggle", 2);
  midiModeBox.addItem("Momentary", 3);
  midiModeBox.onChange = [this] {
    const int selected = midiTargetBox.getSelectedId();
    if (!juce::isPositiveAndBelow(selected - 1,
                                  static_cast<int>(midiTargetOptions.size())))
      return;
    const int targetId = midiTargetOptions[static_cast<size_t>(selected - 1)].id;
    const auto bindings =
        getMidiBindings ? getMidiBindings() : std::vector<MidiBindingView>{};
    const auto it = std::find_if(
        bindings.begin(), bindings.end(),
        [targetId](const MidiBindingView &binding) {
          return binding.targetId == targetId;
        });
    if (it == bindings.end())
      return;

    MidiBindingView updated = *it;
    const int modeId = midiModeBox.getSelectedId();
    if (modeId == 2)
      updated.mode = MidiBindingMode::Toggle;
    else if (modeId == 3)
      updated.mode = MidiBindingMode::Momentary;
    else
      updated.mode = MidiBindingMode::Absolute;

    if (onMidiBindingUpdated != nullptr)
      onMidiBindingUpdated(updated);
    refreshMidiBindings();
  };
  midiPage.addAndMakeVisible(midiModeBox);
  midiWarningLabel.setJustificationType(juce::Justification::centredLeft);
  midiWarningLabel.setFont(juce::FontOptions(12.0f));
  midiPage.addAndMakeVisible(midiWarningLabel);
  midiLearnButton.onClick = [this] {
    const int selected = midiTargetBox.getSelectedId();
    if (!juce::isPositiveAndBelow(selected - 1, static_cast<int>(midiTargetOptions.size())))
      return;
    if (!midiLearnButton.isEnabled()) {
      midiStatusLabel.setText(
          L10n::tr("Habilita una entrada MIDI para aprender",
                   "Enable a MIDI input to learn"),
          juce::dontSendNotification);
      return;
    }
    const int targetId = midiTargetOptions[static_cast<size_t>(selected - 1)].id;
    if (onMidiLearnRequested)
      onMidiLearnRequested(targetId);
    midiStatusLabel.setText(L10n::tr(u8"Esperando mensaje MIDI CC/Note...",
                                     u8"Waiting for MIDI CC/Note message..."),
                            juce::dontSendNotification);
  };
  midiPage.addAndMakeVisible(midiLearnButton);
  midiClearButton.onClick = [this] {
    const int selected = midiTargetBox.getSelectedId();
    if (!juce::isPositiveAndBelow(selected - 1, static_cast<int>(midiTargetOptions.size())))
      return;
    const int targetId = midiTargetOptions[static_cast<size_t>(selected - 1)].id;
    if (onMidiClearRequested)
      onMidiClearRequested(targetId);
    refreshMidiBindings();
  };
  midiPage.addAndMakeVisible(midiClearButton);

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
  refreshMidiInputDevices();
  if (midiTargetBox.getNumItems() > 0)
    midiTargetBox.setSelectedId(1, juce::dontSendNotification);
  refreshMidiInputStatus();
  refreshMidiBindings();
  tabs.setCurrentTabIndex(0);
  applyLanguageTexts();
  refreshThemeColours();
  startTimerHz(4);
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
  layoutMidiPage();
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
  routingMidiHintLabel.setColour(juce::Label::textColourId,
                                 UITheme::textSecondary);
  midiConnectionsLabel.setColour(juce::Label::textColourId,
                                 UITheme::textSecondary);
  midiTargetLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  midiInputStateLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  midiProbeStateLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  midiBindingLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  midiModeLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  midiWarningLabel.setColour(juce::Label::textColourId, UITheme::accent);
  midiStatusLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  midiInputDeviceBox.setColour(juce::ComboBox::backgroundColourId,
                               UITheme::cardBgHover);
  midiInputDeviceBox.setColour(juce::ComboBox::outlineColourId,
                               UITheme::cardBorder.withAlpha(0.65f));
  midiInputDeviceBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
  midiInputDeviceBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
  midiTargetBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBgHover);
  midiTargetBox.setColour(juce::ComboBox::outlineColourId,
                          UITheme::cardBorder.withAlpha(0.65f));
  midiTargetBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
  midiTargetBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
  midiModeBox.setColour(juce::ComboBox::backgroundColourId, UITheme::cardBgHover);
  midiModeBox.setColour(juce::ComboBox::outlineColourId,
                        UITheme::cardBorder.withAlpha(0.65f));
  midiModeBox.setColour(juce::ComboBox::textColourId, UITheme::textPrimary);
  midiModeBox.setColour(juce::ComboBox::arrowColourId, UITheme::textPrimary);
  midiInputEnabledToggle.setColour(juce::ToggleButton::textColourId,
                                   UITheme::textPrimary);
  midiInputEnabledToggle.setColour(juce::ToggleButton::tickColourId,
                                   UITheme::accent);
  midiInputEnabledToggle.setColour(juce::ToggleButton::tickDisabledColourId,
                                   UITheme::textSecondary);
  midiLearnButton.setColour(juce::TextButton::buttonColourId, UITheme::accent);
  midiLearnButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  midiClearButton.setColour(juce::TextButton::buttonColourId,
                            juce::Colour::fromRGB(102, 60, 60));
  midiClearButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  for (auto &row : routingRows)
    row->refreshThemeColours();
  repaint();
}

void SettingsPanel::applyLanguageTexts() {
  title.setText(L10n::tr(u8"Configuración", "Settings"),
                juce::dontSendNotification);
  tabs.setTabName(0, L10n::tr("General", "General"));
  tabs.setTabName(1, L10n::tr("Ruteo", "Routing"));
  tabs.setTabName(2, "MIDI");
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
      L10n::tr("Optimizacion de stems (deshabilitada)",
               "Stem optimization (disabled)"),
      juce::dontSendNotification);
  stemOptimizationToggle.setButtonText(
      L10n::tr("Temporalmente deshabilitado",
               "Temporarily disabled"));
  padsCrossfadeLabel.setText(
      L10n::tr("Crossfade de pads", "Pads crossfade"),
      juce::dontSendNotification);
  padsCrossfadeBox.changeItemText(1, L10n::tr(u8"Rápido (1s)", "Fast (1s)"));
  padsCrossfadeBox.changeItemText(2, L10n::tr("Medio (3s)", "Medium (3s)"));
  padsCrossfadeBox.changeItemText(3, L10n::tr("Largo (5s)", "Long (5s)"));
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
  routingMidiHintLabel.setText(
      L10n::tr("Estado MIDI abajo. Configura entradas en pestaña MIDI.",
               "MIDI status below. Configure inputs in MIDI tab."),
      juce::dontSendNotification);
  midiConnectionsLabel.setText(
      L10n::tr("Conexión MIDI", "MIDI connection"),
      juce::dontSendNotification);
  midiTargetLabel.setText(L10n::tr("Control objetivo", "Target control"),
                          juce::dontSendNotification);
  midiBindingLabel.setText(L10n::tr("Binding actual", "Current binding"),
                           juce::dontSendNotification);
  midiModeLabel.setText(L10n::tr("Modo", "Mode"),
                        juce::dontSendNotification);
  midiInputEnabledToggle.setButtonText(
      L10n::tr("Entrada habilitada", "Input enabled"));
  midiLearnButton.setButtonText(L10n::tr("Aprender", "Learn"));
  midiClearButton.setButtonText(L10n::tr("Quitar", "Clear"));
  refreshMidiInputStatus();
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

  if (audioSetupComp != nullptr) {
    auto audioArea = area.removeFromTop(270);
    audioSetupComp->setBounds(audioArea.reduced(20, 0));
    area.removeFromTop(16);
  }

  auto labelArea = area.removeFromTop(22);
  routingSectionLabel.setBounds(labelArea.reduced(20, 0)); // Alineado con la lista
  
  area.removeFromTop(8);
  auto hintArea = area.removeFromBottom(24);
  routingMidiHintLabel.setBounds(hintArea.reduced(20, 0));
  area.removeFromBottom(6);
  
  auto listArea = area.reduced(20, 0); // Mismo margen que el audioSetupComp
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

void SettingsPanel::layoutMidiPage() {
  auto area = midiPage.getLocalBounds().reduced(8);
  auto connectionLabelRow = area.removeFromTop(24);
  midiConnectionsLabel.setBounds(connectionLabelRow);
  auto connectionRow = area.removeFromTop(28);
  midiInputDeviceBox.setBounds(connectionRow.removeFromLeft(
      juce::jmax(200, connectionRow.getWidth() - 190)));
  connectionRow.removeFromLeft(10);
  midiInputEnabledToggle.setBounds(connectionRow.removeFromLeft(180));
  area.removeFromTop(6);
  auto inputRow = area.removeFromTop(24);
  midiInputStateLabel.setBounds(inputRow);
  auto probeRow = area.removeFromTop(24);
  midiProbeStateLabel.setBounds(probeRow);
  auto warningRow = area.removeFromTop(24);
  midiWarningLabel.setBounds(warningRow);
  area.removeFromTop(8);
  auto targetRow = area.removeFromTop(28);
  midiTargetLabel.setBounds(targetRow.removeFromLeft(160));
  midiTargetBox.setBounds(targetRow);
  area.removeFromTop(14);
  auto bindingRow = area.removeFromTop(24);
  midiBindingLabel.setBounds(bindingRow.removeFromLeft(160));
  midiStatusLabel.setBounds(bindingRow);
  area.removeFromTop(8);
  auto modeRow = area.removeFromTop(28);
  midiModeLabel.setBounds(modeRow.removeFromLeft(160));
  midiModeBox.setBounds(modeRow.removeFromLeft(200));
  area.removeFromTop(16);
  auto actions = area.removeFromTop(30);
  midiLearnButton.setBounds(actions.removeFromLeft(130));
  actions.removeFromLeft(10);
  midiClearButton.setBounds(actions.removeFromLeft(130));
}

void SettingsPanel::refreshMidiBindings() {
  const int selected = midiTargetBox.getSelectedId();
  if (!juce::isPositiveAndBelow(selected - 1, static_cast<int>(midiTargetOptions.size()))) {
    midiStatusLabel.setText("-", juce::dontSendNotification);
    midiModeBox.setEnabled(false);
    return;
  }
  const int targetId = midiTargetOptions[static_cast<size_t>(selected - 1)].id;
  const auto bindings = getMidiBindings ? getMidiBindings() : std::vector<MidiBindingView>{};
  const auto it = std::find_if(bindings.begin(), bindings.end(),
                               [targetId](const MidiBindingView &binding) {
                                 return binding.targetId == targetId;
                               });
  if (it == bindings.end()) {
    midiStatusLabel.setText(L10n::tr("Sin asignar", "Unassigned"),
                            juce::dontSendNotification);
    midiModeBox.setEnabled(false);
    return;
  }
  const juce::String sourceText =
      it->sourceType == MidiBindingSourceType::Note ? "Note" : "CC";
  juce::String modeText = "absolute";
  int modeId = 1;
  if (it->mode == MidiBindingMode::Toggle) {
    modeText = "toggle";
    modeId = 2;
  } else if (it->mode == MidiBindingMode::Momentary) {
    modeText = "momentary";
    modeId = 3;
  }
  midiModeBox.setEnabled(true);
  midiModeBox.setSelectedId(modeId, juce::dontSendNotification);
  midiStatusLabel.setText(
      "Ch " + juce::String(it->midiChannel) + " - " + sourceText + " " +
          juce::String(it->ccNumber) + " (" + modeText + ")",
      juce::dontSendNotification);
}

void SettingsPanel::refreshMidiInputDevices() {
  juce::String selectedIdentifier;
  const int selected = midiInputDeviceBox.getSelectedId();
  if (juce::isPositiveAndBelow(selected - 1,
                               static_cast<int>(midiInputDevices.size()))) {
    selectedIdentifier =
        midiInputDevices[static_cast<size_t>(selected - 1)].identifier;
  }

  midiInputDevices =
      getMidiInputDevices ? getMidiInputDevices() : std::vector<MidiInputDeviceView>{};
  midiInputDeviceBox.clear(juce::dontSendNotification);
  for (size_t i = 0; i < midiInputDevices.size(); ++i) {
    midiInputDeviceBox.addItem(midiInputDevices[i].name,
                               static_cast<int>(i) + 1);
  }

  int selectedId = 0;
  if (selectedIdentifier.isNotEmpty()) {
    for (size_t i = 0; i < midiInputDevices.size(); ++i) {
      if (midiInputDevices[i].identifier == selectedIdentifier) {
        selectedId = static_cast<int>(i) + 1;
        break;
      }
    }
  }
  if (selectedId <= 0 && midiInputDeviceBox.getNumItems() > 0)
    selectedId = 1;
  if (selectedId > 0) {
    midiInputDeviceBox.setSelectedId(selectedId, juce::dontSendNotification);
  }
  refreshMidiInputToggleForSelection();
}

void SettingsPanel::refreshMidiInputToggleForSelection() {
  const int selected = midiInputDeviceBox.getSelectedId();
  const bool validSelection =
      juce::isPositiveAndBelow(selected - 1,
                               static_cast<int>(midiInputDevices.size()));
  midiInputEnabledToggle.setEnabled(validSelection);
  if (!validSelection) {
    midiInputEnabledToggle.setButtonText(
        L10n::tr("Habilitar entrada", "Enable input"));
    midiInputEnabledToggle.setToggleState(false, juce::dontSendNotification);
    return;
  }
  const auto &device = midiInputDevices[static_cast<size_t>(selected - 1)];
  midiInputEnabledToggle.setButtonText(
      device.enabled ? L10n::tr("Entrada habilitada", "Input enabled")
                     : L10n::tr("Habilitar entrada", "Enable input"));
  midiInputEnabledToggle.setToggleState(device.enabled,
                                        juce::dontSendNotification);
}

SettingsPanel::~SettingsPanel() {
  stopTimer();
  if (deviceManager != nullptr)
    deviceManager->removeChangeListener(this);
}

void SettingsPanel::changeListenerCallback(juce::ChangeBroadcaster* source) {
  if (source == deviceManager) {
    refreshRoutingForDevice();
    refreshMidiInputDevices();
    refreshMidiInputStatus();
  }
}

void SettingsPanel::timerCallback() {
  refreshMidiInputStatus();
}

void SettingsPanel::refreshMidiInputStatus() {
  if (getMidiInputDevices == nullptr) {
    midiInputStateLabel.setText("-", juce::dontSendNotification);
    midiWarningLabel.setText({}, juce::dontSendNotification);
    midiLearnButton.setEnabled(false);
    routingMidiHintLabel.setText(
        L10n::tr("MIDI no disponible", "MIDI unavailable"),
        juce::dontSendNotification);
    return;
  }

  const auto inputs = getMidiInputDevices();
  if (inputs.size() != midiInputDevices.size())
    refreshMidiInputDevices();
  int enabledCount = 0;
  for (const auto &dev : inputs) {
    if (dev.enabled)
      ++enabledCount;
  }

  if (inputs.empty()) {
    midiInputStateLabel.setText(
        L10n::tr("MIDI IN: sin dispositivos detectados",
                 "MIDI IN: no devices detected"),
        juce::dontSendNotification);
    midiProbeStateLabel.setText(
        L10n::tr("Ultimo MIDI: sin señal", "Last MIDI: no signal"),
        juce::dontSendNotification);
    midiWarningLabel.setText(
        L10n::tr("Conecta un dispositivo MIDI para usar Learn",
                 "Connect a MIDI device to use Learn"),
        juce::dontSendNotification);
    midiLearnButton.setEnabled(false);
    routingMidiHintLabel.setText(
        L10n::tr("MIDI IN activos: 0 / 0", "Active MIDI IN: 0 / 0"),
        juce::dontSendNotification);
    return;
  }

  midiInputStateLabel.setText(
      L10n::tr("MIDI IN activos: ", "Active MIDI IN: ") + juce::String(enabledCount) +
          " / " + juce::String(inputs.size()),
      juce::dontSendNotification);

  const auto probeText = getMidiProbeText ? getMidiProbeText() : juce::String{};
  midiProbeStateLabel.setText(
      probeText.isNotEmpty()
          ? (L10n::tr("Ultimo MIDI: ", "Last MIDI: ") + probeText)
          : L10n::tr("Ultimo MIDI: sin señal", "Last MIDI: no signal"),
      juce::dontSendNotification);
  routingMidiHintLabel.setText(
      L10n::tr("MIDI IN activos: ", "Active MIDI IN: ") +
          juce::String(enabledCount) + " / " + juce::String(inputs.size()),
      juce::dontSendNotification);

  if (enabledCount <= 0) {
    midiWarningLabel.setText(
        L10n::tr("No hay entradas habilitadas. Activa al menos una.",
                 "No inputs enabled. Turn on at least one."),
        juce::dontSendNotification);
    midiLearnButton.setEnabled(false);
  } else {
    midiWarningLabel.setText({}, juce::dontSendNotification);
    midiLearnButton.setEnabled(true);
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
