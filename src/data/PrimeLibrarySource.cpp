#include "PrimeLibrarySource.h"

#include "SongData.h"
#include "telemetry/Telemetry.h"

#include <algorithm>
#include <map>
#include <vector>

namespace {

std::vector<juce::String> splitTabColumns(const juce::String &line) {
  std::vector<juce::String> cols;
  int start = 0;

  for (int i = 0; i < line.length(); ++i) {
    if (line[i] == '\t') {
      cols.push_back(line.substring(start, i));
      start = i + 1;
    }
  }

  cols.push_back(line.substring(start));
  return cols;
}

juce::String keyFromPrimeIndex(const int keyIndex) {
  static constexpr const char *names[12] = {
      "C", "Db", "D",  "Eb", "E", "F",
      "Gb", "G",  "Ab", "A",  "Bb", "B"};
  if (!juce::isPositiveAndBelow(keyIndex, 12))
    return {};
  return names[keyIndex];
}

juce::String timeSignatureFromPrimeRaw(const juce::String &raw) {
  const auto lower = raw.toLowerCase();
  if (lower.contains("fourfourth"))
    return "4/4";
  if (lower.contains("threefourth"))
    return "3/4";
  if (lower.contains("sixeighth"))
    return "6/8";
  return "4/4";
}

juce::String mapPrimeCategory(const juce::String &categoryJson,
                              const juce::String &trackType,
                              const juce::String &displayName) {
  const auto category = categoryJson.toLowerCase();
  const auto type = trackType.toLowerCase();

  if (category.contains("clickandcues"))
    return type.contains("cue") ? "Cues" : "Click";
  if (category.contains("bass"))
    return "Bass";
  if (category.contains("drums"))
    return "Drums";
  if (category.contains("percussion"))
    return "Percussion";
  if (category.contains("strings"))
    return "Strings";
  if (category.contains("brass"))
    return "Brass";
  if (category.contains("fx"))
    return "FX";
  if (category.contains("vocals"))
    return "Vocals";
  if (category.contains("loop"))
    return "Pads/Ambience";

  if (category.contains("guitars")) {
    if (type.contains("lead"))
      return "Lead Guitars";
    if (type.contains("acoustic"))
      return "Acoustic";
    return "Electric Guitars";
  }

  if (category.contains("keys")) {
    if (type.contains("piano"))
      return "Piano";
    if (type.contains("synth") || type.contains("arpeggiator"))
      return "Synths";
    return "Keys";
  }

  return suggestCategoryFromFilename(displayName);
}

bool runSqliteQuery(const juce::File &dbFile, const juce::String &sql,
                    juce::StringArray &rows, juce::String &errorMessage) {
#if JUCE_MAC && !MTCONTROLS_MAC_APP_STORE
  const juce::File sqliteBinary("/usr/bin/sqlite3");
  if (!sqliteBinary.existsAsFile()) {
    errorMessage = "sqlite3 is not available on this system.";
    return false;
  }

  juce::ChildProcess process;
  juce::StringArray args;
  args.add(sqliteBinary.getFullPathName());
  args.add("-separator");
  args.add("\t");
  args.add(dbFile.getFullPathName());
  args.add(sql);

  if (!process.start(args)) {
    errorMessage = "Couldn't start sqlite3 process.";
    return false;
  }

  if (!process.waitForProcessToFinish(8000)) {
    process.kill();
    errorMessage = "Timed out reading local library database.";
    return false;
  }

  const auto output = process.readAllProcessOutput();
  if (static_cast<int>(process.getExitCode()) != 0) {
    errorMessage = output.trim();
    if (errorMessage.isEmpty())
      errorMessage = "sqlite3 query failed.";
    return false;
  }

  rows = juce::StringArray::fromLines(output.trimEnd());
  return true;
#else
  juce::ignoreUnused(dbFile, sql, rows);
  errorMessage = "Local library import is only supported on macOS.";
  return false;
#endif
}

} // namespace

bool PrimeLibrarySource::isSupportedOnCurrentPlatform() noexcept {
#if JUCE_MAC && !MTCONTROLS_MAC_APP_STORE
  return true;
#else
  return false;
#endif
}

juce::File PrimeLibrarySource::getPrimeDocumentsDirectory() {
  return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
      .getChildFile("Library/Containers/com.loopcommunity.PRIMEMAC/Data/"
                    "Documents");
}

juce::File PrimeLibrarySource::locateDatabaseFile() {
  const auto docs = getPrimeDocumentsDirectory();
  if (!docs.isDirectory())
    return {};

  juce::Array<juce::File> candidates;
  docs.findChildFiles(candidates, juce::File::findFiles, false,
                      "database-*.sqlite");
  if (candidates.isEmpty())
    return {};

  auto best = candidates.getReference(0);
  auto bestTime = best.getLastModificationTime();
  for (const auto &candidate : candidates) {
    const auto candidateTime = candidate.getLastModificationTime();
    if (candidateTime > bestTime) {
      best = candidate;
      bestTime = candidateTime;
    }
  }

  return best;
}

juce::File PrimeLibrarySource::locateTracksDirectory(
    const juce::File &primeDocumentsDir) {
  if (!primeDocumentsDir.isDirectory())
    return {};

  const auto primeRoot = primeDocumentsDir.getChildFile("PRIME");
  if (!primeRoot.isDirectory())
    return {};

  juce::Array<juce::File> trackDirCandidates;
  primeRoot.findChildFiles(trackDirCandidates, juce::File::findDirectories,
                           true, "Tracks");
  if (trackDirCandidates.isEmpty())
    return {};

  juce::File best;
  int bestCount = -1;
  for (const auto &dir : trackDirCandidates) {
    const int count = dir.getNumberOfChildFiles(
        juce::File::findFiles, "*.flac;*.wav;*.aif;*.aiff");
    if (count > bestCount) {
      best = dir;
      bestCount = count;
    }
  }

  return best;
}

