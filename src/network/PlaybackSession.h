#pragma once

#include <JuceHeader.h>
class MTControlsAudioProcessor;
#include <functional>
#include <map>
#include <memory>
#include <cmath>

// A temporary pause belongs to one gesture and one control revision. A Stop,
// seek, song load, or another explicit transport action supersedes that owner.
// Only control/message threads use this value; audio never touches the lease.
struct PracticeScrubLease {
  juce::String token;
  juce::String songId;
  uint64_t revision = 0;
  bool resumePlayback = false;

  void clear() { token.clear(); songId.clear(); revision = 0; resumePlayback = false; }

  template <typename Processor>
  bool validFor(const Processor &processor, const juce::String &song,
                const juce::String &gestureToken) const {
    return token.isNotEmpty() && token == gestureToken && songId == song &&
           processor.hasLoadedSong() &&
           revision == processor.getTransportControlRevision();
  }

  template <typename Processor>
  bool begin(Processor &processor, const juce::String &song,
             const juce::String &gestureToken) {
    if (token.isNotEmpty()) {
      if (token == gestureToken)
        return validFor(processor, song, gestureToken);
      if (validFor(processor, songId, token))
        return false;
      clear();
    }
    if (!processor.hasLoadedSong() || song.isEmpty() || gestureToken.isEmpty())
      return false;
    const bool wasPlaying = processor.isSongPlaying();
    if (wasPlaying)
      processor.pauseSong();
    else
      processor.markTransportControlIntent();
    token = gestureToken;
    songId = song;
    resumePlayback = wasPlaying;
    revision = processor.getTransportControlRevision();
    return true;
  }

  template <typename Processor>
  bool finish(Processor &processor, const juce::String &song,
              const juce::String &gestureToken, const double seconds) {
    if (token != gestureToken)
      return false;
    const bool valid = std::isfinite(seconds) &&
                       validFor(processor, song, gestureToken);
    const bool shouldResume = resumePlayback;
    clear();
    if (!valid)
      return false;
    processor.seekSongTo(seconds);
    if (shouldResume && !processor.isSongPlaying())
      processor.playSong();
    return true;
  }

  template <typename Processor>
  bool cancel(Processor &processor, const juce::String &song,
              const juce::String &gestureToken, const bool restorePlayback) {
    if (token != gestureToken)
      return false;
    const bool shouldResume = restorePlayback && resumePlayback &&
                              validFor(processor, song, gestureToken);
    clear();
    if (shouldResume && !processor.isSongPlaying())
      processor.playSong();
    return true;
  }
};

