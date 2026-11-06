---
type: Audio Processor Class
title: MTControlsAudioProcessor
description: Procesador principal del plugin - JUCE AudioProcessor
resource: src/PluginProcessor.h
tags: [juce, processor, audio, plugin, main]
timestamp: 2026-10-17T00:00:00Z
---

# MTControlsAudioProcessor

Clase principal que extiende `juce::AudioProcessor`. Gestiona toda la pipeline de audio incluyendo stems, mezcla, metrónomo, pads y guías dinámicas.

En escritorio también posee el cliente local FreeShow y el scheduler de marcas
independientes de letras. `setSongLyrics`, `getSongLyrics` y
`getFreeShowClient` pertenecen a la capa de control. Audio sólo evalúa tablas
numéricas acotadas sobre segmentos efectivos y publica eventos; el dispatcher
y el worker realizan toda comunicación externa. Véase [FreeShow](../integrations/FreeShow.md).

> **Performance**: la captura de telemetría de performance de `processBlock` (stages, gaps, decode, render) está documentada en [`AudioDiagnostics`](AudioDiagnostics.md). Toda métrica de rendimiento del plugin pasa por ese módulo.

## Herencia

```
juce::AudioProcessor
└── MTControlsAudioProcessor
```

## Constantes

| Constante | Valor | Descripción |
|-----------|-------|-------------|
| `maxStemCount` | 64 | Máximo número de stems soportados |
| `maxRealtimeBlockSize` | 8192 | Tamaño máximo de bloque |
| `inputGainId` | "inputGain" | ID del parámetro de gain de entrada |
| `mixId` | "mix" | ID del parámetro de mezcla |
| `bypassId` | "bypass" | ID del bypass |

## Songs API

### Cargar canción

```cpp
bool loadSongStems(
    const juce::StringArray &stemPaths,
    const juce::String &songTitle,
    const juce::String &songKey,
    double songBpm,
    const std::function<void(double progress01)> &onLoadProgress = {},
    bool deferTransitionFadeInUntilConfigured = false
);

void finishPendingSongTransition();
```

Carga múltiples archivos de audio como stems separados. El `onLoadProgress` callback recibe valores de 0.0 a 1.0.

**Parámetros:**
- `stemPaths`: rutas runtime ya validadas. El flujo de producto entrega FLAC;
  callers de compatibilidad todavía pueden usar otros formatos decodificables.
- `songTitle`: Título de la canción
- `songKey`: Tonalidad (ej: "C", "Am", "F#m")
- `songBpm`: BPM original de la canción
- `onLoadProgress`: Callback opcional para progress UI
- `deferTransitionFadeInUntilConfigured`: Mantiene la canción nueva en gain 0
  hasta llamar `finishPendingSongTransition()`

**Retorna:** `true` si la carga fue exitosa

### Proyectos portables `.pwproj`

Una canción puede provenir de un proyecto `.pwproj` autocontenido. El paquete
incluye el manifiesto, stems FLAC y portada; antes de llamar `loadSongStems()` la UI
lo valida y extrae sus stems a una caché local. El procesador continúa
recibiendo paths de archivos normales y no abre ZIPs ni realiza I/O de
empaquetado en el audio thread. El manifiesto restaura mezcla, pitch, tempo,
ventana, arrange, guía dinámica y click antes de completar la transición.
FLAC es obligatorio para todo proyecto y para el almacenamiento interno en
todas las plataformas. Un stem que ya es FLAC se empaqueta directamente; los demás se
convierten una sola vez durante la creación del proyecto. El manifiesto agrega
`audioFormat: "flac"` sin cambiar `schemaVersion`, por lo que los lectores
actuales que ignoran campos adicionales siguen siendo compatibles.

`ProjectStore::open()` exige extensión FLAC y un `AudioFormatReader` válido para
cada stem extraído. No existe conversión ni fallback durante la apertura: un
paquete antiguo con ALAC/M4A debe recrearse mediante el flujo explícito de
optimización/migración. La caché
guarda además `project-source.json` con ruta, tamaño y modificación del
`.pwproj`: una segunda apertura reutiliza los recursos si ese marcador sigue
vigente, y reconstruye la caché si el paquete cambió o falta algún archivo.
La previsualización de waveform viaja como `waveform.bin` dentro del paquete;
la copia local en `MTControls/waveforms` es sólo una caché regenerable.
Al generar una onda, `buildMixedWaveformBins` omite las muestras de clic y guía
(categorías Click/Cues y nombres reconocidos como clic/guide/guia/guía). Conserva
la duración total para alinear la visual con el transporte. Esto no modifica los
stems reproducidos ni reescribe automáticamente las ondas guardadas previamente.
Los cambios de controles de canción (volumen, gain, mute/solo, salidas, pitch,
tempo, ventana, arrange, guía y click) se agrupan durante 750 ms y se escriben
fuera del hilo de audio mediante un reemplazo atómico del paquete. Al cambiar o
cerrar un proyecto, el cambio pendiente se termina de escribir antes de soltarlo.
La escritura del ZIP ocurre en `<proyecto>.tmp`; tanto el stream de progreso
como el `FileOutputStream` se destruyen antes de llamar `replaceFileIn()`. Esto
es obligatorio en Windows, donde el handle de escritura de JUCE no comparte
permiso de borrado o renombrado. Antes de escribir se elimina cualquier temporal
obsoleto y el reemplazo se reintenta brevemente para tolerar locks transitorios
de antivirus o proveedores de sincronización. Si aun así falla, el proyecto
anterior permanece intacto, el temporal se limpia y el log registra ambas rutas.
`ProjectStore` conserva el snapshot completo más reciente en memoria y en
`project-cache/<project-id>/song-settings.json`, por lo que cambios consecutivos
se acumulan aun cuando la canción no pertenezca al setlist activo o el `.pwproj`
haya sido movido. El setlist sólo aporta referencia y orden; no es la fuente de
ajustes de un proyecto. Si el paquete no está disponible, la caché local sigue
restaurando los controles y el ZIP queda pendiente hasta relocalizarlo.

