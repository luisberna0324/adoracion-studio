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
  for (const auto *text : {"instrumental","instrument"})
    expect(normalizeGuideCue(0,text)->normalizedType==SectionType::instrumental,"instrumental aliases");
  expect(normalizeGuideCue(0,"solo")->normalizedType==SectionType::solo &&
         normalizeGuideCue(0,"interludio")->normalizedType==SectionType::interlude,
         "solo and interlude retain their distinct editor catalog identities");
  expect(normalizeGuideCue(0,"salida final")->normalizedType==SectionType::outro,
         "salida final is a distinct outro rather than ending");
  expect(normalizeGuideCue(0,"final")->normalizedType==SectionType::ending,"final is distinct");
  expect(!normalizeGuideCue(0,"uno, dos, uno, dos, tres, cuatro"),"count-in never creates a section");
  expect(!normalizeGuideCue(0,"cuatro"),"an isolated count does not become a section");
  for (const auto *text : {"intro dos tres cuatro", "Intro 2 3 4",
       "Intro, dos, tres, cuatro", "Verse one two three four"})
    expect(!normalizeGuideCue(0,text),
           "a section followed by a count sequence is not accepted as a numbered section with a utility suffix");
  const auto explicitIntro=normalizeGuideCue(0,"Intro 2");
  const auto explicitVerse=normalizeGuideCue(0,"Verse 1");
  expect(explicitIntro && explicitIntro->normalizedType==SectionType::intro && explicitIntro->spokenOrdinal==2 &&
         explicitVerse && explicitVerse->normalizedType==SectionType::verse && explicitVerse->spokenOrdinal==1,
         "a single spoken ordinal remains a valid musical variant without a following count sequence");
  const auto withBand=normalizeGuideCue(0,"Coro con toda la banda");
  expect(withBand && withBand->normalizedType==SectionType::chorus && !withBand->spokenOrdinal,
         "rejecting count tails retains explicit musical sections with a known instrument utility suffix");
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
void testBilingualCatalogAndUtilities() {
  struct Example { const char *text; SectionType type; int ordinal; };
  const Example examples[]{
      {"Introduction",SectionType::intro,0},
      {"Verse TWO",SectionType::verse,2},{"Second verse",SectionType::verse,2},
      {"Verse number 3",SectionType::verse,3},{"Tercera estrofa",SectionType::verse,3},
      {"Pre-Chorus four",SectionType::preChorus,4},{"pre chorus 4th",SectionType::preChorus,4},
      {"PRE-CORO",SectionType::preChorus,0},{"Chorus first",SectionType::chorus,1},
      {"Refrain",SectionType::refrain,0},{"estribillo tercero",SectionType::refrain,3},
      {"Post-Chorus two",SectionType::postChorus,2},{"post coro dos",SectionType::postChorus,2},
      {"first postchorus",SectionType::postChorus,1},{"Bridge 2nd",SectionType::bridge,2},
      {"Puente once",SectionType::bridge,11},{"Instrumental",SectionType::instrumental,0},
      {"Solo",SectionType::solo,0},{"interlude",SectionType::interlude,0},
      {"interludio dos",SectionType::interlude,2},{"Tag",SectionType::tag,0},
      {"Vamp one",SectionType::vamp,1},{"Outro",SectionType::outro,0},
      {"Ending",SectionType::ending,0},{"Final",SectionType::ending,0},
      {"Next section, chorus three",SectionType::chorus,3}};
  for (const auto &example : examples) {
    const auto cue = normalizeGuideCue(8.25, example.text, .71);
    expect(cue && cue->normalizedType == example.type &&
           cue->spokenOrdinal.value_or(0) == example.ordinal,
           "Spanish and English catalog labels and ordinals share one normalizer");
    expect(cue->rawText == example.text && close(cue->cueTimeSeconds,8.25) && cue->asrConfidence == .71,
           "bilingual interpretation preserves literal words, timestamps and ASR confidence");
    expect(!isGuideUtilityCue(example.text),"explicit musical labels are not removed as utility instructions");
  }
  for (const auto *text : {"Entra bateria", "Entra la bateria", "Drums in", "Toda la banda", "All in",
       "Full band", "Subir intensidad", "Sube intensidad", "Build", "Slowly build", "Bajar intensidad",
       "Breakdown", "Hold", "Sostener", "A capella", "Big ending", "Final grande", "Last time",
       "Ultima vez", "Key change up", "Baja tono", "count-in", "uno dos tres cuatro", "one two three four",
       "subir intensidad en el coro", "bring it down in the chorus", "drums in chorus two"}) {
    expect(isGuideUtilityCue(text),"known bilingual count, intensity and instrument commands remain utility cues");
    expect(!normalizeGuideCue(0,text),"a utility mentioning a musical section cannot create that section's boundary");
  }
  for (const auto *text : {"Coro, toda la banda", "Chorus, all in", "Verse two with drums in"})
    expect(normalizeGuideCue(0,text).has_value() && !isGuideUtilityCue(text),
           "an explicit musical label may retain its accompanying known utility instruction");
  for (const auto *text : {"For a frame", "Prefrain", "Perso dos", "recuerda el coro de nuestra cancion"})
    expect(!normalizeGuideCue(0,text) && !isGuideUtilityCue(text),
           "unrecognized speech and incidental section words remain reviewable instead of being guessed or dropped");
  expect(!normalizeGuideCue(0,"second verse three"),"conflicting spoken ordinals do not choose a section occurrence");
}

