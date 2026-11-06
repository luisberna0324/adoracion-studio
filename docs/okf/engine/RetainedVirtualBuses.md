---
type: Engine Contract
title: Buses virtuales salientes durante crossfade
resource: src/audio/RetainedVirtualBuses.h
---

# Buses virtuales salientes

`RetainedVirtualBuses` conserva una instancia independiente del metrónomo y la
guía de la canción saliente. El control copia previamente los catálogos de PCM
inmutables mediante `prepareRetainedCopyFrom`; los samples comparten propiedad
para que reemplazar el catálogo entrante no invalide una voz saliente.

La captura en el límite de callback copia sólo estado de tamaño fijo:

- Fase musical, subdivisión, generaciones de reset y voces de click activas.
- Cursores/resampling de las ocho voces de guía, snapshot de secciones/cues y
  flags de anuncios ya emitidos.
- Mute/solo efectivo, ganancias, BPM original/efectivo y rutas de cada bus.

Las dos líneas de compensación de latencia intercambian sus buffers preparados
con las líneas principales, conservando la historia exacta sin copiar el ring
ni reservar memoria dentro del callback. Las líneas de reemplazo se limpian en
control antes de una nueva captura.

El render recibe el span real de frames fuente de cada bloque. El click usa su
propia fase y BPM; la guía mantiene la velocidad natural de la voz y dispara
anuncios futuros/cues según la línea temporal saliente. Ambos buses atraviesan
la compensación de latencia del DSP saliente. Sus destinos mono/estéreo se
normalizan por `StemOutputRouter` contra la salida real.

El renderer agrega sus buses antes del master y la envolvente de crossfade del
deck saliente, que se aplican una sola vez. El callback principal configura y
reproduce los buses entrantes de manera independiente. Los pads siguen siendo
un bus global, no una tercera copia de estos generadores.
