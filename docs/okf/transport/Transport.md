---
type: Transport API
title: Transport
description: Control de transporte y arrange en MTControlsAudioProcessor
resource: src/PluginProcessor.h
tags: [juce, audio, transport, playback, arrange]
timestamp: 2026-07-25T00:00:00Z
---

# Transport API

La app standalone puede exponer este transporte mediante el
[control remoto LAN](../integrations/RemoteControl.md). El servidor sigue
siendo la única fuente de verdad: procesa los comandos en el message thread y
publica posición/estado. El cliente alimenta la interfaz habitual y no
avanza `TransportClock` ni carga stems de esa sesión.

Control de reproducción y arrange musical.

## Conceptos

### Transport States

```cpp
enum class TransportState : int {
    stopped = 0,
    playing = 1,
    paused = 2
};
```

### Sistema dual

El plugin usa dos sistemas de transporte:
1. **Legacy** (atomics): Para UI mirroring
2. **Frame-based** (TransportClock): Source of truth para audio thread

Durante la migración a engine-v2, ambos coexisten.

## Transport Controls

### Reproducción

```cpp
void playSong();           // Iniciar
void pauseSong();          // Pausar
void stopSong();           // Detener y resetear
bool unloadSong();         // Liberar stems y dejar el transporte vacío
void toggleSongPlayPause(); // Toggle
```

`unloadSong()` suspende el callback, espera a que termine cualquier bloque en
curso, libera las fuentes bajo `stemListLock` y reinicia transporte, arrange,
mix y metadatos. Devuelve `false` si el callback no pudo detenerse dentro del
límite de seguridad; en ese caso el llamador no debe eliminar los archivos que
respaldan la canción todavía.

### Posición

```cpp
// Seek
void seekSongTo(double positionSeconds);
void seekSongRelative(double deltaSeconds);

// Ventana de reproducción; al llegar a Fin se detiene, no repite.
void setSongStartSeconds(double seconds) noexcept;
double getSongStartSeconds() const noexcept;
void setSongEndSeconds(double seconds) noexcept;
double getSongEndSeconds() const noexcept;
```

### Consulta de estado

```cpp
bool isSongPlaying() const noexcept;
bool hasLoadedSong() const noexcept;
double getSongCurrentPositionSeconds() const;
double getSongLengthSeconds() const;
```

## Fade

### Fade out

```cpp
void setSongFadeOutSeconds(double seconds) noexcept;
double getSongFadeOutSeconds() const noexcept;
void setFadeActive(bool shouldFadeOut) noexcept;
bool isFadeActive() const noexcept;
```

### Fade behavior

```cpp
void setFadeStopsPlayback(bool shouldStop) noexcept;
bool isFadeStopsPlayback() const noexcept;
```

Si `fadeStopsPlayback = true`, al terminar el fade se pausa.
Si `false`, sigue reproduciendo silenciosamente.

## Tempo

### Playback tempo

```cpp
void setPlaybackTempoBpm(double bpm) noexcept;
double getPlaybackTempoBpm() const noexcept;
double getOriginalSongBpm() const noexcept;
```

Cambia el tempo de reproducción sin alterar el pitch (time stretch).
El BPM base del formulario de edicion corresponde al BPM original de la cancion.
Al guardarlo, si el tempo de reproduccion persistido aun coincide con el BPM
base anterior, se actualiza al nuevo BPM base. Un tempo personalizado se
mantiene; el usuario lo modifica desde el control del reproductor. El mapa
automatico de una sola region sigue el nuevo BPM/compas base, mientras un
mapa de tempo editado por el usuario conserva sus regiones.

El target se mantiene en atomics, pero el ratio efectivo no salta de un bloque
al siguiente: durante reproducción se interpola con `smoothstep` a lo largo de
100 ms. El `TransportClock` avanza con los frames fuente realmente solicitados
por cada bloque de la rampa.

En Android, la representación visual del transporte se actualiza a 30 Hz desde
el temporizador de UI y suaviza variaciones pequeñas durante reproducción. La
caché de seguimiento limita su escala a `1.25` y usa overscan `2x` para evitar
imágenes intermedias de más de 7k píxeles en tablets de alta densidad. Los seeks,
loops y saltos de arrange conservan el cambio inmediato. El reloj de audio
continúa siendo la fuente de verdad y no se interpola.

