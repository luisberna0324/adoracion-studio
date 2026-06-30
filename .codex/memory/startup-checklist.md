# Startup checklist

Use this checklist at the beginning of a new conversation before making code
changes.

## 1. Load context

- Read `AGENTS.md`.
- Read `AGENT.md`.
- Read `.codex/memory/project-memory.md`.
- Read `.codex/memory/lessons-learned.md`.
- For specialized work, read `.codex/memory/skill-routing.md`.

## 2. Inspect current state

Run:

```bash
git status --short
```

Then inspect only the files relevant to the request. Do not assume the worktree
is clean, and do not revert unrelated changes.

## 3. Find the implementation area

Common entry points:

- UI/layout: `src/PluginEditor.cpp`, `src/PluginEditor.h`
- Audio/transport/stems: `src/PluginProcessor.cpp`, `src/PluginProcessor.h`
- Build/package/release: `CMakeLists.txt`, `CMakePresets.json`, `scripts/`,
  `packaging/`
- Docs and product rules: `AGENT.md`, `README.md`, `.codex/memory/`

Use `rg` to locate symbols and behavior before editing.

## 4. Before editing

- Identify whether the change affects UI, audio, persistence, build, or docs.
- Check the relevant contracts in `AGENT.md`.
- Keep edits narrow.
- Announce the edit scope before modifying files.

## 5. Verification

For functional changes, start with the smallest relevant command, then build the
standalone target when practical:

```bash
cmake --build /Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build --target MTControls_Standalone -j
```

Manual app validation is required for changes that affect:

- Loading songs or stems.
- `Play/Pause/Stop/Seek`.
- Space/Esc shortcuts.
- Channel `Vol`, `S`, `M`.
- Per-channel meters.
- Waveform rendering or playhead.
- Setlist behavior.

## 6. End-of-task learning

Before the final response, decide whether the task revealed a reusable lesson.
If yes, update `.codex/memory/lessons-learned.md` or
`.codex/memory/project-memory.md`.

Record only stable guidance that helps future conversations know what to do.
Avoid logging routine edits or one-off implementation details.
