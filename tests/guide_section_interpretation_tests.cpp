#include "analysis/GuideSectionInterpretation.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace playworship::analysis;
int checks = 0;
void expect(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}

GuideTranscriptSegment segment(const juce::String &text, double start = 12.0) {
  GuideTranscriptSegment result;
  result.text = text; result.startSeconds = start; result.endSeconds = start + 0.6;
  result.language = "en"; result.confidence = std::exp(-0.5);
  return result;
}

GuideTranscript transcript(const std::vector<GuideTranscriptSegment> &segments) {
  GuideTranscript result;
  result.provider = "synthetic"; result.providerVersion = "raw-1";
  result.audioFingerprint = "audio"; result.modelFingerprint = "model";
  result.durationSeconds = 120.0; result.language = "es-en"; result.segments = segments;
  return result;
}

SectionSuggestion suggestion(const GuideTranscript &guide, size_t index) {
  const auto &source = guide.segments[index];
  SectionSuggestion result;
  result.id = guideSectionSuggestionId(guide, source);
  result.rawStartSeconds = source.startSeconds;
  result.matchedCueTimeSeconds = source.startSeconds;
  result.startSeconds = source.startSeconds + 2.0; result.endSeconds = result.startSeconds + 8.0;
  return result;
}

struct TemporaryStore {
  juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getNonexistentChildFile("playworship-guide-interpretation-tests", "", false);
  juce::File file = directory.getChildFile("local-guide-corrections.json");
  TemporaryStore() { expect(directory.createDirectory().wasOk(), "create isolated local vocabulary directory"); }
  ~TemporaryStore() { directory.deleteRecursively(); }
};

void catalogAndConfirmedAliases() {
  auto raw = segment("For a frame!");
  raw.alternatives = {{"For a frame!", "en", -0.5, 0.03}, {"otra frase", "es", -0.6, 0.04}};
  const auto original = raw;
  const auto guide = transcript({raw});
  const auto id = guideSectionSuggestionId(guide, raw);
  auto interpretation = interpretGuideSection(raw);
  expect(interpretation.type == SectionType::refrain && !interpretation.ordinal &&
             !interpretation.tentative && interpretation.provenance == "confirmed-alias",
         "the explicitly confirmed complete phrase maps to Refrain without forcing an ordinal");
  for (const auto *text : {"FOR A FRAME", "  For, a frame.  ", "Foraframe!"})
    expect(interpretGuideSection(segment(text)).type == SectionType::refrain,
           "confirmed aliases match normalized case and punctuation");
  for (const auto *text : {"for a frame video", "before foraframe", "the for a frame", "this is unknown speech"})
    expect(!interpretGuideSection(segment(text)).recognized(),
           "confirmed aliases never match a substring in a longer unknown phrase");
  interpretation = interpretGuideSection(segment("Refrain three"));
  expect(interpretation.type == SectionType::refrain && interpretation.ordinal == 3 &&
             interpretation.provenance == "catalog", "ordinary catalogue parsing retains spoken ordinals");
  interpretation = interpretGuideSection(segment("Chorus two"), {{"Chorus two", SectionType::bridge}});
  expect(interpretation.type == SectionType::chorus && interpretation.ordinal == 2,
         "learned mistakes cannot override the standard musical catalogue");
  expect(raw.text == original.text && raw.startSeconds == original.startSeconds &&
             raw.endSeconds == original.endSeconds && raw.language == original.language &&
             raw.confidence == original.confidence && raw.alternatives.size() == original.alternatives.size(),
         "interpretation preserves all selected ASR evidence");
  for (size_t i = 0; i < raw.alternatives.size(); ++i)
    expect(raw.alternatives[i].text == original.alternatives[i].text &&
               raw.alternatives[i].language == original.alternatives[i].language &&
               raw.alternatives[i].avgLogProbability == original.alternatives[i].avgLogProbability &&
               raw.alternatives[i].noSpeechProbability == original.alternatives[i].noSpeechProbability,
           "interpretation never rewrites bilingual alternative evidence");
  expect(guideSectionSuggestionId(guide, raw) == id,
         "interpretation does not change source-based suggestion identity");
}