## Arrangement

### Concepto

El arrange permite definir secciones no lineales:
- Saltar entre partes
- Hacer loops de secciones
- Reordenar playback

En el layout compacto de iPhone, el desplazamiento horizontal de la forma de
onda mantiene una breve inercia al soltar el dedo y desacelera dentro de los
límites de la ventana visible.

### Rangos de fuente al copiar

Una copia conserva el rango de audio de su sección fuente. Al resolver el
final de una sección original que coincide exactamente con el inicio de un
clip insertado, Copy usa el lado izquierdo del límite: el final pertenece a
la sección original, no al clip siguiente. Esta regla se aplica por igual al
botón Copy y a Cmd/Ctrl-C/V. El render de onda, seek y playback mantienen su
semántica habitual de borde derecho, donde ese punto corresponde al clip
insertado.

Un duplicado sólo propaga su rango explícito si está contenido dentro del
rango de su `sourcePartId`; de lo contrario Copy vuelve al rango de la fuente
referida, sin reescribir datos persistidos durante la carga.

Ese rango de referencia se resuelve en coordenadas del audio original,
descontando las inserciones anteriores. La posición de una sección original
desplazada por una copia no se utiliza directamente como límite del audio.
Mover un duplicado conserva su `sourcePartId`, `sourceStartSec` y
`sourceEndSec`, incluso al cruzar hacia la izquierda de su sección fuente.
Una copia que sobrevive a la eliminación de la sección fuente conserva su
rango explícito válido y se puede volver a copiar. Tanto Copy como Cmd/Ctrl-C/V
conservan también la marca `utilityCue` de los anuncios de utilidad.

Al ajustar el borde derecho de una copia, el editor limita la duración al
audio disponible y desplaza las secciones posteriores en ambos sentidos:
recortar cierra el espacio y ampliar lo vuelve a abrir. Cada preview se calcula
desde el snapshot inicial del gesto, sin acumular desplazamientos. Los límites
de fuente se resuelven desde ese mismo snapshot. Una copia al final del arrange
puede recuperar su duración de fuente al arrastrar más allá del borde visible.
El gesto publica un único cambio al soltar y se revierte completo con Deshacer.
`Editar rango` aplica las mismas reglas contra el snapshot previo: conserva la
fuente al trasladar ambos extremos, permite recuperar la longitud de una copia
al final y abre/cierra espacio al cambiar el extremo derecho. Al guardar resuelve
la sección por ID, para no editar otra sección si el orden cambió con el diálogo
abierto.

### Anuncios de guía dinámica

Las secciones musicales (`Intro`, `Verse`, `Pre-Chorus`, `Chorus`,
`Refrain`, `Post-Chorus`, `Bridge`, `Instrumental`, `Solo`, `Breakdown`,
`Interlude`, `Tag`, `Vamp`, `Outro` y `Ending`) se anuncian un compás antes
de su entrada. El compás se calcula con el BPM y la métrica activa de la
canción.

Los conteos y las utilidades se anuncian al entrar en su sección. Las
utilidades incluyen indicaciones como cambios de intensidad, pausas,
sostener, entradas de instrumentos, cambios de tono, última vez y final
grande. Una sección que comienza en `0.0 s` se anuncia al entrar porque no
hay un compás anterior disponible.

Las utilidades insertadas desde el menú `Utilidades` conservan esa identidad
en el arrange. Así, una utilidad cuyo texto coincide con una sección musical
(por ejemplo `Baja intensidad`/`Breakdown`) sigue disparándose al entrar; una
sección musical con el mismo nombre no cambia su anuncio adelantado.

El scheduler vive en el timeline fuente. Por eso, cuando cambia el tempo, el
adelanto de un compás se convierte con
`(60 / playbackBpm) * beatsPerBar * tempoRatio`. Cambiar BPM o métrica vuelve a
publicar el snapshot para mantener el mismo compás audible que el click y el
bounce offline.

