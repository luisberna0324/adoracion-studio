# Skill routing

This file records which specialized workflows to consider at the start of a
new conversation. It does not install skills by itself; it tells the assistant
which built-in skill or workflow should be used when available.

## Repository work

Use normal Codex coding workflow for:

- C++/JUCE implementation.
- CMake changes.
- UI/layout changes in source code.
- Audio engine changes.
- Persistence and local file handling.
- Tests and verification.

Always load `AGENTS.md`, `AGENT.md`, and `project-memory.md` first.

## GitHub work

Use the GitHub skill/plugin when the user asks to:

- Inspect issues or pull requests.
- Address PR comments.
- Fix failing GitHub Actions checks.
- Push a branch or open a pull request.

Before publishing changes, confirm the intended scope if the worktree contains
unrelated local edits.

## Documents, PDFs, spreadsheets, presentations

Use the corresponding document skill only when the user explicitly asks to
create, edit, inspect, render, or verify one of these artifacts:

- `.docx` / Word documents.
- PDFs.
- `.xlsx`, `.csv`, or spreadsheets.
- PowerPoint or slide decks.

These skills are not part of the normal C++ app workflow.

## Images and visual assets

Use image generation or image editing skills only when the user asks for bitmap
visual assets, mockups, icons, screenshots, or image transformations.

Prefer editing native code or vector assets directly when the project already
contains the relevant asset format.

## Browser verification

Use browser automation only for web targets or local HTML/web tooling. The JUCE
standalone app requires local app launch and manual/visual validation instead.

## Automations and reminders

Use the automation tools only when the user asks to create, view, update, or
delete reminders, recurring tasks, monitors, or follow-ups.
