---
type: Transport API
title: Transport
description: Control de transporte y arrange en MTControlsAudioProcessor
resource: src/PluginProcessor.h
tags: [juce, audio, transport, playback, arrange]
timestamp: 2026-11-06
---

# Transport API

En escritorio, las marcas de Letras para FreeShow y Holyrics se evalúan
sobre los segmentos realmente renderizados y los seeks confirmados,
con un scheduler independiente de las secciones. Ir/seek recuperan sólo el
cue vigente en el destino y Loop abre otra pasada. Las órdenes vencidas se
descartan fuera del audio thread; ninguna llamada de red entra al callback.
Cada destino conserva su proveedor y se valida contra la conexión activa.
[FreeShow](../integrations/FreeShow.md), [Holyrics](../integrations/Holyrics.md).

La app standalone puede exponer este transporte mediante el
[control remoto LAN](../integrations/RemoteControl.md). El servidor sigue
siendo la única fuente de verdad: procesa los comandos en el message thread y
publica posición/estado. El cliente alimenta la interfaz habitual y no
avanza `TransportClock` ni carga stems de esa sesión.

Control de reproducción y arrange musical.

### Setlist activo y navegación Teams

Los selectores Personal/Equipo filtran la biblioteca y los setlists visibles;
no cambian el setlist persistido activo ni su cola. Anterior, Siguiente y el
avance automático resuelven canciones por ID contra un snapshot separado de
ese setlist activo, incluso si no aparecen en el espacio que se está explorando.
Abrir explícitamente un setlist actualiza la cola. Abrir una canción de equipo
fuera del setlist activo prepara su lista individual con la revisión publicada.
La vista filtrada nunca se usa como fallback para saltar a otra canción mientras
ya hay una cargada.

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

El metrónomo sigue la posición fuente del callback al iniciar o reanudar.
Play no añade un golpe manual: desde cero suena el beat inicial; desde una
pausa entre beats continúa la misma grilla. Activar el clic durante reproducción
y los saltos/loops siguen la misma regla, sin agregar un ataque junto al beat.
En una canción sin stems, el cambio de tempo conserva el cálculo existente de
frames fuente, pero omite el render de un bus de stems vacío y su compensación
de latencia. El clic vivo no espera a un time-stretcher que no tiene audio.
Las canciones con stems conservan su procesamiento y alineación de latencia.

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
Los editores de BPM del reproductor y del mapa de tempo admiten punto o coma
decimal y solicitan teclado decimal en movil. Los valores se validan completos:
una entrada mal formada no aplica un prefijo numerico. La visualizacion elimina
ceros finales y muestra hasta seis decimales; abrir el control no redondea a
BPM enteros. Ensayo conserva y muestra tambien la fraccion al avanzar sus pasos
de un BPM. Los limites del motor y su rampa de tempo no cambian.

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
`Refrain`, `Post-Chorus`, `Bridge`, `Instrumental`, `Solo`,
`Interlude`, `Tag`, `Vamp`, `Outro` y `Ending`) se anuncian un compás antes
de su entrada. El compás se calcula con el BPM y la métrica activa de la
canción.

Los conteos y las utilidades independientes se anuncian en su punto sobre la
onda, sin crear límites de sección. `Breakdown` (Baja intensidad) y `A Capella`
son siempre indicaciones secundarias y se anuncian inmediatamente. Los fragmentos legacy que no puedan
migrarse sin ambigüedad conservan su anuncio al entrar. Las
utilidades incluyen indicaciones como cambios de intensidad, pausas,
sostener, entradas de instrumentos, cambios de tono, última vez y final
grande. Una sección que comienza en `0.0 s` se anuncia al entrar porque no
hay un compás anterior disponible.

Las utilidades insertadas desde el menú `Utilidades` conservan esa identidad
en el arrange y se disparan al entrar. `Baja intensidad`/`Breakdown` y
`A Capella` conservan el anuncio inmediato incluso en fragmentos antiguos
que no puedan migrarse. Los nombres de las secciones musicales de la lista
anterior mantienen su anuncio adelantado cuando no tienen `utilityCue`.

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

