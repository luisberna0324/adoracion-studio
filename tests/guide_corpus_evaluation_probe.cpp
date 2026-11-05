#include "analysis/GuideSectionInterpretation.h"
#include "data/GuideVocabulary.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>

using namespace playworship::analysis;
namespace {
struct CorpusLabel {
  juce::String kind = "unknown", key;
  std::optional<int> ordinal;
};
CorpusLabel classify(const GuideTranscriptSegment &segment, bool interpreted) {
  if (interpreted) {
    const auto meaning = interpretGuideAnnouncement(segment);
    if (meaning.kind == GuideAnnouncementKind::count)
      return {"count", playworship::guide::normalizedWords(meaning.secondaryName), {}};
    if (meaning.kind == GuideAnnouncementKind::instruction)
      return {"instruction", playworship::guide::normalizedWords(meaning.secondaryName), {}};
    if (meaning.kind == GuideAnnouncementKind::section)
      if (const auto *definition = playworship::guide::findSectionDefinition(meaning.section.type))
        return {meaning.section.tentative ? "tentative-section" : "section", definition->key, meaning.section.ordinal};
    return {};
  }
  // This branch measures the standard catalogue only. Confirmed acoustic
  // repairs and alternative-based interpretations must not inflate it.
  if (const auto count = normalizeGuideCountCue(0.0, segment.text))
    return {"count", playworship::guide::normalizedWords(count->name), {}};
  if (const auto utility = normalizeGuideUtilityCue(0.0, segment.text))
    return {"instruction", playworship::guide::normalizedWords(utility->name), {}};
  if (const auto section = normalizeGuideCue(0.0, segment.text)) {
    if (const auto *definition = playworship::guide::findSectionDefinition(section->normalizedType))
      return {"section", definition->key, section->spokenOrdinal};
  }
  return {};
}
juce::var object() { return juce::var(new juce::DynamicObject()); }
void set(juce::var &value, const char *key, const juce::var &property) {
  value.getDynamicObject()->setProperty(key, property);
}
juce::var labelJson(const CorpusLabel &label) {
  auto value = object(); set(value, "kind", label.kind); set(value, "key", label.key);
  if (label.ordinal) set(value, "ordinal", *label.ordinal);
  return value;
}
bool same(const CorpusLabel &expected, const CorpusLabel &actual) {
  // A tentative proposal still needs review and is not counted as recognized.
  return expected.kind != "unknown" && expected.kind == actual.kind &&
      expected.key == actual.key && expected.ordinal == actual.ordinal;
}
struct Totals {
  int cases = 0, covered = 0, rawCorrect = 0, interpretedCorrect = 0,
      confirmedAliasCorrect = 0, tentative = 0, missing = 0, split = 0;
  juce::var json() const {
    auto v = object(); set(v,"cases",cases); set(v,"catalogCovered",covered);
    set(v,"rawCatalogCorrect",rawCorrect); set(v,"interpretedCorrect",interpretedCorrect);
    set(v,"confirmedAliasCorrect",confirmedAliasCorrect);
    set(v,"tentativeForReview",tentative); set(v,"missing",missing); set(v,"split",split);
    return v;
  }
};
bool numeric(const juce::var &value) {
  return (value.isInt() || value.isInt64() || value.isDouble()) && std::isfinite(double(value));
}
bool nonemptyString(const juce::var &value) {
  return value.isString() && value.toString().trim().isNotEmpty();
}
}

