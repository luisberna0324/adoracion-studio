---
type: Knowledge Base
title: MTControls Plugin API
description: Base de conocimiento para el plugin JUCE de Multitracks Controls
tags: [juce, audio, plugin, multitracks]
timestamp: 2026-07-14T20:45:00Z
---

# MTControls Plugin - Knowledge Base

Plugin de audio profesional para JUCE que soporta stems múltiples, metrónomo, guías dinámicas y pads.

## Estructura de la API

```
docs/okf/
├── processor/         # MTControlsAudioProcessor (clase principal)
├── engine/           # Motores de audio (Mix, Stem, Transport)
├── audio-systems/    # Sistemas de audio (Metronome, Pads, DynamicGuide)
├── midi/             # Sistema MIDI y bindings
├── transport/        # Control de transporte y arrange
├── ui/               # Layout responsive y contratos visuales
├── integrations/     # Integraciones externas (lyrics, cues, control)
└── release/          # Contratos de distribucion y tiendas
```

## Módulos Principales

| Módulo | Descripción |
|--------|-------------|
| [processor/MTControlsAudioProcessor](processor/MTControlsAudioProcessor.md) | Procesador principal del plugin |
| [processor/AudioDiagnostics](processor/AudioDiagnostics.md) | Benchmark lock-free de performance (stages, gaps, decode, render) |
| [integrations/PlaybackDiagnosticSharing](integrations/PlaybackDiagnosticSharing.md) | ZIP manual de soporte con últimos 120 s disponibles, compartir en móvil y guardar en escritorio |
| [integrations/SentryTelemetry](integrations/SentryTelemetry.md) | Eventos categóricos privados y bridge realtime seguro hacia Sentry |
| [processor/SongLoadProbe](processor/SongLoadProbe.md) | Telemetría del UI thread para la carga de canciones |
| [engine/StemMixEngine](engine/StemMixEngine.md) | Motor de mezcla de stems |
| [engine/SectionAutomation](engine/SectionAutomation.md) | Valores de mezcla por sección, selección de audio y Pads |
| [engine/StemTrack](engine/StemTrack.md) | Track individual de audio |
| [audio-systems/Metronome](audio-systems/Metronome.md) | Sistema de metrónomo |
| [audio-systems/Pads](audio-systems/Pads.md) | Motor de pads/sampleos |
| [audio-systems/DynamicGuide](audio-systems/DynamicGuide.md) | Guías dinámicas por idioma |
| [audio-systems/StemCache](audio-systems/StemCache.md) | Cache FLAC on-disk de stems + migración legacy |
| [importing/LibraryPersistence](importing/LibraryPersistence.md) | Persistencia y reubicacion de canciones locales en iOS |
| [midi/MidiTargets](midi/MidiTargets.md) | Targets MIDI configurables |
| [transport/Transport](transport/Transport.md) | Control de reproducción |
| [ui/NativeLyrics](ui/NativeLyrics.md) | Editor nativo iOS de letra y fragmentos musicales |
| [ui/MobileLayout](ui/MobileLayout.md) | Layout responsive para teléfonos iOS y Android |
| [ui/InputControls](ui/InputControls.md) | Campos de texto y selectores adaptativos multiplataforma |
| [ui/SectionAutomation](ui/SectionAutomation.md) | Edición de mezcla por sección con los controles existentes |
| [ui/Popovers](ui/Popovers.md) | Presentacion adaptativa y comportamiento comun de popovers JUCE |
| [ui/LearningGuide](ui/LearningGuide.md) | Guías interactivas sobre controles reales y progreso local por curso |
| [ui/SettingsVersionsSheet](ui/SettingsVersionsSheet.md) | Hoja de acciones adaptativa para consultar y aplicar versiones de ajustes |
| [integrations/FreeShow](integrations/FreeShow.md) | Integración con FreeShow |
| [integrations/LyricsControllers](integrations/LyricsControllers.md) | Integración con software de letras |
| [release/AppStore](release/AppStore.md) | Preflight de firma, privacidad y revision de App Store |

## Uso Rápido

### Cargar una canción con stems

```cpp
auto &processor = ...;
juce::StringArray stemPaths = {"/path/to/drums.wav", "/path/to/bass.wav"};
processor.loadSongStems(stemPaths, "Song Title", "C", 120.0);
processor.playSong();
```

### Controlar un stem individual

```cpp
processor.setStemVolume(0, 0.8f);  // Volumen del stem 0
processor.setStemMute(1, true);     // Silenciar stem 1
processor.setStemSolo(0, true);     // Solo stem 0
```

### Configurar metrónomo

```cpp
processor.setMetronomeEnabled(true);
processor.setMetronomeBpm(120.0);
processor.setMetronomeVolume(0.5f);
```

## Convenciones

La suite de regresión y la validación en dispositivos se describen en
[Pruebas funcionales](Testing.md).


- **Stem indices**: 0 a 63 (maxStemCount = 64)
- **Output channels**: 1-indexed en UI, 0-indexed internamente
- **Volumen vs Gain**: Volume es 0-1 visual, Gain es dB linear
- **Thread safety**: Controles de mix usan std::atomic para lock-free audio thread
