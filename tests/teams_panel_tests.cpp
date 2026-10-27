#include "ui/panels/TeamsPanel.h"
#include "data/TeamsClient.h"
#include "data/TeamsAvailability.h"
#include "ui/components/AppModal.h"
#include "data/ApplicationDataPaths.h"
#include "data/Localization.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

juce::var json(const char *source) { return juce::JSON::parse(juce::String::fromUTF8(source)); }

void drainMessages() {
  juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
}

std::vector<juce::Button *> buttons(juce::Component &component,
                                    const juce::String &label = {}) {
  std::vector<juce::Button *> result;
  if (auto *button = dynamic_cast<juce::Button *>(&component);
      button != nullptr && (label.isEmpty() || button->getButtonText() == label))
    result.push_back(button);
  for (auto *child : component.getChildren()) {
    const auto nested = buttons(*child, label);
    result.insert(result.end(), nested.begin(), nested.end());
  }
  return result;
}

juce::TextEditor *editorTitled(juce::Component &component, const juce::String &title) {
  if (component.getTitle() == title)
    if (auto *editor = dynamic_cast<juce::TextEditor *>(&component)) return editor;
  for (auto *child : component.getChildren())
    if (auto *editor = editorTitled(*child, title)) return editor;
  return nullptr;
}

void press(juce::Component &root, const juce::String &label, size_t index = 0) {
  const auto found = buttons(root, label);
  expect(index < found.size(), "expected action is present");
  expect(found[index]->isEnabled(), "requested action is enabled");
  expect(static_cast<bool>(found[index]->onClick), "requested action has a callback");
  found[index]->onClick();
}

juce::var overview(bool canManage = true) {
  auto data = json(R"({"teams":[
    {"id":"team-a","name":"Alabanza","role":"owner","ownerUid":"owner"},
    {"id":"team-b","name":"Jóvenes","role":"member","ownerUid":"other","ownerEmail":"other@example.test"}],
    "capabilities":{"canManageSharedCloud":true}})");
  data.getProperty("capabilities", {}).getDynamicObject()->setProperty("canManageSharedCloud", canManage);
  return data;
}

juce::var details(const char *role = "owner", const char *teamId = "team-a") {
  auto data = json(R"({"team":{"id":"team-a","name":"Alabanza","role":"owner","ownerUid":"owner"},
    "members":[{"uid":"owner","email":"lider@example.test","role":"owner"},
               {"uid":"player","email":"musico@example.test","role":"member"}],
    "songs":[{"songId":"shared-song","title":"Como en el cielo","artist":"Equipo Alabanza",
              "revision":3,"bpm":135,"key":"C","stemCount":10,"localState":"shared"},
             {"songId":"downloaded-song","title":"Tu eres mi todo","artist":"Equipo Alabanza",
              "revision":2,"localState":"downloaded"}],
    "setlists":[{"id":"obsolete-setlist","name":"Not shown","revision":1}],
    "invitations":[{"id":"obsolete-invitation","email":"Not shown"}]})");
  data.getProperty("team", {}).getDynamicObject()->setProperty("role", role);
  data.getProperty("team", {}).getDynamicObject()->setProperty("id", teamId);
  return data;
}

using Action = std::pair<juce::String, juce::var>;
} // namespace

struct TeamsPanelTestAccess {
  static juce::Component &content(TeamsPanel &panel) { return panel.content; }
  static juce::Viewport &viewport(TeamsPanel &panel) { return panel.viewport; }
  static AppModal *confirmation(TeamsPanel &panel) { return panel.confirmation.get(); }
  static bool managementAllowed(const TeamsPanel &panel) { return panel.canManageMembers(); }
  static void dispatch(TeamsPanel &panel, const juce::String &operation, juce::var data = {}) {
    panel.dispatch(operation, std::move(data));
  }
  static void refresh(TeamsPanel &panel) { panel.refreshButton.onClick(); }

