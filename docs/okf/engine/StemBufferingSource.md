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

En el pipeline actual de `StemTrack`, el source upstream ya está resampleado:

```text
AudioFormatReaderSource
        ↓
ResamplingPositionableSource
        ↓
StemBufferingSource
        ↓
callback de audio
```

## Contrato realtime

`getNextAudioBlock()` no espera al worker y no realiza I/O ni asignaciones:

1. Captura una sola posición inicial para el bloque.
2. Intersecta el bloque pedido con un snapshot coherente del rango válido.
3. Inicializa toda la región activa con silencio.
4. Si existen frames válidos, intenta el lock corto del ring con `tryEnter()`.
5. Copia únicamente los frames disponibles.
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

Sin un seek concurrente, la posición interna es monotónica incluso con looping.
La API
`getNextReadPosition()` aplica módulo por el largo del source cuando corresponde;
esto permite que un callback vacío o parcial mantenga sincronizado el transporte
con los demás stems.

## Diagnóstico de underruns

```cpp
juce::int64 getRealtimeMissCount() const noexcept;
juce::int64 getRealtimeMissingFrameCount() const noexcept;
```

- `getRealtimeMissCount()` cuenta una vez cada callback que no pudo entregar
  todos los frames solicitados.
- `getRealtimeMissingFrameCount()` acumula la cantidad exacta de frames que se
  sustituyeron por silencio.
- Una contención del lock corto cuenta todos los frames del bloque como
  faltantes, porque no se copia audio parcial sin proteger el ring.
- Los contadores son atómicos, acumulativos durante la vida de la instancia y
  sus getters no bloquean.

El silencio normal de EOF que el worker ya publicó dentro de un rango válido no
es un miss: desde la perspectiva del callback esos frames sí estaban
disponibles. Si todavía no existe un rango publicado para la posición pedida,
los frames se contabilizan como no disponibles.

## Worker de read-ahead

El worker carga como máximo 32768 frames por pasada:

1. Lee el source upstream a un buffer de staging sin mantener `callbackLock`.
2. Toma el lock sólo para copiar el staging al ring compartido.
3. Publica `bufferValidStart`, `bufferValidEnd` y el epoch como una unidad
   coherente después de completar la copia.
4. Señala `bufferReadyEvent` para consumidores no-realtime que estén esperando
   el prebuffer inicial.

`waitForNextAudioBlockReady()` puede esperar y sólo debe invocarse fuera del
audio thread, por ejemplo antes de publicar una canción nueva.

## Seek

`setNextReadPosition()` incrementa primero el epoch exclusivo del cursor y luego
publica la nueva posición, sin esperar al worker. El callback sólo avanza
mediante CAS la posición y el epoch de cursor que capturó al comenzar: si un
seek ocurre durante el bloque, no suma `info.numSamples` al destino nuevo.

Después de publicar la posición, el seek incrementa por separado el epoch del
read-ahead. Este orden garantiza que un worker que observa el epoch nuevo
también observa el destino nuevo. El worker es el único escritor del par
start/end y nunca puede volver a publicar una lectura anterior bajo el epoch
nuevo. Mientras se completa el read-ahead, el callback mantiene el contrato de
silencio y métricas; la posición solicitada por el seek tiene prioridad sobre
el avance del bloque anterior.

## Archivos relacionados

- [StemTrack](StemTrack.md) - Owner del source de buffering
- [StemMixEngine](StemMixEngine.md) - Consumidor agrupado de stems
- [TransportClock](TransportClock.md) - Reloj de frames que debe permanecer
  alineado durante misses
