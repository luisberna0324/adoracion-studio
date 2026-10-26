---
type: Release Draft
title: Google Play — preparación de PlayWorship para Android
description: Inventario técnico, borradores de ficha y decisiones pendientes de la primera entrega Android en Google Play.
tags: [android, google-play, release, signing, privacy]
timestamp: 2026-10-26
---

# Google Play — preparación de Android

Este documento es una preparación técnica y editorial de la primera entrega.
Sus borradores no constituyen una declaración legal, una respuesta publicada de
Data safety ni evidencia de envío, aprobación o publicación. Los estados del
portal deben volver a consultarse antes de cerrar cada etapa.

## Estado inicial y decisiones de la sesión

Evidencia de la sesión del 26 de octubre de 2026, comunicada por el agente que
opera Chrome; este documento no realiza acciones en el portal:

| Campo | Estado |
| --- | --- |
| Cuenta de desarrollador | Luis Berna, cuenta personal; al inicio no había apps creadas |
| Nombre elegido | PlayWorship |
| Package de Android | `com.multitracks.playworship`, declarado en Gradle y manifiesto |
| Idioma seleccionado | Español (Latinoamérica), `es-419`, aprobado por el propietario |
| Tipo | Aplicación, aprobado por el propietario |
| Precio de descarga | Sin coste, aprobado por el propietario |
| Creación de ficha | El propietario creó la aplicación; ficha y panel verificados en Play Console el 26 de octubre de 2026 |
| Identificador de app en Play Console | `4972301308867133502`, cuenta de desarrollador `4978274340238447322` |
| Firma existente | El propietario autorizó usar la clave de firma existente en GitHub; no se ha creado otra clave como parte de este borrador |
| Versión pública GitHub de referencia | `v2.3.5`, run 128, verificados durante la sesión por el agente de release |
| Candidato firmado | `versionName=2.3.6`, `versionCode=129`, generado en el run 129, validado y cargado en Alpha |
| AAB cargado y procesado | Google Play muestra `129 (2.3.6)`, API 29+, target 36 y arm64-v8a; borrador guardado |
| Distribución | No consta publicación, acceso a producción ni invitaciones a testers efectuadas por esta preparación |

La aplicación local sin suscripción y Cloud opcional concuerdan con los términos
públicos consultados. La disponibilidad efectiva de una cuenta gratuita depende
del backend: `hasValidAccessBySubscriptionState` admite `free`, `trial` y un plan
activo válido. El acceso local de pruebas de desarrollo se deshabilita en
Release mediante `DevelopmentFeatures::testLogin`; no sirve como acceso del
revisor ni debe anunciarse como prueba gratuita pública sin autenticación.

## Contrato del paquete y versión

- Fuente de Android: `android/app/build.gradle` y
  `android/app/src/main/AndroidManifest.xml`.
- `compileSdk=36`, `targetSdk=36`, `minSdk=29`; la configuración actual compila
  `arm64-v8a` y usa NDK `28.1.13356709`.
- Las variables `PLAYWORSHIP_VERSION_NAME` y `PLAYWORSHIP_VERSION_CODE`
  sustituyen los valores de desarrollo predeterminados `1.1.4` y `1`.
- En el workflow auditado, la versión viene del resolver SemVer y el código
  Android de `github.run_number`. El run siguiente puede cambiar mientras se
  prepara el envío: confirmar el número real, conservarlo en la evidencia y
  comprobar que supera los códigos ya utilizados en Play.
- El candidato `2.3.6 (129)` no es un paquete subido ni un número reservado.
  La versión de iOS se administra por separado; su valor no decide la entrega
  Android.
- Play distribuye APKs generados desde un Android App Bundle firmado. La
  instalación local del APK Debug o del APK publicado en GitHub no demuestra
  que exista un AAB apto ni que Play lo haya procesado.
- El workflow auditado ejecuta `:app:assembleRelease`, que produce un APK. La
  generación para Play necesita `:app:bundleRelease` y conservar el AAB
  resultante, habitualmente
  `android/app/build/outputs/bundle/release/app-release.aab`. Comprobar la
  configuración exacta del workflow que finalmente se ejecute.

Antes de la carga, registrar SHA de fuente y paquete, versión/código reales,
firma, ABIs, manifiesto fusionado, resultado de validación del bundle y
compatibilidad de las bibliotecas nativas con páginas de 16 KiB. Incluir la
biblioteca Bungee y las dependencias de JUCE en esa comprobación. El target API
correcto por sí solo no acredita alineación de todas las bibliotecas.

## Firma: clave de subida y clave que reciben los usuarios

Play App Signing distingue la **clave de subida**, que firma el AAB y permite
validar al remitente, de la **clave de firma de la aplicación**, con la que
Google firma los APKs que instala el usuario. Que un AAB se firme con la clave
existente no obliga a Google a distribuirlo con esa misma identidad.

Hay usuarios que recibieron APKs firmados fuera de Play. Para mantener una ruta
de actualización compatible, comparar el certificado de esos APKs con la clave
de firma configurada en Play. La documentación de Android indica aportar la
clave propia cuando se necesita conservar la misma identidad entre tiendas.
No seleccionar por inercia una clave de aplicación generada por Google si esa
continuidad es la decisión del propietario.

El uso de los secretos de firma existentes en el build y la importación de una
clave privada a Play son operaciones diferentes. Si la configuración necesita
transferir una copia de la clave, preparar la pantalla y las comprobaciones,
pero dejar la aceptación de términos y la importación sensible en manos del
propietario. No escribir contraseñas, material de claves ni secretos en el repo,
logs o textos de la ficha.

