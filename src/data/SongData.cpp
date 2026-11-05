#include "SongData.h"
#include "Localization.h"
#include "GuideVocabulary.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace {
constexpr double cueTimeTolerance = 0.0001;

bool validCountOffsets(const std::vector<double> &offsets,
                        const int countLength) {
  if (countLength < 1 || countLength > 7 ||
      offsets.size() != static_cast<size_t>(countLength) || offsets.front() != 0.0)
    return false;
  for (size_t i = 0; i < offsets.size(); ++i) {
    if (!std::isfinite(offsets[i]) || offsets[i] < 0.0 ||
        (i > 0 && offsets[i] <= offsets[i - 1]))
      return false;
  }
  return true;
}

const SongArrangementPartSnapshot *validCueParent(
    const SongArrangementSnapshot &arrangement,
    const SongArrangementCueSnapshot &cue) {
  if (cue.partId.isEmpty() || !std::isfinite(cue.offsetSec))
    return nullptr;
  for (const auto &part : arrangement.parts) {
    if (part.id == cue.partId && std::isfinite(part.startSec) &&
        std::isfinite(part.endSec) && part.endSec >= part.startSec &&
        cue.offsetSec >= -cueTimeTolerance &&
        cue.offsetSec <= part.endSec - part.startSec + cueTimeTolerance)
      return &part;
  }
  return nullptr;
}

bool legacyCuePart(const SongArrangementPartSnapshot &part) {
  if (part.utilityCue)
    return true;
  const auto name = part.name.trim().toLowerCase();
  static constexpr std::array<const char *, 48> names = {{
      "1", "2", "3", "4", "5", "6", "7", "one", "two", "three", "four",
      "five", "six", "seven", "uno", "dos", "tres", "cuatro", "cinco",
      "seis", "siete", "a capella", "ad lib", "all", "all in", "bass",
      "big", "big ending", "build", "drums", "drums in", "guitar", "hits",
      "hold", "key", "key change down", "key change up", "keys", "last",
      "last time", "slowly", "slowly build", "softly", "swell",
      "worship freely", "baja intensidad", "breakdown", "acapella"}};
  return std::any_of(names.begin(), names.end(), [&name](const auto *candidate) {
    return name == candidate;
  });
}

bool sameLegacyClipContract(const SongArrangementPartSnapshot &a,
                            const SongArrangementPartSnapshot &b) {
  return a.sourcePartId == b.sourcePartId &&
         a.muteInstruments == b.muteInstruments &&
         a.skipPlayback == b.skipPlayback && a.automation == b.automation;
}

bool contiguousLegacySource(const SongArrangementPartSnapshot &a,
                            const SongArrangementPartSnapshot &b) {
  if (!std::isfinite(a.endSec) || !std::isfinite(b.startSec) ||
      std::abs(a.endSec - b.startSec) > cueTimeTolerance ||
      a.endSec <= a.startSec || b.endSec <= b.startSec)
    return false;
  // Original annotations do not introduce source mapping segments. Removing
  // their internal boundary preserves the source timeline, including prior copies.
  if (a.sourcePartId.isEmpty() && b.sourcePartId.isEmpty())
    return true;
  if (a.sourcePartId != b.sourcePartId ||
      !std::isfinite(a.sourceStartSec) || !std::isfinite(a.sourceEndSec) ||
      !std::isfinite(b.sourceStartSec) || !std::isfinite(b.sourceEndSec) ||
      a.sourceEndSec <= a.sourceStartSec ||
      b.sourceEndSec <= b.sourceStartSec ||
      std::abs(a.sourceEndSec - b.sourceStartSec) > cueTimeTolerance)
    return false;
  const auto aRate = (a.sourceEndSec - a.sourceStartSec) /
                     (a.endSec - a.startSec);
  const auto bRate = (b.sourceEndSec - b.sourceStartSec) /
                     (b.endSec - b.startSec);
  return std::abs(aRate - bRate) <= cueTimeTolerance;
}
} // namespace

double resolveArrangementCueSeconds(const SongArrangementSnapshot &arrangement,
                                    const SongArrangementCueSnapshot &cue) {
  if (const auto *parent = validCueParent(arrangement, cue))
    return parent->startSec + juce::jlimit(
        0.0, parent->endSec - parent->startSec, cue.offsetSec);
  return std::isfinite(cue.positionSec) ? juce::jmax(0.0, cue.positionSec) : 0.0;
}

int arrangementCueEventCount(const SongArrangementSnapshot &arrangement) noexcept {
  int total = 0;
  for (const auto &cue : arrangement.cues) {
    const int first = cue.name.trim().getIntValue();
    const int length = first >= 1 && first <= 7 &&
        cue.countEnd >= first && cue.countEnd <= 7 ? cue.countEnd - first + 1 : 0;
    const bool sequence = length > 0 && cue.name.trim() == juce::String(first) &&
        (validCountOffsets(cue.countOffsetsSec, length) ||
         (cue.countOffsetsSec.empty() && std::isfinite(cue.countIntervalSec) &&
          cue.countIntervalSec > 0.0));
    const int added = sequence ? length : 1;
    if (total > std::numeric_limits<int>::max() - added)
      return std::numeric_limits<int>::max();
    total += added;
  }
  return total;
}

