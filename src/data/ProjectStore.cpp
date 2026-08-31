#include "ProjectStore.h"
#include "Localization.h"
#include "SessionAuthLogger.h"
#include "SongLoadProbe.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <thread>
#include <vector>

namespace {
// Forward declarations for the per-project sidecar helpers defined
// later in this translation unit. They are used by the
// ProjectSettingsSaveJob (which compares against the sidecar to skip
// the .pwproj re-pack) and by writeNow (used by flush/flushAll).
bool readCachedSettings(const juce::String &projectId,
                        ProjectSongSettings &outSettings);
bool writeCachedSettings(const juce::String &projectId,
                         const ProjectSongSettings &settings);
juce::File cachedSettingsFile(const juce::String &projectId);
juce::var settingsToVar(const ProjectSongSettings &settings);

constexpr auto kManifestName = "manifest.json";
constexpr auto kAudioPrefix = "audio/";
constexpr auto kWaveformName = "waveform.bin";
constexpr auto kCacheSourceName = "project-source.json";
constexpr auto kProjectAudioFormat = "flac";

class ProgressThrottle final {
public:
  explicit ProgressThrottle(ProjectProgressFn callbackIn)
      : callback(std::move(callbackIn)) {}

  void publish(double progress, const juce::String &phase,
               const bool force = false) {
    if (!callback)
      return;
    progress = juce::jlimit(lastProgress, 1.0, progress);
    const auto now = juce::Time::getMillisecondCounter();
    if (!force && progress < 1.0 && now - lastPublishedAt < 75 &&
        progress - lastProgress < 0.01)
      return;
    lastProgress = progress;
    lastPublishedAt = now;
    callback(progress, phase);
  }

private:
  ProjectProgressFn callback;
  double lastProgress = 0.0;
  juce::uint32 lastPublishedAt = 0;
};

class ProgressOutputStream final : public juce::OutputStream {
public:
  ProgressOutputStream(juce::OutputStream &targetIn,
                       const juce::int64 estimatedBytesIn,
                       std::function<void(double)> onProgressIn)
      : target(targetIn), estimatedBytes(juce::jmax<juce::int64>(
                              1, estimatedBytesIn)),
        onProgress(std::move(onProgressIn)) {}

