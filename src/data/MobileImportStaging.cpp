#include "MobileImportStaging.h"
#include "ApplicationDataPaths.h"

#include <array>
#include <limits>

namespace {

struct StagedEntry final {
  juce::File directory;
  bool committed = false;
  ~StagedEntry() {
    if (!committed)
      directory.deleteRecursively();
  }
};

} // namespace

MobileImportStaging::MobileImportStaging() {
#if defined(MTCONTROLS_ISOLATED_STORAGE_TESTS) && MTCONTROLS_ISOLATED_STORAGE_TESTS
  const auto temporaryRoot = ApplicationDataPaths::isolatedTestRoot();
#else
  const auto temporaryRoot = juce::File::getSpecialLocation(juce::File::tempDirectory);
#endif
  const auto candidate =
      temporaryRoot.getChildFile("playworship-import-" + juce::Uuid().toString());
  // Never assume ownership of a directory that existed before this draft.
  if (candidate.exists()) {
    directoryError = "Couldn't reserve a unique import directory.";
    return;
  }
  const auto created = candidate.createDirectory();
  if (created.failed()) {
    directoryError = "Couldn't prepare the import directory: " +
                     created.getErrorMessage();
    return;
  }
  directory = candidate;
  ownsDirectory = true;
}

MobileImportStaging::~MobileImportStaging() {
  if (ownsDirectory)
    directory.deleteRecursively();
}

juce::File MobileImportStaging::stage(const juce::URL &source,
                                      juce::String &error,
                                      const ProgressFn &progress) {
  error.clear();
  const auto fail = [&error](const juce::String &message) {
    error = message;
    return juce::File{};
  };
  if (!ownsDirectory)
    return fail(directoryError);

  // Copying preserves iOS's security bookmark until the input stream closes.
  const auto selectedUrl = source;
  juce::String filename;
  juce::int64 advertisedSize = -1;
  std::unique_ptr<juce::InputStream> input;
#if JUCE_ANDROID
  if (selectedUrl.getScheme() == "content") {
    // Do not resolve content:// into a filesystem path: scoped storage may
    // deny that path even though the picker granted access to the document.
    const auto document = juce::AndroidDocument::fromDocument(selectedUrl);
    if (!document)
      return fail("Couldn't open the selected document provider.");
    const auto info = document.getInfo();
    if (info.isDirectory())
      return fail("Select a file, not a directory.");
    filename = info.getName();
    if (filename.isEmpty())
      filename = selectedUrl.getFileName();
    if (info.isSizeInBytesValid())
      advertisedSize = info.getSizeInBytes();
    input = document.createInputStream();
  } else
#endif
  {
    if (!selectedUrl.isLocalFile())
      return fail("The selected item is not an accessible document.");
    const auto file = selectedUrl.getLocalFile();
    if (file.isDirectory())
      return fail("Select a file, not a directory.");
    filename = file.getFileName();
    // On iOS this creates the security-scoped stream; opening File directly
    // would discard the bookmark and fail for documents outside the sandbox.
    input = selectedUrl.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));
  }
  if (input == nullptr)
    return fail("Couldn't read the selected file: " + filename);
  if (auto *fileInput = dynamic_cast<juce::FileInputStream *>(input.get()))
    if (!fileInput->openedOk())
      return fail("Couldn't read the selected file: " + filename);

  filename = juce::File::createLegalFileName(filename).trim();
  if (filename.isEmpty() || filename == "." || filename == "..")
    return fail("Couldn't determine the selected file's name.");
  const auto streamSize = input->getTotalLength();
  const auto expectedSize = advertisedSize >= 0 ? advertisedSize : streamSize;
  const auto entryDirectory = directory.getChildFile(juce::Uuid().toString());
  if (entryDirectory.exists() || entryDirectory.createDirectory().failed())
    return fail("Couldn't create a private copy of " + filename);
  StagedEntry entry{entryDirectory};
  const auto destination = entryDirectory.getChildFile(filename);
  juce::TemporaryFile temporary(destination);
  juce::int64 copied = 0;
  {
    juce::FileOutputStream output(temporary.getFile());
    if (!output.openedOk())
      return fail("Couldn't write a private copy of " + filename);
    if (progress)
      progress(0.0);
    std::array<char, 65536> buffer;
    for (;;) {
      const auto count = input->read(buffer.data(), static_cast<int>(buffer.size()));
      if (count <= 0) {
        // Android's provider stream returns -1 at EOF, while JUCE's regular
        // file stream returns 0. Both must agree that the stream is exhausted.
        if (!input->isExhausted())
          return fail("The document provider stopped reading " + filename);
        break;
      }
      if (copied > std::numeric_limits<juce::int64>::max() - count ||
          !output.write(buffer.data(), static_cast<size_t>(count)))
        return fail("Couldn't complete the private copy of " + filename);
      copied += count;
      if (expectedSize > 0 && progress)
        progress(juce::jlimit(0.0, 0.99,
                             static_cast<double>(copied) /
                                 static_cast<double>(expectedSize)));
    }
    output.flush();
    if (output.getStatus().failed())
      return fail("Couldn't finish writing " + filename);
  }
  if (auto *fileInput = dynamic_cast<juce::FileInputStream *>(input.get()))
    if (fileInput->getStatus().failed())
      return fail("Couldn't finish reading " + filename);
  if (copied <= 0 || (expectedSize >= 0 && copied != expectedSize) ||
      (streamSize >= 0 && copied != streamSize) ||
      temporary.getFile().getSize() != copied)
    return fail("The selected file is empty or its copy is incomplete: " + filename);
  input.reset();
  if (!temporary.overwriteTargetFileWithTemporary())
    return fail("Couldn't store the private copy of " + filename);
  if (progress)
    progress(1.0);
  entry.committed = true;
  return destination;
}