bool normalizeArrangementCues(SongArrangementSnapshot &arrangement) {
  const auto previous = arrangement.cues;
  std::unordered_set<std::string> ids;
  for (auto &cue : arrangement.cues) {
    if (cue.id.trim().isEmpty() || !ids.insert(cue.id.toStdString()).second) {
      cue.id = juce::Uuid().toString();
      ids.insert(cue.id.toStdString());
    }
    cue.name = cue.name.trim();
    if (cue.name.isEmpty())
      cue.name = "Cue";
    if (!isArrangementPartColorValid(cue.color))
      cue.color = fallbackArrangementPartColorForName(cue.name);
    const auto firstCount = cue.name.getIntValue();
    const int countLength = firstCount >= 1 && firstCount <= 7 &&
        cue.countEnd >= firstCount && cue.countEnd <= 7
            ? cue.countEnd - firstCount + 1 : 0;
    if (!cue.countOffsetsSec.empty() &&
        !validCountOffsets(cue.countOffsetsSec, countLength))
      cue.countOffsetsSec.clear();
    if (cue.countEnd < 1 || cue.countEnd > 7 ||
        cue.name != juce::String(firstCount) || firstCount < 1 ||
        firstCount > cue.countEnd ||
        (!validCountOffsets(cue.countOffsetsSec, countLength) &&
         (!std::isfinite(cue.countIntervalSec) || cue.countIntervalSec <= 0.0))) {
      cue.countEnd = 0;
      cue.countIntervalSec = 0.0;
      cue.countOffsetsSec.clear();
    } else if (!std::isfinite(cue.countIntervalSec) || cue.countIntervalSec < 0.0) {
      cue.countIntervalSec = 0.0;
    }
    const double position = resolveArrangementCueSeconds(arrangement, cue);
    if (const auto *parent = validCueParent(arrangement, cue)) {
      cue.offsetSec = juce::jlimit(
          0.0, parent->endSec - parent->startSec, cue.offsetSec);
    } else {
      cue.partId.clear();
      cue.offsetSec = 0.0;
    }
    cue.positionSec = position;
  }
  std::stable_sort(arrangement.cues.begin(), arrangement.cues.end(),
                   [](const auto &a, const auto &b) {
                     return a.positionSec < b.positionSec;
                   });
  return arrangement.cues != previous;
}