void testBilingualCatalogCacheCompatibility() {
  expect(static_cast<int>(SectionType::unknown) == 0 && static_cast<int>(SectionType::instrumental) == 6 &&
         static_cast<int>(SectionType::custom) == 9 && static_cast<int>(SectionType::refrain) == 10,
         "new section types are appended without reinterpreting legacy serialized values");
  auto result = fixture();
  for (int value = 0; value <= static_cast<int>(SectionType::rap); ++value) {
    const auto type = static_cast<SectionType>(value);
    auto proposal = suggestion("catalog-" + juce::String(value), double(value), double(value + 1));
    proposal.type = type; proposal.displayName = sectionTypeName(type);
    result.suggestions.push_back(proposal);
    GuideCue cue; cue.cueTimeSeconds = value; cue.rawText = sectionTypeName(type); cue.normalizedType = type;
    result.guideCues.push_back(cue);
  }
  const auto encoded = sectionDetectionResultToVar(result);
  SectionDetectionResult decoded; juce::String error;
  expect(sectionDetectionResultFromVar(encoded,decoded,error) &&
         decoded.suggestions.size() == result.suggestions.size() && decoded.guideCues.size() == result.guideCues.size(),
         "schema one round trips both original and appended catalog types");
  for (size_t i=0;i<result.suggestions.size();++i)
    expect(decoded.suggestions[i].type == result.suggestions[i].type &&
           decoded.suggestions[i].displayName == result.suggestions[i].displayName &&
           decoded.guideCues[i].normalizedType == result.guideCues[i].normalizedType,
           "serialized musical identities remain distinct through cache decoding");
  expect(decoded.suggestions[9].type == SectionType::custom &&
         decoded.suggestions[12].type == SectionType::solo && decoded.suggestions[13].type == SectionType::interlude,
         "legacy custom and newly distinct solo/interlude retain their stable numeric identities");
  auto invalid = juce::JSON::parse(juce::JSON::toString(encoded));
  invalid["suggestions"].getArray()->getReference(0).getDynamicObject()->setProperty("type",static_cast<int>(SectionType::rap)+1);
  expect(!sectionDetectionResultFromVar(invalid,decoded,error),"unknown future section enum values are rejected");
}
void testGuideUtilityNormalization() {
  struct Example { const char *text; const char *name; };
  for (const auto &example : {Example{"Entra bateria","Drums In"}, {"Drums in","Drums In"},
       {"Toda la banda","All In"}, {"Full band","All In"}, {"Subir intensidad","Build"},
       {"Build","Build"}, {"Bajar intensidad","Breakdown"}, {"Breakdown","Breakdown"},
       {"subir la intensidad en el coro","Build"}}) {
    const auto cue=normalizeGuideUtilityCue(.37,example.text,.62);
    expect(cue && cue->name==example.name && close(cue->cueTimeSeconds,.37) && cue->asrConfidence==.62 &&
           cue->rawText==example.text,"known bilingual utilities normalize to existing audio keys while preserving evidence");
    const auto repeated=normalizeGuideUtilityCue(.37,cue->name,.62);
    expect(repeated && repeated->name==cue->name,"canonical utility names are idempotent");
  }
  const auto raw=juce::String::fromUTF8("  ¡Entra batería!  ");
  const auto accented=normalizeGuideUtilityCue(.37,raw,.62);
  expect(accented && accented->rawText==raw && accented->name=="Drums In",
         "utility recognition preserves punctuation, whitespace and accents in raw evidence");
  const auto unsupported=normalizeGuideUtilityCue(.37,"solo bajo");
  expect(unsupported && unsupported->name=="solo bajo","a known instruction without an exact audio key is not assigned a different meaning");
  for (const auto *text : {"1","uno dos tres cuatro","Count-in"})
    expect(isGuideCountCue(text) && !normalizeGuideUtilityCue(.37,text),"counts remain a separate family from utility point suggestions");
  for (const auto *text : {"Coro 2","For a frame"})
    expect(!normalizeGuideUtilityCue(.37,text),"musical labels and unrecognized text do not become utilities");
  expect(!normalizeGuideUtilityCue(-1,"Build") &&
         !normalizeGuideUtilityCue(std::numeric_limits<double>::quiet_NaN(),"Build") &&
         !normalizeGuideUtilityCue(.37,"Build",1.1),"invalid utility timestamps and ASR confidence are rejected");
}
void testGuideCountNormalization() {
  struct Example { const char *text; const char *name; };
  const Example examples[]{
      {"1","1"},{"uno","1"},{"one","1"},{"un","1"},{"una","1"},
      {"2","2"},{"dos","2"},{"two","2"},{"3","3"},{"tres","3"},{"three","3"},
      {"4","4"},{"cuatro","4"},{"four","4"},{"5","5"},{"cinco","5"},{"five","5"},
      {"6","6"},{"seis","6"},{"six","6"},{"7","7"},{"siete","7"},{"seven","7"}};
  for (const auto &example : examples) {
    const auto cue=normalizeGuideCountCue(.37,example.text,.62);
    expect(cue && cue->name==example.name && cue->rawText==example.text && close(cue->cueTimeSeconds,.37) &&
           cue->asrConfidence==.62,"single Spanish, English and numeric counts retain source evidence and native numeric keys");
  }
  const auto raw=juce::String::fromUTF8("  ¡DOS!  ");
  const auto accented=normalizeGuideCountCue(.37,raw,.62);
  expect(accented && accented->name=="2" && accented->rawText==raw,
         "count normalization preserves literal punctuation, whitespace and case separately from its numeric name");
  for (const auto *text : {"uno dos tres cuatro","one two one two three four","Count-in","cuenta uno","ocho","eight","8","0","01"}) {
    const auto cue=normalizeGuideCountCue(.37,text,.62);
    expect(cue && cue->name==text && cue->rawText==text && close(cue->cueTimeSeconds,.37),
           "compound, generic and unavailable count recordings remain literal single points without guessed subdivisions");
  }
  for (const auto *text : {"Coro 2","Build","For a frame","one more chorus"})
    expect(!normalizeGuideCountCue(.37,text),"section labels, utilities and unknown prose are not count announcements");
  expect(!normalizeGuideCountCue(-1,"one") &&
         !normalizeGuideCountCue(std::numeric_limits<double>::quiet_NaN(),"one") &&
         !normalizeGuideCountCue(.37,"one",1.1),"invalid count timing or confidence is rejected");
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
  const auto transcript=guide(14,{{.1,.8,"1, 2, 3, 4"},{2.3,2.7,"anuncio sin etiqueta"},{6.1,6.7,"Perso dos"},{10.1,10.6,"Coro"}});
  auto draft=alignGuideSections(transcript,{});
  expect(draft.error.isEmpty() && draft.suggestions.size()==3 && draft.suggestions[0].displayName=="Revisar: anuncio sin etiqueta" &&
         draft.suggestions[1].displayName=="Revisar: Perso dos" && draft.suggestions[0].type==SectionType::unknown,
         "unrecognized speech is preserved as editable proposals instead of disappearing");
  for (const auto &suggestion : draft.suggestions)
    expect(suggestion.confidenceLevel!=ConfidenceLevel::high,"speech labels do not certify the musical entry");
  const auto fingerprintId=draft.suggestions[0].id;
  auto changed=transcript; changed.audioFingerprint="changed-audio";
  expect(alignGuideSections(changed,{}).suggestions[0].id!=fingerprintId,"source fingerprint participates in stable proposal identities");
  for (double origin : {.25,.5,.75}) {
    GuideAlignmentOptions options; options.originSeconds=origin;
    const auto result=alignGuideSections(guide(12,{{2.2,2.4,"Coro"}}),options);
    expect(result.error.isEmpty() && close(result.suggestions[0].startSeconds,origin+2),
           "the automatic entry always uses the following bar when the origin changes");
  }
  draft=alignGuideSections(guide(12,{{1.1,1.3,"Coro"},{1.4,1.6,"Puente"},{4.1,4.3,"Coro"},{4.4,4.7,"Coro!"}}),{});
  expect(draft.error.isEmpty() && draft.suggestions.size()==2 && draft.suggestions[0].displayName.contains("Coro") &&
         draft.suggestions[0].displayName.contains("Puente") && draft.suggestions[0].displayName.startsWith("Revisar:") &&
         draft.suggestions[0].confidenceLevel==ConfidenceLevel::low,
         "different announcements colliding on an entry preserve both texts for review");
  expect(draft.suggestions[1].type==SectionType::chorus && draft.suggestions[1].reason.contains("Coro!"),
         "matching labels colliding on an entry merge without losing the raw announcements");
  expect(draft.suggestions[0].sources.contains(juce::String::fromUTF8(mergedGuideAnnouncementsSource)) &&
         draft.suggestions[1].sources.contains(juce::String::fromUTF8(mergedGuideAnnouncementsSource)),
         "merged evidence is marked so name corrections cannot teach an ambiguous multi-announcement phrase");
  draft=alignGuideSections(guide(9.9,{{9.7,9.8,"Final"}}),{});
  expect(draft.error.isNotEmpty() && draft.suggestions.empty(),
         "a late announcement cannot be moved backwards to make a following-bar entry fit");
  const auto counts=alignGuideSections(guide(12,{{1,2,"uno dos tres cuatro"},{3,4,"1, 2, 3, 4"}}),{});
  expect(counts.error.isEmpty() && counts.suggestions.empty() && counts.secondaryCues.size()==2 &&
         counts.secondaryCues[0].name=="uno dos tres cuatro" && counts.secondaryCues[1].name=="1, 2, 3, 4",
         "compound counts remain independent literal points without inventing musical sections");
}

