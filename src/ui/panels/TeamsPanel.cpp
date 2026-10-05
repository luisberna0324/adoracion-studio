#include "TeamsPanel.h"
#include "../../data/Localization.h"
#include "../components/AppModal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

namespace {
juce::var object(std::initializer_list<std::pair<juce::String, juce::var>> values) {
  auto *result = new juce::DynamicObject();
  for (const auto &value : values)
    result->setProperty(value.first, value.second);
  return juce::var(result);
}

juce::String text(const juce::var &value, const char *key) {
  return value.getProperty(key, {}).toString();
}

juce::String roleTitle(const juce::String &role) {
  if (role == "owner") return L10n::tr("Propietario", "Owner");
  return L10n::tr("Miembro", "Member");
}

juce::String titleOf(const juce::var &value, const char *key, juce::String fallback) {
  const auto result = text(value, key).trim();
  return result.isNotEmpty() ? result : fallback;
}

void setupLabel(juce::Label &label, juce::String value, float fontSize,
                bool primary = false, AppFontWeight weight = AppFontWeight::Regular) {
  label.setText(std::move(value), juce::dontSendNotification);
  label.setFont(makeAppFont(fontSize, weight));
  label.setColour(juce::Label::textColourId,
                  primary ? UITheme::textPrimary : UITheme::textSecondary);
  label.setBorderSize({0, 0, 0, 0});
  label.setMinimumHorizontalScale(1.0f);
  label.setInterceptsMouseClicks(false, false);
}

} // namespace

class TeamsPanel::ActionButton final : public juce::Button {
public:
  ActionButton(juce::String label, bool isPrimary = false, bool isDestructive = false)
      : juce::Button(label), primary(isPrimary), destructive(isDestructive) {
    setButtonText(label);
    setTitle(label);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }

  void paintButton(juce::Graphics &g, bool over, bool down) override {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    auto fill = primary ? UITheme::accent : UITheme::cardBgHover;
    if (over || hasKeyboardFocus(true)) fill = fill.brighter(0.10f);
    if (down) fill = fill.darker(0.10f);
    if (!isEnabled()) fill = UITheme::cardBgHover;
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 11.0f);
    g.setColour(hasKeyboardFocus(true) ? UITheme::accent : UITheme::cardBorder);
    if (!primary || !isEnabled() || hasKeyboardFocus(true))
      g.drawRoundedRectangle(bounds.reduced(0.7f), 10.5f,
                             hasKeyboardFocus(true) ? 2.0f : 1.0f);
    g.setColour(!isEnabled() ? UITheme::textSecondary
                : destructive ? AppColours::accentDestructive
                : primary ? UITheme::onAccent : UITheme::textPrimary);
    g.setFont(makeAppFont(13.5f, AppFontWeight::SemiBold));
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(8, 3),
                     juce::Justification::centred, 2);
  }

private:
  bool primary, destructive;
};

class TeamsPanel::ItemRow final : public juce::Component {
public:
  ItemRow(juce::String titleText, juce::String detailText,
          juce::String actionText, std::function<void()> action,
          bool destructive = false) {
    setupLabel(title, std::move(titleText), 15.0f, true, AppFontWeight::Medium);
    setupLabel(detail, std::move(detailText), 12.5f);
    addAndMakeVisible(title);
    addAndMakeVisible(detail);
    if (actionText.isNotEmpty()) {
      button = std::make_unique<ActionButton>(std::move(actionText), false,
                                               destructive);
      button->onClick = std::move(action);
      addAndMakeVisible(*button);
    }
  }

  void paint(juce::Graphics &g) override {
    g.setColour(UITheme::dividerSoft);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
  }

  void resized() override {
    auto bounds = getLocalBounds().reduced(0, 10);
    if (button) {
      const auto buttonWidth = getWidth() < 340 ? 100 : 120;
      button->setBounds(bounds.removeFromRight(buttonWidth)
                            .withSizeKeepingCentre(buttonWidth, 44));
      bounds.removeFromRight(12);
    }
    title.setBounds(bounds.removeFromTop(26));
    detail.setBounds(bounds);
  }

private:
  juce::Label title, detail;
  std::unique_ptr<ActionButton> button;
};

