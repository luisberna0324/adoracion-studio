---
type: Integration Contract
title: Teams — nube compartida
description: Usuarios del equipo y descarga de canciones de la nube del propietario.
tags: [teams, cloud, canciones, offline]
timestamp: 2026-10-15T00:00:00Z
---

# Teams

Teams permite que los usuarios de un equipo vean y descarguen las canciones que
su propietario tiene en Cloud. Subir una canción mediante el flujo habitual de
Cloud la hace visible al equipo automáticamente. La nube compartida es implícita
por cuenta: no se crean ni nombran equipos. Teams no tiene publicaciones,
setlists compartidos, invitaciones ni roles de coordinación.

## Suscripción y permisos

El cliente tiene disponible el soporte de Teams (`teamsFileSharingAvailable = true`).
No descarta por una pausa global las canciones compartidas que devuelve el catálogo.
Esto no concede acceso a todas las cuentas: el backend conserva la autorización
de cada fuente, membresía y descarga. Los usuarios sin permiso ni membresía no
obtienen canciones ajenas ni acciones de administración. Un miembro puede ver y
descargar con `canManageSharedCloud:false`; esa capacidad solo habilita administrar.
Las compilaciones anteriores que contienen la pausa fija necesitan actualizarse:
cambiar una membresía en Firestore no modifica el código ya instalado.

Administrar el acceso a la nube compartida requiere que el perfil de suscripción del usuario
contenga `subscriptionFeatures.teams === true`, acceso Cloud vigente y correo
verificado. El valor ausente o falso no habilita Teams. La aplicación no puede
activar ese perfil ni concederlo por tener Cloud; el backend lo comprueba en
cada acción. La configuración del perfil se realiza del lado del servidor.

Los índices conservados de Teams explícitos anteriores no cuentan como otra
nube compartida cuando el backend comprueba su identidad y membresía antigua.
Se preservan esos registros. Los slots y las membresías de nubes compartidas
actuales siguen imponiendo el límite de un solo Team por cuenta; las referencias
desconocidas o inconsistentes no se descartan para conceder acceso.

La nube propia aparece automáticamente al tener el perfil habilitado. El
propietario agrega una cuenta existente por correo y puede retirarla. Los
miembros solo ven canciones y solicitan su descarga. No necesitan Cloud personal
para descargarlas: el acceso depende de la nube y del perfil Teams del propietario.
El listado del equipo no revela la lista de usuarios a sus miembros ordinarios.

## Vista y orientación

Configuración → Teams aparece para administrar cuando `canManageSharedCloud` es
booleana y verdadera, y para salir cuando existe una membresía confirmada. Por defecto está
oculta. El panel administra solo usuarios: agregar por correo y quitar con
confirmación. No tiene canciones, selector de nube ni formulario de creación.
Una respuesta ausente, inválida o revocada no concede el menú ni sus acciones.

Las canciones compartidas aparecen junto a las personales en la vista normal
Nube de Biblioteca. Usan búsqueda, filas, descarga y agregado a setlists del
flujo habitual. Los miembros tienen Configuración → Teams únicamente para consultar su acceso y salir. Descargar registra una
canción ordinaria sin abrir otra vista ni cambiar la canción o lista activa.

En teléfonos iOS y Android el panel es vertical. Al cerrarlo, la política
compartida de orientación vuelve al reproductor horizontal, o conserva vertical
si sigue abierto otro panel que la requiere. Tablets y escritorio conservan su
layout. Biblioteca y Setlists mantienen su navegación habitual, sin un selector
de espacios Personal/Equipo ni acciones para compartir publicaciones.

## API y límite de red

`src/data/TeamsClient.{h,cpp}` renueva el ID token Firebase y envía
`Authorization: Bearer`. `list` usa `GET /api/teams` y recibe
`{teams,capabilities:{canManageSharedCloud}}`. `teams` identifica internamente
las nubes a las que se tiene acceso; cada propietario tiene un ID estable
`shared-cloud-<sha256(ownerUid)[:32]>`. El listado no crea documentos. Las
operaciones POST son `details`, `add-member`, `remove-member` y `download`.

