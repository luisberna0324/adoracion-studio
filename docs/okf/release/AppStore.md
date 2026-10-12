---
type: Release Contract
title: App Store — preflight de PlayWorship para iOS y macOS
description: Estado del registro comun y contrato de compilacion, privacidad, firma y revision para distribuir PlayWorship en iPhone, iPad y Mac.
tags: [ios, ipad, iphone, macos, app-store, privacy, release]
timestamp: 2026-10-05
---

# App Store — preflight de PlayWorship para iOS y macOS

## Ultima evidencia local de pruebas (2026-10-05)

Esta actualizacion usa `output/app-store-preparation/preparation.json` y
`output/app-store-preparation/screenshots/README.md`, evidencia local no
versionada. No se volvio a consultar Apple; el estado de procesamiento o
revision puede cambiar. Los apartados historicos conservan el estado de cada
entrega y no sustituyen este resumen.

La build iOS **1.1.4 (2)** figura pendiente de revision para pruebas externas.
La evidencia registra testers asignados e instalacion interna en iPhone 16.
Esto acredita instalacion, no QA funcional completa ni aprobacion o entrega
de invitaciones externas.

La variante Mac App Store 1.1.4 (1), universal Intel/Apple Silicon, tiene
Archive y paquete de distribucion firmados. Pasaron el validador del paquete
exportado y `mac_app_store_safety`; el inicio con sandbox llego al login.
El paquete fue entregado a Apple el 2026-10-05 a las 19:41:36 UTC; la ultima
evidencia local muestra **«Procesando»**, sin resultado final ni testers
asignados. Se cargaron y comprobaron **14 capturas**: cuatro iPhone, cinco
iPad y cinco Mac. La quinta iPhone remota sigue pendiente de sustitucion y
revision visual.
Su contrato y pendientes se documentan en [Mac App Store](MacAppStore.md).
El login con Apple sigue desactivado en esta compilacion.

## Registro comun confirmado

El 2026-10-05 se creo y comprobo en App Store Connect el registro de la app en
la cuenta de Luis Berna. Estos valores identifican la entrega:

| Campo | Valor confirmado |
| --- | --- |
| Nombre | `PlayWorship` |
| Apple ID de la app | `6819361132` |
| Bundle ID | `com.multitracks.playworship` |
| SKU | `playworship` |
| Idioma principal | Espanol (Espana), `es-ES` |
| Plataformas | `IOS` y `MAC_OS` |
| Apple Developer Team ID | `V9T23PX769` |

La ficha comun admite entregas independientes de iOS/iPadOS y macOS. Su
creacion no demuestra que exista un Archive exportado con firma de
distribucion, una carga procesada en TestFlight o aprobacion de App Review.
Los borradores de metadata y material de entrega se conservan en
`docs/okf/distribution/`.

## Contrato del binario iOS/iPadOS

- Bundle ID: `com.multitracks.playworship`.
- App universal para iPhone y iPad (`TARGETED_DEVICE_FAMILY=1,2`).
- Inicio y vista de performance en ambas orientaciones horizontales.
- En iPhone, el plist tambien permite `Portrait`. El runtime habilita
  orientacion vertical en Biblioteca, setlists, Configuracion, Teams y Ensayo
  mientras el panel correspondiente esta abierto; al cerrar restaura las dos
  orientaciones horizontales. iPad conserva el layout horizontal. Ver
  [Mobile Layout](../ui/MobileLayout.md).
- `UIRequiresFullScreen=true` solicita el modo de compatibilidad de iPad,
  con tamano de escena fijo. En iPadOS 26 con `Windowed Apps` no obliga al
  sistema a presentar la app automaticamente a pantalla completa; la ventana
  puede coexistir con otras y el sistema escala su presentacion. Esta clave
  esta deprecada. La captura iPad se preparo seleccionando `Full Screen Apps`
  en el simulador; esto no cambia el comportamiento contractual del plist.
