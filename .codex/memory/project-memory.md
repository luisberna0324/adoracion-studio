# Project memory - MT Controls / PlayWorship

## Repository identity

- Project root: `/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin`
- App type: JUCE standalone/plugin project focused on multitrack playback,
  mixer channels, setlist flow, waveform, and local persistence.
- Main development target for local validation: `MTControls_Standalone`.
- Product/engineering source of truth: `AGENT.md`.
- API and implementation-behavior reference: `docs/okf/`. Consult the relevant
  module for transport, arrange, audio engine, mixer, MIDI, and audio systems;
  update it when those contracts change.
- Conversation startup entry point: `AGENTS.md`.
- Accumulated task lessons: `.codex/memory/lessons-learned.md`.

## Important source files

- UI and layout: `src/PluginEditor.cpp`, `src/PluginEditor.h`
- Audio engine and persistence-facing processor logic:
  `src/PluginProcessor.cpp`, `src/PluginProcessor.h`
- Build configuration: `CMakeLists.txt`, `CMakePresets.json`
- Packaging scripts: `scripts/`, `packaging/`
- Assets: `assets/`

## Product contracts to preserve

- The app behaves like a stem player plus per-channel mixer.
- Channel strips are compact, repeatable, and built from the loaded stems.
- `Vol`, `S`, and `M` must map to the real stem index.
- `Pause` preserves position. `Stop` returns to `0.0s` and stops.
- Waveform progress follows the current playback position and song duration.
- Setlist double-click loads the full song: stems, waveform, and channels.
- The local persistence root is `~/Library/Application Support/MTControls/`.
- Arrangement duplicate clips are virtual sections: they reference source audio
  from their original section. Ripple edit only applies when the changed clip is
  a duplicate; deleting it or reducing its right edge shifts later blocks left
  without changing their source audio ranges.

## UI contracts to preserve

- Use the existing `UITheme` palette and visual language.
- Do not reintroduce removed channel-strip features:
  `SENDS`, pan knob, top sliders, or dual L/R meters.
- Channel card base dimensions are approximately `90 x 268`.
- The main layout keeps the top bar, waveform bar, collapsible setlist, handle,
  and channel grid.
- Keep the UI compact and useful for live performance.
- In playback mode, section controls show `M`, loop, and next; the active
  control should slide into the centered position and keep its hit target aligned
  with the visual position. These playback controls are mutually exclusive:
  activating one clears the other active playback-control states.
- Toolbar-created arrangement sections, counts, and utility cues append
  immediately to the right of the furthest existing section, independent of the
  currently selected section.
- Duplicating or pasting from an already duplicated arrangement section must
  preserve the original source section's label and color; do not chain duplicate
  suffixes or derive fallback colors from suffixed names.
- Arrangement section `muteInstruments` and `skipPlayback` are mutually
  exclusive. Mute uses a dark/opaque overlay; skip uses a distinct striped
  overlay plus skip icon because the section will not be reproduced.
- `skipPlayback` is an edit-mode arrangement state, distinct from the playback
  `next/jump` control. Skipped sections should not show playback controls, and
  manual seek into or past a skipped section must remain possible.

## Audio and threading contracts

- The intended pitch/tempo backend is Bungee. SoundTouch should only be treated
  as a fallback when explicitly required by platform constraints.
- Meters show real stem signal after per-channel gain and `S/M` state.
- Meter display must not be artificially reduced by global mix normalization.
- Do not block UI callbacks with heavy transport or disk work.
- Do not perform disk operations in `paint` or high-frequency timer callbacks.
- Keep transport state deterministic and centralized.
- Song-mix export is offline and must not disturb live transport. It renders
  WAV 24-bit, MP3 320 kbps, or M4A/AAC 256 kbps from the current stem/mixer snapshot on a background thread;
  the mixer export control and waveform progress are disabled/updated together.
- Dynamic-guide output must receive the same latency compensation as stems when
  pitch/tempo processing is active; keep its delay line primed during silence.

## Verification commands

Preferred local build:

```bash
cmake --build /Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build --target MTControls_Standalone -j
```

Run standalone app:

```bash
open "/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin/build/MTControls_artefacts/Standalone/MT Controls.app"
```

If the app bundle name differs in the active build tree, inspect
`build/MTControls_artefacts/Standalone/` before launching.

## Memory update policy

Update this file only for stable facts that future conversations should know.
Do not record every implementation change here.

Use `.codex/memory/lessons-learned.md` for repeatable lessons discovered during
tasks, especially pitfalls, user preferences, and validation discoveries.
