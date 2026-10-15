---
type: Integration Contract
title: Control remoto LAN
description: La interfaz existente usa una sesión local o una réplica por WebSocket
authority: src/network/PlaybackSession.h
tags: [remote, websocket, mdns, transport, mixer, waveform]
timestamp: 2026-09-25T00:00:00-03:00
---

# Control remoto LAN

Configuración → Control remoto en móvil (General en escritorio ancho) contiene
el selector segmentado **Local | Cliente | Servidor**. Local es el valor al iniciar. El descubrimiento, selección de servidor,
conexión y errores detallados se muestran dentro de Configuración. Cliente
añade solo una etiqueta pequeña pintada en la esquina superior derecha de la
onda; no altera su distribución ni intercepta gestos. Muestra `REMOTO` en verde
tras el primer snapshot, `BUSCANDO` antes de elegir servidor, `CONECTANDO` en el
primer intento y `RECONECTANDO` en ámbar si se pierde una sesión ya activa. Se
ve también con la onda vacía.
Local y Servidor no muestran esa etiqueta.
En móvil, la fila Control remoto se encuentra en el grupo Audio y conexiones.
Su página conserva selector, estado, descubrimiento y alias apilados. Navegar
a otra sección no interrumpe la conexión ni cambia el modo.

## Interfaz compartida

`PlaybackSession` adapta lecturas y acciones de `TopBar`, `ChannelGridPanel`,
`ChannelStripCard`, `StereoVUMeter`, `WaveformBar` y `PadsComponent`. En Local y
Servidor delega al procesador; en Cliente lee el estado recibido y envía comandos.
Los componentes conservan sus funciones de dibujo, dimensiones y layout responsive.
`SongListPanel` y `LibraryCatalogPanel` aceptan una biblioteca remota en memoria.
Los stems incluidos, sus IDs, nombres, categorías y orden generan los mismos strips,
incluidos los canales virtuales Click Dyn y Guía Dyn.

Entrar en Cliente termina las escrituras locales pendientes, guarda los metadatos
y onda locales, pausa el transporte y silencia el procesador mediante un atómico
leído por `processBlock`. No abre ni descarga stems remotos. Al volver a Local
restaura la canción, selección, onda y configuración locales con el transporte
pausado. Los controles recibidos no se escriben en la biblioteca del cliente.
La sesión de UI vive más que los componentes que mantienen referencias a ella.

## Descubrimiento y conexión

- El servidor escucha TCP en el primer puerto libre de 49172–49182 y anuncia
  `_playworship._tcp.local.` por mDNS/DNS-SD.
- El nombre anunciado usa el nombre visible del dispositivo: en macOS, el
  nombre del equipo de Compartir; en Android, `Settings.Global.DEVICE_NAME`
  (con fabricante/modelo como alternativa); en iOS, `UIDevice.name`. No se
  anuncia `localhost`. La página de control remoto permite escribir un alias
  local para el servidor, aplicado sin cerrar las conexiones WebSocket.
  Si el alias queda vacío se vuelve al nombre del dispositivo. En iOS 16+
  `UIDevice.name` puede devolver solo `iPhone` o `iPad` sin el entitlement
  especial de Apple; el alias distingue equipos con ese nombre genérico.
- macOS e iOS usan el Bonjour del sistema (`DNSServiceRegister`, browse,
  resolve y resolución IPv4). No envían multicast desde la app: en un iPhone
  físico ese envío requiere el entitlement restringido de multicast, aunque
  el usuario haya concedido Red local. El tipo concreto está declarado en
  `NSBonjourServices`. Los errores de permiso se muestran en Configuración y
  se reintenta para recuperarse después de conceder acceso.
- Android/Windows conservan el backend mDNS portátil. Publica PTR, SRV, TXT y
  A por interfaz, y responde a las consultas de resolución de Bonjour. SRV
  por sí solo no permite que el Bonjour nativo complete la resolución.
- Un único servidor descubierto se conecta automáticamente. Si hay varios,
  se elige desde Configuración; la app no alterna de servidor por sí sola.
  El selector muestra también el servidor elegido automáticamente.
- WebSocket reconecta con espera creciente. Tras una desconexión se conserva
  la última vista, se congela la posición, los VU caen a cero y no se envían
  comandos hasta recibir un nuevo snapshot. La etiqueta pasa a `RECONECTANDO`
  en el siguiente ciclo de UI y vuelve a `REMOTO` solo después de aplicar ese
  snapshot; una conexión TCP abierta por sí sola no declara el control listo.
