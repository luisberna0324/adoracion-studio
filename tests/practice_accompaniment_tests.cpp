#include "ui/panels/PracticeAccompaniment.h"
#include <iostream>
#include <string>
#include <vector>

namespace {
struct Command {
  std::string action;
  int index;
  int value;
};

// Mirrors the session contract: local writes are immediate, remote writes are
// requests and getters retain the last confirmed snapshot until applyPending.
struct Session {
  bool delayed = false;
  bool dynamicGuideMuted = true;
  bool metronome = false;
  bool divisions = false;
  bool metronomeMuted = true;
  int mask = 0;
  std::vector<bool> stemMuted{false, true, false, true};
  std::vector<Command> commands;
  std::size_t applied = 0;

  bool isDynamicGuideMuted() const { return dynamicGuideMuted; }
  bool isStemMuted(int index) const { return stemMuted.at(index); }
  bool isMetronomeEnabled() const { return metronome; }
  bool isClickDivisionEnabled() const { return divisions; }
  bool isMetronomeMuted() const { return metronomeMuted; }
  int getClickDivision() const { return mask; }

  void applyPending() {
    while (applied < commands.size()) {
      const auto &command = commands[applied++];
      if (command.action == "guide.mute") dynamicGuideMuted = command.value != 0;
      if (command.action == "stem.mute") stemMuted.at(command.index) = command.value != 0;
      if (command.action == "click.metronome") metronome = command.value != 0;
      if (command.action == "click.enabled") divisions = command.value != 0;
      if (command.action == "click.mute") metronomeMuted = command.value != 0;
      if (command.action == "click.division") mask = command.value;
    }
  }
  void record(std::string action, int index, int value) {
    commands.push_back({action, index, value});
    if (!delayed) applyPending();
  }
  void setDynamicGuideMuted(bool value) { record("guide.mute", -1, value); }
  void setStemMute(int index, bool value) { record("stem.mute", index, value); }
  void setMetronomeEnabled(bool value) { record("click.metronome", -1, value); }
  void setClickDivisionEnabled(bool value) { record("click.enabled", -1, value); }
  void setMetronomeMuted(bool value) { record("click.mute", -1, value); }
  void setClickDivision(int value) { record("click.division", -1, value); }
};

bool expect(bool condition, const char *message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

bool testOriginalGuideMapping() {
  struct Stem { bool include; bool guide; };
  const std::vector<Stem> stems{
      {false, true}, {true, false}, {true, true}, {false, false},
      {true, false}, {true, true}, {true, true}};
  const auto indices = playworship::practice::originalGuideStemIndices(
      stems, 4, [](const auto &stem) { return stem.guide; });
  if (!expect(indices == std::vector<int>{1, 3},
              "Guide indices count included stems and stop at loaded count")) return false;
  if (!expect(playworship::practice::originalGuideStemIndices(
                  stems, 0, [](const auto &stem) { return stem.guide; }).empty(),
              "Metadata without loaded stems cannot enable a guide")) return false;
  return true;
}

bool testGuide() {
  using namespace playworship::practice;
  Session session;
  const auto initialStems = session.stemMuted;
  const GuideSelection unavailable{false, true, {}};
  setGuideEnabled(session, unavailable, true);
  if (!expect(!guideEnabled(session, unavailable) && session.commands.empty(),
              "Absent guide cannot claim ON or issue commands")) return false;
  const GuideSelection dynamic{true, true, {1, 3}};
  if (!expect(!guideEnabled(session, dynamic), "Muted dynamic guide is OFF")) return false;
  setGuideEnabled(session, dynamic, true);
  if (!expect(guideEnabled(session, dynamic) && session.stemMuted == initialStems &&
                  session.commands.size() == 1 && session.commands[0].action == "guide.mute",
              "Dynamic ON only unmutes its bus, without switching guide source")) return false;
  setGuideEnabled(session, dynamic, true);
  if (!expect(session.commands.size() == 1, "Confirmed duplicate ON does not resend")) return false;
  setGuideEnabled(session, dynamic, false);
  if (!expect(!guideEnabled(session, dynamic) && !session.metronome,
              "Dynamic OFF preserves click and original guide states")) return false;

  const GuideSelection original{true, false, {1, 3}};
  setGuideEnabled(session, original, true);
  if (!expect(guideEnabled(session, original) && !session.stemMuted[1] &&
                  !session.stemMuted[3] && !session.stemMuted[0] &&
                  !session.stemMuted[2] && session.dynamicGuideMuted,
              "Original ON unmutes every selected guide stem, no music or dynamic bus")) return false;
  session.stemMuted[1] = true;
  if (!expect(guideEnabled(session, original),
              "A partially muted original guide remains ON while a guide stem is enabled")) return false;
  setGuideEnabled(session, original, false);
  return expect(!guideEnabled(session, original) && session.stemMuted[1] &&
                    session.stemMuted[3] && !session.stemMuted[0] && !session.stemMuted[2],
                "Original OFF mutes only included guide stems");
}

bool testMetronome() {
  using namespace playworship::practice;
  Session session;
  const auto stems = session.stemMuted;
  setMetronomeEnabled(session, true);
  if (!expect(metronomeEnabled(session) && session.mask == 1 && session.commands.size() == 4,
              "ON activates engine, divisions, an empty pulse mask, and unmutes")) return false;
  if (!expect(session.commands.back().action == "click.metronome" &&
                  session.stemMuted == stems && session.dynamicGuideMuted,
              "Enable is last, and original click/guide and music remain unchanged")) return false;
  setMetronomeEnabled(session, false);
  if (!expect(!metronomeEnabled(session) && session.divisions && session.mask == 1 &&
                  !session.metronomeMuted && session.commands.size() == 5,
              "OFF disables engine and preserves divisions and mixer settings")) return false;
  setMetronomeEnabled(session, false);
  if (!expect(session.commands.size() == 5, "Repeated OFF does not resend")) return false;

  for (const int mask : {1, 2, 3, 4, 5, 6, 7}) {
    Session selected;
    selected.mask = mask;
    selected.metronome = true;
    selected.divisions = true;
    if (!expect(!metronomeEnabled(selected), "Enabled but muted metronome is OFF")) return false;
    setMetronomeEnabled(selected, true);
    if (!expect(metronomeEnabled(selected) && selected.mask == mask &&
                    selected.commands.size() == 1 && selected.commands[0].action == "click.mute",
                "ON preserves every nonempty division mask and only unmutes when needed")) return false;
  }
  for (int gate = 0; gate < 4; ++gate) {
    Session selected;
    selected.metronome = gate != 0;
    selected.divisions = gate != 1;
    selected.mask = gate == 2 ? 0 : 5;
    selected.metronomeMuted = gate == 3;
    if (!expect(!metronomeEnabled(selected), "Every audibility gate participates in ON")) return false;
    setMetronomeEnabled(selected, true);
    if (!expect(metronomeEnabled(selected), "ON repairs each disabled gate")) return false;
  }
  return true;
}

bool testDelayedConfirmation() {
  using namespace playworship::practice;
  Session remote;
  remote.delayed = true;
  const GuideSelection dynamic{true, true, {}};
  setGuideEnabled(remote, dynamic, true);
  if (!expect(!guideEnabled(remote, dynamic), "Sending remote guide ON is not confirmation")) return false;
  remote.applyPending();
  if (!expect(guideEnabled(remote, dynamic), "Guide reflects confirmed snapshot")) return false;
  setMetronomeEnabled(remote, true);
  if (!expect(!metronomeEnabled(remote), "Sending remote click ON is not confirmation")) return false;
  remote.applyPending();
  if (!expect(metronomeEnabled(remote), "Click reflects confirmed snapshot")) return false;
  setMetronomeEnabled(remote, false);
  if (!expect(metronomeEnabled(remote), "Pending OFF keeps the confirmed ON state")) return false;
  remote.applyPending();
  return expect(!metronomeEnabled(remote) && guideEnabled(remote, dynamic),
                "Confirmed click OFF leaves guide enabled");
}
} // namespace

int main() {
  if (!testOriginalGuideMapping() || !testGuide() || !testMetronome() ||
      !testDelayedConfirmation()) return 1;
  std::cout << "Practice accompaniment tests passed\n";
}