`/api/song-sync/songs` entrega el catálogo personal y las fuentes compartidas
autorizadas. Cada resumen compartido añade `teamId`, `ownerUid` y `ownerEmail`;
`songId` sigue siendo el ID remoto original. La identidad de la fila y de sus
acciones es `shared-cloud:<teamId>:<songId>` para distinguir IDs repetidos entre
propietarios. Las filas personales conservan su ID habitual. Búsqueda y
paginación se aplican al catálogo conjunto y el servidor revalida fuentes al
entregar cada página. Un miembro puede consultar sus fuentes compartidas sin
tener cuota Cloud propia.

`/api/song-sync/capabilities` añade `canManageSharedCloud` para Configuración.
La app consulta esta capacidad al abrirla, oculta Teams durante la consulta y
comprueba la cuenta antes de aplicar la respuesta.

`details` entrega `{team,members,songs}`; el propietario recibe sus usuarios,
los miembros reciben `members: []`. Cada canción corresponde a los metadatos
actuales de Cloud del propietario. `download` reutiliza el paquete Cloud y las
URLs firmadas de los objetos originales, sin duplicar audio en el servidor.
Una revisión cambiada mientras se descarga exige actualizar el listado; una
canción eliminada del Cloud deja de estar disponible para nuevas descargas.
Configuración solicita `details` con `includeSongs:false`: devuelve usuarios y
`songs:[]` sin recorrer el catálogo ni depender de su tamaño. La omisión del
campo conserva la respuesta anterior para clientes compatibles.

La procedencia solo cambia la autorización y ruta de descarga: el mismo
control de Nube usa `TeamsPreparationService` para una fuente compartida. El
vínculo exacto por cuenta/fuente/canción/revisión determina si ya está
descargada. Una compartida no se resuelve con el índice Cloud personal y no
ofrece borrar la nube del propietario, sincronizar sus ajustes ni administrar
sus versiones. Los cambios locales siguen el comportamiento normal.

Las solicitudes de metadatos se ejecutan en un pool de dos hilos. Los callbacks
llegan al message thread y `cancelAll()` invalida trabajo y callbacks pendientes.
Al cambiar de cuenta se cancela el cliente antes de presentar datos nuevos. No
se registran tokens, URLs firmadas ni cuerpos de solicitudes. Las respuestas
JSON tienen un límite de 4 MiB y los errores se entregan como
`CloudTransferError` con código HTTP, código público y mensaje.

`CloudTransferClient::download` transporta los objetos firmados y comprueba sus
hashes. El servidor valida membresía y acceso del propietario antes y después
de firmar. Retirar un usuario impide emitir nuevos paquetes. Una URL ya emitida puede conservar su vigencia de 15 minutos. Las apps actualizadas
procesan la baja confirmada y eliminan las descargas locales cuando sea seguro.

## Caché y descargas locales

`TeamsLocalStore` guarda solo metadatos dentro de
`MTControls/teams/<sha256(uid)>/`. Los sobres validan cuenta y equipo, y excluyen
credenciales y URLs. Las escrituras son atómicas; un índice corrupto se conserva
para diagnóstico en vez de sustituirlo silenciosamente.

Cada descarga tiene una identidad local por cuenta, equipo, canción y revisión.
Repetirla conserva metadatos y ajustes locales; descargar una versión nueva no
sobrescribe la que está sonando ni activa otro setlist. La procedencia es
interna: no genera listas de preparación. La lectura visible filtra copias de
otras cuentas y las escrituras conservan el snapshot completo.

`TeamsPreparationService` materializa el mismo manifiesto Cloud usado por las
descargas personales y comprueba todos sus stems. Si se pierde audio local,
puede recuperar los archivos conservando los cambios personales. Las copias ya
existentes, incluidos setlists de versiones anteriores, no se borran al
simplificar Teams.

## Verificación

Las pruebas aisladas cubren permisos de suscripción y propietario, operaciones
eliminadas, listado automático de Cloud, retirada de membresía y firma de
paquetes, UI de propietario/miembro sin creación de equipos, descargas
idempotentes, edición local,
recuperación de audio, aislamiento entre cuentas y preservación de la selección
activa. El harness usa componentes reales con metadatos ficticios. Transferir
objetos R2 con dos cuentas reales e instalar en dispositivos constituyen
comprobaciones separadas.

