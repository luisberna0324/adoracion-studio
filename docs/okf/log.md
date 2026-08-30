---
type: Change Log
title: OKF Knowledge Base Changelog
description: Registro de cambios en la base de conocimiento OKF
timestamp: 2026-08-29T19:40:00Z
---

# Changelog

## 2026-08-29

### Mixer UI: Channel Strip con layout estable e instrumentación VU opt-in

`ChannelStripCard::resized()` ahora usa `juce::FlexBox` para repartir el bloque
fader + medidor y la fila de solo/mute, manteniendo los tamaños públicos de
`ChannelStripCard::Layout`. El flag existente `setMuteAlignedToFader(true)`
queda aplicado cuando el canal muestra sólo mute, útil para master y guías que
no exponen solo.

`StereoVUMeter` ahora evita repintar por variaciones flotantes que no cambian
ningún LED visible: el timer sólo pide `repaint()` cuando cambia el número de
segmentos activos o el LED de peak. Para medir la actividad sin coste en builds
normales, CMake suma la opción apagada por defecto
`MTCONTROLS_ENABLE_VU_REPAINT_DIAGNOSTICS`; al activarla se loguean contadores
agregados de ticks, repaints pedidos, ticks omitidos y paints cada dos segundos.

### TopBar: layout por grupos y refresco visual incremental

`TopBar::resized()` ahora ordena el transporte y los controles derechos como
grupos `juce::FlexBox`, conservando la prioridad existente: en anchos reducidos
se ocultan primero el texto de BPM y luego CPU/MEM para reservar espacio a la
tarjeta de canción.

El refresco periódico evita reasignar texto, iconos, badges y bounds cuando no
cambian. `refreshFromProviders()` ya no repinta todo el TopBar en cada tick:
la tarjeta central sólo invalida su área cuando cambian título, artista, artwork
o acento. El transporte, metrónomo, fade y labels siguen actualizándose desde
los mismos providers y callbacks.

La tarjeta de canción ahora pinta una superficie propia con franja de acento y
artwork recortado, para separar mejor metadata del fondo del header. Play queda
como único botón primario; al reproducir se marca activo, Stop se marca cuando
la posición vuelve al inicio y Fade muestra badge mientras está activo.

El popover de tempo/compás mantiene las mismas acciones, pero separa `TAP`,
compás y BPM en filas más legibles. En MIDI Learn, el TopBar ahora resalta y
arma targets de ventana, settings, popover de canción, popover de tono y popover
de metrónomo, además de los controles de transporte ya existentes. Los botones
iconográficos muestran badges `CC`/`N` cuando existe una asignación.

### TopBar: jerarquía visual compacta

La pasada visual posterior conserva los mismos callbacks y reglas responsive,
pero reduce el peso del header: los controles estándar pasan a una superficie
más discreta, Play continúa como único botón de acento pleno y el selector de
tempo usa una lectura oscura con una franja de acento en lugar de un bloque
completo coloreado. La tarjeta de canción baja de altura, reduce la tintura del
acento y limita su ancho para dejar respiración entre los grupos. CPU/MEM queda
con menor contraste para funcionar como telemetría periférica.

### TopBar: botones de control inspirados en referencia oscura

Los botones de transporte y acciones adoptan una superficie oscura en degradado
vertical, un borde único de bajo contraste y radio medio. El botón primario
mantiene un gradiente verde, borde luminoso y halo corto para preservar su
jerarquía. BPM usa la misma superficie neutra y reserva el color de acento para
su valor, evitando competir con el control principal.

### TopBar: tarjeta de canción contenida

La tarjeta de canción evita ocupar el ancho libre completo: su ancho preferido
se calcula desde el texto visible, artwork y margen breve, con un crecimiento
acotado cercano al 30% y límites por perfil responsive. Su fondo usa ahora una
superficie neutra más oscura y borde fino, sin franja lateral ni borde de
progreso; el color específico de la canción queda reservado para el artwork o su
placeholder.

### Biblioteca: filas de setlist y canciones más densas

Las filas principales de setlists, canciones del setlist y biblioteca reducen
altura, artwork, sombra y radios para alinearse con la barra superior más
sobria. La barra vertical de estado se conserva como indicador rápido de
canción/selección, pero con menor grosor. El hover de las listas de canciones
ahora invalida sólo la fila anterior y la nueva en lugar de repintar el listado
completo.

### Biblioteca: header y estados visuales más sobrios

El header de canciones reduce la altura reservada para Biblioteca y métricas,
dejando más espacio efectivo al setlist. Los controles de header adoptan una
superficie oscura con borde fino; los chips de filtro quedan preparados con
estilo más compacto/segmentado. Las filas conservan su barra vertical, pero el
color queda reservado por estado: acento para reproducción, amarillo para
selección pendiente/siguiente y color propio sólo como pista secundaria. Los
estados vacíos agregan un icono tenue y texto compacto.