class TeamsPanel::FormRow final : public juce::Component {
public:
  FormRow(std::unique_ptr<juce::Component> fieldComponent,
          std::unique_ptr<ActionButton> actionButton)
      : field(std::move(fieldComponent)), button(std::move(actionButton)) {
    addAndMakeVisible(*this->field);
    addAndMakeVisible(*this->button);
  }

  void resized() override {
    auto bounds = getLocalBounds().reduced(0, 5);
    const auto buttonWidth = getWidth() < 340 ? 104 : 124;
    button->setBounds(bounds.removeFromRight(buttonWidth));
    bounds.removeFromRight(10);
    field->setBounds(bounds);
  }

private:
  std::unique_ptr<juce::Component> field;
  std::unique_ptr<ActionButton> button;
};

class TeamsPanel::Card final : public juce::Component {
public:
  Card(juce::String titleText, juce::String helpText) {
    setupLabel(title, std::move(titleText), 16.0f, true, AppFontWeight::SemiBold);
    setupLabel(help, std::move(helpText), 12.5f);
    help.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(title);
    addAndMakeVisible(help);
  }

  void add(std::unique_ptr<juce::Component> component, int height) {
    addAndMakeVisible(*component);
    items.emplace_back(std::move(component), height);
  }

  void addHelp(juce::String value, int height = 44) {
    auto label = std::make_unique<juce::Label>();
    setupLabel(*label, std::move(value), 12.5f);
    label->setJustificationType(juce::Justification::centredLeft);
    add(std::move(label), height);
  }

  int preferredHeight(int width) const {
    return 56 + helpHeight(width) + std::accumulate(
        items.begin(), items.end(), 0,
        [](int height, const auto &item) { return height + item.second; });
  }

  void paint(juce::Graphics &g) override {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(UITheme::cardBg);
    g.fillRoundedRectangle(bounds, 16.0f);
    g.setColour(UITheme::cardBorder.withAlpha(0.55f));
    g.drawRoundedRectangle(bounds, 16.0f, 1.0f);
  }

  void resized() override {
    auto bounds = getLocalBounds().reduced(16, 12);
    title.setBounds(bounds.removeFromTop(27));
    help.setBounds(bounds.removeFromTop(helpHeight(getWidth())));
    bounds.removeFromTop(5);
    for (auto &item : items)
      item.first->setBounds(bounds.removeFromTop(item.second));
  }

private:
  int helpHeight(int width) const {
    if (help.getText().isEmpty()) return 0;
    // One predictable line height keeps the help at its intended size on phones.
    return juce::jmax(20, juce::roundToInt(std::ceil(
        static_cast<double>(help.getText().length()) /
        juce::jmax(18, (width - 32) / 7))) * 18 + 6);
  }
  juce::Label title, help;
  std::vector<std::pair<std::unique_ptr<juce::Component>, int>> items;
};

TeamsPanel::TeamsPanel() {
  setTitle("Teams");
  setWantsKeyboardFocus(true);
  setFocusContainerType(FocusContainerType::keyboardFocusContainer);
  setupLabel(heading, "Teams", 20.0f, true, AppFontWeight::SemiBold);
  setupLabel(subtitle,
      L10n::tr(u8"Consulta tu acceso al Team o administra los usuarios de tu nube.",
               "View your Team access or manage the users of your Cloud."), 13.0f);
  setupLabel(status, {}, 12.5f);
  status.setJustificationType(juce::Justification::topLeft);
  for (auto *component : std::array<juce::Component *, 6>{
           &heading, &subtitle, &status, &closeButton, &refreshButton, &viewport})
    addAndMakeVisible(component);

  closeButton.setTitle(L10n::tr("Cerrar Teams", "Close Teams"));
  closeButton.setTooltip(closeButton.getTitle());
  closeButton.onClick = [this] { if (onClose) onClose(); };
  refreshButton.setTitle(L10n::tr("Actualizar usuarios", "Refresh users"));
  refreshButton.setTooltip(refreshButton.getTitle());
  refreshButton.onClick = [this] { dispatch("list"); };
  viewport.setViewedComponent(&content, false);
  viewport.setScrollBarsShown(true, false);
  viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::nonHover);
  viewport.setWantsKeyboardFocus(false);
  rebuildContent();
  setSize(640, 740);
  if (useTouchOptimisedControls()) startTimerHz(10);
}

