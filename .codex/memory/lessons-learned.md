# Lessons learned

This file is the long-term learning log for future Codex conversations in this
repository. Update it when a task teaches something reusable.

## How to use this file

- Read it at the start of every new conversation.
- Add short entries only when they affect future decisions.
- Prefer durable lessons over task history.
- If an entry becomes outdated, edit or remove it.
- Keep each lesson actionable: what to do, when to do it, and why.

## Current lessons

- The user wants the assistant to keep learning across conversations by
  updating repository memory files with reusable decisions, preferences,
  pitfalls, and validation knowledge.
- Bungee is the intended pitch/tempo backend. When comparing or restoring
  release tags, treat removal of Bungee CMake wiring, `TimeStretchProcessor`,
  `BungeeProcessor`, or bundle verification as a regression unless the user
  explicitly says otherwise.
- Duplicated arrangement sections are inserted virtual clips: `startSec/endSec`
  are arrangement time and may extend song length, while
  `sourceStartSec/sourceEndSec` identify the original audio. Keep UI seek and
  playhead in arrangement time, keep the master waveform visible as the fixed
  base reference, draw only duplicated/virtual clips as waveform overlays, and
  remap to source frames inside `MTControlsAudioProcessor`/`SectionScheduler`.
- Original sections must not replace the master waveform with per-section clip
  overlays; they only mark the master structure. Once duplicated clips exist,
  the primary waveform represents the arranged master by mapping inserted clips
  and shifted implicit master gaps back to source audio.
- Duplicating a section means inserting that source clip into the primary
  arrangement waveform, not drawing it as an overlay on the original marker.
  The unmarked remainder of the master is implicit arrangement content and must
  shift after inserted clips in both waveform rendering and playback mapping.
- Original arrangement sections are movable markers over the arranged master;
  moving/resizing them must not remap or displace the master waveform. Only
  duplicated sections (`sourcePartId` set) are inserted audio clips that change
  waveform/playback source mapping.
- For original arrangement sections (`sourcePartId` empty),
  `sourceStartSec/sourceEndSec` are not authoritative clip metadata; derive
  their source position from the current arrangement/master mapping. Only
  duplicated sections should preserve and use explicit source ranges.
- Playback through a custom arrangement must be continuous from arrangement
  start to arrangement end, including unmarked master regions. Section
  boundaries should update guide/current-section state but must not auto-seek,
  skip gaps, or stop playback unless the user explicitly loops/jumps.
- Timeline ruler seeking must target any point in the full arranged song,
  including regions with no section marker. Do not clamp custom-arrangement
  seeks to the nearest/current section unless a section loop is active.
- Dynamic guide musical sections should be preannounced from their trigger
  frame, not announced on section entry. Counts and utility cues are the
  exception and trigger on entry/seek. If a numbered musical key such as
  `interlude 2` has no guide asset, fall back to the canonical base key
  (`interlude`) so repeated sections still announce.