void testGuideMusicalOnsetBoundaryTolerance() {
  const auto atZero=alignGuideSections(guide(14,{{0,.4,"Intro"}}),{});
  expect(atZero.error.isEmpty() && close(atZero.suggestions[0].startSeconds,2),
         "an announcement exactly at a zero origin still proposes the following bar");
  for (double onset : {3.98,3.985,4.0,4.015,4.02}) {
    const auto transcript=guide(14,{{onset,onset+.5,"Verse"}});
    const auto draft=alignGuideSections(transcript,{});
    expect(draft.error.isEmpty() && draft.suggestions.size()==1 && close(draft.suggestions[0].startSeconds,6),
           "onsets within twenty milliseconds on either side of a bar use the following bar consistently");
    expect(close(draft.suggestions[0].rawStartSeconds,onset) &&
           draft.suggestions[0].matchedCueTimeSeconds && close(*draft.suggestions[0].matchedCueTimeSeconds,onset),
           "boundary tolerance changes only the calculation anchor, never the original speech timestamp");
  }
  const auto outsideBefore=alignGuideSections(guide(14,{{3.979,4.7,"Chorus"}}),{});
  const auto outsideAfter=alignGuideSections(guide(14,{{4.021,4.7,"Chorus"}}),{});
  expect(outsideBefore.error.isEmpty() && close(outsideBefore.suggestions[0].startSeconds,4) &&
         outsideAfter.error.isEmpty() && close(outsideAfter.suggestions[0].startSeconds,6),
         "outside the tolerance window the actual onset determines the containing bar");
  expect(close(outsideBefore.suggestions[0].startSeconds,4),
         "speech crossing a bar does not automatically delay the section until the bar after its end");
  GuideAlignmentOptions offset;offset.originSeconds=1.25;
  const auto shifted=alignGuideSections(guide(14,{{3.235,3.8,"Chorus"}}),offset);
  expect(shifted.error.isEmpty() && close(shifted.suggestions[0].startSeconds,5.25),
         "a nonzero origin uses the same local boundary tolerance");
  const auto beforeOrigin=alignGuideSections(guide(14,{{1.22,1.7,"Intro"}}),offset);
  expect(beforeOrigin.error.isEmpty() && close(beforeOrigin.suggestions[0].startSeconds,1.25),
         "the next bar for an onset clearly preceding the origin is the first available bar");
  const auto onOrigin=alignGuideSections(guide(14,{{1.25,1.7,"Intro"}}),offset);
  expect(onOrigin.error.isEmpty() && close(onOrigin.suggestions[0].startSeconds,3.25),
         "an onset exactly on bar one proposes bar two");

  GuideAlignmentOptions changing;
  changing.tempoMap.regions={{"fast",0,4,120,"4/4"},{"slow",4,10,60,"4/4"},{"triple",10,16,60,"3/4"}};
  const auto mixed=alignGuideSections(guide(16,{{3.985,4.4,"Verse"},{7.985,8.4,"Chorus"},{9.985,10.4,"Bridge"}}),changing);
  expect(mixed.error.isEmpty() && mixed.suggestions.size()==3 &&
         close(mixed.suggestions[0].startSeconds,8) && close(mixed.suggestions[1].startSeconds,10) &&
         close(mixed.suggestions[2].startSeconds,13),
         "following boundaries come from the actual tempo and meter grid rather than a constant bar length");
  GuideAlignmentOptions shortMeter;shortMeter.bpm=300;shortMeter.timeSignature="1/64";
  const auto tiny=alignGuideSections(guide(1,{{.0065,.01,"Verse"}}),shortMeter);
  expect(tiny.error.isEmpty() && close(tiny.suggestions[0].startSeconds,.0125),
         "tolerance shrinks for very short local bars instead of swallowing multiple boundaries");
}

void testGuideTimingRegressionAndLateReview() {
  // Measured onset positions exercise the reported 9/14 boundary regression;
  // generic labels avoid embedding private speech or correcting ASR output.
  const double onsets[]{3.48,17.38,45.22,73.04,107.82,135.64,163.48,191.30,219.12,233.04,246.96,264.34,292.18,313.04};
  const int oldBars[]{3,6,15,22,32,40,49,56,64,68,73,77,86,91};
  const int newBars[]{3,7,15,23,33,41,49,57,65,69,73,78,86,92};
  auto transcript=guide(337.40916099773244,{});
  for(double onset:onsets)transcript.segments.push_back({onset,onset+.6,"Verse"});
  transcript.segments.push_back({1.74,2.2,"DOS"});
  transcript.segments.push_back({74.78,75.82,juce::String::fromUTF8("Entra batería")});
  GuideAlignmentOptions options;options.bpm=69;
  const auto draft=alignGuideSections(transcript,options);
  expect(draft.error.isEmpty() && draft.suggestions.size()==14,"the real onset regression retains all fourteen musical hypotheses");
  int changed=0;
  for(size_t i=0;i<14;++i) {
    expect(draft.suggestions[i].startBarIndex==newBars[i]-1 &&
           close(draft.suggestions[i].rawStartSeconds,onsets[i]),
           "each measured onset gets its expected following bar while retaining the spoken time");
    if(oldBars[i]!=newBars[i])++changed;
  }
  expect(changed==9,"twenty millisecond classification corrects precisely nine of fourteen measured boundary cases");
  expect(draft.secondaryCues.size()==2 && close(draft.secondaryCues[0].timeSeconds,1.74) &&
         close(draft.secondaryCues[1].timeSeconds,74.78),
         "count and utility points keep their exact source times during musical boundary correction");
  const auto retained=alignGuideSections(guide(9.9,{{1.0,1.5,"Verse"},{9.7,9.8,"Final"},{9.8,9.85,"Hold"}}),{});
  expect(retained.error.isNotEmpty() && retained.error.contains("Final") && retained.suggestions.size()==1 &&
         close(retained.suggestions[0].startSeconds,2) && retained.secondaryCues.size()==1 &&
         close(retained.secondaryCues[0].timeSeconds,9.8),
         "an unavailable late entry is explicit while other valid sections and independent points remain visible");
  const auto plan=appendConfirmedGuideSuggestions({},retained);
  expect(plan.error.isNotEmpty() && plan.arrangement.parts.empty() && plan.arrangement.cues.empty(),
         "a draft with unresolved following-bar timing cannot be partially accepted");
  const auto shortTail=alignGuideSections(guide(10.1,{{9.97,10.04,"Ending"}}),{});
  expect(shortTail.error.isNotEmpty() && shortTail.suggestions.empty(),
         "a following bar with less than the minimum section duration remains unresolved instead of moving backwards");
}

void testGuideFixedNextBarAcrossTempoAndMeter() {
  struct Meter { const char *name; double quarters; };
  for (double bpm : {69.0,120.0,240.0}) for (const auto meter : {
      Meter{"4/4",4.0},Meter{"6/8",3.0},Meter{"7/8",3.5}}) {
    GuideAlignmentOptions options;
    options.bpm=bpm; options.timeSignature=meter.name; options.originSeconds=.375;
    const double barLength=60.0/bpm*meter.quarters;
    for (double delta : {-.021,-.020,-.015,0.0,.015,.021,barLength*.5}) {
      auto transcript=guide(options.originSeconds+21*barLength,{});
      int index=0;
      for (const auto &definition : playworship::guide::sectionDefinitions()) {
        const double onset=options.originSeconds+(index+2)*barLength+delta;
        transcript.segments.push_back({onset,onset+.2,definition.key});
        ++index;
      }
      const auto draft=alignGuideSections(transcript,options);
      expect(draft.error.isEmpty() && draft.suggestions.size()==playworship::guide::sectionDefinitions().size(),
             "every catalog family produces one following-bar proposal across tempos and meters");
      for (size_t i=0;i<draft.suggestions.size();++i) {
        const int targetBarIndex=int(i)+2+(delta>=-.020?1:0);
        const auto &proposal=draft.suggestions[i];
        expect(proposal.startBarIndex==targetBarIndex &&
               close(proposal.startSeconds,options.originSeconds+targetBarIndex*barLength) &&
               proposal.startSeconds>transcript.segments[i].startSeconds &&
               close(proposal.rawStartSeconds,transcript.segments[i].startSeconds),
               "automatic boundaries use exactly the next musical bar, preserving the speech onset and bounded tolerance");
      }
    }
  }
}

