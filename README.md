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

### Backend local (`localhost`) por variable

Para ejecutar el plugin apuntando al backend local:

```bash
export MTCONTROLS_API_BASE_URL="http://127.0.0.1:3000"
bash scripts/run_macos.sh
```

Si no defines la variable, `scripts/run_macos.sh` usa `http://127.0.0.1:3000` por defecto.

### Windows

```bash
cmake --preset windows-release
cmake --build --preset windows-release
```

## Instaladores (MVP)

### macOS (DMG drag-and-drop)

Genera el build y luego empaqueta el instalador `.dmg`:

```bash
cmake --preset macos-release
cmake --build --preset macos-release
bash scripts/package_macos_dmg.sh --version 1.2.3
```

Salida esperada:

`dist/playworship-macos-installer-1.2.3.dmg`

### Windows (Inno Setup EXE)

Genera el build y luego empaqueta el instalador `.exe`:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
.\scripts\package_windows_installer.ps1 -Version 1.2.3
```

Salida esperada:

`dist/playworship-windows-installer-1.2.3.exe`

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

- Windows: GitHub Actions con `windows-latest` + preset `windows-release`
- macOS: GitHub Actions con `macos-latest` + preset `macos-release`
- iOS: generar proyecto Xcode con preset `ios-release` (requiere signing para build/run)

### Workflow `Build Artifacts`

Entradas relevantes:

- `publish_release`: publica (o actualiza) GitHub Release.
- `publish_installers` (default `true`): genera/publica instaladores `dmg` y `exe`.

Assets publicados cuando `publish_release=true`:

- `MTControls-macos-standalone-<semver>.zip`
- `MTControls-windows-standalone-<semver>.zip`
- `playworship-macos-installer-<semver>.dmg` (si `publish_installers=true`)
- `playworship-windows-installer-<semver>.exe` (si `publish_installers=true`)

## Siguiente foco

Iterar el diseno de controles (`LookAndFeel`, layout por secciones, estados hover/drag, tipografia y palette).

Se agrega nueva linea para probar el feature