`teams_panel` exige que el soporte compartido esté disponible y que listar o
descargar sin credenciales devuelva `INVALID_REQUEST` antes de acceder a la red.
Conserva los casos de propietario, miembro, capacidad inválida o revocada y
confirmación de retirada. `teams_workspace` comprueba las filas compartidas en
la Biblioteca normal y la ausencia de acciones de escritura sobre la nube ajena.

### Evidencia de la versión anterior al catálogo unificado — 2026-10-04 UTC

- Compilación de escritorio firmada y compilación iOS Debug: aprobadas.
- CTest: 6/6 (`teams_local_store`, `teams_preparation`, `teams_panel`,
  `teams_workspace`, `cloud_sync_index`, `settings_panel`).
- Backend: 35/35 Teams, 13/13 Paddle, 3/3 secret-cache, typecheck y build.
- Diez capturas de los componentes reales verifican propietario, miembro,
  usuarios, descarga, confirmación, caché y estado vacío en escritorio/teléfono.
- iPhone 16, UDID `00008140-000C7D213A3A801C`: firma validada, ausencia del
  teléfono en USB comprobada, instalación y apertura por Wi-Fi aprobadas.
  Esto acredita entrega y apertura; no una revisión visual del teléfono físico.
- No se asignaron funcionalidades a cuentas reales. Sigue pendiente probar el
  acceso autenticado y la descarga R2 con dos cuentas autorizadas.
- Backend publicado en `api.playworship.app`, revisión
  `playworship-00041-giv` al 100 %. Cloud Build
  `1b8f8a7b-dfa2-4766-a786-c6cd4b543640` aprobado; web y login HTTP 200,
  operaciones Teams sin sesión HTTP 401, antes y después de activar tráfico.

### Evidencia del catálogo unificado — 2026-10-04 UTC

- Configuración solo habilita Teams con `canManageSharedCloud:true`. El panel
  administra usuarios; las canciones compartidas se muestran en Nube con las
  personales y usan descargar/agregar al setlist en el flujo habitual.
- Compilación final de escritorio firmada e iOS Debug: aprobadas.
- CTest: 6/6 (`teams_local_store`, `teams_preparation`, `teams_panel`,
  `teams_workspace`, `cloud_sync_index`, `settings_panel`). Se verificaron
  permisos, revocación, IDs repetidos entre fuentes, revisión exacta, menús de
  lectura y adición al setlist personal sin cambiar la reproducción activa.
- Backend: 49/49 Teams y catálogo Cloud, 13/13 Paddle, 3/3 secret-cache,
  typecheck y build. Cloud Build
  `d7a53da2-f15f-418e-b37a-7e074ec5fdf0`: `SUCCESS`.
- Capturas de componentes JUCE reales, con datos ficticios, revisadas para
  administración de usuarios y Nube en teléfono/escritorio. No constituyen
  revisión visual del teléfono físico ni una transferencia R2 real.
- iPhone 16, UDID `00008140-000C7D213A3A801C`: firma validada, ausencia en USB
  comprobada, instalación y apertura finales por Wi-Fi aprobadas.
- Backend `api.playworship.app`, revisión `playworship-00043-jix`: 100 % del
  tráfico. Digest de imagen:
  `sha256:cf42c20fa4a071f048360497c0551531ed1afd62cf04c78502f6ff19c3f47df6`.
  Se conservaron los nombres de configuración y la identidad del runtime.
- Antes y después de activar tráfico: raíz y login HTTP 200; catálogo Cloud,
  capacidades y consulta de usuarios sin sesión HTTP 401.
- No se asignó el perfil Teams a cuentas reales. Sigue pendiente validar el
  flujo autenticado y la descarga R2 con dos cuentas autorizadas.


## Bienvenida y aprendizaje de nuevos miembros

La app consulta `list` después de iniciar sesión y cada 60 segundos mientras el
editor está activo. Cada resumen de miembro incluye `membershipId`: una nueva
alta genera un UUID; para registros existentes se usa `joinedAt`, sin migrar
ni escribir durante la lectura. Solo se devuelve la identidad de la membresía
del solicitante. Detectar una nube no depende de que contenga canciones.