- Android usa `WifiManager.MulticastLock` en primer plano. iOS declara
  `NSBonjourServices` y la descripción del permiso de red local.
- La LAN no tiene pairing/autenticación por decisión de producto. Los mensajes
  no contienen credenciales ni rutas del servidor. Las portadas se reducen a
  160 px y la caché del cliente usa nombres derivados de SHA-256.

## Automatización de secciones

Local y Cliente comparten las acciones `Automatizar`, `Listo`, `Cancelar` y
`Borrar` del panel de secciones. En Cliente, `automation.begin` identifica la
canción y la sección por sus IDs estables. El servidor mantiene un único editor
del borrador, ligado a la conexión y al ID del comando que lo abrió; un segundo
cliente ve sus cambios y la etiqueta de edición, pero tiene deshabilitados los
controles automatizables de instrumentos/Pads y no puede guardar, cancelar o
borrar ese borrador. Master, Click/Guía, Solo y ruteo siguen siendo manuales.
Los controles locales del servidor también respetan esa propiedad.
El motor bloquea los controles MIDI físicos automatizables durante ese borrador
remoto, incluso los mensajes ya encolados. Master, Click/Guía y Solo mantienen
su funcionamiento manual.

Durante la edición, volumen/ganancia/mute de instrumentos y los controles
automatizables de Pads llevan un
envoltorio con `songId`, `editCommandId` y `controlValue`. El servidor verifica
los tres antes de aplicar el movimiento. Un mensaje atrasado de una edición
terminada no se interpreta como un cambio de la mezcla base. `automation.finish`
identifica la misma sección y lease e incluye `save`; `automation.clear` puede
borrar una sección sin borrador, o cancelar y borrar el borrador propio de esa
sección. Las ediciones de estructura, ventana y mapa de tempo se rechazan
mientras hay un borrador abierto.

Guardar o borrar actualiza el arrangement comprometido del procesador, la
Biblioteca/sidecar del servidor y la onda, y publica un snapshot inmediato. La
edición de faders de un borrador no dispara persistencia de la mezcla base. Si
el editor se desconecta, el servidor cancela su borrador y recupera la mezcla;
una reconexión recibe estado completo y no recupera la propiedad anterior.

Los snapshots incluyen `sectionAutomation` (`editing`, `partId`, `partName`,
`commandId`), indicadores de automatización por stem/Pads y los valores base
además de los efectivos. `settings.arrangement.parts[].automation` conserva el
contenido comprometido con el mismo formato canónico de ProjectStore; los
movimientos aún sin guardar se reflejan sólo en los controles efectivos. El
cliente mantiene su destino de edición provisional hasta confirmar/rechazar el
inicio, evitando que la barra aparezca y desaparezca al esperar el snapshot.

## Protocolo v1

- `snapshot`: estado autoritativo de transporte, canción, mezcla, ventana,
  arrange, mapa de tempo, click, guía, pads y metadatos de biblioteca/setlists.
  El servidor observa cambios a 5 Hz y publica de inmediato tras comandos.
  Su caché conserva el snapshot completo para nuevas conexiones. La biblioteca
  solo se incluye en actualizaciones cuando cambió; el cliente conserva la
  anterior si el campo no está presente.
- `meters`: posición, reproducción y niveles de master, stems, click, guía y
  pads a unos 30 Hz. Los paquetes pendientes se agrupan conservando el más
  reciente y se descartan si el envío de un cliente está congestionado.
  El cliente conserva el suavizado y dibujo existentes de `StereoVUMeter`.
- Onda: bloques binarios `PWW1` de hasta 16 KiB, con floats IEEE-754 en orden
  de red; se comprueban tamaño y SHA-256 antes de aplicar los bins a la onda
  existente. Se envía al conectar o cuando cambia su hash.
- `command`: ID, acción, destino y valor. El servidor valida y aplica en el
  message thread, en orden de recepción, y distribuye `ack` con el resultado.
  Todos los clientes reciben los cambios confirmados.

### Destinos de Ir y Loop

El snapshot anuncia la extensión `sectionControls` con `v: 1`, `songId` y
`revision` (cadena opaca), además de `pendingPartId`, `preparingPartId` y
`loopPartId`. La revisión combina una época única por instancia del motor con
su publicación de estructura, evitando colisiones tras reiniciar el servidor;
cambiar o recargar canción/arreglo la invalida. Las intenciones de transporte
no cambian esa revisión.

