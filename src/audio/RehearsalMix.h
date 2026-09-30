#pragma once

#include "../data/SongData.h"
#include <algorithm>
#include <iterator>
#include <cstdint>
#include <atomic>
#include <cmath>

// Metadata and commands belong to the message thread. Only gains() is read by
// the audio thread. This state deliberately has no project/settings serializer.
class RehearsalMix {
public:
  static constexpr int maxStems = 64;
  struct Group {
    juce::String id, name;
    juce::StringArray stemIds, names;
    uint64_t mask = 0;
  };
  struct Gains {
    uint64_t instruments = 0, selected = 0;
    float group = 1.0f, rest = 1.0f;
    float forStem(int index) const noexcept {
      if (selected == 0 || index < 0 || index >= maxStems) return 1.0f;
      const auto bit = uint64_t{1} << index;
      return (instruments & bit) == 0 ? 1.0f : ((selected & bit) != 0 ? group : rest);
    }
  };

  void clear() {
    reset();
    instrumentMask.store(0, std::memory_order_relaxed);
    songId.clear();
    stemIds.clear();
    groups.clear();
  }
  void configure(const ImportedSongData &song) {
    clear();
    songId = song.id;
    uint64_t mask = 0;
    int index = 0;
    for (const auto &stem : song.stems) {
      if (!stem.include) continue;
      if (index >= maxStems) break;
      stemIds.add(stem.stemId);
      const auto bit = uint64_t{1} << index++;
      // Use the same click/guide classification as the mixer and audio masks.
      if (stemPriorityForStripOrder(stem) <= 1 || stem.stemId.isEmpty()) continue;
      const auto category = stem.category.trim();
      const bool individual = category.isEmpty() || category.equalsIgnoreCase("Other");
      const auto id = individual ? "stem:" + stem.stemId : "category:" + category.toLowerCase();
      auto it = std::find_if(groups.begin(), groups.end(), [&id](const Group &g) { return g.id == id; });
      if (it == groups.end()) {
        groups.push_back({id, individual ? stem.displayName : category, {}, {}, 0});
        it = std::prev(groups.end());
        if (it->name.isEmpty()) it->name = stem.fileName;
      }
      it->mask |= bit;
      it->stemIds.add(stem.stemId);
      it->names.add(stem.displayName.isNotEmpty() ? stem.displayName : stem.fileName);
      mask |= bit;
    }
    instrumentMask.store(mask, std::memory_order_relaxed);
  }
  void reset() {
    ++revision;
    selectedMask.store(0, std::memory_order_release);
    selectedGroup.clear();
    groupLevel.store(1.0f, std::memory_order_relaxed);
    restLevel.store(1.0f, std::memory_order_relaxed);
  }
  bool apply(const juce::String &action, const juce::String &targetSong,
             const juce::var &value) {
    if (songId.isEmpty() || targetSong != songId) return false;
    if (action == "rehearsal.clear") {
      // Deferred clears after reconnect must not undo another musician's edit.
      if (!value.isVoid() && ((!value.isInt() && !value.isInt64()) ||
          static_cast<juce::int64>(value) != revision)) return false;
      reset(); return true;
    }
    if (action == "rehearsal.select") {
      if (!value.isString()) return false;
      const auto id = value.toString();
      const auto it = std::find_if(groups.begin(), groups.end(), [&id](const Group &g) { return g.id == id; });
      if (it == groups.end()) return false;
      if (selectedGroup.isEmpty()) {
        groupLevel.store(1.0f, std::memory_order_relaxed);
        restLevel.store(0.25f, std::memory_order_relaxed);
      }
      selectedGroup = id;
      ++revision;
      selectedMask.store(it->mask, std::memory_order_release);
      return true;
    }
    if (selectedGroup.isEmpty() || (!value.isInt() && !value.isInt64() && !value.isDouble())) return false;
    const auto level = static_cast<double>(value);
    if (!std::isfinite(level) || level < 0.0 || level > 1.0) return false;
    if (action == "rehearsal.groupLevel") groupLevel.store(static_cast<float>(level), std::memory_order_relaxed);
    else if (action == "rehearsal.restLevel") restLevel.store(static_cast<float>(level), std::memory_order_relaxed);
    else return false;
    ++revision;
    return true;
  }
  Gains gains() const noexcept {
    return {instrumentMask.load(std::memory_order_relaxed),
            selectedMask.load(std::memory_order_acquire),
            groupLevel.load(std::memory_order_relaxed), restLevel.load(std::memory_order_relaxed)};
  }
  int indexForStemId(const juce::String &id) const { return stemIds.indexOf(id); }
  juce::var snapshot() const {
    auto *o = new juce::DynamicObject();
    o->setProperty("version", 1);
    o->setProperty("revision", revision);
    o->setProperty("songId", songId);
    o->setProperty("group", selectedGroup);
    o->setProperty("groupLevel", groupLevel.load(std::memory_order_relaxed));
    o->setProperty("restLevel", restLevel.load(std::memory_order_relaxed));
    juce::Array<juce::var> rows;
    for (const auto &g : groups) {
      auto *row = new juce::DynamicObject();
      row->setProperty("id", g.id);
      row->setProperty("name", g.name);
      row->setProperty("stemIds", juce::var(g.stemIds));
      row->setProperty("names", juce::var(g.names));
      rows.add(juce::var(row));
    }
    o->setProperty("groups", rows);
    return juce::var(o);
  }

private:
  juce::String songId, selectedGroup;
  juce::int64 revision = 0;
  juce::StringArray stemIds;
  std::vector<Group> groups;
  std::atomic<uint64_t> instrumentMask{0}, selectedMask{0};
  std::atomic<float> groupLevel{1.0f}, restLevel{1.0f};
};
