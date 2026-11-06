#pragma once

#include <JuceHeader.h>
#include "SongLyrics.h"
#include "integrations/FreeShowModel.h"

#include <array>
#include <functional>
#include <optional>
#include <vector>

struct ImportedStemData {
  juce::String stemId;
  juce::String sourcePath;
  juce::String storedPath;
  juce::String fileName;
  juce::String displayName;
  juce::String category;
  bool include = true;
  double durationSec = 0.0;
  int sampleRate = 0;
  int channels = 0;
};

// Sparse section values: an unset property keeps the song's base mix.
struct SectionStemAutomation {
  juce::String stemId;
  std::optional<float> volume;
  std::optional<float> gain;
  std::optional<bool> mute;

  bool operator==(const SectionStemAutomation &other) const {
    return stemId == other.stemId && volume == other.volume &&
           gain == other.gain && mute == other.mute;
  }
};

struct SectionPadsAutomation {
  std::optional<float> volume;
  std::optional<float> gain;
  std::optional<bool> enabled;
  std::optional<int> noteIndex;
  std::optional<int> octave;

  bool operator==(const SectionPadsAutomation &other) const {
    return volume == other.volume && gain == other.gain &&
           enabled == other.enabled && noteIndex == other.noteIndex &&
           octave == other.octave;
  }
};

struct SectionAutomationSnapshot {
  std::vector<SectionStemAutomation> stems;
  SectionPadsAutomation pads;

  bool hasOverrides() const;
  bool operator==(const SectionAutomationSnapshot &other) const {
    return stems == other.stems && pads == other.pads;
  }
  bool operator!=(const SectionAutomationSnapshot &other) const {
    return !(*this == other);
  }
};

// The decoder accepts an absent legacy value as empty. Invalid input is rejected
// atomically, leaving the supplied output unchanged.
juce::var sectionAutomationToVar(const SectionAutomationSnapshot &automation);
bool sectionAutomationFromVar(const juce::var &value,
                              SectionAutomationSnapshot &outAutomation);

struct SongArrangementPartSnapshot {
  juce::String id;
  juce::String name;
  juce::String color;
  double startSec = 0.0;
  double endSec = 0.0;
  // If non-empty, this part is a virtual reference (duplicate) pointing to the source part.
  juce::String sourcePartId;
  // Arrangement clips can move independently from the original audio source.
  // These values describe the source-audio region rendered by this part.
  double sourceStartSec = 0.0;
  double sourceEndSec = 0.0;
  // Marks an instruction inserted from the arrangement Utilities menu. It is
  // announced at its own entry, even if its display name overlaps a musical
  // section name (for example, "Breakdown").
  bool utilityCue = false;
  bool muteInstruments = false;
  bool skipPlayback = false;
  SectionAutomationSnapshot automation;
};

// A guide instruction is a point on the musical timeline, not an audio clip.
// An anchor follows its parent section when the section moves or is copied.
// positionSec remains an absolute fallback if that parent is removed or trimmed.
struct SongArrangementCueSnapshot {
  juce::String id;
  juce::String name;
  juce::String color;
  juce::String partId;
  double positionSec = 0.0;
  double offsetSec = 0.0;
  // Zero keeps an individual instruction. A count sequence starts with the
  // numeric name and expands through countEnd on the source-time interval.
  int countEnd = 0;
  double countIntervalSec = 0.0;
  // Explicit source-time offsets retain pulse alignment across tempo regions.
  // Empty keeps the fixed-interval representation used by earlier documents.
  std::vector<double> countOffsetsSec;

  bool operator==(const SongArrangementCueSnapshot &other) const {
    return id == other.id && name == other.name && color == other.color &&
           partId == other.partId && positionSec == other.positionSec &&
           offsetSec == other.offsetSec && countEnd == other.countEnd &&
           countIntervalSec == other.countIntervalSec &&
           countOffsetsSec == other.countOffsetsSec;
  }
};

struct SongArrangementSnapshot {
  std::vector<SongArrangementPartSnapshot> parts;
  std::vector<SongArrangementCueSnapshot> cues;
};

constexpr int kMaxArrangementCueEvents = 512;
int arrangementCueEventCount(const SongArrangementSnapshot &arrangement) noexcept;

double resolveArrangementCueSeconds(const SongArrangementSnapshot &arrangement,
                                    const SongArrangementCueSnapshot &cue);
// Invalid/missing anchors become absolute marks; out-of-range offsets are never
// silently clipped onto a section boundary. Neither helper removes a valid cue.
bool normalizeArrangementCues(SongArrangementSnapshot &arrangement);
// Restores continuous identical musical fragments around legacy count/utility
// clips, or absorbs them into a uniquely compatible musical neighbour. Ambiguous
// mappings, copied source IDs and section-specific actions stay intact.
bool migrateLegacyArrangementCues(SongArrangementSnapshot &arrangement);
juce::var arrangementCueToVar(const SongArrangementCueSnapshot &cue);
bool arrangementCueFromVar(const juce::var &value,
                           SongArrangementCueSnapshot &outCue);

enum class ClickDivision { Quarter = 1, Eighth = 2, Sixteenth = 4 };

struct SongTempoRegionSnapshot {
  juce::String id;
  double startSec = 0.0;
  double endSec = 0.0;
  double bpm = 120.0;
  juce::String timeSignature = "4/4";
  ClickDivision clickDivision = ClickDivision::Quarter;
  bool accentEnabled = true;
};

struct SongTempoMapSnapshot {
  std::vector<SongTempoRegionSnapshot> regions;
};