- En iPad, `PluginEditor::resized` sustrae `safeAreaInsets` del rectangulo de
  contenido antes de distribuir TopBar, waveform, mixer y pads. El drawer y
  su scrim usan ese mismo rectangulo y comienzan debajo del TopBar. La tablet
  no recibe insets internos ni bleed del perfil de telefono. Ver
  [Area segura](../ui/MobileLayout.md#area-segura).
- `CFBundleShortVersionString` usa SemVer desde `MTCONTROLS_APP_VERSION`.
- `CFBundleVersion` es un entero creciente independiente configurado con
  `MTCONTROLS_IOS_BUILD_NUMBER`.
- `LSSupportsOpeningDocumentsInPlace=false` declara el flujo real de
  importacion por copia. En `SongListPanel.cpp`, `importProjectUrlToSandbox`
  copia el stream de la URL elegida a `SongLibraryStore` bajo
  `imports/projects`; `openProjectFileFromUrl` abre esa copia en iOS.
  La app no implementa `UIDocumentBrowserViewController`. El preflight
  exige esta declaracion explicita y rechaza `UISupportsDocumentBrowser=true`
  cuando hay tipos de documento declarados.
- El build de distribucion usa Xcode 26 o posterior y el SDK iOS 26 o
  posterior.
- `ITSAppUsesNonExemptEncryption=false` declara que la app usa solo cifrado
  estandar exento, como HTTPS y autenticacion.
- El icono App Store es PNG RGB de 1024 x 1024 sin canal alfa.

## Privacidad

`assets/ios/PrivacyInfo.xcprivacy` se copia en la raiz del bundle iOS. Declara:

- Sin tracking ni dominios de tracking.
- APIs de timestamps de archivos para archivos del contenedor y documentos
  seleccionados por el usuario (`C617.1`, `3B52.1`).
- Tiempo de arranque para temporizadores y MIDI/audio (`35F9.1`).
- Espacio en disco para decidir si se puede generar cache de pads (`E174.1`).
- Email, ID de usuario, ID de dispositivo, historial de compra, audio y otro
  contenido del usuario, vinculados a la cuenta y usados para funcionalidad.

Las respuestas de App Privacy en App Store Connect deben coincidir exactamente
con el comportamiento real de la app, sus SDKs y el backend desplegado. El
manifiesto no sustituye esas respuestas ni la politica publicada.

La inclusion en CMake corresponde a iOS y a la variante Mac App Store. Cada
entrega debe revisar su manifiesto antes de exportarse. Sentry esta
deshabilitado por defecto; si se activa en una entrega se debe revisar tambien
la recoleccion de diagnosticos y los requisitos del SDK. El informe de
reproduccion existente es un ZIP local compartido por decision del usuario,
sin carga automatica al backend. Ver
[Compartir diagnostico](../integrations/PlaybackDiagnosticSharing.md).

## Permisos

- Bluetooth se usa para dispositivos MIDI Bluetooth.
- Red local se usa para servicios compatibles de musica y presentacion en la
  red del usuario.
- Audio en segundo plano se usa para mantener la reproduccion durante una
  presentacion.
- La app no solicita microfono.

## Preflight local iOS/iPadOS

Configurar y compilar un Archive con firma automatica:

```bash
cmake -S . -B build/ios-appstore \
  -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DMTCONTROLS_IOS_TARGETED_DEVICE_FAMILY=1,2 \
  -DMTCONTROLS_IOS_DEVELOPMENT_TEAM=V9T23PX769 \
  -DMTCONTROLS_IOS_BUILD_NUMBER=<BUILD_NUMBER> \
  -DJUCE_DIR=../third_party/JUCE

xcodebuild \
  -project build/ios-appstore/MTControls.xcodeproj \
  -scheme MTControls_Standalone \
  -configuration Release \
  -destination 'generic/platform=iOS' \
  -archivePath build/ios-appstore/archive/PlayWorship-build2.xcarchive \
  -allowProvisioningUpdates archive
```

Validar el `.app` dentro del Archive:

```bash
scripts/validate_ios_app_store.sh \
  build/ios-appstore/archive/PlayWorship-build2.xcarchive/Products/Applications/PlayWorship.app \
  --allow-development-signing
```

El flag solo admite la firma de desarrollo que puede contener el Archive antes
de exportarlo. Despues de exportar con metodo `app-store-connect`, repetir el
script sobre el `.app` distribuible sin ese flag. La validacion de Apple se
completa desde Xcode Organizer o con `altool` sobre el IPA exportado.

### Simbolos de Release y empaquetado del Archive

En iOS, CMake aplica `-g` en Release a `MTControls` y `MTControls_Standalone`,
y configura `DEBUG_INFORMATION_FORMAT=dwarf-with-dsym`. La informacion de
depuracion debe existir tanto en el codigo compartido como en el wrapper
Standalone para producir un dSYM util de la app.

Si Xcode genera el dSYM fuera del Archive, antes de exportar:

1. Localizar el dSYM recien generado para ese Archive. En esta compilacion,
   Xcode lo dejo en
   `build/ios-appstore/MTControls_artefacts/Release/Standalone/PlayWorship.app.dSYM`.
2. Comparar con `xcrun dwarfdump --uuid` el ejecutable del Archive y ese dSYM.
   Los UUID y sus arquitecturas deben coincidir exactamente.
3. Solo tras esa coincidencia, copiar el dSYM a
   `<ARCHIVE>.xcarchive/dSYMs/PlayWorship.app.dSYM`.
4. Ejecutar el preflight del Archive con `--allow-development-signing` antes
   de exportar. El validador exige ahora el dSYM y compara los pares UUID y
   arquitectura con los del ejecutable.

No reutilizar dSYM de un Archive anterior aunque comparta version o nombre.
Los simbolos de Bungee requieren el dSYM correspondiente del proveedor del
framework precompilado; esta correccion no lo fabrica ni sustituye.

## Bloqueos de revision de producto

La auditoria de fuente del 2026-10-05 encontro estos puntos abiertos, comunes
a las entregas de iOS y macOS salvo donde se indica:

- **Login (4.8):** `PluginEditor::launchExternalLogin` inicia exclusivamente
  `/api/auth/google/start` y la bienvenida ofrece una sola accion de login.
  Si Google sigue autenticando la cuenta principal, agregar una alternativa
  equivalente que cumpla la regla, como Sign in with Apple, o justificar una
  excepcion aplicable. Usar exclusivamente un sistema propio de cuentas es
  otra decision de producto; aun no esta implementada.
- **Eliminacion de cuenta (5.1.1(v)):** Configuracion ofrece cerrar sesion y
  purgar datos locales. Esas acciones conservan la cuenta y el contenido
  cloud. Hace falta iniciar desde la app una eliminacion completa, mediante
  flujo nativo o enlace directo al flujo web autenticado. Un correo a soporte
  no sustituye esta capacidad. El backend y el tratamiento de suscripciones
  durante la eliminacion deben definirse y verificarse.
- **Privacidad:** no se encontro un enlace a la politica dentro de la app.
  Publicar y verificar las URLs de privacidad y soporte, e incluir acceso
  sencillo desde la interfaz. El contacto existente es
  `mailto:hola@playworship.app` en Configuracion; su existencia no verifica
  la pagina de soporte ni la entrega de emails. Los terminos deben reflejar
  el modelo de uso y cobro finalmente elegido.
- **Compras digitales (3.1):** no se encontro StoreKit/IAP. El cliente recibe
  plan, estado de suscripcion y licencia del backend y puede bloquear el
  acceso cuando no son validos. Se debe definir como se adquieren y restauran
  las funciones digitales en cada plataforma. Retirar llamadas a comprar
  fuera de la app no demuestra por si solo que corresponda la excepcion de
  app companera gratuita de un servicio web. Evaluar las condiciones de
  3.1.3 y, si no aplica una excepcion, implementar productos equivalentes
  con IAP. No se declara resuelto el modelo comercial.
- **Acceso del revisor (2.1):** preparar cuenta de demostracion o modo
  apropiado con contenido util, instrucciones de acceso y backend estable.
  No se ha verificado una cuenta dedicada de revision.

## Bloqueos adicionales de Mac App Store

El escritorio habitual y la variante Mac App Store son perfiles distintos.
La ultima evidencia local registra la preparacion y entrega de la variante
Store **1.1.4 (1)**; su contrato esta en [Mac App Store](MacAppStore.md).
Los siguientes puntos separan las correcciones incorporadas de la QA pendiente:

- **Sandbox (2.4.5):** el perfil Store incluye App Sandbox y entitlements
  para networking cliente/servidor, USB/Bluetooth y archivos elegidos por el
  usuario. El preflight paso y el inicio llego al login. Queda verificar el
  flujo completo de autenticacion loopback, importacion y apertura persistida,
  reproduccion, audio/MIDI, remote, Cloud/Teams y exportacion dentro del contenedor.
- **Firma y certificados:** el perfil Store omite la firma ad hoc del
  escritorio. El Archive universal y el paquete exportado tienen firma de
  distribucion del equipo `V9T23PX769`; el validador local paso y la entrega
  termino correctamente. El ultimo estado local de Apple es «Procesando»;
  no se acredita validacion final ni aprobacion de la tienda.
- **FFmpeg y procesos incluidos (2.5.2):** el perfil Store impide descargar
  el encoder externo y convierte stems con codecs integrados. Ofrece
  exportacion WAV de 24 bits; no ofrece MP3/M4A de salida. La prueba
  `mac_app_store_safety` paso; falta completar QA de exportacion real con
  el panel de archivos del sistema.
- **Licencias (2.4.5(vi)):** revisar la validacion propia de plan/licencia y
  separar el acceso a servicios de cuenta del desbloqueo de la app. La regla
  de Mac App Store restringe pantallas de licencia, claves y mecanismos
  propios de proteccion. Mostrar una clave en Configuracion no prueba por si
  solo una infraccion; el flujo completo debe resolverse con el modelo de cobro.
- **PrivacyInfo y categoria:** el perfil Store incluye el manifiesto y la
  categoria Music, comprobados en el preflight local. Reconciliar sus
  declaraciones con los SDKs, el backend y las respuestas de privacidad.

## Preparacion y pruebas pendientes

El 2026-10-05 se completo la preparacion local de iOS con Xcode 26.6 y SDK
iphoneos26.5, candidato version 1.1.4 (2), equipo V9T23PX769 y backend de produccion:

- Archive seleccionado: `build/ios-appstore/archive/PlayWorship-build2.xcarchive`,
  ARCHIVE SUCCEEDED; incluye las correcciones de area segura, documentos y
  generacion/empaquetado del dSYM de la app.
- IPA seleccionada: `build/ios-appstore/export-build2/PlayWorship.ipa`,
  EXPORT SUCCEEDED; 22 123 832 bytes.
- SHA-256 de la IPA seleccionada:
  `384555033f1f3ca0a64af0ad2457acefdee790e5715b1874578e7cdc28f58dea`.
- Firma exportada: Apple Distribution: Luis Berna (V9T23PX769).
- Validacion local de firma, perfil de App Store, iconos sin alfa, privacidad,
  version, familias iPhone/iPad, orientaciones y SDK: PASS de distribucion.
- Plist final comprobado: `CFBundleVersion=2` y
  `LSSupportsOpeningDocumentsInPlace=false`.
- dSYM nuevo de PlayWorship incluido en el Archive despues de comprobar
  coincidencia exacta del UUID arm64
  `FA939419-DE44-3303-9A02-6357CAD5FD59` con el ejecutable. El preflight estricto
  del Archive y de distribucion paso.
- El validador rechaza el Archive con firma de desarrollo si se intenta usar
  directamente como binario de envio. Admite iPhone portrait y valida el perfil
  exportado sin dispositivos registrados ni provisioning Enterprise.
- Opciones de exportacion locales en
  `scripts/app-store/export-options-ios.plist`, con `destination=export`.
- Evidencia y checksum del IPA en
  `output/app-store-preparation/preparation.json`. El candidato se genero con
  cambios entonces no commiteados; ese artefacto no identifica una entrega
  Git limpia aunque las fuentes se versionen despues.
- La IPA anterior de `build/ios-appstore/export/PlayWorship.ipa` y el Archive
  anterior `PlayWorship.xcarchive` se conservan localmente; **no subirlos**.
  La compilacion 1 `safearea`, ya entregada, tambien queda como historico.
  El candidato vigente es el artefacto `build2` indicado arriba.

Se guardaron en el portal los textos, palabras clave, soporte, marketing y
version 1.1.4 de ambas plataformas, el subtitulo y categoria Musica comunes, y
la URL de privacidad. Ambas versiones usan publicacion manual. Los datos de
privacidad, edad, derechos y acceso dedicado del revisor siguen pendientes de
validacion; ver
[Metadata](../distribution/AppStoreMetadata.es.md).

La entrega de la compilacion 2 esta confirmada. El comando termino con codigo
0; `/tmp/playworship-build2-upload.log` registra el 2026-10-05 a las 15:38:06,
segun la hora del propio log, `Upload succeeded`, `Uploaded MTControls_Standalone`
y `EXPORT SUCCEEDED`. El portal mostro **1.1.4 (2), «Procesando»**, con hora de
carga 15:37 e ID `bd117184-121d-4af3-ac7b-2053b37773b3`. Despues termino
el procesamiento y se registro inicialmente **«Lista para enviar»**, con
caducidad de **90 dias**. La evidencia local historica
`output/app-store-preparation/testflight-build2.jpg` documenta esa etapa.
Posteriormente se asignaron testers: la ultima evidencia local del 5 de
octubre registra **«Pendiente de revision»** externa e instalacion interna
en iPhone 16. Esto no acredita QA completa ni aprobacion o invitacion externa.

La respuesta de subida de Apple ya no incluye `ITMS-90737` ni el aviso por
dSYM ausente de la app. La unica advertencia de la compilacion 2 es la ausencia
del dSYM de Bungee, UUID `313F0E93-013F-3D80-A1EC-A626D6CA6033`.

### Historico de iOS 1.1.4 (1)

La entrega de la compilacion 1 esta confirmada. Su IPA historica es
`build/ios-appstore/export-safearea/PlayWorship.ipa`, de 8 201 156 bytes y
SHA-256 `e6c19630853dd04ee71ffec7ff8f41ad6a6b11ccd83547da8d12663e7230dca2`.
El comando termino con codigo
0 y `/tmp/playworship-testflight-upload.log` registra el 2026-10-05 a las
15:20:28, segun la hora del propio log, `Upload succeeded` y
`Uploaded package is processing`. Despues se verifico en el portal que el
procesamiento termino: TestFlight muestra version **1.1.4**, compilacion **1**,
estado **«Lista para enviar»** y caducidad de **90 dias**. El ID de compilacion
es `cf98ebc6-6206-4b82-97ff-07d4f8f0193b`. La evidencia local historica
`output/app-store-preparation/testflight-upload.jpg` documenta ese estado.

En esa etapa no se habian asignado grupos o testers al binario universal
iPhone/iPad. Este historico no sustituye el estado posterior de iOS 1.1.4 (2)
ni la entrega Mac App Store. No acredita QA fisica ni aprobacion de App Review.

### Advertencias de la compilacion 1 y correcciones del candidato 2

La entrega de la compilacion 1 termino correctamente con estas tres
advertencias de Apple:

| Advertencia | Ajuste pendiente |
| --- | --- |
| `CFBundleDocumentTypes` sin `UISupportsDocumentBrowser` ni `LSSupportsOpeningDocumentsInPlace`; aviso `ITMS-90737` | Corregido en fuente y plist final de la compilacion 2 con `LSSupportsOpeningDocumentsInPlace=false`, acorde a la importacion por copia. Preflight aprobado; la respuesta de subida de Apple ya no incluye este aviso. |
| dSYM ausente de PlayWorship, UUID historico `846D5D49-3BDF-3FE7-8BAB-16AD27B8575A` | Corregido en compilacion 2 con dSYM nuevo incluido en el Archive y UUID arm64 `FA939419-DE44-3303-9A02-6357CAD5FD59`, igual al ejecutable. La respuesta de subida de Apple ya no incluye esta advertencia. |
| dSYM ausente de `bungee.framework`, UUID `313F0E93-013F-3D80-A1EC-A626D6CA6033` | Bungee se distribuye precompilado sin simbolos correspondientes. Solicitar el dSYM al proveedor y conservarlo al empaquetar y archivar; no fabricar simbolos a partir del binario. La advertencia del proveedor queda pendiente. |

Los UUID de las advertencias corresponden a la compilacion 1. En la
compilacion 2 se genero y empaqueto el dSYM nuevo de la app despues de
comprobar su propio UUID. No reutilizar simbolos de otra compilacion. La falta
de simbolos del framework Bungee sigue pendiente del proveedor y limita el
diagnostico de sus cierres. La compilacion **1.1.4 (2)** tiene entrega y
procesamiento confirmados, testers asignados e instalacion interna registrada;
la ultima evidencia local muestra revision externa pendiente. **1.1.4 (1)**
permanece como historico anterior.

### Entrega macOS 1.1.4 (1)

El Archive `build/mac-appstore/archive/PlayWorship.xcarchive` y el paquete
`build/mac-appstore/export/PlayWorship.pkg` son universales `arm64` / `x86_64`.
La exportacion y el preflight de distribucion pasaron; el paquete tiene
13 663 473 bytes y SHA-256
`675517de52da9837c095ac33ef590b54bbffdd1ae831ecd9cc08c3b6cb161047`.
Se incluyeron dSYM coincidentes de la app para ambas arquitecturas; faltan
los simbolos correspondientes de Bungee.

La entrega termino con codigo 0 el **2026-10-05 a las 19:41:36 UTC**, ID
`af3484c0-ed66-4c86-9038-605c9b7592a5`. La ultima evidencia local,
`output/app-store-preparation/mac-testflight-processing.png`, muestra
**«Procesando»**. El procesamiento final, la asignacion de testers y la QA
completa del binario sandboxed no estan acreditados por esa evidencia.

### Capturas comerciales — ultima evidencia local 2026-10-05

Se cargaron **14 JPEG finales revisados** con contenido demo original para
la ficha 1.1.4, `es-ES`. Se comprobo persistencia y orden al recargar:

| Plataforma | Cantidad y orden | Dimensiones |
| --- | --- | --- |
| iPhone 6,9 pulgadas | 4, `01`–`04`: Biblioteca, Ensayo, setlist y Pads | 1320 × 2868; Pads horizontal 2868 × 1320 |
| iPad 13 pulgadas | 5, `01`–`05`: reproduccion, Biblioteca, setlist, secciones y remoto | 2752 × 2064 |
| Mac | 5, `01`–`05`: reproduccion, Biblioteca, setlist, automatizacion y remoto | 2880 × 1800 |

El inventario local `output/app-store-preparation/screenshots/README.md`
contiene nombres, hashes, revision visual, procedencia y archivos rechazados.
Las pruebas locales de persistencia estan en
`output/app-store-preparation/screenshots-uploaded-iphone.png`,
`output/app-store-preparation/screenshots-uploaded-ipad.png` y
`output/app-store-preparation/screenshots-uploaded-mac.png`. Estos archivos
no estan versionados. La quinta iPhone remota sigue pendiente: se rechazo
el candidato horizontal por solapamiento de Dynamic Island; falta revisar
un reemplazo vertical con alias legible. El ZIP contiene solo las 14 aprobadas.

Las capturas iOS provienen del simulador; las Mac muestran el escritorio.
La carga de material comercial no acredita aprobacion de Apple ni QA del
binario de distribucion.

Para completar la preparacion de publicacion y las pruebas, cerrar los
bloqueos anteriores y completar:

- Sustituir y revisar la quinta captura iPhone de control remoto antes de
  cargarla; comprobar correspondencia de todos los conjuntos con el binario.
- Descripcion, palabras clave, categoria Music, clasificacion de edad,
  respuestas de privacidad y notas de revision precisas. Las respuestas sobre
  contenido compartido deben considerar el acceso a la nube compartida de Teams.
- QA funcional del build entregado en iPhone/iPad fisicos; la instalacion
  interna registrada en iPhone 16 es una prueba distinta. Para Mac, comprobar
  el resultado final de procesamiento en Apple y la QA del binario propio.
- Smoke test de login, importar proyecto/stems, reproducir, pausa/stop,
  audio externo, background en iOS, Cloud/Teams autorizados y exportacion.
  Comprobar estos flujos tambien con permisos opcionales denegados.

Esta pagina registra requisitos y la ultima evidencia local de preparacion,
entrega, procesamiento iOS e instalacion interna. No acredita QA funcional
completa, aprobacion externa de TestFlight ni aprobacion de App Review.

## Fuentes Apple

- App Review Guidelines: https://developer.apple.com/app-store/review/guidelines/
- Privacy manifests: https://developer.apple.com/documentation/bundleresources/privacy-manifest-files
- Account deletion: https://developer.apple.com/support/offering-account-deletion-in-your-app
- Upload builds: https://developer.apple.com/help/app-store-connect/manage-builds/upload-builds
- App Sandbox: https://developer.apple.com/documentation/security/app-sandbox
- macOS files: https://developer.apple.com/documentation/security/accessing-files-from-the-macos-app-sandbox
- Sandboxed helpers: https://developer.apple.com/documentation/xcode/embedding-a-helper-tool-in-a-sandboxed-app
- UIRequiresFullScreen: https://developer.apple.com/documentation/BundleResources/Information-Property-List/UIRequiresFullScreen
- TN3192: https://developer.apple.com/documentation/technotes/tn3192-migrating-your-app-from-the-deprecated-uirequiresfullscreen-key
