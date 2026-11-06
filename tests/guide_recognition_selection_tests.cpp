#include "analysis/GuideRecognitionSelection.h"
#include "analysis/GuideSpeechSegmentation.h"
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
GuideRecognitionCandidate hypothesis(const juce::String &text, const juce::String &language,
                                      double logProbability, double noSpeech = 0.02) {
  GuideRecognitionCandidate candidate;
  candidate.segment.startSeconds = language == "en" ? 12.34 : 12.30;
  candidate.segment.endSeconds = language == "en" ? 12.91 : 12.95;
  candidate.segment.text = text; candidate.segment.language = language;
  candidate.avgLogProbability = logProbability;
  candidate.noSpeechProbability = noSpeech; candidate.textTokenCount = 3;
  return candidate;
}
void acousticEvidenceAndLiteralOutput() {
  const auto spanish = hypothesis("Coro uno.", "es", -0.52);
  const auto english = hypothesis("Chorus one!", "en", -0.19);
  const auto selected = selectGuideRecognitionCandidate({spanish, english});
  expect(selected && selected->text == english.segment.text && selected->language == "en",
         "English wins from decoder evidence even when Spanish is also a recognised section");
  expect(selected->startSeconds == english.segment.startSeconds && selected->endSeconds == english.segment.endSeconds,
         "the selected pass retains its own source times rather than averaged boundaries");
  expect(selected->confidence && std::abs(*selected->confidence - std::exp(-0.19)) < 1.0e-10,
         "confidence is the geometric mean text-token probability, not a claimed accuracy score");
  expect(selected->alternatives.size() == 2 && selected->alternatives[0].text == spanish.segment.text &&
             selected->alternatives[1].text == english.segment.text &&
             selected->alternatives[0].avgLogProbability == spanish.avgLogProbability &&
             selected->alternatives[1].noSpeechProbability == english.noSpeechProbability,
         "both literal hypotheses and original decoder evidence remain inspectable");
  const auto reversed = selectGuideRecognitionCandidate({english, spanish});
  expect(reversed && reversed->language == "en" && reversed->text == selected->text,
         "a clear acoustic winner does not depend on which language was processed first");
  const auto raw = hypothesis(juce::String::fromUTF8("¿Qué sucede aquí?"), "es", -0.20);
  const auto preserved = selectGuideRecognitionCandidate({raw, hypothesis("Bridge", "en", -0.8)});
  expect(preserved && preserved->text == raw.segment.text,
         "strong unknown speech is never replaced with an expected musical label");
}
void boundedVocabularyPrior() {
  auto selected = selectGuideRecognitionCandidate({hypothesis("fuente", "es", -0.58),
                                                   hypothesis("Bridge", "en", -0.65)});
  expect(selected && selected->text == "Bridge",
         "generic section vocabulary can resolve nearby reasonably supported hypotheses");
  selected = selectGuideRecognitionCandidate({hypothesis("otros sonidos", "es", -0.35),
                                               hypothesis("Chorus", "en", -1.0)});
  expect(selected && selected->text == "otros sonidos",
         "the vocabulary prior cannot overrule a clearly stronger unknown transcription");
  selected = selectGuideRecognitionCandidate({hypothesis("otro sonido", "es", -2.0),
                                               hypothesis("Verse", "en", -2.05)});
  expect(selected && selected->text == "otro sonido" && selected->confidence && *selected->confidence < 0.2,
         "poor decoder support disables lexical rescue and stays visible as low confidence");
  selected = selectGuideRecognitionCandidate({hypothesis("un ruido", "es", -0.5),
                                               hypothesis("Chorus", "en", -0.25, 0.8)});
  expect(selected && selected->text == "un ruido",
         "high no-speech probability prevents a familiar label receiving the vocabulary prior");
  selected = selectGuideRecognitionCandidate({hypothesis("frase incierta", "es", -3.0, 0.94),
                                               hypothesis("uncertain phrase", "en", -3.1, 0.95)});
  expect(selected && selected->text == "frase incierta" && selected->alternatives.size() == 2,
         "low-confidence nonempty speech is preserved instead of silently discarded");
  selected = selectGuideRecognitionCandidate({hypothesis("otro sonido", "es", -0.61),
                                               hypothesis("one two three four", "en", -0.69)});
  expect(selected && selected->text == "one two three four",
         "count vocabulary participates without inventing a musical section");
  selected = selectGuideRecognitionCandidate({hypothesis("otro sonido", "es", -0.61),
                                               hypothesis("Breakdown", "en", -0.69)});
  expect(selected && selected->text == "Breakdown",
         "known utility vocabulary uses the shared catalogue rather than song-specific aliases");
}
void validationAndManualLanguage() {
  const auto spanish = hypothesis(juce::String::fromUTF8("¡Puente dos!"), "es", -0.4);
  auto selected = selectGuideRecognitionCandidate({spanish});
  expect(selected && selected->language == "es" && selected->text == spanish.segment.text &&
             selected->alternatives.size() == 1,
         "a single manually selected language preserves the original hypothesis");
  expect(!selectGuideRecognitionCandidate({}), "no hypotheses produces no fabricated speech");
  auto invalid = spanish; invalid.segment.text = "   ";
  expect(!selectGuideRecognitionCandidate({invalid}), "blank decoded text is not published as speech");
  invalid = spanish; invalid.segment.startSeconds = std::numeric_limits<double>::quiet_NaN();
  expect(!selectGuideRecognitionCandidate({invalid}), "non-finite timestamps are rejected");
  invalid = spanish; invalid.segment.endSeconds = invalid.segment.startSeconds;
  expect(!selectGuideRecognitionCandidate({invalid}), "empty source ranges are rejected");
  invalid = spanish; invalid.avgLogProbability = std::numeric_limits<double>::infinity();
  expect(!selectGuideRecognitionCandidate({invalid}), "non-finite decoder evidence is rejected");
  invalid = spanish; invalid.noSpeechProbability = 1.1;
  expect(!selectGuideRecognitionCandidate({invalid}), "invalid no-speech probabilities are rejected");
  auto withoutTokens = spanish; withoutTokens.textTokenCount = 0; withoutTokens.avgLogProbability = -100;
  selected = selectGuideRecognitionCandidate({withoutTokens});
  expect(selected && !selected->confidence && selected->text == spanish.segment.text,
         "missing text-token evidence remains visible without manufacturing confidence");
  selected = selectGuideRecognitionCandidate({invalid, spanish});
  expect(selected && selected->alternatives.size() == 1,
         "invalid alternative evidence cannot contaminate a valid candidate");
}

