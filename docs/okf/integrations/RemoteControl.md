---
type: Integration Contract
title: Control remoto LAN
description: La interfaz existente usa una sesión local o una réplica por WebSocket
authority: src/network/PlaybackSession.h
tags: [remote, websocket, mdns, transport, mixer, waveform]
timestamp: 2026-09-25T00:00:00-03:00
---

# Control remoto LAN

Configuración → General contiene el selector segmentado **Local | Cliente |
Servidor**. Local es el valor al iniciar. El descubrimiento, selección de servidor,
conexión y errores detallados se muestran dentro de Configuración. Cliente
añade solo una etiqueta pequeña pintada en la esquina superior derecha de la
onda; no altera su distribución ni intercepta gestos. Muestra `REMOTO` en verde
tras el primer snapshot, `BUSCANDO` antes de elegir servidor, `CONECTANDO` en el
primer intento y `RECONECTANDO` en ámbar si se pierde una sesión ya activa. Se
ve también con la onda vacía.
Local y Servidor no muestran esa etiqueta.
En la ventana compacta del iPhone, el selector y el estado de descubrimiento
ocupan la primera fila de General para que aparezcan sin desplazamiento.

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
  anuncia `localhost`. Configuración → General permite escribir un alias
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