  static void checkLayout(TeamsPanel &panel) {
    expect(panel.getLocalBounds().contains(panel.closeButton.getBounds()) &&
               panel.closeButton.getWidth() == 44 && panel.closeButton.getHeight() == 44,
           "close target remains inside the panel and touch sized");
    expect(panel.getLocalBounds().contains(panel.refreshButton.getBounds()) &&
               !panel.closeButton.getBounds().intersects(panel.refreshButton.getBounds()),
           "header actions remain separate");
    expect(panel.getLocalBounds().contains(panel.viewport.getBounds()) &&
               panel.viewport.getY() >= panel.subtitle.getBottom(),
           "content begins beneath the fixed header and local help");
    if (panel.content.getWidth() > panel.viewport.getMaximumVisibleWidth() ||
        panel.viewport.getHorizontalScrollBar().isVisible())
      std::cerr << "Teams layout diagnostic: panel=" << panel.getWidth() << "x" << panel.getHeight()
                << " content=" << panel.content.getWidth() << "x" << panel.content.getHeight()
                << " viewport=" << panel.viewport.getWidth() << "x" << panel.viewport.getHeight()
                << " maximumVisible=" << panel.viewport.getMaximumVisibleWidth()
                << " horizontal=" << panel.viewport.getHorizontalScrollBar().isVisible() << '\n';
    expect(panel.content.getWidth() <= panel.viewport.getMaximumVisibleWidth() &&
               !panel.viewport.getHorizontalScrollBar().isVisible(),
           "team material requires no horizontal scrolling");
    for (auto *card : panel.content.getChildren()) {
      expect(panel.content.getLocalBounds().contains(card->getBounds()),
             "each team card fits the scrolling content");
      for (auto *item : card->getChildren()) {
        if (item->getBounds().isEmpty()) continue;
        expect(card->getLocalBounds().contains(item->getBounds()),
               "card titles, help and rows do not clip");
        for (auto *control : item->getChildren()) {
          if (dynamic_cast<juce::Button *>(control) != nullptr ||
              dynamic_cast<juce::ComboBox *>(control) != nullptr ||
              dynamic_cast<juce::Label *>(control) != nullptr)
            expect(item->getLocalBounds().contains(control->getBounds()),
                   "row controls remain within their row on phones and desktop");
        }
      }
    }
    for (const auto *button : buttons(panel.content))
      expect(button->getHeight() >= 44 && button->getWidth() >= 44,
             "team actions preserve touch targets");
    panel.viewport.setViewPosition(0, 100000);
    auto *last = panel.content.getChildComponent(panel.content.getNumChildComponents() - 1);
    const auto visibleLast = panel.viewport.getLocalArea(last, last->getLocalBounds());
    expect(visibleLast.getBottom() <= panel.viewport.getHeight(),
           "the final team card can be reached by scrolling");
  }
};