void measuredSpeechRetries() {
  // A 10 ms frame waveform with the measured short gaps that previously
  // joined Intro and the three count cues into one production window.
  std::vector<float> pcm;
  const auto append = [&](int frames, float amplitude) {
    pcm.insert(pcm.end(), size_t(frames * 160), amplitude);
  };
  append(20, 0); append(49, .1f); append(12, 0); append(46, .1f);
  append(16, 0); append(46, .1f); append(16, 0); append(47, .1f); append(30, 0);
  const auto windows = guideSpeechRetryWindows(pcm);
  expect(windows.size() == 4, "measured 120/160 ms gaps can recover four independently timed announcements");
  const size_t starts[]{20 * 160, 81 * 160, 143 * 160, 205 * 160};
  const size_t ends[]{69 * 160, 127 * 160, 189 * 160, 252 * 160};
  for (size_t i = 0; i < windows.size(); ++i) {
    expect(windows[i].speechBeginSample == starts[i] && windows[i].speechEndSample == ends[i],
           "child timestamps come from source activity rather than equal divisions of a phrase");
    expect(windows[i].beginSample <= starts[i] && windows[i].endSample >= ends[i] &&
               (i == 0 || windows[i].beginSample >= ends[i - 1]) &&
               (i + 1 == windows.size() || windows[i].endSample <= starts[i + 1]),
           "retry context never contains a neighbouring spoken announcement");
  }
  pcm.clear(); append(20, .1f); append(7, 0); append(20, .1f);
  expect(guideSpeechRetryWindows(pcm).empty(), "a short intra-word pause does not supply an invented split");
  pcm.clear(); append(20, .1f); append(8, 0); append(20, .1f);
  expect(guideSpeechRetryWindows(pcm).size() == 2, "an eighty millisecond measured pause permits a bounded retry");
  pcm.clear(); append(7, .1f); append(8, 0); append(20, .1f);
  expect(guideSpeechRetryWindows(pcm).empty(), "an isolated short transient invalidates the proposed decomposition");
  pcm.clear();
  for (int i = 0; i < 9; ++i) { append(10, .1f); append(10, 0); }
  expect(guideSpeechRetryWindows(pcm).empty(), "retry work is bounded to eight children per detected window");
  pcm = {std::numeric_limits<float>::quiet_NaN()};
  expect(guideSpeechRetryWindows(pcm).empty(), "invalid PCM cannot manufacture source boundaries");

  const auto selected = [](const char *text) {
    return *selectGuideRecognitionCandidate({hypothesis(juce::String::fromUTF8(text), "es", -.4)});
  };
  expect(isSingleGuideDirective(selected("Entra batería")) &&
             isSingleGuideDirective(selected("Subir intensidad")),
         "ordinary multiword instructions stay intact before retry segmentation is considered");
  expect(!isSingleGuideDirective(selected("intro, dos, tres, cuatro")) &&
             !isSingleGuideDirective(selected("uno dos tres cuatro")) &&
             !isSingleGuideDirective(selected("Entra batería, puente")),
         "compound count and mixed announcements cannot masquerade as one supported directive");
  auto child = selected("dos");
  expect(isSupportedGuideRetryChild(child), "an individual count requires literal independent decoder evidence");
  child.confidence = .1;
  expect(!isSupportedGuideRetryChild(child), "a weak child makes the caller keep the original whole phrase");
  child = selected("Entra batería"); child.alternatives[0].noSpeechProbability = .7;
  expect(!isSupportedGuideRetryChild(child), "high no-speech evidence cannot validate a familiar child label");
  child = selected("unknown phrase");
  expect(!isSupportedGuideRetryChild(child), "an unknown child prevents partial publication of a split phrase");
}

