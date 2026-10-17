---
type: Integration Contract
title: FreeShow
description: Marcas de letras independientes y control local de FreeShow en escritorio
tags: [freeshow, lyrics, rest, cues, desktop]
timestamp: 2026-10-17T00:00:00Z
---

# FreeShow · Letras de escritorio

La vista **Letras**, junto a Onda y Secciones, muestra el mapa completo de la
canción. Las secciones musicales son únicamente una referencia tenue. Cada
marca de letra tiene identidad propia y posición absoluta en el tiempo musical
del arreglo; no pertenece a una sección ni se desplaza al editar una sección.
No se añade esta función a iOS, Android ni al control remoto.

Los puntos conservan previews compactos visibles, dispuestos en filas para
separar etiquetas próximas. Seleccionar una marca amplía su detalle sin enviar
nada. Arrastrar cambia solamente su tiempo. El menú contextual permite añadir,
cambiar destino/tiempo, duplicar, desactivar y eliminar. Ocultar letras es un
destino explícito (`clear_slide`); no limpia fondo, audio ni overlays.
**Probar en FreeShow** es una acción explícita. El seguimiento durante la
reproducción requiere activar el control de letras de esa canción.

## Protocolo verificado

Fuentes primarias:

- [API oficial](https://freeshow.app/api)
- [Formato .show](https://freeshow.app/docs/format-show)
- [FreeShow v1.6.6](https://github.com/ChurchApps/FreeShow/tree/v1.6.6),
  commit `98b5e19956ef61fb9df1ca5ae361898def87f63d`, publicado el 9 de octubre de 2026.

El cliente usa REST HTTP POST en el puerto configurable (5506 por defecto),
con cuerpo JSON aplanado `{action, ...data}`. La clave opcional se envía en
`Authorization: Bearer …`. Permanece sólo en memoria. El endpoint Socket.IO
5505 no es un WebSocket genérico y no se utiliza.
El socket se abre directamente al destino local validado: no utiliza proxies
del entorno ni sigue redirecciones. El lector HTTP admite Content-Length y
chunked, limita cabeceras/cuerpo y tiene un plazo de lectura acotado.

`get_shows` proporciona IDs; `get_show` con `id` devuelve el show completo,
incluidos sus arreglos. `get_show_layout` recibe sólo `id` y devuelve el arreglo
activo, no un arreglo arbitrario. `get_groups` no determina el orden ni las
repeticiones de una presentación y no se usa para resolver destinos.

Cada arreglo se expande en orden: grupo padre seguido de sus slides hijas;
cada repetición vuelve a expandirse. `index_select_slide` recibe `showId`,
`layoutId` y el índice absoluto **basado en 1**. El estado de salida utiliza
índices basados en 0. El selector conserva el nombre literal del grupo,
pantalla, aparición e identificadores; «Coro 2», pantalla 2 y segunda aparición
son conceptos distintos. No se utilizan `next_slide` acumulativo, búsqueda
aproximada ni nombres como identidad.

Los destinos guardan IDs de show/arreglo/grupo/slide, número de aparición,
referencia textual y firma del arreglo. Antes del envío se valida el destino
contra el catálogo actualizado. Un cambio incompatible requiere revisión en
vez de proyectar silenciosamente otra pantalla. El arreglo activo se contrasta
con `get_show_layout` para detectar opciones de FreeShow que añaden una pantalla
vacía inicial y alteran los índices. Al proyectar hacia otro arreglo se cambia
primero el arreglo activo (sin proyectar), se consulta su orden real y se valida
de nuevo. Consultar el catálogo o seleccionar una marca no cambia el arreglo.

## Transporte y threading

El scheduler sigue los segmentos realmente recorridos por el callback de audio
y sus discontinuidades confirmadas. Un cue se emite una vez por pasada. Seek e
Ir aplicados recuperan solamente el cue vigente en el destino (el último
habilitado con tiempo menor o igual); no se envían las marcas omitidas. Loop
restablece el cue vigente en el inicio y permite recorrer las marcas otra vez.
Si el destino precede la primera marca habilitada, se oculta la letra anterior.
Una canción con el seguimiento apagado o sin marcas no envía esa limpieza.
Cambiar el tempo conserva el anclaje al mismo transporte musical.

El callback usa datos acotados y entrega eventos a la capa de control. No hace
HTTP, consulta de catálogos ni operaciones de disco. El cliente tiene un worker
propio, expiración de órdenes y cancelación por generación. Desconectar no
interrumpe el audio; reconectar no reproduce un historial pendiente.

Una respuesta HTTP de envío no prueba proyección. La interfaz distingue el
último destino enviado del confirmado mediante `get_output`. La confirmación
se limita al estado comunicado por FreeShow; no certifica un proyector físico.

## Persistencia y conexión

`ProjectSongSettings.lyrics` se conserva en el sidecar y en el `.pwproj` al
consolidar cambios. Las canciones legacy usan `ImportedSongData.lyrics`.
Canciones anteriores sin el campo se abren con un snapshot vacío. Los datos
inválidos se rechazan sin aplicar un snapshot parcial. Los cambios de mezcla,
tempo y arreglo deben conservar el snapshot de letras completo.

La conexión admite loopback y direcciones IPv4 privadas de LAN. No crea
servidores, credenciales persistentes ni reglas de red. Host, puerto y clave
son configuración de la sesión de escritorio, no del proyecto portable.

## Verificación

Las tablas admiten hasta 1024 marcas por canción. Si varios cues dejan de ser
vigentes antes de que el dispatcher pueda atenderlos, sólo se entrega el último
vigente: no se intenta recuperar pantallas atrasadas. Pausa no dispara cues por
sí sola. Un seek efectivo detenido puede restablecer el cue de su destino.

Las pruebas están en `freeshow_protocol_tests.cpp`,
`lyrics_cue_scheduler_tests.cpp`, `lyrics_processor_tests.cpp`,
`lyrics_persistence_tests.cpp` y `lyrics_panel_tests.cpp`.
La evidencia de ejecución, plataforma y límites se registra por entrega; una
prueba con transporte simulado no se presenta como ensayo de FreeShow real.