void conservativeAlternativesAndCandidates() {
  auto raw = segment("uncertain speech");
  raw.alternatives = {{raw.text, "en", -0.5, 0.03}, {"Refrain two", "es", -0.55, 0.04}};
  auto interpretation = interpretGuideSection(raw);
  expect(interpretation.type == SectionType::refrain && interpretation.ordinal == 2 &&
             interpretation.provenance == "alternative-catalog" && raw.text == "uncertain speech",
         "a nearby supported alternative can interpret the section while preserving selected text");
  raw.alternatives = {{"Verse", "en", -0.5, 0.03}, {"Chorus", "es", -0.5, 0.04}};
  interpretation = interpretGuideSection(raw);
  expect(!interpretation.recognized() && interpretation.provenance == "ambiguous",
         "conflicting supported alternatives are not forced into a section");
  raw.alternatives = {{"Refrain one", "en", -0.5, 0.03}, {"Refrain two", "es", -0.5, 0.04}};
  expect(!interpretGuideSection(raw).recognized(), "conflicting alternative ordinals require review");
  raw.confidence = std::exp(-0.1);
  raw.alternatives = {{raw.text, "en", -0.1, 0.03}, {"Chorus", "es", -0.8, 0.02}};
  expect(!interpretGuideSection(raw).recognized(), "a weaker familiar alternative cannot override strong unknown speech");
  raw.confidence.reset();
  for (const auto candidate : std::vector<GuideTranscriptAlternative>{
           {"Verse", "en", -2.0, 0.02}, {"Verse", "en", -0.3, 0.8},
           {"Verse", "en", std::numeric_limits<double>::quiet_NaN(), 0.02},
           {"Verse", "fr", -0.3, 0.02}}) {
    raw.alternatives = {candidate};
    expect(!interpretGuideSection(raw).recognized(), "poor or invalid alternative evidence cannot manufacture a section");
  }
  interpretation = interpretGuideSection(segment("Prefrain"));
  expect(interpretation.type == SectionType::refrain && interpretation.tentative &&
             interpretation.provenance == "catalog-near-match" && !interpretation.ordinal,
         "one unambiguous short-word edit proposes a reviewable Refrain candidate");
  expect(!interpretGuideSection(segment("soro")).recognized(),
         "a one-edit tie between Coro and Solo stays unknown");
  for (const auto *text : {"a prefrain video", "refrainvideo", "very unfamiliar speech", "one two three", "baja intensidad"})
    expect(!interpretGuideSection(segment(text)).recognized(),
           "distant phrases, counts and utilities never become fuzzy musical sections");
  auto invalid = segment("For a frame"); invalid.confidence = 1.1;
  expect(!interpretGuideSection(invalid).recognized(), "invalid confidence cannot bypass evidence validation through an alias");
}

void compoundInstructionsStayReviewable() {
  for (const auto *text : {"Interludio, toda la banda.", "Interlude; All In", "Build, Chorus", "Build, All In"}) {
    auto raw = segment(juce::String::fromUTF8(text));
    raw.alternatives = {{"Interlude", "en", -.4, .01}};
    const auto meaning = interpretGuideAnnouncement(raw, {{raw.text, SectionType::interlude}});
    expect(!meaning.recognized() && meaning.provenance == "compound-announcements" &&
               raw.text == juce::String::fromUTF8(text),
           "an unsplit multi-announcement remains visible as a whole review item, never swallowing an instruction");
    const auto guide = transcript({raw});
    const auto item = suggestion(guide, 0);
    expect(guideSectionCorrectionsForConfirmedRenames(guide, {item}, {{item.id, "Interludio"}}).empty(),
           "renaming a compound review cannot teach its whole phrase as one musical family");
  }
  const auto ordinary = interpretGuideSection(segment("Coro con toda la banda"));
  expect(ordinary.type == SectionType::chorus && ordinary.provenance == "catalog" &&
             interpretGuideSection(segment("Verso, uno")).ordinal == 1,
         "an explicit section attribute or a spoken ordinal remains a single announcement");
}

