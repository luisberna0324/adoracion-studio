# Pipeline local complementario

Codex Cloud coordina QA principal; este pipeline aporta evidencia Apple y otras
pruebas disponibles en el Mac, conservando blockers Windows/firma/hardware.

`python3 scripts/local_pipeline.py --config .local-pipeline.json --targets all`

Ejecuta secuencialmente SemVer, recursos binarios, backend, Windows (bloqueo explícito en Mac), Android Debug,
Android Release (firma no autorizada), tests macOS, macOS Release universal y PKG,
iOS simulador y iOS device Debug con firma de desarrollo manual existente.
No instala ni lanza apps, no autentica cuentas, no exporta a App Store, no notariza,
no despacha Actions ni usa gcloud/GCS, tags, releases o manifiestos remotos.
No ejecuta bootstrap_bungee: ese script reemplaza su destino.

`--targets semver,resources` ejecuta las validaciones trasladadas desde Actions.
`--semver-branch` selecciona la rama de origen si el checkout está detached.

`--dry-run` muestra dependencias y plan sin builds ni crear reportes. `--targets`
acepta nombres separados por coma; `--jobs` limita a un máximo de tres. El proceso
continúa targets independientes: exit 0 todo PASS, 1 existe FAIL, 2 hay BLOCKED,
130 interrupción. Revisar summary.json/summary.txt y logs por target en
`dist/local-pipeline/<versión>-<SHA>/<fecha>/`. SHA identifica el commit de fuente;
app_dirty señala cambios locales. Los artefactos llevan plataforma/config/versión/SHA.
No ejecutar sobre cambios personales; preparar worktree limpio.

Configuración JSON local (ignorada por Git): `juce_dir`, `bungee_dir`, `backend`,
`android_sdk`, `java_home`, `version`, `android_version_code`, `ios_build_number`.
Para device: `ios_team`, `ios_profile`, `ios_identity` son identificadores públicos
de firma existente; no usar perfiles antiguos del preset ni provisioning automático.
`ios_development_api` puede seleccionar el preview autorizado; device es desarrollo,
no distribución. macOS Release usa el preset universal, URL productiva y Apple opt-in
apagado hasta rollout. Simulador usa el fallback de audio existente porque el framework
Bungee device no es compatible con simulador.

No guardar secretos, contraseñas, keystores o tokens en config. El proceso elimina
variables de credenciales/telemetría del entorno de builds; bloquea backend si existen
archivos dotenv privados. Sentry upload está desactivado. Backend ejecuta typecheck,
los 125 tests unitarios existentes y Next build; emuladores no se inician implícitamente.
Next standalone se copia con static/public y metadata de versión/SHA backend; se rechazan dotenv en el paquete. next-env.d.ts se preserva tras la regeneración automática de Next. No se empaqueta el repo completo.

Android trabaja offline con Gradle cache y SDK/NDK/CMake existentes, sin descargar SDK
ni aceptar licencias. Dependencias faltantes se informan; preparar caché separadamente
con la autorización pertinente. Release no lee claves y siempre informa bloqueo en
esta versión; su futuro soporte requiere autorización específica y diseño separado.
Windows necesita equipo Windows con VS2022/SDK e Inno Setup: el Mac no simula éxito.

El PKG macOS generado por el script existente no se firma/notariza para distribución.
Firma ad hoc de app y build development iOS no equivalen a release certificado.
GitHub Actions queda limitado a releases manuales después de QA externo del mismo SHA.
