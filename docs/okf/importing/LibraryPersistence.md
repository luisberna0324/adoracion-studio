---
type: Storage Contract
title: Library Persistence on iOS
description: Conservacion y reubicacion de canciones locales al actualizar builds de iPhone y iPad
resource: src/data/SongLibraryStore.cpp
timestamp: 2026-10-12T00:00:00-03:00
---

# Library Persistence on iOS

## Variante Mac App Store

Con `MTCONTROLS_MAC_APP_STORE=ON`, la Biblioteca vive en el contenedor de App
Sandbox. Stems y portadas elegidos desde paneles nativos se copian primero al
borrador privado y luego al proyecto administrado. Abrir un `.pwproj` copia
el paquete a `imports/projects` antes de registrarlo; reabrir tras reinicio no
depende de permisos persistentes al archivo externo.

Convertir a proyecto conserva el proyecto registrado dentro del contenedor.
Elegir ubicacion externa produce una copia exportada; los cambios siguientes
se guardan en Biblioteca. Una exportacion fallida conserva el proyecto interno.
La conversion a FLAC de esta variante usa codecs integrados, incluido
CoreAudio para entradas M4A. El perfil desktop normal conserva su comportamiento.
Ver [Mac App Store](../release/MacAppStore.md).

## Importar un proyecto desde disco

El selector de `.pwproj` y la apertura recibida desde el sistema usan
`SongListPanel::beginProjectImport`. Antes de leer el paquete se presenta el
overlay compartido `SongLoadProgressOverlay`, con porcentaje y fase. La copia
desde un proveedor de documentos (iOS, Android y Mac App Store), la validación
del manifiesto y la extracción se ejecutan fuera del message thread. El URL
original se conserva por valor durante la copia para mantener su permiso de
acceso; `MobileImportStaging` valida longitud, escritura y cierre antes de
confirmar la copia privada. El archivo fuente nunca se modifica.

El progreso se basa en bytes copiados y extraídos, con un único publicador
desde el hilo llamador de `ProjectStore::open`, aunque la extracción use hasta
cuatro workers. Mientras espera esos workers se publica como máximo cada
75 ms, además de al finalizar uno; la interfaz recibe actualizaciones
agrupadas mediante `callAsync`. No se bombea un bucle modal durante la copia
o extracción. La barra separa copia (cuando corresponde), validación/extracción,
registro y preparación final de reproducción.

El registro en Biblioteca, la adición al setlist solicitado y la activación
siguen en el message thread para conservar la transacción read-modify-write
existente. El editor recibe el `ProjectDocument` ya validado y no reabre el
ZIP para esa activación. La importación explícita consume ese documento incluso
si coincide con el proyecto cargado; la deduplicación de navegación habitual
no descarta esa carga. La preparación final del motor mantiene su contrato
actual de cambio de canción; no se mueve el callback de audio ni se cambia
la política de autoplay. Durante la importación se bloquean cargas reentrantes
y se difieren las pendientes de inicio o del sistema operativo.

Cerrar/destruir el panel o pasar a biblioteca remota invalida la entrega del
worker; este conserva sus entradas hasta terminar, sin acceder a componentes.
Un fallo retira el overlay y muestra un `AppModal` con el error. Sólo se elimina
la copia privada de esa importación si no llegó a registrarse, nunca el origen,
una caché compartida o una canción existente. El overlay sigue el tamaño de su
host al rotar o redimensionar.

## Optimizacion automatica de almacenamiento

Al importar o editar una cancion legacy se intenta convertir a FLAC con codecs
locales; los FLAC existentes se copian sin recodificar. No hay preferencia de
usuario para desactivarlo, y los valores antiguos de `stemOptimizationEnabled`
se ignoran. El flujo no interpreta rutas de otras aplicaciones ni requiere un
nombre de stem particular para reutilizar audio FLAC.

Si los codecs integrados no admiten la entrada, escritorio puede usar un encoder
ya instalado; optimizar una cancion legacy no descarga ejecutables. Si tampoco
puede convertir, se copia el original con su extension y se elimina cualquier
salida FLAC incompleta. Los archivos fuente externos permanecen intactos.
La edicion conserva el staging y backup: la nueva carpeta de audio solo se
confirma junto con `songs.json`, y un fallo al persistir restaura la anterior.
Si tambien falla restaurar la carpeta original (por ejemplo, permisos o un
archivo bloqueado), se conserva el backup y el error muestra su ruta para
recuperarlo; la limpieza de un fallo nunca elimina ese unico respaldo.
No se convierten canciones existentes al arrancar ni al leer Biblioteca.
Los proyectos `.pwproj` conservan su contrato de audio FLAC obligatorio y su
transaccion de guardado/reapertura. La cache de decodificacion `StemCache`
conserva su politica de rendimiento; no depende de esta optimizacion de espacio.

## Persistencia en iOS

Una instalacion de actualizacion de PlayWorship debe conservar el contenedor de
datos existente. El flujo de despliegue nunca debe desinstalar la aplicacion
antes de instalar el nuevo bundle, porque una desinstalacion elimina el sandbox
y no puede recuperarse solamente desde el dispositivo.

