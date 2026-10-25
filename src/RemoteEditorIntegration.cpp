#include "PluginEditor.h"

#include "data/Localization.h"
#include "data/AppSettings.h"
#include "data/ProjectStore.h"
#include "data/SongLibraryStore.h"
#include "ui/panels/ChannelGridPanel.h"
#include "ui/panels/SettingsPanel.h"
#include "network/RemoteStateCodec.h"
#include "network/RemoteDeviceName.h"
#include "network/SectionControl.h"
#include <cstring>
#include "ui/panels/SongListPanel.h"
#include "ui/panels/WaveformBar.h"
#include <algorithm>
#include <cmath>

namespace {
juce::var object() { return juce::var(new juce::DynamicObject()); }

juce::var property(const juce::var &value, const char *name) {
  return value.getProperty(juce::Identifier(name), {});
}

juce::String stringProperty(const juce::var &value, const char *name) {
  return property(value, name).toString();
}

bool isNumber(const juce::var &value) {
  return value.isInt() || value.isInt64() || value.isDouble();
}

juce::var makeMixerRow(const juce::String &target, const juce::String &name,
                       float volume, float gain, bool mute, bool solo) {
  auto row = object();
  auto *fields = row.getDynamicObject();
  fields->setProperty("target", target);
  fields->setProperty("name", name);
  fields->setProperty("volume", volume);
  fields->setProperty("gain", gain);
  fields->setProperty("mute", mute);
  fields->setProperty("solo", solo);
  return row;
}
} // namespace

void MTControlsAudioProcessorEditor::setRemoteMode(playworship::remote::RemoteSession::Mode mode) {
  using Mode = playworship::remote::RemoteSession::Mode;
  const auto previousMode = remoteSession.getMode();
  if (mode == previousMode) return;
  learningRemoteAction.disconnected();
  if (songLoadBusy || projectSaveBusy || mixExportBusy) { remoteConnectionStatus = L10n::tr("Espera a que termine la carga", "Wait for loading to finish"); refreshRemoteSettings(); return; }
  auto &p = static_cast<MTControlsAudioProcessor &>(processor);
  if (p.isSectionAutomationEditing()) p.finishSectionAutomationEditing(false);
  p.setSectionAutomationRemoteOwned(false);
  remoteSectionAutomationOwner.clear();
  remoteSectionAutomationCommandId.clear();
  remoteSectionAutomationSongId.clear();
  if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get())) grid->flushPendingPersistenceNow();
  flushActiveSongPersistenceNow();
  if (previousMode != Mode::Client) p.setPracticeListeningMode(-1, 0);
  playbackSession.releasePracticeLoop();
  if (previousMode != Mode::Client) p.clearPracticeLoop();
  remotePracticeLoopOwner.clear();
  remotePracticeLoopCommandId.clear();
  remotePracticeLoopRevision = 0;
  remotePracticeOwner.clear();
  remotePracticeCommandId.clear();
  remotePracticeRevision = 0;
  for (auto &entry : remotePracticeScrubLeases) {
    auto &lease = entry.second;
    lease.cancel(p, hasCurrentLoadedSong ? currentLoadedSong.id : juce::String{},
                 lease.token, true);
  }
  remotePracticeScrubLeases.clear();
  playbackSession.cancelPracticeScrub(true);
  remoteSession.stop();
  remoteHadSnapshot = false;
  if (playbackSession.isRemote()) {
    // The local audio song was kept paused. Restore its metadata and visual cache.
    playbackSession.setRemote(false);
    currentLoadedSong = remoteSavedLocalSong;
    hasCurrentLoadedSong = remoteSavedHasSong;
    remoteDisplayedLibrary.reset();
    if (songListPanelRaw) {
      songListPanelRaw->setRemoteLibrary({});
      currentSongIndex = songListPanelRaw->getSongIndexById(currentLoadedSong.id);
      songListPanelRaw->setSelectedSongIndex(currentSongIndex);
    }
    wasSongPlaying = false; remoteVisualStatePending = false;
    if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get())) grid->setSong(currentLoadedSong);
    if (waveformBarPtr) {
      refreshWaveformPresentationForSong();
      waveformBarPtr->setWaveformBins(std::move(remoteSavedLocalWaveform));
      waveformBarPtr->resetPlaybackViewForSong();
      waveformBarPtr->setSongRhythm(currentLoadedSong.bpm, currentLoadedSong.timeSignature);
      ProjectSongSettings saved;
      ProjectStore::deserializeSettings(remoteSavedLocalSettings, saved);
      waveformBarPtr->setArrangement(saved.arrangement, saved.arrangementCustomized);
      waveformBarPtr->setTempoMap(currentLoadedSong.tempoMap);
    }
    p.setRemoteClientMode(false);
    p.setPadsEnabled(remotePreviousPadsEnabled);
    p.setMetronomeEnabled(remotePreviousMetronomeEnabled);
    remoteClientAudioMuted = false;
    resetTrackedSongWindow();
  }
  remoteConnectionStatus.clear();
  if (mode == Mode::Client) {
    remoteSavedLocalSong = currentLoadedSong;
    remoteSavedHasSong = hasCurrentLoadedSong;
    ProjectSongSettings saved;
    if (hasCurrentLoadedSong) captureCloudSettings(currentLoadedSong, saved);
    remoteSavedLocalSettings = ProjectStore::serializeSettings(saved);
    remoteSavedLocalWaveform = waveformBarPtr ? waveformBarPtr->getWaveformBinsCopy() : std::vector<float>{};
    remotePreviousPadsEnabled = p.isPadsEnabled();
    remotePreviousMetronomeEnabled = p.isMetronomeEnabled();
    p.pauseSong(); p.setPadsEnabled(false); p.setMetronomeEnabled(false); p.setRemoteClientMode(true);
    remoteClientAudioMuted = true;
    deferredStartupSongLoadPending = false;
    ++waveformGeneration;
    playbackSession.setRemote(true);
    playbackSession.setArrangementControlView({}, {});
    playbackSession.onCommand = [this](const juce::String &id, const juce::String &action, const juce::var &value, const juce::String &target) {
      learningRemoteAction.sent(id, action, currentLoadedSong.id,
                                juce::Time::getMillisecondCounterHiRes(),
                                playbackSession.isSongPlaying());
      remoteSession.sendCommand(id, action, value, target);
    };
    remoteLibrarySignature.clear(); remoteSettingsSignature.clear(); remoteChannelSignature.clear(); remoteExpectedWaveformHash.clear();
    currentLoadedSong = {}; hasCurrentLoadedSong = false;
    refreshWaveformPresentationForSong();
    remoteDisplayedLibrary = std::make_shared<LibraryState>();
    if (songListPanelRaw) {
      songListPanelRaw->onRemoteAction = [this](const juce::String &action, const juce::var &value, const juce::String &target) { playbackSession.send(action, value, target); };
      songListPanelRaw->setRemoteLibrary(remoteDisplayedLibrary);
    }
    if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get())) grid->setSong({});
    if (waveformBarPtr) { waveformBarPtr->setWaveformBins({}); waveformBarPtr->setArrangement({}, false); waveformBarPtr->setTempoMap({}); waveformBarPtr->resetPlaybackViewForSong(); }
    remoteSession.becomeClient();
  } else if (mode == Mode::Server) {
    if (!remoteSession.becomeServer(playworship::remote::makeRemoteServerName(
            AppSettings::getRemoteServerAlias())))
      remoteConnectionStatus = L10n::tr("No se pudo iniciar el servidor", "Could not start server");
    nextRemotePublishMs = nextRemoteLibraryMs = nextRemoteSettingsMs = 0;
    remoteLibraryDirty = true;
    remoteStateSignature.clear();
    remoteWaveformSongId.clear(); remoteSettingsSongId.clear(); remoteStateSignature.clear(); remoteLibraryCache = juce::var();
  }
  startTimerHz(mode == Mode::Local ? 20 : 30);
  refreshRemoteSettings();
}

