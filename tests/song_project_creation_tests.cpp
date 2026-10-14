#include "data/ProjectStore.h"
#include "data/ApplicationDataPaths.h"
#include "data/ArrangementSourceMapping.h"
#include "network/RemoteStateCodec.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
constexpr double sampleRate = 8000.0;
constexpr int sampleCount = 1024;

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

// ProjectStore writes a recent-project registry, settings sidecars and caches.
// Never run this integration test against the user's ordinary app-data root.
// Compile this test and AppSettings/SongLibraryStore/SessionAuthLogger with
// MTCONTROLS_ISOLATED_STORAGE_TESTS=1. Set PLAYWORSHIP_CREATION_TEST_ROOT to an
// existing fresh mktemp directory; production builds ignore this variable.
bool isolatedStorageRoot(juce::File &root) {
#if !defined(MTCONTROLS_ISOLATED_STORAGE_TESTS) || !MTCONTROLS_ISOLATED_STORAGE_TESTS
  juce::ignoreUnused(root);
  return false;
#else
  const auto requested = juce::SystemStats::getEnvironmentVariable(
      "PLAYWORSHIP_CREATION_TEST_ROOT", {});
  if (requested.isEmpty() || !juce::File::isAbsolutePath(requested))
    return false;
  try {
    root = ApplicationDataPaths::isolatedTestRoot();
    return ApplicationDataPaths::applicationDataDirectory().isAChildOf(root) &&
           ApplicationDataPaths::documentsDirectory().isAChildOf(root) &&
           ApplicationDataPaths::systemLogsDirectory().isAChildOf(root);
  } catch (const std::exception &error) {
    std::cerr << "Isolation check: " << error.what() << '\n';
    return false;
  }
#endif
}

struct Fixtures {
  juce::String projectId = juce::Uuid().toString();
  juce::String stemId = juce::Uuid().toString();
  juce::File directory;
  juce::File archive;
  juce::File libraryFile;

  explicit Fixtures(const juce::File &isolatedRoot)
      : directory(isolatedRoot.getChildFile("fixtures-" + projectId)),
        archive(directory.getChildFile("created-song.pwproj")) {
    expect(directory.createDirectory().wasOk(), "create test fixture directory");
    const auto storageRoot = SongLibraryStore::getRootDirectory();
    expect(storageRoot.isAChildOf(isolatedRoot), "actual library storage is isolated");
    libraryFile = storageRoot.getChildFile("songs.json");
    expect(!libraryFile.exists(),
           "integration test requires an isolated root without a user library");
  }

  ~Fixtures() {
    // The constructor proved the library did not exist before this test. Only
    // its own database, UUID cache and exact fixtures are removed, never a
    // broad application directory or an existing user database.
    if (libraryFile.existsAsFile())
      libraryFile.deleteFile();
    ProjectStore::discardLocalState(projectId, archive);
    directory.deleteRecursively();
  }
};

float expectedSample(int index) {
  return 0.25f * std::sin(static_cast<float>(index) * 0.075f);
}

void writeFlac(const juce::File &file) {
  std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
  juce::FlacAudioFormat format;
  auto writer = format.createWriterFor(
      stream, juce::AudioFormatWriterOptions{}
                  .withSampleRate(sampleRate).withNumChannels(1).withBitsPerSample(16));
  expect(writer != nullptr, "create actual FLAC writer");
  juce::AudioBuffer<float> audio(1, sampleCount);
  for (int i = 0; i < sampleCount; ++i)
    audio.setSample(0, i, expectedSample(i));
  expect(writer->writeFromAudioSampleBuffer(audio, 0, sampleCount),
         "write actual FLAC samples");
}

void writeWav(const juce::File &file) {
  std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
  juce::WavAudioFormat format;
  auto writer = format.createWriterFor(
      stream, juce::AudioFormatWriterOptions{}
                  .withSampleRate(sampleRate).withNumChannels(1).withBitsPerSample(16));
  expect(writer != nullptr, "create actual WAV writer");
  juce::AudioBuffer<float> audio(1, sampleCount);
  for (int i = 0; i < sampleCount; ++i)
    audio.setSample(0, i, expectedSample(i));
  expect(writer->writeFromAudioSampleBuffer(audio, 0, sampleCount),
         "write actual WAV samples");
}

void writePng(const juce::File &file) {
  juce::Image image(juce::Image::ARGB, 8, 8, true);
  image.setPixelAt(3, 4, juce::Colour(0xff27cd68));
  juce::FileOutputStream stream(file);
  juce::PNGImageFormat format;
  expect(stream.openedOk() && format.writeImageToStream(image, stream),
         "write actual PNG cover");
}

SectionAutomationSnapshot automationFixture(const juce::String &stemId) {
  SectionAutomationSnapshot automation;
  SectionStemAutomation stem;
  stem.stemId = stemId;
  stem.volume = 0.0f;
  stem.gain = 1.25f;
  stem.mute = false;
  automation.stems.push_back(stem);
  automation.pads.volume = 0.35f;
  automation.pads.gain = 1.1f;
  automation.pads.enabled = false;
  automation.pads.noteIndex = -1;
  automation.pads.octave = -1;
  return automation;
}