El contexto del comando pertenece al arreglo instalado en la UI. Si un snapshot
nuevo llega durante un gesto y la actualización visual queda diferida, el
cliente conserva la canción/revisión de esa vista hasta instalar el nuevo
arreglo. Nunca combina una tarjeta antigua con la identidad recién recibida
de otra canción, incluso si ambas reutilizan IDs como `verse`. Mientras esos
contextos difieren, los indicadores nuevos no se proyectan sobre las tarjetas
antiguas.

`arrange.jumpById` y `arrange.loopById` llevan `target` con el ID estable de la
ocurrencia y `value: { songId, revision }`. Nombres repetidos o `sourcePartId`
no identifican el destino. El servidor rechaza una carga en curso, canción o
revisión distinta, ID ausente/duplicado y sección que el runtime no pueda
reproducir. El procesador vuelve a resolver y validar bajo su lock de control
antes de modificar intenciones; el ACK refleja el resultado de esa aceptación.
Aceptar Ir no significa que ya haya sonado: conserva Preparando, confirmación
de buffers y ejecución en la frontera válida. Loop no provoca un salto inmediato.

`arrange.clearJumpById` y `arrange.clearLoopById` llevan el mismo contexto sin
destino. Loop cancela el salto dentro de su única acción validada; un destino
rechazado no cancela una intención anterior ni modifica el mute. La UI resuelve
las acciones y los indicadores por ID, porque el runtime puede ordenar o
filtrar partes de forma distinta a la metadata. La metadata y su persistencia
conservan su orden y contenido.

El cliente presenta `Solicitando` mientras espera ACK y snapshot posterior;
un rechazo/timeout muestra `No aplicado` con el motivo en la ayuda del control.
Cuando falta compatibilidad, la tarjeta muestra directamente `Actualiza servidor`
en vez de `No aplicado`, con la instrucción completa en la ayuda. El cliente
anterior recibe `Update the client app to control sections safely` en el motivo
del ACK; su flujo existente lo muestra en Configuración → Control remoto.
Los indicadores Loop/Preparando siguen leyendo estado confirmado del servidor.
Desconexión, cambio de canción o una nueva solicitud descartan feedback anterior.

La envoltura general sigue en protocolo v1. Las conexiones antiguas y sus otros
controles permanecen disponibles, pero no hay fallback inseguro a índices:
un cliente nuevo frente a un servidor sin `sectionControls.v = 1` no envía
Ir/Loop y solicita actualizar el servidor. Un servidor nuevo rechaza
`arrange.jump`, `arrange.loop`, `arrange.clearJump` y `arrange.clearLoop` de
clientes anteriores con ACK negativo y petición de actualizar el cliente,
manteniendo la conexión. Esas órdenes no contienen contexto suficiente para
demostrar qué canción/ocurrencia vio el cliente.

### Presupuesto de interfaz y clientes lentos

Cada conexión dispone de `RemoteSendQueue`, con serialización JSON y envío TCP
en un hilo propio. Un peer que deja de leer no bloquea al editor ni los envíos
a otros clientes. La cola tiene un máximo de 128 elementos; conserva solo los
VU recientes y reemplaza snapshots pendientes sin cruzar confirmaciones,
catálogo u onda. Si se agota espacio fiable, ese cliente reconecta y recibe el
estado completo. El servidor limita a 2 segundos una escritura TCP bloqueada.
Los callbacks de cierre solicitan detener la cola; nunca esperan a su hilo
dentro del callback de IXWebSocket, que mantiene su mutex de estado.

La entrada agrupa volumen/gain pendientes por conexión, acción y destino.
Play/stop, cambios de canción y otras acciones discretas delimitan el agrupado;
el editor procesa como máximo 32 eventos por tick. Las confirmaciones de los
valores intermedios agrupados no son necesarias: el cliente conserva únicamente
el ID de su última intención por control. Una cola de entrada llena provoca
reconexión en lugar de descartar silenciosamente una acción fiable.

El catálogo se compara por metadatos sin serializar sus portadas. Solo cambia
su revisión cuando cambia su contenido; los snapshots de controles omiten la
biblioteca y reutilizan los datos de canción. Las portadas de 160 px se cachean
por archivo, fecha y tamaño (máximo 256 entradas). La firma de comparación del
editor usa la identidad de canción y revisión de biblioteca; la serialización
de canción/portada completa ocurre en los hilos de envío. La caché de conexión
nueva conserva la última biblioteca aunque el snapshot incremental la omita.

