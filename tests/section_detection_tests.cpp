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
GuideCatalogChoice catalogChoice(GuideAnnouncementKind kind, const juce::String &key) {
  for (const auto &choice : guideCatalogEntries())
    if (choice.kind==kind && choice.key==key) return choice;
  expect(false,"the requested test choice must be an existing canonical catalogue entry");
  return {};
}
const GuideGridMark *gridMark(const GuideAlignmentDraft &draft, double seconds) {
  for (const auto &mark : draft.grid) if (close(mark.timeSeconds,seconds)) return &mark;
  return nullptr;
}
void testInitialGuideGridPhaseFromObservedCounts() {
  const double countTimes[]{.02,1.08,2.18,2.73,3.27,3.82};
  const char *countNames[]{"One", "Two", "One", "Two", "Three", "Four"};
  auto transcript=guide(265.27791383219954,{});
  for(size_t i=0;i<6;++i)
    transcript.segments.push_back({countTimes[i],countTimes[i]+.4,countNames[i]});
  GuideAlignmentOptions context;context.bpm=110;
  context.tempoMap.regions={{"source-tempo",0,transcript.durationSeconds,110,"4/4"}};
  const auto initial=makeInitialGuideAlignmentOptions(transcript,context);
  expect(close(initial.originSeconds,0) && close(initial.bpm,110) && initial.timeSignature=="4/4" &&
         initial.tempoMap.regions.size()==1 && close(initial.tempoMap.regions[0].bpm,110),
         "six actual count attacks recover a small initial phase error without replacing musical context");
  const auto draft=alignGuideSections(transcript,initial);
  expect(draft.error.isEmpty() && draft.secondaryCues.size()==6,
         "initial phase refinement preserves every observed count rather than manufacturing another pulse");
  for(size_t i=0;i<6;++i)
    expect(close(draft.secondaryCues[i].timeSeconds,countTimes[i]) &&
           close(draft.secondaryCues[i].rawTimeSeconds,countTimes[i]) &&
           close(transcript.segments[i].startSeconds,countTimes[i]),
           "phase estimation never changes measured count positions or the raw transcript");

  auto measuredCompound=guide(12,{{.02,4.26,"one two one two three four"}});
  measuredCompound.segments[0].acousticResidualRatio=.01;
  const char *numericNames[]{"1","2","1","2","3","4"};
  for(size_t i=0;i<6;++i)
    measuredCompound.segments[0].acousticAnnouncements.push_back({numericNames[i],"en", "known-count-"+juce::String(i),
        countTimes[i],countTimes[i]+.4,.99});
  GuideAlignmentOptions measuredContext;measuredContext.bpm=110;
  expect(close(makeInitialGuideAlignmentOptions(measuredCompound,measuredContext).originSeconds,0),
         "independently measured count attacks on an intact acoustic parent can support the same initial phase refinement");
  measuredCompound.segments[0].acousticAnnouncements[2].correlation=.5;
  expect(close(makeInitialGuideAlignmentOptions(measuredCompound,measuredContext).originSeconds,.02),
         "invalid acoustic metadata cannot contribute timing votes by masquerading as measured count evidence");

  auto fewer=transcript;fewer.segments.resize(3);
  expect(close(makeInitialGuideAlignmentOptions(fewer,context).originSeconds,.02),
         "fewer than four measured counts cannot certify an initial phase refinement");
  auto inconsistent=guide(12,{{.02,.2,"One"},{.8,1,"Two"},{1.5,1.7,"Three"},{2.1,2.3,"Four"}});
  GuideAlignmentOptions regular;
  expect(close(makeInitialGuideAlignmentOptions(inconsistent,regular).originSeconds,.02),
         "incoherent count attacks retain the first spoken origin rather than snapping arbitrary speech");
  auto excessive=guide(12,{{.08,.3,"One"},{.5,.7,"Two"},{1,1.2,"Three"},{1.5,1.7,"Four"},{2,2.2,"One"}});
  expect(close(makeInitialGuideAlignmentOptions(excessive,regular).originSeconds,.08),
         "a consensus demanding more than forty milliseconds cannot replace the initial origin");
  auto duplicate=guide(12,{{.02,.12,"One"},{.03,.13,"Two"},{.04,.14,"Three"},{.05,.15,"Four"}});
  expect(close(makeInitialGuideAlignmentOptions(duplicate,regular).originSeconds,.02),
         "multiple numbers inside a single pulse cannot supply distinct timing evidence");
  auto changing=context;
  changing.tempoMap.regions={{"before",0,2,110,"4/4"},{"after",2,12,90,"4/4"}};
  expect(close(makeInitialGuideAlignmentOptions(transcript,changing).originSeconds,.02),
         "an initial count crossing tempo changes does not assume a constant pulse for phase estimation");
  auto meterChange=context;
  meterChange.tempoMap.regions={{"before",0,2,110,"4/4"},{"after",2,12,110,"3/4"}};
  expect(close(makeInitialGuideAlignmentOptions(transcript,meterChange).originSeconds,.02),
         "an initial count crossing a meter reset remains available for manual origin review");
  auto words=guide(12,{{.02,2.3,"one two three four"}});
  words.segments[0].words={{"one",.02,.45,.8},{"two",.5,.95,.8},
      {"three",1,1.45,.8},{"four",1.5,1.95,.8}};
  expect(close(makeInitialGuideAlignmentOptions(words,regular).originSeconds,.02),
         "ASR word intervals resolved against the grid cannot circularly estimate that same grid origin");
  auto mixedWords=guide(12,{{.02,.3,"One"},{.5,.8,"Two"},{1,1.3,"Three"},
      {1.5,1.8,"Four"},{2,2.3,"One"},{2.5,4.0,"two three four"}});
  mixedWords.segments.back().words={{"two",2.52,2.9,.8},{"three",3.02,3.4,.8},{"four",3.52,3.9,.8}};
  expect(close(makeInitialGuideAlignmentOptions(mixedWords,regular).originSeconds,.02),
         "a compound already resolved by word intervals protects its existing count pulses even with enough atomic count evidence");
  expect(close(makeInitialGuideAlignmentOptions(guide(12,{{.37,.9,"Verse"}}),regular).originSeconds,.37) &&
         close(makeInitialGuideAlignmentOptions(guide(12,{}),regular).originSeconds,0),
         "a guide without measured counts retains the first spoken occurrence or zero for an empty transcript");

  auto offset=transcript;
  offset.durationSeconds+=2.5;
  for(auto &segment:offset.segments){segment.startSeconds+=2.5;segment.endSeconds+=2.5;}
  regular.bpm=110;
  expect(close(makeInitialGuideAlignmentOptions(offset,regular).originSeconds,2.5),
         "phase refinement supports a nonzero musical origin rather than imposing source zero globally");

  GuideAlignmentOptions manual=context;manual.originSeconds=.02;
  auto boundary=transcript;boundary.segments.push_back({10.9,11.56,"Verse"});
  const auto explicitDraft=alignGuideSections(boundary,manual);
  expect(explicitDraft.error.isEmpty() && close(explicitDraft.options.originSeconds,.02) &&
         close(explicitDraft.suggestions[0].startSeconds,.02+5*240.0/110),
         "an explicit origin passed to alignment remains authoritative even when count evidence could refine initial defaults");
  const auto outside=alignGuideSections(guide(12,{{3.965,4.5,"Chorus"}}),{});
  expect(close(outside.suggestions[0].startSeconds,4),
         "the fixed-origin thirty-five-millisecond boundary regression keeps its original following-bar classification");
}

