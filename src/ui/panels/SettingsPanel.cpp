#include "SettingsPanel.h"
#include "../../platform/NativeAppleSignIn.h"

#include "../../audio/OutputChannelCatalog.h"
#include "../../data/Localization.h"
#include "../../data/PrimeLibrarySource.h"
#include "../../data/SongData.h"

#include <algorithm>
#include <map>

namespace {
// Label, 48 pt control, local explanation, then space before the next option.
constexpr int mobileChoiceHeight = 128;
constexpr int mobileToggleHeight = 100;
constexpr int mobileRemoteChoiceHeight = 128;
constexpr int mobileCardPadding = 32;

juce::Font settingsOptionLabelFont() {
  return makeAppFont(useTouchOptimisedControls() ? 15.0f : 14.0f,
                     AppFontWeight::Medium);
}

juce::Font settingsHelpFont() {
  return makeAppFont(useTouchOptimisedControls() ? 13.5f : 12.5f);
}
}

class SettingsPanel::SettingsHomePage final : public juce::Component {
public:
  explicit SettingsHomePage(SettingsPanel &ownerToUse) : owner(ownerToUse) {
    refreshEntries();
  }

  void refreshEntries() {
    rows.clear();
    remoteRow = nullptr;
    std::vector<Destination> pages{Destination::Appearance, Destination::Playback,
                                   Destination::Library};
    if (!useTouchOptimisedControls())
      pages.push_back(Destination::Audio);
    pages.insert(pages.end(), {Destination::Routing, Destination::Remote});
    if (owner.teamsManagementEnabled) pages.push_back(Destination::Teams);
    pages.insert(pages.end(), {Destination::Account, Destination::Diagnostics, Destination::Learning});
    for (auto page : pages) {
      auto row = std::make_unique<Row>(page);
      row->onClick = [this, page] { owner.showSettingsPage(page); };
      if (page == Destination::Remote) remoteRow = row.get();
      addAndMakeVisible(*row);
      rows.push_back(std::move(row));
    }
    refreshTexts();
  }

  int contentHeight() const {
    return 16 + 3 * (34 + 22) + static_cast<int>(rows.size()) * 72 + 8;
  }

  juce::Component *remoteEntry() const {
    return remoteRow;
  }

  void refreshTexts() {
    const juce::StringArray titles{
        L10n::tr("Apariencia e idioma", "Appearance & language"),
        L10n::tr(u8"Reproducción", "Playback"),
        L10n::tr("Biblioteca", "Library")};
    juce::StringArray pageTitles = titles;
    if (!useTouchOptimisedControls())
      pageTitles.add(L10n::tr("Dispositivo de audio", "Audio device"));
    pageTitles.addArray({L10n::tr("Ruteo de salidas", "Output routing"),
        L10n::tr("Control remoto", "Remote control")});
    if (owner.teamsManagementEnabled) pageTitles.add("Teams");
    pageTitles.addArray({L10n::tr("Mi cuenta", "My account"),
        L10n::tr(u8"Diagnóstico", "Diagnostics"),
        L10n::tr("Aprender PlayWorship", "Learn PlayWorship")});
    using Mode = playworship::remote::RemoteSession::Mode;
    const auto mode = owner.remoteMode == Mode::Client ? L10n::tr("Cliente", "Client")
        : owner.remoteMode == Mode::Server ? L10n::tr("Servidor", "Server") : juce::String("Local");
    const juce::StringArray subtitles{
        L10n::tr("Tema e idioma de la app", "App theme and language"),
        L10n::tr("Click y transiciones de pads", "Click and pad transitions"),
        L10n::tr(u8"Importación y almacenamiento", "Import and storage")};
    juce::StringArray pageSubtitles = subtitles;
    if (!useTouchOptimisedControls())
      pageSubtitles.add(L10n::tr("Interfaz, entradas y salidas", "Interface, inputs and outputs"));
    pageSubtitles.addArray({L10n::tr(u8"Dispositivo y salidas por categoría", "Device and outputs by category"),
        mode + juce::String::fromUTF8(" · ") + L10n::tr("Conexiones en red local", "Local network connections")});
    if (owner.teamsManagementEnabled)
      pageSubtitles.add(L10n::tr("Usuarios de tu nube", "Your Cloud users"));
    pageSubtitles.addArray({owner.session.email.isNotEmpty() ? owner.session.email : L10n::tr(u8"Sesión, plan y almacenamiento", "Session, plan and storage"),
        L10n::tr("Informe para soporte", "Report for support"),
        L10n::tr("Guías paso a paso; continúa donde quedaste", "Step-by-step guides; pick up where you left off")});
    for (size_t i = 0; i < rows.size(); ++i) {
      rows[i]->setButtonText(pageTitles[static_cast<int>(i)]);
      rows[i]->subtitle = pageSubtitles[static_cast<int>(i)];
      rows[i]->setTitle(pageTitles[static_cast<int>(i)]);
      rows[i]->setDescription(pageSubtitles[static_cast<int>(i)]);
    }
    repaint();
  }

  void resized() override {
    auto area = getLocalBounds().reduced(16, 0);
    area = area.withSizeKeepingCentre(juce::jmin(640, area.getWidth()), area.getHeight());
    area.removeFromTop(16);
    size_t index = 0;
    for (size_t group = 0; group < cards.size(); ++group) {
      headings[group] = area.removeFromTop(34).reduced(12, 0);
      const int count = group == 2 ? (owner.teamsManagementEnabled ? 4 : 3) :
          group == 1 && useTouchOptimisedControls() ? 2 : 3;
      cards[group] = area.removeFromTop(count * 72);
      auto card = cards[group];
      for (int row = 0; row < count; ++row)
        rows[index++]->setBounds(card.removeFromTop(72));
      area.removeFromTop(22);
    }
  }

  void paint(juce::Graphics &g) override {
    const juce::StringArray names{
        L10n::tr(u8"Personalización", "Personalization"),
        L10n::tr("Audio y conexiones", "Audio & connections"),
        L10n::tr("Cuenta y soporte", "Account & support")};
    for (size_t group = 0; group < cards.size(); ++group) {
      g.setColour(UITheme::textSecondary);
      g.setFont(makeAppFont(14.0f, AppFontWeight::Medium));
      g.drawText(names[static_cast<int>(group)], headings[group], juce::Justification::centredLeft);
      g.setColour(UITheme::cardBg);
      g.fillRoundedRectangle(cards[group].toFloat(), 18.0f);
      g.setColour(UITheme::textPrimary.withAlpha(0.08f));
      for (int y = cards[group].getY() + 72; y < cards[group].getBottom(); y += 72)
        g.fillRect(cards[group].getX() + 54, y, cards[group].getWidth() - 70, 1);
    }
  }

private:
  class Row final : public juce::TextButton {
  public:
    explicit Row(Destination pageToUse) : page(pageToUse) { setWantsKeyboardFocus(true); }
    juce::String subtitle;

    void mouseUp(const juce::MouseEvent &event) override {
      if (event.mouseWasDraggedSinceMouseDown()) {
        setState(juce::Button::buttonNormal);
        return;
      }
      juce::TextButton::mouseUp(event);
    }

