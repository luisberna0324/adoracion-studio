#include "../../PluginProcessor.h"
#include "RehearsalPanel.h"
#include "../../data/Localization.h"

namespace {
juce::String groupName(const juce::String &name) {
  if (name == "Bass") return L10n::tr("Bajo", "Bass");
  if (name == "Drums") return L10n::tr("Batería", "Drums");
  if (name == "Keys") return L10n::tr("Teclados", "Keys");
  if (name == "Percussion") return L10n::tr("Percusión", "Percussion");
  if (name == "Acoustic") return L10n::tr("Guitarras acústicas", "Acoustic guitars");
  if (name == "Electric Guitars") return L10n::tr("Guitarras eléctricas", "Electric guitars");
  if (name == "Lead Guitars") return L10n::tr("Guitarras principales", "Lead guitars");
  if (name == "Vocals") return L10n::tr("Voces", "Vocals");
  if (name == "Strings") return L10n::tr("Cuerdas", "Strings");
  if (name == "Brass") return L10n::tr("Metales", "Brass");
  if (name == "Woodwinds") return L10n::tr("Vientos", "Woodwinds");
  if (name == "Synths") return L10n::tr("Sintetizadores", "Synths");
  if (name == "Accordion") return L10n::tr("Acordeón", "Accordion");
  return name;
}
}

RehearsalPanel::Header::Header() {
  close.setBare(true);
  close.setTooltip(L10n::tr("Cerrar ensayo", "Close rehearsal"));
  mixer.setSvgLabel(SvgIconName::AudioWaveform, L10n::tr("Mixer", "Mixer"));
  mixer.setTooltip(L10n::tr("Mostrar u ocultar mixer", "Show or hide mixer"));
  addAndMakeVisible(close);
  addAndMakeVisible(mixer);
}
void RehearsalPanel::Header::paint(juce::Graphics &g) {
  g.fillAll(UITheme::panelDark);
  g.setColour(UITheme::textPrimary);
  g.setFont(makeAppFont(20.0f, AppFontWeight::SemiBold));
  g.drawFittedText(L10n::tr("Ensayo", "Rehearsal"),
                  getLocalBounds().withTrimmedRight(160).reduced(16, 0),
                  juce::Justification::centredLeft, 1);
}
void RehearsalPanel::Header::resized() {
  auto row = getLocalBounds().reduced(8, 4);
  close.setBounds(row.removeFromRight(48));
  row.removeFromRight(6);
  mixer.setBounds(row.removeFromRight(98));
}

RehearsalPanel::RehearsalPanel(PlaybackSession &s, ChannelStripLookAndFeel &lnf)
    : session(s), normal(L10n::tr("Mezcla normal", "Normal mix")),
      groupStrip(L10n::tr("GRUPO", "GROUP"), lnf, [this] { return meter(true); }),
      restStrip(L10n::tr("RESTO", "REST"), lnf, [this] { return meter(false); }) {
  viewport.setViewedComponent(&content, false);
  viewport.setScrollBarsShown(true, false);
  viewport.setScrollOnDragEnabled(true);
  addAndMakeVisible(viewport);
  addAndMakeVisible(groupLabel);
  addAndMakeVisible(normal);
  for (juce::Component *c : std::initializer_list<juce::Component *>{&groupSelector,
       &members, &status, &levels, &groupStrip, &restStrip}) content.addAndMakeVisible(c);
  groupLabel.setText(L10n::tr("Instrumento o grupo", "Instrument or group"), juce::dontSendNotification);
  groupSelector.setTextWhenNothingSelected(L10n::tr("Elige qué quieres practicar", "Choose what to practice"));
  groupSelector.setTitle(L10n::tr("Grupo de ensayo", "Rehearsal group"));
  groupSelector.setMobileFormMode(useTouchOptimisedControls());
  groupSelector.onChange = [this] {
    const int index = groupSelector.getSelectedId() - 1;
    if (juce::isPositiveAndBelow(index, groupIds.size())) session.setRehearsalGroup(groupIds[index]);
    refresh();
  };
  normal.setLookAndFeel(&lnf);
  normal.onClick = [this] { session.clearRehearsal(); refresh(); };
  for (auto *strip : {&groupStrip, &restStrip}) {
    strip->setMuteVisible(false);
    strip->setSoloVisible(false);
    strip->setGainVisible(false);
    strip->setPowerVisible(false);
    strip->setLayoutScale(1.1f);
  }
  groupStrip.setOnVolumeChanged([this](float value) { session.setRehearsalLevel(true, value); });
  restStrip.setOnVolumeChanged([this](float value) { session.setRehearsalLevel(false, value); });
  levels.setJustificationType(juce::Justification::centred);
  members.setMinimumHorizontalScale(1.0f);
  status.setMinimumHorizontalScale(1.0f);
  refreshThemeColours();
  refresh();
  startTimerHz(15);
}
RehearsalPanel::~RehearsalPanel() { normal.setLookAndFeel(nullptr); viewport.setViewedComponent(nullptr, false); }

