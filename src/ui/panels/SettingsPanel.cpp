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
                             CloseRequested closeCb,
                             std::vector<CategoryRoutingDefaults> defaults,
                             int outputChannels,
                             RoutingDefaultsChanged routingCb)
    : onThemeChanged(std::move(cb)),
      onExpandLaunchChanged(std::move(expandCb)), onClose(std::move(closeCb)),
      onRoutingDefaultsChanged(std::move(routingCb)),
      routingDefaults(std::move(defaults)),
      availableOutputChannels(juce::jmax(1, outputChannels)) {
  title.setText("Settings", juce::dontSendNotification);
  title.setJustificationType(juce::Justification::centredLeft);
  title.setFont(juce::FontOptions(16.0f).withKerningFactor(0.04f));
  addAndMakeVisible(title);

  generalTab.setButtonText("General");
  generalTab.setClickingTogglesState(true);
  generalTab.onClick = [this] { setActiveSection(0); };
  addAndMakeVisible(generalTab);

  routingTab.setButtonText("Ruteo");
  routingTab.setClickingTogglesState(true);
  routingTab.onClick = [this] { setActiveSection(1); };
  addAndMakeVisible(routingTab);

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

  expandLaunchLabel.setText("Ventana al iniciar", juce::dontSendNotification);
  expandLaunchLabel.setJustificationType(juce::Justification::centredLeft);
  expandLaunchLabel.setFont(juce::FontOptions(12.0f));
  addAndMakeVisible(expandLaunchLabel);

  expandLaunchToggle.setButtonText("Expandir al área de trabajo");
  expandLaunchToggle.setClickingTogglesState(true);
  expandLaunchToggle.setToggleState(expandWindowOnLaunch,
                                    juce::dontSendNotification);
  expandLaunchToggle.onClick = [this] {
    if (onExpandLaunchChanged)
      onExpandLaunchChanged(expandLaunchToggle.getToggleState());
  };
  addAndMakeVisible(expandLaunchToggle);

  routingSectionLabel.setJustificationType(juce::Justification::centredLeft);
  routingSectionLabel.setFont(juce::FontOptions(14.0f));
  addAndMakeVisible(routingSectionLabel);

  routingViewport.setViewedComponent(&routingContent, false);
  routingViewport.setScrollBarsShown(true, false, false, false);
  routingViewport.setScrollBarThickness(2);
  addAndMakeVisible(routingViewport);

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
          if (onRoutingDefaultsChanged)
            onRoutingDefaultsChanged(routingDefaults);
        });
    routingContent.addAndMakeVisible(*row);
    routingRows.push_back(std::move(row));
  }

  setActiveSection(0);
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
  const int headerH = 28;
  auto header = area.removeFromTop(headerH);
  title.setBounds(header);
  area.removeFromTop(10);

  const int navW = 140;
  auto nav = area.removeFromLeft(navW);
  auto body = area;

  const int navBtnH = 30;
  generalTab.setBounds(nav.removeFromTop(navBtnH));
  nav.removeFromTop(6);
  routingTab.setBounds(nav.removeFromTop(navBtnH));

  const int titleH = 22;
  auto bodyHeader = body.removeFromTop(titleH);
  routingSectionLabel.setBounds(bodyHeader);
  body.removeFromTop(10);

  if (activeSection == 0) {
    auto row = body.removeFromTop(26);
    themeLabel.setBounds(row.removeFromLeft(60));
    themeBox.setBounds(row);
    body.removeFromTop(14);
    auto expandRow = body.removeFromTop(26);
    expandLaunchLabel.setBounds(expandRow.removeFromLeft(130));
    expandLaunchToggle.setBounds(expandRow);
  } else {
    routingViewport.setBounds(body);
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

  const int closeH = 28;
  closeButton.setBounds(getLocalBounds().reduced(16).removeFromBottom(closeH).removeFromRight(90));
}

void SettingsPanel::refreshThemeColours() {
  title.setColour(juce::Label::textColourId, UITheme::textPrimary);
  generalTab.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  generalTab.setColour(juce::TextButton::buttonOnColourId,
                       UITheme::cardBgPressed);
  generalTab.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  generalTab.setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);
  routingTab.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  routingTab.setColour(juce::TextButton::buttonOnColourId,
                       UITheme::cardBgPressed);
  routingTab.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  routingTab.setColour(juce::TextButton::textColourOnId, UITheme::textPrimary);
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
  routingSectionLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  closeButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  closeButton.setColour(juce::TextButton::buttonOnColourId,
                        UITheme::cardBgPressed);
  closeButton.setColour(juce::TextButton::textColourOffId,
                        UITheme::textPrimary);
  closeButton.setColour(juce::TextButton::textColourOnId,
                        UITheme::textPrimary);
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

void SettingsPanel::setActiveSection(const int section) {
  activeSection = section;
  const bool showRouting = activeSection == 1;
  generalTab.setToggleState(!showRouting, juce::dontSendNotification);
  routingTab.setToggleState(showRouting, juce::dontSendNotification);

  themeLabel.setVisible(!showRouting);
  themeBox.setVisible(!showRouting);
  expandLaunchLabel.setVisible(!showRouting);
  expandLaunchToggle.setVisible(!showRouting);

  routingViewport.setVisible(showRouting);
  if (showRouting)
    routingSectionLabel.setText("Ruteo por tipo", juce::dontSendNotification);
  else
    routingSectionLabel.setText("General", juce::dontSendNotification);

  resized();
  repaint();
}
