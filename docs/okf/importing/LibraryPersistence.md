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

## Guardar y exportar en un disco externo

La creación de escritorio recupera el diálogo nativo asíncrono para elegir entre
Biblioteca local y una ubicación `.pwproj`. Cancelar conserva el borrador. Móviles
mantienen la Biblioteca administrada; Mac App Store conserva el paquete registrado
en su contenedor y entrega una copia al destino elegido.

Para canciones propias ya registradas, Biblioteca de escritorio ofrece
`Exportar copia del proyecto...`. Antes de copiar, el editor entrega sus ajustes
activos y un worker consolida las escrituras pendientes del sidecar dentro del
paquete. Un conflicto posterior a la consolidación requiere reintentar. El URL
nativo vive hasta completar el trabajo, y un temporal protege un destino existente
ante fallos de copia. Exportar no cambia `projectPath`, no mueve la Biblioteca ni
requiere conexión a Cloud. El selector pide confirmación antes de sobrescribir.
Los proyectos compartidos de Teams no incorporan esta acción.

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

## Tempo decimal

El BPM base de `ImportedSongData` se conserva como `double` en `songs.json`
y en `song.bpm` del `manifest.json` de `.pwproj`. Los documentos anteriores
con BPM entero se leen con el mismo codec; no se cambia la versión del formato.
Un BPM no finito en Biblioteca se repara a `0` (tempo desconocido); el snapshot
remoto usa `120` si falta el tempo o es no finito, conservando un `0` explícito.
Un descriptor Cloud con BPM no finito se rechaza.
Los ajustes de tempo del proyecto/sidecar y del setlist siguen siendo números
decimales independientes del BPM base. El catálogo local y Cloud, las filas de
canción y el snapshot remoto conservan esa precisión; el formato visible usa
`TempoValue::formatBpm` para evitar ceros finales innecesarios.

Al editar el BPM base, los tempos que aún coinciden con el valor base anterior
siguen el nuevo valor. Se usa una tolerancia de `0.000001` BPM para distinguirlos
de ajustes explícitos cercanos (por ejemplo, base `96.75` y override `96.755`);
las tolerancias de posiciones/duración del mapa de tempo no cambian.
Un mapa de tempo predeterminado de una sola región sigue el nuevo BPM base,
pero uno con un BPM modificado de forma independiente se conserva.

El descriptor Cloud y los metadatos de commit conservan el BPM decimal del
paquete, sin convertirlo a entero. La representación JSON sigue usando el campo
numérico `bpm`; no se altera el esquema del protocolo remoto ni del descriptor.
Cuando el BPM Cloud es un entero exacto representable como `int`, su JSON sigue
escribiendo un entero para conservar los fingerprints de descriptores históricos
y evitar que una lectura/guardado sin cambios provoque una nueva publicación.

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


## Letras móviles: texto, secciones y acordes

`ImportedSongData::lyrics` es opcional. El editor nativo de la lista de canciones
conserva texto y marcas sobre ese texto; iOS añade acordes desde su menú contextual.
No guarda vínculos con el timeline, la guía, FreeShow o `SongArrangementSnapshot`
ni añade un editor de escritorio. El esquema 1 histórico permanece sin cambios:

```json
{
  "schemaVersion": 1,
  "offsetEncoding": "utf16",
  "text": "Una letra para cantar",
  "sections": [{"id": "estable", "kind": "verse", "start": 0, "length": 9}]
}
```

El esquema 2 añade `chords` como array obligatorio, con IDs únicos y posiciones
UTF-16 globales. Por ejemplo, el acorde cae sobre la r de «esperanza»:

```json
{
  "schemaVersion": 2,
  "offsetEncoding": "utf16",
  "text": "esperanza",
  "sections": [],
  "chords": [{"id": "acorde-estable", "offset": 4, "symbol": "Am7"}]
}
```

`setSongLyricsChord` crea o sustituye un acorde en una posición única; cambiar su
símbolo conserva el ID. El primer acorde requiere el esquema 2, salvo que el
documento ya necesite el 3 por sus bloques instrumentales. Quitar el último
acorde sólo regresa al 1 si tampoco quedan bloques. Los acordes pueden estar
en mitad de una palabra o al final del
texto para un fragmento instrumental. Una posición no puede dividir pares
surrogate, tildes combinadas o secuencias de emoji comunes. El validador es
conservador y no se declara una implementación completa de UAX #29. No se añaden
espacios para alinear acordes ni se guardan corchetes dentro de la letra.
Los clientes antiguos que sólo conocen el esquema 1 no interpretan el 2/3;
los que sólo conocen el 2 no editan los bloques del 3. Antes de distribuir
estos documentos por Cloud hay que comprobar compatibilidad de los clientes
que los recibirán. La conservación opaca protege versiones
futuras en esta implementación, no modifica los binarios ya distribuidos.