void testCorrectedAnnouncementsKeepTheirMusicalRoles() {
  const auto transcript=guide(26,{{3.985,4.4,"Course 2."},{3.99,4.3,"Thor"},
      {7.985,8.4,"Exotacion"},{7.99,8.3,"Base."},
      {11.985,12.4,"Enterlude."},{11.99,12.3,"Swin."},
      {15.985,16.4,"Wrap!"},{19.985,20.4,"Prab!"}});
  auto draft=alignGuideSections(transcript,{});
  expect(draft.error.isEmpty() && draft.suggestions.size()==5 && draft.secondaryCues.size()==3,
         "confirmed primary, utility and count corrections are routed before musical alignment");
  const SectionType types[]{SectionType::chorus,SectionType::exhortation,SectionType::interlude,SectionType::rap,SectionType::rap};
  const double onsets[]{3.985,7.985,11.985,15.985,19.985};
  for (size_t i=0;i<draft.suggestions.size();++i)
    expect(draft.suggestions[i].type==types[i] && close(draft.suggestions[i].startSeconds,6+4*i) &&
           close(draft.suggestions[i].rawStartSeconds,onsets[i]),
           "corrected primary names receive the next-bar rule and retain literal onset evidence");
  expect(draft.suggestions[0].occurrence==2,"an explicitly spoken corrected ordinal survives alignment");
  const juce::StringArray names{"4","Bass","Swell"};
  const juce::StringArray raw{"Thor","Base.","Swin."};
  for (size_t i=0;i<draft.secondaryCues.size();++i)
    expect(draft.secondaryCues[i].name==names[int(i)] && draft.secondaryCues[i].rawText==raw[int(i)] &&
           close(draft.secondaryCues[i].timeSeconds,3.99+4*i) &&
           close(draft.secondaryCues[i].rawTimeSeconds,3.99+4*i),
           "corrected secondary points are never delayed, quantized or promoted into sections");
  juce::String error;
  const auto id=draft.suggestions[0].id;
  expect(moveGuideSectionBoundary(draft,id,5.25,false,error) &&
         close(draft.suggestions[0].startSeconds,5.25) && close(draft.suggestions[0].rawStartSeconds,3.985),
         "explicit visual editing can still override an automatically proposed following-bar boundary");
  const auto confirmed=appendConfirmedGuideSuggestions({},draft);
  expect(confirmed.error.isEmpty() && confirmed.arrangement.parts.size()==5 &&
         confirmed.arrangement.cues.size()==3 && close(confirmed.arrangement.parts[0].startSec,5.25),
         "a visual correction is preserved at confirmation without automatically reapplying the next-bar rule");
}

void testLearnedInterpretationKeepsGuideEvidenceIdentity() {
  const auto transcript=guide(16,{{3.985,4.7,"frase de ensayo"}});
  const auto original=alignGuideSections(transcript,{});
  GuideAlignmentOptions options;
  options.learnedMappings.push_back({"frase de ensayo",SectionType::bridge});
  const auto interpreted=alignGuideSections(transcript,options);
  expect(original.error.isEmpty() && interpreted.error.isEmpty() &&
         original.suggestions[0].type==SectionType::unknown && interpreted.suggestions[0].type==SectionType::bridge &&
         interpreted.suggestions[0].displayName=="Puente",
         "the review's learned mappings resolve a musical family through the shared interpretation helper");
  expect(original.suggestions[0].id==interpreted.suggestions[0].id &&
         close(interpreted.suggestions[0].rawStartSeconds,3.985) &&
         close(interpreted.suggestions[0].startSeconds,6) &&
         transcript.segments[0].text=="frase de ensayo" && close(transcript.segments[0].startSeconds,3.985),
         "semantic correction and tolerant timing preserve source identity and literal ASR evidence");
  const auto tentative=alignGuideSections(guide(16,{{1.1,1.7,"Prefrain"}}),{});
  expect(tentative.error.isEmpty() && tentative.suggestions.size()==1 &&
         tentative.suggestions[0].type==SectionType::refrain && tentative.suggestions[0].displayName.startsWith("Posible ") &&
         tentative.suggestions[0].confidenceLevel==ConfidenceLevel::low &&
         tentative.suggestions[0].sources.contains(juce::String::fromUTF8(tentativeGuideInterpretationSource)) &&
         tentative.suggestions[0].reason.contains("Prefrain"),
         "a cautious lexical candidate stays visibly tentative and retains its literal source evidence");
  const auto confirmedAliasTranscript=guide(16,{{1.1,1.7,"Foraframe!"}});
  const auto confirmedAlias=alignGuideSections(confirmedAliasTranscript,{});
  expect(confirmedAlias.error.isEmpty() && confirmedAlias.suggestions.size()==1 &&
         confirmedAlias.suggestions[0].type==SectionType::refrain &&
         confirmedAlias.suggestions[0].displayName=="Refrain" &&
         confirmedAlias.suggestions[0].sources.contains("confirmed-alias") &&
         confirmedAlias.suggestions[0].id==guideSectionSuggestionId(confirmedAliasTranscript,confirmedAliasTranscript.segments[0]) &&
         close(confirmedAlias.suggestions[0].rawStartSeconds,1.1) &&
         confirmedAlias.suggestions[0].reason.contains("Foraframe!") &&
         confirmedAliasTranscript.segments[0].text=="Foraframe!",
         "the confirmed ASR alias resolves strongly without changing its literal evidence or suggestion identity");
}
void testRepeatedUnnumberedGuideNames() {
  const auto transcript=guide(44,{{1.1,1.7,"Intro"},{5.1,5.7,"Intro"},
      {9.1,9.7,"Interludio"},{13.1,13.7,"Interludio"},
      {17.1,17.7,"Verso"},{21.1,21.7,"Verso"},
      {25.1,25.7,"Refrain"},{29.1,29.7,"Refrain"},
      {33.1,33.7,"Verse two"},{37.1,37.7,"Intro dos"}});
  const auto draft=alignGuideSections(transcript,{});
  const juce::StringArray expected{"Intro","Intro","Interludio","Interludio",
      "Verso 1","Verso 2","Refrain 1","Refrain 2","Verso 2","Intro 2"};
  expect(draft.error.isEmpty() && draft.suggestions.size()==size_t(expected.size()),
         "repeated announcements remain distinct musical entries without manufacturing extra variants");
  for (size_t i=0;i<draft.suggestions.size();++i) {
    expect(draft.suggestions[i].displayName==expected[int(i)] &&
               close(draft.suggestions[i].rawStartSeconds,transcript.segments[i].startSeconds),
           "unnumbered Intro and Interlude keep their names while recorded variants, Refrains and explicit ordinals remain numbered");
  }
  const auto plan=appendConfirmedGuideSuggestions({},draft);
  expect(plan.error.isEmpty() && plan.arrangement.parts.size()==draft.suggestions.size(),
         "unnumbered repeated names pass the existing atomic confirmation contract");
  for (size_t i=0;i<plan.arrangement.parts.size();++i)
    expect(plan.arrangement.parts[i].name==expected[int(i)] &&
               plan.arrangement.parts[i].id==draft.suggestions[i].id,
           "confirmed names preserve repeated unnumbered labels and source identities");
}
void testTentativeAcceptanceUsesCanonicalGuideNames() {
  auto draft=alignGuideSections(guide(16,{{1.1,1.7,"Prefrain"},{7.1,7.7,"Prefrain"}}),{});
  expect(draft.error.isEmpty() && draft.suggestions.size()==2 &&
         draft.suggestions[0].displayName=="Posible Refrain 1" &&
         draft.suggestions[1].displayName=="Posible Refrain 2",
         "repeated tentative sections are visibly labelled for review before confirmation");
  const auto confirmed=appendConfirmedGuideSuggestions({},draft);
  expect(confirmed.error.isEmpty() && confirmed.arrangement.parts.size()==2 &&
         confirmed.arrangement.parts[0].name=="Refrain 1" && confirmed.arrangement.parts[1].name=="Refrain 2",
         "confirming tentative families persists canonical native guide keys with their occurrences");
  expect(draft.suggestions[0].displayName=="Posible Refrain 1" &&
         confirmed.arrangement.parts[0].id==draft.suggestions[0].id &&
         close(draft.suggestions[0].rawStartSeconds,1.1) &&
         close(confirmed.arrangement.parts[0].startSec,draft.suggestions[0].startSeconds),
         "canonical confirmation leaves the review label, source evidence, identity and chosen position intact");
  juce::String error;
  expect(moveGuideSectionBoundary(draft,draft.suggestions[1].id,9,false,error) &&
         draft.suggestions[1].userEdited && close(draft.suggestions[1].startSeconds,9),
         "a tentative candidate can be manually moved before confirmation");
  const auto moved=appendConfirmedGuideSuggestions({},draft);
  expect(moved.error.isEmpty() && moved.arrangement.parts[1].name=="Refrain 2" &&
         close(moved.arrangement.parts[1].startSec,9) && draft.suggestions[1].displayName=="Posible Refrain 2",
         "moving a tentative candidate does not persist its temporary review prefix");
  auto renamed=draft;
  renamed.suggestions[1].type=SectionType::custom;
  renamed.suggestions[1].displayName="Posible Refrain 2";
  const auto freeform=appendConfirmedGuideSuggestions({},renamed);
  expect(freeform.error.isEmpty() && freeform.arrangement.parts[1].name=="Posible Refrain 2",
         "an explicit custom name preserves the user's literal Posible prefix");
  const auto single=alignGuideSections(guide(16,{{1.1,1.7,"Prefrain"}}),{});
  const auto singlePlan=appendConfirmedGuideSuggestions({},single);
  expect(singlePlan.error.isEmpty() && singlePlan.arrangement.parts[0].name=="Refrain" &&
         single.suggestions[0].displayName=="Posible Refrain",
         "an unnumbered generated tentative label keeps its canonical unnumbered family on acceptance");
}
void testGuideUtilitiesDoNotDivideMusicalSections() {
  auto draft=alignGuideSections(guide(16,{{1.1,1.3,"Chorus"},{1.6,1.9,"Drums in"},
      {4.2,4.7,"Subir intensidad"},{7.1,7.5,"Verse two"},{11.1,11.5,"Estribillo"}}),{});
  expect(draft.error.isEmpty() && draft.suggestions.size()==3,
         "known utility announcements neither collide with a musical entry nor create intermediate sections");
  expect(draft.suggestions[0].type==SectionType::chorus && draft.suggestions[0].displayName=="Coro" &&
         close(draft.suggestions[0].startSeconds,2) && close(draft.suggestions[0].endSeconds,8),
         "a same-bar instrument entry leaves the chorus label and its full range intact");
  expect(draft.suggestions[1].type==SectionType::verse && draft.suggestions[1].displayName=="Verso 2" &&
         close(draft.suggestions[1].startSeconds,8) && close(draft.suggestions[1].endSeconds,12) &&
         draft.suggestions[2].type==SectionType::refrain && close(draft.suggestions[2].startSeconds,12),
         "a mixed-language guide preserves musical order and newly recognized refrain boundaries");
  expect(draft.secondaryCues.size()==2 && draft.secondaryCues[0].name=="Drums In" &&
         close(draft.secondaryCues[0].timeSeconds,1.6) && draft.secondaryCues[1].name=="Build" &&
         close(draft.secondaryCues[1].timeSeconds,4.2),"known utilities survive as independent points at their spoken onsets");
}