El cliente anuncia `hello` con `incrementalSnapshots: true` al conectar.
Después del primer estado, el servidor puede omitir `song` y `settings` si
permanecen iguales; marca el paquete con `incremental: true`. El receptor
reconstruye esos campos antes de entregarlos al editor y parsea JSON en el hilo
de recepción. El editor reutiliza la canción decodificada mientras sus metadatos
no cambien. Cada reconexión comienza con estado completo. Los clientes anteriores
que no anuncian esta capacidad siguen recibiendo snapshots completos.

## Arrastre de mezcla

El fader y el gain conservan la posición del gesto mientras el slider recibe
el dedo/mouse; un refresco del panel no la reemplaza. En Cliente,
`PlaybackSession` mantiene un valor provisional por acción/destino desde que
se envía el comando. No escribe en el procesador local. Un snapshot anterior
o el ack de un movimiento anterior no reemplazan el último movimiento.
La previsualización termina con el primer snapshot cuya secuencia sea posterior
al ack del último comando; un rechazo, desconexión, cambio de canción o timeout
de 2 segundos la descartan. Esto cubre master, stems, click, guía y pads.
El servidor emite ese snapshot incluso si el comando no cambia el valor.

Los comandos de mezcla actualizan inmediatamente el procesador del servidor.
`ChannelGridPanel` agrupa su persistencia tras 150 ms sin nuevos comandos y
reutiliza los snapshots normales de mezcla. Cambiar canción, setlist o modo,
y cerrar la app, vacía el guardado pendiente. El arrastre no invalida la caché
de arrange ni ejecuta una escritura de biblioteca por cada evento. Los cambios
locales de click y guía también esperan a que termine el gesto antes de persistir.

Acciones de reproducción: cargar/anterior/siguiente, play/pause, stop, seek,
fade, tempo y tono; mezcla: volumen, gain, mute y solo; click: on/off, divisiones
y compás; guía: fuente; ventana: inicio, fin y fade; arrange: secciones, reset,
saltos y loops; mapa de tempo; pads: notas, power, volumen y gain; setlists:
seleccionar, crear, renombrar, eliminar, agregar/quitar/reordenar y autoplay.

Importar/editar archivos, publicar/descargar contenido cloud y configurar el
hardware siguen siendo operaciones locales. No se ejecutan escrituras de
archivo o cloud sobre entradas de la biblioteca remota.

## Escucha temporal de Ensayo

El gesto vertical sobre la tarjeta de Ensayo usa `PlaybackSession` para cambiar
una capa temporal de escucha, independiente de volumen, gain, mute y solo
guardados. El gesto recorre tres niveles: instrumento reducido (−12 dB para el
seleccionado y nivel habitual para los demás), mezcla normal y enfoque (+2 dB
para el seleccionado y −12 dB para los demás instrumentos musicales). Arriba
sube un nivel y abajo baja uno: desde enfoque, abajo restaura la mezcla antes
de poder reducir el instrumento con otro gesto. En los extremos no reenvía
comandos. Pads, click y guía conservan su nivel. La restauración explícita
vuelve a la mezcla habitual; los niveles no acumulan ganancia.

El comando discreto `practice.listen` lleva `target` con el ID del stem incluido
y `value: { mode, songId }`, donde `mode` es 0 (restaurar), 1 (enfocar) o 2
(acompañamiento). Restaurar usa target vacío. El servidor valida modo, canción
actual e instrumento; rechaza un comando pendiente de una canción anterior,
un stem excluido, click/guía o una carga de canción en curso. El índice usado por
el procesador y el adaptador cuenta únicamente stems incluidos. El snapshot
publica `practiceStemId`, `practiceMode` y `practiceCommandId` para todos los clientes, incluida la
conexión nueva. Un cliente desconectado no envía gestos y muestra modo normal
hasta recibir otro snapshot.

Mientras `practice.listen` espera confirmación, el cliente presenta su última
intención de modo e instrumento, incluida la vuelta a normal. Así, gestos
consecutivos recorren los tres niveles aunque el snapshot anterior siga en
tránsito. Esta previsualización vence a los dos segundos o al recibir ACK y
un snapshot de secuencia posterior; un rechazo la descarta. Release,
desconexión y cambio de canción también la limpian. La previsualización de
modo normal no concede propiedad ni permite redirigir una escucha.