  void flush() override { target.flush(); }
  bool setPosition(const juce::int64 newPosition) override {
    return target.setPosition(newPosition);
  }
  juce::int64 getPosition() override { return target.getPosition(); }
  bool write(const void *data, const size_t bytes) override {
    if (!target.write(data, bytes))
      return false;
    if (onProgress) {
      const auto fraction = static_cast<double>(target.getPosition()) /
                            static_cast<double>(estimatedBytes);
      onProgress(juce::jlimit(0.0, 0.995, fraction));
    }
    return true;
  }

private:
  juce::OutputStream &target;
  juce::int64 estimatedBytes;
  std::function<void(double)> onProgress;
};

bool replaceProjectFileWithRetry(const juce::File &temporaryFile,
                                 const juce::File &destination) {
  constexpr int retryDelaysMs[] = {0, 30, 90, 180};
  for (const auto delayMs : retryDelaysMs) {
    if (delayMs > 0)
      juce::Thread::sleep(delayMs);
    if (temporaryFile.replaceFileIn(destination))
      return true;
  }
  return false;
}

bool cacheMatchesProject(const juce::File &projectFile,
                         const juce::File &cacheDirectory) {
  const auto marker = cacheDirectory.getChildFile(kCacheSourceName);
  if (!marker.existsAsFile())
    return false;
  const auto parsed = juce::JSON::parse(marker);
  const auto *root = parsed.getDynamicObject();
  if (root == nullptr)
    return false;
  const juce::File recordedPath(root->getProperty("projectPath").toString());
  const auto recordedSize =
      static_cast<juce::int64>(root->getProperty("projectSize"));
  const auto recordedMtime =
      static_cast<juce::int64>(root->getProperty("projectMtimeMs"));
  return recordedPath == projectFile && recordedSize == projectFile.getSize() &&
         recordedMtime ==
             projectFile.getLastModificationTime().toMilliseconds();
}

bool writeCacheProjectMarker(const juce::File &projectFile,
                             const juce::File &cacheDirectory) {
  auto *root = new juce::DynamicObject();
  root->setProperty("projectPath", projectFile.getFullPathName());
  root->setProperty("projectSize", projectFile.getSize());
  root->setProperty("projectMtimeMs",
                    projectFile.getLastModificationTime().toMilliseconds());
  return cacheDirectory.getChildFile(kCacheSourceName)
      .replaceWithText(juce::JSON::toString(juce::var(root), true));
}

bool isDecodableFlacFile(const juce::File &file) {
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  const auto reader = std::unique_ptr<juce::AudioFormatReader>(
      formats.createReaderFor(file));
  return reader != nullptr &&
         reader->getFormatName().containsIgnoreCase("flac");
}

class ProjectSettingsSaveJob final : public juce::ThreadPoolJob {
public:
  ProjectSettingsSaveJob(juce::CriticalSection &writeGuardIn,
                         juce::File fileIn,
                         juce::String projectIdIn,
                         ProjectSongSettings settingsIn,
                         std::function<bool()> isCurrentIn)
      : ThreadPoolJob("Project settings save"), writeGuard(writeGuardIn),
        file(std::move(fileIn)), projectId(std::move(projectIdIn)),
        settings(std::move(settingsIn)),
        isCurrent(std::move(isCurrentIn)) {}
  JobStatus runJob() override {
    if (isCurrent != nullptr && !isCurrent())
      return jobHasFinished;
    // P1.2: skip the re-pack when the persisted settings are byte-for-byte
    // identical to what is already on disk. Without this, every
    // settings flush would re-pack the .pwproj, change its mtime, and
    // force a full re-extract of every stem on the next load.
    ProjectSongSettings previous;
    if (!projectId.isEmpty() &&
        readCachedSettings(projectId, previous) &&
        ProjectStore::settingsJsonEqual(previous, settings)) {
      return jobHasFinished;
    }
    const juce::ScopedLock lock(writeGuard);
    if (isCurrent != nullptr && !isCurrent())
      return jobHasFinished;
    juce::String error;
    if (!ProjectStore::updateSettings(file, settings, &error))
      SessionAuthLogger::write("[Project] Settings save failed path=" +
                               file.getFullPathName() + " error=" + error);
    return jobHasFinished;
  }
private:
  juce::CriticalSection &writeGuard;
  juce::File file;
  juce::String projectId;
  ProjectSongSettings settings;
  std::function<bool()> isCurrent;
};

class ProjectSettingsSaveQueue final : private juce::Timer {
public:
  void schedule(const juce::File &file, const juce::String &projectId,
               ProjectSongSettings settings) {
    if (!file.existsAsFile()) return;
    // P1.2: skip enqueuing when the on-disk sidecar already matches
    // the new settings. Without this guard every open() that finds
    // a newer sidecar (settings have been edited since the last
    // re-pack) would schedule a job that re-packs the .pwproj,
    // changing its mtime and forcing a full re-extract on the
    // very next load.
    if (!projectId.isEmpty()) {
      ProjectSongSettings previous;
      if (readCachedSettings(projectId, previous) &&
          ProjectStore::settingsJsonEqual(previous, settings)) {
        return;
      }
    }
    const juce::ScopedLock lock(guard);
    const auto key = file.getFullPathName().toStdString();
    const auto version = ++generations[key];
    cached[key] = settings;
    pending[key] = {file, projectId, std::move(settings), key, version};
    startTimer(750);
  }
  // Backward-compatible overload used when no projectId is available
  // (settings save triggered from paths that don't have it). The skip
  // optimization in writeNow / runJob requires a projectId; callers
  // that want the optimization should use the overload above.
  void schedule(const juce::File &file, ProjectSongSettings settings) {
    schedule(file, juce::String{}, std::move(settings));
  }
  void remember(const juce::File &file, const ProjectSongSettings &settings) {
    if (!file.existsAsFile()) return;
    const juce::ScopedLock lock(guard);
    cached[file.getFullPathName().toStdString()] = settings;
  }
  bool getCached(const juce::File &file, ProjectSongSettings &outSettings) {
    const juce::ScopedLock lock(guard);
    const auto it = cached.find(file.getFullPathName().toStdString());
    if (it == cached.end()) return false;
    outSettings = it->second;
    return true;
  }
  void flush(const juce::File &file) {
    Pending next;
    {
      const juce::ScopedLock lock(guard);
      const auto it = pending.find(file.getFullPathName().toStdString());
      if (it != pending.end()) { next = std::move(it->second); pending.erase(it); }
      if (pending.empty()) stopTimer();
    }
    if (next.file.existsAsFile()) writeNow(next);
  }
  void flushAll() {
    std::vector<Pending> all;
    {
      const juce::ScopedLock lock(guard);
      for (auto &entry : pending) all.push_back(std::move(entry.second));
      pending.clear(); stopTimer();
    }
    for (const auto &entry : all)
      if (entry.file.existsAsFile()) writeNow(entry);
  }
private:
  struct Pending {
    juce::File file;
    juce::String projectId;  // P1.2: needed to look up the sidecar for
                             // the no-change skip in the job / writeNow.
    ProjectSongSettings settings;
    std::string key;
    uint64_t version = 0;
  };
  bool isCurrent(const std::string &key, const uint64_t version) {
    const juce::ScopedLock lock(guard);
    const auto it = generations.find(key);
    return it != generations.end() && it->second == version;
  }
  void timerCallback() override {
    std::vector<Pending> all;
    {
      const juce::ScopedLock lock(guard);
      for (auto &entry : pending) all.push_back(std::move(entry.second));
      pending.clear(); stopTimer();
    }
    for (const auto &entry : all) queue(entry);
  }
  void queue(const Pending &entry) {
    pool.addJob(new ProjectSettingsSaveJob(
                    writeGuard, entry.file, entry.projectId, entry.settings,
                    [this, key = entry.key, version = entry.version] {
                      return isCurrent(key, version);
                    }),
                true);
  }
  void writeNow(const Pending &entry) {
    // P1.2: skip the re-pack when the settings on disk match what we
    // would write. Re-packing the .pwproj would change its mtime and
    // invalidate the cache for the next load (full re-extract of all
    // stems). If we have no projectId we fall back to the old path
    // (safe but no skip) because we can't read the sidecar.
    if (!entry.projectId.isEmpty()) {
      ProjectSongSettings previous;
      if (readCachedSettings(entry.projectId, previous) &&
          ProjectStore::settingsJsonEqual(previous, entry.settings)) {
        return;
      }
    }
    const juce::ScopedLock lock(writeGuard);
    juce::String error;
    if (!ProjectStore::updateSettings(entry.file, entry.settings, &error))
      SessionAuthLogger::write("[Project] Settings flush failed path=" +
                               entry.file.getFullPathName() + " error=" +
                               error);
  }
  juce::CriticalSection guard;
  juce::CriticalSection writeGuard;
  std::map<std::string, Pending> pending;
  std::map<std::string, ProjectSongSettings> cached;
  std::map<std::string, uint64_t> generations;
  juce::ThreadPool pool{1};
};

ProjectSettingsSaveQueue &settingsSaveQueue() {
  static ProjectSettingsSaveQueue queue;
  return queue;
}

juce::var toVar(const StemMixSnapshot &value) {
  auto *o = new juce::DynamicObject();
  o->setProperty("stemId", value.stemId);
  o->setProperty("volume", value.volume);
  o->setProperty("gain", value.gain);
  o->setProperty("mute", value.mute);
  o->setProperty("solo", value.solo);
  o->setProperty("outputChannel", value.outputChannel);
  return o;
}

StemMixSnapshot stemMixFromVar(const juce::var &value) {
  StemMixSnapshot out;
  if (const auto *o = value.getDynamicObject()) {
    out.stemId = o->getProperty("stemId").toString();
    out.volume = juce::jlimit(0.0f, 1.0f, static_cast<float>(o->getProperty("volume")));
    out.gain = juce::jlimit(0.0f, 4.0f, static_cast<float>(o->getProperty("gain")));
    out.mute = static_cast<bool>(o->getProperty("mute"));
    out.solo = static_cast<bool>(o->getProperty("solo"));
    out.outputChannel = juce::jmax(1, static_cast<int>(o->getProperty("outputChannel")));
  }
  return out;
}

juce::var settingsToVar(const ProjectSongSettings &settings) {
  auto *o = new juce::DynamicObject();
  auto *master = new juce::DynamicObject();
  master->setProperty("volume", settings.mix.master.volume);
  master->setProperty("gain", settings.mix.master.gain);
  master->setProperty("mute", settings.mix.master.mute);
  o->setProperty("master", master);
  juce::Array<juce::var> stems;
  for (const auto &stem : settings.mix.stems)
    stems.add(toVar(stem));
  o->setProperty("stems", stems);
  o->setProperty("pitchSemitones", settings.pitchSemitones);
  o->setProperty("tempoBpm", settings.tempoBpm);
  auto *window = new juce::DynamicObject();
  window->setProperty("startSec", settings.window.startSec);
  window->setProperty("endSec", settings.window.endSec);
  window->setProperty("fadeOutSec", settings.window.fadeOutSec);
  o->setProperty("window", window);
  o->setProperty("windowCustomized", settings.windowCustomized);
  ImportedSongData arrangementCarrier;
  arrangementCarrier.arrangement = settings.arrangement;
  o->setProperty("arrangement", SongLibraryStore::songDataToVar(arrangementCarrier)
                                    .getDynamicObject()->getProperty("arrangement"));
  o->setProperty("arrangementCustomized", settings.arrangementCustomized);
  auto *guide = new juce::DynamicObject();
  guide->setProperty("enabled", settings.dynamicGuide.enabled);
  guide->setProperty("timing", static_cast<int>(settings.dynamicGuide.timing));
  guide->setProperty("mode", static_cast<int>(settings.dynamicGuide.mode));
  guide->setProperty("languagePolicy", static_cast<int>(settings.dynamicGuide.languagePolicy));
  guide->setProperty("channelPolicy", static_cast<int>(settings.dynamicGuide.guideChannelPolicy));
  o->setProperty("dynamicGuide", guide);
  o->setProperty("dynamicGuideCustomized", settings.dynamicGuideCustomized);
  auto addVirtualMix = [&o](const char *key, float volume, float gain, bool mute,
                             bool solo, int channel, int width, int division,
                             bool enabled, bool metronome) {
    auto *m = new juce::DynamicObject();
    m->setProperty("volume", volume); m->setProperty("gain", gain);
    m->setProperty("mute", mute); m->setProperty("solo", solo);
    m->setProperty("outputChannel", channel); m->setProperty("outputWidth", width);
    m->setProperty("divisionMask", division); m->setProperty("enabled", enabled);
    m->setProperty("metronomeEnabled", metronome); o->setProperty(key, m);
  };
  addVirtualMix("dynamicGuideMix", settings.dynamicGuideMix.volume,
                settings.dynamicGuideMix.gain, settings.dynamicGuideMix.mute,
                settings.dynamicGuideMix.solo, settings.dynamicGuideMix.outputChannel,
                settings.dynamicGuideMix.outputWidth, 0, true, false);
  addVirtualMix("clickDynMix", settings.clickDynMix.volume, settings.clickDynMix.gain,
                settings.clickDynMix.mute, settings.clickDynMix.solo,
                settings.clickDynMix.outputChannel, settings.clickDynMix.outputWidth,
                settings.clickDynMix.divisionMask, settings.clickDynMix.enabled,
                settings.clickDynMix.metronomeEnabled);
  return o;
}

void parseVirtualMix(const juce::var &value, float &volume, float &gain,
                     bool &mute, bool &solo, int &channel, int &width,
                     int *division = nullptr, bool *enabled = nullptr,
                     bool *metronome = nullptr) {
  const auto *o = value.getDynamicObject();
  if (o == nullptr) return;
  volume = juce::jlimit(0.0f, 1.0f, static_cast<float>(o->getProperty("volume")));
  gain = juce::jlimit(0.0f, 4.0f, static_cast<float>(o->getProperty("gain")));
  mute = static_cast<bool>(o->getProperty("mute")); solo = static_cast<bool>(o->getProperty("solo"));
  channel = juce::jmax(1, static_cast<int>(o->getProperty("outputChannel")));
  width = juce::jlimit(1, 2, static_cast<int>(o->getProperty("outputWidth")));
  if (division != nullptr) *division = static_cast<int>(o->getProperty("divisionMask")) & 0x07;
  if (enabled != nullptr) *enabled = static_cast<bool>(o->getProperty("enabled"));
  if (metronome != nullptr) *metronome = static_cast<bool>(o->getProperty("metronomeEnabled"));
}

ProjectSongSettings settingsFromVar(const juce::var &value) {
  ProjectSongSettings out;
  const auto *o = value.getDynamicObject();
  if (o == nullptr) return out;
  if (const auto *master = o->getProperty("master").getDynamicObject()) {
    out.mix.master.volume = juce::jlimit(0.0f, 1.0f, static_cast<float>(master->getProperty("volume")));
    out.mix.master.gain = juce::jlimit(0.0f, 4.0f, static_cast<float>(master->getProperty("gain")));
    out.mix.master.mute = static_cast<bool>(master->getProperty("mute"));
  }
  if (const auto *stems = o->getProperty("stems").getArray())
    for (const auto &stem : *stems) out.mix.stems.push_back(stemMixFromVar(stem));
  out.pitchSemitones = juce::jlimit(-12, 12, static_cast<int>(o->getProperty("pitchSemitones")));
  out.tempoBpm = static_cast<double>(o->getProperty("tempoBpm"));
  if (const auto *w = o->getProperty("window").getDynamicObject()) {
    out.window.startSec = juce::jmax(0.0, static_cast<double>(w->getProperty("startSec")));
    out.window.endSec = juce::jmax(0.0, static_cast<double>(w->getProperty("endSec")));
    out.window.fadeOutSec = juce::jmax(0.0, static_cast<double>(w->getProperty("fadeOutSec")));
  }
  out.windowCustomized = static_cast<bool>(o->getProperty("windowCustomized"));
  if (o->hasProperty("arrangement")) {
    auto *songObj = new juce::DynamicObject(); songObj->setProperty("arrangement", o->getProperty("arrangement"));
    bool changed = false; out.arrangement = SongLibraryStore::songDataFromVar(juce::var(songObj), changed).arrangement;
  }
  out.arrangementCustomized = static_cast<bool>(o->getProperty("arrangementCustomized"));
  if (const auto *g = o->getProperty("dynamicGuide").getDynamicObject()) {
    out.dynamicGuide.enabled = static_cast<bool>(g->getProperty("enabled"));
    out.dynamicGuide.timing = static_cast<DynamicGuideTiming>(static_cast<int>(g->getProperty("timing")));
    out.dynamicGuide.mode = static_cast<DynamicGuideMode>(static_cast<int>(g->getProperty("mode")));
    out.dynamicGuide.languagePolicy = static_cast<DynamicGuideLanguagePolicy>(static_cast<int>(g->getProperty("languagePolicy")));
    out.dynamicGuide.guideChannelPolicy = static_cast<DynamicGuideChannelPolicy>(static_cast<int>(g->getProperty("channelPolicy")));
  }
  out.dynamicGuideCustomized = static_cast<bool>(o->getProperty("dynamicGuideCustomized"));
  parseVirtualMix(o->getProperty("dynamicGuideMix"), out.dynamicGuideMix.volume, out.dynamicGuideMix.gain,
                  out.dynamicGuideMix.mute, out.dynamicGuideMix.solo, out.dynamicGuideMix.outputChannel,
                  out.dynamicGuideMix.outputWidth);
  parseVirtualMix(o->getProperty("clickDynMix"), out.clickDynMix.volume, out.clickDynMix.gain,
                  out.clickDynMix.mute, out.clickDynMix.solo, out.clickDynMix.outputChannel,
                  out.clickDynMix.outputWidth, &out.clickDynMix.divisionMask,
                  &out.clickDynMix.enabled, &out.clickDynMix.metronomeEnabled);
  return out;
}

ProjectSongSettings settingsFromLegacySetlistItem(
    const SetlistItem &item, const ImportedSongData &song) {
  ProjectSongSettings settings;
  settings.mix = item.mix;
  settings.pitchSemitones = item.pitchSemitones;
  settings.tempoBpm = item.tempoBpm > 0.0
                          ? item.tempoBpm
                          : (song.bpm > 0 ? static_cast<double>(song.bpm)
                                          : 120.0);
  settings.window = item.window;
  if (settings.window.endSec <= 0.0) {
    settings.window.endSec = estimateSongLengthSeconds(song);
    settings.window.fadeOutSec =
        juce::jmin(1.0, settings.window.endSec);
  }
  settings.windowCustomized = item.windowCustomized;
  settings.arrangement = item.arrangement;
  settings.arrangementCustomized = item.arrangementCustomized;
  settings.dynamicGuide = item.dynamicGuide;
  settings.dynamicGuideCustomized = item.dynamicGuideCustomized;
  settings.dynamicGuideMix = item.dynamicGuideMix;
  settings.clickDynMix = item.clickDynMix;
  return settings;
}

juce::File cachedSettingsFile(const juce::String &projectId) {
  if (projectId.trim().isEmpty())
    return {};
  return ProjectStore::getCacheDirectory(projectId)
      .getChildFile("song-settings.json");
}

bool writeCachedSettings(const juce::String &projectId,
                         const ProjectSongSettings &settings) {
  const auto target = cachedSettingsFile(projectId);
  if (target == juce::File{})
    return false;
  auto *root = new juce::DynamicObject();
  root->setProperty("schemaVersion", ProjectStore::kSchemaVersion);
  root->setProperty("songSettings", settingsToVar(settings));
  const auto temp = target.getSiblingFile(target.getFileName() + ".tmp");
  if (!temp.replaceWithText(juce::JSON::toString(juce::var(root), true)))
    return false;
  if (!temp.replaceFileIn(target)) {
    temp.deleteFile();
    return false;
  }
  return true;
}

bool readCachedSettings(const juce::String &projectId,
                        ProjectSongSettings &outSettings) {
  const auto file = cachedSettingsFile(projectId);
  if (!file.existsAsFile())
    return false;
  const auto parsed = juce::JSON::parse(file);
  const auto *root = parsed.getDynamicObject();
  if (root == nullptr ||
      static_cast<int>(root->getProperty("schemaVersion")) !=
          ProjectStore::kSchemaVersion)
    return false;
  outSettings = settingsFromVar(root->getProperty("songSettings"));
  return true;
}

bool copyZipEntry(juce::ZipFile &zip, const juce::String &name,
                  const juce::File &target, juce::String &error,
                  const std::function<void(juce::int64)> &onBytesCopied = {}) {
  const int index = zip.getIndexOfFileName(name);
  if (index < 0) { error = "Project is missing " + name; return false; }
  std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(index));
  if (in == nullptr) { error = "Couldn't read " + name; return false; }
  target.getParentDirectory().createDirectory();
  juce::FileOutputStream out(target);
  juce::int64 copied = 0;
  std::array<char, 64 * 1024> buffer{};
  while (out.openedOk() && !in->isExhausted()) {
    const auto bytesRead =
        in->read(buffer.data(), static_cast<int>(buffer.size()));
    if (bytesRead < 0 || (bytesRead > 0 &&
                          !out.write(buffer.data(),
                                     static_cast<size_t>(bytesRead)))) {
      copied = -1;
      break;
    }
    if (bytesRead == 0)
      break;
    copied += bytesRead;
    if (onBytesCopied)
      onBytesCopied(bytesRead);
  }
  out.flush();
  if (!out.openedOk() || copied < 0 || out.getStatus().failed()) {
    error = "Couldn't extract " + name; return false;
  }
  return true;
}

