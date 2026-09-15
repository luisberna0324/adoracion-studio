# Análisis del flujo de importación, generación y limpieza de canciones

**Fecha:** 2026-09-12
**Alcance:** revisión end-to-end del flujo de import/generate/load/playback/delete de canciones en el plugin JUCE, con foco en (a) cuellos de botella de carga y reproducción y (b) integridad del sistema de archivos de la biblioteca. Sin build, sin experimento fresco: se inspeccionan el código y el estado actual del disco.
**Audiencia:** Luis (dev principal y único del plugin).

## TL;DR

Se confirman los dos miedos planteados:

1. **La app SÍ está acumulando caché de manera permanente.** Encontré **~770 MB de archivos huérfanos** en este momento en `~/Library/MTControls/`. El origen es estructural: tres rutas distintas escriben cachés que nunca se reconcilian con la biblioteca (FLAC precompute, blobs cloud descargados, staging de subida).
2. **La eliminación de canciones SÍ está rota en varios casos.** `SongLibraryStore::deleteSong` limpia 4 de los 6 directorios que deberían limpiarse. Dos se escapan siempre; un tercero (staging de cloud-sync) nunca se limpió en ninguna versión del código.

Adicionalmente se confirman los cuellos de botella del informe 2026-08-17:
- **P0 cold-extract** sigue intacto (single-threaded copyZipEntry, 16-43 MB/s).
- **P1 warm-swap "other_in_lss"** ya tiene los sub-stages nuevos (`pauseMaintenance`, `swap.lockRegion`) instrumentados pero todavía no hay CSVs nuevos para validar la hipótesis del `removeTimeSliceClient`.

---

## 1. Estado actual del filesystem (`~/Library/MTControls/`)

Inspección hecha con `du -sh` y `find` el 2026-09-12 17:15 ART.

| Directorio | Tamaño | Archivos | Referenciados por `songs.json` | Huérfanos |
|---|---:|---:|---:|---:|
| `decoded/` (FLAC cache) | 193 MB | 1 dir, 15 stems | 0 | **1 dir completo** |
| `project-cache/` | 2.0 GB | 2 caches | 2 | 0 ✓ |
| `cloud-content/objects/` | 1.7 GB | 54 blobs | 30 | **22 archivos, ~580 MB** |
| `cloud-artwork/` | 204 KB | 5 jpgs | 3 | **2 archivos** |
| `cloud-sync-staging/` | 300 KB | 3 dirs (manifest+waveform) | n/a | **1 dir completo** (ALABA) |
| `projects/` (cloud .pwproj) | 1.7 GB | 3 .pwproj | 2 | **1 archivo** |
| `waveforms/` | 352 KB | 4 .bin | 4 | 0 ✓ |
| `audio/` (legacy) | 0 B | — | — | — ✓ |

**Biblioteca actual (`songs.json`):** 4 canciones (Glorioso Día, LA SUNAMITA, In Jesus Name, IMPRESIONANTE).

**Cruft recuperable con un sweep correcto:** ~580 MB (cloud huérfano) + 193 MB (FLAC cache huérfano) ≈ **~770 MB**.

---

## 2. Bugs confirmados con evidencia (cache y eliminación)

### 2.1. `StemCache` (cache FLAC) NUNCA se borra al eliminar una canción

**Evidencia:**
- `src/audio/StemCache.h:34-118` define la API. **No hay `remove/delete/purge/clear`.** Verificado con `grep -rn "StemCache::remove\|StemCache::delete\|StemCache::purge\|StemCache::clear" src/` → 0 matches.
- `src/data/SongLibraryStore.cpp:551-631` (`deleteSong`) limpia 4 cosas:
  - `getWaveformCacheDir().getChildFile(trimmedSongId + ".bin").deleteFile();` (línea 601)
  - `getAudioRoot().getChildFile(trimmedSongId).deleteRecursively();` (línea 602)
  - `ProjectStore::discardLocalState(...)` (línea 603) → borra `project-cache/<id>/`
  - `CloudSyncIndexStore::removeByProjectId(...)` (línea 605)
  - Y limpia blobs cloud no referenciados (líneas 609-626)
- **Pero NO limpia `decoded/<sanitize(songTitle)>/`**, que es el directorio que `StemCache` usa (`StemCache.cpp:22-37`).
- El cache FLAC se escribe en `loadSongStems` solo cuando el stem NO es FLAC (`PluginProcessor.cpp:3412,3427`).