    void paintButton(juce::Graphics &g, bool hovered, bool down) override {
      if (hovered || down || hasKeyboardFocus(false)) {
        g.setColour(UITheme::textPrimary.withAlpha(down ? 0.10f : 0.04f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 14.0f);
      }
      auto area = getLocalBounds().reduced(16, 0);
      const auto icon = area.removeFromLeft(24).withSizeKeepingCentre(24, 24).toFloat();
      area.removeFromLeft(14);
      auto chevron = area.removeFromRight(12).toFloat();
      area.removeFromRight(10);
      g.setColour(UITheme::textSecondary.withAlpha(0.65f));
      juce::Path path;
      path.startNewSubPath(chevron.getX() + 3, chevron.getCentreY() - 5);
      path.lineTo(chevron.getX() + 8, chevron.getCentreY());
      path.lineTo(chevron.getX() + 3, chevron.getCentreY() + 5);
      g.strokePath(path, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
      g.setColour(UITheme::textPrimary);
      g.setFont(makeAppFont(16.0f, AppFontWeight::Medium));
      g.drawText(getButtonText(), area.withY(15).withHeight(23), juce::Justification::centredLeft);
      g.setColour(UITheme::textSecondary);
      g.setFont(makeAppFont(12.5f));
      g.drawText(subtitle, area.withY(39).withHeight(18), juce::Justification::centredLeft);
      paintIcon(g, icon);
    }

  private:
    Destination page;
    void paintIcon(juce::Graphics &g, juce::Rectangle<float> icon) {
      const auto colour = UITheme::accentSoft;
      if (page == Destination::Learning) {
        drawSvgIcon(g, icon, SvgIconName::Music, colour);
        return;
      }
      if (page == Destination::Appearance || page == Destination::Playback ||
          page == Destination::Audio || page == Destination::Diagnostics) {
        const auto glyph = page == Destination::Appearance ? IconGlyph::Settings
            : page == Destination::Playback ? IconGlyph::Metronome
            : page == Destination::Audio ? IconGlyph::Waveform : IconGlyph::Clock;
        drawIconGlyph(g, icon, glyph, colour, 1.7f);
        return;
      }
      juce::Graphics::ScopedSaveState saved(g);
      g.addTransform(juce::AffineTransform::scale(icon.getWidth() / 24.0f)
                         .translated(icon.getX(), icon.getY()));
      g.setColour(colour);
      juce::Path p;
      if (page == Destination::Library) {
        p.startNewSubPath(3, 5); p.lineTo(10, 5); p.lineTo(12, 8);
        p.lineTo(21, 8); p.lineTo(21, 20); p.lineTo(3, 20); p.closeSubPath();
        p.startNewSubPath(3, 10); p.lineTo(21, 10);
      } else if (page == Destination::Routing) {
        p.addRoundedRectangle(2, 9, 6, 6, 1.5f);
        p.addRoundedRectangle(16, 2, 6, 6, 1.5f);
        p.addRoundedRectangle(16, 16, 6, 6, 1.5f);
        p.startNewSubPath(8, 12); p.lineTo(12, 12); p.lineTo(12, 5); p.lineTo(16, 5);
        p.startNewSubPath(12, 12); p.lineTo(12, 19); p.lineTo(16, 19);
      } else if (page == Destination::Remote) {
        p.startNewSubPath(2, 8); p.quadraticTo(12, -1, 22, 8);
        p.startNewSubPath(5, 12); p.quadraticTo(12, 5, 19, 12);
        p.startNewSubPath(8, 16); p.quadraticTo(12, 12, 16, 16);
        g.fillEllipse(10.5f, 19.0f, 3.0f, 3.0f);
      } else {
        p.addEllipse(8, 2, 8, 8);
        p.startNewSubPath(3, 22); p.lineTo(3, 19);
        p.cubicTo(3, 11, 21, 11, 21, 19); p.lineTo(21, 22);
      }
      g.strokePath(p, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
  };
  SettingsPanel &owner;
  juce::Component *remoteRow = nullptr;
  std::vector<std::unique_ptr<Row>> rows;
  std::array<juce::Rectangle<int>, 3> headings, cards;
};

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
  auto bounds = getLocalBounds().toFloat().withSizeKeepingCentre(18.0f, 18.0f);
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

    // These selectors live inside a scrolling form on every platform. Open
    // only on a completed click/tap, and let the wheel scroll instead of
    // silently changing an audio output under the pointer.
    modeBox.setMobileFormMode(true);
    channelBox.setMobileFormMode(true);

    refreshChannelOptions();
    refreshThemeColours();
  }

  static int preferredHeightForWidth(const int width) {
    return width < 520 ? 86 : (useTouchOptimisedControls() ? 56 : 44);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(0, 4);
    if (getWidth() < 520) {
      categoryLabel.setBounds(r.removeFromTop(22));
      r.removeFromTop(8);
      modeBox.setBounds(r.removeFromLeft((r.getWidth() - 12) / 2));
      r.removeFromLeft(12);
      channelBox.setBounds(r);
      return;
    }

    categoryLabel.setBounds(r.removeFromLeft(juce::jlimit(140, 220, getWidth() / 3)));
    r.removeFromLeft(12);
    modeBox.setBounds(r.removeFromLeft(110));
    r.removeFromLeft(12);
    channelBox.setBounds(r.removeFromLeft(juce::jmin(280, r.getWidth())));
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
  AdaptiveComboBox modeBox;
  AdaptiveComboBox channelBox;
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
  closeButton.setSubtleHighlight(true);
  closeButton.onClick = [this] {
    if (onCloseRequested)
      onCloseRequested();
  };
  addAndMakeVisible(closeButton);

  learningLabel.setFont(settingsOptionLabelFont());
  learningHelp.setFont(settingsHelpFont());
  learningHelp.setJustificationType(juce::Justification::topLeft);
  learningHelp.setInterceptsMouseClicks(false, false);
  generalPage.addAndMakeVisible(learningLabel);
  generalPage.addAndMakeVisible(learningHelp);
  generalPage.addAndMakeVisible(learningButton);
  learningButton.onClick = [this] { if (onLearningRequested) onLearningRequested(); };
  teamsLabel.setFont(settingsOptionLabelFont());
  teamsHelp.setFont(settingsHelpFont());
  teamsHelp.setJustificationType(juce::Justification::topLeft);
  teamsHelp.setInterceptsMouseClicks(false, false);
  generalPage.addAndMakeVisible(teamsLabel);
  generalPage.addAndMakeVisible(teamsHelp);
  generalPage.addAndMakeVisible(teamsButton);
  teamsButton.onClick = [this] { showSettingsPage(Destination::Teams); };
  teamsButton.setEnabled(false);
  teamsLabel.setVisible(false);
  teamsHelp.setVisible(false);
  teamsButton.setVisible(false);

  diagnosticsCard = std::make_unique<SettingsCard>();
  diagnosticsPage.addAndMakeVisible(*diagnosticsCard);
  diagnosticsSectionLabel.setFont(
      makeAppFont(useTouchOptimisedControls() ? 18.0f : 16.0f,
                  AppFontWeight::SemiBold));
  diagnosticsSectionLabel.setJustificationType(juce::Justification::centredLeft);
  diagnosticsPage.addAndMakeVisible(diagnosticsSectionLabel);
  diagnosticsHint.setFont(settingsHelpFont());
  diagnosticsHint.setJustificationType(juce::Justification::topLeft);
  diagnosticsHint.setMinimumHorizontalScale(1.0f);
  diagnosticsPage.addAndMakeVisible(diagnosticsHint);
  diagnosticsPrivacyHint.setFont(settingsHelpFont());
  diagnosticsPrivacyHint.setJustificationType(juce::Justification::topLeft);
  diagnosticsPrivacyHint.setMinimumHorizontalScale(1.0f);
  diagnosticsPage.addAndMakeVisible(diagnosticsPrivacyHint);
  diagnosticsButton.onClick = [this] {
    if (onShareAudioDiagnostics) onShareAudioDiagnostics();
  };
  diagnosticsPage.addAndMakeVisible(diagnosticsButton);
  audioHelp.setFont(settingsHelpFont());
  audioHelp.setJustificationType(juce::Justification::topLeft);
  audioHelp.setMinimumHorizontalScale(1.0f);
  audioHelp.setInterceptsMouseClicks(false, false);
  audioPage.addAndMakeVisible(audioHelp);
  if (deviceManager != nullptr) {
    if (!useTouchOptimisedControls()) {
      audioSetupComp = std::make_unique<juce::AudioDeviceSelectorComponent>(
          *deviceManager, 0, maxInputChannels, 0, maxOutputChannels, true, false, true, false);
      audioSetupComp->setItemHeight(24);
      audioPage.addAndMakeVisible(audioSetupComp.get());
    }
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
    resized();
    settingsViewport.setViewPosition(0, 0);
  };
  settingsViewport.setViewedComponent(&tabs, false);
  settingsViewport.setScrollBarsShown(true, false, true, false);
  settingsViewport.setScrollBarThickness(8);
  settingsViewport.setScrollOnDragMode(
      juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(settingsViewport);
  homePage = std::make_unique<SettingsHomePage>(*this);
  backButton.setSvgLabel(SvgIconName::ArrowLeft, {});
  backButton.onClick = [this] { showSettingsPage(Destination::Home); };
  addChildComponent(backButton);
  setWantsKeyboardFocus(true);

  auto configureTabButton = [this](juce::TextButton &button,
                                    const int tabIndex) {
    button.setClickingTogglesState(false);
    button.setWantsKeyboardFocus(false);
    button.onClick = [this, tabIndex] {
      if (usesGroupedNavigation()) {
        const Destination pages[]{Destination::Appearance, Destination::Account,
                                  Destination::Audio, Destination::Routing,
                                  Destination::Diagnostics};
        showSettingsPage(pages[tabIndex]);
      } else {
        tabs.setCurrentTabIndex(tabIndex);
      }
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

  generalRowSeparators.reserve(7);
  for (int i = 0; i < 7; ++i) {
    auto separator = std::make_unique<SettingsRowSeparator>();
    generalPage.addAndMakeVisible(*separator);
    generalRowSeparators.push_back(std::move(separator));
  }

  themeLabel.setJustificationType(juce::Justification::centredLeft);
  themeLabel.setFont(settingsOptionLabelFont());
  generalPage.addAndMakeVisible(themeLabel);

  themeBox.addItem(L10n::tr("Verde Noche", "Night Green"),
                   static_cast<int>(ThemePreset::Spotify));
  themeBox.addItem(L10n::tr(u8"Ámbar Estudio", "Studio Amber"),
                   static_cast<int>(ThemePreset::Ableton));
  const int selectedThemeId =
      current == ThemePreset::Ableton
          ? static_cast<int>(ThemePreset::Ableton)
          : static_cast<int>(ThemePreset::Spotify);
  themeBox.setSelectedId(selectedThemeId, juce::dontSendNotification);
  generalPage.addAndMakeVisible(themeBox);

  languageLabel.setJustificationType(juce::Justification::centredLeft);
  languageLabel.setFont(settingsOptionLabelFont());
  generalPage.addAndMakeVisible(languageLabel);

  languageBox.addItem(juce::String::fromUTF8(u8"Español"),
                      static_cast<int>(AppLanguage::Spanish) + 1);
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
  expandLaunchLabel.setFont(settingsOptionLabelFont());
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
  primeImportLabel.setFont(settingsOptionLabelFont());
  generalPage.addAndMakeVisible(primeImportLabel);

  primeImportToggle.setClickingTogglesState(true);
  primeImportToggle.setToggleState(primeImportEnabled,
                                   juce::dontSendNotification);
  primeImportToggle.setEnabled(PrimeLibrarySource::isSupportedOnCurrentPlatform());
  applyToggleSwitchTheme(primeImportToggle);
  primeImportToggle.onClick = [this] {
    if (onPrimeImportChanged)
      onPrimeImportChanged(primeImportToggle.getToggleState());
  };
  generalPage.addAndMakeVisible(primeImportToggle);

  stemOptimizationLabel.setJustificationType(juce::Justification::centredLeft);
  stemOptimizationLabel.setFont(settingsOptionLabelFont());
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
  padsCrossfadeLabel.setFont(settingsOptionLabelFont());
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
  metronomeClickStyleLabel.setFont(settingsOptionLabelFont());
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
  remoteControlLabel.setJustificationType(juce::Justification::centredLeft);
  remoteControlLabel.setFont(settingsOptionLabelFont());
  generalPage.addAndMakeVisible(remoteControlLabel);
  using Mode = playworship::remote::RemoteSession::Mode;
  remoteLocalButton.setConnectedEdges(juce::Button::ConnectedOnRight);
  remoteClientButton.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
  remoteServerButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
  remoteLocalButton.onClick = [this] { if (onRemoteModeChanged) onRemoteModeChanged(Mode::Local); };
  remoteClientButton.onClick = [this] { if (onRemoteModeChanged) onRemoteModeChanged(Mode::Client); };
  remoteServerButton.onClick = [this] { if (onRemoteModeChanged) onRemoteModeChanged(Mode::Server); };
  for (auto *button : { &remoteLocalButton, &remoteClientButton, &remoteServerButton })
    remoteModeControl.addAndMakeVisible(button);
  generalPage.addAndMakeVisible(remoteModeControl);
  generalPage.addAndMakeVisible(remoteServersBox);
  remoteServerAlias = AppSettings::getRemoteServerAlias();
  remoteServerNameEditor.setText(remoteServerAlias, juce::dontSendNotification);
  remoteServerNameEditor.setInputRestrictions(28);
  remoteServerNameEditor.setSelectAllWhenFocused(true);
  const auto saveRemoteServerAlias = [this] {
    const auto alias = remoteServerNameEditor.getText().trim()
                           .removeCharacters("\r\n\t").replaceCharacter('.', '-');
    if (alias == remoteServerAlias)
      return;
    remoteServerAlias = alias;
    remoteServerNameEditor.setText(alias, juce::dontSendNotification);
    if (onRemoteServerAliasChanged)
      onRemoteServerAliasChanged(alias);
  };
  remoteServerNameEditor.onReturnKey = [this, saveRemoteServerAlias] {
    saveRemoteServerAlias();
    remoteServerNameEditor.giveAwayKeyboardFocus();
  };
  remoteServerNameEditor.onFocusLost = saveRemoteServerAlias;
  generalPage.addAndMakeVisible(remoteServerNameEditor);
  generalPage.addAndMakeVisible(remoteStatus);
  for (auto *help : {&themeHelp, &languageHelp, &expandLaunchHelp,
                     &primeImportHelp, &stemOptimizationHelp,
                     &padsCrossfadeHelp, &metronomeClickStyleHelp,
                     &remoteControlHelp, &remoteTargetHelp}) {
    help->setFont(settingsHelpFont());
    help->setJustificationType(juce::Justification::topLeft);
    help->setMinimumHorizontalScale(1.0f);
    help->setInterceptsMouseClicks(false, false);
    generalPage.addAndMakeVisible(*help);
  }
  remoteServersBox.onChange = [this] {
    const int index = remoteServersBox.getSelectedId() - 1;
    if (juce::isPositiveAndBelow(index, static_cast<int>(remoteServers.size())) && onRemoteConnect)
      onRemoteConnect(remoteServers[static_cast<size_t>(index)]);
  };

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
  actionLogoutLabel.setFont(settingsOptionLabelFont());
  accountPage.addAndMakeVisible(actionLogoutLabel);

  actionInfoLabel.setJustificationType(juce::Justification::centredLeft);
  actionInfoLabel.setFont(settingsOptionLabelFont());
  accountPage.addAndMakeVisible(actionInfoLabel);

  accountPage.addAndMakeVisible(infoLinkButton);
  infoLinkButton.setFont(makeAppFont(13.0f, AppFontWeight::Medium), false);

  logoutButton.onClick = [this] {
    if (onLogoutRequested)
      onLogoutRequested();
  };
  accountPage.addAndMakeVisible(logoutButton);

  actionPurgeLabel.setJustificationType(juce::Justification::centredLeft);
  actionPurgeLabel.setFont(settingsOptionLabelFont());
  accountPage.addAndMakeVisible(actionPurgeLabel);
  for (auto *help : {&actionLogoutHelp, &actionPurgeHelp, &actionInfoHelp}) {
    help->setFont(settingsHelpFont());
    help->setJustificationType(juce::Justification::topLeft);
    help->setMinimumHorizontalScale(1.0f);
    help->setInterceptsMouseClicks(false, false);
    accountPage.addAndMakeVisible(*help);
  }
  purgeButton.onClick = [this] {
    if (onPurgeLocalDataRequested)
      onPurgeLocalDataRequested();
  };
  accountPage.addAndMakeVisible(purgeButton);

  appleLinkLabel.setFont(settingsOptionLabelFont());
  appleLinkLabel.setJustificationType(juce::Justification::centredLeft);
  appleLinkHelp.setFont(settingsHelpFont());
  appleLinkHelp.setJustificationType(juce::Justification::topLeft);
  appleLinkHelp.setMinimumHorizontalScale(1.0f);
  accountPage.addChildComponent(appleLinkLabel);
  accountPage.addChildComponent(appleLinkHelp);

  routingSectionLabel.setJustificationType(juce::Justification::centredLeft);
  routingSectionLabel.setFont(juce::FontOptions(14.0f));
  routingPage.addAndMakeVisible(routingSectionLabel);
  routingDeviceLabel.setFont(settingsOptionLabelFont());
  routingDeviceLabel.setJustificationType(juce::Justification::centredLeft);
  routingPage.addAndMakeVisible(routingDeviceLabel);
  routingDeviceHelp.setFont(settingsHelpFont());
  routingDeviceHelp.setJustificationType(juce::Justification::topLeft);
  routingDeviceHelp.setMinimumHorizontalScale(1.0f);
  routingDeviceHelp.setInterceptsMouseClicks(false, false);
  routingPage.addAndMakeVisible(routingDeviceHelp);
  routingDeviceBox.setMobileFormMode(true);
  routingDeviceBox.onChange = [this] { selectRoutingDevice(); };
  routingPage.addAndMakeVisible(routingDeviceBox);
  routingControlsHelp.setFont(settingsHelpFont());
  routingControlsHelp.setJustificationType(juce::Justification::topLeft);
  routingControlsHelp.setMinimumHorizontalScale(1.0f);
  routingControlsHelp.setInterceptsMouseClicks(false, false);
  routingPage.addAndMakeVisible(routingControlsHelp);
  routingHintLabel.setJustificationType(juce::Justification::centredLeft);
  routingHintLabel.setFont(settingsHelpFont());
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
  const bool grouped = usesGroupedNavigation();
  settingsViewport.setScrollBarThickness(grouped ? 4 : 8);
  for (auto *button : {&generalTabButton, &accountTabButton, &audioTabButton,
                       &routingTabButton, &diagnosticsTabButton})
    button->setVisible(!grouped);
  backButton.setVisible(grouped && destination != Destination::Home);
  title.setText(grouped ? pageTitle() : L10n::tr(u8"Configuración", "Settings"),
                juce::dontSendNotification);
  if (grouped) {
    auto area = getLocalBounds();
    const bool home = destination == Destination::Home;
    auto header = area.removeFromTop(64).reduced(8, 0);
    const int target = mobileTopBarActionSize();
    backButton.setBounds(header.removeFromLeft(target).withSizeKeepingCentre(target, target));
    closeButton.setBounds(header.removeFromRight(target).withSizeKeepingCentre(target, target));
    title.setBounds(header.reduced(4, 0));
    auto *content = home ? static_cast<juce::Component *>(homePage.get()) : &tabs;
    settingsViewport.setViewedComponent(content, false);
    const int previousY = settingsViewport.getViewPositionY();
    settingsViewport.setBounds(area);
    int width = juce::jmax(1, area.getWidth());
    if (mobilePageContentHeight(width) > area.getHeight())
      width = juce::jmax(1, width - settingsViewport.getScrollBarThickness());
    content->setSize(width, juce::jmax(area.getHeight(), mobilePageContentHeight(width)));
    if (!home) {
      layoutMobileGeneralPage();
      layoutAccountPage();
      layoutAudioPage();
      layoutRoutingPage();
      layoutDiagnosticsPage();
    }
    settingsViewport.setViewPosition(0, previousY);
    // The viewport changes its content component during navigation. Keep the
    // fixed navigation chrome above it on the mobile OpenGL surface.
    title.toFront(false);
    backButton.toFront(false);
    closeButton.toFront(false);
    return;
  }
  settingsViewport.setViewedComponent(&tabs, false);
  for (auto *child : generalPage.getChildren()) child->setVisible(true);
  remoteServersBox.setVisible(remoteMode == playworship::remote::RemoteSession::Mode::Client);
  remoteServerNameEditor.setVisible(remoteMode == playworship::remote::RemoteSession::Mode::Server);
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
  const auto previousViewPosition = settingsViewport.getViewPosition();
  const bool needsVerticalScroll = useTouchOptimisedControls() || getHeight() < 820;
  settingsViewport.setBounds(area);
  const auto requiredHeight = [this, needsVerticalScroll](const int width) {
    return tabs.getCurrentTabIndex() == 3
               ? routingPageContentHeight(width)
               : (tabs.getCurrentTabIndex() == 0 ? (teamsManagementEnabled ? 1060 : 980) : (needsVerticalScroll ? 900 : 0));
  };
  int contentWidth = juce::jmax(1, area.getWidth());
  if (requiredHeight(contentWidth) > area.getHeight())
    contentWidth = juce::jmax(1, contentWidth - settingsViewport.getScrollBarThickness());
  const int contentHeight = juce::jmax(area.getHeight(), requiredHeight(contentWidth));
  // The viewport scrolls the tab component, so its size must contain every
  // routing row and the footer. Growing only routingContent clips its tail.
  tabs.setSize(contentWidth, contentHeight);

  layoutGeneralPage();
  layoutAccountPage();
  layoutAudioPage();
  layoutRoutingPage();
  layoutDiagnosticsPage();
  settingsViewport.setViewPosition(0, previousViewPosition.y);
}

void SettingsPanel::refreshThemeColours() {
  settingsViewport.getVerticalScrollBar().setColour(
      juce::ScrollBar::thumbColourId, UITheme::textSecondary.withAlpha(0.35f));
  title.setColour(juce::Label::textColourId, UITheme::textPrimary);
  learningLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  learningHelp.setColour(juce::Label::textColourId, UITheme::textSecondary);
  learningButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  learningButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  teamsLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  teamsHelp.setColour(juce::Label::textColourId, UITheme::textSecondary);
  teamsButton.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  teamsButton.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  title.setJustificationType(juce::Justification::centred);
  title.setFont(makeAppFont(useTouchOptimisedControls() ? 20.0f : 18.0f,
                            AppFontWeight::SemiBold));
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
  for (auto *help : {&themeHelp, &languageHelp, &expandLaunchHelp,
                     &primeImportHelp, &stemOptimizationHelp,
                     &padsCrossfadeHelp, &metronomeClickStyleHelp,
                     &remoteControlHelp, &remoteTargetHelp,
                     &actionLogoutHelp, &actionPurgeHelp, &actionInfoHelp})
    help->setColour(juce::Label::textColourId, UITheme::textSecondary);
  applyComboBoxTheme(themeBox, UITheme::cardBgHover.withAlpha(0.78f),
                     UITheme::cardBorder.withAlpha(0.84f));
  languageLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  remoteControlLabel.setColour(juce::Label::textColourId,
                               UITheme::textSecondary);
  remoteStatus.setColour(juce::Label::textColourId, UITheme::textSecondary);
  for (auto *button : { &remoteLocalButton, &remoteClientButton, &remoteServerButton }) {
    button->setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
    button->setColour(juce::TextButton::buttonOnColourId, UITheme::accent);
    button->setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
    button->setColour(juce::TextButton::textColourOnId, UITheme::onAccent);
  }
  applyComboBoxTheme(remoteServersBox, UITheme::cardBgHover, UITheme::cardBorder);
  applyTextEditorTheme(remoteServerNameEditor, UITheme::cardBgHover,
                       UITheme::cardBorder);
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
  appleLinkLabel.setColour(juce::Label::textColourId, actionSecondary);
  appleLinkHelp.setColour(juce::Label::textColourId, UITheme::textSecondary);
  infoLinkButton.setColour(juce::HyperlinkButton::textColourId,
                           UITheme::accent);
  routingSectionLabel.setColour(juce::Label::textColourId, UITheme::textPrimary);
  routingDeviceLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  routingDeviceHelp.setColour(juce::Label::textColourId, UITheme::textSecondary);
  applyComboBoxTheme(routingDeviceBox, UITheme::cardBgHover,
                     UITheme::cardBorder.withAlpha(0.65f));
  routingControlsHelp.setColour(juce::Label::textColourId, UITheme::textSecondary);
  routingHintLabel.setColour(juce::Label::textColourId, UITheme::textSecondary);
  audioHelp.setColour(juce::Label::textColourId, UITheme::textSecondary);
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
  if (homePage != nullptr) homePage->repaint();
  repaint();
}

void SettingsPanel::setTeamsManagementEnabled(bool enabled) {
  if (teamsManagementEnabled == enabled) return;
  teamsManagementEnabled = enabled;
  if (homePage) homePage->refreshEntries();
  teamsButton.setEnabled(enabled);
  teamsLabel.setVisible(enabled && !usesGroupedNavigation());
  teamsHelp.setVisible(enabled && !usesGroupedNavigation());
  teamsButton.setVisible(enabled && !usesGroupedNavigation());
  if (!enabled && destination == Destination::Teams)
    destination = Destination::Home;
  resized();
  if (homePage) homePage->resized();
}

bool SettingsPanel::usesGroupedNavigation() const {
  return useTouchOptimisedControls() || getWidth() < 760;
}

bool SettingsPanel::isLearningRemotePageOpen() const {
  return usesGroupedNavigation() ? destination == Destination::Remote
                                : tabs.getCurrentTabIndex() == 0;
}

juce::Component *SettingsPanel::getLearningTargetComponent(
    const playworship::learning::Anchor anchor) const {
  using Anchor = playworship::learning::Anchor;
  if (anchor == Anchor::SettingsClose)
    return const_cast<HeaderIconButton *>(&closeButton);
  if (!isLearningRemotePageOpen()) {
    if (!usesGroupedNavigation())
      return const_cast<SettingsTabButton *>(&generalTabButton);
    if (destination != Destination::Home)
      return const_cast<HeaderIconButton *>(&backButton);
    return homePage ? homePage->remoteEntry() : nullptr;
  }
  switch (anchor) {
  case Anchor::RemoteAlias:
    if (remoteServerNameEditor.isVisible())
      return const_cast<juce::TextEditor *>(&remoteServerNameEditor);
    break;
  case Anchor::RemoteStatus: return const_cast<juce::Label *>(&remoteStatus);
  case Anchor::RemoteServers:
    if (remoteServersBox.isVisible())
      return const_cast<AdaptiveComboBox *>(&remoteServersBox);
    return const_cast<juce::Label *>(&remoteStatus);
  default: break;
  }
  return const_cast<juce::Component *>(&remoteModeControl);
}

juce::Rectangle<int> SettingsPanel::getLearningTargetBounds(
    const playworship::learning::Anchor anchor) const {
  const auto *component = getLearningTargetComponent(anchor);
  if (!component || !component->isVisible()) return {};
  auto bounds = getLocalArea(component, component->getLocalBounds());
  if (component != &closeButton && component != &backButton && component != &generalTabButton)
    bounds = bounds.getIntersection(settingsViewport.getBounds());
  return bounds.getIntersection(getLocalBounds());
}

void SettingsPanel::revealLearningTarget(const playworship::learning::Anchor anchor) {
  const auto *component = getLearningTargetComponent(anchor);
  if (!component || !component->isVisible() || component == &closeButton ||
      component == &backButton || component == &generalTabButton) return;
  const auto bounds = getLocalArea(component, component->getLocalBounds());
  const auto viewport = settingsViewport.getBounds();
  if (!viewport.contains(bounds))
    settingsViewport.setViewPosition(0, juce::jmax(0, settingsViewport.getViewPositionY() +
        bounds.getY() - viewport.getY() - 20));
}

juce::String SettingsPanel::pageTitle() const {
  switch (destination) {
    case Destination::Home: return L10n::tr(u8"Configuración", "Settings");
    case Destination::Learning: return L10n::tr("Aprender PlayWorship", "Learn PlayWorship");
    case Destination::Teams: return "Teams";
    case Destination::Appearance: return L10n::tr("Apariencia e idioma", "Appearance & language");
    case Destination::Playback: return L10n::tr(u8"Reproducción", "Playback");
    case Destination::Library: return L10n::tr("Biblioteca", "Library");
    case Destination::Audio: return L10n::tr("Dispositivo de audio", "Audio device");
    case Destination::Routing: return L10n::tr("Ruteo de salidas", "Output routing");
    case Destination::Remote: return L10n::tr("Control remoto", "Remote control");
    case Destination::Account: return L10n::tr("Mi cuenta", "My account");
    case Destination::Diagnostics: return L10n::tr(u8"Diagnóstico", "Diagnostics");
  }
  return {};
}

void SettingsPanel::showSettingsPage(Destination next) {
  if (next == Destination::Teams) {
    if (teamsManagementEnabled && onTeamsRequested) onTeamsRequested();
    return;
  }
  if (next == Destination::Learning) {
    if (onLearningRequested) onLearningRequested();
    return;
  }
  if (useTouchOptimisedControls() && next == Destination::Audio)
    next = Destination::Routing;
  if (destination == Destination::Home)
    homeScrollY = settingsViewport.getViewPositionY();
  // Commit an edited server alias and dismiss its keyboard before navigating.
  if (remoteServerNameEditor.hasKeyboardFocus(true))
    grabKeyboardFocus();
  destination = next;
  const int tab = next == Destination::Account ? 1 : next == Destination::Audio ? 2
      : next == Destination::Routing ? 3 : next == Destination::Diagnostics ? 4 : 0;
  tabs.setCurrentTabIndex(tab);
  resized();
  settingsViewport.setViewPosition(0, next == Destination::Home ? homeScrollY : 0);
  backButton.setTitle(L10n::tr(u8"Volver a configuración", "Back to settings"));
  repaint();
}

bool SettingsPanel::keyPressed(const juce::KeyPress &key) {
  if (key != juce::KeyPress::escapeKey) return false;
  if (usesGroupedNavigation() && destination != Destination::Home)
    showSettingsPage(Destination::Home);
  else if (onCloseRequested)
    onCloseRequested();
  return true;
}

int SettingsPanel::mobileGeneralCardHeight() const {
  switch (destination) {
    case Destination::Home:
    case Destination::Learning:
    case Destination::Teams:
    case Destination::Audio:
    case Destination::Routing:
    case Destination::Account:
    case Destination::Diagnostics:
      return 0;
    case Destination::Appearance:
      return mobileCardPadding + 2 * mobileChoiceHeight +
             (useTouchOptimisedControls() ? 0 : mobileToggleHeight);
    case Destination::Playback:
      return mobileCardPadding + 2 * mobileChoiceHeight;
    case Destination::Library:
      return mobileCardPadding + 2 * mobileToggleHeight;
    case Destination::Remote:
      return mobileCardPadding + mobileRemoteChoiceHeight +
             (remoteMode == playworship::remote::RemoteSession::Mode::Local ? 44 : 158);
  }
  return 0;
}

int SettingsPanel::mobilePageContentHeight(int width) const {
  switch (destination) {
    case Destination::Home: return homePage ? homePage->contentHeight() : 0;
    case Destination::Routing: return routingPageContentHeight(width);
    case Destination::Account: return (width < 520 ? 1120 : 920) +
                                     (appleLinkButton ? (width < 520 ? 136 : 100) : 0);
    case Destination::Audio: return 900;
    case Destination::Diagnostics: return width < 520 ? 456 : 320;
    case Destination::Appearance:
    case Destination::Playback:
    case Destination::Library:
    case Destination::Remote:
      return mobileGeneralCardHeight() + 32;
    default: return 360;
  }
}

void SettingsPanel::layoutMobileGeneralPage() {
  for (auto *child : generalPage.getChildren()) child->setVisible(false);
  auto area = generalPage.getLocalBounds().reduced(16);
  const int contentWidth = juce::jmin(640, area.getWidth());
  area = area.withSizeKeepingCentre(contentWidth, area.getHeight());
  auto cardBounds = area.removeFromTop(mobileGeneralCardHeight());
  appearanceCard->setBounds(cardBounds);
  appearanceCard->setVisible(true);
  auto content = cardBounds.reduced(16);
  auto choice = [&](juce::Label &label, juce::Component &control,
                    juce::Label &help, int helpHeight = 36,
                    int afterHelp = 12) {
    label.setVisible(true); control.setVisible(true); help.setVisible(true);
    label.setBounds(content.removeFromTop(22));
    content.removeFromTop(4);
    control.setBounds(content.removeFromTop(48));
    content.removeFromTop(6);
    help.setBounds(content.removeFromTop(helpHeight));
    content.removeFromTop(afterHelp);
  };
  auto toggle = [&](juce::Label &label, juce::ToggleButton &control,
                    juce::Label &help) {
    label.setVisible(true); control.setVisible(true); help.setVisible(true);
    auto row = content.removeFromTop(48);
    control.setBounds(row.removeFromRight(56).withSizeKeepingCentre(56, 48));
    row.removeFromRight(8);
    label.setBounds(row);
    label.setMinimumHorizontalScale(1.0f);
    content.removeFromTop(4);
    help.setBounds(content.removeFromTop(36));
    content.removeFromTop(12);
  };
  for (auto *box : {&themeBox, &languageBox, &padsCrossfadeBox,
                    &metronomeClickStyleBox, &remoteServersBox})
    box->setMobileFormMode(true);

  if (destination == Destination::Appearance) {
    choice(themeLabel, themeBox, themeHelp);
    choice(languageLabel, languageBox, languageHelp);
    if (!useTouchOptimisedControls()) {
      toggle(expandLaunchLabel, expandLaunchToggle, expandLaunchHelp);
    }
  } else if (destination == Destination::Playback) {
    choice(padsCrossfadeLabel, padsCrossfadeBox, padsCrossfadeHelp);
    choice(metronomeClickStyleLabel, metronomeClickStyleBox,
           metronomeClickStyleHelp);
  } else if (destination == Destination::Library) {
    toggle(primeImportLabel, primeImportToggle, primeImportHelp);
    toggle(stemOptimizationLabel, stemOptimizationToggle, stemOptimizationHelp);
  } else if (destination == Destination::Remote) {
    choice(remoteControlLabel, remoteModeControl, remoteControlHelp, 48, 0);
    auto buttons = remoteModeControl.getLocalBounds();
    const int width = buttons.getWidth() / 3;
    remoteLocalButton.setBounds(buttons.removeFromLeft(width));
    remoteClientButton.setBounds(buttons.removeFromLeft(width));
    remoteServerButton.setBounds(buttons);
    remoteStatus.setVisible(true);
    remoteStatus.setBounds(content.removeFromTop(44));
    remoteStatus.setMinimumHorizontalScale(1.0f);
    if (remoteMode == playworship::remote::RemoteSession::Mode::Local) {
      remoteServersBox.setBounds({});
      remoteServerNameEditor.setBounds({});
      remoteTargetHelp.setBounds({});
      return;
    }
    content.removeFromTop(10);
    using Mode = playworship::remote::RemoteSession::Mode;
    remoteServersBox.setBounds(content.removeFromTop(48));
    remoteServerNameEditor.setBounds(remoteServersBox.getBounds());
    content.removeFromTop(6);
    remoteTargetHelp.setBounds(content.removeFromTop(42));
    remoteServersBox.setVisible(remoteMode == Mode::Client);
    remoteServerNameEditor.setVisible(remoteMode == Mode::Server);
    remoteTargetHelp.setVisible(remoteMode != Mode::Local);
  }
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
  teamsLabel.setText("Teams", juce::dontSendNotification);
  teamsHelp.setText(L10n::tr("Consulta tu acceso al Team o administra los usuarios de tu nube.", "View your Team access or manage the users of your Cloud."), juce::dontSendNotification);
  teamsButton.setButtonText(L10n::tr("Abrir Teams", "Open Teams"));
  learningLabel.setText(L10n::tr("Aprender PlayWorship", "Learn PlayWorship"), juce::dontSendNotification);
  learningHelp.setText(L10n::tr("Guías interactivas que puedes pausar y retomar.", "Interactive guides you can pause and resume."), juce::dontSendNotification);
  learningButton.setButtonText(L10n::tr("Abrir guías", "Open guides"));
  title.setText(L10n::tr(u8"Configuración", "Settings"),
                juce::dontSendNotification);
  generalTabButton.setButtonText(L10n::tr("General", "General"));
  accountTabButton.setButtonText(L10n::tr(u8"Sesión", "Account"));
  audioTabButton.setButtonText(L10n::tr(u8"Audio", "Audio"));
  routingTabButton.setButtonText(L10n::tr("Ruteo", "Routing"));
  diagnosticsTabButton.setButtonText(
      L10n::tr(u8"Diagnóstico", "Diagnostics"));
  themeLabel.setText(L10n::tr("Tema", "Theme"), juce::dontSendNotification);
  themeHelp.setText(L10n::tr("Elige los colores de la aplicación.",
                              "Choose the app's colours."),
                    juce::dontSendNotification);
  languageLabel.setText(L10n::tr("Idioma", "Language"),
                        juce::dontSendNotification);
  languageHelp.setText(L10n::tr("Se aplica a menús, controles y mensajes.",
                                 "Applies to menus, controls, and messages."),
                       juce::dontSendNotification);
  expandLaunchLabel.setText(L10n::tr("Ventana al iniciar", "Window on launch"),
                            juce::dontSendNotification);
  expandLaunchHelp.setText(L10n::tr("Usa el área de trabajo disponible al abrir la app.",
                                     "Uses the available desktop space when opening the app."),
                           juce::dontSendNotification);
  expandLaunchToggle.setButtonText(
      L10n::tr("Expandir al area de trabajo", "Expand to work area"));
  primeImportLabel.setText(
      L10n::tr("Importar desde Prime", "Import from Prime"),
                           juce::dontSendNotification);
#if MTCONTROLS_MAC_APP_STORE
  primeImportHelp.setText(L10n::tr("En esta versión, importa los archivos de audio desde Biblioteca.",
                                   "In this edition, import audio files from Library."),
                         juce::dontSendNotification);
#else
  primeImportHelp.setText(L10n::tr("Solo en macOS: importa la biblioteca local de Prime.",
                                   "macOS only: imports the local Prime library."),
                         juce::dontSendNotification);
#endif
  primeImportToggle.setButtonText(
      L10n::tr(u8"Habilitar importación de Prime",
               "Enable Prime import"));
  stemOptimizationLabel.setText(
      L10n::tr(u8"Optimización de stems",
               "Stem optimization"),
      juce::dontSendNotification);
  stemOptimizationHelp.setText(L10n::tr("En importaciones compatibles, reduce el espacio de los stems.",
                                        "Reduces stem storage in supported imports."),
                               juce::dontSendNotification);
  stemOptimizationToggle.setButtonText(
      L10n::tr("Convertir stems para ahorrar almacenamiento",
               "Convert stems to save storage"));
  padsCrossfadeLabel.setText(
      L10n::tr("Crossfade de pads", "Pads crossfade"),
      juce::dontSendNotification);
  padsCrossfadeHelp.setText(L10n::tr("Duración del cambio suave entre pads.",
                                   "Duration of the smooth transition between pads."),
                            juce::dontSendNotification);
  metronomeClickStyleLabel.setText(
      L10n::tr("Estilo del click", "Click style"),
      juce::dontSendNotification);
  metronomeClickStyleHelp.setText(L10n::tr("Cambia el sonido del click, sin alterar el tempo.",
                                         "Changes the click sound without changing tempo."),
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
  actionLogoutLabel.setText(L10n::tr(u8"Sesión en este dispositivo",
                                     "Session on this device"),
                            juce::dontSendNotification);
  actionLogoutHelp.setText(L10n::tr("Desconecta esta cuenta de la aplicación.",
                                    "Disconnects this account from the app."),
                           juce::dontSendNotification);
  actionPurgeLabel.setText(
      L10n::tr(u8"Purgar datos locales",
               "Purge local data"),
      juce::dontSendNotification);
  actionPurgeHelp.setText(L10n::tr("Vacía la Biblioteca local; conserva los .pwproj y la nube.",
                                   "Empties the local Library; keeps .pwproj files and cloud data."),
                          juce::dontSendNotification);
  actionInfoLabel.setText(L10n::tr(u8"Ayuda con tu cuenta",
                                  "Help with your account"),
                          juce::dontSendNotification);
  actionInfoHelp.setText(L10n::tr("Abre un correo para contactar a soporte.",
                                  "Opens an email to contact support."),
                         juce::dontSendNotification);
  logoutButton.setButtonText(L10n::tr(u8"Cerrar sesión", "Sign out"));
  purgeButton.setButtonText(L10n::tr("Purgar", "Purge"));
  infoLinkButton.setButtonText(L10n::tr("Contactar soporte", "Contact support"));
  appleLinkLabel.setText(L10n::tr("Vincular Apple", "Link Apple"), juce::dontSendNotification);
  if (appleLinkHelp.getText().isEmpty())
    appleLinkHelp.setText(L10n::tr("Añade Apple como acceso a esta misma cuenta, conservando tu biblioteca y permisos.",
                                  "Add Apple sign-in to this account and keep your library and access."),
                          juce::dontSendNotification);
  routingSectionLabel.setText(L10n::tr("Ruteo por tipo", "Routing by category"),
                              juce::dontSendNotification);
  routingDeviceLabel.setText(L10n::tr("Salida de audio", "Audio output"),
                             juce::dontSendNotification);
  routingDeviceHelp.setText(
      L10n::tr("Elige el dispositivo que recibirá el audio. Las asignaciones se guardan por dispositivo.",
               "Choose the device that receives audio. Routing is saved per device."),
      juce::dontSendNotification);
  routingControlsHelp.setText(
      L10n::tr("Mono usa una salida; estéreo, dos consecutivas. Canal elige la primera.",
               "Mono uses one output; stereo uses two adjacent outputs. Channel selects the first."),
      juce::dontSendNotification);
  routingHintLabel.setText(
      L10n::tr("Define salidas por categoría para el dispositivo actual.",
               "Set per-category outputs for the current device."),
      juce::dontSendNotification);
  audioHelp.setText(
      L10n::tr("Las salidas activas aquí estarán disponibles en Ruteo. Revisa las asignaciones al cambiar de dispositivo.",
               "Outputs enabled here are available in Routing. Check assignments when changing devices."),
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
  remoteControlLabel.setText(L10n::tr("Control remoto", "Remote control"),
                             juce::dontSendNotification);
  remoteControlHelp.setText(L10n::tr("Local reproduce aquí. Cliente controla un servidor; Servidor comparte el control.",
                                      "Local plays here. Client controls a server; Server shares control."),
                            juce::dontSendNotification);
  using Mode = playworship::remote::RemoteSession::Mode;
  remoteTargetHelp.setText(
      remoteMode == Mode::Server
          ? L10n::tr("Nombre visible para clientes; vacío usa el del equipo.",
                     "Name shown to clients; blank uses the device name.")
          : L10n::tr("Elige un servidor detectado en la red local.",
                     "Choose a server found on the local network."),
      juce::dontSendNotification);
  remoteLocalButton.setButtonText("Local");
  remoteClientButton.setButtonText(L10n::tr("Cliente", "Client"));
  remoteServerButton.setButtonText(L10n::tr("Servidor", "Server"));
  remoteServersBox.setTextWhenNothingSelected(L10n::tr("Buscar servidor…", "Find server…"));
  remoteServerNameEditor.setTextToShowWhenEmpty(
      L10n::tr("Cambiar nombre del servidor…", "Change server name…"),
      UITheme::textSecondary);
  remoteServerNameEditor.setTooltip(L10n::tr(
      "Nombre que verán los clientes. Vacío usa el nombre del dispositivo.",
      "Name shown to clients. Leave empty to use the device name."));
  closeButton.setTitle(L10n::tr(u8"Cerrar configuración", "Close settings"));
  closeButton.setTooltip(L10n::tr(u8"Cerrar configuración", "Close settings"));
  backButton.setTitle(L10n::tr(u8"Volver a configuración", "Back to settings"));
  backButton.setTooltip(L10n::tr(u8"Volver a configuración", "Back to settings"));
  if (homePage != nullptr) homePage->refreshTexts();
}

void SettingsPanel::layoutGeneralPage() {
  auto area = generalPage.getLocalBounds().reduced(
      useTouchOptimisedControls() ? 28 : 22,
      useTouchOptimisedControls() ? 24 : 18);
  const int rowH = useTouchOptimisedControls() ? 82 : 74;
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
                             juce::Component &control, juce::Label &help) {
    auto right = row.removeFromRight(rightW);
    label.setBounds(row.removeFromTop(28));
    help.setBounds(row);
    control.setBounds(right.withSizeKeepingCentre(right.getWidth(), controlH));
  };

  auto layoutToggleRow = [&](juce::Rectangle<int> row, juce::Label &label,
                             juce::ToggleButton &toggle, juce::Label &help) {
    auto right = row.removeFromRight(rightW);
    label.setBounds(row.removeFromTop(28));
    help.setBounds(row);
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

  auto layoutRemoteControls = [&] {
    layoutChoiceRow(area.removeFromTop(rowH), remoteControlLabel,
                    remoteModeControl, remoteControlHelp);
    auto buttons = remoteModeControl.getLocalBounds();
    const int width = buttons.getWidth() / 3;
    remoteLocalButton.setBounds(buttons.removeFromLeft(width));
    remoteClientButton.setBounds(buttons.removeFromLeft(width));
    remoteServerButton.setBounds(buttons);
    auto connection = area.removeFromTop(40);
    const auto nameBounds = connection.removeFromRight(rightW);
    remoteServersBox.setBounds(nameBounds);
    remoteServerNameEditor.setBounds(nameBounds);
    remoteStatus.setBounds(connection);
    remoteTargetHelp.setBounds(area.removeFromTop(28));
    remoteTargetHelp.setVisible(remoteMode !=
        playworship::remote::RemoteSession::Mode::Local);
  };

  layoutChoiceRow(area.removeFromTop(rowH), learningLabel, learningButton, learningHelp);
  addSeparator();
  teamsLabel.setVisible(teamsManagementEnabled);
  teamsHelp.setVisible(teamsManagementEnabled);
  teamsButton.setVisible(teamsManagementEnabled);
  teamsButton.setEnabled(teamsManagementEnabled);
  if (teamsManagementEnabled) {
    layoutChoiceRow(area.removeFromTop(rowH), teamsLabel, teamsButton, teamsHelp);
    addSeparator();
  } else {
    teamsLabel.setBounds({});
    teamsHelp.setBounds({});
    teamsButton.setBounds({});
  }

  // On iPhone the settings viewport initially shows only a few rows. Keep
  // the mode selector and discovery status visible without requiring a scroll.
  const bool compactHeight = useTouchOptimisedControls() && getHeight() < 700;
  if (compactHeight) {
    layoutRemoteControls();
    addSeparator();
  }

  layoutChoiceRow(area.removeFromTop(rowH), themeLabel, themeBox, themeHelp);
  addSeparator();
  layoutChoiceRow(area.removeFromTop(rowH), languageLabel, languageBox,
                  languageHelp);
  addSeparator();
  layoutToggleRow(area.removeFromTop(rowH), expandLaunchLabel,
                  expandLaunchToggle, expandLaunchHelp);
  addSeparator();
  layoutToggleRow(area.removeFromTop(rowH), primeImportLabel,
                  primeImportToggle, primeImportHelp);
  addSeparator();
  layoutToggleRow(area.removeFromTop(rowH), stemOptimizationLabel,
                  stemOptimizationToggle, stemOptimizationHelp);
  addSeparator();
  layoutChoiceRow(area.removeFromTop(rowH), padsCrossfadeLabel,
                  padsCrossfadeBox, padsCrossfadeHelp);
  addSeparator();
  layoutChoiceRow(area.removeFromTop(rowH), metronomeClickStyleLabel,
                  metronomeClickStyleBox, metronomeClickStyleHelp);
  if (!compactHeight) {
    addSeparator();
    layoutRemoteControls();
  }
  for (; separatorIndex < static_cast<int>(generalRowSeparators.size()); ++separatorIndex)
    generalRowSeparators[static_cast<size_t>(separatorIndex)]->setBounds({});
}

void SettingsPanel::layoutAccountPage() {
  const bool compact = usesGroupedNavigation() && accountPage.getWidth() < 520;
  auto area = accountPage.getLocalBounds().reduced(
      compact ? 16 : useTouchOptimisedControls() ? 24 : 20,
      useTouchOptimisedControls() ? 18 : 14);
  const int cardPaddingX = compact ? 16 : 22;
  constexpr int cardPaddingY = 20;
  constexpr int titleH = 20;
  constexpr int labelH = 14;
  constexpr int valueH = 22;
  constexpr int pairGap = 12;
  constexpr int dividerMargin = 16;
  constexpr int sectionGap = 16;
  const int copyButtonSize = usesGroupedNavigation() ? 44 : 28;

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
  actionLogoutHelp.setBounds({});
  actionPurgeLabel.setBounds({});
  actionPurgeHelp.setBounds({});
  actionInfoLabel.setBounds({});
  actionInfoHelp.setBounds({});
  infoLinkButton.setBounds({});
  logoutButton.setBounds({});
  purgeButton.setBounds({});
  appleLinkLabel.setBounds({});
  appleLinkHelp.setBounds({});
  if (appleLinkButton) appleLinkButton->setBounds({});
  if (sessionIdentitySeparator != nullptr)
    sessionIdentitySeparator->setBounds({});
  if (actionsTitleSeparator != nullptr)
    actionsTitleSeparator->setBounds({});
  if (actionsRowSeparator != nullptr)
    actionsRowSeparator->setBounds({});

  const int pairH = labelH + valueH;
  const int quotaRowH = compact ? 52 : 30;
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
  sessionUidCopyButton.setBounds(uidValueRow.removeFromRight(copyButtonSize)
      .withSizeKeepingCentre(copyButtonSize, usesGroupedNavigation() ? 44 : valueH));
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
      licenseValueRow.removeFromRight(copyButtonSize)
          .withSizeKeepingCentre(copyButtonSize, usesGroupedNavigation() ? 44 : valueH));
  sessionLicenseValue.setBounds(licenseValueRow);
  info.removeFromTop(pairGap);

  layoutInfoPair(info.removeFromTop(pairH), sessionVersionLabel,
                 sessionVersionValue);
  info.removeFromTop(pairGap);
  sessionQuotaLabel.setBounds(info.removeFromTop(labelH));
  sessionQuotaValue.setBounds(info.removeFromTop(valueH));
  const auto layoutQuotaRow = [&](juce::Label& label, juce::Label& value) {
    auto row = info.removeFromTop(quotaRowH);
    if (compact) {
      label.setBounds(row.removeFromTop(22));
      value.setBounds(row.removeFromTop(26));
      value.setJustificationType(juce::Justification::centredLeft);
    } else {
      value.setBounds(row.removeFromRight(180));
      label.setBounds(row);
      value.setJustificationType(juce::Justification::centredRight);
    }
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
    const int actionsH = (compact ? 430 : 250) +
                         (appleLinkButton ? (compact ? 136 : 100) : 0);
    actionsCard->setBounds(area.removeFromTop(actionsH));
    auto actions = actionsCard->getBounds().reduced(22, 4);
    actions.removeFromTop(16);
    actionsSectionLabel.setBounds(actions.removeFromTop(20));
    actions.removeFromTop(8);
    if (actionsTitleSeparator != nullptr)
      actionsTitleSeparator->setBounds(actions.removeFromTop(1));

    if (compact) {
      const auto action = [&](juce::Label &label, juce::Label &help,
                              juce::Component &button, int helpHeight = 36) {
        label.setBounds(actions.removeFromTop(24));
        button.setBounds(actions.removeFromTop(44));
        actions.removeFromTop(4);
        help.setBounds(actions.removeFromTop(helpHeight));
        actions.removeFromTop(8);
      };
      action(actionLogoutLabel, actionLogoutHelp, logoutButton);
      if (appleLinkButton)
        action(appleLinkLabel, appleLinkHelp, *appleLinkButton, 52);
      action(actionPurgeLabel, actionPurgeHelp, purgeButton, 52);
      action(actionInfoLabel, actionInfoHelp, infoLinkButton);
      return;
    }

    auto logoutRow = actions.removeFromTop(60);
    auto logoutControl = logoutRow.removeFromRight(112).reduced(0, 8);
    actionLogoutLabel.setBounds(logoutRow.removeFromTop(24));
    actionLogoutHelp.setBounds(logoutRow);
    logoutButton.setBounds(logoutControl);
    if (actionsRowSeparator != nullptr)
      actionsRowSeparator->setBounds(actions.removeFromTop(1));

    if (appleLinkButton) {
      auto appleRow = actions.removeFromTop(100);
      appleLinkLabel.setBounds(appleRow.removeFromTop(24));
      appleLinkHelp.setBounds(appleRow.removeFromTop(30));
      appleLinkButton->setBounds(appleRow.removeFromTop(44));
    }

    auto purgeRow = actions.removeFromTop(60);
    auto purgeControl = purgeRow.removeFromRight(112).reduced(0, 8);
    actionPurgeLabel.setBounds(purgeRow.removeFromTop(24));
    actionPurgeHelp.setBounds(purgeRow);
    purgeButton.setBounds(purgeControl);

    auto helpRow = actions.removeFromTop(60);
    auto helpControl = helpRow.removeFromRight(112);
    actionInfoLabel.setBounds(helpRow.removeFromTop(24));
    actionInfoHelp.setBounds(helpRow);
    infoLinkButton.setBounds(helpControl);
  }
}

void SettingsPanel::setAppleSignInAvailable(bool available) {
  if (available && !appleLinkButton) {
    appleLinkButton = NativeAppleSignIn::createButton([this] {
      if (onAppleLinkRequested) onAppleLinkRequested();
    });
    if (appleLinkButton) accountPage.addAndMakeVisible(*appleLinkButton);
  } else if (!available) {
    appleLinkButton.reset();
  }
  appleLinkLabel.setVisible(appleLinkButton != nullptr);
  appleLinkHelp.setVisible(appleLinkButton != nullptr);
  resized();
}

void SettingsPanel::setAppleSignInStatus(const juce::String &text, bool busy) {
  appleLinkHelp.setText(text, juce::dontSendNotification);
  if (appleLinkButton) appleLinkButton->setEnabled(!busy);
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
  audioHelp.setBounds(area.removeFromTop(area.getWidth() < 520 ? 52 : 34));
  area.removeFromTop(8);
  if (audioSetupComp != nullptr) {
    audioSetupComp->setBounds(area);
  }
}

void SettingsPanel::layoutDiagnosticsPage() {
  const bool compact = usesGroupedNavigation() && diagnosticsPage.getWidth() < 520;
  auto area = diagnosticsPage.getLocalBounds().reduced(
      compact ? 16 : useTouchOptimisedControls() ? 28 : 22,
      useTouchOptimisedControls() ? 24 : 18);
  const int cardHeight = compact ? 410 : useTouchOptimisedControls() ? 252 : 218;
  const int cardWidth = juce::jmin(area.getWidth(), 720);
  const auto cardBounds = area.removeFromTop(cardHeight).withWidth(cardWidth);
  if (diagnosticsCard != nullptr)
    diagnosticsCard->setBounds(cardBounds);

  auto content = cardBounds.reduced(compact ? 16 : useTouchOptimisedControls() ? 24 : 22,
                                    useTouchOptimisedControls() ? 20 : 18);
  diagnosticsSectionLabel.setBounds(content.removeFromTop(compact ? 48 : 28));
  content.removeFromTop(10);
  diagnosticsHint.setBounds(content.removeFromTop(
      compact ? 88 : useTouchOptimisedControls() ? 52 : 42));
  content.removeFromTop(6);
  diagnosticsPrivacyHint.setBounds(content.removeFromTop(
      compact ? 120 : useTouchOptimisedControls() ? 64 : 52));
  content.removeFromTop(12);
  const int buttonWidth = juce::jmin(useTouchOptimisedControls() ? 280 : 240,
                                    content.getWidth());
  diagnosticsButton.setBounds(
      content.removeFromTop(usesGroupedNavigation() ? 48 : 42)
          .withWidth(buttonWidth));
}

int SettingsPanel::routingPageContentHeight(const int width) const {
  const int listWidth = juce::jmax(1, width - 36);
  const int hintHeight = listWidth < 520 ? 44 : 24;
  const int controlsHelpHeight = listWidth < 520 ? 52 : 34;
  const int deviceHeight = useTouchOptimisedControls()
      ? 22 + 4 + 48 + 6 + (listWidth < 520 ? 44 : 28) + 18 : 0;
  return 32 + deviceHeight + 22 + 6 + controlsHelpHeight + 8 +
         static_cast<int>(routingRows.size()) * RoutingRow::preferredHeightForWidth(listWidth) +
         12 + hintHeight;
}

void SettingsPanel::layoutRoutingPage() {
  auto area = routingPage.getLocalBounds().reduced(18, 16);

  const bool showDevice = useTouchOptimisedControls();
  routingDeviceLabel.setVisible(showDevice);
  routingDeviceBox.setVisible(showDevice);
  routingDeviceHelp.setVisible(showDevice);
  if (showDevice) {
    routingDeviceLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);
    routingDeviceBox.setBounds(area.removeFromTop(48));
    area.removeFromTop(6);
    routingDeviceHelp.setBounds(area.removeFromTop(
        area.getWidth() < 520 ? 44 : 28));
    area.removeFromTop(18);
  }

  auto labelArea = area.removeFromTop(22);
  routingSectionLabel.setBounds(labelArea);
  area.removeFromTop(6);
  routingControlsHelp.setBounds(area.removeFromTop(
      area.getWidth() < 520 ? 52 : 34));
  area.removeFromTop(8);
  const int rowH = RoutingRow::preferredHeightForWidth(area.getWidth());
  const int listHeight = rowH * static_cast<int>(routingRows.size());
  const auto listArea = area.removeFromTop(listHeight);
  int y = 0;
  const int contentW = juce::jmax(1, listArea.getWidth());
  for (auto &rowComp : routingRows) {
    rowComp->setBounds(0, y, contentW, rowH);
    y += rowH;
  }
  routingContent.setBounds(listArea);
  area.removeFromTop(12);
  routingHintLabel.setBounds(area.removeFromTop(area.getWidth() < 520 ? 44 : 24));
}

SettingsPanel::~SettingsPanel() {
  tabs.onTabChanged = nullptr;
  settingsViewport.setViewedComponent(nullptr, false);
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

void SettingsPanel::refreshRoutingDeviceOptions() {
  outputDeviceChoices.clear();
  routingDeviceBox.clear(juce::dontSendNotification);
  if (deviceManager == nullptr) {
    routingDeviceBox.setText(L10n::tr("Sin dispositivo", "No device"),
                             juce::dontSendNotification);
    routingDeviceBox.setEnabled(false);
    return;
  }

  const auto currentType = deviceManager->getCurrentAudioDeviceType();
  const auto *currentDevice = deviceManager->getCurrentAudioDevice();
  const auto currentName = currentDevice != nullptr ? currentDevice->getName()
                                                    : juce::String{};
  struct Candidate { juce::String typeName, deviceName; };
  std::vector<Candidate> candidates;
  for (auto *type : deviceManager->getAvailableDeviceTypes()) {
    if (type == nullptr) continue;
    for (const auto &name : type->getDeviceNames(false))
      if (name.isNotEmpty())
        candidates.push_back({type->getTypeName(), name});
  }
  int currentId = 0;
  for (const auto &candidate : candidates) {
    outputDeviceChoices.push_back({candidate.typeName, candidate.deviceName});
    const int id = static_cast<int>(outputDeviceChoices.size());
    const bool duplicateName = std::count_if(candidates.begin(), candidates.end(),
        [&candidate](const Candidate &other) {
          return other.deviceName == candidate.deviceName;
        }) > 1;
    routingDeviceBox.addItem(duplicateName
        ? candidate.deviceName + " (" + candidate.typeName + ")"
        : candidate.deviceName, id);
    if (candidate.typeName == currentType && candidate.deviceName == currentName)
      currentId = id;
  }
  routingDeviceBox.setSelectedId(currentId, juce::dontSendNotification);
  if (currentId == 0 && currentName.isNotEmpty())
    routingDeviceBox.setText(currentName, juce::dontSendNotification);
  else if (outputDeviceChoices.empty())
    routingDeviceBox.setText(L10n::tr("Sin dispositivo", "No device"),
                             juce::dontSendNotification);
  routingDeviceBox.setEnabled(!outputDeviceChoices.empty());
}

void SettingsPanel::selectRoutingDevice() {
  if (deviceManager == nullptr) return;
  const int index = routingDeviceBox.getSelectedId() - 1;
  if (index < 0 || index >= static_cast<int>(outputDeviceChoices.size())) return;
  const auto choice = outputDeviceChoices[static_cast<size_t>(index)];
  auto *current = deviceManager->getCurrentAudioDevice();
  if (deviceManager->getCurrentAudioDeviceType() == choice.typeName &&
      current != nullptr && current->getName() == choice.deviceName)
    return;

  const auto oldType = deviceManager->getCurrentAudioDeviceType();
  const auto oldSetup = deviceManager->getAudioDeviceSetup();
  if (oldType != choice.typeName)
    deviceManager->setCurrentAudioDeviceType(choice.typeName, false);
  auto setup = deviceManager->getAudioDeviceSetup();
  setup.outputDeviceName = choice.deviceName;
  setup.inputDeviceName = {};
  setup.useDefaultInputChannels = false;
  setup.inputChannels.clear();
  setup.useDefaultOutputChannels = true;
  auto error = deviceManager->getCurrentAudioDeviceType() == choice.typeName
      ? deviceManager->setAudioDeviceSetup(setup, true)
      : L10n::tr("No se pudo seleccionar el tipo de dispositivo.",
                 "Could not select the device type.");
  if (error.isNotEmpty()) {
    if (deviceManager->getCurrentAudioDeviceType() != oldType)
      deviceManager->setCurrentAudioDeviceType(oldType, false);
    deviceManager->setAudioDeviceSetup(oldSetup, false);
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        L10n::tr("No se pudo cambiar la salida", "Could not change audio output"),
        error);
  }
  refreshRoutingForDevice();
}

void SettingsPanel::refreshRoutingForDevice() {
  refreshRoutingDeviceOptions();
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

void SettingsPanel::updateRemoteState(playworship::remote::RemoteSession::Mode mode,
    const std::vector<playworship::remote::DiscoveredServer> &servers, const juce::String &status,
    const juce::String &selectedServerName) {
  using Mode = playworship::remote::RemoteSession::Mode;
  const bool modeChanged = remoteMode != mode;
  remoteMode = mode;
  remoteLocalButton.setToggleState(mode == Mode::Local, juce::dontSendNotification);
  remoteClientButton.setToggleState(mode == Mode::Client, juce::dontSendNotification);
  remoteServerButton.setToggleState(mode == Mode::Server, juce::dontSendNotification);
  const bool remoteVisible = !usesGroupedNavigation() || destination == Destination::Remote;
  remoteServersBox.setVisible(remoteVisible && mode == Mode::Client);
  remoteServerNameEditor.setVisible(remoteVisible && mode == Mode::Server);
  remoteTargetHelp.setVisible(remoteVisible && mode != Mode::Local);
  remoteTargetHelp.setText(
      mode == Mode::Server
          ? L10n::tr("Nombre visible para clientes; vacío usa el del equipo.",
                     "Name shown to clients; blank uses the device name.")
          : L10n::tr("Elige un servidor detectado en la red local.",
                     "Choose a server found on the local network."),
      juce::dontSendNotification);
  remoteStatus.setText(status.isNotEmpty() ? status
      : mode == Mode::Local ? L10n::tr("Este dispositivo reproduce y controla sus canciones.",
                                      "This device plays and controls its songs.")
      : mode == Mode::Client ? L10n::tr("Buscando servidores en la red local…", "Looking for servers on the local network…")
      : L10n::tr("Disponible para clientes en la red local.", "Available to clients on the local network."),
      juce::dontSendNotification);
  juce::StringArray names;
  for (const auto &server : servers) names.add(server.instance + " (" + server.address + ")");
  juce::StringArray oldNames;
  for (const auto &server : remoteServers) oldNames.add(server.instance + " (" + server.address + ")");
  if (names != oldNames) {
    const auto selected = remoteServersBox.getText();
    remoteServers = servers;
    remoteServersBox.clear(juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i) remoteServersBox.addItem(names[i], i + 1);
    remoteServersBox.setSelectedId(names.indexOf(selected) + 1, juce::dontSendNotification);
  }
  if (selectedServerName.isNotEmpty()) {
    for (size_t i = 0; i < remoteServers.size(); ++i) {
      if (remoteServers[i].instance == selectedServerName) {
        remoteServersBox.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
        break;
      }
    }
  }
  if (homePage != nullptr) homePage->refreshTexts();
  if (modeChanged && usesGroupedNavigation() && destination == Destination::Remote)
    resized();
}
