---
type: UI Contract
title: Automatización de secciones
description: Edición de valores de mezcla por sección con los controles existentes
resource: src/ui/panels/WaveformBar.cpp
tags: [automation, arrange, mixer, pads, remote, mobile]
timestamp: 2026-10-02
---

# Automatización de secciones

## Flujo

1. En la barra de una sección del mapa de guía, `Automatizar` abre su borrador.
   En teléfono también está en `Más`, para la sección del cursor actual.
2. La reproducción se pausa. La sección queda fijada por ID, aunque el usuario
   cambie la posición de reproducción para escucharla. No se reanuda al guardar.
3. Una barra flotante persistente en el borde superior de la sección muestra
   `A · nombre` y las acciones del borrador. En escritorio y tablet incluye
   `Cancelar`, `Listo` y `Más`; en teléfono, `Listo` y `Más`.
   Los channel strips instrumentales y Pads muestran un indicador `A` y un
   borde de edición; se reutilizan sus faders, ganancias, mute y notas.
   La sección fijada, el borde y el indicador `A` de sus canales y Pads pulsan
   suavemente con el color del tema, igual que el resaltado de un salto. El
   pulso usa tiempo de interfaz y continúa con el transporte pausado. Al
   terminar, cancelar o borrar, desaparece la animación; el `A` guardado queda
   fijo. Sólo se repintan las zonas animadas, con un máximo de 24 Hz (12 Hz en
   Android); los strips ocultos no solicitan repintados de animación.
4. `Listo` guarda los valores modificados. `Cancelar` o Escape descartan el
   borrador; en teléfono, `Más` incluye `Cancelar cambios`. En todas las
   plataformas, `Más` incluye `Borrar automatización`, que elimina el borrador
   y la automatización de esa sección.
5. Las secciones guardadas y sus controles automatizados muestran `A`.

Los valores guardados son estados finales dispersos: sólo se reemplazan los
parámetros tocados. No se graba la trayectoria del gesto ni una curva temporal.
Master, Solo, Click, Guía y ruteo conservan su funcionamiento manual. Solo queda
deshabilitado en los instrumentos durante la edición para evitar confundirlo
con un valor que se pueda guardar en una sección.

## Barra del borrador

La barra de edición se ancla al ID de la sección automatizada y permanece
visible hasta guardar, cancelar o borrar. Su posición se calcula a partir del
borde superior de esa sección; el cursor de reproducción, el puntero y el hover
no cambian el ancla. El desplazamiento, el zoom y los cambios de tamaño o área
segura recalculan su posición.

Mientras hay un borrador, la onda muestra el mapa de guía/secciones para
mantener el contexto de las secciones, incluso si la vista seleccionada era
el mapa de tempo. Este cambio de presentación conserva el `timelineViewMode`
guardado y restaura su mapa al terminar la edición. Los controles habituales
de edición de secciones se ocultan durante el borrador.

La barra se limita al viewport visible y al área segura de la plataforma para
mantener sus acciones accesibles. Si la sección queda fuera de vista, la barra
se mantiene en el borde visible correspondiente; su etiqueta `A · nombre`
incluye `←` o `→` para indicar la dirección de la sección que conserva el
borrador. El nombre y la flecha mantienen inequívoca la identidad de esa
sección aunque la barra se superponga a otra región de la onda.

Cuando la sección queda fuera de vista, `Más` incluye `Volver a la sección`.
Esta acción ajusta el viewport para mostrar la sección automatizada, conservando
la posición de reproducción y el estado del transporte. Las acciones del
borrador pertenecen a la barra flotante y a su menú `Más`; la toolbar global
dispone de todo su espacio, sin reservar una franja para la automatización.

Los botones táctiles conservan al menos 44 px de altura. El padding vertical
se adapta a las alturas compactas de onda de 108 y 140 px para mantener ese
objetivo dentro del área disponible.

Los faders y controles de Pads existentes conservan sus callbacks y su
comportamiento de mezcla; esta ubicación de las acciones no cambia el contrato
del motor de automatización.

## Integridad de edición

- El borrador no modifica el mix base ni sale en el snapshot comprometido.
- La navegación del minimap y los gestos móviles de desplazamiento y zoom
  mediante pinch siguen disponibles durante el borrador para recorrer la onda.
- Los gestos y atajos estructurales de la onda (mover, copiar, pegar, borrar,
  deshacer y rehacer) se bloquean mientras hay un borrador.
- En móvil, el tap sobre la onda para cambiar la posición de reproducción y
  el doble tap para saltar permanecen bloqueados durante el borrador.
- Guardar o borrar añade una entrada al historial del arrange; Deshacer puede
  restaurar la automatización anterior una vez cerrado el borrador.
- Copiar una sección copia sus valores por valor. Editar una copia no modifica
  la original. Los stems se identifican por ID persistente, no por posición.
- La persistencia usa el snapshot comprometido del procesador. El mix normal
  siempre se guarda a partir de los valores base, evitando filtrar el borrador
  o un estado automatizado hacia los ajustes generales de la canción.

## Reproducción y remoto

El contrato de selección por posición, cambios de mezcla y continuidad de Pads
se define en [SectionAutomation](../engine/SectionAutomation.md). Cada nueva
entrada a una sección aplica sus valores; un ajuste manual temporal termina al
salir. Los controles sin un valor de sección usan el mix base.

El servidor controla la propiedad del borrador. Sólo su autor puede cambiar
instrumentos y Pads hasta terminar o cancelar; las otras interfaces muestran
el estado y deshabilitan esos controles. Desconectar al autor cancela el
borrador. Master, Solo, Click, Guía y ruteo siguen disponibles.
Los comandos atrasados conservan la identidad de canción y borrador para no
alterar otra sección. Véase [Control remoto](../integrations/RemoteControl.md).

La misma implementación de componentes se usa en escritorio, tablet y móvil;
la barra adapta sus acciones al espacio disponible y el menú `Más` concentra
las acciones secundarias de edición.
