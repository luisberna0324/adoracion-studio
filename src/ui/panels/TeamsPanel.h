#pragma once

#include "../UiCommon.h"
#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>

class AppModal;

/** Subscription-gated administration of users with access to the owner's Cloud. */
class TeamsPanel final : public juce::Component, private juce::Timer {
public:
  TeamsPanel();
  ~TeamsPanel() override;

  std::function<void(const juce::String &operation, juce::var payload)> onAction;
  std::function<void()> onClose;

  void setOverview(juce::var data);
  void setDetails(juce::var data);
  void setBusy(bool);
  void setStatus(juce::String text, bool error = false);
  void selectTeam(juce::String teamId);
  juce::String selectedTeamId() const;

  void paint(juce::Graphics &) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &) override;

private:
  friend struct TeamsPanelTestAccess;
  class ActionButton;
  class Card;
  class ItemRow;
  class FormRow;

  juce::Label heading, subtitle, status;
  HeaderIconButton closeButton{"teamsClose", IconGlyph::Close};
  HeaderIconButton refreshButton{"teamsRefresh", IconGlyph::Loop};
  juce::Viewport viewport;
  juce::Component content;
  std::vector<std::unique_ptr<Card>> cards;
  std::unique_ptr<AppModal> confirmation;
  juce::var overview, details;
  juce::String teamId, memberEmail;
  bool busy = false, statusIsError = false;
  juce::String statusText;
  juce::BorderSize<int> lastSafeInsets;

  void rebuildContent();
  void layoutContent();
  void updateEnabledState();
  Card &addCard(juce::String title, juce::String help = {});
  void dispatch(const juce::String &operation, juce::var payload = {});
  void confirmAction(juce::String title, juce::String message,
                     juce::String primaryLabel, juce::String operation,
                     juce::var payload, bool destructive = false);
  bool canManageMembers() const;
  bool hasManagementCapability() const;
  void timerCallback() override;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TeamsPanel)
};