juce::String internalStemPath(const ImportedStemData &stem, int index) {
  auto ext = juce::File(stem.storedPath).getFileExtension();
  if (ext.isEmpty()) ext = juce::File(stem.sourcePath).getFileExtension();
  if (ext.isEmpty()) ext = ".wav";
  auto id = juce::File::createLegalFileName(stem.stemId);
  if (id.isEmpty()) id = "stem_" + juce::String(index + 1);
  return juce::String(kAudioPrefix) + id + ext.toLowerCase();
}

juce::String artworkExtensionFromUrl(const juce::String &url) {
  auto path = url.upToFirstOccurrenceOf("?", false, false)
                  .upToFirstOccurrenceOf("#", false, false);
  auto ext = juce::File(path).getFileExtension().toLowerCase();
  return ext == ".png" || ext == ".webp" || ext == ".jpg" || ext == ".jpeg"
             ? (ext == ".jpeg" ? ".jpg" : ext)
             : juce::String(".jpg");
}

bool downloadArtworkToCache(const juce::String &url, const juce::File &cache,
                            juce::String &outPath) {
  if (!url.startsWithIgnoreCase("http")) return false;
  juce::URL remote(url);
  auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                     .withExtraHeaders("User-Agent: Mozilla/5.0\r\n")
                     .withConnectionTimeoutMs(8000).withHttpRequestCmd("GET");
  auto stream = remote.createInputStream(options);
  if (stream == nullptr) return false;
  juce::MemoryBlock bytes; stream->readIntoMemoryBlock(bytes);
  if (bytes.getSize() == 0) return false;
  const auto target = cache.getChildFile("artwork" + artworkExtensionFromUrl(url));
  if (!target.replaceWithData(bytes.getData(), bytes.getSize())) return false;
  outPath = target.getFullPathName();
  return true;
}
} // namespace

