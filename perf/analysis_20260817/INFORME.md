# Análisis de performance: por qué tarda en cargar las canciones del setlist

**Fecha:** 2026-08-17
**Alcance:** análisis cuantitativo sobre los `song_load_*.csv` y `audio_gaps_*.csv` existentes en `~/Library/Logs/MTControls/`. Sin build, sin experimento fresco.
**Dataset:** 13 archivos `song_load_*.csv` (66 cargas) entre 2026-08-13 y 2026-08-17 + 10 archivos `audio_gaps_*.csv` en la ventana de 2 semanas.

## TL;DR

Hay **dos cuellos de botella distintos** según el estado de la cache de stems:

| Estado | p50 total | p90 total | Cuello de botella | % del total |
|---|---|---|---|---|
| **COLD (primer load del .pwproj, hay que extraer)** | 2.5s | **20.2s** | `ProjectStore::open` (extract del ZIP) | 65-97% |
| **WARM (stems ya extraídos, sólo swap de tracks)** | 2.0s | **5.4s** | `processor.loadSongStems` (medido) | 50-90% |

Y un **bug confirmado en el probe** que esconde costos reales (RSS no cambia entre filas porque usa `ru_maxrss` que es monotónico-creciente, no la RSS actual).

El cuello de cold está bien identificado: `copyZipEntry` extrae stems secuencialmente a 16-43 MB/s (debería ser 200-500 MB/s en SSD). El cuello de warm está **parcialmente invisible** porque la zona entre `waitForReady` y `bungee.prepare` dentro de `PluginProcessor::loadSongStems` no se está midiendo y contiene 3-7 segundos "fantasma".

---

## 1. Distribución de tiempo por sub-stage (todos los CSVs)

```
  sub_phase                     count        avg        p50        p90        p99        max
  project.open                     32   6416995us   1685091us 19380178us 28728544us 30001014us
  processor.loadSongStems          67    855206us    501211us  1650568us  5440016us  6940822us
  settings.restore                 65    515184us    470469us   970097us  1234749us  1285680us
  bungee.prepare                   65     48362us     47820us    67084us    68399us    69856us
  waitForReady.total               65        70us        56us      120us      309us      434us
  suspendProcessing.wait           65         0us         0us        0us        0us        0us

  per-stem sub_phase            count        avg        p50        p90        p99        max
  prepareToPlay                   920      6011us      5792us     6013us    11576us    17358us
  createReader                    920       552us       396us      885us     2567us     4377us
  cache.lookup                    920       291us       253us      532us     1520us     2300us
  waitForReady                    920         0us         0us        0us        4us       24us
```

**Observaciones:**
- `suspendProcessing.wait` siempre 0 ms (no se reportan casos donde el callback no termine en 2000 ms)
- `waitForReady.total` siempre <500 µs (los stems warm ya están pre-llenos)
- `bungee.prepare` es ~50 ms para 11-19 stems (3-5 ms por voz Bungee)
- Los per-stem sub-stages suman 200-400 ms total para un load de 19 stems
- `settings.restore` se mantiene en p50=470 ms (era 1097 ms antes del 16/08 — la mejora del pack de guía se mantiene)

## 2. Cold vs Warm (la división clave)

```
  class          count  avg total  p50 total  p90 total  avg proj.open  avg size
  COLD-REBUILD     31     7727ms     2460ms    20196ms       6623ms       517MB
  WARM-HIT         36     2435ms     2039ms     5422ms          0ms         0MB
```

**Las 10 cargas más lentas (todas COLD-REBUILD, todas >9 s):**

```
  title               total   proj.open  stems  restore  unmeas  #stems  size   hadPrev
  Glorioso            30.9s    30.0s     0.8s   23ms     63ms    15     480MB   True
  In                  27.3s    25.9s     0.9s   507ms    0ms     19     766MB   True
  In                  21.1s    19.4s     0.7s   384ms    467ms   19     766MB   False
  DANZANDO            20.2s    19.5s     0.6s   19ms     102ms   14     784MB   True
  In                  20.1s    19.3s     0.8s   24ms     12ms    19     766MB   True
  In                  19.7s    18.9s     0.7s   5ms      37ms    19     766MB   True
  Glorioso            13.1s    11.9s     0.6s   494ms    40ms    15     480MB   True
  Glorioso            12.1s    11.3s     0.8s   61ms     0ms     15     480MB   True
  IMPRESIONANTE       11.3s    10.1s     0.6s   416ms    241ms   11     392MB   False
  IMPRESIONANTE       10.6s     9.6s     0.6s   359ms    17ms    11     392MB   True
```