iOS puede cambiar el UUID del contenedor de datos al reemplazar un build de
desarrollo aunque preserve su contenido. `songs.json` contiene rutas absolutas
para stems, artwork y proyectos administrados; por eso, al cargar la biblioteca,
`SongLibraryStore` reubica cualquier ruta inexistente que apunte dentro de un
root anterior `MTControls` hacia el mismo path relativo dentro del root actual.

La reparacion se limita a archivos existentes y no vacios que permanecen debajo
de `SongLibraryStore::getRootDirectory()`. Las rutas externas seleccionadas por
el usuario no se reinterpretan. La biblioteca solo elimina una entrada local no
portable despues de intentar esta reubicacion y confirmar que ningun stem
incluido puede reproducirse.

## Validacion

1. Crear o descargar una cancion y registrar la cantidad de entradas y stems.
2. Instalar un bundle nuevo con el mismo `CFBundleIdentifier`, sin desinstalar.
3. Confirmar que `songs.json` y los blobs siguen presentes aunque cambie el UUID
   del contenedor.
4. Abrir la aplicacion y comprobar que las rutas se reescriben al UUID actual,
   que la cancion permanece en Biblioteca y que puede reproducirse.
5. Repetir la instalacion para comprobar que la migracion es idempotente.

## Automatización de secciones

Cada `SongArrangementPartSnapshot` puede incluir un objeto `automation` con
ajustes dispersos de stems (identificados por `stemId`: `volume`, `gain`, `mute`)
y Pads (`volume`, `gain`, `enabled`, `noteIndex`, `octave`). Un campo ausente
mantiene la mezcla base; `0`, `false` y la nota `-1` (detener pad) son ajustes
explícitos. No se guardan Solo, Master ni ruteo dentro de una sección.

La representación común de `SongLibraryStore` conserva estos valores en
Biblioteca, setlists legacy y `manifest.json`. `ProjectStore` reutiliza ese
serializador para el sidecar y las versiones canónicas de ajustes Cloud. Una
edición que cambia sólo automatización cuenta como cambio persistente, y la
copia de una sección tiene valores independientes de la original.

Canciones anteriores sin `automation` se leen con un snapshot vacío. El decoder
compartido valida tipos, números finitos y rangos: volumen `0..1`, ganancia de
stem `0..2`, ganancia de Pads `0..1.2`, nota `-1..11` y octava relativa `-3..3`. Acepta como
máximo 512 stems con identificadores únicos y no vacíos. Una entrada inválida
rechaza el bloque completo sin actualizar parcialmente el destino; al abrir
una canción local se conserva la sección y se ignora su bloque inválido.


## Descargas de la nube compartida de Teams

Las descargas se identifican por cuenta, equipo, canción y revisión. Cada
revisión tiene un ID local independiente; descargar una actualización no
reemplaza la canción cargada ni los ajustes personales de la anterior.
Teams registra una canción ordinaria en Biblioteca sin crear ni activar
setlists. Se puede añadir a listas personales mediante el flujo habitual.
Las canciones disponibles se muestran en Nube junto a las propias. Descargar
desde el selector de canciones puede agregarlas al setlist solicitado, como
cualquier descarga Cloud, manteniendo la cola y selección de reproducción.

Recuperar un stem perdido conserva metadatos y ajustes locales. La vista
verifica todos los stems antes de indicar Descargada, incluso los excluidos.
Los snapshots visibles filtran las copias de otras cuentas; la lectura interna
antes de escribir conserva todas las cuentas y no borra sus datos. Las copias y
setlists existentes se conservan. Ver [Teams](../integrations/Teams.md).


### Baja confirmada de Teams

TeamsPreparationService::removeTeamDownloads elimina revisiones por vínculo de
procedencia validado y usa deleteSong para quitar referencias de todos los setlists.
Conserva blobs usados por canciones restantes y archivos externos. Persiste inventario
antes de borrar para reintentar fallos. El editor descarga del motor la canción afectada
solo después de detener reproducción; no limpia ante fallos de red ni de suscripción.

## Portadas con fuentes históricas remotas

Abrir o guardar una canción no descarga portadas remotas. Se conserva la portada
local existente y la metadata de origen; si falta el archivo, se usa el estado sin
portada. Elegir manualmente una imagen local sigue copiándola al proyecto.

## Canciones en blanco

El objeto opcional `song.blankSong` (`bars`, `clickEnabled`, `padEnabled`)
identifica explícitamente el nuevo tipo. La reproducción siempre es libre;
`bars` se conserva como metadata histórica sin efecto en duración. No se infiere a partir de `stems: []`; las referencias antiguas sin
archivos conservan su significado. `ProjectStore` permite un paquete sin audio
sólo si este contrato es válido. No incluye `audio/` ni `waveform.bin`.
Los datos históricos de secciones siguen siendo legibles, pero el motor y la
UI no los activan en una canción en blanco. Las canciones normales conservan
el codec de secciones, IDs y automatización existente. La preferencia
opcional histórica `settings.blankPadEnabled` sigue leyéndose y conservándose
en paquete/sidecar por compatibilidad con el prototipo `11f804d`; la creación
simplificada no la añade ni la aplica como un control distinto del pad global.
Los campos históricos `clickEnabled`/`padEnabled` no reemplazan los ajustes
habituales del reproductor. El botón nuevo guarda cero compases y arreglo vacío.
