# MT Controls (JUCE)

Proyecto base JUCE para iterar diseno de controles (Standalone).

## Incluye

- Standalone (macOS, Windows, iOS)
- Parametros iniciales:
  - `inputGain` (`-24 dB` a `+24 dB`)
  - `mix` (`0%` a `100%`)
  - `bypass` (`on/off`)
- UI inicial con dos knobs y un toggle conectados por `AudioProcessorValueTreeState`

## Requisitos

- CMake 3.22+
- JUCE (clonado localmente)
- Toolchain C++ (Xcode en macOS o Visual Studio en Windows)

## Build (CMake Presets)

Configura la variable de entorno `JUCE_DIR` apuntando al repo local de JUCE.

### Nota sobre JUCE_DIR en Windows

PowerShell:
```powershell
$env:JUCE_DIR="C:\ruta\JUCE"
```

CMD:
```cmd
set JUCE_DIR=C:\ruta\JUCE
```

### macOS

```bash
cmake --preset macos-release
cmake --build --preset macos-release
```

### Windows

```bash
cmake --preset windows-release
cmake --build --preset windows-release
```

Windows 32-bit (x86):

```bash
cmake --preset windows-x86-release
cmake --build --preset windows-x86-release
```

## Instaladores (MVP)

### macOS (PKG installer)

Genera el build y luego empaqueta el instalador `.pkg`:

```bash
cmake --preset macos-release
cmake --build --preset macos-release
bash scripts/package_macos_pkg.sh --version 1.2.3
```

Salida esperada:

`dist/playworship-macos-installer-1.2.3.pkg`

### Windows (Inno Setup EXE)

Genera el build y luego empaqueta el instalador `.exe`:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
.\scripts\package_windows_installer.ps1 -Version 1.2.3 -Architecture x64
.\scripts\package_windows_installer.ps1 -Version 1.2.3 -Architecture x86
```

Salida esperada:

- `dist/playworship-windows-installer-1.2.3-x64.exe`
- `dist/playworship-windows-installer-1.2.3-x86.exe`

### iOS

```bash
cmake --preset ios-release
cmake --build --preset ios-release
```

El proyecto de Xcode se genera en `build/ios-release` para firmar y ejecutar
en dispositivo.

Abrir en Xcode:
```bash
open build/ios-release/MTControls.xcodeproj
```

## Build (manual)

Si prefieres configurar sin presets:

```bash
cmake -S . -B build -DJUCE_DIR=/Users/luisberna/dev/multitracks/multitracks/third_party/JUCE
cmake --build build -j
```

## CI (idea)

- Windows: GitHub Actions con `windows-latest` + presets `windows-release` (x64) y `windows-x86-release` (x86)
- macOS: GitHub Actions con `macos-latest` + preset `macos-release`
- iOS: generar proyecto Xcode con preset `ios-release` (requiere signing para build/run)

### Workflow `Build Artifacts`

Entradas relevantes:

- `publish_release`: publica (o actualiza) GitHub Release.
- `publish_installers` (default `true`): genera/publica instaladores `pkg` y `exe`.
- `publish_to_downloads_repo` (default `false`): publica la release en otro repo (ideal para distribucion publica).
- `downloads_repo`: repo destino en formato `owner/repo` (por ejemplo `tu-org/playworship-downloads`).
- `create_tag_if_missing` (default `true`): crea y pushea el tag semver si aun no existe en este repo privado.

Assets publicados cuando `publish_release=true`:

- `MTControls-macos-standalone-<semver>.zip`
- `MTControls-windows-x64-standalone-<semver>.zip`
- `MTControls-windows-x86-standalone-<semver>.zip`
- `playworship-macos-installer-<semver>.pkg` (si `publish_installers=true`)
- `playworship-windows-installer-<semver>-x64.exe` (si `publish_installers=true`)
- `playworship-windows-installer-<semver>-x86.exe` (si `publish_installers=true`)

### Opcion 3: Repo publico de descargas (codigo privado)

1. Crea un repo publico para releases, por ejemplo: `tu-org/playworship-downloads`.
2. En este repo privado, agrega el secret:
   - `DOWNLOADS_REPO_TOKEN`: PAT con permisos `repo` sobre el repo de descargas.
3. Ejecuta `Build Artifacts` con:
   - `ref`: branch/SHA origen (ej: `main`)
   - `version` o `tag` (ej: `1.2.3` o `v1.2.3`)
   - `publish_release=true`
   - `publish_to_downloads_repo=true`
   - `downloads_repo=tu-org/playworship-downloads`
   - `create_tag_if_missing=true`

Resultado:
- Si el tag no existe, se crea en el repo privado.
- Se compilan artefactos/instaladores.
- Se crea/actualiza la release `vX.Y.Z` en el repo publico de descargas con assets y notas de cambios.

## Siguiente foco

Iterar el diseno de controles (`LookAndFeel`, layout por secciones, estados hover/drag, tipografia y palette).

Se agrega nueva linea para probar el feature