void MTControlsAudioProcessorEditor::refreshRemoteSettings() {
  using Mode = playworship::remote::RemoteSession::Mode;
  const auto mode = remoteSession.getMode();
  const auto servers = remoteSession.getDiscoveredServers();
  // A single discovered server needs no additional interaction. Multiple
  // servers are selected in the existing Settings panel.
  if (mode == Mode::Client && remoteSession.getSelectedServerName().isEmpty() && servers.size() == 1)
    remoteSession.connectTo(servers.front());
  juce::String status = remoteConnectionStatus;
  if (status.isEmpty()) {
    const auto discoveryStatus = remoteSession.getDiscoveryStatus();
    using DiscoveryStatus = playworship::remote::LanDiscovery::Status;
    if (mode == Mode::Local) status = L10n::tr("Funcionamiento local", "Local operation");
    else if (discoveryStatus == DiscoveryStatus::PermissionDenied)
      status = L10n::tr("Habilita Red local para PlayWorship en Ajustes", "Enable Local Network for PlayWorship in Settings");
    else if (discoveryStatus == DiscoveryStatus::Failed)
      status = L10n::tr("No se pudo iniciar el descubrimiento en la red local", "Unable to start local network discovery");
    else if (mode == Mode::Server)
      status = L10n::tr("Servidor: ", "Server: ") + remoteSession.getServerDisplayName()
          + L10n::tr(" · Clientes: ", " · Clients: ")
          + juce::String(remoteSession.getClientCount());
    else if (playbackSession.isConnected()) status = L10n::tr("Conectado a ", "Connected to ") + remoteSession.getSelectedServerName();
    else if (remoteSession.getSelectedServerName().isEmpty()) status = L10n::tr("Buscando servidores…", "Finding servers…");
    else status = remoteHadSnapshot ? L10n::tr("Reconectando…", "Reconnecting…")
                                    : L10n::tr("Conectando…", "Connecting…");
  }
  if (settingsPanelRaw) settingsPanelRaw->updateRemoteState(mode, servers, status, remoteSession.getSelectedServerName());
  refreshRemoteIndicator();
}

void MTControlsAudioProcessorEditor::refreshRemoteIndicator() {
  if (!waveformBarPtr)
    return;
  using Mode = playworship::remote::RemoteSession::Mode;
  using Indicator = WaveformBar::RemoteIndicator;
  Indicator indicator = Indicator::Hidden;
  if (remoteSession.getMode() == Mode::Client) {
    indicator = playbackSession.isConnected() ? Indicator::Connected
        : remoteSession.getSelectedServerName().isEmpty()
            ? Indicator::Searching
            : remoteHadSnapshot ? Indicator::Reconnecting : Indicator::Connecting;
  }
  waveformBarPtr->setRemoteIndicator(indicator);
}

void MTControlsAudioProcessorEditor::applyRemoteSnapshot(const juce::var &snapshot) {
  if (!playbackSession.isRemote()) return;
  const auto oldId = currentLoadedSong.id;
  playbackSession.applyState(snapshot);
  remoteHadSnapshot = true;
  const auto song = property(snapshot, "song");
  const auto songId = stringProperty(song, "id");
  const auto library = property(snapshot, "library");
  const auto signature = juce::JSON::toString(library);
  if (library.isObject() && signature != remoteLibrarySignature) {
    remoteLibrarySignature = signature;
    remoteDisplayedLibrary = std::make_shared<LibraryState>(playworship::remote::decodeLibrary(library));
    if (songListPanelRaw) songListPanelRaw->setRemoteLibrary(remoteDisplayedLibrary);
  }
  const auto channelSignature = songId + juce::JSON::toString(property(song, "stems"));
  if (song != remoteReceivedSong) {
    remoteReceivedSong = song;
    currentLoadedSong = playworship::remote::decodeSong(song);
  }
  hasCurrentLoadedSong = songId.isNotEmpty();
  refreshWaveformPresentationForSong();
  const auto hash = stringProperty(snapshot, "waveformHash");
  if (oldId != songId || hash != remoteExpectedWaveformHash) {
    if (waveformBarPtr) {
      if (hash != remoteExpectedWaveformHash || hash.isEmpty()) waveformBarPtr->setWaveformBins({});
      waveformBarPtr->resetPlaybackViewForSong();
    }
    remoteExpectedWaveformHash = hash;
  }
  if (channelSignature != remoteChannelSignature) {
    remoteChannelSignature = channelSignature;
    if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get())) grid->setSong(currentLoadedSong);
    if (songListPanelRaw) {
      currentSongIndex = songListPanelRaw->getSongIndexById(songId);
      songListPanelRaw->setSelectedSongIndex(currentSongIndex);
    }
  }
  ProjectSongSettings decoded;
  if (ProjectStore::deserializeSettings(property(snapshot, "settings"), decoded)) {
    currentLoadedSong.arrangement = decoded.arrangement;
    ProjectSongSettings visual;
    visual.arrangement = decoded.arrangement; visual.arrangementCustomized = decoded.arrangementCustomized;
    const auto signature = songId + juce::JSON::toString(ProjectStore::serializeSettings(visual)) + juce::JSON::toString(playworship::remote::encodeTempoMap(currentLoadedSong.tempoMap));
    if (waveformBarPtr) {
      waveformBarPtr->setSongRhythm(currentLoadedSong.bpm, currentLoadedSong.timeSignature);
      const auto controlRevision = stringProperty(property(snapshot, "sectionControls"), "revision");
      remoteVisualStatePending = signature != remoteSettingsSignature ||
          !playbackSession.isArrangementControlView(songId, controlRevision);
      if (remoteVisualStatePending && !waveformBarPtr->isMouseButtonDown(true)) {
        if (signature != remoteSettingsSignature) {
          remoteSettingsSignature = signature;
          waveformBarPtr->setTempoMap(currentLoadedSong.tempoMap);
          waveformBarPtr->setArrangement(decoded.arrangement, decoded.arrangementCustomized);
        }
        playbackSession.setArrangementControlView(songId, controlRevision);
        remoteVisualStatePending = false;
      }
    }
    if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
      grid->setGuideSourceMode(decoded.dynamicGuide.enabled ? ChannelGridPanel::GuideSourceMode::DynamicBySections : ChannelGridPanel::GuideSourceMode::SongGuide, false);
  }

}

void MTControlsAudioProcessorEditor::applyRemoteWaveform(const std::vector<uint8_t> &bytes) {
  if (!playbackSession.isRemote() || bytes.empty() || bytes.size() % sizeof(float) != 0 || bytes.size() > 1024 * 1024 || remoteExpectedWaveformHash.isEmpty()) return;
  std::vector<float> bins(bytes.size() / sizeof(float));
  for (size_t i = 0; i < bins.size(); ++i) {
    const size_t offset = i * sizeof(float);
    const uint32_t bits = (static_cast<uint32_t>(bytes[offset]) << 24) | (static_cast<uint32_t>(bytes[offset+1]) << 16) | (static_cast<uint32_t>(bytes[offset+2]) << 8) | bytes[offset+3];
    std::memcpy(&bins[i], &bits, sizeof(bits));
    if (!std::isfinite(bins[i])) return;
  }
  if (juce::SHA256(bins.data(), bins.size()*sizeof(float)).toHexString() != remoteExpectedWaveformHash) return;
  if (waveformBarPtr) waveformBarPtr->setWaveformBins(std::move(bins));
}

