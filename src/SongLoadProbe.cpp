#include "SongLoadProbe.h"

#if !defined(_WIN32)
#include <sys/resource.h>
#endif

namespace {
constexpr int kFlushEveryNRows = 32;
}

SongLoadProbe &SongLoadProbe::instance() {
  static SongLoadProbe inst;
  return inst;
}

int64_t SongLoadProbe::getCurrentRssKb() {
#if defined(_WIN32)
  // getrusage is not provided by MSVC. Keep the CSV schema stable and mark
  // the optional diagnostic unavailable rather than changing load behavior.
  return -1;
#else
  struct rusage ru {};
  if (getrusage(RUSAGE_SELF, &ru) == 0) {
#if defined(__APPLE__)
    // macOS: ru_maxrss is in KB
    return static_cast<int64_t>(ru.ru_maxrss);
#else
    // Linux: ru_maxrss is in KB too (despite the man page, glibc reports KB)
    return static_cast<int64_t>(ru.ru_maxrss);
#endif
  }
  return -1;
#endif
}

void SongLoadProbe::ensureStarted() {
  if (active_)
    return;
  // Match the path used by AudioDiagnostics in PluginProcessor.cpp:
  // userApplicationDataDirectory/Logs/MTControls. On macOS this resolves
  // to ~/Library/Logs/MTControls, which is where the existing audio_*.csv
  // files live.
  const auto logDir =
      juce::File::getSpecialLocation(
          juce::File::SpecialLocationType::userApplicationDataDirectory)
          .getChildFile("Logs/MTControls");
  logDir.createDirectory();
  const auto ts = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
  logFile_ = logDir.getChildFile("song_load_" + ts + ".csv");
  stream_.open(logFile_.getFullPathName().toStdString(),
               std::ios::out | std::ios::trunc);
  if (stream_.is_open()) {
    stream_ << "wall_ms,phase,sub_phase,duration_us,rss_kb,extra\n";
    stream_.flush();
    headerWritten_ = true;
    active_ = true;
    ++sessionOrdinal_;
  }
}

static int64_t nowTicks() {
  return juce::Time::getHighResolutionTicks();
}

static double ticksToMicros(int64_t delta) {
  if (delta <= 0)
    return 0.0;
  return static_cast<double>(delta) * 1'000'000.0 /
         static_cast<double>(juce::Time::getHighResolutionTicksPerSecond());
}

static double nowWallMs() {
  return static_cast<double>(juce::Time::currentTimeMillis());
}

void SongLoadProbe::beginPhase(const juce::String &phase,
                               const juce::String &subPhase,
                               const juce::String &extra) {
  std::lock_guard<std::mutex> lock(mutex_);
  ensureStarted();
  if (!active_)
    return;
  stack_.push_back({phase, subPhase, extra, nowTicks()});
}

void SongLoadProbe::endPhase() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_ || stack_.empty())
    return;
  const auto top = stack_.back();
  stack_.pop_back();
  const auto dur = ticksToMicros(nowTicks() - top.startTick);
  const auto wall = nowWallMs();
  const auto rss = getCurrentRssKb();
  stream_ << wall << ',' << top.phase.toStdString() << ','
          << top.subPhase.toStdString() << ',' << static_cast<int64_t>(dur)
          << ',' << rss << ',' << top.extra.toStdString() << '\n';
  if (++pendingRows_ >= kFlushEveryNRows) {
    stream_.flush();
    pendingRows_ = 0;
  }
}

void SongLoadProbe::endPhaseAndPopUntil(const juce::String &phase,
                                         const juce::String &sub) {
  std::lock_guard<std::mutex> lock(mutex_);
  while (!stack_.empty()) {
    const auto &top = stack_.back();
    const auto dur = ticksToMicros(nowTicks() - top.startTick);
    const auto wall = nowWallMs();
    const auto rss = getCurrentRssKb();
    stream_ << wall << ',' << top.phase.toStdString() << ','
            << top.subPhase.toStdString() << ',' << static_cast<int64_t>(dur)
            << ',' << rss << ',' << top.extra.toStdString() << '\n';
    if (++pendingRows_ >= kFlushEveryNRows) {
      stream_.flush();
      pendingRows_ = 0;
    }
    const bool match = (top.phase == phase && top.subPhase == sub);
    stack_.pop_back();
    if (match)
      break;
  }
  if (stream_.is_open())
    stream_.flush();
}

void SongLoadProbe::recordEvent(const juce::String &phase,
                                const juce::String &subPhase,
                                const juce::String &extra) {
  std::lock_guard<std::mutex> lock(mutex_);
  ensureStarted();
  if (!active_)
    return;
  stream_ << nowWallMs() << ',' << phase.toStdString() << ','
          << subPhase.toStdString() << ',' << 0 << ',' << getCurrentRssKb()
          << ',' << extra.toStdString() << '\n';
  if (++pendingRows_ >= kFlushEveryNRows) {
    stream_.flush();
    pendingRows_ = 0;
  }
}

void SongLoadProbe::recordDurationUs(const juce::String &phase,
                                     const juce::String &subPhase,
                                     int64_t durationUs,
                                     const juce::String &extra) {
  std::lock_guard<std::mutex> lock(mutex_);
  ensureStarted();
  if (!active_)
    return;
  stream_ << nowWallMs() << ',' << phase.toStdString() << ','
          << subPhase.toStdString() << ',' << durationUs << ','
          << getCurrentRssKb() << ',' << extra.toStdString() << '\n';
  if (++pendingRows_ >= kFlushEveryNRows) {
    stream_.flush();
    pendingRows_ = 0;
  }
}

void SongLoadProbe::recordStemStage(int stemIndex, const juce::String &stage,
                                    int64_t durationUs,
                                    const juce::String &extra) {
  recordDurationUs("loadSongStems", "stem#" + juce::String(stemIndex) + "." + stage,
                   durationUs, extra);
}

void SongLoadProbe::flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (active_)
    stream_.flush();
}

juce::String SongLoadProbe::getLogPath() const {
  return logFile_.getFullPathName();
}
