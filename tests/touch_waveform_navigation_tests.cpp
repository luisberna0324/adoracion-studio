#include "../src/ui/panels/TouchWaveformNavigation.h"

#include <iostream>
#include <stdexcept>

using namespace touchWaveformNavigation;

void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

int main() {
  try {
    expect(!crossedTapSlop(10, 10, 10, 10), "stationary pointer is a tap");
    expect(!crossedTapSlop(10, 10, 13, 14), "five pixel jitter stays within slop");
    expect(crossedTapSlop(10, 10, 16, 10), "horizontal slop boundary starts drag");
    expect(crossedTapSlop(10, 10, 10, 4), "vertical slop boundary cancels tap");
    expect(crossedTapSlop(10, 10, 15, 15), "diagonal distance uses both axes");
    ReleaseState valid{false, false, false, 1, 7, 7, true, true, true};
    expect(isValidTap(valid), "one original pointer released inside is valid");
    auto state = valid;
    state.moved = true;
    expect(!isValidTap(state), "returning to the origin after drag stays invalid");
    state = valid; state.withinSlop = false;
    expect(!isValidTap(state), "release displacement invalidates tap without a drag event");
    state = valid; state.insideOriginalTarget = false;
    expect(!isValidTap(state), "outside release cancels tap");
    state = valid; state.releasedSource = 8;
    expect(!isValidTap(state), "unrelated source cannot release the primary tap");
    state = valid; state.pointerCount = 2;
    expect(!isValidTap(state), "multi-touch cannot invoke an action");
    state = valid; state.pinching = true;
    expect(!isValidTap(state), "pinch cannot become a release action");
    state = valid; state.cancelled = true;
    expect(!isValidTap(state), "remaining pointer after cancelled pinch cannot become a tap");
    state = valid; state.enabled = false;
    expect(!isValidTap(state), "loading or disabled interaction cannot commit tap");
    state = valid; state.pointerCount = 0;
    expect(!isValidTap(state), "stale release without active pointers does nothing");
    state = valid; state.primarySource = state.releasedSource = -1;
    expect(!isValidTap(state), "unassigned sources cannot create taps");
    std::cout << "PASS: production mobile tap/slop/release arbitration\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
