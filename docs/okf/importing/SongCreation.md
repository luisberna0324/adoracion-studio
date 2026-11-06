---
type: Import Contract
title: Song Creation
description: Creacion portable compartida entre escritorio y movil
resource: src/ui/panels/SongImportWizardComponent.cpp
timestamp: 2026-11-06T00:00:00-03:00
---

# Creacion de canciones

El guardado completo de Editar vuelve a empaquetar el proyecto y regenera la onda mixta desde los stems incluidos, aunque no hayan cambiado sus archivos o categorías. No reutiliza los bins del proyecto anterior; actualiza la onda del paquete y su caché local al confirmar el guardado. Conserva los ajustes musicales y las letras existentes.

Al guardar una edición cuyo `.pwproj` falta, el asistente conserva el borrador y ofrece Localizar proyecto, Crear nuevo proyecto o Cancelar. Localizar valida la identidad antes de actualizar; Crear exige una ruta nueva y audios disponibles, recupera ajustes del caché o setlist y conserva la identidad de biblioteca. No se muestra este aviso al abrir Editar.

En Archivos, cada fila muestra el nombre visible del stem (`displayName`), con el nombre físico como tooltip y fallback si no hay título. Los nombres internos UUID de proyectos optimizados no sustituyen los títulos guardados al editar.

El asistente mantiene un borrador al recorrer Detalles, Archivos, Secciones
(mapeo de pistas) y Revisar. Retroceder no descarta nombres, categorias ni la
seleccion Cargar. Cancelar el selector de archivos conserva los archivos previos.

La sugerencia de categoría al agregar archivos reconoce `cue`, `cues`, `guide`,
`guia` y `guía` como palabras completas, con acentos normalizados. `Cuerdas`
y `Cuerda` proponen `Strings`. Así, `CUERDAS` conserva su tratamiento musical
y su transposición, mientras `GUIA` propone `Cues`. Las categorías existentes
se conservan al abrir o editar una canción; la sugerencia se aplica a los
archivos que se agregan al asistente.

En telefonos iOS/Android el asistente ocupa toda la pantalla vertical y respeta
area segura/teclado. Detalles tiene viewport; Archivos una lista desplazable;
el mapeo filas apiladas; Revisar un resumen desplazable. Agregar cancion en
escritorio usa esa misma jerarquia, indicador Detalles/Archivos/Secciones/Revisar
y navegacion inferior. Solo reduce controles (campos de 36 px, filas de archivos
de 48 px y mapeo de 80 px). La edicion de escritorio reutiliza el mismo formulario desplazable y la pagina interna de Agregar, con datos precargados y callbacks de actualizacion.
El `+` del encabezado de Biblioteca abre Detalles como pagina interna de
`SongListPanel` en escritorio, telefono y tablet, a continuacion de Biblioteca en su
`PageContainer`. La pagina entra desde la derecha con la misma transicion
horizontal de Biblioteca; al pulsar `X` o terminar el guardado, sale hacia la
derecha mientras reaparece Biblioteca. El asistente ocupa los mismos limites
del panel y sigue sus redimensiones. La pagina de creacion se libera al terminar
la animacion de retorno. Se conserva el contexto de busqueda/seleccion y la
lista se recarga antes del retorno al guardar. La creacion incrustada usa el
formulario desplazable tambien en tablets y escritorios tactiles, de modo que
sus controles se ajustan al ancho del panel. La pagina regresa a la vista de origen al cancelar o guardar. Los accesos externos y la edicion movil mantienen su presentacion previa.

Los controles compartidos son JUCE y la entrada de texto movil usa el teclado
del sistema. El telefono adapta el viewport y muestra la barra sobre el teclado.

## Foco y teclado movil

Titulo, artista y BPM se recorren sin un desenfoque intermedio. Los nombres de
pistas siguen el mismo recorrido en Secciones. La barra sobre el teclado del
telefono ofrece Anterior/Siguiente/Listo; Return avanza o termina en el ultimo
editor. Los selectores de tono, compas y categoria cierran la entrada de texto
y esperan un anclaje estable para abrir una sola lista. Desplazarse sobre un
selector no lo abre ni cambia el valor. Ver [Mobile Layout](../ui/MobileLayout.md).

