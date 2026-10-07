#include "ui/learning/RemoteLearningAction.h"
#include <cstdlib>
#include <iostream>

static void expect(bool condition, const char *message) {
  if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}

int main() {
  using playworship::learning::RemoteLearningAction;
  RemoteLearningAction action;
  action.begin(true);
  action.snapshot(10, "song", true, 0);
  expect(!action.applied(), "playback from another client is not an exercise");
  action.sent("pause", "transport.toggle", "song", 10, true);
  action.acknowledge("pause", true, 10, {});
  action.snapshot(11, "song", true, 11);
  expect(!action.applied(), "own Pause followed by another client's Play is not own Play");
  action.sent("mine", "transport.toggle", "song", 100);
  action.snapshot(11, "song", true, 101);
  expect(!action.applied(), "a snapshot before ACK cannot confirm the command");
  action.acknowledge("other", true, 11, {});
  action.snapshot(12, "song", true, 102);
  expect(!action.applied(), "another client's ACK cannot confirm the exercise");
  action.acknowledge("mine", true, 12, {});
  action.snapshot(12, "song", true, 103);
  expect(!action.applied(), "the previous snapshot is not confirmation");
  action.snapshot(13, "song", false, 104);
  expect(!action.applied(), "accepted pause does not teach Play");
  action.sent("play", "transport.toggle", "song", 105);
  action.acknowledge("play", true, 13, {});
  action.snapshot(14, "song", true, 106);
  expect(action.applied(), "own accepted Play and subsequent state complete exercise");
  action.snapshot(15, "different-song", true, 107);
  expect(!action.applied(), "changing song invalidates previous proof");

  action.begin(false);
  action.sent("wrong-control", "mix.volume", "song", 200);
  action.acknowledge("wrong-control", true, 15, {});
  action.snapshot(16, "song", false, 201);
  expect(!action.applied(), "a mix change cannot satisfy Stop");
  action.sent("stop", "transport.stop", "song", 202);
  action.acknowledge("stop", false, 16, "Server busy");
  action.snapshot(17, "song", false, 203);
  expect(!action.applied() && action.reason() == "Server busy", "rejected commands remain pending exercises");
  action.sent("retry", "transport.stop", "song", 204);
  action.acknowledge("retry", true, 17, {});
  action.snapshot(18, "song", false, 205);
  expect(action.applied(), "Stop can be checked even when transport was already stopped");
  action.disconnected();
  expect(!action.applied(), "disconnect removes confirmation");
  action.sent("expired", "transport.stop", "song", 300);
  action.acknowledge("expired", true, 18, {});
  action.snapshot(19, "song", false, 5301);
  expect(!action.applied(), "expired exercises cannot use a late snapshot");
  action.sent("old-step", "transport.stop", "song", 6000);
  action.begin(true);
  action.acknowledge("old-step", true, 19, {});
  action.snapshot(20, "song", true, 6001);
  expect(!action.applied(), "late replies from a previous step are ignored");
  action.deactivate();
  action.sent("closed-guide", "transport.toggle", "song", 7000);
  action.acknowledge("closed-guide", true, 20, {});
  action.snapshot(21, "song", true, 7001);
  expect(!action.applied(), "closing guide prevents evidence collection");
  std::cout << "Remote learning action tests passed\n";
}