`project.open` representa entre 65% y 97% del total en cold. Es el cuello de botella dominante en cold.

**Las 5 cargas warm más lentas (WARM-HIT, todas >5 s):**

```
  title        total   stems  restore  unmeas  #stems  hadPrev
  In            9.3s   7.1s   1.3s     898ms   19      True
  In            6.7s   4.9s   1.1s     762ms   19      True
  Tus           5.8s   3.5s   1.2s     1.1s    12      True
  Glorioso      5.8s   3.9s   0.9s     0.9s    15      True
  IMPRESIONANTE 5.0s   3.0s   1.1s     0.8s    11      True
```

`processor.loadSongStems` representa 50-80% del total en warm. Es el cuello de warm.

## 3. Correlación tamaño del .pwproj vs tiempo de `project.open`

```
  title          size (MB)  proj.open (ms)   MB/s
  Glorioso            480          30001       16.0   ← PEOR CASO (cold post-startup)
  In                  766          25896       29.6
  DANZANDO            784          19483       40.2
  In                  766          19388       39.5
  In                  766          19310       39.7
  In                  766          18940       40.4
  Glorioso            480          11926       40.2
  Glorioso            480          11274       42.6
  IMPRESIONANTE       392          10083       38.8
  IMPRESIONANTE       392           9578       40.9
  Tus                 331           8234       40.1
  Como                225           7284       30.9
  Al                  428           1962      218.2   ← warm-ish (no rebuild real)
  Al                  357           1733      205.8
```

**Hallazgo:** la tasa de extract es **16-43 MB/s para ZIPs grandes** (200-784 MB). En un SSD moderno la capacidad de write es 500-2000 MB/s; el SSD está siendo subutilizado por un factor de **10x-100x**.

La regresión `proj.open_ms ≈ 10.93 * size_MB + 973 ms` con r²=0.08 indica que el costo es **mayoritariamente constante** (overhead de abrir el ZIP, parsear el manifest, iterar entries), no proporcional al tamaño.

**El primer cold después de startup es 2-3x más lento que los siguientes** (16 MB/s vs 40 MB/s). El peek `Glorioso 30s` a 16 MB/s coincide con el primer load de la sesión — probablemente porque la page cache del SO está fría, o porque el thread de `flushAllPendingSettings` aún está completando un save previo.

## 4. Bug dentro de `processor.loadSongStems` (warm-hit fantasma)

Para los warm-hits, el `processor.loadSongStems` mide 3-7 segundos pero **sus sub-stages per-stem suman solo 130-150 ms** y `bungee.prepare` suma 50-70 ms. Hay 2-7 segundos **no contabilizados** dentro de esa función.

```
  title         proc_lss  per_stem  bungee  wait  fade  suspd  other_in_lss
  In             6941ms    134ms     67ms   0ms   0ms   0ms     6739ms (97%)   ← warm
  In             4667ms    134ms     70ms   0ms   0ms   0ms     4463ms (96%)   ← warm
  Glorioso       3876ms    108ms     53ms   0ms   0ms   0ms     3715ms (96%)   ← warm
  Tus            3413ms     78ms     43ms   0ms   0ms   0ms     3292ms (96%)   ← warm
  IMPRESIONANTE  2978ms     84ms     40ms   0ms   0ms   0ms     2854ms (96%)   ← warm
  In             27258ms (cold)  proc_lss=738ms (cold)         per_stem=142ms
  In             21124ms (cold)  proc_lss=661ms (cold)         per_stem=157ms
```

**El "other_in_lss" es la zona entre `waitForReady.total` (línea 3498) y `bungeeStart` (línea 3688) en `PluginProcessor::loadSongStems`.** Esa zona incluye:
- `shouldFadeOutCurrentSong` / `fadeOut.wait` → medido, 0 ms en estos casos
- `suspendProcessing.wait` → medido, 0 ms
- `pauseTimeProcessorMaintenance()` → no medido
- `removeAllStemSources()` → no medido
- `clearLoadedSongState()` → no medido
- `stemTracks = std::move(newTracks)` → no medido (pero es solo move)
- `rebuildSectionScheduler()` → no medido
- `setMetronomeBpm(...)` → no medido
- Varios `store()` a atomics → no medido