void MTControlsAudioProcessorEditor::processRemoteEvents() {
  auto &p = static_cast<MTControlsAudioProcessor &>(processor);
  const auto clearAutomationOwner = [&] {
    p.setSectionAutomationRemoteOwned(false);
    remoteSectionAutomationOwner.clear();
    remoteSectionAutomationCommandId.clear();
    remoteSectionAutomationSongId.clear();
  };
  if (!remoteSectionAutomationOwner.empty() &&
      (!p.isSectionAutomationEditing() || !hasCurrentLoadedSong ||
       remoteSectionAutomationSongId != currentLoadedSong.id)) {
    if (p.isSectionAutomationEditing()) p.finishSectionAutomationEditing(false);
    clearAutomationOwner();
    nextRemotePublishMs = 0;
    remoteStateSignature.clear();
  }
  const auto revokeStalePracticeLoopOwner = [&] {
    if (!remotePracticeLoopOwner.empty() &&
        p.getPracticeLoopRevision() != remotePracticeLoopRevision) {
      remotePracticeLoopOwner.clear();
      remotePracticeLoopCommandId.clear();
      remotePracticeLoopRevision = 0;
    }
  };
  revokeStalePracticeLoopOwner();
  // A local gesture or song change supersedes any earlier remote ownership.
  if (!remotePracticeOwner.empty() &&
      p.getPracticeListeningRevision() != remotePracticeRevision) {
    remotePracticeOwner.clear();
    remotePracticeCommandId.clear();
    remotePracticeRevision = 0;
  }
  for (auto &event : remoteSession.drainEvents()) {
    revokeStalePracticeLoopOwner();
    if (!remotePracticeOwner.empty() &&
        p.getPracticeListeningRevision() != remotePracticeRevision) {
      remotePracticeOwner.clear();
      remotePracticeCommandId.clear();
      remotePracticeRevision = 0;
    }
    using Kind = playworship::remote::RemoteSession::Event::Kind;
    if (event.kind == Kind::Connected || event.kind == Kind::Disconnected) {
      playbackSession.setConnected(false);
      learningRemoteAction.disconnected();
      continue;
    }
    if (event.kind == Kind::PeerDisconnected) {
      if (remoteSectionAutomationOwner == event.sourceId) {
        p.finishSectionAutomationEditing(false);
        clearAutomationOwner();
        nextRemotePublishMs = 0;
        remoteStateSignature.clear();
      }
      if (remotePracticeLoopOwner == event.sourceId) {
        if (p.getPracticeLoopRevision() == remotePracticeLoopRevision)
          p.clearPracticeLoop();
        remotePracticeLoopOwner.clear();
        remotePracticeLoopCommandId.clear();
        remotePracticeLoopRevision = 0;
        nextRemotePublishMs = 0;
        remoteStateSignature.clear();
      }
      const auto scrub = remotePracticeScrubLeases.find(event.sourceId);
      if (scrub != remotePracticeScrubLeases.end()) {
        auto &lease = scrub->second;
        lease.cancel(p, hasCurrentLoadedSong ? currentLoadedSong.id : juce::String{},
                     lease.token, !songLoadBusy);
        remotePracticeScrubLeases.erase(scrub);
        nextRemotePublishMs = 0;
        remoteStateSignature.clear();
      }
      if (remotePracticeOwner == event.sourceId) {
        if (p.getPracticeListeningRevision() == remotePracticeRevision) {
          p.setPracticeListeningMode(-1, 0);
          nextRemotePublishMs = 0;
          remoteStateSignature.clear();
        }
        remotePracticeOwner.clear();
        remotePracticeCommandId.clear();
        remotePracticeRevision = 0;
      }
      continue;
    }
    if (event.kind == Kind::Waveform) { applyRemoteWaveform(event.bytes); continue; }
    auto value = event.value.isObject() ? event.value : juce::JSON::parse(juce::String::fromUTF8(event.text.data(), static_cast<int>(event.text.size())));
    if (static_cast<int>(property(value, "v")) != 1) continue;
    const auto type = stringProperty(value, "type");
    if (event.kind == Kind::Snapshot || event.kind == Kind::Meters) {
      if (type == "snapshot") {
        applyRemoteSnapshot(value);
        learningRemoteAction.snapshot(
            static_cast<juce::int64>(property(value, "seq")),
            stringProperty(property(value, "song"), "id"),
            static_cast<bool>(property(value, "playing")),
            juce::Time::getMillisecondCounterHiRes());
      }
      else if (type == "meters" && playbackSession.isRemote() && playbackSession.isConnected() && stringProperty(value, "songId") == currentLoadedSong.id) playbackSession.applyMeters(value);
      else if (type == "ack") {
        const bool accepted = static_cast<bool>(property(value, "accepted"));
        playbackSession.applyAcknowledgement(stringProperty(value, "id"), accepted,
                                             stringProperty(value, "reason"));
        learningRemoteAction.acknowledge(stringProperty(value, "id"), accepted,
            static_cast<juce::int64>(property(playbackSession.snapshot(), "seq")),
            stringProperty(value, "reason"));
        if (!accepted) remoteConnectionStatus = stringProperty(value, "reason");
      }
    } else if (event.kind == Kind::Command && remoteSession.getMode() == playworship::remote::RemoteSession::Mode::Server) {
      juce::String error;
      const auto action = stringProperty(value, "action");
      bool accepted = false;
      bool automationControl = action == "pads.enabled" || action == "pads.volume" ||
          action == "pads.gain" || action == "pads.note" || action == "pads.stop";
      if (hasCurrentLoadedSong && (action == "mix.volume" || action == "mix.gain" || action == "mix.mute")) {
        const auto target = stringProperty(value, "target");
        for (const auto &stem : currentLoadedSong.stems)
          if (stem.include && stem.stemId == target && stemPriorityForStripOrder(stem) >= 2)
            automationControl = true;
      }
      const bool structureEdit = action == "arrange.set" || action == "arrange.reset" ||
                                 action == "tempoMap.set" || action.startsWith("window.");
      const auto automationEnvelope = property(value, "value");
      const bool wrappedAutomationControl = automationControl &&
          automationEnvelope.isObject() &&
          automationEnvelope.getDynamicObject()->hasProperty("controlValue");
      const auto activeSong = hasCurrentLoadedSong ? currentLoadedSong.id : juce::String{};
      if (action.startsWith("automation.")) {
        const auto song = property(automationEnvelope, "songId");
        const auto target = stringProperty(value, "target");
        const auto editCommandId = stringProperty(automationEnvelope, "editCommandId");
        if (stringProperty(value, "type") != "command" ||
            stringProperty(value, "id").isEmpty() || event.sourceId.empty() ||
            !automationEnvelope.isObject() || !song.isString() ||
            song.toString() != activeSong || activeSong.isEmpty() ||
            !p.hasLoadedSong() || songLoadBusy || target.isEmpty() || target.length() > 512) {
          error = "Automation song changed";
        } else if (action == "automation.begin") {
          if (p.isSectionAutomationEditing()) error = "Automation is being edited";
          else {
            if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
              grid->flushPendingPersistenceNow();
            accepted = p.beginSectionAutomationEditing(target, true);
            if (accepted) {
              p.setSectionAutomationRemoteOwned(true);
              remoteSectionAutomationOwner = event.sourceId;
              remoteSectionAutomationCommandId = stringProperty(value, "id");
              remoteSectionAutomationSongId = activeSong;
            } else error = "Unknown automation section";
          }
        } else if (action == "automation.finish") {
          const auto save = property(automationEnvelope, "save");
          if (!save.isBool() || !p.isSectionAutomationEditing() ||
              remoteSectionAutomationOwner != event.sourceId ||
              editCommandId != remoteSectionAutomationCommandId ||
              target != p.getSectionAutomationEditingPartId()) {
            error = "Automation is not owned by this client";
          } else {
            accepted = p.finishSectionAutomationEditing(static_cast<bool>(save));
            if (accepted) {
              clearAutomationOwner();
              if (static_cast<bool>(save)) {
                const auto arrangement = p.getSongArrangementSnapshot();
                currentLoadedSong.arrangement = arrangement;
                accepted = SongLibraryStore::updateSongArrangement(activeSong, arrangement,
                                                                   p.isCustomArrangementActive());
                if (waveformBarPtr) waveformBarPtr->setCommittedSectionAutomation(arrangement);
                if (!accepted) error = "Unable to save section automation";
              }
            } else error = "Unable to finish section automation";
          }
        } else if (action == "automation.clear") {
          if (p.isSectionAutomationEditing() &&
              (remoteSectionAutomationOwner != event.sourceId ||
               editCommandId != remoteSectionAutomationCommandId ||
               target != p.getSectionAutomationEditingPartId()))
            error = "Automation is not owned by this client";
          else {
            if (p.isSectionAutomationEditing()) p.finishSectionAutomationEditing(false);
            clearAutomationOwner();
            accepted = p.clearSectionAutomation(target);
            if (accepted) {
              const auto arrangement = p.getSongArrangementSnapshot();
              currentLoadedSong.arrangement = arrangement;
              accepted = SongLibraryStore::updateSongArrangement(activeSong, arrangement,
                                                                 p.isCustomArrangementActive());
              if (waveformBarPtr) waveformBarPtr->setCommittedSectionAutomation(arrangement);
              if (!accepted) error = "Unable to save section automation";
            } else error = "Unknown automation section";
          }
        } else error = "Invalid automation action";
        nextRemoteSettingsMs = 0;
        nextRemotePublishMs = 0;
        remoteStateSignature.clear();
      } else if (p.isSectionAutomationEditing() && structureEdit) {
        error = "Finish section automation before editing the arrangement";
      } else if (automationControl &&
                 ((p.isSectionAutomationEditing() &&
                   (remoteSectionAutomationOwner != event.sourceId ||
                    !wrappedAutomationControl ||
                    stringProperty(automationEnvelope, "songId") != activeSong ||
                    stringProperty(automationEnvelope, "editCommandId") != remoteSectionAutomationCommandId)) ||
                  (!p.isSectionAutomationEditing() && wrappedAutomationControl))) {
        error = "Automation control is not owned by this client";
      } else if (action == "transport.scrubBegin" || action == "transport.scrubEnd" ||
          action == "transport.scrubCancel") {
        const auto payload = property(value, "value");
        const auto tokenValue = property(payload, "token");
        const auto songValue = property(payload, "songId");
        const auto token = tokenValue.toString();
        const auto song = songValue.toString();
        const auto activeSong = hasCurrentLoadedSong ? currentLoadedSong.id : juce::String{};
        if (stringProperty(value, "type") != "command" || !payload.isObject() ||
            !tokenValue.isString() || token.isEmpty() ||
            token.length() > 128 || !songValue.isString() || song.isEmpty() ||
            event.sourceId.empty() || stringProperty(value, "id").isEmpty()) {
          error = "Invalid practice scrub";
        } else if (action == "transport.scrubCancel") {
          const auto restore = property(payload, "restorePlayback");
          if (!restore.isBool()) error = "Invalid practice scrub cancellation";
          else {
            // Cleanup is safe after a song change, but can only restore a
            // pause whose current song, peer, token, and revision still match.
            const auto entry = remotePracticeScrubLeases.find(event.sourceId);
            if (entry != remotePracticeScrubLeases.end() && entry->second.token == token) {
              entry->second.cancel(p, activeSong, token,
                                   static_cast<bool>(restore) && song == activeSong);
              remotePracticeScrubLeases.erase(entry);
            }
            accepted = true;
          }
        } else if (song != activeSong || !p.hasLoadedSong() || songLoadBusy) {
          const auto entry = remotePracticeScrubLeases.find(event.sourceId);
          if (entry != remotePracticeScrubLeases.end() && entry->second.token == token) {
            entry->second.cancel(p, activeSong, token, false);
            remotePracticeScrubLeases.erase(entry);
          }
          error = "Practice scrub song changed";
        } else if (action == "transport.scrubBegin") {
          accepted = remotePracticeScrubLeases[event.sourceId].begin(p, song, token);
          if (!accepted) error = "Practice scrub superseded";
        } else {
          const auto seconds = property(payload, "seconds");
          const auto entry = remotePracticeScrubLeases.find(event.sourceId);
          if (!isNumber(seconds) || !std::isfinite(static_cast<double>(seconds)))
            error = "Invalid practice scrub position";
          else if (entry == remotePracticeScrubLeases.end() || entry->second.token != token)
            error = "Practice scrub not owned";
          else {
            accepted = entry->second.finish(p, song, token, static_cast<double>(seconds));
            remotePracticeScrubLeases.erase(entry);
            if (!accepted) error = "Practice scrub superseded";
          }
        }
      } else {
        if (wrappedAutomationControl) {
          value = value.clone();
          value.getDynamicObject()->setProperty("value", property(automationEnvelope, "controlValue"));
        }
        accepted = applyRemoteCommand(value, error);
      }
      if (accepted && action == "practice.listen") {
        remotePracticeOwner = p.getPracticeListeningMode() == 0 ? std::string{} : event.sourceId;
        remotePracticeCommandId = p.getPracticeListeningMode() == 0 ? juce::String{} : stringProperty(value, "id");
        remotePracticeRevision = p.getPracticeListeningRevision();
      } else if (accepted && (action == "practice.loop" || action == "practice.loopPrepare")) {
        remotePracticeLoopOwner = event.sourceId;
        remotePracticeLoopCommandId = stringProperty(value, "id");
        remotePracticeLoopRevision = p.getPracticeLoopRevision();
      } else if (accepted && action == "practice.loopRelease" &&
                 remotePracticeLoopOwner == event.sourceId &&
                 remotePracticeLoopRevision == p.getPracticeLoopRevision() &&
                 stringProperty(property(value, "value"), "commandId") == remotePracticeLoopCommandId &&
                 stringProperty(property(value, "value"), "songId") ==
                     (hasCurrentLoadedSong ? currentLoadedSong.id : juce::String{})) {
        p.clearPracticeLoop();
        remotePracticeLoopOwner.clear();
        remotePracticeLoopCommandId.clear();
        remotePracticeLoopRevision = 0;
      } else if (accepted && action == "practice.release" &&
                 !remotePracticeOwner.empty() && remotePracticeOwner == event.sourceId &&
                 stringProperty(property(value, "value"), "songId") ==
                     (hasCurrentLoadedSong ? currentLoadedSong.id : juce::String{})) {
        p.setPracticeListeningMode(-1, 0);
        remotePracticeOwner.clear();
        remotePracticeCommandId.clear();
        remotePracticeRevision = 0;
      }
      remoteSession.sendAck(stringProperty(value, "id"), accepted, error);
      if (accepted) {
        nextRemotePublishMs = 0;
        // Even a no-op command needs a snapshot after its ack so a client can
        // retire its preview. Continuous controls do not invalidate arrangement.
        remoteStateSignature.clear();
        if (action.startsWith("mix.") && (!p.isSectionAutomationEditing() || !automationControl)) {
          if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
            grid->noteRemoteMixChange();
        } else if (!action.startsWith("pads.") && !action.startsWith("practice.")) nextRemoteSettingsMs = 0;
        if (stringProperty(value, "action").startsWith("setlist.") || stringProperty(value, "action").startsWith("song.")) nextRemoteLibraryMs = 0;
      }
    }
  }
  refreshRemoteIndicator();
}

