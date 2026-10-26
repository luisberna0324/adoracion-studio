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
| Creación de ficha | Las tres casillas legales ya aparecen marcadas por el propietario; sigue pendiente que pulse «Crear aplicación», que envía la aceptación final |
| Identificador de app en Play Console | Pendiente; no inventar un ID ni una URL de ficha |
| Firma existente | El propietario autorizó usar la clave de firma existente en GitHub; no se ha creado otra clave como parte de este borrador |
| Versión pública GitHub de referencia | `v2.3.5`, run 128, verificados durante la sesión por el agente de release |
| Candidato firmado | `versionName=2.3.6`, `versionCode=129`, generado en el run 129 y validado; pendiente de carga y comprobación en Play Console |
| AAB cargado y procesado | Pendiente de evidencia de paquete, firma y resultado del portal |
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
la revisión de políticas ni publicación. El portal aún muestra «Crear aplicación»
con las declaraciones marcadas; el clic final corresponde al titular porque
envía los términos y certificaciones legales.
