---
type: Import Contract
title: Song Creation
description: Creacion portable compartida entre escritorio y movil
resource: src/ui/panels/SongImportWizardComponent.cpp
timestamp: 2026-09-23T00:00:00-03:00
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
El `+` del encabezado de Biblioteca en escritorio abre Detalles como pagina
interna de `SongListPanel`, a continuacion de Biblioteca en su `PageContainer`.
El asistente ocupa los mismos limites del panel y sigue sus redimensiones;
no crea un modal sobre el editor. Cancelar o completar el guardado vuelve a la
misma Biblioteca, conservando su contexto de busqueda/seleccion. La lista se
recarga antes del retorno al guardar. Los accesos externos y la edicion mantienen
su presentacion previa.

Los controles compartidos son JUCE; teclado y area segura solo aplican al telefono.

## Guardar

1. Validar titulo y archivos incluidos existentes; en ambas plataformas deben ser legibles
   por JUCE (WAV, AIFF, FLAC o MP3). Revalidar antes del guardado definitivo.
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
La edicion conserva su ejecucion existente, con proteccion contra reentrancia.

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
Escritorio sigue usando FFmpeg para empaquetar FLAC. La creacion limita su
selector y validacion a los mismos WAV/AIFF/FLAC/MP3 del movil; la edicion
conserva sus formatos adicionales M4A, AAC, OGG y WMA.

Una portada manual referenciada como `manual:<path>` se copia al staging y se
incluye en el ZIP; si desaparecio se informa error. Una portada HTTP(S) usa la
descarga/cache existente, best-effort: si no esta disponible no impide guardar
audio y queda la referencia para reintentar. Una portada ya local se conserva.

## Verificacion

- `input_controls`: teclado, callbacks y espaciado; `input_controls_popup`
  valida interaccion del menu cuando el sistema concede foco de ventana.
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

Estas pruebas de creacion no certifican la edicion posterior de un `.pwproj`,
ni sustituyen la prueba tactil en dispositivos reales.

Las pruebas de persistencia y del asistente compilan sus unidades de almacenamiento con
`MTCONTROLS_ISOLATED_STORAGE_TESTS=1` y exigen
`PLAYWORSHIP_CREATION_TEST_ROOT` apuntando a un directorio temporal nuevo.
Sin aislamiento se omite antes de escribir. El binario de produccion ignora
esta variable y mantiene las rutas habituales de preferencias y Biblioteca.
