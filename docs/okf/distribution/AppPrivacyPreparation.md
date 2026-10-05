---
type: Distribution Draft
title: PlayWorship — preparacion de App Privacy
description: Inventario de privacidad confirmado en fuente y candidato iOS, con verificaciones pendientes antes de responder App Store Connect.
tags: [distribution, app-store, privacy, ios, macos]
timestamp: 2026-10-05T00:00:00-03:00
---

# PlayWorship — preparacion de App Privacy

Estado: **borrador local; no son respuestas enviadas al portal**. La ficha
`6819361132` comparte iOS/iPadOS y macOS, Bundle ID
`com.multitracks.playworship`, equipo `V9T23PX769`. Los datos del registro y el
preflight estan en [App Store](../release/AppStore.md).

## Evidencia confirmada

`assets/ios/PrivacyInfo.xcprivacy` declara `NSPrivacyTracking=false` y una lista
vacia de dominios de tracking. Para los seis tipos siguientes declara
`Linked=true`, `Tracking=false` y un unico proposito:
`NSPrivacyCollectedDataTypePurposeAppFunctionality`.

| Tipo declarado en el manifiesto | Identificador exacto |
| --- | --- |
| Email | `NSPrivacyCollectedDataTypeEmailAddress` |
| ID de usuario | `NSPrivacyCollectedDataTypeUserID` |
| ID de dispositivo | `NSPrivacyCollectedDataTypeDeviceID` |
| Historial de compras | `NSPrivacyCollectedDataTypePurchaseHistory` |
| Audio | `NSPrivacyCollectedDataTypeAudioData` |
| Otro contenido del usuario | `NSPrivacyCollectedDataTypeOtherUserContent` |

