---
type: UI Contract
title: Vinculación de faders de instrumentos
description: Igualar el volumen de todos los instrumentos desde cualquier fader vinculado
resource: src/ui/panels/ChannelGridPanel.cpp
timestamp: 2026-10-31
---

# Vincular instrumentos

El canal Master ofrece un botón de faders enlazados junto a M, en su espacio
libre. Su fila M/vinculación usa el mismo ancho de botones, separación,
centrado y margen inferior que la fila S/M de los demás canales. Comparte el
fondo, borde y colores de estado de Mute. El icono
`assets/ui-icons/faders_linked.svg` usa el catálogo SVG y su caché compartida,
con tintado según el estado del botón. El botón activa un modo temporal del mezclador: no mueve ni modifica el
volumen, gain, mute ni ruteo del Master. No aparece una barra nueva sobre los
canales. La indicación es fija para no añadir repintados periódicos al mixer.

## Participantes y gesto

Los faders de instrumentos incluidos en la canción muestran un marco iluminado,
incluso cuando su volumen está en cero. Master, stems de clic/guía, Click Dyn,
Guía Dyn y Pads quedan fuera del grupo. Gain, Mute y Solo conservan su
funcionamiento individual.

Activar la vinculación no cambia ningún nivel. A partir del primer movimiento,
cualquiera de los faders iluminados establece su valor absoluto de volumen
(0..1) en todos los instrumentos elegibles. No se conserva la proporción ni la
diferencia entre sus niveles anteriores. Por ejemplo, llevar una guitarra a
cero baja todos los instrumentos a cero; subirla después a 0.5 coloca todos en
0.5. Una pista que ya estaba arriba no limita el recorrido de las demás: el
nuevo valor sustituye también su nivel.

Cada movimiento actualiza los controles y volúmenes de todos los participantes.
No se recuperan los balances anteriores al soltar el gesto o salir del silencio.
Una modificación externa de la mezcla se reconoce antes del siguiente gesto.
Un cambio de contexto durante un gesto lo invalida para no propagar niveles
de una sección anterior.

## Persistencia, automatización y ciclo de vida

Al terminar el gesto se entregan los niveles de todos los participantes al
flujo existente de persistencia por stemId. No se añade un formato de proyecto,
una propiedad de audio o un bus. Para automatización se usan los getters base
y setters habituales; las sustituciones manuales temporales siguen el contrato
de [SectionAutomation](../engine/SectionAutomation.md).

El estado de vinculación no se guarda como preferencia de la canción. Cargar o
reconstruir la canción, entrar en aprendizaje MIDI o iniciar edición de
automatización desactiva el modo. El Master mantiene sus asignaciones MIDI y su
control habitual. En clientes remotos el botón está deshabilitado e indica que
la vinculación está disponible en el mezclador local; no se introduce un nuevo
comando de red ni se amplía el protocolo.

## Verificación

La regresión integrada usa los controles reales del mixer, un processor y un
proyecto aislado. Comprueba diferentes faders como origen, igualdad de niveles,
pistas originalmente en cero, recorrido sin bloqueo por un participante que
estaba arriba, todos a cero y todos a medio nivel tras salir del silencio,
persistencia/reapertura y exclusión de Master/clic/guía/Pads. La validación de
componentes no acredita por sí sola una prueba táctil en un dispositivo físico.