### Automatización de secciones

`SongArrangementPartSnapshot::automation` guarda una mezcla parcial por ID de
sección y `stemId`. El motor la aplica también en el arrange normal, según la
posición efectiva de audio: seeks, loops, jumps y huecos recuperan su mezcla sin
depender de un timer de UI. Instrumentos admiten volumen/ganancia/mute; Pads
admite volumen/ganancia/power/nota/octava. Click, guía, Solo, master y ruteo
mantienen su control habitual.

`beginSectionAutomationEditing(partId)` pausa y fija un borrador; otro Begin se
rechaza hasta guardar o cancelar. Los setters existentes ajustan ese borrador.
`finishSectionAutomationEditing(save)` guarda o cancela sin reanudar;
`clearSectionAutomation(partId)` borra el preset de la sección editada.
`getSongArrangementSnapshot()` entrega sólo datos confirmados. La persistencia
de la mezcla general utiliza getters `Base` para evitar capturar un preset o
una sustitución manual temporal como mezcla global.

El callback conserva una publicación coherente, aplica todos los límites que
cruza su bloque y programa rampas de mezcla de 30 ms. Pads no relanza una nota
idéntica al repetir una sección. La carga y el seek detenido seleccionan sus
valores sin iniciar sonido automatizado nuevo; Play o un preview de edición
permiten activarlo. Ver [contrato de Section Automation](../engine/SectionAutomation.md)
para ownership remoto, MIDI, prefetch y restitución de la mezcla base.

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
Loop no modifica Mute; pulsar Mute conserva el loop activo y el salto pendiente.
Armar un loop fuera de la sección no mueve el transporte: espera la llegada
natural. Si ya está sonando otra sección en Loop, elegir Loop en un nuevo
destino conserva el loop saliente, prepara los buffers y confirma la cola sólo
cuando todos los stems están listos. Al próximo final válido de la sección
saliente salta al destino, que queda en Loop. Preparando/En cola señala el
destino; el indicador Loop permanece en la sección saliente hasta el cruce.
Pulsar otra vez Loop en ese destino o limpiar el salto cancela el cambio y
mantiene el loop saliente. Limpiar Loop apaga la repetición y cancela también
ese cambio pendiente. Reemplazar un loop armado que aún no está sonando sigue
sin solicitar salto.

Ir conserva Loop mientras muestra Preparando. Cuando todos los destinos están
residentes, confirma la cola, cancela Loop y salta al siguiente final válido
sin desmutear el destino. Mute sólo cambia por una acción explícita de mute/desmute. Un cambio de Mute, Skip o metadatos que mantenga los mismos
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

Ir no se confirma mientras falte audio de algún stem: continúa el recorrido
actual, incluido Loop, sin congelar reloj ni silenciar click/guía. Se indica
Preparando sin anunciar el destino. Una vez residente se protege y se programa
el siguiente final válido; nunca se ejecuta un salto diferido de Ir contra un
límite ya pasado. La caché puente entrega el prefijo fuente mientras el reader
principal retoma streaming. Otros handoffs de arrange conservan su protección
de generación. Ver el contrato de caché en SectionScheduler.md.

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

La marca de inserción de la regla aplica a secciones musicales y a indicaciones.
`Cuenta` y `Utilidad` crean marcadores independientes en `arrangement.cues`:
no dividen una sección musical, no cambian su duración, y no ofrecen límites de
loop, salto ni automatización. Una secuencia `1–4` o `1–7` ocupa una sola marca;
cada anuncio sigue el pulso del mapa de tempo vigente y conserva sus offsets
explícitos en segundos de source, incluso al cruzar un cambio de tempo.

