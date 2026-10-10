#include "ui/panels/PracticeToggleRequest.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void expect(bool value, const char *message) {
  if (!value) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}
}

int main() {
  PracticeToggleRequest request;
  expect(!request.pending, "no command is pending initially");
  request.begin(true, 100);
  expect(request.pending && request.requested, "ON records intention without replacing actual state");
  expect(!request.reconcile(false, true, 200) && request.pending,
         "an older OFF snapshot does not acknowledge ON");
  expect(!request.reconcile(true, true, 201) && !request.pending,
         "the first authoritative ON snapshot releases the button");
  request.begin(false, 500);
  expect(!request.reconcile(true, true, 2499) && request.pending,
         "OFF remains pending while the authoritative source is still ON");
  expect(request.reconcile(true, true, 2500) && !request.pending,
         "unconfirmed request expires once and reports its timeout");
  expect(!request.reconcile(true, true, 2600), "timeout is never reported twice");
  request.begin(true, 3000);
  expect(!request.reconcile(false, false, 3001) && !request.pending,
         "source, song, local/remote or availability change cancels without a timeout");
  request.begin(true, 4000);
  expect(!request.reconcile(true, true, 6000) && !request.pending,
         "an acknowledgement at the deadline wins over timeout");
  const auto nearWrap = std::numeric_limits<std::uint32_t>::max() - 1000;
  request.begin(true, nearWrap);
  expect(!request.reconcile(false, true, nearWrap + std::uint32_t{1999}) && request.pending,
         "elapsed time is correct across the millisecond-counter wrap");
  expect(request.reconcile(false, true, nearWrap + std::uint32_t{2000}) && !request.pending,
         "wrapped timeout has the same bounded lifetime");
  PracticeToggleRequest guide, metronome;
  guide.begin(true, 100);
  metronome.begin(false, 150);
  guide.reconcile(true, true, 200);
  expect(!guide.pending && metronome.pending && !metronome.requested,
         "one source's acknowledgement cannot complete another source's request");
  metronome.reconcile(false, true, 201);
  expect(!metronome.pending, "independent OFF acknowledgement completes");
  std::cout << "Practice toggle request: acknowledgement, timeout, context, independence and wrap passed\n";
}