namespace {
void testEnabledClientRequiresSession() {
  expect(teamsFileSharingAvailable, "shared Cloud support must not be globally paused");
  for (const auto *operation : {"list", "download"}) {
    TeamsClient client;
    bool returned = false;
    CloudTransferError result;
    client.request({}, operation, {}, [&](juce::var, CloudTransferError error) {
      result = std::move(error);
      returned = true;
    });
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    while (!returned && juce::Time::getMillisecondCounterHiRes() < deadline)
      drainMessages();
    expect(returned, "invalid Teams requests complete without a network dependency");
    expect(result.httpCode == 0 && result.code == "INVALID_REQUEST",
           "enabled Teams still rejects missing credentials before networking");
  }
}

void expectAdministrationOnly(TeamsPanel &panel) {
  for (const auto &label : {L10n::tr("Compartir", "Share"), L10n::tr("Publicar", "Publish"),
                           L10n::tr("Invitar", "Invite"), L10n::tr("Aceptar", "Accept"),
                           L10n::tr("Abrir", "Open"), L10n::tr("Preparar", "Prepare"),
                           L10n::tr("Descargar", "Download"), L10n::tr("Actualizar", "Update"),
                           L10n::tr("Abrir mi biblioteca", "Open my library"), L10n::tr("Crear", "Create")})
    expect(buttons(TeamsPanelTestAccess::content(panel), label).empty(),
           "Teams settings contain only user access actions");
  expect(editorTitled(panel, L10n::tr("Nombre del equipo", "Team name")) == nullptr,
         "there is no named team creation form");
  for (auto *child : panel.getChildren())
    expect(dynamic_cast<juce::ComboBox *>(child) == nullptr,
           "user access settings have no Cloud source selector");
}

void testOwnCloudSelectionAndCapability() {
  TeamsPanel panel;
  std::vector<Action> actions;
  panel.onAction = [&](const auto &operation, auto data) { actions.emplace_back(operation, data); };
  panel.setOverview(json(R"({"teams":[]})"));
  expect(panel.selectedTeamId().isEmpty() && actions.empty(), "no feature means no roster request");
  expectAdministrationOnly(panel);
  panel.setBusy(true);
  panel.setOverview(overview());
  expect(panel.selectedTeamId() == "team-a" && actions.size() == 1 &&
             actions.back().first == "details" &&
             actions.back().second.getProperty("teamId", {}).toString() == "team-a",
         "current server capability loads the owner's default Cloud roster during list completion");
  panel.setBusy(false);
  actions.clear();
  panel.setOverview(overview());
  expect(actions.empty() && panel.selectedTeamId() == "team-a", "refresh preserves own Cloud routing");
  panel.selectTeam("unknown-team");
  expect(actions.empty() && panel.selectedTeamId() == "team-a", "member Clouds cannot become administration targets");
  panel.setDetails(details("member", "team-b"));
  expect(!TeamsPanelTestAccess::managementAllowed(panel), "late member data cannot unlock management");
  panel.setDetails(details());
  expect(TeamsPanelTestAccess::managementAllowed(panel), "owner details and live capability unlock roster");
  TeamsPanelTestAccess::refresh(panel);
  expect(actions.size() == 1 && actions.back().first == "list", "refresh requests current access data");
  actions.clear();
  panel.setOverview(overview(false));
  expect(panel.selectedTeamId() == "team-b" &&
             buttons(panel, L10n::tr("Agregar", "Add")).empty(),
         "revoked subscription immediately clears the owner roster and does not fetch a member roster");
  auto malformed = overview();
  malformed.getProperty("capabilities", {}).getDynamicObject()->setProperty("canManageSharedCloud", "true");
  panel.setOverview(malformed);
  expect(!TeamsPanelTestAccess::managementAllowed(panel), "management capability must be a real server boolean");
  panel.setOverview(json(R"({"teams":[{"id":"member-cloud","role":"member"}],
                            "capabilities":{"canManageSharedCloud":true}})"));
  expect(panel.selectedTeamId() == "member-cloud" && !TeamsPanelTestAccess::managementAllowed(panel), "members can leave but cannot administer");
}

void testResponsiveAccessAndBusyState() {
  TeamsPanel panel;
  panel.setVisible(true);
  for (const auto size : {juce::Point<int>(1000, 780), juce::Point<int>(390, 844),
                          juce::Point<int>(320, 667)}) {
    panel.setSize(size.x, size.y);
    for (const bool enabled : {false, true}) {
      panel.setOverview(overview(enabled));
      if (enabled) panel.setDetails(details());
      TeamsPanelTestAccess::checkLayout(panel);
      expectAdministrationOnly(panel);
      expect(buttons(panel, L10n::tr("Agregar", "Add")).size() == (enabled ? 1u : 0u),
             "only an enabled owner gets the add-user field");
      expect(buttons(panel, L10n::tr("Quitar", "Remove")).size() == (enabled ? 1u : 0u),
             "an enabled owner can remove a member but never the owner");
    }
  }
  panel.setOverview(overview());
  panel.setDetails(details());
  std::vector<Action> actions;
  panel.onAction = [&](const auto &operation, auto data) { actions.emplace_back(operation, data); };
  auto *email = editorTitled(panel, L10n::tr("Correo del usuario", "User email"));
  email->setText("musico@example.test");
  drainMessages();
  panel.setBusy(true);
  for (auto *button : buttons(TeamsPanelTestAccess::content(panel)))
    expect(!button->isEnabled(), "in-flight requests disable duplicate access operations");
  const auto addButtons = buttons(panel, L10n::tr("Agregar", "Add"));
  addButtons.front()->onClick();
  expect(actions.empty(), "busy protects even a direct add callback");
  panel.setBusy(false);
  addButtons.front()->onClick();
  expect(actions.size() == 1 && actions.back().first == "add-member", "request completion re-enables adding a user");
}

void testMemberAddition() {
  TeamsPanel panel;
  panel.setOverview(overview());
  panel.setDetails(details());
  std::vector<Action> actions;
  panel.onAction = [&](const auto &operation, auto data) { actions.emplace_back(operation, data); };
  auto *email = editorTitled(panel, L10n::tr("Correo del usuario", "User email"));
  expect(email != nullptr && email->getKeyboardType() == juce::TextInputTarget::emailAddressKeyboard,
         "user email uses the mobile email keyboard");
  expect(email->getJustificationType() == juce::Justification::centredLeft,
         "user email remains centered in its touch sized field");
  email->setText("  Musico@Example.Test  ");
  drainMessages();
  press(panel, L10n::tr("Agregar", "Add"));
  expect(actions.size() == 1 && actions.back().first == "add-member" &&
             actions.back().second.getProperty("email", {}).toString() == "musico@example.test" &&
             actions.back().second.getProperty("teamId", {}).toString() == "team-a" &&
             !actions.back().second.hasProperty("role"),
         "adding Cloud access sends canonical email without invitations or selectable roles");
  panel.setDetails(details());
  email = editorTitled(panel, L10n::tr("Correo del usuario", "User email"));
  expect(email->getText() == "  Musico@Example.Test  ", "refresh retains the unfinished email field");
  for (const auto *invalid : {"invalid address", "@example.test", "user@@example.test", "user@example."}) {
    email->setText(invalid);
    drainMessages();
    actions.clear();
    press(panel, L10n::tr("Agregar", "Add"));
    expect(actions.empty(), "invalid email cannot add a user");
  }
}

void testMemberLeave() {
  TeamsPanel panel;
  panel.setVisible(true);
  panel.setSize(390, 844);
  panel.setOverview(json(R"({"teams":[{"id":"team-a","role":"member","ownerEmail":"owner@test.com"}],"capabilities":{"canManageSharedCloud":false}})"));
  panel.setDetails(details("member"));
  std::vector<Action> actions;
  panel.onAction = [&](const auto &op, auto data) { actions.emplace_back(op, data); };
  expect(buttons(panel, L10n::tr("Agregar", "Add")).empty(), "member has no owner actions");
  press(panel, L10n::tr("Salir del Team", "Leave Team"));
  expect(actions.empty(), "leaving requires destructive confirmation");
  press(*TeamsPanelTestAccess::confirmation(panel), L10n::tr("Cancelar", "Cancel"));
  drainMessages();
  expect(actions.empty(), "cancel keeps membership");
  press(panel, L10n::tr("Salir del Team", "Leave Team"));
  const auto capture = juce::SystemStats::getEnvironmentVariable("PW_TEAMS_CAPTURE_DIR", {});
  if (capture.isNotEmpty()) {
    const juce::File dir(capture); dir.createDirectory();
    dir.getChildFile("leave-team-phone.png").deleteFile();
    auto out = dir.getChildFile("leave-team-phone.png").createOutputStream();
    if (out) juce::PNGImageFormat().writeImageToStream(panel.createComponentSnapshot(panel.getLocalBounds()), *out);
  }
  press(*TeamsPanelTestAccess::confirmation(panel), L10n::tr("Salir y eliminar", "Leave and delete"));
  drainMessages();
  expect(actions.size() == 1 && actions[0].first == "leave" &&
      actions[0].second.getProperty("teamId", {}).toString() == "team-a", "confirmed self leave routes exact source");
}

void testRemovalAndForbiddenActions() {
  TeamsPanel panel;
  panel.setVisible(true);
  panel.setOverview(overview());
  panel.setDetails(details());
  std::vector<Action> actions;
  panel.onAction = [&](const auto &operation, auto data) { actions.emplace_back(operation, data); };
  press(panel, L10n::tr("Quitar", "Remove"));
  expect(actions.empty() && TeamsPanelTestAccess::confirmation(panel) != nullptr,
         "removing Cloud access requires confirmation");
  press(*TeamsPanelTestAccess::confirmation(panel), L10n::tr("Cancelar", "Cancel"));
  drainMessages();
  expect(actions.empty() && TeamsPanelTestAccess::confirmation(panel) == nullptr, "cancel retains Cloud access");
  press(panel, L10n::tr("Quitar", "Remove"));
  press(*TeamsPanelTestAccess::confirmation(panel), L10n::tr("Quitar", "Remove"));
  drainMessages();
  expect(actions.size() == 1 && actions.back().first == "remove-member" &&
             actions.back().second.getProperty("uid", {}).toString() == "player",
         "confirmed removal identifies the selected member");
  actions.clear();
  press(panel, L10n::tr("Quitar", "Remove"));
  panel.setOverview(overview(false));
  press(*TeamsPanelTestAccess::confirmation(panel), L10n::tr("Quitar", "Remove"));
  drainMessages();
  expect(actions.empty(), "revoked subscription also blocks an already open removal confirmation");
  panel.setOverview(overview());
  panel.setDetails(details("member"));
  actions.clear();
  for (const auto *operation : {"create", "share-local-song", "share-local-setlist", "prepare-song", "prepare-setlist",
                                 "invite", "accept", "open-song", "open-library", "add-member", "remove-member"})
    TeamsPanelTestAccess::dispatch(panel, operation);
  expect(actions.empty(), "song operations and non-owner access mutations cannot dispatch");
  expectAdministrationOnly(panel);
}
} // namespace

int main() {
#if JUCE_LINUX
  if (std::getenv("DISPLAY") == nullptr) return 77;
#endif
  if (juce::SystemStats::getEnvironmentVariable("PLAYWORSHIP_CREATION_TEST_ROOT", {}).isEmpty()) return 77;
  try {
    (void)ApplicationDataPaths::isolatedTestRoot();
    juce::ScopedJuceInitialiser_GUI gui;
    applyTheme(ThemePreset::Spotify);
    testEnabledClientRequiresSession();
    testOwnCloudSelectionAndCapability();
    testResponsiveAccessAndBusyState();
    testMemberAddition();
    testMemberLeave();
    testRemovalAndForbiddenActions();
    std::cout << "Teams settings: owner-only subscribed access administration passed\n";
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