GuideSecondaryCueSuggestion secondary(juce::String id, juce::String name, double time) {
  GuideSecondaryCueSuggestion cue;
  cue.id=std::move(id); cue.name=cue.rawText=std::move(name);
  cue.rawTimeSeconds=cue.timeSeconds=time; cue.asrConfidence=.8; return cue;
}
SongArrangementPartSnapshot sourcePart(juce::String id, double start, double end) {
  SongArrangementPartSnapshot part;
  part.id=part.name=std::move(id); part.startSec=part.sourceStartSec=start;
  part.endSec=part.sourceEndSec=end; return part;
}
bool sameParts(const std::vector<SongArrangementPartSnapshot> &a,
               const std::vector<SongArrangementPartSnapshot> &b) {
  return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](const auto &left,const auto &right) {
    return left.id==right.id && left.name==right.name && left.color==right.color &&
        left.startSec==right.startSec && left.endSec==right.endSec && left.sourcePartId==right.sourcePartId &&
        left.sourceStartSec==right.sourceStartSec && left.sourceEndSec==right.sourceEndSec &&
        left.utilityCue==right.utilityCue && left.muteInstruments==right.muteInstruments &&
        left.skipPlayback==right.skipPlayback && left.automation==right.automation;
  });
}
void testSecondarySourceTimingAndMoves() {
  auto transcript=guide(16,{{1.1,1.4,"Coro"},
      {1.6,1.9,juce::String::fromUTF8("  ¡Entra batería!  ")},{4.2,4.7,"Subir intensidad"},{7.1,7.5,"Verso dos"}});
  transcript.segments[1].confidence=.62;
  auto draft=alignGuideSections(transcript,{});
  expect(draft.error.isEmpty() && draft.secondaryCues.size()==2 && draft.secondaryCues[0].id.startsWith("guide-secondary-") &&
         draft.secondaryCues[0].rawText==transcript.segments[1].text && draft.secondaryCues[0].asrConfidence==.62,
         "secondary proposals have distinct stable IDs and retain literal source evidence");
  const auto id=draft.secondaryCues[0].id;
  for (double origin : {.1,.3,.6}) {
    GuideAlignmentOptions options; options.originSeconds=origin;
    const auto rebuilt=alignGuideSections(transcript,options);
    expect(rebuilt.error.isEmpty() && rebuilt.secondaryCues[0].id==id &&
           close(rebuilt.secondaryCues[0].timeSeconds,1.6) && close(rebuilt.secondaryCues[1].timeSeconds,4.2),
           "changing bar origin never shifts utility source onsets");
  }
  const auto primary=draft.suggestions;
  juce::String error;
  expect(moveGuideSecondaryCue(draft,id,2.25,true,error) && close(draft.secondaryCues[0].timeSeconds,2.5),
         "secondary dragging snaps to a pulse, breaking an exact tie forward");
  expect(moveGuideSecondaryCue(draft,id,2.375,false,error) && close(draft.secondaryCues[0].timeSeconds,2.375) &&
         close(draft.secondaryCues[0].rawTimeSeconds,1.6) && draft.secondaryCues[0].userEdited &&
         close(draft.secondaryCues[1].timeSeconds,4.2),"free secondary dragging changes one point and preserves raw onset and other instructions");
  expect(draft.suggestions[0].startSeconds==primary[0].startSeconds && draft.suggestions[0].endSeconds==primary[0].endSeconds &&
         draft.suggestions[1].startSeconds==primary[1].startSeconds && draft.suggestions[1].endSeconds==primary[1].endSeconds,
         "moving secondary points never divides, stretches or moves musical sections");
  const auto retained=draft.secondaryCues[0].timeSeconds;
  expect(!moveGuideSecondaryCue(draft,"missing",3,false,error) &&
         !moveGuideSecondaryCue(draft,id,std::numeric_limits<double>::quiet_NaN(),false,error) &&
         draft.secondaryCues[0].timeSeconds==retained,"stale IDs and nonfinite drag positions fail without edits");
  auto invalid=draft; invalid.secondaryCues[1].id=id;
  expect(!moveGuideSecondaryCue(invalid,id,3,false,error),"duplicate secondary IDs are rejected before a move");
  invalid=draft; invalid.secondaryCues[0].id=invalid.suggestions[0].id;
  expect(!moveGuideSecondaryCue(invalid,invalid.secondaryCues[0].id,3,false,error),"primary and secondary IDs cannot collide");
  invalid=draft; invalid.secondaryCues[1].rawTimeSeconds=std::numeric_limits<double>::infinity();
  expect(!moveGuideSecondaryCue(invalid,id,3,false,error),"every raw secondary timestamp is validated");
  invalid=draft; invalid.secondaryCues[1].asrConfidence=1.1;
  expect(!moveGuideSecondaryCue(invalid,id,3,false,error),"invalid secondary confidence prevents a partial update");
  invalid=draft; invalid.grid[0].timeSeconds=std::numeric_limits<double>::quiet_NaN();
  expect(!moveGuideSecondaryCue(invalid,id,3,true,error),"invalid musical grid prevents snapping");
  expect(moveGuideSecondaryCue(draft,id,-2,false,error) && draft.secondaryCues[0].timeSeconds==0 &&
         moveGuideSecondaryCue(draft,id,100,false,error) && draft.secondaryCues[0].timeSeconds<draft.durationSeconds,
         "free movement clamps into the half-open source range, never to the silent EOF point");
}

