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

## Integracion

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