void MTControlsAudioProcessorEditor::publishRemoteMeters() {
  auto &p = static_cast<MTControlsAudioProcessor &>(processor);
  if (remoteSession.getClientCount() == 0) return;
  auto message = object(); auto *fields = message.getDynamicObject();
  fields->setProperty("v", 1); fields->setProperty("type", "meters");
  fields->setProperty("songId", hasCurrentLoadedSong ? currentLoadedSong.id : juce::String{});
  fields->setProperty("position", p.getSongCurrentPositionSeconds());
  fields->setProperty("playing", p.isSongPlaying());
  auto levels = object();
  auto add = [&](const juce::String &target, float left, float right) {
    auto value = object(); value.getDynamicObject()->setProperty("l", left); value.getDynamicObject()->setProperty("r", right);
    levels.getDynamicObject()->setProperty(juce::Identifier(target), value);
  };
  float left=0, right=0;
  p.getOutputMeterLevels(left,right); add("master",left,right);
  p.getRemoteMetronomeMeterLevels(left,right); add("click",left,right);
  p.getDynamicGuideMeterLevels(left,right); add("guide",left,right);
  int index = 0;
  if (hasCurrentLoadedSong) for (const auto &stem : currentLoadedSong.stems) {
    if (!stem.include) continue;
    const auto level = p.getStemLevel(index++); add(stem.stemId,level,level);
  }
  add("pads", p.getPadsOutputLevel(), p.getPadsOutputLevel());
  fields->setProperty("levels", levels);
  remoteSession.broadcast(juce::JSON::toString(message).toStdString());
}

