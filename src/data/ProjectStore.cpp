#include "ProjectStore.h"
#include "AppSettings.h"
#include "SessionAuthLogger.h"

#include <algorithm>
#include <map>

namespace {
constexpr auto kManifestName = "manifest.json";
constexpr auto kAudioPrefix = "audio/";
constexpr auto kWaveformName = "waveform.bin";

class ProjectSettingsSaveJob final : public juce::ThreadPoolJob {
public:
  ProjectSettingsSaveJob(juce::CriticalSection &writeGuardIn,
                         juce::File fileIn,
                         ProjectSongSettings settingsIn,
                         std::function<bool()> isCurrentIn)
      : ThreadPoolJob("Project settings save"), writeGuard(writeGuardIn),
        file(std::move(fileIn)), settings(std::move(settingsIn)),
        isCurrent(std::move(isCurrentIn)) {}
  JobStatus runJob() override {
    if (isCurrent != nullptr && !isCurrent())
      return jobHasFinished;
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
  ProjectSongSettings settings;
  std::function<bool()> isCurrent;
};

class ProjectSettingsSaveQueue final : private juce::Timer {
public:
  void schedule(const juce::File &file, ProjectSongSettings settings) {
    if (!file.existsAsFile()) return;
    const juce::ScopedLock lock(guard);
    const auto key = file.getFullPathName().toStdString();
    const auto version = ++generations[key];
    cached[key] = settings;
    pending[key] = {file, std::move(settings), key, version};
    startTimer(750);
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
                    writeGuard, entry.file, entry.settings,
                    [this, key = entry.key, version = entry.version] {
                      return isCurrent(key, version);
                    }),
                true);
  }
  void writeNow(const Pending &entry) {
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

bool copyZipEntry(juce::ZipFile &zip, const juce::String &name, const juce::File &target,
                  juce::String &error) {
  const int index = zip.getIndexOfFileName(name);
  if (index < 0) { error = "Project is missing " + name; return false; }
  std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(index));
  if (in == nullptr) { error = "Couldn't read " + name; return false; }
  target.getParentDirectory().createDirectory();
  juce::FileOutputStream out(target);
  const auto copied = out.openedOk() ? out.writeFromInputStream(*in, -1) : -1;
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
                        juce::String *errorMessage, double *progress01) {
  if (errorMessage != nullptr) errorMessage->clear();
  if (progress01 != nullptr) *progress01 = 0.0;
  if (!destination.hasFileExtension(kFileExtension)) {
    if (errorMessage != nullptr) *errorMessage = "Project file must use .pwproj";
    return false;
  }
  auto &song = document.song;
  if (song.id.trim().isEmpty()) song.id = juce::Uuid().toString();
  if (song.stems.empty()) { if (errorMessage != nullptr) *errorMessage = "A project needs at least one stem."; return false; }
  const auto temp = destination.getSiblingFile(destination.getFileName() + ".tmp");
  if (temp.existsAsFile()) temp.deleteFile();
  juce::ZipFile::Builder builder;
  const auto stageDir = SongLibraryStore::getRootDirectory()
                            .getChildFile("project-staging")
                            .getChildFile(juce::Uuid().toString());
  stageDir.createDirectory();
  auto cleanupStage = [&stageDir] { if (stageDir.exists()) stageDir.deleteRecursively(); };
  ImportedSongData manifestSong = song;
  manifestSong.projectPath.clear();
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
    if (AppSettings::getStemOptimizationEnabled()) {
      const auto legalId = juce::File::createLegalFileName(stem.stemId);
      const auto converted = stageDir.getChildFile(
          (legalId.isNotEmpty() ? legalId : juce::String("stem_") + juce::String(static_cast<int>(i + 1))) +
          SongLibraryStore::projectStorageStemExtension());
      juce::String conversionError;
      if (!SongLibraryStore::convertStemForProject(source, converted, conversionError)) {
        cleanupStage();
        if (errorMessage != nullptr) *errorMessage = conversionError;
        return false;
      }
      packagedSource = converted;
    }
    ImportedStemData packagedStem = stem;
    packagedStem.storedPath = packagedSource.getFullPathName();
    const auto path = internalStemPath(packagedStem, static_cast<int>(i));
    builder.addFile(packagedSource, 0, path);
    manifestSong.stems[i].storedPath = path;
    manifestSong.stems[i].sourcePath.clear();
    manifestSong.stems[i].fileName = juce::File(path).getFileName();
  }
  if (juce::File artwork(song.artworkPath); juce::File(song.artworkPath).existsAsFile()) {
    const auto artworkPath = "artwork" + juce::File(song.artworkPath).getFileExtension().toLowerCase();
    builder.addFile(juce::File(song.artworkPath), 0, artworkPath);
    manifestSong.artworkPath = artworkPath; manifestSong.artworkSource.clear();
  } else { manifestSong.artworkPath.clear(); }
  auto *root = new juce::DynamicObject();
  root->setProperty("schemaVersion", kSchemaVersion);
  root->setProperty("song", SongLibraryStore::songDataToVar(manifestSong));
  root->setProperty("songSettings", settingsToVar(document.settings));
  const auto manifest = juce::JSON::toString(juce::var(root), true);
  builder.addEntry(new juce::MemoryInputStream(manifest.toRawUTF8(), static_cast<size_t>(manifest.getNumBytesAsUTF8()), true),
                   0, kManifestName, juce::Time::getCurrentTime());
  if (!document.waveformBins.empty() &&
      document.waveformBins.size() == static_cast<size_t>(kWaveformPreviewBinCount)) {
    const auto byteCount = document.waveformBins.size() * sizeof(float);
    builder.addEntry(new juce::MemoryInputStream(document.waveformBins.data(), byteCount, true),
                     0, kWaveformName, juce::Time::getCurrentTime());
  }
  temp.getParentDirectory().createDirectory();
  juce::FileOutputStream out(temp);
  const bool wrote = out.openedOk() && builder.writeToStream(out, progress01);
  out.flush();
  if (!wrote || out.getStatus().failed()) {
    cleanupStage(); temp.deleteFile(); if (errorMessage != nullptr) *errorMessage = "Couldn't write project package."; return false;
  }
  if (!temp.replaceFileIn(destination)) { cleanupStage(); temp.deleteFile(); if (errorMessage != nullptr) *errorMessage = "Couldn't replace project package."; return false; }
  cleanupStage();
  document.projectFile = destination; recordRecent(document);
  if (progress01 != nullptr) *progress01 = 1.0;
  return true;
}