// Message-thread adapter used by the existing UI. Audio always uses the real
// processor; a client only reads the mirrored state and sends control intents.
template <typename Processor>
class BasicPlaybackSession {
public:
  explicit BasicPlaybackSession(Processor &p) : local(p) {}
  bool isRemote() const { return remote; }
  bool isConnected() const { return connected; }
  void setRemote(bool enabled) { cancelPracticeScrub(false); releasePracticeLoop(); remote = enabled; connected = false; state = juce::var(); meters = juce::var(); includedStemIds.clear(); pendingControls.clear(); clearPracticeListeningOwnership(); clearPracticeLoopOwnership(); position = frozenPosition = receivedAt = 0; }
  void setConnected(bool enabled) {
    if (!enabled && connected) frozenPosition = getSongCurrentPositionSeconds();
    connected = enabled;
    if (!enabled) { clearPracticeScrub(); clearPracticeLoopOwnership(); meters = juce::var(); pendingControls.clear(); clearPracticeListeningOwnership(); }
  }
  std::function<void(const juce::String &, const juce::String &, const juce::var &, const juce::String &)> onCommand;
  juce::String send(const juce::String &action, const juce::var &value = {}, const juce::String &target = {}) {
    if (!remote || !connected || !onCommand) return {};
    const auto id = juce::Uuid().toString();
    if (action == "mix.volume" || action == "mix.gain" || action == "pads.volume" || action == "pads.gain")
      pendingControls[{action, target}] = {id, value, juce::Time::getMillisecondCounterHiRes() + 2000.0, -1};
    if (action == "practice.listen") {
      clearPracticeListeningOwnership();
      practiceListeningPendingCommandId = id;
      practiceListeningPendingMode = static_cast<int>(value.getProperty("mode", 0));
      practiceListeningPendingStemId = target;
      practiceListeningPending = true;
      practiceListeningExpiresAt = juce::Time::getMillisecondCounterHiRes() + 2000.0;
      // A normal-mix intent still needs an optimistic echo, but immediately
      // relinquishes the lease so changing instrument cannot retarget it.
      if (practiceListeningPendingMode != 0)
        practiceListeningCommandId = id;
    } else if (action == "practice.release") clearPracticeListeningOwnership();
    onCommand(id, action, value, target);
    return id;
  }
  void applyAcknowledgement(const juce::String &id, bool accepted,
                            const juce::String &reason = {}) {
    if (id.isNotEmpty() && id == practiceLoopCommandId) {
      practiceLoopRequestRejected = !accepted;
      if (!accepted) {
        practiceLoopError = reason.isEmpty() ? juce::String("rejected") : reason;
        practiceLoopPending = false;
        practiceLoopCommandId = practiceLoopPreviousCommandId;
        practiceLoopPreviousCommandId.clear();
      } else practiceLoopAcknowledgedAtSequence = static_cast<juce::int64>(field(state, "seq"));
    }
    if (id.isNotEmpty() && id == practiceScrubFinishCommandId)
      practiceScrubFinishRejected = !accepted;
    if (id == practiceScrubBeginCommandId && !accepted)
      clearPracticeScrub();
    if (id.isNotEmpty() && id == practiceListeningPendingCommandId) {
      if (!accepted) clearPracticeListeningOwnership();
      else practiceListeningAcknowledgedAtSequence = static_cast<juce::int64>(field(state, "seq"));
    }
    for (auto it = pendingControls.begin(); it != pendingControls.end(); ++it) {
      if (it->second.id != id) continue;
      if (!accepted) pendingControls.erase(it);
      else it->second.acknowledgedAtSequence = static_cast<juce::int64>(field(state, "seq"));
      return;
    }
  }
  void applyState(const juce::var &next) {
    if (field(field(state, "song"), "id") != field(field(next, "song"), "id")) {
      clearPracticeScrub();
      clearPracticeLoopOwnership();
      pendingControls.clear();
      clearPracticeListeningOwnership();
    }
    const auto sequence = static_cast<juce::int64>(field(next, "seq"));
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (practiceLoopPending &&
        (now >= practiceLoopExpiresAt ||
         (practiceLoopAcknowledgedAtSequence >= 0 &&
          sequence > practiceLoopAcknowledgedAtSequence)))
      practiceLoopPending = false;
    if (practiceListeningPending && (now >= practiceListeningExpiresAt ||
        (practiceListeningAcknowledgedAtSequence >= 0 && sequence > practiceListeningAcknowledgedAtSequence)))
      practiceListeningPending = false;
    for (auto it = pendingControls.begin(); it != pendingControls.end();) {
      const auto &pending = it->second;
      // TCP orders ack before the following snapshot. Earlier echoes (including
      // a deferred UI reapplication of the same snapshot) must not move the thumb.
      if (now >= pending.expiresAt || (pending.acknowledgedAtSequence >= 0 && sequence > pending.acknowledgedAtSequence))
        it = pendingControls.erase(it);
      else ++it;
    }
    state = next; connected = true;
    includedStemIds.clear();
    const auto list = field(field(state, "song"), "stems");
    if (const auto *values = list.getArray()) for (const auto &v : *values)
      if (static_cast<bool>(v.getProperty("include", true))) includedStemIds.add(field(v,"id").toString());
    position = number(state, "position");
    frozenPosition = position;
    receivedAt = juce::Time::getMillisecondCounterHiRes();
  }
  void applyMeters(const juce::var &next) {
    meters = next;
    position = number(next, "position");
    frozenPosition = position;
    receivedAt = juce::Time::getMillisecondCounterHiRes();
    if (auto *o = state.getDynamicObject()) { o->setProperty("playing", field(next, "playing")); o->setProperty("position", position); }
  }
  const juce::var &snapshot() const { return state; }
  static juce::var field(const juce::var &v, const char *key) { return v.getProperty(key, {}); }
  static double number(const juce::var &v, const char *key, double fallback = 0.0) {
    return static_cast<double>(v.getProperty(key, fallback));
  }
  double getSongCurrentPositionSeconds() const {
    if (!remote) return local.getSongCurrentPositionSeconds();
    if (!connected) return frozenPosition;
    const auto rate = getPlaybackTempoBpm() / juce::jmax(1.0, number(field(state, "song"), "bpm", 120));
    const auto elapsed = juce::jlimit(0.0, 0.25, (juce::Time::getMillisecondCounterHiRes() - receivedAt) / 1000.0);
    return juce::jlimit(0.0, getSongLengthSeconds(), position + (isSongPlaying() ? elapsed * rate : 0.0));
  }
  bool hasLoadedSong() const { return remote ? field(field(state, "song"), "id").toString().isNotEmpty() : local.hasLoadedSong(); }
  int getLoadedStemCount() const { return remote ? includedStemIds.size() : local.getLoadedStemCount(); }
  juce::String getLoadedSongTitle() const { return remote ? field(field(state, "song"), "title").toString() : local.getLoadedSongTitle(); }
  juce::String getLoadedSongKey() const { return remote ? field(field(state, "song"), "key").toString() : local.getLoadedSongKey(); }
  bool isSongPlaying() const { return remote ? static_cast<bool>(field(state, "playing")) : local.isSongPlaying(); }
  double getSongLengthSeconds() const { return remote ? static_cast<double>(field(state, "length")) : local.getSongLengthSeconds(); }
  double getLoadedSongLengthSeconds() const { return remote ? static_cast<double>(field(state, "sourceLength")) : local.getLoadedSongLengthSeconds(); }
  double getSongStartSeconds() const { return remote ? static_cast<double>(field(state, "windowStart")) : local.getSongStartSeconds(); }
  double getSongEndSeconds() const { return remote ? static_cast<double>(field(state, "windowEnd")) : local.getSongEndSeconds(); }
  bool isSongEndCustomized() const { return remote ? static_cast<bool>(field(state, "windowEndCustomized")) : local.isSongEndCustomized(); }
  double getPlaybackTempoBpm() const { return remote ? static_cast<double>(field(state, "tempo")) : local.getPlaybackTempoBpm(); }
  int getPitchSemiTones() const { return remote ? static_cast<int>(field(state, "pitch")) : local.getPitchSemiTones(); }
  bool isFadeActive() const { return remote ? static_cast<bool>(field(state, "fade")) : local.isFadeActive(); }
  double getSongFadeOutSeconds() const { return remote ? static_cast<double>(field(field(state, "ui"), "getSongFadeOutSeconds")) : local.getSongFadeOutSeconds(); }
  bool isCustomArrangementActive() const { return remote ? static_cast<bool>(field(field(state, "ui"), "isCustomArrangementActive")) : local.isCustomArrangementActive(); }
  bool isMetronomeEnabled() const { return remote ? static_cast<bool>(field(field(state, "ui"), "isMetronomeEnabled")) : local.isMetronomeEnabled(); }
  double getMetronomeBpm() const { return remote ? static_cast<double>(field(field(state, "ui"), "getMetronomeBpm")) : local.getMetronomeBpm(); }
  int getClickDivision() const { return remote ? static_cast<int>(field(field(state, "ui"), "getClickDivision")) : local.getClickDivision(); }
  bool isClickDivisionEnabled() const { return remote ? static_cast<bool>(field(field(state, "ui"), "isClickDivisionEnabled")) : local.isClickDivisionEnabled(); }
  int getPendingArrangementPartJumpIndex() const { return remote ? static_cast<int>(field(field(state, "ui"), "getPendingArrangementPartJumpIndex")) : local.getPendingArrangementPartJumpIndex(); }
  int getArrangementPartLoopIndex() const { return remote ? static_cast<int>(field(field(state, "ui"), "getArrangementPartLoopIndex")) : local.getArrangementPartLoopIndex(); }
  bool isPracticeLoopActive() const {
    if (!remote) return local.isPracticeLoopActive();
    return connected && ((!practiceLoopPendingRange.isEmpty() && hasPendingPracticeLoop()) ||
                         static_cast<bool>(field(state, "practiceLoopActive")));
  }
  juce::Range<double> getPracticeLoopRange() const {
    if (!remote) return local.getPracticeLoopRange();
    if (!connected) return {};
    if (!practiceLoopPendingRange.isEmpty() && hasPendingPracticeLoop())
      return practiceLoopPendingRange;
    return isPracticeLoopActive()
        ? juce::Range<double>(number(state, "practiceLoopStart"), number(state, "practiceLoopEnd"))
        : juce::Range<double>{};
  }
  juce::String getPracticeLoopIdentity() const {
    if (!remote) return juce::String(local.getPracticeLoopRevision());
    return hasPendingPracticeLoop() ? practiceLoopCommandId
                                   : field(state, "practiceLoopCommandId").toString();
  }
  double getMinimumPracticeLoopDuration() const {
    return remote ? juce::jmax(0.25, number(state, "practiceLoopMinimumDuration", 0.25))
                  : local.getMinimumPracticeLoopDuration();
  }
  bool ownsPracticeLoop() const {
    if (!remote) return localPracticeLoopOwned &&
        (local.isPracticeLoopActive() || local.hasPracticeLoopPreparation()) &&
        localPracticeLoopRevision == local.getPracticeLoopRevision();
    return connected && practiceLoopCommandId.isNotEmpty() &&
        (hasPendingPracticeLoop() ||
         ((isPracticeLoopActive() || static_cast<bool>(field(state, "practiceLoopPrepared"))) &&
          field(state, "practiceLoopCommandId").toString() == practiceLoopCommandId));
  }
  bool wasPracticeLoopRequestRejected() const { return practiceLoopRequestRejected; }
  juce::String getPracticeLoopError() const {
    if (!remote) return local.getPracticeLoopError();
    return practiceLoopError.isNotEmpty() ? practiceLoopError
                                         : field(state, "practiceLoopError").toString();
  }
  bool setPracticeLoopRange(double start, double end) {
    practiceLoopRequestRejected = false;
    practiceLoopError.clear();
    if (!remote) {
      const bool accepted = local.setPracticeLoopRange(start, end);
      if (accepted) {
        localPracticeLoopOwned = true;
        localPracticeLoopRevision = local.getPracticeLoopRevision();
      }
      practiceLoopRequestRejected = !accepted;
      return accepted;
    }
    if (!connected || !hasLoadedSong() || !std::isfinite(start) ||
        !std::isfinite(end) || start < getSongStartSeconds() ||
        end > getSongEndSeconds() || end <= start) {
      practiceLoopRequestRejected = true;
      practiceLoopError = "invalid-range";
      return false;
    }
    if (end - start < getMinimumPracticeLoopDuration()) {
      practiceLoopRequestRejected = true;
      practiceLoopError = "too-short";
      return false;
    }
    auto value = std::make_unique<juce::DynamicObject>();
    value->setProperty("songId", field(field(state, "song"), "id"));
    value->setProperty("start", start);
    value->setProperty("end", end);
    const auto id = send("practice.loop", juce::var(value.release()));
    if (id.isEmpty()) { practiceLoopError = "rejected"; practiceLoopRequestRejected = true; return false; }
    practiceLoopPreviousCommandId = practiceLoopCommandId;
    practiceLoopCommandId = id;
    practiceLoopPendingRange = {start, end};
    practiceLoopPending = true;
    practiceLoopExpiresAt = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    practiceLoopAcknowledgedAtSequence = -1;
    return true;
  }
  bool preparePracticeLoop(double start) {
    if (ownsPracticeLoop()) releasePracticeLoop();
    practiceLoopRequestRejected = false;
    practiceLoopError.clear();
    if (!remote) {
      const bool accepted = local.preparePracticeLoop(start);
      if (accepted) {
        localPracticeLoopOwned = true;
        localPracticeLoopRevision = local.getPracticeLoopRevision();
      }
      return accepted;
    }
    if (!connected || !hasLoadedSong() || !std::isfinite(start) ||
        start < getSongStartSeconds() || start >= getSongEndSeconds()) {
      practiceLoopError = "invalid-range";
      return false;
    }
    auto value = std::make_unique<juce::DynamicObject>();
    value->setProperty("songId", field(field(state, "song"), "id"));
    value->setProperty("start", start);
    const auto id = send("practice.loopPrepare", juce::var(value.release()));
    if (id.isEmpty()) return false;
    practiceLoopCommandId = id;
    practiceLoopPendingRange = {};
    practiceLoopPending = true;
    practiceLoopExpiresAt = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    practiceLoopAcknowledgedAtSequence = -1;
    return true;
  }
  void releasePracticeLoop() {
    if (!remote) {
      if (ownsPracticeLoop()) local.clearPracticeLoop();
      clearPracticeLoopOwnership();
      return;
    }
    if (connected && practiceLoopCommandId.isNotEmpty()) {
      auto value = std::make_unique<juce::DynamicObject>();
      value->setProperty("songId", field(field(state, "song"), "id"));
      value->setProperty("commandId", practiceLoopCommandId);
      send("practice.loopRelease", juce::var(value.release()));
    }
    clearPracticeLoopOwnership();
  }
  int getAvailableOutputChannelCount() const { return remote ? static_cast<int>(field(field(state, "ui"), "getAvailableOutputChannelCount")) : local.getAvailableOutputChannelCount(); }
  float getMasterVolume() const { return remote ? static_cast<float>(controlValue("mix.volume", "master", number(mix("master"), "volume", 1))) : local.getMasterVolume(); }
  float getMasterGain() const { return remote ? static_cast<float>(controlValue("mix.gain", "master", number(mix("master"), "gain", 1))) : local.getMasterGain(); }
  bool isMasterMuted() const { return remote ? static_cast<bool>(field(mix("master"), "mute")) : local.isMasterMuted(); }
  void setMasterVolume(float value) { if (remote) send("mix.volume", value, "master"); else local.setMasterVolume(value); }
  void setMasterGain(float value) { if (remote) send("mix.gain", value, "master"); else local.setMasterGain(value); }
  void setMasterMute(bool value) { if (remote) send("mix.mute", value, "master"); else local.setMasterMute(value); }
  float getMetronomeVolume() const { return remote ? static_cast<float>(controlValue("mix.volume", "click", number(mix("click"), "volume", 1))) : local.getMetronomeVolume(); }
  float getMetronomeGain() const { return remote ? static_cast<float>(controlValue("mix.gain", "click", number(mix("click"), "gain", 1))) : local.getMetronomeGain(); }
  int getMetronomeOutputChannel() const { return remote ? static_cast<int>(number(mix("click"), "outputChannel", 1)) : local.getMetronomeOutputChannel(); }
  int getMetronomeOutputWidth() const { return remote ? static_cast<int>(number(mix("click"), "outputWidth", 1)) : local.getMetronomeOutputWidth(); }
  bool isMetronomeMuted() const { return remote ? static_cast<bool>(field(mix("click"), "mute")) : local.isMetronomeMuted(); }
  bool isMetronomeSolo() const { return remote ? static_cast<bool>(field(mix("click"), "solo")) : local.isMetronomeSolo(); }
  void setMetronomeVolume(float value) { if (remote) send("mix.volume", value, "click"); else local.setMetronomeVolume(value); }
  void setMetronomeGain(float value) { if (remote) send("mix.gain", value, "click"); else local.setMetronomeGain(value); }
  void setMetronomeMuted(bool value) { if (remote) send("mix.mute", value, "click"); else local.setMetronomeMuted(value); }
  void setMetronomeSolo(bool value) { if (remote) send("mix.solo", value, "click"); else local.setMetronomeSolo(value); }
  float getDynamicGuideVolume() const { return remote ? static_cast<float>(controlValue("mix.volume", "guide", number(mix("guide"), "volume", 1))) : local.getDynamicGuideVolume(); }
  float getDynamicGuideGain() const { return remote ? static_cast<float>(controlValue("mix.gain", "guide", number(mix("guide"), "gain", 1))) : local.getDynamicGuideGain(); }
  int getDynamicGuideOutputChannel() const { return remote ? static_cast<int>(number(mix("guide"), "outputChannel", 1)) : local.getDynamicGuideOutputChannel(); }
  int getDynamicGuideOutputWidth() const { return remote ? static_cast<int>(number(mix("guide"), "outputWidth", 1)) : local.getDynamicGuideOutputWidth(); }
  bool isDynamicGuideMuted() const { return remote ? static_cast<bool>(field(mix("guide"), "mute")) : local.isDynamicGuideMuted(); }
  bool isDynamicGuideSolo() const { return remote ? static_cast<bool>(field(mix("guide"), "solo")) : local.isDynamicGuideSolo(); }
  void setDynamicGuideVolume(float value) { if (remote) send("mix.volume", value, "guide"); else local.setDynamicGuideVolume(value); }
  void setDynamicGuideGain(float value) { if (remote) send("mix.gain", value, "guide"); else local.setDynamicGuideGain(value); }
  void setDynamicGuideMuted(bool value) { if (remote) send("mix.mute", value, "guide"); else local.setDynamicGuideMuted(value); }
  void setDynamicGuideSolo(bool value) { if (remote) send("mix.solo", value, "guide"); else local.setDynamicGuideSolo(value); }
  float getStemVolume(int index) const { return remote ? static_cast<float>(controlValue("mix.volume", stemId(index), number(mix(stemId(index)), "volume"))) : local.getStemVolume(index); }
  float getStemGain(int index) const { return remote ? static_cast<float>(controlValue("mix.gain", stemId(index), number(mix(stemId(index)), "gain"))) : local.getStemGain(index); }
  bool isStemMuted(int index) const { return remote ? static_cast<bool>(number(mix(stemId(index)), "mute")) : local.isStemMuted(index); }
  bool isStemSoloed(int index) const { return remote ? static_cast<bool>(number(mix(stemId(index)), "solo")) : local.isStemSoloed(index); }
  int getStemOutputChannel(int index) const { return remote ? static_cast<int>(number(mix(stemId(index)), "outputChannel")) : local.getStemOutputChannel(index); }
  void setPracticeListeningMode(int index, int mode) {
    if (mode < 0 || mode > 2) return;
    if (!remote) { local.setPracticeListeningMode(index, mode); return; }
    if (!connected || (mode != 0 && stemId(index).isEmpty())) return;
    auto value = std::make_unique<juce::DynamicObject>();
    value->setProperty("mode", mode);
    value->setProperty("songId", field(field(state, "song"), "id"));
    send("practice.listen", juce::var(value.release()), mode == 0 ? juce::String{} : stemId(index));
  }
  void releasePracticeListening() {
    if (!remote) { local.setPracticeListeningMode(-1, 0); return; }
    auto value = std::make_unique<juce::DynamicObject>();
    value->setProperty("mode", 0);
    value->setProperty("songId", field(field(state, "song"), "id"));
    clearPracticeListeningOwnership();
    send("practice.release", juce::var(value.release()));
  }
  bool ownsPracticeListening() const {
    if (!remote) return local.getPracticeListeningMode() != 0;
    if (!connected || practiceListeningCommandId.isEmpty()) return false;
    if (hasPendingPracticeListening()) return true;
    return getPracticeListeningMode() != 0 && field(state, "practiceCommandId").toString() == practiceListeningCommandId;
  }
  int getPracticeListeningStem() const {
    if (!remote) return local.getPracticeListeningStem();
    if (!connected || getPracticeListeningMode() == 0) return -1;
    return includedStemIds.indexOf(hasPendingPracticeListening()
        ? practiceListeningPendingStemId : field(state, "practiceStemId").toString());
  }
  int getPracticeListeningMode() const {
    return remote ? (connected ? (hasPendingPracticeListening() ? practiceListeningPendingMode
                                : static_cast<int>(number(state, "practiceMode"))) : 0)
                  : local.getPracticeListeningMode();
  }
  void setStemVolume(int index, float value) { if (remote) send("mix.volume", value, stemId(index)); else local.setStemVolume(index, value); }
  void setStemGain(int index, float value) { if (remote) send("mix.gain", value, stemId(index)); else local.setStemGain(index, value); }
  void setStemMute(int index, bool value) { if (remote) send("mix.mute", value, stemId(index)); else local.setStemMute(index, value); }
  void setStemSolo(int index, bool value) { if (remote) send("mix.solo", value, stemId(index)); else local.setStemSolo(index, value); }
  void setStemOutputChannel(int index, int value) { if (remote) send("mix.output", value, stemId(index)); else local.setStemOutputChannel(index, value); }
  void setPlaybackTempoBpm(double value) { if (remote) send("tempo.set", value); else local.setPlaybackTempoBpm(value); }
  void setPitchSemiTones(int value) { if (remote) send("pitch.set", value); else local.setPitchSemiTones(value); }
  void setFadeActive(bool value) { if (remote) send("transport.fade", value); else local.setFadeActive(value); }
  void setMetronomeEnabled(bool value) { if (remote) send("click.metronome", value); else local.setMetronomeEnabled(value); }
  void setClickDivision(int value) { if (remote) send("click.division", value); else local.setClickDivision(value); }
  void setClickDivisionEnabled(bool value) { if (remote) send("click.enabled", value); else local.setClickDivisionEnabled(value); }
  void setSongStartSeconds(double value) { if (remote) send("window.start", value); else local.setSongStartSeconds(value); }
  void setSongEndSeconds(double value) { if (remote) send("window.end", value); else local.setSongEndSeconds(value); }
  void setSongFadeOutSeconds(double value) { if (remote) send("window.fade", value); else local.setSongFadeOutSeconds(value); }
  void requestArrangementPartJump(int value) { if (remote) send("arrange.jump", value); else local.requestArrangementPartJump(value); }
  void toggleArrangementPartLoop(int value) { if (remote) send("arrange.loop", value); else local.toggleArrangementPartLoop(value); }
  void toggleSongPlayPause() { if (remote) send("transport.toggle"); else local.toggleSongPlayPause(); }
  void stopSong() { if (remote) send("transport.stop"); else local.stopSong(); }
  void clearPendingArrangementPartJump() { if (remote) send("arrange.clearJump"); else local.clearPendingArrangementPartJump(); }
  void clearArrangementPartLoop() { if (remote) send("arrange.clearLoop"); else local.clearArrangementPartLoop(); }
  void seekSongTo(double seconds) { if (remote) send("transport.seek", seconds); else local.seekSongTo(seconds); }
  void seekSongRelative(double seconds) { seekSongTo(getSongCurrentPositionSeconds() + seconds); }
  // A second drag during settling keeps the same lease and original intent.
  void beginPracticeScrub() {
    practiceScrubFinishCommandId.clear();
    practiceScrubFinishRejected = false;
    if (!hasLoadedSong() || (remote && !connected)) return;
    if (practiceScrubToken.isNotEmpty()) {
      if (remote || localPracticeScrub.validFor(local, "local", practiceScrubToken)) return;
      cancelPracticeScrub(false);
    }
    practiceScrubToken = juce::Uuid().toString();
    practiceScrubSongId = remote ? field(field(state, "song"), "id").toString() : "local";
    if (!remote) {
      if (!localPracticeScrub.begin(local, practiceScrubSongId, practiceScrubToken))
        clearPracticeScrub();
      return;
    }
    practiceScrubBeginCommandId = send("transport.scrubBegin", practiceScrubValue());
    if (practiceScrubBeginCommandId.isEmpty()) clearPracticeScrub();
  }
  bool finishPracticeScrub(const double seconds) {
    if (practiceScrubToken.isEmpty() || !std::isfinite(seconds)) return false;
    if (!remote) {
      const bool applied = localPracticeScrub.finish(local, practiceScrubSongId,
                                                    practiceScrubToken, seconds);
      clearPracticeScrub();
      return applied;
    }
    auto value = practiceScrubValue();
    value.getDynamicObject()->setProperty("seconds", seconds);
    practiceScrubFinishCommandId = send("transport.scrubEnd", value);
    const bool sent = practiceScrubFinishCommandId.isNotEmpty();
    clearPracticeScrub(true);
    return sent;
  }
  bool wasPracticeScrubFinishRejected() const { return practiceScrubFinishRejected; }
  void cancelPracticeScrub(const bool restorePlayback) {
    if (practiceScrubToken.isEmpty()) return;
    if (!remote) localPracticeScrub.cancel(local, practiceScrubSongId,
                                          practiceScrubToken, restorePlayback);
    else {
      auto value = practiceScrubValue();
      value.getDynamicObject()->setProperty("restorePlayback", restorePlayback);
      send("transport.scrubCancel", value);
    }
    clearPracticeScrub();
  }
  void setFadeStopsPlayback(bool value) { if (!remote) local.setFadeStopsPlayback(value); }
  // Rhythm display initialization must not send commands back on every UI tick.
  void setMetronomeBeatsPerBar(int value) { if (remote) send("click.beats", value); else local.setMetronomeBeatsPerBar(value); }
  void setMetronomeBpm(double value) { if (!remote) local.setMetronomeBpm(value); }
  void setDynamicGuideOutputRouting(int channel, int width) { if (!remote) local.setDynamicGuideOutputRouting(channel, width); }
  juce::StringArray getAvailableOutputChannelNames() const {
    if (!remote) return local.getAvailableOutputChannelNames();
    juce::StringArray result;
    const auto outputs = field(field(state, "ui"), "outputs");
    if (const auto *values = outputs.getArray())
      for (const auto &v : *values) result.add(v.toString());
    return result;
  }
  float getStemLevel(int index) const { return remote ? meter(stemId(index)).first : local.getStemLevel(index); }
  void getOutputMeterLevels(float &l, float &r) const { if (!remote) local.getOutputMeterLevels(l,r); else { const auto v = meter("master"); l=v.first; r=v.second; } }
  void getMetronomeMeterLevels(float &l, float &r) const { if (!remote) local.getMetronomeMeterLevels(l,r); else { const auto v = meter("click"); l=v.first; r=v.second; } }
  void getDynamicGuideMeterLevels(float &l, float &r) const { if (!remote) local.getDynamicGuideMeterLevels(l,r); else { const auto v = meter("guide"); l=v.first; r=v.second; } }
  float getPadsVolume() const { return remote ? static_cast<float>(controlValue("pads.volume", {}, number(field(state, "ui"), "getPadsVolume", 1.0))) : local.getPadsVolume(); }
  float getPadsGain() const { return remote ? static_cast<float>(controlValue("pads.gain", {}, number(field(state, "ui"), "getPadsGain", 1.0))) : local.getPadsGain(); }
  int getActivePadNoteIndex() const { return remote ? static_cast<int>(field(state, "ui").getProperty("getActivePadNoteIndex", -1)) : local.getActivePadNoteIndex(); }
  bool isPadsEnabled() const { return remote ? static_cast<bool>(field(state, "ui").getProperty("isPadsEnabled", false)) : local.isPadsEnabled(); }
  bool isPadsPackLoaded() const { return remote ? static_cast<bool>(field(state, "ui").getProperty("isPadsPackLoaded", false)) : local.isPadsPackLoaded(); }
  bool isPadsPreloadRunning() const { return remote ? static_cast<bool>(field(state, "ui").getProperty("isPadsPreloadRunning", false)) : local.isPadsPreloadRunning(); }
  juce::String getPadsPackStatusText() const { return remote ? field(field(state,"ui"),"getPadsPackStatusText").toString() : local.getPadsPackStatusText(); }
  float getPadsOutputLevel() const { return remote ? meter("pads").first : local.getPadsOutputLevel(); }
  void setPadsEnabled(bool value) { if (remote) { if (value != isPadsEnabled()) send("pads.enabled", value); } else local.setPadsEnabled(value); }
  void setPadsVolume(float value) { if (remote) send("pads.volume", value); else local.setPadsVolume(value); }
  void setPadsGain(float value) { if (remote) send("pads.gain", value); else local.setPadsGain(value); }
  void triggerPadNote(int note, int octave) { if (remote) send("pads.note", juce::var(juce::Array<juce::var>{note,octave})); else local.triggerPadNote(note,octave); }
  void triggerPadNoteForSongTransition(int note, int octave) { if (remote) triggerPadNote(note,octave); else local.triggerPadNoteForSongTransition(note,octave); }
  void stopPadNote(int note) { if (remote) send("pads.stop", note); else local.stopPadNote(note); }
  bool ensurePadsPackReady() { return remote ? isPadsPackLoaded() : local.ensurePadsPackReady(); }
  void startPadsPreloadAsync() { if (remote) send("pads.preload"); else local.startPadsPreloadAsync(); }
  void refreshPadsRoutingFromSettings() { if (!remote) local.refreshPadsRoutingFromSettings(); }

private:
  bool localPracticeLoopOwned = false;
  uint64_t localPracticeLoopRevision = 0;
  juce::String practiceLoopCommandId, practiceLoopError;
  juce::String practiceLoopPreviousCommandId;
  juce::Range<double> practiceLoopPendingRange;
  bool practiceLoopPending = false;
  bool practiceLoopRequestRejected = false;
  double practiceLoopExpiresAt = 0.0;
  juce::int64 practiceLoopAcknowledgedAtSequence = -1;
  bool hasPendingPracticeLoop() const {
    return practiceLoopPending && !practiceLoopRequestRejected &&
           juce::Time::getMillisecondCounterHiRes() < practiceLoopExpiresAt;
  }
  void clearPracticeLoopOwnership() {
    localPracticeLoopOwned = false;
    localPracticeLoopRevision = 0;
    practiceLoopCommandId.clear();
    practiceLoopPreviousCommandId.clear();
    practiceLoopError.clear();
    practiceLoopPendingRange = {};
    practiceLoopPending = false;
    practiceLoopRequestRejected = false;
    practiceLoopExpiresAt = 0.0;
    practiceLoopAcknowledgedAtSequence = -1;
  }
  PracticeScrubLease localPracticeScrub;
  juce::String practiceScrubToken, practiceScrubSongId, practiceScrubBeginCommandId;
  juce::String practiceScrubFinishCommandId;
  bool practiceScrubFinishRejected = false;
  juce::var practiceScrubValue() const {
    auto value = std::make_unique<juce::DynamicObject>();
    value->setProperty("token", practiceScrubToken);
    value->setProperty("songId", practiceScrubSongId);
    return juce::var(value.release());
  }
  void clearPracticeScrub(const bool preserveFinish = false) {
    localPracticeScrub.clear();
    practiceScrubToken.clear();
    practiceScrubSongId.clear();
    practiceScrubBeginCommandId.clear();
    if (!preserveFinish) {
      practiceScrubFinishCommandId.clear();
      practiceScrubFinishRejected = false;
    }
  }
  struct PendingControl {
    juce::String id;
    juce::var value;
    double expiresAt;
    juce::int64 acknowledgedAtSequence;
  };
  std::map<std::pair<juce::String, juce::String>, PendingControl> pendingControls;
  juce::String practiceListeningCommandId;
  juce::String practiceListeningPendingCommandId, practiceListeningPendingStemId;
  int practiceListeningPendingMode = 0;
  bool practiceListeningPending = false;
  double practiceListeningExpiresAt = 0.0;
  juce::int64 practiceListeningAcknowledgedAtSequence = -1;
  void clearPracticeListeningOwnership() {
    practiceListeningCommandId.clear();
    practiceListeningPendingCommandId.clear();
    practiceListeningPendingStemId.clear();
    practiceListeningPendingMode = 0;
    practiceListeningPending = false;
    practiceListeningExpiresAt = 0.0;
    practiceListeningAcknowledgedAtSequence = -1;
  }
  bool hasPendingPracticeListening() const {
    return connected && practiceListeningPending &&
           juce::Time::getMillisecondCounterHiRes() < practiceListeningExpiresAt;
  }
  double controlValue(const juce::String &action, const juce::String &target, double confirmed) const {
    const auto pending = pendingControls.find({action, target});
    if (pending != pendingControls.end() && juce::Time::getMillisecondCounterHiRes() < pending->second.expiresAt)
      return static_cast<double>(pending->second.value);
    return confirmed;
  }
  juce::StringArray includedStemIds;
  juce::String stemId(int index) const { return juce::isPositiveAndBelow(index, includedStemIds.size()) ? includedStemIds[index] : juce::String{}; }
  juce::var mix(const juce::String &id) const {
    const auto rows = field(state, "mixer");
    if (const auto *values = rows.getArray()) for (const auto &row : *values)
      if (field(row, "target").toString() == id) return row;
    return {};
  }
  std::pair<float,float> meter(const juce::String &id) const {
    if (!connected || juce::Time::getMillisecondCounterHiRes() - receivedAt > 300.0) return {};
    const auto levels = field(meters, "levels");
    const auto row = levels.getProperty(juce::Identifier(id), {});
    return {static_cast<float>(number(row, "l")), static_cast<float>(number(row, "r"))};
  }
  Processor &local;
  bool remote = false, connected = false;
  juce::var state, meters;
  double position = 0.0, frozenPosition = 0.0, receivedAt = 0.0;
};

using PlaybackSession = BasicPlaybackSession<MTControlsAudioProcessor>;
