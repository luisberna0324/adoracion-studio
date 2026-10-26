---
type: Technical Contract
title: Native Song Lyrics
description: Editores nativos de letra y fragmentos musicales para iOS y Android
resource: src/platform/NativeSongLyricsEditor.mm
timestamp: 2026-10-25T00:00:00-03:00
---

# Letra nativa de la canción

## Alcance y acceso

En iOS y Android, el menú de una canción local de Biblioteca o del setlist ofrece
**Letra**. La acción conserva el ID de la canción y vuelve a consultar su metadata;
no depende del índice visual de la fila. iOS abre un `UITextView` en un
`UINavigationController` a pantalla completa. Android presenta un editor nativo
con un único `EditText`. Escritorio conserva la letra, pero no ofrece esta acción.
Una canción sin letra sigue siendo válida; el editor vacío invita a pegar o
escribir y explica cómo marcar un fragmento. No inserta texto de ejemplo.

En iOS, el editor usa la tipografía dinámica, selección, teclado, desplazamiento
y menú de edición de UIKit. iPhone solicita portrait y restaura la orientación anterior
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
círculo navega a la tarjeta y coloca un cursor al inicio, sin seleccionar el bloque
ni abrir el teclado si estaba oculto; su estado activo acompaña la posición visible.
Si todas las tarjetas caben en el editor, conserva la última elección explícita.
La navegación resuelve el layout pendiente del destino antes de calcular el scroll;
la caché decorativa se invalida junto al layout nativo.
Versos, coros y pre-coros se numeran por su orden en la letra. Los demás tipos
sólo añaden número si se repiten. Los colores identifican el tipo, y los nombres
y códigos completos permanecen disponibles para accesibilidad.

La escala visual base es compacta: navegación con círculo de 36 pt dentro de un
área táctil de al menos 44 pt, encabezados de tarjeta de 32 pt y círculo interior
de 24 pt. El título y la letra usan 24/17 pt de base; la metadata usa 13 pt.
Fuentes y geometría acompañan Dynamic Type. La franja de navegación, los márgenes
internos y la separación vertical evitan sumar espacio decorativo innecesario
al texto, conservando los saltos de línea guardados.

El fondo usa los mismos tokens `appBgTop`/`appBgBottom` de Biblioteca y un halo
del acento al 6 %. El editor toma texto, secundario, acento, tarjeta y borde del
tema activo, sin fijar un acento ámbar para todos los temas. La barra UIKit es
transparente: flecha de 21 pt en color de texto, título de 18 pt y «Listo» de 16 pt en acento.
Ambas acciones son Plain y en iOS 26+ usan `hidesSharedBackground` para eliminar
las cápsulas Glass. Conservan sus IDs y bloqueo durante el guardado. Menús y
teclado nativos siguen el modo claro/oscuro del tema de la app.


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

Los saltos y espacios finales que pertenecen a la tarjeta también forman parte
de su área editable, aunque aún no se escriba otra palabra. El fondo incluye la
línea vacía de inserción, incluido el fragmento sin glifo de TextKit al final del
documento. El espacio del pie se sitúa después de esa línea. Una línea vacía antes
de un separador externo admite continuar la tarjeta; el siguiente párrafo con
texto o el inicio de otra sección no se incorpora a su fondo ni a su rango.
`UITextInput.insertText` registra el rango UTF-16 previo a la inserción; el
delegado puede refinarlo. Esto evita deducir un límite incorrecto cuando varios
saltos de línea iguales vuelven ambiguo el diff de texto. Undo/Redo conserva la
agrupación nativa y restaura los rangos correspondientes al texto que queda.


La presentación conserva un único `UITextView` transparente con TextKit y un
layout manager propio para calcular la geometría. Los fondos son una vista hermana
por debajo del editor; los encabezados, otra por encima. Ambas decoraciones dejan
pasar los gestos al texto. No se pinta una tarjeta opaca en el paso de dibujo del
layout manager: eso tapaba el resaltado de selección nativo dentro de la tarjeta.
La selección, pegado, teclado, Undo/Redo y offsets UTF-16 siguen perteneciendo al
texto real. Los encabezados son elementos UIKit accesibles y nunca se guardan
dentro de la letra.