void compoundCountEvidence() {
  const auto intro = guideCompoundCount("Intro, dos, tres, cuatro.");
  expect(intro && intro->sectionPrefix == "intro" && intro->prefixWordCount == 1 &&
             intro->counts == std::vector<int>{2, 3, 4},
         "a compound intro keeps precisely the three counts actually transcribed");
  const auto repeated = guideCompoundCount("1, 2, 2, 3, 4");
  expect(repeated && repeated->sectionPrefix.isEmpty() && repeated->prefixWordCount == 0 &&
             repeated->counts == std::vector<int>{1, 2, 2, 3, 4},
         "a literal repeated count remains repeated rather than filling or deduplicating beats");
  const auto multiword = guideCompoundCount("turn around one two");
  expect(multiword && multiword->sectionPrefix == "turn around" && multiword->prefixWordCount == 2 &&
             multiword->counts == std::vector<int>{1, 2},
         "an ordinary unnumbered multiword prefix has explicit word cardinality");
  const auto spanish = guideCompoundCount(juce::String::fromUTF8("Introducción dos tres cuatro"));
  expect(spanish && spanish->prefixWordCount == 1 && spanish->counts == std::vector<int>{2, 3, 4},
         "accented catalogue prefixes use the same ordinary name normalization");
  for (const auto *text : {"Intro 2", "Verse 1 2 3", "Chorus one two", "Intro cuenta dos tres",
                          "intro one eight", "intro one two three four five six seven one two",
                          "en la intro dos tres", "Intro y dos tres", "dos tres otras palabras"})
    expect(!guideCompoundCount(text),
           "single ordinals, numbered variants, unsupported counts and narration do not request word rescue");

  auto parent = hypothesis("intro, dos, tres, cuatro.", "es", -.2);
  parent.segment.words = {{"Intro,", 12.35, 12.42, .4}, {"dos,", 12.43, 12.55, .8},
                          {"tres,", 12.56, 12.65, .9}, {"cuatro.", 12.66, 12.90, .9}};
  const auto selected = selectGuideRecognitionCandidate({parent, hypothesis("other phrase", "en", -1.0)});
  expect(selected && selected->text == parent.segment.text && selected->language == "es" &&
             selected->startSeconds == parent.segment.startSeconds && selected->endSeconds == parent.segment.endSeconds &&
             selected->words.size() == 4 && selected->words[1].text == "dos," &&
             selected->words[1].startSeconds == 12.43 && selected->words[1].endSeconds == 12.55 &&
             selected->words[1].confidence == .8,
         "word uncertainty follows only its original selected-language hypothesis without replacing parent speech");
}

