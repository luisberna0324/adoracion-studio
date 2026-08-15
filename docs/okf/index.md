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
└── integrations/     # Integraciones externas (lyrics, cues, control)
```

## Módulos Principales

| Módulo | Descripción |
|--------|-------------|
| [processor/MTControlsAudioProcessor](processor/MTControlsAudioProcessor.md) | Procesador principal del plugin |
| [processor/AudioDiagnostics](processor/AudioDiagnostics.md) | Benchmark lock-free de performance (stages, gaps, decode, render) |
| [processor/SongLoadProbe](processor/SongLoadProbe.md) | Telemetría del UI thread para la carga de canciones |
| [engine/StemMixEngine](engine/StemMixEngine.md) | Motor de mezcla de stems |
| [engine/StemTrack](engine/StemTrack.md) | Track individual de audio |
| [audio-systems/Metronome](audio-systems/Metronome.md) | Sistema de metrónomo |
| [audio-systems/Pads](audio-systems/Pads.md) | Motor de pads/sampleos |
| [audio-systems/DynamicGuide](audio-systems/DynamicGuide.md) | Guías dinámicas por idioma |
| [audio-systems/StemCache](audio-systems/StemCache.md) | Cache FLAC on-disk de stems + migración legacy |
| [midi/MidiTargets](midi/MidiTargets.md) | Targets MIDI configurables |
| [transport/Transport](transport/Transport.md) | Control de reproducción |
| [integrations/FreeShow](integrations/FreeShow.md) | Integración con FreeShow |
| [integrations/LyricsControllers](integrations/LyricsControllers.md) | Integración con software de letras |

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

- **Stem indices**: 0 a 63 (maxStemCount = 64)
- **Output channels**: 1-indexed en UI, 0-indexed internamente
- **Volumen vs Gain**: Volume es 0-1 visual, Gain es dB linear
- **Thread safety**: Controles de mix usan std::atomic para lock-free audio thread
