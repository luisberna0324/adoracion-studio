---
type: Audio System
title: StemCache
description: Cache on-disk de stems decodificados a FLAC, evita el costo de AAC en el hot path
resource: src/audio/StemCache.h
tags: [juce, audio, cache, flac, performance, song-load, storage]
timestamp: 2026-09-12T17:45:00Z
---

> **Layout change (2026-09-12):** the cache directory is now keyed on
> `songId` (UUID), not `songTitle`. See
> `perf/analysis_20260912/INFORME.md §4.1` for the rationale. The
> one-shot `migrateTitleKeyedDirectoriesToIds` runs at startup to
> preserve the existing cache.

# StemCache

`StemCache` resuelve el problema más caro del pipeline de stems: el decode AAC/WAV en `getNextAudioBlock()`. La cache precomputa una copia lossless FLAC de cada stem la primera vez que se carga, y a partir de ahí el audio thread lee el FLAC (5-10x más rápido de decodificar que AAC en todas las plataformas que JUCE soporta).

Es **el sistema que más impacto tuvo** en el rendimiento del load path: sin cache válida, cada stem AAC paga 1.2-2.9 ms por bloque de audio, lo que excede el budget de 11.6 ms con 19 stems cargados.

## Convenciones de uso

- **Nombre canónico**: `StemCache` (matches la clase, el header, y este doc).
- **Alias informales**: "el cache FLAC", "el cache de stems", "el precompute".
- **Activación**: `precomputeAsync()` se llama desde `loadSongStems()` automáticamente cuando `isValid()` retorna `false`. No requiere setup manual.

---

## Qué hace

`StemCache` mantiene un directorio `decoded/<songId>/` donde cada stem se almacena como un par de archivos:

```
decoded/
└── <sanitized songId>/
    ├── stem_<hash-of-source-path>.flac
    └── stem_<hash-of-source-path>.flac.meta.txt   ← sidecar con mtime + size
```

El lookup es por `(sourceFile, songId)` y valida tres cosas:
1. El `.flac` existe
2. El sidecar `.flac.meta.txt` existe y parsea limpio
3. El `source_mtime_ms` y `source_size` del sidecar matchean el source actual

Si cualquiera falla, `isValid()` retorna `false` y el caller usa el source original (degraded but functional). En paralelo, `precomputeAsync()` re-encoda el source a FLAC para que la próxima carga sea rápida.

---

## API pública

| Función | Propósito |
|---|---|
| `getCacheDirectory(songId)` | Devuelve `decoded/<sanitized-songId>/`, la crea si no existe |
| `getCacheFile(sourceFile, songId)` | Path completo del FLAC: `decoded/<sanitized-songId>/stem_<hash>.flac` |
| `isValid(sourceFile, songId)` | true si cache existe y sidecar matchea source mtime/size |
| `precompute(sourceFile, songId)` | Síncrono: encode source→FLAC, throws on error |
| `precomputeAsync(sourceFile, songId, onComplete)` | Background thread; `onComplete` se llama en el message thread |
| `sanitizeCacheKey(rawSongId)` | Reemplaza `/`, `:`, `*`, `?`, `"`, `<`, `>`, `\|` con `_`; trims leading/trailing whitespace y dots |
| `migrateLegacyCacheDirectories()` | One-shot al startup; renombra directorios legacy al nuevo formato |

---

## Por qué FLAC

La elección de FLAC no es accidental:

| Atributo | FLAC | AAC nativo | PCM WAV |
|---|---|---|---|
| Decode speed (JUCE benchmark) | **5-10x más rápido que AAC** | Baseline | Más rápido que AAC pero ~2x tamaño |
| Calidad | Lossless | Lossy | Lossless |
| Tamaño relativo | ~50-60% del PCM | ~10-15% del PCM | 100% del PCM |
| Soporte JUCE | `juce_audio_formats` (libFLAC bundleado) | `juce_audio_formats` (AVFoundation/CoreAudio) | Nativo |
| Compatibilidad con time-stretch (Bungee) | Buena (decoded PCM) | Buena (decoded PCM) | Nativa |

