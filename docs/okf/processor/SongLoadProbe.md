---
type: Diagnostic Module
title: SongLoadProbe
description: Telemetría del UI thread para la carga de canciones - stages, memoria, hit/miss de cache FLAC
resource: src/SongLoadProbe.h
tags: [juce, audio, benchmark, diagnostics, performance, telemetry, song-load, cache]
timestamp: 2026-08-17T03:00:00Z
---

# SongLoadProbe

`SongLoadProbe` es el **sistema canónico de telemetría del song-load path** — el camino UI-thread que recorre la app cuando el usuario cambia de canción en un setlist. Es el companion de `AudioDiagnostics` (que mide el audio thread durante la reproducción); este mide el costo del swap.

Es la herramienta de referencia para reproducir, medir y validar cualquier optimización en `loadSongStems` o en la restauración de configuration post-load. Toda métrica de performance del project-load path debe pasar por este sistema para ser comparable entre commits.

## Convenciones de uso

- **Nombre canónico**: `SongLoadProbe` (matches la clase, los logs, y este doc).
- **Alias informales**: "el load probe", "el benchmark de carga", "el stage benchmark del load path".
- **Sentinel de medición**: `ScopedSongLoadTimer` — RAII helper que cierra el bracket en cualquier path de retorno, incluyendo `return` temprano.

---

## Qué mide

`SongLoadProbe` captura **una dimensión** del swap de canción — el wall-clock del UI thread — con resolución de microsegundos y un snapshot de RSS (Resident Set Size) por fila. A diferencia de `AudioDiagnostics`, que es lock-free y captura del audio thread, este probe es **mutex-guarded y pensado exclusivamente para el message thread**.

### 1. Per-stage del UI thread

Las sub-etapas bracketeadas con `juce::Time::getHighResolutionTicks()` en el path de carga:

| Stage | CSV `sub_phase` | Qué mide |
|---|---|---|
| `loadSong.total` | `total` | Tiempo total del swap end-to-end (UI thread) |
| `loadSong.project.open` | `project.open` | Apertura del .pwproj: lectura del manifest, validación de recursos, extracción de stems desde el ZIP al `project-cache/<id>/audio/` |
| `loadSong.processor.loadSongStems` | `processor.loadSongStems` | Total del pipeline en el audio processor (suma de las sub-etapas siguientes) |
| `loadSongStems.stem#N.cache.lookup` | `stem#N.cache.lookup` | Selección de source/cache para el stem N. `sourceFlac=1` implica bypass directo de `StemCache`; los callers legacy consultan hit/miss. |
| `loadSongStems.stem#N.createReader` | `stem#N.createReader` | `playbackFormatManager.createReaderFor()` — apertura del archivo de reproducción (FLAC canónico; otros formatos sólo para callers legacy/no canónicos). |
| `loadSongStems.stem#N.prepareToPlay` | `stem#N.prepareToPlay` | `StemTrack::prepareToPlay()` — inicializa el reader y construye `ResamplingPositionableSource` + `StemBufferingSource`. |
| `loadSongStems.stem#N.waitForReady` | `stem#N.waitForReady` | `waitForNextAudioBlockReady()` — read-ahead warm-up (timeout 250ms) |
| `loadSongStems.waitForReady.total` | `waitForReady.total` | Suma de todos los `waitForReady` per-stem |
| `loadSongStems.fadeOut.wait` | `fadeOut.wait` | Espera al fade-out del song previo (hasta 1500ms) |
| `loadSongStems.suspendProcessing.wait` | `suspendProcessing.wait` | Espera al drain del audio thread (hasta 2000ms) |
| `loadSongStems.bungee.prepare` | `bungee.prepare` | `prepareRoutingTimeProcessors()` — allocación de voces Bungee (2 por stem) |
| `loadSong.settings.restore` | `settings.restore` | Restauración de mix, click, guide y arrangement post-load. El pack global de guía se reutiliza si ya está cargado. |

CSV: `song_load_<ts>.csv`, una fila por sub-stage. Columnas: `wall_ms, phase, sub_phase, duration_us, rss_kb, extra`. `extra` carga metadata variante (hit/miss, ext, size, etc).

### 2. StemCache hit/miss (one-shot events)

`StemCache::isValid()` se invoca sólo para fuentes no FLAC recibidas por rutas
legacy o callers no canónicos. Los stems FLAC de un `.pwproj` se reproducen
directamente y registran `sourceFlac=1`, sin crear una segunda copia FLAC.
Cada consulta real de cache registra un evento con la razón de la decisión:

| Evento | Cuándo |
|---|---|
| `StemCache.isValid.hit` | El FLAC existe, sidecar parsea, size y mtime matchean |
| `StemCache.isValid.miss.noFlac` | `decoded/<songId>/stem_<hash>.flac` no existe (cache vacía o split-path legacy) |
| `StemCache.isValid.miss.noMeta` | El FLAC existe pero el sidecar `.flac.meta.txt` no |
| `StemCache.isValid.miss.badMeta` | El sidecar existe pero no tiene `source_mtime_ms` o `source_size` |
| `StemCache.isValid.miss.sizeMismatch` | El sidecar tiene un size distinto al source legacy actual. |
| `StemCache.isValid.miss.mtimeMismatch` | El mtime del source legacy no coincide con el del sidecar. |

CSV: `song_load_<ts>.csv`. El campo `extra` lleva `us=<lookup_microseconds>` y, para los miss con detalle, los valores `got=...` vs `want=...` que permiten reconstruir por qué falló el cache.

### 3. Memory snapshot (RSS por fila)

Cada `recordXxx` cierra con un `getCurrentRssKb()` que se escribe en la columna `rss_kb`. Implementado via `getrusage(RUSAGE_SELF)` que en macOS retorna `ru_maxrss` en KiB.

Los deltas entre filas del mismo stage permiten ver asignaciones grandes. Una
captura histórica de "Al Que Es Digno / Vencio" (12 stems M4A previos a la
política FLAC estricta) mostró:
- Inicio: ~470 MB
- Post `project.open`: ~478 MB (+8 MB del manifest)
- Post 12× `prepareToPlay`: ~572 MB (+94 MB de los 12 buffers read-ahead de 256K cada uno)
- Post `bungee.prepare`: ~596 MB (+24 MB de las 24 voces Bungee)

---

## Archivos del sistema

| Archivo | Rol |
|---|---|
| `src/SongLoadProbe.h` | Definición de la clase, `ScopedSongLoadTimer` |
| `src/SongLoadProbe.cpp` | Implementación, escritura CSV, `getrusage` |
| `src/PluginEditor.cpp` | Bracket externo `loadSong.total`, sub-stages `project.open`, `processor.loadSongStems`, `settings.restore` |
| `src/PluginProcessor.cpp` | Sub-stages per-stem en `loadSongStems` (cache.lookup, createReader, prepareToPlay, waitForReady, totals) |
| `src/audio/StemCache.cpp` | Hit/miss events con razón detallada |

---

## CSV generado

```
~/Library/Logs/MTControls/song_load_<ts>.csv
```

Header:
```
wall_ms,phase,sub_phase,duration_us,rss_kb,extra
```

Una carga típica de 12 stems produce 60-80 filas. Para análisis rápido en awk / Python, ver los snippets de la sección "Cómo analizar".

---

## Cómo correr un experimento reproducible

1. **Build**: `cd build/macos-release && ninja MTControls_Standalone`
2. **App**: `open build/macos-release/MTControls_artefacts/Release/Standalone/PlayWorship.app`
3. **Reproducir** la carga objetivo: abrir la app (carga la última canción seleccionada), o navegar en un setlist. La carga ocurre en el message thread; la duración típica es 0.5-1.5 s.
4. **Recolectar datos** del CSV más reciente en `~/Library/Logs/MTControls/song_load_*.csv`
5. **Comparar** contra la sesión previa (mismo escenario, misma canción) usando `git diff` del CSV o el snippet de análisis.

### Snippet de análisis

```python
import csv
from collections import defaultdict

totals = defaultdict(lambda: [0, 0])
with open("song_load_20260813_195223.csv") as f:
    for r in csv.reader(f)[1:]:
        if len(r) < 4: continue
        try: d = int(r[3])
        except: continue
        key = f"{r[1]}.{r[2]}"
        totals[key][0] += 1
        totals[key][1] += d

for k, (c, t) in sorted(totals.items(), key=lambda x: -x[1][1])[:15]:
    print(f"{k:<48} {c:>3} {t:>10}us  avg={t//c if c else 0}us")
```

Salida histórica para "Al Que Es Digno / Vencio" antes de reutilizar el pack
global de guía:
```
loadSong.settings.restore                       1   408000us  avg=408000us
loadSongStems.bungee.prepare                    1    38000us  avg=38000us
loadSong.project.open                           1    27000us  avg=27000us
loadSongStems.stem#0.prepareToPlay              1     5800us  avg=5800us
loadSongStems.stem#5.createReader               1     2600us  avg=2600us
loadSongStems.stem#3.cache.lookup               1      250us  avg=250us
...
```