### Snapshots

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
int getPendingArrangementPartJumpIndex() const noexcept;
void clearPendingArrangementPartJump() noexcept;
```

Pide saltar a una parte específica en el próximo ciclo.

### Loops

```cpp
void toggleArrangementPartLoop(int partIndex);
void clearArrangementPartLoop() noexcept;
int getArrangementPartLoopIndex() const noexcept;
```

Activa loop en una parte específica.

### Loop A/B temporal de Ensayo

```cpp
bool preparePracticeLoop(double startSeconds) noexcept;
bool setPracticeLoopRange(double startSeconds, double endSeconds) noexcept;
void clearPracticeLoop() noexcept;
bool isPracticeLoopActive() const noexcept;
bool hasPracticeLoopPreparation() const noexcept;
juce::Range<double> getPracticeLoopRange() const noexcept;
uint64_t getPracticeLoopRevision() const noexcept;
double getMinimumPracticeLoopDuration() const noexcept;
juce::String getPracticeLoopError() const;
```

Marcar A conserva un borrador en la vista y publica solo un destino de prefetch.
Esa preparación no activa un loop ni mueve o pausa el transporte. El banco de
un seek, loop de sección, jump o transición de custom arrange inminente conserva
prioridad. Marcar B publica una región exacta del timeline fuente/arrange y
activa su repetición; no modifica `Inicio`, `Fin`, fade ni secciones persistidas.
Pausa conserva la posición; Stop vuelve a A mientras la región está activa.
Los seeks se limitan a A y al último frame anterior a B.

La vista Ensayo ajusta A y B al pulso más cercano de la regla antes de llamar
a estas APIs; la API y el renderer mantienen sus posiciones exactas en segundos.
El ajuste utiliza el mapa vigente y la métrica en tiempo fuente, sin depender
del zoom ni del BPM de reproducción modificado. En empate elige el pulso posterior.
A se limita usando el último pulso válido de Fin y la duración mínima para
garantizar que B pueda cerrarse en otro pulso. Si no existe uno, la marca se rechaza.

La región debe estar completamente dentro de Inicio/Fin, contener valores
finitos y durar al menos `getMinimumPracticeLoopDuration()`: 0.25 s como base,
o el tiempo fuente de un bloque a 2x más dos frames si ese valor es mayor.
El renderer admite una discontinuidad por callback. En custom arrange, A/B
puede cruzar secciones source-continuas, pero no un corte o clip que cambie la
fuente estrictamente dentro del intervalo. Un corte exactamente en A o B es
válido. Esta restricción evita combinar dos handoffs en el mismo bloque.

Mientras A/B está activo se suspenden los loops y jumps de sección y se ignora
Skip en un snapshot temporal de tamaño fijo. Mute y guía conservan sus
metadatos. El callback prearma A y aplica B→A en el frame exacto mediante el
handoff habitual de stems, click y guía; no depende de un timer de interfaz.
Una entrada A situada en mitad de una sección no dispara su anuncio de entrada.
El reloj de presentación de Ensayo se acota a A/B y reconoce explícitamente
el retroceso B→A por la longitud del rango, incluso si el salto queda por
debajo del umbral habitual de discontinuidad visual de 250 ms.
Si el destino no está listo al llegar a B, la canción se pausa exactamente en
B, el loop se desactiva y se publica `buffer-not-ready`, en lugar de continuar
fuera del intervalo. Un callback inesperadamente mayor que la región usa el
mismo corte con `too-short`.

`PlaybackSession` conserva la revisión del loop/preparación que creó. Cerrar
Ensayo o desconectar su cliente libera solo esa revisión; una intención más
reciente de otro cliente queda vigente. Cargar otra canción, cambiar modo,
modificar Inicio/Fin o arrangement, pedir un jump/loop de sección o cambiar el
dispositivo a un buffer que invalide la duración cancela A/B. Apagar repetición
libera el motor; la vista puede conservar A y B para activarlo otra vez.
Borrar región en la vista libera esa misma propiedad y elimina el borrador,
sin cambiar posición ni estado de reproducción. `PlaybackSession` expone la
identidad de la revisión local o comando remoto (incluido uno pendiente) para
que snapshots atrasados no vuelvan a dibujar una región eliminada.

Los errores estables son `invalid-range`, `too-short`,
`discontinuous-arrangement`, `buffer-not-ready` y `rejected` para fallos remotos.
La UI los traduce a una ayuda breve. La región y su preparación nunca se
guardan en ProjectStore, SongLibraryStore ni sincronización Cloud.

El loop y `muteInstruments` de una sección son independientes: se puede
silenciar la instrumentación y repetir esa misma sección al mismo tiempo. La
guía y el click conservan su tratamiento normal de mute. Activar o desactivar
Loop no modifica Mute; pulsar Mute conserva el loop activo y cancela únicamente
un salto pendiente. Un cambio de Mute, Skip o metadatos que mantenga los mismos
límites y rangos de fuente publica el nuevo snapshot sin reiniciar el loop,
el salto pendiente ni la posición, incluso fuera del editor de arrange.

### Transiciones dentro del bloque

`SectionScheduler` devuelve el offset exacto del límite. El callback prearma el
destino de loops y jumps en el banco secundario de cada stem y, cuando todos
publicaron la misma ventana, compone un único bloque:

```text
sección actual ─────────┐
                       │ offset exacto