void RehearsalPanel::refreshThemeColours() {
  applyComboBoxTheme(groupSelector);
  for (auto *label : {&groupLabel, &members, &status, &levels}) {
    label->setColour(juce::Label::textColourId, label == &groupLabel ? UITheme::textPrimary : UITheme::textSecondary);
    label->setFont(makeAppFont(label == &groupLabel ? 15.0f : 12.0f));
  }
  normal.setColour(juce::TextButton::buttonColourId, UITheme::cardBgHover);
  normal.setColour(juce::TextButton::textColourOffId, UITheme::textPrimary);
  groupStrip.refreshThemeColours();
  restStrip.refreshThemeColours();
  for (auto *strip : {&groupStrip, &restStrip}) {
    strip->setCardBgColour(UITheme::cardBg);
    strip->setChannelBandColours(UITheme::accent, UITheme::onAccent);
  }
  header.repaint();
  repaint();
}
void RehearsalPanel::refresh() {
  const auto state = session.getRehearsalState();
  const auto rows = state.getProperty("groups", {});
  const auto signature = juce::JSON::toString(rows);
  if (signature != groupsSignature) {
    groupsSignature = signature;
    groups.clear(); groupIds.clear(); groupSelector.clear(juce::dontSendNotification);
    if (const auto *array = rows.getArray()) for (const auto &g : *array) {
      groups.add(g); groupIds.add(g.getProperty("id", {}).toString());
      const auto names = g.getProperty("names", {});
      groupSelector.addItem(groupName(g.getProperty("name", {}).toString()) + " (" + juce::String(names.size()) + ")", groupIds.size());
    }
  }
  const auto activeGroup = state.getProperty("group", {}).toString();
  const int selected = groupIds.indexOf(activeGroup);
  groupSelector.setSelectedId(selected + 1, juce::dontSendNotification);
  const bool connected = !session.isRemote() || session.isConnected();
  const bool supported = session.supportsRehearsal();
  const bool enabled = connected && supported && session.hasLoadedSong();
  groupSelector.setEnabled(enabled && !groups.isEmpty());
  groupStrip.setEnabled(enabled && selected >= 0);
  restStrip.setEnabled(enabled && selected >= 0);
  normal.setEnabled(enabled && selected >= 0);
  if (!groupStrip.isMouseButtonDown(true)) groupStrip.setVolume(session.getRehearsalLevel(true));
  if (!restStrip.isMouseButtonDown(true)) restStrip.setVolume(session.getRehearsalLevel(false));
  juce::StringArray names;
  if (selected >= 0) {
    const auto values = groups[selected].getProperty("names", {});
    if (auto *array = values.getArray()) for (const auto &v : *array) names.add(v.toString());
  }
  members.setText(names.joinIntoString(", "), juce::dontSendNotification);
  members.setTooltip(names.joinIntoString("\n"));
  juce::String message;
  if (!connected) message = L10n::tr("Reconectando. El balance se controla en el servidor.", "Reconnecting. The balance is controlled on the server.");
  else if (!supported) message = L10n::tr("Actualiza el servidor para usar Ensayo por grupos.", "Update the server to use rehearsal groups.");
  else if (!session.hasLoadedSong()) message = L10n::tr("Selecciona una canción para ensayar.", "Select a song to rehearse.");
  else if (groups.isEmpty()) message = L10n::tr("Esta canción no tiene pistas instrumentales disponibles.", "This song has no available instrument tracks.");
  else if (selected < 0) message = L10n::tr("Mezcla normal · elige un grupo para destacarlo.", "Normal mix · choose a group to highlight it.");
  else message = session.isRemote()
      ? L10n::tr("Balance del servidor · compartido con los demás clientes.", "Server balance · shared with other clients.")
      : L10n::tr("Balance temporal · click, guía, Mute y Solo conservan sus ajustes.", "Temporary balance · click, cues, Mute and Solo retain their settings.");
  status.setText(message, juce::dontSendNotification);
  levels.setText(L10n::tr("Grupo ", "Group ") + juce::String(juce::roundToInt(session.getRehearsalLevel(true) * 100)) + "%    ·    " +
                 L10n::tr("Resto ", "Rest ") + juce::String(juce::roundToInt(session.getRehearsalLevel(false) * 100)) + "%", juce::dontSendNotification);
}
std::pair<float, float> RehearsalPanel::meter(bool selected) const {
  float peak = 0.0f;
  const auto state = session.getRehearsalState();
  const auto active = state.getProperty("group", {}).toString();
  if (active.isEmpty()) return {};
  // The session exposes levels by stable IDs so remote ordering stays identical.
  for (const auto &g : groups) {
    if ((g.getProperty("id", {}).toString() == active) != selected) continue;
    const auto ids = g.getProperty("stemIds", {});
    if (const auto *array = ids.getArray()) for (const auto &id : *array)
      peak = juce::jmax(peak, session.getRehearsalStemLevel(id.toString()));
  }
  return {peak, peak};
}
void RehearsalPanel::paint(juce::Graphics &g) { g.fillAll(UITheme::panelDark); }
void RehearsalPanel::resized() {
  auto panelArea = getLocalBounds();
  auto heading = panelArea.removeFromTop(52).reduced(14, 4);
  normal.setBounds(heading.removeFromRight(140));
  heading.removeFromRight(8);
  groupLabel.setBounds(heading);
  viewport.setBounds(panelArea);
  const int w = juce::jmax(1, getWidth() - 12);
  const int h = juce::jmax(340, viewport.getHeight());
  content.setSize(w, h);
  auto area = content.getLocalBounds().reduced(14, 8);
  groupSelector.setBounds(area.removeFromTop(48));
  members.setBounds(area.removeFromTop(26));
  status.setBounds(area.removeFromBottom(42));
  levels.setBounds(area.removeFromBottom(24));
  auto faders = area.reduced(0, 6);
  const int cardWidth = juce::jmin(160, (faders.getWidth() - 12) / 2);
  faders = faders.withSizeKeepingCentre(cardWidth * 2 + 12, faders.getHeight());
  groupStrip.setBounds(faders.removeFromLeft(cardWidth));
  faders.removeFromLeft(12);
  restStrip.setBounds(faders);
}
