---
type: Diagnostic Module
title: AudioDiagnostics
description: Benchmark lock-free del audio thread y workers - stages, gaps, decode, render
resource: src/AudioDiagnostics.h
tags: [juce, audio, benchmark, diagnostics, performance, telemetry]
timestamp: 2026-07-26T23:00:00Z
---

# AudioDiagnostics

`AudioDiagnostics` es el **sistema canónico de telemetría de performance** del audio engine. Permite medir cuánto tarda cada sub-etapa del audio thread y de los workers de read-ahead, con captura lock-free y volcado periódico a CSV.

Es la herramienta de referencia para reproducir, medir y validar cualquier optimización de performance en `processBlock` o en el pipeline de stems. Toda métrica de performance del proyecto debe pasar por este sistema para ser comparable entre commits.

## Convenciones de uso

- **Nombre canónico**: `AudioDiagnostics` (matches la clase, los logs, y este doc).
- **Alias informales**: "el benchmark", "el stage benchmark", "el benchmark de etapas".
- **Commits de referencia**: ver [Optimizaciones medidas](#optimizaciones-medidas).

---

## Qué mide

`AudioDiagnostics` captura **siete dimensiones** de la pipeline de audio, todas con captura no-bloqueante en el audio thread (relajada) o el worker thread (relajada), y volcado a disco por un thread de flush dedicado.

### 1. Per-stage del audio thread

Seis sub-etapas bracketeadas con `juce::Time::getHighResolutionTicks()` en `processBlock`:

| Stage | CSV column | Qué mide |
|---|---|---|
| `preRead` | `stage_pre_read_us` | Setup antes de tocar stems (state reads, section snapshot, gain de transición, tempo updates, seek coordination, fade math, arrangement transitions) |
| `readNext` | `stage_read_next_us` | Loop de `getNextAudioBlock` por stem (path A) o `mixEngine.mixStems` (path B) |
| `decodeRender` | `stage_decode_render_us` | `proc.process()` del BungeeProcessor (pitch/time-stretch) + delay line per stem |
| `mixStems` | `stage_mix_stems_us` | `mixEngine.mixStems` / `mixEngine.renderDirect` (path B) |
| `mixOutput` | `stage_mix_output_us` | `mixEngine.renderToOutput` (master bus) |
| `rampTransition` | `stage_ramp_transition_us` | `applySongTransitionRamp` (fade envelope al handoff entre canciones) |

CSV: `audio_<ts>.csv`, fila por bloque. Columnas también: `block_ms`, `budget_ms`, `overrun`, `lock_wait_ms`, `streaming_misses`, `streaming_missing_frames`, `time_processor_reset_misses`, `time_processor_service_failures`.

### 2. Gap detector (underrun del callback)

Cuando el gap entre el `blockStartTick` de un callback y el `blockEndTick` del anterior supera `max(kDefaultGapThresholdMs = 15.0, 1.5 × duración del callback)`, se emite un `GapEvent`. El umbral se adapta a callbacks grandes de Oboe: un callback sano de 20 ms no se registra como corte, pero perder un callback completo sí. Esto detecta "mini cortes" audibles (el callback fue starving) y los mapea a la posición de la canción.

CSV: `audio_gaps_<ts>.csv`. Columnas: `wall_ms,delta_ms,song_position_sec,stem_count,is_playing`.

### 3. Worker drain instrumentation (background thread)

`StemBufferingSource::useTimeSlice()` marca `getHighResolutionTicks()` alrededor de la llamada y mantiene:

| Métrica | CSV column | Semántica |
|---|---|---|
| `worker_drain_us` | `worker_drain_us` | Duración del último `useTimeSlice()` (cualquier drain) |
| `worker_gap_us` | `worker_gap_us` | Tiempo desde el fin del último drain hasta el sample (sujeta al decimation) |
| `worker_visit_count` | `worker_visit_count` | Calls acumulativos de `useTimeSlice()` |
| `real_drain_us` | `real_drain_us` | Duración del último **REAL** drain (los que no son no-op) |
| `real_drain_gap_us` | `real_drain_gap_us` | Inter-gap entre drains REALes consecutivos (sin sesgo de decimación) |
| `real_drain_count` | `real_drain_count` | Count de drains REALes |

CSV: `audio_stems_<ts>.csv`. Muestreado por stem cada `kStemTimingSnapshotEveryBlocks = 20` bloques. La columna
`routing_grouped_eligible` deja explícito qué bloque del callback usó la ruta
agrupada de `StemMixEngine` (los 3 voces `TimeStretchProcessor` y una sola
llamada a `StemOutputRouter`) en lugar de la ruta por stem legacy; el script
de análisis puede filtrar por `routing_active=1,routing_grouped_eligible=1`
para aislar los bloques que aprovecharon el agrupado y comparar `decode_render`
contra `routing_active=1,routing_grouped_eligible=0`.

### 4. AAC decode + resample (background thread)

`ResamplingPositionableSource::getNextAudioBlock()` está bracketeado — es el único punto donde el `AudioFormatReader` (que decodifica AAC/WAV/FLAC) y el `juce::ResamplingAudioSource` corren juntos.

| Métrica | CSV column | Semántica |
|---|---|---|
| `decode_us` | `decode_us` | Duración del último decode+resample |
| `decode_gap_us` | `decode_gap_us` | Inter-gap entre decodes consecutivos (sin sesgo) |
| `decode_count` | `decode_count` | Calls acumulativos |

### 5. Bungee (pitch/time-stretch) processing (audio thread)

`proc.process()` en el path de time processing está bracketeado per-stem.

| Métrica | CSV column | Cuándo se llena |
|---|---|---|
| `bungee_us` | `bungee_us` | Solo cuando `timeProcessActive` y `stemNeedsTimeProcess[stem]` (pitch/tempo activo para ese stem) |
| `bungee_gap_us` | `bungee_gap_us` | Inter-gap entre calls consecutivos |
| `bungee_count` | `bungee_count` | Calls acumulativos |

**Importante:** el array `lastBungeeUsMicros[]` se resetea a 0 cuando el stem no necesita time processing, para evitar reportar valores residuales de escenarios previos.

### 6. Stats acumuladas in-process

`AudioDiagnostics::getStats()` retorna una vista compacta para mostrar en UI / status bar:

```
Blocks: N | Overruns: K (P%) | Avg: Xms | Max: Yms | Gaps: M (max Zms)
```

---

## Archivos del sistema

| Archivo | Rol |
|---|---|
| `src/AudioDiagnostics.h` | Definición de la clase, ring buffers lock-free, schema de los CSVs |
| `src/PluginProcessor.cpp` | Cableado de los 6 stages en `processBlock` |
| `src/StemBufferingSource.h/.cpp` | Atomics de worker drain y real drain |
| `src/ResamplingPositionableSource.h/.cpp` | Atomics de decode + resample |
| `src/StemTrack.h` | Getters `bufferingSourceForBenchmark()` y `resamplingSourceForBenchmark()` |
| `src/SimpleDelayLine.h` | Getter `isActive()` (usado por el skip del delay line transparent) |

---

## CSVs generados

Los logs se escriben en `~/Library/Logs/MTControls/` (macOS) — ver `AudioDiagnostics::startBackgroundLogger()` para el path completo.

| Archivo | Frecuencia | Contenido |
|---|---|---|
| `audio_<ts>.csv` | 1 fila por callback del audio thread | block metrics, stages, streaming_misses |
| `audio_gaps_<ts>.csv` | 1 fila por gap detectado | timestamp, delta_ms, song position |
| `audio_stems_<ts>.csv` | 1 fila por stem cada 20 bloques | per-stem worker drain, decode, bungee, latencia |

`ts` es el timestamp de inicio de la sesión. Los archivos se rotan automáticamente al iniciar el plugin.

---

## Cómo correr un experimento reproducible

1. **Build**: `cd build/macos-release && ninja MTControls_Standalone`
2. **App**: `build/macos-release/MTControls_artefacts/Release/Standalone/PlayWorship.app`
3. **Reproducir** la canción objetivo durante 2-5 minutos (la duración exacta determina cuántos samples hay en el CSV)
4. **Recolectar datos** del CSV más reciente en `~/Library/Logs/MTControls/`
5. **Comparar** contra la sesión previa (mismo escenario, mismo path) usando `git diff` o awk

Para análisis rápido, los scripts awk de los commits de cada optimización están documentados en sus mensajes de commit.

---

## Optimizaciones medidas

Estas son las optimizaciones cuyo impacto fue medido y validado usando `AudioDiagnostics`:

| Commit | Optimización | Métrica clave | Antes | Después | Delta |
|---|---|---|---|---|---|
| `83ea516` | Skip delay line transparent | `stage_read_next_us` avg | 1137us | 343us | **-70%** |
| `83ea516` | Skip delay line transparent | `block_ms` avg | 1.70ms | 0.87ms | **-48%** |
| `83ea516` | Skip delay line transparent | bloques con readNext > 1500us | 22.8% | 0.0% | **-100%** |
| `1a3e7b2` | Constante `kMinPrefetchBufferSamples` | n/a (doc del intento fallido) | - | - | - |
| `33dbbe8` | Workers 4 → 8 | `streaming_misses` | 61395 | 48217 | **-21%** |
| `33dbbe8` | Workers 4 → 8 | bloques con miss | 84.2% | 54.3% | **-30%** |
| `b1f9026` | Bin-packing LPT para asignación de stems | `decode_gap_max` | 295-574ms (bimodal) | 31-33ms (uniforme) | **-17x** |
| `6f9b46f` | FLAC cache para stems | `block_ms` avg | 1.682ms | 0.632ms | **-2.7x** |
| `6f9b46f` | FLAC cache para stems | `decode_us` avg | ~1245us | ~90us | **-14x** |
| `6f9b46f` | FLAC cache para stems | `decode_us` (con pitch -2) | 1245us | 67us | **-18x** |

Total: 6 commits, 1 docs/okf/processor/MTControlsAudioProcessor.md actualizado, 1 docs/okf/log.md actualizado.

---

## Diagnósticos encontrados (este benchmark reveló)

| Hallazgo | Métrica que lo expuso | Optimización resultante |
|---|---|---|
| El audio thread no es el cuello | `block_ms` vs `budget_ms` (11.6ms) | (ninguna, era falsa alarma) |
| Delay line transparent siempre corriendo | `stage_read_next_us` por stem | `83ea516` |
| Buffer size subir de 32K a 128K **empeora** | `streaming_misses` | `1a3e7b2` (reverted, doc del intento) |
| Worker scheduling round-robin ignora peso del stem | `decode_gap_max` bimodal (295/574ms) | `b1f9026` |
| AAC decode consume 1.2-2.9ms por bloque por stem | `decode_us` | `6f9b46f` |

---

## Artefactos relacionados

Estos subsistemas se benefician del benchmark o lo usan:

- **FLAC cache** (`src/audio/StemCache.h/.cpp`): elimina el decode AAC del hot path
- **Bin-packing LPT** (`kUseBinPackingForStemWorkers` en `PluginProcessor.cpp`): asignación óptima de stems a workers
- **Sparse delay line** (`isActive()` en `SimpleDelayLine.h`): skip del delay line transparent

---

## Companion: `SongLoadProbe`

Para diagnosticar la **carga de canciones** (no la reproducción), existe [`SongLoadProbe`](SongLoadProbe.md), un módulo hermano que vive en `src/SongLoadProbe.h` y mide el UI thread del swap:

- Stages de `loadSongStems` (per-stem: `cache.lookup`, `createReader`, `prepareToPlay`, `waitForReady`)
- Stages de `loadSong` (`project.open`, `settings.restore`, `bungee.prepare`, fade/suspend waits)
- Eventos one-shot de `StemCache.isValid` (hit / miss con razón: `noFlac`, `noMeta`, `badMeta`, `sizeMismatch`, `mtimeMismatch`)
- Memory snapshot (RSS) por fila

CSV en `~/Library/Logs/MTControls/song_load_<ts>.csv`.

Para una sesión completa de "tarda mucho en cargar", correr **ambos** benchmarks y cruzar los CSVs: `song_load_*.csv` te dice qué fase del swap domina el wall-clock del UI thread, `audio_*.csv` te dice si el audio thread queda starving durante el warm-up post-load.

---

## Cómo extender

Para añadir un nuevo stage al benchmark:

1. **Definir el campo** en `AudioDiagnostics::DiagEvent` (o `StemTimingEvent` para per-stem)
2. **Brackear** con `getHighResolutionTicks()` en el punto del código
3. **Computar microsegundos** al final del callback (saturar a `int16_t` con `saturateUsToInt16()`)
4. **Añadir columna al CSV** en `startBackgroundLogger()` y en `flushBlockEvents()` / `flushStemTimingEvents()`
5. **Documentar** en este archivo (sección "Qué mide" + tabla de métricas)
6. **Commitear** con mensaje que incluya la métrica del antes/después

---

## Limitaciones conocidas

- **Decimation bias**: la mayoría de métricas per-stem se muestrean cada 20 bloques (~232ms). El `worker_gap_us` calculado contra el sample puede ser engañoso — usar `real_drain_gap_us` o `decode_gap_us` (que mantienen el par `last/previous` con `exchange`) en su lugar.
- **Primer sample artifact**: en el primer sample de cada stem, `previousXxxStartTick = 0`, así que `gap = lastStart - 0 = lastStart` puede parecer varios segundos. Filtrar el primer sample en el análisis.
- **No captura cross-worker**: si un stem migra de worker (no implementado todavía), los atomics no lo detectan.
- **No captura el thread de lectura de disco puro**: solo se mide lo que pasa por el path de la pipeline JUCE. Si hay un thread de disco separado (no es el caso actual), habría que instrumentarlo aparte.
