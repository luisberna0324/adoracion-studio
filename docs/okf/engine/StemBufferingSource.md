---
type: Audio Source Class
title: StemBufferingSource
description: Read-ahead circular no bloqueante para reproducción de stems
resource: src/StemBufferingSource.h
tags: [juce, audio, stem, streaming, read-ahead, realtime]
timestamp: 2026-07-25T00:00:00Z
---

# StemBufferingSource

`StemBufferingSource` adapta un `PositionableAudioSource` a reproducción con
read-ahead. El I/O y el resampling del source upstream ocurren en un
`TimeSliceThread`; el callback de audio sólo consulta el rango publicado y
copia desde un ring buffer preasignado.

En el pipeline actual de `StemTrack`, cada banco recibe su propia cadena ya
resampleada:

```text
reader principal                    reader de prefetch
       ↓                                   ↓
AudioFormatReaderSource             AudioFormatReaderSource
       ↓                                   ↓
ResamplingPositionableSource        ResamplingPositionableSource
       └───────────────┬───────────────────┘
                       ↓
              StemBufferingSource
                       ↓
               callback de audio
```

Además del ring principal, cada instancia prepara un banco secundario compacto
de al menos 32768 frames. Este banco conserva un destino futuro de seek, loop o
jump sin invalidar la ventana que todavía está sonando. El reader y el
resampler secundarios son instancias independientes: posicionar o calentar el
destino futuro no mueve el cursor principal ni altera el estado de su filtro.

## Contrato realtime

`getNextAudioBlock()` no espera al worker y no realiza I/O ni asignaciones:

1. Captura una sola posición inicial para el bloque.
2. Intersecta el bloque pedido con snapshots coherentes del ring principal y
   del banco secundario.
3. Inicializa toda la región activa con silencio.
4. Si existen frames válidos, intenta el lock corto del ring con `tryEnter()`.
5. Copia la unión de los frames disponibles en ambos bancos. Ante un
   solapamiento, el ring principal tiene precedencia y el banco secundario sólo
   completa huecos.
6. Avanza condicionalmente la posición capturada exactamente
   `info.numSamples`; un seek concurrente conserva intacto su destino.

Si el lock está ocupado, el rango está vacío o sólo hay disponibilidad parcial,
los frames no disponibles permanecen en silencio. El callback nunca hace una
espera para recuperarlos.

`bufferValidStart` y `bufferValidEnd` se publican con una secuencia par/impar.
El callback realiza un único intento de lectura: si observa al worker en mitad
de una publicación, entrega silencio en vez de girar. El epoch de read-ahead
invalida de inmediato el rango anterior durante un seek. Si ese epoch cambia
durante la copia corta, se descarta el bloque para no exponer audio de la
posición vieja.

El banco secundario tiene su propio lock corto, secuencia y generación. El
callback también usa `tryEnter()` en este banco: nunca espera a que termine una
copia del worker. Si cambia la generación capturada durante el bloque, descarta
esa mezcla en vez de combinar dos destinos.

Sin un seek concurrente, la posición interna es monotónica incluso con looping.
La API
`getNextReadPosition()` aplica módulo por el largo del source cuando corresponde;
esto permite que un callback vacío o parcial mantenga sincronizado el transporte
con los demás stems.

## Diagnóstico de underruns

```cpp
juce::int64 getRealtimeMissCount() const noexcept;
juce::int64 getRealtimeMissingFrameCount() const noexcept;
juce::int64 getRealtimeLockMissCount() const noexcept;
```

- `getRealtimeMissCount()` cuenta una vez cada callback que no pudo entregar
  todos los frames solicitados.
- `getRealtimeMissingFrameCount()` acumula la cantidad exacta de frames que se
  sustituyeron por silencio.
- Una contención del lock corto cuenta todos los frames del bloque como
  faltantes, porque no se copia audio parcial sin proteger el ring.
- Los contadores son atómicos, acumulativos durante la vida de la instancia y
  sus getters no bloquean.
