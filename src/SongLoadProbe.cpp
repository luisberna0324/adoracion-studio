#include "SongLoadProbe.h"

#include <cstdio>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task.h>
#endif
#if !defined(_WIN32)
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
constexpr int kFlushEveryNRows = 32;

// Read current resident memory in KB. The historical implementation
// used ru_maxrss which is the *peak* RSS since process start — it
// never decreases, so the CSV column was a constant and useless for
// detecting per-load allocations. We now query the live RSS:
//   - macOS: task_info(mach_task_self(), TASK_VM_INFO).phys_footprint
//   - Linux: /proc/self/statm (resident pages × page size)
//   - Windows: not implemented; keep the column as -1 to preserve
//              the CSV schema documented in docs/okf/processor/SongLoadProbe.md
// See perf/analysis_20260912/INFORME.md §2.7.
int64_t readCurrentRssKb() {
#if defined(_WIN32)
  return -1;
#elif defined(__APPLE__)
  task_vm_info_data_t info;
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) ==
      KERN_SUCCESS) {
    // phys_footprint is the best "current RSS" approximation that
    // excludes mmap pages not actively touched; it tracks allocations
    // made by the load path (StemTrack buffers, Bungee voices, etc.).
    return static_cast<int64_t>(info.phys_footprint) / 1024;
  }
  return -1;
#else
  // Linux: /proc/self/statm reports (size, resident, shared, text, lib,
  // data, dt) in pages. Resident × page_size = current RSS.
  FILE *f = std::fopen("/proc/self/statm", "r");
  if (f == nullptr)
    return -1;
  long size_pages = 0;
  long resident_pages = 0;
  const int n = std::fscanf(f, "%ld %ld", &size_pages, &resident_pages);
  std::fclose(f);
  if (n != 2)
    return -1;
  const long page_bytes = sysconf(_SC_PAGESIZE);
  if (page_bytes <= 0)
    return -1;
  return (resident_pages * page_bytes) / 1024;
#endif
}
}  // namespace

SongLoadProbe &SongLoadProbe::instance() {
  static SongLoadProbe inst;
  return inst;
}

int64_t SongLoadProbe::getCurrentRssKb() { return readCurrentRssKb(); }

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

static int64_t nowWallMs() {
  // Return epoch ms as int64_t. The previous implementation returned a
  // double that streamed to CSV in scientific notation ("1.78924e+12"),
  // which made it impossible to cross-reference song_load_*.csv rows
  // with audio_gaps_*.csv timestamps. See
  // perf/analysis_20260912/INFORME.md §2.8.
  return juce::Time::currentTimeMillis();
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