### Biblioteca: acciones primarias visibles y menú secundario

La vista Biblioteca mantiene `Cargar` y `Crear` como acciones primarias visibles
para mejorar descubrimiento en iPad/iPhone. El menú de tres puntos queda
reservado para opciones secundarias como ordenamiento y limpieza de selección,
evitando duplicar acciones principales. La fila de acciones limita su ancho en
pantallas amplias y mantiene altura táctil consistente en paneles compactos.

### iOS: firma de Xcode y assets del bundle

Los targets iOS usan deployment target 18.5 para coincidir con el mínimo del
`bungee.framework` incluido, sin propagar ese valor a las herramientas host de
JUCE. La generación CMake pasa el icono como ruta absoluta para que `ZERO_CHECK`
de Xcode no dependa del directorio de trabajo. En iOS, los targets auxiliares de
librería no se firman. El standalone usa launch storyboard de JUCE en lugar de
`LaunchImage.launchimage`, evitando que `codesign` trate PNGs legacy del asset
catalog como subcomponentes no firmados. Los packs de guías y clicks se copian a
`installer/` directamente dentro del bundle plano de iOS; usar el directorio
macOS `Resources/` hace que `codesign` deje de reconocer el producto como una
aplicación iOS y no enlace su `Info.plist`. Los archivos de datos quedan sellados
como recursos por la firma final de Xcode, sin firma manual ni `--deep`. El
post-build completa `CFBundleIdentifier` en el `bungee.framework` copiado al
`.app` y vuelve a firmar sólo ese framework antes de la firma de la aplicación,
porque el SDK iOS incluido no declara ese campo.

### Repertorio: acción compacta y búsqueda integrada

La vista de Repertorio deja de usar **Crear setlist** como bloque ancho de
acento completo. La acción pasa al header como botón compacto, mientras la
búsqueda queda inmediatamente debajo con una superficie más integrada al panel.
La tarjeta de setlist activo mantiene su barra vertical, pero baja el tinte de
fondo y borde para evitar que el acento domine toda la vista.

### Setlist: acciones secundarias más discretas

En la vista de canciones, el acceso a Biblioteca queda centrado y limitado en
ancho para evitar una banda full-width. Las filas bajan levemente la tipografía
principal/secundaria y el menú de tres puntos pierde peso cuando no está en
hover. La fila para agregar canciones mantiene affordance de drop/action, pero
reduce altura visual, opacidad del borde punteado y tamaño del icono.

### Setlist: Biblioteca en el header

El acceso a Biblioteca se mueve al botón derecho del header de canciones,
reemplazando el menú de tres puntos verticales en esa vista. El botón usa el
icono SVG musical y conserva el texto **Biblioteca** para mantener claridad. El
panel de métricas oculta su acción central de Biblioteca y reduce la altura
reservada, dejando más espacio útil para la lista. Las métricas mantienen los
mismos callbacks y estados clicables.

Al quedar Biblioteca completamente abierta, el header y listado del setlist se
ocultan en lugar de permanecer desplazados fuera del borde izquierdo. Esto evita
que sombras o bordes redondeados de la vista anterior sobresalgan dentro de la
subvista de biblioteca; durante swipe/animación siguen visibles para conservar
la transición.

## 2026-08-19

### Standalone: versión visible en título de ventana

La app standalone mantiene `JucePlugin_Name` como nombre interno, pero el título
de la ventana ahora usa `PlayWorship v<JucePlugin_VersionString>`. La versión
sigue saliendo de `MTCONTROLS_APP_VERSION` en CMake; el fallback local actual es
`1.1.4`, y GitHub Actions lo sobrescribe con el semver calculado del release.

### Import wizard: sugerencias no bloquean metadata manual

En creación/edición de canción, el autocomplete del título ya no fuerza elegir
una sugerencia. Al perder foco, presionar Enter/Escape o avanzar de metadata, se
cierran las sugerencias pendientes sin borrar el título escrito manualmente.

### Biblioteca: guardado rápido de metadata/artwork en edición

El wizard de edición de canciones ahora muestra **Guardar** en el primer paso
de metadata. Ese camino llama `SongLibraryStore::updateSongMetadata` y preserva
stems, arrangement, setlists y waveform cache; sólo actualiza metadata e imagen
en la biblioteca. El flujo completo de **Guardar cambios** sigue disponible en
el paso de revisión para ediciones que modifican stems o mapeos.

### Mixer: persistencia completa de controles en proyectos