bool ProjectStore::save(ProjectDocument document, const juce::File &destination,
                        juce::String *errorMessage, double *progress01,
                        const ProjectProgressFn &onProgress) {
  if (errorMessage != nullptr) errorMessage->clear();
  if (progress01 != nullptr) *progress01 = 0.0;
  ProjectProgressFn combinedProgress;
  if (progress01 != nullptr || onProgress) {
    combinedProgress = [progress01, &onProgress](
                           const double progress,
                           const juce::String &phase) {
      if (progress01 != nullptr)
        *progress01 = progress;
      if (onProgress)
        onProgress(progress, phase);
    };
  }
  ProgressThrottle progress(std::move(combinedProgress));
  progress.publish(0.0,
                   L10n::tr("Preparando paquete...", "Preparing package..."),
                   true);
  if (!destination.hasFileExtension(kFileExtension)) {
    if (errorMessage != nullptr) *errorMessage = "Project file must use .pwproj";
    return false;
  }
  auto &song = document.song;
  if (song.id.trim().isEmpty()) song.id = juce::Uuid().toString();
  if (song.projectId.trim().isEmpty()) song.projectId = song.id.trim();
  if (song.stems.empty()) { if (errorMessage != nullptr) *errorMessage = "A project needs at least one stem."; return false; }
  const auto temp = destination.getSiblingFile(destination.getFileName() + ".tmp");
  if (temp.exists() && !temp.deleteFile()) {
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr(
          "No se pudo retirar un archivo temporal anterior. Cierra otras instancias de PlayWorship e inténtalo nuevamente.",
          "Couldn't remove a previous temporary file. Close other PlayWorship instances and try again.");
    SessionAuthLogger::write(
        "[Project] Couldn't remove stale temporary package path=" +
        temp.getFullPathName());
    return false;
  }
  juce::ZipFile::Builder builder;
  const auto stageDir = SongLibraryStore::getRootDirectory()
                            .getChildFile("project-staging")
                            .getChildFile(juce::Uuid().toString());
  stageDir.createDirectory();
  auto cleanupStage = [&stageDir] { if (stageDir.exists()) stageDir.deleteRecursively(); };
  ImportedSongData manifestSong = song;
  manifestSong.id = song.projectId;
  manifestSong.projectId.clear();
  manifestSong.projectPath.clear();
  manifestSong.fileType = "FLAC";
  const auto fallbackSongDuration = estimateSongLengthSeconds(song);
  std::vector<double> stemWork;
  stemWork.reserve(song.stems.size());
  double totalStemWork = 0.0;
  for (const auto &stem : song.stems) {
    const juce::File source(stem.storedPath.isNotEmpty() ? stem.storedPath
                                                         : stem.sourcePath);
    const auto weight = stem.durationSec > 0.0
                            ? stem.durationSec
                            : juce::jmax(1.0, static_cast<double>(source.getSize()) /
                                                 (1024.0 * 1024.0));
    stemWork.push_back(weight);
    totalStemWork += weight;
  }
  totalStemWork = juce::jmax(1.0, totalStemWork);
  double completedStemWork = 0.0;
  juce::int64 estimatedPackageBytes = 4096;
  for (size_t i = 0; i < song.stems.size(); ++i) {
    auto &stem = song.stems[i];
    const juce::File source(stem.storedPath.isNotEmpty() ? stem.storedPath : stem.sourcePath);
    if (!source.existsAsFile()) {
      cleanupStage();
      if (errorMessage != nullptr)
        *errorMessage = "Stem source missing: " + source.getFullPathName();
      return false;
    }
    juce::File packagedSource = source;
    const auto projectStemExtension =
        SongLibraryStore::projectStorageStemExtension();
    if (!source.hasFileExtension(projectStemExtension)) {
      const auto legalId = juce::File::createLegalFileName(stem.stemId);
      const auto converted = stageDir.getChildFile(
          (legalId.isNotEmpty() ? legalId : juce::String("stem_") + juce::String(static_cast<int>(i + 1))) +
          projectStemExtension);
      juce::String conversionError;
      const auto phase =
          L10n::tr("Convirtiendo ", "Converting ") +
          (stem.displayName.isNotEmpty() ? stem.displayName
                                         : source.getFileNameWithoutExtension());
      const auto progressDuration = stem.durationSec > 0.0
                                        ? stem.durationSec
                                        : fallbackSongDuration;
      if (!SongLibraryStore::convertStemForProject(
              source, converted, conversionError, progressDuration,
              [&](const double stemProgress, const juce::String &) {
                const auto overall =
                    (completedStemWork + stemWork[i] * stemProgress) /
                    totalStemWork;
                progress.publish(0.60 * overall, phase);
              })) {
        cleanupStage();
        if (errorMessage != nullptr) *errorMessage = conversionError;
        return false;
      }
      packagedSource = converted;
    }
    completedStemWork += stemWork[i];
    progress.publish(0.60 * completedStemWork / totalStemWork,
                     L10n::tr("Preparando stems...", "Preparing stems..."));
    ImportedStemData packagedStem = stem;
    packagedStem.storedPath = packagedSource.getFullPathName();
    const auto path = internalStemPath(packagedStem, static_cast<int>(i));
    builder.addFile(packagedSource, 0, path);
    estimatedPackageBytes += packagedSource.getSize();
    manifestSong.stems[i].storedPath = path;
    manifestSong.stems[i].sourcePath.clear();
    manifestSong.stems[i].fileName = juce::File(path).getFileName();
  }
  if (juce::File artwork(song.artworkPath); juce::File(song.artworkPath).existsAsFile()) {
    const auto artworkPath = "artwork" + juce::File(song.artworkPath).getFileExtension().toLowerCase();
    builder.addFile(juce::File(song.artworkPath), 0, artworkPath);
    estimatedPackageBytes += juce::File(song.artworkPath).getSize();
    manifestSong.artworkPath = artworkPath; manifestSong.artworkSource.clear();
  } else { manifestSong.artworkPath.clear(); }
  auto *root = new juce::DynamicObject();
  root->setProperty("schemaVersion", kSchemaVersion);
  root->setProperty("audioFormat", kProjectAudioFormat);
  root->setProperty("song", SongLibraryStore::songDataToVar(manifestSong));
  root->setProperty("songSettings", settingsToVar(document.settings));
  const auto manifest = juce::JSON::toString(juce::var(root), true);
  estimatedPackageBytes += manifest.getNumBytesAsUTF8();
  builder.addEntry(new juce::MemoryInputStream(manifest.toRawUTF8(), static_cast<size_t>(manifest.getNumBytesAsUTF8()), true),
                   0, kManifestName, juce::Time::getCurrentTime());
  if (!document.waveformBins.empty() &&
      document.waveformBins.size() == static_cast<size_t>(kWaveformPreviewBinCount)) {
    const auto byteCount = document.waveformBins.size() * sizeof(float);
    estimatedPackageBytes += static_cast<juce::int64>(byteCount);
    builder.addEntry(new juce::MemoryInputStream(document.waveformBins.data(), byteCount, true),
                     0, kWaveformName, juce::Time::getCurrentTime());
  }
  progress.publish(0.60,
                   L10n::tr("Escribiendo proyecto...", "Writing project..."),
                   true);
  temp.getParentDirectory().createDirectory();
  bool wrote = false;
  auto writeStatus = juce::Result::ok();
  {
    // Windows does not allow MoveFile/ReplaceFile while this output handle is
    // open because JUCE shares it for reads only. Keep both stream wrappers in
    // this scope so their destructors run before replaceFileIn() below.
    juce::FileOutputStream out(temp);
    if (out.openedOk()) {
      ProgressOutputStream progressOut(
          out, estimatedPackageBytes,
          [&progress](const double writeProgress) {
            progress.publish(
                0.60 + 0.40 * writeProgress,
                L10n::tr("Escribiendo proyecto...", "Writing project..."));
          });
      wrote = builder.writeToStream(progressOut, nullptr);
      progressOut.flush();
      out.flush();
    }
    writeStatus = out.getStatus();
  }
  if (!wrote || writeStatus.failed()) {
    cleanupStage();
    const bool removedTemporary = !temp.exists() || temp.deleteFile();
    SessionAuthLogger::write(
        "[Project] Package write failed temp=" + temp.getFullPathName() +
        " status=\"" + writeStatus.getErrorMessage() +
        "\" removedTemporary=" + juce::String(removedTemporary ? 1 : 0));
    if (errorMessage != nullptr)
      *errorMessage = "Couldn't write project package.";
    return false;
  }
  if (!replaceProjectFileWithRetry(temp, destination)) {
    const auto temporaryBytes = temp.getSize();
    const bool removedTemporary = !temp.exists() || temp.deleteFile();
    cleanupStage();
    SessionAuthLogger::write(
        "[Project] Package replace failed temp=" + temp.getFullPathName() +
        " destination=" + destination.getFullPathName() +
        " tempBytes=" + juce::String(temporaryBytes) +
        " destinationExists=" + juce::String(destination.exists() ? 1 : 0) +
        " removedTemporary=" + juce::String(removedTemporary ? 1 : 0));
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr(
          "No se pudo reemplazar el proyecto. Verifica que el archivo no esté abierto en otra aplicación e inténtalo nuevamente.",
          "Couldn't replace the project. Make sure the file isn't open in another application and try again.");
    return false;
  }
  cleanupStage();
  document.projectFile = destination; recordRecent(document);
  progress.publish(1.0,
                   L10n::tr("Proyecto guardado", "Project saved"), true);
  return true;
}