void confirmedBilingualCorpusCorrections() {
  // User-reviewed cases from guide-feedback-8e0e3fe399be.json. Keep the raw
  // decoder output as input: these checks exercise interpretation, not ASR.
  struct Case {
    const char *raw;
    GuideAnnouncementKind kind;
    SectionType section = SectionType::unknown;
    int ordinal = 0;
    const char *secondary = "";
  };
  const Case cases[]{
      {"Adlib", GuideAnnouncementKind::instruction, SectionType::unknown, 0, "Ad Lib"},
      {"Base.", GuideAnnouncementKind::instruction, SectionType::unknown, 0, "Bass"},
      {"Balsa.", GuideAnnouncementKind::instruction, SectionType::unknown, 0, "Break"},
      {"Entra a batería.", GuideAnnouncementKind::instruction, SectionType::unknown, 0, "Drums In"},
      {"Course 2.", GuideAnnouncementKind::section, SectionType::chorus, 2},
      {"Thor", GuideAnnouncementKind::count, SectionType::unknown, 0, "4"},
      {"Exotación", GuideAnnouncementKind::section, SectionType::exhortation},
      {"Pulsos", GuideAnnouncementKind::instruction, SectionType::unknown, 0, "Hits"},
      {"Enterlude.", GuideAnnouncementKind::section, SectionType::interlude},
      {"Subetono.", GuideAnnouncementKind::instruction, SectionType::unknown, 0, "Key Change Up"},
      {"Teclados.", GuideAnnouncementKind::instruction, SectionType::unknown, 0, "Keys"},
      {"Última vez.", GuideAnnouncementKind::instruction, SectionType::unknown, 0, "Last Time"},
      {"Wrap!", GuideAnnouncementKind::section, SectionType::rap},
      {"¡Prab!", GuideAnnouncementKind::section, SectionType::rap},
      {"Swin.", GuideAnnouncementKind::instruction, SectionType::unknown, 0, "Swell"}};
  for (const auto &item : cases) {
    auto raw = segment(juce::String::fromUTF8(item.raw));
    const auto original = raw;
    const auto guide = transcript({raw});
    const auto id = guideSectionSuggestionId(guide, raw);
    const auto meaning = interpretGuideAnnouncement(raw);
    expect(meaning.recognized() && meaning.kind == item.kind &&
               meaning.section.type == item.section && meaning.section.ordinal.value_or(0) == item.ordinal &&
               meaning.secondaryName == item.secondary && !meaning.section.tentative,
           "each confirmed corpus correction resolves to its reviewed family and ordinal or secondary key");
    expect(raw.text == original.text && raw.startSeconds == original.startSeconds &&
               raw.endSeconds == original.endSeconds && raw.confidence == original.confidence &&
               guideSectionSuggestionId(guide, raw) == id,
           "corpus corrections preserve original source evidence and suggestion identity");
    if (item.kind != GuideAnnouncementKind::section)
      expect(!interpretGuideSection(raw, {{raw.text, SectionType::verse}}).recognized(),
             "a corrected secondary cue cannot become a section through fuzzy or learned matching");
  }

  // The remaining reviewed case is a hallucinated selected English list. Its
  // real Spanish alternative says the plural 'Coros 2', with a supported score.
  // Recover the ordinal from that evidence, never from the expected fixture.
  auto list = segment("chorus, chorus, chorus, refrain, post-chorus.");
  list.endSeconds = list.startSeconds + 0.78;
  list.confidence = std::exp(-0.92590);
  list.alternatives = {{list.text, "en", -0.92590, 0.000001},
                       {"Coros 2.", "es", -1.06894, 0.000000268}};
  auto meaning = interpretGuideAnnouncement(list);
  expect(meaning.kind == GuideAnnouncementKind::section && meaning.section.type == SectionType::chorus &&
             meaning.section.ordinal == 2 && meaning.provenance == "alternative-catalog" &&
             !meaning.section.tentative && list.text == "chorus, chorus, chorus, refrain, post-chorus.",
         "the reviewed hallucinated list resolves from real supported Spanish evidence, preserving raw ASR");
  list.alternatives.clear();
  expect(!interpretGuideAnnouncement(list).recognized(),
         "a hallucinated list alone cannot manufacture a Chorus 2 correction or ordinal");
  list.alternatives = {{"Coros 2.", "es", -1.15, 0.000000268}};
  expect(!interpretGuideAnnouncement(list).recognized(),
         "a familiar alternative outside the existing score margin remains unrecognized");

  for (const auto *text : {"bass at home", "thor movie", "base de datos", "swin in the lake",
                          "wrap the gift", "prab other phrase", "course 2 of the lesson", "una balsa en el rio",
                          "enterlude en el video", "exotacion de ejemplo"})
    expect(!interpretGuideAnnouncement(segment(text)).recognized(),
           "confirmed acoustic corrections require complete announcements, not substrings in narration");
  for (const auto *text : {"Base", "Balsa", "Entra a bateria", "Thor", "Swin"}) {
    auto raw = segment(text);
    raw.alternatives = {{"Verse", "en", -0.4, 0.01}, {"Coro", "es", -0.4, 0.01}};
    const auto meaning = interpretGuideAnnouncement(raw);
    expect(meaning.recognized() && meaning.kind != GuideAnnouncementKind::section &&
               meaning.provenance == "confirmed-corpus-alias" && !interpretGuideSection(raw).recognized(),
           "confirmed count and instruction aliases take precedence over plausible musical alternatives");
  }
  for (const auto *text : {"Base", "Thor", "Swin", "Course 2"}) {
    auto raw = segment(text); raw.endSeconds = raw.startSeconds;
    expect(!interpretGuideAnnouncement(raw).recognized(),
           "zero-duration evidence cannot bypass validation through a confirmed correction");
    raw = segment(text); raw.confidence = std::numeric_limits<double>::quiet_NaN();
    expect(!interpretGuideAnnouncement(raw).recognized(),
           "invalid confidence cannot bypass typed interpretation validation");
  }
}