`cmake/IOSFormKeyboard.cmake` genera una copia de `juce_gui_basics` solo para iOS,
conservando el checkout externo de JUCE. Para campos marcados con
`playworshipContinuousTextInput`, mantiene el first responder, notifica los
cambios de texto/seleccion y llama a `reloadInputViews` al transferir el foco.
`playworshipReturnKeyNext` decide la etiqueta nativa Siguiente/Listo. El resto
de los editores conserva el comportamiento original de JUCE. BPM solicita teclado decimal
en iOS, con la accion Listo de la barra para terminar la entrada. La configuracion
falla si cambia el codigo fuente esperado de JUCE, para revisar esta adaptacion
al actualizar la dependencia. Android reutiliza la actualizacion del IME de JUCE.
Referencias: [UIKit reloadInputViews](https://developer.apple.com/documentation/uikit/uiresponder/reloadinputviews()),
[Android keyboard visibility](https://developer.android.com/develop/ui/views/touch-and-input/keyboard-input/visibility).

El editor de una canción existente se presenta en `SongWizardOverlay`, como
hermano de Biblioteca bajo el editor principal. Al cambiar el teclado, el área
segura o el tamaño, `layoutMainUi` mantiene ese mismo overlay por delante de
Biblioteca y su fondo mediante `bringSongEditorOverlayToFront()` y
`toFront(false)`. No lo reconstruye ni solicita foco de nuevo: conserva el campo,
el borrador, la selección y el caret. Reautenticación y Ensayo mantienen su
prioridad posterior. El cierre sólo se ejecuta por la acción explícita existente.

## Guardar

1. Validar titulo y archivos incluidos existentes. WAV, AIFF, FLAC y MP3 deben
   ser legibles por JUCE. Escritorio admite tambien M4A (AAC o ALAC); si la
   plataforma no dispone de lector nativo, su decodificacion se valida en el
   worker de creacion antes de guardar. Revalidar antes del guardado definitivo.
2. En escritorio, mostrar el diálogo nativo asíncrono `Biblioteca local` /
   `Elegir ubicación...` / `Cancelar`. Biblioteca conserva la resolución de nombre
   disponible en `MTControls/projects/` mediante `SongProjectCreation::resolveDestination`;
   elegir ubicación abre el panel nativo Guardar con filtro `.pwproj` y confirmación
   antes de reemplazar un archivo existente. El resumen anticipa esta elección.
   Cancelar cualquiera de los dos paneles mantiene el borrador completo y permite
   reintentar, sin guardar ni registrar una canción. La transacción está bloqueada
   mientras se espera el destino; la generación de solicitud consume cada callback
   una sola vez e ignora respuestas tardías de solicitudes anteriores.
   iOS y Android mantienen su destino administrado sin este diálogo.
   Mac App Store registra siempre el proyecto dentro del contenedor y, si se eligió
   otra ubicación, exporta una copia conservando el URL nativo durante el worker.
   Si esa copia falla, conserva la canción interna y muestra un aviso sin repetir
   su creación. El resolver síncrono permanece disponible para los consumidores
   sin interfaz nativa y las pruebas del asistente.
3. Generar forma de onda solo con las pistas musicales y guardar con
   `ProjectStore::save`. Click/clic y guia/cues se excluyen de los picos y de la
   normalizacion visual, no del paquete ni de la reproduccion. Su duracion sigue
   formando parte del timeline para no desplazar la onda; una cancion formada
   solo por clic y guia produce una onda plana. La regla vive en
   `buildMixedWaveformBins`, compartida con importacion y regeneracion de cache.
   Las ondas ya guardadas se conservan hasta su regeneracion.
   Ensayo usa `buildStemWaveformBins` para previsualizar un solo instrumento
   local sin alterar la onda mezclada del proyecto. Se calcula fuera del hilo
   de UI y se cancela al cambiar de pista. Los bins se construyen con la
   duración fuente real, conservando silencio para stems más cortos. Después,
   `PracticeWaveform` los proyecta al arreglo usando el snapshot real del
   scheduler, sin duplicar PCM ni normalizar cada copia por separado.
4. Volver a abrir y validar el paquete, y registrar la cancion en Biblioteca.
   El registro final se serializa en el message thread antes de declarar exito;
   solo el worker espera ese resultado, sin retener locks.
5. Cerrar el asistente solo despues del exito. Ante un error, conservar el
   borrador y desbloquearlo para corregir/reintentar.

En creacion portable, movil y escritorio ejecutan la misma transaccion
`SongProjectCreation::save` en background: forma de onda, paquete, reapertura
y registro en Biblioteca.
El worker captura datos y callback por valor; no modifica componentes JUCE.
Progreso y resultado vuelven mediante `callAsync` y `SafePointer`. Mientras
guarda se bloquean doble envio, navegacion y cierre. Si el paquete se guarda
pero falla el registro, se conserva el `.pwproj` y se informa su ubicacion.
`songs.json` solo se reemplaza tras comprobar escritura completa y flush de
un temporal, manteniendo intacta la biblioteca anterior ante errores de I/O.
La edicion de proyectos tambien guarda en background con progreso y proteccion
contra reentrancia. Al guardar metadatos o stems de un `.pwproj`, se reconstruye
el paquete portable, se comprueba su reapertura y se conserva temporalmente el
paquete anterior para restaurarlo si falla la actualizacion de Biblioteca.

## Copia portable para otro equipo

En Biblioteca local de escritorio, el menú de una canción propia con un `.pwproj`
disponible incluye `Exportar copia del proyecto...`. Es una copia del paquete con
pistas y configuración, independiente de `Exportar canción` en Onda, que produce
una mezcla de audio. No se ofrece para entradas remotas, paquetes no disponibles,
contenido compartido de Teams ni en móviles.

El selector Guardar es nativo y asíncrono. Cancelar no modifica la canción ni su
registro. Después de elegir el destino, el editor entrega sus ajustes activos al
sistema de persistencia; un worker termina las escrituras pendientes y consolida
el sidecar con `ProjectStore::savePendingPortableChanges` antes de copiar. Si el
proyecto vuelve a cambiar durante esa consolidación, se informa el conflicto para
reintentar en lugar de declarar una copia actualizada.

Se conserva el URL devuelto por el picker hasta terminar. La copia usa un temporal
y reemplazo final; un fallo no mueve el proyecto original ni reemplaza parcialmente
un destino anterior. El progreso modal evita otro envío mientras se copia y los
resultados vuelven al message thread con `SafePointer`. El registro de Biblioteca
y el proyecto que usa la reproducción conservan sus rutas. El `.pwproj` exportado
se abre con `Abrir proyecto...` en el otro equipo sin depender de la nube.

## Audio y portada

En iOS/Android los selectores entregan URLs, no se convierten directamente en
rutas. El borrador copia en background los documentos seleccionados a un
directorio temporal privado: conserva el acceso security-scoped de iOS y abre
`content://` mediante `AndroidDocument` en Android. Conserva nombre y extension,
verifica el tamano cuando el proveedor lo anuncia y solo actualiza el borrador
cuando termina toda la seleccion. Cancelar o fallar conserva la seleccion
anterior; agregar reutiliza las copias existentes. La portada usa el mismo
recorrido y se decodifica antes de aceptarla. El temporal vive hasta terminar
el guardado y se elimina al liberar el borrador; nunca borra los originales.
Escritorio mantiene su selector y sus rutas locales existentes. La creacion
usa archivos elegidos por el usuario; no inspecciona bibliotecas privadas
de otras aplicaciones.

En Android el picker abre con `setType("*/*")` y sin `EXTRA_MIME_TYPES`:
`juce::FileChooser` traduce el wildcard de extensiones a tipos MIME y al
intentar fijar `audio/*` varios providers (Samsung My Files, DocumentsUI
frente a Google Drive, carpetas sin indexar MediaStore) etiquetan audio como
`application/octet-stream` o `audio/*` generico y los ocultan. Devolver el
wildcard como `"*"` desde `stemImportWildcard()` evita el filtro en el SAF;
la validacion final del formato (extension + `AudioFormatManager::createReaderFor`)
sigue ocurriendo en `MobileImportStaging::stage()` y en
`PortableFlacConversion`, por lo que el asistente no persiste fuentes que
no sean audio.

Movil convierte WAV/AIFF/FLAC/MP3 a FLAC dentro del proceso mediante JUCE,
con bloques de 65536 muestras y un temporal validado antes de reemplazar el
destino. MP3 reutiliza el decoder MinMP3 embebido en `juce_core`
(JUCE_USE_MP3AUDIOFORMAT=1 ya en el NDK) y los tags ID3/APIC no se propagan
al FLAC destino. Conserva frecuencia y canales (hasta 8); PCM hasta 24 bits
conserva precision, y fuentes flotantes/32 bits pasan a PCM24, limite del
escritor FLAC de JUCE. No se descarga ni ejecuta FFmpeg en iOS/Android.
Escritorio sigue usando FFmpeg para empaquetar FLAC. La creacion admite
WAV/AIFF/FLAC/MP3/M4A; la edicion conserva ademas AAC, OGG y WMA.
El selector, las ayudas de formatos y la validacion comparten esa lista.
`SongProjectCreation::save` convierte los M4A a FLAC en un staging privado
antes de calcular duracion, canales, ventana de reproduccion y forma de onda.
Esto permite crearlos tambien sin un decoder nativo M4A (Windows/Linux),
evita una segunda conversion al empaquetar y conserva los originales.
Los temporales se eliminan al terminar o fallar. Un M4A corrupto no genera
un proyecto ni una entrada de Biblioteca; el asistente mantiene el borrador.
El soporte de importacion movil continua siendo WAV/AIFF/FLAC/MP3.

La optimizacion de almacenamiento es automatica y no tiene interruptor en
Configuracion ni cambios temporales de preferencias desde el asistente. Los
paquetes `.pwproj` mantienen FLAC como formato portable obligatorio. Los stems
que ya estan en FLAC se reutilizan sin recodificar,
independientemente de la biblioteca de origen o del nombre del archivo.

La importacion y edicion legacy tambien intentan guardar FLAC automaticamente:
usan primero los codecs integrados y, en escritorio normal, un FFmpeg ya
instalado como respaldo para otros formatos. Esta optimizacion no descarga
encoders. Si el formato no se puede convertir, conservan una copia del archivo
original con su extension; un fallo no elimina la fuente ni deja un FLAC parcial.
La preferencia historica `stemOptimizationEnabled` ya no modifica el resultado.
La creacion portable conserva su validacion estricta: si no puede generar el
FLAC requerido informa el error y conserva el borrador en vez de producir un
paquete incompatible.

Una portada manual referenciada como `manual:<path>` se copia al staging y se
incluye en el ZIP; si desaparecio se informa error. Una portada HTTP(S) usa la
descarga/cache existente, best-effort: si no esta disponible no impide guardar
audio y queda la referencia para reintentar. Una portada ya local se conserva.

## Editar una cancion

`BPM base` en el formulario es el BPM original de la cancion: fija la referencia
de compases y el ratio para el audio. El control de tempo del reproductor es el
valor de reproduccion. Al corregir el BPM base, el tempo guardado se mueve al
nuevo valor solo si aun coincidia con el BPM base anterior; un tempo ajustado
por el usuario se conserva. El formulario admite BPM vacio (desconocido) o de
20 a 300, el rango aceptado por el reproductor. Se pueden escribir decimales
con punto o coma (por ejemplo `120.5` o `120,5`); se conserva el valor numerico
en biblioteca, proyecto y nube, sin convertirlo en entero. El mapa de tempo automatico
de una sola region sigue el BPM y compas base corregidos; un mapa de tempo
personalizado se conserva.

La portada elegida invalida la ruta local anterior antes de empaquetar el
proyecto. En canciones legacy, la copia se lee antes de limpiar la portada
previa, incluso si origen y destino son el mismo archivo. Las dos rutas de
guardado actualizan Biblioteca y la cancion seleccionada/cargada; la barra
superior vuelve a leer la imagen si cambian fuente, ruta, fecha o tamaño.
La edicion de stems en un proyecto actualiza el manifiesto y la forma de onda
del paquete. El proyecto mantiene su ID y ruta de Biblioteca. El guardado lee
el manifiesto nuevo sin borrar stems que el motor pueda tener abiertos; si la
cancion estaba cargada, el editor libera esos stems antes de recargar el
paquete para aplicar los cambios.

## Verificacion

- `input_controls`: teclado, callbacks y espaciado; `input_controls_popup`
  valida interaccion del menu cuando el sistema concede foco de ventana,
  incluyendo tap frente a arrastre, apertura con Return y cancelacion pendiente
  de los selectores del formulario.
- `portable_flac_conversion`: formatos, precision, multicanal, progreso y errores.
- `mobile_import_staging`: copias privadas, nombres, bytes, errores y limpieza;
  los permisos de proveedores nativos requieren ademas prueba en dispositivo.
- `project_artwork`: resolucion manual/remota y empaquetado sin dependencia externa.
- `song_project_creation`: save/open reales, FLAC+PNG, optimizacion automatica
  en importacion/edicion legacy, copia FLAC sin recodificar, fallback con bytes
  originales y rollback del audio si falla la escritura de Biblioteca;
  exige directorio de datos temporal aislado antes de ejecutar persistencia.
- `song_import_wizard`: layouts movil/escritorio, borrador al recorrer los cuatro
  pasos, envio real en background con progreso y bloqueo de reentrancia; crea un
  `.pwproj`, recarga Biblioteca y reabre desde la ruta registrada sin el audio
  original. Ademas pulsa el + real de Biblioteca, comprueba pagina interna,
  redimension, cancelacion durante la transicion y retorno tras guardar mediante
  el callback de produccion del panel.
- `local_demo_access --phone-input-only`: drawer real con creación incrustada y
  edición como overlay; conserva instancia, prioridad visual/hit testing,
  borrador, selección y caret en reflows, foco cuando el SO lo concede y cierre
  explícito. No sustituye la prueba de teclado nativo.
- Validacion manual en iOS y Android: teclado, pickers de archivos, todos los
  pasos, cancion creada visible en Biblioteca y reapertura tras reinicio.

`song_import_wizard` tambien edita un `.pwproj` real, cambia BPM y portada dos
veces y comprueba Biblioteca, ajustes y reapertura del paquete. La prueba no
sustituye la validacion tactil en dispositivos reales.

Incluye fixtures sinteticos M4A/AAC y M4A/ALAC: seleccion, avance por los pasos
y conservacion del archivo original. Si hay un encoder local disponible,
comprueba conversion a FLAC, waveform, metadata, registro y reapertura sin el
M4A original; tambien rechazo de audio corrupto y limpieza del staging.

Las pruebas de persistencia y del asistente compilan sus unidades de almacenamiento con
`MTCONTROLS_ISOLATED_STORAGE_TESTS=1` y exigen
`PLAYWORSHIP_CREATION_TEST_ROOT` apuntando a un directorio temporal nuevo.
Sin aislamiento se omite antes de escribir. El binario de produccion ignora
esta variable y mantiene las rutas habituales de preferencias y Biblioteca.

## Catálogo externo retirado

La creación y edición usan título, artista, BPM y tono introducidos manualmente.
No consultan MultiTracks/Secuencias ni descargan sus portadas. Elegir audio y
portadas locales sigue disponible. Esta regla aplica a todas las distribuciones.

## Nueva canción en blanco

Detalles reutiliza la tarjeta que antes mostraba información/búsqueda de
metadatos como única acción `Crear canción en blanco`, con la descripción
`Empieza sin pistas de audio y usa el clic y los pads.`. No hay un botón adicional
al pie. La tarjeta completa acepta ratón, toque y teclado, y se desplaza con los
campos dentro del viewport; mantiene su texto accesible y se bloquea al guardar.
Usa los metadatos habituales y la misma resolución
de destino/transacción background que la importación. Si el título o BPM están
vacíos, propone `Canción en blanco` y 120 BPM; un BPM explícito inválido sigue
rechazándose. Guarda, registra en Biblioteca y cierra el asistente directamente,
sin pasar por Archivos, Mapeo, Revisar ni otra pantalla. Siguiente conserva el
flujo normal de importación y sus validaciones de audio. La tarjeta sólo está
disponible en Detalles al crear un proyecto, y se oculta al editar una canción
o avanzar a Archivos. Cancelar el destino permite reintentar.

No hay un selector Libre/Con secciones ni un formulario de click/pad/secciones.
Esos controles siguen siendo los del reproductor. El proyecto nuevo tiene cero
stems, cero secciones y duración abierta hasta Detener, sin WAV silencioso ni
bins de waveform. El metrónomo usa los ajustes iniciales habituales de un proyecto;
el pad conserva su comportamiento global y sigue la tonalidad mediante el panel
existente. No se crea una preferencia de pad por canción ni se fuerza una nota
al cargar desde el motor.

El modelo requiere `blankSong` explícito; una referencia antigua sin audio no
se convierte en canción en blanco. `.pwproj`, Biblioteca y setlists utilizan
sus rutas habituales. Se conserva la lectura del objeto opcional `blankSong`
con `bars`, `clickEnabled` y `padEnabled`, y del ajuste opcional
`settings.blankPadEnabled`, para poder reabrir proyectos del primer prototipo
recuperable en `11f804d`. El nuevo botón usa `bars = 0`; ningún valor histórico de compases fija duración.
Los campos históricos de acompañamiento no sustituyen los controles habituales. Los proyectos con
audio conservan el esquema y comportamiento existentes.

La canción en blanco no admite secciones ni mapa regional de tempo en esta
versión. Su zona de onda se colapsa a altura cero y libera espacio al mixer:
no muestra señal, regla,
cursor, compases, tarjetas ni aviso para importar audio. Se ocultan Editar,
Onda/Secciones y sus opciones de menú, también en Cliente. El reloj de la barra
superior muestra sólo el tiempo transcurrido y no abre ajustes Inicio/Fin.
Click, pad, tono y tempo mantienen sus controles habituales. Al cargar de nuevo
una canción con audio se restauran sus vistas, edición y acciones existentes.
Agregar stems desde la edición habitual también convierte la misma canción a
metadatos de audio (`blankSong = false`), conservando su ID y proyecto. El área
normal reaparece al aplicar esa carga; el layout no reinicia el transporte.

Los metadatos históricos de compases/secciones del prototipo siguen siendo
legibles, pero no definen duración ni se ejecutan en una canción en blanco.
La creación/redimensión de secciones, regla ampliable y Fin automático/manual
para este tipo quedan fuera del alcance de la versión. El análisis anterior
se conserva en el historial `a25bcd9`; no hay una fase pendiente que ejecutar.

### Regresión del editor superpuesto y teclado — 2026-10-25

Se reprodujo en iPhone Simulator iOS 26.5: al mostrar el teclado del sistema en
Título, Biblioteca cubría el editor aunque éste seguía abierto. La revisión de
la compilación corregida comprobó editor visible con QWERTY y NumberPad,
escritura mediante una tecla táctil real, transferencia Título → Artista → BPM,
Listo para ocultar el teclado y X para regresar a Biblioteca. El borrador de QA
se descartó al cerrar; la canción guardada no cambió.

El gate enfocado `--phone-input-only` pasó sus 91 comprobaciones sobre drawer,
creación y edición reales. La fixture consume la expansión inicial de la ventana
del host macOS antes de simular los reflows de teléfono.

Capturas antes/después y log del gate: `output/ios-song-editor-focus/` del checkout
local de QA.
Esta prueba acredita el simulador de iPhone; no una instalación física.