El halo y el estado visual de enfoque usan los getters confirmados
`getConfirmedPracticeListeningMode` y `getConfirmedPracticeListeningStem`:
leen `practiceMode`/`practiceStemId` del snapshot, sin aplicar el preview.
La máquina de gestos conserva los getters anteriores para recorrer niveles
rápidamente. Una orden aún pendiente o rechazada nunca enciende el halo antes
de que el servidor confirme el estado; desconectado se presenta modo normal.

La escucha modifica el audio del servidor compartido. El último cliente que
activa un modo se convierte en su propietario. La restauración explícita desde
cualquier cliente es una intención nueva y se aplica a la mezcla compartida.
Cerrar Ensayo usa una acción distinta, `practice.release`, con
`value: { mode: 0, songId }`. Es una limpieza de la vista: si el cliente ya no
es propietario, el servidor confirma sin modificar la escucha actual. Esto
preserva una intención posterior del servidor local o de otro cliente. Si
conserva la propiedad y coincide la canción, restaura la mezcla habitual.

El cliente puede redirigir automáticamente su escucha al cambiar instrumento
solo mientras conserva la propiedad de su última intención. Durante el último
`practice.listen` pendiente la conserva provisionalmente por hasta dos segundos;
un rechazo la revoca. El ack y un snapshot con secuencia posterior terminan esa
espera, siguiendo el orden usado para faders. Después, `practiceCommandId` debe
coincidir con el ID de su último listen. La selección local no recupera una
escucha que otro participante restauró o cambió. Release, restauración explícita,
desconexión y cambio de canción revocan esta propiedad del cliente. El servidor
publica ID vacío para escucha local o restaurada.

El servidor recibe `PeerDisconnected` con la identidad de conexión y conserva
los comandos pendientes de los otros clientes. Los avisos de cierre tienen
espacio reservado aunque se alcance el límite de comandos de entrada; un
aviso duplicado del mismo peer se agrupa. Esa desconexión restaura la
mezcla solo si el propietario coincide y la revisión del procesador sigue
siendo la que estableció su último comando. Una intención local posterior,
incluso si repite el mismo modo, revoca esa propiedad. Cambiar canción o el modo
Local/Cliente/Servidor restaura la escucha temporal; cerrar Ensayo libera solo
la escucha que aún pertenece a esa vista.
Estos comandos no solicitan persistencia de mezcla ni de ajustes de canción.

## Guía y metrónomo en Ensayo

Los controles de acompañamiento leen estado confirmado del mismo
`PlaybackSession`. Guía conserva la fuente seleccionada: cuando la fuente es
dinámica sólo cambia `mix.mute` del destino `guide`; cuando es original cambia
el mute de sus stems incluidos y realmente cargados. El mapeo cuenta únicamente
stems incluidos, igual que el mixer. En remoto cada índice también necesita
una fila de mezcla confirmada: que la metadata incluya un stem no prueba que
el servidor haya podido cargarlo. No envía `guide.enabled`, que selecciona
la fuente y no representa silencio. Una guía original con algún stem sin mute
se muestra encendida; apagarla silencia todos sus stems de guía, sin cambiar
instrumentos, click, Solo, niveles ni enfoque temporal.

El snapshot añade dos booleanos en `ui`, publicados desde el processor:
`isDynamicGuideEnabled` (fuente seleccionada) y `hasDynamicGuideAssets`
(pack cargado). El cliente no deduce estos estados de `settings`, porque su
caché/sidecar puede estar atrasado. Si el servidor no publica ambos campos
booleanos, Guía queda deshabilitada y pide actualizar el servidor. También se
deshabilita si no existen stems de guía original cargados o si la fuente
dinámica seleccionada no tiene assets; nunca cambia de fuente como fallback.
La consulta de assets ocurre en control/message thread, sin I/O ni cambios en
el callback de audio.
Guía dinámica también necesita su fila `guide` de mixer con `mute` booleano.
Metrónomo requiere la fila `click` con `mute` booleano, sus dos flags UI
booleanos y una máscara entera entre 0 y 7. Un snapshot incompleto o con tipos
incorrectos deshabilita el control correspondiente con explicación; ningún
campo faltante se interpreta como un bus desmuteado.