int main(int argc, char **argv) {
  juce::StringArray args;
  for (int i = 1; i < argc; ++i) args.add(juce::String::fromUTF8(argv[i]));
  const auto value = [&](const char *key) {
    const auto index = args.indexOf(key); return index >= 0 && index+1 < args.size() ? args[index+1] : juce::String{};
  };
  if (args.size()!=6 || value("--manifest").isEmpty() || value("--transcript").isEmpty() || value("--output").isEmpty()) {
    std::cerr << "Usage: --manifest corpus.json --transcript whisper.json --output evaluation.json\n"; return 1;
  }
  const juce::File manifestFile(value("--manifest")), transcriptFile(value("--transcript")), output(value("--output"));
  if (output == manifestFile || output == transcriptFile) return 1;
  const auto manifest = juce::JSON::parse(manifestFile);
  const auto transcript = juce::JSON::parse(transcriptFile);
  const auto *clips = manifest["clips"].getArray();
  const auto *segments = transcript["segments"].getArray();
  if (!clips || clips->isEmpty() || !segments || !transcript["success"].isBool() || !bool(transcript["success"])) {
    std::cerr << "A nonempty corpus and successful real transcript are required.\n"; return 1;
  }
  double previousClipEnd = 0.0;
  std::set<juce::String> clipIds;
  for (const auto &clip : *clips) {
    if (!clip.getDynamicObject() || !numeric(clip["startSeconds"]) || !numeric(clip["endSeconds"]) ||
        double(clip["startSeconds"]) < previousClipEnd ||
        double(clip["endSeconds"]) <= double(clip["startSeconds"]) ||
        !nonemptyString(clip["id"]) || !clipIds.insert(clip["id"].toString()).second ||
        !nonemptyString(clip["expectedLabel"]) || !nonemptyString(clip["language"])) {
      std::cerr << "Manifest clips require unique IDs, nonempty references/languages and finite ordered nonoverlapping ranges.\n";
      return 1;
    }
    previousClipEnd = double(clip["endSeconds"]);
  }
  std::vector<GuideTranscriptSegment> spoken;
  std::vector<std::vector<size_t>> assigned(static_cast<size_t>(clips->size()));
  juce::Array<juce::var> extras;
  for (const auto &item : *segments) {
    if (!item.getDynamicObject() || !numeric(item["startSeconds"]) || !numeric(item["endSeconds"]) ||
        double(item["startSeconds"]) < 0.0 || !nonemptyString(item["text"]) ||
        !nonemptyString(item["language"])) return 1;
    GuideTranscriptSegment segment;
    segment.startSeconds = double(item["startSeconds"]); segment.endSeconds = double(item["endSeconds"]);
    segment.text = item["text"].toString(); segment.language = item["language"].toString();
    if (!std::isfinite(segment.startSeconds) || !std::isfinite(segment.endSeconds) || segment.endSeconds<=segment.startSeconds) return 1;
    if (item.hasProperty("confidence")) {
      if (!numeric(item["confidence"]) || double(item["confidence"]) < 0.0 || double(item["confidence"]) > 1.0) return 1;
      segment.confidence = double(item["confidence"]);
    }
    if (const auto *alternatives = item["alternatives"].getArray()) {
      if (alternatives->size() > 2) return 1;
      for (const auto &alt : *alternatives) {
        if (!alt.getDynamicObject() || !nonemptyString(alt["text"]) || !nonemptyString(alt["language"]) ||
            !numeric(alt["avgLogProbability"]) || double(alt["avgLogProbability"]) > 0.0 ||
            !numeric(alt["noSpeechProbability"]) || double(alt["noSpeechProbability"]) < 0.0 ||
            double(alt["noSpeechProbability"]) > 1.0) return 1;
        segment.alternatives.push_back({alt["text"].toString(),alt["language"].toString(),double(alt["avgLogProbability"]),double(alt["noSpeechProbability"])});
      }
    } else if (item.hasProperty("alternatives")) return 1;
    int best = -1; double overlap = 0.0;
    juce::Array<juce::var> overlappingClipIds;
    constexpr double minimumOverlapSeconds = 0.03;
    for (int i=0;i<clips->size();++i) {
      const auto &clip = clips->getReference(i);
      const double amount = std::min(segment.endSeconds,double(clip["endSeconds"])) -
                            std::max(segment.startSeconds,double(clip["startSeconds"]));
      if (amount >= minimumOverlapSeconds) overlappingClipIds.add(clip["id"]);
      if (amount > overlap) { overlap = amount; best = i; }
    }
    const bool crossClip = overlappingClipIds.size() > 1;
    const bool sufficientOverlap = overlap >= minimumOverlapSeconds &&
        overlap >= (segment.endSeconds - segment.startSeconds) * 0.5;
    if (best >= 0 && sufficientOverlap && !crossClip)
      assigned[static_cast<size_t>(best)].push_back(spoken.size());
    else {
      auto extra = object(); set(extra,"segment",item);
      set(extra,"reason", crossClip ? "cross-clip" : best < 0 ? "outside-reference-clips" : "insufficient-overlap");
      set(extra,"winningOverlapSeconds",overlap); set(extra,"overlappingClipIds",overlappingClipIds);
      extras.add(std::move(extra));
    }
    spoken.push_back(std::move(segment));
  }
  Totals total; std::map<juce::String,Totals> byLanguage;
  juce::Array<juce::var> rows;
  for (int i=0;i<clips->size();++i) {
    const auto &clip = clips->getReference(i);
    const auto &indices = assigned[static_cast<size_t>(i)];
    GuideTranscriptSegment reference; reference.startSeconds=0; reference.endSeconds=1;
    reference.text = clip["expectedLabel"].toString();
    const auto expected = classify(reference,false);
    auto row = object(); set(row,"id",clip["id"]); set(row,"sourceZipPath",clip["sourceZipPath"]);
    set(row,"language",clip["language"]); set(row,"expectedLabel",reference.text);
    set(row,"startSeconds",clip["startSeconds"]); set(row,"endSeconds",clip["endSeconds"]);
    set(row,"expected",labelJson(expected));
    juce::Array<juce::var> actual;
    for (const auto index : indices) {
      const auto &segment = spoken[index]; auto a = object();
      set(a,"text",segment.text); set(a,"language",segment.language);
      set(a,"startSeconds",segment.startSeconds); set(a,"endSeconds",segment.endSeconds);
      set(a,"rawCatalog",labelJson(classify(segment,false)));
      set(a,"interpreted",labelJson(classify(segment,true)));
      const auto meaning = interpretGuideAnnouncement(segment);
      set(a,"interpretationProvenance",meaning.provenance); set(a,"interpretationReason",meaning.reason);
      actual.add(a);
    }
    const bool single = indices.size()==1;
    const bool rawCorrect = single && same(expected,classify(spoken[indices.front()],false));
    const auto interpreted = single ? classify(spoken[indices.front()],true) : CorpusLabel{};
    const bool interpretedCorrect = single && same(expected,interpreted);
    const auto provenance = single ? interpretGuideAnnouncement(spoken[indices.front()]).provenance : juce::String{};
    const bool confirmedAliasCorrect = interpretedCorrect &&
        (provenance.contains("confirmed-alias") || provenance.contains("confirmed-corpus-alias"));
    set(row,"rawCatalogCorrect",rawCorrect); set(row,"interpretedCorrect",interpretedCorrect);
    set(row,"confirmedAliasCorrect",confirmedAliasCorrect);
    set(row,"actual",actual);
    set(row,"status",expected.kind=="unknown" ? "reference-not-in-catalog" : indices.empty() ? "missing" :
        !single ? "split" : interpretedCorrect ? "recognized" : interpreted.kind=="tentative-section" ? "review" : "mismatch");
    rows.add(row);
    const auto add = [&](Totals &t) {
      ++t.cases; if(expected.kind!="unknown") ++t.covered;
      if(rawCorrect) ++t.rawCorrect; if(interpretedCorrect) ++t.interpretedCorrect;
      if(confirmedAliasCorrect) ++t.confirmedAliasCorrect;
      if(interpreted.kind=="tentative-section") ++t.tentative;
      if(indices.empty()) ++t.missing; if(indices.size()>1) ++t.split;
    };
    add(total); add(byLanguage[clip["language"].toString()]);
  }
  auto report=object(), languages=object();
  for (const auto &[language,counts] : byLanguage) languages.getDynamicObject()->setProperty(language,counts.json());
  set(report,"scope","Bundled isolated guide clips with silence; not mixed-song accuracy. Expected filenames are used only after ASR.");
  set(report,"interpretationVersion",sectionDetectionAlgorithmVersion);
  set(report,"metricNotice","rawCatalogCorrect classifies original ASR text with the current standard vocabulary; interpretedCorrect also includes confirmed aliases and supported alternatives. This reviewed corpus is a regression set, not an independent accuracy estimate.");
  set(report,"manifest",manifestFile.getFullPathName()); set(report,"transcript",transcriptFile.getFullPathName());
  set(report,"providerVersion",transcript["providerVersion"]); set(report,"modelFingerprint",transcript["modelFingerprint"]);
  auto summary = total.json(); set(summary,"unexpectedSegments",extras.size());
  set(report,"summary",summary); set(report,"byLanguage",languages);
  auto matchingPolicy = object();
  set(matchingPolicy,"minimumOverlapSeconds",0.03); set(matchingPolicy,"minimumSegmentOverlapRatio",0.5);
  set(matchingPolicy,"rejectSegmentsSpanningMultipleClips",true);
  set(report,"matchingPolicy",matchingPolicy);
  set(report,"unexpectedSegments",extras); set(report,"cases",rows);
  if (!output.getParentDirectory().createDirectory()) return 1;
  juce::TemporaryFile temporary(output);
  if (!temporary.getFile().replaceWithText(juce::JSON::toString(report)) || !temporary.overwriteTargetFileWithTemporary()) return 1;
  std::cout << "cases="<<total.cases<<" catalog="<<total.covered<<" raw="<<total.rawCorrect
            <<" interpreted="<<total.interpretedCorrect<<" missing="<<total.missing
            <<" split="<<total.split<<" unexpected="<<extras.size()<<"\n";
  return 0;
}
