#pragma once

#include <JuceHeader.h>

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

struct SongArrangementSnapshot {
  std::vector<SongArrangementPartSnapshot> parts;
};

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
  int bpm = 0;
  juce::String key;
  juce::String timeSignature;
  juce::String fileType;
  juce::String artworkPath;
  juce::String artworkSource;
  // Ruta del paquete portable .pwproj cuando la canción no pertenece al
  // almacenamiento legacy de la aplicación. No forma parte del manifiesto
  // interno del proyecto: es una referencia local de la biblioteca reciente.
  juce::String projectPath;
  int64_t createdMs = 0;
  std::vector<ImportedStemData> stems;
  SongArrangementSnapshot arrangement;
  SongTempoMapSnapshot tempoMap;
};

extern const std::array<const char *, 19> kTrackCategories;
extern const std::array<const char *, 15> kSongSectionSuggestions;

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
double estimateSongLengthSeconds(const ImportedSongData &song);
bool isProjectBackedSong(const ImportedSongData &song);
juce::String projectIdentityForSong(const ImportedSongData &song);
juce::String playbackIdentityForSong(const ImportedSongData &song);
constexpr int kWaveformPreviewBinCount = 22400;
// Visual mix excludes click/guide samples while retaining the full song timeline.
std::vector<float> buildMixedWaveformBins(
    const ImportedSongData &song, int binCount = kWaveformPreviewBinCount,
    const std::function<void(double progress01)> &onProgress = {});

// Decode a single local stem for the rehearsal preview. Keeps the bins aligned
// with the whole-song timeline so its playhead can use the shared transport.
std::vector<float> buildStemWaveformBins(const ImportedStemData &stem,
                                         double songLengthSeconds,
                                         int binCount = kWaveformPreviewBinCount,
                                         const std::function<bool()> &shouldCancel = {});