La penalización en tamaño (~430 MB de M4A → ~250 MB de FLAC) es aceptable porque el cache se beneficia cada vez que la canción se carga. El ahorro en CPU es permanente.

---

## El bug del split-path (legacy) y su fix

### Historia

Antes de la versión actual, el `songId` se usaba como nombre de directorio sin sanitizar. Títulos con caracteres que rompen paths en algún OS — el caso más común fue `/` en "Al Que Es Digno / Vencio" — generaban estructuras como:

```
decoded/
└── Al Que Es Digno /       ← trailing space (posix interpretó el / como separador)
    └──  Vencio/             ← leading space
        ├── stem_<hash>.flac
        └── stem_<hash>.flac.meta.txt
```

El `isValid()` buscaba en `decoded/Al Que Es Digno / Vencio/stem_<hash>.flac` (un solo path), que **no existía** porque el `/` se había interpretado como separador. La cache siempre daba miss y el usuario pagaba el costo AAC cada vez que cargaba esa canción.

### El fix: `sanitizeCacheKey` + `migrateLegacyCacheDirectories`

Dos partes:

**(1) `sanitizeCacheKey()`** — normaliza el `songId` antes de construir paths:
```cpp
static const juce::juce_wchar kBadChars[] = {'/', '\\', ':', '*', '?', '"', '<', '>', '|'};
for (juce::juce_wchar c : kBadChars) result = result.replaceCharacter(c, '_');
result = result.trim();
while (result.startsWithChar('.')) result = result.substring(1);
```

Para "Al Que Es Digno / Vencio" → "Al Que Es Digno _ Vencio". El cache lookup ahora busca en un único directorio cuyo nombre no contiene caracteres patológicos.

**(2) `migrateLegacyCacheDirectories()`** — one-shot al startup, corre antes del primer `loadSongStems()`. Tres etapas:

**Etapa 1 — Recuperación de espacios:** para cada directorio top-level bajo `decoded/`, si tiene un sub-directorio cuyo nombre empieza con espacio, restaura el espacio al final del nombre del padre. Esto revierte el daño de una migración parcial previa que ya hubiera perdido el espacio.

```
Al Que Es Digno/         →  Al Que Es Digno /
   ↑                                 ↑
   falta el trailing space           restaurado
```

**Etapa 2 — Walk recursivo:** post-order DFS sobre la jerarquía. Para cada directorio visitado, reconstruye el `chainName` uniendo los segmentos hasta `decoded/`:

```
parent = "Al Que Es Digno "  (con trailing space)
child  = " Vencio"           (con leading space)
chainName = "Al Que Es Digno / Vencio"
sanitized = "Al Que Es Digno _ Vencio"
target    = decoded/Al Que Es Digno _ Vencio
```

Mueve el directorio del child al target. La jerarquía queda plana.

**Etapa 3 — Limpieza:** cualquier sub-directorio top-level que haya quedado vacío (sus contenidos fueron migrados hacia arriba) se elimina.

**Idempotencia**: la migración puede correr múltiples veces sin efectos colaterales. Una vez que el cache está en el formato nuevo, las pasadas 1 y 2 no encuentran nada que migrar y la 3 no encuentra subdirs vacíos.

### Por qué fue importante

El primer caso documentado fue exactamente "Al Que Es Digno / Vencio" — ver `docs/okf/index.md` y el comentario en `StemCache.h:94-98`. Después del fix, esa canción carga en ~1.2 s con cache hit en lugar de pagar el costo AAC full en cada carga. La diferencia es audible para el operador del setlist (la app queda responsive durante el swap en vez de frozen).

---

## Sub-stages observados (SongLoadProbe)

El `StemCache` es instrumentado por `SongLoadProbe` para que se pueda ver hit/miss y razón en el CSV `song_load_*.csv`:

| Evento | Cuándo | Acción recomendada |
|---|---|---|
| `StemCache.isValid.hit` | El FLAC existe, sidecar matchea | Nada, óptimo |
| `miss.noFlac` | El FLAC no existe (cache vacía o split-path legacy) | Verificar que `migrateLegacyCacheDirectories()` corrió |
| `miss.noMeta` | FLAC existe pero el sidecar no | Re-precompute (forzar con `precomputeAsync`) |
| `miss.badMeta` | Sidecar sin `source_mtime_ms` o `source_size` | Mismo, el sidecar está corrupto |
| `miss.sizeMismatch` | El source fue re-encodado (mismo nombre, bytes distintos) | Re-precompute |
| `miss.mtimeMismatch` | El source fue editado (mtime cambió, bytes pueden o no cambiar) | Re-precompute |

