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
- En una superficie tactil compacta, el mismo contenido se presenta junto al
  borde inferior del area segura, con ancho adaptativo y sin flecha visible,
  siguiendo el patron de sheet/action sheet de iPhone.
- La aparicion usa un fade de `160 ms`. La superficie comparte radio de
  `14 pt`, borde interior tenue y dos niveles de sombra.
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