Estos son valores comprobados del manifiesto, no una auditoria completa del
backend o de todos los proveedores. Estar vinculado a la cuenta y usarse para
funcionalidad no implica tracking publicitario. Tampoco permite afirmar que
la app no recoge datos. Las respuestas finales deben cubrir la recoleccion y
uso reales, incluidos terceros. [Criterios de App Privacy de Apple](https://developer.apple.com/app-store/app-privacy-details/).

| API de motivo obligatorio declarada | Motivos | Uso descrito por el contrato local |
| --- | --- | --- |
| `NSPrivacyAccessedAPICategoryFileTimestamp` | `C617.1`, `3B52.1` | Archivos del contenedor y documentos elegidos por el usuario |
| `NSPrivacyAccessedAPICategorySystemBootTime` | `35F9.1` | Temporizadores y audio/MIDI |
| `NSPrivacyAccessedAPICategoryDiskSpace` | `E174.1` | Espacio disponible para cache de audio |

Los motivos deben corresponder a los accesos del binario final y sus
dependencias; la tabla no reemplaza esa comprobacion.

El candidato iOS en `build/ios-appstore/CMakeCache.txt` tiene
`MTCONTROLS_ENABLE_SENTRY:BOOL=OFF`, verificado el 2026-10-05. CMake solo enlaza
`sentry::sentry` cuando la integracion esta habilitada. Ese estado debe
comprobarse otra vez para cada entrega. No describe otras compilaciones ni
demuestra que proveedores del backend no guarden logs o diagnosticos.

## Flujos conocidos y validaciones pendientes

| Flujo | Evidencia local | Confirmar antes de responder el formulario |
| --- | --- | --- |
| Autenticacion | Google OAuth con `openid email profile`; tokens Firebase; el cliente envia un ID estable de dispositivo | Campos que conserva el backend/Google, incluido posible nombre o avatar; propositos y retencion |
| Plan y acceso | El backend devuelve plan, estado y licencia; la app los vincula a la sesion | Datos de compras conservados, proveedores de cobro y usos adicionales |
| Cloud | Audio, artwork, metadata portable y versiones de ajustes se publican por accion del usuario | Categorias exactas del contenido, incluida portada, almacenamiento/retencion y acceso de proveedores |
| Teams | La nube compartida usa miembros y datos de propietario; el acceso se valida en el servidor | Emails de miembros/propietario, visibilidad, contenido compartido y su eliminacion |
| Diagnostico | ZIP local pedido por el usuario, con ID de cuenta y metricas; sin audio, credenciales ni upload automatico al backend | Recoleccion/retencion cuando soporte recibe el archivo y si cumple todos los criterios de declaracion opcional de Apple |
| Permisos | Bluetooth MIDI, red local y audio en segundo plano; microfono deshabilitado | Que el binario y los SDKs no incorporen recoleccion adicional mediante estas capacidades |

Fuentes: [Auth](../integrations/Auth.md),
[Cloud Sync](../integrations/CloudSync.md),
[Teams](../integrations/Teams.md),
[Compartir diagnostico](../integrations/PlaybackDiagnosticSharing.md),
`src/PluginEditorSupport.cpp::buildStableDeviceId` y
`src/data/AuthSessionApi.cpp`. No se han inspeccionado datos privados de
usuarios ni credenciales para preparar este documento.

## Decisiones abiertas para revision

- **Cuenta:** agregar inicio de eliminacion completa desde la app, con
  confirmacion, contenido asociado y tratamiento de suscripciones. Cerrar
  sesion, purgar datos locales y contactar soporte son funciones distintas.
- **Autenticacion:** resolver Google como login principal conforme a 4.8,
  mediante alternativa equivalente como Sign in with Apple o una excepcion
  aplicable comprobada. El cliente nativo Apple está preparado, pero su
  activación sigue deshabilitada por defecto y depende del backend, la
  capacidad de firma y una prueba de ingreso real. Ver [Auth](../integrations/Auth.md).
- **Privacidad y soporte:** verificar URLs HTTPS funcionales y agregar acceso
  a la politica dentro de la app. El enlace de soporte existente abre
  `mailto:hola@playworship.app`; no verifica una politica publicada.
- **Modelo comercial:** no hay StoreKit/IAP en la fuente auditada. Definir
  compras, acceso adquirido en otras plataformas y restauracion conforme a
  3.1. No dar por admitida la excepcion de app companera solo por ausencia de
  un boton de compra.

En iOS, la entrega debe incluir el manifiesto que CMake agrega actualmente al
bundle y verificarse despues de exportacion. Mac tiene bloqueos adicionales:
App Sandbox y permisos de archivos/red; firma de distribucion que reemplace
el POST_BUILD ad hoc; inclusion y revision del manifiesto; eliminar la
descarga de FFmpeg ejecutable o usar un helper incluido y firmado; resolver
el acceso directo al contenedor de Prime y su proceso SQLite; revisar la
validacion propia de licencias para Mac App Store. El detalle y las fuentes
estan en [App Store](../release/AppStore.md#bloqueos-adicionales-de-mac-app-store).

## Comprobacion antes de guardar respuestas

- [ ] Identificar el Archive/export exacto de cada plataforma y sus flags;
  comprobar que Sentry sigue OFF en el candidato entregado.
- [ ] Inventariar dependencias incluidas, manifiestos de SDKs, frameworks y
  codigo de helpers. Validar firma y bundle final con las herramientas de Apple.
- [ ] Comparar el manifiesto exportado con el de fuente y comprobar motivos
  de APIs frente a los accesos reales de la app y JUCE.
- [ ] Revisar codigo/configuracion de backend y contratos de proveedores sin
  exponer credenciales: campos conservados, fines, vinculacion, retencion,
  terceros y borrado. Registrar las conclusiones por tipo de dato.
- [ ] Revisar nombre/avatar, artwork, compras y diagnosticos; no asumir que
  el listado de seis tipos ya cubre todos los flujos.
- [ ] Revisar las practicas de iOS y macOS de la ficha comun y reconciliar
  cualquier diferencia entre plataformas antes de responder.
- [ ] Verificar politica accesible por HTTPS y desde la app, contacto de
  soporte, y coherencia con login, Cloud, Teams, cobro y eliminacion.
- [ ] Probar login, eliminar cuenta y las capacidades opcionales denegadas
  usando cuentas de prueba autorizadas.
- [ ] Validar con el responsable las respuestas finales y sus propositos;
  conservar esta evidencia antes de guardarlas en App Store Connect.

## Referencias Apple

- [App Privacy: datos, usos y terceros](https://developer.apple.com/app-store/app-privacy-details/)
- [Manifiestos de privacidad](https://developer.apple.com/documentation/bundleresources/privacy-manifest-files)
- [App Review Guidelines: 3.1, 4.8 y 5.1](https://developer.apple.com/app-store/review/guidelines/)
- [Eliminacion de cuenta](https://developer.apple.com/help/app-review/guideline-reference/5-1-1-account-deletion)
