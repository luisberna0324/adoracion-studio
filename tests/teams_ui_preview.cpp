// Isolated GUI harness: deterministic Teams metadata only, never a production API.
#include "ui/panels/TeamsPanel.h"
#include "ui/components/AppModal.h"
#include "data/ApplicationDataPaths.h"
#include "data/Localization.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
juce::var json(const char *source) { return juce::JSON::parse(juce::String::fromUTF8(source)); }
juce::var object(std::initializer_list<std::pair<juce::String, juce::var>> values) {
  auto *value = new juce::DynamicObject();
  for (const auto &entry : values) value->setProperty(entry.first, entry.second);
  return value;
}

std::vector<juce::Button *> buttons(juce::Component &component, const juce::String &label) {
  std::vector<juce::Button *> result;
  if (auto *button = dynamic_cast<juce::Button *>(&component);
      button != nullptr && button->getButtonText() == label)
    result.push_back(button);
  for (auto *child : component.getChildren()) {
    const auto nested = buttons(*child, label);
    result.insert(result.end(), nested.begin(), nested.end());
  }
  return result;
}

juce::TextEditor *editor(juce::Component &component, const juce::String &title) {
  if (component.getTitle() == title)
    if (auto *field = dynamic_cast<juce::TextEditor *>(&component)) return field;
  for (auto *child : component.getChildren())
    if (auto *field = editor(*child, title)) return field;
  return nullptr;
}
} // namespace

struct TeamsPanelTestAccess {
  static juce::Component &content(TeamsPanel &panel) { return panel.content; }
  static AppModal *confirmation(TeamsPanel &panel) { return panel.confirmation.get(); }
  static void scrollTop(TeamsPanel &panel) { panel.viewport.setViewPosition(0, 0); }
  static void revealCard(TeamsPanel &panel, const juce::String &heading) {
    for (auto *card : panel.content.getChildren())
      for (auto *child : card->getChildren())
        if (auto *label = dynamic_cast<juce::Label *>(child);
            label != nullptr && label->getText() == heading) {
          panel.viewport.setViewPosition(0, card->getY());
          return;
        }
  }
};