El origen de ajustes se decide exclusivamente por la referencia explícita
`projectPath` de la entrada cargada. Una canción legacy con `projectPath` vacío
no se convierte implícitamente en proyecto por coincidir su ID con la caché o
con el índice de recientes: conserva pitch, tempo y mezcla del setlist. La
Biblioteca mantiene además `projectId` (identidad del manifiesto) separado de
`id` (identidad local de la entrada), de modo que un `.pwproj` exportado desde
una canción legacy puede coexistir con ella. La UI compara ID y origen al
detectar un cambio de canción; así reinicializa la tonalidad restaurada incluso
cuando ambas variantes comparten el mismo ID histórico.

### Optimización de canciones legacy

Las canciones legacy aparecen atenuadas en el setlist y exponen la acción
**Optimizar**. Activar la fila también abre el mismo diálogo y nunca carga el
audio legacy. Tras confirmar, el proyecto se guarda automáticamente en
`MTControls/projects`; el copy de UI no expone el formato interno. La acción
**Convertir a proyecto...** de Biblioteca conserva además la alternativa de
elegir otra ubicación.

Ambas rutas obtienen audio, portada, metadatos y waveform del almacenamiento
interno, toman mezcla, pitch, tempo, ventana, arrange, guía y click del setlist
activo (o del primer setlist que contenga la canción) y construye un `.pwproj`.
El paquete se vuelve a abrir y validar antes de modificar la Biblioteca.

Tras la validación, la entrada conserva su ID local y todos los `SetlistItem`
conservan `itemId`, orden y `transitionToNext`; únicamente la canción cambia su
origen a `projectPath`. La carpeta `MTControls/audio/<song-id>` se mueve primero
a staging, `songs.json` se reemplaza y sólo entonces se elimina el staging. Si
no se puede guardar la Biblioteca, la carpeta vuelve a su ubicación original y
el paquete recién creado se retira. No se permite elegir como destino una ruta
dentro de la carpeta legacy. Una canción cargada debe soltarse antes de iniciar
la conversión para que Windows también pueda cerrar y eliminar sus archivos.

El progreso de esta migración representa trabajo real y es monotónico. La
generación de waveform ocupa el tramo inicial; la conversión de cada stem usa
`ffmpeg -progress pipe:1` y pondera `out_time_us` por la duración del stem; la
escritura del ZIP avanza según los bytes entregados al `OutputStream`; y la
validación avanza según los bytes extraídos nuevamente a caché. Las
notificaciones se limitan a intervalos cortos para no bloquear la UI, pero no
se simula avance mediante temporizadores. Por eso una demora del disco o del
encoder se refleja en la fase y porcentaje que realmente la está causando.

En escritorio, la aplicación Standalone es de instancia única y recibe rutas
`.pwproj` desde la asociación del sistema operativo. Un doble clic inicia
PlayWorship o lleva al frente la ventana existente, termina de persistir el
proyecto actual, detiene el transporte y entrega la ruta al mismo flujo validado
de apertura de la biblioteca. La canción queda cargada sin autoplay. Si la UI
de autenticación todavía está activa, la solicitud se conserva hasta que la
biblioteca esté disponible.

Si había una canción reproduciéndose, la fuente anterior continúa durante la
preparación. El intercambio usa fade-out de 100 ms, reemplazo en silencio y
fade-in de 150 ms. Antes del intercambio se verifica que el primer bloque de
todos los stems esté disponible en read-ahead.

Los procesadores de tono/tempo por routing se preparan sólo para los stems
cargados, no para los 64 slots máximos. Un único worker
`TimeProcessorMaintenance` reconstruye fuera del callback las voces Bungee
retiradas por seeks, loops y cambios estructurales.

El contrato común de `TimeStretchProcessor` incluye
`releaseResources() noexcept` para devolver completamente la memoria de un slot
que deja de utilizarse. Antes de llamarlo, el procesador principal debe detener
el callback y usar la barrera que pausa `TimeProcessorMaintenance`; la
operación destruye voces/backend y buffers, por lo que no es realtime-safe. El
slot queda no preparado y tanto el preflight como el servicio responden
`false` hasta un nuevo `prepare()`. Los parámetros lógicos de pitch, tempo y
tipo de contenido permanecen configurados.

Un seek u otra discontinuidad dura de timeline también limpia los delays de
compensación del click y de la guía dinámica, además de sus voces activas. De
este modo no se filtra audio virtual perteneciente a la posición anterior. Una
transición de arrange prearmada no usa ese flush: parte stems y buses virtuales
en el offset exacto del bloque. Los cambios de perfil de tempo que no mueven el
playhead reinician sólo el DSP de tono/tempo.

Si un reset llega antes de que el worker haya reconstruido algún spare, el
callback hace preflight de todos los procesadores necesarios. En routing
descarta el bus completo mientras falta uno; en mezcla agrupada
`StemMixEngine` aplica la misma regla. Stems, click y guía avanzan sin emitirse,
los delays invalidan su historia en O(1) y todos retoman juntos desde la
posición fuente actual. Los pads continúan porque no pertenecen al timeline de
la canción.

### Estado de canción