void explicitCompoundDirectives() {
  struct Example { const char *text; int parts; };
  const Example examples[]{
      {"Interludio, toda la banda.",2}, {"Interlude, all in",2},
      {"Entra batería, puente.",2}, {"Build, chorus",2}, {"Build; All In",2},
      {"Verso, uno, toda la banda.",2}, {"Verse, one, all in",2},
      {"Third bridge, drums in",2}, {"Build, All In, Drums In",3}};
  for (const auto &example : examples) {
    const auto text = juce::String::fromUTF8(example.text);
    const auto compound = guideCompoundDirectives(text);
    const auto selected = selectGuideRecognitionCandidate({hypothesis(text,"es",-.4)});
    expect(compound && compound->phrases.size()==example.parts && selected &&
               !isSingleGuideDirective(*selected),
           "exact comma-separated section and utility parts request independent source-audio recognition");
  }
  const auto ordinal = guideCompoundDirectives("Verse, one, all in");
  expect(ordinal && ordinal->phrases[0]=="Verse, one" && ordinal->phrases[1]=="all in",
         "a comma spoken inside a section ordinal does not create a spurious independent count");
  for (const auto *text : {"Verso, uno", "Verse, one", "Coro con toda la banda", "Chorus with full band",
                          "solo, bajo", "Interlude, unknown words", "next section chorus, all in",
                          "one more chorus, all in", "Interlude,,All In", "Interlude; ; All In",
                          "Interlude, All In,", "chorus, chorus, refrain, post-chorus",
                          "Build, Chorus, Verse", "All in, bass in the chorus"})
    expect(!guideCompoundDirectives(text),
           "ordinary ordinal or multiword labels, narration, missing parts and ambiguous section lists stay outside explicit compound classification");
  for (const auto *text : {"Verso, uno", "Coro con toda la banda", "Chorus with full band", "solo, bajo"}) {
    const auto selected = selectGuideRecognitionCandidate({hypothesis(text,"es",-.4)});
    expect(selected && isSingleGuideDirective(*selected),
           "known single directives retain their existing treatment despite punctuation or an explicit utility connector");
  }
  expect(sameGuideDirectiveMeaning("Interludio","Interlude.") &&
             sameGuideDirectiveMeaning("Verso, dos","Verse 2.") &&
             sameGuideDirectiveMeaning("Toda la banda","All In") &&
             sameGuideDirectiveMeaning("Build","Subir intensidad"),
         "independently decoded compound children may use equivalent Spanish or English catalogue labels");
  expect(!sameGuideDirectiveMeaning("Interlude","Verse") &&
             !sameGuideDirectiveMeaning("Verse 1","Verse 2") &&
             !sameGuideDirectiveMeaning("All In","Build") &&
             !sameGuideDirectiveMeaning("Interlude","Interlude with full band") &&
             !sameGuideDirectiveMeaning("Build","Build in the chorus"),
         "a known but different or still-combined child cannot replace its expected atomic announcement");
}