**Disco actual:** `decoded/Glorioso Día/` con 15 stems FLAC = 193 MB. Glorioso Día ya es un proyecto `.pwproj` con stems FLAC propios (`project-cache/.../audio/*.flac`), así que el cache FLAC precomputado de cuando era legacy nunca se consulta y es 100% peso muerto.

**Causa raíz:** la API de `StemCache` nunca tuvo un método de remoción, y nadie llamó a `decoded/<sanitize(title)>/deleteRecursively()` desde fuera.

### 2.2. `finalizeLegacyProjectMigration` deja el cache FLAC atrás

**Evidencia:** `SongLibraryStore.cpp:912-999` (`finalizeLegacyProjectMigration`):
- Línea 946-963: mueve `audio/<songId>/` a `migration-staging/<songId>-<uuid>/`.
- Línea 965: reemplaza la entrada de la biblioteca con la versión `.pwproj`.
- Línea 976: borra el staging.
- **Pero nunca toca `decoded/<title>/`.**

Esto es el mecanismo exacto que produjo el 193 MB de Glorioso Día: era legacy (M4A) → se precomputó cache → se convirtió a `.pwproj` → `audio/<id>/` se borró pero `decoded/<title>/` quedó.

### 2.3. `cloud-content/objects/` acumula 22 blobs huérfanos (~580 MB)

**Evidencia:**
```
54 archivos en disco
30 referenciados por songs.json (vía stems.storedPath, stems.sourcePath, artworkPath)
────────────────────────────────
22 huérfanos (~580 MB)
```

La lógica de `deleteSong` (líneas 569-626 de `SongLibraryStore.cpp`) **sí intenta limpiar** los blobs cloud, pero solo captura los paths del song que se está borrando en `removableCloudObjects`. Si los blobs vinieron por un camino que no actualiza `songs.json` (cloud download staging incompleto, version vieja del código, clearLocalLibrary, etc.), nadie los limpia después.

**No hay un sweep periódico** que reconcilie `cloud-content/objects/` con la biblioteca. La única limpieza de toda-la-biblioteca está en `clearLocalLibrary` (`CloudSyncIndexStore.cpp:120-148`), que borra `cloud-content/` entero (junto con `decoded`, `cloud-artwork`, etc.) — pero ese es un botón de pánico, no un mantenimiento.

### 2.4. `cloud-artwork/` acumula 2 jpgs huérfanos

**Evidencia:** 5 archivos, 3 referenciados, 2 huérfanos.

**Causa:** `deleteSong` ni siquiera mira `cloud-artwork/`. La cache se llena en `CloudLibraryClient.cpp:40-65` (`cacheCloudArtwork`) cuando un stem cloud tiene `artworkSource` que necesita descarga. Como el nombre es `sha256.ext` (content-addressed), no hay forma de saber a qué canción pertenece desde el filesystem.

### 2.5. `cloud-sync-staging/<projectId>/` se acumula sin limpieza NUNCA

**Evidencia:**
- `CloudProjectArchive.cpp:83-86`: `prepareUploadRequest` crea `cloud-sync-staging/<projectId>/manifest.json` y luego `cloud-sync-staging/<projectId>/waveform.bin` (línea 130).
- `grep -n "staging\|deleteRecursively" src/data/CloudProjectArchive.cpp`: solo 3 hits, todos en `prepareUploadRequest`. **No hay cleanup en éxito, en fallo ni en aborto.**
- Disco actual: 3 directorios (LA SUNAMITA, Glorioso Día, "ALABA" huérfano). Cada uno pesa ~100 KB pero es un leak garantizado por cada upload.

### 2.6. `projects/` (cloud .pwproj) tiene 1 huérfano

**Evidencia:** 3 archivos `cloud-*.pwproj` en `projects/`. Solo 2 referenciados por `songs.json` (vía `cloud-sync-index.json`). El tercero (`cloud-fcd396b6c77b40d3beb0b50ba6e37305.pwproj`, "ALABA") quedó de una descarga cuya entrada fue eliminada.

Esto es intencional según el doc (`docs/okf/processor/MTControlsAudioProcessor.md:156-160`): "nunca borra el archivo `.pwproj`". Pero en `projects/` (que es donde la app los guarda localmente) sí debería limpiarse.

### 2.7. Bug de probe: `rss_kb` siempre es el pico histórico