El esquema 3 añade `instrumentalBlocks` y exige también el array `chords`.
Cada bloque contiene `id`, `kind` (`intro` o `instrumental`), `label` opcional,
`anchorUtf16`, `order` y `rows`. Cada fila tiene un ID y celdas con ID y símbolo
opcional; la ausencia de `symbol` representa una celda vacía, no un círculo,
una cadena de espacios o una posición inferida por píxeles. `barLengths`
opcional divide las celdas de una fila en compases de longitudes positivas;
si se omite, toda la fila pertenece a un compás.

El ancla sólo puede estar entre párrafos o en EOF. Varios bloques en el mismo
ancla conservan un orden único. `addSongLyricsInstrumentalBlock` crea una fila
real de cuatro celdas vacías y no altera `text`; Solo usa una etiqueta de un
bloque `instrumental`. `addSongLyricsInstrumentalRow`,
`setSongLyricsInstrumentalCell` y `clearSongLyricsInstrumentalCell` actualizan
sus IDs estables. Quitar todos los símbolos no elimina las filas ni el bloque.
`normalizeSongLyricsSchema` mantiene el esquema 3 mientras haya bloques; luego
elige el esquema mínimo capaz de conservar el documento.

Estas funciones y el esquema 3 se conservan por compatibilidad de datos; no
implican que la interfaz ofrezca creación o edición de celdas. iOS retira el
menú de creación de instrumentales vacíos y los controles de añadir filas,
rellenar celdas o eliminar el bloque desde esa tarjeta. Los acordes sobre letra
y el menú contextual nativo de selección permanecen independientes.

Los bloques schema 3 ya guardados o importados se muestran como progresiones de
lectura, conservando su etiqueta, ancla, orden, filas, IDs y `barLengths`. Las
celdas vacías no se dibujan como `○`; su separación se conserva en la proyección
visual para no convertir, por ejemplo, cuatro pulsos en dos cambios contiguos.
La vista no inserta tabulaciones o espacios en `text`, ni filtra celdas del
documento. Un bloque sin símbolos sigue siendo contenido estructurado y no se
borra ni se degrada de esquema al abrir, editar otra parte o guardar. Su estado
vacío debe permanecer identificable. Retirar controles no es una migración ni
una eliminación de datos; tampoco deshabilita el intercambio de grids ChordPro.

Una inserción en el ancla queda después del bloque. Una edición previa desplaza
su posición; borrar texto que lo atraviesa lo reancla sin eliminarlo. Si desaparece
el límite de párrafo, pasa al siguiente límite o a EOF conservando el orden
relativo. Borrar el bloque requiere una operación explícita sobre su ID.
No se admiten IDs duplicados entre secciones, acordes, bloques, filas o celdas.
Los límites adicionales son 128 bloques, 128 filas por bloque, 32 celdas por fila,
4096 celdas totales y etiquetas de hasta 80 caracteres. El límite total de 4096
símbolos incluye tanto acordes sobre letra como celdas instrumentales ocupadas.

`start`, `length`, `offset` y `anchorUtf16` cuentan unidades UTF-16, como `NSRange`
de UIKit. Son
independientes del índice Unicode de `juce::String`; nunca se interpretan como
bytes UTF-8. Los límites de un rango no pueden partir el par surrogate de un
carácter suplementario (por ejemplo, un emoji). El formato declara la
codificación explícitamente para que futuras interfaces nativas puedan usar
el mismo contrato. Se conserva el texto pegado, incluidos sus saltos de línea.

Los tipos canónicos son `intro`, `verse`, `prechorus`, `chorus`, `bridge`,
`instrumental`, `outro` y `ending`; sus nombres de presentación se localizan en
la interfaz. Las marcas tienen IDs únicos estables, longitud positiva, orden
canónico por inicio y no se solapan. Aplicar o quitar una marca en una selección
recorta/divide únicamente los rangos intersectados y conserva el resto. El
primer fragmento superviviente conserva el ID; un segundo recibe un ID nuevo.
Repetir la misma asignación exacta es un no-op.