TeamsPanel::~TeamsPanel() {
  stopTimer();
  confirmation.reset();
  viewport.setViewedComponent(nullptr, false);
}

void TeamsPanel::paint(juce::Graphics &g) {
  g.fillAll(UITheme::appBg);
}

void TeamsPanel::resized() {
  const int inset = getWidth() < 460 ? 16 : 24;
  auto bounds = getLocalBounds();
  if (useTouchOptimisedControls()) {
    if (auto *display = juce::Desktop::getInstance().getDisplays()
                            .getDisplayForRect(getScreenBounds())) {
      lastSafeInsets = display->safeAreaInsets;
      lastSafeInsets.setBottom(juce::jmax(lastSafeInsets.getBottom(),
                                         display->keyboardInsets.getBottom()));
      bounds = lastSafeInsets.subtractedFrom(bounds);
    }
  }
  bounds.reduce(inset, 12);
  auto top = bounds.removeFromTop(44);
  closeButton.setBounds(top.removeFromRight(44));
  top.removeFromRight(8);
  refreshButton.setBounds(top.removeFromRight(44));
  top.removeFromRight(12);
  heading.setBounds(top);
  bounds.removeFromTop(4);
  subtitle.setBounds(bounds.removeFromTop(getWidth() < 400 ? 40 : 28));
  bounds.removeFromTop(8);
  status.setVisible(statusText.isNotEmpty() || busy);
  if (status.isVisible()) {
    status.setBounds(bounds.removeFromTop(getWidth() < 460 ? 52 : 40));
    bounds.removeFromTop(6);
  }
  viewport.setBounds(bounds);
  layoutContent();
}

void TeamsPanel::timerCallback() {
  auto *display = juce::Desktop::getInstance().getDisplays()
                      .getDisplayForRect(getScreenBounds());
  if (display == nullptr) return;
  auto safeInsets = display->safeAreaInsets;
  safeInsets.setBottom(juce::jmax(safeInsets.getBottom(),
                                display->keyboardInsets.getBottom()));
  if (safeInsets == lastSafeInsets) return;
  resized();
  if (display->keyboardInsets.getBottom() <= 0) return;
  auto *focused = juce::Component::getCurrentlyFocusedComponent();
  if (focused == nullptr || !content.isParentOf(focused)) return;
  auto *field = dynamic_cast<juce::TextEditor *>(focused);
  if (field == nullptr) field = focused->findParentComponentOfClass<juce::TextEditor>();
  if (field == nullptr) return;
  const auto fieldBounds = viewport.getLocalArea(field, field->getLocalBounds());
  const int margin = 8;
  const int shift = fieldBounds.getBottom() > viewport.getHeight() - margin
      ? fieldBounds.getBottom() - viewport.getHeight() + margin
      : fieldBounds.getY() < margin ? fieldBounds.getY() - margin : 0;
  if (shift != 0)
    viewport.setViewPosition(0, viewport.getViewPositionY() + shift);
}

bool TeamsPanel::keyPressed(const juce::KeyPress &key) {
  if (key == juce::KeyPress::escapeKey) {
    if (onClose) onClose();
    return true;
  }
  return false;
}