class TeamsPreviewApplication final : public juce::JUCEApplication,
                                      private juce::Timer {
public:
  const juce::String getApplicationName() override { return "PlayWorship Teams Preview"; }
  const juce::String getApplicationVersion() override { return "1.0"; }
  bool moreThanOneInstanceAllowed() override { return true; }

  void initialise(const juce::String &) override {
    try {
      (void)ApplicationDataPaths::isolatedTestRoot();
      AppSettings::setLanguage(AppLanguage::Spanish);
      applyTheme(ThemePreset::Spotify);
      window = std::make_unique<PreviewWindow>();
      window->onKey = [this](const juce::KeyPress &key) {
        if (key.getKeyCode() == juce::KeyPress::F1Key) { switchRole("owner"); return true; }
        if (key.getKeyCode() == juce::KeyPress::F2Key) { switchRole("member"); return true; }
        if (key.getKeyCode() == juce::KeyPress::F3Key) { resize(390, 844); return true; }
        if (key.getKeyCode() == juce::KeyPress::F4Key) { resize(1000, 780); return true; }
        return false;
      };
      panel().onClose = [this] { systemRequestedQuit(); };
      panel().onAction = [this](const auto &operation, auto data) {
        // Keep the same asynchronous boundary as the authenticated network client.
        juce::MessageManager::callAsync([this, operation, data] {
          if (window) handle(operation, data);
        });
      };
      outputPath = juce::SystemStats::getEnvironmentVariable("PW_TEAMS_PREVIEW_DIR", {});
      if (outputPath.isNotEmpty()) {
        if (!juce::File::isAbsolutePath(outputPath))
          throw std::runtime_error("PW_TEAMS_PREVIEW_DIR must be absolute");
        if (juce::File(outputPath).createDirectory().failed())
          throw std::runtime_error("Couldn't create the preview folder");
        refresh();
        startTimer(450);
      } else {
        // Interactive demo starts with an existing shared team; all data is in memory.
        memberAdded = true;
        refresh();
      }
      window->setVisible(true);
      std::cout << "Isolated Teams preview. F1 owner, F2 member, F3 phone, F4 desktop.\n";
    } catch (const std::exception &error) {
      std::cerr << "Preview failed: " << error.what() << '\n';
      setApplicationReturnValue(1);
      quit();
    }
  }

  void shutdown() override { stopTimer(); window.reset(); }
  void systemRequestedQuit() override { quit(); }

private:
  class PreviewWindow final : public juce::DocumentWindow {
  public:
    PreviewWindow() : juce::DocumentWindow(juce::String::fromUTF8("PlayWorship Teams — prueba aislada"),
                                           UITheme::appBg,
                                           juce::DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      setContentOwned(new TeamsPanel(), false);
      setResizable(true, false);
      centreWithSize(1000, 780);
    }
    void closeButtonPressed() override {
      juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
    bool keyPressed(const juce::KeyPress &key) override {
      return onKey && onKey(key);
    }
    std::function<bool(const juce::KeyPress &)> onKey;
  };

  std::unique_ptr<PreviewWindow> window;
  juce::String role{"owner"}, outputPath;
  bool memberAdded = false;
  int step = 0;
  juce::StringArray operations;

  TeamsPanel &panel() { return *static_cast<TeamsPanel *>(window->getContentComponent()); }

  void resize(int width, int height) {
    window->setContentComponentSize(width, height);
    window->centreWithSize(window->getWidth(), window->getHeight());
  }

  juce::var overview() const {
    juce::Array<juce::var> teams;
    if (role == "owner" || memberAdded)
      teams.add(object({{"id", "team-demo"}, {"ownerEmail", "lider@example.test"}, {"role", role},
                        {"ownerUid", "demo-owner"}}));
    return object({{"teams", teams}, {"capabilities", object({{"canManageSharedCloud", role == "owner"}})}});
  }

  juce::var details() const {
    juce::Array<juce::var> members;
    members.add(object({{"uid", "demo-owner"}, {"email", "lider@example.test"}, {"role", "owner"}}));
    if (memberAdded)
      members.add(object({{"uid", "demo-member"}, {"email", "musico@example.test"}, {"role", "member"}}));
    return object({{"team", object({{"id", "team-demo"}, {"ownerEmail", "lider@example.test"},
                                    {"ownerUid", "demo-owner"}, {"role", role}})},
        {"members", members}});
  }

  void refresh() {
    panel().setBusy(false);
    panel().setOverview(overview());
    if (panel().selectedTeamId().isNotEmpty()) panel().setDetails(details());
  }

  void switchRole(juce::String selectedRole) {
    role = std::move(selectedRole);
    panel().setStatus({});
    refresh();
    TeamsPanelTestAccess::scrollTop(panel());
  }

  void handle(const juce::String &operation, const juce::var &data) {
    operations.add(operation);
    if (operation == "add-member") {
      memberAdded = true;
      panel().setStatus(juce::String::fromUTF8("Usuario agregado. Puede descargar tus canciones desde Nube."));
    } else if (operation == "remove-member") {
      memberAdded = false;
      panel().setStatus("Acceso a la nube eliminado.");
    }
    refresh();
  }

  void press(juce::Component &root, const juce::String &label, size_t index = 0) {
    const auto found = buttons(root, label);
    if (index >= found.size() || !found[index]->isEnabled() || !found[index]->onClick)
      throw std::runtime_error("Missing or disabled preview action: " + label.toStdString());
    found[index]->onClick();
  }

  void fill(const juce::String &title, const juce::String &value) {
    auto *field = editor(panel(), title);
    if (field == nullptr) throw std::runtime_error("Missing preview field");
    field->setText(value, false);
    if (field->onTextChange) field->onTextChange();
  }

  void capture(const juce::String &name) {
    auto stream = juce::File(outputPath).getChildFile(name + ".png").createOutputStream();
    if (!stream || !stream->setPosition(0) || stream->truncate().failed() ||
        !juce::PNGImageFormat().writeImageToStream(
          panel().createComponentSnapshot(panel().getLocalBounds(), true, 2.0f), *stream))
      throw std::runtime_error("Couldn't save preview snapshot");
    std::cout << "Captured " << name << '\n';
  }

  void timerCallback() override {
    try {
      switch (step++) {
      case 0:
        capture("01-desktop-owner-access-settings");
        fill("Correo del usuario", "musico@example.test");
        press(panel(), "Agregar");
        break;
      case 1:
        capture("02-desktop-user-added");
        resize(390, 844);
        break;
      case 2:
        TeamsPanelTestAccess::scrollTop(panel());
        capture("03-phone-users");
        press(panel(), "Quitar");
        capture("04-phone-remove-confirmation");
        press(*TeamsPanelTestAccess::confirmation(panel()), "Cancelar");
        break;
      case 3:
        capture("05-phone-cancel-retains-user");
        press(panel(), "Quitar");
        press(*TeamsPanelTestAccess::confirmation(panel()), "Quitar");
        break;
      case 4:
        capture("06-phone-access-removed");
        panel().setStatus(juce::String::fromUTF8("Actualizando usuarios…"));
        panel().setBusy(true);
        capture("07-phone-busy");
        panel().setBusy(false);
        switchRole("member");
        capture("08-phone-member-no-administration");
        resize(1000, 780);
        break;
      case 5:
        capture("09-desktop-member-no-administration");
        switchRole("owner");
        panel().setOverview(object({{"teams", juce::Array<juce::var>{object({
            {"id", "team-demo"}, {"ownerEmail", "lider@example.test"}, {"role", "owner"}})}},
            {"capabilities", object({{"canManageSharedCloud", false}})}}));
        capture("10-desktop-permission-revoked");
        break;
      default:
        for (const auto *required : {"add-member", "remove-member"})
          if (!operations.contains(required)) throw std::runtime_error("Preview access action missing");
        for (const auto *removed : {"create", "invite", "accept", "share-local-song", "share-local-setlist",
                                    "prepare-song", "prepare-setlist", "open-song", "open-library"})
          if (operations.contains(removed)) throw std::runtime_error("Removed workflow still dispatched");
        std::cout << "Teams user administration preview passed: " << operations.joinIntoString(", ") << '\n';
        stopTimer();
        quit();
        break;
      }
    } catch (const std::exception &error) {
      stopTimer();
      std::cerr << "Preview failed: " << error.what() << '\n';
      setApplicationReturnValue(1);
      quit();
    }
  }
};

START_JUCE_APPLICATION(TeamsPreviewApplication)
