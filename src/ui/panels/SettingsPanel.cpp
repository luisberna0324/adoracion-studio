#include "SettingsPanel.h"

#include "../../audio/OutputChannelCatalog.h"
#include "../../data/SongData.h"

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

SettingsPanel::SettingsPanel(ThemePreset current, const bool expandWindowOnLaunch,
                             ThemeChanged cb, ExpandLaunchChanged expandCb,
                             const bool primeImportEnabled,
                             PrimeImportChanged primeImportCb,
                             CloseRequested closeCb,
                             std::vector<CategoryRoutingDefaults> defaults,
                             int outputChannels,
                             RoutingDefaultsChanged routingCb,
                             juce::AudioDeviceManager *deviceManagerToUse,
                             int maxInputChannels,
                             int maxOutputChannels)
    : onThemeChanged(std::move(cb)),
      onExpandLaunchChanged(std::move(expandCb)),
      onPrimeImportChanged(std::move(primeImportCb)),
      onClose(std::move(closeCb)),
      onRoutingDefaultsChanged(std::move(routingCb)),
      routingDefaults(std::move(defaults)),
      availableOutputChannels(juce::jmax(1, outputChannels)),
      deviceManager(deviceManagerToUse) {
  title.setText("Settings", juce::dontSendNotification);
  title.setJustificationType(juce::Justification::centredLeft);
  title.setFont(juce::FontOptions(16.0f).withKerningFactor(0.04f));
  addAndMakeVisible(title);

  tabs.addTab("General", UITheme::cardBg, &generalPage, false);
  tabs.addTab("Ruteo", UITheme::cardBg, &routingPage, false);
  tabs.addTab("Audio", UITheme::cardBg, &audioPage, false);
  tabs.onTabChanged = [this](int newIndex, const juce::String &) {
    if (newIndex == 1) {
      refreshDeviceOptions();
      refreshRoutingForDevice();
    }
  };
  addAndMakeVisible(tabs);

  deviceLabel.setText("Interfaz", juce::dontSendNotification);
  deviceLabel.setJustificationType(juce::Justification::centredLeft);
  deviceLabel.setFont(juce::FontOptions(11.0f));
  routingPage.addAndMakeVisible(deviceLabel);

  deviceBox.onChange = [this] {
    const int id = deviceBox.getSelectedId();
    if (id > 0)
      applyDeviceSelection(id - 1);
  };
  routingPage.addAndMakeVisible(deviceBox);

  themeLabel.setText("Skin", juce::dontSendNotification);
  themeLabel.setJustificationType(juce::Justification::centredLeft);
  themeLabel.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(themeLabel);

  themeBox.addItem("Default", static_cast<int>(ThemePreset::Default));
  themeBox.addItem("Spotify", static_cast<int>(ThemePreset::Spotify));
  themeBox.addItem("Ableton", static_cast<int>(ThemePreset::Ableton));
  themeBox.setSelectedId(static_cast<int>(current));
  themeBox.setJustificationType(juce::Justification::centredLeft);
  generalPage.addAndMakeVisible(themeBox);

  expandLaunchLabel.setText("Ventana al iniciar", juce::dontSendNotification);
  expandLaunchLabel.setJustificationType(juce::Justification::centredLeft);
  expandLaunchLabel.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(expandLaunchLabel);

  expandLaunchToggle.setButtonText("Expandir al área de trabajo");
  expandLaunchToggle.setClickingTogglesState(true);
  expandLaunchToggle.setToggleState(expandWindowOnLaunch,
                                    juce::dontSendNotification);
  expandLaunchToggle.onClick = [this] {
    if (onExpandLaunchChanged)
      onExpandLaunchChanged(expandLaunchToggle.getToggleState());
  };
  generalPage.addAndMakeVisible(expandLaunchToggle);

  primeImportLabel.setText("Importar desde PRIME", juce::dontSendNotification);
  primeImportLabel.setJustificationType(juce::Justification::centredLeft);
  primeImportLabel.setFont(juce::FontOptions(12.0f));
  generalPage.addAndMakeVisible(primeImportLabel);

  primeImportToggle.setButtonText("Habilitar importacion desde PRIME");
  primeImportToggle.setClickingTogglesState(true);
  primeImportToggle.setToggleState(primeImportEnabled,
                                   juce::dontSendNotification);
  primeImportToggle.onClick = [this] {
    if (onPrimeImportChanged)
      onPrimeImportChanged(primeImportToggle.getToggleState());
  };
  generalPage.addAndMakeVisible(primeImportToggle);

  routingSectionLabel.setJustificationType(juce::Justification::centredLeft);
  routingSectionLabel.setFont(juce::FontOptions(14.0f));
  routingSectionLabel.setText("Ruteo por tipo", juce::dontSendNotification);
  routingPage.addAndMakeVisible(routingSectionLabel);

  routingViewport.setViewedComponent(&routingContent, false);
  routingViewport.setScrollBarsShown(true, false, false, false);
  routingViewport.setScrollBarThickness(2);
  routingPage.addAndMakeVisible(routingViewport);

  if (deviceManager != nullptr) {
    const int maxIns = juce::jmax(0, maxInputChannels);
    const int maxOuts = juce::jmax(0, maxOutputChannels);
    audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        *deviceManager, 0, maxIns, 0, maxOuts, true, true, true, false);
    audioPage.addAndMakeVisible(*audioSelector);
  } else {
    audioUnavailableLabel.setText("Audio disponible solo en Standalone",
                                  juce::dontSendNotification);
    audioUnavailableLabel.setJustificationType(
        juce::Justification::centredLeft);
    audioUnavailableLabel.setFont(juce::FontOptions(12.0f));
    audioPage.addAndMakeVisible(audioUnavailableLabel);
  }

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
  auto footer = area.removeFromBottom(28);
  closeButton.setBounds(footer.removeFromRight(90));
  tabs.setBounds(area);

  layoutGeneralPage();
  layoutRoutingPage();
  layoutAudioPage();
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
  routingSectionLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  closeButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  closeButton.setColour(juce::TextButton::buttonOnColourId,
                        UITheme::cardBgPressed);
  closeButton.setColour(juce::TextButton::textColourOffId,
                        UITheme::textPrimary);
  closeButton.setColour(juce::TextButton::textColourOnId,
                        UITheme::textPrimary);
  audioUnavailableLabel.setColour(juce::Label::textColourId,
                                  UITheme::textSecondary);
  for (auto &row : routingRows)
    row->refreshThemeColours();
  repaint();
}

