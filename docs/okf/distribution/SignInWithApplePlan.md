---
type: Distribution Plan
title: PlayWorship — alternativa de inicio de sesión con Apple
description: Auditoría de autenticación actual y plan de implementación Apple para iOS, iPadOS y macOS, con identidad Firebase y conservación de acceso.
tags: [distribution, auth, apple, firebase, ios, macos]
timestamp: 2026-10-10T00:00:00Z
---

# PlayWorship — inicio de sesión con Apple

Estado al 2026-10-10: **cliente/UI nativos existentes reutilizados y backend Apple implementado en ramas locales; activación y autorización Apple real pendientes**. El backend `luisberna0324/playworship` implementa config/challenge/native y conserva Firebase Admin/custom tokens como arquitectura de sesiones. Los desafios se consumen atomicamente y la asociacion Firebase se protege con reservas de unicidad en el mismo Firestore: el Auth Emulator permite duplicados mediante una actualizacion Admin directa, por lo que no se supone que providerToLink garantice esa unicidad. Se comprobaron pruebas backend, asociacion Admin en Auth Emulator, contratos/UI macOS, builds nativos macOS e iOS y arranque en un iPad Simulator aislado. No se crearon claves persistentes ni se modificaron portal, reglas de seguridad o produccion; no hubo despliegue ni login Apple real.

Ver [Auth](../integrations/Auth.md) y `docs/operations/apple-auth.md` en el backend para el contrato implementado, variables y colecciones privadas requeridas. El estado de eliminacion completa, notificaciones de revocacion, correo relay y pruebas Apple reales sigue pendiente. La tabla siguiente conserva la auditoria historica del 2026-10-05; no describe como ausentes los componentes que ahora ya existen.

## Recomendación

Agregar **Continuar con Apple** nativo en iPhone, iPad y Mac, manteniendo Google como otra opción. Usar `AuthenticationServices` para obtener una autorización Apple y un endpoint HTTPS propio para validarla. La sesión final continúa siendo Firebase y conserva el contrato de licencias, Cloud y Teams. La app JUCE no necesita incorporar Firebase SDK para este camino.