| Método | Retorno | Descripción |
|--------|---------|-------------|
| `hasLoadedSong()` | `bool` | Hay canción cargada |
| `getLoadedSongTitle()` | `juce::String` | Título de la canción |
| `getLoadedSongKey()` | `juce::String` | Tonalidad |
| `getLoadedSongBpm()` | `double` | BPM de la canción |
| `getSongLengthSeconds()` | `double` | Duración total |
| `getLoadedSongLengthSeconds()` | `double` | Duración del audio fuente, sin extensiones del arrange |
| `isSongEndCustomized()` | `bool` | El usuario fijó explícitamente un límite `Fin` |
| `getSongCurrentPositionSeconds()` | `double` | Posición actual |
| `getLoadedStemCount()` | `int` | Número de stems cargados |

## Stem Controls

Control individual de cada stem (índice 0 a maxStemCount-1).

### Volumen y Gain

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setStemVolume(int, float)` | volume: 0.0-1.0 | Volumen visual |
| `getStemVolume(int)` | — | Obtener volumen |
| `setStemGain(int, float)` | gain: lineal | Gain en dB linear |
| `getStemGain(int)` | — | Obtener gain |

### Mute/Solo

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setStemMute(int, bool)` | muted | Silenciar stem |
| `isStemMuted(int)` | — | ¿Está silenciado? |
| `setStemSolo(int, bool)` | soloed | Aislar stem |
| `isStemSoloed(int)` | — | ¿Está solo? |

### Nivel de salida

```cpp
float getStemLevel(int stemIndex) const noexcept;
```

Retorna el nivel actual del stem para metering (0.0-1.0).

### Enrutamiento de salida

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setStemOutputChannel(int, int)` | channel (1-indexed) | Canal de salida |
| `getStemOutputChannel(int)` | — | Canal actual |
| `setStemOutputWidth(int, int)` | width: 1=mono, 2=stereo | Ancho |
| `getStemOutputWidth(int)` | — | Ancho actual |

**Canales disponibles:**

```cpp
juce::StringArray getAvailableOutputChannelNames() const;
int getAvailableOutputChannelCount() const noexcept;
```

## Transport API

La superficie de control remoto standalone aplica los setters de transporte y
mezcla desde el message thread del servidor. El cliente recibe metadatos y
waveform por WebSocket; ningún callback de red toca `processBlock`. Ver
[RemoteControl](../integrations/RemoteControl.md).
En modo cliente, `setRemoteClientMode(true)` hace que `processBlock` borre el
buffer y salga antes de procesar MIDI, stems, click, guía o pads; el editor
restaura ese indicador al salir del modo o al destruirse.

### Reproducción

```cpp
void playSong();       // Iniciar reproducción
void pauseSong();      // Pausar
void stopSong();       // Detener y resetear posición
void toggleSongPlayPause();
```

### Posición

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `seekSongTo(double)` | seconds | Ir a posición |
| `seekSongRelative(double)` | deltaSeconds | Mover relativo |
| `setSongStartSeconds(double)` | seconds | Punto de inicio |
| `getSongStartSeconds()` | — | Inicio actual |
| `setSongEndSeconds(double)` | seconds | Punto de fin |
| `getSongEndSeconds()` | — | Fin actual |

### Fade

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setSongFadeOutSeconds(double)` | seconds | Duración del fade |
| `getSongFadeOutSeconds()` | — | Fade actual |
| `setFadeActive(bool)` | shouldFadeOut | Activar fade |
| `isFadeActive()` | — | ¿Fade activo? |
| `setFadeStopsPlayback(bool)` | shouldStop | ¿Fade detiene? |
| `isFadeStopsPlayback()` | — | ¿Fade detiene? |

El fade de transporte (`setFadeActive`) reduce instrumentos y pads, pero
mantiene audibles los cuatro buses de referencia: click stem, click dinámico,
guía stem y guía dinámica. La exclusión de stems usa `stemClickGuideStates` y
rige tanto en mezcla agrupada como con routing multicanal explícito. El mute
master y el fade técnico de reemplazo de canción continúan afectando la salida
completa.

Con arreglo personalizado activo, `setSongStartSeconds()` y
`setSongEndSeconds()` delimitan la ventana de reproducción sin modificar el
mapa de secciones. Sus límites usan la duración total del arrange. Un `Fin`
personalizado recorta el playback, muestra el marcador correspondiente en la
waveform y detiene el transporte en ese segundo; sin personalización, el custom
arrange reproduce su duración completa.

Todo seek se limita a `[Inicio, Fin]`. `stopSong()` retorna a `Inicio` y
`getSongCurrentPositionSeconds()` consulta el espejo atómico de control para que
la UI refleje inmediatamente seeks solicitados mientras el transporte está
detenido.

La UI usa `getLoadedSongLengthSeconds()` para conservar la escala de la
waveform original y `getSongLengthSeconds()` para el largo del timeline de
arrange. Así, un `Fin` recortado se presenta como una guía vertical visible
sobre las secciones, sin convertir el resto del timeline en una falsa duración
de audio.

### Estado de reproducción

```cpp
bool isSongPlaying() const noexcept;
// Retorna true si está reproduciendo
```

El pitch público se normaliza a `-12..+12` semitonos. El tempo usa la relación
`source/output` y se limita a `0.75..2.0`.

Cada callback sincroniza los valores activos de pitch y tempo con
`StemMixEngine` **antes** de calcular `requiredInputFrames()`. Los setters
son idempotentes y conservan el historial cuando el valor no cambia. Esto
también cubre la carga de una canción: reiniciar `playbackTempoRatioActive`
a `1.0` no basta si el motor todavía conserva la relación de la canción
anterior. Un transporte a BPM original debe consumir exactamente los mismos
frames que sus lectores; de lo contrario deriva y dispara seeks correctivos
repetidos con silencios de read-ahead.

