#pragma once

#include <JuceHeader.h>
class MTControlsAudioProcessor;
#include <functional>
#include <map>

// Message-thread adapter used by the existing UI. Audio always uses the real
// processor; a client only reads the mirrored state and sends control intents.
template <typename Processor>
class BasicPlaybackSession {
public:
  explicit BasicPlaybackSession(Processor &p) : local(p) {}
  bool isRemote() const { return remote; }
  bool isConnected() const { return connected; }
  void setRemote(bool enabled) { pendingRehearsalClear = juce::var(); remote = enabled; connected = false; state = juce::var(); meters = juce::var(); includedStemIds.clear(); pendingControls.clear(); position = frozenPosition = receivedAt = 0; }
  void setConnected(bool enabled) {
    if (!enabled && connected) frozenPosition = getSongCurrentPositionSeconds();
    connected = enabled;
    if (!enabled) { meters = juce::var(); pendingControls.clear(); }
  }
  std::function<void(const juce::String &, const juce::String &, const juce::var &, const juce::String &)> onCommand;
  void send(const juce::String &action, const juce::var &value = {}, const juce::String &target = {}) {
    if (!remote || !connected || !onCommand) return;
    const auto id = juce::Uuid().toString();
    if (action == "mix.volume" || action == "mix.gain" || action == "pads.volume" || action == "pads.gain" || action == "rehearsal.groupLevel" || action == "rehearsal.restLevel")
      pendingControls[{action, target}] = {id, value, juce::Time::getMillisecondCounterHiRes() + 2000.0, -1};
    onCommand(id, action, value, target);
  }
  void applyAcknowledgement(const juce::String &id, bool accepted) {
    for (auto it = pendingControls.begin(); it != pendingControls.end(); ++it) {
      if (it->second.id != id) continue;
      if (!accepted) pendingControls.erase(it);
      else it->second.acknowledgedAtSequence = static_cast<juce::int64>(field(state, "seq"));
      return;
    }
  }
  void applyState(const juce::var &next) {
    if (field(field(state, "song"), "id") != field(field(next, "song"), "id")) pendingControls.clear();
    const auto sequence = static_cast<juce::int64>(field(next, "seq"));
    const auto now = juce::Time::getMillisecondCounterHiRes();
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
    if (pendingRehearsalClear.isObject()) {
      const auto request = pendingRehearsalClear;
      pendingRehearsalClear = juce::var();
      const auto confirmed = field(state, "rehearsal");
      if (field(request, "songId") == field(confirmed, "songId") &&
          field(request, "revision") == field(confirmed, "revision"))
        send("rehearsal.clear", field(request, "revision"), field(request, "songId").toString());
    }
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
  void setStemVolume(int index, float value) { if (remote) send("mix.volume", value, stemId(index)); else local.setStemVolume(index, value); }
  void setStemGain(int index, float value) { if (remote) send("mix.gain", value, stemId(index)); else local.setStemGain(index, value); }
  void setStemMute(int index, bool value) { if (remote) send("mix.mute", value, stemId(index)); else local.setStemMute(index, value); }
  void setStemSolo(int index, bool value) { if (remote) send("mix.solo", value, stemId(index)); else local.setStemSolo(index, value); }
  juce::var getRehearsalState() const { return remote ? field(state, "rehearsal") : local.getRehearsalState(); }
  bool supportsRehearsal() const { return static_cast<int>(field(getRehearsalState(), "version")) == 1; }
  float getRehearsalLevel(bool group) const {
    const auto rehearsal = getRehearsalState();
    const auto action = group ? "rehearsal.groupLevel" : "rehearsal.restLevel";
    return static_cast<float>(controlValue(action, field(rehearsal, "songId").toString(),
        number(rehearsal, group ? "groupLevel" : "restLevel", 1.0)));
  }
  void setRehearsalGroup(const juce::String &group) { rehearsalAction("rehearsal.select", group); }
  void setRehearsalLevel(bool group, float value) { rehearsalAction(group ? "rehearsal.groupLevel" : "rehearsal.restLevel", value); }
  void clearRehearsal() {
    if (remote && !connected) pendingRehearsalClear = getRehearsalState();
    else rehearsalAction("rehearsal.clear", {});
  }
  float getRehearsalStemLevel(const juce::String &id) const { return remote ? meter(id).first : local.getRehearsalStemLevel(id); }
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
  juce::var pendingRehearsalClear;
  void rehearsalAction(const juce::String &action, const juce::var &value) {
    if (!supportsRehearsal()) return;
    const auto song = field(getRehearsalState(), "songId").toString();
    if (remote) send(action, value, song);
    else local.applyRehearsalAction(action, song, value);
  }
  struct PendingControl {
    juce::String id;
    juce::var value;
    double expiresAt;
    juce::int64 acknowledgedAtSequence;
  };
  std::map<std::pair<juce::String, juce::String>, PendingControl> pendingControls;
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
