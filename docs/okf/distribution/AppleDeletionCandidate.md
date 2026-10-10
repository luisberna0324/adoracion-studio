---
title: Candidato Apple y eliminación — evidencia local 2026-10-10
status: Firmado localmente; no habilitado ni publicado
---

# Candidato mínimo

Rama feat/account-deletion-minimal. Integra fuente df0275e de autenticación/borrado y UI e1f2097 en commit8059dd4 (selector de vista de secciones en Más). Sin CI ni rediseño checkout/claim. La comparación de código y los builds locales no incluyen cambios personales de otros checkouts.

QA10oct: app macOS compilada; CTests account_deletion_flow, email_authentication_ui, apple_signin_contract, apple_credential_lifecycle y waveform_editing: 5/5. iOS Simulator arm64 con MTCONTROLS_APPLE_SIGN_IN_ENABLED=ON y CODE_SIGNING_ALLOWED=NO: BUILD SUCCEEDED, instalado y abierto en PlayWorship Apple Auth QA iOS26.5. Se verificó pantalla inicial y navegación al formulario de demo offline; no login/proveedor/borrado reales. Apple no visible cuando config de producción lo deshabilita (fail-closed). Se configuró candidato dispositivo arm64 con SDK Bungee existente en lectura; archive sin firma en evidencia externa, no IPA distribuible.

Simulator metadata: com.multitracks.playworship, 1.1.4 build2, MinimumOSVersion18.5, ITSAppUsesNonExemptEncryption=false, PrivacyInfo.xcprivacy presente; entitlements generados Apple=[Default]. Esto no acredita capability del perfil de distribución ni firma. TestFlight leído10oct muestra builds1,2,3 de1.1.4 En pruebas. Este build2 es solo QA local; para distribución usar4 o siguiente libre tras reconsulta, no reutilizar1–3.

## Portal Apple: auditoría y cambios explícitamente autorizados

Registro6819361132, iOS/macOS1.1.4 en preparación. iOS sin build asociado; copyright, cuenta de prueba requerida, contacto y notas de revisión vacíos. Descripción/promo/keywords/URLs soporte y marketing existentes. Publicación manual seleccionada. iPad13 tiene cinco capturas; iPhone muestra cuatro imágenes reutilizadas pero cero para tamaño mediano seleccionado: revisar gestor tamaños/compatibilidad antes de afirmar cumplimiento.

General nombre/subtítulo/categoría Música y EULA estándar presentes. Derechos de contenido y edades sin configurar. Privacidad URL presente y etiquetas todavía no iniciadas (Comenzar). Aviso comercianteUE visible; no se declaró situación ni aceptó acuerdo.

Developer conserva clave Apple y certificados previos. Tras aprobación específica
se generó CSR con clave privada en llavero Mac y se emitió AppleDistribution
RG6N9746XA (vigencia10oct2026–10oct2027). Se creó perfilAppStore
H6DG2DGGNL, UUID41bfab24-08e5-4da3-afed-e65b5632cc88, paraTeamV9T23PX769
ybundlecom.multitracks.playworship. Perfil activo con Sign In with Apple,
get-task-allow=false; identidad privada reconocida localmente. Solo CSR público
se transmitió aApple; no se exportó clave privada ni se revocó certificado.
Archive firmado1.1.4 build4 verificado con codesign --verify --deep --strict; perfil/entitlements correctos, PrivacyInfo presente. Xcode generó dSYM fuera del archive por outputCMake; se incorporó tras verificar UUID idéntico al binario C5BDFB07-0EBC-3135-B9F7-562AB9541ECB. Ninguna subida aTestFlight/revisión.

R2: código backend7049b27/appd971ff0 implementa multipart con finalización exclusiva
del backend. QA178/178 backend,7/7 CTests (añadidosCloudSettings/CloudSyncIndex).
Estas pruebas no verifican aborto físicoR2 ni drenaje dePUThistóricos. Mantener
borrado productivo deshabilitado hasta evidencia real y políticas restantes.

## Checklist finito previo a publicación

1. Resolver R2/retención/Gumroad y ejecutar backend/worker solo tras aprobación. El inicio dentro de app y estado durable están preparados; no marcar completado sin garantías reales.
2. Configurar Apple en revisión nueva basada en producción61, preservando callback Google público. No promover preview72 ni cambiar sus credenciales. Prueba Apple real con cuenta autorizada.
3. Certificado/perfil ya autorizados y creados; finalizar archive/export local con build4, comprobar firma, dSYM y privacidad; QA de dispositivo y del IPA final. No permitir creación automática de credenciales no aprobadas.
4. Completar campos ASC indicados con datos aprobados y nota de revisión exacta: ubicación de Apple/borrado, comportamiento asíncrono y datos retenidos conforme a política. No inventar cuenta/contacto/plazo.
5. Revisar capturas y declaraciones contra binario final. Subida/TestFlight/AppReview/publicación requieren aprobación adicional; no realizadas.

Apple permite borrado asíncrono razonable iniciado en app, con información de plazo y confirmación. No exige instantaneidad ni permite sustituirlo por soporte obligatorio en una app no regulada. No interpretar este documento como App Review ready.

Fuente actual: https://developer.apple.com/help/app-review/guideline-reference/5-1-1-account-deletion . Plan backend completo en docs/operations/apple-deletion-activation.md del repo playworship.
