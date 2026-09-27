---
type: Technical Contract
title: Popovers
description: Presentacion adaptativa y comportamiento comun de popovers JUCE
resource: src/ui/UiCommon.cpp
timestamp: 2026-09-09T17:45:00-03:00
---

# Popovers

Los popovers JUCE se presentan exclusivamente mediante
`presentActionPopover()`. El presentador mantiene una sola superficie activa:
abrir otra descarta primero la anterior, de modo que no se apilen callouts.

## Comportamiento

- En iPad y escritorio, el contenido se ancla al control de origen mediante una
  flecha de `12 pt`, se posiciona dentro del area segura y se cierra al elegir
  una accion, pulsar Escape o tocar fuera.
- El toque exterior no se consume. Esto permite cerrar un popover y abrir otro
  control con una sola interaccion.
- En una superficie tactil compacta, el contenido conserva un ancho adaptativo
  y una flecha de `10 pt` que apunta al control exacto que lo abrio. El
  posicionador puede colocarlo arriba, abajo o a un lado del origen para
  mantenerlo completo dentro del area segura.
- La clasificacion de telefono comparte `isPhoneSizedTouchLayout()` con el
  editor principal. Primero respeta la familia nativa iPhone/iPad y luego usa
  el lado corto normalizado por la escala del display. El popover y la vista no
  pueden elegir perfiles diferentes para el mismo dispositivo.
- Tanto la presentacion anclada como la compacta limitan el contenido al area
  segura disponible. El ancla se recorta al espacio visible y una ventana de
  escritorio pequena no permite que el popover se desborde fuera del editor.
- La aparicion usa un fade de `160 ms`. La superficie comparte radio de
  `14 pt`, borde interior tenue y dos niveles de sombra.
- El contenido usa una superficie lisa. No agrega franjas decorativas en los
  bordes; el color de acento queda reservado para controles y estados activos.
- El contenido recibe foco al terminar la presentacion asincrona.

Los componentes internos siguen siendo responsables de decidir si una accion
cierra inmediatamente la superficie o si permanece abierta para completar una
tarea con multiples selecciones. Cualquier edicion que pueda cerrarse tocando
fuera debe persistir sus cambios conforme se realizan; descartar cambios exige
una accion explicita de cancelar.

El selector de formato de exportacion de la waveform usa un popover de acciones
anclado al boton de archivo. Presenta WAV, MP3 y M4A/AAC en filas tactiles con
una breve descripcion y cierra la superficie antes de iniciar la exportacion.
No usa `PopupMenu`, para conservar la misma flecha, area segura y estilo en
telefono, tablet y escritorio.

En telefono, tocar la tarjeta de portada, titulo y artista del `TopBar` abre
el selector del setlist activo mediante `SongListPanel::showSongPicker()`,
anclado a la tarjeta completa. Reemplaza al popover informativo de la cancion
y al boton separado de nota musical. Muestra
numero, artwork de 32 pt, titulo y artista, con una marca en la seleccion actual.
Si falta la portada, muestra un icono musical; las imagenes se reutilizan durante
la apertura del selector. Tiene ancho de 280 pt, encabezado de 44 pt y altura
para hasta cinco filas de 48 pt; el presentador limita
ambas dimensiones al area segura y la lista permite scroll tactil. No ofrece
edicion ni reordenamiento. Elegir una fila cierra el popover antes de cargar
el proyecto por el mismo camino que la lista principal, resolviendo la cancion
por ID. Un setlist vacio muestra un mensaje sin acciones de carga.
El encabezado incluye un boton `+` con area tactil de 44 pt, disponible incluso
con el setlist vacio. Cierra el selector y abre el panel de Biblioteca y setlists;
en iPhone y telefonos Android este panel usa orientacion vertical y restaura
landscape al cerrarse.

En telefono, el boton Pads del `TopBar` abre el mismo presentador, anclado al
propio boton. El contenido usa un encabezado de 44 pt con titulo y cierre, y
adapta la cuadricula de 12 notas, el control de encendido, el preset y el strip
de volumen al espacio disponible. El marco exterior de `PadsComponent` se
omite dentro del callout para compartir su superficie y sombra. Se reutiliza
la misma instancia de Pads y su `PlaybackSession`: cerrar con `X`, Escape o
toque exterior solo retira la vista; no reinicia el motor ni corta una nota
sostenida. En tablet y escritorio permanece el cajon inferior de Pads.

## Integracion

La [hoja de versiones de ajustes](SettingsVersionsSheet.md) no usa este
presentador: es una tarjeta lateral plana, sin flecha, colocada junto a su
origen mediante una capa transparente. Consume el clic exterior para que el
cierre no active el mixer debajo. Es una elección con confirmación posterior,
no un panel de edición inmediata. Al activarla, el callout de acciones de la
canción permanece visible y su rectángulo se usa como ancla lateral; sólo esa
acción opta por no cerrar el callout al ejecutarse.

No llamar directamente a `juce::CallOutBox::launchAsynchronously()` desde un
panel. Los menus simples pueden continuar usando `juce::PopupMenu`, pero deben
evitar jerarquias en cascada cuando el contenido pueda resolverse dentro de una
sola superficie.

`ActionPopoverOptions` permite forzar `Anchored` o `CompactAnchored` cuando el
contenido requiera una presentacion concreta. El modo predeterminado
`Automatic` usa el tamano natural en iPad/escritorio y el ancho compacto en
telefono; ambos permanecen anclados al control de origen;
`compactMaximumWidth` y `edgeInset` permiten ajustar casos especiales sin
duplicar posicionamiento, area segura, apariencia ni reglas de cierre.