void testSectionAutomationCodec(const Fixtures &fixtures) {
  const auto automation = automationFixture(fixtures.stemId);
  SectionAutomationSnapshot decoded;
  const auto encoded = sectionAutomationToVar(automation);
  expect(automation.hasOverrides() &&
             sectionAutomationFromVar(juce::JSON::parse(juce::JSON::toString(encoded)), decoded) &&
             decoded == automation,
         "section automation JSON preserves sparse zero, false and stop-note values");
  expect(!decoded.stems.front().mute.value() &&
             !decoded.pads.enabled.value() && decoded.pads.noteIndex == -1,
         "false overrides remain present rather than reverting to the base mix");
  expect(!encoded.getDynamicObject()->hasProperty("solo") &&
             !encoded.getDynamicObject()->hasProperty("master"),
         "section automation does not capture solo or master controls");
  ImportedSongData remoteSong;
  ImportedStemData renamedClick;
  renamedClick.stemId = "renamed-click";
  renamedClick.displayName = "Track";
  renamedClick.category = "Other";
  renamedClick.fileName = "Click.flac";
  remoteSong.stems.push_back(renamedClick);
  remoteSong.arrangement.parts.push_back({});
  remoteSong.arrangement.parts.front().id = "verse";
  remoteSong.arrangement.parts.front().automation = automation;
  const auto remoteRoundtrip = playworship::remote::decodeSong(
      playworship::remote::encodeSong(remoteSong, false));
  expect(remoteRoundtrip.stems.size() == 1 &&
             remoteRoundtrip.stems.front().fileName == renamedClick.fileName &&
             stemPriorityForStripOrder(remoteRoundtrip.stems.front()) == 0 &&
             remoteRoundtrip.arrangement.parts.front().automation == automation,
         "remote song codec preserves automation and the identity of renamed Click stems");
  remoteSong.stems.front().fileName = "/private/server/stems/Click.flac";
  expect(!juce::JSON::toString(playworship::remote::encodeSong(remoteSong, false))
              .contains("/private/server"),
         "remote filenames never disclose the server's local directories");

  auto copied = automation;
  copied.stems.front().volume = 0.8f;
  copied.pads.noteIndex = 7;
  expect(automation.stems.front().volume == 0.0f &&
             automation.pads.noteIndex == -1 && copied != automation,
         "copied section automation is independent from its source");

  for (const auto &invalid : {
         "[]", "{\"stems\":{}}", "{\"pads\":[]}",
         "{\"stems\":[{\"stemId\":\"\",\"volume\":0.5}]}",
         "{\"stems\":[{\"stemId\":\"a\",\"volume\":1.01}]}",
         "{\"stems\":[{\"stemId\":\"a\",\"gain\":2.1}]}",
         "{\"stems\":[{\"stemId\":\"a\",\"mute\":1}]}",
         "{\"stems\":[{\"stemId\":\"a\",\"volume\":0.5},{\"stemId\":\"a\",\"gain\":1}]}",
         "{\"stems\":[{\"stemId\":\"a\",\"volume\":0.5}],\"pads\":{\"gain\":1.3}}",
         "{\"pads\":{\"volume\":-0.1}}", "{\"pads\":{\"enabled\":\"true\"}}",
         "{\"pads\":{\"noteIndex\":12}}", "{\"pads\":{\"noteIndex\":-2}}",
         "{\"pads\":{\"octave\":9}}", "{\"pads\":{\"octave\":3.5}}" }) {
    decoded = automation;
    expect(!sectionAutomationFromVar(juce::JSON::parse(invalid), decoded) &&
               decoded == automation,
           "invalid section automation is rejected without a partial update");
  }
  auto *nonfinitePads = new juce::DynamicObject();
  nonfinitePads->setProperty("volume", std::numeric_limits<double>::quiet_NaN());
  auto *nonfiniteRoot = new juce::DynamicObject();
  nonfiniteRoot->setProperty("pads", nonfinitePads);
  expect(!sectionAutomationFromVar(juce::var(nonfiniteRoot), decoded) &&
             decoded == automation,
         "nonfinite automation values cannot enter playback or persistence");
  juce::Array<juce::var> oversized;
  for (int i = 0; i < 513; ++i) {
    auto *stem = new juce::DynamicObject();
    stem->setProperty("stemId", "stem-" + juce::String(i));
    stem->setProperty("volume", 0.5);
    oversized.add(juce::var(stem));
  }
  auto *oversizedRoot = new juce::DynamicObject();
  oversizedRoot->setProperty("stems", oversized);
  expect(!sectionAutomationFromVar(juce::var(oversizedRoot), decoded) &&
             decoded == automation,
         "automation array bounds reject oversized remote or disk input");
  expect(sectionAutomationFromVar({}, decoded) && !decoded.hasOverrides(),
         "legacy missing automation loads as an empty snapshot");

  auto legacy = juce::JSON::parse(
      "{\"arrangement\":{\"parts\":[{\"id\":\"verse\",\"name\":\"Verse\",\"endSec\":1}]}}");
  bool changed = false;
  const auto legacySong = SongLibraryStore::songDataFromVar(legacy, changed);
  expect(legacySong.arrangement.parts.size() == 1 &&
             !legacySong.arrangement.parts.front().automation.hasOverrides(),
         "legacy songs retain their sections without invented automation");
  legacy.getDynamicObject()->getProperty("arrangement").getDynamicObject()
      ->getProperty("parts").getArray()->getReference(0).getDynamicObject()
      ->setProperty("automation", juce::JSON::parse("{\"pads\":{\"noteIndex\":12}}"));
  const auto malformedSong = SongLibraryStore::songDataFromVar(legacy, changed);
  expect(malformedSong.arrangement.parts.size() == 1 &&
             !malformedSong.arrangement.parts.front().automation.hasOverrides(),
         "malformed saved automation is ignored as a block while its section survives");
}