Los marcadores son puntos de color dentro de la onda. Sólo al editar muestran
una línea fina hacia abajo y una etiqueta compacta junto al punto. Fuera de
edición se muestran únicamente los puntos; su target no ocupa el área de la
etiqueta oculta. Las etiquetas próximas se apilan en filas independientes; cada punto conserva su
momento temporal y puede elegirse por separado, incluso cuando coinciden.
En los extremos, la etiqueta se coloca al lado que cabe dentro de la onda.

La interacción de edición es directa: arrastrar cambia sólo la posición y
publica una operación de historial al soltar, sin hacer seek. El snap vigente
sigue aplicando; Option/Alt permite ubicación libre y las secuencias de conteo
conservan su ajuste al pulso. Doble clic/doble toque sobre el target del punto
elimina únicamente esa indicación y Deshacer la recupera. El target táctil
es de 44 puntos; en filas próximas, la cercanía al centro del punto resuelve
el hit. En escritorio el target es de 24 puntos. El tooltip pequeño indica
cómo eliminar: aparece con hover en escritorio y brevemente al tocar en móvil.
Click simple sólo selecciona; no hay popover ni botones de ±1 pulso. Fuera de
edición, o durante un borrador de automatización, no se eliminan ni mueven
marcas. `Breakdown` sale del selector de secciones musicales; junto con
`A Capella` pertenece al catálogo de utilidades.

Una indicación dentro de una sección queda vinculada a su ID y offset. Mover
la sección lleva su marca; copiar/pegar crea IDs nuevos y conserva ese offset.
Un recorte elimina las marcas excluidas y Deshacer las restaura. Una división
musical reasigna las marcas al fragmento que contiene el momento original.
Las marcas absolutas, sin sección, conservan su posición durante el ripple.
Crear, copiar y pegar respetan el máximo de 512 anuncios expandidos por canción
y rechazan la operación completa antes de modificar el arrange si lo excede.

La carga migra pequeñas secciones antiguas de conteo/utilidad únicamente cuando
se pueden absorber en una sección musical continua sin perder mapeo de source,
automatización ni referencias de copias. Los casos ambiguos conservan su
representación anterior; no se descartan ni se reinterpretan a la fuerza.
La detección incluye `Breakdown`, `Baja intensidad`, `A Capella` y `acapella`
sin distinguir mayúsculas, aunque la sección antigua no tenga `utilityCue`.

En escritorio y tablets iOS/Android, la regla de la waveform comparte el dibujo
de Ensayo mediante `TimelineRulerStyle`: altura de 24 puntos, números de compás
en fuente de 10 puntos y marcas cortas de compás/pulso en color secundario, sin
una caja independiente alrededor. `PracticeTimelineRuler` calcula las marcas
desde el mapa de tempo vigente, incluido el denominador de la métrica y cambios
sin guardar. Los números mantienen al menos 72 puntos entre etiquetas y los
pulsos aparecen cuando tienen al menos 10 puntos de separación. Al alejar el
zoom se reduce la densidad automáticamente; las marcas de compás visibles
mantienen al menos 8 puntos también durante la revisión de sugerencias.
Las subdivisiones de snap siguen
disponibles para seek y edición sin llenar la regla de marcas adicionales.
Los cursores de reproducción, salto pendiente y marca de inserción conservan
su dibujo e interacción sobre esta referencia. Las indicaciones se superponen
a la onda y no reservan altura adicional en la regla. En teléfono la regla
está disponible en edición y permanece oculta en performance.

En escritorio, `drawTimelinePlayhead` de `TimelineRulerStyle.h` dibuja el cursor con
una línea fina de `UITheme::textPrimary`, también en la onda y el minimapa. En
la regla añade una pequeña punta y usa opacidad de 0.65 en reproducción y 0.25
en pausa. Las sugerencias transitorias conservan sus marcas discontinuas de
color para distinguirlas del cursor. La revisión dibuja el cursor sobre su propia
cuadrícula, sin superponer las marcas de la regla habitual. Esta apariencia
recupera el estilo existente de escritorio sin cambiar posición, seguimiento,
seek ni comandos de transporte.
El dibujo de iOS y Android conserva su implementación vigente.

