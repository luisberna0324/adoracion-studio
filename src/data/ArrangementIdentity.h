#pragma once
#include <cstddef>
#include <limits>

namespace playworship::arrangement {
// Metadata, UI and compiled audio sections need not have the same ordering or
// size. An ID names exactly one occurrence; repeated display names are fine.
template <typename Parts, typename Id>
int uniquePartIndex(const Parts &parts, const Id &id,
                    int playableLimit = std::numeric_limits<int>::max()) {
  if (id == Id{}) return -1;
  int found = -1;
  for (int i = 0; i < static_cast<int>(parts.size()); ++i) {
    if (parts[static_cast<std::size_t>(i)].id != id) continue;
    if (found >= 0) return -1;
    found = i;
  }
  return found < playableLimit ? found : -1;
}
} // namespace playworship::arrangement