struct ImportedSongData {
  juce::String id;
  // Identidad estable guardada dentro del manifiesto .pwproj. Puede diferir
  // de `id`, que es la identidad local de la entrada de Biblioteca, cuando el
  // proyecto y una canción legacy provienen de la misma canción original.
  juce::String projectId;
  juce::String title;
  juce::String artist;
  double bpm = 0.0;
  juce::String key;
  juce::String timeSignature;
  juce::String fileType;
  juce::String artworkPath;
  juce::String artworkSource;
  // Ruta del paquete portable .pwproj cuando la canción no pertenece al
  // almacenamiento legacy de la aplicación. No forma parte del manifiesto
  // interno del proyecto: es una referencia local de la biblioteca reciente.
  juce::String projectPath;
  // Explicit opt-in: legacy metadata-only/missing-audio songs are never inferred
  // to be playable blank songs. Blank playback is always open-ended.
  bool blankSong = false;
  int blankSongBars = 0; // Historical prototype metadata, ignored by playback.
  bool blankClickEnabled = true;
  bool blankPadEnabled = false;
  int64_t createdMs = 0;
  std::vector<ImportedStemData> stems;
  SongArrangementSnapshot arrangement;
  SongTempoMapSnapshot tempoMap;
  // Optional lyric text and native UTF-16 section ranges, not playback sections.
  std::optional<SongLyricsDocument> lyrics;
  // Independent musical-time marks. Connection details are session-only.
  playworship::lyrics::SongLyrics freeShowCues;
};

// The early desktop integration used "lyrics" before the native text editor
// owned that key. Only its unambiguous version/marks shape is a cue payload;
// a native document (including a future schema) must never be reinterpreted.
inline bool isLegacyFreeShowCuesPayload(const juce::var &value) {
  const auto *object = value.getDynamicObject();
  return object != nullptr && !object->hasProperty("schemaVersion") &&
         object->hasProperty("version") && object->hasProperty("marks");
}

inline bool hasSongFreeShowCuesPayload(const juce::var &container) {
  const auto *object = container.getDynamicObject();
  return object != nullptr &&
         (object->hasProperty("freeShowCues") ||
          isLegacyFreeShowCuesPayload(object->getProperty("lyrics")));
}

inline bool songFreeShowCuesFromContainer(
    const juce::var &container, playworship::lyrics::SongLyrics &outCues,
    juce::String *errorMessage = nullptr) {
  const auto *object = container.getDynamicObject();
  // Explicit canonical data always wins, including a validation failure.
  if (object != nullptr && object->hasProperty("freeShowCues"))
    return playworship::lyrics::fromVar(object->getProperty("freeShowCues"),
                                       outCues, errorMessage);
  if (object != nullptr && isLegacyFreeShowCuesPayload(object->getProperty("lyrics")))
    return playworship::lyrics::fromVar(object->getProperty("lyrics"),
                                       outCues, errorMessage);
  if (object != nullptr && object->hasProperty("lyrics")) {
    const auto value = object->getProperty("lyrics");
    const auto *native = value.getDynamicObject();
    if (!value.isVoid() && !value.isUndefined() &&
        (native == nullptr || !native->hasProperty("schemaVersion"))) {
      if (errorMessage != nullptr)
        *errorMessage = "Unrecognized legacy lyric payload; previous data kept unchanged.";
      return false;
    }
  }
  return playworship::lyrics::fromVar({}, outCues, errorMessage);
}

extern const std::array<const char *, 19> kTrackCategories;
extern const std::array<const char *, 17> kSongSectionSuggestions;

int timeSignatureBeatsPerBar(const juce::String &timeSignature);

juce::String makeDisplayNameFromFile(const juce::File &file);
juce::String suggestCategoryFromFilename(const juce::String &filename);
int stemPriorityForStripOrder(const ImportedStemData &stem);
void sortStemsForStripOrder(std::vector<ImportedStemData> &stems);
bool isStemPitchShiftable(const ImportedStemData &stem);
juce::String makeRandomArrangementPartColor();
bool isArrangementPartColorValid(const juce::String &value);
juce::String canonicalArrangementSectionName(const juce::String &name);
bool isKnownArrangementSectionName(const juce::String &name);
juce::String standardArrangementPartColorForName(const juce::String &name);
juce::String fallbackArrangementPartColorForName(const juce::String &name);
juce::String arrangementPartDisplayLabelForName(const juce::String &name);
bool isValidBlankSong(const ImportedSongData &song);
double estimateSongLengthSeconds(const ImportedSongData &song);
bool isProjectBackedSong(const ImportedSongData &song);
juce::String projectIdentityForSong(const ImportedSongData &song);
juce::String playbackIdentityForSong(const ImportedSongData &song);
constexpr int kWaveformPreviewBinCount = 22400;
// Visual mix excludes click/guide samples while retaining the full song timeline.
std::vector<float> buildMixedWaveformBins(
    const ImportedSongData &song, int binCount = kWaveformPreviewBinCount,
    const std::function<void(double progress01)> &onProgress = {});

// Decode a single local stem against the source song's duration (not the
// extended arrangement duration). Shorter stems retain a silent source tail;
// PracticeWaveform projects these peaks through the playback map for rehearsal.
std::vector<float> buildStemWaveformBins(const ImportedStemData &stem,
                                         double songLengthSeconds,
                                         int binCount = kWaveformPreviewBinCount,
                                         const std::function<bool()> &shouldCancel = {});
