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
separar etiquetas próximas. Seleccionar una marca la resalta sin enviar nada;
no hay panel inferior que repita la letra. Arrastrar desde el punto o cualquier parte de la tarjeta conserva el punto de agarre y cambia solamente su tiempo al soltar. Durante el arrastre las otras tarjetas mantienen su posición y se permite la superposición; al soltar se recalculan las filas y se anima el reacomodo durante 220 ms.
El menú de una marca conserva Cambiar destino FreeShow, Probar en FreeShow y
Eliminar marca. Ocultar letras es un
destino explícito (`clear_slide`); no limpia fondo, audio ni overlays.
**Probar en FreeShow** es una acción explícita. El seguimiento durante la
reproducción requiere activar el control de letras de esa canción.

La cabecera compacta vincula la canción al Show y no muestra selector de arreglo
ni un botón superior de añadir. Show muestra solo el nombre, con un ancho máximo
de 220 puntos; no añade IDs, texto contiguo ni ayuda bajo el selector. Conserva
Actualizar catálogo, Seguir reproducción y el badge de conexión. La creación usa el puntero compartido con la
onda de guía: colocar tiempo en regla/mapa, `+` contextual, grupo y diapositiva
en menús en cascada. El clic secundario sobre un espacio vacío de la regla o del mapa coloca el puntero en ese tiempo y abre directamente el mismo selector de grupos y diapositivas (clic derecho en Windows y clic secundario con dos dedos configurado en macOS). Sobre una marca conserva su menú de edición. No se abre un buscador ni un panel de diapositivas.
El menú contiene cada grupo del show una sola vez, con sus diapositivas hijas;
elegir de nuevo un destino crea otro punto temporal independiente.

El clic primario sobre la regla salta al tiempo señalado e inicia la reproducción mediante PlaybackSession; limpia el puntero punteado de inserción. El clic primario en el área de letras conserva el puntero punteado para añadir marcas. El clic secundario conserva el selector contextual.

La regla utiliza `TimelineRulerStyle` y las marcas de `WaveformBar`, por lo que
comparte compases, métrica y mapa de tempo con la onda. No hay botones de zoom.
La rueda vertical sobre la regla o el mapa amplía/aleja bajo el cursor; el
desplazamiento horizontal del mouse o trackpad recorre la vista. Ambos usan
`TimelineNavigationGestures`, con la misma dirección, sensibilidad y clasificación
diagonal que Onda. No requieren modificadores; Shift, Ctrl, Cmd y Alt no cambian
la interpretación. Se usan los deltas recibidos de JUCE sin volver a invertirlos
por `isReversed`. Predomina el desplazamiento horizontal cuando
`abs(deltaX) > abs(deltaY) * 1.05`; en los demás casos se amplía/aleja con `deltaY`.
El pellizco conserva la transformación existente de Onda: factor `1 + amount`,
con entrada cero sin efecto. No se reinterpreta como un factor nativo neutro en 1.
Alejar hasta 1× devuelve la canción completa; el máximo compartido es 18×.
Las barras de desplazamiento se conservan. El ancho temporal excluye los márgenes
del mapa, de modo que pan y zoom usan la misma proporción visible que Onda.
Ambas vistas comparten el cálculo del intervalo visible. Letras conserva las
fracciones de desplazamiento y compensa el scroll entero del viewport al dibujar
y resolver hits; secuencias pequeñas del trackpad no pierden movimiento.
Mientras se arrastra un punto o el puntero contextual, rueda y pellizco no cambian
la vista; después el `+` sigue ligado al mismo segundo. Zoom y desplazamiento son estado
visual: las marcas mantienen segundos absolutos e identidad; no cambian tiempos
de sección, transporte ni proyección.

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

Las marcas anteriores guardan IDs de show/arreglo/grupo/slide, número de aparición,
referencia textual y firma del arreglo. Antes del envío se valida el destino
contra el catálogo actualizado. Un cambio incompatible requiere revisión en
vez de proyectar silenciosamente otra pantalla. El arreglo activo se contrasta
con `get_show_layout` para detectar opciones de FreeShow que añaden una pantalla
vacía inicial y alteran los índices. Al proyectar hacia otro arreglo se cambia
primero el arreglo activo (sin proyectar), se consulta su orden real y se valida
de nuevo. Consultar el catálogo o seleccionar una marca no cambia el arreglo.

Las marcas nuevas guardan `useActiveLayout: true` y los IDs estables de Show,
grupo y diapositiva. Antes de cada envío se vuelven a consultar Show y orden
activo; se resuelve la primera aparición de esa diapositiva dentro del arreglo
activo validado y se envían explícitamente su `layoutId` e índice. Repetir un
punto vuelve a señalar esa misma diapositiva, sin navegar por apariciones.
No se omite `layoutId`, no se reutiliza el índice de otro arreglo, no se cambia
el arreglo activo ni se crea/reescribe un arreglo del Show para estas marcas.
Un grupo no incluido en el arreglo activo puede elegirse y guardarse, pero su
envío se bloquea con aviso de revisión mientras falte allí. Las marcas antiguas
mantienen su arreglo, aparición, firma y validación exacta; no hay migración
automática. El nuevo campo es opcional y su ausencia conserva esa semántica.

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

`ProjectSongSettings.freeShowCues` se conserva en el sidecar y en el `.pwproj` al
consolidar cambios. Las canciones legacy usan `ImportedSongData.freeShowCues`.
La clave JSON `freeShowCues` pertenece únicamente a las marcas de proyección.
`ImportedSongData.lyrics` y la clave `lyrics` conservan el documento nativo de
texto y acordes; editar cualquiera de los dos no reemplaza el otro.
Las marcas de la rama FreeShow anterior que usaban `lyrics` sólo se reconocen
por la forma de su modelo, sin interpretar el documento nativo como proyección.
Canciones anteriores sin el campo se abren con un snapshot vacío. Los datos
inválidos se rechazan sin aplicar un snapshot parcial. Los cambios de mezcla,
tempo y arreglo deben conservar tanto las marcas como la letra y sus acordes.

La conexión admite loopback y direcciones IPv4 privadas de LAN. No crea
servidores, credenciales persistentes ni reglas de red. Los controles se alojan
en Configuración → Integraciones → FreeShow. Abrirlos no cambia la conexión ni
guarda valores; Guardar y probar valida y aplica explícitamente el destino.
Host y puerto se guardan en `AppSettings` de este computador, con los valores
predeterminados `127.0.0.1:5506`. Al arrancar se restaura el destino sin conectar
automáticamente. El token sigue solo en memoria. Ninguno entra en `.pwproj`.

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