**Evidencia:** `SongLoadProbe.cpp:16-34` usa `ru_maxrss` que es monotónico-creciente. 6 de los 10 CSVs en `~/Library/Logs/MTControls/` tienen un único valor de `rss_kb` en todas las filas. La columna es inservible para detectar asignaciones grandes durante el load. **Esto ya estaba en el informe 2026-08-17 (#5) y sigue sin arreglar.**

### 2.8. Probe: `wall_ms` con precisión rota

**Evidencia:** el CSV imprime `1.78924e+12` en notación científica. `grep "1\.78924e+12" song_load_20260912_152639.csv` → presente. No se puede cross-referenciar con `audio_gaps_*.csv` por timestamp. **Esto también estaba en el informe 2026-08-17 (#2 P2).**

---

## 3. Cuellos de botella de carga y reproducción

### 3.1. Cold-extract del `.pwproj` (P0, intacto)

**Estado actual (sin cambios desde el 17/08):**
- `ProjectStore.cpp:555-595` (`copyZipEntry`): single-threaded, 16-43 MB/s en SSD.
- 65-97% del wall-clock de un cold load va a esto.
- En el log histórico: 30.9 s para 480 MB, 27.3 s para 766 MB.

**Acción:** thread-pool con 4-8 workers extrayendo entries en paralelo. Riesgo bajo (las entries del ZIP son independientes). Beneficio estimado: cold 7-30s → 1.5-4s (4-8x). Eliminaría además los `audio_gaps` que el UI thread provoca durante el extract.

### 3.2. Warm-swap "other_in_lss" (P1, instrumentado pero no medido)

**Estado actual:**
- `PluginProcessor.cpp:3656-3763` ahora divide la zona sospechosa en `pauseMaintenance` + `swap.lockRegion`. Esto es trabajo del 17/08-12/09.
- **Pero no hay CSVs nuevos posteriores al cambio**, así que no podemos confirmar la hipótesis.

**Hipótesis (sigue vigente del 17/08):**
- `swap.lockRegion` contiene `removeAllStemSources()` (línea 3676) que es `stemTracks.clear()`.
- `~StemTrack` (`StemTrack.cpp:43`) llama `bufferingSource.reset()`.
- `~StemBufferingSource` (`StemBufferingSource.cpp:19`) llama `releaseResources()`.
- `StemBufferingSource::releaseResources` (línea 91) llama `backgroundThread.removeTimeSliceClient(this)`.
- JUCE `TimeSliceThread::removeTimeSliceClient` toma `callbackLock` (`CriticalSection`) y **espera** a que termine cualquier `useTimeSlice()` en curso.
- Para 19 stems × 2 fuentes (main + prefetch) = 38 llamadas por warm swap. Si los workers están mid-drain de read-ahead de la canción anterior, 50-200 ms cada una → 2-7 segundos total. Coincide con los "other_in_lss" del log.

**Acción:** build limpio, correr 5 warm-swaps, leer el CSV y comparar `pauseMaintenance + swap.lockRegion` vs el viejo "other_in_lss". Si la hipótesis es correcta, el fix es conceptual (mover el cleanup a un worker off-thread, o destruir `StemTrack` antes de pausar el `TimeSliceThread`).

### 3.3. `loadSongStems` mide cache hit pero ignora cache miss.pathological

**Evidencia:** `PluginProcessor.cpp:3404-3428`:
- `if (!sourceIsFlac && StemCache::isValid(file, songTitle))` → hit, usa cache.
- `else if (!sourceIsFlac) { precomputeAsync(file, songTitle, nullptr); }` → dispara precompute.
- Pero **si la cache existe y está stale** (mtime mismatch), tira a la rama de precompute en background Y carga el source original por la rama `hit=false`. Eso es 2x trabajo: leer el FLAC stale que ya no matchea + leer el source original + computar el nuevo FLAC en background.

Peor: si el cache es stale para 8/12 stems (caso del informe 17/08: "miss.mtimeMismatch count = 8/12"), el primer load paga 2x durante ~0.5-1s.

**Acción:** cuando `isValid()==false` por mtime/size mismatch, considerar borrar el cache stale inmediatamente y hacer `precompute` (no `precomputeAsync`) — el audio thread puede usar el source original mientras el nuevo FLAC se escribe. Pero el caller ya marca `playbackFile = file` así que el comportamiento actual es seguro. La mejora es eliminar el FLAC stale para no acumularlo.

### 3.4. `settings.restore` (P2 histórico)

**Estado actual:** p50 ≈ 470 ms, fue 1097 ms. Mejoró con el batch de guía dinámica. Sigue siendo el segundo consumidor en warm-swap. **No es prioritario** comparado con el warm "other_in_lss".

---

## 4. Otros gaps y observaciones

### 4.1. `StemCache` key por `songTitle`, no por `songId`

**Evidencia:** `PluginProcessor.cpp:3412/3422/3427` usa `songTitle` (legible por humanos, mutable). El directorio del cache es `decoded/<sanitize(songTitle)>/`.

**Problemas concretos:**
1. Si el usuario edita el título de una canción en la biblioteca, el cache FLAC queda en el directorio viejo y un nuevo cache se construye en el directorio nuevo. El viejo se queda para siempre.
2. Dos canciones con el mismo título colisionan en el mismo directorio. El sidecar `mtime/size` las distingue pero el espacio se desperdicia (los 15 stems de "Glorioso Día A" viven junto a los 15 stems de "Glorioso Día B" sin separación lógica).

**Acción recomendada:** usar `songId` (UUID) como key del cache. Es estable, único, y resuelve los dos problemas. El trade-off es perder inspección humana directa del directorio, pero el `decoded/` está pensado para ser interno (ver `StemCache.h:21-26`).

### 4.2. `getCacheDirectory` usa `createLegalFileName(projectId)` pero el directorio viene del `song.id` en algunos sitios

**Evidencia:**
- `ProjectStore.cpp:917`: `auto cache = getCacheDirectory(song.id);` — usa `song.id`
- `ProjectStore.cpp:1588-1592`: `getCacheDirectory(projectId)` — usa `projectId` (= `projectIdentityForSong(song) = projectId ?: song.id`).
- `ProjectStore.cpp:1648` (`discardLocalState`): usa `projectIdentityForSong(removedSong)` = `projectId ?: song.id`.

Para canciones proyecto-backed (`isProjectBackedSong`), `song.id == projectId` por construcción (`registerProjectSong` línea 760). Para legacy, `projectId` queda vacío y `projectIdentityForSong` retorna `song.id`. Por tanto las claves coinciden siempre.

**Conclusión:** no es bug hoy, pero es un acoplamiento implícito. Si en el futuro `song.id` y `projectId` divergen para legacy, el cache del open y el delete apuntarán a directorios distintos.

### 4.3. `songTitle` puede tener `/`, `\`, etc. — `sanitizeCacheKey` lo arregla pero el log de migración es ruidoso

**Evidencia:** `StemCache.cpp:39-64` (`sanitizeCacheKey`). Correcto. Pero la doc dice "el primer caso fue Al Que Es Digno / Vencio" y el comentario en `StemCache.h:96-98` lo reconoce.

**Acción:** menor. La solución actual funciona, pero combinada con el #4.1 (migrar a `songId`) elimina el problema de raíz.

### 4.4. `clearLocalLibrary` borra `decoded/` entero

**Evidencia:** `CloudSyncIndexStore.cpp:130-133`. Esto es correcto para un "panic reset" pero **no se ejecuta en el flujo normal**. No hay forma de recuperar espacio huérfano sin esto o sin borrar manualmente.

### 4.5. `flushAllPendingSettings` no se llama al eliminar

**Evidencia:** `deleteSong` (líneas 551-631) no llama `ProjectStore::flushAllPendingSettings()`. Si hay un settings-pending para el proyecto que se va a borrar, podría quedarse en la cola y escribirse en `project-cache/<id>/song-settings.json` después de que `discardLocalState` ya borró el directorio.

Esto es teórico (el flush se dispara desde la cola antes del próximo callback), pero podría dejar un `project-cache/<id>/song-settings.json` huérfano en el peor caso.

### 4.6. `imports/projects/` (iOS) — el `isManagedImportedProjectFile` se chequea en registerProjectSong pero no en deleteSong

**Evidencia:** `SongLibraryStore.cpp:37-47` define `managedImportedProjectsDirectory() = root/imports/projects/`. Solo se referencia en `registerProjectSong` (líneas 802-810, para borrar el paquete anterior cuando se re-importa el mismo `projectId`).

`deleteSong` NO mira `imports/projects/`. Si el usuario borra una canción importada desde Files en iOS, el `.pwproj` queda en `imports/projects/` para siempre. Esto es parcialmente intencional (la doc dice "nunca borra el archivo `.pwproj`"), pero para los que están bajo `imports/projects/` (que son COPIAS administradas por la app, no el original del usuario) podría limpiarse.

**Decisión:** confirmar con Luis si la política es "no borrar `.pwproj` en ningún caso" o "no borrar `.pwproj` que esté fuera del sandbox administrado". Probablemente la segunda, dado que el código de re-import sí limpia.

---

## 5. Optimizaciones priorizadas (actualizadas)

| # | Optimización | Impacto | Esfuerzo | Riesgo | Bloqueada por |
|---|---|---|---|---|---|
| **P0.A** | Agregar `StemCache::removeSongCache(songTitle)` y llamarlo en `deleteSong` y `finalizeLegacyProjectMigration` | Recupera ~193 MB por canción legacy borrada + previene acumulación futura | XS (1-2 h) | Ninguno | Nada |
| **P0.B** | Reconciliación al startup: para cada `<root>/decoded/<dir>/`, si `songs.json` no tiene ningún stem que matchee, eliminar. Idem `cloud-content/objects/`, `cloud-artwork/`, `cloud-sync-staging/` | Recupera ~770 MB ya perdidos + previene drift futuro | S (medio día) | Bajo (log + dry-run flag) | Nada |
| **P1.A** | Confirmar warm-swap "other_in_lss" con build limpio + 5 swaps, leer `pauseMaintenance` y `swap.lockRegion` del CSV | Permite priorizar P1.B con datos | XS (30 min experimento) | Ninguno | Build limpio |
| **P1.B** | Si P1.A confirma, mover `stemTracks.clear()` a un thread off-message o destruir `StemTrack` antes de pausar el `TimeSliceThread` | Warm: 5-9s → 1-2s | M | Medio | P1.A |
| **P1.C** | Thread-pool para `copyZipEntry` en `ProjectStore::open` | Cold: 7-30s → 1.5-4s | M (1-2 días) | Bajo | Nada |
| **P2.A** | Fix `getCurrentRssKb()` con `task_info(TASK_VM_INFO)` en macOS y `/proc/self/statm` en Linux | Calidad de telemetría | XS (1-2 h) | Ninguno | Nada |
| **P2.B** | Fix `wall_ms` precision en CSV (epoch ms int64 o `.precision(15)`) | Calidad de telemetría | XS | Ninguno | Nada |
| **P2.C** | Cambiar `StemCache` key de `songTitle` a `songId` | Estabilidad + evita colisiones | S | Bajo (necesita migración one-shot) | Nada |
| **P2.D** | En deleteSong, llamar `ProjectStore::flushAllPendingSettings(file)` antes de `discardLocalState` | Previene un edge case raro | XS | Ninguno | Nada |
| **P3.A** | Cleanup de `cloud-sync-staging/<id>/` después de cada upload (éxito o fallo) | Previene leak pequeño | XS | Ninguno | Nada |
| **P3.B** | Cachear `ProjectStore::open` por `(path, mtime, size)` para evitar re-leer el ZIP en warm-after-cache-evict | Warm-cold marginal | S | Bajo (invalidación explícita) | Nada |

### Justificación de P0.A (la más barata y de mayor ROI inmediato)

- Es responsable de TODO el cruft del `decoded/`.
- El fix son ~10 líneas: agregar `StemCache::removeSongCache(songTitle)`, llamarlo en `deleteSong` y en `finalizeLegacyProjectMigration` antes del `decoded/<title>/deleteRecursively()`.
- Riesgo cero: el directorio solo se usa como cache, regenerable.
- Recupera espacio tangible de inmediato (193 MB en este sistema, podría ser >1 GB en una biblioteca más grande).

### Justificación de P0.B (reconciliación al startup)

- Cubre TODOS los demás directorios huérfanos (cloud-content, cloud-artwork, cloud-sync-staging).
- Es idempotente y seguro (no toca archivos referenciados por `songs.json`).
- Loguea lo que borró para auditoría.
- Dry-run flag (`--purge-orphans` o env var) para revisar antes de aplicar.

---

## 6. Lo que NO recomiendo tocar

- **El bin-packing LPT de workers** — estable, p50 medido consistente.
- **El formato FLAC del cache** — el decode speed es el factor, no el tamaño.
- **Aumentar `kMinPrefetchBufferSamples`** — ya intentado en 2026-07-26, empeoró.
- **`settings.restore` batching** — ya está en 470 ms, no es el cuello dominante.
- **El timeout de `waitForNextAudioBlockReady`** — funciona bien, el cambio es marginal.
- **El flujo de `migration-staging`** — se limpia correctamente en éxito y rollback.

---

## 7. Anexo: scripts de diagnóstico

### 7.1. Cuantificar orphans actuales

```bash
python3 - <<'PY'
import json, os, sys

ROOT = os.path.expanduser("~/Library/MTControls")
with open(os.path.join(ROOT, "songs.json")) as f:
    data = json.load(f)

def scan(dirs):
    ref = set()
    for s in data.get("songs", []):
        for stem in s.get("stems", []):
            for k in ("storedPath", "sourcePath"):
                p = stem.get(k, "")
                if p and any(d in p for d in dirs):
                    ref.add(os.path.basename(p))
        for k in ("artworkPath",):
            p = s.get(k, "")
            if p and any(d in p for d in dirs):
                ref.add(os.path.basename(p))
    return ref

# decoded: not in paths; we need to compare dir name against title.
referenced_titles = {s.get("title", "").strip() for s in data.get("songs", [])}
import unicodedata
def norm(t):
    return "".join(c for c in unicodedata.normalize("NFD", t) if unicodedata.category(c) != "Mn").lower()

decoded_root = os.path.join(ROOT, "decoded")
if os.path.isdir(decoded_root):
    print("decoded/ orphan check:")
    for entry in sorted(os.listdir(decoded_root)):
        # stem cache uses sanitizeCacheKey(title); approximate match
        norm_entry = norm(entry).replace("_", " ")
        is_ref = any(norm_entry in norm(t) or norm(t) in norm_entry for t in referenced_titles)
        size = 0
        for r, _, files in os.walk(os.path.join(decoded_root, entry)):
            for f in files:
                try: size += os.path.getsize(os.path.join(r, f))
                except: pass
        marker = "" if is_ref else "  <-- ORPHAN"
        print(f"  {entry:40s}  {size//1024//1024:>4} MB{marker}")

# cloud-content/objects
ref = scan(["cloud-content/objects/"])
p = os.path.join(ROOT, "cloud-content", "objects")
if os.path.isdir(p):
    files = set(os.listdir(p))
    orphan = sorted(files - ref)
    total = sum(os.path.getsize(os.path.join(p, f)) for f in orphan if os.path.isfile(os.path.join(p, f)))
    print(f"\ncloud-content/objects/  {len(ref)} referenced, {len(files)} on disk, {len(orphan)} orphan ({total//1024//1024} MB)")
    for f in orphan: print(f"  {f}")

# cloud-artwork
ref = scan(["cloud-artwork/"])
p = os.path.join(ROOT, "cloud-artwork")
if os.path.isdir(p):
    files = set(os.listdir(p))
    orphan = sorted(files - ref)
    print(f"\ncloud-artwork/  {len(ref)} referenced, {len(files)} on disk, {len(orphan)} orphan")
    for f in orphan: print(f"  {f}")

# cloud-sync-staging: check if dir corresponds to a songId in songs.json or projects.json
ref_projects = set()
for s in data.get("songs", []):
    if s.get("projectId"): ref_projects.add(s["projectId"])
    if s.get("id"): ref_projects.add(s["id"])
sync = os.path.join(ROOT, "cloud-sync-staging")
if os.path.isdir(sync):
    print(f"\ncloud-sync-staging/  referenced={len(ref_projects)}")
    for entry in sorted(os.listdir(sync)):
        marker = "" if entry in ref_projects else "  <-- ORPHAN"
        print(f"  {entry}{marker}")

# projects: .pwproj
proj_dir = os.path.join(ROOT, "projects")
if os.path.isdir(proj_dir):
    files = set(os.listdir(proj_dir))
    # We can't link these to songs.json directly; just list.
    print(f"\nprojects/  {len(files)} files")
    for f in sorted(files): print(f"  {f}")
PY
```

### 7.2. Reproducir el análisis

El script vive en este mismo directorio (`analyze_bloat.py`). Re-ejecutable en cualquier momento para detectar drift.

---

## 8. Próximos pasos sugeridos

1. **Inmediato (1-2 horas):** implementar P0.A (`StemCache::removeSongCache` + llamadores). Riesgo cero, recupera espacio tangible.
2. **Mismo día:** P0.B (reconciliación al startup con dry-run). Riesgo bajo.
3. **Mismo día:** P2.A y P2.B (fixes de probe). Riesgo cero, mejora la calidad de telemetría para los siguientes análisis.
4. **Siguiente sesión:** P1.A (build + experimento de warm-swap con `pauseMaintenance`/`swap.lockRegion`). 30 min, da datos firmes para priorizar P1.B.
5. **Backlog:** P1.C (parallel copyZipEntry), P2.C (StemCache por songId), P3.A/B.