- `getRealtimeLockMissCount()` cuenta intentos fallidos de `callbackLock.tryEnter()`
  en el ring principal. El prefetch puede cubrir esos frames; este contador
  distingue contención de falta de datos sin convertirla en una espera.

El silencio normal de EOF que el worker ya publicó dentro de un rango válido no
es un miss: desde la perspectiva del callback esos frames sí estaban
disponibles. Si todavía no existe un rango publicado para la posición pedida,
los frames se contabilizan como no disponibles.

## Worker de read-ahead

El mismo `TimeSliceClient` atiende primero una solicitud pendiente del banco
secundario y después continúa el ring principal, pero cada operación lee desde
su propia cadena reader/resampler. Una solicitud se publica con campos atómicos
y una generación `release/acquire`; el callback es su único productor durante
reproducción.

El worker carga como máximo 32768 frames por pasada:

1. Lee el source upstream a un buffer de staging sin mantener `callbackLock`.
2. Toma el lock sólo para copiar el staging al ring compartido.
3. Publica `bufferValidStart`, `bufferValidEnd` y el epoch como una unidad
   coherente después de completar la copia.
4. Señala `bufferReadyEvent` para consumidores no-realtime que estén esperando
   el prebuffer inicial.

Cuando no hay trabajo vuelve a consultarse en 10 ms. El callback no usa
`moveToFrontOfQueue()` al hacer seek, porque esa operación de JUCE sincroniza la
lista interna del `TimeSliceThread`.

`waitForNextAudioBlockReady()` puede esperar y sólo debe invocarse fuera del
audio thread, por ejemplo antes de publicar una canción nueva.

## Seek

```cpp
void requestPrefetchPosition(juce::int64 position,
                             int minimumFrames) noexcept;
bool isPrefetchReady(juce::int64 position,
                     int minimumFrames) const noexcept;
```

`requestPrefetchPosition()` prepara un destino sin tocar `nextPlayPos` ni el
ring actual. La lectura continúa desde la ventana audible mientras el worker
llena el banco secundario. `isPrefetchReady()` sólo responde `true` cuando el
rango solicitado pertenece a la misma generación publicada.

Antes de leer el rango publicable, el worker posiciona la cadena secundaria
hasta 256 frames antes del destino y descarta ese preroll. Así calienta el
filtro del resampler independiente sin exponer su transitorio de arranque. Si
el destino está a menos de 256 frames del inicio, el preroll se limita a cero.

`setNextReadPosition()` incrementa primero el epoch exclusivo del cursor y luego
publica la nueva posición, sin esperar al worker. El callback sólo avanza
mediante CAS la posición y el epoch de cursor que capturó al comenzar: si un
seek ocurre durante el bloque, no suma `info.numSamples` al destino nuevo.

Después de publicar la posición, el seek incrementa por separado el epoch del
read-ahead principal. Este orden garantiza que un worker que observa el epoch
nuevo también observa el destino nuevo. El worker es el único escritor del par
start/end y nunca puede volver a publicar una lectura anterior bajo el epoch
nuevo. Si el destino se prearmó, el primer bloque posterior se sirve
inmediatamente desde el banco secundario mientras el ring principal se
reposiciona; de lo contrario rigen el silencio y las métricas normales.

Cuando `setNextReadPosition()` aterriza sobre un destino ya prefetcheado, esa
generación queda pinned. Mientras el ring principal no cubra el próximo bloque
desde el cursor actual, nuevas llamadas a `requestPrefetchPosition()` se
ignoran para no sobrescribir el banco que está sirviendo el handoff post-seek.
El pin se libera cuando el ring principal vuelve a cubrir el bloque completo.

## Archivos relacionados

- [StemTrack](StemTrack.md) - Owner del source de buffering
- [StemMixEngine](StemMixEngine.md) - Consumidor agrupado de stems
- [TransportClock](TransportClock.md) - Reloj de frames que debe permanecer
  alineado durante misses