void TeamsPanel::setOverview(juce::var data) {
  overview = data.clone();
  const auto memberships = data.getProperty("memberships", {});
  if (const auto *items = memberships.getArray()) {
    auto clouds = overview.getProperty("teams", {});
    if (!clouds.isArray()) clouds = juce::var(juce::Array<juce::var>());
    for (const auto &member : *items) {
      bool exists = false;
      for (const auto &cloud : *clouds.getArray()) if (text(cloud, "id") == text(member, "id")) exists = true;
      if (!exists) clouds.append(member);
    }
    overview.getDynamicObject()->setProperty("teams", clouds);
  }
  juce::String ownCloudId;
  if (hasManagementCapability()) {
    const auto clouds = overview.getProperty("teams", {});
    if (const auto *sources = clouds.getArray())
      for (const auto &source : *sources)
        if (text(source, "role") == "owner" && text(source, "id").isNotEmpty()) {
          ownCloudId = text(source, "id");
          break;
        }
  }
  if (ownCloudId.isEmpty()) {
    const auto clouds = overview.getProperty("teams", {});
    if (const auto *sources = clouds.getArray())
      for (const auto &source : *sources)
        if (text(source, "role") == "member") { ownCloudId = text(source, "id"); break; }
  }
  if (ownCloudId != teamId) selectTeam(ownCloudId);
  else rebuildContent();
  resized();
}

void TeamsPanel::setDetails(juce::var data) {
  const auto incomingId = text(data.getProperty("team", {}), "id");
  if (incomingId.isNotEmpty() && incomingId != teamId) return;
  details = std::move(data);
  rebuildContent();
}

void TeamsPanel::selectTeam(juce::String newId) {
  if (newId.isNotEmpty()) {
    bool isOwnCloud = false;
    const auto clouds = overview.getProperty("teams", {});
    if (const auto *sources = clouds.getArray())
        for (const auto &source : *sources)
          isOwnCloud = isOwnCloud ||
              (text(source, "id") == newId &&
               (text(source, "role") == "member" || (hasManagementCapability() && text(source, "role") == "owner")));
    if (!isOwnCloud) return;
  }
  if (newId == teamId) return;
  teamId = std::move(newId);
  details = juce::var();
  const auto clouds = overview.getProperty("teams", {});
  if (const auto *sources = clouds.getArray())
    for (const auto &source : *sources)
      if (text(source, "id") == teamId && text(source, "role") == "member")
        details = object({{"team", source}});
  rebuildContent();
  viewport.setViewPosition(0, 0);
  if (teamId.isNotEmpty() && text(details.getProperty("team", {}), "role") != "member") dispatch("details", object({{"teamId", teamId}}));
}

juce::String TeamsPanel::selectedTeamId() const { return teamId; }

void TeamsPanel::setBusy(bool newBusy) {
  busy = newBusy;
  status.setText(statusText.isNotEmpty() ? statusText
                  : busy ? L10n::tr("Conectando con Teams…", "Connecting to Teams…")
                         : juce::String(), juce::dontSendNotification);
  updateEnabledState();
  resized();
}

void TeamsPanel::setStatus(juce::String value, bool error) {
  statusText = std::move(value);
  statusIsError = error;
  status.setTooltip(statusText);
  status.setColour(juce::Label::textColourId,
                    error ? AppColours::accentDestructive : UITheme::textSecondary);
  status.setText(statusText.isNotEmpty() ? statusText
                  : busy ? L10n::tr("Conectando con Teams…", "Connecting to Teams…")
                         : juce::String(), juce::dontSendNotification);
  resized();
}

void TeamsPanel::updateEnabledState() {
  refreshButton.setEnabled(!busy);
  for (auto &card : cards) card->setEnabled(!busy);
}