bool migrateLegacyArrangementCues(SongArrangementSnapshot &arrangement) {
  bool changed = false;
  // Preserve fallback positions before remapping anchors of absorbed fragments.
  for (auto &cue : arrangement.cues) {
    const auto position = resolveArrangementCueSeconds(arrangement, cue);
    changed = changed || position != cue.positionSec;
    cue.positionSec = position;
  }
  for (size_t leftIndex = 0; leftIndex + 2 < arrangement.parts.size();) {
    const auto &left = arrangement.parts[leftIndex];
    if (legacyCuePart(left)) {
      ++leftIndex;
      continue;
    }
    size_t rightIndex = leftIndex + 1;
    while (rightIndex < arrangement.parts.size() &&
           legacyCuePart(arrangement.parts[rightIndex]))
      ++rightIndex;
    if (rightIndex == leftIndex + 1 || rightIndex >= arrangement.parts.size() ||
        !left.name.trim().equalsIgnoreCase(arrangement.parts[rightIndex].name.trim())) {
      ++leftIndex;
      continue;
    }
    bool canMerge = true;
    for (const auto &candidate : arrangement.parts)
      if (candidate.sourcePartId == left.id)
        canMerge = false;
    for (size_t i = leftIndex + 1; i <= rightIndex; ++i) {
      const auto &part = arrangement.parts[i];
      canMerge = canMerge && sameLegacyClipContract(left, part) &&
                 contiguousLegacySource(arrangement.parts[i - 1], part);
      // A retained copy may use a removed ID to resolve its source. Do not
      // widen that source or rewrite the clip's reference implicitly.
      for (const auto &candidate : arrangement.parts)
        if (candidate.sourcePartId == part.id)
          canMerge = false;
    }
    if (!canMerge) {
      ++leftIndex;
      continue;
    }
    const auto parentId = left.id;
    const auto parentStart = left.startSec;
    for (size_t i = leftIndex + 1; i < rightIndex; ++i) {
      const auto &part = arrangement.parts[i];
      const bool alreadyPresent = std::any_of(
          arrangement.cues.begin(), arrangement.cues.end(),
          [&part](const auto &cue) { return cue.id == part.id; });
      if (!alreadyPresent) {
        SongArrangementCueSnapshot cue;
        cue.id = part.id;
        cue.name = part.name;
        cue.color = part.color;
        cue.partId = parentId;
        cue.positionSec = part.startSec;
        cue.offsetSec = part.startSec - parentStart;
        arrangement.cues.push_back(std::move(cue));
      }
    }
    for (auto &cue : arrangement.cues) {
      for (size_t i = leftIndex + 1; i <= rightIndex; ++i) {
        if (cue.partId == arrangement.parts[i].id) {
          cue.partId = parentId;
          cue.offsetSec = cue.positionSec - parentStart;
          break;
        }
      }
    }
    auto &merged = arrangement.parts[leftIndex];
    merged.endSec = arrangement.parts[rightIndex].endSec;
    merged.sourceEndSec = arrangement.parts[rightIndex].sourceEndSec;
    arrangement.parts.erase(arrangement.parts.begin() + leftIndex + 1,
                            arrangement.parts.begin() + rightIndex + 1);
    changed = true;
    // Stay on the restored musical section to absorb another fragmented run.
  }
  // Leading/trailing instructions, or a run compatible with exactly one of
  // its musical neighbours, can be absorbed without guessing which section
  // owns its controls. If both different neighbours fit, keep the legacy data.
  for (size_t first = 0; first < arrangement.parts.size();) {
    if (!legacyCuePart(arrangement.parts[first])) {
      ++first;
      continue;
    }
    size_t after = first + 1;
    while (after < arrangement.parts.size() && legacyCuePart(arrangement.parts[after]))
      ++after;
    const auto referenced = [&arrangement](const juce::String &id) {
      return std::any_of(arrangement.parts.begin(), arrangement.parts.end(),
                         [&id](const auto &part) { return part.sourcePartId == id; });
    };
    bool continuousRun = true;
    for (size_t i = first; i < after; ++i) {
      continuousRun = continuousRun && !referenced(arrangement.parts[i].id);
      if (i > first)
        continuousRun = continuousRun &&
            sameLegacyClipContract(arrangement.parts[first], arrangement.parts[i]) &&
            contiguousLegacySource(arrangement.parts[i - 1], arrangement.parts[i]);
    }
    if ((first > 0 && referenced(arrangement.parts[first - 1].id)) ||
        (after < arrangement.parts.size() && referenced(arrangement.parts[after].id)))
      continuousRun = false;
    bool fitLeft = continuousRun && first > 0;
    if (fitLeft)
      fitLeft = !referenced(arrangement.parts[first - 1].id) &&
          sameLegacyClipContract(arrangement.parts[first - 1], arrangement.parts[first]) &&
          contiguousLegacySource(arrangement.parts[first - 1], arrangement.parts[first]);
    bool fitRight = continuousRun && after < arrangement.parts.size();
    if (fitRight)
      fitRight = !referenced(arrangement.parts[after].id) &&
          sameLegacyClipContract(arrangement.parts[after - 1], arrangement.parts[after]) &&
          contiguousLegacySource(arrangement.parts[after - 1], arrangement.parts[after]);
    if (fitLeft == fitRight) {
      first = after;
      continue;
    }
    const size_t parentIndex = fitLeft ? first - 1 : after;
    auto &parent = arrangement.parts[parentIndex];
    const auto parentId = parent.id;
    const double nextStart = fitLeft ? parent.startSec : arrangement.parts[first].startSec;
    const double oldStart = parent.startSec;
    // Existing marks on the right-hand parent keep their absolute instant when
    // the musical range expands backwards to include the old instruction.
    if (fitRight) {
      for (auto &cue : arrangement.cues)
        if (cue.partId == parentId)
          cue.offsetSec += oldStart - nextStart;
    }
    for (size_t i = first; i < after; ++i) {
      const auto &part = arrangement.parts[i];
      const bool alreadyPresent = std::any_of(
          arrangement.cues.begin(), arrangement.cues.end(),
          [&part](const auto &cue) { return cue.id == part.id; });
      if (!alreadyPresent) {
        SongArrangementCueSnapshot cue;
        cue.id = part.id;
        cue.name = part.name;
        cue.color = part.color;
        cue.partId = parentId;
        cue.positionSec = part.startSec;
        cue.offsetSec = part.startSec - nextStart;
        arrangement.cues.push_back(std::move(cue));
      }
      for (auto &cue : arrangement.cues)
        if (cue.partId == part.id) {
          cue.partId = parentId;
          cue.offsetSec = cue.positionSec - nextStart;
        }
    }
    if (fitLeft) {
      parent.endSec = arrangement.parts[after - 1].endSec;
      parent.sourceEndSec = arrangement.parts[after - 1].sourceEndSec;
    } else {
      parent.startSec = nextStart;
      parent.sourceStartSec = arrangement.parts[first].sourceStartSec;
    }
    arrangement.parts.erase(arrangement.parts.begin() + first,
                            arrangement.parts.begin() + after);
    changed = true;
    if (fitLeft)
      --first;
  }
  return normalizeArrangementCues(arrangement) || changed;
}

juce::var arrangementCueToVar(const SongArrangementCueSnapshot &cue) {
  auto *object = new juce::DynamicObject();
  object->setProperty("id", cue.id);
  object->setProperty("name", cue.name);
  object->setProperty("color", cue.color);
  object->setProperty("positionSec", cue.positionSec);
  if (cue.countEnd > 0) {
    object->setProperty("countEnd", cue.countEnd);
    object->setProperty("countIntervalSec", cue.countIntervalSec);
    if (!cue.countOffsetsSec.empty()) {
      juce::Array<juce::var> offsets;
      for (const auto offset : cue.countOffsetsSec)
        offsets.add(offset);
      object->setProperty("countOffsetsSec", std::move(offsets));
    }
  }
  if (cue.partId.isNotEmpty()) {
    object->setProperty("partId", cue.partId);
    object->setProperty("offsetSec", cue.offsetSec);
  }
  return juce::var(object);
}