La reordenación de tarjetas se resuelve en el documento estructurado con
`moveSongLyricsCardBefore(document, sourceId, beforeId)`. `beforeId` vacío coloca
el origen al final, después de la última tarjeta; el mismo ID de origen y
destino es un no-op. `movableSongLyricsCardIds` calcula la elegibilidad en lote
con una validación/lectura del documento; la interfaz puede cachearla por
revisión. `canMoveSongLyricsCard` permite consultar un ID individual.

Un origen lírico debe abarcar párrafos completos con contenido distinto de
espacios y no puede contener un ancla instrumental estrictamente en su
interior. Los rangos históricos parciales, los párrafos sólo de espacios y los
límites ambiguos dentro de CRLF no se ofrecen como tarjetas arrastrables. Se
conservan en el documento y no se convierten en párrafos completos para poder
moverlos. Un destino lírico parcial también se rechaza. Un bloque instrumental
existente puede trasladarse: cambia únicamente su ancla y orden, manteniendo
filas, celdas, símbolos y compases; un instrumental anidado en una sección de
letra no sirve como destino de reordenación.

El movimiento de letra permuta unidades de texto y reutiliza los separadores
externos reales entre ellas, incluidos LF, CRLF, huecos y EOF. Conserva los
separadores internos, el ID y tipo de cada sección y el ID/símbolo de cada
acorde, recalculando sus posiciones globales. Un acorde al final de línea sigue
al cuerpo de esa línea. No usa una secuencia de borrado/pegado que pierda
anclas. Las tarjetas y el texto que no se trasladan mantienen sus identidades
y contenido. Reordenar un instrumental no habilita su antigua edición de
celdas ni lo convierte en letra.

La operación es atómica: un ID, destino o rango no representable se rechaza
sin modificar el borrador. El gesto reordena tarjetas; no redimensiona su
contenido, no reasigna tipos a otras letras y no cambia transporte, secciones
de reproducción, tempo, tono, audio o FreeShow. La numeración visible de
versos/coros se deriva del nuevo orden de sus tipos; no sustituye los IDs
persistidos. La integración iOS debe registrar el cambio completo como una
acción de deshacer; un no-op no añade una acción al historial. El arrastre
pertenece a la interfaz nativa y el resultado sólo llega a Biblioteca/`.pwproj`
por el guardado de letra habitual; cancelar el editor descarta el borrador
completo.

`replaceSongLyricsText` recibe la edición exacta del control nativo. Una
inserción dentro de una marca amplía su rango; en sus bordes queda fuera.
Reemplazar texto contenido en una marca conserva su tipo e ID. Un reemplazo
que atraviesa varias conserva sólo sus fragmentos supervivientes y deja el
texto nuevo sin marcar, evitando asignarle un tipo arbitrario o crear solapes.
`setSongLyricsText` deriva un reemplazo común para puentes que sólo entregan el
texto final; ajusta el diff a límites Unicode válidos. Las operaciones inválidas
no modifican el borrador.

Las anclas de acordes acompañan el texto original: una inserción justo en su
posición las desplaza a la derecha. Borrar `[inicio, fin)` elimina las anclas
interiores; una en `fin` se conserva y se desplaza con el texto que sigue.
Los cambios son atómicos: si una edición dejara un ancla inválida, no se guarda
un documento parcial. Las secciones y sus reglas de pertenencia siguen siendo
independientes de los acordes.

`ChordProCodec` realiza el intercambio local de texto UTF-8/Unicode. Importa
acordes inline, incluidos los que están dentro de palabras, metadatos y los ocho
tipos de sección nativos; normaliza CRLF/CR a LF. Rechaza directivas no compatibles
y anclas inválidas antes de devolver un documento. Exportar exige secciones de
líneas completas y rechaza marcado que no pueda representar sin pérdida; no
convierte silenciosamente marcas parciales. También rechaza un rango que
etiquete sólo un LF terminal no representable, en vez de acortarlo o perder la
sección. Los grids admitidos conservan filas, símbolos, celdas vacías y compases;
se intercalan por `anchorUtf16`/`order` sin consumir letra. Se permite un grid
dentro de un entorno de letra conservando su rango global, pero no un grid
dentro de otro. El subconjunto acepta `label`, anchura `shape` simple y
separadores `|`; márgenes, repeticiones, strums y otras celdas no representables
fallan explícitamente. Los metadatos importados se entregan
separados y no actualizan tempo, tono, audio o FreeShow. El formato persistido en
Biblioteca y `.pwproj` sigue siendo el documento estructurado JSON, no el archivo
ChordPro. No se reclama compatibilidad con todas las directivas de ChordPro.