bool ProjectStore::open(const juce::File &file, ProjectDocument &outDocument,
                        juce::String *errorMessage, const bool rebuildCache,
                        const ProjectProgressFn &onProgress) {
  if (errorMessage != nullptr) errorMessage->clear();
  SessionAuthLogger::write("[ProjectOpen] begin path=" +
                           file.getFullPathName() + " exists=" +
                           juce::String(file.existsAsFile() ? 1 : 0) +
                           " size=" + juce::String(file.getSize()) +
                           " rebuildCache=" +
                           juce::String(rebuildCache ? 1 : 0));
  ProgressThrottle progress(onProgress);
  progress.publish(0.0,
                   L10n::tr("Validando manifiesto...",
                            "Validating manifest..."),
                   true);
  if (!file.existsAsFile() || !file.hasFileExtension(kFileExtension)) {
    SessionAuthLogger::write("[ProjectOpen] invalid file path=" +
                             file.getFullPathName());
    if (errorMessage != nullptr) *errorMessage = "Project file not found or invalid."; return false;
  }
  juce::ZipFile zip(file);
  const int manifestIndex = zip.getIndexOfFileName(kManifestName);
  if (manifestIndex < 0) {
    SessionAuthLogger::write("[ProjectOpen] manifest missing path=" +
                             file.getFullPathName());
    if (errorMessage != nullptr) *errorMessage = "Project manifest is missing."; return false;
  }
  std::unique_ptr<juce::InputStream> manifestStream(zip.createStreamForEntry(manifestIndex));
  if (manifestStream == nullptr) {
    SessionAuthLogger::write("[ProjectOpen] manifest unreadable path=" +
                             file.getFullPathName());
    if (errorMessage != nullptr) *errorMessage = "Couldn't read project manifest."; return false;
  }
  const auto root = juce::JSON::parse(manifestStream->readEntireStreamAsString());
  const auto *o = root.getDynamicObject();
  if (o == nullptr || static_cast<int>(o->getProperty("schemaVersion")) != kSchemaVersion) {
    SessionAuthLogger::write("[ProjectOpen] unsupported manifest path=" +
                             file.getFullPathName() + " schema=" +
                             (o != nullptr ? o->getProperty("schemaVersion").toString()
                                           : juce::String("<null>")));
    if (errorMessage != nullptr) *errorMessage = "Unsupported project manifest."; return false;
  }
  bool changed = false;
  auto song = SongLibraryStore::songDataFromVar(o->getProperty("song"), changed);
  if (song.id.isEmpty() || song.stems.empty()) {
    SessionAuthLogger::write("[ProjectOpen] invalid song payload path=" +
                             file.getFullPathName() + " songId=" + song.id +
                             " stems=" +
                             juce::String(static_cast<int>(song.stems.size())));
    if (errorMessage != nullptr) *errorMessage = "Project has no valid song or stems."; return false;
  }
  song.projectId = song.id.trim();
  auto entrySize = [&zip](const juce::String &name) -> juce::int64 {
    const auto index = zip.getIndexOfFileName(name);
    const auto *entry = index >= 0 ? zip.getEntry(index) : nullptr;
    return entry != nullptr ? entry->uncompressedSize : 0;
  };
  juce::int64 totalResourceBytes = 0;
  for (const auto &stem : song.stems) {
    const auto bytes = entrySize(stem.storedPath);
    totalResourceBytes += bytes;
  }
  if (song.artworkPath.isNotEmpty())
    totalResourceBytes += entrySize(song.artworkPath);
  totalResourceBytes += entrySize(kWaveformName);
  totalResourceBytes = juce::jmax<juce::int64>(1, totalResourceBytes);
  // processedResourceBytes is read/written from the parallel extract workers
  // (Phase 2 below). It must be atomic to avoid a data race on the counter.
  // We do NOT call progress.publish from the workers because
  // ProgressThrottle has its own non-atomic state.
  std::atomic<juce::int64> processedResourceBytes{0};
  auto reportResourceBytes = [&](const juce::int64 bytes,
                                 const juce::String &resourceName) {
    processedResourceBytes.fetch_add(
        juce::jmax<juce::int64>(0, bytes), std::memory_order_acq_rel);
    const auto resourceProgress =
        static_cast<double>(processedResourceBytes.load(
            std::memory_order_acquire)) /
        static_cast<double>(totalResourceBytes);
    progress.publish(
        0.05 + 0.90 * juce::jlimit(0.0, 1.0, resourceProgress),
        L10n::tr("Validando ", "Validating ") + resourceName);
  };
  progress.publish(0.05,
                   L10n::tr("Validando recursos...",
                            "Validating resources..."),
                   true);
  ProjectSongSettings newerCachedSettings;
  const auto settingsCacheFile = cachedSettingsFile(song.id);
  const bool hasNewerCachedSettings =
      rebuildCache && settingsCacheFile.existsAsFile() &&
      settingsCacheFile.getLastModificationTime() >
          file.getLastModificationTime() &&
      readCachedSettings(song.id, newerCachedSettings);
  auto cache = getCacheDirectory(song.id);
  const bool shouldRebuildCache =
      rebuildCache && !cacheMatchesProject(file, cache);
  SessionAuthLogger::write("[ProjectOpen] manifest ok songId=" + song.id +
                           " stems=" +
                           juce::String(static_cast<int>(song.stems.size())) +
                           " cache=" + cache.getFullPathName() +
                           " shouldRebuild=" +
                           juce::String(shouldRebuildCache ? 1 : 0) +
                           " cachedSettingsNewer=" +
                           juce::String(hasNewerCachedSettings ? 1 : 0));
  if (shouldRebuildCache && cache.exists() && !cache.deleteRecursively()) {
    SessionAuthLogger::write("[ProjectOpen] cache delete failed path=" +
                             cache.getFullPathName());
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr(
          "No se pudo actualizar la caché del proyecto. Detén la canción e inténtalo nuevamente.",
          "Couldn't refresh the project cache. Stop the song and try again.");
    return false;
  }
  cache.createDirectory();
  juce::String localError;

  // ---- Phase 1: plan the work serially ----------------------------
  // Pre-resolve target paths, expected sizes, and which stems actually need
  // extraction. Doing this in one pass keeps the parallel section below
  // free of juce::ZipFile calls (juce::ZipFile::createStreamForEntry is not
  // thread-safe when sharing a single instance).
  struct StemWork {
    juce::String internalPath;
    juce::File target;
    juce::int64 expectedSize = 0;
    bool needsExtraction = false;
    // Plain bool, not atomic: this is only read in the main thread after
    // joining all workers, and the std::thread::join call provides the
    // happens-before relationship so writes by workers are visible.
    // std::atomic<bool> would also work but is not copy/move-constructible
    // and breaks std::vector<StemWork>.
    bool ok = true;
  };
  std::vector<StemWork> work;
  work.reserve(song.stems.size());
  for (const auto &stem : song.stems) {
    const auto internalPath = stem.storedPath;
    if (!internalPath.startsWith(kAudioPrefix) || internalPath.contains("..")) {
      SessionAuthLogger::write("[ProjectOpen] unsafe stem path songId=" +
                               song.id + " path=" + internalPath);
      localError = "Unsafe stem path in project.";
      break;
    }
    StemWork sw;
    sw.internalPath = internalPath;
    sw.target = cache.getChildFile(internalPath);
    sw.expectedSize = entrySize(internalPath);
    sw.needsExtraction = shouldRebuildCache || !sw.target.existsAsFile();
    work.push_back(std::move(sw));
  }

  if (localError.isEmpty()) {
    // ---- Phase 2: extract in parallel ------------------------------
    // Each worker opens its own juce::ZipFile from the same path. The OS
    // allows concurrent reads of the same file in read-only mode, and
    // each ZipFile instance owns its file handle and entry table, so this
    // is safe. Workers bail early on the first failure (anyError flag).
    //
    // Empirical: the sequential path runs at 16-43 MB/s per ZIP, so a
    // 766 MB project takes 19-26s. With 4 workers the SSD can sustain
    // ~150-200 MB/s of parallel write, so we expect 4-8x speedup on
    // multi-stem cold loads. The validation pass below stays serial
    // because isDecodableFlacFile is <1ms per stem.
    std::atomic<size_t> nextIndex{0};
    std::atomic<bool> anyError{false};
    const int numCpus =
        static_cast<int>(juce::SystemStats::getNumCpus());
    const size_t numWorkers = static_cast<size_t>(
        juce::jlimit(1, 4, juce::jmax(1, numCpus - 1)));
    const size_t effectiveWorkers =
        juce::jmin(numWorkers, work.size());
    std::vector<std::thread> workers;
    workers.reserve(effectiveWorkers);
    for (size_t w = 0; w < effectiveWorkers; ++w) {
      workers.emplace_back([&]() {
        // Each worker opens its own ZipFile. The constructor parses the
        // central directory (~few ms for a typical .pwproj) and gives
        // us a private handle for stream creation.
        juce::ZipFile workerZip(file);
        while (!anyError.load(std::memory_order_acquire)) {
          const size_t i =
              nextIndex.fetch_add(1, std::memory_order_acq_rel);
          if (i >= work.size())
            return;
          StemWork &sw = work[i];
          if (!sw.needsExtraction) {
            // Already on disk; no I/O, just count the bytes for the
            // progress bar (we don't call progress.publish from the
            // workers to avoid the throttle data race).
            processedResourceBytes.fetch_add(
                juce::jmax<juce::int64>(0, sw.expectedSize),
                std::memory_order_acq_rel);
            continue;
          }
          juce::String perWorkerError;
          if (!copyZipEntry(workerZip, sw.internalPath, sw.target,
                            perWorkerError,
                            [&](const juce::int64 bytes) {
                              processedResourceBytes.fetch_add(
                                  juce::jmax<juce::int64>(0, bytes),
                                  std::memory_order_acq_rel);
                            })) {
            sw.ok = false;
            anyError.store(true, std::memory_order_release);
            return;
          }
        }
      });
    }
    for (auto &t : workers)
      t.join();

    if (anyError.load(std::memory_order_acquire)) {
      // Find the first failed stem to populate the user-facing error.
      for (const auto &sw : work) {
        if (!sw.ok) {
          SessionAuthLogger::write("[ProjectOpen] extract failed songId=" +
                                   song.id + " path=" + sw.internalPath);
          localError = "Couldn't extract " + sw.internalPath;
          break;
        }
      }
      if (localError.isEmpty())
        localError = "Couldn't extract one or more stems.";
    } else {
      progress.publish(0.95,
                       L10n::tr("Validando...", "Validating..."), true);
    }

    // ---- Phase 3: validate serially + commit paths -----------------
    for (size_t i = 0; i < work.size(); ++i) {
      const StemWork &sw = work[i];
      auto &stem = song.stems[i];
      if (!sw.ok) {
        if (localError.isEmpty())
          localError = "Couldn't extract " + sw.internalPath;
        break;
      }
      if (!sw.target.hasFileExtension(
              SongLibraryStore::projectStorageStemExtension()) ||
          !([&]() {
            // SongLoadProbe: time the validation pass. This is the 3rd
            // createReaderFor per stem in cold (after copyZipEntry wrote
            // the file and loadSongStems will open it again for
            // playback). At ~300us per stem this is a fixed 5-10ms tax
            // we can either keep, drop for canonical .pwproj authors
            // (own app wrote the ZIP), or move to a background thread.
            const int64_t validateStart =
                juce::Time::getHighResolutionTicks();
            const bool decodable = isDecodableFlacFile(sw.target);
            const int64_t validateUs = static_cast<int64_t>(
                (juce::Time::getHighResolutionTicks() - validateStart) *
                1000000.0 /
                juce::Time::getHighResolutionTicksPerSecond());
            SongLoadProbe::instance().recordDurationUs(
                "loadSong", "project.open.isDecodableFlac", validateUs,
                "stem=" + sw.target.getFileName() +
                    " ok=" + (decodable ? "1" : "0"));
            return decodable;
          }())) {
        localError = L10n::tr("No se puede decodificar el stem: ",
                              "Couldn't decode stem: ") +
                     sw.target.getFileName();
        SessionAuthLogger::write("[ProjectOpen] decode failed songId=" +
                                 song.id + " stem=" +
                                 sw.target.getFullPathName());
        break;
      }
      stem.storedPath = sw.target.getFullPathName();
      stem.sourcePath = stem.storedPath;
      stem.fileName = sw.target.getFileName();
    }
  }
  if (localError.isEmpty() && song.artworkPath.isNotEmpty()) {
    const auto artworkName = juce::File(song.artworkPath).getFileName();
    if (artworkName != song.artworkPath || !artworkName.startsWith("artwork.")) {
      SessionAuthLogger::write("[ProjectOpen] unsafe artwork path songId=" +
                               song.id + " artwork=" + song.artworkPath);
      localError = "Unsafe artwork path in project.";
    }
    else {
      const auto artworkTarget = cache.getChildFile(artworkName);
      const bool needsExtraction =
          shouldRebuildCache || !artworkTarget.existsAsFile();
      const bool extracted = !needsExtraction ||
                             copyZipEntry(zip, song.artworkPath, artworkTarget,
                                          localError,
                                          [&](const juce::int64 bytes) {
                                            reportResourceBytes(
                                                bytes,
                                                artworkTarget.getFileName());
                                          });
      if (!needsExtraction)
        reportResourceBytes(entrySize(song.artworkPath), artworkName);
      if (extracted)
        song.artworkPath = artworkTarget.getFullPathName();
    }
  }
  if (localError.isEmpty() && song.artworkPath.isEmpty() &&
      song.artworkSource.isNotEmpty()) {
    juce::String downloadedArtwork;
    const auto cachedArtwork = cache.getChildFile(
        "artwork" + artworkExtensionFromUrl(song.artworkSource));
    if (!shouldRebuildCache && cachedArtwork.existsAsFile())
      song.artworkPath = cachedArtwork.getFullPathName();
    else if (downloadArtworkToCache(song.artworkSource, cache, downloadedArtwork))
      song.artworkPath = downloadedArtwork;
  }
  if (localError.isNotEmpty()) {
    SessionAuthLogger::write("[ProjectOpen] failed songId=" + song.id +
                             " error=\"" + localError + "\"");
    cache.deleteRecursively();
    if (errorMessage != nullptr) *errorMessage = localError; return false;
  }
  song.projectPath = file.getFullPathName();
  std::vector<float> waveformBins;
  const int waveformIndex = zip.getIndexOfFileName(kWaveformName);
  if (waveformIndex >= 0) {
    const auto *entry = zip.getEntry(waveformIndex);
    const auto expectedBytes = static_cast<juce::int64>(kWaveformPreviewBinCount) *
                               static_cast<juce::int64>(sizeof(float));
    if (entry == nullptr || entry->uncompressedSize != expectedBytes) {
      SessionAuthLogger::write("[ProjectOpen] waveform size invalid songId=" +
                               song.id + " expected=" +
                               juce::String(expectedBytes));
      cache.deleteRecursively();
      if (errorMessage != nullptr) *errorMessage = "Project waveform has an invalid size.";
      return false;
    }
    std::unique_ptr<juce::InputStream> waveformStream(zip.createStreamForEntry(waveformIndex));
    if (waveformStream == nullptr) {
      SessionAuthLogger::write("[ProjectOpen] waveform unreadable songId=" +
                               song.id);
      cache.deleteRecursively();
      if (errorMessage != nullptr) *errorMessage = "Couldn't read project waveform.";
      return false;
    }
    waveformBins.resize(kWaveformPreviewBinCount);
    const auto bytes = static_cast<int>(expectedBytes);
    if (waveformStream->read(waveformBins.data(), bytes) != bytes) {
      SessionAuthLogger::write("[ProjectOpen] waveform extract failed songId=" +
                               song.id);
      cache.deleteRecursively();
      if (errorMessage != nullptr) *errorMessage = "Couldn't extract project waveform.";
      return false;
    }
    reportResourceBytes(expectedBytes, kWaveformName);
    SongLibraryStore::saveWaveformCache(song.id, waveformBins);
  }
  progress.publish(0.96,
                   L10n::tr("Restaurando configuración...",
                            "Restoring settings..."),
                   true);
  auto settings = hasNewerCachedSettings
                      ? newerCachedSettings
                      : settingsFromVar(o->getProperty("songSettings"));
  if (rebuildCache) {
    settingsSaveQueue().remember(file, settings);
    writeCachedSettings(song.id, settings);
    if (!writeCacheProjectMarker(file, cache))
      SessionAuthLogger::write(
          "[Project] Couldn't persist cache source marker path=" +
          cache.getFullPathName());
  }
  outDocument = {std::move(song), std::move(settings),
                 std::move(waveformBins), file};
  recordRecent(outDocument);
  if (hasNewerCachedSettings)
    settingsSaveQueue().schedule(file, song.id, outDocument.settings);
  SessionAuthLogger::write("[ProjectOpen] success songId=" + outDocument.song.id +
                           " cache=" + cache.getFullPathName() +
                           " stems=" +
                           juce::String(static_cast<int>(outDocument.song.stems.size())));
  progress.publish(1.0,
                   L10n::tr("Proyecto validado", "Project validated"), true);
  return true;
}

