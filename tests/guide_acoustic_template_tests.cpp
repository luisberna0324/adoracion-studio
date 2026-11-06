#include "analysis/GuideAcousticTemplateMatcher.h"
#include "analysis/GuideCatalogueGrammar.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace playworship::analysis;
namespace {
int checks = 0;
void expect(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

GuideAcousticTemplate voice(const char *label, int seed) {
  GuideAcousticTemplate result{label, "es", "synthetic-" + juce::String(seed), {}};
  constexpr double pi = 3.14159265358979323846;
  result.samples.resize(3520);
  double phase = .31 * seed;
  for (size_t i = 0; i < result.samples.size(); ++i) {
    const double t = double(i) / 16000.0;
    const double envelope = .5 - .5 * std::cos(2 * pi * double(i) / double(result.samples.size() - 1));
    phase += 2 * pi * (180.0 + seed * 87.0 + (500 + 40 * seed) * t) / 16000.0;
    result.samples[i] = float(.3 * envelope * (std::sin(phase) + .37 * std::sin(phase * 1.91 + seed)));
  }
  return result;
}

GuideAcousticTemplateBank bank() {
  GuideAcousticTemplateBank result;
  result.fingerprint = "synthetic-unit-bank";
  result.templates = {voice("1", 1), voice("2", 2), voice("3", 3), voice("4", 4),
                      voice("intro", 8), voice("interludio", 9), voice("toda la banda", 10),
                      voice("subir intensidad", 11), voice("entra bateria", 12)};
  return result;
}

void add(std::vector<float> &source, const GuideAcousticTemplate &reference,
         double seconds, double gain = 1.0) {
  const auto offset = size_t(std::llround(seconds * 16000));
  for (size_t i = 0; i < reference.samples.size(); ++i) source.at(offset + i) += float(gain * reference.samples[i]);
}

GuideTranscriptSegment parent(const char *text, double start = .05, double end = 1.5) {
  GuideTranscriptSegment result;
  result.startSeconds = start; result.endSeconds = end; result.text = text;
  result.language = "es"; result.confidence = .9;
  return result;
}

void completeAndOverlappingCounts() {
  const auto references = bank();
  std::vector<float> source(24000);
  const double times[]{.10, .23, .36, .49};
  for (int i = 0; i < 4; ++i) add(source, references.templates[size_t(i)], times[i], .4 + .2 * i);
  auto raw = parent("uno, dos, tres, cuatro");
  const auto result = matchKnownGuideCompound(source, 0.0, raw, references);
  expect(result.announcements.size() == 4, "overlapping count recordings are recovered independently");
  expect(result.residualRatio < .001, "the complete recording mixture explains the observed source");
  for (size_t i = 0; i < result.announcements.size(); ++i) {
    expect(std::abs(result.announcements[i].startSeconds - times[i]) < 1.0 / 16000,
           "the source recording, not a presumed beat, supplies each count time");
    expect(result.announcements[i].correlation >= guideAcousticMinimumCorrelation,
           "each count has independently strong conditional recording evidence");
  }
  expect(raw.text == "uno, dos, tres, cuatro" && raw.words.empty() && raw.acousticAnnouncements.empty(),
         "matching never mutates original ASR text or word evidence");
  raw.acousticAnnouncements = result.announcements; raw.acousticResidualRatio = result.residualRatio;
  expect(guideAcousticAnnouncementsMatchParent(raw), "complete measured count evidence is semantically valid");
  raw.acousticAnnouncements[1].text = "7";
  expect(!guideAcousticAnnouncementsMatchParent(raw), "corrupted count identity cannot satisfy the literal ASR sequence");
}

void missingAsrCountUsesPhysicalRecording() {
  const auto references = bank(); std::vector<float> source(24000);
  add(source, references.templates[4], .10);
  add(source, references.templates[1], .23);
  add(source, references.templates[2], .36);
  add(source, references.templates[3], .49);
  auto raw = parent("Intro, tres, cuatro");
  const auto result = matchKnownGuideCompound(source, 0, raw, references);
  expect(result.announcements.size() == 4 && result.announcements[1].text == "2",
         "a number omitted by ASR is recovered only from its actual complete recording");
  raw.acousticAnnouncements = result.announcements; raw.acousticResidualRatio = result.residualRatio;
  expect(guideAcousticAnnouncementsMatchParent(raw), "literal ASR parts remain an ordered subsequence of physical recordings");
  raw.text = "Intro, tres, siete";
  expect(!guideAcousticAnnouncementsMatchParent(raw), "an absent spoken number cannot be manufactured by the recording matcher");
  auto incomplete = references; incomplete.templates.erase(incomplete.templates.begin() + 1);
  expect(matchKnownGuideCompound(source, 0, parent("Intro, tres, cuatro"), incomplete).announcements.empty(),
         "unexplained missing-recording energy rejects an incomplete reconstruction");
}

void isolatedOrdinalAndRepeatedInstances() {
  const auto references = bank(); std::vector<float> source(24000);
  add(source, references.templates[4], .10); add(source, references.templates[1], .34);
  const auto ordinal = matchKnownGuideCompound(source, 0, parent("Intro 2"), references);
  expect(ordinal.announcements.size() == 2 && ordinal.announcements[0].text == "intro" && ordinal.announcements[1].text == "2",
         "Intro plus one count needs two actual recordings before reinterpreting an ordinal");
  expect(!guideAcousticCompoundPhrases("Verso 2"), "numbered section families do not become count candidates");
  std::fill(source.begin(), source.end(), 0);
  add(source, references.templates[1], .10);
  expect(matchKnownGuideCompound(source, 0, parent("dos, dos"), references).announcements.empty(),
         "one physical count peak cannot be reused for two ASR instances");
  add(source, references.templates[1], .60);
  const auto repeated = matchKnownGuideCompound(source, 0, parent("dos, dos"), references);
  expect(repeated.announcements.size() == 2 && std::abs(repeated.announcements[0].startSeconds - .1) < 1e-5 &&
             std::abs(repeated.announcements[1].startSeconds - .6) < 1e-5,
         "two real occurrences retain their independent recording peaks");
}

void noPauseInstructionsAndNegativeSignals() {
  const auto references = bank(); std::vector<float> source(24000);
  add(source, references.templates[5], .10); add(source, references.templates[6], .32);
  const auto result = matchKnownGuideCompound(source, 0, parent("Interludio, toda la banda"), references);
  expect(result.announcements.size() == 2 && std::abs(result.announcements[1].startSeconds - .32) < 1e-5,
         "touching section and utility recordings retain independent measured onsets");
  expect(matchKnownGuideCompound(source, 0, parent("Coro con toda la banda"), references).announcements.empty(),
         "ordinary section attributes are not split into presumed independent announcements");
  std::fill(source.begin(), source.end(), 0);
  expect(matchKnownGuideCompound(source, 0, parent("uno dos"), references).announcements.empty(),
         "digital silence cannot produce a numerical correlation match");
  std::fill(source.begin(), source.end(), .03f);
  expect(matchKnownGuideCompound(source, 0, parent("uno dos"), references).announcements.empty(),
         "a constant nonzero signal cannot masquerade as matching voice energy");
  std::uint32_t state = 17;
  for (auto &sample : source) { state = state * 1664525u + 1013904223u; sample = float(double(state) / 4294967296.0 - .5) * .2f; }
  expect(matchKnownGuideCompound(source, 0, parent("uno dos"), references).announcements.empty(),
         "unrelated noise never becomes a known count recording");
  expect(matchKnownGuideCompound(source, 0, parent("uno dos"), references, [] { return true; }).cancelled,
         "matching is cancelable before publishing any evidence");
  source[100] = std::numeric_limits<float>::quiet_NaN();
  expect(matchKnownGuideCompound(source, 0, parent("uno dos"), references).announcements.empty(),
         "nonfinite PCM is rejected without partial evidence");
}

void omittedInstructionAndMixedRoles() {
  const auto references = bank(); std::vector<float> source(24000);
  add(source, references.templates[7], .1); add(source, references.templates[6], .4);
  add(source, references.templates[8], .7);
  auto raw = parent("Subir intensidad, entra a bateria");
  const auto complete = matchKnownGuideCompound(source, 0, raw, references);
  expect(complete.announcements.size() == 3 && complete.announcements[1].text == "toda la banda",
         "an instruction omitted by ASR needs its own physically matching recording");
  raw.acousticAnnouncements = complete.announcements; raw.acousticResidualRatio = complete.residualRatio;
  expect(guideAcousticAnnouncementsMatchParent(raw), "confirmed utility repairs remain ordered semantic anchors");
  raw.text = "Subir intensidad, o entra a bateria";
  expect(guideAcousticAnnouncementsMatchParent(raw), "an isolated ASR conjunction between exact atomic anchors is not a new announcement");
  raw.text = "Subir intensidad, luego una narracion, entra bateria";
  expect(!guideAcousticAnnouncementsMatchParent(raw), "unknown narrative content cannot authorize vocabulary discovery");
  std::fill(source.begin(), source.end(), 0);
  add(source, references.templates[4], .1); add(source, references.templates[8], .34);
  add(source, references.templates[0], .58); add(source, references.templates[1], .82);
  raw = parent("Intro, entra bateria, uno, dos");
  expect(matchKnownGuideCompound(source, 0, raw, references).announcements.size() == 4,
         "mixed section, instruction and count roles preserve four independent audio events");
}

void fractionalSamplePhases() {
  auto references = bank(); references.templates.resize(4);
  for (int phase = 1; phase < 16; ++phase) {
    std::vector<float> source(16000);
    const double shift = double(phase) / 16000.0;
    add(source, references.templates[0], .100 + shift);
    add(source, references.templates[1], .360 + shift);
    add(source, references.templates[2], .620 + shift);
    const auto result = matchKnownGuideCompound(source, 0, parent("uno dos tres", .05, .95), references);
    expect(result.announcements.size() == 3, "count mixtures retain all components at arbitrary sample phase");
    for (size_t i = 0; i < result.announcements.size(); ++i) {
      const double expected = .100 + .260 * double(i) + shift;
      if (std::abs(result.announcements[i].startSeconds - expected) > 2.0 / 16000)
        std::cerr << "phase=" << phase << " component=" << i << " expected=" << expected
                  << " actual=" << result.announcements[i].startSeconds << "\n";
      expect(std::abs(result.announcements[i].startSeconds - expected) <= 2.0 / 16000,
             "a sampling-grid phase change must not move a known recording by a pitch period");
    }
  }
}

void atomicCatalogueRecoveryAndRejections() {
  const auto references = bank();
  // Every recording is a possible result; no ASR spelling selects the class.
  for (size_t index = 0; index < references.templates.size(); ++index) {
    std::vector<float> source(16000);
    add(source, references.templates[index], .137, .4);
    auto raw = parent("unresolved spoken call", .10, .8);
    raw.confidence = .05; // Independent physical evidence is not token confidence.
    const auto matched = matchKnownGuideAtomic(source, 5.0, raw, references);
    // Absolute source timestamps must describe the same parent interval.
    expect(matched.announcements.empty(), "an atomic match cannot escape its absolute source parent interval");
    raw.startSeconds += 5.0; raw.endSeconds += 5.0;
    const auto result = matchKnownGuideAtomic(source, 5.0, raw, references);
    expect(result.announcements.size() == 1 &&
               result.announcements[0].text == references.templates[index].text &&
               std::abs(result.announcements[0].startSeconds - 5.137) <= 2.0 / 16000,
           "the complete known-voice catalogue can recover atomic counts, sections and instructions without ASR aliases");
    expect(result.residualRatio < .001 && result.announcements[0].correlation >= guideAcousticMinimumCorrelation,
           "a recovered atomic label needs the same strict independent waveform evidence");
    raw.acousticAnnouncements = result.announcements; raw.acousticResidualRatio = result.residualRatio;
    expect(guideAcousticAnnouncementsMatchParent(raw), "single-recording evidence remains usable on the unchanged raw parent");
    expect(raw.text == "unresolved spoken call" && raw.confidence == .05,
           "atomic recovery preserves the decoder's literal text and token confidence");
  }
  std::vector<float> source(16000);
  add(source, references.templates[7], .137);
  auto nearMatch = parent("Perso!", .1, .8);
  expect(guideAcousticAtomicCandidate(nearMatch),
         "a tentative lexical match can reach the physical search and catalogue retry");
  const auto recovered = matchKnownGuideAtomic(source, 0, nearMatch, references);
  expect(recovered.announcements.size() == 1 &&
             recovered.announcements.front().text == references.templates[7].text,
         "independent source evidence can recover an instruction despite a tentative primary hypothesis");
  nearMatch.acousticAnnouncements = recovered.announcements;
  nearMatch.acousticResidualRatio = recovered.residualRatio;
  expect(guideAcousticAnnouncementsMatchParent(nearMatch) && nearMatch.text == "Perso!",
         "validation retains the recovered mark and literal ASR after a tentative interpretation");
  expect(matchKnownGuideAtomic(source, 0, parent("Verse 2", .1, .8), references).announcements.empty(),
         "atomic catalogue search cannot replace an already resolved section by a different physical label");
  auto withSupportedAlternative = parent("unresolved spoken call", .1, .8);
  withSupportedAlternative.confidence = std::exp(-.5);
  withSupportedAlternative.language = "en";
  withSupportedAlternative.alternatives = {{withSupportedAlternative.text, "en", -.5, .01},
                                         {"Chorus", "es", -.55, .01}};
  expect(!guideAcousticAtomicCandidate(withSupportedAlternative),
         "a supported existing alternative constrains an atomic parent before recovery");
  withSupportedAlternative.alternatives = {{"Verse", "en", -.5, .01}, {"All In", "es", -.55, .01}};
  expect(!guideAcousticAtomicCandidate(withSupportedAlternative) &&
             matchKnownGuideAtomic(source, 0, withSupportedAlternative, references).announcements.empty(),
         "conflicting supported roles require review even when one recorded clip fits");
  expect(!guideAcousticAtomicCandidate(parent("Intro, uno, dos", .1, .8)),
         "an explicit compound cannot collapse into one atomic catalogue label");
  auto competing = references;
  competing.templates.push_back(competing.templates[7]);
  competing.templates.back().text = "Pausa";
  competing.templates.back().identity = "synthetic-competing-break";
  expect(matchKnownGuideAtomic(source, 0, parent("unresolved spoken call", .1, .8), competing).ambiguous,
         "two distinct meanings with indistinguishable physical evidence remain unresolved for catalogue review");
  std::fill(source.begin(), source.end(), 0.0f);
  expect(matchKnownGuideAtomic(source, 0, parent("unresolved spoken call", .1, .8), references).announcements.empty(),
         "silence does not become a catalogue event");
  std::fill(source.begin(), source.end(), .03f);
  expect(matchKnownGuideAtomic(source, 0, parent("unresolved spoken call", .1, .8), references).announcements.empty(),
         "DC energy cannot become an atomic catalogue event");
  std::uint32_t state = 71;
  for (auto &sample : source) { state = state * 1664525u + 1013904223u; sample = float(double(state) / 4294967296.0 - .5) * .2f; }
  expect(matchKnownGuideAtomic(source, 0, parent("a longer unknown narrative", .1, .8), references).announcements.empty(),
         "unmatched narration/noise does not get an arbitrary best catalogue label");
  expect(matchKnownGuideAtomic(source, 0, parent("unresolved spoken call", .1, .8), references,
                              [] { return true; }).cancelled,
         "full catalogue search can be cancelled before any atomic evidence is returned");
  source[200] = std::numeric_limits<float>::quiet_NaN();
  expect(matchKnownGuideAtomic(source, 0, parent("unresolved spoken call", .1, .8), references).announcements.empty(),
         "nonfinite source data cannot enter atomic catalogue recovery");
}

void grammarUsesTheEntireApplicationCatalogue() {
  const auto phrases = guideCatalogueUtterances();
  expect(!phrases.empty() && std::is_sorted(phrases.begin(), phrases.end()) &&
             std::adjacent_find(phrases.begin(), phrases.end()) == phrases.end(),
         "the closed grammar is bounded, sorted and deduplicated from the shared catalogue");
  for (const auto &entry : playworship::guide::sectionDefinitions()) {
    expect(isGuideCatalogueUtterance(entry.key) && isGuideCatalogueUtterance(entry.spanishLabel) &&
               isGuideCatalogueUtterance(entry.englishLabel), "all musical families are generated in both languages");
    for (const auto *alias : entry.aliases)
      expect(isGuideCatalogueUtterance(alias), "ordinary section aliases remain catalogue grammar alternatives");
    for (int ordinal = 1; ordinal <= entry.numberedVariantLimit; ++ordinal)
      expect(isGuideCatalogueUtterance(juce::String(entry.key) + " " + juce::String(ordinal)),
             "each managed musical ordinal is available without a hand-written song exception");
  }
  for (const auto &entry : playworship::guide::utilityDefinitions()) {
    expect(isGuideCatalogueUtterance(entry.key) && isGuideCatalogueUtterance(entry.spanishLabel) &&
               isGuideCatalogueUtterance(entry.englishLabel), "every managed secondary instruction is in the grammar");
    for (const auto *alias : entry.aliases)
      expect(isGuideCatalogueUtterance(alias), "ordinary utility aliases are generated from their catalogue entries");
  }
  for (int number = 1; number <= 7; ++number)
    expect(isGuideCatalogueUtterance(juce::String(number)), "all supported count numbers are valid grammar results");
  expect(!isGuideCatalogueUtterance("an unknown narrative") && !isGuideCatalogueUtterance("verse 99"),
         "the grammar does not add free narration or unsupported ordinal variants to the catalogue");
}
} // namespace

int main() {
  try {
    completeAndOverlappingCounts(); missingAsrCountUsesPhysicalRecording();
    isolatedOrdinalAndRepeatedInstances(); noPauseInstructionsAndNegativeSignals();
    omittedInstructionAndMixedRoles();
    fractionalSamplePhases(); atomicCatalogueRecoveryAndRejections(); grammarUsesTheEntireApplicationCatalogue();
    std::cout << "Guide acoustic templates: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << "\n"; return 1;
  }
}