Al completar naturalmente la ventana `[Inicio, Fin]`, el transporte queda
detenido y estacionado exactamente en `Fin`. La UI diferencia este estado de
un stop manual —que retorna a `Inicio`— y, si la canción tiene configurado
auto-play dentro del setlist activo, carga el destino y llama `playSong()`.
La marca se persiste como `SetlistItem.transitionToNext.autoPlay`: significa
«reproducir el item inmediatamente siguiente». Sin crossfade espera a Fin;
con `transitionToNext.crossfade` la UI prepara el destino cuando quedan los
segundos configurados en `AppSettings::getSongCrossfadeSeconds()`. El fundido
real empieza sólo después de restaurar el destino y se acorta si la preparación
consumió parte de la cola saliente. Ver `ui/SongCrossfade.md` y el contrato de
transporte para restricciones del renderer de salida inicial. No busca otra canción marcada ni salta posiciones intermedias; el
último item nunca puede tener una transición activa. Por tanto sobrevive al
reinicio de la aplicación y continúa siendo una propiedad local del setlist,
no del proyecto `.pwproj`.

Los items que referencian un `.pwproj` serializan sólo identidad, referencia y
transición. Mezcla, pitch, tempo, ventana, arrange, guía, click y routing se
restauran desde el proyecto. Los mismos campos se siguen leyendo y escribiendo
en el item únicamente para canciones legacy hasta completar su migración. El
lector convierte el antiguo `autoPlayNext` al objeto `transitionToNext` sin
perder la marca existente.

Un cambio de pitch publica inmediatamente ese target entero, pero Bungee acerca
la escala DSP al nuevo valor durante 50 ms de audio. La rampa es interna al
`TimeStretchProcessor`, está acotada a ocho sub-bloques o llamadas al backend
por callback y conserva exactamente el total de frames fuente/salida; no crea
hilos ni requiere trabajo de control adicional en `MTControlsAudioProcessor`.

### Handoff de arrange y buses de canción

Los loops y jumps se prearman en una segunda cadena reader/resampler por stem.
Si todos los destinos están residentes, el callback compone prefijo actual y
sufijo destino en el offset source devuelto por `SectionScheduler`; el offset
equivalente de salida parte también metrónomo y guía dinámica. El click se
resincroniza con el beat source y BPM efectivo, y la guía puede disparar el
anuncio de entrada antes de renderizar el sufijo.

Si el destino todavía no está listo, audio mantiene un handoff diferido ligado
a `arrangementTransitionIntentGeneration`. Cualquier nueva intención de
seek/stop/edición/jump/loop lo invalida. Un seek de control tiene prioridad
sobre la única ventana secundaria del stem durante todo su callback.

Al alcanzar el final de canción dentro de un bloque, stems, click y guía se
limpian desde el offset de salida exacto. Los pads siguen sonando porque su
lifecycle no depende del timeline de la canción.

## Diagnóstico realtime

El evento `audio_*.csv` se cierra al final del callback, después del
time-stretch agrupado, buses virtuales, master y transiciones. Registra:

- `streaming_misses`: callbacks acumulados con read-ahead incompleto;
- `streaming_missing_frames`: frames exactos sustituidos por silencio;
- `time_processor_reset_misses`: discontinuidades que llegaron antes de que
  el worker reconstruyera un spare Bungee;
- `time_processor_service_failures`: reconstrucciones Bungee fallidas; el
  servicio aplica backoff exponencial antes de reintentar.

La hora de pared se calibra una vez al iniciar el logger. El callback deriva
`wall_ms` desde el contador monotónico de alta resolución y no consulta el reloj
de sistema por bloque. El script de resumen usa `sort` O(n log n) y reporta
tanto deltas dentro del CSV como el último acumulado observado.

El aprendizaje MIDI publica desde el callback sólo un resultado de tamaño
fijo. La mutación del vector de bindings y su persistencia se realizan en el
message thread. Transporte, tempo, power de pads y demás acciones de control se
publican en una FIFO SPSC fija; un timer del procesador la consume en orden a
100 Hz aunque el editor esté cerrado. La solicitud de carga del pack se
coalesce por separado. Las notas de pads usan dos bitmasks atómicos y se aplican
sólo cuando el callback obtiene su `tryLock` normal de render.

### Benchmark: marcas por etapa dentro de `processBlock`