bool arrangementCueFromVar(const juce::var &value,
                           SongArrangementCueSnapshot &outCue) {
  const auto *object = value.getDynamicObject();
  if (object == nullptr || !object->getProperty("name").isString())
    return false;
  SongArrangementCueSnapshot parsed;
  for (const auto *property : {"id", "name", "color", "partId"}) {
    if (object->hasProperty(property) &&
        (!object->getProperty(property).isString() ||
         object->getProperty(property).toString().length() > 512))
      return false;
  }
  parsed.id = object->getProperty("id").toString();
  parsed.name = object->getProperty("name").toString();
  parsed.color = object->getProperty("color").toString();
  parsed.partId = object->getProperty("partId").toString();
  for (const auto *property : {"positionSec", "offsetSec"}) {
    if (!object->hasProperty(property))
      continue;
    const auto &number = object->getProperty(property);
    if ((!number.isInt() && !number.isInt64() && !number.isDouble()) ||
        !std::isfinite(static_cast<double>(number)) ||
        static_cast<double>(number) < 0.0)
      return false;
  }
  parsed.positionSec = static_cast<double>(object->getProperty("positionSec"));
  parsed.offsetSec = static_cast<double>(object->getProperty("offsetSec"));
  if (object->hasProperty("countEnd")) {
    const auto &count = object->getProperty("countEnd");
    const double countNumber = static_cast<double>(count);
    if ((!count.isInt() && !count.isInt64() && !count.isDouble()) ||
        !std::isfinite(countNumber) || countNumber != std::trunc(countNumber) ||
        countNumber < 0.0 || countNumber > 7.0)
      return false;
    parsed.countEnd = static_cast<int>(countNumber);
  }
  if (object->hasProperty("countOffsetsSec")) {
    const auto *offsets = object->getProperty("countOffsetsSec").getArray();
    if (offsets == nullptr || offsets->isEmpty() || offsets->size() > 7)
      return false;
    for (const auto &number : *offsets) {
      if (!number.isInt() && !number.isInt64() && !number.isDouble())
        return false;
      parsed.countOffsetsSec.push_back(static_cast<double>(number));
    }
  }
  if (parsed.countEnd > 0) {
    const auto startCount = parsed.name.trim().getIntValue();
    if (parsed.name.trim() != juce::String(startCount) ||
        startCount < 1 || startCount > parsed.countEnd)
      return false;
    if (!parsed.countOffsetsSec.empty() &&
        !validCountOffsets(parsed.countOffsetsSec, parsed.countEnd - startCount + 1))
      return false;
    if (object->hasProperty("countIntervalSec")) {
      const auto &interval = object->getProperty("countIntervalSec");
      if ((!interval.isInt() && !interval.isInt64() && !interval.isDouble()) ||
          !std::isfinite(static_cast<double>(interval)) ||
          static_cast<double>(interval) < 0.0)
        return false;
      parsed.countIntervalSec = static_cast<double>(interval);
    }
    if (parsed.countOffsetsSec.empty() && parsed.countIntervalSec <= 0.0)
      return false;
  } else if (!parsed.countOffsetsSec.empty()) {
    return false;
  }
  outCue = std::move(parsed);
  return true;
}

bool SectionAutomationSnapshot::hasOverrides() const {
  if (pads.volume || pads.gain || pads.enabled || pads.noteIndex || pads.octave)
    return true;
  return std::any_of(stems.begin(), stems.end(), [](const auto &stem) {
    return stem.volume || stem.gain || stem.mute;
  });
}

namespace {
template <typename T>
void writeAutomationValue(juce::DynamicObject &object, const char *property,
                          const std::optional<T> &value) {
  if (value.has_value())
    object.setProperty(property, *value);
}

bool readAutomationFloat(const juce::DynamicObject &object, const char *property,
                         std::optional<float> &out, float maxValue) {
  if (!object.hasProperty(property))
    return true;
  const auto &value = object.getProperty(property);
  if (!value.isInt() && !value.isInt64() && !value.isDouble())
    return false;
  const auto number = static_cast<double>(value);
  if (!std::isfinite(number) || number < 0.0 || number > maxValue)
    return false;
  out = static_cast<float>(number);
  return true;
}

bool readAutomationBool(const juce::DynamicObject &object, const char *property,
                        std::optional<bool> &out) {
  if (!object.hasProperty(property))
    return true;
  const auto &value = object.getProperty(property);
  if (!value.isBool())
    return false;
  out = static_cast<bool>(value);
  return true;
}

bool readAutomationInt(const juce::DynamicObject &object, const char *property,
                       std::optional<int> &out, int minValue, int maxValue) {
  if (!object.hasProperty(property))
    return true;
  const auto &value = object.getProperty(property);
  if (!value.isInt() && !value.isInt64() && !value.isDouble())
    return false;
  const auto number = static_cast<double>(value);
  if (!std::isfinite(number) || std::trunc(number) != number ||
      number < minValue || number > maxValue)
    return false;
  out = static_cast<int>(number);
  return true;
}
} // namespace

juce::var sectionAutomationToVar(const SectionAutomationSnapshot &automation) {
  auto *root = new juce::DynamicObject();
  juce::Array<juce::var> stems;
  for (const auto &stem : automation.stems) {
    if (!stem.volume && !stem.gain && !stem.mute)
      continue;
    auto *object = new juce::DynamicObject();
    object->setProperty("stemId", stem.stemId);
    writeAutomationValue(*object, "volume", stem.volume);
    writeAutomationValue(*object, "gain", stem.gain);
    writeAutomationValue(*object, "mute", stem.mute);
    stems.add(juce::var(object));
  }
  if (!stems.isEmpty())
    root->setProperty("stems", stems);
  const auto &pads = automation.pads;
  if (pads.volume || pads.gain || pads.enabled || pads.noteIndex || pads.octave) {
    auto *object = new juce::DynamicObject();
    writeAutomationValue(*object, "volume", pads.volume);
    writeAutomationValue(*object, "gain", pads.gain);
    writeAutomationValue(*object, "enabled", pads.enabled);
    writeAutomationValue(*object, "noteIndex", pads.noteIndex);
    writeAutomationValue(*object, "octave", pads.octave);
    root->setProperty("pads", object);
  }
  return juce::var(root);
}