destino                 └────────────────
```

Los stems reciben el mismo `scheduleReadDiscontinuity()`. El reloj se coloca en
el frame destino y sólo avanza por el largo del sufijo, por ejemplo:

```text
bloque [512, 1024), límite 1000, destino 2000
prefijo = 488, sufijo = 24, posición final = 2024
```

Si el disco no termina el prefetch a tiempo, el audio no se vacía ni se resetea
el DSP. El callback conserva un salto diferido audio-thread-owned, continúa la
línea actual y ejecuta el handoff con offset cero en cuanto todos los stems
están listos. Así se prefiere continuidad audible a emitir una mezcla parcial.

El salto diferido conserva la generación de intención vigente al crearse.
Seek, stop, edición/publicación del arrange y cambios o cancelaciones de
jump/loop incrementan `arrangementTransitionIntentGeneration`. Si la generación
ya no coincide al comienzo de otro callback, audio descarta el destino diferido
en vez de ejecutar una intención obsoleta. Mientras haya un seek de control
pendiente, ese comando es el único dueño del banco secundario durante todo el
callback; un prearmado de arrange no puede sobrescribir su destino.

### Buses virtuales y fin exacto

El offset de transición de source se convierte proporcionalmente al offset de
salida del callback después de considerar tempo. Metrónomo y guía dinámica
renderizan por rangos usando el mismo límite que los stems:

1. El prefijo conserva la fase o voz de la sección anterior.
2. En el offset se resetea el estado virtual y se sincroniza con el frame source
   del destino.
3. El metrónomo dispara el click de entrada y la guía dispara el anuncio de
   entrada sólo cuando esa sección lo requiere.
4. El sufijo se renderiza desde la nueva sección.

Después, click y guía atraviesan su delay de compensación normal para mantener
el alineamiento con la latencia de pitch/tempo.

Cuando el final de la ventana cae dentro del callback, el motor también mapea
ese frame source al offset de salida y limpia desde allí hasta el final del
bloque en stems, click y guía. El estado de transporte se detiene en el segundo
marcado como `Fin`, dejando visible el punto exacto de corte. La waveform lo
representa con una guía vertical persistente por encima de las tarjetas del
arrange; no se confunde con el playhead ni se oculta detrás de una sección. La
guía usa un trazo punteado y se actualiza inmediatamente al pulsar `+` o `−` en
`Fin`, mientras la waveform posterior permanece visible con opacidad reducida.
La duración de audio fuente se conserva separada del `Fin` configurado para que el
recorte no se muestre como una duración de waveform errónea. Los pads no
pertenecen al timeline de canción y no se recortan.

`Inicio` usa el mismo patrón: su guía punteada se mueve con `+`/`−` y el tramo
anterior queda atenuado. `−` recorta tiempo del comienzo y `+` lo recupera; por
eso, en `00:00`, `+` queda deshabilitado y `−` permanece disponible mientras
exista ventana por recortar. En `Fin`, `−` recorta y `+` extiende hasta la
duración total disponible.

Con arreglo personalizado activo, `Inicio` y `Fin` delimitan únicamente la
ventana de reproducción; no desplazan ni reescriben las secciones. Ambos pueden
recorrer la duración completa del arrange. Si no hay un `Fin` personalizado, el
custom arrange usa su duración completa.

Las anotaciones de guía pueden cubrir sólo una parte de la canción. Crear,
mover, recortar o borrar esas anotaciones no reduce la duración disponible del
audio fuente ni el `Fin` que sigue al borde de la canción. La duración efectiva
del arrange es al menos la duración fuente más el tiempo de las copias
insertadas, y puede extenderse hasta una sección situada más allá de ese borde.
El marcador de inserción de la regla sólo actualiza su posición y el seek; no
modifica `Inicio` ni `Fin`.

Un `Fin` que coincide con el borde efectivo del arrange anterior se considera
un límite que sigue al arrange, no un recorte: si una edición inserta una copia
más allá del audio fuente, ese borde avanza con la nueva duración para que los
stems, click y guía dinámica lleguen a la sección insertada. Un `Fin` situado
antes de ese borde sigue siendo un recorte explícito.

`Inicio` y `Fin` son también los límites inferior y superior de cualquier seek.
Un clic o arrastre fuera de la ventana se limita a la guía correspondiente.
`Stop` devuelve el transporte a `Inicio` (a A si hay un loop temporal de
Ensayo activo), no a `0.0 s`, y la posición publicada
a la UI cambia de inmediato aunque el dispositivo no esté ejecutando callbacks.

Ensayo reutiliza `PlaybackSession::seekSongTo` para los dobles toques; el
arrastre horizontal usa `beginPracticeScrub` / `finishPracticeScrub` para una
pausa temporal y un único seek cuando termina el movimiento visual. Al soltar,
la onda continúa con la velocidad reciente del dedo y frena suavemente durante
180–1600 ms. Mantener el dedo quieto al menos 100 ms antes de soltar elimina
el impulso; Inicio/Fin y el loop activo limitan el desplazamiento sin rebote.
Si el destino alcanza un límite, la distancia restante acorta el frenado,
con un mínimo de 180 ms.
Durante el arrastre y la inercia no se envían seeks intermedios ni se reanuda
el audio. Un nuevo toque interrumpe la inercia y conserva la misma pausa.
La pausa guarda el estado de reproducción y la revisión de control; solo
reanuda si esa revisión y la canción siguen vigentes. Una orden posterior de
Stop, play/pause, seek, carga o cambio de ventana/arrange invalida la propiedad
del gesto. La revisión se actualiza en los hilos de control, fuera del callback
de audio. `cancelPracticeScrub(true)` restaura una pausa propia al cerrar;
`false` descarta el gesto al detener o cambiar de contexto.
El callback `finishScrub` devuelve `bool`: en Local refleja la aceptación de
la finalización; en Cliente confirma únicamente el envío del comando a la cola.
El rechazo remoto posterior se observa mediante `scrubFinishRejected` al llegar
el `ack` de End. Un fallo inmediato o ese rechazo cancelan la previsualización
del panel y no recuperan la propiedad de la pausa. Las vistas local y remota
mantienen el destino provisional durante al menos 120 ms y esperan dos muestras
consecutivas consistentes de posición antes de liberarlo, con un timeout de
respaldo de 1.5 segundos. El desfase restante converge suavemente hacia el reloj
autoritativo; no altera `TransportClock` ni genera seeks adicionales.

Después de esa previsualización, `PracticePlaybackClock` presenta la posición
de Ensayo usando el delta de cada frame y una corrección gradual de 90 ms,
con tolerancia base de 10 ms para la cuantización de los bloques. Conserva un
avance visual monótono durante reproducción normal y reinicia la estimación
ante un seek intencional, una posición nueva separada al menos 0.25 segundos
de la predicción (incluido un loop), pausa o cambio de canción. Se inicializa
desde la posición ya presentada al liberar el seek pendiente, evitando volver
al reloj previo del bloque. Si la posición fuente deja de avanzar, solo
predice durante un máximo de 250 ms. Es un reloj de presentación; no cambia
el estado, los límites ni el avance del reloj autoritativo de audio.

El compás se mide en segundos del timeline fuente/arrange usando
`60 / BPM original * numerador * 4 / denominador`; no usa el BPM modificado
de reproducción. Así, 7/8 a 120 BPM mueve 1.75 segundos del timeline aunque
el audio se reproduzca más lento. Se conservan Play/Pausa y los límites de
Inicio/Fin y del loop activo. La previsualización de Ensayo lee el rango de
sección normalizado vigente de `WaveformBar`, incluyendo las ediciones de
arrange que aún no se hayan guardado.

Al cargar una canción, el playhead se sitúa en `Inicio` (0:00 por defecto)
después de restaurar la ventana y las secciones. La primera sección es una
anotación del timeline y no determina el punto de arranque: si la única sección
empieza en 3:52, el audio anterior sigue disponible desde 0:00. La vista de la
onda también reinicia su cámara y su interpolación visual; no arrastra la
posición ni la inercia de seguimiento de la canción anterior. Pausar y reanudar
sin cargar otra canción conserva la posición actual.

### Edición

```cpp
void setArrangementEditingActive(bool editingActive) noexcept;
```

Marca si el usuario está editando el arrange.

Mientras la edición está activa, publicar un snapshot mediante
`setSongArrangement()` conserva el estado y la posición actuales del
transporte. Crear, eliminar, mover o redimensionar una sección actualiza el
scheduler en el lugar y solo realinea la lectura de source si el nuevo mapeo
afecta al playhead; no detiene ni rebobina la canción. Los loops y saltos
pendientes se conservan por ID mientras su sección siga existiendo.

La marca de inserción de la regla aplica a todos los tipos de sección,
incluidas `Cuenta` y `Utilidad`. Estas dos conservan su duración corta de grilla
y dividen la sección existente alrededor de la marca sin crear solapamientos.

En escritorio y tablets iOS/Android, la regla de la waveform comparte el dibujo
de Ensayo mediante `TimelineRulerStyle`: altura de 24 puntos, números de compás
en fuente de 10 puntos y marcas cortas de compás/pulso en color secundario, sin
una caja independiente alrededor. `PracticeTimelineRuler` calcula las marcas
desde el mapa de tempo vigente, incluido el denominador de la métrica y cambios
sin guardar. Los números mantienen al menos 72 puntos entre etiquetas y los
pulsos aparecen cuando tienen al menos 10 puntos de separación. Al alejar el
zoom se reduce la densidad automáticamente. Las subdivisiones de snap siguen
disponibles para seek y edición sin llenar la regla de marcas adicionales.
Los cursores de reproducción, salto pendiente y marca de inserción conservan
su dibujo e interacción sobre esta referencia. En performance de teléfono,
la regla continúa oculta.

En el editor de guía, si no hay secciones se muestra un único botón `+` centrado
sobre la waveform. Con secciones, el mismo `+` aparece como cuarta acción en la
barra contextual de cada sección y abre el selector contextual. La lista inicial
muestra directamente todas las secciones; las secciones con variantes (por
ejemplo `Verso` o `Coro`) abren su propio submenú. `Conteos` y `Utilidades`
también se abren como submenús en cascada. Al abrirlo desde una sección, la
inserción usa su límite final; al seleccionar una opción se cierra toda la
cadena de menús. El botón no reserva altura adicional ni reduce el área de
waveform.

## Song Metadata

### Información de canción cargada

```cpp
juce::String getLoadedSongTitle() const;
juce::String getLoadedSongKey() const;
double getLoadedSongBpm() const;
int getLoadedStemCount() const noexcept;
```

### Carga de canción

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

La UI activa el indicador de carga del waveform y procesa su primer frame
antes de abrir un `.pwproj` o resolver los stems. Así una extracción cold no
deja la interfaz sin feedback. Las actualizaciones posteriores reflejan audio,
configuración, interfaz y waveform sin bombear el message loop por cada stem.

## Transición entre canciones

Cuando se carga otra canción durante playback, el transporte usa una transición
`fade-through-zero`:

1. La canción actual continúa mientras se crean y prebufferizan los stems nuevos.
2. El output completo baja de 1 a 0 en 100 ms.
3. El reemplazo de fuentes ocurre únicamente con el output en silencio.
4. La canción nueva queda pausada y en gain 0 mientras se restaura su snapshot.
5. `finishPendingSongTransition()` inicia playback y sube de 0 a 1 en 150 ms.

La suspensión breve necesaria para reemplazar los contenedores se realiza solo
después de alcanzar cero. Por tanto, cualquier buffer vacío del host coincide
con una ventana que ya es silenciosa.

Si `deferTransitionFadeInUntilConfigured` es `false`, `loadSongStems()` completa
automáticamente el fade-in. La UI principal usa `true` para aplicar antes del
fade-in mezcla, routing, tempo, click, guía, ventana y arrangement.

## Seek lock-free

El seek usa atomics para ser thread-safe:

```cpp
std::atomic<double> seekTargetSeconds{0.0};
std::atomic<uint64_t> seekRequestGeneration{0};
uint64_t appliedSeekRequestGeneration = 0; // sólo audio thread
```

### Algoritmo de seek

1. Control escribe `seekTargetSeconds` e incrementa la generación con
   `release`.
2. Audio captura una generación y mapea el frame de arrange al source usando
   el mismo snapshot de secciones del bloque.
3. Solicita el banco secundario en todos los stems.
4. Si la reproducción es audible, sólo confirma el seek cuando todos exponen
   el destino. Pausado o detenido puede confirmar de inmediato.
5. Vuelve a comprobar la generación, cambia todos los cursores, limpia la
   historia temporal y actualiza `TransportClock` únicamente desde audio.
6. Publica la generación aplicada. Una solicitud más nueva nunca se pierde por
   un `exchange(false)` de una solicitud anterior.

## TransportClock (engine-v2)

Namespace: `playworship::audio`

### Estados

```cpp
enum class State : int {
    stopped = 0,
    playing = 1,
    paused = 2
};
```

### API

```cpp
void play() noexcept;
void pause() noexcept;
void stop(Frame returnFrame = 0) noexcept;
void seek(Frame frame) noexcept;
void advance(int frameCount) noexcept;