Para localizar de dónde viene el tiempo de un callback lento (o un "mini
corte" audible), el callback captura la duración de seis sub-etapas con el
contador de alta resolución. Todas se serializan como microsegundos saturados
a `int16` (~32.7 ms sin signo) en el mismo CSV `audio_*.csv`:

| Columna | Etapa | Qué mide |
|---|---|---|
| `stage_pre_read_us` | `preRead` | Desde la entrada al callback hasta la primera lectura de stem. Incluye lectura atómica de estado, snapshot de sección, math de gain de transición, updates de tempo, coordinación de seek, inyección de estado por stem, math de fade. Crece si el UI thread está contiendo con el audio thread vía cache-line bouncing. |
| `stage_read_next_us` | `read_next` | El loop de `getNextAudioBlock` sobre todos los stems. Solo se llena en el path `routingActive`. |
| `stage_decode_render_us` | `decode_render` | Suma del tiempo dentro de `proc.process` (pitch / time-stretch) y del delay line por stem. Es un sub-conjunto de `read_next`. |
| `stage_mix_stems_us` | `mix_stems` | `mixEngine.mixStems` (separated) o `mixEngine.renderDirect` (direct). Solo se llena en el path `!routingActive`. |
| `stage_mix_output_us` | `mix_output` | `mixEngine.renderToOutput` — mezcla agrupada donde el fade se aplica a instrumentos y preserva stems de click/guía. Los buses dinámicos y pads se procesan después. |
| `stage_ramp_transition_us` | `ramp_transition` | `applySongTransitionRamp` final — envolvente de fade aplicada al buffer de salida en el handoff entre canciones. |

Una etapa que no corrió en un bloque queda en 0 (no se interpreta como
"corrió en 0us").

### Benchmark: detector de underrun (gap event)

Un "mini corte" audible es el resultado de un callback que no se ejecutó
en su slot esperado. El callback compara su `blockStartTick` contra el
`blockEndTick` del callback anterior. Si la diferencia supera el umbral
(`kDefaultGapThresholdMs = 15.0` ms, configurable vía
`AudioDiagnostics::setGapThresholdMs`), se emite un evento de gap.

El evento se serializa a `audio_gaps_<ts>.csv` con la forma:

```
wall_ms,delta_ms,song_position_sec,stem_count,is_playing
```

- `wall_ms`: timestamp del callback que finalmente corrió.
- `delta_ms`: duración del silencio (gap) en milisegundos.
- `song_position_sec`: posición de playback al momento de detectar el gap,
  para mapear el corte a un instante específico de la canción.
- `stem_count` / `is_playing`: contexto.

`AudioDiagnostics::getStats()` ahora también reporta el conteo acumulado de
gaps y el peor gap observado en milisegundos, p.ej.
`Blocks: 110719 | Overruns: 0 (0.0%) | Avg: 0.70ms | Max: 4.34ms | Gaps: 9 (max 484ms)`.

### Routing agrupado

`processBlock` resuelve dos rutas cuando `routingActive == true`:

1. **Ruta por stem**: se mantiene para salidas multicanal heterogéneas,
   click/guía que requieren transposición, o cuando no hay pitch/tempo activo. Cada stem
   corre su propio `BungeeProcessor::process()` y se enruta con
   `StemOutputRouter.routeStemToOutputChannels(...)` de forma individual.
2. **Ruta agrupada**: con salida estéreo o destinos multicanal homogéneos,
   **y** con pitch/tempo activo, el
   callback reparte los stems en `stemMixBuffer`, `percussionBuffer` y
   `dryBuffer` con `StemMixEngine::mixStems`, corre las tres voces
   `TimeStretchProcessor` que `StemMixEngine` mantiene agrupadas y
   conserva un máximo de tres voces. En estéreo, `StemOutputRouter` aplica
   canal y ancho de cada stem a la entrada de su grupo: click L, guía R e
   instrumentos estéreo pueden coexistir. En multicanal homogéneo se enruta
   el resultado después. El render recibe explícitamente `numSamples`, nunca
   la capacidad de scratch de 8192 frames. El preflight consulta únicamente
   las voces agrupadas utilizadas, sin depender de spares individuales.

Los controles siguientes se conservan en la
ruta agrupada:

- Volumen, trim, mute y solo por stem (vía `StemTrack::setMuted/
  setSoloed/setVolume/setTrim` antes del `getNextAudioBlock`).
- Fade de instrumentos y la excepción para stems click/guía (vía
  `StemMixEngine::applyFade`, que excluye `dryBuffer` cuando el
  tempo está activo).
- Pitch sólo en stems marcados como pitch-shiftable (los otros van a
  `percussionBuffer` y `dryBuffer`, no a `stemMixBuffer`).
- Cambio de tempo para los grupos que lo requieren.
- Compensación de latencia vía `mixEngine.getOutputLatencySamples()`,
  aplicada a `metronomeLatencyDelay` y `dynamicGuideLatencyDelay`.
- Routing mono/estéreo hacia el canal físico configurado por el
  usuario (con plegado L+R para mono).
- Routing individual para salidas multicanal heterogéneas.

El procesamiento espectral de una suma puede diferir de sumar procesadores
independientes; no se promete igualdad sample a sample. Los cambios de routing
estéreo atraviesan ahora la latencia de Bungee, igual que el volumen por stem.

El campo `routing_grouped_eligible` de `audio_stems_<ts>.csv` deja
explícito qué bloque del callback usó la ruta agrupada, para que el
script de análisis distinga ambos modos dentro del mismo escenario.

## Metrónomo API

### Habilitación y estado

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setMetronomeEnabled(bool)` | enabled | Activar/desactivar |
| `isMetronomeEnabled()` | — | ¿Está activo? |

### Tempo y volumen

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setMetronomeBpm(double)` | bpm | BPM del click |
| `getMetronomeBpm()` | — | BPM actual |
| `setMetronomeBeatsPerBar(int)` | beats | Beats por compás |
| `setMetronomeVolume(float)` | volume (0-1) | Volumen |
| `getMetronomeVolume()` | — | Volumen actual |

La única perilla de `CLICK DYN`, rotulada `GAIN`, representa el nivel combinado
`volume * gain` entre 0 y 4 (hasta aproximadamente +12 dB). La unidad 1 conserva
el nivel anterior y ocupa el centro del recorrido; un doble clic recupera 1.
El tooltip muestra dB. `ClickDivisionPanel::setVolume` y sus callbacks
`onVolumeChanged`/`onVolumeDragEnded` usan este nivel combinado 0..4 sólo en ese
panel: no redefinen el volumen 0..1 del processor.

Al mover la perilla, `ChannelGridPanel` transforma niveles hasta 1 en
`volume=level, gain=1`, y niveles superiores en `volume=1, gain=level`.
La carga y los refrescos sólo muestran el producto existente, sin normalizar
ni escribir la mezcla guardada. Al terminar el gesto se persisten el volumen
0..1 y la ganancia independientes en `ClickDynMixSnapshot`, manteniendo su
formato y las canciones previas. MIDI y remoto conservan sus controles y rangos
separados; el refresco visual refleja el producto de ambos.

### Mute/Solo/Gain

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setMetronomeMuted(bool)` | muted | Silenciar |
| `isMetronomeMuted()` | — | ¿Muteado? |
| `setMetronomeSolo(bool)` | soloed | Solo |
| `isMetronomeSolo()` | — | ¿Solo? |
| `setMetronomeGain(float)` | gainValue | Gain lineal |
| `getMetronomeGain()` | — | Gain actual |

El processor calcula una sola condición global de solo por callback para
stems, Click Dyn y Guía Dyn. Cada bus conserva su propio mute y queda audible
cuando no hay ningún solo o cuando ese bus también está en solo. Click Dyn
participa aunque exista un stem de click en la canción; sólo se excluye cuando
está deshabilitado o su máscara de divisiones es cero.

`getMetronomeMeterLevels()` consume el máximo pico publicado desde su llamada
anterior. El pico se mide sobre el buffer de Click Dyn después de su gain y de
la compensación de latencia, de modo que el VU representa la señal que se
enruta y no pierde transitorios entre ticks de interfaz.

### Enrutamiento de salida

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setMetronomeOutputRouting(int, int)` | channel, width | Canal y ancho |
| `getMetronomeOutputChannel()` | — | Canal actual |
| `getMetronomeOutputWidth()` | — | Ancho actual |

### Click division (dinámica)

Controla qué notas del click suenan (bitmask):

| Valor | Constante | Descripción |
|-------|-----------|-------------|
| 1 | 1/4 | negras |
| 2 | 1/8 | corcheas |
| 4 | 1/16 | semicorcheas |

```cpp
void setClickDivision(int division) noexcept;
int getClickDivision() const noexcept;
void setClickDivisionEnabled(bool enabled) noexcept;
bool isClickDivisionEnabled() const noexcept;
```

El metrónomo mantiene por separado la subdivisión solicitada por control y la
subdivisión activa de audio; sólo la cambia en un límite de negra. Sus resets
desde control se publican por generación. Durante un handoff interno al bloque,
el processor usa el render por rango para conservar el prefijo, resetear y
sincronizar en el límite, y producir el sufijo desde la fase destino.

### Samples personalizados

```cpp
bool loadMetronomeClickSamplesFromFolder(
    const juce::File &folder,
    const juce::String &style,
    juce::String *errorMessage = nullptr
);
void clearMetronomeClickSamples() noexcept;
bool hasMetronomeClickSamples() const noexcept;
```

### Guía por stem

```cpp
void setStemClickGuideMask(const std::vector<bool> &mask) noexcept;
// mask[i] = true → stem i contribuye al click
```

## Dynamic Guide API

Guias de audio localizadas por idioma.

### Settings

```cpp
void setDynamicGuideSettings(const DynamicGuideSettings &settings);
DynamicGuideSettings getDynamicGuideSettings() const;
```

### Volumen y enrutamiento

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setDynamicGuideVolume(float)` | volume | Volumen |
| `getDynamicGuideVolume()` | — | Volumen actual |
| `setDynamicGuideGain(float)` | gainValue | Gain |
| `getDynamicGuideGain()` | — | Gain actual |
| `setDynamicGuideMuted(bool)` | muted | Mute |
| `isDynamicGuideMuted()` | — | ¿Muteado? |
| `setDynamicGuideSolo(bool)` | soloed | Solo |
| `isDynamicGuideSolo()` | — | ¿Solo? |
| `setDynamicGuideOutputRouting(int, int)` | channel, width | Enrutamiento |
| `getDynamicGuideOutputChannel()` | — | Canal |
| `getDynamicGuideOutputWidth()` | — | Ancho |

### Idioma

```cpp
void setDynamicGuideLanguage(AppLanguage language);
```

Idiomas soportados: `AppLanguage::Spanish`, `AppLanguage::English`

### Warnings

```cpp
bool consumeDynamicGuideMissingSectionWarning(juce::String &outSectionName);
// Retorna true si hay una sección faltante
```

## Pitch Shift API

### Pitch por stem

```cpp
void setStemPitchShiftMask(const std::vector<bool> &mask) noexcept;
// mask[i] = true → aplicar pitch shift al stem i
```

### Pitch global

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setPitchSemiTones(int)` | semitoneDelta (-12 a +12) | Pitch global |
| `getPitchSemiTones()` | — | Semitonos actuales |

## Guide Mask

```cpp
void setStemGuideMask(const std::vector<bool> &mask) noexcept;
// mask[i] = true → stem i es guía (para DynamicGuide)
```

## Pads API

### Habilitación

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setPadsEnabled(bool)` | enabled | Activar/desactivar |
| `isPadsEnabled()` | — | ¿Está activo? |

### Carga de pack

```cpp
bool loadPadsPackFromFolder(const juce::File &sourceFolder);
bool reloadLastPadsPack();
bool ensurePadsPackReady();
bool isPadsPackLoaded() const noexcept;
juce::String getPadsPackStatusText() const;
```

### Volumen y gain

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setPadsVolume(float)` | volume | Volumen |
| `getPadsVolume()` | — | Volumen actual |
| `setPadsGain(float)` | gain | Gain (default 0.75) |
| `getPadsGain()` | — | Gain actual |

### Preset de crossfade

```cpp
void setPadsCrossfadePreset(PadsEngine::CrossfadePreset preset) noexcept;
PadsEngine::CrossfadePreset getPadsCrossfadePreset() const noexcept;
double getPadsCrossfadeDurationSeconds() const noexcept;
void beginPadsSongTransitionFadeOut() noexcept;
void beginPadsSongTransitionFadeIn() noexcept;
```

### Trigger de notas

```cpp
void triggerPadNote(int noteIndex, int octave);
void triggerPadNoteForSongTransition(int noteIndex, int octave);
void stopPadNote(int noteIndex);
float getPadsOutputLevel() const noexcept;
```

Las voces de pad repiten indefinidamente el sample con un crossfade de loop en
el borde. Al cargar otra canción, la UI mantiene cualquier pad activo y lo
cambia mediante crossfade a la tonalidad efectiva de la nueva canción
(`songKey + pitchSemitones`). El cambio de transporte por sí solo nunca detiene
el pad. Todas las plataformas mantienen el inicio de las doce notas preparado
para disparar desde RAM. El worker completa la nota activa y reutiliza una caché
WAV PCM en cambios posteriores. Se conservan el cursor y el crossfade al publicar
el buffer completo, y sólo sus buffers activos permanecen residentes además de
los doce inicios. La preparación no bloquea el message thread.

En una transición automática, el editor solicita el fade down del bus de Pads
cuando el transporte entra en los últimos segundos definidos por el preset de
crossfade (1/3/5 s). La nota activa no se borra. Tras restaurar la siguiente
canción, elimina la voz anterior mientras el bus permanece en cero, prepara el
pad de su tonalidad efectiva y solicita el fade up con la misma duración antes
de reanudar el transporte. No se solapan tonalidades durante la entrada. Una detención manual, un seek
hacia atrás o retirar el destino de autoplay cancelan la bajada y recuperan el
nivel del pad.

Todo cambio manual entre dos canciones cargadas también usa
`triggerPadNoteForSongTransition()`. Antes de cargar se difiere la solicitud por
la duración configurada para que el bus complete su fade down. La carga actúa
entonces como frontera silenciosa: se reemplaza la voz anterior y sólo la
tonalidad de la nueva canción entra con el fade up configurado. Esto aplica a
siguiente, anterior y selección desde biblioteca.

### Actualizar enrutamiento

```cpp
void refreshPadsRoutingFromSettings() noexcept;
```

## Master Controls

### Volumen master

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setMasterVolume(float)` | volume (0-1) | Volumen master |
| `getMasterVolume()` | — | Volumen actual |
| `setMasterGain(float)` | gain | Gain master |
| `getMasterGain()` | — | Gain actual |

### Mute master

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setMasterMute(bool)` | muted | Silenciar master |
| `isMasterMuted()` | — | ¿Muteado? |

## Tempo

### Tempo de reproducción

| Método | Parámetros | Descripción |
|--------|------------|-------------|
| `setPlaybackTempoBpm(double)` | bpm | Tempo de playback |
| `getPlaybackTempoBpm()` | — | Tempo actual |
| `getOriginalSongBpm()` | — | BPM original |

## Arrangement API

### Snapshots de arrange

```cpp
void setSongArrangement(
    const SongArrangementSnapshot &arrangement,
    bool isCustomArrangement
);
bool isCustomArrangementActive() const noexcept;
```

### Part jumps

```cpp
void requestArrangementPartJump(int partIndex);
void toggleArrangementPartLoop(int partIndex);
void clearArrangementPartLoop() noexcept;
void clearPendingArrangementPartJump() noexcept;
int getArrangementPartLoopIndex() const noexcept;
int getPendingArrangementPartJumpIndex() const noexcept;
```

**Semántica de toggleArrangementPartLoop(partIndex):**

- Si no hay loop activo y la playhead está **fuera** de la sección: arma
  el loop sin salto. Sólo empieza a repetir cuando la reproducción alcanza
  naturalmente la sección.
- Si la playhead está **dentro** de la sección elegida: activa su loop sin
  mover la posición.
- Si se reemplaza otra sección marcada en Loop que **no está sonando**:
  cambia el armado al nuevo `partIndex`, sin salto automático.
- Si está **sonando una sección en Loop** y se elige otra: prepara el destino
  conservando el loop saliente. Sólo confirma la cola cuando todos los stems
  tienen los buffers residentes; al siguiente final válido de la sección
  saliente, salta al destino y deja ese nuevo destino en Loop. No adelanta el
  salto ni libera el loop anterior durante la preparación o la espera del límite.
- Pulsar de nuevo Loop en el destino de ese cambio cancela la preparación/cola
  y conserva el loop saliente. `clearPendingArrangementPartJump()` hace lo
  mismo; `clearArrangementPartLoop()` apaga el loop y cancela ese cambio.
- Si el loop activo es **la misma** sección: apaga el loop y limpia el salto
  pendiente. Una cancelación no puede dejar un salto fantasma.

Durante el cambio entre loops, Preparando/En cola identifica el destino;
`getArrangementPartLoopIndex()` sigue identificando la sección saliente hasta
la transición. Las reglas de Ir permanecen independientes: Ir conserva el loop
mientras prepara, lo libera al confirmar y no activa Loop en el destino.

### Edición de arrange

```cpp
void setArrangementEditingActive(bool editingActive) noexcept;
```

La publicación estructural de arrange usa una secuencia odd/even
`arrangementStructurePublicationSequence`. El callback de audio acepta una
versión nueva sólo si puede leer de forma coherente el snapshot de
`SectionScheduler`, `arrangementCustomActive`,
`arrangementTotalLengthSeconds`, `arrangementPendingJumpPartIndex` y
`arrangementLoopPartIndex`. Si la secuencia cambia o está impar, conserva la
última versión audio-owned.

Una actualización estructural de arrange no detiene un transporte que ya está
reproduciendo, incluso si coincide con una transición de modo de la UI. El
callback conserva el playhead actual y adopta el siguiente snapshot coherente;
la inicialización o restauración estando detenido posiciona el transporte en
`Inicio` (normalmente 0:00), nunca en el timestamp de la primera sección.
La carga del editor vuelve a publicar ese destino después de restaurar la
ventana y el arrange, antes del fade-in. Esto cubre también canciones con una
única sección tardía y la carga desde un editor de arrange abierto. Un recorte
de `Inicio` guardado explícitamente sigue definiendo el comienzo reproducible.

Los loops, jumps y discontinuidades implícitas de clips insertados se resuelven
como transiciones de bloque con `transitionOffsetFrames`. El processor prearma
el destino en el banco secundario de cada stem; si no está listo en el sample de
frontera, difiere el handoff y mantiene la línea actual audible hasta que todos
los stems expongan el mismo destino.

## Metering

### Salidas master

```cpp
void getOutputMeterLevels(float &left, float &right) const;
```

### Metrónomo

```cpp
void getMetronomeMeterLevels(float &left, float &right) const noexcept;
```

### Dynamic Guide

```cpp
void getDynamicGuideMeterLevels(float &left, float &right) const noexcept;
```

## Audio Device

`StandaloneAudioDeviceBridge` conserva la configuración predeterminada del
dispositivo; se retiró el override Android de 40 ms. Los 40 ms observados en
algunas capturas son el tamaño efectivo de esa sesión, no una garantía del
bridge. `AudioDiagnostics.budget_ms` permite comprobarlo en cada dispositivo.
El callback siempre procesa sus frames efectivos, independientemente de la
capacidad máxima reservada en `prepareToPlay`.

El stream auxiliar que JUCE usa para obtener un hilo con prioridad de tiempo
real se abre en modo compartido. En algunos Samsung, abrir ese probe como
exclusivo desplaza el stream de reproducción recién configurado antes de su
primer callback y deja la sesión sin audio.

```cpp
StandaloneAudioDeviceBridge &getAudioDeviceBridge() noexcept;
const StandaloneAudioDeviceBridge &getAudioDeviceBridge() const noexcept;
AppAudioMode getCurrentAudioMode() const noexcept;
```

## MIDI API

Ver [MidiTargets](midi/MidiTargets.md) para lista completa de targets.

### Bindings

```cpp
std::vector<MidiBinding> getMidiBindings() const;
void setMidiBinding(const MidiBinding &binding);
void clearMidiBindingForTarget(int targetId);
```

### MIDI Learn

```cpp
void beginMidiLearnForTarget(int targetId);
bool consumePendingMidiLearnBinding(MidiBinding &outBinding);
```

### Probing

```cpp
MidiInputProbe getLastMidiInputProbe() const noexcept;
bool consumePendingMidiUiCommand(int &outCommand);
```

La marca de tiempo del probe se deriva de una calibración monotónica; recibir
MIDI no consulta el reloj de pared. Los comandos internos 12–21 trasladan
play/pause/stop/seek, tempo y lifecycle de pads al timer del message thread.

## Estado y presets

```cpp
void getStateInformation(juce::MemoryBlock &destData);
void setStateInformation(const void *data, int sizeInBytes);
juce::AudioProcessorValueTreeState &getValueTreeState();
```

## Valores de retorno comunes

| Método | Tipo de retorno | Valores |
|--------|----------------|---------|
| `isSongPlaying()` | `bool` | true/false |
| `hasLoadedSong()` | `bool` | true/false |
| `isMetronomeEnabled()` | `bool` | true/false |
| `isMasterMuted()` | `bool` | true/false |
| `isStemMuted(int)` | `bool` | true/false |
| `isStemSoloed(int)` | `bool` | true/false |

## Ejemplos de uso

### Ejemplo 1: Cargar y reproducir

```cpp
MTControlsAudioProcessor &proc = ...;

// Cargar canción con 4 stems
juce::StringArray stems = {
    "/audio/song_drums.wav",
    "/audio/song_bass.wav",
    "/audio/song_guitars.wav",
    "/audio/song_vocals.wav"
};

proc.loadSongStems(stems, "Amazing Grace", "G", 72.0, 
    [](double progress) {
        std::cout << "Loading: " << (progress * 100) << "%\n";
    });

// Reproducir
proc.playSong();
```

### Ejemplo 2: Mezcla con mute/solo

```cpp
// Bajar volumen de bass, silenciar drums
proc.setStemVolume(1, 0.6f);  // Bass
proc.setStemMute(0, true);     // Mute drums

// Solo vocals
for (int i = 0; i < proc.getLoadedStemCount(); i++) {
    proc.setStemSolo(i, i == 3);  // Solo stem 3 (vocals)
}
```

### Ejemplo 3: Setup de metrónomo

```cpp
proc.setMetronomeEnabled(true);
proc.setMetronomeBpm(72.0);
proc.setMetronomeVolume(0.7f);

// Solo clicks de negra y semicorcheas
proc.setClickDivision(1 | 4);  // 1/4 + 1/16
proc.setClickDivisionEnabled(true);

// Cargar samples personalizados
proc.loadMetronomeClickSamplesFromFolder(
    juce::File("/samples/clicks/jazz"),
    "jazz"
);
```

## Archivos relacionados

- [StemMixEngine](../engine/StemMixEngine.md)
- [StemTrack](../engine/StemTrack.md)
- [Metronome](../audio-systems/Metronome.md)
- [Pads](../audio-systems/Pads.md)
- [DynamicGuide](../audio-systems/DynamicGuide.md)

### VU de control remoto

`getRemoteMetronomeMeterLevels()` usa acumuladores de pico independientes del
VU local. La publicación LAN los lee desde el message thread; no consume los
picos reservados a `getMetronomeMeterLevels()`. El resto de niveles son lecturas
de los atómicos de metering existentes. Ver [Control remoto](../integrations/RemoteControl.md).
