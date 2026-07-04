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
- In virtual waveform/clip mode, every visible transport projection must use
  the visual timeline, not raw source `position / songLength`. This includes
  follow tracking, seek ruler, mini map, pending-jump markers, and duplicated
  clip playheads; otherwise duplicated source ranges appear frozen or detached.
- Dynamic Guide announcements in custom virtual arrangements must follow the
  active arrangement part index, not source-time lookahead triggers. Duplicated
  clips can jump backward in source time, so `guideTriggerFrame` lookahead can
  announce the section after the duplicate instead of the duplicate itself. Keep
  the one-bar preannounce by looking ahead to the next arrangement index from
  the current virtual part end.
- The waveform arrangement editor is expected to support standard editing
  shortcuts: undo/redo and copy/paste of selected sections. Pasted virtual clips
  must get new ids while preserving their original source ranges.
- Entering waveform edit mode should not shrink the waveform work area. Editing
  controls should be compact/floating, and waveform expansion should remain a
  separate user-controlled action.
- Legacy song sections and virtual arrangement clips must coexist without eager
  migration. Keep legacy `parts` as fallback/source sections, prefer `clips`
  for playback when present, and only materialize/persist `clips` after the
  user actually enters the new editable arrangement flow.
- Dynamic Guide should pre-announce normal musical sections only. Count cues
  and utility cues are entry-triggered and must not be pre-announced.
- Editing an active virtual arrangement must not stop playback. Updating the
  arrangement scheduler while transport is playing should preserve the current
  transport state and source position.
- Duplicated arrangement clips must preserve the original `sourceSectionId`;
  otherwise the UI can treat duplicates as new source sections and pollute
  restoration/context menus.
- Paste and add actions in the waveform arrangement editor should insert at the
  current seek/playhead position, not after the selected section. Deleted source
  sections restored from the context menu remain the exception: they return to
  their original source-order position.
- Virtual arrangement resizing differs by instance type. Primary/source clips
  update `sourceStartSec/sourceEndSec` and keep playback equal to source, while
  duplicate clips update only `playbackStartSec/playbackEndSec` within the
  original source bounds and close virtual time when shortened.
- When primary/source clips are shortened, preserve the unassigned source space
  as an internal gap in both the waveform visual timeline and the audio
  scheduler. Keep those gap entries internal only; UI loop/jump/current indices
  must still refer to visible arrangement sections.
- In arrangement edit mode, dragging inside a section body should select only;
  resizing belongs to the section edges. This prevents accidental movement of
  section markers while editing.
- Clip mute is not delete: it must preserve virtual duration and timeline
  position while muting only musical stems. Click, guide stems, metronome, and
  dynamic guide should remain available during muted clips.
- Section resize should preview locally while the pointer is down. Avoid
  reflowing the whole virtual timeline on every drag tick; apply the final
  timeline duration change when the user releases the drag.
- Arrangement reset should clear the editable virtual clip layer and restore
  captured source sections. Do not reset by persisting an empty intermediate
  arrangement after the store has already restored defaults.
