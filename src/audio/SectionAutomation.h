#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace playworship::audio {

// Control publishes compiled, index-addressed mixes. Realtime selection reads
// atomics only, bounded to three attempts and the section/stem capacities.
class SectionAutomation {
public:
  static constexpr int maxStems = 64;
  static constexpr int maxSections = 256;
  enum Field : unsigned { volume = 1, gain = 2, mute = 4,
                          enabled = 4, note = 8, octave = 16 };
  struct Stem {
    unsigned mask = 0;
    float volume = 1.0f;
    float gain = 1.0f;
    bool mute = false;
  };
  struct Pads {
    unsigned mask = 0;
    float volume = 1.0f;
    float gain = 0.75f;
    bool enabled = true;
    int note = -1;
    int octave = 0;
  };
  struct Part {
    uint64_t token = 0;
    double start = 0.0;
    double end = 0.0;
    std::array<Stem, maxStems> stems{};
    Pads pads;
  };
  struct Selection {
    bool valid = false;
    int bank = -1;
    uint64_t sequence = 0;
    uint64_t token = 0;
    double nextBoundary = -1.0;
    std::array<Stem, maxStems> stems{};
    Pads pads;
  };

  void publish(const std::vector<Part>& parts, bool padsScoped = false) {
    std::lock_guard<std::mutex> lock(publicationMutex);
    const int bankIndex = (published.load(std::memory_order_relaxed) + 1) % 3;
    auto& bank = banks[static_cast<size_t>(bankIndex)];
    bank.sequence.fetch_add(1, std::memory_order_acq_rel);
    bank.padsScoped.store(padsScoped, std::memory_order_relaxed);
    const int count = static_cast<int>(parts.size()) < maxSections
        ? static_cast<int>(parts.size()) : maxSections;
    for (int index = 0; index < count; ++index) {
      const auto& source = parts[static_cast<size_t>(index)];
      auto& target = bank.parts[static_cast<size_t>(index)];
      target.token.store(source.token, std::memory_order_relaxed);
      target.start.store(source.start, std::memory_order_relaxed);
      target.end.store(source.end, std::memory_order_relaxed);
      for (int stem = 0; stem < maxStems; ++stem) {
        const auto& value = source.stems[static_cast<size_t>(stem)];
        auto& out = target.stems[static_cast<size_t>(stem)];
        out.mask.store(value.mask, std::memory_order_relaxed);
        out.volume.store(value.volume, std::memory_order_relaxed);
        out.gain.store(value.gain, std::memory_order_relaxed);
        out.mute.store(value.mute, std::memory_order_relaxed);
      }
      target.pads.mask.store(source.pads.mask, std::memory_order_relaxed);
      target.pads.volume.store(source.pads.volume, std::memory_order_relaxed);
      target.pads.gain.store(source.pads.gain, std::memory_order_relaxed);
      target.pads.enabled.store(source.pads.enabled, std::memory_order_relaxed);
      target.pads.note.store(source.pads.note, std::memory_order_relaxed);
      target.pads.octave.store(source.pads.octave, std::memory_order_relaxed);
    }
    bank.count.store(count, std::memory_order_relaxed);
    bank.sequence.fetch_add(1, std::memory_order_release);
    published.store(bankIndex, std::memory_order_release);
  }

