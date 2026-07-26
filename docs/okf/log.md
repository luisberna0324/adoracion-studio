---
type: Change Log
title: OKF Knowledge Base Changelog
description: Registro de cambios en la base de conocimiento OKF
timestamp: 2026-07-14T20:45:00Z
---

# Changelog

## 2026-07-26

### Agregado

- Benchmark de performance para `processBlock`: marcas por etapa
  (preRead, readNext, decodeRender, mixStems, mixOutput, rampTransition)
  serializadas a `audio_*.csv` en microsegundos.
- Detector de underrun (gap event): cuando el callback se ejecuta más
  tarde que `kDefaultGapThresholdMs = 15ms` después del callback
  anterior, se emite un evento a `audio_gaps_*.csv` con
  `wall_ms`, `delta_ms`, `song_position_sec`, `stem_count`,
  `is_playing`. Permite mapear "mini cortes" audibles a la posición
  de la canción donde ocurrieron.
- `AudioDiagnostics::getStats()` ahora reporta el conteo acumulado de
  gaps y el peor gap observado en milisegundos.

## 2026-07-14

### Agregado

- Base de conocimiento OKF inicial
- Documentación de MTControlsAudioProcessor
- Documentación de StemMixEngine y StemTrack
- Documentación de TransportClock y SectionScheduler
- Documentación de MetronomeEngine
- Documentación de PadsEngine
- Documentación de DynamicGuideEngine
- Documentación de MIDI Targets y Bindings
- Documentación de Transport API

## 2026-07-18

### Agregado

- Sección de integraciones externas para software de letras
- Documentación de FreeShow como primer destino compatible
- Contrato técnico inicial para disparo de cues externos desde secciones
- Priorización inicial de ProPresenter como primer destino compatible
- Transición de canciones `fade-through-zero` con handoff en silencio
- Verificación de read-ahead antes de publicar una canción nueva
- Restauración completa de mix, routing, click, guía y arrange antes del fade-in

### Estructura

```
docs/okf/
├── index.md                    # Raíz de la KB
├── log.md                      # Este archivo
├── processor/
│   ├── index.md
│   └── MTControlsAudioProcessor.md
├── engine/
│   ├── index.md
│   ├── StemMixEngine.md
│   ├── StemTrack.md
│   ├── TransportClock.md
│   └── SectionScheduler.md
├── audio-systems/
│   ├── index.md
│   ├── Metronome.md
│   ├── Pads.md
│   └── DynamicGuide.md
├── midi/
│   ├── index.md
│   ├── MidiTargets.md
│   └── MidiBindings.md
├── transport/
│   └── Transport.md
└── integrations/
    ├── index.md
    └── LyricsControllers.md
```