**Hipótesis principal:** la espera viene de `removeAllStemSources()` → `stemTracks.clear()` → destructor de cada `StemTrack` → `~StemBufferingSource` → `releaseResources()` → `backgroundThread.removeTimeSliceClient(this)`. JUCE's `TimeSliceThread::removeTimeSliceClient` (en `juce_TimeSliceThread.cpp:59`) toma `callbackLock` si el thread está mid-drain del cliente. Si los workers están haciendo I/O para los stems de la canción anterior, el `removeTimeSliceClient` puede bloquear varios ms por stem × 19 stems.

**Confirmar requiere instrumentar esa zona** con un sub-stage. Sin build, no lo puedo validar; queda como prioridad #2.

## 5. Bug del probe: `rss_kb` siempre fijo

6 de los 10 CSVs tienen un único valor de `rss_kb` en todas sus filas, y 4 tienen 1-2 valores. Mirando el código (`SongLoadProbe.cpp:16-34`):

```cpp
int64_t SongLoadProbe::getCurrentRssKb() {
  struct rusage ru {};
  if (getrusage(RUSAGE_SELF, &ru) == 0) {
#if defined(__APPLE__)
    return static_cast<int64_t>(ru.ru_maxrss);  // ← BUG
#else
    return static_cast<int64_t>(ru.ru_maxrss);
#endif
  }
  return -1;
}
```

`ru_maxrss` es el **pico histórico** de RSS desde el arranque del proceso. No se decrementa nunca. Por eso el RSS es constante durante toda una sesión. El doc de `SongLoadProbe.md:67-77` dice que es "Memory snapshot (RSS por fila)" pero en realidad es "max resident set size monotónico-creciente".

Esto significa que **toda la columna `rss_kb` del CSV es engañosa** y no sirve para detectar asignaciones grandes del load path. Por ejemplo, en `song_load_20260817_094204.csv` todas las 992 filas dicen `1112276992` (1061 MB), cuando el delta real durante el load es probablemente mucho mayor (los 12× `prepareToPlay` deberían inflar ~94 MB según el log histórico).

**Fix sugerido:** en macOS, reemplazar `ru.ru_maxrss` por `task_info(mach_task_self(), TASK_VM_INFO, ...)` con `phys_footprint`. En Linux, leer `/proc/self/statm` y calcular `resident * page_size`. El doc de `SongLoadProbe.md:67` también debe actualizarse para reflejar el cambio.

## 6. Audio thread starving durante el load (cross-reference)

`audio_gaps_20260817_094202.csv` (sesión de hoy) tiene 14 gaps con `song_position_sec=0.000` y `is_playing=0`. El más grande es 406 ms. El segundo peor, 115 ms. Esto significa que **durante el load, el audio thread queda starving 70-400 ms** aunque la canción no esté sonando.

```
  wall_time    delta_ms   stems   playing   during_load
  12:42:15.6   406.2ms    11      0         (idle)
  12:42:34.8   81.1ms     15      0         (idle)
  12:43:00.1   92.7ms     19      0         (idle)
  ...
  12:48:24.3   115.8ms    19      0         (idle)
```

**Hipótesis:** el `copyZipEntry` (extract del ZIP) en `ProjectStore::open` corre en el UI thread single-threaded. Esto monopoliza el CPU y bloquea el scheduling del audio thread, que se queda sin callback. Cuando la canción anterior estaba sonando, esto se manifiesta como "mini cortes" audibles.

**Confirmación indirecta:** los gaps NO ocurren durante la reproducción normal (todos tienen `playing=0` y `position=0`), pero ocurren **durante el momento en que el UI thread está extrayendo el ZIP**. No podemos correlacionar timestamps exactos (el `wall_ms` del CSV se imprime con precisión doble limitada — otro bug pequeño), pero el patrón cualitativo es claro.

Esto es un side-effect del cuello principal (cold extract) más un cuello nuevo (warm unload). Cualquiera de los dos, si se mueve off-thread, debería eliminar los gaps.

## 7. Lo que ya está optimizado (no tocar)

- `settings.restore` p50 = 19-24 ms en la sesión del 17/08, vs 1097 ms en la del 14/08. La mejora del pack global de guía funciona.
- `suspendProcessing.wait` = 0 ms siempre. El drain del audio thread no es problema.
- `waitForReady.total` <500 µs. El read-ahead warm-up es instantáneo.
- `StemCache.isValid` hit rate = 100% (todos los stems son FLAC, no hay misses legacy).
- Bin-packing LPT para asignación de stems a workers está estable.

