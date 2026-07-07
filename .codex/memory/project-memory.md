# Project memory - MT Controls / PlayWorship

## Repository identity

- Project root: `/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin`
- App type: JUCE standalone/plugin project focused on multitrack playback,
  mixer channels, setlist flow, waveform, and local persistence.
- Main development target for local validation: `MTControls_Standalone`.
- Product/engineering source of truth: `AGENT.md`.
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

## Audio and threading contracts

- The intended pitch/tempo backend is Bungee. SoundTouch should only be treated
  as a fallback when explicitly required by platform constraints.
- Meters show real stem signal after per-channel gain and `S/M` state.
- Meter display must not be artificially reduced by global mix normalization.
- Do not block UI callbacks with heavy transport or disk work.
- Do not perform disk operations in `paint` or high-frequency timer callbacks.
- Keep transport state deterministic and centralized.

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
