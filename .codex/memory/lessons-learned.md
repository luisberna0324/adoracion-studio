# Lessons learned

This file is the long-term learning log for future Codex conversations in this
repository. Update it when a task teaches something reusable.

## How to use this file

- Read it at the start of every new conversation.
- Add short entries only when they affect future decisions.
- Prefer durable lessons over task history.
- If an entry becomes outdated, edit or remove it.
- Keep each lesson actionable: what to do, when to do it, and why.

## Current lessons

Las marcas de letras de FreeShow son independientes de las secciones musicales.
Conservar sus IDs, aparición y referencia del arreglo; nunca deducir pantallas
por nombres ni avanzar mediante `next`. Verificar el protocolo en una versión
publicada: las cachés de `main` pueden diferir. `get_show_layout` sólo describe
el arreglo activo y puede incluir una pantalla vacía sintética inicial.

Inicializar eventos sintéticos de JUCE con `{}`. `MouseWheelDetails` en JUCE
8.0.12 deja campos sin inicializar; fijar sólo `deltaX` o `deltaY` puede hacer
que una prueba lea el otro eje o flags indefinidos y cambie con el orden de ejecución.