void testGloriosoDiaEntrancesPreserveCounts() {
  const double countTimes[]{.02,1.08,2.18,2.73,3.27,3.82};
  const char *countNames[]{"One", "Two", "One", "Two", "Three", "Four"};
  const double mainTimes[]{10.9,28.36,37.08,54.54,63.26,80.72,93.8,102.54,119.98,
      128.72,146.18,159.26,176.72,194.18,209.44,226.9,242.18,244.36,253.08};
  const int entryBarIndices[]{6,14,18,26,30,38,44,48,56,60,68,74,82,90,97,105,112,113,117};
  const double instructionTimes[]{68.72,168,195.28,237.8};
  const char *instructionNames[]{"All In","Breakdown","Build","Breakdown"};
  auto transcript=guide(265.27791383219954,{});
  for(size_t i=0;i<6;++i)transcript.segments.push_back({countTimes[i],countTimes[i]+.4,countNames[i]});
  for(double time:mainTimes)transcript.segments.push_back({time,time+.4,"Verse"});
  for(size_t i=0;i<4;++i)transcript.segments.push_back({instructionTimes[i],instructionTimes[i]+.4,instructionNames[i]});
  std::sort(transcript.segments.begin(),transcript.segments.end(),[](const auto &left,const auto &right){
    return left.startSeconds<right.startSeconds;
  });
  GuideAlignmentOptions context;context.bpm=110;
  context.tempoMap.regions={{"unchanged-source-tempo",0,transcript.durationSeconds,110,"4/4"}};
  const auto options=makeInitialGuideAlignmentOptions(transcript,context);
  auto draft=alignGuideSections(transcript,options);
  expect(draft.error.isEmpty() && draft.suggestions.size()==19 && draft.secondaryCues.size()==10 &&
         close(options.originSeconds,0),
         "the real count and onset timing regression creates nineteen entrances, six counts and four independent instructions");
  const double bar=240.0/110;
  for(size_t i=0;i<19;++i)
    expect(close(draft.suggestions[i].startSeconds,entryBarIndices[i]*bar) &&
           close(draft.suggestions[i].rawStartSeconds,mainTimes[i]),
           "every reported primary enters at the following musical bar while preserving its original announcement time");
  for(size_t i=0;i<6;++i)
    expect(close(draft.secondaryCues[i].timeSeconds,countTimes[i]) &&
           close(draft.secondaryCues[i].rawTimeSeconds,countTimes[i]),
           "all six correct count cues remain exactly where the original voice pronounced them");
  for(size_t i=0;i<4;++i)
    expect(draft.secondaryCues[i+6].name==instructionNames[i] &&
           close(draft.secondaryCues[i+6].timeSeconds,instructionTimes[i]) &&
           close(draft.secondaryCues[i+6].rawTimeSeconds,instructionTimes[i]),
           "midbar and near-boundary instructions retain their measured spoken times without musical-entry quantization");
  const auto plan=appendConfirmedGuideSuggestions({},draft);
  expect(plan.error.isEmpty() && plan.arrangement.parts.size()==19 && plan.arrangement.cues.size()==10,
         "corrected entrances and unchanged counts confirm together through the existing additive transaction");
  for(size_t i=0;i<6;++i)
    expect(close(resolveArrangementCueSeconds(plan.arrangement,plan.arrangement.cues[i]),countTimes[i]),
           "confirmed counts preserve their absolute source positions across section-relative anchoring");
  for(size_t i=0;i<4;++i)
    expect(close(resolveArrangementCueSeconds(plan.arrangement,plan.arrangement.cues[i+6]),instructionTimes[i]),
           "instruction spoken times survive native arrangement anchoring without adding a bar delay");

  auto moved=options;moved.originSeconds=.005;
  const auto rebuilt=alignGuideSections(transcript,moved);
  expect(rebuilt.secondaryCues.size()==10 && rebuilt.secondaryCues[6].id==draft.secondaryCues[6].id &&
         close(rebuilt.secondaryCues[6].rawTimeSeconds,68.72) &&
         close(rebuilt.secondaryCues[6].timeSeconds,68.72),
         "rebuilding from a manually moved grid preserves utility identity and its exact source position");
  juce::String error;
  expect(moveGuideSecondaryCue(draft,draft.secondaryCues[6].id,70.123,false,error) &&
         close(draft.secondaryCues[6].timeSeconds,70.123) && close(draft.secondaryCues[6].rawTimeSeconds,68.72),
         "explicit free adjustment remains available while preserving the instruction source evidence");
}

void testInstructionsKeepSourceOnsetsAtBoundariesAndSourceEnd() {
  for(double onset:{3.965,3.98,3.985,4.0,4.015,4.02}) {
    const auto draft=alignGuideSections(guide(12,{{onset,onset+.3,"All In"}}),{});
    expect(draft.error.isEmpty() && draft.suggestions.empty() && draft.secondaryCues.size()==1 &&
           close(draft.secondaryCues[0].timeSeconds,onset) && close(draft.secondaryCues[0].rawTimeSeconds,onset),
           "an instruction stays at its spoken onset on either side of a bar without section-boundary snapping");
  }
  const auto late=alignGuideSections(guide(9.9,{{9.8,9.85,"Build"}}),{});
  expect(late.error.isEmpty() && late.timingIssues.empty() && late.secondaryCues.size()==1 &&
         close(late.secondaryCues[0].rawTimeSeconds,9.8) && close(late.secondaryCues[0].timeSeconds,9.8),
         "a valid spoken instruction near EOF needs no unavailable following bar");
  const auto accepted=appendConfirmedGuideSuggestions({},late);
  expect(accepted.error.isEmpty() && accepted.arrangement.parts.empty() && accepted.arrangement.cues.size()==1 &&
         close(accepted.arrangement.cues[0].positionSec,9.8),
         "a late instruction confirms at its measured source position without clamping or inventing a musical section");
}

void testSecondaryReviewPreservesUnresolvedPrincipalErrors() {
  const auto transcript=guide(9.9,{{.1,.4,"One"},{1,1.4,"Verse"},
      {9.05,9.25,"Build"},{9.35,9.55,"Final"},{9.7,9.85,"All In"}});
  auto draft=alignGuideSections(transcript,{});
  expect(canReviewGuideAlignment(draft) && draft.timingIssues.size()==1 &&
         !draft.timingIssues[0].secondary && draft.error.contains("Final") && draft.secondaryCues.size()==3,
         "only a principal without a following bar remains unresolved while late spoken instructions remain reviewable");
  const auto buildId=draft.secondaryCues[1].id, allInId=draft.secondaryCues[2].id;
  juce::String error;
  expect(moveGuideSecondaryCue(draft,buildId,8.5,false,error) &&
         discardGuideSecondaryCue(draft,allInId,error) && draft.timingIssues.size()==1 &&
         draft.error.contains("Final") && close(draft.secondaryCues[0].timeSeconds,.1) &&
         close(draft.secondaryCues[1].rawTimeSeconds,9.05) && close(draft.secondaryCues[1].timeSeconds,8.5) &&
         appendConfirmedGuideSuggestions({},draft).error.isNotEmpty(),
         "editing and discarding independent points cannot hide a principal timing failure or move a count");
  expect(!discardGuideSecondaryCue(draft,allInId,error) && draft.secondaryCues.size()==2 && draft.timingIssues.size()==1,
         "a stale discarded ID cannot remove another point or clear the remaining principal error");
  auto rebuilt=alignGuideSections(transcript,{});
  expect(canReviewGuideAlignment(rebuilt) && moveGuideSecondaryCue(rebuilt,buildId,8.5,false,error) &&
         discardGuideSecondaryCue(rebuilt,allInId,error) && rebuilt.timingIssues.size()==1 && rebuilt.error.contains("Final"),
         "explicit point edits can be reapplied by source ID without suppressing unresolved musical entries");
  auto alias=alignGuideSections(transcript,{});
  expect(discardGuideSecondaryCue(alias,alias.secondaryCues[1].id,error) && alias.secondaryCues.size()==2 &&
         alias.timingIssues.size()==1 && alias.error.contains("Final"),
         "discard is safe when its ID references the point being removed and preserves the principal issue");
  auto fatal=alignGuideSections(transcript,{});fatal.error="Unrelated structural failure";
  expect(!canReviewGuideAlignment(fatal) && !moveGuideSecondaryCue(fatal,buildId,8.5,false,error) &&
         !discardGuideSecondaryCue(fatal,allInId,error) && fatal.error=="Unrelated structural failure" &&
         fatal.timingIssues.size()==1 && fatal.secondaryCues.size()==3,
         "point review cannot erase or bypass unrelated validation failures");
  auto invalid=alignGuideSections(transcript,{});invalid.grid[0].timeSeconds=std::numeric_limits<double>::quiet_NaN();
  expect(!canReviewGuideAlignment(invalid) && !moveGuideSecondaryCue(invalid,buildId,8.5,false,error) &&
         !discardGuideSecondaryCue(invalid,allInId,error),
         "independent instruction review cannot make an invalid musical grid editable");
  auto hidden=alignGuideSections(transcript,{});hidden.error.clear();
  expect(!canReviewGuideAlignment(hidden) && appendConfirmedGuideSuggestions({},hidden).error.isNotEmpty(),
         "clearing an error label cannot bypass the unresolved principal timing state");
}