Los rectángulos del cursor y de los dos extremos de selección usan la línea real
(baseline del glifo y métricas de la fuente), sin sumar el espacio del encabezado
ni del pie de la tarjeta. El adaptador público de `UITextSelectionRect` conserva
coordenadas horizontales, flags de inicio/fin, dirección de escritura y transform;
los rectángulos intermedios y verticales siguen siendo los de UIKit. Esto conserva
los tiradores y los gestos nativos, con resaltado visible sobre los fondos.

Durante la escritura, el formato se compara con los atributos existentes y sólo
se aplican las diferencias reales. Una edición local no vuelve a aplicar estilos
a los párrafos lejanos ni fuerza el layout del documento entero. El formato de
escritura hereda la geometría del párrafo actual; no intercala un estilo normal
antes de restaurar la tarjeta. UIKit conserva el control del cursor, los tiradores
de selección y el scroll necesario para escribir por encima del teclado.
La restauración explícita del viewport se reserva para asignar o quitar marcas
y actualizar la escala, no para cada carácter. La composición marcada del teclado
no se reformatea hasta confirmarse, incluso cuando termina sin otro cambio de
selección. No se agregan reconocedores de gestos sobre el texto.

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


### Fluidez de edición — 2026-10-25

- Compilación Debug arm64 para iPhone Simulator y dispositivo físico: correcta.
- Harness UIKit en iPhone iOS 26.5: 109 comprobaciones correctas. Además de los
  casos existentes, cubre formato local sin invalidar párrafos lejanos, geometría
  de escritura heredada, selección nativa de palabras, composición marcada que
  crece y se acorta, cursor visible sobre el teclado tras escritura continuada,
  párrafo final fuera de tarjeta y navegación con teclado abierto tras Undo.
- En la app completa se seleccionó una palabra mediante el menú nativo, se
  sustituyó desde el teclado en pantalla y se restauró con Deshacer, conservando
  el texto y las dos tarjetas originales. Los círculos colocan un cursor sin
  seleccionar el contenido completo.
- Instalación y arranque por Wi-Fi en iPhone 16, con firma y ausencia del equipo
  en el registro USB verificadas. Esto acredita el despliegue; la sensación de
  edición en ese dispositivo debe comprobarse manualmente.
- Reporte, log de UIKit y log de instalación: `output/ios-lyrics-fluency/` del
  checkout local de QA.


### Resaltado y extremos de selección — 2026-10-25

- Se reprodujo en la app completa: seleccionar varias líneas sólo mostraba color
  en los huecos entre tarjetas; los extremos incluían separación decorativa.
- Compilación de app y harness arm64 para iPhone Simulator: correcta. Harness:
  120 comprobaciones correctas en iOS 26.5, incluyendo un Coro de seis líneas
  con espaciado antes/después, altura y baseline de ambos extremos, metadatos
  nativos de selección, selección de palabra y persistencia idéntica al guardar.
- Comparación visual en la app completa: selección visible dentro de las dos
  tarjetas y selección del Coro con extremos de altura de línea. La captura final
  conserva la letra original; la selección no modifica el documento.
- Compilación firmada para dispositivo e instalación/arranque por Wi-Fi en iPhone
  16: correctos. No equivale a revisión visual de esta selección en el físico.
- Capturas antes/después, reporte y log de instalación:
  `output/ios-lyrics-selection/` del checkout de QA.


### Continuidad de sección y estilo de Biblioteca — 2026-10-25

- App y harness arm64 compilados correctamente para iPhone Simulator.
- Harness UIKit en iOS 26.5: 151 comprobaciones correctas. Añade líneas vacías
  al final del documento y antes de otra sección, Enter repetido, espacios sin
  texto posterior, contención inmediata del cursor, crecimiento de tarjeta,
  Undo/Redo con agrupación nativa y JSON exacto al guardar. Detectó y corrigió
  la atribución ambigua de LF cuando `insertText` no pasa por el delegado.