void MTControlsAudioProcessorEditor::publishRemoteState() {
  if (remoteSession.getMode() !=
      playworship::remote::RemoteSession::Mode::Server)
    return;
  const auto now = juce::Time::currentTimeMillis();
  auto &p = static_cast<MTControlsAudioProcessor &>(processor);
  if (now >= nextRemoteLibraryMs || remoteLibraryCache.isVoid() || remoteWaveformSongId != (hasCurrentLoadedSong ? currentLoadedSong.id : juce::String{})) {
    const auto library = SongLibraryStore::loadLibrarySnapshot();
    auto root = object();
    auto *fields = root.getDynamicObject();
    fields->setProperty("activeSetlistId", library.activeSetlistId);
    juce::Array<juce::var> songs;
    for (const auto &song : library.songs) {
      songs.add(playworship::remote::encodeSong(song, false));
    }
    fields->setProperty("songs", juce::var(songs));
    juce::Array<juce::var> setlists;
    for (const auto &setlist : library.setlists) {
      auto item = object();
      item.getDynamicObject()->setProperty("id", setlist.id);
      item.getDynamicObject()->setProperty("name", setlist.name);
      juce::Array<juce::var> songIds;
      for (const auto &entry : setlist.items) {
        auto e = object(); e.getDynamicObject()->setProperty("id", entry.itemId);
        e.getDynamicObject()->setProperty("songId", entry.songId);
        e.getDynamicObject()->setProperty("autoPlay", entry.transitionToNext.autoPlay);
        songIds.add(e);
      }
      item.getDynamicObject()->setProperty("items", juce::var(songIds));
      setlists.add(item);
    }
    fields->setProperty("setlists", juce::var(setlists));
    fields->setProperty("activeSong", hasCurrentLoadedSong ? playworship::remote::encodeSong(currentLoadedSong, false) : object());
    const auto metadataSignature = juce::JSON::toString(root, true);
    if (metadataSignature != remoteLibraryMetadataSignature || remoteLibraryCache.isVoid()) {
      remoteLibraryMetadataSignature = metadataSignature;
      songs.clearQuick();
      for (const auto &song : library.songs) songs.add(playworship::remote::encodeSong(song));
      fields->setProperty("songs", juce::var(songs));
      fields->setProperty("activeSong", hasCurrentLoadedSong ? playworship::remote::encodeSong(currentLoadedSong) : object());
      remoteLibraryCache = root;
      ++remoteLibraryRevision;
      remoteLibraryDirty = true;
    }
    nextRemoteLibraryMs = now + 2000;
  }

  const juce::String songId =
      hasCurrentLoadedSong ? currentLoadedSong.id : juce::String{};
  if (songId != remoteWaveformSongId) {
    remoteWaveformSongId = songId;
    remoteWaveformBins.clear();
    remoteWaveformHash.clear();
  }
  if (songId.isNotEmpty() && !currentLoadedSong.blankSong && remoteWaveformBins.empty()) {
    if (waveformBarPtr != nullptr)
      remoteWaveformBins = waveformBarPtr->getWaveformBinsCopy();
    if (remoteWaveformBins.empty())
      remoteWaveformBins = SongLibraryStore::loadWaveformCache(songId);
    if (!remoteWaveformBins.empty())
      remoteWaveformHash =
          juce::SHA256(remoteWaveformBins.data(),
                       remoteWaveformBins.size() * sizeof(float))
              .toHexString();
  }

  if (songId != remoteSettingsSongId || now >= nextRemoteSettingsMs) {
    remoteSettingsSongId = songId;
    nextRemoteSettingsMs = now + 2000;
    ProjectSongSettings settings;
    if (songId.isNotEmpty()) {
      const auto projectId = projectIdentityForSong(currentLoadedSong);
      if (!isProjectBackedSong(currentLoadedSong) ||
          !ProjectStore::getCachedSettings(projectId, settings)) {
        SongLibraryStore::getSongArrangement(
            songId, settings.arrangement, settings.arrangementCustomized);
      }
      // Settings-sidecar writes are deferred. A committed automation change
      // must be visible in the first snapshot after its acknowledgement.
      settings.arrangement = p.getSongArrangementSnapshot();
      settings.arrangementCustomized = p.isCustomArrangementActive();
    }
    remoteSettingsCache = ProjectStore::serializeSettings(settings);
  }

  auto snapshot = object();
  auto *fields = snapshot.getDynamicObject();
  fields->setProperty("v", 1);
  fields->setProperty("type", "snapshot");

  if (!p.isSongPlaying()) fields->setProperty("position", p.getSongCurrentPositionSeconds());
  fields->setProperty("length", p.getSongLengthSeconds());
  fields->setProperty("sourceLength", p.getLoadedSongLengthSeconds());
  fields->setProperty("windowStart", p.getSongStartSeconds());
  fields->setProperty("windowEnd", p.getSongEndSeconds());
  fields->setProperty("windowEndCustomized", p.isSongEndCustomized());
  fields->setProperty("playing", p.isSongPlaying());
  fields->setProperty("fade", p.isFadeActive());
  fields->setProperty("tempo", p.getPlaybackTempoBpm());
  fields->setProperty("pitch", p.getPitchSemiTones());
  auto automation = object();
  automation.getDynamicObject()->setProperty("editing", p.isSectionAutomationEditing());
  automation.getDynamicObject()->setProperty("partId", p.getSectionAutomationEditingPartId());
  automation.getDynamicObject()->setProperty("partName", p.getSectionAutomationEditingPartName());
  automation.getDynamicObject()->setProperty("commandId",
      p.isSectionAutomationEditing() && !remoteSectionAutomationOwner.empty()
          ? remoteSectionAutomationCommandId : juce::String{});
  fields->setProperty("sectionAutomation", automation);
  const auto practiceRange = p.getPracticeLoopRange();
  fields->setProperty("practiceLoopActive", p.isPracticeLoopActive());
  fields->setProperty("practiceLoopPrepared", p.hasPracticeLoopPreparation());
  fields->setProperty("practiceLoopStart", practiceRange.getStart());
  fields->setProperty("practiceLoopEnd", practiceRange.getEnd());
  fields->setProperty("practiceLoopMinimumDuration", p.getMinimumPracticeLoopDuration());
  fields->setProperty("practiceLoopError", p.getPracticeLoopError());
  fields->setProperty("practiceLoopCommandId",
      !remotePracticeLoopOwner.empty() &&
          p.getPracticeLoopRevision() == remotePracticeLoopRevision
      ? remotePracticeLoopCommandId : juce::String{});
  const int practiceMode = p.getPracticeListeningMode();
  const int practiceIndex = p.getPracticeListeningStem();
  juce::String practiceStemId;
  if (hasCurrentLoadedSong && practiceMode != 0) {
    int index = 0;
    for (const auto &stem : currentLoadedSong.stems) {
      if (!stem.include) continue;
      if (index++ == practiceIndex) { practiceStemId = stem.stemId; break; }
    }
  }
  fields->setProperty("practiceMode", practiceMode);
  fields->setProperty("practiceStemId", practiceStemId);
  fields->setProperty("practiceCommandId",
      !remotePracticeOwner.empty() && practiceMode != 0 &&
              p.getPracticeListeningRevision() == remotePracticeRevision
          ? remotePracticeCommandId : juce::String{});
  fields->setProperty("waveformHash", remoteWaveformHash);
  fields->setProperty("settings", remoteSettingsCache);
  fields->setProperty("libraryRevision", remoteLibraryRevision);
  auto song = property(remoteLibraryCache, "activeSong");
  // libraryRevision already tracks metadata/artwork changes. Compare only its
  // identity on the UI thread; the sender serializes the full song afterwards.
  fields->setProperty("song", stringProperty(song, "id"));

  juce::Array<juce::var> mixer;
  mixer.add(makeMixerRow("master", L10n::tr("Master", "Master"),
                         p.getMasterVolume(), p.getMasterGain(),
                         p.isMasterMuted(), false));
  if (hasCurrentLoadedSong) {
    int index = 0;
    for (const auto &stem : currentLoadedSong.stems) {
      if (!stem.include) continue;
      if (index >= p.getLoadedStemCount()) break;
      mixer.add(makeMixerRow(stem.stemId, stem.displayName,
                             p.getStemVolume(index), p.getStemGain(index),
                             p.isStemMuted(index), p.isStemSoloed(index)));
      mixer.getReference(mixer.size()-1).getDynamicObject()->setProperty("outputChannel", p.getStemOutputChannel(index));
      auto *stemFields = mixer.getReference(mixer.size()-1).getDynamicObject();
      stemFields->setProperty("sectionAutomated", p.isStemSectionAutomated(index));
      stemFields->setProperty("baseVolume", p.getStemBaseVolume(index));
      stemFields->setProperty("baseGain", p.getStemBaseGain(index));
      stemFields->setProperty("baseMute", p.isStemBaseMuted(index));
      ++index;
    }
  }
  mixer.add(makeMixerRow("click", L10n::tr("Click", "Click"),
                         p.getMetronomeVolume(), p.getMetronomeGain(),
                         p.isMetronomeMuted(), p.isMetronomeSolo()));
  mixer.add(makeMixerRow("guide", L10n::tr("Guía", "Guide"),
                         p.getDynamicGuideVolume(), p.getDynamicGuideGain(),
                         p.isDynamicGuideMuted(), p.isDynamicGuideSolo()));
  fields->setProperty("mixer", juce::var(mixer));
  auto ui = object();
  ui.getDynamicObject()->setProperty("getSongFadeOutSeconds", p.getSongFadeOutSeconds());
  ui.getDynamicObject()->setProperty("isCustomArrangementActive", p.isCustomArrangementActive());
  ui.getDynamicObject()->setProperty("isMetronomeEnabled", p.isMetronomeEnabled());
  ui.getDynamicObject()->setProperty("getMetronomeBpm", p.getMetronomeBpm());
  ui.getDynamicObject()->setProperty("getClickDivision", p.getClickDivision());
  ui.getDynamicObject()->setProperty("isClickDivisionEnabled", p.isClickDivisionEnabled());
  // Authoritative guide source/availability for PracticePanel. The settings
  // sidecar may lag a source change and cannot confirm an audibility toggle.
  ui.getDynamicObject()->setProperty("isDynamicGuideEnabled", p.isDynamicGuideEnabled());
  ui.getDynamicObject()->setProperty("hasDynamicGuideAssets", p.hasDynamicGuideAssets());
  const auto jumpState = p.getArrangementJumpState();
  ui.getDynamicObject()->setProperty("getPendingArrangementPartJumpIndex", jumpState.first);
  ui.getDynamicObject()->setProperty("getPreparingArrangementPartJumpIndex", jumpState.second);
  ui.getDynamicObject()->setProperty("getArrangementPartLoopIndex", p.getArrangementPartLoopIndex());
  ui.getDynamicObject()->setProperty("getAvailableOutputChannelCount", p.getAvailableOutputChannelCount());
  juce::Array<juce::var> outputs;
  for (const auto &name : p.getAvailableOutputChannelNames()) outputs.add(name);
  ui.getDynamicObject()->setProperty("outputs", outputs);
  ui.getDynamicObject()->setProperty("getPadsVolume", p.getPadsVolume());
  ui.getDynamicObject()->setProperty("getPadsGain", p.getPadsGain());
  ui.getDynamicObject()->setProperty("isPadsEnabled", p.isPadsEnabled());
  ui.getDynamicObject()->setProperty("isPadsPackLoaded", p.isPadsPackLoaded());
  ui.getDynamicObject()->setProperty("isPadsPreloadRunning", p.isPadsPreloadRunning());
  ui.getDynamicObject()->setProperty("getPadsPackStatusText", p.getPadsPackStatusText());
  ui.getDynamicObject()->setProperty("getActivePadNoteIndex", p.getActivePadNoteIndex());
  ui.getDynamicObject()->setProperty("getActivePadOctave", p.getActivePadOctave());
  ui.getDynamicObject()->setProperty("isPadsSectionAutomated", p.isPadsSectionAutomated());
  ui.getDynamicObject()->setProperty("getPadsBaseVolume", p.getPadsBaseVolume());
  ui.getDynamicObject()->setProperty("getPadsBaseGain", p.getPadsBaseGain());
  ui.getDynamicObject()->setProperty("isPadsBaseEnabled", p.isPadsBaseEnabled());
  ui.getDynamicObject()->setProperty("getBasePadNoteIndex", p.getBasePadNoteIndex());
  ui.getDynamicObject()->setProperty("getBasePadOctave", p.getBasePadOctave());
  fields->setProperty("ui", ui);
  fields->setProperty("sectionControls", playworship::remote::encodeSectionControls(p, songId));

  const auto signature = juce::JSON::toString(snapshot, true);
  if (signature == remoteStateSignature) return;
  remoteStateSignature = signature;
  fields->setProperty("seq", ++remoteSnapshotSequence);
  fields->setProperty("position", p.getSongCurrentPositionSeconds());
  fields->setProperty("song", song);
  if (remoteLibraryDirty) fields->setProperty("library", remoteLibraryCache);
  remoteSession.publishState(snapshot, remoteWaveformBins, remoteWaveformHash);
  remoteLibraryDirty = false;
}