Una respuesta válida habilita la bienvenida «Tienes acceso a una nube compartida».
Se pospone durante reproducción, carga, modo remoto, otros paneles/diálogos,
una guía abierta o cuando la app no está al frente. «Ver canciones» abre la
Biblioteca en Cloud e inicia la guía existente; «Más tarde» conserva el curso
pendiente en Aprender PlayWorship. La notificación por correo del backend se
describe más abajo y es independiente del progreso local.

El ID del curso es un hash de cuenta, fuente y membresía. El progreso se guarda
localmente por dispositivo en `ProgressStore`; no se sincroniza entre equipos.
Una nueva alta después de una baja recibe otro ID y su propia bienvenida.
Cambiar de cuenta cancela solicitudes, limpia fuentes y cierra la bienvenida
anterior. Fallos de red conservan los cursos pero bloquean su avance hasta
revalidar acceso; una revocación confirmada retira el curso activo.

Los tres pasos comprueban Cloud abierto, una descarga local utilizable de esa
fuente y su presencia en un setlist personal. Se aceptan descargas ya existentes.
La comprobación usa procedencia exacta por cuenta/fuente y archivos utilizables,
no una fila ni el inicio de una descarga. Solo se consulta almacenamiento durante
la guía activa y sin reproducción, como máximo cada 1,5 segundos. Ningún paso
carga ni reproduce la canción automáticamente. La nube vacía deja descargar
pendiente y permite salir para retomar después.

### Verificación del onboarding — 2026-10-05

- Compilaciones finales macOS firmada e iOS Debug: aprobadas.
- CTest: 7/7 (`teams_local_store`, `teams_preparation`, `teams_panel`,
  `teams_workspace`, `learning_guide`, `learning_overlay`, `settings_panel`).
  La prueba gráfica pasó con acceso normal a los menús nativos de macOS;
  dentro del sandbox terminó durante la apertura de un menú de Settings.
- Bienvenida y tres pasos revisados en capturas de componentes reales con datos
  ficticios, en 390×844 y 1200×800. La guía queda abajo para liberar las filas.
- Backend: 51/51 Teams/catálogo, 13/13 Paddle, 3/3 secret-cache, tipos y build.
  Cloud Build `985c58bf-0689-4172-b93c-a4c45ef6bc31`: SUCCESS.
- Producción: revisión `playworship-00045-buq`, 100 % del tráfico, imagen
  `sha256:eca4b297000caabac73dafb9eaeb4c021a4f37483279b702272292fe816dd6dc`.
  Configuración y runtime conservados. Raíz/login HTTP 200; Teams, catálogo
  y capacidades sin sesión HTTP 401 en preview y dominio público.
- No se asignaron perfiles, agregaron miembros reales ni enviaron correos.
  Sigue pendiente el recorrido autenticado con dos cuentas y su revisión en
  un teléfono físico; esta entrega acredita compilación iOS, no instalación.


## Pertenencia única y retirada — 2026-10-05

Cada nueva alta reserva `teamMembershipSlots/{uid}` para propietario e integrante
mediante una transacción. Consulta también los índices previos dentro de la misma
transacción: no permite que dos propietarios incorporen simultáneamente a alguien,
ni que un propietario de un equipo existente se una a otro. No se migran ni eliminan
membresías históricas automáticamente. Un alta repetida devuelve ALREADY_MEMBER;
un equipo distinto devuelve OTHER_TEAM_MEMBERSHIP (409).

`leave` elimina solo la membresía del solicitante; no admite expulsar al propietario.
Sigue disponible si Cloud del propietario está inactivo. `list.memberships` devuelve
las pertenencias para esa acción, separadas de `teams`, que concede acceso al catálogo.
`remove-member` conserva autorización de propietario. Ambas bajas escriben un recibo
persistente `teamRevocations/{teamId}` con removalId nuevo; reingresar no borra el recibo.
Una caída de red, un 403 o una suscripción vencida nunca se interpretan como baja.

La app consulta recibos cada 60 s y persiste pendientes/completados por cuenta y
dispositivo. Posponer una limpieza no pierde el aviso al reiniciar. Al salir muestra
confirmación destructiva; al retirar, el propietario ve las mismas consecuencias.
Configuración del miembro permite salir, sin alta de usuarios ni modificación remota.