void testArrangementCues(const Fixtures &fixtures) {
  SongArrangementPartSnapshot verse;
  verse.id = "verse";
  verse.name = "Verse 2";
  verse.startSec = 5.0;
  verse.endSec = 20.0;
  SongArrangementSnapshot arrangement;
  arrangement.parts.push_back(verse);
  SongArrangementCueSnapshot cue;
  cue.id = "instruction";
  cue.name = "Build";
  cue.color = "ff72a465";
  cue.partId = verse.id;
  cue.positionSec = 7.5;
  cue.offsetSec = 2.5;
  arrangement.cues.push_back(cue);
  expect(resolveArrangementCueSeconds(arrangement, cue) == 7.5,
         "anchored cue resolves within its full musical section");
  arrangement.parts.front().startSec += 10.0;
  arrangement.parts.front().endSec += 10.0;
  expect(resolveArrangementCueSeconds(arrangement, cue) == 17.5,
         "moving a section moves its cue by the same amount");
  expect(normalizeArrangementCues(arrangement) &&
             arrangement.cues.front().positionSec == 17.5 &&
             arrangement.cues.front().color == cue.color,
         "normalization refreshes the fallback and preserves the chosen color");
  arrangement.parts.front().endSec = 16.0;
  expect(resolveArrangementCueSeconds(arrangement, arrangement.cues.front()) == 17.5 &&
             normalizeArrangementCues(arrangement) &&
             arrangement.cues.front().partId.isEmpty() &&
             arrangement.cues.front().positionSec == 17.5,
         "a trimmed invalid anchor keeps its absolute location instead of clipping to the edge");
  expect(!normalizeArrangementCues(arrangement), "cue normalization is idempotent");

  SongArrangementCueSnapshot count = cue;
  count.id = "count";
  count.name = "1";
  count.countEnd = 4;
  count.countIntervalSec = 0.5;
  SongArrangementCueSnapshot decoded;
  expect(arrangementCueFromVar(juce::JSON::parse(
             juce::JSON::toString(arrangementCueToVar(count))), decoded) && decoded == count,
         "count sequence codec retains the source-time pulse and first/last count");
  auto variableCount = count;
  variableCount.countIntervalSec = 0.0;
  variableCount.countOffsetsSec = {0.0, 0.5, 1.1, 1.8};
  expect(arrangementCueFromVar(juce::JSON::parse(
             juce::JSON::toString(arrangementCueToVar(variableCount))), decoded) &&
             decoded == variableCount,
         "variable tempo count offsets survive JSON without imposing a fixed interval");
  auto offsetsWithoutInterval = arrangementCueToVar(variableCount);
  offsetsWithoutInterval.getDynamicObject()->removeProperty("countIntervalSec");
  expect(arrangementCueFromVar(offsetsWithoutInterval, decoded) && decoded == variableCount,
         "an explicit source pulse map does not require the legacy interval field");
  SongArrangementSnapshot offsetArrangement;
  offsetArrangement.parts = {verse};
  offsetArrangement.cues = {variableCount};
  expect(!normalizeArrangementCues(offsetArrangement) &&
             arrangementCueEventCount(offsetArrangement) == 4,
         "normalization retains a valid variable tempo sequence and counts its expanded events");
  decoded = count;
  expect(!arrangementCueFromVar(juce::JSON::parse(
             R"({"name":"1","positionSec":7.5,"countEnd":4,"countIntervalSec":0})"), decoded) &&
             decoded == count,
         "invalid count interval is rejected without a partial update");
  for (const auto *invalid : {
           R"({"name":"Build","positionSec":"7.5"})",
           R"({"name":"Build","positionSec":-1})",
           R"({"name":"Build","offsetSec":-1})",
           R"({"name":"4","countEnd":2,"countIntervalSec":0.5})",
           R"({"name":"1","countEnd":8,"countIntervalSec":0.5})",
           R"({"name":"1","countEnd":4,"countOffsetsSec":[0,0.5,1]})",
           R"({"name":"1","countEnd":4,"countOffsetsSec":[0.1,0.5,1,1.5]})",
           R"({"name":"1","countEnd":4,"countOffsetsSec":[0,0.5,0.5,1.5]})",
           R"({"name":"1","countEnd":4,"countOffsetsSec":[0,1,0.5,1.5]})",
           R"({"name":"1","countEnd":4,"countOffsetsSec":[0,0.5,"1",1.5]})",
           R"({"name":"1","countEnd":4,"countOffsetsSec":[]})",
           R"({"name":"1","countOffsetsSec":[0]})"})
    expect(!arrangementCueFromVar(juce::JSON::parse(invalid), decoded) && decoded == count,
           "malformed cues cannot partially replace an existing instruction");
  for (const double nonFinite : {std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::quiet_NaN()}) {
    auto invalidOffsets = arrangementCueToVar(variableCount);
    auto *offsets = invalidOffsets.getDynamicObject()->getProperty("countOffsetsSec").getArray();
    offsets->getReference(2) = nonFinite;
    expect(!arrangementCueFromVar(invalidOffsets, decoded) && decoded == count,
           "nonfinite count offsets are rejected atomically before JSON or remote publication");
  }

  ImportedSongData song;
  song.arrangement.parts = {verse};
  song.arrangement.cues = {cue, count, variableCount};
  song.arrangement.cues.back().id = "variable-count";
  auto encoded = playworship::remote::encodeSong(song, false);
  expect(playworship::remote::validateArrangementAutomation(
             encoded.getProperty("arrangement", {})),
         "remote arrangement validation accepts individual cues and count sequences");
  const auto remote = playworship::remote::decodeSong(encoded);
  expect(remote.arrangement.parts.size() == 1 && remote.arrangement.cues == song.arrangement.cues,
         "remote codec preserves primary sections and independent secondary marks");
  auto *invalidArray = new juce::DynamicObject();
  invalidArray->setProperty("parts", juce::Array<juce::var>{});
  invalidArray->setProperty("cues", juce::JSON::parse(R"([{"name":"1","countEnd":4}])"));
  expect(!playworship::remote::validateArrangementAutomation(juce::var(invalidArray)),
         "remote validation rejects a malformed cue array before applying arrangement state");
  SongArrangementSnapshot limitArrangement;
  auto seven = count;
  seven.countEnd = 7;
  for (int i = 0; i < 73; ++i) {
    seven.id = "limit-count-" + juce::String(i);
    limitArrangement.cues.push_back(seven);
  }
  limitArrangement.cues.push_back(cue);
  ImportedSongData limitSong;
  limitSong.arrangement = limitArrangement;
  expect(arrangementCueEventCount(limitArrangement) == kMaxArrangementCueEvents &&
             playworship::remote::validateArrangementAutomation(
                 playworship::remote::encodeSong(limitSong, false).getProperty("arrangement", {})),
         "remote validation accepts exactly 512 expanded guide events including sequences");
  auto excess = cue;
  excess.id = "excess";
  limitSong.arrangement.cues.push_back(excess);
  expect(arrangementCueEventCount(limitSong.arrangement) == kMaxArrangementCueEvents + 1 &&
             !playworship::remote::validateArrangementAutomation(
                 playworship::remote::encodeSong(limitSong, false).getProperty("arrangement", {})),
         "remote validation rejects sequences that expand beyond scheduler capacity");
  bool excessChanged = false;
  const auto legacyExcess = SongLibraryStore::songDataFromVar(
      SongLibraryStore::songDataToVar(limitSong), excessChanged);
  expect(legacyExcess.arrangement.cues.size() == limitSong.arrangement.cues.size() &&
             arrangementCueEventCount(legacyExcess.arrangement) == kMaxArrangementCueEvents + 1,
         "legacy disk payloads retain all markers even when playback capacity is exceeded");

  ImportedSongData legacyLimitSong;
  auto legacyPrimary = verse;
  legacyPrimary.startSec = 0.0;
  legacyPrimary.endSec = 1.0;
  legacyLimitSong.arrangement.parts.push_back(legacyPrimary);
  for (int i = 0; i < kMaxArrangementCueEvents; ++i) {
    auto legacyInstruction = legacyPrimary;
    legacyInstruction.id = "legacy-limit-" + juce::String(i);
    legacyInstruction.name = "Build";
    legacyInstruction.utilityCue = true;
    legacyInstruction.startSec = i + 1.0;
    legacyInstruction.endSec = i + 2.0;
    legacyLimitSong.arrangement.parts.push_back(std::move(legacyInstruction));
  }
  auto legacyLimitPayload = playworship::remote::encodeSong(legacyLimitSong, false)
                                .getProperty("arrangement", {});
  expect(!legacyLimitPayload.getDynamicObject()->hasProperty("cues") &&
             playworship::remote::validateArrangementAutomation(legacyLimitPayload),
         "remote validation accepts 512 legacy clips that migrate to independent cues");
  auto legacyExtra = legacyLimitSong.arrangement.parts.back();
  legacyExtra.id = "legacy-extra";
  legacyExtra.startSec += 1.0;
  legacyExtra.endSec += 1.0;
  legacyLimitSong.arrangement.parts.push_back(legacyExtra);
  const auto legacyExcessPayload = SongLibraryStore::songDataToVar(legacyLimitSong);
  expect(!playworship::remote::validateArrangementAutomation(
             legacyExcessPayload.getProperty("arrangement", {})),
         "remote validation rejects legacy clips that migrate beyond 512 guide events");
  bool legacyLimitChanged = false;
  const auto oversizedLegacy = SongLibraryStore::songDataFromVar(
      legacyExcessPayload, legacyLimitChanged);
  expect(legacyLimitChanged && oversizedLegacy.arrangement.parts.size() == 1 &&
             oversizedLegacy.arrangement.cues.size() == kMaxArrangementCueEvents + 1,
         "remote rejection does not trim migrated markers in legacy disk storage");

  // Mixed old/new payloads must count both newly migrated points and each
  // expanded pulse of a sequence, rather than either representation alone.
  ImportedSongData mixedLimitSong;
  mixedLimitSong.arrangement.parts.assign(legacyLimitSong.arrangement.parts.begin(),
                                        legacyLimitSong.arrangement.parts.begin() + 2);
  mixedLimitSong.arrangement.cues.assign(limitArrangement.cues.begin(),
                                       limitArrangement.cues.begin() + 73);
  expect(playworship::remote::validateArrangementAutomation(
             SongLibraryStore::songDataToVar(mixedLimitSong).getProperty("arrangement", {})),
         "remote validation accepts 511 count events plus one migrated legacy instruction");
  mixedLimitSong.arrangement.parts.push_back(legacyLimitSong.arrangement.parts[2]);
  expect(!playworship::remote::validateArrangementAutomation(
             SongLibraryStore::songDataToVar(mixedLimitSong).getProperty("arrangement", {})),
         "remote validation rejects count sequences plus migrated legacy instructions over capacity");

  // This is the actual legacy insertion shape: a short instruction split the
  // original Verse 2, retaining its clip flags and automation on both fragments.
  auto left = verse;
  left.startSec = 0.0;
  left.endSec = 4.0;
  left.automation = automationFixture(fixtures.stemId);
  auto instruction = left;
  instruction.id = "legacy-build";
  instruction.name = "Build";
  instruction.utilityCue = true;
  instruction.startSec = 4.0;
  instruction.endSec = 4.5;
  auto right = left;
  right.id = "verse-tail";
  right.startSec = 4.5;
  right.endSec = 10.0;
  SongArrangementSnapshot legacy;
  legacy.parts = {left, instruction, right};
  SongArrangementCueSnapshot tailCue = cue;
  tailCue.id = "tail-mark";
  tailCue.partId = right.id;
  tailCue.positionSec = 6.0;
  tailCue.offsetSec = 1.5;
  legacy.cues.push_back(tailCue);
  const auto before = legacy;
  expect(migrateLegacyArrangementCues(legacy) && legacy.parts.size() == 1 &&
             legacy.parts.front().id == left.id &&
             legacy.parts.front().name == left.name &&
             legacy.parts.front().startSec == 0.0 &&
             legacy.parts.front().endSec == 10.0 &&
             legacy.parts.front().automation == left.automation &&
             legacy.cues.size() == 2,
         "legacy utility migration restores the complete Verse without changing its automation");
  expect(legacy.cues[0].id == instruction.id && legacy.cues[0].name == "Build" &&
             legacy.cues[0].partId == left.id && legacy.cues[0].offsetSec == 4.0 &&
             legacy.cues[1].partId == left.id && legacy.cues[1].offsetSec == 6.0 &&
             resolveArrangementCueSeconds(legacy, legacy.cues[1]) == 6.0,
         "migration retains instruction timing and reanchors marks on an absorbed fragment");
  for (double position : {0.0, 3.99, 4.0, 4.49, 4.5, 7.0, 10.0}) {
    double oldSource = -1.0, newSource = -1.0;
    expect(arrangement_source_mapping::mapArrangementSecondsToSourceSeconds(
               before.parts, position, 20.0, oldSource) &&
               arrangement_source_mapping::mapArrangementSecondsToSourceSeconds(
                   legacy.parts, position, 20.0, newSource) && oldSource == newSource,
           "restoring musical annotation boundaries does not alter source audio mapping");
  }
  expect(!migrateLegacyArrangementCues(legacy), "legacy migration is idempotent");
  auto unsafe = before;
  unsafe.parts[1].automation.pads.volume = 0.91f;
  expect(!migrateLegacyArrangementCues(unsafe) && unsafe.parts.size() == 3 &&
             unsafe.parts[1].automation.pads.volume == 0.91f,
         "a legacy utility with distinct automation is retained without losing its preset");
  unsafe = before;
  unsafe.parts[2].name = "Chorus";
  expect(!migrateLegacyArrangementCues(unsafe) && unsafe.parts.size() == 3,
         "ambiguous instructions between different musical sections retain their ranges");
  unsafe = before;
  auto referenced = left;
  referenced.id = "copy";
  referenced.startSec = 10.0;
  referenced.endSec = 14.0;
  referenced.sourcePartId = left.id;
  referenced.sourceStartSec = 0.0;
  referenced.sourceEndSec = 4.0;
  unsafe.parts.push_back(referenced);
  expect(!migrateLegacyArrangementCues(unsafe) && unsafe.parts.size() == 4,
         "migration does not widen a source section referenced by an existing copy");
  auto numeric = before;
  numeric.parts[1].name = "3";
  numeric.parts[1].utilityCue = false;
  expect(migrateLegacyArrangementCues(numeric) && numeric.cues.front().name == "3",
         "legacy numeric counts also become independent points");
  auto translated = before;
  translated.parts[1].name = "Tres";
  translated.parts[1].utilityCue = false;
  expect(migrateLegacyArrangementCues(translated), "localized legacy counts retain their cue identity");
  for (const auto *name : {"Breakdown", "bReAkDoWn", "Baja intensidad",
                           "A Capella", "acapella", "ACAPELLA"}) {
    auto secondary = before;
    secondary.parts[1].name = name;
    secondary.parts[1].utilityCue = false;
    expect(migrateLegacyArrangementCues(secondary) && secondary.parts.size() == 1 &&
               secondary.cues.front().name == name,
           "secondary instructions migrate without requiring a legacy utility flag");
  }
  SongArrangementSnapshot trailing;
  trailing.parts = {left, instruction};
  expect(migrateLegacyArrangementCues(trailing) && trailing.parts.size() == 1 &&
             trailing.parts.front().endSec == instruction.endSec &&
             trailing.cues.front().positionSec == instruction.startSec,
         "an unambiguous trailing instruction becomes a cue and extends its musical parent");
  SongArrangementSnapshot leading;
  leading.parts = {instruction, right};
  leading.cues = {tailCue};
  expect(migrateLegacyArrangementCues(leading) && leading.parts.size() == 1 &&
             leading.parts.front().startSec == instruction.startSec &&
             leading.cues.size() == 2 &&
             leading.cues[1].offsetSec == 2.0 &&
             leading.cues[1].positionSec == 6.0,
         "a leading instruction is absorbed without shifting existing cues on its parent");
  SongArrangementSnapshot onlyInstructions;
  onlyInstructions.parts = {instruction};
  expect(!migrateLegacyArrangementCues(onlyInstructions) && onlyInstructions.parts.size() == 1,
         "legacy instruction-only arrangements remain intact without inventing musical sections");
  auto unequalMap = before;
  for (auto &part : unequalMap.parts) {
    part.sourcePartId = "source";
    part.sourceStartSec = part.startSec;
    part.sourceEndSec = part.endSec;
  }
  unequalMap.parts[1].sourceEndSec = 4.4;
  expect(!migrateLegacyArrangementCues(unequalMap) && unequalMap.parts.size() == 3,
         "noncontinuous virtual source mappings prevent legacy conversion");
  auto virtualFragments = before;
  for (auto &part : virtualFragments.parts) {
    part.sourcePartId = "source";
    part.sourceStartSec = part.startSec;
    part.sourceEndSec = part.endSec;
    part.startSec += 10.0;
    part.endSec += 10.0;
  }
  virtualFragments.cues.clear();
  const auto beforeVirtual = virtualFragments;
  expect(migrateLegacyArrangementCues(virtualFragments) &&
             virtualFragments.parts.size() == 1 &&
             virtualFragments.parts.front().sourceStartSec == 0.0 &&
             virtualFragments.parts.front().sourceEndSec == 10.0,
         "continuous virtual fragments merge without losing their explicit source region");
  for (double position : {10.0, 13.99, 14.0, 14.49, 14.5, 17.0, 20.0, 23.0}) {
    double oldSource = -1.0, newSource = -1.0;
    expect(arrangement_source_mapping::mapArrangementSecondsToSourceSeconds(
               beforeVirtual.parts, position, 30.0, oldSource) &&
               arrangement_source_mapping::mapArrangementSecondsToSourceSeconds(
                   virtualFragments.parts, position, 30.0, newSource) &&
               std::abs(oldSource - newSource) < 0.00001,
           "virtual migration preserves source audio and the inserted-duration offset");
  }

  bool changed = false;
  const auto oldDocument = SongLibraryStore::songDataFromVar(
      juce::JSON::parse(R"({"arrangement":{"parts":[{"id":"intro","name":"Intro","endSec":8}]}})"),
      changed);
  expect(oldDocument.arrangement.parts.size() == 1 && oldDocument.arrangement.cues.empty(),
         "documents without the new cues field preserve all original musical sections");
  ImportedSongData legacySong;
  legacySong.arrangement = before;
  const auto migratedDocument = SongLibraryStore::songDataFromVar(
      SongLibraryStore::songDataToVar(legacySong), changed);
  expect(migratedDocument.arrangement.parts.size() == 1 &&
             migratedDocument.arrangement.cues.size() == 2,
         "library/project deserialization applies the conservative migration once");
}