bool ProjectStore::open(const juce::File &file, ProjectDocument &outDocument,
                        juce::String *errorMessage, const bool rebuildCache) {
  if (errorMessage != nullptr) errorMessage->clear();
  if (!file.existsAsFile() || !file.hasFileExtension(kFileExtension)) { if (errorMessage != nullptr) *errorMessage = "Project file not found or invalid."; return false; }
  juce::ZipFile zip(file);
  const int manifestIndex = zip.getIndexOfFileName(kManifestName);
  if (manifestIndex < 0) { if (errorMessage != nullptr) *errorMessage = "Project manifest is missing."; return false; }
  std::unique_ptr<juce::InputStream> manifestStream(zip.createStreamForEntry(manifestIndex));
  if (manifestStream == nullptr) { if (errorMessage != nullptr) *errorMessage = "Couldn't read project manifest."; return false; }
  const auto root = juce::JSON::parse(manifestStream->readEntireStreamAsString());
  const auto *o = root.getDynamicObject();
  if (o == nullptr || static_cast<int>(o->getProperty("schemaVersion")) != kSchemaVersion) { if (errorMessage != nullptr) *errorMessage = "Unsupported project manifest."; return false; }
  bool changed = false;
  auto song = SongLibraryStore::songDataFromVar(o->getProperty("song"), changed);
  if (song.id.isEmpty() || song.stems.empty()) { if (errorMessage != nullptr) *errorMessage = "Project has no valid song or stems."; return false; }
  ProjectSongSettings newerCachedSettings;
  const auto settingsCacheFile = cachedSettingsFile(song.id);
  const bool hasNewerCachedSettings =
      rebuildCache && settingsCacheFile.existsAsFile() &&
      settingsCacheFile.getLastModificationTime() >
          file.getLastModificationTime() &&
      readCachedSettings(song.id, newerCachedSettings);
  auto cache = getCacheDirectory(song.id);
  if (rebuildCache && cache.exists()) cache.deleteRecursively();
  cache.createDirectory();
  juce::String localError;
  for (auto &stem : song.stems) {
    const auto internalPath = stem.storedPath;
    if (!internalPath.startsWith(kAudioPrefix) || internalPath.contains("..")) { localError = "Unsafe stem path in project."; break; }
    const auto target = cache.getChildFile(internalPath);
    if ((rebuildCache || !target.existsAsFile()) &&
        !copyZipEntry(zip, internalPath, target, localError)) break;
    stem.storedPath = target.getFullPathName(); stem.sourcePath = stem.storedPath; stem.fileName = target.getFileName();
  }
  if (localError.isEmpty() && song.artworkPath.isNotEmpty()) {
    const auto artworkName = juce::File(song.artworkPath).getFileName();
    if (artworkName != song.artworkPath || !artworkName.startsWith("artwork.")) localError = "Unsafe artwork path in project.";
    else {
      const auto artworkTarget = cache.getChildFile(artworkName);
      const bool needsExtraction = rebuildCache || !artworkTarget.existsAsFile();
      const bool extracted = !needsExtraction ||
                             copyZipEntry(zip, song.artworkPath, artworkTarget,
                                          localError);
      if (extracted)
        song.artworkPath = artworkTarget.getFullPathName();
    }
  }
  if (localError.isEmpty() && song.artworkPath.isEmpty() &&
      song.artworkSource.isNotEmpty()) {
    juce::String downloadedArtwork;
    const auto cachedArtwork = cache.getChildFile(
        "artwork" + artworkExtensionFromUrl(song.artworkSource));
    if (!rebuildCache && cachedArtwork.existsAsFile())
      song.artworkPath = cachedArtwork.getFullPathName();
    else if (downloadArtworkToCache(song.artworkSource, cache, downloadedArtwork))
      song.artworkPath = downloadedArtwork;
  }
  if (localError.isNotEmpty()) { cache.deleteRecursively(); if (errorMessage != nullptr) *errorMessage = localError; return false; }
  song.projectPath = file.getFullPathName();
  std::vector<float> waveformBins;
  const int waveformIndex = zip.getIndexOfFileName(kWaveformName);
  if (waveformIndex >= 0) {
    const auto *entry = zip.getEntry(waveformIndex);
    const auto expectedBytes = static_cast<juce::int64>(kWaveformPreviewBinCount) *
                               static_cast<juce::int64>(sizeof(float));
    if (entry == nullptr || entry->uncompressedSize != expectedBytes) {
      cache.deleteRecursively();
      if (errorMessage != nullptr) *errorMessage = "Project waveform has an invalid size.";
      return false;
    }
    std::unique_ptr<juce::InputStream> waveformStream(zip.createStreamForEntry(waveformIndex));
    if (waveformStream == nullptr) {
      cache.deleteRecursively();
      if (errorMessage != nullptr) *errorMessage = "Couldn't read project waveform.";
      return false;
    }
    waveformBins.resize(kWaveformPreviewBinCount);
    const auto bytes = static_cast<int>(expectedBytes);
    if (waveformStream->read(waveformBins.data(), bytes) != bytes) {
      cache.deleteRecursively();
      if (errorMessage != nullptr) *errorMessage = "Couldn't extract project waveform.";
      return false;
    }
    SongLibraryStore::saveWaveformCache(song.id, waveformBins);
  }
  auto settings = hasNewerCachedSettings
                      ? newerCachedSettings
                      : settingsFromVar(o->getProperty("songSettings"));
  if (rebuildCache) {
    settingsSaveQueue().remember(file, settings);
    writeCachedSettings(song.id, settings);
  }
  outDocument = {std::move(song), std::move(settings),
                 std::move(waveformBins), file};
  recordRecent(outDocument);
  if (hasNewerCachedSettings)
    settingsSaveQueue().schedule(file, outDocument.settings);
  return true;
}