Letras de escritorio usa esa misma regla y solicita las marcas al mapa de tempo
de `WaveformBar`. Ambas vistas comparten la línea fina y bandera del playhead
mediante `TimelineRulerStyle`; el cambio es de dibujo, sin alterar posición ni
comandos de transporte. El zoom/desplazamiento de Letras transforma píxeles a
segundos absolutos antes de crear o arrastrar marcas. Ampliar la referencia de
secciones no cambia sus límites ni duraciones musicales.
La navegación con rueda comparte `TimelineNavigationGestures` con la onda:
componente horizontal dominante desplaza, vertical amplía/aleja bajo el cursor.
El pellizco conserva la transformación existente de Onda (`1 + amount`, entrada
cero sin efecto). El intervalo visible mínimo compartido es 1/18 de la canción.
Los modificadores no cambian el gesto y no se vuelve a invertir `isReversed`.
Estos gestos no publican ediciones y se ignoran durante un arrastre temporal en Letras.

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

### Crossfade de canciones con DSP y buses virtuales

`requestSongCrossfade(seconds)` arma un cambio explícito (0.1–10 s), sin cambiar
la canción ni iniciar sonido. Requiere una canción reproduciéndose con stems,
sin custom arrange, salto/loop/ensayo ni otro fade activo. Pitch/tempo modificados,
click y guía dinámicos son compatibles; no se recurre a audio seco para el tail.

El control prepara lectores y recursos de reemplazo antes del handoff. El
callback toma la captura mediante CAS (solicitada → capturando → retenida), de
modo que un timeout no pueda retirar las fuentes después de un load fallido.
En ese límite de bloque se intercambian los owners del vector de tracks y del
`SongProcessingState`: `StemMixEngine`, voces individuales Bungee/SoundTouch y
líneas de compensación conservan sus historiales, sin reconstruirse en audio.
Se retienen máscaras de pitch/percussion/guía, rutas, master, ventana y rampa
de tempo. Cada deck consume su propio número de frames fuente.

`RetainedVirtualBuses` conserva las voces y la fase del metrónomo, voces de guía
en curso, anuncios/cues pendientes de la línea temporal lineal y sus dos delays.
Los catálogos de samples se comparten en control y la captura realtime sólo
copia estado fijo o intercambia buffers ya preparados. Mute/solo y destinos
son independientes por deck; el master de la entrante no modifica la saliente.
Al llegar a Fin, se alimentan ceros y se drenan las latencias sin disparar nuevos
clicks ni anuncios más allá del final. Ver `engine/RetainedVirtualBuses.md`.

Durante la configuración entrante, el callback reproduce la saliente sin usar
`suspendProcessing()`, que haría silenciar al host. `finishPendingSongTransition()`
publica el destino configurado. Mientras su DSP resuelve el arranque y acumula
su latencia conocida, la salida saliente conserva nivel completo. No se decide
por la amplitud del contenido: una canción puede empezar intencionalmente en
silencio. Después se aplica un fundido lineal complementario (salida=1−t,
entrada=t) a stems, click y guía. La duración efectiva se limita al audio real
que aún queda en la saliente, convertido a frames de salida según su tempo.
Pads continúa siendo un único bus global con sus envolventes propias.

Stop/Pause cancelan la cola; un Stop durante la configuración impide el autoplay.
Un seek durante el overlap cancela la saliente; el seek interno que configura la
ventana entrante se permite durante el hold. Unload y cambio de dispositivo
cancelan y liberan recursos fuera del callback. Al concluir, el timer de control
llama `releaseRetiredSongCrossfade()` para liberar tracks y DSP bajo la
sincronización del servicio de mantenimiento, sin retener una segunda canción
completa hasta el siguiente load. La estructura/automatización saliente se
congela al capturar; loops, saltos y custom arrange siguen fuera de este flujo.

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
processor.requestArrangementPartJumpById("chorus-2");
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