---

## Diagnósticos encontrados (este benchmark reveló)

| Hallazgo | Métrica que lo expuso | Optimización resultante |
|---|---|---|
| StemCache `mtimeMismatch` masivo en stems legacy precomputados | `StemCache.isValid.miss.mtimeMismatch` count = 8/12 | La política actual evita el problema: los `.pwproj` contienen FLAC y omiten `StemCache`. |
| `settings.restore` consume 34% del total del swap (408ms / 1.2s) | `loadSong.settings.restore` duration | Corregido: `setDynamicGuideSettings()` ya no suspende audio ni recarga el pack global en cada canción. |
| Split-path legacy de StemCache (subdirs anidados por `/` en titles) | `miss.noFlac` con `path=decoded/Al Que Es Digno / Vencio/...` | `StemCache::migrateLegacyCacheDirectories` 3-stage (recupera espacio + walk + cleanup) |

---

## Limitaciones conocidas

- **Solo UI thread**: no captura el audio thread durante la carga. Para eso está `AudioDiagnostics`, que se solapa en el tiempo durante la primera media segundo post-load.
- **Mutex-guarded**: cada `recordXxx` toma un `std::mutex`. La latencia es ~1-3 μs por call, negligible para un load que tarda 1+ segundos, pero no apto para el audio thread.
- **No captura ZIP-level I/O del .pwproj**: el `project.open` mide end-to-end pero no desglosa manifest-parse vs stem-extract. Si el proyecto es muy grande, se puede añadir sub-stages dentro de `ProjectStore::open`.
- **Wall-clock drift**: los timestamps vienen de `juce::Time::getHighResolutionTicks()` por stage. Si el clock base se descalibra entre stages, los deltas pueden tener jitter ±100 μs en hardware de baja calidad.
- **RSS no diferencia heap de mmap**: el `.pwproj` ZIP se mapea entero en memoria al abrirse; un proyecto de 449 MB infla RSS ~450 MB aunque solo se hayan extraído 12 stems. El delta real de heap es menor.
- **RSS no está disponible en Windows**: el build MSVC no expone `getrusage`; el probe conserva la columna `rss_kb` y escribe `-1`, sin afectar la carga ni las métricas de tiempo.

---

## Cómo extender

Para añadir un nuevo stage al benchmark:

1. **Envolver** el bloque con `ScopedSongLoadTimer t("phase", "sub")` o `SongLoadProbe::instance().beginPhase(...)` / `endPhase()`.
2. **Para loops** donde RAII no aplica, usar `recordDurationUs()` con el `juce::Time::getHighResolutionTicks()` calculado a mano.
3. **Para eventos sin duración** (cache hit, etc), usar `recordEvent("phase", "subPhase", "extra=...")`.
4. **Para per-stem** sub-stages, usar `recordStemStage(stemIdx, "stage", us, "extra")` que formatea automáticamente el `sub_phase` como `stem#N.stage`.
5. **Documentar** en este archivo (sección "Qué mide" + tabla de métricas).
6. **Commitear** con mensaje que incluya la métrica antes/después.

---

## Relación con `AudioDiagnostics`

Ambos sistemas coexisten, midiendo dimensiones complementarias:

| Aspecto | `AudioDiagnostics` | `SongLoadProbe` |
|---|---|---|
| **Thread** | Audio thread (lock-free ring buffer) | UI / message thread (mutex) |
| **Mide** | Per-block performance durante reproducción | Per-stage del swap de canción |
| **Resolución** | Microsegundos por stage del processBlock | Microsegundos por stage del load path |
| **CSV** | `audio_<ts>.csv`, `audio_stems_<ts>.csv`, `audio_gaps_<ts>.csv` | `song_load_<ts>.csv` |
| **Activación** | Automática al construir el processor | Lazy, se inicia en el primer `recordXxx` |
| **Cuándo correr** | Reproducir 2-5 min una canción | Cargar una canción (swap o startup) |

Para una sesión completa de diagnóstico de "tarda mucho en cargar", se recomienda correr ambos y cruzar los CSVs: `song_load_*.csv` te dice qué fase del swap domina, `audio_*.csv` te dice si el audio thread queda starving durante el warm-up.