void localCorrectionsAndOrdinalIndependence() {
  TemporaryStore store;
  std::vector<GuideLearnedMapping> mappings;
  juce::String error;
  expect(loadGuideSectionLearnedMappings(store.file, mappings, error) && mappings.empty() && !store.file.exists(),
         "loading a missing vocabulary performs no implicit write");
  const auto guide = transcript({segment("odd guide phrase", 12), segment("odd guide phrase", 28), segment("odd guide phrase", 44)});
  std::vector<SectionSuggestion> suggestions;
  std::vector<GuideConfirmedSectionRename> renames;
  for (size_t i = 0; i < guide.segments.size(); ++i) {
    auto item = suggestion(guide, i);
    item.type = SectionType::refrain; item.displayName = "Refrain " + juce::String(int(i) + 1);
    item.userEdited = true;
    renames.push_back({item.id, item.displayName}); suggestions.push_back(item);
  }
  const auto corrections = guideSectionCorrectionsForConfirmedRenames(guide, suggestions, renames);
  expect(corrections.size() == 1 && corrections.front().rawText == "odd guide phrase" &&
             corrections.front().type == SectionType::refrain,
         "three independently renamed occurrences teach one family rather than Refrain 1");
  expect(!store.file.exists(), "extracting corrections and cancelling a review never creates a local vocabulary");
  expect(mergeAndSaveGuideSectionLearnedMappings(store.file, corrections, error),
         "an explicit post-confirmation operation atomically stores the local correction");
  expect(loadGuideSectionLearnedMappings(store.file, mappings, error) && mappings.size() == 1,
         "local corrections reload independently of the current review");
  for (const auto *text : {"odd guide phrase", "ODD, GUIDE PHRASE!"}) {
    const auto interpreted = interpretGuideSection(segment(text), mappings);
    expect(interpreted.type == SectionType::refrain && !interpreted.ordinal && !interpreted.tentative &&
               interpreted.provenance == "learned-correction",
           "a reloaded exact phrase supplies the family without reusing an earlier occurrence number");
  }
  expect(!interpretGuideSection(segment("extra odd guide phrase"), mappings).recognized(),
         "a learned phrase is bounded and cannot match an unrelated substring");
  const auto bytes = store.file.loadFileAsString();
  interpretGuideSection(guide.segments.front(), mappings);
  loadGuideSectionLearnedMappings(store.file, mappings, error);
  expect(store.file.loadFileAsString() == bytes, "preview and reload leave the persisted file byte-for-byte unchanged");
  const auto json = juce::JSON::parse(bytes);
  const auto &entry = json["mappings"][0];
  expect(entry["family"].toString() == "Refrain" && !entry.getDynamicObject()->hasProperty("ordinal"),
         "the versioned store persists a canonical shared family key and no ordinal");
  expect(mergeAndSaveGuideSectionLearnedMappings(store.file, {{"Odd guide phrase!", SectionType::bridge}}, error) &&
             loadGuideSectionLearnedMappings(store.file, mappings, error) && mappings.size() == 1 &&
             interpretGuideSection(segment("odd guide phrase"), mappings).type == SectionType::bridge,
         "a later explicit correction replaces only the same normalized phrase");
  expect(!interpretGuideSection(segment("unclear phrase"),
             {{"unclear phrase", SectionType::chorus}, {"UNCLEAR PHRASE!", SectionType::bridge}}).recognized(),
         "contradictory in-memory learned mappings remain ambiguous");
}

