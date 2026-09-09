---
type: Integration Contract
title: Auth — Google OAuth en iOS vía ASWebAuthenticationSession
description: Contrato del login nativo en iOS. La app abre la URL del backend,
  que redirige a Google y vuelve con un idToken de Firebase. Define endpoints,
  allowlist de redirect_uri, scopes, errores y contrato del payload de
  callback.
tags: [auth, ios, google, oauth, aswebauthenticationsession, firebase]
timestamp: 2026-09-05T14:55:00Z
---

# Auth — Google OAuth nativo en iOS y macOS

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
4. El backend valida `redirect_uri` contra el allowlist y, si las
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

Definida en `isAllowedRedirectUri` (idéntica a la de `app/login/page.tsx`):

- `playworship://auth/callback` (iOS)
- `http://127.0.0.1:<port>/auth/callback` (desktop, port efímero)
- `http://localhost:<port>/auth/callback` (desktop)

Cualquier otro protocolo u host → 400 con `{"ok":false,"error":"redirect_uri invalida"}`.

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
un 302 al `redirect_uri` original con:

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

El plugin trata `error` como un callback URL con sesión ausente y
muestra "Autenticacion invalida. Reintenta." (mismo mensaje que ya
usa para state mismatch). Mantener este comportamiento evita agregar
parsing específico para `error`/`message` y simplifica la rama de
recuperación: el usuario vuelve a tocar **Iniciar sesión**.

## Lo que NO hace este flujo

- No usa el SDK de Firebase en el cliente. La app nunca ve
  `firebaseapp.com`.
- No usa `signInWithRedirect` ni `signInWithPopup`. El account picker
  de Google se invoca vía OAuth 2.0 estándar.
- No emite tokens de Drive. La integración con Drive (`enable_drive`)
  sigue siendo opt-in y se cablea en una iteración aparte.
- No persiste el `state` entre reinicios del backend. El store vive en
  memoria con TTL de 10 min (`lib/googleOAuth.ts::STATE_TTL_MS`).
  Producción debe mover esto a Redis o a una cookie firmada con
  `AUTH_STATE_SECRET`.

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

El contenedor de configuracion usa desplazamiento vertical para sus cuatro
secciones (General, Sesion, Audio y Ruteo) cuando el dispositivo es tactil o la
ventana tiene menos de `760 px` de alto. El desplazamiento se hace sobre el
contenido de la pestaña y conserva fijas la cabecera, el cierre y la navegacion.
