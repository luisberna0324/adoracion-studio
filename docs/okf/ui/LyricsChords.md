---
type: UI Contract
title: Letra y acordes
resource: src/ui/panels/SongLyricsPanel.cpp
description: Documento musical opcional con acordes anclados al texto y editor nativo.
timestamp: 2026-10-25T00:00:00-03:00
---

# Letra y acordes

## Alcance

Herramienta para músicos, disponible desde Detalles al crear o editar una
canción. «Agregar letra y acordes» abre un editor amplio; guardar la canción
sin letra sigue siendo válido, incluidas las canciones en blanco.

La lectura se abre desde «Letra» en la barra de escritorio, «Letra y acordes»
en Más de móvil/tablet y el icono de nota en Ensayo cuando la canción local
tiene letra. El contenido corresponde a esa canción; no se asignan tiempos
ni se avanza automáticamente de bloque. Cambiar de canción cierra la lectura
anterior, al igual que cerrar sesión o cambiar de modo remoto. Consultar el
documento no pausa ni modifica el transporte. El teléfono solicita portrait
mientras lee y restaura la orientación y el foco al cerrar.

FreeShow conserva su integración de proyección. El documento de práctica no
modifica las secciones del arreglo, su guía de audio, saltos, loops o
automatización. No se transmite a controles remotos en esta versión.

## Edición

- Pegar o escribir texto normal, con saltos de línea.
- Organizarlo en bloques nombrados (por ejemplo Intro, Verso 1, Coro o Puente).
- Elegir una posición del texto en la vista previa y añadir un acorde.
- Seleccionar un acorde para corregir su símbolo, moverlo o eliminarlo.
- Los acordes se dibujan encima del texto y se ajustan al ancho disponible;
  no se alinean mediante espacios escritos por el usuario.
- En escritorio hay texto y vista previa; en teléfono se alternan para mantener
  espacio útil y respetar el teclado y las áreas seguras.

El editor trabaja sobre una copia. «Listo» aplica esa copia al borrador del
asistente; cancelar conserva el documento anterior. El guardado definitivo se
realiza al guardar la canción. Editar y volver entre pasos conserva el borrador.

## Datos

`ImportedSongData::lyrics` es un `optional<SongLyricsDocument>` independiente
del snapshot de arreglo y de `ProjectSongSettings`. El codec común de canción
persiste `lyrics.schemaVersion = 1` dentro del manifiesto de `.pwproj` y de la
Biblioteca. Cloud transporta el mismo manifiesto; no requiere otro endpoint ni
un archivo de texto auxiliar.

La jerarquía es documento → bloques → líneas → acordes. Cada bloque, línea y
acorde tiene identidad estable. `position` es el índice de caracteres Unicode
de `juce::String` dentro de la línea, no un índice de bytes UTF-8. El modelo es
conceptualmente compatible con acordes insertados en el texto, sin exigir
sintaxis ChordPro en la interfaz ni declarar soporte de todo ese formato.

Al modificar el texto se reajustan las anclas. Insertar antes de un acorde lo
desplaza; borrar su palabra lo conserva en el inicio del rango editado hasta
que el usuario lo elimine. Dividir o unir líneas reasigna las posiciones sin
perder identidades de acordes. El lector valida estructura, límites y posiciones;
una canción antigua sin documento continúa abriendo normalmente.

## Etapas posteriores

Transposición, seguimiento temporal y presentación en equipos remotos requieren
contratos propios. No están implícitos en los nombres de bloques ni en las
secciones de reproducción existentes.

## Verificación

Pruebas de modelo y codec; guardado y reapertura de proyecto, Biblioteca y
manifiesto Cloud; edición y cancelación del asistente; posicionamiento, corrección
y retirada de acordes; lectura y distribución en teléfono y escritorio. Las
pruebas usan contenido ficticio y almacenamiento aislado.

### Evidencia local — 2026-10-25

- Standalone macOS arm64 Release compilado (build local sin firma de distribución).
- Gates aprobados: `song_lyrics`, `song_lyrics_panel`, `song_import_wizard`,
  `song_project_creation`, `practice_panel_gestures` y `waveform_editing`.
- Se probaron pegado y confirmación inmediata sin esperar notificaciones,
  cambio de bloque, arrastre real de acorde y geometrías reducidas de teclado.
- El roundtrip usa un `.pwproj` real y objetos Cloud locales sin red; verifica
  mezcla, ventana, identidades y hashes de audio. No acredita una transferencia
  a producción ni instalación en dispositivos.
- Capturas del editor y lector generadas desde componentes reales con texto
  ficticio en tamaños de teléfono, tablet y escritorio, revisadas visualmente.
