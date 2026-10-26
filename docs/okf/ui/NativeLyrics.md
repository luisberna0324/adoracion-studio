---
type: Technical Contract
title: Native Song Lyrics
description: Editor UIKit de letra y fragmentos musicales, disponible inicialmente en iOS
resource: src/platform/NativeSongLyricsEditor.mm
timestamp: 2026-10-25T00:00:00-03:00
---

# Letra nativa de la canción

## Alcance y acceso

En iOS, el menú de una canción local de Biblioteca o del setlist ofrece **Letra**.
La acción conserva el ID de la canción, vuelve a consultar su metadata y abre un
`UITextView` en un `UINavigationController` a pantalla completa. No depende del
índice visual de la fila. Android y escritorio todavía no ofrecen esta acción.
Una canción sin letra sigue siendo válida; el editor vacío invita a pegar o
escribir y explica cómo marcar un fragmento. No inserta texto de ejemplo.

El editor usa la tipografía dinámica, selección, teclado, desplazamiento y menú
de edición de UIKit. iPhone solicita portrait y restaura la orientación anterior
al cerrar. iPad conserva las orientaciones admitidas y usa el área segura y el
teclado de la ventana. La vista adapta sus colores al tema de PlayWorship.

## Selección y secciones de letra

Seleccionar texto permite abrir **Sección** desde el menú nativo de edición.
Las opciones son Intro, Verso, Pre-coro, Coro, Puente, Instrumental, Outro y Final.
Cada sección se presenta en una tarjeta de ancho completo, con fondo de panel,
esquinas redondeadas y separación vertical. El encabezado pastel sobresale sobre
el borde superior y contiene un círculo con código y el nombre de la sección
(`V1 · Verso 1`, `C1 · Coro 1`, `PC1 · Pre-coro 1`, `IN · Intro`). Las tarjetas
crecen con su contenido y no recortan la letra.

Encima del editor se muestran el nombre de canción, artista y BPM disponibles.
La fila horizontal de círculos permanece fuera del scroll del texto. Tocar un
círculo navega a la tarjeta; su estado seleccionado acompaña la posición visible.
Versos, coros y pre-coros se numeran por su orden en la letra. Los demás tipos
sólo añaden número si se repiten. Los colores identifican el tipo, y los nombres
y códigos completos permanecen disponibles para accesibilidad.

La escala visual base es compacta: navegación con círculo de 36 pt dentro de un
área táctil de al menos 44 pt, encabezados de tarjeta de 32 pt y círculo interior
de 24 pt. El título y la letra conservan 28/17 pt de base; la metadata usa 14 pt.
Fuentes y geometría acompañan Dynamic Type. La franja de navegación, los márgenes
internos y la separación vertical evitan sumar espacio decorativo innecesario
al texto, conservando los saltos de línea guardados.

Al asignar una sección nueva, la selección se expande a los párrafos completos
que toca, sin abarcar el siguiente cuando termina justo en su inicio. El último
terminador LF/CRLF queda fuera de la marca; no se modifica el texto ni se insertan
encabezados. **Quitar sección** retira la marca del rango seleccionado y conserva
la letra. Los rangos históricos que cubren una parte de un párrafo se preservan:
usan resaltado inline y círculo de navegación, sin presentar texto ajeno como
parte de esa sección. Un bloque completo sin el salto de línea final sí admite
tarjeta. Al escribir en el inicio o final de una tarjeta completa, el texto nuevo
se incorpora a esa tarjeta, incluidos los saltos de línea. En un límite compartido,
se prioriza la tarjeta que empieza allí para mantener rangos sin solapamiento.
Los fragmentos históricos parciales conservan su semántica de edición exacta.

La presentación conserva un único `UITextView` con TextKit y un layout manager
propio: fondos y encabezados son decoración. La selección, pegado, teclado,
Undo/Redo y offsets UTF-16 siguen perteneciendo al texto real. Los encabezados
son elementos UIKit accesibles y nunca se guardan dentro de la letra.

Deshacer y Rehacer conservan también las marcas del texto que se borra y se
recupera. El editor conserva un historial de metadatos junto al historial nativo
de texto, sin sustituir su selección ni registrar acciones de texto duplicadas.

Los rangos son UTF-16, compatibles con `NSRange`, y no pueden cortar un par
sustituto Unicode. Las marcas no se superponen: al aplicar otra se recorta o
divide la anterior, conservando los fragmentos restantes. Editar texto reajusta
los rangos. No se insertan encabezados ocultos ni espacios para posicionarlos.