El panel de canales ahora observa periódicamente el estado real del processor y
persiste un `SongMixSnapshot` completo cuando cambia master o cualquier stem.
Esto cubre volumen, gain, mute, solo y canal de salida de un número variable de
stems, incluso cuando el cambio no viene de `onDragEnded`/`onClick` sino de MIDI
o comandos programáticos. Para canciones `.pwproj`, el camino sigue pasando por
`SongLibraryStore::updateActiveSetlistItem`, que redirige al sidecar del proyecto
con `ProjectStore::scheduleSettingsSave`; para canciones legacy mantiene el
setlist activo como antes.

### Routing: legacy songs ahora también aplican los defaults por categoría

La lambda `loadSong` en `PluginEditor.cpp` arrancaba con un
`if (!isProjectBackedSong(song)) return false;` que salía antes de
llegar a `applyRoutingDefaultsToSong`. El comentario decía que las
canciones legacy debían pasar por un "explicit optimization flow"
(la conversión a `.pwproj`), pero en la práctica la rama legacy
nunca corría: el audio thread seguía tocando los stems anteriores
con el routing default (todos mono ch=1) hasta que el usuario
tocaba el panel.

Cambios:

- Se sacó el early-return. El bloque de apertura de `.pwproj` (que
  abre el ZIP, extrae stems, y carga el manifest con los settings)
  queda envuelto en `if (isProjectSong)`. El resto del lambda es
  compartido y ya funcionaba para legacy porque el `settings.restore`
  cae a `SongLibraryStore` cuando no hay `projectSettings`.
- Con esto, una canción legacy ahora también recibe el apply del
  routing panel y deja de sonar "todo en mono ch=1" hasta que el
  usuario toque el panel de ruteo.

## 2026-08-18

### Routing: per-stem width now applied on project load

Antes, `applyRoutingDefaultsToSong` se saltaba en el lazo de
`loadSong` para canciones `.pwproj` (`if (!hasProjectSettings)`). El
mix snapshot del proyecto solo guarda `outputChannel` por stem
(`StemMixSnapshot` no tiene `outputWidth`), así que el ancho de cada
stem quedaba en el default de `resetStemControls` (1 = mono). La
audible era "todo suena por L" hasta que el usuario tocaba el panel
de ruteo, que disparaba el apply completo y reseteaba todo a estéreo.

Cambios:

- `applyRoutingDefaultsToSong` se llama siempre, también para
  proyectos. El proyecto sigue siendo la fuente de verdad para el
  canal base; el ancho lo conduce el default de la categoría.
- `refreshPadsRoutingFromSettings` ya no se invoca al final de
  `applyRoutingDefaultsToSong`: el call site es responsable. Antes se
  ejecutaba 2 veces por click en el panel (1× en el callback del
  `onChanged`, 1× al final del apply). Ahora se ejecuta 1 vez.
- Marcadores en `SongLoadProbe` con phase `routing`:
  `loadSong.gate`, `apply.enter`, `apply.stem` (prevW/newW/prevC/
  newC por stem), `apply.exit`, `panel.onChanged`, `device.change`.
  Sirven para confirmar el gate (isProject=0 vs 1) y para ver en
  el CSV por qué tocar el panel re-aplica la tabla completa.

## 2026-08-17

### Performance: setlist swap cold 8-18x, warm 3-5x, sin cold repetido

El swap entre dos proyectos del setlist tardaba 5-30s en cold
(extract secuencial del ZIP) y 700-1000ms en warm. Con los fixes
aplicados baja a 1-2s cold y 180-300ms warm. También desaparece
el patrón de "cold repetido" donde volver a una canción recién
cargada forzaba otro re-extract completo.

Cambios principales:

- `ProjectStore::open` extrae los stems del .pwproj en paralelo
  con 4 workers (`std::thread` + `juce::ZipFile` propio por
  worker). El extract secuencial corría a 16-43 MB/s sobre SSD;
  en paralelo satura el ancho de banda y baja el cold de 19-26s
  a 1-2s para proyectos de 500-800MB.
- Se quita el `pumpUi()` del callback de `onLoadProgress` en
  `PluginEditor::loadSong`. Cada `pumpUi()` forzaba un
  `runDispatchLoopUntil(1)` que procesaba mensajes UI pendientes.
  Con 38 llamadas por load, dominaba el wall-clock de los warm
  swaps (400-600ms del "other_in_lss" en el probe, todo
  pumpUi). El redraw queda para el próximo vsync.
