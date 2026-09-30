---
type: Technical Contract
title: Vista de ensayo por grupos
resource: src/ui/panels/RehearsalPanel.cpp
timestamp: 2026-09-29
---

# Ensayo por grupos

Ensayo permite escuchar un grupo instrumental frente al resto de la canción.
Se abre desde el botón Ensayo de la barra de escritorio o desde Más en pantallas
táctiles. La tarjeta de canción conserva el selector del setlist.

La vista comparte `TopBar`, `WaveformBar`, `PlaybackSession`, los faders de
`ChannelStripCard` y sus estilos. `RehearsalPanel` añade el selector de grupo,
dos faders, Mezcla normal y un encabezado con Mixer y Cerrar. En teléfono gira
a vertical; el transporte se reparte en tres filas. El contenido del panel
permite desplazamiento vertical cuando no caben los faders a su altura mínima.
El mixer detallado se abre con el botón del encabezado. En escritorio ancho
puede verse junto al balance de ensayo.

Biblioteca, Configuración y popovers conservan el ensayo al cerrar. Cerrar Ensayo
retira suavemente su balance temporal y recupera la orientación de performance.
Escape/Atrás cierra primero el mixer detallado y después Ensayo. Entrar/salir no
pausa, reinicia ni cambia la posición del transporte.

## Estado y audio

`RehearsalMix` contiene la identidad de canción, grupos y niveles temporales.
Los grupos usan categorías explícitas y IDs de stems incluidos. Las pistas
sin categoría o con Other quedan como opciones individuales. Click y guía se
excluyen con la misma clasificación que usa el mixer. No hay separación de
instrumentos dentro de un archivo mezclado.

Al elegir el primer grupo, su factor es 1 y el del resto es 0.25 (-12.04 dB).
Ambos faders admiten 0–1 respecto del nivel base de cada pista. Elegir otro grupo
conserva esos dos factores y recalcula su pertenencia; no acumula reducciones.
Mute, Solo, click, guía, master, pads y ruteo conservan su semántica. Los faders
individuales representan la mezcla base; los medidores incluyen la atenuación.

El audio toma una copia de máscaras y factores atómicos por bloque y aplica el
factor en `StemTrack`, suavizado durante 50 ms a la tasa del source. Por ello la
rampa acompaña el tempo efectivo. Las rutas directas, con stretch y con salidas
individuales usan el mismo factor antes de mezclarse. No hay locks ni asignaciones
adicionales en el callback de audio.

Mezcla normal, cambiar de canción y cerrar Ensayo eliminan el ajuste temporal.
Los controles normales del mixer conservan sus escrituras habituales: quitar el
factor de ensayo no deshace una edición explícita posterior del usuario.
El balance de ensayo no forma parte de los settings del proyecto, Cloud ni los
volúmenes de una exportación normal. El exportador crea sus fuentes desde la
mezcla base; sus `StemTrack` comienzan con factor de ensayo 1.

## Control remoto

El snapshot incluye `rehearsal.version = 1`, songId, revision, group, groupLevel,
restLevel y groups (id, name, stemIds y names). Es estado de sesión, separado del
objeto de settings. Los servidores anteriores no anuncian la capacidad y los
clientes deshabilitan sólo los controles de balance con un mensaje.

Los comandos `rehearsal.select`, `rehearsal.groupLevel`, `rehearsal.restLevel` y
`rehearsal.clear` llevan songId en target. El servidor rechaza otra canción,
cargas en curso, grupos desconocidos, valores no numéricos/no finitos y niveles
fuera de 0–1. Las actualizaciones de faders se agrupan sin cruzar una selección
de grupo ni otro comando discreto. El arrastre tiene la misma previsualización
local, confirmación y rollback que los faders normales.

Si se cierra Ensayo estando desconectado, el cliente recuerda el pedido de quitar
el balance. Al reconectar lo envía sólo si canción y revisión siguen iguales.
El servidor vuelve a comprobar la revisión para no deshacer cambios de otro
cliente. El audio remoto sigue en el servidor y la vista explica que el balance
es compartido. No se descargan stems ni se produce una mezcla local del cliente.

## Validación

`rehearsal` comprueba agrupación, exclusiones, cambio de grupo, restauración,
validación de comandos, reconexión y rampas reales de audio con Mute/Solo.
`stem_mix_engine` comprueba atenuación en rutas con tempo/pitch y salidas dedicadas.
Los previews de componentes reales pueden generarse con
`PLAYWORSHIP_REHEARSAL_SNAPSHOTS`, sin datos ni letras de canciones ajenas.
La compilación, esos renders y la ejecución en dispositivos se registran como
evidencias distintas.
