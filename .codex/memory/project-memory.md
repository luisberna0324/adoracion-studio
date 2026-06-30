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
- Auto-update system:
  `src/update/UpdateService.*`, `src/update/UpdateManifest.*`,
  `src/update/UpdateCrypto.*`, `tools/updater/PlayWorshipUpdater.cpp`
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

## Audio and threading contracts

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

## Auto-update architecture

- PlayWorship uses a lightweight in-app update coordinator plus an independent
  `PlayWorshipUpdater` executable for transactional install/rollback.
- `MTCONTROLS_UPDATE_BASE_URL` controls the update manifest API. Empty disables
  update checks for local/dev builds.
- The manifest endpoint is documented in `docs/update-system.md`.
- Transactional packages are ZIPs: macOS should contain signed/notarized
  `PlayWorship.app`; Windows should contain the replacement install payload.
- The app validates package size and SHA-256 before launching the updater; the
  updater re-validates SHA-256, extracts to staging, backs up the current install,
  swaps payloads, relaunches, and rolls back if install or relaunch fails.

## Memory update policy

Update this file only for stable facts that future conversations should know.
Do not record every implementation change here.

Use `.codex/memory/lessons-learned.md` for repeatable lessons discovered during
tasks, especially pitfalls, user preferences, and validation discoveries.