void testGuideIndividualCountPoints() {
  auto transcript=guide(16,{{0,.44,"Uno."},{1.74,2.2,"DOS"},{3.48,4,"Intro"},
      {4.34,4.82,"DOS"},{5.2,5.68,juce::String::fromUTF8("¡Tres!")},{6.08,6.56,"4."},
      {8.1,8.5,"All in"},{10.1,10.5,"Verse two"}});
  transcript.segments[0].confidence=.62;
  GuideAlignmentOptions options; options.bpm=69;
  const auto draft=alignGuideSections(transcript,options);
  expect(draft.error.isEmpty() && draft.suggestions.size()==2 && draft.secondaryCues.size()==6,
         "five separate spoken counts and one utility survive alongside the two musical section labels");
  const juce::StringArray names{"1","2","2","3","4","All In"};
  const double times[]{0,1.74,4.34,5.2,6.08,8.1};
  for (size_t i=0;i<draft.secondaryCues.size();++i)
    expect(draft.secondaryCues[i].name==names[int(i)] && close(draft.secondaryCues[i].timeSeconds,times[i]) &&
           close(draft.secondaryCues[i].rawTimeSeconds,times[i]),
           "count points retain the actual spoken onsets without adding a missing beat or merging repeated numbers");
  expect(draft.secondaryCues[0].rawText=="Uno." && draft.secondaryCues[0].asrConfidence==.62 &&
         draft.secondaryCues[1].id!=draft.secondaryCues[2].id &&
         draft.secondaryCues[1].rawText==draft.secondaryCues[2].rawText,
         "two identical count words at different times keep separate stable source identities");
  for (double origin : {.1,.25,.6}) {
    auto changed=options; changed.originSeconds=origin;
    const auto rebuilt=alignGuideSections(transcript,changed);
    expect(rebuilt.error.isEmpty() && rebuilt.secondaryCues.size()==draft.secondaryCues.size(),
           "changing section-grid settings preserves all independent count occurrences");
    for (size_t i=0;i<draft.secondaryCues.size();++i)
      expect(rebuilt.secondaryCues[i].id==draft.secondaryCues[i].id &&
             rebuilt.secondaryCues[i].timeSeconds==draft.secondaryCues[i].timeSeconds,
             "count IDs and source positions remain independent of the chosen origin");
  }
  const auto accepted=appendConfirmedGuideSuggestions({},draft);
  expect(accepted.error.isEmpty() && accepted.arrangement.parts.size()==2 && accepted.arrangement.cues.size()==6 &&
         arrangementCueEventCount(accepted.arrangement)==6,"individual counts and utilities persist as six events, without automatic count sequences");
  for (size_t i=0;i<5;++i) {
    const auto &cue=accepted.arrangement.cues[i];
    expect(cue.name==names[int(i)] && close(cue.positionSec,times[i]) && cue.countEnd==0 &&
           cue.countIntervalSec==0 && cue.countOffsetsSec.empty() &&
           cue.color==juce::Colour::fromRGB(91,169,218).toString(),
           "native single-count names persist at exact source positions with existing blue styling and no fabricated pulse offsets");
  }
  expect(accepted.arrangement.cues[0].positionSec==0 && accepted.arrangement.cues[0].partId.isEmpty(),
         "the first spoken count at source zero remains valid before the first musical section");
  auto moved=draft; juce::String error;
  expect(moveGuideSecondaryCue(moved,moved.secondaryCues[0].id,.37,false,error) &&
         close(moved.secondaryCues[0].timeSeconds,.37) && moved.secondaryCues[0].rawTimeSeconds==0 &&
         moved.suggestions[0].startSeconds==draft.suggestions[0].startSeconds,
         "a user may freely correct one count point without changing its source evidence or musical sections");
}

void testGuideCompoundCountPreservationAndCapacity() {
  const auto compound=alignGuideSections(guide(12,{{0,2,"uno dos tres cuatro"},{3,4,"ocho"},
      {5,6,"Count-in"},{7,8,"1, 2, 3, 4"},{9,10,"0"}}),{});
  expect(compound.error.isEmpty() && compound.suggestions.empty() && compound.secondaryCues.size()==5,
         "compound and unsupported count announcements stay visible as one reviewable point per source segment");
  const auto accepted=appendConfirmedGuideSuggestions({},compound);
  const juce::StringArray names{"uno dos tres cuatro","ocho","Count-in","1, 2, 3, 4","0"};
  expect(accepted.error.isEmpty() && accepted.arrangement.parts.empty() && accepted.arrangement.cues.size()==5 &&
         arrangementCueEventCount(accepted.arrangement)==5,"compound counts can be confirmed without musical sections and never invent subword times");
  for (size_t i=0;i<accepted.arrangement.cues.size();++i)
    expect(accepted.arrangement.cues[i].name==names[int(i)] && accepted.arrangement.cues[i].countEnd==0 &&
           accepted.arrangement.cues[i].countOffsetsSec.empty() &&
           accepted.arrangement.cues[i].color==juce::Colour::fromRGB(91,169,218).toString(),
           "unsupported and compound counts retain their literal names, single event and preview blue after confirmation");

  GuideAlignmentDraft renamed; renamed.durationSeconds=12;
  renamed.secondaryCues={secondary("manual-count","Dos",0)};
  const auto normalized=appendConfirmedGuideSuggestions({},renamed);
  expect(normalized.error.isEmpty() && normalized.arrangement.cues[0].name=="2" && normalized.arrangement.cues[0].countEnd==0,
         "a manually corrected Spanish count name is saved under its native numeric key");

  const auto five=alignGuideSections(guide(8,{{0,.44,"Uno."},{1.74,2.2,"DOS"},{4.34,4.82,"DOS"},
      {5.2,5.68,juce::String::fromUTF8("¡Tres!")},{6.08,6.56,"4."}}),{});
  SongArrangementSnapshot existing;
  for (int i=0;i<509;++i) {
    SongArrangementCueSnapshot cue; cue.id="count-capacity-"+juce::String(i); cue.name="Build"; cue.positionSec=.01*i;
    existing.cues.push_back(cue);
  }
  const auto rejected=appendConfirmedGuideSuggestions(existing,five);
  expect(rejected.error.isNotEmpty() && rejected.arrangement.cues==existing.cues && rejected.arrangement.parts.empty() &&
         rejected.addedSuggestionIds.isEmpty(),"five new single counts reject a 514-event result atomically");
  existing.cues.resize(507);
  const auto full=appendConfirmedGuideSuggestions(existing,five);
  expect(full.error.isEmpty() && arrangementCueEventCount(full.arrangement)==512,
         "five independent count announcements fit exactly when the combined event total is 512");
}

