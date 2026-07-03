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