Después de configurar Play App Signing, registrar por separado las huellas
públicas de upload y app signing. Los proveedores que autentiquen por
certificado de la app deben reconocer el certificado que firma el APK
entregado por Play. Comprobar el login en una instalación obtenida de Play,
además del binario local.

Referencias oficiales:
[Play App Signing](https://support.google.com/googleplay/android-developer/answer/9842756?hl=en),
[firma de aplicaciones Android](https://developer.android.com/studio/publish/app-signing).

## Itinerario de distribución para la cuenta personal

La guía oficial aplica a cuentas personales creadas después del 13 de noviembre
de 2023: antes de solicitar acceso a producción, la app debe completar una
prueba cerrada con al menos 12 testers inscritos de forma continua durante
14 días. La prueba interna permite validar instalación y operación, pero no
sustituye esa condición. Confirmar en el panel qué requisitos corresponden a
esta cuenta y su estado efectivo.

Preparar una versión de prueba y cargarla no acredita que esté disponible a
sus testers. Confirmar procesamiento, revisión requerida, estado del track y
enlace de participación. Los nombres/correos y el envío de invitaciones se
resolverán con instrucciones del propietario; esta preparación no selecciona
personas ni envía mensajes.

Fuentes:
[requisitos de prueba de cuentas personales](https://support.google.com/googleplay/android-developer/answer/14151465?hl=en),
[crear y configurar una app](https://support.google.com/googleplay/android-developer/answer/9859152?hl=en).

## Borrador de ficha en español

Los textos describen el código actual, incluido el editor de letras Android.
Revisarlos contra el AAB exacto antes de guardarlos. Categoría propuesta:
**Música y audio**. Las decisiones comerciales aprobadas se conservan arriba.

### Nombre

```text
PlayWorship
```

### Descripción breve

71 caracteres; límite de Google Play: 80.

```text
Pistas, mezcla y letras para practicar y dirigir tu equipo de adoración
```

### Descripción completa

1614 caracteres, sin contar el salto final del bloque; límite de Google Play:
4000.

```text
Prepara tus canciones y acompaña a tu equipo con PlayWorship, una herramienta para trabajar con pistas de audio en ensayos y presentaciones musicales.

ORGANIZA CANCIONES Y SETLISTS
Importa tus archivos de audio o proyectos, añade título, tonalidad y tempo, y prepara el orden de las canciones para cada ensayo o presentación.

CONTROLA LA MEZCLA
Ajusta el volumen de cada instrumento, silencia pistas con Mute y escúchalas por separado con Solo. Adapta el tono y el tempo, y configura las salidas disponibles de tu dispositivo o interfaz de audio compatible.

ENSAYA POR PARTES
Recorre la forma de onda, selecciona un pasaje y repítelo mientras practicas. Organiza secciones musicales y prepara loops y saltos para seguir el arreglo.

PREPARA EL CLIC, LAS GUÍAS Y LOS PADS
Configura el metrónomo, utiliza guías de audio y acompaña tus canciones con pads, ajustando su tonalidad y nivel.

ESCRIBE Y ORGANIZA LA LETRA
Pega o escribe letras en el editor nativo. Selecciona párrafos para marcarlos como Intro, Verso, Coro, Puente y otras partes, y navega entre ellas desde sus indicadores.

CONECTA TUS EQUIPOS
Controla una sesión desde otro dispositivo compatible conectado a la misma red local. El audio se reproduce en el equipo que actúa como servidor.

APRENDE CON LA DEMO Y LAS GUÍAS
La canción demo y las guías integradas ayudan a conocer los controles y practicar los flujos de PlayWorship.

Se requiere una cuenta de PlayWorship. Las funciones de nube y equipo necesitan conexión a Internet y dependen de los permisos y límites de la cuenta. Trabaja con archivos y contenido para los que tengas autorización.
```

### Contacto y enlaces

| Campo | Preparación |
| --- | --- |
| Soporte | `hola@playworship.app`, publicado en las páginas del servicio; confirmar atención del buzón |
| Web de soporte | `https://help.playworship.app` |
| Política de privacidad | `https://playworship.app/legal/privacidad.html` |
| Términos | `https://playworship.app/legal/terminos.html` |
| Recurso web para eliminación | Pendiente de seleccionar y comprobar una solicitud accesible fuera de la app |

Las páginas públicas de privacidad y términos respondieron HTTPS 200 en la
consulta del 26 de octubre. Esto confirma disponibilidad, no exactitud de todas
sus declaraciones ni la ejecución de una eliminación de cuenta.

### Gráficos

- Icono fuente: `assets/icons/app-icon-1024.png`, RGB, 1024 × 1024. Preparar la
  variante de ficha 512 × 512 y comprobar formato/tamaño.
- Logo existente: `assets/pwLogoSinFondo.png`, 1920 × 709.
- Gráfico de funciones: preparar 1024 × 500, JPEG o PNG sin alfa. No se halló
  uno terminado durante este inventario.
- Preparar como mínimo dos capturas de la app Android. Conservar evidencia del
  dispositivo, build y contenido que muestran; las capturas de Apple no
  acreditan la interfaz Android.
- En el checkout local de QA existen capturas del editor físico Samsung en
  `output/android-lyrics-samsung-cursor-fix/`. Las capturas antiguas del
  reproductor en `output/android-modal-freeze-20261023/` necesitan revisión:
  `demo-playing.png` presenta el comienzo del título recortado.

Fuentes:
[límites de textos](https://support.google.com/googleplay/android-developer/answer/13393723?hl=en),
[gráficos de la ficha](https://support.google.com/googleplay/android-developer/answer/9866151?hl=en).

## App content y acceso de revisión

El candidato necesita una cuenta válida de PlayWorship para que el revisor
pueda acceder a las funciones anunciadas. Proporcionar instrucciones
comprobadas, contenido demo accesible y las credenciales dedicadas en el campo
privado de revisión. No usar credenciales personales para completar el campo.
Las autorizaciones anteriores para Apple no se tratan como autorización para
transmitir una contraseña a Google Play; resolver con el propietario el uso de
la cuenta concreta y el destino antes de introducirla.

La app permite Google y correo/contraseña en Android. El login local de Debug
no es una alternativa de revisión. Verificar acceso gratuito/plan y duración de
la cuenta de revisión, sin crear una suscripción pagada ni conceder cuotas por
suposición. El flujo sugerido es iniciar sesión, cargar la demo, reproducir,
abrir Ensayo, probar una región A–B y abrir Biblioteca → menú de canción → Letra.

La fuente auditada no enlaza un SDK publicitario ni Play Billing, ni ofrece un
botón nativo de compra. Los enlaces nativos encontrados llevan al acceso OAuth
y a términos, privacidad y copyright. Cloud se paga externamente mediante
Paddle según la web pública. Revisar también las pantallas web del acceso y la
configuración de países antes de declarar el modelo comercial: las reglas de
consumo de servicios ya adquiridos y los programas de pagos externos tienen
condiciones específicas.

Las respuestas sobre anuncios, audiencia objetivo y clasificación deben
corresponder al producto y al contenido real. La audiencia de la app no se
deduce del nombre de la cuenta ni de la temática musical. Las funciones de
nube compartida deben tenerse en cuenta cuando el cuestionario pregunte por
intercambio de contenido entre usuarios.

Referencia:
[política de pagos y apps de consumo](https://support.google.com/googleplay/android-developer/answer/10281818?hl=en).

## Data safety y eliminación: inventario pendiente de validación

Las declaraciones de Apple son un inventario de referencia, no una respuesta
intercambiable con Data safety. Las categorías, obligatoriedad, fines y
excepciones de Google deben verificarse para el Android entregado, su backend
y sus proveedores. No marcar «No se recopilan datos» a partir de que el audio
se reproduzca localmente.

| Flujo / categoría candidata | Evidencia y decisión pendiente |
| --- | --- |
| Correo e ID de usuario | Login Google/correo y sesión Firebase; funcionalidad y gestión de cuenta |
| Nombre | Google/perfil de autenticación y referencia del manifiesto Apple; confirmar conservación real en proveedores/backend Android |
| Dispositivo u otros identificadores | `PluginEditorSupport::buildStableDeviceId`, validación de acceso y límites de dispositivos |
| Fotos | Portadas que el usuario sincroniza voluntariamente; comprobar campos y retención |
| Audio | Stems/proyectos subidos a Cloud por acción del usuario; el contenido local no implica una carga automática |
| Archivos y otros contenidos del usuario | Metadata portable, proyectos y letras sincronizadas; mapear a los subtipos del formulario de Google |
| Historial de compras | Estado y eventos de suscripción; Paddle procesa el pago y la política no declara almacenamiento de tarjeta completa por PlayWorship |
| Diagnósticos | ZIP local solicitado por el usuario y tratamiento posterior de soporte/backend; revisar antes de declarar recolección automática u opcional |

En la configuración Android auditada no se habilita/enlaza Sentry ni se incluye
Firebase Analytics. El código de telemetría depende de
`MTCONTROLS_SENTRY_ENABLED`; verificar de nuevo el AAB final y sus dependencias.
La analítica de Google descrita por la política pública corresponde al sitio
web y consentimiento de cookies; no demuestra un SDK de analítica en Android.

El manifiesto aún declara `RECORD_AUDIO`, pero
`StandaloneAudioDeviceBridge::initialise` abre cero entradas y limpia los
canales de micrófono. Esa declaración de permiso no prueba que la app grabe o
suba voz. Evaluar los datos de los archivos que el usuario decide subir por
separado de la captura de micrófono.

La política pública consultada afirma que Cloud no pone archivos a disposición
de otros usuarios. Reconciliar ese texto con la nube compartida de Teams y
los controles de acceso efectivos antes de presentar declaraciones finales.
La existencia de proveedores no determina por sí sola la respuesta «se
comparten datos»: revisar las excepciones de proveedores de servicio y
acciones iniciadas por el usuario frente a las relaciones reales.

El flujo nativo de eliminación consulta
`GET /api/account/deletion/preview`; habilita avanzar según
`executionAvailable` y exige reautenticación y confirmación. La documentación
histórica conserva un despliegue con ejecución deshabilitada, pero **no se ha
comprobado aquí el estado productivo actual**. Verificar esa capacidad con una
consulta autenticada de sólo lectura autorizada. No ejecutar una eliminación
real para completar este inventario ni reutilizar tokens sin autorización.

Google exige un camino dentro de la app y un recurso web para solicitar la
eliminación cuando la app permite crear cuentas. La política pública ofrece
contacto por email, pero debe comprobarse que el recurso seleccionado cumple
el alcance y visibilidad exigidos, referencia a PlayWorship y datos asociados.
No confundir cerrar sesión, borrar una copia local o mostrar un botón con
confirmar que una solicitud de eliminación funciona.

Fuentes:
[Data safety](https://support.google.com/googleplay/android-developer/answer/10787469?hl=en),
[solicitudes de eliminación de cuenta](https://support.google.com/googleplay/android-developer/answer/13327111?hl=en).

## Evidencia de fuente y separación de etapas

Contratos y fuentes usados en esta preparación:

- [Privacidad](../distribution/AppPrivacyPreparation.md),
  [textos de Apple](../distribution/AppStoreMetadata.es.md) como antecedentes,
  [Auth](../integrations/Auth.md), [Cloud Sync](../integrations/CloudSync.md),
  [Teams](../integrations/Teams.md), [letras nativas](../ui/NativeLyrics.md) y
  [QA / releases manuales](LocalFirstCI.md).
- `android/app/build.gradle`, manifiesto y CMake de Android.
- `src/PluginEditorSupport.cpp`, `src/EmailPasswordAuthIntegration.cpp`,
  `src/data/AuthSessionApi.cpp`, `src/data/AccountDeletionApi.cpp`,
  `src/ui/components/AccountDeletionModal.cpp`,
  `src/audio/StandaloneAudioDeviceBridge.cpp`, `src/DevelopmentFeatures.h`.
- Páginas públicas de términos y privacidad, consultadas por HTTPS sin
  credenciales ni modificación de datos.

Actualizar la evidencia al obtener cada resultado: fuente fijada y QA,
compilación de Release, firma y validación de AAB, carga y procesamiento,
versión de prueba disponible, revisión y acceso a producción. Un resultado de
una etapa no demuestra las siguientes.


## Preparación local y autorización del 2026-10-26

El responsable confirmó descarga sin coste y uso de la firma Android existente
de GitHub. Después autorizó subir los cambios a `main` y generar únicamente el
AAB de Android, con retención de siete días; la primera respuesta «sólo preparar»
fue corregida explícitamente por «subamos». Esto no sustituye la aceptación
personal de las declaraciones legales en Play Console ni acredita envío a
revisión o publicación.

La compilación local `:app:packageReleaseBundle` finalizó correctamente como
preparación sin firma de distribución. `bundletool` oficial 1.18.3, con SHA-256
verificado contra el asset de Google, validó la estructura y el manifiesto:
paquete `com.multitracks.playworship`, versión 2.3.6, código 129, mínimo API 29 y
target API 36; `debuggable` no está habilitado. BundleConfig solicita
`PAGE_ALIGNMENT_16K` y los tres ELF arm64 tienen todos sus segmentos LOAD
alineados a 16384 bytes. El Dex contiene el `CursorState` de la corrección
Samsung. Este archivo local no está firmado y no es el entregable de Play.

Evidencia local: `output/google-play-preparation/` del checkout de QA, incluidos
`local-unsigned-bundle-validation.json`, manifiesto decodificado y logs. El AAB
firmado de GitHub deberá verificarse nuevamente contra su SHA y versión reales.


## AAB firmado generado y validado — 2026-10-26

Los cambios autorizados se subieron a `main`: `11b847d` contiene los editores
nativos y la corrección Samsung; `431f705` añade el modo Google Play y esta guía.
El run [129](https://github.com/luisberna0324/adoracion-studio/actions/runs/40321130667)
finalizó correctamente sobre `431f705bc1dc838841cff78be0382d9085b75793`. Sólo
`prepare` y `Build signed Android App Bundle` se ejecutaron; las tareas de
macOS, Windows, publicación web/GCS y GitHub Release quedaron omitidas.

Artefacto: `playworship-google-play-2.3.6-arm64.aab`, versión 2.3.6 (129),
conservado siete días en GitHub y descargado al checkout de QA bajo
`output/google-play-preparation/signed-2.3.6-129/`.

Verificación del archivo descargado:

- Firma JAR Release válida, certificado distinto de Android Debug. Huella SHA-256:
  `D1:16:2E:4A:7A:9B:B8:02:BE:08:94:8C:A3:3C:90:DA:61:F0:08:4F:AC:72:50:CB:98:1A:7D:A9:FC:26:F2:4D`.
- SHA-256 del AAB: `4b7538c00a8cdcfb3bc8f2479234b18f7d65c04199ae7738757aeb138ddc4151`.
- `bundletool validate` correcto; paquete, versión y código coinciden;
  `debuggable` no está habilitado, ABI arm64, API mínima 29 y target 36.
- BundleConfig solicita `PAGE_ALIGNMENT_16K` y todos los LOAD de Bungee,
  JUCE y libc++ tienen alineación de 16384 bytes. El Dex incluye el cursor
  clonable de Samsung y excluye la clase de instrumentación de pruebas.

`validation.json`, manifiesto decodificado y logs de firma están junto al AAB.
Esto acredita generación y verificación del paquete, no su recepción por Play,
la revisión de políticas ni publicación. El propietario ya creó la aplicación;
la carga y procesamiento del AAB se confirmaron posteriormente en Alpha, como se documenta al final.


## Ficha creada y continuidad de firma — 2026-10-26

El panel de Play Console confirma la ficha `PlayWorship`, paquete
`com.multitracks.playworship`, app ID `4972301308867133502`. La cuenta personal
requiere al menos 12 testers inscritos en prueba cerrada durante 14 días
continuos antes de solicitar acceso a producción. El panel muestra cero
testers inscritos; crear la ficha no inicia esa prueba.

La pantalla **Protegida con Play → Protección de Play Store → Firma de
aplicaciones** ya muestra una clave generada por Google, distinta de la que
firma los APK distribuidos: SHA-256
`56:DA:6D:A9:39:3A:38:D0:BB:19:21:73:71:75:E4:86:53:5B:13:D8:2E:78:F7:84:9B:9B:AC:ED:9C:82:23:5C`.
No tratarla como equivalente a la firma Release `D1:16:2E:…:F2:4D`.

El propietario autorizó expresamente transferir a Google Play una copia
cifrada de la clave existente para conservar la compatibilidad de las
actualizaciones. La original permanece en los secretos protegidos de GitHub.
La autorización es distinta de la evidencia de ejecución; la exportación
y descarga cifrada se documentan al final. La importación sigue pendiente. El propietario abrió las preferencias de firma y se seleccionó
«Exportar y subir una clave del almacén de claves de Java». Se obtuvieron las
instrucciones PEPK y su clave pública desde esta consola. No exportar ni traer el keystore privado a la Mac
ni incluirlo, contraseñas o material privado en artefactos o logs.

La configuración productiva consultada de sólo lectura tiene
`ACCOUNT_DELETION_ENABLED=true`, referencia a política contable y retención de
estado de 30 días. La ruta pública de preview responde 401 sin autenticación.
Esto verifica configuración y ruta, no una eliminación efectiva ni la consulta
de capacidades de una cuenta autenticada. Sigue pendiente conciliar la
política pública con Teams y completar las declaraciones de seguridad de datos.


### Instrucciones PEPK obtenidas de Play Console

La opción Java Keystore de esta app ofrece `--include-cert`,
`--rsa-aes-encryption` y `--encryption-key-path`. El JAR fue descargado con su
botón oficial; la procedencia registrada apunta a
`https://www.gstatic.com/play-apps-publisher-rapid/signing-tool/prod/pepk.jar`.

- SHA-256 del JAR revisado:
  `aaccc0774b240aa5304bdad2a49865e92f229ca73209ecb6eaafe75dc858e24e`.
- SHA-256 del PEM de cifrado de Google:
  `7c50d42784451af7b1f437a6298341c1f866a3dbab665f54a376e2d6c6029264`.
- La clave de cifrado RSA de 3072 bits es pública y puede conservarse junto al
  wrapper. No es la clave privada Release de PlayWorship.

El modo `export_play_signing_key` del generador manual prepara exclusivamente
la copia cifrada con esos recursos fijados. El CLI PEPK usa consola para
contraseñas y puede imprimir argumentos ante errores; el wrapper invoca su API
pública con las contraseñas en memoria procedentes del entorno. Sólo se
conservan `encryptedPrivateKey` y `certificate.pem` dentro del ZIP. Comparar
el certificado con la huella Release conocida antes de entregarlo al portal.

La exportación se completó en el run 130, documentado abajo. La importación
sigue pendiente; no crear ni usar una nueva clave de subida durante este trámite.


### Copia cifrada generada y validada — run 130

El commit `eaecf12dfa1d16dc076e2a31c36d205ee5219579` quedó en `main` y el
[run 130](https://github.com/luisberna0324/adoracion-studio/actions/runs/40327400021)
ejecutó únicamente `Export encrypted Google Play signing key`. La exportación,
validación del ZIP y limpieza de material temporal finalizaron correctamente.
`prepare`, los tres builds y todas las publicaciones quedaron omitidos.
No se recompiló ni cambió el AAB 2.3.6 (129), construido desde `431f705`.

El artefacto `google-play-encrypted-signing-eaecf12dfa1d16dc076e2a31c36d205ee5219579`,
ID `12651841436`, conserva únicamente el ZIP cifrado y su reporte público
hasta el 27 de octubre de 2026. El propietario autorizó expresamente descargar
la copia cifrada a `output/google-play-preparation/signing-export-130/` del
checkout local de QA. No se descargó keystore privado sin cifrar ni contraseñas.

Validación local del ZIP de 4402 bytes:

- SHA-256: `08b406368c76e5023cfb93fa511dfca45f58518d36b5ec689af68d9ab0f255b3`.
- Exactamente `encryptedPrivateKey` y `certificate.pem`, sin entradas adicionales.
- Certificado público idéntico al Release conocido `D1:16:2E:…:F2:4D`.
- Hash y revisión coinciden con `report.json` del runner.

El propietario debe seleccionar ese ZIP y guardar en Play Console; el estado
actual todavía no acredita importación, configuración de firma final, carga
del AAB ni disponibilidad de pruebas. Comparar el certificado **de firma de
aplicación** del portal tras guardar; no basta con el certificado de subida.


## Firma importada y AAB cargado en Alpha — 2026-10-26

El propietario confirmó la importación y guardado de la copia cifrada. La
pantalla de firma del portal, mediante su Digital Asset Links JSON, confirma
que la clave **de firma de aplicación** tiene la huella Release original:
`D1:16:2E:4A:7A:9B:B8:02:BE:08:94:8C:A3:3C:90:DA:61:F0:08:4F:AC:72:50:CB:98:1A:7D:A9:FC:26:F2:4D`.
La validación de la carga posterior aceptó el AAB con la firma existente; no se
generó otra clave de subida durante la preparación.

El AAB cuyo hash es `4b7538c00a8cdcfb3bc8f2479234b18f7d65c04199ae7738757aeb138ddc4151`
se cargó y procesó en **Prueba cerrada — Alpha**, canal
`4699464310723384488`, versión en borrador `1`. Google muestra:

- App bundle `129 (2.3.6)`, API 29 y posterior, SDK objetivo 36, ABI arm64-v8a.
- Símbolos de depuración nativos adjuntos al bundle.
- Nombre de release `2.3.6 (129)` y notas en `es-419` sobre editor nativo,
  organización de párrafos y corrección del cursor/pegado Samsung.
- «Cambios guardados. Ahora puedes previsualizar tu versión antes de enviarla
  a revisión». Esto acredita borrador guardado, no envío ni disponibilidad.

La vista de revisión informó inicialmente cinco bloqueos de configuración: tareas
de ficha pendientes, descripción completa ausente, países sin seleccionar y
declaraciones financieras/de salud sin completar. Se guardaron las declaraciones
correctas **sin anuncios, sin funciones financieras, sin funciones de salud y
sin vínculo gubernamental**. También quedó guardada la categoría **Música y
audio**, el correo público `hola@playworship.app` y la web de soporte
`https://help.playworship.app`. El panel confirmó 5 de 11 tareas completas.

El propietario seleccionó **todos los países disponibles** para la prueba
cerrada. Tras guardar, el canal confirma **178 países o regiones** y 2 de 4
tareas completas. Continúa inactivo y sin testers inscritos. La ficha de textos
y recursos está en preparación; no se ha enviado nada a revisión.

Las advertencias iniciales fueron ausencia de testers y ausencia de archivo
de desofuscación. Esta build no activa R8/ProGuard: el log usa dexBuilderRelease
y mergeDexRelease, sin minifyReleaseWithR8 ni mapping.txt. No generar un mapa
artificial para ocultar esa advertencia; los símbolos nativos son independientes.

La extensión de Chrome no permitió setFiles sin un permiso adicional de acceso
a URLs locales; no se cambió ese permiso. La carga se completó con el selector
nativo de macOS. Evidencia de QA local: `firma-original-importada.jpg` y
`aab-2.3.6-129-cargado.jpg`, bajo `output/google-play-preparation/`.


## Audiencia mixta desde 9 años y preparación Families — 2026-10-26

El propietario precisó que PlayWorship está diseñada **desde los 9 años,
además de adolescentes y adultos**. Las franjas que corresponden a esa decisión
son **9–12, 13–15, 16–17 y 18+**. No añadir 5 o menos ni 6–8 sin otra decisión,
ni declarar 13+ para evitar requisitos. La clasificación IARC se obtiene de su
cuestionario real; no se deduce de estas franjas. Este apartado registra la
intención de producto, no acredita que las edades se hayan guardado en el
portal ni certifica cumplimiento.

Google exige Families si cualquiera de las audiencias objetivo incluye niños,
aunque también haya adultos y no se muestren anuncios. Seleccionar edades no
sustituye revisar la app, sus proveedores, las declaraciones y el contenido que
reciben esos usuarios. La ausencia de anuncios sigue siendo la respuesta
sustentada; no se incorpora una red publicitaria ni un servicio nuevo.

### Brechas concretas antes de presentar la audiencia 9+

| Área | Evidencia actual y tarea mínima pendiente |
| --- | --- |
| Acceso sin cuenta Google | Google permite Sign-In opcional en audiencia mixta, pero la app completa debe ser accesible sin iniciar sesión en Google. Android ofrece correo/contraseña; el backend auditado recupera contraseña de una cuenta existente y crea cuentas en el callback Google. Eso no demuestra un alta independiente para esta audiencia. Definir y comprobar un camino de acceso completo apropiado, sin presentar la mera presencia del botón de correo como cumplimiento. |
| APIs y tratamiento por edad | Families exige que APIs no aprobadas para uso infantil estén detrás de una pantalla neutral de edad o se implementen sin recopilar datos de niños; tampoco pueden ser el acceso obligatorio al contenido. No se encontró un flujo neutral de edad o parental en las superficies auditadas. Determinar qué proveedores y rutas necesitan tratamiento por edad; no afirmar que basta un aviso o que toda app sin anuncios tiene la misma solución. |
| Cuenta, dispositivo y Cloud | Se transmiten datos de autenticación y un ID estable del dispositivo, y se sincroniza contenido por acción del usuario. Revisar campos, finalidades, retención y controles aplicables a los usuarios 9–12 con el responsable antes de cerrar Data safety y la política. No copiar la declaración Apple ni marcar ausencia de datos por reproducir audio local. |
| Teams y contenido compartido | Teams distribuye archivos privados del propietario a sus miembros. Evaluar las reglas Families de funciones sociales e intercambio: información de seguridad, gestión por adultos y acción adulta antes de funciones que intercambien información personal cuando corresponda. Definir qué acceso tienen los menores y verificar los controles; el perfil de administrador de Teams no acredita tutela o consentimiento parental. |
| Bluetooth MIDI Android | Families exige Companion Device Manager al solicitar Bluetooth, salvo versiones de SO incompatibles. El mínimo Android es API 29, el manifiesto declara BLUETOOTH_CONNECT y JuceMidiSupport usa BluetoothLeScanner; no se encontró integración CDM en la fuente auditada. Revisar y adecuar este flujo antes de certificar Families. |
| Políticas públicas | Términos y privacidad, consultados por HTTPS el 26 de octubre, no fijan edad mínima ni describen un tratamiento específico de menores o responsables. No existe aquí una cláusula pública 18+ que se pueda dar por cambiada: falta acordar y reflejar la audiencia 9+, los controles efectivos, el tratamiento y las solicitudes de sus responsables. Los términos también deben conciliarse con el intercambio privado de Teams. Preparar un texto revisable y obtener revisión del propietario antes de publicarlo; este documento no modifica las páginas. |

La revisión infantil no cambia automáticamente iOS o macOS. Las políticas
públicas son compartidas y cualquier cambio debe representar los flujos reales
de cada plataforma. Las adaptaciones de aplicación/backend o declaraciones de
Apple constituyen trabajo posterior explícito; este apunte no las implementa ni
aprueba un despliegue.

### Decisiones y comprobaciones para cerrar la ficha

- Audiencia de producto confirmada: 9–12, 13–15, 16–17 y 18+; falta constatar
  guardado y resumen final del formulario correspondiente.
- Resolver el acceso completo sin Google para usuarios 9–12 y el alcance de
  cuenta/responsable; verificar el flujo antes de ofrecerlo a revisión.
- Resolver los controles de Teams y los proveedores/rutas que requieren
  tratamiento por edad; verificar Bluetooth MIDI con el requisito CDM.
- Revisar el texto de privacidad/términos y completar Data safety e IARC con
  esos comportamientos comprobados. No enviar una certificación Families ni la
  versión a revisión sólo por haber cargado correctamente el AAB.
- Mantener separadas la carga aceptada, el borrador Alpha, la ficha completa,
  el envío a revisión y la disponibilidad de la prueba. El requisito vigente de
  12 testers inscritos durante 14 días continuos antes de solicitar producción
  sigue siendo independiente.

Fuentes primarias consultadas el 26 de octubre de 2026:

- [Franjas del formulario de audiencia](https://developers.google.com/android-publisher/app-store-review/policy-declarations).
- [Selección de audiencia y contenido](https://support.google.com/googleplay/android-developer/answer/9867159?hl=en).
- [Google Play Families: datos, APIs, funciones sociales y Bluetooth](https://support.google.com/googleplay/android-developer/answer/9893335?hl=en).
- [Google APIs: aplicaciones infantiles y de audiencia mixta](https://developers.google.com/terms/api-services-user-data-policy#child-directed_apps).
- [Términos públicos actuales](https://playworship.app/legal/terminos.html) y
  [privacidad pública actual](https://playworship.app/legal/privacidad.html).


## Ficha gráfica guardada y capturas Android — 2026-10-26

La ficha predeterminada `es-419` conserva como borrador el nombre PlayWorship,
la descripción breve de 71 caracteres y la descripción completa de 1614
caracteres documentadas arriba. El portal confirmó «Se han guardado los cambios».
Quedaron adjuntos el icono oficial RGB de 512 × 512, el gráfico de funciones
RGB de 1024 × 500 y dos capturas nuevas aceptadas de teléfono:

| Recurso cargado | Dimensiones | SHA-256 del PNG |
| --- | --- | --- |
| `01-reproductor-demo.png` | 1920 × 1080 | `79419ce8d24da0d04e38ae3ca9c379489b0cab9d56947ecb68fe8118ad6fa3c6` |
| `02-biblioteca-local.png` | 1080 × 1920 | `9e989ee7176f45179644f11ca4735ce55d4d98f7698a8f5c0030bf6e44328049` |

Son capturas reales completas del emulador API 36, con la canción original
«Luz de esperanza — PlayWorship · Demo». El tamaño temporal del emulador fue
1080 × 1920 para obtener 16:9/9:16; después se restauró la resolución original
1080 × 2400, sin borrar datos. Se eliminó únicamente alfa completamente opaco
al exportar RGB; no se recortó, estiró ni reconstruyó la interfaz. El AVD
abierto para estas capturas se cerró al terminar.

La captura usa el APK **Debug** local cuyo SHA-256 es
`bf0ff2a9e599362d0609abcb3fda3d1991d3c446e7f0f4f96a666564923e9bfb`,
con valores de desarrollo `1.1.4 (1)`. No confundir esas capturas de la interfaz
con QA de una instalación distribuida por Play de Release 2.3.6 (129).
Evidencia de procedencia: `store-assets/android-phone-captures-metadata.json`.

Al intentar dar foco al editor nativo de letras en ese emulador se registró un
ANR por Input dispatching timed out / FocusEvent(hasFocus=false) de JuceActivity.
El log también muestra frames omitidos y retrasos gráficos generales; no se
obtuvo el stack de ANR por permisos. **La causa queda indeterminada**: no
atribuirla a memoria, audio o un deadlock sin evidencia. No se tomó una captura
final del editor ni se declaró fluidez. Conviene reproducir de forma acotada
el flujo JUCE → editor nativo en Release/Android físico antes del envío; las
pruebas previas del componente nativo no sustituyen ese flujo manual completo.

El propietario facilitó dos correos para el grupo de prueba y autorizó usar
la cuenta dedicada de revisión en el campo privado de Google Play. No guardar
esos correos ni credenciales en este documento. La lista de testers se preparó
como «PlayWorship - prueba inicial»; la entrada por herramienta fue reemplazada
por un marcador literal de redacción, así que quedó al propietario completar
los dos correos y guardar. Después el propietario confirmó el guardado y el
portal mostró la lista con **2 usuarios**. Se asoció la lista a Alpha y se
guardó el canal: el panel confirmó **3 de 4 tareas completas**. Esto no acredita
participación: el enlace sigue inactivo hasta publicar la prueba y no se han
enviado mensajes ni invitaciones por esta preparación.

Pendientes: completar audiencia mixta 9+, clasificación y Data safety conforme
a los controles reales y Families; política pública coherente; acceso privado
del revisor; incorporación del grupo de testers; revisión del ANR observado;
confirmación/envío de la versión. No hay envío, aprobación ni publicación.


### Acceso privado y dependencias del formulario de edades

Google no permite comenzar «Contenido y audiencia objetivo» hasta completar
«Datos de inicio de sesión». Por ello las franjas 9–12, 13–15, 16–17 y 18+
están definidas por el propietario y documentadas, pero todavía no se han
registrado en ese cuestionario bloqueado. No declarar que esté cerrado.

Se eligió que la app requiere acceso y se preparó un conjunto de instrucciones
de revisión en inglés con el flujo de correo, demo, reproducción, Ensayo,
Biblioteca y Letra. Los archivos de evidencia Apple no conservan la contraseña;
el formulario privado quedó al propietario para introducir la cuenta dedicada
y su clave directamente en Google y confirmar el acceso completo real. No
guardar credenciales ni capturas que muestren la clave en este repositorio.
La habilitación del formulario de edades no prueba cumplimiento Families.


## Acceso de revisión e IARC completados — 2026-10-26

El propietario introdujo directamente las credenciales de la cuenta dedicada y
confirmó el guardado. La página de acceso mostró «Cambio guardado» y el panel
marcó Datos de inicio de sesión como completado. No se conservaron contraseñas
ni capturas del formulario con credenciales. El guardado del formulario no
acredita una prueba autenticada completa de las funciones de esa cuenta.

Después el propietario completó el primer cuestionario IARC. La consulta en
vivo muestra Estado de la IARC **Completado**, enviado el 26 de octubre de 2026
a las 11:56, con ESRB **Para todos**, PEGI **3**, ClassInd **Todas las edades**,
USK **Para todos los públicos** e IARC Generic **3+**. El campo de certificado
mostraba un guion: no inventar un identificador. El aviso anterior de 13+ en
el formulario de audiencia no representa esta clasificación ya completada.

El panel confirma **7 de 11 tareas completas**. Permanecen pendientes la URL
de privacidad, la audiencia objetivo, Seguridad de los datos y finalizar la
ficha de Play Store. La intención de audiencia mixta desde 9 años se mantiene;
la clasificación IARC no demuestra que se hayan registrado esas franjas ni
resuelve las brechas Families documentadas arriba.

La última vista observada quedó en «Crear versión de prueba interna», sin
bundle añadido ni nombre de versión. No se creó ni lanzó una segunda versión
desde la automatización. La preparación Alpha 2.3.6 (129) continúa siendo un
borrador; no hay evidencia nueva de envío a revisión ni disponibilidad.


### Audiencia y URL pública guardadas

Se avanzó el formulario de audiencia con 9–12, 13–15, 16–17 y 18+, sin
participación en Aprobada por profesores. La vista final mostró «Cambio guardado»
y el panel marcó la tarea completada. También se guardó
`https://playworship.app/legal/privacidad.html` y se verificó por HTTPS que la
página pública responde. Esto guarda el enlace existente en el borrador; no
actualiza la política ni acredita que se resolvieran sus brechas sobre menores.
El panel llegó a **9 de 11 tareas completas**: faltan Seguridad de los datos y
finalizar la ficha. No se envió la versión a revisión.

La revisión de fuente precisó que no corresponde declarar todos los datos
cifrados en tránsito: el remoto LAN propio usa `ws://` y envía metadatos de
canciones, nombres de archivos, biblioteca/setlists y portadas fuera del
dispositivo. Base64 no es cifrado. Fuentes: `RemoteSession.cpp:122`,
`RemoteStateCodec.h:61-111` y `RemoteEditorIntegration.cpp:610-645`. La política
oficial de Data safety incluye transmisiones efímeras y metadatos de archivos;
la excepción para servicios externos como SMS no acredita este servidor propio.
No se cambiaron los transportes durante la preparación de la ficha.


### Ficha finalizada y Data safety guardado parcialmente

Con autorización específica del propietario se etiquetó como creado/editado con
IA únicamente `playworship-feature-1024x500.png`. El diálogo individual mantuvo
sin marcar el icono existente y las dos capturas reales. La ficha pasó de
Borrador a **Lista para enviar a revisión**; no equivale al envío de la app.
El panel acredita **10 de 11 tareas completas**, quedando Seguridad de los datos.

En Data safety se guardó un borrador parcial en el paso 2 de 5 con recogida de
datos Sí, cifrado en tránsito No por el remoto LAN descrito arriba, creación de
cuentas OAuth y sin solicitar la insignia opcional Families. No se inventó un
alta pública por contraseña a partir del flujo de recuperación. La URL de
eliminación continúa vacía: la política pública actual no describe todos los
pasos, datos y plazos requeridos. No se completaron los tipos/finalidades de
datos, la vista previa ni su envío final. El formulario confirmó el guardado
parcial, no una declaración completa.

El propietario pidió usar el estándar para preparar la página de eliminación.
Se preparó un HTML local revisable en
`output/google-play-preparation/eliminar-cuenta-borrador.html`, sin publicar.
La propuesta usa el marco colombiano: atención de reclamos sobre datos personales
en 15 días hábiles desde el día siguiente al recibo, con ampliación excepcional
de hasta 8 días hábiles previo aviso motivado; y conservación comercial general
de 10 años desde el último asiento, documento o comprobante, sin perjuicio de
plazos especiales menores. Este último criterio se limita a soportes contables
mínimos y no a canciones ni a toda la cuenta. No representa una garantía de
purga automática ni prueba que el backend tenga configurada esa caducidad.

El plazo de 30 días de consulta del estado de eliminación no demuestra borrado
físico de recibos o marcadores de seguridad. La fuente histórica consultada
conserva marcadores para impedir que callbacks o cargas antiguas resuciten
cuentas eliminadas; sus plazos y la revisión productiva requieren validación
antes de cerrar el texto público. Continúan las brechas Families y el QA Android
ya descritos; no se ha enviado la prueba cerrada a revisión.

Referencias verificadas para esta propuesta:

- [Ley 1581 de 2012, artículo 15](https://cancilleria.gov.co/normograma/compilacion/docs/ley_1581_2012.htm).
- [Ley 962 de 2005, artículo 28](https://normativa.archivogeneral.gov.co/ley-962-de-2005/).
- [Recurso web de eliminación de cuentas de Google Play](https://support.google.com/googleplay/android-developer/answer/13327111?hl=es).
- [Declaración individual de recursos de IA](https://support.google.com/googleplay/android-developer/answer/17262077?hl=es).