bool sectionAutomationFromVar(const juce::var &value,
                              SectionAutomationSnapshot &outAutomation) {
  SectionAutomationSnapshot parsed;
  if (value.isVoid() || value.isUndefined()) {
    outAutomation = std::move(parsed);
    return true;
  }
  const auto *root = value.getDynamicObject();
  if (root == nullptr)
    return false;
  if (root->hasProperty("stems")) {
    const auto *stems = root->getProperty("stems").getArray();
    constexpr int maxAutomatedStems = 512;
    if (stems == nullptr || stems->size() > maxAutomatedStems)
      return false;
    std::unordered_set<std::string> seenIds;
    parsed.stems.reserve(static_cast<size_t>(stems->size()));
    for (const auto &item : *stems) {
      const auto *object = item.getDynamicObject();
      if (object == nullptr || !object->getProperty("stemId").isString())
        return false;
      SectionStemAutomation stem;
      stem.stemId = object->getProperty("stemId").toString();
      if (stem.stemId.trim().isEmpty() || stem.stemId.length() > 512 ||
          !seenIds.insert(stem.stemId.toStdString()).second ||
          !readAutomationFloat(*object, "volume", stem.volume, 1.0f) ||
          !readAutomationFloat(*object, "gain", stem.gain, 2.0f) ||
          !readAutomationBool(*object, "mute", stem.mute))
        return false;
      if (stem.volume || stem.gain || stem.mute)
        parsed.stems.push_back(std::move(stem));
    }
  }
  if (root->hasProperty("pads")) {
    const auto *object = root->getProperty("pads").getDynamicObject();
    if (object == nullptr ||
        !readAutomationFloat(*object, "volume", parsed.pads.volume, 1.0f) ||
        !readAutomationFloat(*object, "gain", parsed.pads.gain, 1.2f) ||
        !readAutomationBool(*object, "enabled", parsed.pads.enabled) ||
        !readAutomationInt(*object, "noteIndex", parsed.pads.noteIndex, -1, 11) ||
        !readAutomationInt(*object, "octave", parsed.pads.octave, -3, 3))
      return false;
  }
  outAutomation = std::move(parsed);
  return true;
}

const std::array<const char *, 19> kTrackCategories = {{
    "Bass",
    "Click",
    "Accordion",
    "Cues",
    "Drums",
    "Acoustic",
    "Keys",
    "Percussion",
    "Piano",
    "Vocals",
    "Other",
    "Electric Guitars",
    "Lead Guitars",
    "Synths",
    "Strings",
    "Brass",
    "Woodwinds",
    "FX",
    "Pads/Ambience",
}};

const std::array<const char *, 17> kSongSectionSuggestions = [] {
  std::array<const char *, 17> names{};
  const auto &definitions = playworship::guide::sectionDefinitions();
  for (size_t i = 0; i < definitions.size(); ++i) names[i] = definitions[i].key;
  return names;
}();

int timeSignatureBeatsPerBar(const juce::String &timeSignature) {
  const auto trimmed = timeSignature.trim();
  int numerator = 4;
  int denominator = 4;

  if (trimmed.isNotEmpty()) {
    const int slash = trimmed.indexOfChar('/');
    if (slash > 0) {
      numerator = trimmed.substring(0, slash).getIntValue();
      denominator = trimmed.substring(slash + 1).getIntValue();
    } else {
      numerator = trimmed.getIntValue();
    }
  }

  if (numerator <= 0)
    numerator = 4;
  if (denominator <= 0)
    denominator = 4;

  // This app measures the metronome and bar grid in quarter-note units.
  // Convert the written meter so 6/8 becomes 3 quarter-note beats per bar.
  return juce::jmax(1, (numerator * 4) / denominator);
}

juce::String makeDisplayNameFromFile(const juce::File &file) {
  auto name = file.getFileNameWithoutExtension().trim();
  if (name.isEmpty())
    return "STEM";

  name = name.replaceCharacter('_', ' ')
             .replaceCharacter('-', ' ')
             .replaceCharacter('.', ' ')
             .toUpperCase();
  return name;
}

juce::String suggestCategoryFromFilename(const juce::String &filename) {
  const auto lower = filename.toLowerCase();
  juce::String normalized;
  normalized.preallocateBytes(lower.getNumBytesAsUTF8());
  for (int i = 0; i < lower.length(); ++i) {
    const juce::juce_wchar c = lower[i];
    normalized += static_cast<juce::juce_wchar>(juce::CharacterFunctions::isLetterOrDigit(c) ? c : ' ');
  }
  auto tokens = juce::StringArray::fromTokens(normalized, " ", "");
  tokens.removeEmptyStrings();

  auto hasToken = [&tokens](const juce::String &t) {
    for (const auto &tok : tokens) {
      if (tok.equalsIgnoreCase(t))
        return true;
    }
    return false;
  };

  if (lower.contains("click") || hasToken("click"))
    return "Click";
  if (lower.contains("cue") || lower.contains("guide") || hasToken("cue") ||
      hasToken("cues"))
    return "Cues";
  if (lower.contains("bass") || hasToken("bass"))
    return "Bass";
  if (lower.contains("drum") || lower.contains("kick") ||
      lower.contains("snare") || hasToken("drums") || hasToken("drum"))
    return "Drums";
  if (lower.contains("perc") || hasToken("perc") || hasToken("percussion"))
    return "Percussion";
  if (lower.contains("piano") || hasToken("piano"))
    return "Piano";
  if (lower.contains("vocal") || lower.contains("vox") || hasToken("vocal") ||
      hasToken("vocals"))
    return "Vocals";

  if (lower.contains("synth") || hasToken("synth") || hasToken("synths"))
    return "Synths";
  if (lower.contains("pad") || lower.contains("ambience") ||
      lower.contains("ambient") || hasToken("pad") || hasToken("pads") ||
      hasToken("ambience") || hasToken("ambient"))
    return "Pads/Ambience";
  if (lower.contains("string") || hasToken("string") || hasToken("strings") ||
      hasToken("violin") || hasToken("cello"))
    return "Strings";
  if (lower.contains("brass") || hasToken("brass") || hasToken("trumpet") ||
      hasToken("trombone") || hasToken("horn"))
    return "Brass";
  if (lower.contains("woodwind") || hasToken("woodwind") ||
      hasToken("woodwinds") || hasToken("flute") || hasToken("sax") ||
      hasToken("saxophone") || hasToken("clarinet") || hasToken("oboe") ||
      hasToken("bassoon"))
    return "Woodwinds";
  if (lower.contains("fx") || hasToken("fx") || hasToken("sfx"))
    return "FX";

  if (lower.contains("lead") || hasToken("lead") || hasToken("solo") ||
      hasToken("lg"))
    return "Lead Guitars";
  if (lower.contains("electric") || hasToken("electric") || hasToken("eg"))
    return "Electric Guitars";
  if (lower.contains("acoustic") || lower.contains("gtr") ||
      lower.contains("guitar") || hasToken("ag") || hasToken("acoustic") ||
      hasToken("acgtr"))
    return "Acoustic";

  if (lower.contains("key") || hasToken("keys"))
    return "Keys";

  return "Other";
}