Esta es una recomendación de arquitectura basada en el código auditado. La regla 4.8 permite un servicio equivalente que reduzca la recolección, permita ocultar el correo y no recoja interacciones para publicidad sin consentimiento. Apple es una opción apropiada para cumplir esas condiciones; ofrecer también correo/contraseña no demuestra por sí solo que las cumple. [Reglas de revisión, 4.8](https://developer.apple.com/app-store/review/guidelines/uk/).

Las capturas y la subida de la compilación actual a TestFlight son trabajo separado. Revisar este plan no convierte al candidato 1.1.4 en una compilación con login Apple. Cuando se implemente, deberá generarse y probarse otro build.

## Fuente actual y consecuencias

Contrato de referencia: [Auth](../integrations/Auth.md), [Teams](../integrations/Teams.md), [App Privacy](AppPrivacyPreparation.md). Backend auditado: repositorio `playworship`.

| Fuente | Comportamiento observado | Consecuencia para Apple |
| --- | --- | --- |
| `src/PluginEditor.cpp::launchExternalLogin` | Un solo botón inicia `/api/auth/google/start` | Agregar selección de proveedor en bienvenida y reautenticación |
| `src/platform/NativeAuthSession.mm` | iOS abre `ASWebAuthenticationSession`; no implementa `ASAuthorizationAppleIDProvider` | Crear puente Apple independiente; conservar Google |
| `CMakeLists.txt` | `AuthenticationServices` y `.mm` de autenticación se incluyen solo para iOS | Agregar puente y enlace de framework para macOS con UIKit/AppKit por plataforma |
| `src/data/LocalAuthCallbackServer.cpp` | Escritorio recibe callback HTTP en loopback | Apple nativo puede devolver directamente un resultado al editor, sin navegador/loopback |
| Backend `app/api/auth/google/callback/route.ts` | Verifica Google, busca `users` en Firestore por correo, crea usuario Firebase si falta y emite custom token | No copiar la búsqueda por correo como identidad Apple |
| Backend `lib/auth.ts` | Verifica Firebase ID token con comprobación de revocación | Reutilizar para APIs autenticadas y vinculación |
| Backend `lib/firestore.ts::ensureUserProfile` | Puede sustituir `users/{uid}.email` y adoptar beneficios pendientes por correo | Conservar correo principal al vincular Apple; no introducir el alias relay como reemplazo |
| Backend `app/api/validate-access/route.ts` | Licencia requiere `license.userUid === uid` | Preservar UID conserva la licencia; otro UID representa otra cuenta |
| Backend `lib/teams.ts` | Agregar miembro usa Firebase Auth `getUserByEmail` | Cuenta Apple nueva debe mostrar su correo relay para agregarla; cuenta vinculada conserva correo principal |
| Backend `lib/teams-service.ts` | Permisos/membresías usan UID; administración exige correo verificado y perfil Teams | Apple no concede Teams ni altera el perfil de suscripción |
| Backend `lib/paddle-cloud-webhook.ts` | Beneficios Cloud pendientes se reclaman por correo verificado | Un alias Apple no identifica automáticamente una compra con otro correo |
| `src/data/SessionPersistence.cpp` | Snapshot incluye refresh token Firebase con ofuscación reversible | Migrar credenciales Apple/Firebase a Keychain en plataformas Apple; no guardar tokens Apple en ese snapshot |

Hay diferencias documentales previas que deberán corregirse al implementar: `Auth.md` describe state en memoria en su apartado final, pero la fuente Google actual utiliza un payload HMAC autocontenido con TTL; también describe búsqueda en Firebase por correo donde el callback auditado consulta Firestore. El callback actual incluye refresh token en la URL. No se han cambiado esos contratos en esta revisión.

## Implementación por fases

### 1. Portal, firma y configuración

- Confirmar equipo propietario y App ID de la ficha compartida iOS/macOS: el candidato documenta `com.multitracks.playworship`. Habilitar Sign in with Apple para el App ID correcto y regenerar perfiles de desarrollo/distribución.
- Incluir `com.apple.developer.applesignin = ["Default"]` en entitlements y comprobarlo en binario **y** perfil exportados. Una casilla en el portal no demuestra que el binario tenga la capacidad.
- Crear una clave Sign in with Apple asociada al App ID primario; guardar su `.p8` únicamente en secretos del servidor. Registrar Team ID y Key ID. La clave App Store Connect de subida y la clave de autenticación Apple son credenciales distintas. [Configuración Apple](https://developer.apple.com/documentation/signinwithapple/configuring-your-environment-for-sign-in-with-apple).
- Configuración propuesta: `APPLE_TEAM_ID`, `APPLE_KEY_ID`, `APPLE_NATIVE_CLIENT_ID` y referencia a secreto `APPLE_PRIVATE_KEY_SECRET_VERSION`. Estos nombres son una propuesta y no variables actuales verificadas.
- Para web/Android/Windows, crear un Services ID asociado al mismo App ID primario y un callback HTTPS registrado; agrupar otros App IDs si finalmente hay identificadores distintos en Mac/iOS. El Services ID web y el Bundle ID nativo son audiences distintos. [Configuración web](https://developer.apple.com/help/account/capabilities/configure-sign-in-with-apple-for-the-web/).

### 2. Autorización nativa y endpoint backend

Contrato acordado para la implementación en curso. El puente y cliente local están
descritos en [Auth](../integrations/Auth.md#apple-nativo--cliente-preparado-activacion-condicionada):

- `GET /api/auth/apple/config` devuelve `{enabled,nativeClientId}` sin secretos.
  La opción exige booleano enabled real, capacidad nativa y entitlements del build;
  una UI compilada no demuestra que el servidor o portal estén listos.
- Challenge usa `{ok:true,challengeId,state,nonce,expiresAtMs}`; state y nonce son
  base64url criptográficos del servidor y expiresAtMs es entero Unix en milisegundos.
  El cliente acepta hasta diez minutos de plazo restante y pasa SHA256(nonce) a Apple.
- Native usa POST JSON con prueba Apple y desafío; `profile.email/fullName` son
  opcionales. No envía UID de Firebase, nonce raw, nuevos grants ni purpose para
  sustituir el propósito ya ligado al desafío. En link requiere Bearer en ambos POST.
- La sesión HTTPS termina en `PersistedSessionSnapshot`, conservando UID y correo
  canónico del servidor y allowed booleano. El cliente no aplica/persiste ni crea
  permisos; el editor conserva las validaciones comunes de acceso.

La siguiente secuencia describe el flujo completo, incluidas las partes del
servidor y de configuración que requieren validación independiente:

1. La app solicita `POST /api/auth/apple/challenge` y recibe `challengeId`, state y nonce de un solo uso, con vencimiento corto. Persistir el desafío del lado servidor para todas las instancias; vincular a proveedor, plataforma, propósito y device ID. En vinculación, ligarlo al UID ya autenticado y a su consentimiento.
2. El puente `NativeAppleSignIn.{h,cpp,mm}` inicia `ASAuthorizationController` con `ASAuthorizationAppleIDProvider`, `email` y opcionalmente `fullName`, `state` y hash SHA-256 del nonce. En este contrato los valores secretos los genera el CSPRNG del servidor; el cliente no los sustituye por UUID ni aleatoriedad no criptográfica. El botón del sistema usa el ancla de ventana UIKit/AppKit adecuada. [Autenticación nativa](https://developer.apple.com/documentation/signinwithapple/authenticating-users-with-sign-in-with-apple), [nonce y Firebase](https://firebase.google.com/docs/auth/ios/apple).
3. El delegate entrega resultado o cancelación al message thread de JUCE mediante `SafePointer`. La app confirma state y envía `identityToken`, `authorizationCode`, `challengeId` y datos iniciales de perfil a `POST /api/auth/apple/native` por HTTPS. No incluirlos en URLs, logs o telemetría.
4. El servidor verifica firma con claves Apple y algoritmo permitido, issuer `https://appleid.apple.com`, audience configurado para ese desafío, expiración obligatoria, subject y nonce. No confiar en un audience enviado por el cliente. Claves con caché, refresco acotado al rotar `kid` y fallo cerrado. Consumir el desafío de forma atómica e impedir replays. [Verificar usuario](https://developer.apple.com/documentation/signinwithapple/verifying-a-user), [claves Apple](https://developer.apple.com/documentation/signinwithapplerestapi/fetch-apple%27s-public-key-for-verifying-token-signature).
5. Intercambiar código en `POST https://appleid.apple.com/auth/token`; comprobar que la identidad devuelta coincide con la autorización verificada. Usar `client_id` nativo correcto y JWT `client_secret` generado del lado servidor. Conservar tokens Apple cifrados y limitados al servidor para validar y revocar. Son distintos del refresh token Firebase. [Validación Apple](https://developer.apple.com/documentation/SigninwithAppleRESTAPI/Generate-and-validate-tokens?changes=_7&language=objc).
6. Resolver UID como se especifica abajo; emitir sesión Firebase mediante custom token y `accounts:signInWithCustomToken`. Consultar `/api/validate-access` y devolver el mismo objeto de sesión consumido por el editor, en JSON por HTTPS. Refactorizar la aplicación de sesión a un método común para Apple y Google; no fabricar una URL con refresh token para entrar por el parser legado.

El JWT secreto debe tener `sub` igual al `client_id` elegido para esa operación, `iss` del equipo y `aud` de Apple, con firma ES256 y expiración controlada. Generarlo/renovarlo en servidor y tratar rotación/vencimiento como estados operativos. Nunca incorporarlo a la app.

### 3. Identidad y vinculación: conservar el UID

**El subject Apple, con su ámbito de App ID/grupo/equipo, identifica al usuario; el correo no es la clave de asociación.**

| Situación | Resultado propuesto |
| --- | --- |
| Identidad Apple ya vinculada | Recuperar su UID; conservar licencia, Cloud y Teams |
| Usuario ya autenticado que pulsa Vincular Apple | Reautenticación reciente + consentimiento explícito + prueba Apple nueva; asociar al mismo UID |
| Apple sin vínculo y correo coincidente con cuenta existente | Ofrecer ingresar a la cuenta existente y vincular; no unir automáticamente por coincidencia |
| Apple nuevo con Ocultar correo | Informar creación de cuenta nueva y ofrecer vincular si ya tiene cuenta; conservar alias relay recibido |
| Identidad Apple pertenece a otro UID | Rechazar vínculo; flujo aparte de resolución tras probar control de ambas cuentas |

Ruta preferida para investigar en implementación: Firebase Admin `getUserByProviderUid("apple.com", sub)` y `updateUser(uid, { providerToLink: { providerId: "apple.com", uid: sub, ... } })` **después** de validar la prueba Apple. Ambas APIs están presentes en el SDK local; la segunda no valida por sí misma el token Apple. Verificar comportamiento de asociación, unicidad, fallos y configuración del proveedor en un entorno de prueba antes de producción. [Firebase Admin: asociación](https://firebase.google.com/docs/reference/admin/node/firebase-admin.auth.updaterequest), [Firebase Admin: identidad de proveedor](https://firebase.google.com/docs/reference/admin/node/firebase-admin.auth.userprovider).

Si se mantiene un mapa de identidades propio en vez de `providerData`, debe tener unicidad transaccional de subject/ámbito a UID y accesos solo del servidor. Un custom claim `provider: "apple.com"` por sí solo no constituye asociación Firebase ni permite localizar usuarios por proveedor. No implementar ambas representaciones sin una fuente de identidad principal.

Conservar el correo canónico del usuario vinculado y sus estados de verificación. No marcar un correo principal distinto como verificado basándose en el alias Apple. Guardar correo Apple/nombre inicial separados; normalizar `email_verified` booleano o string mediante comparación explícita, no `Boolean("false")`. No exigir nombre nuevo en los siguientes ingresos ni borrar valores existentes cuando Apple los omita. La vinculación de datos identificables con una identidad anonimizada exige consentimiento. [Requisitos Firebase para Apple](https://firebase.google.com/docs/auth/ios/apple).

Si se elige el flujo Firebase federado de SDK/REST en lugar de custom token, habrá que habilitar y configurar `apple.com` en Firebase Authentication, incluida su configuración de código OAuth. Esa configuración es parte del flujo Firebase documentado; no sustituye la capacidad de Apple ni se ha comprobado en el proyecto. El camino de validación Apple propia + custom token no depende de agregar Firebase SDK al cliente.

### 4. Teams y correo relay

Para una cuenta Apple nueva, mostrar en Configuración → Cuenta el correo usado por PlayWorship con opción de copiarlo. Teams ya admite sintácticamente alias relay y sus memberships quedan por UID. El propietario puede agregar ese correo; no pedir al usuario revelar su correo personal. Para una cuenta vinculada a Google, conservar el correo actual mantiene el lookup existente. Un cambio futuro a invitación por código/UID sería otro cambio de producto y contrato.

Registrar en Apple los remitentes/dominios reales de `RESEND_FROM_EMAIL`. Comprobar SPF y/o DKIM y su alineación exacta con el envelope sender o el From firmado; con un proveedor externo no alcanza con registrar un dominio distinto al remitente efectivo. Si Firebase envía mensajes, registrar también su remitente o dominio personalizado. Verificar recepción real con una cuenta relay autorizada, no solo aceptación de Resend. [Relay y autenticación del correo](https://developer.apple.com/help/account/capabilities/configure-private-email-relay-service/).

A fecha de esta revisión, Apple anunció que nuevos alias Sign in with Apple usarán `private.icloud.com`; `privaterelay.appleid.com` continúa funcionando. Aceptar ambos sin deducir identidad o permisos por el dominio. La actualización de agosto de 2026 mantiene iCloud+ Hide My Email en `icloud.com`; no aplicar el anuncio anterior que fue corregido. [Actualización oficial](https://developer.apple.com/news/?id=1ptvdtcm).

### 5. Revocación y eliminación de cuenta

No se encontró endpoint de eliminación completa en la fuente auditada. Implementar inicio de eliminación dentro de la app con confirmación y reautenticación reciente, y un job idempotente que cubra:

- Revocar autorización Apple mediante `/auth/revoke` usando el access/refresh token Apple correspondiente; si falta, solicitar autorización nueva y obtenerlo mediante el código. No revocar usando el token Firebase.
- Revocar sesiones Firebase y eliminar identidad/perfil/datos asociados; definir qué información de facturación se conserva y por qué, y el tratamiento de la suscripción activa. La eliminación de cuenta no demuestra cancelación de cobro.
- Limpiar Cloud y objetos privados, índices, dispositivos, referencias de Teams del propietario/miembro y credenciales locales; coordinar descarga/reproducción en curso con los contratos actuales.
- Mantener estado de borrado recuperable ante fallos externos y distinguir revocación Apple, retirada de un proveedor y eliminación completa de PlayWorship.

Implementado y probado con el observador nativo: `credentialRevokedNotification` y `getCredentialState` al reanudar/activar sistemas Apple. Se descartan callbacks duplicados o antiguos, se conservan sesiones ante errores del sistema y la perdida confirmada de credenciales usa el logout existente. Para eliminación de Apple Account no depender únicamente de esa notificación. Añadir webhook Apple con verificación de JWS, audience y deduplicación para cambios de consentimiento, cuenta y forwarding; revocar acceso acorde al evento sin borrar automáticamente una cuenta PlayWorship vinculada a otros proveedores por una simple baja de forwarding. [Eliminación y revocación](https://developer.apple.com/documentation/technotes/tn3194-handling-account-deletions-and-revoking-tokens-for-sign-in-with-apple), [eventos Apple](https://developer.apple.com/documentation/signinwithapple/processing-changes-for-sign-in-with-apple-accounts).

### 6. Camino web para otras plataformas

Para reutilizar el transporte actual, crear `/api/auth/apple/start` y **POST** `/api/auth/apple/callback` con Services ID, nonce y state de un solo uso. Apple vuelve a un callback HTTPS registrado mediante `form_post`; no puede hacerlo directamente a `playworship://` ni a `127.0.0.1`/`localhost`. Tras validar, el servidor devuelve a la app un código de sesión efímero y ligado al desafío; la app lo canjea por POST HTTPS. Usar protección contra interceptación mediante desafío tipo PKCE y permitir únicamente rutas exactas de retorno. No trasladar refresh tokens al deep link. [Flujo en otras plataformas](https://developer.apple.com/documentation/signinwithapple/incorporating-sign-in-with-apple-into-other-platforms), [autorización Apple](https://developer.apple.com/documentation/signinwithapplerestapi/request-an-authorization-to-the-sign-in-with-apple-server.).

El state Google actual tiene fallback derivado de Firebase Web API key o constante de desarrollo. Para Apple usar un secreto real del servidor o un desafío persistido; no copiar ese fallback, la allowlist amplia de loopback ni la ausencia de consumo persistido. Google puede migrarse después al mismo canje protegido.

## Verificación y decisiones pendientes

- Pruebas backend aisladas: firma/audience/issuer/exp/nonce inválidos; replay y caducidad; rotación JWKS; código inválido; corrupción de respuesta; payload acotado; estados de cancelación; fallos recuperables; ausencia de credenciales/URLs en logs.
- Identidad: ingreso inicial/segundo sin nombre; relay en ambos dominios; asociación explícita mantiene UID; coincidencia de correo no une cuentas; conflicto entre UIDs; carreras de vinculación; no sobreescritura del correo principal; `email_verified="false"`; cuenta deshabilitada/revocada.
- Integración: licencia/dispositivos y Cloud iguales tras vincular; Teams por correo relay y cuenta existente; beneficios de pago no se conceden por un correo arbitrario; eliminación parcial/reintento; revocación y cambios de forwarding.
- UI/firma: botón Apple del sistema en bienvenida y reauth, cancelación, ventanas/orientación, reanudación, Keychain y entitlements/perfil exportados en iPhone, iPad y Mac. El simulador no reemplaza la prueba de autorización Apple en dispositivo real con cuenta de prueba habilitada para 2FA/iCloud.
- Ejecutar typecheck/build/tests relevantes del backend y builds iOS/macOS; actualizar `Auth.md` y los contratos afectados cuando cambie el comportamiento. No hace falta probar audio/transporte por redactar este plan.

Decisiones que todavía requieren ejecución o datos del titular: confirmar App ID primario/equipo y capacidad del portal; elegir en implementación asociación Firebase o mapa propio con prueba de compatibilidad; definir alcance y conservación del borrado de cuenta; designar cuentas de prueba y comprobar sender relay; decidir cuándo sumar Apple a web/Android/Windows. La recomendación inicial para esta entrega es nativo en iOS/iPadOS/macOS, con vinculación explícita disponible antes de crear nuevas cuentas de usuarios existentes.