bool PrimeLibrarySource::isAvailable() {
  if (!isSupportedOnCurrentPlatform())
    return false;
  const auto db = locateDatabaseFile();
  if (!db.existsAsFile())
    return false;
  const auto tracksDir = locateTracksDirectory(getPrimeDocumentsDirectory());
  return tracksDir.isDirectory();
}

std::vector<PrimeSongData> PrimeLibrarySource::loadSongs(
    juce::String &errorMessage) {
  std::vector<PrimeSongData> songs;
  errorMessage.clear();

  if (!isSupportedOnCurrentPlatform()) {
#if MTCONTROLS_MAC_APP_STORE
    errorMessage = "Select audio files to import. Direct access to another app's library is unavailable in this edition.";
#else
    errorMessage = "Local library import is only supported on macOS.";
#endif
    playworship::telemetry::capture(playworship::telemetry::Incident::songImportFailed);
    return songs;
  }

  const auto docs = getPrimeDocumentsDirectory();
  const auto db = locateDatabaseFile();
  const auto tracksDir = locateTracksDirectory(docs);
  if (!db.existsAsFile()) {
    errorMessage = "Local library database not found.";
    playworship::telemetry::capture(playworship::telemetry::Incident::songImportFailed);
    return songs;
  }
  if (!tracksDir.isDirectory()) {
    errorMessage = "Local tracks folder not found.";
    playworship::telemetry::capture(playworship::telemetry::Incident::songImportFailed);
    return songs;
  }

  juce::StringArray songRows;
  const juce::String songSql =
      "SELECT localId,"
      "COALESCE(REPLACE(name, char(9), ' '), ''),"
      "COALESCE(REPLACE(artist, char(9), ' '), ''),"
      "CAST(COALESCE(tempo, 0) AS INT),"
      "COALESCE(key, 0),"
      "COALESCE(REPLACE(timeSignature, char(9), ' '), '') "
      "FROM multitrack ORDER BY name COLLATE NOCASE;";
  if (!runSqliteQuery(db, songSql, songRows, errorMessage)) {
    playworship::telemetry::capture(playworship::telemetry::Incident::songImportFailed);
    return songs;
  }

  std::map<juce::String, size_t> indexBySongId;
  for (const auto &row : songRows) {
    if (row.isEmpty())
      continue;
    const auto cols = splitTabColumns(row);
    if (cols.size() < 6)
      continue;

    PrimeSongData song;
    song.songId = cols[0].trim();
    if (song.songId.isEmpty())
      continue;
    song.title = cols[1].trim();
    song.artist = cols[2].trim();
    song.bpm = cols[3].getIntValue();
    song.key = keyFromPrimeIndex(cols[4].getIntValue());
    song.timeSignature = timeSignatureFromPrimeRaw(cols[5]);

    indexBySongId[song.songId] = songs.size();
    songs.push_back(std::move(song));
  }

  juce::StringArray trackRows;
  const juce::String trackSql =
      "SELECT multitrackId,"
      "localId,"
      "COALESCE(REPLACE(name, char(9), ' '), ''),"
      "COALESCE(REPLACE(category, char(9), ' '), ''),"
      "COALESCE(REPLACE(type, char(9), ' '), '') "
      "FROM track ORDER BY multitrackId, name COLLATE NOCASE;";
  if (!runSqliteQuery(db, trackSql, trackRows, errorMessage)) {
    playworship::telemetry::capture(playworship::telemetry::Incident::songImportFailed);
    return {};
  }

  for (const auto &row : trackRows) {
    if (row.isEmpty())
      continue;
    const auto cols = splitTabColumns(row);
    if (cols.size() < 5)
      continue;

    const auto songId = cols[0].trim();
    const auto stemId = cols[1].trim();
    const auto songIt = indexBySongId.find(songId);
    if (songIt == indexBySongId.end() || stemId.isEmpty())
      continue;

    juce::File trackFile = tracksDir.getChildFile(stemId + ".flac");
    if (!trackFile.existsAsFile())
      trackFile = tracksDir.getChildFile(stemId + ".wav");
    if (!trackFile.existsAsFile())
      trackFile = tracksDir.getChildFile(stemId + ".aif");
    if (!trackFile.existsAsFile())
      trackFile = tracksDir.getChildFile(stemId + ".aiff");
    if (!trackFile.existsAsFile())
      continue;

    PrimeTrackData track;
    track.stemId = stemId;
    track.displayName = cols[2].trim().isNotEmpty() ? cols[2].trim() : "STEM";
    track.category = mapPrimeCategory(cols[3], cols[4], track.displayName);
    track.sourcePath = trackFile.getFullPathName();
    songs[songIt->second].tracks.push_back(std::move(track));
  }

  songs.erase(std::remove_if(songs.begin(), songs.end(),
                             [](const PrimeSongData &song) {
                               return song.tracks.empty();
                             }),
              songs.end());

  if (songs.empty()) {
    errorMessage = "No songs with local tracks were found.";
    playworship::telemetry::capture(playworship::telemetry::Incident::songImportFailed);
  }

  return songs;
}