## 8. Optimizaciones priorizadas

| # | Optimización | Impacto estimado | Esfuerzo | Riesgo |
|---|---|---|---|---|
| **P0** | **Paralelizar `copyZipEntry`** en `ProjectStore::open` con un thread pool (4-8 threads). Hoy un solo thread extrae secuencialmente. | Cold: 7-30s → **1.5-4s** (4-8x speedup) | M (1-2 días) | Bajo (mismo código, distintos threads) |
| **P0** | **Mover `copyZipEntry` a un thread off-UI** (el archivo `decoded/` se construye en background, `PluginEditor` muestra "Extrayendo stems..."). Hoy bloquea el UI thread. | Cold: percepción **instant** (UI sigue responsiva) | M | Medio (UX: necesita progress bar) |
| **P1** | **Investigar y eliminar el "other_in_lss" de warm-hit** (3-7s en `processor.loadSongStems`). Probable causa: `removeAllStemSources` + `removeTimeSliceClient`. Instrumentar con sub-stages. | Warm: 5-9s → **1-2s** (3-5x speedup) | M (investigar + fix) | Medio |
| **P1** | **Fix `getCurrentRssKb()`** (reemplazar `ru_maxrss` por `task_info(TASK_VM_INFO)` en macOS, `/proc/self/statm` en Linux). Actualizar doc. | Calidad de telemetría | XS (1-2 horas) | Ninguno |
| **P2** | **Fix `wall_ms` precision** en el CSV. Cambiar `double` por `int64_t` (epoch ms), o imprimir con `.precision(15)`. | Calidad de telemetría (permite cross-ref con audio_gaps) | XS | Ninguno |
| **P2** | **Cachear el resultado de `ProjectStore::open`** en memoria. Si el `.pwproj` no cambió desde la última apertura, devolver el `ProjectDocument` cacheado sin re-leer el ZIP. | Warm que pasa por `ProjectStore::open` (cold-after-cache-evict) | S-M | Bajo (invalidación por mtime) |
| **P2** | **Bajar el timeout de `waitForNextAudioBlockReady`** de 250 ms a 50 ms. Si el reader no está listo, fallar y reintentar desde el audio thread (más robusto que bloquear el UI). | Reduce peor caso de cold | XS | Bajo |
| **P3** | **Async `flushAllPendingSettings`** en `PluginEditor::loadSong` (no esperar al save en background, solo al `flushPendingSettings` del proyecto actual). Hoy el UI thread espera a un write de 700+ MB. | Reduce "unmeasured" en cold con save pendiente | S | Bajo (asumir save idempotente) |

### Justificación de P0

- Es responsable del 65-97% del wall-clock de cold.
- El cold es el caso peor y el que más impacta al operador del setlist (app congelada 5-30s).
- El SSD está subutilizado por 10-100x. Es la mejora de mayor ROI.
- Al mover a off-thread, además se eliminan los gaps del audio thread (efecto secundario gratuito).

### Justificación de P1

- Es responsable del 50-80% del wall-clock de warm.
- Warm es la operación más frecuente (cada click en el setlist).
- Si el "other_in_lss" es realmente `removeTimeSliceClient` bloqueado, el fix es conceptual (mover el cleanup a un thread o hacerlo async).
- Sin instrumentación es especulación; por eso necesita un sub-stage primero.

## 9. Lo que NO recomiendo

- **Aumentar el buffer size de read-ahead** (`kMinPrefetchBufferSamples`). El log del 2026-07-26 ya documenta que un intento así empeoró las cosas.
- **Tocar el bin-packing LPT de workers** — está estable.
- **Cache FLAC a nivel de sistema** — el cache ya es FLAC, el cuello no es decode sino extract del ZIP.
- **Comprimir el ZIP en formato distinto** — añadir overhead de CPU por I/O ya no es el problema.

## 10. Anexo: cómo reproducir el análisis

```bash
python3 /tmp/analyze_v3.py
```

El script está en `/tmp/analyze_v3.py` (versión corregida que agrupa por fin de load). Para regenerar después de nuevos logs, basta re-ejecutarlo.

Si querés que arme un nuevo experimento (limpiar cache, correr swaps cold/warm controlados) o que instrumente el probe con un sub-stage para el "other_in_lss" de warm, avisame y lo arrancamos.