- App completa revisada con Ámbar Estudio, seleccionado desde Configuración:
  fondo compartido con Biblioteca, flecha blanca y «Listo» ámbar sin cápsulas.
  Desde el teclado de software oscuro se pulsó Enter al final del Coro antes
  de un LF existente: la tarjeta creció y el cursor quedó dentro de la línea
  vacía. El borrador de prueba se descartó al volver a Biblioteca.
- Compilación firmada, instalación y arranque por Wi-Fi en iPhone 16 correctos;
  firma y ausencia del iPhone en el registro USB verificadas. La revisión visual
  descrita corresponde al simulador.
- Evidencia: `output/ios-lyrics-theme/` del checkout local de QA, con capturas
  `editor-ambar.png` y `enter-dentro-de-seccion.png`, reporte UIKit y log Wi-Fi.


## Android: paridad del flujo nativo

El punto de entrada y el guardado usan `NativeSongLyricsEditor::Handle` y
`SongListPanel::openSongLyricsEditor`, compartidos con iOS. La biblioteca vuelve
a consultar la canción por ID antes de abrir; el guardado asíncrono actualiza
únicamente `lyrics`, con los mismos controles de cancelación y generación.
La selección de canción y el audio no se recargan por guardar letra.

El bridge JNI intercambia el documento versionado y la configuración de tema
como JSON. Las cadenas se leen y escriben como UTF-16 real, sin usar la variante
UTF-8 modificada de JNI para texto de usuario. Los comandos de texto reciben
el rango exacto de `TextWatcher`, incluso al introducir saltos consecutivos.
`NativeSongLyricsEditing` mantiene la misma pertenencia de bordes que iOS:
escribir en los bordes de una sección de párrafos completos la amplía; en
un límite compartido, la sección siguiente tiene prioridad. Asignar sección
expande a párrafos completos; quitar la marca conserva el texto. No se guardan
etiquetas visuales dentro de la letra.

El editor conserva selección, pegado, teclado y composición de Android. La
barra usa flecha de regreso, «Letra» y «Listo», con título de canción, metadata
y navegación circular encima del texto. Los colores provienen de los mismos
tokens y las etiquetas de sección se numeran con `sectionLabels`, sin duplicar
la traducción o los códigos en Java. Los tamaños base corresponden a iOS,
expresados en dp para geometría y sp para tipografía.

El cierre con cambios exige confirmación. Mientras se guarda se bloquean
acciones duplicadas; un error mantiene el borrador y permite reintentar.
Una cancelación desde el propietario descarta la presentación silenciosamente;
las respuestas tardías no afectan a otra sesión. El editor solicita orientación
vertical en teléfonos y restaura la orientación previa al cerrar.


En Android las tarjetas se dibujan antes del resaltado y del texto nativos.
Un `LineHeightSpan` con `UpdateLayout` reserva el espacio de encabezado: cambiar
la marca provoca el reflow nativo sin sustituir el contenido del `Editable`.
El cursor se dibuja con las métricas de su línea y el resaltado excluye sólo
las bandas decorativas. Los rangos visuales se limitan al texto y al layout
vigentes mientras una composición se acorta; una presentación pendiente no
puede indexar fuera del texto actual. La clasificación de tarjeta completa
viene del modelo C++, incluyendo CRLF y separadores Unicode.

La composición del IME conserva sus spans. Confirmar o terminar la composición
aplica el formato pendiente; no se vuelve a cargar el texto en cada pulsación.
El historial de metadatos acompaña Deshacer/Rehacer del `EditText`, con una base
nueva al reclasificar secciones y un límite de 256 instantáneas / 4 MiB. El
scroll y la selección permanecen a cargo del campo nativo.

Atrás oculta primero el teclado visible; una segunda acción solicita cerrar.
Android 13+ usa el callback público de navegación predictiva. Al destruir la
Activity se cierra su editor; el puente comprueba la vigencia de la sesión
antes de mostrar una presentación encolada. El cierre, la cancelación y los
resultados tardíos de guardado no pueden abrir de nuevo una sesión terminada.

Las pruebas de interfaz Android son una instrumentación independiente, sin
dependencias de JUnit/AndroidX añadidas: `:app:assembleDebugAndroidTest` y
`am instrument -w com.multitracks.playworship.test/com.rmsl.juce.NativeLyricsInstrumentation`.
El reporte `files/native-lyrics-android-report.json` declara `success`, las
comprobaciones y los fallos; el código de salida del proceso no sustituye ese
reporte. El observador de pruebas no se instala ni activa desde ningún flujo
de la app.