bool ProjectStore::convertLegacySongToProject(
    const juce::String &legacySongId, const juce::File &destination,
    ImportedSongData &outProjectSong, juce::String &outSourceSetlistName,
    juce::String *errorMessage,
    const ProjectMigrationProgressFn &onProgress) {
  if (errorMessage != nullptr)
    errorMessage->clear();
  outProjectSong = {};
  outSourceSetlistName.clear();

  if (destination.exists()) {
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr(
          "Ya existe un archivo con ese nombre. Elige otro destino.",
          "A file with that name already exists. Choose another destination.");
    return false;
  }

  ImportedSongData legacySong;
  SetlistItem sourceSettings;
  if (!SongLibraryStore::prepareLegacyProjectMigration(
          legacySongId, legacySong, sourceSettings, outSourceSetlistName,
          errorMessage))
    return false;

  const auto legacyAudioDirectory = SongLibraryStore::getRootDirectory()
                                        .getChildFile("audio")
                                        .getChildFile(legacySong.id);
  if (destination.isAChildOf(legacyAudioDirectory)) {
    if (errorMessage != nullptr)
      *errorMessage = L10n::tr(
          "El proyecto no puede guardarse dentro de la carpeta legacy que será eliminada.",
          "The project can't be saved inside the legacy folder that will be removed.");
    return false;
  }

  if (onProgress)
    onProgress(0.05, L10n::tr("Preparando canción legacy...",
                              "Preparing legacy song..."));
  ProjectDocument document;
  document.song = legacySong;
  document.song.projectId = legacySong.id;
  document.settings = settingsFromLegacySetlistItem(sourceSettings, legacySong);
  document.waveformBins = SongLibraryStore::loadWaveformCache(legacySong.id);
  if (document.waveformBins.empty()) {
    document.waveformBins = buildMixedWaveformBins(
        legacySong, kWaveformPreviewBinCount,
        [&onProgress](const double waveformProgress) {
          if (onProgress)
            onProgress(0.08 + 0.22 * juce::jlimit(0.0, 1.0, waveformProgress),
                       L10n::tr("Generando forma de onda...",
                                "Generating waveform..."));
        });
  }

  if (onProgress)
    onProgress(0.32, L10n::tr("Empaquetando proyecto...",
                              "Packaging project..."));
  juce::String migrationError;
  if (!save(document, destination, &migrationError, nullptr,
            [&onProgress](const double saveProgress,
                          const juce::String &phase) {
              if (onProgress)
                onProgress(0.32 + 0.50 * saveProgress, phase);
            })) {
    if (errorMessage != nullptr)
      *errorMessage = migrationError;
    return false;
  }

  auto rollbackPackage = [&destination] {
    ProjectStore::forgetRecentProject(destination);
    if (destination.existsAsFile())
      destination.deleteFile();
  };

  if (onProgress)
    onProgress(0.82, L10n::tr("Validando proyecto...",
                              "Validating project..."));
  ProjectDocument opened;
  if (!open(destination, opened, &migrationError, true,
            [&onProgress](const double openProgress,
                          const juce::String &phase) {
              if (onProgress)
                onProgress(0.82 + 0.12 * openProgress, phase);
            })) {
    rollbackPackage();
    if (errorMessage != nullptr)
      *errorMessage = migrationError;
    return false;
  }

  if (onProgress)
    onProgress(0.94, L10n::tr("Reemplazando estructura legacy...",
                              "Replacing legacy structure..."));
  if (!SongLibraryStore::finalizeLegacyProjectMigration(
          legacySong.id, opened.song, &migrationError)) {
    rollbackPackage();
    if (errorMessage != nullptr)
      *errorMessage = migrationError;
    return false;
  }

  opened.song.id = legacySong.id;
  outProjectSong = std::move(opened.song);
  if (onProgress)
    onProgress(1.0, L10n::tr("Conversión completada",
                             "Conversion completed"));
  return true;
}

