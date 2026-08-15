---
type: Change Log
title: OKF Knowledge Base Changelog
description: Registro de cambios en la base de conocimiento OKF
timestamp: 2026-07-14T20:45:00Z
---

# Changelog

## 2026-08-13

### Agregado

- Nuevo módulo [`SongLoadProbe`](processor/SongLoadProbe.md) —
  telemetría del UI thread para la carga de canciones. Mide los
  sub-stages de `loadSong` y `loadSongStems` (per-stem:
  `cache.lookup`, `createReader`, `prepareToPlay`, `waitForReady`)
  + totales (`waitForReady.total`, `fadeOut.wait`,
  `suspendProcessing.wait`, `bungee.prepare`) +
  `StemCache.isValid` hit/miss con razón detallada
  (`noFlac`, `noMeta`, `badMeta`, `sizeMismatch`, `mtimeMismatch`).
  CSV en `~/Library/Logs/MTControls/song_load_<ts>.csv`. Memory
  snapshot (RSS) por fila via `getrusage`.
- Documento canónico de referencia para [`StemCache`](audio-systems/StemCache.md)
  (no existía antes). Cubre la cache FLAC, el sidecar `meta.txt`,
  el bug del split-path legacy, y el fix de `migrateLegacyCacheDirectories`
  3-stage (recuperación de espacio + walk recursivo + cleanup).
- Sección "Companion: `SongLoadProbe`" en
  [`AudioDiagnostics.md`](processor/AudioDiagnostics.md) explicando
  cuándo usar cada uno y cómo cruzar los CSVs.

### Modificado

- [`StemCache::migrateLegacyCacheDirectories`](audio-systems/StemCache.md#el-fix-sanitizecachekey--migratelegacycachedirectories)
  reescrito para manejar el caso de **directorios anidados** (cuando
  el bug del `/` partió el cache en dos sub-niveles, e.g.
  `decoded/Al Que Es Digno /` + `decoded/Al Que Es Digno / Vencio/`).
  El walker original era non-recursive y dejaba el cache sin migrar.
  Nueva versión: 3 etapas idempotentes (recupera espacio al final del
  padre si el hijo empieza con espacio, walk recursivo post-order,
  limpieza de subdirs vacíos).
- [`AudioDiagnostics.md`](processor/AudioDiagnostics.md): actualizada
  la sección de "Artefactos relacionados" para mencionar el
  companion.

### Diagnóstico

- Caso "Al Que Es Digno / Vencio" (proyecto .pwproj 449 MB, 12 stems
  M4A, setlist "Culto domingo 2 Agosto"):
  - **Causa raíz**: el cache FLAC vivía en
    `decoded/Al Que Es Digno / Vencio/` (split-path legacy) pero
    `sanitizeCacheKey("Al Que Es Digno / Vencio")` busca en
    `decoded/Al Que Es Digno _ Vencio/`. Miss permanente.
  - **Consecuencia**: cada carga usaba el M4A nativo (1.2-2.9 ms
    de decode por stem por bloque) y disparaba `precomputeAsync` en
    background sin beneficio para esa carga.
  - **Después del fix de migración**: 4/12 stems con `hit=1`
    (FLAC válido, 0.5 ms `createReader` vs 2.5 ms con M4A), 8/12
    con `miss.mtimeMismatch` (m4a source re-extraído del .pwproj
    cambió mtime, FLAC precomputado quedó stale). Pendiente:
    trigger re-precompute cuando el miss es mtime-only.
  - **Hallazgo secundario**: `settings.restore` (408 ms, 34% del
    total de 1.2 s) domina el swap. Son ~60 operaciones atómicas
    per-stem (setStemVolume/Gain/Mute/Solo/OutputChannel) que
    iteran sobre los stems y hacen JSON lookups por
    `getActiveSetlist*`. Candidata a batching.

### Estructura

```
docs/okf/
├── index.md                    # Raíz de la KB
├── log.md                      # Este archivo
├── processor/
│   ├── index.md
│   ├── MTControlsAudioProcessor.md
│   ├── AudioDiagnostics.md     # actualizado
│   └── SongLoadProbe.md        # NUEVO
├── engine/
│   ├── index.md
│   ├── StemMixEngine.md
│   ├── StemTrack.md
│   ├── TransportClock.md
│   └── SectionScheduler.md
├── audio-systems/
│   ├── index.md                # actualizado
│   ├── Metronome.md
│   ├── Pads.md
│   ├── DynamicGuide.md
│   ├── OutputChannelCatalog.md
│   ├── StemOutputRouter.md
│   ├── SongMixExporter.md
│   └── StemCache.md            # NUEVO
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

## 2026-07-26 (tarde)

### Agregado

- Documento canónico de referencia [`AudioDiagnostics`](processor/AudioDiagnostics.md) — descripción unificada del benchmark lock-free (6 stages, gap detector, worker drain, decode, Bungee) con tabla de optimizaciones medidas, CSVs, y guía para extender.

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