Las acciones e indicadores de sección se resuelven por ID de ocurrencia, no
por el índice de la metadata. El procesador conserva índices internos para el
scheduler, devuelve aceptación booleana y rechaza antes de cambiar intenciones
un ID ausente/duplicado, fuera del límite reproducible o de una revisión vieja.
La UI traduce el ID confirmado a su propio orden; la compilación del runtime
puede ordenar o filtrar partes sin dirigir Ir/Loop a otra tarjeta. Los métodos
por índice del procesador conservan su uso interno y también devuelven aceptación.

### Transporte de indicaciones independientes

Los marcadores no son destinos de jump, rangos de loop ni propietarios de
mix/automatización. Su publicación comparte la generación de las secciones,
pero se evalúa sólo sobre los segmentos realmente recorridos por audio. Al
volver con un loop o seek, se reproducen de nuevo los puntos encontrados a
partir del destino, sin recuperar voces o conteos anteriores. Pausa no genera
nuevos cues aunque el playhead coincida con uno. Una canción con cues y sin
secciones explícitas conserva su rango de audio predeterminado.


### Retirada de descargas Teams

Un recibo de baja confirmado se procesa solo sin reproducción, carga, edición
o sesión remota. Si la canción retirada está cargada, se usa
`unloadActiveSongForLocalPurge` antes de borrar su entrada/archivos. No se fuerza
Stop durante una actuación; el trabajo permanece pendiente por dispositivo.

### Canciones en blanco

`loadSongStems` acepta un `ImportedSongData*` opcional para una canción en
blanco explícita y válida. Sólo ese caso permite una lista de rutas vacía;
las importaciones normales siguen rechazando audio ausente. Reutiliza el mismo
reemplazo, transición, `TransportClock`, mixer, metrónomo, PadsEngine y
SectionScheduler. No hay transportes ni lectores de silencio adicionales.

Las canciones en blanco siempre tienen duración/Fin cero (abierto). El reloj y
el click avanzan normalmente, y Seek no se limita a cero. Play continúa, Pause
conserva la posición y Stop vuelve a cero. No tienen un máximo artificial de
compases/tiempo. Los valores `blankSongBars` de prototipos antiguos son metadata
inerte, no duración. No se crean secciones de respaldo, ni se publican partes
o cues en el scheduler; restaurar un arreglo histórico no lo activa. Ir, Loop
y automatización de sección no encuentran destinos para una canción en blanco.
Los setters de Inicio/Fin/Salida se ignoran para este tipo. Las canciones con
audio conservan su duración, secciones, ventanas y scheduler habituales.

Click y pad se manejan desde sus controles existentes. Cargar desde el motor
no fuerza sus estados; el editor restaura los ajustes habituales de click y
sigue la tonalidad con `PadsComponent::followSongKey`. El pad global conserva
el mismo comportamiento ante Play/Pause/Stop que en canciones con audio; no hay
un apagado especial para canciones en blanco. Usa packs locales o importados,
sin solicitar un catálogo externo. El tono y el ratio de tempo existentes
siguen disponibles sin añadir otro motor.

La zona de WaveformBar queda vacía y sin controles; no se dibuja waveform,
regla, cursor, progreso de compás ni tarjetas. El selector Onda/Secciones y las
opciones de sección/ventana se ocultan en escritorio y en el menú táctil. Volver
a una canción con audio restaura sus vistas habituales. Cargar una canción en
blanco descarta la revisión transitoria de sugerencias e ignora sus resultados
tardíos para que no puedan volver a abrir esa vista. El cliente remoto usa
el mismo tipo explícito y extrapola posición abierta sin limitarla a cero.
El servidor rechaza acciones de arreglo, ventana y mapa regional para la vacía;
transport, tempo, tono y controles independientes de click/pad siguen siendo
los ordinarios y autoritativos. La regla ampliable, secciones y Fin manual o
automático para canciones en blanco están fuera del alcance de esta versión.