int stemPriorityForStripOrder(const ImportedStemData &stem) {
  const auto bucket =
      (stem.displayName + " " + stem.category + " " + stem.fileName)
          .toLowerCase();

  if (bucket.contains("click") || bucket.contains("clic"))
    return 0;
  if (bucket.contains("guia") || bucket.contains("guide") ||
      bucket.contains("cue"))
    return 1;
  return 2;
}

void sortStemsForStripOrder(std::vector<ImportedStemData> &stems) {
  if (stems.size() <= 1)
    return;

  std::stable_sort(
      stems.begin(), stems.end(),
      [](const ImportedStemData &a, const ImportedStemData &b) {
        return stemPriorityForStripOrder(a) < stemPriorityForStripOrder(b);
      });
}

bool isStemPitchShiftable(const ImportedStemData &stem) {
  const auto category = stem.category.trim();
  if (category.isEmpty())
    return true;
  if (category.equalsIgnoreCase("Drums") ||
      category.equalsIgnoreCase("Percussion") ||
      category.equalsIgnoreCase("Click") ||
      category.equalsIgnoreCase("Cues"))
    return false;
  return true;
}

juce::String makeRandomArrangementPartColor() {
  auto &rng = juce::Random::getSystemRandom();
  const float hue = rng.nextFloat();
  const float saturation = 0.52f + (rng.nextFloat() * 0.35f);
  const float brightness = 0.68f + (rng.nextFloat() * 0.24f);
  return juce::Colour::fromHSV(hue, saturation, brightness, 1.0f).toString();
}

bool isArrangementPartColorValid(const juce::String &value) {
  if (value.trim().isEmpty())
    return false;
  const auto parsed = juce::Colour::fromString(value);
  return parsed.getAlpha() > 0;
}

namespace {
using ArrangementSectionStyle = playworship::guide::SectionDefinition;

// Older arrangements may still contain a Breakdown/Break musical clip.
// Preserve its style and canonical key without offering it as a new section;
// the shared vocabulary classifies new Breakdown/Break instructions as cues.
const ArrangementSectionStyle kLegacyBreakdownStyle{
    playworship::guide::SectionFamily::unknown, "Breakdown", "Breakdown", "Breakdown",
    "Breakdown", "ff8A6B6B", {"breakdown", "break"}, 0};

juce::String normalizeArrangementSectionToken(juce::String name) {
  auto text = playworship::guide::normalizedWords(name);
  while (text.isNotEmpty()) {
    const auto ch = text[text.length() - 1];
    if (!juce::CharacterFunctions::isDigit(ch))
      break;
    text = text.dropLastCharacters(1).trimEnd();
  }

  juce::String normalized;
  normalized.preallocateBytes(text.getNumBytesAsUTF8());
  for (int i = 0; i < text.length(); ++i) {
    const auto ch = text[i];
    if (juce::CharacterFunctions::isLetterOrDigit(ch))
      normalized << juce::String::charToString(ch);
  }
  return normalized;
}

juce::String arrangementSectionVariantSuffix(const juce::String &name) {
  const auto trimmed = name.trim();
  int suffixStart = trimmed.length();
  while (suffixStart > 0 &&
         juce::CharacterFunctions::isDigit(trimmed[suffixStart - 1])) {
    --suffixStart;
  }
  if (suffixStart <= 0 || suffixStart == trimmed.length() ||
      !juce::CharacterFunctions::isWhitespace(trimmed[suffixStart - 1])) {
    return {};
  }
  return trimmed.substring(suffixStart);
}

const ArrangementSectionStyle *findArrangementSectionStyle(const juce::String &name) {
  const auto token = normalizeArrangementSectionToken(name);
  if (token.isEmpty())
    return nullptr;

  for (const auto &style : playworship::guide::sectionDefinitions()) {
    if (token == normalizeArrangementSectionToken(style.key)) return &style;
    for (const auto *alias : style.aliases)
      if (token == normalizeArrangementSectionToken(alias)) return &style;
  }
  for (const auto *alias : kLegacyBreakdownStyle.aliases)
    if (token == normalizeArrangementSectionToken(alias)) return &kLegacyBreakdownStyle;
  return nullptr;
}

juce::String countDisplayWordForName(const juce::String &name) {
  const auto token = normalizeArrangementSectionToken(name);
  if (token == "1" || token == "one" || token == "uno")
    return L10n::tr("Uno", "One");
  if (token == "2" || token == "two" || token == "dos")
    return L10n::tr("Dos", "Two");
  if (token == "3" || token == "three" || token == "tres")
    return L10n::tr("Tres", "Three");
  if (token == "4" || token == "four" || token == "cuatro")
    return L10n::tr("Cuatro", "Four");
  if (token == "5" || token == "five" || token == "cinco")
    return L10n::tr("Cinco", "Five");
  if (token == "6" || token == "six" || token == "seis")
    return L10n::tr("Seis", "Six");
  if (token == "7" || token == "seven" || token == "siete")
    return L10n::tr("Siete", "Seven");
  return {};
}

} // namespace

