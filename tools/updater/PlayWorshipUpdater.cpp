#include "../../src/update/UpdateCrypto.h"

#include <juce_core/juce_core.h>

#include <iostream>

namespace {

struct Options {
  juce::String mode;
  juce::File package;
  juce::File installDir;
  juce::File appExe;
  juce::String version;
  juce::String sha256;
  juce::String publisher;
};

juce::File getStateRoot() {
  auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                  .getChildFile("MTControls")
                  .getChildFile("updates");
  root.createDirectory();
  return root;
}

juce::File getLogFile() {
  auto logs = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                  .getChildFile("MTControls")
                  .getChildFile("logs");
  logs.createDirectory();
  return logs.getChildFile("updater.log");
}

void logLine(const juce::String &line) {
  const auto text = juce::Time::getCurrentTime().toISO8601(true) + " " + line + "\n";
  getLogFile().appendText(text);
  std::cout << line << std::endl;
}

juce::String usage() {
  return "PlayWorshipUpdater --mode apply --package <path> --install-dir <path> "
         "--app-exe <path> --version <semver> --sha256 <hash> [--publisher <name>]";
}

bool parseOptions(int argc, char *argv[], Options &out) {
  juce::StringArray args;
  for (int i = 1; i < argc; ++i)
    args.add(juce::String::fromUTF8(argv[i]));

  for (int i = 0; i < args.size(); ++i) {
    const auto key = args[i];
    const auto value = i + 1 < args.size() ? args[i + 1] : juce::String();
    if (key == "--mode") {
      out.mode = value;
      ++i;
    } else if (key == "--package") {
      out.package = juce::File(value);
      ++i;
    } else if (key == "--install-dir") {
      out.installDir = juce::File(value);
      ++i;
    } else if (key == "--app-exe") {
      out.appExe = juce::File(value);
      ++i;
    } else if (key == "--version") {
      out.version = value;
      ++i;
    } else if (key == "--sha256") {
      out.sha256 = value;
      ++i;
    } else if (key == "--publisher") {
      out.publisher = value;
      ++i;
    } else {
      return false;
    }
  }

  return out.mode == "apply" && out.package.exists() && out.installDir.exists() &&
         out.appExe.getFullPathName().isNotEmpty() && out.version.isNotEmpty() &&
         out.sha256.isNotEmpty();
}

bool deleteRecursivelyWithRetries(const juce::File &target, int attempts = 40) {
  if (!target.exists())
    return true;
  for (int i = 0; i < attempts; ++i) {
    if (target.deleteRecursively())
      return true;
    juce::Thread::sleep(250);
  }
  return !target.exists();
}

bool moveWithRetries(const juce::File &source, const juce::File &dest,
                     int attempts = 40) {
  for (int i = 0; i < attempts; ++i) {
    if (source.moveFileTo(dest))
      return true;
    juce::Thread::sleep(250);
  }
  return false;
}

bool copyDirectoryContents(const juce::File &source, const juce::File &dest) {
  if (!source.isDirectory())
    return false;
  dest.createDirectory();

  const auto children = source.findChildFiles(juce::File::findFilesAndDirectories,
                                             false);
  for (const auto &child : children) {
    const auto target = dest.getChildFile(child.getFileName());
    if (child.isDirectory()) {
      if (!copyDirectoryContents(child, target))
        return false;
    } else if (!child.copyFileTo(target)) {
      return false;
    }
  }
  return true;
}

juce::File findInstallPayload(const juce::File &root) {
#if JUCE_MAC
  const auto apps = root.findChildFiles(juce::File::findDirectories, true, "*.app");
  if (!apps.isEmpty())
    return apps.getFirst();
#endif
  const auto direct = root.getChildFile("PlayWorship");
  if (direct.exists())
    return direct;
  const auto children = root.findChildFiles(juce::File::findFilesAndDirectories, false);
  if (children.size() == 1)
    return children.getFirst();
  return root;
}

bool extractPackage(const juce::File &package, const juce::File &dest) {
  deleteRecursivelyWithRetries(dest);
  dest.createDirectory();

  if (package.isDirectory())
    return copyDirectoryContents(package, dest);

  if (package.hasFileExtension(".zip")) {
    juce::ZipFile zip(package);
    return zip.uncompressTo(dest);
  }

  logLine("Unsupported transactional package format: " + package.getFullPathName());
  return false;
}

bool runCommand(const juce::StringArray &args, juce::String *output = nullptr) {
  juce::ChildProcess process;
  if (!process.start(args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    return false;
  const bool finished = process.waitForProcessToFinish(60000);
  const auto text = process.readAllProcessOutput();
  if (output != nullptr)
    *output = text;
  return finished && process.getExitCode() == 0;
}

bool verifyCodeSignature(const juce::File &target, const juce::String &publisher) {
  juce::ignoreUnused(publisher);
#if JUCE_MAC
  juce::String output;
  if (!runCommand({"codesign", "--verify", "--deep", "--strict",
                   target.getFullPathName()},
                  &output)) {
    logLine("codesign verification failed: " + output);
    return false;
  }
  output.clear();
  if (!runCommand({"spctl", "-a", "-t", "exec", target.getFullPathName()}, &output)) {
    logLine("spctl verification failed: " + output);
    return false;
  }
  return true;
#elif JUCE_WINDOWS
  if (publisher.isEmpty())
    return true;
  const juce::String command =
      "$s=Get-AuthenticodeSignature -LiteralPath '" +
      target.getFullPathName().replace("'", "''") +
      "'; if ($s.Status -ne 'Valid') { exit 2 }; "
      "if ($s.SignerCertificate.Subject -notlike '*" +
      publisher.replace("'", "''") + "*') { exit 3 }";
  juce::String output;
  const bool ok = runCommand({"powershell", "-NoProfile", "-ExecutionPolicy",
                              "Bypass", "-Command", command},
                             &output);
  if (!ok)
    logLine("Authenticode verification failed: " + output);
  return ok;
#else
  return true;
#endif
}

bool relaunchApp(const juce::File &appExe) {
#if JUCE_MAC
  juce::File appBundle = appExe;
  for (int i = 0; i < 4 && appBundle.exists(); ++i) {
    if (appBundle.hasFileExtension(".app")) {
      juce::StringArray args;
      args.add("open");
      args.add(appBundle.getFullPathName());
      juce::ChildProcess process;
      return process.start(args, 0);
    }
    appBundle = appBundle.getParentDirectory();
  }
#endif
  juce::StringArray args;
  args.add(appExe.getFullPathName());
  juce::ChildProcess process;
  return process.start(args, 0);
}

bool writeState(const juce::String &status, const Options &options,
                const juce::String &error = {}) {
  auto *root = new juce::DynamicObject();
  root->setProperty("version", 1);
  root->setProperty("status", status);
  root->setProperty("targetVersion", options.version);
  root->setProperty("installDir", options.installDir.getFullPathName());
  root->setProperty("updatedAtMs", juce::Time::currentTimeMillis());
  if (error.isNotEmpty())
    root->setProperty("error", error);
  return getStateRoot()
      .getChildFile("update-state.json")
      .replaceWithText(juce::JSON::toString(juce::var(root), true));
}

int applyUpdate(const Options &options) {
  logLine("Starting update to " + options.version);
  writeState("starting", options);

  const auto actualHash = PlayWorshipUpdate::sha256ForFile(options.package);
  if (!PlayWorshipUpdate::constantTimeEqualsIgnoreCase(actualHash, options.sha256)) {
    writeState("failed", options, "sha256 mismatch");
    logLine("SHA-256 mismatch");
    return 10;
  }

  const auto root = getStateRoot();
  const auto staging = root.getChildFile("staging").getChildFile(options.version);
  const auto backup = root.getChildFile("backup").getChildFile(options.version);
  const auto extracted = staging.getChildFile("extracted");

  writeState("extracting", options);
  if (!extractPackage(options.package, extracted)) {
    writeState("failed", options, "extract failed");
    return 11;
  }

  const auto payload = findInstallPayload(extracted);
  if (!payload.exists()) {
    writeState("failed", options, "payload missing");
    return 12;
  }

  writeState("verifying-signature", options);
  if (!verifyCodeSignature(payload, options.publisher)) {
    writeState("failed", options, "signature invalid");
    return 13;
  }

  writeState("backing-up", options);
  deleteRecursivelyWithRetries(backup);
  backup.getParentDirectory().createDirectory();
  if (!moveWithRetries(options.installDir, backup)) {
    writeState("failed", options, "backup failed");
    return 14;
  }

  writeState("installing", options);
  bool installed = false;
  if (payload.isDirectory() || payload.existsAsFile())
    installed = moveWithRetries(payload, options.installDir);

  if (!installed) {
    logLine("Install failed; attempting rollback");
    deleteRecursivelyWithRetries(options.installDir);
    moveWithRetries(backup, options.installDir);
    writeState("rolled-back", options, "install failed");
    relaunchApp(options.appExe);
    return 15;
  }

  writeState("relaunching", options);
  if (!relaunchApp(options.appExe)) {
    logLine("Relaunch failed; rolling back");
    deleteRecursivelyWithRetries(options.installDir);
    moveWithRetries(backup, options.installDir);
    writeState("rolled-back", options, "relaunch failed");
    relaunchApp(options.appExe);
    return 16;
  }

  deleteRecursivelyWithRetries(staging);
  writeState("installed", options);
  logLine("Update installed");
  return 0;
}

} // namespace

int main(int argc, char *argv[]) {
  Options options;
  if (!parseOptions(argc, argv, options)) {
    std::cerr << usage() << std::endl;
    return 2;
  }
  return applyUpdate(options);
}