bool ProjectStore::updateSettings(const juce::File &file, const ProjectSongSettings &settings,
                                  juce::String *errorMessage) {
  ProjectDocument document;
  if (!open(file, document, errorMessage, false)) return false;
  document.settings = settings;
  return save(std::move(document), file, errorMessage);
}

void ProjectStore::scheduleSettingsSave(const juce::File &file,
                                        ProjectSongSettings settings) {
  settingsSaveQueue().schedule(file, std::move(settings));
}

void ProjectStore::scheduleSettingsSave(const juce::File &file,
                                        const juce::String &projectId,
                                        ProjectSongSettings settings) {
  if (!writeCachedSettings(projectId, settings))
    SessionAuthLogger::write(
        "[Project] Couldn't persist local settings cache id=" + projectId);
  if (file.existsAsFile())
    settingsSaveQueue().schedule(file, std::move(settings));
  else if (file != juce::File{})
    SessionAuthLogger::write(
        "[Project] Package unavailable; settings cached locally path=" +
        file.getFullPathName());
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
  ProjectDocument document;
  if (!open(file, document, errorMessage, false)) return false;
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
  entries.insert(entries.begin(), {document.song.id, document.song.title, document.projectFile, juce::Time::getCurrentTime().toMilliseconds(), true});
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