juce::String canonicalArrangementSectionName(const juce::String &name) {
  if (const auto *style = findArrangementSectionStyle(name))
    return style->key;
  return {};
}

bool isKnownArrangementSectionName(const juce::String &name) {
  return findArrangementSectionStyle(name) != nullptr;
}

juce::String standardArrangementPartColorForName(const juce::String &name) {
  if (const auto *style = findArrangementSectionStyle(name))
    return style->colour;
  return {};
}

juce::String fallbackArrangementPartColorForName(const juce::String &name) {
  auto token = normalizeArrangementSectionToken(name);
  if (token.isEmpty())
    token = "part";

  uint32_t hash = 2166136261u;
  for (int i = 0; i < token.length(); ++i) {
    hash ^= static_cast<uint32_t>(token[i]);
    hash *= 16777619u;
  }

  const float hue = static_cast<float>(hash % 360u) / 360.0f;
  const float saturation = 0.48f + (static_cast<float>((hash >> 8) % 24u) / 100.0f);
  const float brightness = 0.62f + (static_cast<float>((hash >> 16) % 18u) / 100.0f);
  return juce::Colour::fromHSV(hue, saturation, brightness, 1.0f).toString();
}

juce::String arrangementPartDisplayLabelForName(const juce::String &name) {
  if (const auto countWord = countDisplayWordForName(name);
      countWord.isNotEmpty()) {
    return countWord;
  }

  if (const auto *style = findArrangementSectionStyle(name)) {
    auto label = L10n::tr(style->spanishLabel, style->englishLabel);
    if (const auto variant = arrangementSectionVariantSuffix(name);
        variant.isNotEmpty()) {
      label << " " << variant;
    }
    return label;
  }

  const auto trimmed = name.trim();
  return trimmed.isNotEmpty() ? trimmed : L10n::tr("Parte", "Part");
}

bool isValidBlankSong(const ImportedSongData &song) {
  return song.blankSong && song.stems.empty() && !song.title.trim().isEmpty() &&
      song.bpm >= 20 && song.bpm <= 300 &&
      (song.timeSignature == "4/4" || song.timeSignature == "3/4" || song.timeSignature == "6/8");
}

double estimateSongLengthSeconds(const ImportedSongData &song) {
  if (song.blankSong)
    return 0.0; // Blank songs are always open-ended; historical bar counts are inert.
  double maxDuration = 0.0;
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;
    maxDuration = std::max(maxDuration, stem.durationSec);
  }
  return std::max(0.0, maxDuration);
}

bool isProjectBackedSong(const ImportedSongData &song) {
  return song.projectPath.trim().isNotEmpty();
}

juce::String projectIdentityForSong(const ImportedSongData &song) {
  const auto projectId = song.projectId.trim();
  return projectId.isNotEmpty() ? projectId : song.id.trim();
}

juce::String playbackIdentityForSong(const ImportedSongData &song) {
  const auto localId = song.id.trim();
  if (localId.isEmpty())
    return {};
  const auto projectPath = song.projectPath.trim();
  return projectPath.isNotEmpty() ? localId + "|project:" + projectPath
                                  : localId + "|legacy";
}