- `ProjectStore::updateSettings` ya no re-packea el .pwproj ZIP.
  Solo persiste al sidecar (`project-cache/<id>/song-settings.json`).
  `ProjectStore::open` ya prefiere el sidecar cuando su mtime es
  más nuevo, así que el efecto neto es idéntico. Antes, el
  re-pack cambiaba el mtime del .pwproj y `cacheMatchesProject()`
  invalidaba el cache, forzando un re-extract de todos los stems
  en la siguiente carga.
- El mismo skip de re-pack se aplica en `scheduleSettingsSave`,
  `settingsSaveQueue::schedule` interno,
  `ProjectSettingsSaveJob::runJob` y `writeNow`: si los
  settings del sidecar son byte-idénticos a los nuevos, no se
  re-packea nada.
- `updateWaveform` del callback async queda deshabilitado: el
  waveform regenerado tenía variación de 1-ULP respecto al
  embebido (orden de operaciones FP no determinístico), y la
  comparación bit-exact nunca saltaba. El sidecar
  `song-waveform-cache` ya tenía el waveform actual; re-embeberlo
  en el .pwproj no aporta.
- Nuevos sub-stages en `SongLoadProbe` para visibilidad:
  `createReaderFor` se separa en `.main` y `.prefetch`;
  `project.open` instrumenta `isDecodableFlac` per-stem;
  `processor.loadSongStems` mide `pauseMaintenance`,
  `swap.lockRegion` y `progress.{0.86,0.90,0.98,1.0}` por
  separado.
- Bug del RSS del probe: `getCurrentRssKb` usaba
  `getrusage().ru_maxrss` (pico histórico monotónico, no RSS
  actual). 6 de 10 CSVs tenían un único valor de RSS en todas
  sus filas. Documentado como limitación conocida, no fixado
  en este commit (el fix usa `task_info(TASK_VM_INFO,
  phys_footprint)` en macOS o `/proc/self/statm` en Linux).
- `CMakeLists.txt`: `target_include_directories(MTControls
  PRIVATE src)` para que `src/data/*.cpp` pueda incluir
  `SongLoadProbe.h` directamente.
- Informe y script reproducible en
  `perf/analysis_20260817/INFORME.md` y
  `perf/analysis_20260817/analyze_setlist_load.py`.

Speedup medido (2026-08-17):

| Métrica | Antes | Después |
|---|---|---|
| Cold 800MB project | 19.7s | 1.0-2.0s (8-18x) |
| Warm swap | 700-1000ms | 180-300ms (3-5x) |
| Cold repetido (mismo proyecto 2da vez) | 1-2s | 180-240ms (warm) |
| Audio gaps durante load | 70-406ms | eliminados |

Causas raíz identificadas (ya no aplican):

- `copyZipEntry` single-threaded → cuello del cold.
- `pumpUi` por stem → cuello del warm.
- `updateSettings` re-paqueando el .pwproj → causa del cold
  repetido.
- `updateWaveform` bit-exact compare → re-pack por 1-ULP.

## 2026-08-16

### Política FLAC y migración visible

- El almacenamiento de stems legacy usa FLAC en todas las plataformas; los
  imports nuevos aceptan WAV, AIFF y FLAC como formatos lossless de entrada.
- Los `.pwproj` se validan estrictamente como FLAC. Se retiraron la conversión
  M4A específica de Windows y el fallback de carga hacia `sourcePath` para un
  M4A existente.
- `loadSongStems()` reproduce directamente un source FLAC y no genera una
  segunda copia FLAC en `StemCache`.
- `setDynamicGuideSettings()` reutiliza el pack ya cargado. La restauración de
  una canción deja de suspender audio y decodificar nuevamente todas las guías;
  sólo el primer uso o un cambio real de idioma recarga assets.
- Esto corrige el diagnóstico inicial del `settings.restore` de 408 ms: el
  costo dominante era la recarga redundante del pack de guía, no los stores
  atómicos del mixer. El batching queda como mejora separada, no como causa raíz.
- Las canciones legacy del setlist se muestran atenuadas y exponen
  **Optimizar**. Confirmar pregunta dónde guardar el proyecto; la opción
  **Biblioteca local** usa el directorio `projects` de la biblioteca de la app.
  Luego lo valida, preserva setlists/ajustes/transiciones y elimina el audio
  anterior mediante staging transaccional.
- Las rutas de activación desde setlist y Biblioteca bloquean la carga legacy;
  `PluginEditor` conserva un guard adicional para callers futuros.
- Los pools de click y guía aceptan WAV/AIFF/FLAC/MP3, sin ALAC/M4A. El export
  de mezcla conserva WAV, MP3 y M4A porque es un formato de salida al usuario.
- Se confirmó que no existe un bug de anuncios de sección; no se modificaron
  `DynamicGuideTiming` ni `SectionScheduler`.

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