### Verificación Android — 2026-10-26

- APK Debug arm64 y APK de instrumentación compilados correctamente.
- Suite local `song_lyrics`: 76 comprobaciones correctas, incluyendo límites de
  tarjeta, LF/CRLF, separadores Unicode y equivalencia de clasificación de
  párrafos, más el documento máximo de 262144 unidades / 4096 secciones.
- Pruebas aisladas de biblioteca y persistencia: `song_lyrics`,
  `song_project_creation` y `song_list_lyrics`, 3/3 correctas.
- El harness nativo se ejecutó en `PlayWorship_Phone_API_36_FirstRun`, arm64,
  Android API 36: **167 comprobaciones correctas**. Incluye pegado y selección
  nativos, selector de secciones,
  Return/espacios antes de otra tarjeta y al final, composición que crece y se
  acorta, Undo/Redo con metadatos, navegación por 18 tarjetas, posiciones reales
  del encabezado al hacer scroll y guardado asíncrono con error/reintento.
- La prueba detectó y corrigió un acceso a rangos visuales antiguos durante
  composición y una captura accidental del documento inicial en el watcher de
  texto que impedía actualizar metadatos al deshacer hasta el texto original.
- Los fondos, encabezados y selección se revisaron en capturas de la vista real
  con Ámbar Estudio; Enter conserva la línea vacía dentro de la tarjeta.
- Evidencia: `output/android-lyrics/` del checkout local de QA, con reporte JSON,
  logs de compilación y capturas `native-lyrics-android-*.png`. Esta validación
  corresponde al emulador; no acredita instalación o prueba en Android físico.
- Tras la instrumentación, la app normal arrancó en frío correctamente (Activity
  `JuceActivity`, proceso activo), con el mismo APK Debug instalado.


### Cursor clonable para Samsung / Android 11 — 2026-10-26

`LyricsCursor` expone un `Drawable.ConstantState` válido. Las tres variantes
de `newDrawable` producen otra instancia con `Paint` y límites independientes,
con el mismo acento y ancho. El estado sólo conserva una referencia débil al
editor; una copia retenida por el framework no mantiene viva la Activity.
La altura sigue usando las métricas de la línea real.

El Galaxy A50 (SM-A505G, API 30) cerraba la app al tocar el campo para pegar:
`Editor.HandleView` creaba el tirador de inserción mediante
`getTextCursorDrawable().getConstantState().newDrawable()`. El cursor anterior
heredaba `Drawable.getConstantState()`, que devuelve `null`. Se confirmó el
camino exacto en el framework del dispositivo y coincide con los dos registros
de `NullPointerException` en `HandleView.<init>`. Las pruebas previas de API 36
pegaban a través del método de edición sin abrir este tirador OEM.

La regresión Android verifica la recreación del cursor y los tres tiradores,
además de eventos táctiles, pulsación larga y pegado sobre una letra sintética.
El portapapeles anterior se conserva sólo en memoria en el teléfono y se
restaura al terminar; el contenido original no se incluye en el reporte.


Verificación de la corrección en el Samsung físico:

- APK Debug compilado e instalado como actualización en el SM-A505G, API 30.
- Harness nativo: **196 comprobaciones correctas**. La prueba táctil crea el
  tirador de inserción que fallaba y luego los dos tiradores de selección.
  El pegado conserva el texto Unicode, la composición, los rangos y el historial.
- El menú «Pegar» se mostró y se capturó en el teléfono. La acción accesible del
  menú OEM no aceptó el clic automático; el reporte lo registra como
  `nativePasteMenuClicked: false` y comprueba el pegado mediante el método
  público nativo `TextView.onTextContextMenuItem(android.R.id.paste)`. Esto no
  acredita un toque manual en el botón del menú, ni arrastre manual de tiradores.
- Capturas y reporte del dispositivo en
  `output/android-lyrics-samsung-cursor-fix/` del checkout local de QA. El
  registro anterior al ajuste se conserva como `crash-before-fix.log`.