void TeamsPanel::dispatch(const juce::String &operation, juce::var payload) {
  if ((busy && operation != "details") || !onAction) return;
  if ((operation == "add-member" || operation == "remove-member") && !canManageMembers()) return;
  if ((operation == "add-member" || operation == "remove-member") &&
      text(payload, "teamId") != teamId)
    return;
  if (operation != "list" && operation != "details" &&
      operation != "add-member" && operation != "remove-member" && operation != "leave")
    return;
  if (operation == "leave" && (text(details.getProperty("team", {}), "role") != "member" ||
      text(payload, "teamId") != teamId)) return;
  onAction(operation, std::move(payload));
}

bool TeamsPanel::hasManagementCapability() const {
  const auto enabled = overview.getProperty("capabilities", {})
                           .getProperty("canManageSharedCloud", false);
  return enabled.isBool() && static_cast<bool>(enabled);
}

bool TeamsPanel::canManageMembers() const {
  const auto team = details.getProperty("team", {});
  return hasManagementCapability() && teamId.isNotEmpty() &&
         text(team, "id") == teamId && text(team, "role") == "owner";
}

void TeamsPanel::confirmAction(juce::String title, juce::String message,
                               juce::String primaryLabel, juce::String operation,
                               juce::var payload, bool destructive) {
  if (busy) return;
  const juce::Component::SafePointer<TeamsPanel> safe(this);
  confirmation = std::make_unique<AppModal>(
      *this, destructive ? AppModal::Kind::Destructive : AppModal::Kind::Confirm,
      std::move(title), std::move(message), std::move(primaryLabel),
      L10n::tr("Cancelar", "Cancel"),
      [safe, operation, payload](bool accepted, const juce::String &) {
        if (safe && accepted) safe->dispatch(operation, payload);
      });
  confirmation->onClosed = [safe] { if (safe) safe->confirmation.reset(); };
  confirmation->show();
}

TeamsPanel::Card &TeamsPanel::addCard(juce::String title, juce::String help) {
  auto card = std::make_unique<Card>(std::move(title), std::move(help));
  content.addAndMakeVisible(*card);
  cards.push_back(std::move(card));
  return *cards.back();
}

void TeamsPanel::layoutContent() {
  // Setting the content height may reveal/hide the vertical scrollbar. Its
  // width must be included before positioning the final card controls.
  for (int pass = 0; pass < 3; ++pass) {
    const int width = juce::jmax(0, viewport.getMaximumVisibleWidth());
    const int cardWidth = juce::jmin(760, width);
    int y = 0;
    for (auto &card : cards) {
      const int height = card->preferredHeight(cardWidth);
      card->setBounds((width - cardWidth) / 2, y, cardWidth, height);
      y += height + 16;
    }
    content.setSize(width, juce::jmax(viewport.getHeight(), y));
    if (viewport.getMaximumVisibleWidth() == width) break;
  }
}

