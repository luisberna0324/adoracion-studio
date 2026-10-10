# Validación local y CI manual

Los pushes de ramas y tags no construyen instaladores. En PR se conservan
`binary-resources` (Linux, sin JUCE) y la comprobación SemVer de ramas hacia
main. Fusionar no crea tags ni despacha workflows. Las validaciones obsoletas de un
PR se cancelan por workflow y número de PR; los dispatch manuales tienen grupo
por run y no se cancelan. Build/publicación y Cloud Build conservan
`cancel-in-progress: false` en sus grupos separados.

## Antes de una release

1. Registrar SHA de app y backend. Ejecutar en el backend `npm ci`,
   `npm run typecheck` y los cinco scripts `test:auth-callback`,
   `test:secret-cache`, `test:paddle`, `test:teams`, `test:password-auth`.
   `node --import tsx --test tests/*.test.ts` ejecuta todas esas pruebas.
   Ejecutar también `npm run build` sin token de subida Sentry.
2. Ejecutar los recursos sin SDK:

   ```sh
   cmake -S tests/binary_resources -B build/binary-resources -G Ninja
   cmake --build build/binary-resources --parallel 3
   ctest --test-dir build/binary-resources --output-on-failure --no-tests=error
   ```

3. Con JUCE fijado al SHA del workflow, Bungee y dependencias de plataforma,
   ejecutar la suite completa descrita en [Testing](../Testing.md), compilar
   `MTControls_Standalone` y comprobar la app en dispositivos reales.
   `tests/run_isolated_test.py` conserva el aislamiento de datos.
   Alternativamente ejecutar manualmente `tests.yml`: macOS CTest y Windows
   x64/x86. El resultado de recursos no acredita la compilación completa JUCE.
4. Para construir instaladores manualmente usar `ci.yml` con `ref` y
   `publish_release=false` (predeterminado). No sube símbolos Sentry ni
   instaladores a GCS, no limpia versiones, no cambia manifiestos ni crea
   tags/releases. La matriz completa sigue siendo costosa y Android requiere
   los secretos de firma ya previstos; las pruebas locales no los necesitan.
5. Solo tras solicitar publicación ejecutar `ci.yml` con
   `publish_release=true`. Este permiso incluye uploads Sentry/GCS, retención
   de las últimas tres versiones, manifiesto de descargas y GitHub Release.
   La creación del tag no dispara una segunda compilación.

`ref` acepta rama, SHA o tag estable existente. El resolver fija un SHA antes
de compilar; un tag explícito conserva su versión. Para rama/SHA se calcula el
incremento `release_kind` usando únicamente tags estables. Las ramas se
resuelven desde el origen aunque el dispatch use otra rama; el SHA fijado
identifica también la release Sentry. Una colisión de tag calculado falla
en lugar de sustituir silenciosamente la revisión solicitada. No usar una build sin publicación como evidencia
para otra revisión del código.

## Android Cloud Build

`android-cloudbuild.yml` sigue siendo exclusivamente manual. Requiere además
`publish_artifacts=true`: Cloud Build necesariamente sube fuentes y produce
APK en GCS, y el workflow sube un artefacto GitHub de depuración. Sin esa
opción el job no se ejecuta. Este camino consume recursos GCP y no publica
una release firmada. Usar el build Android local si se desea evitar cualquier
subida externa; requiere SDK/NDK/Java y `scripts/bootstrap_bungee.sh`.

## Límites del entorno Linux guardado

La auditoría del 10/10/2026 validó recursos C++ y backend. No había JUCE,
Bungee ni paquetes de desarrollo ALSA/X11/FreeType/OpenGL, por lo que no se
acredita la app Linux ni `PlayWorshipTests`. Los presets macOS/iOS necesitan
Xcode/SDK Apple; Windows necesita Visual Studio 2022 y su SDK. Firma,
instaladores, audio/MIDI físico, foco de UI y descubrimiento LAN deben
verificarse en sus plataformas. `scripts/e2e-local.mjs` requiere servidor
Next y emuladores Firebase Auth/Firestore; los tests unitarios no lo sustituyen.