bool ProjectStore::updateSettings(const juce::File &file, const ProjectSongSettings &settings,
                                  juce::String *errorMessage) {
  // P1.2: the .pwproj no longer needs to be re-packed to persist
  // settings. The settings sidecar (project-cache/<id>/song-settings.json)
  // is the authoritative source of truth, and ProjectStore::open already
  // prefers it over the embedded manifest when its mtime is newer.
  // Re-packing the .pwproj was changing the ZIP's mtime, which
  // cacheMatchesProject() interpreted as "cache invalid", forcing a
  // full re-extract of every stem on the very next load. Settings
  // changes are now flushed to the sidecar (via writeCachedSettings
  // in scheduleSettingsSave) and any explicit "Save As" path still
  // re-packs because it goes through ProjectStore::save directly.
  return writeCachedSettingsFromFile(file, settings);
}

bool ProjectStore::writeCachedSettingsFromFile(const juce::File &file,
                                              const ProjectSongSettings &settings) {
  // Derive the projectId from the file by reading the manifest. The
  // manifest is small (~2 KB) and we only do this when settings
  // actually change. If the file is unreadable we silently no-op so
  // we don't lose the user's settings on the next load (the cached
  // version is still authoritative via writeCachedSettings later in
  // scheduleSettingsSave).
  juce::ZipFile zip(file);
  const int manifestIndex = zip.getIndexOfFileName(kManifestName);
  if (manifestIndex < 0) return false;
  std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(manifestIndex));
  if (in == nullptr) return false;
  const auto root = juce::JSON::parse(in->readEntireStreamAsString());
  const auto *o = root.getDynamicObject();
  if (o == nullptr) return false;
  const auto *songObj = o->getProperty("song").getDynamicObject();
  if (songObj == nullptr) return false;
  const auto songId = songObj->getProperty("id").toString();
  if (songId.isEmpty()) return false;
  return writeCachedSettings(songId, settings);
}