ProjectDocument makeDocument(const Fixtures &fixtures, const juce::File &stem,
                              const juce::File &artwork) {
  ProjectDocument document;
  document.song.id = fixtures.projectId;
  document.song.projectId = fixtures.projectId;
  document.song.title = juce::String::fromUTF8(u8"Canción de integración");
  document.song.artist = "Artista de prueba";
  document.song.bpm = 92;
  document.song.key = "Bb";
  document.song.timeSignature = "6/8";
  document.song.createdMs = 1700000000000;
  document.song.artworkSource = "manual:" + artwork.getFullPathName();
  expect(document.song.artworkPath.isEmpty(),
         "fixture must reproduce a wizard draft with only artworkSource");
  ImportedStemData track;
  track.stemId = fixtures.stemId;
  track.sourcePath = stem.getFullPathName();
  track.fileName = stem.getFileName();
  track.displayName = "Piano";
  track.category = "Keys";
  track.durationSec = static_cast<double>(sampleCount) / sampleRate;
  track.sampleRate = static_cast<int>(sampleRate);
  track.channels = 1;
  document.song.stems.push_back(track);

  document.settings.tempoBpm = 96.0;
  document.settings.pitchSemitones = -2;
  document.settings.mix.master.volume = 0.67f;
  document.settings.mix.master.gain = 1.12f;
  StemMixSnapshot mix;
  mix.stemId = fixtures.stemId;
  mix.volume = 0.42f;
  mix.solo = true;
  document.settings.mix.stems.push_back(mix);
  document.settings.window.endSec = track.durationSec;
  document.settings.window.fadeOutSec = 0.025;
  document.settings.windowCustomized = true;
  SongArrangementPartSnapshot section;
  section.id = "verse";
  section.name = "Verse";
  section.color = standardArrangementPartColorForName(section.name);
  section.endSec = track.durationSec;
  section.sourceEndSec = track.durationSec;
  section.automation = automationFixture(fixtures.stemId);
  document.song.arrangement.parts.push_back(section);
  SongArrangementCueSnapshot count;
  count.id = "count-in";
  count.name = "1";
  count.color = "ff73a984";
  count.partId = section.id;
  count.positionSec = 0.032;
  count.offsetSec = 0.032;
  count.countEnd = 4;
  count.countIntervalSec = 0.02;
  count.countOffsetsSec = {0.0, 0.02, 0.043, 0.071};
  document.song.arrangement.cues.push_back(count);
  document.settings.arrangement = document.song.arrangement;
  document.settings.arrangementCustomized = true;
  return document;
}

