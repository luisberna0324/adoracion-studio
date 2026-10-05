---
type: Integration Contract
title: Auth — Google OAuth y autorizacion Apple nativa
description: Contrato Google y puente Apple para iOS y macOS, con sesion Firebase, desafios HTTPS, vinculacion explicita y errores sin credenciales.
tags: [auth, ios, macos, apple, google, oauth, aswebauthenticationsession, firebase]
timestamp: 2026-10-05T00:00:00-03:00
---

# Auth — Google OAuth y autorizacion Apple nativa

## Retorno en Android

Android usa `NativeAuthSession::start` con `playworship://auth/callback`.
Abre el navegador externo y recibe el resultado mediante el intent VIEW de
`JuceActivity`, que llama al puente JNI `onAuthCallback`. No usa el servidor
HTTP de loopback de escritorio. La actividad es `singleTask` para recuperar
la instancia existente al volver desde el navegador. El puente valida scheme,
host y path antes de consumir el callback pendiente y lo entrega en el message
thread; el editor valida el state y lo consume una sola vez.

Si no se puede abrir el navegador se descarta el callback pendiente. Los errores
OAuth del backend se muestran como un fallo recuperable del inicio de sesion.
Los logs registran apertura, recepcion y rechazo de state sin incluir la URL ni
credenciales. Si Android mata el proceso mientras el navegador esta abierto,
se debe iniciar un nuevo intento: no se restaura el state pendiente.

Esta página es el contrato entre `juce-controls-plugin`
(`src/platform/NativeAuthSession.{h,cpp,mm}`, `PluginEditor::launchExternalLogin`)
y el backend `playworship` (`lib/googleOAuth.ts`,
`app/api/auth/google/*`). Tanto el camino iOS
(`ASWebAuthenticationSession`) como el camino macOS (callback HTTP local
en `127.0.0.1:<port>` que sirve `LocalAuthCallbackServer`) usan el
mismo endpoint backend `/api/auth/google/start` y el mismo callback
shape. Ambos evitan `firebaseapp.com` y abren el account picker de
Google en el primer frame.

## Flujo end-to-end

1. El usuario toca **Iniciar sesión** en `WelcomeModal` o en el prompt
   de reauth.
2. `PluginEditor::launchExternalLogin` genera
   `authExpectedState = juce::Uuid().toString()` y construye
   ```
   GET {authBase}/api/auth/google/start
       ?redirect_uri={redirectUri}
       &state={authExpectedState}
       &deviceId={stableDeviceId}
   ```
   En iOS, `redirectUri = "playworship://auth/callback"`. En macOS,
   `redirectUri = "http://127.0.0.1:<port>/auth/callback"` donde
   `<port>` es el puerto efímero del `LocalAuthCallbackServer`.
   `authBase` sale de `MTCONTROLS_AUTH_BASE_URL` cuando está definido;
   si está vacío usa `MTCONTROLS_API_BASE_URL`.
3. En iOS, `NativeAuthSession::start(loginUrl, "playworship", completion)`
   abre `ASWebAuthenticationSession` apuntando a esa URL. En macOS,
   `juce::URL(loginUrl).launchInDefaultBrowser()` abre Safari (o el
   browser default) en esa misma URL.
4. El backend valida `redirect_uri` contra el allowlist y normaliza los
   retornos HTTP de escritorio de `localhost` a `127.0.0.1`. Si las
   credenciales Google están configuradas, redirige 302 a
   `https://accounts.google.com/o/oauth2/v2/auth` con
   `prompt=select_account` y `scope=openid email profile`.
5. `ASWebAuthenticationSession` muestra el account picker nativo de
   Google (no el formulario de PlayWorship).
6. Google redirige a
   `{GOOGLE_OAUTH_CALLBACK_URL}/api/auth/google/callback?code=...&state=...`.
7. El backend:
   - Valida `state` contra el token HMAC autocontenido (`lib/googleOAuth.ts`).
   - Intercambia `code` por tokens vía
     `https://oauth2.googleapis.com/token`.
   - Verifica `id_token` contra `https://www.googleapis.com/oauth2/v3/certs`
     (JWKS cacheado 10 min, RS256, claim `aud` debe coincidir con
     `GOOGLE_CLIENT_ID`).
   - Resuelve o crea el usuario Firebase por email
     (`getAdminAuth().getUserByEmail` o `createUser`).
   - Minta `createCustomToken(uid)` con `provider: "google.com"` y
     `providerUid: sub`.
   - Intercambia el custom token por `idToken` + `refreshToken` vía
     `accounts:signInWithCustomToken` de Identity Toolkit.
   - Llama `/api/validate-access` con el idToken para resolver
     `plan`, `subscriptionStatus`, `allowed`, `licenseKey`, `expiresAt`,
     `graceUntil`.
   - Redirige 302 a `playworship://auth/callback` con un payload
     `session=<json>` idéntico al que produce `/login`.