Metrónomo controla exclusivamente Click Dyn, independiente del click original.
ON habilita divisiones, desmutea el bus y habilita el motor. Preserva una máscara
no vacía; si era cero, activa negras (`1`) para que ON tenga un pulso. OFF
deshabilita el motor y conserva máscara, divisiones y mezcla. El estado ON
requiere motor y divisiones habilitados, máscara no vacía y bus sin mute; no
depende de estar reproduciendo ni del VU. Estos controles conservan los niveles,
Solo y ruteo existentes, por lo que ON no garantiza señal audible cuando esos
otros controles silencian la salida.

La vista mantiene el estado confirmado mientras espera el cambio y evita
duplicar pulsaciones pendientes. Confirma con el estado recibido, descarta la
espera al cambiar canción, fuente, stems o conexión, y permite reintentar tras
el timeout. Son ajustes normales de mezcla, con la persistencia existente del
servidor/local; cerrar Ensayo no restaura guía ni metrónomo. No alteran el modo
de enfoque ni su propiedad.

## Región A/B temporal de Ensayo

Los comandos discretos `practice.loopPrepare`, `practice.loop` y
`practice.loopRelease` usan el mismo canal fiable de controles. Prepare lleva
`{songId, start}` y prearma A sin activar repetición; Loop lleva
`{songId, start, end}` y activa el rango exacto validado por el motor. Release
lleva `{songId, commandId}` con el ID de la preparación o loop que esa vista
creó. El servidor comprueba peer, canción, ID y revisión antes de limpiar el
estado. Un cierre tardío, release duplicado o desconexión de un cliente anterior
no puede desactivar un loop nuevo de otro participante.

El snapshot publica `practiceLoopActive`, `practiceLoopPrepared`,
`practiceLoopStart`, `practiceLoopEnd`, `practiceLoopMinimumDuration`,
`practiceLoopError` y `practiceLoopCommandId`. Un cambio local conserva un ID
vacío para la propiedad remota. El mínimo base es 0.25 s; buffers grandes pueden
exigir un intervalo mayor. A/B debe estar dentro de Inicio/Fin y, en custom
arrange, dentro de un tramo source-continuo: no admite cortes de source
estrictamente interiores, aunque sí cortes exactamente en A o B.

El cliente conserva el rango enviado provisionalmente hasta el ack y un
snapshot posterior, con límite de dos segundos. Una preparación pendiente
solo concede propiedad de cleanup; no muestra un loop activo ni reduce el
rango de seek a cero. `setPracticeLoopRange` remoto devuelve si se encoló el
comando, mientras `wasPracticeLoopRequestRejected` y `getPracticeLoopError`
informan el rechazo posterior. Si B se rechaza, el cliente conserva la propiedad
de la preparación A anterior para poder liberarla al cerrar.

Los errores se publican con IDs estables: `invalid-range`, `too-short`,
`discontinuous-arrangement`, `buffer-not-ready` y `rejected`. Un fallo de
prefetch al límite B pausa allí y desactiva el loop; no reproduce audio fuera
del intervalo. La UI traduce el motivo. Stop vuelve a A cuando A/B está activo.
Los comandos de región/preparación no escriben ajustes de canción, setlist ni
Cloud. Tono continúa usando `pitch.set` y la persistencia normal del transporte.

El doble toque en Ensayo reutiliza `transport.seek` para saltar un compás.
El arrastre usa `transport.scrubBegin`, `transport.scrubEnd` y
`transport.scrubCancel`, con un token y la canción del gesto. Begin pausa una
sola vez y guarda si reproducía. La cámara sigue el dedo y, al soltar, conserva
una inercia proporcional a su velocidad reciente, con frenado suave de
180–1600 ms y límites de Inicio/Fin y loop sin rebote. Mantener el dedo quieto
100 ms antes de soltar elimina el impulso. Si el destino alcanza un límite,
la distancia restante acorta el frenado, con un mínimo de 180 ms.
El transporte permanece pausado
durante el arrastre y la inercia. End, cuando la onda deja de moverse, solicita
un único seek y reanuda solo si el token y la revisión de transporte siguen
vigentes. Tocar de nuevo interrumpe la inercia y conserva el token de la pausa
para continuar recorriendo la onda. Otro cliente o el servidor pueden invalidar
esa revisión con Stop, play/pause, seek o cambios de canción/ventana/arrange;
el gesto anterior no
sobrescribe esa intención. Cancel puede restaurar la pausa propia al cerrar o
solo descartarla cuando otra acción toma el control.