`SongLyricsDocument` tiene versión 1, `text` y `sections` con ID, tipo, inicio y
longitud; su JSON declara `offsetEncoding: "utf16"`. El límite es 262144 unidades
UTF-16 y 4096 marcas. JSON/rangos inválidos no producen un documento parcial.
La letra es `ImportedSongData::lyrics` opcional y viaja con la canción, incluida
su metadata de proyecto y de nube; no es un ajuste de mezcla por setlist.

Estas secciones son etiquetas de la letra. No crean ni modifican las secciones
del arreglo, el transporte, la guía, las automatizaciones ni FreeShow. Acordes,
transposición y seguimiento sincronizado no forman parte de esta primera etapa.

## Guardar y cerrar

**Listo** solicita el guardado de forma asíncrona. La vista permanece abierta y
bloquea ediciones y acciones duplicadas mientras espera el resultado. Un error
muestra un aviso y conserva el borrador para reintentar. Sólo el éxito cierra el
editor y refresca la biblioteca; no activa `onSongEdited` ni recarga el audio.
**Cerrar** pide confirmación si hay cambios. La cancelación por destrucción o
cambio de contexto cierra silenciosamente y descarta callbacks tardíos.

`NativeSongLyricsEditor::Handle` delimita la presentación; los callbacks usan
`SafePointer`, una generación y cancelación compartida. La finalización de cada
solicitud de guardado sólo puede consumirse una vez. El guardado captura el ID y
actualiza exclusivamente la letra sobre metadata actual; consultar
[LibraryPersistence](../importing/LibraryPersistence.md) para la transacción.

## Verificación

- `song_lyrics`: Unicode, rangos, edición, solapamientos y JSON.
- `song_project_creation`: persistencia de letra y conservación del proyecto.
- `song_list_lyrics`: menús, ID estable, guardado y ciclo de vida con bridge simulado.
- `MTControlsNativeSongLyricsUITests`: app aislada UIKit en iOS, editor real,
  menú, selección, undo, teclado, error y cierre; escribe su reporte en Documents.
- La compilación o el bridge simulado no sustituyen la revisión visual del flujo
  completo en el simulador de iPhone.

### Evidencia anterior del editor inicial — 2026-10-25

- Compilación Debug arm64 para iPhone Simulator: correcta.
- Pruebas aisladas `song_lyrics`, `song_project_creation` y `song_list_lyrics`:
  3/3 correctas.
- Harness UIKit en iPhone, iOS 26.5: 47 comprobaciones correctas. Incluye pegado
  Unicode mediante el permiso real de iOS, edición y Undo/Redo con metadatos,
  error/reintento de guardado, cierre asíncrono y cancelación con alerta abierta.
- Flujo de la app completa revisado desde Biblioteca local: editor vacío,
  escritura, selección, asignación de Verso, guardado y reapertura persistente.

Esta evidencia corresponde al simulador; no acredita instalación o prueba de
esta funcionalidad en un dispositivo físico.

### Evidencia de tarjetas — 2026-10-25

- Compilación Debug de app y harness arm64 para iPhone Simulator: correcta.
- Prueba enfocada `song_list_lyrics`: correcta (4,38 s).
- Harness UIKit en iPhone iOS 26.5: 93 comprobaciones correctas. Incluye
  cabecera inicial sin recorte, tarjetas adaptables, selección por párrafos,
  CRLF/Unicode, navegación circular, compatibilidad de rangos parciales,
  escritura en ambos bordes y nueva línea, Undo/Redo y guardado/cancelación.
- En la app completa se verificaron dos tarjetas guardadas (Verso y Coro),
  navegación, edición carácter por carácter al final del Coro, guardado real
  del proyecto y reapertura con el texto y los mismos IDs de sección.
- Se corrigió el desplazamiento al mostrar la navegación, restaurando selección
  y scroll después de estabilizar el layout nativo.
- Captura y reportes: `output/ios-lyrics-cards/` del checkout local de QA.

Esta evidencia corresponde al simulador; no acredita instalación o revisión
visual del diseño de tarjetas en un iPhone físico.

### Escala compacta — 2026-10-25

- App y harness iOS arm64 compilados correctamente con la escala compacta.
- Las 93 comprobaciones UIKit existentes pasaron sin cambios en los tests.
- Revisión de la app completa sobre las mismas secciones guardadas: círculos
  compactos dentro de targets de 44 pt, encabezados de 32 pt, menos sangría y
  separación vertical; conserva el tamaño base de letra y título.
- Se centraron los códigos en el círculo interior durante la revisión visual.
- Evidencia: `output/ios-lyrics-cards/compact-uikit-report.json` y captura
  `letras-tarjetas-compactas-iphone.png` del checkout local de QA.
