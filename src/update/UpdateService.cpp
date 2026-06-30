#include "UpdateService.h"

#include "UpdateCrypto.h"
#include "../data/SessionAuthLogger.h"

namespace PlayWorshipUpdate {
namespace {

#if defined(MTCONTROLS_UPDATE_BASE_URL)
constexpr const char *kDefaultUpdateBaseUrl = MTCONTROLS_UPDATE_BASE_URL;
#else
constexpr const char *kDefaultUpdateBaseUrl = "";
#endif

juce::String trimBaseUrl(const juce::String &value) {
  auto out = value.trim();
  while (out.endsWithChar('/'))
    out = out.dropLastCharacters(1);
  return out;
}

juce::String readStreamToString(juce::InputStream &stream) {
  return stream.readEntireStreamAsString();
}

juce::String quoteArg(const juce::String &value) {
  auto escaped = value;
  escaped = escaped.replace("\"", "\\\"");
  return "\"" + escaped + "\"";
}

juce::String packageExtensionForUrl(const juce::String &url) {
  const juce::URL parsed(url);
  auto name = parsed.getFileName().trim();
  if (name.isEmpty())
    name = "playworship-update.zip";
  if (!name.containsChar('.'))
    name << ".zip";
  return name;
}

} // namespace

UpdateService::UpdateService(UpdateServiceConfig configIn)
    : Thread("PlayWorship UpdateService"), config(std::move(configIn)) {
  if (config.updateBaseUrl.isEmpty())
    config.updateBaseUrl = kDefaultUpdateBaseUrl;
}

UpdateService::~UpdateService() {
  signalThreadShouldExit();
  stopThread(5000);
}

void UpdateService::setCallbacks(AvailableCallback available,
                                 ProgressCallback progress,
                                 ErrorCallback error) {
  const juce::ScopedLock sl(lock);
  onAvailable = std::move(available);
  onProgress = std::move(progress);
  onError = std::move(error);
}

bool UpdateService::isBusy() const noexcept { return isThreadRunning(); }

void UpdateService::checkNow() {
  if (isThreadRunning() || trimBaseUrl(config.updateBaseUrl).isEmpty())
    return;
  {
    const juce::ScopedLock sl(lock);
    pendingJob = Job::Check;
  }
  startThread();
}

void UpdateService::downloadAndInstall(UpdateManifest manifest) {
  if (isThreadRunning())
    return;
  {
    const juce::ScopedLock sl(lock);
    pendingManifest = std::move(manifest);
    pendingJob = Job::Download;
  }
  startThread();
}

void UpdateService::run() {
  Job job = Job::None;
  {
    const juce::ScopedLock sl(lock);
    job = pendingJob;
    pendingJob = Job::None;
  }

  if (job == Job::Check)
    runCheck();
  else if (job == Job::Download)
    runDownload();
}

juce::URL UpdateService::buildCheckUrl() const {
  juce::URL url(trimBaseUrl(config.updateBaseUrl) + "/v1/updates/check");
  url = url.withParameter("platform", getPlatformId());
  url = url.withParameter("arch", getArchitectureId());
  url = url.withParameter("version", config.currentVersion);
  url = url.withParameter("channel", config.channel);
  return url;
}

void UpdateService::runCheck() {
  const auto url = buildCheckUrl();
  SessionAuthLogger::write("UpdateService: checking " + url.toString(false));

  int status = 0;
  auto options =
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withConnectionTimeoutMs(8000)
          .withHttpRequestCmd("GET")
          .withStatusCode(&status);
  auto stream = url.createInputStream(std::move(options));
  if (stream == nullptr || status < 200 || status >= 300) {
    postError("No se pudo consultar actualizaciones.");
    return;
  }

  auto parsed = parseUpdateManifest(readStreamToString(*stream));
  if (!parsed.ok) {
    postError(parsed.error);
    return;
  }

  if (!parsed.manifest.updateAvailable ||
      compareSemver(parsed.manifest.latestVersion, config.currentVersion) <= 0) {
    SessionAuthLogger::write("UpdateService: no update available");
    return;
  }

  AvailableCallback availableCallback;
  {
    const juce::ScopedLock sl(lock);
    availableCallback = onAvailable;
  }
  if (availableCallback) {
    auto availableManifest = parsed.manifest;
    juce::MessageManager::callAsync(
        [cb = std::move(availableCallback),
         manifestForCallback = std::move(availableManifest)]() mutable {
          cb(std::move(manifestForCallback));
        });
  }
}

void UpdateService::runDownload() {
  UpdateManifest manifest;
  {
    const juce::ScopedLock sl(lock);
    manifest = pendingManifest;
  }

  const auto package = manifest.hasDelta ? manifest.delta : manifest.package;
  const auto updatesRoot = getUpdatesRoot();
  const auto downloads = updatesRoot.getChildFile("downloads");
  downloads.createDirectory();

  const auto target = downloads.getChildFile(packageExtensionForUrl(package.url));
  target.deleteFile();
  postProgress({"Descargando PlayWorship " + manifest.latestVersion, 0.0, 0,
                package.sizeBytes});

  const juce::URL url(package.url);
  auto input = url.createInputStream(
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withConnectionTimeoutMs(15000)
          .withHttpRequestCmd("GET"));
  if (input == nullptr) {
    postError("No se pudo descargar la actualizacion.");
    return;
  }

  auto output = target.createOutputStream();
  if (output == nullptr) {
    postError("No se pudo preparar el archivo de descarga.");
    return;
  }

  const auto total = package.sizeBytes > 0 ? package.sizeBytes : input->getTotalLength();
  juce::HeapBlock<char> buffer(1024 * 128);
  juce::int64 done = 0;
  while (!threadShouldExit()) {
    const int read = input->read(buffer.getData(), 1024 * 128);
    if (read < 0) {
      postError("La descarga se interrumpio.");
      return;
    }
    if (read == 0)
      break;
    if (!output->write(buffer.getData(), static_cast<size_t>(read))) {
      postError("No se pudo escribir la actualizacion en disco.");
      return;
    }
    done += read;
    const double progress =
        total > 0 ? juce::jlimit(0.0, 1.0,
                                 static_cast<double>(done) /
                                     static_cast<double>(total))
                  : -1.0;
    postProgress({"Descargando PlayWorship " + manifest.latestVersion, progress,
                  done, total});
  }
  output.reset();

  if (package.sizeBytes > 0 && target.getSize() != package.sizeBytes) {
    target.deleteFile();
    postError("La descarga no coincide con el tamano esperado.");
    return;
  }

  postProgress({"Validando integridad", 1.0, done, total});
  const auto actualHash = sha256ForFile(target);
  if (!constantTimeEqualsIgnoreCase(actualHash, package.sha256)) {
    target.deleteFile();
    postError("La actualizacion descargada no coincide con el SHA-256 del servidor.");
    return;
  }

  postProgress({"Preparando instalacion", 1.0, done, total});
  if (!launchUpdater(target, package, manifest.latestVersion)) {
    postError("No se pudo iniciar el actualizador.");
    return;
  }

  juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
}

void UpdateService::postError(juce::String message) {
  SessionAuthLogger::write("UpdateService error: " + message);
  ErrorCallback errorCallback;
  {
    const juce::ScopedLock sl(lock);
    errorCallback = onError;
  }
  if (errorCallback)
    juce::MessageManager::callAsync(
        [cb = std::move(errorCallback), errorMessage = std::move(message)] {
          cb(errorMessage);
        });
}

void UpdateService::postProgress(UpdateProgress progress) {
  ProgressCallback progressCallback;
  {
    const juce::ScopedLock sl(lock);
    progressCallback = onProgress;
  }
  if (progressCallback)
    juce::MessageManager::callAsync(
        [cb = std::move(progressCallback), updateProgress = std::move(progress)] {
          cb(updateProgress);
        });
}

juce::File UpdateService::getUpdatesRoot() const {
  auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                  .getChildFile("MTControls")
                  .getChildFile("updates");
  root.createDirectory();
  return root;
}

juce::File UpdateService::resolveInstallRoot() const {
  const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
#if JUCE_MAC
  const auto contents = exe.getParentDirectory().getParentDirectory();
  return contents.getParentDirectory();
#else
  return exe.getParentDirectory();
#endif
}

juce::File UpdateService::resolveUpdaterExecutable() const {
  const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
#if JUCE_MAC
  return exe.getParentDirectory()
      .getSiblingFile("Resources")
      .getChildFile("PlayWorshipUpdater");
#elif JUCE_WINDOWS
  return exe.getParentDirectory()
      .getChildFile("updater")
      .getChildFile("PlayWorshipUpdater.exe");
#else
  return exe.getParentDirectory()
      .getChildFile("updater")
      .getChildFile("PlayWorshipUpdater");
#endif
}

bool UpdateService::launchUpdater(const juce::File &packageFile,
                                  const UpdatePackage &package,
                                  const juce::String &targetVersion) {
  const auto updater = resolveUpdaterExecutable();
  if (!updater.existsAsFile()) {
    SessionAuthLogger::write("UpdateService: updater missing at " +
                             updater.getFullPathName());
    return false;
  }

  const auto runnerDir = getUpdatesRoot().getChildFile("runner");
  runnerDir.createDirectory();
  const auto runnerUpdater = runnerDir.getChildFile(updater.getFileName());
  runnerUpdater.deleteFile();
  if (!updater.copyFileTo(runnerUpdater)) {
    SessionAuthLogger::write("UpdateService: failed to copy updater runner to " +
                             runnerUpdater.getFullPathName());
    return false;
  }
  runnerUpdater.setExecutePermission(true);

  const auto installRoot = resolveInstallRoot();
  const auto appExe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
  juce::StringArray args;
  args.add(runnerUpdater.getFullPathName());
  args.add("--mode");
  args.add("apply");
  args.add("--package");
  args.add(packageFile.getFullPathName());
  args.add("--install-dir");
  args.add(installRoot.getFullPathName());
  args.add("--app-exe");
  args.add(appExe.getFullPathName());
  args.add("--version");
  args.add(targetVersion);
  args.add("--sha256");
  args.add(package.sha256);
  if (package.publisher.isNotEmpty()) {
    args.add("--publisher");
    args.add(package.publisher);
  }

  juce::ChildProcess process;
  const bool started = process.start(args, 0);
  SessionAuthLogger::write(juce::String("UpdateService: launch updater ") +
                           (started ? "ok" : "failed"));
  juce::ignoreUnused(quoteArg);
  return started;
}

} // namespace PlayWorshipUpdate