8. `ASWebAuthenticationSession` cierra la hoja segura y entrega el
   callback URL al `completion` de `NativeAuthSession`.
9. El plugin valida `state == authExpectedState`, parsea
   `params["session"]` y aplica la sesión.

## Allowlist de `redirect_uri`

Definida en `lib/auth-callback.ts`, compartida por `/login` y el flujo OAuth:

- `playworship://auth/callback` (iOS y Android)
- `http://127.0.0.1:<port>/auth/callback` (desktop, port efímero)
- `http://localhost:<port>/auth/callback` (compatibilidad desktop; se convierte
  a `http://127.0.0.1:<port>/auth/callback` antes de volver a la app)

Cualquier otro protocolo u host → 400 con `{"ok":false,"error":"redirect_uri invalida"}`.

### Retorno de escritorio por IP de loopback

La app escucha y genera callbacks en `127.0.0.1`. El backend conserva el
puerto, la ruta, los parámetros y el fragmento al normalizar un retorno legado
con `localhost`. La normalización se aplica antes de firmar el state y al
construir los retornos de éxito y error, incluidos los state ya emitidos.
`/login` usa la misma función para su retorno al escritorio.

`playworship://auth/callback` conserva su scheme en iOS y Android. La URL
configurada de Google al backend (`GOOGLE_OAUTH_CALLBACK_URL`) no pasa por
esta normalización.

## Variables de entorno (backend)

- `GOOGLE_CLIENT_ID` — OAuth 2.0 client (tipo Web application) del
  proyecto GCP `play-worship`.
- `GOOGLE_CLIENT_SECRET` — secret del mismo client.
- `GOOGLE_OAUTH_CALLBACK_URL` — debe coincidir **exactamente** con un
  Authorized redirect URI del client. En iPad físico debe ser HTTPS,
  porque Google no puede devolver el callback a `127.0.0.1` del Mac desde
  Safari/iOS. En prod:
  `https://api.playworship.app/api/auth/google/callback`.

## Bases URL de la app

- `MTCONTROLS_BACKEND_MODE` selecciona el backend al configurar CMake:
  `cloud` usa `https://api.playworship.app` y es el valor predeterminado;
  `local` usa `MTCONTROLS_LOCAL_API_BASE_URL` (`http://127.0.0.1:3000`
  por defecto); `custom` requiere `MTCONTROLS_API_BASE_URL`.
- Para desarrollo en un iPad físico con modo `local`, definir
  `MTCONTROLS_LOCAL_API_BASE_URL` con la IP LAN del Mac, no `localhost`.
  Ejemplo: `-DMTCONTROLS_BACKEND_MODE=local
  -DMTCONTROLS_LOCAL_API_BASE_URL=http://192.168.1.20:3000`.
- `MTCONTROLS_API_BASE_URL` queda reservado para integraciones especiales
  cuando se usa `MTCONTROLS_BACKEND_MODE=custom`.
- `MTCONTROLS_AUTH_BASE_URL` define donde inicia el OAuth
  `/api/auth/google/start`. Si está vacío, usa el backend resuelto por
  `MTCONTROLS_BACKEND_MODE`.
  Para iPad con API local se debe apuntar a un backend HTTPS desplegado,
  por ejemplo `https://api.playworship.app`, y ese backend
  debe tener `GOOGLE_OAUTH_CALLBACK_URL` HTTPS configurado.

Si falta alguna, `/api/auth/google/start` redirige con
`error=GoogleOAuthNotConfigured` en lugar de abrir Google.

## iOS — `src/platform/NativeAuthSession`

- `start(loginUrl, callbackScheme, completion)` retorna `false` si la
  URL o el scheme son inválidos o si `[activeSession start]` falla.
- El `completion` se despacha vía `juce::MessageManager::callAsync`
  para ejecutarse en el message thread.
- `presentationContextProvider` resuelve la `UIWindow` activa de la
  `UIWindowScene` foreground. Si no hay ninguna, devuelve la primera
  ventana disponible.
- `prefersEphemeralWebBrowserSession = NO` para reusar la sesión de
  Safari si Google lo permite.
- `cancel()` aborta la sesión activa y libera los punteros.

## Errores

El backend nunca responde JSON de error: cualquier fallo se traduce a
un 302 al `redirect_uri` de la app (normalizado a `127.0.0.1` en escritorio)
con:

- `error=<codigo>` — uno de:
  - `GoogleOAuthNotConfigured` (falta env)
  - `GoogleAuth_<google_error>` (p.ej. `access_denied`)
  - `GoogleTokenExchangeFailed`
  - `GoogleTokenVerificationFailed`
  - `FirebaseUserProvisioningFailed`
  - `FirebaseCustomTokenFailed`
  - `FirebaseSessionExchangeFailed`
- `message=<detalle>`
- `state=<authExpectedState original>` (para que el plugin no rechace
  la respuesta por state mismatch)

El plugin valida primero el state y, si el callback contiene `error`, muestra
"No se pudo completar el inicio de sesion con Google. Reintenta." sin aplicar
una sesion parcial. Un state incorrecto muestra "Autenticacion invalida.
Reintenta.". El usuario puede volver a tocar **Iniciar sesión**.

## Lo que NO hace este flujo

- No usa el SDK de Firebase en el cliente. La app nunca ve
  `firebaseapp.com`.
- No usa `signInWithRedirect` ni `signInWithPopup`. El account picker
  de Google se invoca vía OAuth 2.0 estándar.
- No emite tokens de Drive. La integración con Drive (`enable_drive`)
  sigue siendo opt-in y se cablea en una iteración aparte.
- El state Google actual es un token HMAC autocontenido con TTL, no un store
  en memoria. El flujo Apple usa un desafio persistido y consumido en servidor;
  no reutiliza los fallbacks de secreto del state Google.

## Apple nativo — cliente preparado, activacion condicionada

`src/platform/NativeAppleSignIn.{h,cpp,mm}` separa autorizacion Apple de la
sesion PlayWorship. `src/data/AppleSignInApi.{h,cpp}` transporta y valida el
contrato JSON. Estos componentes no asignan UID, licencias, planes, Teams ni
Cloud; tampoco aplican o guardan una sesion. Su inclusion en la UI requiere
que el build tenga los entitlements Apple correspondientes y que el backend
informe configuracion habilitada. Fuente local y pruebas de contrato no
demuestran configuracion del portal ni autorizacion Apple real.

- `GET /api/auth/apple/config` devuelve `enabled` booleano y `nativeClientId`.
  Un error, valor no booleano, audiencia ausente o proveedor deshabilitado no
  habilita la opcion. `NativeAppleSignIn::isSupported()` indica solo soporte
  del OS; no acredita capacidad del portal, firma o claves de servidor.
- `createButton(callback, darkBackground)` devuelve un `UIViewComponent` o
  `NSViewComponent` con `ASAuthorizationAppleIDButton` de tipo Continue real.
  Usa estilo White sobre fondo oscuro y Black sobre fondo claro; la UI elige
  el estilo del tema. Crear el componente no inicia autenticacion.
- `POST /api/auth/apple/challenge` envia `{purpose, platform, deviceId}`, donde
  purpose es `signin` o `link` y platform es `ios` o `macos`. Devuelve
  `{ok:true, challengeId, state, nonce, expiresAtMs}`. `nonce` es el valor raw
  generado criptograficamente en servidor; state y nonce usan base64url sin
  padding. `expiresAtMs` es un entero JSON en milisegundos Unix, nunca una
  fecha ISO ni un string. El cliente rechaza valores vencidos y un plazo
  restante superior a diez minutos.
- `Challenge::nativeRequest()` calcula SHA256 UTF-8 del nonce raw como 64
  caracteres hexadecimales minusculos. `ASAuthorizationAppleIDRequest` recibe
  ese hash y el state. Solicita email; FullName es opt-in. Los datos de perfil
  pueden faltar en autorizaciones posteriores y no reemplazan el correo
  canonico de la cuenta existente. FullName se formatea con el formatter del
  sistema y se limita a 200 caracteres.
- `start(Request, Component&, Completion)` exige message/main thread y una
  ventana del componente: UIKit en escena foreground activa o AppKit visible.
  Rechaza inicios concurrentes e inputs invalidos. El delegate comprueba state,
  vencimiento y presencia de identityToken/authorizationCode antes de entregar
  un exito. Una cancelacion tiene Outcome::cancelled y no devuelve credenciales.
  attemptId descarta callbacks de intentos anteriores y el completion se
  despacha una sola vez al message thread. `cancel()` desvincula el delegate y
  entrega cancelacion; tambien cierra la hoja nativa desde iOS 16/macOS 13.
  En macOS anteriores no hay ese metodo de cierre, pero el resultado pendiente
  se descarta igualmente.