void testGuideSecondaryAcceptance() {
  SongArrangementSnapshot existing;
  existing.parts={sourcePart("A",0,4),sourcePart("B",4,8)};
  existing.parts[0].muteInstruments=true;
  SongArrangementCueSnapshot old; old.id="saved-cue"; old.name="Hold"; old.positionSec=2; old.partId="A"; old.offsetSec=2;
  existing.cues.push_back(old);
  GuideAlignmentDraft draft; draft.durationSeconds=12;
  draft.secondaryCues={secondary("boundary",juce::String::fromUTF8("Entra batería"),4),
      secondary("gap","Entrada suave",9),secondary("zero","Build",0)};
  auto plan=appendConfirmedGuideSuggestions(existing,draft);
  expect(plan.error.isEmpty() && sameParts(existing.parts,plan.arrangement.parts) && plan.arrangement.cues.size()==4,
         "secondary-only confirmation adds points without changing any musical section or action");
  const auto find=[&](const char *id)->const SongArrangementCueSnapshot & {
    const auto found=std::find_if(plan.arrangement.cues.begin(),plan.arrangement.cues.end(),
        [&](const auto &cue){return cue.id==id;});
    expect(found!=plan.arrangement.cues.end(),"accepted independent cue is present"); return *found;
  };
  expect(find("saved-cue")==old,"every saved cue field remains intact");
  expect(find("boundary").name=="Drums In" && find("boundary").partId=="B" && find("boundary").offsetSec==0 &&
         find("boundary").positionSec==4,"a cue exactly on a section boundary anchors to the following section and canonical audio key");
  expect(find("gap").name=="Entrada suave" && find("gap").partId.isEmpty() && find("gap").positionSec==9 &&
         find("gap").offsetSec==0,"manual cue names survive and a gap keeps the absolute fallback without inventing a parent");
  expect(find("zero").partId=="A" && find("zero").positionSec==0 && find("zero").countEnd==0 &&
         find("zero").countOffsetsSec.empty(),"utility points at source zero are valid and do not invent count sequences");
  expect(plan.addedSuggestionIds.size()==3,"atomic acceptance reports secondary additions even without new sections");

  SongArrangementSnapshot empty;
  GuideAlignmentDraft mixed; mixed.durationSeconds=12;
  mixed.suggestions={suggestion("new-section",2,8)};
  mixed.secondaryCues={secondary("before","Build",1),secondary("inside","All In",3),secondary("after","Hold",9)};
  plan=appendConfirmedGuideSuggestions(empty,mixed);
  expect(plan.error.isEmpty() && plan.arrangement.parts.size()==1 && find("inside").partId=="new-section" &&
         find("inside").offsetSec==1 && find("before").partId.isEmpty() && find("after").partId.isEmpty(),
         "new utility anchors resolve against the final proposed sections, with absolute marks before and after them");

  const auto rejectedUnchanged=[&](const GuideAlignmentDraft &bad) {
    const auto rejected=appendConfirmedGuideSuggestions(existing,bad);
    return rejected.error.isNotEmpty() && sameParts(rejected.arrangement.parts,existing.parts) &&
        rejected.arrangement.cues==existing.cues && rejected.addedSuggestionIds.isEmpty();
  };
  auto invalid=draft; invalid.secondaryCues[0].timeSeconds=12;
  expect(rejectedUnchanged(invalid),"a cue at EOF rejects the entire operation without touching saved annotations");
  invalid=draft; invalid.secondaryCues[0].id="A";
  expect(rejectedUnchanged(invalid),"secondary IDs may not reuse an existing musical section ID");
  invalid=draft; invalid.secondaryCues[0].id="saved-cue";
  expect(rejectedUnchanged(invalid),"secondary IDs may not reuse a saved cue ID");
  invalid=draft; invalid.secondaryCues[0].id.clear();
  expect(rejectedUnchanged(invalid),"empty secondary IDs cannot enter persistence");
  invalid=draft; invalid.secondaryCues[0].timeSeconds=std::numeric_limits<double>::quiet_NaN();
  expect(rejectedUnchanged(invalid),"nonfinite secondary positions reject the whole plan");
  invalid=draft; invalid.secondaryCues[1].name=" ";
  expect(rejectedUnchanged(invalid),"empty manual cue labels reject the whole plan");
  invalid=draft; invalid.suggestions={suggestion("too-short",8,8.1)};
  expect(rejectedUnchanged(invalid),"invalid primary minimum duration also prevents committing secondary points");
  invalid=draft; invalid.suggestions={suggestion("overlap",3,5)};
  expect(rejectedUnchanged(invalid),"a musical overlap prevents otherwise-valid secondary additions atomically");

  SongArrangementSnapshot overlapping; overlapping.parts={sourcePart("left",0,6),sourcePart("right",4,8)};
  GuideAlignmentDraft ambiguous; ambiguous.durationSeconds=12; ambiguous.secondaryCues={secondary("cue","Build",5)};
  const auto ambiguity=appendConfirmedGuideSuggestions(overlapping,ambiguous);
  expect(ambiguity.error.isNotEmpty() && sameParts(ambiguity.arrangement.parts,overlapping.parts) && ambiguity.arrangement.cues.empty(),
         "ambiguous parents reject the whole plan rather than selecting a section arbitrarily");

  SongArrangementSnapshot stale=existing;
  stale.cues[0].offsetSec=1; stale.cues[0].positionSec=10;
  SongArrangementCueSnapshot absolute; absolute.id="absolute"; absolute.name="Hold"; absolute.positionSec=5;
  stale.cues.insert(stale.cues.begin(),absolute);
  GuideAlignmentDraft ordering; ordering.durationSeconds=12; ordering.secondaryCues={secondary("middle","Build",3)};
  plan=appendConfirmedGuideSuggestions(stale,ordering);
  expect(plan.error.isEmpty() && plan.arrangement.cues[0]==stale.cues[1] && plan.arrangement.cues[1].id=="middle" &&
         plan.arrangement.cues[2]==absolute,"combined ordering uses resolved source time without repairing saved fallback fields");
}