void inspectPackage(const Fixtures &fixtures, const ProjectDocument &document) {
  juce::ZipFile zip(fixtures.archive);
  expect(zip.getNumEntries() == 3, "project contains manifest, FLAC and PNG only");
  const auto stemEntry = "audio/" + fixtures.stemId + ".flac";
  expect(zip.getIndexOfFileName(stemEntry) >= 0 &&
             zip.getIndexOfFileName("artwork.png") >= 0,
         "portable project embeds actual audio and manual artwork");
  const int manifestIndex = zip.getIndexOfFileName("manifest.json");
  expect(manifestIndex >= 0, "project has its real manifest");
  const auto stream = std::unique_ptr<juce::InputStream>(
      zip.createStreamForEntry(manifestIndex));
  expect(stream != nullptr, "manifest stream is readable");
  const auto json = stream->readEntireStreamAsString();
  expect(!json.contains(fixtures.directory.getFullPathName()) &&
             !json.contains("manual:"),
         "manifest contains no dependency on fixture/manual paths");
  const auto value = juce::JSON::parse(json);
  const auto *root = value.getDynamicObject();
  expect(root != nullptr, "manifest is valid JSON");
  const auto songValue = root->getProperty("song");
  const auto *song = songValue.getDynamicObject();
  expect(song != nullptr && song->getProperty("title").toString() == document.song.title &&
             song->getProperty("artist").toString() == document.song.artist &&
             song->getProperty("artworkPath").toString() == "artwork.png" &&
             song->getProperty("artworkSource").toString().isEmpty(),
         "manifest preserves metadata and uses a portable cover entry");
  const auto *cues = song->getProperty("arrangement").getProperty("cues", {}).getArray();
  expect(cues != nullptr && cues->size() == 1 &&
             cues->getReference(0).getProperty("countEnd", 0) == juce::var(4),
         "portable manifest includes independent cues and sequence parameters");
}

void expectRegisteredSong(const Fixtures &fixtures, const ImportedSongData &expected) {
  const auto library = SongLibraryStore::loadLibrarySnapshot();
  expect(library.songs.size() == 1, "reload finds exactly the registered project song");
  const auto &saved = library.songs.front();
  expect(saved.id == expected.id && saved.projectId == fixtures.projectId &&
             saved.title == expected.title && saved.artist == expected.artist &&
             saved.bpm == expected.bpm && saved.key == expected.key &&
             saved.timeSignature == expected.timeSignature,
         "registered song retains metadata and portable identity after library reload");
  expect(juce::File(saved.projectPath) == fixtures.archive &&
             juce::File(saved.projectPath).existsAsFile() && saved.stems.size() == 1,
         "reloaded library retains the valid portable project path and its stem");
  expect(saved.stems.front().stemId == fixtures.stemId &&
             juce::File(saved.stems.front().storedPath).existsAsFile() &&
             saved.stems.front().sampleRate == sampleRate &&
             saved.stems.front().channels == 1 &&
             juce::ImageFileFormat::loadFrom(juce::File(saved.artworkPath)).isValid(),
         "reloaded library retains readable cached audio and artwork paths");
}

// Deterministically block replacing only this test's songs.json. A nonempty
// directory survives attempts to replace the database; the real isolated
// database is held in our fixture directory and restored even on assertion.
class BlockedLibraryFile {
public:
  explicit BlockedLibraryFile(const Fixtures &fixtures)
      : target(fixtures.libraryFile),
        backup(fixtures.directory.getChildFile("registered-library.backup")),
        sentinel(target.getChildFile("test-sentinel.txt")) {}

  void block() {
    expect(target.moveFileTo(backup), "back up the test database before fault injection");
    expect(target.createDirectory().wasOk() && sentinel.replaceWithText("test-owned marker"),
           "create a nonempty test-owned destination to reject database replacement");
  }

  bool isIntact() const {
    return target.isDirectory() && sentinel.loadFileAsString() == "test-owned marker";
  }

  bool restore() {
    if (!backup.existsAsFile())
      return true;
    if (sentinel.existsAsFile() && !sentinel.deleteFile())
      return false;
    if (target.exists() && !target.deleteFile())
      return false;
    return backup.moveFileTo(target);
  }

  ~BlockedLibraryFile() { restore(); }

private:
  juce::File target, backup, sentinel;
};