void SettingsPanel::closeSelf() {
  if (onClose)
    onClose();
  if (auto *dw = findParentComponentOfClass<juce::DialogWindow>()) {
    dw->exitModalState(0);
    dw->setVisible(false);
  }
}

void SettingsPanel::layoutGeneralPage() {
  auto area = generalPage.getLocalBounds().reduced(4);
  auto row = area.removeFromTop(26);
  themeLabel.setBounds(row.removeFromLeft(60));
  themeBox.setBounds(row);
  area.removeFromTop(14);
  auto expandRow = area.removeFromTop(26);
  expandLaunchLabel.setBounds(expandRow.removeFromLeft(130));
  expandLaunchToggle.setBounds(expandRow);
  area.removeFromTop(10);
  auto primeRow = area.removeFromTop(26);
  primeImportLabel.setBounds(primeRow.removeFromLeft(150));
  primeImportToggle.setBounds(primeRow);
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

void SettingsPanel::layoutAudioPage() {
  auto area = audioPage.getLocalBounds().reduced(4);
  if (audioSelector != nullptr) {
    audioSelector->setBounds(area);
  } else {
    audioUnavailableLabel.setBounds(area.removeFromTop(24));
  }
}

void SettingsPanel::refreshDeviceOptions() {
  deviceOptions.clear();
  deviceBox.clear(juce::dontSendNotification);

  if (deviceManager == nullptr) {
    deviceBox.addItem("Audio no disponible", 1);
    deviceBox.setSelectedId(1, juce::dontSendNotification);
    return;
  }

  auto *currentDevice = deviceManager->getCurrentAudioDevice();
  const juce::String currentType = deviceManager->getCurrentAudioDeviceType();
  const juce::String currentName =
      currentDevice != nullptr ? currentDevice->getName() : juce::String{};

  auto &deviceTypes = deviceManager->getAvailableDeviceTypes();
  for (auto *ioType : deviceTypes) {
    if (ioType == nullptr)
      continue;
    ioType->scanForDevices();
    const juce::String typeName = ioType->getTypeName();
    const auto outputs = ioType->getDeviceNames(false);
    for (int i = 0; i < outputs.size(); ++i) {
      const auto name = outputs[i];
      if (name.isNotEmpty())
        deviceOptions.push_back({typeName, name});
    }
  }

  int selectedId = 0;
  for (size_t i = 0; i < deviceOptions.size(); ++i) {
    const auto &opt = deviceOptions[i];
    const bool includeType = deviceOptions.size() > 1 &&
                             opt.typeName.isNotEmpty();
    const juce::String label =
        includeType ? (opt.typeName + " - " + opt.deviceName) : opt.deviceName;
    const int itemId = static_cast<int>(i) + 1;
    deviceBox.addItem(label, itemId);
    if (opt.typeName == currentType && opt.deviceName == currentName)
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
  deviceManager->setCurrentAudioDeviceType(opt.typeName, true);
  juce::AudioDeviceManager::AudioDeviceSetup setup;
  deviceManager->getAudioDeviceSetup(setup);
  setup.outputDeviceName = opt.deviceName;
  deviceManager->setAudioDeviceSetup(setup, true);

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