`SongLibraryStore::updateSongLyrics` valida el borrador y guarda sólo la letra.
Una canción legacy se actualiza mediante un read-modify-write breve de
Biblioteca en el message thread. Para un proyecto, el worker prepara un ZIP
con `ProjectStore::prepareLyricsUpdate`: cambia únicamente `manifest.song.lyrics`
y copia los objetos codificados sin decodificar audio ni normalizar metadatos,
mezcla o ventana. Reabre el ZIP temporal, verifica manifiesto y SHA-256 de cada
objeto, y comprueba el hash completo de la versión de origen. El commit breve
en el message thread verifica identidad del archivo (inode), tamaño, fecha,
ruta, ID y ausencia de una edición concurrente de letra; vuelve a leer
Biblioteca y cambia sólo ese campo. Un cambio concurrente del proyecto hace
fallar el guardado para reintentar. Los cambios de mezcla/setlists/metadatos
de otras canciones realizados durante la preparación permanecen intactos.

El proyecto anterior se aparta como respaldo antes del reemplazo. Si falla
Biblioteca, se restaura el original; un rollback fallido conserva el backup e
informa su ruta. Nunca se elimina ese único respaldo. No se llama a
`updateSongMetadata`, reconciliación de stems, actualización de sidecars,
`scheduleSettingsSave` ni callbacks de recarga del motor. Las lecturas y copias
grandes del ZIP y su hash permanecen en el worker, sin retener locks durante
`callSync`. La interfaz lo ejecuta al confirmar; cancelar sólo descarta el
borrador.

Como los objetos de audio y `songSettings` embebidos no cambian, esta operación
conserva el timestamp de origen del paquete y actualiza sólo un marcador de
caché que ya correspondía a esa versión. Así conserva la precedencia del
sidecar vigente y evita reextraer stems en la siguiente apertura. No altera
sus bytes ni recuerda una mezcla antigua. Cloud detecta el contenido por
SHA-256/fingerprint, no por tamaño/fecha; la regresión verifica un fingerprint
nuevo aun con timestamp idéntico. `songToVar/songFromVar` conservan el documento en Biblioteca,
manifiestos `.pwproj` y descargas Cloud. No forma parte de sidecars ni de
`ProjectSongSettings` o versiones de mezcla. Texto vacío sin secciones, acordes
ni bloques elimina el campo. Una tarjeta instrumental vacía permanece, aunque
no contenga símbolos. Canciones
históricas sin `lyrics` conservan su comportamiento. También se permite en
canciones en blanco sin stems.

Se validan atómicamente versión/codificación, tipos JSON, Unicode, límites,
IDs, catálogo, ausencia de solapes de sección y posiciones únicas de acorde.
Los límites son 262144 unidades UTF-16 de texto, 4096 secciones y 4096 acordes,
con IDs de hasta 128 caracteres y símbolos de hasta 32. Abrir un esquema 1/2/3
inválido ignora únicamente la letra y conserva la canción. Guardar un documento
inválido falla antes de reemplazar Biblioteca o paquete. Una versión superior a
3 se conserva como JSON opaco, hasta 2 MiB: puede volver a serializarse con la
metadata, pero `isEditableSongLyricsDocument` impide editarla con este editor.
No se interpreta el prototipo descartado de bloques/acordes como este esquema.

Verificación: `song_lyrics` prueba selección, recorte/división, edición, UTF-16,
anclas, JSON y límites; `chordpro` cubre el intercambio local y sus rechazos.
`song_project_creation` añade paquetes reales, Biblioteca,
metadatos actuales, cancelación de borrador, mezclas/ventanas conservadas,
rechazo sin cambios de bytes, canciones en blanco y reconstrucción Cloud sin red.

La regresión del modelo de reordenación pasó `song_lyrics` con 201
comprobaciones y `song_project_creation` con paquetes/Biblioteca reales
(2/2 pruebas, 2026-10-28). Incluye conservación de texto, acordes, IDs y
separadores, rechazo atómico de movimientos no admitidos y persistencia de la
nueva disposición. Esto verifica el modelo y el guardado; la retirada de
controles y el arrastre UIKit, su deshacer/rehacer y su ejercicio en el iPhone
físico permanecen pendientes. Una prueba del modelo no demuestra el gesto
nativo ni que la versión esté instalada.