void confirmationExtractionGuards() {
  const auto guide = transcript({segment("unfamiliar call")});
  auto item = suggestion(guide, 0);
  const std::vector<GuideConfirmedSectionRename> rename{{item.id, "Refrain 3"}};
  expect(guideSectionCorrectionsForConfirmedRenames(guide, {item}, rename).size() == 1,
         "a confirmed ID links a renamed section to one original announcement");
  item.sources.add(juce::String::fromUTF8(mergedGuideAnnouncementsSource));
  expect(guideSectionCorrectionsForConfirmedRenames(guide, {item}, rename).empty(),
         "renaming a merged block never teaches all of its ambiguous announcements");
  item.sources.clear(); item.matchedCueTimeSeconds = 99;
  expect(guideSectionCorrectionsForConfirmedRenames(guide, {item}, rename).empty(),
         "a timestamp mismatch cannot teach an unrelated source phrase");
  item = suggestion(guide, 0);
  expect(guideSectionCorrectionsForConfirmedRenames(guide, {item}, {{"guide-secondary-no-match", "Refrain"}}).empty() &&
             guideSectionCorrectionsForConfirmedRenames(guide, {item}, {{item.id, "My custom section"}}).empty(),
         "secondary IDs and free custom names do not teach musical families");
  expect(guideSectionCorrectionsForConfirmedRenames(guide, {item},
             {{item.id, "Refrain 1"}, {item.id, "Coro 2"}}).empty(),
         "conflicting explicit renames of one phrase are not learned");
  auto changed = guide; changed.segments.front().text = "different raw evidence";
  expect(guideSectionCorrectionsForConfirmedRenames(changed, {item}, rename).empty(),
         "source identity changes prevent learning from a stale review");

  const auto compoundGuide = transcript({segment("Intro, dos, tres, cuatro")});
  auto compound = suggestion(compoundGuide, 0);
  compound.sources.add("word-interval-counts");
  expect(guideSectionCorrectionsForConfirmedRenames(compoundGuide, {compound},
             {{compound.id, "Intro"}}).empty() &&
             !interpretGuideSection(compoundGuide.segments.front(),
                 {{compoundGuide.segments.front().text, SectionType::intro}}).recognized(),
         "a recovered label never teaches its whole compound as a section that could swallow the counts later");

  const auto tentativeGuide = transcript({segment("Prefrain")});
  auto tentative = suggestion(tentativeGuide, 0);
  tentative.type = SectionType::refrain; tentative.displayName = "Posible Refrain";
  tentative.sources.add(juce::String::fromUTF8(tentativeGuideInterpretationSource));
  expect(guideSectionCorrectionsForConfirmedReview(tentativeGuide, {tentative}, {}).size() == 1,
         "explicit acceptance of a tentative generated name can teach its family");
  tentative.startSeconds += 0.5; tentative.userEdited = true;
  expect(guideSectionCorrectionsForConfirmedReview(tentativeGuide, {tentative}, {}).size() == 1,
         "moving only the proposed boundary preserves acceptance of its tentative family");
  expect(guideSectionCorrectionsForConfirmedReview(tentativeGuide, {tentative},
             {{tentative.id, "Personal custom name"}}).empty(),
         "an explicit free rename prevents implicit tentative learning");
  tentative.status = SuggestionStatus::rejected;
  expect(guideSectionCorrectionsForConfirmedReview(tentativeGuide, {tentative}, {}).empty(),
         "rejected tentative proposals are never learned");
  tentative.status = SuggestionStatus::suggested; tentative.sources.clear();
  expect(guideSectionCorrectionsForConfirmedReview(tentativeGuide, {tentative}, {}).empty(),
         "editing a neighbouring proposal cannot teach an old inferred family without its source flag");
}

