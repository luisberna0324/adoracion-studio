---
type: Import Contract
title: Song Creation
description: Creacion portable compartida entre escritorio y movil
resource: src/ui/panels/SongImportWizardComponent.cpp
timestamp: 2026-09-26T00:00:00-03:00
---

# Creacion de canciones

El asistente mantiene un borrador al recorrer Detalles, Archivos, Secciones
(mapeo de pistas) y Revisar. Retroceder no descarta nombres, categorias ni la
seleccion Cargar. Cancelar el selector de archivos conserva los archivos previos.

En telefonos iOS/Android el asistente ocupa toda la pantalla vertical y respeta
area segura/teclado. Detalles tiene viewport; Archivos una lista desplazable;
el mapeo filas apiladas; Revisar un resumen desplazable. Agregar cancion en
escritorio usa esa misma jerarquia, indicador Detalles/Archivos/Secciones/Revisar
y navegacion inferior. Solo reduce controles (campos de 36 px, filas de archivos
de 48 px y mapeo de 80 px). La edicion de escritorio conserva su disposicion.
El `+` del encabezado de Biblioteca abre Detalles como pagina interna de
`SongListPanel` en escritorio, telefono y tablet, a continuacion de Biblioteca en su
`PageContainer`. La pagina entra desde la derecha con la misma transicion
horizontal de Biblioteca; al pulsar `X` o terminar el guardado, sale hacia la
derecha mientras reaparece Biblioteca. El asistente ocupa los mismos limites
del panel y sigue sus redimensiones. La pagina de creacion se libera al terminar
la animacion de retorno. Se conserva el contexto de busqueda/seleccion y la
lista se recarga antes del retorno al guardar. La creacion incrustada usa el
formulario desplazable tambien en tablets y escritorios tactiles, de modo que
sus controles se ajustan al ancho del panel. Los accesos externos y la edicion
mantienen su presentacion previa.

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
de los editores conserva el comportamiento original de JUCE. BPM usa NumberPad
en iOS, con la accion Listo de la barra para terminar la entrada. La configuracion
falla si cambia el codigo fuente esperado de JUCE, para revisar esta adaptacion
al actualizar la dependencia. Android reutiliza la actualizacion del IME de JUCE.
Referencias: [UIKit reloadInputViews](https://developer.apple.com/documentation/uikit/uiresponder/reloadinputviews()),
[Android keyboard visibility](https://developer.android.com/develop/ui/views/touch-and-input/keyboard-input/visibility).

## Guardar

1. Validar titulo y archivos incluidos existentes. WAV, AIFF, FLAC y MP3 deben
   ser legibles por JUCE. Escritorio admite tambien M4A (AAC o ALAC); si la
   plataforma no dispone de lector nativo, su decodificacion se valida en el
   worker de creacion antes de guardar. Revalidar antes del guardado definitivo.
2. Elegir un nombre disponible en `MTControls/projects/` mediante
   `SongProjectCreation::resolveDestination`, sin dialogos adicionales. El resumen
   informa que se creara un `.pwproj` en Biblioteca; no sobrescribir homonimos.
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
Escritorio mantiene su selector y sus rutas locales existentes.

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
20 a 300, el rango aceptado por el reproductor. El mapa de tempo automatico
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
- `song_project_creation`: save/open reales, FLAC+PNG y fallos sin sobrescritura;
  exige directorio de datos temporal aislado antes de ejecutar persistencia.
- `song_import_wizard`: layouts movil/escritorio, borrador al recorrer los cuatro
  pasos, envio real en background con progreso y bloqueo de reentrancia; crea un
  `.pwproj`, recarga Biblioteca y reabre desde la ruta registrada sin el audio
  original. Ademas pulsa el + real de Biblioteca, comprueba pagina interna,
  redimension, cancelacion durante la transicion y retorno tras guardar mediante
  el callback de produccion del panel.
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
