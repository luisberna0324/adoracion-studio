---
type: Distribution Draft
title: PlayWorship — textos de App Store y TestFlight en español
description: Identidad verificada del registro compartido de iOS y macOS, textos por plataforma y estado por campo de publicacion.
tags: [distribution, app-store, testflight, ios, macos, metadata]
timestamp: 2026-10-05T00:00:00-03:00
---

# PlayWorship — metadatos en español

La identidad del registro ya está verificada. Los textos y los pendientes
tienen estado por campo en este documento; prepararlos o guardarlos en el
portal no implica aprobación de Apple. Comprobar que describen el binario
exacto que se enviará. iOS y macOS tienen compilaciones, capturas y pruebas
independientes aunque compartan un registro.

El estado de entrega de esta página corresponde a la **última evidencia local
del 5 de octubre de 2026**, registrada en
`output/app-store-preparation/preparation.json` y
`output/app-store-preparation/screenshots/README.md`. Estos archivos, capturas
y paquetes son evidencia local no versionada. No se volvió a consultar Apple
para esta actualización; el estado de procesamiento o revisión puede cambiar.

## Registro compartido

| Campo | Valor | Estado |
| --- | --- | --- |
| Apple ID de la app | `6819361132` | Verificado en el registro real |
| Plataformas | iOS y macOS | Verificadas en el registro real |
| Nombre | PlayWorship | Verificado en el registro real |
| Idioma principal | Español (España), `es-ES` | Verificado en el registro real |
| ID de pack | `com.multitracks.playworship` | Verificado en el registro real |
| Equipo Apple | `V9T23PX769` | Verificado para este registro |
| SKU | `playworship` | Verificado en el registro real |
| Categoría principal | Música; el binario macOS debe declarar la misma categoría | Guardada en App Store Connect y confirmada al volver al registro |
| Subtítulo | Pistas para ensayo y adoración | Guardado en App Store Connect y confirmado al volver al registro |
| URL de privacidad común | `https://playworship.app/legal/privacidad.html` | Guardada y mostrada en App Store Connect |
| Acceso de usuarios | Según los colaboradores autorizados en App Store Connect | Confirmar configuración del registro |
| Copyright | Confirmar titular de los derechos y año | Pendiente; la web identifica a LUJUMA SYSTEM SAS como responsable del servicio, sin sustituir la confirmación de derechos |
| Clasificación por edades | Responder el cuestionario según contenido y funciones reales | Pendiente |
| Precio y disponibilidad | Decisión comercial y países de distribución | Pendiente |