void regroupedSpeechEvidence() {
  std::vector<float> pcm;
  const auto append = [&](int frames, float amplitude) {
    pcm.insert(pcm.end(), size_t(frames * 160), amplitude);
  };
  append(20, 0); append(84, .1f); append(6, 0); append(82, .1f); append(30, 0);
  expect(guideSpeechRetryWindows(pcm).empty(), "the general retry retains its eighty millisecond gate");
  const auto shortGap = guideSpeechRetryWindowsAtGap(pcm, 6);
  expect(shortGap.size() == 2 && shortGap[1].speechBeginSample == 110 * 160,
         "explicit compounds can retry a measured sixty millisecond gap without inventing a timestamp");
  pcm.clear(); append(20, 0); append(84, .1f); append(82, .1f); append(30, 0);
  expect(guideSpeechRetryWindowsAtGap(pcm, 6).empty(),
         "touching announcements without a measured gap are not assigned a guessed boundary");
  pcm.clear(); append(20, 0); append(116, .1f); append(20, 0);
  append(32, .1f); append(10, 0); append(40, .1f); append(20, 0);
  append(104, .1f); append(30, 0);
  const auto regions = guideSpeechRetryWindowsAtGap(pcm, 6);
  const auto partitions = guideSpeechRetryPartitions(regions, 3);
  expect(regions.size() == 4 && partitions.size() == 3,
         "a pause inside All In supplies three complete candidates for three parent directives");
  int expectedGrouping = 0;
  for (const auto &partition : partitions) {
    expect(partition.size() == 3 && partition.front().speechBeginSample == regions.front().speechBeginSample &&
               partition.back().speechEndSample == regions.back().speechEndSample,
           "every grouping covers the entire observed phrase and keeps its first and last attacks");
    for (size_t i = 0; i + 1 < partition.size(); ++i)
      expect(partition[i].endSample <= partition[i + 1].speechBeginSample &&
                 partition[i + 1].beginSample >= partition[i].speechEndSample,
             "group context contains no voice from a neighbouring group");
    if (partition[0].speechEndSample == regions[0].speechEndSample &&
        partition[1].speechBeginSample == regions[1].speechBeginSample &&
        partition[1].speechEndSample == regions[2].speechEndSample) ++expectedGrouping;
  }
  expect(expectedGrouping == 1, "the All and In regions are available together as exactly one complete grouping");
  expect(guideSpeechRetryPartitions(regions, 1).empty() &&
             guideSpeechRetryPartitions(regions, 5).empty(),
         "partial publication and more directives than observed regions cannot be proposed");
  auto invalid = regions; invalid[1].beginSample = 0;
  expect(guideSpeechRetryPartitions(invalid, 3).empty(), "overlapping spoken contexts invalidate every partition");
  const juce::StringArray phrases{"Build", "All In", "Drums In"};
  const auto decode = [&](const GuideSpeechWindow &window, size_t,
                          std::optional<GuideTranscriptSegment> &child) {
    juce::String text = "unknown phrase";
    if (window.speechBeginSample == regions[0].speechBeginSample &&
        window.speechEndSample == regions[0].speechEndSample) text = "Subir intensidad";
    if (window.speechBeginSample == regions[1].speechBeginSample &&
        window.speechEndSample == regions[2].speechEndSample) text = "All In";
    if (window.speechBeginSample == regions[3].speechBeginSample &&
        window.speechEndSample == regions[3].speechEndSample) text = "Entra bateria";
    child = selectGuideRecognitionCandidate({hypothesis(text, "es", -.3)});
    return true;
  };
  const auto unique = guideUniqueSpeechRetry(partitions, phrases, decode);
  expect(unique.completed && !unique.ambiguous && !unique.exhausted && unique.children.size() == 3 &&
             unique.children[1].text == "All In",
         "only a complete independently supported grouping recovers all three directives");
  const auto competing = guideUniqueSpeechRetry(guideSpeechRetryPartitions(regions, 2),
      juce::StringArray{"Build", "All In"},
      [&](const GuideSpeechWindow &window, size_t, std::optional<GuideTranscriptSegment> &child) {
        child = selectGuideRecognitionCandidate({hypothesis(
            window.speechBeginSample == regions[0].speechBeginSample ? "Build" : "All In", "en", -.3)});
        return true;
      });
  expect(competing.ambiguous && competing.children.empty(),
         "two semantically supported boundaries keep the complete parent for review");
  auto reordered = partitions;
  std::rotate(reordered.begin(), reordered.begin() + 1, reordered.end());
  const auto exhausted = guideUniqueSpeechRetry(reordered, phrases, decode, 3);
  expect(exhausted.exhausted && exhausted.children.empty(),
         "finding one valid grouping before exhausting the budget never publishes an unverified unique result");
  const auto aborted = guideUniqueSpeechRetry(partitions, phrases,
      [](const GuideSpeechWindow &, size_t, std::optional<GuideTranscriptSegment> &) { return false; });
  expect(!aborted.completed && aborted.children.empty(), "cancelled retry cannot publish a partial grouping");
  pcm.clear(); append(7, .1f); append(6, 0); append(20, .1f);
  expect(guideSpeechRetryWindowsAtGap(pcm, 6).empty(), "a short transient cannot become an extra compound part");
}

