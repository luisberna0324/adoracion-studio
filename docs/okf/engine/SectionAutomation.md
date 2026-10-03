---
type: Audio Runtime Contract
title: Section Automation
description: Mezclas parciales por sección y edición con controles existentes
resource: src/audio/SectionAutomation.h
tags: [audio, arrange, automation, mixer, pads, realtime]
timestamp: 2026-10-03T00:00:00Z
---

# Automatización de secciones

Cada `SongArrangementPartSnapshot` puede conservar una `automation` parcial.
Los ajustes pertenecen al ID de la sección y al `stemId` estable del instrumento;
no al índice visual del channel strip. Copiar una sección copia sus valores y
moverla conserva su automatización. Los datos se guardan con el arrange normal,
independientemente de `isCustomArrangementActive()`.

## Base, automatización y edición manual

La mezcla base mantiene los controles habituales de la canción. Una propiedad
opcional de automatización sustituye sólo esa propiedad mientras la posición
pertenece a la sección `[startSec, endSec)`. Los instrumentos restantes y las
propiedades ausentes continúan usando la base. Una posición entre secciones o
fuera de sus límites recupera la mezcla base sin exigir la reproducción de las
secciones anteriores. Excepción de Pads: cuando la canción contiene algún
preset de Pads, una sección sin campos de Pads o un hueco significa apagado
(`enabled=false`, `note=-1`), sin resucitar la nota global. Esta regla se compila
en el mismo banco/secuencia del timeline; no agrega datos al snapshot guardado.
Las canciones sin automatización de Pads conservan su control global normal.
Al descargar una canción controlada por automatización de Pads, una canción
siguiente sin preset no reactiva el pad base a través del seguimiento tonal.
Un gesto explícito de nota o de encendido vuelve a habilitar el control manual.

Los stems admiten `volume` (0..1), `gain` (0..2) y `mute`. Los stems identificados
como click/guía quedan fuera de esta modalidad. Solo, master, ruteo, click y guía
dinámicos conservan su control habitual. Pads admite `volume` (0..1), `gain`
(0..1.2), `enabled`, `noteIndex` (-1 para apagar, 0..11 para una nota) y `octave`
relativa (-3..3; el selector actual usa -2..2).

Modificar un control automatizado fuera de la edición crea una sustitución
manual temporal para la sección actual; no sobrescribe la base ni el preset.
Entrar en otra sección o en un espacio sin sección invalida esa sustitución:
volver a la sección anterior no la recupera. Un loop que permanece en el mismo
ID conserva el ajuste manual hasta entrar en otra sección.

Los getters habituales (`getStemVolume`, `getStemGain`, `isStemMuted`, getters
de Pads) reflejan los valores efectivos. La persistencia de mezcla general
usa `getStemBaseVolume`, `getStemBaseGain`, `isStemBaseMuted` y los getters base
de Pads. El seguimiento automático de tonalidad de canción actualiza la nota
base y respeta una nota programada para la sección; no crea un override manual.

## Borrador fijado a una sección

`beginSectionAutomationEditing(partId)` valida el ID, rechaza otro inicio con
un borrador abierto y pausa el transporte. El destino queda fijado aunque la
posición cambie. Los setters existentes actualizan campos atómicos del borrador
para previsualizar la mezcla sin modificar su snapshot persistido. Sólo se
incorporan las propiedades ajustadas y las que ya tenía la sección.

`finishSectionAutomationEditing(true)` publica los valores del borrador en la
sección, conserva los ajustes de instrumentos actualmente excluidos o ausentes
y mantiene el estado pausado. `false` descarta el borrador y recupera la mezcla
previa, incluyendo una sustitución manual que existía al empezar. La publicación
normal del arrange, una carga/unload, cerrar el editor o cambiar de contexto
cancela la edición. `clearSectionAutomation(partId)` elimina el preset y su
borrador si es la sección editada; no permite eliminar otra sección mientras
hay un borrador fijado.

`getSongArrangementSnapshot()` devuelve el arrange original y su automatización
confirmada, con sus IDs y orden originales. Nunca expone valores de un borrador.
La UI guarda este resultado a través de su flujo habitual de persistencia e
historial; el motor no escribe archivos.

## Publicación y callback de audio

`setSectionAutomationStemIds` recibe los IDs en el mismo orden de stems cargados,
antes de configurar el arrange. El hilo de control compila rangos y valores en
una tabla acotada: 256 secciones y 64 stems. La publicación usa tres bancos
atómicos con una secuencia por banco. La adquisición de audio intenta como máximo
tres lecturas coherentes y conserva su selección previa ante contención. No
reserva memoria, modifica vectores, realiza I/O ni toma locks de publicación.

El callback selecciona la mezcla a partir de la posición efectiva del
`TransportClock`, después de aceptar un seek. Enumera todos los inicios y finales
que atraviesa el bloque, incluidos espacios entre anotaciones. Los eventos
pertenecen al mismo banco y secuencia que la selección inicial. Un handoff de
loop/jump enumera primero el prefijo original y después el tramo del destino.
Un callback largo puede aplicar varias transiciones sin esperar al siguiente
refresco de UI. Cada stem tiene capacidad fija para 1025 eventos, suficiente
para los dos tramos y los límites de las 256 secciones.