void testAlabaSecondaryAnnouncementsKeepSpokenTiming() {
  // Instruction and surrounding section times come from ALABA. Independent
  // atomic count controls isolate this timing regression from compound-word
  // alignment: the previous next-bar policy delayed Drums In by about two
  // quarter notes at 127 BPM.
  const double countTimes[]{0,.94,1.88,2.36,2.82,3.30};
  auto transcript=guide(296.7045804988662,{{0,.44,"Uno."},{.94,1.4,"DOS"},
      {1.88,2.28,"Uno"},{2.36,2.76,"Dos"},{2.82,3.22,"Tres"},{3.30,3.70,"Cuatro"},
      {183.3,184,"Coro uno"},{191.8,192.86,juce::String::fromUTF8("Entra a batería.")},
      {195.58,196.76,"Submit intensity."},{198.42,199,"Coro uno"}});
  transcript.segments[8].confidence=.56852166226233;
  transcript.segments[8].alternatives={{"subir intensidad","es",-.902326901753744,1.456069639971247e-6},
      {"Submit intensity.","en",-.56471586227417,2.971145534047537e-7}};
  GuideAlignmentOptions options;options.bpm=127;
  auto draft=alignGuideSections(transcript,options);
  expect(draft.error.isEmpty() && draft.suggestions.size()==2 && draft.secondaryCues.size()==7 &&
         draft.pendingAnnouncements.size()==1 && !draft.pendingAnnouncements[0].candidates.empty() &&
         draft.pendingAnnouncements[0].candidates.front().key=="Build" &&
         !draft.pendingAnnouncements[0].candidates.front().supported &&
         appendConfirmedGuideSuggestions({},draft).error.isNotEmpty(),
         "ALABA's weak Spanish alternative ranks Build for explicit selection without creating an intensity section");
  const auto pendingId=draft.pendingAnnouncements[0].id;
  juce::String choiceError;
  expect(chooseGuideCatalogMark(draft,pendingId,catalogChoice(GuideAnnouncementKind::instruction,"Build"),choiceError) &&
         draft.pendingAnnouncements.empty() && draft.secondaryCues.size()==8 && draft.suggestions.size()==2,
         "explicitly choosing Build resolves the uncertain announcement into a secondary point only");
  const double pulse=60.0/127;
  for(size_t i=0;i<6;++i)
    expect(close(draft.secondaryCues[i].timeSeconds,countTimes[i]),
           "the corrected instruction policy preserves the six independent count positions exactly");
  const auto &drums=draft.secondaryCues[6];
  const auto &build=draft.secondaryCues[7];
  expect(drums.name=="Drums In" && close(drums.timeSeconds,191.8) && close(drums.rawTimeSeconds,191.8) &&
         drums.rawText==juce::String::fromUTF8("Entra a batería."),
         "Entra a bateria uses its exact spoken source time instead of the next bar two quarter notes later");
  expect(build.name=="Build" && close(build.timeSeconds,195.58) && close(build.rawTimeSeconds,195.58) &&
         build.rawText=="Submit intensity.",
         "Submit intensity becomes Build at its measured onset while the raw Whisper phrase remains intact");
  expect(close(draft.suggestions[0].startSeconds,98*4*pulse) &&
         close(draft.suggestions[1].startSeconds,106*4*pulse),
         "repairing an intensity instruction leaves both surrounding principal next-bar entries intact");
  const auto accepted=appendConfirmedGuideSuggestions({},draft);
  expect(accepted.error.isEmpty() && accepted.arrangement.parts.size()==2 && accepted.arrangement.cues.size()==8 &&
         close(resolveArrangementCueSeconds(accepted.arrangement,accepted.arrangement.cues[6]),191.8) &&
         close(resolveArrangementCueSeconds(accepted.arrangement,accepted.arrangement.cues[7]),195.58),
         "native confirmation preserves both exact instruction times without creating an intensity section");
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
  expect(draft.error.isEmpty() && draft.suggestions.size()==1 && draft.pendingAnnouncements.size()==3 &&
         draft.secondaryCues.empty() && draft.suggestions[0].type==SectionType::chorus,
         "unresolved speech and unsplit count phrases remain pending points without becoming musical boundaries");
  expect(draft.pendingAnnouncements[1].rawText=="anuncio sin etiqueta" &&
         draft.pendingAnnouncements[2].rawText=="Perso dos" &&
         draft.pendingAnnouncements[1].candidates.size()==guideCatalogEntries().size() &&
         appendConfirmedGuideSuggestions({},draft).error.isNotEmpty(),
         "pending speech preserves its source text and offers the whole catalogue while blocking automatic confirmation");
  const auto fingerprintId=draft.pendingAnnouncements[1].id;
  auto changed=transcript;changed.audioFingerprint="changed-audio";
  expect(alignGuideSections(changed,{}).pendingAnnouncements[1].id!=fingerprintId,
         "source fingerprint participates in stable pending identities");
  for(double origin:{.25,.5,.75}) {
    GuideAlignmentOptions options;options.originSeconds=origin;
    const auto result=alignGuideSections(guide(12,{{2.2,2.4,"Coro"}}),options);
    expect(result.error.isEmpty() && close(result.suggestions[0].startSeconds,origin+2),
           "known principal entries keep their following-bar policy when the origin changes");
  }
  draft=alignGuideSections(guide(12,{{1.1,1.3,"Coro"},{1.4,1.6,"Puente"},{4.1,4.3,"Coro"},{4.4,4.7,"Coro!"}}),{});
  expect(draft.error.isEmpty() && draft.suggestions.size()==1 && draft.pendingAnnouncements.size()==2 &&
         draft.pendingAnnouncements[0].rawText=="Coro" && draft.pendingAnnouncements[1].rawText=="Puente",
         "different labels colliding at one musical entry remain separate pending source points");
  expect(draft.suggestions[0].type==SectionType::chorus && draft.suggestions[0].reason.contains("Coro!") &&
         draft.suggestions[0].sources.contains(juce::String::fromUTF8(mergedGuideAnnouncementsSource)),
         "matching labels at the same entry can still merge their compatible evidence");
  expect(appendConfirmedGuideSuggestions({},draft).error.isNotEmpty(),
         "a compatible merged section cannot bypass other unresolved collision points");
  draft=alignGuideSections(guide(9.9,{{9.7,9.8,"Final"}}),{});
  expect(draft.error.isNotEmpty() && draft.suggestions.empty(),
         "a late known principal is not moved backwards to force a following-bar entry");
  const auto counts=alignGuideSections(guide(12,{{1,2,"uno dos tres cuatro"},{3,4,"1, 2, 3, 4"}}),{});
  expect(counts.error.isEmpty() && counts.suggestions.empty() && counts.secondaryCues.empty() &&
         counts.pendingAnnouncements.size()==2 && appendConfirmedGuideSuggestions({},counts).error.isNotEmpty(),
         "compound counts without temporal evidence remain pending instead of free-text cues or invented pulses");
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
         close(draft.secondaryCues[1].timeSeconds,74.78) &&
         close(draft.secondaryCues[1].rawTimeSeconds,74.78),
         "counts and utilities retain their exact source times during musical boundary correction");
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
           "corrected counts and utilities keep their spoken onsets without becoming musical sections");
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
  expect(original.error.isEmpty() && interpreted.error.isEmpty() && original.suggestions.empty() &&
         original.pendingAnnouncements.size()==1 && interpreted.suggestions.size()==1 &&
         interpreted.suggestions[0].type==SectionType::bridge && interpreted.suggestions[0].displayName=="Puente",
         "a supported learned family resolves a pending phrase into an existing catalogue section");
  expect(original.pendingAnnouncements[0].id==interpreted.suggestions[0].id &&
         close(interpreted.suggestions[0].rawStartSeconds,3.985) &&
         close(interpreted.suggestions[0].startSeconds,6) &&
         transcript.segments[0].text=="frase de ensayo" && close(transcript.segments[0].startSeconds,3.985),
         "learned interpretation retains the pending source identity, timing and literal ASR text");
  const auto tentative=alignGuideSections(guide(16,{{1.1,1.7,"Prefrain"}}),{});
  expect(tentative.error.isEmpty() && tentative.suggestions.empty() && tentative.pendingAnnouncements.size()==1 &&
         tentative.pendingAnnouncements[0].rawText=="Prefrain" &&
         std::any_of(tentative.pendingAnnouncements[0].candidates.begin(),tentative.pendingAnnouncements[0].candidates.end(),
             [](const auto &choice){return choice.kind==GuideAnnouncementKind::section && choice.type==SectionType::refrain;}),
         "a fuzzy family is a pending catalogue choice rather than an automatically created section");
  const auto confirmedAliasTranscript=guide(16,{{1.1,1.7,"Foraframe!"}});
  const auto confirmedAlias=alignGuideSections(confirmedAliasTranscript,{});
  expect(confirmedAlias.error.isEmpty() && confirmedAlias.suggestions.size()==1 && confirmedAlias.pendingAnnouncements.empty() &&
         confirmedAlias.suggestions[0].type==SectionType::refrain &&
         confirmedAlias.suggestions[0].displayName=="Refrain" &&
         confirmedAlias.suggestions[0].sources.contains("confirmed-alias") &&
         confirmedAlias.suggestions[0].id==guideSectionSuggestionId(confirmedAliasTranscript,confirmedAliasTranscript.segments[0]) &&
         close(confirmedAlias.suggestions[0].rawStartSeconds,1.1) && confirmedAliasTranscript.segments[0].text=="Foraframe!",
         "the previously confirmed ASR alias remains an existing canonical family without modifying its evidence");
}
void testRepeatedUnnumberedGuideNames() {
  const auto transcript=guide(44,{{1.1,1.7,"Intro"},{5.1,5.7,"Intro"},
      {9.1,9.7,"Interludio"},{13.1,13.7,"Interludio"},
      {17.1,17.7,"Verso"},{21.1,21.7,"Verso"},
      {25.1,25.7,"Refrain"},{29.1,29.7,"Refrain"},
      {33.1,33.7,"Verse two"},{37.1,37.7,"Intro dos"}});
  auto draft=alignGuideSections(transcript,{});
  const juce::StringArray expected{"Intro","Intro","Interludio","Interludio",
      "Verso 1","Verso 2","Refrain 1","Refrain 2","Verso 2"};
  expect(draft.error.isEmpty() && draft.suggestions.size()==size_t(expected.size()) &&
         draft.pendingAnnouncements.size()==1 && draft.pendingAnnouncements[0].rawText=="Intro dos",
         "known families and allowed variants align while an unsupported Intro ordinal remains pending");
  for(size_t i=0;i<draft.suggestions.size();++i)
    expect(draft.suggestions[i].displayName==expected[int(i)] &&
           close(draft.suggestions[i].rawStartSeconds,transcript.segments[i].startSeconds),
           "repeated unnumbered families and supported explicit ordinals preserve their source identities");
  expect(appendConfirmedGuideSuggestions({},draft).error.isNotEmpty(),
         "an unsupported ordinal is not silently accepted alongside otherwise known sections");
  juce::String error;
  expect(discardGuidePendingAnnouncement(draft,draft.pendingAnnouncements[0].id,error),
         "an unsupported source ordinal can be explicitly discarded without removing known sections");
  const auto plan=appendConfirmedGuideSuggestions({},draft);
  expect(plan.error.isEmpty() && plan.arrangement.parts.size()==draft.suggestions.size(),
         "known repeated names confirm after the unsupported source announcement is resolved");
  for(size_t i=0;i<plan.arrangement.parts.size();++i)
    expect(plan.arrangement.parts[i].name==expected[int(i)] && plan.arrangement.parts[i].id==draft.suggestions[i].id,
           "confirmed canonical names preserve their occurrence labels and source identities");
}
void testPendingCatalogueSelectionsUseCanonicalGuideNames() {
  auto draft=alignGuideSections(guide(16,{{1.1,1.7,"Prefrain"},{7.1,7.7,"Prefrain"}}),{});
  expect(draft.error.isEmpty() && draft.suggestions.empty() && draft.pendingAnnouncements.size()==2 &&
         appendConfirmedGuideSuggestions({},draft).error.isNotEmpty(),
         "repeated fuzzy candidates cannot become accepted sections without an explicit catalogue choice");
  const auto firstId=draft.pendingAnnouncements[0].id,secondId=draft.pendingAnnouncements[1].id;
  const auto choice=catalogChoice(GuideAnnouncementKind::section,"Refrain");
  juce::String error;
  expect(chooseGuideCatalogMark(draft,firstId,choice,error) && draft.pendingAnnouncements.size()==1 &&
         appendConfirmedGuideSuggestions({},draft).error.isNotEmpty(),
         "choosing one fuzzy announcement leaves the other unresolved and blocking");
  expect(chooseGuideCatalogMark(draft,secondId,choice,error) && draft.pendingAnnouncements.empty() &&
         draft.suggestions.size()==2 && draft.suggestions[0].displayName=="Refrain 1" &&
         draft.suggestions[1].displayName=="Refrain 2",
         "explicit catalogue choices create numbered canonical occurrences with stable source IDs");
  const auto confirmed=appendConfirmedGuideSuggestions({},draft);
  expect(confirmed.error.isEmpty() && confirmed.arrangement.parts.size()==2 &&
         confirmed.arrangement.parts[0].name=="Refrain 1" && confirmed.arrangement.parts[1].name=="Refrain 2" &&
         confirmed.arrangement.parts[0].id==firstId && close(draft.suggestions[0].rawStartSeconds,1.1),
         "confirmation persists only canonical guide names after all pending decisions are explicit");
  expect(moveGuideSectionBoundary(draft,secondId,9,false,error) && draft.suggestions[1].userEdited &&
         close(draft.suggestions[1].startSeconds,9),
         "a catalogue-selected boundary remains editable before confirmation");
  const auto moved=appendConfirmedGuideSuggestions({},draft);
  expect(moved.error.isEmpty() && close(moved.arrangement.parts[1].startSec,9),
         "confirmation preserves the explicit boundary adjustment of a selected canonical section");
  auto single=alignGuideSections(guide(16,{{1.1,1.7,"Prefrain"}}),{});
  expect(chooseGuideCatalogMark(single,single.pendingAnnouncements[0].id,choice,error) &&
         appendConfirmedGuideSuggestions({},single).arrangement.parts[0].name=="Refrain",
         "one explicitly selected family persists without manufacturing an ordinal");
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
         close(draft.secondaryCues[1].timeSeconds,4.2) && close(draft.secondaryCues[0].rawTimeSeconds,1.6) &&
         close(draft.secondaryCues[1].rawTimeSeconds,4.2),
         "known utilities retain independent measured onsets and do not divide musical sections");
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
           close(rebuilt.secondaryCues[0].timeSeconds,1.6) &&
           close(rebuilt.secondaryCues[1].timeSeconds,4.2) &&
           close(rebuilt.secondaryCues[0].rawTimeSeconds,1.6) && close(rebuilt.secondaryCues[1].rawTimeSeconds,4.2),
           "changing the bar origin cannot shift utility source onsets or their stable identities");
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
    expect(draft.secondaryCues[i].name==names[int(i)] &&
           close(draft.secondaryCues[i].timeSeconds,times[i]) &&
           close(draft.secondaryCues[i].rawTimeSeconds,times[i]),
           "counts and utilities retain their measured onsets without adding absent counts or bar delays");
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
             "count and utility IDs and source positions remain independent of the chosen origin");
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
  auto compound=alignGuideSections(guide(12,{{0,2,"uno dos tres cuatro"},{3,4,"ocho"},
      {5,6,"Count-in"},{7,8,"1, 2, 3, 4"},{9,10,"0"}}),{});
  expect(compound.error.isEmpty() && compound.suggestions.empty() && compound.secondaryCues.empty() &&
         compound.pendingAnnouncements.size()==5,
         "unsplit, generic and unsupported count announcements remain pending instead of free-text playback cues");
  expect(appendConfirmedGuideSuggestions({},compound).error.isNotEmpty(),
         "pending count text cannot be confirmed as a playable free-text event");
  const auto original=compound.pendingAnnouncements;
  juce::String pendingError;
  for(const auto &pending:original)
    expect(discardGuidePendingAnnouncement(compound,pending.id,pendingError),
           "an unsupported count announcement can be explicitly discarded by source ID");
  expect(compound.pendingAnnouncements.empty() && compound.secondaryCues.empty() &&
         appendConfirmedGuideSuggestions({},compound).error.isNotEmpty(),
         "discarding unresolved counts does not fabricate another event or permit an empty acceptance");

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
  expect(rejected.secondaryCues.size()==2 && rejected.suggestions.size()==1 &&
         rejected.pendingAnnouncements.size()==1 && rejected.pendingAnnouncements[0].rawText==transcript.segments[2].text,
         "ambiguous word pulses preserve the intact parent as pending without inventing a musical boundary");
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