bool MTControlsAudioProcessorEditor::applyRemoteCommand(
    const juce::var &command, juce::String &error) {
  if (stringProperty(command, "type") != "command" ||
      static_cast<int>(property(command, "v")) != 1 ||
      stringProperty(command, "id").isEmpty()) {
    error = "Invalid command";
    return false;
  }
  auto &p = static_cast<MTControlsAudioProcessor &>(processor);
  const auto action = stringProperty(command, "action");
  const auto target = stringProperty(command, "target");
  const auto value = property(command, "value");
  if (action == "practice.loop" || action == "practice.loopPrepare" ||
      action == "practice.loopRelease") {
    const auto songId = property(value, "songId");
    if (!value.isObject() || !songId.isString() || songId.toString().isEmpty()) {
      error = "invalid-range"; return false;
    }
    if (action == "practice.loopRelease") {
      if (!property(value, "commandId").isString() || stringProperty(value, "commandId").isEmpty()) {
        error = "rejected"; return false;
      }
      return true; // The event handler checks peer, command ID and revision.
    }
    if (!hasCurrentLoadedSong || songLoadBusy || !p.hasLoadedSong() ||
        songId.toString() != currentLoadedSong.id) {
      error = "rejected"; return false;
    }
    const auto start = property(value, "start");
    const auto end = property(value, "end");
    if (!isNumber(start) || !std::isfinite(static_cast<double>(start)) ||
        (action == "practice.loop" && (!isNumber(end) || !std::isfinite(static_cast<double>(end))))) {
      error = "invalid-range"; return false;
    }
    const bool accepted = action == "practice.loopPrepare"
        ? p.preparePracticeLoop(static_cast<double>(start))
        : p.setPracticeLoopRange(static_cast<double>(start), static_cast<double>(end));
    if (!accepted) error = p.getPracticeLoopError().isEmpty() ? juce::String("rejected") : p.getPracticeLoopError();
    return accepted;
  }
  if (action == "practice.listen" || action == "practice.release") {
    const auto modeValue = property(value, "mode");
    const auto songId = property(value, "songId");
    if (!value.isObject() || !modeValue.isInt() || !songId.isString() ||
        static_cast<int>(modeValue) < 0 || static_cast<int>(modeValue) > 2 ||
        (action == "practice.release" && static_cast<int>(modeValue) != 0)) {
      error = "Invalid practice listening mode";
      return false;
    }
    // Release is a lifecycle cleanup, not a new listening intent. The event
    // handler restores only the matching current owner; stale releases are no-ops.
    if (action == "practice.release") return true;
    if (songId.toString() != (hasCurrentLoadedSong ? currentLoadedSong.id : juce::String{})) {
      error = "Practice song changed";
      return false;
    }
    const int mode = static_cast<int>(modeValue);
    if (mode == 0) { p.setPracticeListeningMode(-1, 0); return true; }
    if (!hasCurrentLoadedSong || songLoadBusy) {
      error = "No active practice song";
      return false;
    }
    int index = 0;
    for (const auto &stem : currentLoadedSong.stems) {
      if (!stem.include) continue;
      const int mappedIndex = index++;
      if (stem.stemId != target || mappedIndex >= p.getLoadedStemCount()) continue;
      p.setPracticeListeningMode(mappedIndex, mode);
      if (p.getPracticeListeningStem() != mappedIndex || p.getPracticeListeningMode() != mode) {
        error = "Invalid practice instrument";
        return false;
      }
      return true;
    }
    error = "Unknown practice instrument";
    return false;
  }
  if (action.startsWith("song.") || action.startsWith("setlist."))
    if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
      grid->flushPendingPersistenceNow();
  if (action.startsWith("setlist.") && action != "setlist.select") {
    bool ok = false;
    if (action == "setlist.create" && value.isString() && value.toString().trim().isNotEmpty()) {
      const auto id = SongLibraryStore::createSetlist(value.toString().substring(0, 200));
      ok = id.isNotEmpty() && SongLibraryStore::setActiveSetlistId(id);
    } else if (action == "setlist.rename" && value.isString() && value.toString().trim().isNotEmpty()) ok = SongLibraryStore::renameSetlist(target, value.toString().substring(0,200));
    else if (action == "setlist.delete" && SongLibraryStore::loadLibrarySnapshot().setlists.size() > 1) ok = SongLibraryStore::deleteSetlist(target);
    else if (action == "setlist.add" && value.isString()) ok = SongLibraryStore::addSongToSetlist(target, value.toString());
    else if (action == "setlist.remove" && value.isString()) ok = SongLibraryStore::removeSongFromSetlist(target, value.toString());
    else if (action == "setlist.move" && value.isArray() && value.size() == 2)
      ok = SongLibraryStore::moveSongInSetlist(target, static_cast<int>(value[0]), static_cast<int>(value[1]));
    else if (action == "setlist.autoplay" && property(value,"enabled").isBool())
      ok = SongLibraryStore::setAutoPlayNextForSetlist(target, stringProperty(value,"songId"), static_cast<bool>(property(value,"enabled")));
    if (ok && songListPanelRaw) songListPanelRaw->refreshFromStorageView();
    if (!ok) error = "Invalid setlist action";
    return ok;
  }
  if (action == "click.metronome" || action == "click.enabled" || action == "guide.enabled") {
    if (!value.isBool()) { error = "Invalid switch"; return false; }
    const bool enabled = static_cast<bool>(value);
    if (action == "click.metronome") {
      p.setMetronomeEnabled(enabled);
      persistClickDynMetronomeEnabledForActiveSong(enabled);
    } else if (action == "click.enabled") p.setClickDivisionEnabled(enabled);
    else {
      auto settings = p.getDynamicGuideSettings(); settings.enabled = enabled; p.setDynamicGuideSettings(settings);
      SongLibraryStore::updateDynamicGuideForActiveSetlist(currentLoadedSong.id, settings, true);
      if (auto *grid = dynamic_cast<ChannelGridPanel *>(channelGridPanel.get()))
        grid->setGuideSourceMode(enabled ? ChannelGridPanel::GuideSourceMode::DynamicBySections : ChannelGridPanel::GuideSourceMode::SongGuide, false);
    }
    return true;
  }
  if (action == "click.beats") {
    if (!value.isInt() || static_cast<int>(value) < 1 || static_cast<int>(value) > 16) { error = "Invalid meter"; return false; }
    p.setMetronomeBeatsPerBar(static_cast<int>(value)); return true;
  }
  if (action == "click.division") {
    if (!value.isInt() || static_cast<int>(value) < 1 || static_cast<int>(value) > 7) { error = "Invalid division"; return false; }
    p.setClickDivision(static_cast<int>(value)); return true;
  }
  if (action.startsWith("window.")) {
    if (p.isBlankSongLoaded()) { error = "Blank songs have no playback window"; return false; }
    if (!hasCurrentLoadedSong || !isNumber(value) || !std::isfinite(static_cast<double>(value)) || static_cast<double>(value) < 0) { error = "Invalid window"; return false; }
    const double seconds = static_cast<double>(value);
    if (action == "window.start") p.setSongStartSeconds(seconds);
    else if (action == "window.end") p.setSongEndSeconds(seconds);
    else if (action == "window.fade") p.setSongFadeOutSeconds(seconds);
    else { error = "Invalid window action"; return false; }
    maybePersistSongWindowForActiveSong(); return true;
  }
  if (action.startsWith("arrange.")) {
    if (p.isBlankSongLoaded()) { error = "Blank songs have no sections"; return false; }
    if (playworship::remote::isSectionControlAction(action))
      return playworship::remote::applySectionControl(p,
          hasCurrentLoadedSong ? currentLoadedSong.id : juce::String{},
          songLoadBusy, action, value, property(command, "target"), error);
    if (!hasCurrentLoadedSong) { error = "No active song"; return false; }
    if (action == "arrange.set" || action == "arrange.reset") {
      ProjectSongSettings settings;
      if (action == "arrange.set") {
        if (!playworship::remote::validateArrangementAutomation(property(value, "arrangement"))) {
          error = "Invalid section automation"; return false;
        }
        if (!ProjectStore::deserializeSettings(value, settings) || settings.arrangement.parts.size() > 1024) { error = "Invalid arrangement"; return false; }
        for (const auto &part : settings.arrangement.parts)
          if (!std::isfinite(part.startSec) || !std::isfinite(part.endSec) || part.startSec < 0 || part.endSec < part.startSec) { error = "Invalid section"; return false; }
        SongLibraryStore::updateSongArrangement(currentLoadedSong.id, settings.arrangement, settings.arrangementCustomized);
      } else {
        SongLibraryStore::resetSongArrangement(currentLoadedSong.id);
        SongLibraryStore::getSongArrangement(currentLoadedSong.id, settings.arrangement, settings.arrangementCustomized);
      }
      p.setSongArrangement(settings.arrangement, settings.arrangementCustomized);
      if (waveformBarPtr) waveformBarPtr->setArrangement(settings.arrangement, settings.arrangementCustomized);
    } else { error = "Invalid arrangement action"; return false; }
    return true;
  }
  if (action.startsWith("pads.")) {
    if (action == "pads.enabled" && value.isBool()) p.setPadsEnabled(static_cast<bool>(value));
    else if ((action == "pads.volume" || action == "pads.gain") && isNumber(value) && std::isfinite(static_cast<double>(value)) && static_cast<double>(value) >= 0 && static_cast<double>(value) <= (action == "pads.volume" ? 1.0 : 2.0)) {
      if (action == "pads.volume") p.setPadsVolume(static_cast<float>(static_cast<double>(value)));
      else p.setPadsGain(static_cast<float>(static_cast<double>(value)));
    } else if (action == "pads.note" && value.isArray() && value.size() == 2 && value[0].isInt() && value[1].isInt() && static_cast<int>(value[0]) >= 0 && static_cast<int>(value[0]) < 12 && static_cast<int>(value[1]) >= -2 && static_cast<int>(value[1]) <= 2) p.triggerPadNote(static_cast<int>(value[0]), static_cast<int>(value[1]));
    else if (action == "pads.stop" && value.isInt() && static_cast<int>(value) >= 0 && static_cast<int>(value) < 12) p.stopPadNote(static_cast<int>(value));
    else if (action == "pads.preload") p.startPadsPreloadAsync();
    else { error = "Invalid pads action"; return false; }
    return true;
  }
  if (action == "tempoMap.set") {
    if (p.isBlankSongLoaded()) { error = "Blank songs use the tempo control"; return false; }
    if (!hasCurrentLoadedSong || !value.isArray() || value.size() > 1024) { error = "Invalid tempo map"; return false; }
    const auto map = playworship::remote::decodeTempoMap(value);
    for (const auto &r : map.regions) if (!std::isfinite(r.startSec) || !std::isfinite(r.endSec) || !std::isfinite(r.bpm) || r.startSec < 0 || r.endSec < r.startSec || r.bpm < 20 || r.bpm > 300) { error = "Invalid tempo region"; return false; }
    currentLoadedSong.tempoMap = map;
    SongLibraryStore::updateSong(currentLoadedSong);
    if (waveformBarPtr) waveformBarPtr->setTempoMap(map);
    nextRemoteLibraryMs = 0;
    return true;
  }
  if (action == "setlist.select") {
    if (!SongLibraryStore::setActiveSetlistId(target)) {
      error = "Unknown setlist";
      return false;
    }
    if (songListPanelRaw != nullptr)
      songListPanelRaw->refreshFromStorageView();
    nextRemoteLibraryMs = 0;
    return true;
  }
  if (action == "song.load") {
    if (songLoadBusy || loadSongAction == nullptr) {
      error = "Song loader busy";
      return false;
    }
    const auto songs = SongLibraryStore::loadSongs();
    const auto it = std::find_if(songs.begin(), songs.end(),
                                 [&target](const ImportedSongData &song) {
                                   return song.id == target;
                                 });
    if (it == songs.end()) {
      error = "Unknown song";
      return false;
    }
    int index = -1;
    if (songListPanelRaw != nullptr)
      for (int i = 0; i < songListPanelRaw->getSongCount(); ++i)
        if (const auto *row = songListPanelRaw->getSongPtr(i);
            row != nullptr && row->id == target) {
          index = i;
          break;
        }
    if (hasCurrentLoadedSong && currentLoadedSong.id == target)
      return true;
    if (!loadSongAction(*it, index)) {
      error = "Could not load song";
      return false;
    }
    if (index >= 0 && songListPanelRaw != nullptr)
      songListPanelRaw->setSelectedSongIndex(index);
    return true;
  }
  if (action == "song.previous" || action == "song.next") {
    auto &callback = action == "song.previous" ? prevSongAction : nextSongAction;
    if (!callback) {
      error = "No adjacent song";
      return false;
    }
    callback();
    return true;
  }
  if (action == "transport.toggle") {
    p.toggleSongPlayPause();
    return true;
  }
  if (action == "transport.stop") {
    p.stopSong();
    return true;
  }
  if (action == "transport.seek") {
    if (!isNumber(value) || !std::isfinite(static_cast<double>(value))) {
      error = "Invalid seek";
      return false;
    }
    p.seekSongTo(juce::jlimit(p.getSongStartSeconds(), p.getSongEndSeconds(),
                              static_cast<double>(value)));
    return true;
  }
  if (action == "transport.fade") {
    if (!value.isBool()) {
      error = "Invalid fade";
      return false;
    }
    p.setFadeStopsPlayback(false);
    p.setFadeActive(static_cast<bool>(value));
    return true;
  }
  if (!hasCurrentLoadedSong) {
    error = "No active song";
    return false;
  }
  if (action == "tempo.set") {
    if (!isNumber(value) || !std::isfinite(static_cast<double>(value)) ||
        static_cast<double>(value) < 20.0 ||
        static_cast<double>(value) > 300.0) {
      error = "Invalid tempo";
      return false;
    }
    p.setPlaybackTempoBpm(static_cast<double>(value));
    SongLibraryStore::updateTempoBpmForActiveSetlist(
        currentLoadedSong.id, p.getPlaybackTempoBpm());
    return true;
  }
  if (action == "pitch.set") {
    if (!isNumber(value) || static_cast<int>(value) < -12 ||
        static_cast<int>(value) > 12) {
      error = "Invalid pitch";
      return false;
    }
    p.setPitchSemiTones(static_cast<int>(value));
    SongLibraryStore::updatePitchSemitonesForActiveSetlist(
        currentLoadedSong.id, p.getPitchSemiTones());
    return true;
  }
  if (action != "mix.volume" && action != "mix.gain" &&
      action != "mix.mute" && action != "mix.solo") {
    error = "Unknown action";
    return false;
  }
  const bool scalar = action == "mix.volume" || action == "mix.gain";
  if ((scalar && (!isNumber(value) ||
                  !std::isfinite(static_cast<double>(value)) ||
                  static_cast<double>(value) < 0.0 ||
                  static_cast<double>(value) >
                      (action == "mix.volume" ? 1.0 : 2.0))) ||
      (!scalar && !value.isBool())) {
    error = "Invalid mix value";
    return false;
  }
  const float numeric = scalar ? static_cast<float>(static_cast<double>(value))
                               : 0.0f;
  const bool flag = !scalar && static_cast<bool>(value);
  if (target == "master") {
    if (action == "mix.volume") {
      p.setMasterVolume(numeric);
    } else if (action == "mix.gain") {
      p.setMasterGain(numeric);
    } else if (action == "mix.mute") {
      p.setMasterMute(flag);
    } else {
      error = "Master has no solo";
      return false;
    }
    return true;
  }
  if (target == "click" || target == "guide") {
    if (target == "click") {
      if (action == "mix.volume") {
        p.setMetronomeVolume(numeric);
      } else if (action == "mix.gain") {
        p.setMetronomeGain(numeric);
      } else if (action == "mix.mute") {
        p.setMetronomeMuted(flag);
      } else {
        p.setMetronomeSolo(flag);
      }
    } else {
      if (action == "mix.volume") {
        p.setDynamicGuideVolume(numeric);
      } else if (action == "mix.gain") {
        p.setDynamicGuideGain(numeric);
      } else if (action == "mix.mute") {
        p.setDynamicGuideMuted(flag);
      } else {
        p.setDynamicGuideSolo(flag);
      }
    }
    return true;
  }
  int mappedIndex = 0;
  for (const auto &stem : currentLoadedSong.stems) {
    if (!stem.include) continue;
    const int index = mappedIndex++;
    if (stem.stemId != target || index >= p.getLoadedStemCount()) continue;
    if (action == "mix.volume") {
      p.setStemVolume(index, numeric);
    } else if (action == "mix.gain") {
      p.setStemGain(index, numeric);
    } else if (action == "mix.mute") {
      p.setStemMute(index, flag);
    } else {
      p.setStemSolo(index, flag);
    }
    return true;
  }
  error = "Unknown mixer channel";
  return false;
}
