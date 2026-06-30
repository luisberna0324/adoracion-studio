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
- For PlayWorship auto-updates, keep the in-app code lightweight and put file
  replacement/rollback in the independent `PlayWorshipUpdater`; the main app
  must not try to replace its own bundle/executable while running.