void testLibraryRegistration(const Fixtures &fixtures, ImportedSongData song) {
  expect(SongLibraryStore::registerProjectSong(song),
         "created portable project registers successfully in the isolated library");
  expect(fixtures.libraryFile.existsAsFile(), "registration actually persists songs.json");
  expectRegisteredSong(fixtures, song);
  expect(SongLibraryStore::registerProjectSong(song),
         "registering the same project a second time remains valid");
  expectRegisteredSong(fixtures, song);

  juce::MemoryBlock originalLibrary;
  expect(fixtures.libraryFile.loadFileAsData(originalLibrary), "record registered database bytes");
  {
    BlockedLibraryFile blocked(fixtures);
    blocked.block();
    auto anotherSong = song;
    anotherSong.id = juce::Uuid().toString();
    anotherSong.projectId = anotherSong.id;
    anotherSong.title = "Registration must fail";
    expect(!SongLibraryStore::registerProjectSong(anotherSong),
           "database commit failure must not report a successfully registered song");
    expect(blocked.isIntact(), "failed commit must not replace the blocking sentinel directory");
    expect(fixtures.archive.existsAsFile(), "failed registration preserves the created package");
    expect(blocked.restore(), "restore the test's original registered database");
  }
  juce::MemoryBlock restoredLibrary;
  expect(fixtures.libraryFile.loadFileAsData(restoredLibrary) &&
             restoredLibrary == originalLibrary,
         "failed commit does not alter the prior registered database");
  expectRegisteredSong(fixtures, song);
}

void testMusicalWaveform(const Fixtures &fixtures) {
  auto makeStem = [&](const char *name, const char *category, int samples,
                      int pulseAt, float amplitude) {
    const auto file = fixtures.directory.getChildFile(juce::String::fromUTF8(name) + ".wav");
    {
      juce::WavAudioFormat format;
      std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
      auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
          .withSampleRate(8000).withNumChannels(1).withBitsPerSample(16));
      expect(writer != nullptr, "create waveform fixture");
      juce::AudioBuffer<float> audio(1, samples);
      audio.clear();
      audio.setSample(0, pulseAt, amplitude);
      expect(writer->writeFromAudioSampleBuffer(audio, 0, samples), "write waveform pulse");
    }
    ImportedStemData stem;
    stem.fileName = file.getFileName();
    stem.sourcePath = file.getFullPathName();
    stem.displayName = juce::String::fromUTF8(name);
    stem.category = category;
    stem.durationSec = samples / 8000.0;
    return stem;
  };
  auto music = makeStem("Piano", "Piano", 256, 64, 0.1f);
  auto click = makeStem("Reference pulse", "Click", 256, 192, 0.9f);
  auto guide = makeStem("Spoken instructions", "Cues", 256, 128, 0.8f);
  ImportedSongData song;
  song.stems = {music};
  const auto reference = buildMixedWaveformBins(song, 8);
  expect(reference.size() == 8 && reference[2] == 1.0f,
         "musical pulse has the expected timeline position");
  for (const auto &excluded : {click, guide,
         makeStem("GUIDE", "Other", 256, 128, 0.9f),
         makeStem("Guía", "Other", 256, 128, 0.9f),
         makeStem("CLIC", "Other", 256, 192, 0.9f)}) {
    song.stems = {music, excluded};
    double progress = 0.0;
    const auto bins = buildMixedWaveformBins(song, 8, [&](double next) {
      expect(next >= progress, "waveform progress remains monotonic");
      progress = next;
    });
    expect(bins == reference && progress == 1.0,
           "click/guide neither add peaks nor change music normalization");
    expect(song.stems.size() == 2 && song.stems[1].include,
           "visual exclusion never removes or disables playback stems");
  }
  click = makeStem("Long click", "Click", 512, 400, 0.9f);
  song.stems = {music, click};
  const auto fullTimeline = buildMixedWaveformBins(song, 8);
  expect(fullTimeline[1] == 1.0f && fullTimeline[2] == 0.0f && fullTimeline[6] == 0.0f,
         "long click preserves full timeline without drawing its pulse");
  song.stems = {click, guide};
  const auto guidesOnly = buildMixedWaveformBins(song, 8);
  expect(guidesOnly.size() == 8 &&
             std::all_of(guidesOnly.begin(), guidesOnly.end(), [](float v) { return v == 0.0f; }),
         "click/guide-only song produces a flat waveform");
}