State getState() const noexcept;
bool isPlaying() const noexcept;
Frame getPositionFrames() const noexcept;
double getPositionSeconds() const noexcept;
```

## Ejemplo de uso

### Reproducción básica

```cpp
processor.playSong();

// Durante playback
auto position = processor.getSongCurrentPositionSeconds();
auto length = processor.getSongLengthSeconds();
float progress = position / length;
```

### Loop de sección

```cpp
// Configurar ventana: reproduce de 30 a 60 s y se detiene en Fin.
processor.setSongStartSeconds(30.0);
processor.setSongEndSeconds(60.0);

// Iniciar playback
processor.playSong();
```

### Cambio seguro a la siguiente canción

```cpp
// Preparar y reemplazar manteniendo la entrada en silencio.
processor.loadSongStems(nextSongPaths, nextTitle, nextKey, nextBpm,
    [](double p) { /* progress */ }, true);

// Restaurar aquí mix/routing/click/guía/arrangement.
processor.finishPendingSongTransition();
```

### Arrangement

```cpp
// Definir arrange
SongArrangementSnapshot arrangement;
arrangement.parts = {
    { .start = 0.0, .end = 10.0, .loop = false },
    { .start = 10.0, .end = 30.0, .loop = true },  // Loop verse
    { .start = 30.0, .end = 50.0, .loop = false }
};

processor.setSongArrangement(arrangement, true);
processor.playSong();

// Saltar a chorus
processor.requestArrangementPartJump(2);
```

## Archivos relacionados

- [TransportClock](../engine/TransportClock.md)
- [SectionScheduler](../engine/SectionScheduler.md)
- [MTControlsAudioProcessor](../processor/MTControlsAudioProcessor.md)

### Interfaz local o remota

`PlaybackSession` conecta los mismos proveedores/callbacks visuales al
procesador local o al estado autoritativo del servidor. El cliente extrapola
como máximo 250 ms entre mensajes de posición y congela al desconectar. Los
comandos solo se aceptan tras recibir el snapshot inicial; no hay una segunda
vista de transporte. Ver [Control remoto](../integrations/RemoteControl.md).
