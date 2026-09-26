#pragma once

#include "data/ApplicationDataPaths.h"

#if !defined(MTCONTROLS_ISOLATED_STORAGE_TESTS) || !MTCONTROLS_ISOLATED_STORAGE_TESTS
#error "Filesystem tests must use isolated storage"
#endif

inline juce::File isolatedTestRoot() {
  return ApplicationDataPaths::isolatedTestRoot();
}