void runRoundtripAndFailureChecks(Fixtures &fixtures) {
  const auto source = fixtures.directory.getChildFile("source.flac");
  const auto cover = fixtures.directory.getChildFile("cover.png");
  writeFlac(source);
  writePng(cover);
  const auto document = makeDocument(fixtures, source, cover);
  juce::String error;
  double progress = -1.0;
  expect(ProjectStore::save(document, fixtures.archive, &error, &progress),
         "real ProjectStore::save succeeds for wizard-style draft");
  expect(error.isEmpty() && progress == 1.0, "successful save reports completion");
  inspectPackage(fixtures, document);
  juce::MemoryBlock originalPackage;
  expect(fixtures.archive.loadFileAsData(originalPackage), "record created package bytes");

  expect(source.deleteFile() && cover.deleteFile(), "delete only this test's source fixtures");
  ProjectDocument opened;
  expect(ProjectStore::open(fixtures.archive, opened, &error),
         "real ProjectStore::open succeeds without original source files");
  expect(opened.song.title == document.song.title && opened.song.artist == document.song.artist &&
             opened.song.bpm == 92 && opened.song.key == "Bb" &&
             opened.song.timeSignature == "6/8" &&
             opened.song.projectId == fixtures.projectId && opened.song.stems.size() == 1,
         "opening preserves song metadata and identity");
  expect(ProjectStore::settingsJsonEqual(opened.settings, document.settings),
         "opening preserves all nondefault mixer/performance settings");
  expect(opened.song.arrangement.parts.size() == 1 &&
             opened.song.arrangement.parts.front().automation ==
                 document.song.arrangement.parts.front().automation &&
             opened.song.arrangement.cues == document.song.arrangement.cues,
         "portable manifest preserves the song's section automation and independent cues");
  ProjectSongSettings canonical;
  expect(ProjectStore::deserializeSettings(
             juce::JSON::parse(juce::JSON::toString(ProjectStore::serializeSettings(document.settings))),
             canonical) && ProjectStore::settingsJsonEqual(canonical, document.settings),
         "Cloud canonical settings preserve section automation and independent cues");
  auto cueOnlyEdit = document.settings;
  cueOnlyEdit.arrangement.cues.front().offsetSec = 0.042;
  cueOnlyEdit.arrangement.cues.front().positionSec = 0.042;
  expect(!ProjectStore::settingsJsonEqual(cueOnlyEdit, document.settings) &&
             ProjectStore::writeCachedSettingsFromFile(fixtures.archive, cueOnlyEdit) &&
             ProjectStore::getCachedSettings(fixtures.projectId, canonical) &&
             ProjectStore::settingsJsonEqual(canonical, cueOnlyEdit),
         "cue-only edits change settings fingerprints and survive project sidecar persistence");
  auto automationOnlyEdit = document.settings;
  automationOnlyEdit.arrangement.parts.front().automation.pads.noteIndex = 8;
  expect(!ProjectStore::settingsJsonEqual(automationOnlyEdit, document.settings),
         "automation-only edits are recognized as persistence changes");
  expect(ProjectStore::writeCachedSettingsFromFile(fixtures.archive, automationOnlyEdit) &&
             ProjectStore::getCachedSettings(fixtures.projectId, canonical) &&
             ProjectStore::settingsJsonEqual(canonical, automationOnlyEdit),
         "project sidecar roundtrip preserves an automation-only edit");
  expect(ProjectStore::writeCachedSettingsFromFile(fixtures.archive, document.settings),
         "restore original test sidecar before package failure checks");
  const auto image = juce::ImageFileFormat::loadFrom(juce::File(opened.song.artworkPath));
  expect(image.isValid() && image.getWidth() == 8 && image.getHeight() == 8 &&
             image.getPixelAt(3, 4) == juce::Colour(0xff27cd68),
         "extracted artwork remains a valid, unchanged PNG");
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  const auto reader = std::unique_ptr<juce::AudioFormatReader>(
      formats.createReaderFor(juce::File(opened.song.stems.front().storedPath)));
  expect(reader != nullptr && reader->sampleRate == sampleRate &&
             reader->numChannels == 1 && reader->lengthInSamples == sampleCount,
         "extracted FLAC is decodable with its original audio dimensions");
  juce::AudioBuffer<float> decoded(1, sampleCount);
  expect(reader->read(&decoded, 0, sampleCount, 0, true, false), "read extracted audio");
  for (int i = 0; i < sampleCount; ++i)
    expect(std::abs(decoded.getSample(0, i) - expectedSample(i)) < 0.0001f,
           "FLAC roundtrip preserves source samples within 16-bit precision");

  const auto expectUnchangedArchive = [&] {
    juce::MemoryBlock current;
    expect(fixtures.archive.loadFileAsData(current) && current == originalPackage,
           "failed save leaves existing portable project byte-for-byte intact");
  };
  progress = -1.0;
  expect(!ProjectStore::save(document, fixtures.archive, &error, &progress) &&
             error.isNotEmpty() && progress < 1.0,
         "missing source cannot report successful creation");
  expectUnchangedArchive();

  writeFlac(source);
  expect(!ProjectStore::save(document, fixtures.archive, &error) && error.isNotEmpty(),
         "missing manually selected cover cannot silently disappear on save");
  expectUnchangedArchive();

  writePng(cover);
  const auto invalidSource = fixtures.directory.getChildFile("invalid.flac");
  expect(invalidSource.replaceWithText("not a FLAC stream"), "create invalid audio fixture");
  auto invalid = document;
  invalid.song.stems.front().sourcePath = invalidSource.getFullPathName();
  invalid.song.stems.front().durationSec = 0.0;
  invalid.song.stems.front().sampleRate = 0;
  invalid.song.stems.front().channels = 0;
  expect(!ProjectStore::save(invalid, fixtures.archive, &error) &&
             error.contains(invalidSource.getFileName()),
         "invalid undecodable source does not report success");
  expectUnchangedArchive();
  expect(!fixtures.libraryFile.exists(),
         "package-only operations do not register prematurely in the library");
  testLibraryRegistration(fixtures, opened.song);
}
void testAutomaticLegacyStemStorage(const Fixtures &fixtures) {
  // Old settings must not disable automatic storage conversion. Write the
  // historical key directly so the removed public toggle is not needed.
  const auto settings = ApplicationDataPaths::applicationDataDirectory()
                            .getChildFile("MTControls/app_settings.json");
  expect(settings.getParentDirectory().createDirectory().wasOk() &&
             settings.replaceWithText(R"({"stemOptimizationEnabled":false})"),
         "seed the historical disabled optimization preference");

  const auto wav = fixtures.directory.getChildFile("automatic.wav");
  const auto flac = fixtures.directory.getChildFile("unrelated-original-name.flac");
  const auto unsupported = fixtures.directory.getChildFile("preserve-original.audio");
  writeWav(wav);
  writeFlac(flac);
  expect(unsupported.replaceWithText("unsupported audio payload retained exactly"),
         "create an unconvertible legacy payload");
  juce::MemoryBlock originalWav, originalFlac, originalUnsupported;
  expect(wav.loadFileAsData(originalWav) && flac.loadFileAsData(originalFlac) &&
             unsupported.loadFileAsData(originalUnsupported),
         "record source bytes before importing");

  ImportedSongData draft;
  draft.id = juce::Uuid().toString();
  draft.title = "Automatic legacy storage";
  draft.fileType = "WAV";
  draft.bpm = 96;
  auto makeStem = [](const juce::File &file, const char *id) {
    ImportedStemData stem;
    stem.stemId = id;
    stem.displayName = id;
    stem.category = "Other";
    stem.include = true;
    stem.sourcePath = file.getFullPathName();
    stem.fileName = file.getFileName();
    stem.durationSec = static_cast<double>(sampleCount) / sampleRate;
    stem.sampleRate = sampleRate;
    stem.channels = 1;
    return stem;
  };
  draft.stems = {makeStem(wav, "convert-wav"), makeStem(flac, "copy-flac"),
                 makeStem(unsupported, "keep-original")};
  juce::String error;
  expect(SongLibraryStore::appendSong(draft, &error),
         "legacy import succeeds with automatic conversion and fallback together");
  const auto readSong = [&]() {
    const auto songs = SongLibraryStore::loadSongs();
    const auto found = std::find_if(songs.begin(), songs.end(), [&](const auto &song) {
      return song.id == draft.id;
    });
    expect(found != songs.end(), "automatic storage song is registered");
    return *found;
  };
  const auto readStem = [](const ImportedSongData &song, const char *id) {
    const auto found = std::find_if(song.stems.begin(), song.stems.end(),
                                   [&](const auto &stem) { return stem.stemId == id; });
    expect(found != song.stems.end(), "all imported stems are preserved");
    return *found;
  };
  const auto sameBytes = [](const juce::File &file, const juce::MemoryBlock &bytes) {
    juce::MemoryBlock current;
    return file.loadFileAsData(current) && current == bytes;
  };
  const auto expectStoredAudio = [&](const ImportedSongData &song) {
    const juce::File optimized(readStem(song, "convert-wav").storedPath);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(optimized));
    expect(optimized.hasFileExtension("flac") && reader != nullptr &&
               reader->getFormatName().containsIgnoreCase("flac") &&
               reader->sampleRate == sampleRate && reader->numChannels == 1 &&
               reader->lengthInSamples == sampleCount,
           "WAV is automatically stored as decodable FLAC with unchanged dimensions");
    expect(sameBytes(juce::File(readStem(song, "copy-flac").storedPath), originalFlac),
           "an existing FLAC is copied byte-for-byte regardless of origin or filename");
    const juce::File fallback(readStem(song, "keep-original").storedPath);
    expect(fallback.hasFileExtension("audio") && sameBytes(fallback, originalUnsupported) &&
               !fallback.withFileExtension("flac").exists(),
           "failed conversion preserves original format and bytes without partial FLAC");
    expect(sameBytes(wav, originalWav) && sameBytes(flac, originalFlac) &&
               sameBytes(unsupported, originalUnsupported),
           "automatic storage never modifies the original files");
  };

  auto imported = readSong();
  expectStoredAudio(imported);
  // A replacement WAV must optimize during editing as well. Clearing the
  // cached path makes the existing production source-selection path use it.
  const auto replacement = fixtures.directory.getChildFile("replacement.wav");
  writeWav(replacement);
  auto edited = imported;
  edited.title = "Edited automatic storage";
  for (auto &stem : edited.stems) {
    if (stem.stemId == "convert-wav") {
      stem.sourcePath = replacement.getFullPathName();
      stem.storedPath.clear();
    }
  }
  expect(SongLibraryStore::updateSong(edited, &error),
         "legacy edit automatically converts a replacement WAV");
  imported = readSong();
  expectStoredAudio(imported);
  expect(imported.title == edited.title && replacement.existsAsFile(),
         "editing persists metadata and retains the replacement original");

  // Reject the database replacement after audio staging has completed; the
  // previous song directory and metadata must survive the failed commit.
  const juce::File previousAudio(readStem(imported, "convert-wav").storedPath);
  juce::MemoryBlock previousBytes;
  expect(previousAudio.loadFileAsData(previousBytes), "record edited audio before rollback");
  auto rejected = imported;
  rejected.title = "Must not replace saved metadata";
  bool blockedAtCommit = false;
  {
    BlockedLibraryFile blocked(fixtures);
    expect(!SongLibraryStore::updateSong(rejected, &error,
               [&](double progress, const juce::String &) {
                 if (!blockedAtCommit && progress >= 0.86) {
                   blocked.block();
                   blockedAtCommit = true;
                 }
               }) && blockedAtCommit && blocked.isIntact() && error.isNotEmpty(),
           "failed final database commit reports failure after audio staging");
    expect(sameBytes(previousAudio, previousBytes),
           "failed edit restores the previous stored audio byte-for-byte");
    expect(blocked.restore(), "restore the test database after injected failure");
  }
  expect(readSong().title == imported.title && replacement.existsAsFile(),
         "failed edit preserves registered metadata and source files");
  expect(SongLibraryStore::deleteSong(draft.id), "remove only the legacy fixture song");
}