  Selection select(double seconds, int requiredBank = -1,
                   uint64_t requiredSequence = 0) const noexcept {
    for (int attempt = 0; attempt < 3; ++attempt) {
      Selection result;
      const int bankIndex = requiredBank >= 0 ? requiredBank : published.load(std::memory_order_acquire);
      const auto& bank = banks[static_cast<size_t>(bankIndex)];
      const auto before = bank.sequence.load(std::memory_order_acquire);
      if (requiredBank >= 0 && before != requiredSequence) return {};
      if ((before & 1U) != 0)
        continue;
      const int count = bank.count.load(std::memory_order_relaxed);
      int selected = -1;
      for (int index = 0; index < count; ++index) {
        const auto& part = bank.parts[static_cast<size_t>(index)];
        const double start = part.start.load(std::memory_order_relaxed);
        const double end = part.end.load(std::memory_order_relaxed);
        if (start <= seconds && seconds < end)
          selected = index;
        for (const double boundary : {start, end})
          if (boundary > seconds && (result.nextBoundary < 0.0 || boundary < result.nextBoundary))
            result.nextBoundary = boundary;
      }
      if (selected >= 0) {
        const auto& part = bank.parts[static_cast<size_t>(selected)];
        result.token = part.token.load(std::memory_order_relaxed);
        for (int index = 0; index < maxStems; ++index) {
          const auto& value = part.stems[static_cast<size_t>(index)];
          auto& target = result.stems[static_cast<size_t>(index)];
          target.mask = value.mask.load(std::memory_order_relaxed);
          target.volume = value.volume.load(std::memory_order_relaxed);
          target.gain = value.gain.load(std::memory_order_relaxed);
          target.mute = value.mute.load(std::memory_order_relaxed);
        }
        result.pads.mask = part.pads.mask.load(std::memory_order_relaxed);
        result.pads.volume = part.pads.volume.load(std::memory_order_relaxed);
        result.pads.gain = part.pads.gain.load(std::memory_order_relaxed);
        result.pads.enabled = part.pads.enabled.load(std::memory_order_relaxed);
        result.pads.note = part.pads.note.load(std::memory_order_relaxed);
        result.pads.octave = part.pads.octave.load(std::memory_order_relaxed);
      }
      // Once Pads are programmed for this song, an unassigned section/gap
      // means silence, rather than unexpectedly resurrecting the global pad.
      if (result.pads.mask == 0 && bank.padsScoped.load(std::memory_order_relaxed)) {
        result.pads.mask = enabled | note;
        result.pads.enabled = false;
        result.pads.note = -1;
      }
      if (before == bank.sequence.load(std::memory_order_acquire)) {
        result.valid = true;
        result.bank = bankIndex;
        result.sequence = before;
        return result;
      }
    }
    return {};
  }

  // Returns every distinct start/end inside a span, from the same publication
  // as the captured initial selection. Caller supplies fixed-capacity storage.
  int collectBoundaries(const Selection &origin, double start, double end,
                        double *times, int capacity) const noexcept {
    if (!origin.valid || origin.bank < 0 || end <= start || capacity <= 0) return 0;
    const auto &bank = banks[static_cast<size_t>(origin.bank)];
    if (bank.sequence.load(std::memory_order_acquire) != origin.sequence) return 0;
    int found = 0;
    const int count = bank.count.load(std::memory_order_relaxed);
    for (int index = 0; index < count; ++index) {
      const auto &part = bank.parts[static_cast<size_t>(index)];
      for (const double boundary : {part.start.load(std::memory_order_relaxed),
                                    part.end.load(std::memory_order_relaxed)})
        if (boundary > start && boundary < end && found < capacity) times[found++] = boundary;
    }
    if (bank.sequence.load(std::memory_order_acquire) != origin.sequence) return 0;
    std::sort(times, times + found);
    int unique = 0;
    for (int index = 0; index < found; ++index)
      if (unique == 0 || times[index] != times[unique - 1]) times[unique++] = times[index];
    return unique;
  }

private:
  struct AtomicStem {
    std::atomic<unsigned> mask{0};
    std::atomic<float> volume{1.0f}, gain{1.0f};
    std::atomic<bool> mute{false};
  };
  struct AtomicPads {
    std::atomic<unsigned> mask{0};
    std::atomic<float> volume{1.0f}, gain{0.75f};
    std::atomic<bool> enabled{true};
    std::atomic<int> note{-1}, octave{0};
  };
  struct AtomicPart {
    std::atomic<uint64_t> token{0};
    std::atomic<double> start{0.0}, end{0.0};
    std::array<AtomicStem, maxStems> stems;
    AtomicPads pads;
  };
  struct Bank {
    std::atomic<uint64_t> sequence{0};
    std::atomic<bool> padsScoped{false};
    std::atomic<int> count{0};
    std::array<AtomicPart, maxSections> parts;
  };
  std::array<Bank, 3> banks;
  std::atomic<int> published{0};
  std::mutex publicationMutex;
};

} // namespace playworship::audio