void boundedVersionedStore() {
  TemporaryStore store;
  juce::String error;
  std::vector<GuideLearnedMapping> mappings;
  for (const auto &family : playworship::guide::sectionDefinitions()) {
    const auto raw = "unrecognized call " + juce::String(int(family.family));
    expect(mergeAndSaveGuideSectionLearnedMappings(store.file, {{raw, family.family}}, error),
           "every family in the shared section vocabulary can be persisted");
  }
  expect(loadGuideSectionLearnedMappings(store.file, mappings, error) && mappings.size() == 17,
         "all seventeen current section families reload without a duplicated private catalogue");
  const auto original = store.file.loadFileAsString();
  std::vector<GuideLearnedMapping> tooMany;
  for (int i = 0; i < 257; ++i) tooMany.push_back({"bounded unknown call " + juce::String(i), SectionType::bridge});
  expect(!mergeAndSaveGuideSectionLearnedMappings(store.file, tooMany, error) &&
             store.file.loadFileAsString() == original, "an oversized correction batch preserves the prior file atomically");
  for (const auto &invalid : std::vector<GuideLearnedMapping>{
           {"unknown spoken phrase", SectionType::unknown}, {"unknown spoken phrase", SectionType::custom},
           {"Coro", SectionType::refrain}, {"1", SectionType::verse},
           {"Thor", SectionType::verse}, {"Base", SectionType::chorus}, {"Swin", SectionType::bridge},
           {juce::String::repeatedString("x", 257), SectionType::bridge}})
    expect(!mergeAndSaveGuideSectionLearnedMappings(store.file, {invalid}, error) &&
               store.file.loadFileAsString() == original,
           "invalid mappings, standard-name overrides and secondary promotions preserve the existing store");
  expect(!mergeAndSaveGuideSectionLearnedMappings(store.file,
             {{"ambiguous local phrase", SectionType::verse}, {"Ambiguous local phrase!", SectionType::chorus}}, error) &&
             store.file.loadFileAsString() == original, "a contradictory batch is rejected before any file replacement");

  for (const auto *invalid : {"not-json", "{\"schemaVersion\":99,\"mappings\":[]}",
           "{\"schemaVersion\":1,\"mappings\":[{\"rawText\":\"odd label\",\"family\":\"future-family\"}]}",
           "{\"schemaVersion\":1,\"mappings\":[{\"rawText\":42,\"family\":\"Verse\"}]}",
           "{\"schemaVersion\":1,\"mappings\":[{\"rawText\":\"odd label\",\"family\":\"Verse\"},{\"rawText\":\"Odd label!\",\"family\":\"Verse\"}]}"}) {
    expect(store.file.replaceWithText(invalid), "write malformed/forward-version isolated fixture");
    expect(!loadGuideSectionLearnedMappings(store.file, mappings, error) && mappings.empty() && error.isNotEmpty(),
           "invalid or future schemas load safely without a partial vocabulary");
    expect(!mergeAndSaveGuideSectionLearnedMappings(store.file, {{"new correction", SectionType::bridge}}, error) &&
               store.file.loadFileAsString() == invalid,
           "malformed and unknown-schema files are never silently overwritten");
  }
  const auto oversized = juce::String::repeatedString("x", 256 * 1024 + 1);
  expect(store.file.replaceWithText(oversized), "write oversized isolated vocabulary fixture");
  expect(!loadGuideSectionLearnedMappings(store.file, mappings, error) &&
             !mergeAndSaveGuideSectionLearnedMappings(store.file, {{"new correction", SectionType::bridge}}, error) &&
             store.file.loadFileAsString() == oversized,
         "the file-size cap protects reads and prevents destructive replacement");
}

} // namespace

int main() {
  try {
    catalogAndConfirmedAliases();
    conservativeAlternativesAndCandidates();
    compoundInstructionsStayReviewable();
    confirmedBilingualCorpusCorrections();
    localCorrectionsAndOrdinalIndependence();
    confirmationExtractionGuards();
    boundedVersionedStore();
    std::cout << "PASS: " << checks << " guide interpretation and local-learning checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