void testGuideSecondaryCapacityAndAtomicity() {
  SongArrangementSnapshot existing;
  for (int i=0;i<507;++i) {
    SongArrangementCueSnapshot cue; cue.id="saved-"+juce::String(i); cue.name="Build"; cue.positionSec=i*.02;
    existing.cues.push_back(cue);
  }
  SongArrangementCueSnapshot count; count.id="count-sequence"; count.name="1"; count.positionSec=1;
  count.countEnd=4; count.countIntervalSec=.5; count.countOffsetsSec={0,.5,1,1.5}; existing.cues.push_back(count);
  expect(arrangementCueEventCount(existing)==511,"capacity counts expanded count events, not just persisted point objects");
  GuideAlignmentDraft draft; draft.durationSeconds=12; draft.suggestions={suggestion("new-primary",0,12)};
  draft.secondaryCues={secondary("new-a","Build",.37),secondary("new-b","Hold",.71)};
  const auto rejected=appendConfirmedGuideSuggestions(existing,draft);
  expect(rejected.error.isNotEmpty() && rejected.arrangement.parts.empty() && rejected.arrangement.cues==existing.cues &&
         rejected.addedSuggestionIds.isEmpty(),"exceeding 512 expanded announcements rolls back both primary and secondary additions");
  draft.secondaryCues.pop_back();
  const auto accepted=appendConfirmedGuideSuggestions(existing,draft);
  expect(accepted.error.isEmpty() && accepted.arrangement.parts.size()==1 && arrangementCueEventCount(accepted.arrangement)==512,
         "exactly 512 expanded announcements can be confirmed together with musical sections");
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
  auto invalid=guide(12,{{-1,1,"Coro"}});
  expect(alignGuideSections(invalid,{}).error.isNotEmpty(),"invalid source speech timestamps produce no proposals");
  invalid=guide(std::numeric_limits<double>::quiet_NaN(),{});
  expect(alignGuideSections(invalid,{}).error.isNotEmpty(),"nonfinite source duration is invalid");
  options={}; SongTempoRegionSnapshot a; a.startSec=0;a.endSec=6;
  auto b=a;b.startSec=4;b.endSec=12;options.tempoMap.regions={a,b};
  expect(alignGuideSections(guide(12,{}),options).error.isNotEmpty(),"overlapping tempo regions fail instead of guessing their ownership");
}
void testWordCountIntervalsUseUniqueMusicalPulses() {
  auto transcript=guide(14,{{.02,.46,"Uno."},{1.06,1.52,"DOS"},
      {2.16,4.12,"intro, dos, tres, cuatro."},{10.44,10.96,"Intro"}});
  auto &parent=transcript.segments[2];
  parent.language="es"; parent.confidence=.817;
  parent.words={{"intro,",2.17,2.36,.8},{"dos,",2.53,2.78,.778},
      {"tres,",2.93,3.27,.992},{"cuatro.",3.43,3.96,.984}};
  GuideAlignmentOptions options; options.originSeconds=.02; options.bpm=116;
  auto draft=alignGuideSections(transcript,options);
  expect(draft.error.isEmpty() && draft.suggestions.size()==2 && draft.secondaryCues.size()==5,
         "word evidence recovers Intro and only the three actually transcribed counts");
  expect(draft.suggestions[0].displayName=="Intro" && draft.suggestions[1].displayName=="Intro" &&
         close(draft.suggestions[0].rawStartSeconds,2.16),
         "the main label retains the parent announcement identity and is not numbered from a count");
  const char *names[]{"1","2","2","3","4"};
  const double times[]{.02,1.06,.02+5*60./116,.02+6*60./116,.02+7*60./116};
  for(size_t i=0;i<5;++i) expect(draft.secondaryCues[i].name==names[i] &&
      close(draft.secondaryCues[i].timeSeconds,times[i]),
      "individual onsets stay exact while compound counts use their sole evidenced musical pulse");
  expect(close(draft.secondaryCues[2].rawTimeSeconds,2.53) &&
         close(transcript.segments[2].words[1].startSeconds,2.53) &&
         transcript.segments[2].text=="intro, dos, tres, cuatro.",
         "uncertain word bounds and original parent text remain intact rather than masquerading as precise speech times");
  const auto countId=draft.secondaryCues[2].id;
  auto moved=options; moved.originSeconds=.025;
  auto rebuilt=alignGuideSections(transcript,moved);
  expect(rebuilt.secondaryCues[2].id==countId &&
         close(rebuilt.secondaryCues[2].timeSeconds,.025+5*60./116),
         "correcting the grid may refine an interval-based proposal without changing source evidence identity");
  juce::String error;
  expect(moveGuideSecondaryCue(draft,countId,2.62,false,error) &&
         appendConfirmedGuideSuggestions({},draft).error.isEmpty(),
         "interval-based counts retain ordinary visual adjustment and atomic acceptance");
  auto invalid=transcript;
  invalid.segments[2].words[2].startSeconds=2.8;
  invalid.segments[2].words[2].endSeconds=3.7;
  auto rejected=alignGuideSections(invalid,options);
  expect(rejected.secondaryCues.size()==2 && rejected.suggestions[0].type==SectionType::unknown,
         "two possible pulses preserve the entire compound for review instead of guessing one");
  invalid=transcript; invalid.segments[2].words[1].startSeconds=2.7;
  rejected=alignGuideSections(invalid,options);
  expect(rejected.secondaryCues.size()==2,
         "a count interval with no pulse cannot manufacture a missing beat");
  invalid=transcript; invalid.segments[2].words[1].text="cinco,";
  expect(alignGuideSections(invalid,options).secondaryCues.size()==2,
         "word evidence inconsistent with the parent's literal transcript cannot replace it");
  invalid=transcript; invalid.segments[2].words[1].confidence=.1;
  expect(alignGuideSections(invalid,options).secondaryCues.size()==2,
         "weak word evidence does not produce confident counts from a tempo assumption");
  invalid=transcript; invalid.segments[2].words.clear();
  expect(alignGuideSections(invalid,options).secondaryCues.size()==2,
         "the old cached phrase without word evidence stays reviewable and is not evenly distributed");
}

void testKnownVoiceEvidenceKeepsRawParentAndOverlappingCounts() {
  auto transcript=guide(14,{{.5,.94,"Uno."},{1.24,3.60,"Two, intro, three, four."}});
  auto &parent=transcript.segments[1];
  parent.language="en";parent.confidence=.8;
  parent.acousticAnnouncements={
      {"2","es","known-count-two",1.25,1.71,.99},
      {"Intro","es","known-intro",2.0,2.52,.99},
      {"2","es","known-count-two",2.375,2.835,.99},
      {"3","es","known-count-three",2.75,3.21,.99},
      {"4","es","known-count-four",3.125,3.595,.99}};
  parent.acousticResidualRatio=.01;
  GuideAlignmentOptions options;options.originSeconds=.5;options.bpm=160;
  const auto draft=alignGuideSections(transcript,options);
  expect(draft.error.isEmpty() && draft.suggestions.size()==1 && draft.secondaryCues.size()==5,
         "independent recordings recover a physically present ASR omission and all overlapping counts");
  expect(draft.suggestions[0].displayName=="Intro" && close(draft.suggestions[0].startSeconds,3.5) &&
         draft.suggestions[0].sources.contains("known-guide-recording"),
         "a measured Intro retains the next-bar rule and explicit waveform provenance");
  const double expected[]{.5,1.25,2.375,2.75,3.125};
  for(size_t i=0;i<5;++i)
    expect(close(draft.secondaryCues[i].timeSeconds,expected[i]),
           "acoustic counts retain measured source attacks rather than ASR interval subdivision");
  expect(transcript.segments[1].text=="Two, intro, three, four." &&
         close(transcript.segments[1].startSeconds,1.24) && close(transcript.segments[1].endSeconds,3.60) &&
         transcript.segments[1].words.empty() && transcript.segments[1].confidence==.8 &&
         parent.acousticAnnouncements[1].endSeconds>parent.acousticAnnouncements[2].startSeconds,
         "ASR text, bounds and token confidence stay intact while real vocal overlap remains explicit");
  auto shifted=options;shifted.originSeconds=.505;
  const auto rebuilt=alignGuideSections(transcript,shifted);
  expect(rebuilt.secondaryCues.size()==5 && rebuilt.secondaryCues[2].id==draft.secondaryCues[2].id &&
         close(rebuilt.secondaryCues[2].timeSeconds,2.375),
         "changing the musical origin cannot move a waveform-measured count or its identity");
  for(int failure=0;failure<5;++failure) {
    auto invalid=transcript;
    auto &evidence=invalid.segments[1];
    if(failure==0)evidence.acousticAnnouncements[2].correlation=.5;
    if(failure==1)evidence.acousticResidualRatio=.9;
    if(failure==2)evidence.acousticAnnouncements[2].text="Coro";
    if(failure==3)evidence.acousticAnnouncements[2].startSeconds=evidence.acousticAnnouncements[1].startSeconds;
    if(failure==4)evidence.acousticResidualRatio.reset();
    const auto rejected=alignGuideSections(invalid,options);
    expect(rejected.secondaryCues.size()==1,
           "incomplete, weak or semantically incompatible acoustic evidence keeps the whole ASR parent reviewable");
  }
}
} // namespace
int main() {
  testSpeechNormalization(); testBilingualCatalogAndUtilities(); testBilingualCatalogCacheCompatibility();
  testGuideUtilityNormalization(); testGuideCountNormalization();
  testFusionWithoutGuideAndSafeSnapping();
  testGuideAnticipationAndRepetitions(); testConfirmedGridAndMeters();
  testAcceptancePreservesManualState(); testTimebaseCancellationAndCodec();
  testGuideAlignmentOriginAndMeter(); testGuideAlignmentTempoPhaseAndCapacity();
  testGuideAlignmentReviewAndAnticipation(); testGuideUtilitiesDoNotDivideMusicalSections();
  testGuideMusicalOnsetBoundaryTolerance(); testGuideTimingRegressionAndLateReview();
  testGuideFixedNextBarAcrossTempoAndMeter(); testCorrectedAnnouncementsKeepTheirMusicalRoles();
  testLearnedInterpretationKeepsGuideEvidenceIdentity();
  testRepeatedUnnumberedGuideNames();
  testTentativeAcceptanceUsesCanonicalGuideNames();
  testSecondarySourceTimingAndMoves(); testGuideSecondaryAcceptance(); testGuideSecondaryCapacityAndAtomicity();
  testGuideIndividualCountPoints(); testGuideCompoundCountPreservationAndCapacity();
  testWordCountIntervalsUseUniqueMusicalPulses();
  testKnownVoiceEvidenceKeepsRawParentAndOverlappingCounts();
  testGuideBoundaryEditingAndInvalidInput();
  std::cout << "Section detection: " << checks << " checks passed.\n";
}