void completeWordIntervals() {
  GuideWordTimingCollector grouped(1.0, 4.0);
  grouped.append(" Inter", 1.0, 1.4, -.2);
  grouped.append("lud", 1.4, 1.4, -.1);
  grouped.append("io", 1.4, 1.4, -.1);
  grouped.append(",", 1.4, 1.4, 0);
  grouped.append(" toda", 1.6, 1.8, -.3);
  const auto words = grouped.takeWords();
  expect(words.size() == 2 && words[0].text == "Interludio," &&
             words[0].startSeconds == 1.0 && words[0].endSeconds == 1.4 &&
             words[1].text == "toda" && words[0].confidence &&
             std::abs(*words[0].confidence - std::exp(-.4 / 3.0)) < 1e-10,
         "zero-duration subtokens retain spelling and confidence without extending a positive word interval");
  GuideWordTimingCollector leadingZero(1.0, 4.0);
  leadingZero.append(" Inter", 1.0, 1.0, -.2);
  leadingZero.append("lude", 1.1, 1.5, -.1);
  const auto positive = leadingZero.takeWords();
  expect(positive.size() == 1 && positive[0].startSeconds == 1.1 && positive[0].endSeconds == 1.5,
         "only positive source evidence supplies a recovered word range");
  GuideWordTimingCollector collapsed(1.0, 4.0);
  collapsed.append("Intro,", 1.0, 3.8, -.2);
  collapsed.append(" dos", 4.0, 4.0, -.1);
  collapsed.append(" tres", 4.0, 4.0, -.1);
  expect(collapsed.takeWords().empty(), "entire count words with no positive interval keep the whole parent unresolved");
  GuideWordTimingCollector outOfRange(1.0, 4.0);
  outOfRange.append("Intro", .9, 1.2, -.2);
  expect(outOfRange.takeWords().empty(), "word evidence outside its actual source parent is rejected");
  GuideWordTimingCollector overlapsPadding(1.0, 4.0);
  overlapsPadding.append(" cuatro", 3.9, 4.2, -.1);
  expect(overlapsPadding.takeWords().empty(),
         "a timing token partly beyond its real source end cannot supply word evidence");
  GuideWordTimingCollector insidePadding(1.0, 4.0);
  insidePadding.append(" cuatro", 4.1, 4.2, -.1);
  expect(insidePadding.takeWords().empty(),
         "a timing wholly beyond the source end cannot supply a count interval");
  GuideWordTimingCollector reversed(1.0, 4.0);
  reversed.append("Inter", 1.2, 1.5, -.2);
  reversed.append("lude", 1.1, 1.4, -.2);
  expect(reversed.takeWords().empty(), "reversed positive subtoken intervals cannot manufacture a word range");
}
} // namespace

int main() {
  try {
    acousticEvidenceAndLiteralOutput();
    boundedVocabularyPrior();
    validationAndManualLanguage();
    measuredSpeechRetries();
    compoundCountEvidence();
    explicitCompoundDirectives();
    regroupedSpeechEvidence();
    completeWordIntervals();
    std::cout << "Guide recognition selection: " << checks << " checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