Cada `StemTrack` cambia volumen/trim/mute en el frame fuente del evento y suaviza
su ganancia resultante durante 30 ms en ese dominio. El gate de mute/skip de una
sección se resuelve para el destino, no se hereda del prefijo. La ganancia de
Pads se aplica por voz, antes de sumarlas. Un cambio de sección usa la duración
de crossfade configurada (1/3/5 s); un fader manual conserva la rampa de 30 ms.
Una voz saliente fija su nivel instantáneo al iniciar su apagado: restaurar la
base o introducir una nota más fuerte no aumenta su cola. Si la misma nota
continúa, su nivel cambia gradualmente sin reiniciar fase ni ataque. Mute sigue
actuando sobre el bus completo con rampa de 30 ms. Las ganancias ya estables se
aplican por bloque para mantener el costo habitual de mezcla.

## Pads, prefetch y continuidad

Pads se reconcilia dentro del `try-lock` acotado que ya protege su render. Una
nota cuyo índice y octava coinciden con la voz activa conserva fase y envolvente:
volver a entrar, repetir una sección o recibir el mismo snapshot no vuelve a
lanzar su ataque. Un cambio de nota usa el crossfade normal del motor; `-1` es
una orden explícita de apagado.

Cargar o buscar una sección con el transporte detenido/pausado muestra sus
valores, pero no inicia una nota automatizada nueva mientras el motor de Pads
esté silencioso. Play permite iniciarla. La edición permite escucharla como
preview y los gestos manuales/MIDI de Pads siguen pudiendo lanzarla. Una voz ya
activa conserva su continuidad al pausar. Los Pads permanecen independientes
del recorte exacto del bus de canción en Fin.

El hilo de control y el worker preparan las notas fuera del callback. Audio
publica un bit de carga si una nota no está lista y reintenta el estado vigente
cuando llega el buffer; nunca decodifica ni espera al worker. El worker no puede
resucitar una petición manual pendiente que ya perdió su vigencia tras un seek,
una cancelación o el cambio de sección.

## Propietario remoto y MIDI

El servidor mantiene el motor y el snapshot confirmado; los clientes operan los
mismos controles y reciben su estado efectivo. La sesión remota fija el cliente
propietario de un borrador. `setSectionAutomationRemoteOwned(true)` impide que
MIDI físico en el servidor escriba volumen/ganancia/mute de instrumentos o Pads
en ese borrador. También descarta comandos de power/notas pendientes. Los
controles excluidos, Solo y master siguen disponibles. El propietario remoto
usa los setters habituales; terminar, cancelar, desconectar o cargar otra canción
libera ese ownership. Un borrador iniciado localmente permite capturar cambios
MIDI de los controles elegibles.

## Exportación

El bounce de mezcla usa su renderer offline y el snapshot confirmado de arrange;
no reutiliza el borrador ni overrides manuales temporales del procesador.
Su aplicación de automatización debe mantener los mismos límites inclusivos de
inicio/exclusivos de final y la mezcla base en los huecos. La validación y las
reglas del renderer se documentan en [Song Mix Export](../audio-systems/SongMixExporter.md).

## Validación

`section_automation` cubre publicación concurrente sin generaciones mezcladas,
selección directa e intervalos, edición parcial, Save/Cancel/Clear, rechazo de
un segundo Begin, preservación de IDs ausentes, exclusión de click, restitución
de base, expiración A→B→A del override manual, nota/octava de Pads, seguimiento
de tonalidad base y transiciones de audio sin timer de UI. También verifica
varios eventos en una sola lectura, un callback de 8192 frames que atraviesa
una sección y un hueco, y la continuidad de Pads al reentrar en secciones.
Un loop real y un salto en cola dividen un callback en prefijo/sufijo: se valida
que el loop del mismo ID conserva el override manual de volumen/ganancia y que
el mute de la sección de origen no silencie la mezcla del destino del salto.
El test usa las generaciones de seek y la disponibilidad real del banco de
lectura para esperar el prefetch fuera del callback. Una regresión de
`PadsComponent::followSongKey` valida el flujo de carga: una nota automatizada
no convierte una base silenciosa en nota activa, el seguimiento conserva la
octava global aunque la sección tenga otra y el cliente no emite comandos de
seguimiento tonal hacia el servidor.

## Regresión de transiciones de Pads (3 de octubre)

Las pruebas nuevas verifican que una cola de nivel bajo no aumenta al entrar
una nota de nivel alto ni al apagarse en una sección sin Pads, que llega a
silencio, que una misma nota ajusta su nivel sin saltos y que el fader manual
conserva su respuesta corta. La integración cubre sección/hueco sin preset,
descarga de canción automatizada, seguimiento de tono sin reactivación y nuevo
disparo manual. Compilación macOS Standalone correcta y CTest `section_automation` + `pads_engine`: 2/2 correctos. La escucha manual en dispositivo físico no está acreditada.
