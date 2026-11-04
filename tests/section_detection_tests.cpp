#include "../src/analysis/SectionDetection.h"
#include "../src/analysis/GuideSectionAlignment.h"

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

GuideTranscript guide(double duration, std::vector<GuideTranscriptSegment> segments) {
  GuideTranscript transcript;
  transcript.provider="synthetic-speech"; transcript.providerVersion="timed-utterances-1";
  transcript.audioFingerprint="synthetic-audio"; transcript.modelFingerprint="synthetic-model";
  transcript.language="es"; transcript.durationSeconds=duration; transcript.segments=std::move(segments);
  return transcript;
}
const GuideGridMark *gridMark(const GuideAlignmentDraft &draft, double seconds) {
  for (const auto &mark : draft.grid) if (close(mark.timeSeconds,seconds)) return &mark;
  return nullptr;
}
void testGuideAlignmentOriginAndMeter() {
  const auto transcript=guide(30,{{1.25,2.5,"uno dos tres cuatro"},{2.8,3.1,"Verso uno"},{8.0,8.4,"Coro"}});
  GuideAlignmentOptions options; options.originSeconds=1.25; options.bpm=120;
  auto draft=alignGuideSections(transcript,options);
  expect(draft.error.isEmpty() && draft.suggestions.size()==2,"spoken counts define no invented section");
  const auto *origin=gridMark(draft,1.25), *fourth=gridMark(draft,2.75), *second=gridMark(draft,3.25);
  expect(origin && origin->strong && origin->barNumber==1 && origin->pulseNumber==1,
         "shifted source origin is the first pulse of bar one");
  expect(fourth && !fourth->strong && fourth->barNumber==1 && fourth->pulseNumber==4 && second && second->strong && second->barNumber==2,
         "four-four pulses and bar numbers are independent of a visible ruler");
  expect(close(draft.suggestions[0].startSeconds,3.25) && close(draft.suggestions[0].endSeconds,9.25) &&
         close(draft.suggestions[1].endSeconds,30) && draft.suggestions[0].displayName=="Verso 1",
         "proposed entries use the following bar and end at the next entry or source end");
  expect(close(draft.suggestions[0].rawStartSeconds,2.8) && draft.suggestions[0].matchedCueTimeSeconds==2.8 &&
         close(*draft.suggestions[0].guideAnticipationSeconds,.45) && draft.suggestions[0].confidenceLevel!=ConfidenceLevel::high,
         "raw voice onset and revisable alignment survive without automatic high confidence");
  const auto id=draft.suggestions[0].id;
  options.originSeconds=1.35;
  const auto movedOrigin=alignGuideSections(transcript,options);
  expect(movedOrigin.suggestions[0].id==id && close(movedOrigin.suggestions[0].startSeconds,3.35) &&
         close(transcript.segments[1].startSeconds,2.8),"origin changes rebuild positions while preserving cue identity and source times");
  options.originSeconds=.5; options.timeSignature="6/8";
  draft=alignGuideSections(guide(8,{{1.1,1.4,"Coro"}}),options);
  const auto *sixth=gridMark(draft,1.75), *nextBar=gridMark(draft,2.0);
  expect(draft.error.isEmpty() && sixth && sixth->pulseNumber==6 && nextBar && nextBar->strong &&
         close(draft.suggestions[0].startSeconds,2.0),"six-eight uses eighth-note pulses and a 1.5-second bar at 120 quarter BPM");
}
void testGuideAlignmentTempoPhaseAndCapacity() {
  GuideAlignmentOptions options; options.originSeconds=1; options.bpm=120;
  SongTempoRegionSnapshot first; first.startSec=1; first.endSec=1.75; first.bpm=120; first.timeSignature="4/4";
  auto slower=first; slower.startSec=1.75; slower.endSec=4; slower.bpm=60;
  auto compound=slower; compound.startSec=4; compound.endSec=12; compound.timeSignature="6/8";
  options.tempoMap.regions={first,slower,compound};
  const auto draft=alignGuideSections(guide(12,{{2.4,2.8,"Verso"},{7.2,7.6,"Puente"}}),options);
  const auto *preserved=gridMark(draft,2.25), *reset=gridMark(draft,4), *next=gridMark(draft,7);
  expect(draft.error.isEmpty() && !gridMark(draft,1.75) && preserved && preserved->barNumber==1 && preserved->pulseNumber==3,
         "a BPM change midway through a pulse preserves fractional musical phase");
  expect(reset && reset->strong && reset->barNumber==2 && next && next->strong && next->barNumber==3,
         "a meter change starts a new numbered bar instead of extending the previous meter");
  expect(close(draft.suggestions[0].startSeconds,4) && close(draft.suggestions[1].startSeconds,10) &&
         close(options.tempoMap.regions[0].bpm,120) && close(options.tempoMap.regions[1].bpm,60),
         "alignment follows the original variable tempo map without modifying it");
  const auto longDraft=alignGuideSections(guide(3000,{}),{});
  expect(longDraft.error.isEmpty() && longDraft.grid.size()>4096 && gridMark(longDraft,3000),
         "full musical grid extends past the visual ruler's old 4096-mark limit");
  options={}; options.bpm=300; options.timeSignature="4/16";
  const auto limited=alignGuideSections(guide(43200,{}),options);
  expect(limited.error.isNotEmpty() && limited.grid.empty() && limited.suggestions.empty(),
         "excessive musical grid returns a visible error rather than a truncated draft");
}
void testGuideAlignmentReviewAndAnticipation() {
  const auto transcript=guide(14,{{.1,.8,"1, 2, 3, 4"},{2.3,2.7,"Cordo!"},{6.1,6.7,"Perso dos"},{10.1,10.6,"Coro"}});
  auto draft=alignGuideSections(transcript,{});
  expect(draft.error.isEmpty() && draft.suggestions.size()==3 && draft.suggestions[0].displayName=="Revisar: Cordo!" &&
         draft.suggestions[1].displayName=="Revisar: Perso dos" && draft.suggestions[0].type==SectionType::unknown,
         "unrecognized speech is preserved as editable proposals instead of disappearing");
  for (const auto &suggestion : draft.suggestions)
    expect(suggestion.confidenceLevel!=ConfidenceLevel::high,"speech labels do not certify the musical entry");
  const auto fingerprintId=draft.suggestions[0].id;
  auto changed=transcript; changed.audioFingerprint="changed-audio";
  expect(alignGuideSections(changed,{}).suggestions[0].id!=fingerprintId,"source fingerprint participates in stable proposal identities");
  for (int anticipation : {0,1,2}) {
    GuideAlignmentOptions options; options.originSeconds=.5; options.anticipationBars=anticipation;
    const auto result=alignGuideSections(guide(12,{{2.2,2.4,"Coro"}}),options);
    expect(result.error.isEmpty() && close(result.suggestions[0].startSeconds,.5+2*anticipation),
           "zero, one and two bars refer to the bar containing the spoken cue");
  }
  draft=alignGuideSections(guide(12,{{1.1,1.3,"Coro"},{1.4,1.6,"Puente"},{4.1,4.3,"Coro"},{4.4,4.7,"Coro!"}}),{});
  expect(draft.error.isEmpty() && draft.suggestions.size()==2 && draft.suggestions[0].displayName.contains("Coro") &&
         draft.suggestions[0].displayName.contains("Puente") && draft.suggestions[0].displayName.startsWith("Revisar:") &&
         draft.suggestions[0].confidenceLevel==ConfidenceLevel::low,
         "different announcements colliding on an entry preserve both texts for review");
  expect(draft.suggestions[1].type==SectionType::chorus && draft.suggestions[1].reason.contains("Coro!"),
         "matching labels colliding on an entry merge without losing the raw announcements");
  draft=alignGuideSections(guide(9.9,{{9.7,9.8,"Final"}}),{});
  expect(draft.error.isEmpty() && close(draft.suggestions[0].startSeconds,8) && close(draft.suggestions[0].endSeconds,9.9) &&
         draft.suggestions[0].confidenceLevel==ConfidenceLevel::low,"a late entry is limited to an available bar without creating a zero-length final section");
  const auto counts=alignGuideSections(guide(12,{{1,2,"uno dos tres cuatro"},{3,4,"1, 2, 3, 4"}}),{});
  expect(counts.error.isEmpty() && counts.suggestions.empty(),"pure counts and silence never invent intro or ending sections");
}
void testGuideBoundaryEditingAndInvalidInput() {
  auto draft=alignGuideSections(guide(30,{{1,1.4,"Verso"},{9,9.4,"Coro"},{17,17.4,"Puente"}}),{});
  const auto firstId=draft.suggestions[0].id, middleId=draft.suggestions[1].id, lastId=draft.suggestions[2].id;
  const auto gridBefore=draft.grid;
  juce::String error;
  expect(moveGuideSectionBoundary(draft,middleId,11.3,false,error) && close(draft.suggestions[0].endSeconds,11.3) &&
         close(draft.suggestions[1].startSeconds,11.3) && close(draft.suggestions[1].endSeconds,18) &&
         close(draft.suggestions[2].startSeconds,18),"moving an interior boundary changes only its start and the previous end");
  expect(draft.suggestions[0].userEdited && draft.suggestions[1].userEdited &&
         close(draft.suggestions[1].rawStartSeconds,9) && close(*draft.suggestions[1].guideAnticipationSeconds,2.3),
         "manual boundary edits retain original cue timing and protect both changed neighbours");
  expect(moveGuideSectionBoundary(draft,firstId,3,false,error) && close(draft.suggestions[0].startSeconds,3) &&
         close(draft.suggestions[0].endSeconds,11.3),"first boundary can move without manufacturing a preceding section");
  expect(moveGuideSectionBoundary(draft,lastId,19,true,error) && close(draft.suggestions[2].startSeconds,20) &&
         close(draft.suggestions[1].endSeconds,20) && close(draft.suggestions[2].endSeconds,30),
         "bar snapping breaks ties forward while preserving the source end of the last section");
  expect(moveGuideSectionBoundary(draft,lastId,100,false,error) && close(draft.suggestions[2].startSeconds,29.75) &&
         close(draft.suggestions[2].endSeconds,30),"boundary movement clamps before the minimum final duration");
  expect(moveGuideSectionBoundary(draft,middleId,-100,false,error) && close(draft.suggestions[1].startSeconds,3.25) &&
         close(draft.suggestions[0].endSeconds,3.25),"boundary movement cannot cross or erase the previous section");
  bool sameGrid=draft.grid.size()==gridBefore.size();
  for(size_t i=0;sameGrid && i<draft.grid.size();++i) sameGrid=close(draft.grid[i].timeSeconds,gridBefore[i].timeSeconds) &&
      draft.grid[i].barNumber==gridBefore[i].barNumber && draft.grid[i].pulseNumber==gridBefore[i].pulseNumber;
  expect(sameGrid,"moving one proposal never moves the musical grid");
  const double retained=draft.suggestions[1].startSeconds;
  expect(!moveGuideSectionBoundary(draft,"missing",5,false,error) && !error.isEmpty() && close(draft.suggestions[1].startSeconds,retained),
         "stale proposal IDs fail without partial neighbour edits");
  expect(!moveGuideSectionBoundary(draft,middleId,std::numeric_limits<double>::quiet_NaN(),false,error),"nonfinite boundary edits are rejected");
  GuideAlignmentOptions options; options.bpm=0;
  expect(alignGuideSections(guide(12,{}),options).error.isNotEmpty(),"invalid original BPM is not replaced with an invented tempo");
  options={}; options.timeSignature="6/nope";
  expect(alignGuideSections(guide(12,{}),options).error.isNotEmpty(),"invalid meter is visible");
  options={}; options.originSeconds=-1;
  expect(alignGuideSections(guide(12,{}),options).error.isNotEmpty(),"negative grid origin is invalid");
  options={}; options.anticipationBars=3;
  expect(alignGuideSections(guide(12,{}),options).error.isNotEmpty(),"unsupported anticipation is rejected");
  auto invalid=guide(12,{{-1,1,"Coro"}});
  expect(alignGuideSections(invalid,{}).error.isNotEmpty(),"invalid source speech timestamps produce no proposals");
  invalid=guide(std::numeric_limits<double>::quiet_NaN(),{});
  expect(alignGuideSections(invalid,{}).error.isNotEmpty(),"nonfinite source duration is invalid");
  options={}; SongTempoRegionSnapshot a; a.startSec=0;a.endSec=6;
  auto b=a;b.startSec=4;b.endSec=12;options.tempoMap.regions={a,b};
  expect(alignGuideSections(guide(12,{}),options).error.isNotEmpty(),"overlapping tempo regions fail instead of guessing their ownership");
}
} // namespace
int main() {
  testSpeechNormalization(); testFusionWithoutGuideAndSafeSnapping();
  testGuideAnticipationAndRepetitions(); testConfirmedGridAndMeters();
  testAcceptancePreservesManualState(); testTimebaseCancellationAndCodec();
  testGuideAlignmentOriginAndMeter(); testGuideAlignmentTempoPhaseAndCapacity();
  testGuideAlignmentReviewAndAnticipation(); testGuideBoundaryEditingAndInvalidInput();
  std::cout << "Section detection: " << checks << " checks passed.\n";
}
