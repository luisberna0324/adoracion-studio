#include "analysis/SectionDetection.h"
#include "audio/DynamicGuideTiming.h"
#include "data/GuideVocabulary.h"
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace {
using namespace playworship::analysis;
namespace vocabulary = playworship::guide;
int checks = 0;
void expect(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

void nativeAndDetectedSectionsAgree() {
  const juce::StringArray expected{
      "Intro", "Verse", "Pre-Chorus", "Chorus", "Refrain", "Post-Chorus",
      "Bridge", "Instrumental", "Solo", "Interlude", "Tag", "Vamp", "Outro",
      "Ending", "Turnaround", "Exhortation", "Rap"};
  expect(kSongSectionSuggestions.size() == 17 && vocabulary::sectionDefinitions().size() == 17,
         "the native editor and detection expose all seventeen musical families");
  std::set<int> families;
  std::set<std::string> keys;
  size_t index = 0;
  for (const auto &entry : vocabulary::sectionDefinitions()) {
    expect(juce::String(entry.key) == expected[int(index)] && juce::String(kSongSectionSuggestions[index]) == entry.key,
           "shared section choices preserve the original order and append the three recorded families");
    expect(families.insert(int(entry.family)).second && keys.insert(entry.key).second,
           "each musical family and canonical recording key is unique");
    const auto canonical = normalizeGuideCue(.37, entry.key, .62);
    expect(canonical && canonical->normalizedType == entry.family && canonical->cueTimeSeconds == .37 &&
               canonical->rawText == entry.key && canonical->asrConfidence == .62,
           "every native menu label is recognized without modifying its source evidence");
    expect(canonicalArrangementSectionName(entry.key) == entry.key &&
               standardArrangementPartColorForName(entry.key) == entry.colour,
           "native names and section colors come from the same family definition");
    for (const auto *alias : entry.aliases) {
      const auto cue = normalizeGuideCue(.37, alias);
      expect(cue && cue->normalizedType == entry.family &&
                 canonicalArrangementSectionName(alias) == entry.key,
             "spoken bilingual aliases and native section styling agree on musical identity");
    }
    expect(playworship::audio::dynamicGuideAnnouncementTiming(entry.key, entry.key) ==
               playworship::audio::DynamicGuideAnnouncementTiming::oneBarBeforeSection,
           "new musical families retain musical lead-in rather than immediate cue timing");
    ++index;
  }
  expect(standardArrangementPartColorForName("Verse") == "ff8A7A68" &&
             standardArrangementPartColorForName("Refrain") == "ff6D91A3" &&
             standardArrangementPartColorForName("Interlude") == "ff6E9A91",
         "existing native section colors remain stable");
  expect(canonicalArrangementSectionName("Break") == "Breakdown" &&
             standardArrangementPartColorForName("Breakdown") == "ff8A6B6B" &&
             !normalizeGuideCue(.37, "Breakdown") && normalizeGuideUtilityCue(.37, "Breakdown"),
         "legacy Breakdown styling remains available while new instructions stay independent cues");
}

void aliasesAndOrdinals() {
  struct Example { const char *text; SectionType family; int ordinal; };
  const Example examples[]{
      {"Turnaround two", SectionType::turnaround, 2},
      {"Next section, turn around 3rd", SectionType::turnaround, 3},
      {"Exhortation first", SectionType::exhortation, 1},
      {"Exhortación dos", SectionType::exhortation, 2},
      {"Rap fifth", SectionType::rap, 5},
      {"Repetir dos", SectionType::tag, 2},
      {"Pre 3", SectionType::preChorus, 3},
      {"Pre chorus four", SectionType::preChorus, 4},
      {"Pre-coro número 2", SectionType::preChorus, 2},
      {"Coros 2.", SectionType::chorus, 2},
      {"Salida final", SectionType::outro, 0}};
  for (const auto &example : examples) {
    const auto cue = normalizeGuideCue(2.5, juce::String::fromUTF8(example.text));
    expect(cue && cue->normalizedType == example.family && cue->spokenOrdinal.value_or(0) == example.ordinal,
           "new section families and native short aliases retain bilingual ordinal grammar");
  }
  expect(canonicalArrangementSectionName("Repetir 2") == "Tag" &&
             canonicalArrangementSectionName(juce::String::fromUTF8("Exhortación 3")) == "Exhortation",
         "native variant styling recognizes new Spanish labels without changing the recording key");
  for (const auto *unknown : {"For a frame", "Prefrain", "one more chorus", "irreconocible"})
    expect(!normalizeGuideCue(.37, unknown) && !normalizeGuideUtilityCue(.37, unknown) &&
               !vocabulary::findSectionDefinition(unknown),
           "the shared ordinary vocabulary does not silently learn fixture-specific ASR mistakes");
  expect(!normalizeGuideCue(.37, "Rap first second") && !normalizeGuideCue(.37, "Turnaround bass music"),
         "sharing labels does not turn ambiguous trailing speech into a confident musical section");
}

void confirmedOrdinaryUtilitySpellings() {
  struct Example { const char *text, *key; };
  const Example examples[]{
      {"Adlib", "Ad Lib"}, {"Teclados.", "Keys"}, {"Pulsos", "Hits"},
      {"Subetono.", "Key Change Up"}, {"Última vez.", "Last Time"},
      {"ÚLTIMA VEZ", "Last Time"}, {"U\u0301ltima vez", "Last Time"},
      {"BATERÍA", "Drums"}, {"Bateri\u0301a", "Drums"}};
  for (const auto &example : examples) {
    const auto raw = juce::String::fromUTF8(example.text);
    const auto cue = normalizeGuideUtilityCue(3.25, raw, .6);
    expect(cue && cue->name == example.key && cue->rawText == raw &&
               cue->cueTimeSeconds == 3.25 && cue->asrConfidence == .6 &&
               !normalizeGuideCue(3.25, raw),
           "validated spellings and composed/decomposed accents retain instruction identity and raw evidence");
  }
  expect(!normalizeGuideUtilityCue(0, "base") && !normalizeGuideUtilityCue(0, "swin") &&
             !normalizeGuideCountCue(0, "thor"),
         "acoustic repairs stay outside ordinary labels and decoder scoring");
}

void utilitiesAndCountsKeepTheirRoles() {
  const juce::StringArray visible{
      "Build", "Slowly Build", "Breakdown", "Hold", "Softly", "Break", "All In",
      "Drums", "Drums In", "Bass", "Guitar", "Keys", "Key Change Up", "Key Change Down",
      "Big Ending", "Last Time", "A Capella"};
  const auto &menu = vocabulary::editorUtilityDefinitions();
  expect(menu.size() == 17 && vocabulary::utilityDefinitions().size() == 26,
         "sharing the utility vocabulary preserves all seventeen visible native choices");
  for (size_t i = 0; i < menu.size(); ++i)
    expect(juce::String(menu[i].key) == visible[int(i)], "native utility menu order stays unchanged");
  size_t aliasCount = 0;
  for (const auto &entry : vocabulary::utilityDefinitions()) {
    for (const auto *alias : entry.aliases) {
      ++aliasCount;
      const auto cue = normalizeGuideUtilityCue(.37, alias, .62);
      expect(cue && cue->name == entry.key && cue->rawText == alias && cue->cueTimeSeconds == .37 &&
                 cue->asrConfidence == .62 && !normalizeGuideCue(.37, alias),
             "every prior utility alias retains its canonical identity and exact source evidence");
      expect(playworship::audio::isDynamicGuideUtilityCueKey(alias) &&
                 playworship::audio::dynamicGuideAnnouncementTiming(alias, alias) ==
                     playworship::audio::DynamicGuideAnnouncementTiming::atSectionEntry,
             "bilingual utility aliases stay immediate instructions in the native timing policy");
    }
  }
  expect(aliasCount == 99 && vocabulary::literalUtilityAliases().size() == 25,
         "the shared catalogue retains every existing canonical and literal utility alias");
  for (const auto *literal : vocabulary::literalUtilityAliases()) {
    const auto cue = normalizeGuideUtilityCue(.37, literal);
    expect(cue && cue->name == literal && !normalizeGuideCue(.37, literal),
           "known instructions without equivalent clips keep their literal meaning");
  }
  const auto build = normalizeGuideUtilityCue(.37, "subir la intensidad en el coro");
  expect(build && build->name == "Build" && !normalizeGuideCue(.37, "subir la intensidad en el coro"),
         "an instruction mentioning a musical destination does not divide that section");
  expect(normalizeGuideCue(.37, "Coro con toda la banda") &&
             !normalizeGuideUtilityCue(.37, "Coro con toda la banda"),
         "an explicit musical label with an instruction suffix remains musical");
  for (const auto *number : {"1", "uno", "one", "7", "siete", "seven"}) {
    const auto cue = normalizeGuideCountCue(0, number);
    expect(cue && cue->cueTimeSeconds == 0 && !normalizeGuideCue(0, number) &&
               !normalizeGuideUtilityCue(0, number) &&
               playworship::audio::dynamicGuideAnnouncementTiming(cue->name, cue->name) ==
                   playworship::audio::DynamicGuideAnnouncementTiming::atSectionEntry,
           "individual counts remain independent immediate source-zero points");
  }
}

void persistedTypesKeepTheirNumbers() {
  const SectionType legacy[]{SectionType::unknown, SectionType::intro, SectionType::verse,
      SectionType::preChorus, SectionType::chorus, SectionType::bridge, SectionType::instrumental,
      SectionType::outro, SectionType::ending, SectionType::custom, SectionType::refrain,
      SectionType::postChorus, SectionType::solo, SectionType::interlude, SectionType::tag, SectionType::vamp};
  for (size_t i = 0; i < std::size(legacy); ++i)
    expect(int(legacy[i]) == int(i), "all sixteen original schema-one enum values remain unchanged");
  expect(int(SectionType::turnaround) == 16 && int(SectionType::exhortation) == 17 && int(SectionType::rap) == 18,
         "new section types are appended without reinterpreting any cached type");
  SectionDetectionResult result;
  result.songId = "vocabulary-roundtrip"; result.durationSeconds = 24;
  result.analysisProvider = result.structure.provider = "synthetic-vocabulary";
  result.analyzedAudioFingerprint = "synthetic-fingerprint";
  for (int i = 0; i <= int(SectionType::rap); ++i) {
    SectionSuggestion suggestion;
    suggestion.id = "family-" + juce::String(i); suggestion.type = SectionType(i);
    suggestion.displayName = sectionTypeName(suggestion.type);
    suggestion.rawStartSeconds = suggestion.startSeconds = i; suggestion.endSeconds = i + 1;
    result.suggestions.push_back(suggestion);
    GuideCue cue; cue.cueTimeSeconds = i; cue.rawText = suggestion.displayName; cue.normalizedType = suggestion.type;
    result.guideCues.push_back(cue);
  }
  auto encoded = sectionDetectionResultToVar(result);
  SectionDetectionResult decoded; juce::String error;
  expect(sectionDetectionResultFromVar(encoded, decoded, error) && decoded.suggestions.size() == 19 &&
             decoded.guideCues.size() == 19, "schema one round trips both legacy and new musical families");
  for (size_t i = 0; i < decoded.suggestions.size(); ++i)
    expect(decoded.suggestions[i].type == SectionType(i) && decoded.guideCues[i].normalizedType == SectionType(i),
           "serialized family identities are preserved across cache decoding");
  encoded["suggestions"].getArray()->getReference(0).getDynamicObject()->setProperty("type", int(SectionType::rap) + 1);
  expect(!sectionDetectionResultFromVar(encoded, decoded, error), "unknown future enum values remain rejected");
}
} // namespace

int main() {
  try {
    nativeAndDetectedSectionsAgree(); aliasesAndOrdinals(); confirmedOrdinaryUtilitySpellings(); utilitiesAndCountsKeepTheirRoles(); persistedTypesKeepTheirNumbers();
    std::cout << "Guide vocabulary tests passed (" << checks << " checks)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Guide vocabulary test failed: " << error.what() << '\n';
    return 1;
  }
}