Para el caso típico "Al Que Es Digno / Vencio" después del fix de migración, se observa un mix de `hit` y `miss.mtimeMismatch` — los stems precomputados hace semanas (m4a original sin tocar) hacen hit, los stems re-extraídos del .pwproj recientemente (mtime nuevo) hacen miss.

---

## Archivos del sistema

| Archivo | Rol |
|---|---|
| `src/audio/StemCache.h` | API pública + doc de la cache FLAC + nota del split-path bug |
| `src/audio/StemCache.cpp` | Implementación, sidecar parse, `migrateLegacyCacheDirectories` 3-stage |
| `src/PluginProcessor.cpp` | Llamador: `isValid()` en `loadSongStems` línea ~3380, `precomputeAsync()` on miss |
| `src/data/SongLibraryStore.cpp` | Define `getRootDirectory()` (donde vive `decoded/`) |

---

## Limitaciones conocidas

- **Cache no se invalida por cambio de contenido parcial**: el sidecar valida `mtime` y `size`. Si el usuario edita un M4A sin cambiar el tamaño (raro pero posible con re-encoding idéntico), el cache devuelve hit pero el contenido cambió. La mitigación es re-precompute manual.
- **Precompute bloquea el stem que está siendo encoded**: `precomputeAsync` corre en background pero el primer `loadSongStems` después de un miss todavía paga el AAC source. El usuario no ve bloqueo de UI, pero el audio thread puede hacer más trabajo durante los primeros 0.5-1 s.
- **Cache no se borra al desinstalar**: el directorio `decoded/` sobrevive a una desinstalación del app. Es deliberado (el FLAC precomputado se puede reutilizar si el usuario reinstala), pero acumula ~50-100 MB por canción.
- **Migración solo corre al startup del app**: si una canción se carga con songId nuevo entre startups, el `precomputeAsync` la crea. No hay mecanismo para pre-poblar el cache en background.
- **Sidecar basado en mtime**: si el filesystem tiene mtime resolution > 1 ms (algunos FAT32 o network mounts), dos edits en el mismo segundo pueden colisionar. En la práctica, el .pwproj siempre está en APFS.

---

## Diagnósticos encontrados (este benchmark reveló)

| Hallazgo | Métrica que lo expuso | Optimización resultante |
|---|---|---|
| "Al Que Es Digno / Vencio" carga lento por split-path cache miss | `StemCache.isValid.miss.noFlac` con `path=decoded/Al Que Es Digno / Vencio/...` | `migrateLegacyCacheDirectories` 3-stage |
| 8/12 stems con `miss.mtimeMismatch` después de extraer stems nuevos del .pwproj | `miss.mtimeMismatch` count vs `hit` count | Pendiente: trigger re-precompute cuando el miss es mtime-only |

---

## Cómo extender

Para añadir un nuevo evento al probe (cache hit/miss reasons se registran via `recordEvent`):

```cpp
SongLoadProbe::instance().recordEvent(
    "StemCache.isValid", "miss.<reason>",
    "songId=" + songId + " us=" + juce::String(dt) +
    " <extra-detail-key>=<value>");
```

Para añadir un nuevo campo al sidecar:
1. Agregar `constexpr const char *kMetaXxxKey = "xxx"` en el namespace anónimo
2. Parsear en `isValid()` — la línea `if (line.startsWith(kMetaXxxKey))` está lista para extenderse
3. Si el campo es requerido para `isValid()` retornar true, agregarlo a la validación `recordedXxx < 0`
4. Setear en `precompute()` después de computar el valor

Para modificar la migración:
- `migrateLegacyCacheDirectories()` está estructurada en 3 etapas documentadas arriba. Si se necesita un nuevo patrón de split-path, agregar un detector en la etapa 1 antes del walk de la etapa 2.