Registro: [PlayWorship en App Store Connect](https://appstoreconnect.apple.com/apps/6819361132).

### Estado del resto de los campos

| Campo | iOS | macOS |
| --- | --- | --- |
| Versión | `1.1.4`, creada y guardada en App Store Connect | `1.1.4`, creada y guardada en App Store Connect |
| Descripción | Guardada en App Store Connect | Guardada en App Store Connect |
| Texto promocional | Guardado en App Store Connect | Guardado en App Store Connect |
| Palabras clave | Guardadas en App Store Connect | Guardadas en App Store Connect |
| URL de soporte | `https://help.playworship.app`, guardada | `https://help.playworship.app`, guardada |
| URL de marketing | `https://playworship.app`, guardada | `https://playworship.app`, guardada |
| URL de privacidad | Guardada como dato común del registro | Guardada como dato común del registro |
| Publicación de la versión | Manual, guardada | Manual, guardada |
| Capturas comerciales | 4 de iPhone y 5 de iPad cargadas; persistencia y orden comprobados. Quinta iPhone pendiente | 5 cargadas; persistencia y orden comprobados |
| Descripción y pruebas de TestFlight | Borradores abajo; la compilación 2 ya está pendiente de revisión externa | Borradores preparados; la última evidencia del binario es «Procesando» |
| Notas de revisión | Borrador local; sin guardar | Borrador local; sin guardar |
| Cuenta y contacto de revisión | Pendientes; sin guardar | Pendientes; sin guardar |
| Declaraciones, clasificación de edad y copyright | Pendientes; sin guardar | Pendientes; sin guardar |
| Compilación de distribución | Archive y exportación locales completados; ver detalle abajo | Archive universal y paquete firmado completados; preflight local aprobado |
| Carga de compilación en Apple | Compilación 2 entregada y procesada; última evidencia «Pendiente de revisión» externa. Compilación 1 histórica | Paquete 1.1.4 (1) entregado; última evidencia «Procesando», sin resultado final confirmado |
| Grupos, testers e invitaciones | Testers asignados; instalación interna registrada en iPhone 16. Acceso externo sujeto a revisión | Sin testers asignados en la última evidencia local |

### Estado de la compilación iOS

Se completaron localmente el nuevo Archive y la exportación de distribución de
PlayWorship **1.1.4 (2)** con el equipo `V9T23PX769`, firma **Apple Distribution**
y SDK **26.5**, incorporando la corrección de área segura de iPad, la
configuración de importación de documentos y el dSYM de la app. Archive,
exportación y preflight estricto del artefacto distribuible pasaron.

| Artefacto seleccionado | Valor verificado |
| --- | --- |
| Archive | `build/ios-appstore/archive/PlayWorship-build2.xcarchive` |
| IPA | `build/ios-appstore/export-build2/PlayWorship.ipa` |
| Tamaño de IPA | 22 123 832 bytes |
| SHA-256 | `384555033f1f3ca0a64af0ad2457acefdee790e5715b1874578e7cdc28f58dea` |
| Preflight de distribución | PASS |
| Plist final | `CFBundleVersion=2`, `LSSupportsOpeningDocumentsInPlace=false` |
| dSYM de PlayWorship | Incluido en `archive/PlayWorship-build2.xcarchive/dSYMs/PlayWorship.app.dSYM` |
| UUID app y dSYM, arm64 | `FA939419-DE44-3303-9A02-6357CAD5FD59`, coincidencia exacta |
| Entrega a Apple | Confirmada: subida de compilación 2 finalizada con código 0 |
| ID de compilación 2 | `bd117184-121d-4af3-ac7b-2053b37773b3` |
| Procesamiento de Apple y TestFlight | Procesada; última evidencia local «Pendiente de revisión» para pruebas externas |
| Caducidad de compilación 2 en TestFlight | 90 días |

La entrega de la compilación 2 está confirmada. El log
`/tmp/playworship-build2-upload.log` registra el 5 de octubre de 2026 a las
15:38:06, según su propia hora, `Upload succeeded`, `Uploaded MTControls_Standalone`
y `EXPORT SUCCEEDED`; el proceso terminó con código 0. El portal mostró
inicialmente **1.1.4 (2), «Procesando»**, con hora de carga 15:37.

El procesamiento terminó y se registró inicialmente **«Lista para enviar»**.
La evidencia histórica de esa etapa está en
`output/app-store-preparation/testflight-build2.jpg`. Después se asignaron
testers y la última evidencia local del 5 de octubre registra **1.1.4 (2),
«Pendiente de revisión»** para pruebas externas e instalación del tester
interno en iPhone 16. La instalación acredita acceso interno, sin demostrar
QA funcional completa ni aprobación o envío de invitaciones externas.

La respuesta de esta subida ya no incluye `ITMS-90737` ni la advertencia por
dSYM ausente de PlayWorship. Su única advertencia es el dSYM pendiente de Bungee,
UUID `313F0E93-013F-3D80-A1EC-A626D6CA6033`.

### Estado de la compilación macOS

La última evidencia local del 5 de octubre registra **1.1.4 (1)** universal
`arm64` / `x86_64`, Archive
`build/mac-appstore/archive/PlayWorship.xcarchive` y paquete firmado
`build/mac-appstore/export/PlayWorship.pkg`, de 13 663 473 bytes y SHA-256
`675517de52da9837c095ac33ef590b54bbffdd1ae831ecd9cc08c3b6cb161047`.
La exportación, el preflight y `mac_app_store_safety` pasaron; el inicio con
sandbox llegó al login. La QA funcional completa sigue pendiente.

La entrega a Apple terminó con código 0 el **2026-10-05 a las 19:41:36 UTC**,
ID `af3484c0-ed66-4c86-9038-605c9b7592a5`. La última captura local,
`output/app-store-preparation/mac-testflight-processing.png`, mostró
**«Procesando»**. No hay resultado final de procesamiento ni testers asignados
registrados en esa evidencia. Los dSYM de la app están incluidos; siguen
pendientes los símbolos de Bungee. Ver [Mac App Store](../release/MacAppStore.md).

### Histórico confirmado — compilación 1

El artefacto `build/ios-appstore/export-safearea/PlayWorship.ipa`, de
8 201 156 bytes y SHA-256
`e6c19630853dd04ee71ffec7ff8f41ad6a6b11ccd83547da8d12663e7230dca2`,
se entregó y procesó como **1.1.4 (1)**. Su Archive es
`build/ios-appstore/archive/PlayWorship-safearea.xcarchive`.

El log `/tmp/playworship-testflight-upload.log` registra el 5 de octubre de
2026 a las 15:20:28, según la hora del propio log, la entrega exitosa y
`Uploaded package is processing`. Esto acredita la subida de la compilación
1; el resultado de procesamiento se comprobó después en el portal.

Después se verificó el portal el 5 de octubre de 2026: el procesamiento
terminó y TestFlight muestra **1.1.4 (1), «Lista para enviar»**, con caducidad de
90 días. La evidencia local histórica
`output/app-store-preparation/testflight-upload.jpg` registra esta comprobación;
su ID de compilación es `cf98ebc6-6206-4b82-97ff-07d4f8f0193b`.
Se cargó el binario universal de iPhone y iPad. En ese momento aún no se habían
asignado grupos o testers. Este estado histórico no sustituye el de la
compilación 2 ni la entrega macOS descritos arriba.

La IPA anterior de `build/ios-appstore/export/PlayWorship.ipa` se conserva
localmente como evidencia histórica y **no debe subirse**. El Archive anterior
`PlayWorship.xcarchive` tampoco es el candidato seleccionado. La compilación 1
`safearea` ya entregada también queda como histórico; el candidato vigente es
la compilación 2.

La entrega y el procesamiento históricos de la compilación 1 no acreditan
aprobación de App Review ni una instalación de esa compilación mediante
TestFlight. La instalación interna posterior corresponde a iOS 1.1.4 (2).

### Advertencias de la compilación 1 y correcciones del candidato 2

Apple emitió tres advertencias no fatales en la compilación 1, cuya subida
terminó correctamente. El candidato 2 incorpora estas correcciones locales;
su entrega y procesamiento están confirmados:

| Advertencia | Próximo ajuste de build |
| --- | --- |
| `CFBundleDocumentTypes` sin configuración de documentos; aviso `ITMS-90737` | Corregido en fuente y plist final de la compilación 2: `LSSupportsOpeningDocumentsInPlace=false`, acorde al flujo por copia. Preflight aprobado; la respuesta de subida de Apple ya no incluye este aviso. |
| Falta dSYM de PlayWorship, UUID histórico `846D5D49-3BDF-3FE7-8BAB-16AD27B8575A` | Corregido en compilación 2: dSYM nuevo incluido en el Archive con UUID `FA939419-DE44-3303-9A02-6357CAD5FD59`, igual al ejecutable arm64. La respuesta de subida de Apple ya no incluye esta advertencia. |
| Falta dSYM de Bungee, UUID `313F0E93-013F-3D80-A1EC-A626D6CA6033` | El framework viene precompilado sin símbolos correspondientes. Queda pendiente obtener el dSYM del proveedor; no se pueden fabricar símbolos útiles a partir de ese binario. |

Los UUID de las advertencias pertenecen a la compilación 1; el UUID nuevo
identifica el ejecutable y dSYM de la compilación 2. No reutilizar símbolos de
otra compilación. La falta de símbolos de Bungee limita el análisis de sus
reportes de cierre y queda pendiente del proveedor.

CMake aplica `-g` y `DEBUG_INFORMATION_FORMAT=dwarf-with-dsym` a Release en
`MTControls` y `MTControls_Standalone`. Xcode dejó el dSYM nuevo en
`build/ios-appstore/MTControls_artefacts/Release/Standalone/PlayWorship.app.dSYM`.
Se comprobó su UUID arm64 contra el ejecutable antes de copiarlo a
`PlayWorship-build2.xcarchive/dSYMs`. El validador del Archive ahora exige un
dSYM coincidente. La última evidencia local registra **1.1.4 (2)** recibida y
procesada, con revisión externa pendiente; **1.1.4 (1)** es el histórico anterior.

El comportamiento de importación está verificado en
`src/ui/panels/SongListPanel.cpp`: `importProjectUrlToSandbox` copia el stream
de la URL a `SongLibraryStore` bajo `imports/projects`, y
`openProjectFileFromUrl` abre esa copia en iOS. La declaración `false` expresa
ese flujo; no habilita edición del archivo externo en su ubicación original.

### Capturas comerciales cargadas — última evidencia local 2026-10-05

El conjunto cargado contiene **14 JPEG revisados**, con persistencia y orden
comprobados tras recargar la ficha 1.1.4, `es-ES`:

| Plataforma | Cantidad y orden | Dimensiones |
| --- | --- | --- |
| iPhone 6,9 pulgadas | 4: `01-biblioteca.jpg`, `02-ensayo.jpg`, `03-setlist.jpg`, `04-pads.jpg` | 1320 × 2868; Pads horizontal 2868 × 1320 |
| iPad 13 pulgadas | 5: `01-reproduccion.jpg`, `02-biblioteca.jpg`, `03-setlist.jpg`, `04-secciones.jpg`, `05-control-remoto.jpg` | 2752 × 2064 |
| Mac | 5: `01-reproduccion.jpg`, `02-biblioteca.jpg`, `03-setlist.jpg`, `04-automatizacion.jpg`, `05-control-remoto.jpg` | 2880 × 1800 |

Los archivos están bajo `output/app-store-preparation/screenshots/` en las
carpetas `iphone/`, `ipad/` y `macos/`. La quinta iPhone de control remoto sigue
pendiente de sustitución y revisión: el candidato horizontal fue rechazado por
solapamiento de Dynamic Island; el reemplazo vertical necesita un alias legible.
El ZIP local contiene solamente las 14 imágenes aprobadas y cargadas.

El inventario local `output/app-store-preparation/screenshots/README.md`
registra hashes, procedencia, revisión visual y archivos rechazados. La prueba
de persistencia está en `output/app-store-preparation/screenshots-uploaded-iphone.png`,
`output/app-store-preparation/screenshots-uploaded-ipad.png` y
`output/app-store-preparation/screenshots-uploaded-mac.png`. Esta evidencia no
está versionada y no acredita aprobación de Apple. Las imágenes muestran la
aplicación real con contenido demo original, sin datos personales visibles.
Las capturas iOS proceden del simulador; las de Mac muestran la app de escritorio
y no acreditan por sí mismas QA del binario de Mac App Store.

La imagen iPad se capturó tras corregir el área segura: TopBar, waveform,
mixer, pads y drawer se mantienen dentro de los límites seguros. El simulador
se preparó con `Full Screen Apps` y estado 9:41. Esa versión usaba
`UIRequiresFullScreen=true` y compatibilidad de tamaño; la captura conserva su
procedencia. El contrato actual usa `UIRequiresFullScreen=false` y las cuatro
orientaciones de iPad para adaptar el layout a la escena real. Las imágenes
anteriores no acreditan esa rotación ni el resize del nuevo binario. Ver el
[contrato de distribución](../release/AppStore.md).

## Texto promocional — iOS y macOS

```text
Prepara tus pistas, organiza setlists y controla la mezcla, el clic y los pads. Ensaya y acompaña a tu equipo de adoración con PlayWorship.
```

## Descripción — iOS

```text
PlayWorship reúne tus pistas de audio y los controles que necesitas para preparar ensayos y acompañar a tu equipo de adoración desde iPhone o iPad.

ORGANIZA TU MÚSICA
Importa tus propios archivos de audio, crea canciones con título, tono y tempo, y organiza la biblioteca en setlists. Guarda tus proyectos para volver a usarlos.

CONTROLA CADA PISTA
Ajusta el volumen de los instrumentos, silencia pistas o escúchalas por separado con Solo. Cambia el tono y el tempo para adaptar la canción a tu ensayo. Configura las salidas disponibles de tu dispositivo o interfaz de audio compatible.

CLIC, GUÍAS Y PADS
Configura el metrónomo y utiliza guías de audio para identificar las partes de una canción. Añade pads y controla su tonalidad y nivel desde la aplicación.

PREPARA TUS SECCIONES
En iPad puedes editar secciones del arreglo, preparar repeticiones y saltos, y guardar ajustes de mezcla y pads por sección.

ENSAYA EN IPHONE
La vista Ensayo permite elegir un instrumento, recorrer su forma de onda, ajustar tono y tempo y repetir una región A–B mientras practicas.

CONECTA TUS EQUIPOS
El control remoto permite manejar una sesión de PlayWorship desde otro dispositivo compatible en la misma red local. El audio se reproduce en el dispositivo que actúa como servidor.

APRENDE DENTRO DE LA APP
Las guías de aprendizaje te acompañan por la biblioteca, el transporte, la mezcla y el control remoto usando los controles de PlayWorship.

Se requiere iniciar sesión con una cuenta de PlayWorship. Las funciones de nube y colaboración dependen de los permisos y límites de la cuenta y necesitan conexión a Internet. Utiliza archivos y contenido para los que tengas los derechos necesarios.
```

## Descripción — macOS

```text
PlayWorship reúne tus pistas de audio y los controles que necesitas para preparar ensayos y acompañar a tu equipo de adoración desde tu Mac.

ORGANIZA TU MÚSICA
Importa tus propios archivos de audio, crea canciones con título, tono y tempo, y organiza la biblioteca en setlists. Guarda tus proyectos para volver a usarlos.

CONTROLA CADA PISTA
Ajusta el volumen de los instrumentos, silencia pistas o escúchalas por separado con Solo. Cambia el tono y el tempo para adaptar la canción a tu ensayo. Configura las salidas disponibles de tu dispositivo o interfaz de audio compatible.

CLIC, GUÍAS Y PADS
Configura el metrónomo y utiliza guías de audio para identificar las partes de una canción. Añade pads y controla su tonalidad y nivel desde la aplicación.

PREPARA TUS ARREGLOS
Edita las secciones de una canción, copia partes para construir tu arreglo y prepara repeticiones y saltos. Guarda ajustes de mezcla y pads por sección y vuelve a ellos al reproducir la canción.

CONECTA TUS EQUIPOS
El control remoto permite manejar una sesión de PlayWorship desde otro dispositivo compatible en la misma red local. El audio se reproduce en el dispositivo que actúa como servidor.

APRENDE DENTRO DE LA APP
Las guías de aprendizaje te acompañan por la biblioteca, el transporte, la mezcla, las secciones y el control remoto usando los controles de PlayWorship.

Se requiere iniciar sesión con una cuenta de PlayWorship. Las funciones de nube y colaboración dependen de los permisos y límites de la cuenta y necesitan conexión a Internet. Utiliza archivos y contenido para los que tengas los derechos necesarios.
```

## Palabras clave — iOS y macOS

```text
multitracks,stems,iglesia,alabanza,mezclador,metronomo,pads,setlist,tono,tempo,musicos
```

Las palabras clave se mantienen sin acentos para cumplir también el límite
de **100 bytes UTF-8**. No contienen marcas de otras aplicaciones.

## URLs verificadas y datos que faltan

| Campo | Estado |
| --- | --- |
| URL de soporte | [https://help.playworship.app](https://help.playworship.app): HTTPS 200; centro de ayuda con enlace publicado a `hola@playworship.app` en barra lateral y pie. |
| URL de política de privacidad | [https://playworship.app/legal/privacidad.html](https://playworship.app/legal/privacidad.html): HTTPS 200; política real con responsable y email. **Pendiente** comprobar correspondencia con cada binario, backend y SDK. |
| URL de marketing | [https://playworship.app](https://playworship.app): HTTPS 200; landing real de PlayWorship. |
| Términos / acuerdo de licencia | [https://playworship.app/legal/terminos.html](https://playworship.app/legal/terminos.html): HTTPS 200; términos reales. **Pendiente** revisar el uso de la licencia estándar de Apple o aportar una licencia propia. |
| Email de comentarios de TestFlight | `hola@playworship.app` es el contacto publicado; **pendiente** confirmar que el buzón está atendido y guardarlo para TestFlight. |
| Contacto de revisión | **Pendiente**: nombre, email y teléfono internacional del responsable. |
| Cuenta de revisión | **Pendiente**: usuario y contraseña exclusivos de prueba; no se incluyen credenciales en este archivo. |
| Canción de prueba | **Pendiente**: preparar una canción accesible a la cuenta de revisión, con audio autorizado y funciones identificables. |

No usar una URL candidata como URL confirmada ni introducir direcciones,
teléfonos o credenciales inventados para completar los campos.

### Evidencia de páginas públicas

Comprobación del 5 de octubre de 2026, alrededor de las 17:10 UTC: las cuatro
URLs respondieron **HTTP 200 por HTTPS con validación normal del certificado**.
La lectura web de estas páginas no estuvo disponible en la herramienta de
navegación; se verificaron mediante solicitudes HTTP públicas de lectura.

La landing devuelve un documento HTML de PlayWorship y carga su aplicación
web. Su script público contiene el enlace de privacidad y el email de
contacto. El centro de ayuda también usa una aplicación web; su script
público publica enlaces `mailto:hola@playworship.app` y el texto de contacto
«Contacta al equipo». Esta comprobación acredita contenido publicado y enlaces;
no es una prueba del funcionamiento del buzón ni una revisión visual en navegador.

La política declara a LUJUMA SYSTEM SAS como responsable, describe datos de
cuenta y dispositivo, suscripción, almacenamiento, proyectos, archivos
sincronizados y registros técnicos, y ofrece consultas sobre datos mediante
`hola@playworship.app`. Los términos describen el uso local y el servicio Cloud.
La solicitud de eliminación por email publicada en la política no acredita
por sí misma un flujo de eliminación de cuenta dentro de la app.

## TestFlight — descripción de la beta

### iOS

```text
Esta beta de PlayWorship para iPhone y iPad permite probar la biblioteca, los setlists, la reproducción multipista, la mezcla, el clic, las guías, los pads y el control remoto en red local. En iPhone incluye la vista Ensayo; en iPad, la edición de secciones y sus ajustes de mezcla.

Necesitas una cuenta de prueba habilitada y audio propio o autorizado. Las funciones de nube dependen de los permisos de esa cuenta. Envía comentarios indicando modelo, versión de iOS o iPadOS, interfaz de audio y pasos para reproducir el problema.
```

### macOS

```text
Esta beta de PlayWorship para Mac permite probar la biblioteca, los setlists, la reproducción multipista, la mezcla, el clic, las guías, los pads, los arreglos por secciones y el control remoto en red local.

Necesitas una cuenta de prueba habilitada y audio propio o autorizado. Las funciones de nube dependen de los permisos de esa cuenta. Envía comentarios indicando modelo de Mac, versión de macOS, interfaz de audio y pasos para reproducir el problema.
```

## TestFlight — qué probar

### iOS

```text
1. Inicia sesión con la cuenta de prueba habilitada y comprueba el regreso a la app.
2. Abre Biblioteca, crea una canción con archivos de audio desde Archivos y agrégala a un setlist. Cierra y abre la app para comprobar que sigue disponible.
3. Reproduce, pausa y detén. Cambia volumen, Mute y Solo de una pista; ajusta tono y tempo y comprueba el resultado audible.
4. Configura el clic y las guías, prepara los pads y prueba sus notas y niveles.
5. En iPhone, abre Más > Ensayo, elige un instrumento y prueba desplazamiento, zoom y una repetición A–B. Al cerrar, comprueba el retorno a la vista de reproducción.
6. En iPad, crea o edita secciones, prueba una repetición y un salto, y guarda un ajuste de mezcla por sección.
7. Prueba los altavoces y una interfaz de audio compatible: revisa salidas, clic/guía y continuidad al cambiar de canción o poner la app en segundo plano.
8. Con dos dispositivos en la misma red, abre Configuración > Control remoto, activa Servidor en el que reproduce audio y Cliente en el otro. Autoriza red local y comprueba Play, Stop y mezcla desde el cliente.
9. Si la cuenta tiene nube habilitada, descarga una canción y comprueba una sincronización de ajustes sin cambiar el ruteo propio del dispositivo.

Reporta cierres, audio interrumpido, controles ocultos por teclado/área segura o cambios que no se conserven. Incluye dispositivo, versión del sistema y pasos exactos.
```

### macOS

```text
1. Inicia sesión con la cuenta de prueba habilitada y comprueba el regreso desde el navegador a la app.
2. Abre Biblioteca, importa archivos de audio, crea una canción y agrégala a un setlist. Cierra y abre la app para comprobar que sigue disponible.
3. Reproduce, pausa y detén. Cambia volumen, Mute y Solo de una pista; ajusta tono y tempo y comprueba el resultado audible.
4. Configura el clic y las guías, prepara los pads y prueba sus notas y niveles.
5. Edita secciones, copia una parte para preparar un arreglo y prueba una repetición y un salto. Guarda un ajuste de mezcla por sección y comprueba su aplicación durante la reproducción.
6. Prueba una interfaz de audio compatible: revisa el dispositivo seleccionado, sus salidas, el ruteo del clic y de las guías, y la continuidad al cambiar de canción.
7. Comprueba selección de archivos, teclado, rueda/trackpad, desplazamiento en Configuración y tamaños de ventana.
8. Con dos dispositivos en la misma red, activa Servidor en el que reproduce audio y Cliente en el otro desde Configuración. Comprueba conexión, Play, Stop y mezcla desde el cliente.
9. Si la cuenta tiene nube habilitada, descarga una canción y comprueba una sincronización de ajustes sin cambiar el ruteo propio del Mac.

Reporta cierres, audio interrumpido, problemas de acceso a archivos o cambios que no se conserven. Incluye modelo de Mac, versión de macOS, interfaz de audio y pasos exactos.
```

## Notas de revisión — borrador común

Texto para adaptar al binario y a la cuenta reales. No enviarlo mientras los
campos pendientes impidan que el revisor complete estos pasos.

```text
PlayWorship es una herramienta de reproducción y preparación de pistas para ensayos y presentaciones musicales.

ACCESO
La app requiere iniciar sesión. Se proporcionarán en los campos privados de App Review las credenciales de una cuenta de prueba que no expire durante la revisión. Esa cuenta debe tener acceso al contenido de prueba y a las funciones que se describen en la ficha. Las credenciales y la canción de prueba están pendientes de preparación; estos textos son un borrador.

RECORRIDO
1. Abrir PlayWorship y pulsar Ingresar. Completar el inicio de sesión con la cuenta de revisión.
2. Abrir Biblioteca, localizar la canción de prueba indicada en las notas finales y cargarla. Si requiere descarga, esperar a que termine.
3. Pulsar Play; probar Pausa y Stop. Abrir la mezcla, cambiar el volumen de una pista y comprobar Mute y Solo. Probar tono y tempo.
4. Configurar el metrónomo y probar las guías disponibles para la canción. Abrir Pads y probar una nota después de completar la preparación de su paquete.
5. En iPhone, abrir Más > Ensayo y probar la selección de instrumento y la repetición A–B. En iPad y Mac, revisar las secciones del arreglo y sus ajustes de mezcla.
6. Abrir Configuración para revisar la sesión y el dispositivo de audio. Las salidas adicionales dependen de una interfaz de audio compatible conectada al equipo.

CONTROL REMOTO OPCIONAL
Requiere dos dispositivos compatibles en la misma red local. En Configuración, seleccionar Servidor en el equipo que reproduce audio y Cliente en el otro. Conectar el cliente al servidor detectado. El cliente envía controles; el audio se reproduce únicamente en el servidor.

DEPENDENCIAS
El inicio de sesión y las funciones de nube necesitan Internet. Para importar contenido propio, el revisor puede seleccionar archivos de audio autorizados mediante el selector de archivos del sistema.
```

Antes del envío, sustituir el párrafo de acceso por instrucciones comprobadas,
identificar por nombre el contenido de prueba y describir el modelo de acceso
comercial del binario. Confirmar los pendientes de autenticación, eliminación
de cuenta y compras digitales del [preflight de iOS](../release/AppStore.md).
La distribución macOS requiere además validar su binario para Mac App Store.

## Comprobación de límites

Los conteos incluyen saltos de línea dentro de los bloques, excluyen el salto
final del bloque y no incluyen sus delimitadores Markdown.

<!-- metadata-counts:start -->
| Campo | Caracteres | Bytes UTF-8 | Límite |
| --- | ---: | ---: | --- |
| Nombre | 11 | 11 | 30 caracteres |
| Subtítulo | 30 | 31 | 30 caracteres |
| Texto promocional (ambas) | 139 | 141 | 170 caracteres |
| Descripción iOS | 1689 | 1714 | 4000 caracteres |
| Descripción macOS | 1608 | 1632 | 4000 caracteres |
| Palabras clave (ambas) | 86 | 86 | 100 bytes |
| Descripción beta iOS | 532 | 537 | Texto breve propuesto |
| Descripción beta macOS | 457 | 461 | Texto breve propuesto |
| Qué probar iOS | 1407 | 1426 | Texto breve propuesto |
| Qué probar macOS | 1423 | 1442 | Texto breve propuesto |
| Notas de revisión (borrador) | 1862 | 1889 | 4000 bytes |
<!-- metadata-counts:end -->

## Fuentes de producto

- [Creación de canciones](../importing/SongCreation.md): audio importado y proyectos.
- [Transporte](../transport/Transport.md): reproducción, secciones, saltos y loops.
- [Layout móvil](../ui/MobileLayout.md): Ensayo en teléfono e interfaz en tablet.
- [Automatización](../engine/SectionAutomation.md): mezcla y pads por sección.
- [Metrónomo](../audio-systems/Metronome.md), [guías](../audio-systems/DynamicGuide.md) y [pads](../audio-systems/Pads.md).
- [Control remoto](../integrations/RemoteControl.md): red local y audio en servidor.
- [Nube](../integrations/CloudSync.md): contenido portable y ajustes.
- [Aprendizaje](../ui/LearningGuide.md): cursos y diferencias por dispositivo.
- [Autenticación](../integrations/Auth.md): regreso de la sesión en Apple.
- `src/ui/components/WelcomeModal.cpp`: acceso mediante Ingresar.

## Fuentes Apple

Revisadas el 5 de octubre de 2026:

- [Información de la app](https://developer.apple.com/help/app-store-connect/reference/app-information/app-information/): nombre, subtítulo, identificador, SKU y privacidad.
- [Información de la versión por plataforma](https://developer.apple.com/help/app-store-connect/reference/app-information/platform-version-information/): descripción, texto promocional, palabras clave, soporte y revisión.
