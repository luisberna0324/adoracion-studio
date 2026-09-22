---
type: Technical Contract
title: Input Controls
description: Apariencia y comportamiento compartido de campos de texto y selectores adaptativos
resource: src/ui/UiCommon.cpp
timestamp: 2026-09-21T00:00:00-03:00
---

# Input Controls

Los campos de texto y selectores de una sola opcion comparten un contrato
visual y de interaccion en macOS, iOS, Android, Windows y Linux. La base vive
en `UiCommon`: un solo `InputControlLookAndFeel`, `applyTextEditorTheme()` y
`applyComboBoxTheme()`.

La adaptacion sigue las Human Interface Guidelines de Apple para
[Text fields](https://developer.apple.com/design/human-interface-guidelines/text-fields),
[Pop-up buttons](https://developer.apple.com/design/human-interface-guidelines/pop-up-buttons)
y [Pickers](https://developer.apple.com/design/human-interface-guidelines/pickers),
con renderizado JUCE en todas las plataformas, sin presentar selectores UIKit.

## Contrato visual

- Ambos controles usan el mismo fondo, borde, radio, tipografia y padding.
- El foco se comunica con un borde de acento de `2 pt`; el estado normal usa
  un borde de `1 pt`.
- Hover y press modifican el fondo sin desplazar contenido.
- El estado deshabilitado reduce el contraste del fondo, borde y chevron.
- La tipografia base es de `17 pt` en superficies tactiles y `15 pt` en
  escritorio.
- El alto tactil recomendado es `44 pt`. Los menus usan filas de `44 pt` en
  iOS/Android y `32 pt` en escritorio.
- El campo reserva padding inicial para el texto y espacio final para el
  chevron o una accion accesoria.
- Los editores de una linea no tienen indentacion superior asimetrica; su
  alineacion vertical pertenece al formulario. Seleccion y caret usan el acento.

## Campos de texto

`applyTextEditorTheme()` solo define presentacion. No cambia si un editor es
de una o varias lineas, su alineacion, restricciones, tecla Return ni reglas
de validacion; esos comportamientos pertenecen al formulario que lo usa.

Todo editor interactivo debe:

1. Configurar primero su comportamiento (`setMultiLine`, restricciones,
   alineacion y callbacks).
2. Aplicar `applyTextEditorTheme()` al construirlo y cada vez que cambie el
   tema global.
3. Usar una etiqueta visible cuando el significado no pueda depender solo del
   placeholder.
4. Validar valores numericos al confirmar o perder el foco.

En superficies tactiles, tocar fuera de un editor descarta el teclado virtual.
El asistente de canciones desactiva ese comportamiento en sus editores para
permitir desplazar el formulario sin interrumpir la escritura; ofrece `Listo`,
Return y apertura de selector para finalizar la edicion.
Los formularios que encadenan varios campos deben definir Return/Done de forma
explicita; el estilo compartido no decide por si solo cual es el siguiente
campo.

`SearchField` agrega icono inicial y un boton de limpiar al final cuando hay
texto. Durante una carga, el spinner reemplaza temporalmente al boton de
limpiar y el campo conserva el espacio necesario para no superponer contenido.
Ese espacio se reserva con el borde derecho del viewport, no con el segundo
argumento de `setIndents` (que es superior). Escribir, limpiar o cargar no
desplaza verticalmente el texto.

## Selectores

`AdaptiveComboBox` representa listas planas de opciones mutuamente
excluyentes. Siempre muestra el valor actual y un chevron final.

- En iPhone, iPad, Android y escritorio abre exclusivamente `juce::PopupMenu`
  anclado al campo, de una sola columna y con desplazamiento si no cabe.
- La opcion activa aparece marcada con un check.
- Elegir confirma una vez; tocar fuera o Escape descarta sin cambiar el valor.
  JUCE conserva navegacion por teclado y foco accesible al regresar al campo.
- No se llama `hidePopup()` antes de abrir: anularia `menuActive`, permitiendo
  otra apertura en `mouseUp` despues de `mouseDown`. JUCE administra el ciclo.
- Abrir el combo finaliza la edicion de texto en movil. El teclado de texto
  sigue siendo el del sistema mediante `juce::TextEditor`; el campo, el menu,
  la seleccion y el tema siguen siendo JUCE. No se promete equivalencia total
  con autocorreccion, capitalizacion ni accesorios especificos de UIKit.
- Listas de acciones, seleccion multiple o jerarquias no deben modelarse como
  `AdaptiveComboBox`; deben usar botones, popovers o listas dedicadas.

## Integracion

No asignar manualmente colores de `TextEditor` o `ComboBox` en componentes
nuevos. Las excepciones son superficies de solo lectura que no representan una
entrada, por ejemplo el mensaje de estado transparente de
`SettingsVersionsSheet`.

Los usos actuales incluyen busqueda, metadatos de cancion, BPM, compas, tono,
snap de waveform, apariencia, idioma, crossfade de pads, estilo del click y
ruteo de salidas.

## Validacion

- Compilar `MTControls_Standalone` en macOS.
- Ejecutar `input_controls`: restricciones, callbacks, espaciado estable del
  buscador y configuracion de una sola columna.
- Ejecutar `input_controls_popup`: una apertura por gesto, seleccion unica,
  Escape y deshabilitado. Esta prueba necesita foco de ventana real; se omite
  con codigo 77 si el sistema no lo concede, sin ocultar las pruebas anteriores.
- Compilar el target compartido `MTControls` para iOS y el wrapper Android.
- En iPhone comprobar menu JUCE, cancelacion y teclado.
- En iPad comprobar anclaje al campo y rotacion.
- En Android comprobar filas tactiles, foco y teclado de software.
- En escritorio comprobar teclado, Tab, Escape, hover y seleccion con mouse.