La limpieza espera a detener reproducción local, carga, edición o modo remoto. Descarga
la canción del motor si está cargada, cancela transferencias de Teams, elimina todas
las revisiones vinculadas y sus referencias en setlists, y refresca Biblioteca. Muestra
un aviso al completar. Preserva canciones personales, otros usuarios y archivos
compartidos con entradas restantes. El inventario de objetos descargados permite
reintentar fallos de borrado; solo borra archivos administrados dentro de cloud-content.
No borra proyectos exportados ni archivos externos. Otros dispositivos limpian al
conectarse con una versión actualizada; no se puede borrar remotamente un equipo offline.

Verificación: 53 pruebas backend; 3 pruebas CTest de almacenamiento, preparación y
panel, incluido cancelar/confirmar salida. Captura de confirmación real de componente
JUCE en teléfono: output/teams-policy-qa/leave-team-phone.png. Datos aislados, sin
retirar usuarios reales ni probar aún el recorrido autenticado en un teléfono físico.

Las nuevas transferencias usan un subdirectorio de cloud-content por cuenta y
fuente para limpiar también objetos de descargas interrumpidas. Las copias
anteriores se limpian por vínculo e inventario. Auditoría de solo lectura: un
shared-cloud, dos cuentas y cero pertenencias múltiples; no se modificaron datos.

Entrega: compilaciones finales macOS e iOS aprobadas. Cloud Build
`9d4564ab-b968-4f11-a440-bd424eaf7de0` SUCCESS; producción
`playworship-00047-gom` al 100 %, digest
`sha256:43913d1c59e2d354b3f1ac86dab2f6a193aa23ed48d64129af53fd878eb80e11`.
Preview y dominio público: raíz/login 200, Teams/catálogo/capacidades 401 sin
sesión. POST leave sin sesión: 401 en preview. No se instaló en dispositivos
ni se ejerció una baja real; apps anteriores requieren actualizarse.

## Notificaciones por correo

El backend envía vía Resend una bienvenida al agregar una cuenta, un aviso al
retirarla y una confirmación cuando sale voluntariamente. La bienvenida no
requiere aceptar invitación: dirige a Biblioteca → Nube y al flujo habitual de
descargar/agregar a setlists. Todos los correos explican la política de limpieza
y conservación de canciones personales. No cambia el onboarding dentro de la app.

La cola de correo se guarda atómicamente con el cambio de acceso, con reintentos
e idempotencia. Un fallo de Resend no revierte la operación. No se envían avisos
retroactivos a miembros existentes. La aceptación por el proveedor se registra
separada de una entrega real a la bandeja. Ver docs/operations/teams.md del backend.

Backend de correos publicado como `playworship-00049-wos` al 100 %. 60 pruebas
aprobadas y prueba real de worker/Resend aceptada en un intento, únicamente al
buzón técnico del proveedor. El 2026-10-05 se eliminó la tarea de Cloud Scheduler
por decisión del usuario: se mantiene el envío inmediato con Resend y los fallos
quedan guardados, sin reintentos periódicos. Se verificó la eliminación. No se enviaron
correos a integrantes reales para probar. Esta funcionalidad es del servidor y
no requiere otra compilación de la app para recibir las notificaciones.

## Retirada de la pausa del cliente — 2026-10-15

- Soporte Teams disponible; se conserva la autorización del backend por
  propietario, capacidad y membresía. No se modificaron cuentas ni contenido Cloud.
- Compilación macOS Release arm64 del standalone y verificación estricta de su
  firma local: aprobadas en `build/codex-modal-macos`.
- CTest: 6/6 (`teams_local_store`, `teams_preparation`, `teams_panel`,
  `teams_workspace`, `cloud_sync_index`, `settings_panel`). Pruebas del backend:
  65/65 de Teams, catálogo y correo, con fixtures sin cuentas reales.
- Biblioteca compartida y Configuración revisadas mediante capturas de componentes
  reales con datos ficticios. No acredita una sesión en el dispositivo del miembro.
- No se publicó ni instaló una nueva versión iOS/Android. Las compilaciones que
  incluyen la pausa fija siguen necesitando una actualización para mostrar Teams.