void ProjectStore::scheduleSettingsSave(const juce::File &file,
                                        ProjectSongSettings settings) {
  settingsSaveQueue().schedule(file, std::move(settings));
}

void ProjectStore::scheduleSettingsSave(const juce::File &file,
                                        const juce::String &projectId,
                                        ProjectSongSettings settings) {
  // P1.2 (perf): skip the re-pack of the .pwproj ZIP when the new
  // settings are byte-identical to what we last persisted. Re-packing
  // changes the .pwproj mtime, which made cacheMatchesProject() return
  // false on the next load, which forced a full re-extract of every
  // stem (8-30s per cold-rebuild). Comparing the serialized JSON
  // catches both real field changes and ordering noise — a more
  // robust structural compare would be slower and is unnecessary here
  // because the var is built deterministically by settingsToVar().
  ProjectSongSettings previous;
  if (readCachedSettings(projectId, previous) &&
      ProjectStore::settingsJsonEqual(previous, settings)) {
    // Nothing changed on disk since the last save. Both the sidecar
    // and the eventual .pwproj re-pack are no-ops. Return early so we
    // do not enqueue a job that would touch the .pwproj for nothing.
    return;
  }
  if (!writeCachedSettings(projectId, settings))
    SessionAuthLogger::write(
        "[Project] Couldn't persist local settings cache id=" + projectId);
  if (file.existsAsFile())
    settingsSaveQueue().schedule(file, projectId, std::move(settings));
  else if (file != juce::File{})
    SessionAuthLogger::write(
        "[Project] Package unavailable; settings cached locally path=" +
        file.getFullPathName());
}

bool ProjectStore::settingsJsonEqual(const ProjectSongSettings &a,
                                    const ProjectSongSettings &b) {
  // settingsToVar builds a juce::DynamicObject with deterministic key
  // insertion order (setProperty appends in the order the fields are
  // written). Two ProjectSongSettings with the same field values
  // therefore serialize to the same JSON string. This is robust
  // enough for the cache-skip use case: a false positive (declared
  // equal when they actually differ) would mean we skip a needed
  // re-pack, which would desync the .pwproj manifest. We avoid that
  // by being conservative: when in doubt, write.
  return juce::JSON::toString(settingsToVar(a)) ==
         juce::JSON::toString(settingsToVar(b));
}

bool ProjectStore::getCachedSettings(const juce::File &file,
                                     ProjectSongSettings &outSettings) {
  if (settingsSaveQueue().getCached(file, outSettings))
    return true;
  ProjectDocument document;
  if (!open(file, document, nullptr, false))
    return false;
  outSettings = document.settings;
  settingsSaveQueue().remember(file, outSettings);
  return true;
}

bool ProjectStore::getCachedSettings(const juce::String &projectId,
                                     ProjectSongSettings &outSettings) {
  return readCachedSettings(projectId, outSettings);
}

void ProjectStore::flushPendingSettings(const juce::File &file) {
  settingsSaveQueue().flush(file);
}

void ProjectStore::flushAllPendingSettings() {
  settingsSaveQueue().flushAll();
}

bool ProjectStore::updateWaveform(const juce::File &file,
                                  const std::vector<float> &waveformBins,
                                  juce::String *errorMessage) {
  if (waveformBins.size() != static_cast<size_t>(kWaveformPreviewBinCount)) {
    if (errorMessage != nullptr) *errorMessage = "Invalid project waveform.";
    return false;
  }
  // P1.2: skip the re-pack of the .pwproj ZIP when the embedded
  // waveform is "close enough" to the new one. The waveform is
  // regenerated from the stems by a background job (PluginEditor.cpp
  // buildMixedWaveformBins -> updateWaveform). Even with the same
  // stems and the same mix state, floating-point summation order is
  // not bit-deterministic across runs, so an exact memcmp would
  // always see a 1-ULP difference and never skip. A relative
  // tolerance of 1% max sample error matches the precision the
  // waveform actually conveys to the user (it's downsampled to
  // kWaveformPreviewBinCount bins and rendered at single-pixel
  // resolution in the WaveformBar).
  constexpr float kSkipRelativeTolerance = 0.01f;
  ProjectDocument document;
  if (!open(file, document, errorMessage, false)) return false;
  if (document.waveformBins.size() == waveformBins.size()) {
    bool withinTolerance = true;
    for (size_t i = 0; i < waveformBins.size(); ++i) {
      const float a = document.waveformBins[i];
      const float b = waveformBins[i];
      const float denom = juce::jmax(1e-6f, juce::jmax(std::fabs(a), std::fabs(b)));
      if (std::fabs(a - b) / denom > kSkipRelativeTolerance) {
        withinTolerance = false;
        break;
      }
    }
    if (withinTolerance) {
      return true;
    }
  }
  document.waveformBins = waveformBins;
  return save(std::move(document), file, errorMessage);
}

juce::File ProjectStore::getCacheDirectory(const juce::String &projectId) {
  auto root = SongLibraryStore::getRootDirectory().getChildFile("project-cache");
  root.createDirectory();
  return root.getChildFile(juce::File::createLegalFileName(projectId));
}

juce::File ProjectStore::getRegistryFile() { return SongLibraryStore::getRootDirectory().getChildFile("projects.json"); }

std::vector<RecentProjectInfo> ProjectStore::getRecentProjects() {
  std::vector<RecentProjectInfo> out; const auto file = getRegistryFile();
  if (!file.existsAsFile()) return out;
  const auto parsed = juce::JSON::parse(file); const auto *root = parsed.getDynamicObject();
  if (root == nullptr) return out; const auto *items = root->getProperty("recent").getArray();
  if (items == nullptr) return out;
  for (const auto &item : *items) if (const auto *o = item.getDynamicObject()) {
    RecentProjectInfo info; info.id = o->getProperty("id").toString(); info.title = o->getProperty("title").toString();
    info.file = juce::File(o->getProperty("path").toString()); info.lastOpenedMs = static_cast<juce::int64>(o->getProperty("lastOpenedMs")); info.available = info.file.existsAsFile(); out.push_back(std::move(info));
  }
  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.lastOpenedMs > b.lastOpenedMs; }); return out;
}

juce::File ProjectStore::findRecentProjectFile(
    const juce::String &projectId) {
  const auto recent = getRecentProjects();
  const auto it = std::find_if(
      recent.begin(), recent.end(),
      [&projectId](const RecentProjectInfo &candidate) {
        return candidate.id == projectId;
      });
  return it != recent.end() ? it->file : juce::File{};
}

void ProjectStore::recordRecent(const ProjectDocument &document) {
  auto entries = getRecentProjects();
  entries.erase(std::remove_if(entries.begin(), entries.end(), [&document](const auto &item) { return item.file == document.projectFile; }), entries.end());
  entries.insert(entries.begin(), {projectIdentityForSong(document.song), document.song.title, document.projectFile, juce::Time::getCurrentTime().toMilliseconds(), true});
  auto *root = new juce::DynamicObject(); juce::Array<juce::var> items;
  for (const auto &entry : entries) { auto *o = new juce::DynamicObject(); o->setProperty("id", entry.id); o->setProperty("title", entry.title); o->setProperty("path", entry.file.getFullPathName()); o->setProperty("lastOpenedMs", entry.lastOpenedMs); items.add(o); }
  root->setProperty("recent", items); getRegistryFile().replaceWithText(juce::JSON::toString(juce::var(root), true));
}

void ProjectStore::forgetRecentProject(const juce::File &file) {
  auto entries = getRecentProjects(); entries.erase(std::remove_if(entries.begin(), entries.end(), [&file](const auto &item) { return item.file == file; }), entries.end());
  auto *root = new juce::DynamicObject(); juce::Array<juce::var> items;
  for (const auto &entry : entries) { auto *o = new juce::DynamicObject(); o->setProperty("id", entry.id); o->setProperty("title", entry.title); o->setProperty("path", entry.file.getFullPathName()); o->setProperty("lastOpenedMs", entry.lastOpenedMs); items.add(o); }
  root->setProperty("recent", items); getRegistryFile().replaceWithText(juce::JSON::toString(juce::var(root), true));
}
