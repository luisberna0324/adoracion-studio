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