- `POST /api/auth/apple/native` envia
  `{challengeId,state,identityToken,authorizationCode,platform,deviceId}` y
  opcionalmente `profile:{email,fullName}` por JSON HTTPS. No envia nonce raw,
  UID de Firebase, purpose nuevo ni permisos. AppleUser del credential tampoco
  es una identidad Firebase; el servidor verifica el subject del JWT firmado.
  Antes de enviar se repiten las comprobaciones de state y vencimiento.
- En link ambas llamadas incluyen `Authorization: Bearer <Firebase ID token>`.
  El servidor debe comprobar token vigente y autenticacion reciente, ligar el
  desafio al UID autenticado y consumirlo atomicamente. `expectedUid` es una
  defensa adicional local para rechazar una respuesta de otra cuenta, no una
  identidad enviada o confiable para el servidor. No se asocian cuentas por
  coincidencia de correo ni por datos de profile.
- La respuesta es `{ok:true,session:{...}}` con el contrato Firebase existente:
  uid, email canonico, plan, subscriptionStatus, allowed booleano, refreshToken,
  firebaseApiKey, idTokenExpiresAtMs entero y campos de acceso opcionales.
  El helper devuelve `PersistedSessionSnapshot` completo o un error tipado;
  conserva `allowed=false` y no deduce nuevos grants. No construye una URL de
  callback con tokens para reutilizar el parser Google. El editor aplica la
  sesion mediante el contrato comun y mantiene las validaciones de acceso.
- El transporte exige un origen HTTPS sin userinfo, path, query ni fragment.
  No sigue redirects y lee como maximo 64 KiB. Ningun payload, URL de respuesta,
  token, state, nonce o descripcion de NSError se registra o muestra. Los
  errores del backend se traducen a enums de una whitelist; codigos desconocidos
  quedan como backendRejected. La UI usa mensajes propios y puede pedir nuevo
  desafio para vencimiento/replay o reautenticacion para vincular.

El editor debe cancelar el puente al destruirse e invalidar su generacion de
intento antes de aplicar respuestas de red demoradas: cancelar la hoja no
cancela un intercambio HTTP ya iniciado. Las claves Apple y tokens de
revocacion permanecen en servidor. El opt-in de entitlements agrega
`com.apple.developer.applesignin = [Default]` al plist de entitlements generado
por JUCE, preservando Sandbox y permisos existentes; no lo agrega a Info.plist.
El portal y perfil de firma deben habilitar esa capacidad por separado.

`tests/apple_signin_contract_tests.cpp` verifica hash, vencimiento, state,
representacion de POST/bearer, binding local de dispositivo/proposito, origen
HTTPS, rechazo de inyeccion de headers, conservacion de UID/denegacion y
configuracion deshabilitada. El 2026-10-05 compilo y paso aislado en macOS;
el puente Objective-C++ paso syntax-only para macOS e iOS 18.5. Estas pruebas
no ejercen autorizacion Apple, cancelacion del OS, vinculacion en Firebase ni
un backend desplegado. Ver [plan Apple](../distribution/SignInWithApplePlan.md).

## Layout de bienvenida

`WelcomeModal` limita el ancho del formulario y centra verticalmente sus
controles. Usa dos columnas solo cuando dispone de al menos `760 px` de ancho,
`360 px` de alto y una relacion horizontal suficiente; en el resto de tamaños
organiza logo, textos y acciones en una sola columna centrada. El formulario no
supera `420 px` en dos columnas ni `440 px` en una columna, evitando botones
excesivamente anchos en ventanas grandes.

La aplicacion standalone de escritorio abre su ventana principal en pantalla
completa antes de hacerla visible. De esta forma, la pantalla de bienvenida usa
desde el primer frame toda la resolucion disponible.

## Configuracion en pantallas reducidas

El contenedor de configuracion usa desplazamiento vertical para sus cinco
secciones (General, Sesion, Audio, Ruteo y Diagnostico). En General, Sesion,
Audio y Diagnostico reserva altura adicional cuando el dispositivo es tactil
o la ventana tiene menos de `820 px` de alto. Ruteo siempre calcula su altura
segun todas las filas y el pie, incluso en ventanas grandes. El desplazamiento se hace sobre el
contenido de la pestaña y conserva fijas la cabecera, el cierre y la navegacion.
Ruteo comparte ese viewport principal para que rueda/trackpad y arrastre con
clic o toque sostenido no compitan con un contenedor anidado.
Sus selectores propagan rueda/trackpad al viewport y abren solo al completar
un clic/tap, para que desplazar la lista no altere una salida. Ver
[Mobile Layout](../ui/MobileLayout.md).
