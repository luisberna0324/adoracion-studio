#pragma once

#include <juce_core/juce_core.h>

#include <functional>

// A private, short-lived copy of documents selected from mobile providers.
// Keep this object in a shared_ptr owned by both the wizard and any import/save
// worker. Only the last owner's release removes its own temporary directory.
// Source documents are never modified. Desktop uses this class only in tests.
class MobileImportStaging final {
public:
  using ProgressFn = std::function<void(double)>;

  MobileImportStaging();
  ~MobileImportStaging();

  // Call off the message thread; provider reads may block. Pass/copy the URL
  // returned by FileChooser, not a URL reconstructed from its text (iOS needs
  // its embedded security bookmark). Failure returns File{} and sets error.
  // Each successful call has a unique path retaining the original filename.
  juce::File stage(const juce::URL &source, juce::String &error,
                    const ProgressFn &progress = {});

  const juce::File &getDirectory() const noexcept { return directory; }

private:
  juce::File directory;
  juce::String directoryError;
  bool ownsDirectory = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MobileImportStaging)
};
