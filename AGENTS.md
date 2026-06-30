# AGENTS.md - Startup instructions for Codex

This file is the entry point for new Codex conversations in this repository.
Read it first, then load the project memory files listed below before making
non-trivial code changes.

## Required startup context

1. Read `AGENT.md`.
2. Read `.codex/memory/project-memory.md`.
3. Read `.codex/memory/lessons-learned.md`.
4. Read `.codex/memory/startup-checklist.md`.
5. Read `.codex/memory/skill-routing.md` when the task touches build, audio,
   UI, persistence, packaging, or release work.

## Operating rules

- Treat `AGENT.md` as the product and engineering source of truth.
- Treat `.codex/memory/project-memory.md` as the persistent working memory.
- Keep changes scoped to the user request and the current architecture.
- Do not revert user changes or unrelated work.
- Prefer `rg`/`rg --files` for repository searches.
- Use `apply_patch` for manual file edits.
- After functional changes, run the smallest relevant verification first, then
  the full required validation when practical.

## Memory maintenance

Update `.codex/memory/project-memory.md` when a decision should survive into
future conversations, for example:

- Product rules that should not be rediscovered.
- Known implementation contracts.
- Important file locations.
- Validation commands that changed.
- Repeated pitfalls or regressions.

Also update `.codex/memory/lessons-learned.md` whenever a task reveals a
repeatable lesson, for example:

- A bug pattern that should be avoided.
- A build/run command that differs from expected docs.
- A user preference about implementation style, UX, naming, or workflow.
- A verification step that caught an issue.
- A decision made after comparing alternatives.

Do not use memory files as a raw changelog. Keep them concise, current, and
actionable.
