#include "../src/data/MobileImportStaging.h"
#include "IsolatedTestRoot.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void createFixture(const juce::File &file) {
  std::vector<char> bytes(200123);
  for (size_t i = 0; i < bytes.size(); ++i)
    bytes[i] = static_cast<char>((i * 37) % 251);
  expect(file.replaceWithData(bytes.data(), bytes.size()),
         "test creates source data");
}

bool fileMatches(const juce::File &file, const juce::MemoryBlock &expected) {
  // JUCE appends to a supplied MemoryBlock instead of replacing its content.
  // Always use a fresh block so repeated integrity checks compare one file.
  juce::MemoryBlock contents;
  return file.loadFileAsData(contents) && contents == expected;
}

void testCopiesAndOwnership(const juce::File &sources) {
  const auto source = sources.getChildFile("01 Clic % demo.wav");
  createFixture(source);
  juce::MemoryBlock original;
  expect(source.loadFileAsData(original), "test reads source fixture");

  auto draft = std::make_shared<MobileImportStaging>();
  const auto directory = draft->getDirectory();
  expect(directory.isDirectory() && !source.isAChildOf(directory),
         "staging owns a new directory independent from source documents");
  auto worker = draft;
  juce::String error = "old error";
  std::vector<double> progress;
  const auto first = draft->stage(
      juce::URL(source), error,
      [&progress](double value) { progress.push_back(value); });
  expect(first.existsAsFile() && error.isEmpty(), "selected URL is copied");
  expect(first.getFileName() == source.getFileName(),
         "copy preserves the original basename and extension");
  expect(first.isAChildOf(directory), "copy stays under this draft's directory");
  expect(fileMatches(first, original),
         "streaming copy preserves all bytes");
  expect(progress.size() > 3 && progress.front() == 0.0 && progress.back() == 1.0,
         "copy emits streaming progress from zero through completion");
  for (size_t i = 1; i < progress.size(); ++i)
    expect(progress[i] >= progress[i - 1] && progress[i] <= 1.0,
           "copy progress remains monotonic and bounded");

  const auto second = draft->stage(juce::URL(source), error);
  expect(second.existsAsFile() && second != first &&
             second.getFileName() == first.getFileName(),
         "same-named documents never overwrite one another");
  expect(fileMatches(first, original) && fileMatches(second, original),
         "both independently staged copies retain the complete source bytes");
  expect(fileMatches(source, original),
         "staging never edits or removes the source document");

  draft.reset();
  expect(first.existsAsFile() && second.existsAsFile(),
         "worker ownership keeps files alive after the wizard closes");
  worker.reset();
  expect(!directory.exists() && source.existsAsFile(),
         "the last owner deletes only its own staging directory");
  worker.reset();
  draft.reset();
  expect(fileMatches(source, original),
         "repeated release is safe and source content survives cleanup");
}

void testFailuresAndPartialCleanup(const juce::File &sources) {
  MobileImportStaging staging;
  const auto directory = staging.getDirectory();
  juce::String error;
  const auto failCleanly = [&](const juce::URL &url) {
    const auto staged = staging.stage(url, error);
    expect(staged == juce::File() && error.isNotEmpty(),
           "invalid selection returns an error without a usable path");
    expect(directory.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0,
           "failed copy leaves no incomplete files or entry directories");
  };
  failCleanly(juce::URL(sources.getChildFile("missing.wav")));
  failCleanly(juce::URL(sources));
  failCleanly(juce::URL("https://example.invalid/not-a-picked-document.wav"));
  const auto empty = sources.getChildFile("empty.wav");
  expect(empty.create().wasOk(), "test creates empty source");
  failCleanly(juce::URL(empty));

  const auto changing = sources.getChildFile("changing.wav");
  createFixture(changing);
  bool truncated = false;
  const auto result = staging.stage(juce::URL(changing), error, [&](double value) {
    if (value > 0.0 && !truncated) {
      // Simulate a provider whose document becomes incomplete during reading.
      juce::FileOutputStream output(changing);
      expect(output.openedOk() && output.setPosition(1) && output.truncate().wasOk(),
             "test truncates its input while a copy is in progress");
      truncated = true;
    }
  });
  expect(truncated && result == juce::File() && error.isNotEmpty(),
         "a short copy cannot be committed as a successful import");
  expect(directory.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0,
         "short copy removes its partial file and private entry directory");
  expect(changing.existsAsFile(), "failed staging never removes the source");

  const auto valid = sources.getChildFile("after-error.flac");
  createFixture(valid);
  expect(staging.stage(juce::URL(valid), error).existsAsFile() && error.isEmpty(),
         "a draft can retry successfully after a failed copy");
}

} // namespace

int main() {
  try {
    const auto sources = isolatedTestRoot().getChildFile("sources");
    expect(sources.createDirectory().wasOk(),
           "test creates its isolated source directory");
    testCopiesAndOwnership(sources);
    testFailuresAndPartialCleanup(sources);
    std::cout << "Mobile import staging tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