La previsualización durante el arrastre y la inercia es local y no llena la
cola con posiciones intermedias. El callback `finishScrub` devuelve `bool`:
en Cliente solo confirma el envío a la cola, no la aceptación del servidor.
Un `ack` de End rechazado se observa mediante `scrubFinishRejected`; ese rechazo
o un fallo inmediato de envío descartan el destino provisional y no recuperan
la propiedad de la pausa. Las vistas local y remota mantienen el destino
mostrado al menos 120 ms y esperan dos muestras consecutivas consistentes de
posición antes de liberarlo, con un timeout de respaldo de 1.5 segundos.
Si el transporte está reproduciendo, el cursor avanza según la velocidad
de reproducción; el desfase residual converge
suavemente hacia la posición recibida. Así no vuelve durante un bloque a una
posición anterior entre la solicitud y su aplicación. Un cambio de canción,
de modo o una desconexión cancela el gesto pendiente. Play/Pausa descarta esa
previsualización y Detener cancela también
el gesto y su confirmación visual antes de enviar el comando. El servidor sigue
aplicando los límites habituales de seek.

Tras liberar esa previsualización, `PracticePlaybackClock` presenta el cursor
con los deltas de frame y absorbe la cuantización de las posiciones recibidas
mediante una corrección gradual de 90 ms y tolerancia base de 10 ms. El avance
visual es monótono durante playback normal; un seek intencional, una
discontinuidad nueva de al menos 0.25 segundos respecto a la predicción
(incluido un loop), una pausa o cambio de canción reinician la estimación.
La transición parte del cursor presentado por el seek pendiente. Una fuente
congelada permite como máximo 250 ms de predicción, y una desconexión detiene
esa estimación. Este cursor no envía comandos adicionales, no reproduce audio
en el cliente ni modifica la posición autoritativa del servidor.

## Audio y validación

La red, JSON y cambios de UI quedan fuera del callback de audio. Para el click,
`getRemoteMetronomeMeterLevels()` consume un acumulador de pico independiente:
la publicación remota no roba transitorios a `getMetronomeMeterLevels()` local.
Ambos acumuladores se actualizan con atómicos en el mismo punto del render.

`remote_session` verifica dos clientes, descubrimiento, onda fragmentada,
comandos/ack y broadcast de VU. También prueba el adaptador con un procesador de
prueba: control remoto sin escritura local, orden de stems incluidos, posición
congelada y VU nulos tras desconexión, y retorno al estado local.
La reconexión repite el catálogo completo y los bloques de la misma onda: el
ensamblador debe reinicializar su lista de bloques incluso si el serial y el
número de bloques coinciden con la transferencia anterior ya completada.

Validación del 25/09/2026: compilaciones macOS, iOS Simulator y Android Debug
correctas; actualización de ambos emuladores conservando datos. En la prueba
de interfaz, macOS descubrió automáticamente al iPad Simulator servidor y
mostró la onda, portada, setlist y 15 stems de «Glorioso Día» en los componentes
habituales. Play, stop, posición, VU y volumen respondieron desde el cliente;
se restauró el volumen original y se dejó la reproducción detenida. La prueba
automatizada cubre dos clientes y reconexión; no equivale a validación en
dispositivos físicos ni a una prueba visual de Android como cliente.

Regresión de descubrimiento en iPhone físico: `remote_session` conecta usando
la dirección resuelta por Bonjour; `remote_session_portable` repite la prueba
con el backend mDNS de Android/Windows, y `remote_discovery_interop` verifica
descubrimiento y conexión WebSocket nativo → portátil y portátil → nativo.
Las tres pruebas pasaron tras reemplazar el multicast directo en Apple.

Referencia: [Apple TN3179 — privacidad de red local](https://developer.apple.com/documentation/technotes/tn3179-understanding-local-network-privacy).

## Canción en blanco libre

Una canción `blankSong` se controla con transporte, click, pads, tono y tempo
habituales. Su duración es cero/abierta y el cliente extrapola posiciones sin
recortarlas a cero. La zona de onda permanece vacía también en Cliente; para
este tipo no se muestra la etiqueta de conexión sobre ella (el estado sigue
disponible en Configuración). El snapshot del motor no contiene secciones
sintéticas. El servidor rechaza `arrange.*`, `window.*` y `tempoMap.set` para
este tipo, sin afectar sus controles de acompañamiento ni las canciones normales.
