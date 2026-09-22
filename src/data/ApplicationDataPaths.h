#pragma once

#include <juce_core/juce_core.h>

#if defined(MTCONTROLS_ISOLATED_STORAGE_TESTS) && MTCONTROLS_ISOLATED_STORAGE_TESTS
#include <filesystem>
#include <stdexcept>
#endif

namespace ApplicationDataPaths {
// Internal linkage is intentional: the integration executable compiles a few
// storage translation units with the test flag while linking the normal app
// archive for the remaining code. A production inline definition must never
// replace a test-only one at link time.
#if defined(MTCONTROLS_ISOLATED_STORAGE_TESTS) && MTCONTROLS_ISOLATED_STORAGE_TESTS
static inline juce::File isolatedTestRoot() {
  const auto requested = juce::SystemStats::getEnvironmentVariable(
      "PLAYWORSHIP_CREATION_TEST_ROOT", {});
  if (requested.isEmpty() || !juce::File::isAbsolutePath(requested))
    throw std::runtime_error("An explicit temporary test storage root is required.");

  // JUCE's macOS tempDirectory is an application cache under ~/Library, not
  // the system mktemp/TMPDIR location. Use the actual system temporary root
  // and canonical paths to account for /var -> /private/var on macOS.
  std::error_code pathError;
  const auto systemTemporary = std::filesystem::temp_directory_path(pathError);
  if (pathError)
    throw std::runtime_error("Couldn't resolve the system temporary directory.");
  const auto canonicalTemporary = std::filesystem::canonical(systemTemporary, pathError);
  if (pathError)
    throw std::runtime_error("Couldn't resolve the canonical temporary directory.");
  const auto canonicalRoot = std::filesystem::canonical(
      std::filesystem::path(requested.toStdString()), pathError);
  if (pathError)
    throw std::runtime_error("Test storage root must already exist.");
  const juce::File root(juce::String::fromUTF8(canonicalRoot.u8string().c_str()));
  const juce::File temporaryDirectory(
      juce::String::fromUTF8(canonicalTemporary.u8string().c_str()));
  if (!root.isDirectory() || !root.isAChildOf(temporaryDirectory))
    throw std::runtime_error("Test storage root must be an existing child of the temporary directory.");

  for (auto directory = root; directory != temporaryDirectory;
       directory = directory.getParentDirectory()) {
    if (directory.isSymbolicLink())
      throw std::runtime_error("Test storage root must not traverse symbolic links.");
  }
  return root;
}

static inline juce::File testSubdirectory(const char *name) {
  const auto directory = isolatedTestRoot().getChildFile(name);
  if (directory.isSymbolicLink())
    throw std::runtime_error("Test storage subdirectory must not be a symbolic link.");
  return directory;
}
#endif

static inline juce::File applicationDataDirectory() {
#if defined(MTCONTROLS_ISOLATED_STORAGE_TESTS) && MTCONTROLS_ISOLATED_STORAGE_TESTS
  return testSubdirectory("application-data");
#else
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#endif
}

static inline juce::File documentsDirectory() {
#if defined(MTCONTROLS_ISOLATED_STORAGE_TESTS) && MTCONTROLS_ISOLATED_STORAGE_TESTS
  return testSubdirectory("documents");
#else
  return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
#endif
}

static inline juce::File systemLogsDirectory() {
#if defined(MTCONTROLS_ISOLATED_STORAGE_TESTS) && MTCONTROLS_ISOLATED_STORAGE_TESTS
  return testSubdirectory("logs");
#else
  return juce::FileLogger::getSystemLogFileFolder();
#endif
}
} // namespace ApplicationDataPaths