std::vector<float> buildMixedWaveformBins(
    const ImportedSongData &song, const int binCount,
    const std::function<void(double)> &onProgress) {
  if (binCount <= 0)
    return {};

  const auto report = [&onProgress](double p) {
    if (onProgress)
      onProgress(juce::jlimit(0.0, 1.0, p));
  };

  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();

  struct WaveformSource {
    juce::File file;
    bool contributesToWaveform = true;
  };
  std::vector<WaveformSource> files;
  for (const auto &stem : song.stems) {
    if (!stem.include)
      continue;

    juce::File file(stem.storedPath);
    if (!file.existsAsFile())
      file = juce::File(stem.sourcePath);
    if (!file.existsAsFile())
      continue;

    // Use the same click/guide roles as playback, including the accented
    // Spanish guide label. Keep their duration for timeline alignment, but
    // never add their samples to the visual mix.
    const auto label = stem.displayName + " " + stem.category + " " + stem.fileName;
    const bool isMusical = stemPriorityForStripOrder(stem) > 1 &&
                           !label.containsIgnoreCase(juce::String::fromUTF8("guía"));
    files.push_back({std::move(file), isMusical});
  }

  if (files.empty())
    return {};

  report(0.03);
  juce::int64 maxLengthSamples = 0;
  for (const auto &source : files) {
    if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager.createReaderFor(source.file))) {
      maxLengthSamples = std::max(maxLengthSamples, (juce::int64)reader->lengthInSamples);
    }
  }

  if (maxLengthSamples <= 0)
    return {};

  report(0.07);
  std::vector<float> mixedBins(static_cast<size_t>(binCount), 0.0f);
  std::vector<float> stemBins(static_cast<size_t>(binCount), 0.0f);

  constexpr int blockSize = 4096;
  const int nFiles = static_cast<int>(files.size());
  int fileIndex = 0;
  for (const auto &source : files) {
    const double spanStart = static_cast<double>(fileIndex) / static_cast<double>(nFiles);
    const double spanEnd =
        static_cast<double>(fileIndex + 1) / static_cast<double>(nFiles);
    report(0.07 + 0.88 * spanStart);

    if (!source.contributesToWaveform) {
      report(0.07 + 0.88 * spanEnd);
      ++fileIndex;
      continue;
    }

    std::fill(stemBins.begin(), stemBins.end(), 0.0f);

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager.createReaderFor(source.file));
    if (reader == nullptr) {
      ++fileIndex;
      continue;
    }

    const int channels = std::max(1, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> readBuffer(channels, blockSize);

    juce::int64 readPos = 0;
    juce::int64 lastProgressPos = 0;
    const juce::int64 totalLen = std::max((juce::int64)1, (juce::int64)reader->lengthInSamples);
    while (readPos < reader->lengthInSamples) {
      const int toRead = static_cast<int>(std::min((juce::int64)blockSize, (juce::int64)(reader->lengthInSamples - readPos)));
      if (!reader->read(&readBuffer, 0, toRead, readPos, true, true))
        break;

      for (int sample = 0; sample < toRead; ++sample) {
        float mono = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
          mono += readBuffer.getSample(ch, sample);
        mono /= static_cast<float>(channels);

        const auto globalPos = readPos + static_cast<juce::int64>(sample);
        const int bin = juce::jlimit(
            0, binCount - 1,
            static_cast<int>((globalPos * static_cast<juce::int64>(binCount)) /
                             maxLengthSamples));

        stemBins[static_cast<size_t>(bin)] =
            std::max(stemBins[static_cast<size_t>(bin)], std::abs(mono));
      }

      readPos += toRead;
      if (onProgress && (readPos - lastProgressPos) >= 300000) {
        lastProgressPos = readPos;
        const double inner =
            static_cast<double>(readPos) / static_cast<double>(totalLen);
        report(0.07 + 0.88 * (spanStart + (spanEnd - spanStart) * inner));
      }
    }

    report(0.07 + 0.88 * spanEnd);
    for (int i = 0; i < binCount; ++i)
      mixedBins[static_cast<size_t>(i)] += stemBins[static_cast<size_t>(i)];
    ++fileIndex;
  }

  report(0.96);
  const auto maxIt = std::max_element(mixedBins.begin(), mixedBins.end());
  const float maxValue = (maxIt != mixedBins.end()) ? *maxIt : 0.0f;
  if (maxValue <= 0.0f) {
    report(1.0);
    return mixedBins;
  }

  for (auto &v : mixedBins) {
    const float normal = juce::jlimit(0.0f, 1.0f, v / maxValue);
    v = std::sqrt(normal);
  }

  report(1.0);
  return mixedBins;
}

std::vector<float> buildStemWaveformBins(const ImportedStemData &stem,
                                         const double songLengthSeconds,
                                         const int binCount,
                                         const std::function<bool()> &shouldCancel) {
  if (!stem.include || binCount <= 0 || songLengthSeconds <= 0.0 ||
      (shouldCancel && shouldCancel()))
    return {};

  juce::File file(stem.storedPath);
  if (!file.existsAsFile())
    file = juce::File(stem.sourcePath);
  if (!file.existsAsFile())
    return {};

  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();
  auto reader = std::unique_ptr<juce::AudioFormatReader>(
      formatManager.createReaderFor(file));
  if (reader == nullptr || reader->sampleRate <= 0.0)
    return {};

  const auto timelineSamples = juce::jmax(
      static_cast<juce::int64>(1),
      static_cast<juce::int64>(std::ceil(songLengthSeconds * reader->sampleRate)));
  const int channels = juce::jmax(1, static_cast<int>(reader->numChannels));
  constexpr int blockSize = 4096;
  juce::AudioBuffer<float> buffer(channels, blockSize);
  std::vector<float> bins(static_cast<size_t>(binCount), 0.0f);
  for (juce::int64 position = 0; position < reader->lengthInSamples;) {
    if (shouldCancel && shouldCancel())
      return {};
    const int count = static_cast<int>(juce::jmin(
        static_cast<juce::int64>(blockSize), reader->lengthInSamples - position));
    if (!reader->read(&buffer, 0, count, position, true, true))
      break;
    for (int sample = 0; sample < count; ++sample) {
      float mono = 0.0f;
      for (int channel = 0; channel < channels; ++channel)
        mono += buffer.getSample(channel, sample);
      const auto bin = juce::jlimit(
          0, binCount - 1,
          static_cast<int>(((position + sample) * binCount) / timelineSamples));
      auto &peak = bins[static_cast<size_t>(bin)];
      peak = juce::jmax(peak, std::abs(mono / static_cast<float>(channels)));
    }
    position += count;
  }

  const auto maximum = *std::max_element(bins.begin(), bins.end());
  if (maximum > 0.0f)
    for (auto &peak : bins)
      peak = std::sqrt(juce::jlimit(0.0f, 1.0f, peak / maximum));
  return bins;
}
