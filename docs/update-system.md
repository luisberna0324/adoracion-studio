# PlayWorship update system

Architecture:

- The PlayWorship app only checks manifests, prompts the user, downloads the
  package, validates size and SHA-256, and launches `PlayWorshipUpdater`.
- `PlayWorshipUpdater` is a separate executable copied into the standalone app
  artifact. It extracts the update, validates the signed payload where supported,
  backs up the current install, swaps in the new install, relaunches PlayWorship,
  and rolls back if install or relaunch fails.
- Before launch, the app copies the packaged updater to
  `updates/runner/PlayWorshipUpdater` and executes that external copy. This keeps
  Windows from locking the install directory during replacement.
- `MTCONTROLS_UPDATE_BASE_URL` controls the manifest endpoint. Empty disables
  update checks, which is the default for local/dev builds.

Build:

```bash
cmake -S . -B build -DJUCE_DIR=/path/to/JUCE \
  -DMTCONTROLS_UPDATE_BASE_URL=https://updates.playworship.com
cmake --build build --target MTControls_Standalone -j
```

Manifest endpoint:

```http
GET /v1/updates/check?platform=macos&arch=arm64&version=0.1.0&channel=stable
```

Example response:

```json
{
  "updateAvailable": true,
  "currentVersion": "0.1.0",
  "latestVersion": "0.2.0",
  "channel": "stable",
  "mandatory": false,
  "package": {
    "type": "full",
    "platform": "macos",
    "arch": "arm64",
    "url": "https://updates.playworship.com/releases/0.2.0/PlayWorship-0.2.0-macos-arm64.zip",
    "sizeBytes": 184230912,
    "sha256": "b1f4c8d0...",
    "signature": {
      "type": "developer-id",
      "publisher": "Multitracks"
    }
  },
  "delta": {
    "available": false
  },
  "releaseNotes": {
    "title": "PlayWorship 0.2.0",
    "url": "https://playworship.com/releases/0.2.0",
    "items": [
      "Mejoras de estabilidad",
      "Correcciones en carga de stems"
    ]
  }
}
```

Transactional package format:

- Use `.zip` for transactional updates.
- macOS ZIP should contain the signed/notarized `PlayWorship.app`.
- Windows ZIP should contain the replacement install directory contents or a
  single top-level payload directory.
- The manifest `sha256` is the SHA-256 of the downloaded ZIP.
- If delta packages are introduced, they must produce the same extracted payload
  layout before the updater swap step. If delta application fails, the server or
  app should fall back to the full package.

Updater state and logs:

- State: `~/Library/Application Support/MTControls/updates/update-state.json`
  on macOS, equivalent user app data path on Windows.
- Logs: `~/Library/Application Support/MTControls/logs/updater.log`.

Current security behavior:

- The app validates package size and SHA-256 before launching the updater.
- The updater re-validates SHA-256 before extraction.
- macOS payloads are checked with `codesign --verify --deep --strict` and
  `spctl -a -t exec`.
- Windows payloads can be checked with PowerShell Authenticode validation when
  `signature.publisher` is present in the manifest.
- The next hardening step is detached manifest signing, so the manifest itself is
  authenticated before trusting package URLs and hashes.