void testCountWordBoundariesFollowUniqueEvidencedRun() {
  auto transcript = guide(8, {{1.70, 3.58, "uno, dos, tres, cuatro."}});
  auto &segment = transcript.segments.front();
  segment.words = {{"uno,", 1.71, 1.77, .64}, {"dos,", 1.95, 2.22, .99},
                   {"tres,", 2.42, 2.76, .99}, {"cuatro.", 3.00, 3.43, .98}};
  GuideAlignmentOptions options;
  options.bpm = 140;
  auto draft = alignGuideSections(transcript, options);
  expect(draft.pendingAnnouncements.empty() && draft.secondaryCues.size() == 4,
         "a boundary including the next beat by 1.43 ms resolves when the preceding words anchor one run");
  for (size_t i = 0; i < draft.secondaryCues.size(); ++i)
    expect(close(draft.secondaryCues[i].timeSeconds, double(4 + i) * 60.0 / 140.0) &&
           close(draft.secondaryCues[i].rawTimeSeconds, segment.words[i].startSeconds),
           "count proposals use evidenced grid pulses and preserve the measured word onsets");

  segment.startSeconds = 1.88;
  segment.endSeconds = 3.78;
  segment.words = {{"uno,", 1.89, 1.95, .73}, {"dos,", 2.13, 2.40, .99},
                   {"tres,", 2.58, 2.94, .99}, {"cuatro.", 3.12, 3.68, .98}};
  options.bpm = 127;
  draft = alignGuideSections(transcript, options);
  expect(draft.pendingAnnouncements.empty() && draft.secondaryCues.size() == 4 &&
         close(draft.secondaryCues.front().timeSeconds, 4.0 * 60.0 / 127.0) &&
         close(draft.secondaryCues.back().timeSeconds, 7.0 * 60.0 / 127.0),
         "a word edge 0.236 ms past its beat retains the run supported by three other words");

  segment.words.front().startSeconds = 1.895;
  draft = alignGuideSections(transcript, options);
  expect(draft.secondaryCues.empty() && draft.pendingAnnouncements.size() == 1,
         "a word more than half a timestamp tick from the nearest pulse stays pending");

  auto uncertain = guide(5, {{0, 2.1, "one two three four"}});
  uncertain.segments.front().words = {{"one", 0, .5, .99}, {"two", .5, 1, .99},
                                       {"three", 1, 1.5, .99}, {"four", 1.5, 2, .99}};
  options.bpm = 120;
  draft = alignGuideSections(uncertain, options);
  expect(draft.secondaryCues.empty() && draft.pendingAnnouncements.size() == 1,
         "two equally supported consecutive runs cannot resolve a grouped count");

  uncertain.segments.front().words = {{"one", .001, .20, .99}, {"two", .501, .70, .99},
                                       {"three", 1.001, 1.20, .99}, {"four", 1.501, 1.70, .99}};
  draft = alignGuideSections(uncertain, options);
  expect(draft.secondaryCues.empty() && draft.pendingAnnouncements.size() == 1,
         "rounding every interval cannot by itself prove the phase of a count");
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
void testWithinGuideRepetitionAlignment() {
  GuideTranscriptSegment seed{.5, 1.4, "Sube la intensidad.", "es", .9};
  seed.alternatives = {{seed.text, "es", -.1, .01}};
  GuideTranscriptSegment pending{4.0, 5.9, "Instrumentar, subir, la intensidad.", "es", .7};
  pending.catalogueAlternatives = {{"Instrumental.", "es", -.3, .01}};
  auto transcript = guide(16, {seed, pending});
  GuideAlignmentOptions options;
  options.bpm = 120;
  options.originSeconds = 0.0;
  const auto baseline = alignGuideSections(transcript, options);
  expect(baseline.pendingAnnouncements.size() == 1, "the source utterance requires review before recovery");
  auto &parts = transcript.segments[1].repetitionAnnouncements;
  parts = {{"Instrumental.", "es", 4.0, 5.0, {}, {}}, {seed.text, "es", 5.0, 5.9, 0, .99}};
  const auto recovered = alignGuideSections(transcript, options);
  expect(recovered.error.isEmpty() && recovered.pendingAnnouncements.empty() && recovered.suggestions.size() == 1 &&
         recovered.suggestions.front().displayName == "Instrumental" && close(recovered.suggestions.front().startSeconds, 6.0) &&
         recovered.secondaryCues.size() == 2 && close(recovered.secondaryCues.back().timeSeconds, 5.0),
         "recovering a compound uses a section on the next bar and an instruction at its measured onset");

  auto manuallySelected = options;
  manuallySelected.catalogueSelections.push_back({baseline.pendingAnnouncements.front().id,
                                                 catalogChoice(GuideAnnouncementKind::instruction, "Breakdown")});
  const auto manual = alignGuideSections(transcript, manuallySelected);
  expect(manual.suggestions.empty() && manual.secondaryCues.size() == 2 && manual.secondaryCues.back().name == "Breakdown" &&
         close(manual.secondaryCues.back().timeSeconds, 4.0),
         "an explicit owner selection on the original parent takes priority over automatic recovery");
  auto competing = transcript;
  competing.segments[0].alternatives.push_back({"Breakdown", "en", -.24, .01});
  auto ambiguous = alignGuideSections(competing, options);
  expect(ambiguous.pendingAnnouncements.size() == 1 && ambiguous.suggestions.empty(),
         "a plausible opposite-direction reading prevents reusing that voice as a reference");
  competing.segments[0].alternatives.back().avgLogProbability = -.4;
  expect(alignGuideSections(competing, options).pendingAnnouncements.empty(),
         "a competing meaning below the ordinary decoder support margin does not block a proven repetition");
  auto stretched = transcript;
  stretched.segments[0].endSeconds -= .1;
  expect(alignGuideSections(stretched, options).pendingAnnouncements.size() == 1,
         "a repeated child cannot be longer than the original recording it cites");
  auto incomplete = transcript;
  auto &explicitCompound = incomplete.segments[1];
  explicitCompound.text = "Intro, Sube la intensidad.";
  explicitCompound.endSeconds = 4.9;
  explicitCompound.repetitionAnnouncements = {{seed.text, "es", 4.0, 4.9, 0, .99}};
  expect(alignGuideSections(incomplete, options).pendingAnnouncements.size() == 1,
         "a known two-part announcement cannot be consumed by one matching instruction");

  parts.front().text = "Puente.";
  const auto conflict = alignGuideSections(transcript, options);
  expect(conflict.pendingAnnouncements.size() == 1 && conflict.suggestions.empty() && conflict.secondaryCues.size() == 1,
         "a partial or conflicting split keeps the full original utterance for review");
}

void testCatalogueRoleSelectionPreservesSourceAndManualEdits() {
  const auto transcript=guide(30,{{.1,.4,"One"},{1.1,1.7,"unidentified announcement"},
      {9.1,9.6,"Verse"},{17.1,17.7,"Chorus"}});
  auto draft=alignGuideSections(transcript,{});
  expect(draft.pendingAnnouncements.size()==1 && draft.suggestions.size()==2 && draft.secondaryCues.size()==1,
         "an unknown source remains pending beside known musical and count events");
  const auto id=draft.pendingAnnouncements[0].id,knownId=draft.suggestions[0].id,countId=draft.secondaryCues[0].id;
  juce::String error;
  expect(moveGuideSectionBoundary(draft,knownId,10.25,false,error),
         "an unrelated known section can be adjusted while another source awaits catalogue selection");
  auto invalid=catalogChoice(GuideAnnouncementKind::instruction,"Build");invalid.key="non-catalogue instruction";
  expect(!chooseGuideCatalogMark(draft,id,invalid,error) && !error.isEmpty() &&
         draft.pendingAnnouncements.size()==1 && draft.options.catalogueSelections.empty() &&
         close(draft.suggestions[0].startSeconds,10.25),
         "an invalid catalogue identity cannot partially consume pending state or alter a manual boundary");
  expect(!chooseGuideCatalogMark(draft,"stale-source-id",catalogChoice(GuideAnnouncementKind::count,"4"),error) &&
         draft.pendingAnnouncements.size()==1 && draft.secondaryCues.size()==1,
         "a stale source ID cannot manufacture a catalogue mark");
  expect(chooseGuideCatalogMark(draft,id,catalogChoice(GuideAnnouncementKind::instruction,"Build"),error) &&
         draft.pendingAnnouncements.empty() && draft.secondaryCues.size()==2 && draft.suggestions.size()==2,
         "a pending phrase can explicitly become an instruction without dividing musical sections");
  auto selected=std::find_if(draft.secondaryCues.begin(),draft.secondaryCues.end(),[&](const auto &cue){return cue.id==id;});
  expect(selected!=draft.secondaryCues.end() && selected->name=="Build" && selected->rawText=="unidentified announcement" &&
         close(selected->rawTimeSeconds,1.1) && close(selected->timeSeconds,1.1),
         "the instruction choice retains the pending source ID, literal phrase and measured time");
  expect(moveGuideSecondaryCue(draft,id,1.23,false,error) &&
         chooseGuideCatalogMark(draft,id,catalogChoice(GuideAnnouncementKind::count,"4"),error),
         "an explicit catalogue role can change while retaining a manually adjusted point");
  selected=std::find_if(draft.secondaryCues.begin(),draft.secondaryCues.end(),[&](const auto &cue){return cue.id==id;});
  expect(selected!=draft.secondaryCues.end() && selected->name=="4" && close(selected->timeSeconds,1.23) &&
         close(selected->rawTimeSeconds,1.1) && selected->rawText=="unidentified announcement",
         "switching a selected instruction to a count preserves its explicit edit and source evidence");
  expect(chooseGuideCatalogMark(draft,id,catalogChoice(GuideAnnouncementKind::section,"Bridge 2"),error) &&
         draft.suggestions.size()==3 && draft.secondaryCues.size()==1 && draft.secondaryCues[0].id==countId &&
         close(draft.secondaryCues[0].timeSeconds,.1),
         "choosing a principal replaces only this source mark while retaining independent counts");
  const auto principal=std::find_if(draft.suggestions.begin(),draft.suggestions.end(),[&](const auto &item){return item.id==id;});
  const auto manual=std::find_if(draft.suggestions.begin(),draft.suggestions.end(),[&](const auto &item){return item.id==knownId;});
  expect(principal!=draft.suggestions.end() && principal->type==SectionType::bridge && principal->displayName=="Puente 2" &&
         close(principal->rawStartSeconds,1.1) && close(principal->startSeconds,2) &&
         manual!=draft.suggestions.end() && close(manual->startSeconds,10.25) && manual->userEdited,
         "a principal choice follows musical timing and preserves an unrelated manual section boundary");
  expect(appendConfirmedGuideSuggestions({},draft).error.isEmpty() &&
         transcript.segments[1].text=="unidentified announcement" && close(transcript.segments[1].startSeconds,1.1),
         "fully resolved catalogue choices confirm without rewriting the original transcript");
  auto options=draft.options;options.originSeconds=.25;
  const auto rebuilt=alignGuideSections(transcript,options);
  const auto replayed=std::find_if(rebuilt.suggestions.begin(),rebuilt.suggestions.end(),[&](const auto &item){return item.id==id;});
  expect(rebuilt.pendingAnnouncements.empty() && replayed!=rebuilt.suggestions.end() &&
         replayed->type==SectionType::bridge && close(replayed->startSeconds,2.25) && close(replayed->rawStartSeconds,1.1),
         "a source-ID catalogue choice survives rebuilding with an explicitly changed origin");
}

void testWordCountCatalogueOverrideSurvivesOriginRebuild() {
  auto transcript=guide(12,{{0,1.9,"one two three four"},{7.1,7.6,"Verse"}});
  transcript.segments[0].words={{"one",0,.4,.8},{"two",.45,.95,.8},
      {"three",.96,1.45,.8},{"four",1.46,1.9,.8}};
  auto draft=alignGuideSections(transcript,{});
  expect(draft.pendingAnnouncements.empty() && draft.secondaryCues.size()==4,
         "complete word evidence creates four supported count points before an explicit role correction");
  const auto id=draft.secondaryCues[1].id;
  juce::String error;
  expect(chooseGuideCatalogMark(draft,id,catalogChoice(GuideAnnouncementKind::instruction,"Drums In"),error),
         "a supported word-count point can be explicitly reclassified through the catalogue");
  const auto changed=std::find_if(draft.secondaryCues.begin(),draft.secondaryCues.end(),[&](const auto &cue){return cue.id==id;});
  expect(changed!=draft.secondaryCues.end() && changed->name=="Drums In" && changed->rawText=="two" &&
         close(changed->rawTimeSeconds,.45) && close(changed->timeSeconds,.45),
         "reclassifying a word count uses its measured word boundary rather than the previous proposed pulse");
  auto options=draft.options;options.originSeconds=.01;
  const auto rebuilt=alignGuideSections(transcript,options);
  const auto preserved=std::find_if(rebuilt.secondaryCues.begin(),rebuilt.secondaryCues.end(),[&](const auto &cue){return cue.id==id;});
  expect(rebuilt.pendingAnnouncements.empty() && rebuilt.secondaryCues.size()==4 && preserved!=rebuilt.secondaryCues.end() &&
         preserved->name=="Drums In" && preserved->rawText=="two" && close(preserved->timeSeconds,.45) &&
         close(preserved->rawTimeSeconds,.45) && transcript.segments[0].text=="one two three four" &&
         close(transcript.segments[0].words[1].startSeconds,.45),
         "a word-source catalogue decision survives origin rebuilding without duplicating the original count or changing ASR");
}

void testCatalogueSelectionCapacityAndPendingDiscard() {
  auto draft=alignGuideSections(guide(12,{{1.1,1.7,"unidentified announcement"}}),{});
  const auto id=draft.pendingAnnouncements[0].id;
  for(size_t i=0;i<kMaxArrangementCueEvents;++i)
    draft.secondaryCues.push_back(secondary("capacity-"+juce::String(int(i)),"Build",.01*double(i)));
  juce::String error;
  expect(canReviewGuideAlignment(draft) &&
         !chooseGuideCatalogMark(draft,id,catalogChoice(GuideAnnouncementKind::instruction,"Build"),error) &&
         draft.secondaryCues.size()==kMaxArrangementCueEvents && draft.pendingAnnouncements.size()==1 &&
         draft.pendingAnnouncements[0].id==id && draft.options.catalogueSelections.empty() && !error.isEmpty(),
         "a catalogue selection exceeding cue capacity is rejected atomically without consuming its pending source");
  draft.secondaryCues.pop_back();
  expect(chooseGuideCatalogMark(draft,id,catalogChoice(GuideAnnouncementKind::instruction,"Build"),error) &&
         draft.secondaryCues.size()==kMaxArrangementCueEvents && draft.pendingAnnouncements.empty() &&
         appendConfirmedGuideSuggestions({},draft).error.isEmpty(),
         "the same pending instruction fits exactly at the declared event capacity");
  auto pending=alignGuideSections(guide(12,{{1.1,1.7,"unidentified first"},{5.1,5.7,"unidentified second"},{9.1,9.6,"Verse"}}),{});
  expect(pending.pendingAnnouncements.size()==2 && appendConfirmedGuideSuggestions({},pending).error.isNotEmpty(),
         "unresolved sources cannot be automatically accepted alongside a known principal");
  const auto firstId=pending.pendingAnnouncements[0].id,secondId=pending.pendingAnnouncements[1].id;
  expect(discardGuidePendingAnnouncement(pending,firstId,error) && pending.pendingAnnouncements.size()==1 &&
         pending.pendingAnnouncements[0].id==secondId && appendConfirmedGuideSuggestions({},pending).error.isNotEmpty(),
         "discarding one pending source preserves other unresolved choices and keeps confirmation blocked");
  expect(!discardGuidePendingAnnouncement(pending,firstId,error) && pending.pendingAnnouncements.size()==1 &&
         discardGuidePendingAnnouncement(pending,pending.pendingAnnouncements[0].id,error) &&
         pending.pendingAnnouncements.empty() && pending.suggestions.size()==1 &&
         appendConfirmedGuideSuggestions({},pending).error.isEmpty(),
         "a stale discard is harmless and resolving the final pending source enables canonical-only acceptance");
}

void testManualGuideSectionInsertionAndSourceIsolation() {
  const auto transcript=guide(30,{{.1,.3,"One"},{1,1.4,"Intro"},{9,9.4,"Verse"},{17,17.4,"Bridge"}});
  auto draft=alignGuideSections(transcript,{});
  const auto original=draft;
  juce::String error;
  expect(addGuideSectionAt(draft,catalogChoice(GuideAnnouncementKind::section,"Chorus 2"),6.3,true,error) &&
         error.isEmpty() && draft.suggestions.size()==4 && draft.options.manualSections.size()==1,
         "a canonical missing section can be added provisionally at the nearest musical bar");
  const auto &added=draft.suggestions[1];
  const auto id=added.id;
  expect(close(draft.suggestions[0].startSeconds,2) && close(draft.suggestions[0].endSeconds,6) &&
         close(added.startSeconds,6) && close(added.endSeconds,10) &&
         close(draft.suggestions[2].startSeconds,10) && close(draft.suggestions[3].startSeconds,18),
         "manual insertion splits only the preceding draft range and does not delay the chosen entry by another bar");
  expect(id.startsWith("guide-manual-") && isManualGuideSection(draft,id) &&
         added.type==SectionType::chorus && added.occurrence==2 && added.displayName=="Coro 2" &&
         added.userEdited && added.sources.contains(manualGuideSectionSource) &&
         !added.matchedCueTimeSeconds && !added.guideAnticipationSeconds &&
         draft.options.catalogueSelections.empty(),
         "a user-selected section has its own stable identity and manual provenance without invented spoken evidence");
  expect(draft.secondaryCues.size()==original.secondaryCues.size() &&
         draft.secondaryCues[0].id==original.secondaryCues[0].id &&
         close(draft.secondaryCues[0].timeSeconds,original.secondaryCues[0].timeSeconds) &&
         draft.sourceAnnouncements.size()==original.sourceAnnouncements.size(),
         "adding a musical section neither consumes count points nor manufactures a source announcement");
  for(size_t i=0;i<original.sourceAnnouncements.size();++i)
    expect(draft.sourceAnnouncements[i].id==original.sourceAnnouncements[i].id &&
           draft.sourceAnnouncements[i].rawText==original.sourceAnnouncements[i].rawText &&
           close(draft.sourceAnnouncements[i].startSeconds,original.sourceAnnouncements[i].startSeconds),
           "all original spoken identities, literal text and measured timings survive manual insertion");
  const auto plan=appendConfirmedGuideSuggestions({},draft);
  expect(plan.error.isEmpty() && plan.arrangement.parts.size()==4 && plan.arrangement.cues.size()==1 &&
         plan.arrangement.parts[1].id==id && close(plan.arrangement.parts[1].startSec,6),
         "manual and recognized suggestions use the same explicit atomic acceptance plan");
  expect(guideSectionCorrectionsForConfirmedReview(transcript,{added},{{id,"Coro 2"}}).empty(),
         "confirming a manually added section cannot teach an ASR correction for speech that never existed");

  auto empty=alignGuideSections(guide(12,{}),{});
  expect(addGuideSectionAt(empty,catalogChoice(GuideAnnouncementKind::section,"Intro"),0,false,error) &&
         empty.suggestions.size()==1 && close(empty.suggestions[0].startSeconds,0) &&
         close(empty.suggestions[0].endSeconds,12),
         "a review without recognized primary sections can receive its first section");
  auto before=original;
  expect(addGuideSectionAt(before,catalogChoice(GuideAnnouncementKind::section,"Intro"),0,false,error) &&
         close(before.suggestions[0].endSeconds,2) && before.suggestions[1].id==original.suggestions[0].id,
         "a manual section before the first recognized boundary preserves that original boundary and identity");
}

void testManualGuideSectionRebuildRenameAndDiscard() {
  const auto transcript=guide(30,{{1,1.4,"Intro"},{9,9.4,"Verse"},{17,17.4,"Bridge"}});
  auto draft=alignGuideSections(transcript,{});
  juce::String error;
  expect(addGuideSectionAt(draft,catalogChoice(GuideAnnouncementKind::section,"Chorus"),6,false,error),
         "manual-section rebuild fixture can insert the omitted chorus");
  const auto id=draft.options.manualSections[0].id;
  expect(chooseGuideCatalogMark(draft,id,catalogChoice(GuideAnnouncementKind::section,"Bridge 2"),error) &&
         draft.suggestions[1].displayName=="Puente 2" && close(draft.suggestions[1].startSeconds,6) &&
         draft.options.manualSections[0].choice.key=="Bridge 2" && draft.options.catalogueSelections.empty() &&
         draft.sourceAnnouncements.size()==3,
         "catalogue renaming of a manual section changes only its explicit musical choice without a fictional source");
  expect(!chooseGuideCatalogMark(draft,id,catalogChoice(GuideAnnouncementKind::instruction,"Drums In"),error) &&
         draft.suggestions[1].displayName=="Puente 2" && draft.secondaryCues.empty(),
         "a manual primary section cannot accidentally be reinterpreted as spoken secondary evidence");
  expect(moveGuideSectionBoundary(draft,id,7,false,error) && close(draft.options.manualSections[0].seconds,7),
         "dragging a manual entry also updates its origin-independent rebuild position");
  auto options=draft.options;options.originSeconds=.25;
  const auto rebuilt=alignGuideSections(transcript,options);
  const auto mark=std::find_if(rebuilt.suggestions.begin(),rebuilt.suggestions.end(),[&](const auto &item){return item.id==id;});
  expect(rebuilt.error.isEmpty() && mark!=rebuilt.suggestions.end() && close(mark->startSeconds,7) &&
         close(mark->endSeconds,10.25) && mark->displayName=="Puente 2" && mark->userEdited &&
         !mark->matchedCueTimeSeconds && isManualGuideSection(rebuilt,id),
         "a changed origin rebuilds automatic neighbours while retaining the added identity, name and chosen absolute entry");
  auto collision=draft.options;collision.originSeconds=7;
  const auto rejected=alignGuideSections(transcript,collision);
  expect(rejected.error.isNotEmpty() && !canReviewGuideAlignment(rejected) &&
         draft.options.manualSections.size()==1 && close(draft.suggestions[1].startSeconds,7),
         "a rebuilt automatic boundary colliding with a manual entry is rejected rather than silently deleting that entry");
  options.manualSections.clear();
  const auto discarded=alignGuideSections(transcript,options);
  expect(discarded.error.isEmpty() && discarded.suggestions.size()==3 && !isManualGuideSection(discarded,id) &&
         std::none_of(discarded.suggestions.begin(),discarded.suggestions.end(),[&](const auto &item){return item.id==id;}) &&
         close(discarded.suggestions[0].endSeconds,10.25),
         "removing a manual option before rebuilding discards only that addition and reconnects the automatic ranges");

  auto replacement=alignGuideSections(transcript,{});
  const auto discardedAutomaticId=replacement.suggestions[1].id;
  replacement.suggestions.erase(replacement.suggestions.begin()+1);
  replacement.suggestions[0].endSeconds=replacement.suggestions[1].startSeconds;
  expect(addGuideSectionAt(replacement,catalogChoice(GuideAnnouncementKind::section,"Chorus"),10,false,error),
         "a manual section may occupy the boundary of an automatic suggestion the reviewer already discarded");
  const auto replacementId=replacement.options.manualSections.front().id;
  auto automaticOptions=replacement.options;automaticOptions.manualSections.clear();
  auto replay=alignGuideSections(transcript,automaticOptions);
  replay.suggestions.erase(std::remove_if(replay.suggestions.begin(),replay.suggestions.end(),
      [&](const auto &part){return part.id==discardedAutomaticId;}),replay.suggestions.end());
  replay.suggestions[0].endSeconds=replay.suggestions[1].startSeconds;
  expect(reapplyManualGuideSections(replay,replacement.options.manualSections,error) &&
         replay.options.manualSections.front().id==replacementId && replay.suggestions[1].id==replacementId &&
         close(replay.suggestions[1].startSeconds,10) && replay.suggestions[1].type==SectionType::chorus,
         "filtering discarded automatic entries before replay restores the manual replacement with its original stable ID");
  auto duplicateEntries=replay.options.manualSections;duplicateEntries.push_back(duplicateEntries[0]);
  expect(!reapplyManualGuideSections(replay,duplicateEntries,error) && replay.suggestions.size()==3 &&
         replay.options.manualSections.size()==1 && replay.options.manualSections[0].id==replacementId,
         "invalid replay metadata is rejected without consuming or duplicating an existing manual replacement");
}

void testManualGuideSectionRejectionIsAtomic() {
  auto draft=alignGuideSections(guide(30,{{1,1.4,"Intro"},{9,9.4,"Verse"},{17,17.4,"Bridge"}}),{});
  const auto original=draft;
  const auto sameRanges=[&] {
    if(draft.suggestions.size()!=original.suggestions.size() || !draft.options.manualSections.empty()) return false;
    for(size_t i=0;i<original.suggestions.size();++i)
      if(draft.suggestions[i].id!=original.suggestions[i].id ||
          draft.suggestions[i].startSeconds!=original.suggestions[i].startSeconds ||
          draft.suggestions[i].endSeconds!=original.suggestions[i].endSeconds ||
          draft.suggestions[i].userEdited!=original.suggestions[i].userEdited) return false;
    return true;
  };
  juce::String error;
  const auto choice=catalogChoice(GuideAnnouncementKind::section,"Chorus");
  for(const auto time:{-1.0,2.0,2.1,9.9,29.9,30.0,std::numeric_limits<double>::quiet_NaN()})
    expect(!addGuideSectionAt(draft,choice,time,false,error) && !error.isEmpty() && sameRanges(),
           "invalid or too-close manual insertion leaves all draft ranges, edit flags and rebuild options unchanged");
  auto invalidChoice=choice;invalidChoice.key="not a catalog section";
  expect(!addGuideSectionAt(draft,invalidChoice,6,false,error) && sameRanges() &&
         !addGuideSectionAt(draft,catalogChoice(GuideAnnouncementKind::count,"2"),6,false,error) && sameRanges(),
         "manual primary insertion accepts only canonical section choices, never an unknown name or a count");
  auto full=alignGuideSections(guide(600,{}),{});
  for(size_t i=0;i<maximumSectionSuggestions;++i)
    full.suggestions.push_back(suggestion("capacity-"+juce::String(int(i)),double(i)*2,
        i+1==maximumSectionSuggestions?600:double(i+1)*2));
  expect(!addGuideSectionAt(full,choice,599,false,error) && full.suggestions.size()==maximumSectionSuggestions &&
         full.options.manualSections.empty(),
         "adding beyond the native section capacity cannot partially split a range or retain a phantom manual option");
}

} // namespace
int main() {
  testSpeechNormalization(); testBilingualCatalogAndUtilities(); testBilingualCatalogCacheCompatibility();
  testGuideUtilityNormalization(); testGuideCountNormalization();
  testFusionWithoutGuideAndSafeSnapping();
  testGuideAnticipationAndRepetitions(); testConfirmedGridAndMeters();
  testAcceptancePreservesManualState(); testTimebaseCancellationAndCodec();
  testInitialGuideGridPhaseFromObservedCounts(); testGloriosoDiaEntrancesPreserveCounts();
  testInstructionsKeepSourceOnsetsAtBoundariesAndSourceEnd(); testSecondaryReviewPreservesUnresolvedPrincipalErrors();
  testAlabaSecondaryAnnouncementsKeepSpokenTiming();
  testGuideAlignmentOriginAndMeter(); testGuideAlignmentTempoPhaseAndCapacity();
  testGuideAlignmentReviewAndAnticipation(); testGuideUtilitiesDoNotDivideMusicalSections();
  testGuideMusicalOnsetBoundaryTolerance(); testGuideTimingRegressionAndLateReview();
  testGuideFixedNextBarAcrossTempoAndMeter(); testCorrectedAnnouncementsKeepTheirMusicalRoles();
  testLearnedInterpretationKeepsGuideEvidenceIdentity();
  testRepeatedUnnumberedGuideNames();
  testPendingCatalogueSelectionsUseCanonicalGuideNames();
  testSecondarySourceTimingAndMoves(); testGuideSecondaryAcceptance(); testGuideSecondaryCapacityAndAtomicity();
  testGuideIndividualCountPoints(); testGuideCompoundCountPreservationAndCapacity();
  testWordCountIntervalsUseUniqueMusicalPulses();
  testCountWordBoundariesFollowUniqueEvidencedRun();
  testKnownVoiceEvidenceKeepsRawParentAndOverlappingCounts();
  testWithinGuideRepetitionAlignment();
  testCatalogueRoleSelectionPreservesSourceAndManualEdits();
  testWordCountCatalogueOverrideSurvivesOriginRebuild();
  testCatalogueSelectionCapacityAndPendingDiscard();
  testManualGuideSectionInsertionAndSourceIsolation();
  testManualGuideSectionRebuildRenameAndDiscard();
  testManualGuideSectionRejectionIsAtomic();
  testGuideBoundaryEditingAndInvalidInput();
  std::cout << "Section detection: " << checks << " checks passed.\n";
}