void testColdProjectExtractionProgress(const Fixtures &fixtures) {
  const auto archive = fixtures.directory.getChildFile("cold-progress.pwproj");
  const auto shortSource = fixtures.directory.getChildFile("progress-short.flac");
  const auto longSource = fixtures.directory.getChildFile("progress-long.flac");
  constexpr int shortSamples = 2 * 1024 * 1024;
  constexpr int longSamples = 32 * 1024 * 1024;
  constexpr double fixtureRate = 48000.0;
  auto writeNoise = [](const juce::File &file, const int frames) {
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    juce::FlacAudioFormat format;
    auto writer = format.createWriterFor(
        stream, juce::AudioFormatWriterOptions{}
                    .withSampleRate(fixtureRate).withNumChannels(1)
                    .withBitsPerSample(16));
    expect(writer != nullptr, "create cold-load FLAC fixture writer");
    juce::AudioBuffer<float> block(1, 8192);
    juce::uint32 noise = 0x71a64b25;
    for (int written = 0; written < frames;) {
      const int count = juce::jmin(block.getNumSamples(), frames - written);
      for (int i = 0; i < count; ++i) {
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        block.setSample(0, i,
                        (static_cast<int>(noise & 0xffff) - 32768) / 32768.0f);
      }
      expect(writer->writeFromAudioSampleBuffer(block, 0, count),
             "write actual cold-load FLAC samples");
      written += count;
    }
  };
  // Four real FLAC entries with different lengths keep extraction in flight
  // after the first worker finishes. Noise avoids a tiny compressed fixture
  // that could finish before any visible progress is meaningful on fast SSDs.
  // Sources are streamed in 8K blocks; the test never allocates their PCM in RAM.
  writeNoise(shortSource, shortSamples);
  writeNoise(longSource, longSamples);
  ProjectDocument document;
  document.song.id = juce::Uuid().toString();
  document.song.projectId = document.song.id;
  document.song.title = "Cold extraction progress";
  document.song.bpm = 96;
  document.song.key = "C";
  document.song.timeSignature = "4/4";
  document.song.fileType = "FLAC";
  for (int index = 0; index < 4; ++index) {
    const bool shorter = index == 0;
    const auto source = shorter ? shortSource : longSource;
    ImportedStemData stem;
    stem.stemId = "progress-stem-" + juce::String(index);
    stem.displayName = "Progress stem " + juce::String(index);
    stem.category = "Piano";
    stem.include = true;
    stem.sourcePath = source.getFullPathName();
    stem.storedPath = stem.sourcePath;
    stem.fileName = source.getFileName();
    stem.sampleRate = static_cast<int>(fixtureRate);
    stem.channels = 1;
    stem.durationSec = (shorter ? shortSamples : longSamples) / fixtureRate;
    document.song.stems.push_back(std::move(stem));
  }
  document.settings.tempoBpm = 96.0;
  document.settings.window.endSec = longSamples / fixtureRate;
  struct Cleanup {
    juce::String projectId;
    juce::File archive;
    ~Cleanup() { ProjectStore::discardLocalState(projectId, archive); }
  } cleanup{document.song.id, archive};
  juce::String error;
  expect(ProjectStore::save(document, archive, &error),
         "save a real multi-stem package for cold extraction");
  expect(!ProjectStore::getCacheDirectory(document.song.id).exists(),
         "progress test starts without an extracted cache");

  std::vector<double> updates;
  const auto caller = std::this_thread::get_id();
  bool callbacksUseCaller = true;
  bool sawPartialExtraction = false;
  ProjectDocument opened;
  expect(ProjectStore::open(
             archive, opened, &error, true,
             [&](const double progress, const juce::String &) {
               callbacksUseCaller = callbacksUseCaller &&
                                    std::this_thread::get_id() == caller;
               updates.push_back(progress);
               sawPartialExtraction = sawPartialExtraction ||
                                      (progress > 0.05 && progress < 0.95);
             }),
         "cold multi-stem package opens successfully");
  expect(callbacksUseCaller,
         "parallel extraction publishes all progress on the calling thread");
  expect(sawPartialExtraction,
         "cold extraction reports actual partial work before validation finishes");
  expect(!updates.empty() && updates.front() == 0.0 && updates.back() == 1.0 &&
             std::is_sorted(updates.begin(), updates.end()),
         "cold-load progress is monotonic from start through successful completion");
  expect(opened.song.stems.size() == document.song.stems.size(),
         "progress reporting preserves every extracted stem");
  for (size_t index = 0; index < opened.song.stems.size(); ++index) {
    const juce::File extracted(opened.song.stems[index].storedPath);
    const juce::File source(document.song.stems[index].storedPath);
    expect(extracted.existsAsFile() && extracted.getSize() == source.getSize(),
           "every extracted FLAC retains its complete real payload");
  }
}

} // namespace

int main() {
  juce::File isolatedRoot;
  if (!isolatedStorageRoot(isolatedRoot)) {
    std::cerr << "SKIP: project creation test needs an isolated temporary app-data root; "
                 "compile with MTCONTROLS_ISOLATED_STORAGE_TESTS=1 and set "
                 "PLAYWORSHIP_CREATION_TEST_ROOT.\n";
    return 77;
  }
  juce::ScopedJuceInitialiser_GUI gui;
  try {
    Fixtures fixtures(isolatedRoot);
    testSectionAutomationCodec(fixtures);
    testArrangementCues(fixtures);
    testMusicalWaveform(fixtures);
    runRoundtripAndFailureChecks(fixtures);
    testAutomaticLegacyStemStorage(fixtures);
    testColdProjectExtractionProgress(fixtures);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Song project creation: all integration checks passed\n";
}
