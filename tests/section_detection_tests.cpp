#include "../src/analysis/SectionDetection.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>

namespace {
using namespace playworship::analysis;
int checks = 0;
void expect(bool condition, const char *message) {
  ++checks;
  if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
bool close(double a, double b) { return std::abs(a-b) < 1.0e-7; }
SectionDetectionResult fixture() {
  SectionDetectionResult result;
  result.songId="synthetic-song";
  result.durationSeconds=24.0;
  result.analysisProvider="test-structure";
  result.analyzedAudioFingerprint="content-sha256";
  result.structure.provider=result.analysisProvider;
  result.structure.version="test-v1";
  result.structure.sections={{0,8.1},{8.1,16},{16,24}};
  result.structure.tempo.bars={0,2,4,6,8,10,12,14,16,18,20,22,24};
  result.structure.tempo.beatsPerMinute=120;
  result.structure.tempo.source="synthetic";
  return result;
}
SectionSuggestion suggestion(juce::String id, double start, double end) {
  SectionSuggestion value;
  value.id=id; value.displayName="Coro 1";
  value.startSeconds=value.rawStartSeconds=start; value.endSeconds=end;
  return value;
}
void testSpeechNormalization() {
  auto cue=normalizeGuideCue(12.125,juce::String::fromUTF8("¡Verso UNO!"),0.8);
  expect(cue && cue->normalizedType==SectionType::verse && cue->spokenOrdinal==1,
         "Spanish verse plus spoken ordinal is normalized");
  expect(cue->rawText==juce::String::fromUTF8("¡Verso UNO!") && close(cue->cueTimeSeconds,12.125) && cue->asrConfidence==0.8,
         "original words and ASR timing/confidence are retained");
  cue=normalizeGuideCue(0,"estrofa 2");
  expect(cue && cue->normalizedType==SectionType::verse && cue->spokenOrdinal==2,
         "numeric ordinal and estrofa alias");
  cue=normalizeGuideCue(0,"Segunda estrofa");
  expect(cue && cue->spokenOrdinal==2,"ordinal before section label");
  for (const auto *text : {"pre coro","PRE-CORO","precoro"})
    expect(normalizeGuideCue(0,text)->normalizedType==SectionType::preChorus,"pre-chorus aliases");
  for (const auto *text : {"instrumental","solo","interludio"})
    expect(normalizeGuideCue(0,text)->normalizedType==SectionType::instrumental,"instrumental aliases");
  expect(normalizeGuideCue(0,"salida final")->normalizedType==SectionType::outro,
         "salida final is a distinct outro rather than ending");
  expect(normalizeGuideCue(0,"final")->normalizedType==SectionType::ending,"final is distinct");
  expect(!normalizeGuideCue(0,"uno, dos, uno, dos, tres, cuatro"),"count-in never creates a section");
  expect(!normalizeGuideCue(0,"cuatro"),"an isolated count does not become a section");
  expect(!normalizeGuideCue(-1,"coro") && !normalizeGuideCue(0,"coro",1.1),
         "invalid timing and invalid ASR probability are rejected");
  std::map<SectionType,int> counts;
  for (const auto *text : {"uno dos uno dos tres cuatro","coro","verso uno","pre coro","coro",
                            "verso dos","precoro","coro","puente","puente","instrumental",
                            "coro","puente","salida final","final"}) {
    const auto normalized=normalizeGuideCue(0,text);
    if (normalized) ++counts[normalized->normalizedType];
  }
  expect(counts[SectionType::chorus]==4 && counts[SectionType::verse]==2 &&
         counts[SectionType::preChorus]==2 && counts[SectionType::bridge]==3 &&
         counts[SectionType::instrumental]==1 && counts[SectionType::outro]==1 &&
         counts[SectionType::ending]==1 && counts[SectionType::intro]==0,
         "reference utterance sequence has the expected lexical counts, not ASR accuracy");
}
void testFusionWithoutGuideAndSafeSnapping() {
  auto result=fixture(); juce::String error;
  expect(fuseSectionSuggestions(result,{},error),"music-only structure fusion succeeds");
  expect(result.suggestions.size()==3,"one suggestion per structural interval");
  expect(close(result.suggestions[1].rawStartSeconds,8.1) &&
         close(result.suggestions[1].startSeconds,8) &&
         close(result.suggestions[1].quantizationDistanceSeconds,-0.1),
         "raw provider time and signed bar quantization distance both survive");
  expect(result.suggestions[1].startBarIndex==4 && result.suggestions[1].endBarIndex==8,
         "bar indices are zero-based");
  for (size_t i=0;i<result.suggestions.size();++i) {
    expect(result.suggestions[i].type==SectionType::unknown,"music structure alone cannot name a chorus");
    if (i) expect(close(result.suggestions[i-1].endSeconds,result.suggestions[i].startSeconds),
                  "suggestions are contiguous and nonoverlapping");
  }
  const auto previousId=result.suggestions[1].id;
  expect(fuseSectionSuggestions(result,{},error) && result.suggestions[1].id==previousId &&
         result.suggestions.size()==3,"repeat analysis has stable IDs and does not append duplicates");
  result.structure.sections={{0,8.8},{8.8,24}};
  expect(fuseSectionSuggestions(result,{},error) && close(result.suggestions[1].startSeconds,8.8),
         "structure far from a bar is not forced onto the grid");
  result=fixture(); result.structure.tempo.bars.clear(); result.structure.tempo.beatsPerMinute.reset();
  expect(fuseSectionSuggestions(result,{},error) && !result.suggestions[1].startBarIndex &&
         close(result.suggestions[1].startSeconds,8.1),"no click, bars or BPM still permits musical structure");
  result=fixture(); result.structure.sections={{2.0,8.1},{8.1,20}};
  expect(fuseSectionSuggestions(result,{},error) && result.suggestions.front().type==SectionType::unknown &&
         close(result.suggestions.front().endSeconds,2) && close(result.suggestions.back().endSeconds,24),
         "initial silence and unclassified final tail are retained, never invented as intro/outro");
}
void testGuideAnticipationAndRepetitions() {
  auto result=fixture(); juce::String error;
  result.guideCues={*normalizeGuideCue(5,"coro"),*normalizeGuideCue(12,"coro")};
  expect(fuseSectionSuggestions(result,{},error),"timed cue fusion succeeds");
  expect(result.suggestions[1].type==SectionType::chorus && result.suggestions[2].type==SectionType::chorus,
         "guide can anticipate a structural boundary by local bars");
  expect(result.suggestions[1].displayName=="Coro 1" && result.suggestions[2].displayName=="Coro 2",
         "repeated section announcements remain separate occurrences");
  expect(result.suggestions[1].guideAnticipationSeconds==3 &&
         result.suggestions[2].guideAnticipationSeconds==4,
         "each cue retains its own offset; no global anticipation is imposed");
  expect(result.suggestions[1].sources.contains(juce::String::fromUTF8("guía")) &&
         result.suggestions[1].sources.contains("estructura"),"provenance accompanies each suggestion");
  result.guideCues.push_back(*normalizeGuideCue(5.3,"verso dos"));
  expect(fuseSectionSuggestions(result,{},error) && result.suggestions[1].type==SectionType::unknown &&
         result.suggestions[1].confidenceLevel==ConfidenceLevel::low,
         "conflicting directives do not silently choose a label");
  result=fixture();
  result.structure.sections={{0,6},{6,18},{18,24}};
  result.structure.tempo.bars={0,1.5,3,4.5,6,7.5,9,12,15,18,21,24};
  result.guideCues={*normalizeGuideCue(4,"verso uno"),*normalizeGuideCue(12,"puente")};
  expect(fuseSectionSuggestions(result,{},error) &&
         result.suggestions[1].guideAnticipationSeconds==2 &&
         result.suggestions[2].guideAnticipationSeconds==6,
         "anticipation uses the local grid across variable tempo/meter");
}
void testConfirmedGridAndMeters() {
  juce::String error;
  for (const auto &bars : {std::vector<double>{0.5,2,3.5,5,6.5,8,9.5,11,12.5,14,15.5,17,18.5,20,21.5,23},
                           std::vector<double>{0.5,3.5,6.5,9.5,12.5,15.5,18.5,21.5}}) {
    auto result=fixture();
    result.structure.sections={{0,3.6},{3.6,24}};
    FusionOptions options; options.confirmedBars=bars;
    expect(fuseSectionSuggestions(result,options,error) && close(result.suggestions[1].startSeconds,3.5) &&
           result.suggestions[1].sources.contains(options.confirmedGridSource),
           "explicit 3/4 or 6/8 grid takes precedence with nonzero first downbeat");
    expect(result.structure.tempo.beatsPerMinute==120 && result.structure.tempo.bars.front()==0,
           "fusion never replaces provider evidence or user tempo metadata");
  }
  auto result=fixture(); FusionOptions options; options.confirmedBars={0,2,1};
  expect(!fuseSectionSuggestions(result,options,error),"unordered confirmed bar grid is rejected");
  result=fixture(); result.structure.sections.clear();
  expect(!fuseSectionSuggestions(result,{},error),"empty structure produces a clear error");
  result=fixture(); result.structure.sections[0].endSeconds=std::numeric_limits<double>::quiet_NaN();
  expect(!fuseSectionSuggestions(result,{},error),"nonfinite provider output is rejected");
  result=fixture(); result.structure.tempo.beats={2,1};
  expect(!fuseSectionSuggestions(result,{},error),"unordered pulse output is rejected");
  result=fixture(); result.structure.sections={{0,16},{8,24}};
  expect(!fuseSectionSuggestions(result,{},error),"overlapping ranges within a provider hierarchy are rejected");
  result=fixture(); result.durationSeconds=300; result.structure.sections.clear(); result.structure.tempo.bars.clear();
  for (int i=0;i<257;++i) result.structure.sections.push_back({double(i),double(i+1)});
  expect(!fuseSectionSuggestions(result,{},error),"fusion never exceeds the scheduler's section capacity");
}
void testAcceptancePreservesManualState() {
  SongArrangementSnapshot existing;
  SongArrangementPartSnapshot part;
  part.id="manual"; part.name="Coro corregido"; part.startSec=8; part.endSec=16;
  part.color="#abcdef"; part.muteInstruments=true;
  SectionStemAutomation automation; automation.stemId="bass"; automation.volume=.123f;
  part.automation.stems={automation}; existing.parts={part};
  SongArrangementCueSnapshot cue; cue.id="cue"; cue.name="Cuenta"; cue.partId="manual";
  cue.positionSec=9; cue.offsetSec=1; existing.cues={cue};
  auto accepted=appendConfirmedSuggestions(existing,{suggestion("first",0,8),suggestion("last",16,24)},24);
  expect(accepted.error.isEmpty() && accepted.arrangement.parts.size()==3 && accepted.addedSuggestionIds.size()==2,
         "confirmation fills gaps without replacing manual sections");
  const auto &preserved=accepted.arrangement.parts[1];
  expect(preserved.id==part.id && preserved.name==part.name && preserved.color==part.color &&
         preserved.muteInstruments && preserved.automation==part.automation && accepted.arrangement.cues==existing.cues,
         "manual names, automation, playback actions and anchored cues stay unchanged");
  auto conflict=appendConfirmedSuggestions(existing,{suggestion("first",0,8),suggestion("overlap",7,9)},24);
  expect(conflict.error.isNotEmpty() && conflict.addedSuggestionIds.isEmpty() &&
         conflict.arrangement.parts.size()==1 && conflict.conflictingSuggestionIds.contains("overlap"),
         "any overlap aborts the whole acceptance transaction");
  conflict=appendConfirmedSuggestions(accepted.arrangement,{suggestion("first",0,8)},24);
  expect(conflict.error.isNotEmpty() && conflict.arrangement.parts.size()==3,
         "accepting the same proposal twice cannot duplicate it");
  conflict=appendConfirmedSuggestions({}, {suggestion("a",0,10),suggestion("b",9,12)},24);
  expect(conflict.error.isNotEmpty() && conflict.arrangement.parts.empty(),
         "selected suggestions cannot overlap each other");
  part.sourcePartId="original"; existing.parts={part};
  expect(appendConfirmedSuggestions(existing,{suggestion("a",0,8)},24).error.isNotEmpty(),
         "source-time results are refused on a nonlinear copied arrangement");
  part.sourcePartId.clear(); part.sourceStartSec=0; part.sourceEndSec=8; existing.parts={part};
  expect(appendConfirmedSuggestions(existing,{suggestion("a",0,8)},24).error.isNotEmpty(),
         "moved source ranges are refused even when sourcePartId is empty");
  expect(appendConfirmedSuggestions({}, {suggestion("too-short",0,.2)},24).error.isNotEmpty(),
         "shorter-than-editor-minimum proposals are refused rather than silently extended");
  std::vector<SectionSuggestion> tooMany;
  for (int i=0;i<257;++i) tooMany.push_back(suggestion(juce::String(i),double(i),double(i+1)));
  expect(appendConfirmedSuggestions({},tooMany,300).error.isNotEmpty(),
         "confirmation cannot overflow the scheduler capacity");
  auto rejected=suggestion("rejected",0,8); rejected.status=SuggestionStatus::rejected;
  expect(appendConfirmedSuggestions({}, {rejected},24).arrangement.parts.empty(),
         "rejected proposals never persist");
}
void testTimebaseCancellationAndCodec() {
  for (double rate : {22050.,44100.,48000.,96000.}) {
    const auto sample=secondsToSourceSamples(286.123456789,rate);
    expect(sample.has_value(),"sample conversion accepts supported source rate");
    const auto seconds=sourceSamplesToSeconds(*sample,rate);
    expect(seconds && std::abs(*seconds-286.123456789)<=.5/rate,
           "seconds-samples roundtrip stays within half a source sample");
    expect(close(*sourceSampleToTimelineSeconds(static_cast<std::int64_t>(rate*3),rate,1.125),4.125),
           "stem offset and silence explicitly map to the song timeline");
  }
  expect(!secondsToSourceSamples(-1,44100) && !secondsToSourceSamples(1,0) &&
         !secondsToSourceSamples(std::numeric_limits<double>::max(),96000),
         "invalid or overflowing sample conversions are rejected");
  auto result=fixture(); juce::String error;
  result.suggestions={suggestion("retained",0,1)};
  expect(!fuseSectionSuggestions(result,{},error,[]{return true;}) && result.suggestions[0].id=="retained",
         "early cancellation preserves review state");
  int checksBeforeCancellation=0;
  expect(!fuseSectionSuggestions(result,{},error,[&]{return ++checksBeforeCancellation>2;}) &&
         result.suggestions[0].id=="retained","mid-fusion cancellation commits no partial results");
  result=fixture(); result.guideCues={*normalizeGuideCue(5,"coro")};
  expect(fuseSectionSuggestions(result,{},error),"fixture fusion before codec");
  result.suggestions[1].userEdited=true; result.suggestions[1].status=SuggestionStatus::confirmed;
  SectionDetectionResult decoded;
  const auto encoded=sectionDetectionResultToVar(result);
  expect(sectionDetectionResultFromVar(juce::JSON::parse(juce::JSON::toString(encoded)),decoded,error),
         "versioned normalized analysis roundtrips through JSON");
  expect(decoded.suggestions[1].userEdited && decoded.suggestions[1].status==SuggestionStatus::confirmed &&
         close(decoded.suggestions[1].rawStartSeconds,8.1) &&
         decoded.suggestions[1].matchedCueTimeSeconds==5 && decoded.guideCues[0].rawText=="coro",
         "codec retains original signals, offsets and review status");
  expect(!fuseSectionSuggestions(decoded,{},error) && decoded.suggestions[1].userEdited &&
         decoded.suggestions[1].status==SuggestionStatus::confirmed,
         "analyze again cannot overwrite an edited or reviewed proposal without explicit discard");
  auto malformed=juce::JSON::parse(juce::JSON::toString(encoded));
  malformed.getDynamicObject()->setProperty("schemaVersion",999);
  expect(!sectionDetectionResultFromVar(malformed,decoded,error),"unknown future schema is rejected");
  malformed=juce::JSON::parse(juce::JSON::toString(encoded));
  malformed["suggestions"].getArray()->getReference(0).getDynamicObject()->setProperty("endSeconds",999);
  expect(!sectionDetectionResultFromVar(malformed,decoded,error),"corrupted cached suggestion range is rejected");
  const auto key=sectionDetectionCacheKey(result,{});
  auto changed=result; changed.analyzedAudioFingerprint="different-content";
  expect(sectionDetectionCacheKey(changed,{})!=key,"changed source content invalidates cache");
  changed=result; changed.structure.version="provider-v2";
  expect(sectionDetectionCacheKey(changed,{})!=key,"provider version invalidates cache");
  changed=result; changed.analysisVersion="algorithm-v2";
  expect(sectionDetectionCacheKey(changed,{})!=key,"normalized algorithm version invalidates cache");
  FusionOptions options; options.maximumSnapSeconds=.2;
  expect(sectionDetectionCacheKey(result,options)!=key,"analysis parameters invalidate cache");
  options={}; options.confirmedBars={.5,2,3.5};
  expect(sectionDetectionCacheKey(result,options)!=key,"corrected user grid invalidates fused cache");
}
} // namespace
int main() {
  testSpeechNormalization(); testFusionWithoutGuideAndSafeSnapping();
  testGuideAnticipationAndRepetitions(); testConfirmedGridAndMeters();
  testAcceptancePreservesManualState(); testTimebaseCancellationAndCodec();
  std::cout << "Section detection: " << checks << " checks passed.\n";
}
