#pragma once

#include <JuceHeader.h>

#include <array>
#include <functional>
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
std::vector<float> buildMixedWaveformBins(
    const ImportedSongData &song, int binCount = kWaveformPreviewBinCount,
    const std::function<void(double progress01)> &onProgress = {});