void TeamsPanel::rebuildContent() {
  const int scroll = viewport.getViewPositionY();
  cards.clear();
  const juce::Component::SafePointer<TeamsPanel> safe(this);
  const auto team = details.getProperty("team", {});
  if (canManageMembers()) {
    auto &members = addCard(L10n::tr("Usuarios con acceso", "Users with access"),
        L10n::tr(u8"Agrega usuarios con una cuenta de PlayWorship para que puedan ver y descargar tus canciones de la nube.",
                 "Add users with a PlayWorship account so they can view and download your Cloud songs."));
    const auto memberData = details.getProperty("members", {});
    if (const auto *items = memberData.getArray()) {
      for (const auto &member : *items) {
        const auto uid = text(member, "uid");
        const auto email = titleOf(member, "email", uid);
        const bool removable = uid.isNotEmpty() && text(member, "role") != "owner" &&
                               uid != text(team, "ownerUid");
        members.add(std::make_unique<ItemRow>(email, roleTitle(text(member, "role")),
            removable ? L10n::tr("Quitar", "Remove") : juce::String(),
            [safe, uid, email] {
              if (safe) safe->confirmAction(L10n::tr("Quitar usuario", "Remove user"),
                  L10n::tr(u8"Este usuario dejará de acceder a la nube compartida: ",
                           "This user will lose access to the shared Cloud: ") + email +
                  L10n::tr(u8". Se eliminarán sus canciones y descargas del Team, también de sus setlists, cuando sus dispositivos se conecten. Sus canciones personales se conservarán.",
                           ". Team songs and downloads will also be removed from their setlists when their devices reconnect. Personal songs will be kept."),
                  L10n::tr("Quitar", "Remove"), "remove-member",
                  object({{"teamId", safe->teamId}, {"uid", uid}}), true);
            }, true), 80);
      }
    }
    auto email = std::make_unique<juce::TextEditor>();
    auto *editor = email.get();
    email->setTitle(L10n::tr("Correo del usuario", "User email"));
    email->setTextToShowWhenEmpty("nombre@correo.com", UITheme::textSecondary);
    email->setText(memberEmail, false);
    email->setFont(makeAppFont(16.0f));
    email->setKeyboardType(juce::TextInputTarget::emailAddressKeyboard);
    email->setSelectAllWhenFocused(true);
    email->setViewportIgnoreDragFlag(true);
    applyTextEditorTheme(*email);
    email->setJustification(juce::Justification::centredLeft);
    email->onTextChange = [safe, editor] { if (safe) safe->memberEmail = editor->getText(); };
    auto add = std::make_unique<ActionButton>(L10n::tr("Agregar", "Add"), true);
    add->onClick = [safe] {
      if (!safe || !safe->canManageMembers()) return;
      const auto address = safe->memberEmail.trim().toLowerCase();
      const int at = address.indexOfChar('@');
      const auto domain = address.substring(at + 1);
      if (at <= 0 || at != address.lastIndexOfChar('@') ||
          address.containsAnyOf(" \r\n\t") || !domain.containsChar('.') ||
          domain.startsWithChar('.') || domain.endsWithChar('.')) {
        safe->setStatus(L10n::tr(u8"Escribe un correo válido de una cuenta de PlayWorship.",
                                "Enter a valid email for a PlayWorship account."), true);
        return;
      }
      safe->dispatch("add-member", object({{"teamId", safe->teamId}, {"email", address}}));
    };
    members.add(std::make_unique<FormRow>(std::move(email), std::move(add)), 54);
  } else if (text(team, "role") == "member" && text(team, "id") == teamId) {
    auto &card = addCard(L10n::tr("Mi Team", "My Team"), text(team, "ownerEmail"));
    card.add(std::make_unique<ItemRow>(L10n::tr("Nube compartida", "Shared cloud"),
        L10n::tr("Puedes descargar sus canciones.", "You can download its songs."),
        L10n::tr("Salir del Team", "Leave Team"), [safe] {
          if (safe) safe->confirmAction(L10n::tr("Salir del Team", "Leave Team"),
              L10n::tr(u8"Al salir del Team, se eliminarán de tus dispositivos sus canciones y descargas, también de tus setlists. Tus canciones personales se conservarán. Si estás reproduciendo, la limpieza se completará al detenerla; los dispositivos sin conexión se actualizarán al reconectarse.",
                       "Leaving removes this Team's songs and downloads from your devices and setlists. Personal songs stay. Cleanup waits until playback stops; offline devices update when they reconnect."),
              L10n::tr("Salir y eliminar", "Leave and delete"), "leave", object({{"teamId", safe->teamId}}), true);
        }, true), 90);
  } else {
    auto &card = addCard(L10n::tr("Usuarios de tu nube", "Your Cloud users"));
    card.addHelp(hasManagementCapability() && teamId.isNotEmpty() && text(team, "id") != teamId
        ? L10n::tr(u8"Cargando usuarios con acceso…", "Loading users with access…")
        : L10n::tr(u8"Esta configuración está disponible para el perfil de administración de Teams. Las canciones compartidas se consultan en Nube.",
                   "This setting is available to the Teams management profile. Shared songs are available in Cloud."), 84);
  }
  layoutContent();
  viewport.setViewPosition(0, scroll);
  updateEnabledState();
}
